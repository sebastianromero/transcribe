#ifndef TRANSCRIBE_APP_PATHS_H
#define TRANSCRIBE_APP_PATHS_H

#include <stddef.h>

/* Fills `out` with the directory containing the running executable
 * (no trailing slash), so bundled assets/models can be found regardless
 * of the current working directory the app was launched from. */
void app_get_executable_dir(char *out, size_t out_len);

#endif
