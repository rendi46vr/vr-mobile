#include "file_manager_protocol.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
base64_value(unsigned char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

static bool
base64_decode(const char *input, size_t input_len, char *output,
              size_t output_len) {
    size_t out_pos = 0;
    unsigned value = 0;
    unsigned bits = 0;

    for (size_t i = 0; i < input_len; ++i) {
        unsigned char c = (unsigned char) input[i];
        if (c == '=') {
            break;
        }

        int decoded = base64_value(c);
        if (decoded < 0) {
            return false;
        }

        value = (value << 6) | (unsigned) decoded;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (out_pos + 1 >= output_len) {
                return false;
            }
            output[out_pos++] = (char) ((value >> bits) & 0xff);
        }
    }

    output[out_pos] = '\0';
    return out_pos > 0;
}

bool
vr_file_manager_name_is_safe(const char *name) {
    if (!name || !*name || !strcmp(name, ".") || !strcmp(name, "..")) {
        return false;
    }

    for (const unsigned char *p = (const unsigned char *) name; *p; ++p) {
        if (*p == '/' || *p < 0x20 || *p == 0x7f) {
            return false;
        }
    }
    return true;
}

bool
vr_file_manager_path_is_safe(const char *path) {
    static const char root[] = "/sdcard";
    size_t root_len = sizeof(root) - 1;
    if (!path || strncmp(path, root, root_len)
            || (path[root_len] && path[root_len] != '/')) {
        return false;
    }

    if (!path[root_len]) {
        return true;
    }

    const char *component = path + root_len + 1;
    if (!*component) {
        return false;
    }

    while (*component) {
        const char *slash = strchr(component, '/');
        size_t len = slash ? (size_t) (slash - component) : strlen(component);
        if (!len || (len == 1 && component[0] == '.')
                || (len == 2 && component[0] == '.'
                    && component[1] == '.')) {
            return false;
        }
        for (size_t i = 0; i < len; ++i) {
            unsigned char c = (unsigned char) component[i];
            if (c < 0x20 || c == 0x7f) {
                return false;
            }
        }
        if (!slash) {
            break;
        }
        component = slash + 1;
        if (!*component) {
            return false;
        }
    }
    return true;
}

bool
vr_file_manager_join_path(const char *parent, const char *name, char *out,
                          size_t out_len) {
    if (!vr_file_manager_path_is_safe(parent)
            || !vr_file_manager_name_is_safe(name) || !out_len) {
        return false;
    }

    int written = snprintf(out, out_len, "%s/%s", parent, name);
    return written > 0 && (size_t) written < out_len
        && vr_file_manager_path_is_safe(out);
}

static bool
parse_u64(const char *text, uint64_t *value) {
    errno = 0;
    char *end;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (errno || !*text || *end) {
        return false;
    }
    *value = (uint64_t) parsed;
    return true;
}

static bool
parse_i64(const char *text, int64_t *value) {
    errno = 0;
    char *end;
    long long parsed = strtoll(text, &end, 10);
    if (errno || !*text || *end) {
        return false;
    }
    *value = (int64_t) parsed;
    return true;
}

size_t
vr_file_manager_parse_listing(const char *input,
                              struct vr_file_manager_entry *entries,
                              size_t max_entries) {
    if (!input || !entries || !max_entries) {
        return 0;
    }

    size_t count = 0;
    const char *line = input;
    while (*line && count < max_entries) {
        const char *line_end = strchr(line, '\n');
        if (!line_end) {
            line_end = line + strlen(line);
        }

        const char *first = memchr(line, '|', (size_t) (line_end - line));
        const char *second = first
            ? memchr(first + 1, '|', (size_t) (line_end - first - 1))
            : NULL;
        const char *third = second
            ? memchr(second + 1, '|', (size_t) (line_end - second - 1))
            : NULL;

        if (first == line + 1 && second && third) {
            char size_text[32];
            char modified_text[32];
            size_t size_len = (size_t) (second - first - 1);
            size_t modified_len = (size_t) (third - second - 1);
            size_t encoded_len = (size_t) (line_end - third - 1);
            if (encoded_len && third[1 + encoded_len - 1] == '\r') {
                --encoded_len;
            }

            if (size_len < sizeof(size_text)
                    && modified_len < sizeof(modified_text)) {
                memcpy(size_text, first + 1, size_len);
                size_text[size_len] = '\0';
                memcpy(modified_text, second + 1, modified_len);
                modified_text[modified_len] = '\0';

                struct vr_file_manager_entry entry;
                bool type_ok = true;
                switch (*line) {
                    case 'd':
                        entry.type = VR_FILE_MANAGER_ENTRY_DIRECTORY;
                        break;
                    case 'f':
                        entry.type = VR_FILE_MANAGER_ENTRY_FILE;
                        break;
                    case 'l':
                        entry.type = VR_FILE_MANAGER_ENTRY_SYMLINK;
                        break;
                    default:
                        type_ok = false;
                        break;
                }

                if (type_ok && parse_u64(size_text, &entry.size)
                        && parse_i64(modified_text, &entry.modified)
                        && base64_decode(third + 1, encoded_len, entry.name,
                                         sizeof(entry.name))
                        && vr_file_manager_name_is_safe(entry.name)) {
                    entries[count++] = entry;
                }
            }
        }

        line = *line_end ? line_end + 1 : line_end;
    }
    return count;
}
