#ifndef TRANSCRIBE_ENGINE_H
#define TRANSCRIBE_ENGINE_H

#include "session.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char whisper_model_path[1024];   /* ggml *.bin model */
    char diarization_seg_model_path[1024]; /* sherpa-onnx segmentation (pyannote) model, may be empty */
    char diarization_embed_model_path[1024]; /* sherpa-onnx speaker-embedding model, may be empty */
    int num_threads;
    bool enable_diarization;
} EngineConfig;

/* Kicks off decoding + (optional) diarization + transcription on a background
 * thread. Progress/results are pushed into *session (thread-safe). Only one
 * job may run at a time per EngineConfig instance. */
bool engine_transcribe_file_async(const EngineConfig *cfg, Session *session);

/* Requests cooperative cancellation of the running job, if any. */
void engine_cancel(Session *session);

#ifdef __cplusplus
}
#endif

#endif
