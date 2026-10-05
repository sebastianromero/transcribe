#include "app_paths.h"

#include <string.h>
#include <stdio.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <limits.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

#if !defined(_WIN32)
#include <stdlib.h>
#endif

static void strip_to_dir(char *path) {
    char *slash = strrchr(path, '/');
#if defined(_WIN32)
    char *bslash = strrchr(path, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
    if (slash) *slash = 0;
}

void app_get_executable_dir(char *out, size_t out_len) {
    out[0] = 0;

#if defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
        char resolved[PATH_MAX];
        if (realpath(buf, resolved)) {
            strncpy(out, resolved, out_len - 1);
        } else {
            strncpy(out, buf, out_len - 1);
        }
    }
#elif defined(_WIN32)
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf)) {
        strncpy(out, buf, out_len - 1);
    }
#else
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = 0;
        strncpy(out, buf, out_len - 1);
    }
#endif

    out[out_len - 1] = 0;
    strip_to_dir(out);
}
