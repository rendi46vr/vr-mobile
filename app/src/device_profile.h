#ifndef SC_DEVICE_PROFILE_H
#define SC_DEVICE_PROFILE_H

#include "common.h"

#include <stdbool.h>

struct sc_device_profile_args {
    int argc;
    char **argv;
};

bool
sc_device_profile_name_is_valid(const char *name);

bool
sc_device_profile_load(const char *name, struct sc_device_profile_args *args);

bool
sc_device_profile_save(const char *name, int argc, char *argv[]);

void
sc_device_profile_args_destroy(struct sc_device_profile_args *args);

#endif
