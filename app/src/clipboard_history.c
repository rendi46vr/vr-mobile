#include "clipboard_history.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "util/log.h"
#include "vr_config.h"

#define SC_CLIPBOARD_HISTORY_FILE "clipboard-history.txt"

bool
sc_clipboard_history_append(const char *text) {
    char *path = sc_vr_config_get_file(SC_CLIPBOARD_HISTORY_FILE, true);
    if (!path) {
        return false;
    }

    FILE *file = fopen(path, "ab");
    if (!file) {
        LOGW("Could not open clipboard history: %s", path);
        free(path);
        return false;
    }

    time_t now = time(NULL);
    bool ok = fprintf(file, "\n--- %" PRId64 " ---\n%s\n",
                      (int64_t) now, text ? text : "") > 0;
    ok = !fclose(file) && ok;
    if (!ok) {
        LOGW("Could not write clipboard history");
    } else {
        LOGD("Clipboard text appended to %s", path);
    }

    free(path);
    return ok;
}
