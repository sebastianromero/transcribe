cmake_minimum_required(VERSION 3.16)
project(transcribe C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Some vendored dependencies (pulled in transitively by sherpa-onnx) still
# declare cmake_minimum_required() below what modern CMake accepts.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

option(TRANSCRIBE_ENABLE_DIARIZATION "Build with sherpa-onnx speaker diarization" ON)

include(FetchContent)

# ---------------------------------------------------------------------------
# GLFW (window + input, used as the ImGui backend windowing layer)
# ---------------------------------------------------------------------------
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glfw)

# ---------------------------------------------------------------------------
# Dear ImGui (docking branch) + cimgui (C API wrapper)
# ---------------------------------------------------------------------------
FetchContent_Declare(
    cimgui
    GIT_REPOSITORY https://github.com/cimgui/cimgui.git
    GIT_TAG docking_inter
    GIT_SHALLOW TRUE
    GIT_SUBMODULES "imgui"
)
# cimgui's own CMakeLists.txt already defines a "cimgui" target; we only
# want its sources, and build our own static target below so we control
# which imgui backends get compiled in.
FetchContent_GetProperties(cimgui)
if(NOT cimgui_POPULATED)
    FetchContent_Populate(cimgui)
endif()

add_library(cimgui STATIC
    ${cimgui_SOURCE_DIR}/cimgui.cpp
    ${cimgui_SOURCE_DIR}/imgui/imgui.cpp
    ${cimgui_SOURCE_DIR}/imgui/imgui_draw.cpp
    ${cimgui_SOURCE_DIR}/imgui/imgui_widgets.cpp
    ${cimgui_SOURCE_DIR}/imgui/imgui_tables.cpp
    ${cimgui_SOURCE_DIR}/imgui/imgui_demo.cpp
    ${cimgui_SOURCE_DIR}/imgui/backends/imgui_impl_glfw.cpp
    ${cimgui_SOURCE_DIR}/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(cimgui PUBLIC
    ${cimgui_SOURCE_DIR}
    ${cimgui_SOURCE_DIR}/imgui
    ${cimgui_SOURCE_DIR}/imgui/backends
    ${cimgui_SOURCE_DIR}/generator/output
)
target_compile_definitions(cimgui PRIVATE
    IMGUI_IMPL_API=extern\ \"C\"
)
target_link_libraries(cimgui PUBLIC glfw)
if(APPLE)
    target_link_libraries(cimgui PUBLIC "-framework OpenGL")
elseif(UNIX)
    find_package(OpenGL REQUIRED)
    target_link_libraries(cimgui PUBLIC OpenGL::GL)
elseif(WIN32)
    target_link_libraries(cimgui PUBLIC opengl32)
endif()

# ---------------------------------------------------------------------------
# whisper.cpp (speech-to-text engine, used only through its C API)
# ---------------------------------------------------------------------------
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    whispercpp
    GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(whispercpp)

# ---------------------------------------------------------------------------
# sherpa-onnx (speaker diarization, used only through its C API)
# ---------------------------------------------------------------------------
if(TRANSCRIBE_ENABLE_DIARIZATION)
    set(SHERPA_ONNX_ENABLE_PYTHON OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_C_API ON CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_BINARY OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_CHECK OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_TTS OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_PORTAUDIO OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_WEBSOCKET OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_JNI OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_GPU OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        sherpa_onnx
        GIT_REPOSITORY https://github.com/k2-fsa/sherpa-onnx.git
        GIT_TAG v1.13.4
        GIT_SHALLOW TRUE
    )
    # sherpa-onnx's own CMakeLists.txt appends to CMAKE_MODULE_PATH using
    # CMAKE_SOURCE_DIR (the top-level project dir) instead of its own
    # CMAKE_CURRENT_SOURCE_DIR, which breaks when it's consumed as a
    # FetchContent subproject. Populate it first and point CMAKE_MODULE_PATH
    # at its cmake/ dirs ourselves before add_subdirectory runs.
    FetchContent_GetProperties(sherpa_onnx)
    if(NOT sherpa_onnx_POPULATED)
        FetchContent_Populate(sherpa_onnx)
        list(APPEND CMAKE_MODULE_PATH ${sherpa_onnx_SOURCE_DIR}/cmake/Modules)
        list(APPEND CMAKE_MODULE_PATH ${sherpa_onnx_SOURCE_DIR}/cmake)
        # sherpa-onnx's csrc/CMakeLists.txt does
        # include_directories(${CMAKE_SOURCE_DIR}) expecting to be the
        # top-level project; since it's nested here, point that at its own
        # source root so `#include "sherpa-onnx/csrc/x.h"` resolves.
        include_directories(${sherpa_onnx_SOURCE_DIR})
        add_subdirectory(${sherpa_onnx_SOURCE_DIR} ${sherpa_onnx_BINARY_DIR})
    endif()
endif()

# ---------------------------------------------------------------------------
# dr_libs (single-header C audio decoders: wav/mp3/flac) for loading dropped files
# ---------------------------------------------------------------------------
FetchContent_Declare(
    dr_libs
    GIT_REPOSITORY https://github.com/mackron/dr_libs.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(dr_libs)

# ---------------------------------------------------------------------------
# Application (pure C)
# ---------------------------------------------------------------------------
add_executable(transcribe
    src/main.c
    src/ui.c
    src/engine.c
    src/audio_decode.c
    src/session.c
    src/export.c
    src/app_paths.c
)

target_include_directories(transcribe PRIVATE
    src
    ${dr_libs_SOURCE_DIR}
)

target_compile_definitions(transcribe PRIVATE CIMGUI_DEFINE_ENUMS_AND_STRUCTS)
target_link_libraries(transcribe PRIVATE cimgui glfw whisper)

if(TRANSCRIBE_ENABLE_DIARIZATION)
    target_compile_definitions(transcribe PRIVATE TRANSCRIBE_WITH_DIARIZATION=1)
    target_link_libraries(transcribe PRIVATE sherpa-onnx-c-api)
endif()

if(APPLE)
    target_link_libraries(transcribe PRIVATE "-framework CoreFoundation" "-framework Cocoa" "-framework IOKit")
endif()

set_target_properties(transcribe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

file(COPY ${cimgui_SOURCE_DIR}/imgui/misc/fonts/DroidSans.ttf
     DESTINATION ${CMAKE_BINARY_DIR}/bin/assets)

# For local development, keep <bin>/models in sync with <repo>/models (if it
# exists yet) so the freshly built binary finds them next to itself, exactly
# like a packaged self-contained build would.
if(EXISTS ${CMAKE_SOURCE_DIR}/models)
    add_custom_command(TARGET transcribe POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/models ${CMAKE_BINARY_DIR}/bin/models
    )
endif()

# Headless CLI used only to smoke-test the engine pipeline without a display.
add_executable(test_engine_cli
    src/test_engine_cli.c
    src/engine.c
    src/audio_decode.c
    src/session.c
)
target_include_directories(test_engine_cli PRIVATE src ${dr_libs_SOURCE_DIR})
target_link_libraries(test_engine_cli PRIVATE whisper)
if(TRANSCRIBE_ENABLE_DIARIZATION)
    target_compile_definitions(test_engine_cli PRIVATE TRANSCRIBE_WITH_DIARIZATION=1)
    target_link_libraries(test_engine_cli PRIVATE sherpa-onnx-c-api)
endif()
set_target_properties(test_engine_cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
