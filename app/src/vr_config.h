#ifndef SC_VR_CONFIG_H
#define SC_VR_CONFIG_H

#include "common.h"

#include <stdbool.h>

#define SC_VR_CONFIG_DIR_ENV "VR_MOBILE_CONFIG_DIR"

bool
sc_vr_config_mkdir(const char *path);

char *
sc_vr_config_get_dir(bool create);

char *
sc_vr_config_get_file(const char *filename, bool create);

#endif
