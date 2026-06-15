#include "launcher_commands.h"

static const char *const connect_args[] = {
    "--connect-manager",
    NULL,
};

static const char *const wireless_setup_args[] = {
    "--wireless-setup",
    NULL,
};

static const char *const device_status_args[] = {
    "--connect-manager",
    "--device-status",
    "--output-format=json",
    NULL,
};

static const char *const connection_health_args[] = {
    "--connection-health",
    "--output-format=json",
    NULL,
};

const char *
vr_launcher_command_label(enum vr_launcher_command command) {
    switch (command) {
        case VR_LAUNCHER_COMMAND_CONNECT:
            return "Connect";
        case VR_LAUNCHER_COMMAND_WIRELESS_SETUP:
            return "Wireless setup";
        case VR_LAUNCHER_COMMAND_DEVICE_STATUS:
            return "Device status";
        case VR_LAUNCHER_COMMAND_CONNECTION_HEALTH:
            return "Connection health";
        case VR_LAUNCHER_COMMAND_RUN_PROFILE:
            return "Run profile";
        case VR_LAUNCHER_COMMAND_SEND_FILE:
            return "Send file";
        default:
            return "Unknown command";
    }
}

const char *const *
vr_launcher_command_args(enum vr_launcher_command command) {
    switch (command) {
        case VR_LAUNCHER_COMMAND_CONNECT:
            return connect_args;
        case VR_LAUNCHER_COMMAND_WIRELESS_SETUP:
            return wireless_setup_args;
        case VR_LAUNCHER_COMMAND_DEVICE_STATUS:
            return device_status_args;
        case VR_LAUNCHER_COMMAND_CONNECTION_HEALTH:
            return connection_health_args;
        case VR_LAUNCHER_COMMAND_RUN_PROFILE:
        case VR_LAUNCHER_COMMAND_SEND_FILE:
            return NULL;
        default:
            return NULL;
    }
}

size_t
vr_launcher_command_arg_count(enum vr_launcher_command command) {
    const char *const *args = vr_launcher_command_args(command);
    if (!args) {
        return 0;
    }

    size_t count = 0;
    while (args[count]) {
        ++count;
    }
    return count;
}
