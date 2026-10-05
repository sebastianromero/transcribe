/* Headless smoke test: exercises the exact same engine.c pipeline as the
 * GUI (decode -> diarize -> whisper) without needing a display, so we can
 * verify end-to-end correctness in CI / sandboxed environments. */
#include "session.h"
#include "engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <audio_file> [language] [expected_speakers]\n", argv[0]);
        return 1;
    }

    Session s;
    session_init(&s);
    session_reset_for_new_file(&s, argv[1]);
    if (argc > 2) {
        strncpy(s.language, argv[2], sizeof(s.language) - 1);
        s.language[sizeof(s.language) - 1] = 0;
    }
    if (argc > 3) {
        s.expected_speakers = atoi(argv[3]);
    }

    EngineConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.whisper_model_path, sizeof(cfg.whisper_model_path), "models/ggml-small.bin");
    snprintf(cfg.diarization_seg_model_path, sizeof(cfg.diarization_seg_model_path), "models/diarization/segmentation.onnx");
    snprintf(cfg.diarization_embed_model_path, sizeof(cfg.diarization_embed_model_path), "models/diarization/embedding.onnx");
    cfg.num_threads = 4;
    cfg.enable_diarization = 1;

    engine_transcribe_file_async(&cfg, &s);

    EngineStatus st;
    do {
        usleep(200000);
        session_lock(&s);
        st = s.status;
        float progress = s.progress;
        char detected[8];
        strncpy(detected, s.detected_language, sizeof(detected));
        session_unlock(&s);
        fprintf(stderr, "[status=%d progress=%.2f]\n", st, progress);
        if (st == ENGINE_AWAITING_LANGUAGE_CONFIRM) {
            fprintf(stderr, "[auto-confirming detected language: %s]\n", detected);
            session_confirm_language(&s, detected[0] ? detected : "en");
        }
    } while (st != ENGINE_DONE && st != ENGINE_ERROR);

    session_lock(&s);
    if (st == ENGINE_ERROR) {
        fprintf(stderr, "ERROR: %s\n", s.error_message);
        session_unlock(&s);
        return 1;
    }
    for (size_t i = 0; i < s.segment_count; i++) {
        Segment *seg = &s.segments[i];
        printf("[%6.2f - %6.2f] %s: %s\n", seg->start_sec, seg->end_sec,
               session_speaker_name(&s, seg->speaker_id), seg->text);
    }
    session_unlock(&s);

    session_destroy(&s);
    return 0;
}
