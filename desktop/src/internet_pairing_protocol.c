#include "internet_pairing_protocol.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define QR_DATA_CODEWORDS 19
#define QR_ECC_CODEWORDS 7
#define QR_TOTAL_CODEWORDS (QR_DATA_CODEWORDS + QR_ECC_CODEWORDS)

static bool
starts_with(const char *value, const char *prefix) {
    return !strncmp(value, prefix, strlen(prefix));
}

bool
vr_internet_server_url_valid(const char *url) {
    if (!url || !*url || strlen(url) >= VR_INTERNET_MAX_SERVER_URL) {
        return false;
    }
    for (const char *c = url; *c; ++c) {
        if (isspace((unsigned char) *c) || *c == '"' || *c == '\\') {
            return false;
        }
    }
    if (starts_with(url, "https://")) {
        return strlen(url) > strlen("https://");
    }
    return starts_with(url, "http://localhost")
        || starts_with(url, "http://127.0.0.1")
        || starts_with(url, "http://[::1]");
}

bool
vr_internet_normalize_pairing_code(
        const char *input,
        char output[VR_INTERNET_PAIRING_CODE_DIGITS + 1]) {
    if (!input || !output) {
        return false;
    }
    size_t count = 0;
    for (const char *c = input; *c; ++c) {
        if (*c == ' ' || *c == '-') {
            continue;
        }
        if (!isdigit((unsigned char) *c)
                || count >= VR_INTERNET_PAIRING_CODE_DIGITS) {
            return false;
        }
        output[count++] = *c;
    }
    output[count] = '\0';
    return count == VR_INTERNET_PAIRING_CODE_DIGITS;
}

static const char *
json_value(const char *json, const char *key) {
    if (!json || !key) {
        return NULL;
    }
    char needle[96];
    size_t key_len = strlen(key);
    if (key_len + 4 > sizeof(needle)) {
        return NULL;
    }
    needle[0] = '"';
    memcpy(needle + 1, key, key_len);
    needle[key_len + 1] = '"';
    needle[key_len + 2] = '\0';
    const char *found = strstr(json, needle);
    if (!found) {
        return NULL;
    }
    found += key_len + 2;
    while (*found && isspace((unsigned char) *found)) {
        ++found;
    }
    if (*found++ != ':') {
        return NULL;
    }
    while (*found && isspace((unsigned char) *found)) {
        ++found;
    }
    return found;
}

static bool
json_string(const char *json, const char *key, char *out, size_t out_len) {
    const char *value = json_value(json, key);
    if (!value || *value++ != '"' || !out_len) {
        return false;
    }
    size_t used = 0;
    while (*value && *value != '"') {
        unsigned char c = (unsigned char) *value++;
        if (c == '\\') {
            c = (unsigned char) *value++;
            switch (c) {
                case '"':
                case '\\':
                case '/':
                    break;
                case 'b':
                    c = '\b';
                    break;
                case 'f':
                    c = '\f';
                    break;
                case 'n':
                    c = '\n';
                    break;
                case 'r':
                    c = '\r';
                    break;
                case 't':
                    c = '\t';
                    break;
                default:
                    return false;
            }
        }
        if (used + 1 >= out_len || c < 0x20) {
            return false;
        }
        out[used++] = (char) c;
    }
    if (*value != '"') {
        return false;
    }
    out[used] = '\0';
    return true;
}

static bool
json_bool(const char *json, const char *key, bool *out) {
    const char *value = json_value(json, key);
    if (!value || !out) {
        return false;
    }
    if (!strncmp(value, "true", 4)) {
        *out = true;
        return true;
    }
    if (!strncmp(value, "false", 5)) {
        *out = false;
        return true;
    }
    return false;
}

static bool
json_integer(const char *json, const char *key, long long *out) {
    const char *value = json_value(json, key);
    if (!value || !out) {
        return false;
    }
    char *end;
    long long parsed = strtoll(value, &end, 10);
    if (end == value) {
        return false;
    }
    *out = parsed;
    return true;
}

bool
vr_internet_parse_pairing_created(
        const char *json, struct vr_internet_pairing_created *result) {
    if (!result) {
        return false;
    }
    memset(result, 0, sizeof(*result));
    return json_string(json, "code", result->code, sizeof(result->code))
        && vr_internet_normalize_pairing_code(result->code, result->code)
        && json_string(json, "sessionId", result->session_id,
                       sizeof(result->session_id))
        && json_string(json, "desktopSecret", result->desktop_secret,
                       sizeof(result->desktop_secret))
        && json_integer(json, "expiresAt", &result->expires_at);
}

bool
vr_internet_parse_pairing_status(
        const char *json, struct vr_internet_pairing_status *result) {
    if (!result) {
        return false;
    }
    memset(result, 0, sizeof(*result));
    if (!json_bool(json, "paired", &result->paired)) {
        return false;
    }
    if (!result->paired) {
        return true;
    }
    return json_string(json, "deviceId", result->device_id,
                       sizeof(result->device_id))
        && json_string(json, "deviceName", result->device_name,
                       sizeof(result->device_name))
        && json_string(json, "linkToken", result->link_token,
                       sizeof(result->link_token));
}

static uint8_t
gf_multiply(uint8_t x, uint8_t y) {
    uint8_t product = 0;
    for (int i = 0; i < 8; ++i) {
        product ^= (uint8_t) (-(y & 1) & x);
        bool high = (x & 0x80) != 0;
        x <<= 1;
        if (high) {
            x ^= 0x1D;
        }
        y >>= 1;
    }
    return product;
}

static void
append_bits(uint8_t *data, int *bit_len, unsigned value, int count) {
    for (int i = count - 1; i >= 0; --i) {
        if ((value >> i) & 1) {
            data[*bit_len >> 3] |= (uint8_t) (1 << (7 - (*bit_len & 7)));
        }
        ++*bit_len;
    }
}

static void
set_function(uint8_t *modules, uint8_t *function, int x, int y, bool dark) {
    if (x < 0 || x >= VR_INTERNET_QR_SIZE
            || y < 0 || y >= VR_INTERNET_QR_SIZE) {
        return;
    }
    modules[y * VR_INTERNET_QR_SIZE + x] = dark ? 1 : 0;
    function[y * VR_INTERNET_QR_SIZE + x] = 1;
}

static void
draw_finder(uint8_t *modules, uint8_t *function, int center_x, int center_y) {
    for (int dy = -4; dy <= 4; ++dy) {
        for (int dx = -4; dx <= 4; ++dx) {
            int distance = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
            set_function(modules, function, center_x + dx, center_y + dy,
                         distance != 2 && distance != 4);
        }
    }
}

static void
draw_format(uint8_t *modules, uint8_t *function) {
    int data = 1 << 3; /* Error correction level L, mask pattern 0. */
    int remainder = data;
    for (int i = 0; i < 10; ++i) {
        remainder = (remainder << 1) ^ ((remainder >> 9) * 0x537);
    }
    int bits = ((data << 10) | remainder) ^ 0x5412;

    for (int i = 0; i <= 5; ++i) {
        set_function(modules, function, 8, i, ((bits >> i) & 1) != 0);
    }
    set_function(modules, function, 8, 7, ((bits >> 6) & 1) != 0);
    set_function(modules, function, 8, 8, ((bits >> 7) & 1) != 0);
    set_function(modules, function, 7, 8, ((bits >> 8) & 1) != 0);
    for (int i = 9; i < 15; ++i) {
        set_function(modules, function, 14 - i, 8,
                     ((bits >> i) & 1) != 0);
    }
    for (int i = 0; i < 8; ++i) {
        set_function(modules, function, VR_INTERNET_QR_SIZE - 1 - i, 8,
                     ((bits >> i) & 1) != 0);
    }
    for (int i = 8; i < 15; ++i) {
        set_function(modules, function, 8,
                     VR_INTERNET_QR_SIZE - 15 + i,
                     ((bits >> i) & 1) != 0);
    }
    set_function(modules, function, 8, VR_INTERNET_QR_SIZE - 8, true);
}

bool
vr_internet_pairing_qr(
        const char *code,
        uint8_t modules[VR_INTERNET_QR_SIZE * VR_INTERNET_QR_SIZE]) {
    char normalized[VR_INTERNET_PAIRING_CODE_DIGITS + 1];
    if (!modules
            || !vr_internet_normalize_pairing_code(code, normalized)) {
        return false;
    }

    uint8_t data[QR_DATA_CODEWORDS] = {0};
    int bit_len = 0;
    append_bits(data, &bit_len, 1, 4);
    append_bits(data, &bit_len, VR_INTERNET_PAIRING_CODE_DIGITS, 10);
    for (int i = 0; i < 9; i += 3) {
        unsigned value = (unsigned) (normalized[i] - '0') * 100
                       + (unsigned) (normalized[i + 1] - '0') * 10
                       + (unsigned) (normalized[i + 2] - '0');
        append_bits(data, &bit_len, value, 10);
    }
    append_bits(data, &bit_len, (unsigned) (normalized[9] - '0'), 4);
    int capacity = QR_DATA_CODEWORDS * 8;
    int terminator = capacity - bit_len < 4 ? capacity - bit_len : 4;
    append_bits(data, &bit_len, 0, terminator);
    while (bit_len & 7) {
        append_bits(data, &bit_len, 0, 1);
    }
    for (int i = bit_len / 8; i < QR_DATA_CODEWORDS; ++i) {
        data[i] = (i - bit_len / 8) % 2 ? 0x11 : 0xEC;
    }

    static const uint8_t generator[QR_ECC_CODEWORDS] = {
        87, 229, 146, 149, 238, 102, 21,
    };
    uint8_t remainder[QR_ECC_CODEWORDS] = {0};
    for (int i = 0; i < QR_DATA_CODEWORDS; ++i) {
        uint8_t factor = data[i] ^ remainder[0];
        memmove(remainder, remainder + 1, QR_ECC_CODEWORDS - 1);
        remainder[QR_ECC_CODEWORDS - 1] = 0;
        for (int j = 0; j < QR_ECC_CODEWORDS; ++j) {
            remainder[j] ^= gf_multiply(generator[j], factor);
        }
    }

    uint8_t codewords[QR_TOTAL_CODEWORDS];
    memcpy(codewords, data, sizeof(data));
    memcpy(codewords + sizeof(data), remainder, sizeof(remainder));

    uint8_t function[VR_INTERNET_QR_SIZE * VR_INTERNET_QR_SIZE] = {0};
    memset(modules, 0,
           VR_INTERNET_QR_SIZE * VR_INTERNET_QR_SIZE * sizeof(*modules));
    for (int i = 0; i < VR_INTERNET_QR_SIZE; ++i) {
        set_function(modules, function, 6, i, i % 2 == 0);
        set_function(modules, function, i, 6, i % 2 == 0);
    }
    draw_finder(modules, function, 3, 3);
    draw_finder(modules, function, VR_INTERNET_QR_SIZE - 4, 3);
    draw_finder(modules, function, 3, VR_INTERNET_QR_SIZE - 4);
    draw_format(modules, function);

    int bit_index = 0;
    for (int right = VR_INTERNET_QR_SIZE - 1; right >= 1; right -= 2) {
        if (right == 6) {
            right = 5;
        }
        for (int vertical = 0; vertical < VR_INTERNET_QR_SIZE; ++vertical) {
            bool upward = ((right + 1) & 2) == 0;
            int y = upward ? VR_INTERNET_QR_SIZE - 1 - vertical : vertical;
            for (int offset = 0; offset < 2; ++offset) {
                int x = right - offset;
                int index = y * VR_INTERNET_QR_SIZE + x;
                if (function[index]) {
                    continue;
                }
                bool dark = false;
                if (bit_index < QR_TOTAL_CODEWORDS * 8) {
                    dark = ((codewords[bit_index >> 3]
                             >> (7 - (bit_index & 7))) & 1) != 0;
                    ++bit_index;
                }
                if ((x + y) % 2 == 0) {
                    dark = !dark;
                }
                modules[index] = dark ? 1 : 0;
            }
        }
    }
    return bit_index == QR_TOTAL_CODEWORDS * 8;
}
