#include "vr_config.h"

#include <errno.h>
#include <stdlib.h>
#ifdef _WIN32
# include <direct.h>
#else
# include <sys/stat.h>
#endif

#include "util/env.h"
#include "util/file.h"
#include "util/log.h"

bool
sc_vr_config_mkdir(const char *path) {
#ifdef _WIN32
    int r = _mkdir(path);
#else
    int r = mkdir(path, 0700);
#endif
    if (!r || errno == EEXIST) {
        return true;
    }

    LOGW("Could not create config directory: %s", path);
    return false;
}

char *
sc_vr_config_get_dir(bool create) {
    char *dir = sc_get_env(SC_VR_CONFIG_DIR_ENV);
    if (dir) {
        if (!create || sc_vr_config_mkdir(dir)) {
            return dir;
        }
        free(dir);
        return NULL;
    }

#ifdef _WIN32
    char *base = sc_get_env("APPDATA");
    if (!base) {
        return NULL;
    }

    dir = sc_file_build_path(base, "VR Mobile");
    free(base);
    if (!dir) {
        return NULL;
    }
#else
    char *base = sc_get_env("XDG_CONFIG_HOME");
    if (!base) {
        char *home = sc_get_env("HOME");
        if (!home) {
            return NULL;
        }
        base = sc_file_build_path(home, ".config");
        free(home);
        if (!base) {
            return NULL;
        }
    }

    dir = sc_file_build_path(base, "vr-mobile");
    free(base);
    if (!dir) {
        return NULL;
    }
#endif

    if (!create || sc_vr_config_mkdir(dir)) {
        return dir;
    }

    free(dir);
    return NULL;
}

char *
sc_vr_config_get_file(const char *filename, bool create) {
    char *dir = sc_vr_config_get_dir(create);
    if (!dir) {
        return NULL;
    }

    char *path = sc_file_build_path(dir, filename);
    free(dir);
    return path;
}
