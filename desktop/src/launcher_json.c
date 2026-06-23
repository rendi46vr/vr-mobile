#include "launcher_json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
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

static bool
json_extract_long_long(const char *object, const char *key, long long *out) {
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

    char *end;
    long long value = strtoll(p, &end, 10);
    if (end == p) {
        return false;
    }
    *out = value;
    return true;
}

static bool
json_extract_bool(const char *object, const char *key, bool *out) {
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
    if (!strncmp(p, "true", 4)) {
        *out = true;
        return true;
    }
    if (!strncmp(p, "false", 5)) {
        *out = false;
        return true;
    }
    return false;
}

static const char *
json_matching_end(const char *start, char open, char close) {
    if (*start != open) {
        return NULL;
    }

    unsigned depth = 0;
    bool quoted = false;
    bool escaped = false;
    for (const char *p = start; *p; ++p) {
        if (quoted) {
            if (escaped) {
                escaped = false;
            } else if (*p == '\\') {
                escaped = true;
            } else if (*p == '"') {
                quoted = false;
            }
            continue;
        }

        if (*p == '"') {
            quoted = true;
        } else if (*p == open) {
            ++depth;
        } else if (*p == close && --depth == 0) {
            return p;
        }
    }
    return NULL;
}

static bool
json_find_array(const char *json, const char *key, const char **begin,
                const char **end) {
    char pattern[64];
    int written = snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    if (written < 0 || (size_t) written >= sizeof(pattern)) {
        return false;
    }

    const char *p = strstr(json, pattern);
    if (!p) {
        return false;
    }
    p += strlen(pattern);
    while (isspace((unsigned char) *p)) {
        ++p;
    }
    const char *array_end = json_matching_end(p, '[', ']');
    if (!array_end) {
        return false;
    }
    *begin = p + 1;
    *end = array_end;
    return true;
}

static int
base64url_value(char value) {
    if (value >= 'A' && value <= 'Z') {
        return value - 'A';
    }
    if (value >= 'a' && value <= 'z') {
        return value - 'a' + 26;
    }
    if (value >= '0' && value <= '9') {
        return value - '0' + 52;
    }
    if (value == '-' || value == '+') {
        return 62;
    }
    if (value == '_' || value == '/') {
        return 63;
    }
    return -1;
}

static char *
decode_bridge_output(const char *output) {
    const char *encoded = strstr(output, "data=");
    if (!encoded) {
        return NULL;
    }
    encoded += strlen("data=");

    const char *end = encoded;
    while (base64url_value(*end) >= 0) {
        ++end;
    }
    size_t encoded_len = (size_t) (end - encoded);
    if (!encoded_len || encoded_len > 4 * 1024 * 1024) {
        return NULL;
    }

    size_t decoded_cap = (encoded_len * 3) / 4 + 4;
    char *decoded = malloc(decoded_cap);
    if (!decoded) {
        return NULL;
    }

    unsigned accumulator = 0;
    unsigned bits = 0;
    size_t decoded_len = 0;
    for (size_t i = 0; i < encoded_len; ++i) {
        int value = base64url_value(encoded[i]);
        accumulator = (accumulator << 6) | (unsigned) value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            decoded[decoded_len++] = (char) (accumulator >> bits);
            accumulator &= (1U << bits) - 1;
        }
    }
    decoded[decoded_len] = '\0';
    return decoded;
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

static void
parse_companion_files(const char *json, struct vr_companion_snapshot *snapshot) {
    const char *p;
    const char *end;
    if (!json_find_array(json, "outbox", &p, &end)) {
        return;
    }

    while (p < end && snapshot->file_count < VR_COMPANION_MAX_FILES) {
        p = strchr(p, '{');
        if (!p || p >= end) {
            break;
        }
        const char *object_end = json_matching_end(p, '{', '}');
        if (!object_end || object_end > end) {
            break;
        }

        struct vr_companion_file *file =
            &snapshot->files[snapshot->file_count];
        long long id = 0;
        json_extract_long_long(p, "id", &id);
        snprintf(file->id, sizeof(file->id), "%lld", id);
        json_extract_string(p, "name", file->name, sizeof(file->name));
        json_extract_string(p, "path", file->path, sizeof(file->path));
        json_extract_string(p, "mime", file->mime, sizeof(file->mime));
        json_extract_long_long(p, "size", &file->size);
        if (file->name[0] && file->path[0]) {
            ++snapshot->file_count;
        }
        p = object_end + 1;
    }
}

static void
parse_companion_notifications(const char *json,
                              struct vr_companion_snapshot *snapshot) {
    const char *p;
    const char *end;
    if (!json_find_array(json, "notifications", &p, &end)) {
        return;
    }

    while (p < end
            && snapshot->notification_count < VR_COMPANION_MAX_NOTIFICATIONS) {
        p = strchr(p, '{');
        if (!p || p >= end) {
            break;
        }
        const char *object_end = json_matching_end(p, '{', '}');
        if (!object_end || object_end > end) {
            break;
        }

        struct vr_companion_notification *notification =
            &snapshot->notifications[snapshot->notification_count];
        long long reply_action = -1;
        json_extract_string(p, "key", notification->key,
                            sizeof(notification->key));
        json_extract_string(p, "package", notification->package_name,
                            sizeof(notification->package_name));
        json_extract_string(p, "title", notification->title,
                            sizeof(notification->title));
        json_extract_string(p, "text", notification->text,
                            sizeof(notification->text));
        json_extract_long_long(p, "post_time", &notification->post_time);
        json_extract_bool(p, "can_open", &notification->can_open);
        json_extract_long_long(p, "reply_action", &reply_action);
        notification->reply_action = (int) reply_action;
        if (notification->key[0]) {
            ++snapshot->notification_count;
        }
        p = object_end + 1;
    }
}

bool
vr_launcher_parse_companion_snapshot(const char *adb_output,
                                     struct vr_companion_snapshot *snapshot) {
    memset(snapshot, 0, sizeof(*snapshot));
    char *json = decode_bridge_output(adb_output);
    if (!json) {
        return false;
    }

    bool enabled = false;
    bool ok = json_extract_bool(json, "enabled", &enabled);
    snapshot->enabled = enabled;
    json_extract_string(json, "error", snapshot->error,
                        sizeof(snapshot->error));
    if (ok && enabled) {
        parse_companion_files(json, snapshot);
        parse_companion_notifications(json, snapshot);
    }
    free(json);
    return ok;
}

bool
vr_launcher_parse_companion_action_result(const char *adb_output,
                                          bool *success) {
    char *json = decode_bridge_output(adb_output);
    if (!json) {
        return false;
    }
    bool ok = json_extract_bool(json, "ok", success);
    free(json);
    return ok;
}
