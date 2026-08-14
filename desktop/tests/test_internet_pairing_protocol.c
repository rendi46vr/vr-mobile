#include "internet_pairing_protocol.h"

#include <assert.h>
#include <string.h>

static void
test_server_urls(void) {
    assert(vr_internet_server_url_valid("https://connect.example.com"));
    assert(vr_internet_server_url_valid("http://localhost:8787"));
    assert(vr_internet_server_url_valid("http://127.0.0.1:8787"));
    assert(!vr_internet_server_url_valid("http://example.com"));
    assert(!vr_internet_server_url_valid("https://"));
    assert(!vr_internet_server_url_valid("https://bad host"));
}

static void
test_pairing_codes(void) {
    char output[VR_INTERNET_PAIRING_CODE_DIGITS + 1];
    assert(vr_internet_normalize_pairing_code("12345-67890", output));
    assert(!strcmp(output, "1234567890"));
    assert(vr_internet_normalize_pairing_code("123 456 7890", output));
    assert(!vr_internet_normalize_pairing_code("1234", output));
    assert(!vr_internet_normalize_pairing_code("12345A6789", output));
}

static void
test_responses(void) {
    struct vr_internet_pairing_created created;
    assert(vr_internet_parse_pairing_created(
        "{\"code\":\"1234567890\",\"sessionId\":\"session-1\","
        "\"desktopSecret\":\"secret-1\",\"expiresAt\":123456789}",
        &created));
    assert(!strcmp(created.code, "1234567890"));
    assert(!strcmp(created.session_id, "session-1"));
    assert(created.expires_at == 123456789);

    struct vr_internet_pairing_status status;
    assert(vr_internet_parse_pairing_status("{\"paired\":false}", &status));
    assert(!status.paired);
    assert(vr_internet_parse_pairing_status(
        "{\"paired\":true,\"deviceId\":\"phone-1\","
        "\"deviceName\":\"Xiaomi 14\",\"linkToken\":\"token-1\"}",
        &status));
    assert(status.paired);
    assert(!strcmp(status.device_name, "Xiaomi 14"));
}

static void
test_qr(void) {
    uint8_t modules[VR_INTERNET_QR_SIZE * VR_INTERNET_QR_SIZE];
    assert(vr_internet_pairing_qr("1234567890", modules));
    assert(modules[0]);
    assert(modules[6]);
    assert(modules[6 * VR_INTERNET_QR_SIZE]);
    assert(modules[(VR_INTERNET_QR_SIZE - 8)
                   * VR_INTERNET_QR_SIZE + 8]);
    assert(!vr_internet_pairing_qr("invalid", modules));
}

int
main(void) {
    test_server_urls();
    test_pairing_codes();
    test_responses();
    test_qr();
    return 0;
}
