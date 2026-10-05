#include "session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
typedef CRITICAL_SECTION mutex_t;
static void mutex_init(mutex_t *m) { InitializeCriticalSection(m); }
static void mutex_lock(mutex_t *m) { EnterCriticalSection(m); }
static void mutex_unlock(mutex_t *m) { LeaveCriticalSection(m); }
static void mutex_destroy(mutex_t *m) { DeleteCriticalSection(m); }
#else
#include <pthread.h>
typedef pthread_mutex_t mutex_t;
static void mutex_init(mutex_t *m) { pthread_mutex_init(m, NULL); }
static void mutex_lock(mutex_t *m) { pthread_mutex_lock(m); }
static void mutex_unlock(mutex_t *m) { pthread_mutex_unlock(m); }
static void mutex_destroy(mutex_t *m) { pthread_mutex_destroy(m); }
#endif

void session_init(Session *s) {
    memset(s, 0, sizeof(*s));
    mutex_t *m = (mutex_t *)malloc(sizeof(mutex_t));
    mutex_init(m);
    s->lock = m;
    s->segment_capacity = 256;
    s->segments = (Segment *)malloc(s->segment_capacity * sizeof(Segment));
    s->segment_count = 0;
    s->speaker_count = 0;
    strncpy(s->language, "auto", sizeof(s->language) - 1);
    s->status = ENGINE_IDLE;
}

void session_destroy(Session *s) {
    free(s->segments);
    mutex_destroy((mutex_t *)s->lock);
    free(s->lock);
    memset(s, 0, sizeof(*s));
}

void session_reset_for_new_file(Session *s, const char *path) {
    session_lock(s);
    strncpy(s->current_file, path, sizeof(s->current_file) - 1);
    s->current_file[sizeof(s->current_file) - 1] = 0;
    s->segment_count = 0;
    s->speaker_count = 0;
    s->status = ENGINE_IDLE;
    s->progress = 0.0f;
    s->error_message[0] = 0;
    s->cancel_requested = false;
    s->detected_language[0] = 0;
    s->language_confirmed = false;
    s->expected_speakers = 0;
    strncpy(s->language, "auto", sizeof(s->language) - 1);
    s->language[sizeof(s->language) - 1] = 0;
    session_unlock(s);
}

void session_lock(Session *s) { mutex_lock((mutex_t *)s->lock); }
void session_unlock(Session *s) { mutex_unlock((mutex_t *)s->lock); }

void session_add_segment_ts(Session *s, const Segment *seg) {
    session_lock(s);
    if (s->segment_count >= s->segment_capacity) {
        s->segment_capacity *= 2;
        s->segments = (Segment *)realloc(s->segments, s->segment_capacity * sizeof(Segment));
    }
    s->segments[s->segment_count++] = *seg;
    session_unlock(s);
}

void session_set_status_ts(Session *s, EngineStatus status, float progress) {
    session_lock(s);
    s->status = status;
    s->progress = progress;
    session_unlock(s);
}

void session_set_error_ts(Session *s, const char *msg) {
    session_lock(s);
    s->status = ENGINE_ERROR;
    strncpy(s->error_message, msg, sizeof(s->error_message) - 1);
    session_unlock(s);
}

int session_get_or_create_speaker_ts(Session *s, int diarization_cluster_id) {
    session_lock(s);
    int id = diarization_cluster_id;
    if (id < 0) id = 0;
    if (id >= TRANSCRIBE_MAX_SPEAKERS) id = TRANSCRIBE_MAX_SPEAKERS - 1;
    if (s->speaker_names[id][0] == 0) {
        snprintf(s->speaker_names[id], TRANSCRIBE_SPEAKER_NAME_LEN, "Speaker %d", id + 1);
        if (id + 1 > s->speaker_count) s->speaker_count = id + 1;
    }
    session_unlock(s);
    return id;
}

void session_rename_speaker(Session *s, int speaker_id, const char *name) {
    if (speaker_id < 0 || speaker_id >= TRANSCRIBE_MAX_SPEAKERS) return;
    session_lock(s);
    strncpy(s->speaker_names[speaker_id], name, TRANSCRIBE_SPEAKER_NAME_LEN - 1);
    s->speaker_names[speaker_id][TRANSCRIBE_SPEAKER_NAME_LEN - 1] = 0;
    session_unlock(s);
}

const char *session_speaker_name(Session *s, int speaker_id) {
    if (speaker_id < 0 || speaker_id >= TRANSCRIBE_MAX_SPEAKERS) return "Unknown";
    return s->speaker_names[speaker_id];
}

void session_set_detected_language_ts(Session *s, const char *lang) {
    session_lock(s);
    strncpy(s->detected_language, lang, sizeof(s->detected_language) - 1);
    s->detected_language[sizeof(s->detected_language) - 1] = 0;
    session_unlock(s);
}

void session_merge_speakers(Session *s, int from_id, int to_id) {
    if (from_id == to_id) return;
    session_lock(s);
    for (size_t i = 0; i < s->segment_count; i++) {
        if (s->segments[i].speaker_id == from_id) {
            s->segments[i].speaker_id = to_id;
        }
    }
    session_unlock(s);
}

void session_confirm_language(Session *s, const char *lang) {
    session_lock(s);
    strncpy(s->language, lang, sizeof(s->language) - 1);
    s->language[sizeof(s->language) - 1] = 0;
    s->language_confirmed = true;
    session_unlock(s);
}
