#ifndef TRANSCRIBE_UI_H
#define TRANSCRIBE_UI_H

#include "session.h"
#include "engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Session *session;
    EngineConfig engine_cfg;
    bool with_timestamps;
    int export_format; /* 0 = txt, 1 = md */
    char status_line[256];
    char pending_language[8]; /* language selected in the confirm-language screen */
    bool pending_language_synced; /* whether pending_language was seeded from detection yet */

    bool setup_pending; /* showing the pre-transcription setup screen */
    char pending_path[1024]; /* file dropped, waiting for setup confirmation */
    int speaker_count_input; /* 0 = auto */

    int merge_target[TRANSCRIBE_MAX_SPEAKERS]; /* per-speaker "merge into" combo selection */
} AppState;

void ui_init(AppState *app);
/* Called once per frame between igNewFrame() and rendering. */
void ui_render(AppState *app);

/* Called from the GLFW drop callback with a single dropped file path. */
void ui_on_file_dropped(AppState *app, const char *path);

#ifdef __cplusplus
}
#endif

#endif
