#ifndef VR_LAUNCHER_JSON_H
#define VR_LAUNCHER_JSON_H

#include <stdbool.h>
#include <stddef.h>

#define VR_LAUNCHER_MAX_DEVICES 32
#define VR_LAUNCHER_MAX_SERIAL_LEN 256
#define VR_COMPANION_MAX_FILES 128
#define VR_COMPANION_MAX_NOTIFICATIONS 64

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

struct vr_companion_file {
    char id[32];
    char name[512];
    char path[2048];
    char mime[256];
    long long size;
};

struct vr_companion_notification {
    char key[1024];
    char package_name[256];
    char title[1024];
    char text[4096];
    long long post_time;
    bool can_open;
    int reply_action;
};

struct vr_companion_snapshot {
    bool enabled;
    char error[512];
    struct vr_companion_file files[VR_COMPANION_MAX_FILES];
    size_t file_count;
    struct vr_companion_notification
        notifications[VR_COMPANION_MAX_NOTIFICATIONS];
    size_t notification_count;
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
                                    size_t last_wifi_serial_len,
                                    char *last_tailscale_serial,
                                    size_t last_tailscale_serial_len);

bool
vr_launcher_parse_device_status(const char *output,
                                 struct vr_launcher_device_status *status);

bool
vr_launcher_parse_companion_snapshot(const char *adb_output,
                                     struct vr_companion_snapshot *snapshot);

bool
vr_launcher_parse_companion_action_result(const char *adb_output,
                                          bool *success);

#endif
