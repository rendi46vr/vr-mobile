#include "connect_manager.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/log.h"
#include "vr_config.h"

#define SC_CONNECT_MANAGER_CONFIG_FILE "connect-manager-last-device"
#define SC_CONNECT_MANAGER_TAILSCALE_CONFIG_FILE \
    "connect-manager-last-tailscale"

const char *
sc_connect_manager_status_get_name(enum sc_connect_manager_status status) {
    switch (status) {
        case SC_CONNECT_MANAGER_STATUS_NO_DEVICE:
            return "No device";
        case SC_CONNECT_MANAGER_STATUS_USB_CONNECTED:
            return "USB connected";
        case SC_CONNECT_MANAGER_STATUS_WIFI_CONNECTED:
            return "Wi-Fi connected";
        case SC_CONNECT_MANAGER_STATUS_WIFI_LAST_KNOWN:
            return "Wi-Fi last known";
        case SC_CONNECT_MANAGER_STATUS_ADB_UNAUTHORIZED:
            return "ADB unauthorized";
        case SC_CONNECT_MANAGER_STATUS_DEVICE_OFFLINE:
            return "Device offline";
        case SC_CONNECT_MANAGER_STATUS_MULTIPLE_DEVICES:
            return "Multiple devices";
        default:
            return "(unknown)";
    }
}

static bool
sc_connect_manager_is_usb(const struct sc_adb_device *device) {
    return sc_adb_device_get_type(device->serial) == SC_ADB_DEVICE_TYPE_USB;
}

static bool
sc_connect_manager_is_wifi(const struct sc_adb_device *device) {
    return sc_adb_device_get_type(device->serial) == SC_ADB_DEVICE_TYPE_TCPIP;
}

static bool
sc_connect_manager_has_state(const struct sc_adb_device *device,
                             const char *state) {
    return !strcmp(device->state, state);
}

typedef bool sc_connect_manager_filter(const struct sc_adb_device *device);

static size_t
sc_connect_manager_count_state(const struct sc_adb_device *devices,
                               size_t count,
                               sc_connect_manager_filter *filter,
                               const char *state,
                               const struct sc_adb_device **first) {
    size_t matched = 0;
    for (size_t i = 0; i < count; ++i) {
        const struct sc_adb_device *device = &devices[i];
        if ((!filter || filter(device))
                && sc_connect_manager_has_state(device, state)) {
            if (first && !matched) {
                *first = device;
            }
            ++matched;
        }
    }

    return matched;
}

static bool
sc_connect_manager_any_state(const struct sc_adb_device *devices,
                             size_t count,
                             sc_connect_manager_filter *filter,
                             const char *state) {
    return sc_connect_manager_count_state(devices, count, filter, state,
                                          NULL) > 0;
}

static void
sc_connect_manager_set_error(struct sc_connect_manager_result *result,
                             enum sc_connect_manager_status status) {
    result->action = SC_CONNECT_MANAGER_ACTION_ERROR;
    result->status = status;
    result->serial = NULL;
}

static void
sc_connect_manager_set_action(struct sc_connect_manager_result *result,
                              enum sc_connect_manager_action action,
                              enum sc_connect_manager_status status,
                              const char *serial) {
    result->action = action;
    result->status = status;
    result->serial = serial;
}

static bool
sc_connect_manager_select_usb(const struct sc_adb_device *devices,
                              size_t count,
                              struct sc_connect_manager_result *result) {
    const struct sc_adb_device *device = NULL;
    size_t connected =
        sc_connect_manager_count_state(devices, count,
                                       sc_connect_manager_is_usb, "device",
                                       &device);
    if (connected == 1) {
        sc_connect_manager_set_action(result,
                                      SC_CONNECT_MANAGER_ACTION_USE_SERIAL,
                                      SC_CONNECT_MANAGER_STATUS_USB_CONNECTED,
                                      device->serial);
        return true;
    }

    if (connected > 1) {
        sc_connect_manager_set_error(result,
                                     SC_CONNECT_MANAGER_STATUS_MULTIPLE_DEVICES);
        return false;
    }

    if (sc_connect_manager_any_state(devices, count, sc_connect_manager_is_usb,
                                     "unauthorized")) {
        sc_connect_manager_set_error(result,
                                     SC_CONNECT_MANAGER_STATUS_ADB_UNAUTHORIZED);
        return false;
    }

    if (sc_connect_manager_any_state(devices, count, sc_connect_manager_is_usb,
                                     "offline")) {
        sc_connect_manager_set_error(result,
                                     SC_CONNECT_MANAGER_STATUS_DEVICE_OFFLINE);
        return false;
    }

    sc_connect_manager_set_error(result, SC_CONNECT_MANAGER_STATUS_NO_DEVICE);
    return false;
}

static bool
sc_connect_manager_select_wifi(const struct sc_adb_device *devices,
                               size_t count,
                               const char *last_wifi_serial,
                               bool allow_usb_switch,
                               struct sc_connect_manager_result *result) {
    const struct sc_adb_device *device = NULL;
    size_t connected =
        sc_connect_manager_count_state(devices, count,
                                       sc_connect_manager_is_wifi, "device",
                                       &device);
    if (connected == 1) {
        sc_connect_manager_set_action(result,
                                      SC_CONNECT_MANAGER_ACTION_USE_SERIAL,
                                      SC_CONNECT_MANAGER_STATUS_WIFI_CONNECTED,
                                      device->serial);
        return true;
    }

    if (connected > 1) {
        sc_connect_manager_set_error(result,
                                     SC_CONNECT_MANAGER_STATUS_MULTIPLE_DEVICES);
        return false;
    }

    if (allow_usb_switch) {
        connected =
            sc_connect_manager_count_state(devices, count,
                                           sc_connect_manager_is_usb, "device",
                                           &device);
        if (connected == 1) {
            sc_connect_manager_set_action(
                result, SC_CONNECT_MANAGER_ACTION_SWITCH_USB_TO_WIFI,
                SC_CONNECT_MANAGER_STATUS_USB_CONNECTED, device->serial);
            return true;
        }

        if (connected > 1) {
            sc_connect_manager_set_error(
                result, SC_CONNECT_MANAGER_STATUS_MULTIPLE_DEVICES);
            return false;
        }
    }

    if (last_wifi_serial && *last_wifi_serial) {
        sc_connect_manager_set_action(
            result, SC_CONNECT_MANAGER_ACTION_CONNECT_LAST_WIFI,
            SC_CONNECT_MANAGER_STATUS_WIFI_LAST_KNOWN, last_wifi_serial);
        return true;
    }

    if (sc_connect_manager_any_state(devices, count, sc_connect_manager_is_wifi,
                                     "unauthorized")
            || sc_connect_manager_any_state(devices, count,
                                            sc_connect_manager_is_usb,
                                            "unauthorized")) {
        sc_connect_manager_set_error(result,
                                     SC_CONNECT_MANAGER_STATUS_ADB_UNAUTHORIZED);
        return false;
    }

    if (sc_connect_manager_any_state(devices, count, sc_connect_manager_is_wifi,
                                     "offline")
            || sc_connect_manager_any_state(devices, count,
                                            sc_connect_manager_is_usb,
                                            "offline")) {
        sc_connect_manager_set_error(result,
                                     SC_CONNECT_MANAGER_STATUS_DEVICE_OFFLINE);
        return false;
    }

    sc_connect_manager_set_error(result, SC_CONNECT_MANAGER_STATUS_NO_DEVICE);
    return false;
}

void
sc_connect_manager_select(const struct sc_adb_device *devices, size_t count,
                          enum sc_connect_manager_mode mode,
                          const char *last_wifi_serial,
                          struct sc_connect_manager_result *result) {
    assert(result);
    assert(mode != SC_CONNECT_MANAGER_DISABLED);

    if (mode == SC_CONNECT_MANAGER_USB) {
        sc_connect_manager_select_usb(devices, count, result);
        return;
    }

    if (mode == SC_CONNECT_MANAGER_WIFI) {
        sc_connect_manager_select_wifi(devices, count, last_wifi_serial, true,
                                       result);
        return;
    }

    assert(mode == SC_CONNECT_MANAGER_AUTO);

    if (sc_connect_manager_select_usb(devices, count, result)) {
        return;
    }

    if (result->status == SC_CONNECT_MANAGER_STATUS_MULTIPLE_DEVICES) {
        return;
    }

    sc_connect_manager_select_wifi(devices, count, last_wifi_serial, false,
                                   result);
}

static void
sc_connect_manager_trim_line(char *s) {
    char *end = s + strlen(s);
    while (end > s && (end[-1] == '\n' || end[-1] == '\r'
            || end[-1] == ' ' || end[-1] == '\t')) {
        --end;
    }
    *end = '\0';
}

static char *
sc_connect_manager_load_serial_from_file(const char *name) {
    char *path = sc_vr_config_get_file(name, false);
    if (!path) {
        return NULL;
    }

    FILE *file = fopen(path, "rb");
    free(path);
    if (!file) {
        return NULL;
    }

    char buf[512];
    char *serial = fgets(buf, sizeof(buf), file);
    fclose(file);
    if (!serial) {
        return NULL;
    }

    sc_connect_manager_trim_line(buf);
    if (!*buf) {
        return NULL;
    }

    return strdup(buf);
}

char *
sc_connect_manager_load_last_wifi_serial(void) {
    return sc_connect_manager_load_serial_from_file(
        SC_CONNECT_MANAGER_CONFIG_FILE);
}

char *
sc_connect_manager_load_last_tailscale_serial(void) {
    return sc_connect_manager_load_serial_from_file(
        SC_CONNECT_MANAGER_TAILSCALE_CONFIG_FILE);
}

static bool
sc_connect_manager_save_serial_to_file(const char *name, const char *serial) {
    if (!serial || !*serial) {
        return false;
    }

    char *path = sc_vr_config_get_file(name, true);
    if (!path) {
        return false;
    }

    FILE *file = fopen(path, "wb");
    free(path);
    if (!file) {
        LOGW("Could not save connect manager device file: %s", name);
        return false;
    }

    bool ok = fprintf(file, "%s\n", serial) > 0;
    ok = !fclose(file) && ok;
    if (!ok) {
        LOGW("Could not save connect manager device file: %s", name);
    }
    return ok;
}

bool
sc_connect_manager_save_last_wifi_serial(const char *serial) {
    if (!serial || sc_adb_device_get_type(serial) != SC_ADB_DEVICE_TYPE_TCPIP) {
        return false;
    }

    return sc_connect_manager_save_serial_to_file(SC_CONNECT_MANAGER_CONFIG_FILE,
                                                 serial);
}

bool
sc_connect_manager_save_last_tailscale_serial(const char *serial) {
    return sc_connect_manager_save_serial_to_file(
        SC_CONNECT_MANAGER_TAILSCALE_CONFIG_FILE, serial);
}
