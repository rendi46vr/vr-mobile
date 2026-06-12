#include "device_profile.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/file.h"
#include "util/log.h"
#include "vr_config.h"

#define SC_DEVICE_PROFILE_DIR "profiles"
#define SC_DEVICE_PROFILE_SUFFIX ".profile"

bool
sc_device_profile_name_is_valid(const char *name) {
    if (!name || !*name) {
        return false;
    }

    for (const char *p = name; *p; ++p) {
        unsigned char c = (unsigned char) *p;
        if (!isalnum(c) && c != '-' && c != '_' && c != '.') {
            return false;
        }
    }

    return true;
}

static char *
sc_device_profile_get_dir(bool create) {
    char *config_dir = sc_vr_config_get_dir(create);
    if (!config_dir) {
        return NULL;
    }

    char *profile_dir = sc_file_build_path(config_dir, SC_DEVICE_PROFILE_DIR);
    free(config_dir);
    if (!profile_dir) {
        return NULL;
    }

    if (!create || sc_vr_config_mkdir(profile_dir)) {
        return profile_dir;
    }

    free(profile_dir);
    return NULL;
}

static char *
sc_device_profile_get_path(const char *name, bool create) {
    if (!sc_device_profile_name_is_valid(name)) {
        LOGE("Invalid profile name: %s", name ? name : "(null)");
        LOGE("Use only letters, numbers, dot, dash or underscore.");
        return NULL;
    }

    char *dir = sc_device_profile_get_dir(create);
    if (!dir) {
        return NULL;
    }

    size_t name_len = strlen(name);
    size_t suffix_len = strlen(SC_DEVICE_PROFILE_SUFFIX);
    char *filename = malloc(name_len + suffix_len + 1);
    if (!filename) {
        LOG_OOM();
        free(dir);
        return NULL;
    }

    memcpy(filename, name, name_len);
    memcpy(&filename[name_len], SC_DEVICE_PROFILE_SUFFIX, suffix_len + 1);

    char *path = sc_file_build_path(dir, filename);
    free(filename);
    free(dir);
    return path;
}

static char *
sc_device_profile_trim(char *s) {
    while (*s == ' ' || *s == '\t') {
        ++s;
    }

    char *end = s + strlen(s);
    while (end > s && (end[-1] == '\n' || end[-1] == '\r'
            || end[-1] == ' ' || end[-1] == '\t')) {
        --end;
    }
    *end = '\0';

    return s;
}

static bool
sc_device_profile_args_push(struct sc_device_profile_args *args,
                            const char *arg) {
    char **argv = realloc(args->argv, (args->argc + 2) * sizeof(*argv));
    if (!argv) {
        LOG_OOM();
        return false;
    }

    args->argv = argv;
    args->argv[args->argc] = strdup(arg);
    if (!args->argv[args->argc]) {
        LOG_OOM();
        return false;
    }

    ++args->argc;
    args->argv[args->argc] = NULL;
    return true;
}

bool
sc_device_profile_load(const char *name, struct sc_device_profile_args *args) {
    args->argc = 0;
    args->argv = NULL;

    char *path = sc_device_profile_get_path(name, false);
    if (!path) {
        return false;
    }

    FILE *file = fopen(path, "rb");
    if (!file) {
        LOGE("Could not open profile '%s': %s", name, path);
        free(path);
        return false;
    }

    char line[4096];
    bool ok = true;
    while (fgets(line, sizeof(line), file)) {
        char *arg = sc_device_profile_trim(line);
        if (!*arg || *arg == '#') {
            continue;
        }

        if (!sc_device_profile_args_push(args, arg)) {
            ok = false;
            break;
        }
    }

    if (ferror(file)) {
        LOGE("Could not read profile '%s'", name);
        ok = false;
    }

    fclose(file);
    free(path);

    if (!ok) {
        sc_device_profile_args_destroy(args);
    }

    return ok;
}

bool
sc_device_profile_save(const char *name, int argc, char *argv[]) {
    char *path = sc_device_profile_get_path(name, true);
    if (!path) {
        return false;
    }

    FILE *file = fopen(path, "wb");
    if (!file) {
        LOGE("Could not save profile '%s': %s", name, path);
        free(path);
        return false;
    }

    bool ok = fprintf(file, "# VR Mobile device profile: %s\n", name) > 0;
    for (int i = 0; ok && i < argc; ++i) {
        if (strchr(argv[i], '\n') || strchr(argv[i], '\r')) {
            LOGE("Cannot save profile argument containing a newline");
            ok = false;
            break;
        }
        ok = fprintf(file, "%s\n", argv[i]) > 0;
    }

    ok = !fclose(file) && ok;
    if (ok) {
        LOGI("Profile '%s' saved to %s", name, path);
    } else {
        LOGE("Could not save profile '%s'", name);
    }

    free(path);
    return ok;
}

void
sc_device_profile_args_destroy(struct sc_device_profile_args *args) {
    for (int i = 0; i < args->argc; ++i) {
        free(args->argv[i]);
    }
    free(args->argv);
}
