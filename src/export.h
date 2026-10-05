#ifndef TRANSCRIBE_EXPORT_H
#define TRANSCRIBE_EXPORT_H

#include "session.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { EXPORT_TXT, EXPORT_MD } ExportFormat;

/* Writes the current session's segments (with speaker names) to `path`.
 * Thread-safe: takes the session lock internally. Returns true on success. */
bool export_write(Session *s, const char *path, ExportFormat fmt, bool with_timestamps);

#ifdef __cplusplus
}
#endif

#endif
