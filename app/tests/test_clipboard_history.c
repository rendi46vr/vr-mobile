#include "common.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clipboard_history.h"
#include "util/file.h"

static void
test_append_clipboard_history(void) {
#ifdef _WIN32
    _putenv("VR_MOBILE_CONFIG_DIR=test-clipboard-history-config");
#else
    setenv("VR_MOBILE_CONFIG_DIR", "test-clipboard-history-config", true);
#endif

    bool ok = sc_clipboard_history_append("hello");
    assert(ok);

    ok = sc_clipboard_history_append("world");
    assert(ok);

    char *path =
        sc_file_build_path("test-clipboard-history-config",
                           "clipboard-history.txt");
    assert(path);

    FILE *file = fopen(path, "rb");
    assert(file);

    char buf[512];
    size_t r = fread(buf, 1, sizeof(buf) - 1, file);
    fclose(file);
    free(path);

    buf[r] = '\0';
    assert(strstr(buf, "hello"));
    assert(strstr(buf, "world"));
}

int
main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_append_clipboard_history();
    return 0;
}
