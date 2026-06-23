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
test_parse_empty_connection_health(void) {
    const char *output =
        "scrcpy 4.0\n"
        "{\"devices\":[],\"last_wifi_serial\":\"10.208.64.184:5555\","
        "\"last_tailscale_serial\":\"100.80.12.34:5555\"}\n";

    struct vr_launcher_device_info devices[VR_LAUNCHER_MAX_DEVICES];
    size_t count = 42;
    char last_wifi_serial[VR_LAUNCHER_MAX_SERIAL_LEN];
    char last_tailscale_serial[VR_LAUNCHER_MAX_SERIAL_LEN];
    bool ok = vr_launcher_parse_connection_health(
        output, devices, VR_LAUNCHER_MAX_DEVICES, &count, last_wifi_serial,
        sizeof(last_wifi_serial), last_tailscale_serial,
        sizeof(last_tailscale_serial));

    assert(ok);
    assert(count == 0);
    assert(!strcmp("10.208.64.184:5555", last_wifi_serial));
    assert(!strcmp("100.80.12.34:5555", last_tailscale_serial));
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

static void
test_parse_companion_snapshot(void) {
    const char *output =
        "Result: Bundle[{data="
        "eyJ2ZXJzaW9uIjoxLCJlbmFibGVkIjp0cnVlLCJvdXRib3giOlt7ImlkIjo0Miwibm"
        "FtZSI6InJlcG9ydC5wZGYiLCJtaW1lIjoiYXBwbGljYXRpb24vcGRmIiwic2l6ZSI6MT"
        "IzNCwicGF0aCI6Ii9zZGNhcmQvRG93bmxvYWQvVlIgTW9iaWxlIENvbXBhbmlvbi9PdX"
        "Rib3gvcmVwb3J0LnBkZiJ9XSwibm90aWZpY2F0aW9ucyI6W3sia2V5Ijoid2hhdHNhcH"
        "Ata2V5IiwicGFja2FnZSI6ImNvbS53aGF0c2FwcCIsInRpdGxlIjoiUmVuZGkiLCJ0ZX"
        "h0IjoiSGVsbG8iLCJwb3N0X3RpbWUiOjEyMzQ1LCJjYW5fb3BlbiI6dHJ1ZSwicmVwbH"
        "lfYWN0aW9uIjowfV19}]";

    struct vr_companion_snapshot snapshot;
    bool ok = vr_launcher_parse_companion_snapshot(output, &snapshot);

    assert(ok);
    assert(snapshot.enabled);
    assert(snapshot.file_count == 1);
    assert(!strcmp("report.pdf", snapshot.files[0].name));
    assert(snapshot.files[0].size == 1234);
    assert(snapshot.notification_count == 1);
    assert(!strcmp("com.whatsapp",
                   snapshot.notifications[0].package_name));
    assert(!strcmp("Hello", snapshot.notifications[0].text));
    assert(snapshot.notifications[0].post_time == 12345);
    assert(snapshot.notifications[0].can_open);
    assert(snapshot.notifications[0].reply_action == 0);
}

static void
test_parse_disabled_companion(void) {
    const char *output =
        "Result: Bundle[{data=eyJ2ZXJzaW9uIjoxLCJlbmFibGVkIjpmYWxzZSwiZX"
        "Jyb3IiOiJFbmFibGUgYnJpZGdlIn0}]";
    struct vr_companion_snapshot snapshot;
    bool ok = vr_launcher_parse_companion_snapshot(output, &snapshot);

    assert(ok);
    assert(!snapshot.enabled);
    assert(!strcmp("Enable bridge", snapshot.error));
}

static void
test_parse_companion_action(void) {
    bool success = false;
    bool ok = vr_launcher_parse_companion_action_result(
        "Result: Bundle[{data=eyJvayI6dHJ1ZX0}]", &success);
    assert(ok);
    assert(success);
}

int
main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_parse_devices_with_log_prefix();
    test_parse_empty_connection_health();
    test_parse_device_status();
    test_parse_companion_snapshot();
    test_parse_disabled_companion();
    test_parse_companion_action();

    return 0;
}
