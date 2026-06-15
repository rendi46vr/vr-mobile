#include <assert.h>
#include <string.h>

#include "launcher_json.h"

static void
test_parse_devices_with_log_prefix(void) {
    const char *output =
        "scrcpy 4.0\n"
        "DEBUG: Using adb\n"
        "{\"devices\":["
        "{\"serial\":\"bf1953fa\",\"state\":\"device\",\"type\":\"usb\"},"
        "{\"serial\":\"10.50.2.168:5555\",\"state\":\"device\",\"type\":\"wifi\"}"
        "],\"last_wifi_serial\":\"10.50.2.168:5555\"}\n";

    struct vr_launcher_device_info devices[VR_LAUNCHER_MAX_DEVICES];
    size_t count = vr_launcher_parse_devices(output, devices,
                                             VR_LAUNCHER_MAX_DEVICES);

    assert(count == 2);
    assert(!strcmp("bf1953fa", devices[0].serial));
    assert(!strcmp("device", devices[0].state));
    assert(!strcmp("usb", devices[0].type));
    assert(!strcmp("10.50.2.168:5555", devices[1].serial));
    assert(!strcmp("wifi", devices[1].type));
}

static void
test_parse_device_status(void) {
    const char *output =
        "INFO: Connect manager status: Wi-Fi connected\n"
        "{\"serial\":\"10.50.2.168:5555\","
        "\"manufacturer\":\"Xiaomi\","
        "\"model\":\"23127PN0CG\","
        "\"android_version\":\"16\","
        "\"wifi_ip\":\"10.50.2.168\","
        "\"screen_size\":\"Physical size: 1200x2670\\nOverride size: 1200x2670\\n\","
        "\"battery\":\"Current Battery Service state:\\n  level: 87\\n\","
        "\"storage\":\"Filesystem      Size Used Avail Use% Mounted on\\n"
        "/dev/fuse       228G 89G 139G 40% /sdcard\\n\"}\n";

    struct vr_launcher_device_status status;
    bool ok = vr_launcher_parse_device_status(output, &status);

    assert(ok);
    assert(!strcmp("10.50.2.168:5555", status.serial));
    assert(!strcmp("Xiaomi", status.manufacturer));
    assert(!strcmp("23127PN0CG", status.model));
    assert(!strcmp("16", status.android_version));
    assert(!strcmp("10.50.2.168", status.wifi_ip));
    assert(!strcmp("Physical size: 1200x2670", status.screen_line));
    assert(!strcmp("87%", status.battery_level));
    assert(!strcmp("Filesystem      Size Used Avail Use% Mounted on",
                   status.storage_line));
}

int
main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_parse_devices_with_log_prefix();
    test_parse_device_status();

    return 0;
}
