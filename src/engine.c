#include "engine.h"
#include "audio_decode.h"

#include "whisper.h"

#ifdef TRANSCRIBE_WITH_DIARIZATION
#include "sherpa-onnx/c-api/c-api.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
typedef HANDLE thread_t;
static thread_t thread_start(DWORD (WINAPI *fn)(LPVOID), void *arg) {
    return CreateThread(NULL, 0, fn, arg, 0, NULL);
}
#define THREAD_FN_RET DWORD WINAPI
#define THREAD_FN_END return 0;
#else
#include <pthread.h>
typedef pthread_t thread_t;
static thread_t thread_start(void *(*fn)(void *), void *arg) {
    pthread_t t;
    pthread_create(&t, NULL, fn, arg);
    pthread_detach(t);
    return t;
}
#define THREAD_FN_RET void *
#define THREAD_FN_END return NULL;
#include <unistd.h>
#endif

typedef struct {
    EngineConfig cfg;
    Session *session;
    char audio_path[1024];
} Job;

/* --------------------------------------------------------------------- */
/* Diarization: run sherpa-onnx offline speaker diarization over the full
 * clip, producing (start, end, cluster_id) turns sorted by start time.    */
/* --------------------------------------------------------------------- */

typedef struct {
    double start;
    double end;
    int cluster;
} DiaTurn;

typedef struct {
    DiaTurn *turns;
    int count;
} DiaResult;

#ifdef TRANSCRIBE_WITH_DIARIZATION
static bool run_diarization(const EngineConfig *cfg, const float *samples, size_t n,
                             int expected_speakers, DiaResult *out, char *err, size_t err_len) {
    if (!cfg->enable_diarization || cfg->diarization_seg_model_path[0] == 0) {
        out->turns = NULL;
        out->count = 0;
        return true;
    }

    SherpaOnnxOfflineSpeakerDiarizationConfig config;
    memset(&config, 0, sizeof(config));
    config.segmentation.pyannote.model = cfg->diarization_seg_model_path;
    config.segmentation.num_threads = cfg->num_threads;
    config.embedding.model = cfg->diarization_embed_model_path;
    config.embedding.num_threads = cfg->num_threads;
    /* If the user told us how many people are speaking, honor it exactly
     * (by far the most reliable way to avoid over-segmentation). Otherwise
     * fall back to auto-clustering with a higher-than-default similarity
     * threshold: sherpa-onnx's default (0.5) tends to badly over-split a
     * clip into many spurious speakers, especially on longer recordings. */
    config.clustering.num_clusters = expected_speakers > 0 ? expected_speakers : -1;
    config.clustering.threshold = 0.75f;
    config.min_duration_on = 0.3f;
    config.min_duration_off = 0.5f;

    const SherpaOnnxOfflineSpeakerDiarization *sd = SherpaOnnxCreateOfflineSpeakerDiarization(&config);
    if (!sd) {
        snprintf(err, err_len, "Failed to load diarization models");
        return false;
    }

    const SherpaOnnxOfflineSpeakerDiarizationResult *result =
        SherpaOnnxOfflineSpeakerDiarizationProcess(sd, samples, (int32_t)n);

    if (!result) {
        SherpaOnnxDestroyOfflineSpeakerDiarization(sd);
        snprintf(err, err_len, "Diarization failed to process audio");
        return false;
    }

    int32_t nseg = SherpaOnnxOfflineSpeakerDiarizationResultGetNumSegments(result);
    const SherpaOnnxOfflineSpeakerDiarizationSegment *segs =
        SherpaOnnxOfflineSpeakerDiarizationResultSortByStartTime(result);

    out->turns = (DiaTurn *)malloc(sizeof(DiaTurn) * (nseg > 0 ? nseg : 1));
    out->count = nseg;
    for (int i = 0; i < nseg; i++) {
        out->turns[i].start = segs[i].start;
        out->turns[i].end = segs[i].end;
        out->turns[i].cluster = segs[i].speaker;
    }

    SherpaOnnxOfflineSpeakerDiarizationDestroySegment(segs);
    SherpaOnnxOfflineSpeakerDiarizationDestroyResult(result);
    SherpaOnnxDestroyOfflineSpeakerDiarization(sd);
    return true;
}
#else
static bool run_diarization(const EngineConfig *cfg, const float *samples, size_t n,
                             int expected_speakers, DiaResult *out, char *err, size_t err_len) {
    (void)cfg; (void)samples; (void)n; (void)expected_speakers; (void)err; (void)err_len;
    out->turns = NULL;
    out->count = 0;
    return true;
}
#endif

static int cluster_for_time(const DiaResult *dia, double t0, double t1) {
    if (dia->count == 0) return -1;
    int best = -1;
    double best_overlap = 0.0;
    for (int i = 0; i < dia->count; i++) {
        double ov_start = t0 > dia->turns[i].start ? t0 : dia->turns[i].start;
        double ov_end = t1 < dia->turns[i].end ? t1 : dia->turns[i].end;
        double overlap = ov_end - ov_start;
        if (overlap > best_overlap) {
            best_overlap = overlap;
            best = dia->turns[i].cluster;
        }
    }
    return best;
}

/* --------------------------------------------------------------------- */
/* Whisper transcription, streaming segments to the session as they land. */
/* --------------------------------------------------------------------- */

typedef struct {
    Session *session;
    DiaResult *dia;
} SegCbCtx;

static void on_new_segment(struct whisper_context *ctx, struct whisper_state *state,
                            int n_new, void *user_data) {
    (void)n_new;
    SegCbCtx *cb = (SegCbCtx *)user_data;
    int n_segments = whisper_full_n_segments_from_state(state);
    /* Only emit the most recently finalized segment(s). */
    for (int i = n_segments - n_new; i < n_segments; i++) {
        if (i < 0) continue;
        int64_t t0 = whisper_full_get_segment_t0_from_state(state, i);
        int64_t t1 = whisper_full_get_segment_t1_from_state(state, i);
        const char *text = whisper_full_get_segment_text_from_state(state, i);

        Segment seg;
        memset(&seg, 0, sizeof(seg));
        seg.start_sec = t0 / 100.0;
        seg.end_sec = t1 / 100.0;
        strncpy(seg.text, text, sizeof(seg.text) - 1);

        int cluster = cluster_for_time(cb->dia, seg.start_sec, seg.end_sec);
        seg.speaker_id = session_get_or_create_speaker_ts(cb->session, cluster);

        session_add_segment_ts(cb->session, &seg);
    }
    (void)ctx;
}

static THREAD_FN_RET job_run(void *arg) {
    Job *job = (Job *)arg;
    Session *s = job->session;
    EngineConfig *cfg = &job->cfg;
    char err[512];

    session_set_status_ts(s, ENGINE_DECODING_AUDIO, 0.05f);
    float *samples = NULL;
    size_t n_samples = 0;
    if (!audio_decode_to_mono16k(job->audio_path, &samples, &n_samples, err, sizeof(err))) {
        session_set_error_ts(s, err);
        free(job);
        THREAD_FN_END
    }

    session_lock(s);
    int expected_speakers = s->expected_speakers;
    session_unlock(s);

    DiaResult dia = {0};
    if (cfg->enable_diarization) {
        session_set_status_ts(s, ENGINE_DIARIZING, 0.15f);
        if (!run_diarization(cfg, samples, n_samples, expected_speakers, &dia, err, sizeof(err))) {
            /* Diarization is best-effort: fall back to a single unnamed speaker. */
            fprintf(stderr, "[transcribe] diarization warning: %s\n", err);
            dia.turns = NULL;
            dia.count = 0;
        }
    }

    session_set_status_ts(s, ENGINE_LOADING_MODELS, 0.3f);
    struct whisper_context_params cparams = whisper_context_default_params();
    struct whisper_context *ctx = whisper_init_from_file_with_params(cfg->whisper_model_path, cparams);
    if (!ctx) {
        snprintf(err, sizeof(err), "Failed to load whisper model '%s'", cfg->whisper_model_path);
        session_set_error_ts(s, err);
        free(samples);
        free(dia.turns);
        free(job);
        THREAD_FN_END
    }

    bool language_was_forced = s->language[0] && strcmp(s->language, "auto") != 0;
    char confirmed_language[8];

    if (language_was_forced) {
        /* User already picked a language before dropping the file (or a
         * previous run set it): skip detection and go straight to it. */
        strncpy(confirmed_language, s->language, sizeof(confirmed_language) - 1);
        confirmed_language[sizeof(confirmed_language) - 1] = 0;
    } else {
        session_set_status_ts(s, ENGINE_DETECTING_LANGUAGE, 0.32f);

        struct whisper_full_params detect_params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        detect_params.n_threads = cfg->num_threads > 0 ? cfg->num_threads : 4;
        detect_params.language = NULL;
        detect_params.detect_language = true; /* here we WANT it to stop after detecting */
        whisper_full(ctx, detect_params, samples, (int)n_samples);

        int lang_id = whisper_full_lang_id(ctx);
        const char *lang_str = lang_id >= 0 ? whisper_lang_str(lang_id) : "en";
        session_set_detected_language_ts(s, lang_str);

        /* Pause here and let the UI show the detected language so the user
         * can confirm or correct it before we spend time transcribing. */
        session_set_status_ts(s, ENGINE_AWAITING_LANGUAGE_CONFIRM, 0.35f);
        for (;;) {
            session_lock(s);
            bool confirmed = s->language_confirmed;
            bool cancelled = s->cancel_requested;
            if (confirmed) {
                strncpy(confirmed_language, s->language, sizeof(confirmed_language) - 1);
                confirmed_language[sizeof(confirmed_language) - 1] = 0;
            }
            session_unlock(s);
            if (cancelled) {
                whisper_free(ctx);
                free(samples);
                free(dia.turns);
                free(job);
                THREAD_FN_END
            }
            if (confirmed) break;
#if defined(_WIN32)
            Sleep(150);
#else
            usleep(150000);
#endif
        }
    }

    struct whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.print_progress = false;
    wparams.print_realtime = false;
    wparams.print_timestamps = false;
    wparams.translate = false;
    wparams.n_threads = cfg->num_threads > 0 ? cfg->num_threads : 4;
    /* Always pass an explicit language here: detect_language must stay false,
     * since that flag makes whisper_full() stop right after detecting the
     * language and return zero segments (see the dedicated pass above). */
    wparams.language = confirmed_language;

    SegCbCtx cb_ctx = { .session = s, .dia = &dia };
    wparams.new_segment_callback = on_new_segment;
    wparams.new_segment_callback_user_data = &cb_ctx;

    session_set_status_ts(s, ENGINE_TRANSCRIBING, 0.4f);
    int rc = whisper_full(ctx, wparams, samples, (int)n_samples);

    whisper_free(ctx);
    free(samples);
    free(dia.turns);

    if (rc != 0) {
        session_set_error_ts(s, "Whisper transcription failed");
    } else {
        session_set_status_ts(s, ENGINE_DONE, 1.0f);
    }

    free(job);
    THREAD_FN_END
}

bool engine_transcribe_file_async(const EngineConfig *cfg, Session *session) {
    Job *job = (Job *)calloc(1, sizeof(Job));
    job->cfg = *cfg;
    job->session = session;
    strncpy(job->audio_path, session->current_file, sizeof(job->audio_path) - 1);

    session_lock(session);
    session->cancel_requested = false;
    session_unlock(session);

#if defined(_WIN32)
    thread_start((DWORD (WINAPI *)(LPVOID))job_run, job);
#else
    thread_start((void *(*)(void *))job_run, job);
#endif
    return true;
}

void engine_cancel(Session *session) {
    session_lock(session);
    session->cancel_requested = true;
    session_unlock(session);
}
