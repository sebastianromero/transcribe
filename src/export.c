#include "export.h"

#include <stdio.h>

static void fmt_timestamp(double sec, char *buf, size_t len) {
    int h = (int)(sec / 3600);
    int m = (int)((sec - h * 3600) / 60);
    double s = sec - h * 3600 - m * 60;
    snprintf(buf, len, "%02d:%02d:%05.2f", h, m, s);
}

bool export_write(Session *s, const char *path, ExportFormat fmt, bool with_timestamps) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;

    session_lock(s);

    if (fmt == EXPORT_MD) {
        fprintf(f, "# Transcripcion: %s\n\n", s->current_file);
    }

    int last_speaker = -2;
    for (size_t i = 0; i < s->segment_count; i++) {
        Segment *seg = &s->segments[i];
        const char *name = session_speaker_name(s, seg->speaker_id);

        bool new_speaker = seg->speaker_id != last_speaker;
        last_speaker = seg->speaker_id;

        char ts0[32], ts1[32];
        if (with_timestamps) {
            fmt_timestamp(seg->start_sec, ts0, sizeof(ts0));
            fmt_timestamp(seg->end_sec, ts1, sizeof(ts1));
        }

        if (fmt == EXPORT_MD) {
            if (new_speaker) {
                if (i != 0) fprintf(f, "\n");
                fprintf(f, "**%s**", name);
                if (with_timestamps) fprintf(f, " _[%s - %s]_", ts0, ts1);
                fprintf(f, "\n\n");
            }
            fprintf(f, "%s ", seg->text);
        } else {
            if (with_timestamps)
                fprintf(f, "[%s - %s] %s: %s\n", ts0, ts1, name, seg->text);
            else
                fprintf(f, "%s: %s\n", name, seg->text);
        }
    }

    session_unlock(s);
    fclose(f);
    return true;
}
