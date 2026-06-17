#ifndef VR_LAUNCHER_JSON_H
#define VR_LAUNCHER_JSON_H

#include <stdbool.h>
#include <stddef.h>

#define VR_LAUNCHER_MAX_DEVICES 32
#define VR_LAUNCHER_MAX_SERIAL_LEN 256

struct vr_launcher_device_info {
    char serial[VR_LAUNCHER_MAX_SERIAL_LEN];
    char state[64];
    char type[32];
};

struct vr_launcher_device_status {
    char serial[VR_LAUNCHER_MAX_SERIAL_LEN];
    char manufacturer[128];
    char model[128];
    char android_version[64];
    char wifi_ip[128];
    char screen_line[256];
    char battery_level[32];
    char storage_line[256];
};

size_t
vr_launcher_parse_devices(const char *output,
                           struct vr_launcher_device_info *devices,
                           size_t max_devices);

bool
vr_launcher_parse_connection_health(const char *output,
                                    struct vr_launcher_device_info *devices,
                                    size_t max_devices, size_t *device_count,
                                    char *last_wifi_serial,
                                    size_t last_wifi_serial_len);

bool
vr_launcher_parse_device_status(const char *output,
                                 struct vr_launcher_device_status *status);

#endif
