#include <assert.h>
#include <string.h>

#include "launcher_commands.h"

static void
test_connect_command(void) {
    const char *const *args =
        vr_launcher_command_args(VR_LAUNCHER_COMMAND_CONNECT);

    assert(!strcmp("Connect",
                   vr_launcher_command_label(VR_LAUNCHER_COMMAND_CONNECT)));
    assert(vr_launcher_command_arg_count(VR_LAUNCHER_COMMAND_CONNECT) == 1);
    assert(!strcmp("--connect-manager", args[0]));
    assert(!args[1]);
}

static void
test_wireless_setup_command(void) {
    const char *const *args =
        vr_launcher_command_args(VR_LAUNCHER_COMMAND_WIRELESS_SETUP);

    assert(!strcmp("Wireless setup",
                   vr_launcher_command_label(
                       VR_LAUNCHER_COMMAND_WIRELESS_SETUP)));
    assert(vr_launcher_command_arg_count(
               VR_LAUNCHER_COMMAND_WIRELESS_SETUP) == 1);
    assert(!strcmp("--wireless-setup", args[0]));
    assert(!args[1]);
}

static void
test_device_status_command(void) {
    const char *const *args =
        vr_launcher_command_args(VR_LAUNCHER_COMMAND_DEVICE_STATUS);

    assert(!strcmp("Device status",
                   vr_launcher_command_label(
                       VR_LAUNCHER_COMMAND_DEVICE_STATUS)));
    assert(vr_launcher_command_arg_count(
               VR_LAUNCHER_COMMAND_DEVICE_STATUS) == 3);
    assert(!strcmp("--connect-manager", args[0]));
    assert(!strcmp("--device-status", args[1]));
    assert(!strcmp("--output-format=json", args[2]));
    assert(!args[3]);
}

static void
test_connection_health_command(void) {
    const char *const *args =
        vr_launcher_command_args(VR_LAUNCHER_COMMAND_CONNECTION_HEALTH);

    assert(!strcmp("Connection health",
                   vr_launcher_command_label(
                       VR_LAUNCHER_COMMAND_CONNECTION_HEALTH)));
    assert(vr_launcher_command_arg_count(
               VR_LAUNCHER_COMMAND_CONNECTION_HEALTH) == 2);
    assert(!strcmp("--connection-health", args[0]));
    assert(!strcmp("--output-format=json", args[1]));
    assert(!args[2]);
}

static void
test_dynamic_commands(void) {
    assert(!strcmp("Run profile",
                   vr_launcher_command_label(
                       VR_LAUNCHER_COMMAND_RUN_PROFILE)));
    assert(vr_launcher_command_arg_count(
                VR_LAUNCHER_COMMAND_RUN_PROFILE) == 0);
    assert(!vr_launcher_command_args(VR_LAUNCHER_COMMAND_RUN_PROFILE));

    assert(!strcmp("Tailscale connect",
                   vr_launcher_command_label(
                       VR_LAUNCHER_COMMAND_TAILSCALE_CONNECT)));
    assert(vr_launcher_command_arg_count(
               VR_LAUNCHER_COMMAND_TAILSCALE_CONNECT) == 0);
    assert(!vr_launcher_command_args(VR_LAUNCHER_COMMAND_TAILSCALE_CONNECT));

    assert(!strcmp("Send file",
                   vr_launcher_command_label(
                       VR_LAUNCHER_COMMAND_SEND_FILE)));
    assert(vr_launcher_command_arg_count(
               VR_LAUNCHER_COMMAND_SEND_FILE) == 0);
    assert(!vr_launcher_command_args(VR_LAUNCHER_COMMAND_SEND_FILE));
}

int
main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_connect_command();
    test_wireless_setup_command();
    test_device_status_command();
    test_connection_health_command();
    test_dynamic_commands();

    return 0;
}
