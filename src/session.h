#ifndef TRANSCRIBE_SESSION_H
#define TRANSCRIBE_SESSION_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TRANSCRIBE_MAX_SPEAKERS 32
#define TRANSCRIBE_SPEAKER_NAME_LEN 64
#define TRANSCRIBE_SEGMENT_TEXT_LEN 1024

typedef struct {
    double start_sec;
    double end_sec;
    int speaker_id;               /* -1 = unknown/unassigned */
    char text[TRANSCRIBE_SEGMENT_TEXT_LEN];
} Segment;

typedef enum {
    ENGINE_IDLE = 0,
    ENGINE_DECODING_AUDIO,
    ENGINE_DIARIZING,
    ENGINE_LOADING_MODELS,
    ENGINE_DETECTING_LANGUAGE,
    ENGINE_AWAITING_LANGUAGE_CONFIRM,
    ENGINE_TRANSCRIBING,
    ENGINE_DONE,
    ENGINE_ERROR
} EngineStatus;

typedef struct {
    /* Guarded by 'lock' -- UI thread reads, engine thread writes. */
    void *lock; /* opaque mutex, see session.c */

    char current_file[1024];
    char language[8]; /* "es", "en", "auto", ... -- desired/confirmed language */
    char detected_language[8]; /* auto-detected guess, shown to the user for confirmation */
    bool language_confirmed; /* set by the UI once the user accepts/corrects the language */
    int expected_speakers; /* 0 = let diarization auto-detect the speaker count */

    Segment *segments;
    size_t segment_count;
    size_t segment_capacity;

    char speaker_names[TRANSCRIBE_MAX_SPEAKERS][TRANSCRIBE_SPEAKER_NAME_LEN];
    int speaker_count;

    EngineStatus status;
    float progress; /* 0..1 */
    char error_message[512];
    bool cancel_requested;
} Session;

void session_init(Session *s);
void session_destroy(Session *s);
void session_reset_for_new_file(Session *s, const char *path);

void session_lock(Session *s);
void session_unlock(Session *s);

/* Engine-side helpers (must be called with lock held or via the *_ts variants). */
void session_add_segment_ts(Session *s, const Segment *seg);
void session_set_status_ts(Session *s, EngineStatus status, float progress);
void session_set_error_ts(Session *s, const char *msg);

int session_get_or_create_speaker_ts(Session *s, int diarization_cluster_id);
void session_rename_speaker(Session *s, int speaker_id, const char *name);
const char *session_speaker_name(Session *s, int speaker_id);

/* Reassigns every segment currently labeled `from_id` to `to_id` in one
 * shot, so the user doesn't have to fix an over-segmented speaker line by
 * line. Must be called with the lock NOT held (it takes it internally). */
void session_merge_speakers(Session *s, int from_id, int to_id);

void session_set_detected_language_ts(Session *s, const char *lang);
/* Called by the UI once the user accepts/corrects the detected language. */
void session_confirm_language(Session *s, const char *lang);

#ifdef __cplusplus
}
#endif

#endif
