#include "ui.h"
#include "export.h"
#include "cimgui.h"

#include <stdio.h>
#include <string.h>

/* Codes match whisper.cpp's own language codes (whisper_lang_str()), so the
 * value the user picks here can be passed straight back into whisper. */
static const struct { const char *code; const char *label; } kLanguages[] = {
    {"es", "Español"},
    {"en", "English"},
    {"fr", "Français"},
    {"de", "Deutsch"},
    {"it", "Italiano"},
    {"pt", "Português"},
    {"ca", "Català"},
    {"nl", "Nederlands"},
    {"ru", "Русский"},
    {"ja", "日本語"},
    {"zh", "中文"},
    {"ko", "한국어"},
    {"ar", "العربية"},
};
#define N_LANGUAGES (int)(sizeof(kLanguages) / sizeof(kLanguages[0]))

static const char *language_label(const char *code) {
    for (int i = 0; i < N_LANGUAGES; i++)
        if (strcmp(kLanguages[i].code, code) == 0) return kLanguages[i].label;
    return code;
}

void ui_init(AppState *app) {
    app->with_timestamps = true;
    app->export_format = 0;
    strncpy(app->status_line, "Arrastra un archivo de audio o video para empezar", sizeof(app->status_line) - 1);
    for (int i = 0; i < TRANSCRIBE_MAX_SPEAKERS; i++) app->merge_target[i] = -1;
}

void ui_on_file_dropped(AppState *app, const char *path) {
    strncpy(app->pending_path, path, sizeof(app->pending_path) - 1);
    app->pending_path[sizeof(app->pending_path) - 1] = 0;
    app->speaker_count_input = 0;
    app->setup_pending = true;
}

static void render_setup(AppState *app) {
    ImVec2 avail = igGetContentRegionAvail();
    igDummy((ImVec2){0, avail.y * 0.35f});

    float win_w = igGetWindowWidth();
    igSetCursorPosX(win_w * 0.5f - 220.0f);
    igTextWrapped("Archivo: %s", app->pending_path);

    igDummy((ImVec2){0, 10});
    igSetCursorPosX(win_w * 0.5f - 220.0f);
    igText("Numero de interlocutores (0 = detectar automaticamente):");
    igSameLine(0, 10);
    igSetNextItemWidth(80);
    igInputInt("##speakers", &app->speaker_count_input, 1, 1, 0);
    if (app->speaker_count_input < 0) app->speaker_count_input = 0;
    igSetCursorPosX(win_w * 0.5f - 220.0f);
    igTextDisabled("Si sabes cuantas personas hablan (ej: 2 para una entrevista),");
    igSetCursorPosX(win_w * 0.5f - 220.0f);
    igTextDisabled("indicarlo mejora mucho la separacion de interlocutores.");

    igDummy((ImVec2){0, 14});
    igSetCursorPosX(win_w * 0.5f - 70.0f);
    if (igButton("Continuar", (ImVec2){140, 0})) {
        session_reset_for_new_file(app->session, app->pending_path);
        session_lock(app->session);
        app->session->expected_speakers = app->speaker_count_input;
        session_unlock(app->session);
        app->pending_language_synced = false;
        app->setup_pending = false;
        engine_transcribe_file_async(&app->engine_cfg, app->session);
    }
}

static const char *status_label(EngineStatus st) {
    switch (st) {
        case ENGINE_IDLE: return "Listo";
        case ENGINE_DECODING_AUDIO: return "Leyendo audio...";
        case ENGINE_DIARIZING: return "Detectando interlocutores...";
        case ENGINE_LOADING_MODELS: return "Cargando modelos...";
        case ENGINE_DETECTING_LANGUAGE: return "Detectando idioma...";
        case ENGINE_AWAITING_LANGUAGE_CONFIRM: return "Confirma el idioma";
        case ENGINE_TRANSCRIBING: return "Transcribiendo...";
        case ENGINE_DONE: return "Completado";
        case ENGINE_ERROR: return "Error";
    }
    return "";
}

static void render_language_confirm(AppState *app) {
    Session *s = app->session;

    session_lock(s);
    if (!app->pending_language_synced && s->detected_language[0]) {
        strncpy(app->pending_language, s->detected_language, sizeof(app->pending_language) - 1);
        app->pending_language[sizeof(app->pending_language) - 1] = 0;
        app->pending_language_synced = true;
    }
    char detected[8];
    strncpy(detected, s->detected_language, sizeof(detected) - 1);
    detected[sizeof(detected) - 1] = 0;
    session_unlock(s);

    ImVec2 avail = igGetContentRegionAvail();
    igDummy((ImVec2){0, avail.y * 0.35f});

    float win_w = igGetWindowWidth();
    igSetCursorPosX(win_w * 0.5f - 220.0f);
    igText("Idioma detectado: %s", detected[0] ? language_label(detected) : "?");

    igSetCursorPosX(win_w * 0.5f - 220.0f);
    igText("Corregir si hace falta:");
    igSameLine(0, 10);
    igSetNextItemWidth(200);
    if (igBeginCombo("##lang", language_label(app->pending_language), 0)) {
        for (int i = 0; i < N_LANGUAGES; i++) {
            bool selected = strcmp(app->pending_language, kLanguages[i].code) == 0;
            if (igSelectable_Bool(kLanguages[i].label, selected, 0, (ImVec2){0, 0})) {
                strncpy(app->pending_language, kLanguages[i].code, sizeof(app->pending_language) - 1);
            }
        }
        igEndCombo();
    }

    igDummy((ImVec2){0, 12});
    igSetCursorPosX(win_w * 0.5f - 60.0f);
    if (igButton("Transcribir", (ImVec2){120, 0})) {
        session_confirm_language(s, app->pending_language);
    }
}

static void speaker_combo(AppState *app, Segment *seg, int row_idx) {
    Session *s = app->session;
    char combo_id[32];
    snprintf(combo_id, sizeof(combo_id), "##spk%d", row_idx);

    const char *preview = session_speaker_name(s, seg->speaker_id);
    if (igBeginCombo(combo_id, preview, 0)) {
        for (int i = 0; i < s->speaker_count; i++) {
            bool selected = (seg->speaker_id == i);
            if (igSelectable_Bool(s->speaker_names[i], selected, 0, (ImVec2){0, 0})) {
                seg->speaker_id = i;
            }
        }
        igEndCombo();
    }
}

/* Unique speaker ids actually used by segments right now, in first-seen order. */
static int collect_present_speakers(Session *s, int *out, int max_out) {
    int n = 0;
    for (size_t i = 0; i < s->segment_count && n < max_out; i++) {
        int id = s->segments[i].speaker_id;
        bool seen = false;
        for (int j = 0; j < n; j++) if (out[j] == id) { seen = true; break; }
        if (!seen) out[n++] = id;
    }
    return n;
}

static void render_speaker_panel(AppState *app) {
    Session *s = app->session;
    session_lock(s);

    int present[TRANSCRIBE_MAX_SPEAKERS];
    int n_present = collect_present_speakers(s, present, TRANSCRIBE_MAX_SPEAKERS);

    if (n_present > 1) {
        igText("Hablantes (%d): renombra o fusiona los que en realidad son la misma persona.", n_present);
        for (int k = 0; k < n_present; k++) {
            int id = present[k];
            igPushID_Int(id);

            char name_buf[TRANSCRIBE_SPEAKER_NAME_LEN];
            strncpy(name_buf, s->speaker_names[id], sizeof(name_buf) - 1);
            name_buf[sizeof(name_buf) - 1] = 0;

            igSetNextItemWidth(160);
            if (igInputText("##name", name_buf, sizeof(name_buf), 0, NULL, NULL)) {
                strncpy(s->speaker_names[id], name_buf, sizeof(s->speaker_names[id]) - 1);
            }
            igSameLine(0, 6);
            igText("fusionar con:");
            igSameLine(0, 6);
            igSetNextItemWidth(140);
            const char *merge_preview = app->merge_target[id] >= 0 ? s->speaker_names[app->merge_target[id]] : "(ninguno)";
            if (igBeginCombo("##merge", merge_preview, 0)) {
                if (igSelectable_Bool("(ninguno)", app->merge_target[id] < 0, 0, (ImVec2){0, 0})) {
                    app->merge_target[id] = -1;
                }
                for (int j = 0; j < n_present; j++) {
                    if (present[j] == id) continue;
                    bool selected = app->merge_target[id] == present[j];
                    if (igSelectable_Bool(s->speaker_names[present[j]], selected, 0, (ImVec2){0, 0})) {
                        app->merge_target[id] = present[j];
                    }
                }
                igEndCombo();
            }
            igSameLine(0, 6);
            bool can_merge = app->merge_target[id] >= 0;
            if (!can_merge) igBeginDisabled(true);
            if (igButton("Aplicar", (ImVec2){0, 0})) {
                session_unlock(s);
                session_merge_speakers(s, id, app->merge_target[id]);
                app->merge_target[id] = -1;
                session_lock(s);
            }
            if (!can_merge) igEndDisabled();

            igPopID();
        }
        igSeparator();
    }

    session_unlock(s);
}

static void render_transcript(AppState *app) {
    Session *s = app->session;

    session_lock(s);
    igText("%s", status_label(s->status));
    if (s->status != ENGINE_IDLE && s->status != ENGINE_DONE && s->status != ENGINE_ERROR) {
        igSameLine(0, 10);
        igProgressBar(s->progress, (ImVec2){200, 0}, NULL);
    }
    if (s->status == ENGINE_ERROR) {
        igTextColored((ImVec4){1, 0.3f, 0.3f, 1}, "%s", s->error_message);
    }
    session_unlock(s);

    igSeparator();
    render_speaker_panel(app);

    session_lock(s);
    igBeginChild_Str("transcript", (ImVec2){0, -40}, 0, 0);
    for (size_t i = 0; i < s->segment_count; i++) {
        Segment *seg = &s->segments[i];
        igPushID_Int((int)i);

        if (app->with_timestamps) {
            igText("[%05.1f - %05.1f]", seg->start_sec, seg->end_sec);
            igSameLine(0, 8);
        }

        igSetNextItemWidth(140);
        speaker_combo(app, seg, (int)i);
        igSameLine(0, 8);

        igTextWrapped("%s", seg->text);

        igPopID();
    }
    igEndChild();

    session_unlock(s);

    igSeparator();
    igCheckbox("Incluir timestamps", &app->with_timestamps);
    igSameLine(0, 12);

    if (igButton("Exportar .txt", (ImVec2){0, 0})) {
        char out_path[1100];
        snprintf(out_path, sizeof(out_path), "%s.txt", s->current_file);
        export_write(s, out_path, EXPORT_TXT, app->with_timestamps);
        snprintf(app->status_line, sizeof(app->status_line), "Exportado: %s", out_path);
    }
    igSameLine(0, 8);
    if (igButton("Exportar .md", (ImVec2){0, 0})) {
        char out_path[1100];
        snprintf(out_path, sizeof(out_path), "%s.md", s->current_file);
        export_write(s, out_path, EXPORT_MD, app->with_timestamps);
        snprintf(app->status_line, sizeof(app->status_line), "Exportado: %s", out_path);
    }
}

static void render_dropzone(AppState *app) {
    ImVec2 avail = igGetContentRegionAvail();
    igDummy((ImVec2){0, avail.y * 0.4f});

    float win_w = igGetWindowWidth();
    igSetCursorPosX(win_w * 0.5f - 180.0f);
    igText("Arrastra aqui un archivo de audio o video");
    igSetCursorPosX(win_w * 0.5f - 120.0f);
    igTextDisabled("(wav, mp3, flac, mp4, mov, m4a...)");
    (void)app;
}

void ui_render(AppState *app) {
    const ImGuiViewport *viewport = igGetMainViewport();
    igSetNextWindowPos(viewport->WorkPos, ImGuiCond_Always, (ImVec2){0, 0});
    igSetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);

    igBegin("transcribe", NULL,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    session_lock(app->session);
    bool has_file = app->session->current_file[0] != 0;
    EngineStatus st = app->session->status;
    session_unlock(app->session);

    if (app->setup_pending) {
        render_setup(app);
    } else if (!has_file) {
        render_dropzone(app);
    } else if (st == ENGINE_AWAITING_LANGUAGE_CONFIRM) {
        render_language_confirm(app);
    } else {
        render_transcript(app);
    }

    igEnd();
}
