#include "cimgui.h"
#define CIMGUI_USE_GLFW
#define CIMGUI_USE_OPENGL3
#include "cimgui_impl.h"

#if defined(__APPLE__)
#include <OpenGL/gl.h>
#elif defined(_WIN32)
#include <windows.h>
#include <GL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>

#include "session.h"
#include "engine.h"
#include "ui.h"
#include "app_paths.h"

static AppState g_app;
static char g_exe_dir[1024];

static void glfw_error_callback(int error, const char *desc) {
    fprintf(stderr, "[glfw] error %d: %s\n", error, desc);
}

static void drop_callback(GLFWwindow *window, int count, const char **paths) {
    (void)window;
    if (count > 0) {
        ui_on_file_dropped(&g_app, paths[0]);
    }
}

static void find_default_paths(EngineConfig *cfg) {
    /* The app expects a self-contained layout next to the executable:
     *   <bin>/models/ggml-small.bin (or ggml-medium.bin)
     *   <bin>/models/diarization/segmentation.onnx
     *   <bin>/models/diarization/embedding.onnx
     * scripts/fetch_models.sh populates this tree for local development;
     * the packaging step (see README.md) bundles it per-OS. Paths are
     * resolved relative to the running binary, not the current working
     * directory, so double-clicking the app works from anywhere.
     */
    snprintf(cfg->whisper_model_path, sizeof(cfg->whisper_model_path),
             "%s/models/ggml-small.bin", g_exe_dir);
    snprintf(cfg->diarization_seg_model_path, sizeof(cfg->diarization_seg_model_path),
              "%s/models/diarization/segmentation.onnx", g_exe_dir);
    snprintf(cfg->diarization_embed_model_path, sizeof(cfg->diarization_embed_model_path),
              "%s/models/diarization/embedding.onnx", g_exe_dir);
    cfg->num_threads = 4;
    cfg->enable_diarization = true;
}

int main(void) {
    app_get_executable_dir(g_exe_dir, sizeof(g_exe_dir));

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) return 1;

#if defined(__APPLE__)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    const char *glsl_version = "#version 150";
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    const char *glsl_version = "#version 130";
#endif

    GLFWwindow *window = glfwCreateWindow(1000, 700, "transcribe", NULL, NULL);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetDropCallback(window, drop_callback);

    igCreateContext(NULL);
    ImGuiIO *io = igGetIO_Nil();
    io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    char font_path[1100];
    snprintf(font_path, sizeof(font_path), "%s/assets/DroidSans.ttf", g_exe_dir);
    io->Fonts->TexGlyphPadding = 1;
    ImFontAtlas_AddFontFromFileTTF(io->Fonts, font_path, 18.0f, NULL, NULL);

    Session session;
    session_init(&session);

    g_app.session = &session;
    find_default_paths(&g_app.engine_cfg);
    ui_init(&g_app);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        igNewFrame();

        ui_render(&g_app);

        igRender();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.1f, 0.1f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(igGetDrawData());

        glfwSwapBuffers(window);
    }

    session_destroy(&session);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    igDestroyContext(NULL);

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
