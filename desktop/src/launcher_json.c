#include "launcher_json.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool
json_copy_string(const char *quote, char *out, size_t out_len) {
    if (*quote != '"') {
        return false;
    }

    ++quote;
    size_t len = 0;
    while (*quote && *quote != '"') {
        char c = *quote++;
        if (c == '\\' && *quote) {
            char esc = *quote++;
            switch (esc) {
                case 'n':
                    c = '\n';
                    break;
                case 'r':
                    c = '\r';
                    break;
                case 't':
                    c = '\t';
                    break;
                case 'b':
                    c = '\b';
                    break;
                case 'f':
                    c = '\f';
                    break;
                default:
                    c = esc;
                    break;
            }
        }

        if (len + 1 < out_len) {
            out[len++] = c;
        }
    }

    if (!*quote) {
        return false;
    }

    out[len] = '\0';
    return true;
}

static bool
json_extract_string(const char *object, const char *key, char *out,
                    size_t out_len) {
    char pattern[64];
    int written = snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    if (written < 0 || (size_t) written >= sizeof(pattern)) {
        return false;
    }

    const char *p = strstr(object, pattern);
    if (!p) {
        return false;
    }

    p += strlen(pattern);
    while (isspace((unsigned char) *p)) {
        ++p;
    }

    if (!strncmp(p, "null", 4)) {
        out[0] = '\0';
        return true;
    }

    return json_copy_string(p, out, out_len);
}

static const char *
find_json_object(const char *output, const char *marker) {
    const char *p = output;
    while ((p = strchr(p, '{'))) {
        if (strstr(p, marker)) {
            return p;
        }
        ++p;
    }
    return NULL;
}

static const char *
first_line_or_empty(const char *s, char *out, size_t out_len) {
    if (!s || !*s) {
        out[0] = '\0';
        return out;
    }

    size_t i = 0;
    while (s[i] && s[i] != '\n' && s[i] != '\r' && i + 1 < out_len) {
        out[i] = s[i];
        ++i;
    }
    out[i] = '\0';
    return out;
}

static bool
extract_battery_level(const char *battery, char *out, size_t out_len) {
    const char *level = strstr(battery, "level:");
    if (!level) {
        out[0] = '\0';
        return false;
    }

    level += strlen("level:");
    while (isspace((unsigned char) *level)) {
        ++level;
    }

    size_t i = 0;
    while (isdigit((unsigned char) level[i]) && i + 2 < out_len) {
        out[i] = level[i];
        ++i;
    }
    out[i++] = '%';
    out[i] = '\0';
    return i > 1;
}

size_t
vr_launcher_parse_devices(const char *output,
                          struct vr_launcher_device_info *devices,
                          size_t max_devices) {
    size_t count;
    if (!vr_launcher_parse_connection_health(output, devices, max_devices,
                                             &count, NULL, 0, NULL, 0)) {
        return 0;
    }

    return count;
}

bool
vr_launcher_parse_connection_health(const char *output,
                                    struct vr_launcher_device_info *devices,
                                    size_t max_devices, size_t *device_count,
                                    char *last_wifi_serial,
                                    size_t last_wifi_serial_len,
                                    char *last_tailscale_serial,
                                    size_t last_tailscale_serial_len) {
    const char *json = find_json_object(output, "\"devices\"");
    if (!json) {
        return false;
    }

    const char *devices_key = strstr(json, "\"devices\":[");
    if (!devices_key) {
        return false;
    }

    const char *p = strchr(devices_key, '[');
    const char *end = p ? strchr(p, ']') : NULL;
    if (!p || !end) {
        return false;
    }

    size_t count = 0;
    while (count < max_devices && (p = strchr(p, '{')) && p < end) {
        const char *obj_end = strchr(p, '}');
        if (!obj_end || obj_end > end) {
            break;
        }

        struct vr_launcher_device_info device = {0};
        if (json_extract_string(p, "serial", device.serial,
                                sizeof(device.serial))) {
            json_extract_string(p, "state", device.state,
                                sizeof(device.state));
            json_extract_string(p, "type", device.type,
                                sizeof(device.type));

            if (!device.state[0]) {
                snprintf(device.state, sizeof(device.state), "unknown");
            }
            if (!device.type[0]) {
                snprintf(device.type, sizeof(device.type), "unknown");
            }

            devices[count++] = device;
        }

        p = obj_end + 1;
    }

    if (device_count) {
        *device_count = count;
    }

    if (last_wifi_serial && last_wifi_serial_len) {
        if (!json_extract_string(json, "last_wifi_serial", last_wifi_serial,
                                 last_wifi_serial_len)) {
            last_wifi_serial[0] = '\0';
        }
    }

    if (last_tailscale_serial && last_tailscale_serial_len) {
        if (!json_extract_string(json, "last_tailscale_serial",
                                 last_tailscale_serial,
                                 last_tailscale_serial_len)) {
            last_tailscale_serial[0] = '\0';
        }
    }

    return true;
}

bool
vr_launcher_parse_device_status(const char *output,
                                struct vr_launcher_device_status *status) {
    const char *json = find_json_object(output, "\"serial\"");
    if (!json) {
        return false;
    }

    char screen[256] = "";
    char battery[4096] = "";
    char storage[4096] = "";

    memset(status, 0, sizeof(*status));
    json_extract_string(json, "serial", status->serial,
                        sizeof(status->serial));
    json_extract_string(json, "manufacturer", status->manufacturer,
                        sizeof(status->manufacturer));
    json_extract_string(json, "model", status->model, sizeof(status->model));
    json_extract_string(json, "android_version", status->android_version,
                        sizeof(status->android_version));
    json_extract_string(json, "wifi_ip", status->wifi_ip,
                        sizeof(status->wifi_ip));
    json_extract_string(json, "screen_size", screen, sizeof(screen));
    json_extract_string(json, "battery", battery, sizeof(battery));
    json_extract_string(json, "storage", storage, sizeof(storage));

    first_line_or_empty(screen, status->screen_line,
                        sizeof(status->screen_line));
    first_line_or_empty(storage, status->storage_line,
                        sizeof(status->storage_line));
    extract_battery_level(battery, status->battery_level,
                          sizeof(status->battery_level));

    return status->serial[0] || status->model[0];
}
