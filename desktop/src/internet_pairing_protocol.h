#ifndef VR_INTERNET_PAIRING_PROTOCOL_H
#define VR_INTERNET_PAIRING_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VR_INTERNET_PAIRING_CODE_DIGITS 10
#define VR_INTERNET_QR_SIZE 21
#define VR_INTERNET_MAX_SERVER_URL 512
#define VR_INTERNET_MAX_ID 128
#define VR_INTERNET_MAX_TOKEN 256
#define VR_INTERNET_MAX_DEVICE_NAME 128

struct vr_internet_pairing_created {
    char code[VR_INTERNET_PAIRING_CODE_DIGITS + 1];
    char session_id[VR_INTERNET_MAX_ID];
    char desktop_secret[VR_INTERNET_MAX_TOKEN];
    long long expires_at;
};

struct vr_internet_pairing_status {
    bool paired;
    char device_id[VR_INTERNET_MAX_ID];
    char device_name[VR_INTERNET_MAX_DEVICE_NAME];
    char link_token[VR_INTERNET_MAX_TOKEN];
};

bool
vr_internet_server_url_valid(const char *url);

bool
vr_internet_normalize_pairing_code(const char *input,
                                   char output[VR_INTERNET_PAIRING_CODE_DIGITS
                                               + 1]);

bool
vr_internet_parse_pairing_created(
    const char *json, struct vr_internet_pairing_created *result);

bool
vr_internet_parse_pairing_status(
    const char *json, struct vr_internet_pairing_status *result);

/*
 * Encode a ten-digit pairing code as a QR version 1-L matrix. Each output
 * byte is 0 or 1, indexed as modules[y * VR_INTERNET_QR_SIZE + x].
 */
bool
vr_internet_pairing_qr(
    const char *code,
    uint8_t modules[VR_INTERNET_QR_SIZE * VR_INTERNET_QR_SIZE]);

#endif
