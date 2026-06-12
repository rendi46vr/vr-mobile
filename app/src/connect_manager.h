#ifndef SC_CONNECT_MANAGER_H
#define SC_CONNECT_MANAGER_H

#include "common.h"

#include <stdbool.h>
#include <stddef.h>

#include "adb/adb_device.h"
#include "options.h"

enum sc_connect_manager_action {
    SC_CONNECT_MANAGER_ACTION_ERROR,
    SC_CONNECT_MANAGER_ACTION_USE_SERIAL,
    SC_CONNECT_MANAGER_ACTION_SWITCH_USB_TO_WIFI,
    SC_CONNECT_MANAGER_ACTION_CONNECT_LAST_WIFI,
};

enum sc_connect_manager_status {
    SC_CONNECT_MANAGER_STATUS_NO_DEVICE,
    SC_CONNECT_MANAGER_STATUS_USB_CONNECTED,
    SC_CONNECT_MANAGER_STATUS_WIFI_CONNECTED,
    SC_CONNECT_MANAGER_STATUS_WIFI_LAST_KNOWN,
    SC_CONNECT_MANAGER_STATUS_ADB_UNAUTHORIZED,
    SC_CONNECT_MANAGER_STATUS_DEVICE_OFFLINE,
    SC_CONNECT_MANAGER_STATUS_MULTIPLE_DEVICES,
};

struct sc_connect_manager_result {
    enum sc_connect_manager_action action;
    enum sc_connect_manager_status status;
    const char *serial;
};

const char *
sc_connect_manager_status_get_name(enum sc_connect_manager_status status);

void
sc_connect_manager_select(const struct sc_adb_device *devices, size_t count,
                          enum sc_connect_manager_mode mode,
                          const char *last_wifi_serial,
                          struct sc_connect_manager_result *result);

char *
sc_connect_manager_load_last_wifi_serial(void);

bool
sc_connect_manager_save_last_wifi_serial(const char *serial);

#endif
