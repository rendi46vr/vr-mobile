#include "common.h"

#include <assert.h>
#include <string.h>

#include "connect_manager.h"

static struct sc_adb_device
device(const char *serial, const char *state) {
    struct sc_adb_device d = {
        .serial = (char *) serial,
        .state = (char *) state,
        .model = NULL,
        .selected = false,
    };
    return d;
}

static void
assert_selection(const struct sc_adb_device *devices, size_t count,
                 enum sc_connect_manager_mode mode,
                 const char *last_wifi_serial,
                 enum sc_connect_manager_action action,
                 enum sc_connect_manager_status status,
                 const char *serial) {
    struct sc_connect_manager_result result;
    sc_connect_manager_select(devices, count, mode, last_wifi_serial, &result);

    assert(result.action == action);
    assert(result.status == status);
    if (serial) {
        assert(result.serial);
        assert(!strcmp(result.serial, serial));
    } else {
        assert(!result.serial);
    }
}

static void
test_auto_prefers_usb(void) {
    struct sc_adb_device devices[] = {
        device("0123456789abcdef", "device"),
        device("192.168.1.8:5555", "device"),
    };

    assert_selection(devices, ARRAY_LEN(devices), SC_CONNECT_MANAGER_AUTO,
                     NULL, SC_CONNECT_MANAGER_ACTION_USE_SERIAL,
                     SC_CONNECT_MANAGER_STATUS_USB_CONNECTED,
                     "0123456789abcdef");
}

static void
test_auto_uses_wifi(void) {
    struct sc_adb_device devices[] = {
        device("192.168.1.8:5555", "device"),
    };

    assert_selection(devices, ARRAY_LEN(devices), SC_CONNECT_MANAGER_AUTO,
                     NULL, SC_CONNECT_MANAGER_ACTION_USE_SERIAL,
                     SC_CONNECT_MANAGER_STATUS_WIFI_CONNECTED,
                     "192.168.1.8:5555");
}

static void
test_wifi_switches_usb_to_wifi(void) {
    struct sc_adb_device devices[] = {
        device("0123456789abcdef", "device"),
    };

    assert_selection(devices, ARRAY_LEN(devices), SC_CONNECT_MANAGER_WIFI,
                     NULL, SC_CONNECT_MANAGER_ACTION_SWITCH_USB_TO_WIFI,
                     SC_CONNECT_MANAGER_STATUS_USB_CONNECTED,
                     "0123456789abcdef");
}

static void
test_auto_uses_last_wifi(void) {
    assert_selection(NULL, 0, SC_CONNECT_MANAGER_AUTO, "192.168.1.8:5555",
                     SC_CONNECT_MANAGER_ACTION_CONNECT_LAST_WIFI,
                     SC_CONNECT_MANAGER_STATUS_WIFI_LAST_KNOWN,
                     "192.168.1.8:5555");
}

static void
test_unauthorized(void) {
    struct sc_adb_device devices[] = {
        device("0123456789abcdef", "unauthorized"),
    };

    assert_selection(devices, ARRAY_LEN(devices), SC_CONNECT_MANAGER_AUTO,
                     NULL, SC_CONNECT_MANAGER_ACTION_ERROR,
                     SC_CONNECT_MANAGER_STATUS_ADB_UNAUTHORIZED, NULL);
}

static void
test_offline(void) {
    struct sc_adb_device devices[] = {
        device("0123456789abcdef", "offline"),
    };

    assert_selection(devices, ARRAY_LEN(devices), SC_CONNECT_MANAGER_AUTO,
                     NULL, SC_CONNECT_MANAGER_ACTION_ERROR,
                     SC_CONNECT_MANAGER_STATUS_DEVICE_OFFLINE, NULL);
}

static void
test_multiple_usb(void) {
    struct sc_adb_device devices[] = {
        device("0123456789abcdef", "device"),
        device("fedcba9876543210", "device"),
    };

    assert_selection(devices, ARRAY_LEN(devices), SC_CONNECT_MANAGER_AUTO,
                     NULL, SC_CONNECT_MANAGER_ACTION_ERROR,
                     SC_CONNECT_MANAGER_STATUS_MULTIPLE_DEVICES, NULL);
}

static void
test_usb_mode_ignores_wifi(void) {
    struct sc_adb_device devices[] = {
        device("192.168.1.8:5555", "device"),
    };

    assert_selection(devices, ARRAY_LEN(devices), SC_CONNECT_MANAGER_USB,
                     NULL, SC_CONNECT_MANAGER_ACTION_ERROR,
                     SC_CONNECT_MANAGER_STATUS_NO_DEVICE, NULL);
}

static void
test_status_names(void) {
    assert(!strcmp("USB connected",
                   sc_connect_manager_status_get_name(
                       SC_CONNECT_MANAGER_STATUS_USB_CONNECTED)));
    assert(!strcmp("Wi-Fi connected",
                   sc_connect_manager_status_get_name(
                       SC_CONNECT_MANAGER_STATUS_WIFI_CONNECTED)));
    assert(!strcmp("ADB unauthorized",
                   sc_connect_manager_status_get_name(
                       SC_CONNECT_MANAGER_STATUS_ADB_UNAUTHORIZED)));
    assert(!strcmp("Device offline",
                   sc_connect_manager_status_get_name(
                       SC_CONNECT_MANAGER_STATUS_DEVICE_OFFLINE)));
}

int
main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_auto_prefers_usb();
    test_auto_uses_wifi();
    test_wifi_switches_usb_to_wifi();
    test_auto_uses_last_wifi();
    test_unauthorized();
    test_offline();
    test_multiple_usb();
    test_usb_mode_ignores_wifi();
    test_status_names();
    return 0;
}
