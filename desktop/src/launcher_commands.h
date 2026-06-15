#ifndef VR_LAUNCHER_COMMANDS_H
#define VR_LAUNCHER_COMMANDS_H

#include <stddef.h>

enum vr_launcher_command {
    VR_LAUNCHER_COMMAND_CONNECT,
    VR_LAUNCHER_COMMAND_WIRELESS_SETUP,
    VR_LAUNCHER_COMMAND_DEVICE_STATUS,
    VR_LAUNCHER_COMMAND_CONNECTION_HEALTH,
    VR_LAUNCHER_COMMAND_RUN_PROFILE,
    VR_LAUNCHER_COMMAND_SEND_FILE,
};

const char *
vr_launcher_command_label(enum vr_launcher_command command);

const char *const *
vr_launcher_command_args(enum vr_launcher_command command);

size_t
vr_launcher_command_arg_count(enum vr_launcher_command command);

#endif
