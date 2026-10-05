#include "audio_decode.h"

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *ext_of(const char *path) {
    const char *dot = strrchr(path, '.');
    return dot ? dot + 1 : "";
}

static bool str_ieq(const char *a, const char *b) {
#if defined(_WIN32)
    return _stricmp(a, b) == 0;
#else
    return strcasecmp(a, b) == 0;
#endif
}

/* Very small linear resampler mono float -> 16kHz mono float. Good enough for
 * speech; whisper.cpp itself is tolerant of minor resampling artifacts. */
static float *resample_to_16k(const float *in, size_t in_count, uint32_t in_rate,
                               size_t *out_count) {
    if (in_rate == 16000) {
        float *out = (float *)malloc(in_count * sizeof(float));
        memcpy(out, in, in_count * sizeof(float));
        *out_count = in_count;
        return out;
    }
    double ratio = 16000.0 / (double)in_rate;
    size_t n_out = (size_t)((double)in_count * ratio);
    float *out = (float *)malloc(n_out * sizeof(float));
    for (size_t i = 0; i < n_out; i++) {
        double src_pos = (double)i / ratio;
        size_t i0 = (size_t)src_pos;
        size_t i1 = i0 + 1 < in_count ? i0 + 1 : i0;
        double frac = src_pos - (double)i0;
        out[i] = (float)((1.0 - frac) * in[i0] + frac * in[i1]);
    }
    *out_count = n_out;
    return out;
}

static float *downmix_to_mono(const float *interleaved, uint64_t frames, uint32_t channels) {
    float *mono = (float *)malloc((size_t)frames * sizeof(float));
    for (uint64_t i = 0; i < frames; i++) {
        double sum = 0.0;
        for (uint32_t c = 0; c < channels; c++)
            sum += interleaved[i * channels + c];
        mono[i] = (float)(sum / channels);
    }
    return mono;
}

/* Extract audio via a bundled/system ffmpeg into a temp 16kHz mono wav file. */
static bool ffmpeg_extract_to_wav(const char *in_path, char *out_wav, size_t out_wav_len,
                                   char *err, size_t err_len) {
#if defined(_WIN32)
    snprintf(out_wav, out_wav_len, "%s\\transcribe_tmp_%p.wav", getenv("TEMP"), (void *)in_path);
#else
    snprintf(out_wav, out_wav_len, "/tmp/transcribe_tmp_%p.wav", (void *)in_path);
#endif
    char cmd[4096];
    /* Looks for a bundled ffmpeg next to the executable first, falls back to PATH. */
    snprintf(cmd, sizeof(cmd),
             "\"%s\" -y -i \"%s\" -ac 1 -ar 16000 -vn -f wav \"%s\" >%s 2>&1",
             "ffmpeg", in_path, out_wav,
#if defined(_WIN32)
             "NUL"
#else
             "/dev/null"
#endif
    );
    int rc = system(cmd);
    if (rc != 0) {
        snprintf(err, err_len, "ffmpeg failed to decode '%s' (is ffmpeg bundled/installed?)", in_path);
        return false;
    }
    return true;
}

bool audio_decode_to_mono16k(const char *path, float **out_samples, size_t *out_count,
                              char *err, size_t err_len) {
    const char *ext = ext_of(path);

    if (str_ieq(ext, "wav")) {
        unsigned int channels, sample_rate;
        drwav_uint64 frames;
        float *data = drwav_open_file_and_read_pcm_frames_f32(path, &channels, &sample_rate, &frames, NULL);
        if (!data) { snprintf(err, err_len, "Could not read WAV file '%s'", path); return false; }
        float *mono = channels > 1 ? downmix_to_mono(data, frames, channels) : NULL;
        const float *src = mono ? mono : data;
        size_t rc;
        float *resampled = resample_to_16k(src, (size_t)frames, sample_rate, &rc);
        drwav_free(data, NULL);
        if (mono) free(mono);
        *out_samples = resampled;
        *out_count = rc;
        return true;
    }

    if (str_ieq(ext, "mp3")) {
        drmp3_config cfg;
        drmp3_uint64 frames;
        float *data = drmp3_open_file_and_read_pcm_frames_f32(path, &cfg, &frames, NULL);
        if (!data) { snprintf(err, err_len, "Could not read MP3 file '%s'", path); return false; }
        float *mono = cfg.channels > 1 ? downmix_to_mono(data, frames, cfg.channels) : NULL;
        const float *src = mono ? mono : data;
        size_t rc;
        float *resampled = resample_to_16k(src, (size_t)frames, cfg.sampleRate, &rc);
        drmp3_free(data, NULL);
        if (mono) free(mono);
        *out_samples = resampled;
        *out_count = rc;
        return true;
    }

    if (str_ieq(ext, "flac")) {
        unsigned int channels, sample_rate;
        drflac_uint64 frames;
        float *data = drflac_open_file_and_read_pcm_frames_f32(path, &channels, &sample_rate, &frames, NULL);
        if (!data) { snprintf(err, err_len, "Could not read FLAC file '%s'", path); return false; }
        float *mono = channels > 1 ? downmix_to_mono(data, frames, channels) : NULL;
        const float *src = mono ? mono : data;
        size_t rc;
        float *resampled = resample_to_16k(src, (size_t)frames, sample_rate, &rc);
        drflac_free(data, NULL);
        if (mono) free(mono);
        *out_samples = resampled;
        *out_count = rc;
        return true;
    }

    /* Anything else (mp4, mov, mkv, m4a, aac, ogg, webm...) goes through ffmpeg,
     * which is bundled alongside the executable for a fully self-contained app. */
    char tmp_wav[1024];
    if (!ffmpeg_extract_to_wav(path, tmp_wav, sizeof(tmp_wav), err, err_len))
        return false;

    bool ok = audio_decode_to_mono16k(tmp_wav, out_samples, out_count, err, err_len);
    remove(tmp_wav);
    return ok;
}
