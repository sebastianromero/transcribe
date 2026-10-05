#ifndef TRANSCRIBE_AUDIO_DECODE_H
#define TRANSCRIBE_AUDIO_DECODE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Decodes any dropped media file (wav/mp3/flac/ogg or any container ffmpeg-less
 * path via raw PCM extraction) into mono 16kHz float32 samples, ready for whisper.
 * Returns true on success. Caller must free *out_samples with free(). */
bool audio_decode_to_mono16k(const char *path, float **out_samples, size_t *out_count,
                              char *err, size_t err_len);

#ifdef __cplusplus
}
#endif

#endif
