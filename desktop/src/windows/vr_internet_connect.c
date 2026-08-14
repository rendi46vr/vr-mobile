#define UNICODE
#define _UNICODE

#include "vr_internet_connect.h"

#include "../internet_pairing_protocol.h"
#include "vr_theme.h"

#include <bcrypt.h>
#include <commctrl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wincrypt.h>
#include <winhttp.h>

#define ID_INTERNET_SERVER 6101
#define ID_INTERNET_CREATE 6102
#define ID_INTERNET_TEST 6103
#define ID_INTERNET_FORGET 6104
#define ID_INTERNET_STATUS 6105
#define ID_INTERNET_CODE 6106
#define ID_INTERNET_QR 6107
#define ID_INTERNET_HEADING 6108
#define ID_INTERNET_SUBHEADING 6109
#define ID_INTERNET_SERVER_LABEL 6110
#define ID_INTERNET_INSTRUCTIONS 6111
#define ID_INTERNET_PREVIEW 6112

#define ID_INTERNET_TIMER 6120
#define WM_INTERNET_RESULT (WM_APP + 60)

#define INTERNET_OPERATION_CREATE 1
#define INTERNET_OPERATION_POLL 2
#define INTERNET_OPERATION_PRESENCE 3

#define REGISTRY_PATH L"Software\\VRMobile\\Internet"
#define REG_SERVER L"ServerUrl"
#define REG_DESKTOP_ID L"DesktopId"
#define REG_DEVICE_ID L"DeviceId"
#define REG_DEVICE_NAME L"DeviceName"
#define REG_LINK_TOKEN L"LinkToken"

struct internet_state {
    HINSTANCE instance;
    HWND owner;
    HWND window;
    HWND server_edit;
    HWND status;
    HWND code;
    HWND qr;
    HFONT title_font;
    HFONT code_font;
    HFONT ui_font;
    bool busy;
    bool qr_ready;
    uint8_t qr_modules[VR_INTERNET_QR_SIZE * VR_INTERNET_QR_SIZE];
    char server_url[VR_INTERNET_MAX_SERVER_URL];
    char desktop_id[VR_INTERNET_MAX_ID];
    char session_id[VR_INTERNET_MAX_ID];
    char desktop_secret[VR_INTERNET_MAX_TOKEN];
    char device_id[VR_INTERNET_MAX_ID];
    char device_name[VR_INTERNET_MAX_DEVICE_NAME];
    char link_token[VR_INTERNET_MAX_TOKEN];
};

struct internet_runner {
    int operation;
    HWND window;
    char server_url[VR_INTERNET_MAX_SERVER_URL];
    char desktop_id[VR_INTERNET_MAX_ID];
    char session_id[VR_INTERNET_MAX_ID];
    char desktop_secret[VR_INTERNET_MAX_TOKEN];
    char link_token[VR_INTERNET_MAX_TOKEN];
};

struct internet_result {
    int operation;
    bool ok;
    char error[256];
    char response[65536];
};

static struct internet_state internet;

static bool
wide_to_utf8(const WCHAR *wide, char *utf8, size_t utf8_len) {
    if (!wide || !utf8 || !utf8_len) {
        return false;
    }
    int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, NULL, 0, NULL, NULL);
    return required > 0 && (size_t) required <= utf8_len
        && WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, utf8,
            (int) utf8_len, NULL, NULL) > 0;
}

static bool
utf8_to_wide(const char *utf8, WCHAR *wide, size_t wide_len) {
    if (!utf8 || !wide || !wide_len) {
        return false;
    }
    int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, NULL, 0);
    return required > 0 && (size_t) required <= wide_len
        && MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, wide,
            (int) wide_len) > 0;
}

static void
set_status(const WCHAR *text) {
    HWND status = internet.window
                ? GetDlgItem(internet.window, ID_INTERNET_STATUS) : NULL;
    if (status) {
        SetWindowTextW(status, text);
    }
}

static bool
registry_write_string(const WCHAR *name, const char *value) {
    WCHAR wide[VR_INTERNET_MAX_SERVER_URL];
    if (!utf8_to_wide(value, wide, sizeof(wide) / sizeof(wide[0]))) {
        return false;
    }
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REGISTRY_PATH, 0, NULL, 0,
                        KEY_WRITE, NULL, &key, NULL) != ERROR_SUCCESS) {
        return false;
    }
    LONG result = RegSetValueExW(
        key, name, 0, REG_SZ, (const BYTE *) wide,
        (DWORD) ((wcslen(wide) + 1) * sizeof(*wide)));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

static bool
registry_read_string(const WCHAR *name, char *value, size_t value_len) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REGISTRY_PATH, 0, KEY_READ, &key)
            != ERROR_SUCCESS) {
        return false;
    }
    WCHAR wide[VR_INTERNET_MAX_SERVER_URL];
    DWORD type = 0;
    DWORD size = sizeof(wide);
    LONG result = RegQueryValueExW(
        key, name, NULL, &type, (BYTE *) wide, &size);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS || type != REG_SZ
            || size < sizeof(WCHAR) || size > sizeof(wide)) {
        return false;
    }
    wide[(sizeof(wide) / sizeof(wide[0])) - 1] = L'\0';
    return wide_to_utf8(wide, value, value_len);
}

static bool
registry_write_token(const char *token) {
    DATA_BLOB clear = {
        .cbData = (DWORD) (strlen(token) + 1),
        .pbData = (BYTE *) token,
    };
    DATA_BLOB protected = {0};
    if (!CryptProtectData(&clear, L"VR Mobile Internet Link", NULL, NULL, NULL,
                          CRYPTPROTECT_UI_FORBIDDEN, &protected)) {
        return false;
    }
    HKEY key;
    bool ok = RegCreateKeyExW(
        HKEY_CURRENT_USER, REGISTRY_PATH, 0, NULL, 0, KEY_WRITE,
        NULL, &key, NULL) == ERROR_SUCCESS;
    if (ok) {
        ok = RegSetValueExW(
            key, REG_LINK_TOKEN, 0, REG_BINARY,
            protected.pbData, protected.cbData) == ERROR_SUCCESS;
        RegCloseKey(key);
    }
    LocalFree(protected.pbData);
    return ok;
}

static bool
registry_read_token(char *token, size_t token_len) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REGISTRY_PATH, 0, KEY_READ, &key)
            != ERROR_SUCCESS) {
        return false;
    }
    BYTE encrypted[1024];
    DWORD type = 0;
    DWORD size = sizeof(encrypted);
    LONG result = RegQueryValueExW(
        key, REG_LINK_TOKEN, NULL, &type, encrypted, &size);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS || type != REG_BINARY || !size) {
        return false;
    }
    DATA_BLOB protected = {.cbData = size, .pbData = encrypted};
    DATA_BLOB clear = {0};
    if (!CryptUnprotectData(&protected, NULL, NULL, NULL, NULL,
                            CRYPTPROTECT_UI_FORBIDDEN, &clear)) {
        return false;
    }
    bool ok = clear.cbData > 0 && clear.cbData <= token_len
           && ((char *) clear.pbData)[clear.cbData - 1] == '\0';
    if (ok) {
        memcpy(token, clear.pbData, clear.cbData);
    }
    LocalFree(clear.pbData);
    return ok;
}

static void
registry_forget_link(void) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REGISTRY_PATH, 0, KEY_SET_VALUE, &key)
            == ERROR_SUCCESS) {
        RegDeleteValueW(key, REG_DEVICE_ID);
        RegDeleteValueW(key, REG_DEVICE_NAME);
        RegDeleteValueW(key, REG_LINK_TOKEN);
        RegCloseKey(key);
    }
}

static bool
random_desktop_id(char output[VR_INTERNET_MAX_ID]) {
    uint8_t random[16];
    if (BCryptGenRandom(NULL, random, sizeof(random),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        return false;
    }
    snprintf(output, VR_INTERNET_MAX_ID,
             "desktop-%02x%02x%02x%02x-%02x%02x-%02x%02x-"
             "%02x%02x-%02x%02x%02x%02x%02x%02x",
             random[0], random[1], random[2], random[3],
             random[4], random[5], random[6], random[7],
             random[8], random[9], random[10], random[11],
             random[12], random[13], random[14], random[15]);
    return true;
}

static bool
load_identity(void) {
    if (!registry_read_string(
            REG_DESKTOP_ID, internet.desktop_id,
            sizeof(internet.desktop_id))) {
        if (!random_desktop_id(internet.desktop_id)
                || !registry_write_string(
                    REG_DESKTOP_ID, internet.desktop_id)) {
            return false;
        }
    }
    registry_read_string(REG_SERVER, internet.server_url,
                         sizeof(internet.server_url));
    bool trusted =
        registry_read_string(REG_DEVICE_ID, internet.device_id,
                             sizeof(internet.device_id))
        && registry_read_string(REG_DEVICE_NAME, internet.device_name,
                                sizeof(internet.device_name))
        && registry_read_token(internet.link_token,
                               sizeof(internet.link_token));
    if (!trusted) {
        internet.device_id[0] = '\0';
        internet.device_name[0] = '\0';
        internet.link_token[0] = '\0';
    }
    return true;
}

static bool
http_request(const char *method, const char *url, const char *bearer,
             const char *body, char *response, size_t response_len,
             char *error, size_t error_len) {
    WCHAR wide_url[1024];
    if (!utf8_to_wide(url, wide_url,
                      sizeof(wide_url) / sizeof(wide_url[0]))) {
        snprintf(error, error_len, "Invalid server URL");
        return false;
    }
    URL_COMPONENTSW parts;
    ZeroMemory(&parts, sizeof(parts));
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = (DWORD) -1;
    parts.dwUrlPathLength = (DWORD) -1;
    parts.dwExtraInfoLength = (DWORD) -1;
    if (!WinHttpCrackUrl(wide_url, 0, 0, &parts)) {
        snprintf(error, error_len, "Could not parse signaling URL");
        return false;
    }

    WCHAR host[256];
    if (parts.dwHostNameLength >= sizeof(host) / sizeof(host[0])) {
        return false;
    }
    memcpy(host, parts.lpszHostName,
           parts.dwHostNameLength * sizeof(*host));
    host[parts.dwHostNameLength] = L'\0';

    WCHAR path[768];
    size_t path_len = parts.dwUrlPathLength + parts.dwExtraInfoLength;
    if (path_len >= sizeof(path) / sizeof(path[0])) {
        return false;
    }
    memcpy(path, parts.lpszUrlPath,
           parts.dwUrlPathLength * sizeof(*path));
    if (parts.dwExtraInfoLength) {
        memcpy(path + parts.dwUrlPathLength, parts.lpszExtraInfo,
               parts.dwExtraInfoLength * sizeof(*path));
    }
    path[path_len] = L'\0';

    bool local_host = !lstrcmpiW(host, L"localhost")
                   || !lstrcmpW(host, L"127.0.0.1")
                   || !lstrcmpW(host, L"::1");
    HINTERNET session = WinHttpOpen(
        L"VR-Mobile-Desktop/0.3",
        local_host ? WINHTTP_ACCESS_TYPE_NO_PROXY
                   : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET connection =
        session ? WinHttpConnect(session, host, parts.nPort, 0) : NULL;
    WCHAR wide_method[16];
    utf8_to_wide(method, wide_method,
                 sizeof(wide_method) / sizeof(wide_method[0]));
    DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS
                ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = connection ? WinHttpOpenRequest(
        connection, wide_method, path, NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, flags) : NULL;
    bool ok = request != NULL;
    if (ok) {
        WinHttpSetTimeouts(request, 12000, 12000, 12000, 12000);
        WCHAR headers[768] = L"Accept: application/json\r\n";
        if (body) {
            wcscat(headers, L"Content-Type: application/json\r\n");
        }
        if (bearer && *bearer) {
            WCHAR auth[400];
            if (!utf8_to_wide(bearer, auth,
                              sizeof(auth) / sizeof(auth[0]))) {
                ok = false;
            } else {
                wcscat(headers, L"Authorization: Bearer ");
                wcscat(headers, auth);
                wcscat(headers, L"\r\n");
            }
        }
        DWORD body_len = body ? (DWORD) strlen(body) : 0;
        ok = ok && WinHttpSendRequest(
            request, headers, (DWORD) -1,
            body ? (LPVOID) body : WINHTTP_NO_REQUEST_DATA,
            body_len, body_len, 0)
          && WinHttpReceiveResponse(request, NULL);
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (ok) {
        ok = WinHttpQueryHeaders(
            request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
            WINHTTP_NO_HEADER_INDEX);
    }
    size_t used = 0;
    while (ok && used + 1 < response_len) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            ok = false;
            break;
        }
        if (!available) {
            break;
        }
        if (available > response_len - used - 1) {
            snprintf(error, error_len, "Server response is too large");
            ok = false;
            break;
        }
        DWORD read = 0;
        if (!WinHttpReadData(
                request, response + used, available, &read)) {
            ok = false;
            break;
        }
        used += read;
    }
    response[used] = '\0';
    if (ok && (status < 200 || status >= 300)) {
        snprintf(error, error_len, "Signaling server returned HTTP %lu",
                 (unsigned long) status);
        ok = false;
    } else if (!ok && !*error) {
        snprintf(error, error_len, "Could not reach the signaling server");
    }
    if (request) {
        WinHttpCloseHandle(request);
    }
    if (connection) {
        WinHttpCloseHandle(connection);
    }
    if (session) {
        WinHttpCloseHandle(session);
    }
    return ok;
}

static bool
join_url(const char *server, const char *path, char *out, size_t out_len) {
    size_t server_len = strlen(server);
    while (server_len && server[server_len - 1] == '/') {
        --server_len;
    }
    return snprintf(out, out_len, "%.*s%s", (int) server_len, server, path)
        > 0;
}

static DWORD WINAPI
internet_worker(LPVOID userdata) {
    struct internet_runner *runner = userdata;
    struct internet_result *result =
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*result));
    if (!result) {
        HeapFree(GetProcessHeap(), 0, runner);
        return 1;
    }
    result->operation = runner->operation;
    char url[1024];
    char body[1024];
    const char *method = "POST";
    const char *bearer = NULL;
    const char *request_body = NULL;
    switch (runner->operation) {
        case INTERNET_OPERATION_CREATE:
            join_url(runner->server_url, "/v1/pair/create",
                     url, sizeof(url));
            {
                WCHAR computer[MAX_COMPUTERNAME_LENGTH + 1];
                DWORD length = sizeof(computer) / sizeof(computer[0]);
                char name[256] = "Windows Laptop";
                if (GetComputerNameW(computer, &length)) {
                    wide_to_utf8(computer, name, sizeof(name));
                }
                snprintf(body, sizeof(body),
                         "{\"desktopId\":\"%s\",\"desktopName\":\"%s\"}",
                         runner->desktop_id, name);
                request_body = body;
            }
            break;
        case INTERNET_OPERATION_POLL:
            snprintf(body, sizeof(body),
                     "/v1/pair/status?sessionId=%s", runner->session_id);
            join_url(runner->server_url, body, url, sizeof(url));
            method = "GET";
            bearer = runner->desktop_secret;
            break;
        case INTERNET_OPERATION_PRESENCE:
            join_url(runner->server_url, "/v1/presence", url, sizeof(url));
            snprintf(body, sizeof(body),
                     "{\"role\":\"desktop\",\"peerId\":\"%s\"}",
                     runner->desktop_id);
            request_body = body;
            bearer = runner->link_token;
            break;
        default:
            snprintf(result->error, sizeof(result->error),
                     "Unknown Internet operation");
            PostMessageW(runner->window, WM_INTERNET_RESULT, 0,
                         (LPARAM) result);
            HeapFree(GetProcessHeap(), 0, runner);
            return 0;
    }
    result->ok = http_request(
        method, url, bearer, request_body, result->response,
        sizeof(result->response), result->error, sizeof(result->error));
    PostMessageW(runner->window, WM_INTERNET_RESULT, 0, (LPARAM) result);
    HeapFree(GetProcessHeap(), 0, runner);
    return 0;
}

static bool
start_operation(int operation) {
    if (internet.busy) {
        return false;
    }
    struct internet_runner *runner =
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*runner));
    if (!runner) {
        return false;
    }
    runner->operation = operation;
    runner->window = internet.window;
    snprintf(runner->server_url, sizeof(runner->server_url), "%s",
             internet.server_url);
    snprintf(runner->desktop_id, sizeof(runner->desktop_id), "%s",
             internet.desktop_id);
    snprintf(runner->session_id, sizeof(runner->session_id), "%s",
             internet.session_id);
    snprintf(runner->desktop_secret, sizeof(runner->desktop_secret), "%s",
             internet.desktop_secret);
    snprintf(runner->link_token, sizeof(runner->link_token), "%s",
             internet.link_token);
    internet.busy = true;
    EnableWindow(GetDlgItem(internet.window, ID_INTERNET_CREATE), FALSE);
    HANDLE thread = CreateThread(NULL, 0, internet_worker, runner, 0, NULL);
    if (!thread) {
        internet.busy = false;
        EnableWindow(GetDlgItem(internet.window, ID_INTERNET_CREATE), TRUE);
        HeapFree(GetProcessHeap(), 0, runner);
        return false;
    }
    CloseHandle(thread);
    return true;
}

static void
cache_server_url(HWND server_edit) {
    WCHAR wide[VR_INTERNET_MAX_SERVER_URL] = L"";
    LRESULT copied = SendMessageW(
        server_edit, WM_GETTEXT,
        sizeof(wide) / sizeof(wide[0]), (LPARAM) wide);
    if (copied > 0) {
        wide_to_utf8(wide, internet.server_url,
                     sizeof(internet.server_url));
    }
}

static void
create_pairing(HWND hwnd) {
    internet.window = hwnd;
    internet.busy = false;
    if (!internet.server_url[0]) {
        set_status(L"The signaling URL contains unsupported characters.");
        return;
    }
    if (!vr_internet_server_url_valid(internet.server_url)) {
        WCHAR debug_url[VR_INTERNET_MAX_SERVER_URL];
        WCHAR message[640];
        if (utf8_to_wide(internet.server_url, debug_url,
                         sizeof(debug_url) / sizeof(debug_url[0]))) {
            swprintf(message, sizeof(message) / sizeof(message[0]),
                     L"Invalid signaling URL: [%ls]", debug_url);
            set_status(message);
        } else {
            set_status(L"Use an HTTPS signaling URL. HTTP is allowed only for localhost.");
        }
        return;
    }
    registry_write_string(REG_SERVER, internet.server_url);
    internet.session_id[0] = '\0';
    internet.desktop_secret[0] = '\0';
    internet.qr_ready = false;
    InvalidateRect(GetDlgItem(hwnd, ID_INTERNET_QR), NULL, TRUE);
    SetWindowTextW(GetDlgItem(hwnd, ID_INTERNET_CODE), L"\x2014");
    set_status(L"Creating a five-minute pairing invitation...");
    if (!start_operation(INTERNET_OPERATION_CREATE)) {
        set_status(L"Could not start the pairing request.");
    }
}

static void
forget_link(void) {
    registry_forget_link();
    internet.device_id[0] = '\0';
    internet.device_name[0] = '\0';
    internet.link_token[0] = '\0';
    set_status(L"Trusted phone removed. Existing USB, Wi-Fi, and Tailscale settings were not changed.");
}

static void
open_standard_preview(void) {
    if (!internet.link_token[0]) {
        set_status(L"Pair a phone before opening Standard Remote Preview.");
        return;
    }
    size_t server_len = strlen(internet.server_url);
    while (server_len && internet.server_url[server_len - 1] == '/') {
        --server_len;
    }
    char url[1280];
    int length = snprintf(
        url, sizeof(url), "%.*s/viewer#token=%s&peerId=%s",
        (int) server_len, internet.server_url, internet.link_token,
        internet.desktop_id);
    WCHAR wide_url[1280];
    if (length <= 0 || (size_t) length >= sizeof(url)
            || !utf8_to_wide(
                url, wide_url, sizeof(wide_url) / sizeof(wide_url[0]))) {
        set_status(L"Could not prepare the Standard Remote viewer URL.");
        return;
    }
    HINSTANCE launched = ShellExecuteW(
        internet.window, L"open", wide_url, NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR) launched <= 32) {
        set_status(L"Could not open Standard Remote Preview in the browser.");
        return;
    }
    set_status(L"Standard Remote Preview opened. Start screen sharing on the phone.");
}

static void
paint_qr(HWND hwnd) {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(hwnd, &paint);
    RECT rect;
    GetClientRect(hwnd, &rect);
    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(dc, &rect, white);
    DeleteObject(white);
    if (internet.qr_ready) {
        int total = VR_INTERNET_QR_SIZE + 8;
        int scale_x = (rect.right - rect.left) / total;
        int scale_y = (rect.bottom - rect.top) / total;
        int scale = scale_x < scale_y ? scale_x : scale_y;
        if (scale < 1) {
            scale = 1;
        }
        int rendered = total * scale;
        int origin_x = (rect.right - rendered) / 2 + 4 * scale;
        int origin_y = (rect.bottom - rendered) / 2 + 4 * scale;
        HBRUSH black = CreateSolidBrush(RGB(8, 15, 30));
        for (int y = 0; y < VR_INTERNET_QR_SIZE; ++y) {
            for (int x = 0; x < VR_INTERNET_QR_SIZE; ++x) {
                if (!internet.qr_modules[
                        y * VR_INTERNET_QR_SIZE + x]) {
                    continue;
                }
                RECT module = {
                    origin_x + x * scale,
                    origin_y + y * scale,
                    origin_x + (x + 1) * scale,
                    origin_y + (y + 1) * scale,
                };
                FillRect(dc, &module, black);
            }
        }
        DeleteObject(black);
    }
    EndPaint(hwnd, &paint);
}

static LRESULT CALLBACK
qr_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    (void) wparam;
    (void) lparam;
    if (msg == WM_PAINT) {
        paint_qr(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static void
resize_controls(HWND hwnd) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    int width = rect.right;
    const int margin = 24;
    const int gap = 12;
    const int button_h = 38;
    int y = margin;
    MoveWindow(GetDlgItem(hwnd, ID_INTERNET_HEADING), margin, y,
               width - 2 * margin, 36, TRUE);
    y += 36;
    MoveWindow(GetDlgItem(hwnd, ID_INTERNET_SUBHEADING), margin, y,
               width - 2 * margin, 42, TRUE);
    y += 48;
    MoveWindow(GetDlgItem(hwnd, ID_INTERNET_SERVER_LABEL), margin, y,
               120, button_h, TRUE);
    MoveWindow(GetDlgItem(hwnd, ID_INTERNET_SERVER), margin + 120, y,
               width - 2 * margin - 120 - 160 - gap, button_h, TRUE);
    MoveWindow(GetDlgItem(hwnd, ID_INTERNET_CREATE),
               width - margin - 160, y, 160, button_h, TRUE);
    y += button_h + gap;

    int qr_size = 250;
    MoveWindow(GetDlgItem(hwnd, ID_INTERNET_QR),
               margin, y, qr_size, qr_size, TRUE);
    int right_x = margin + qr_size + 24;
    int right_w = width - right_x - margin;
    MoveWindow(GetDlgItem(hwnd, ID_INTERNET_INSTRUCTIONS),
               right_x, y, right_w, 72, TRUE);
    MoveWindow(GetDlgItem(hwnd, ID_INTERNET_CODE),
               right_x, y + 80, right_w, 48, TRUE);
    MoveWindow(GetDlgItem(hwnd, ID_INTERNET_STATUS),
                right_x, y + 138, right_w, 68, TRUE);
    MoveWindow(GetDlgItem(hwnd, ID_INTERNET_PREVIEW), right_x,
                y + 212, 150, button_h, TRUE);
    MoveWindow(GetDlgItem(hwnd, ID_INTERNET_TEST), right_x + 158,
                y + 212, 130, button_h, TRUE);
    MoveWindow(GetDlgItem(hwnd, ID_INTERNET_FORGET), right_x + 296,
                y + 212, 150, button_h, TRUE);
}

static enum vr_theme_icon
button_icon(int id) {
    switch (id) {
        case ID_INTERNET_CREATE:
            return VR_THEME_ICON_PHONE;
        case ID_INTERNET_TEST:
            return VR_THEME_ICON_REFRESH;
        case ID_INTERNET_FORGET:
            return VR_THEME_ICON_DELETE;
        case ID_INTERNET_PREVIEW:
            return VR_THEME_ICON_PHONE;
        default:
            return VR_THEME_ICON_NONE;
    }
}

static void
handle_result(struct internet_result *result) {
    internet.busy = false;
    EnableWindow(GetDlgItem(internet.window, ID_INTERNET_CREATE), TRUE);
    if (!result->ok) {
        WCHAR error[512];
        if (utf8_to_wide(result->error, error,
                         sizeof(error) / sizeof(error[0]))) {
            WCHAR message[640];
            swprintf(message, sizeof(message) / sizeof(message[0]),
                     L"Internet link error: %ls", error);
            set_status(message);
        } else {
            set_status(L"Internet link request failed.");
        }
        return;
    }

    if (result->operation == INTERNET_OPERATION_CREATE) {
        struct vr_internet_pairing_created created;
        if (!vr_internet_parse_pairing_created(
                result->response, &created)) {
            set_status(L"The signaling server returned an invalid pairing response.");
            return;
        }
        snprintf(internet.session_id, sizeof(internet.session_id), "%s",
                 created.session_id);
        snprintf(internet.desktop_secret, sizeof(internet.desktop_secret),
                 "%s", created.desktop_secret);
        internet.qr_ready = vr_internet_pairing_qr(
            created.code, internet.qr_modules);
        WCHAR code[32];
        swprintf(code, sizeof(code) / sizeof(code[0]),
                 L"%c%c%c%c%c  %c%c%c%c%c",
                 created.code[0], created.code[1], created.code[2],
                 created.code[3], created.code[4], created.code[5],
                 created.code[6], created.code[7], created.code[8],
                 created.code[9]);
        SetWindowTextW(GetDlgItem(internet.window, ID_INTERNET_CODE), code);
        InvalidateRect(GetDlgItem(internet.window, ID_INTERNET_QR),
                       NULL, TRUE);
        set_status(L"Waiting for the phone. Scan the QR or enter the code in Companion.");
        SetTimer(internet.window, ID_INTERNET_TIMER, 2000, NULL);
        return;
    }

    if (result->operation == INTERNET_OPERATION_POLL) {
        struct vr_internet_pairing_status status;
        if (!vr_internet_parse_pairing_status(
                result->response, &status)) {
            set_status(L"The signaling server returned an invalid status.");
            return;
        }
        if (!status.paired) {
            return;
        }
        KillTimer(internet.window, ID_INTERNET_TIMER);
        snprintf(internet.device_id, sizeof(internet.device_id), "%s",
                 status.device_id);
        snprintf(internet.device_name, sizeof(internet.device_name), "%s",
                 status.device_name);
        snprintf(internet.link_token, sizeof(internet.link_token), "%s",
                 status.link_token);
        internet.session_id[0] = '\0';
        internet.desktop_secret[0] = '\0';
        registry_write_string(REG_DEVICE_ID, internet.device_id);
        registry_write_string(REG_DEVICE_NAME, internet.device_name);
        registry_write_token(internet.link_token);
        WCHAR name[VR_INTERNET_MAX_DEVICE_NAME];
        WCHAR message[320];
        utf8_to_wide(internet.device_name, name,
                     sizeof(name) / sizeof(name[0]));
        swprintf(message, sizeof(message) / sizeof(message[0]),
                 L"Paired with %ls. Signaling reconnect is enabled; "
                 L"the remote media tunnel is the next phase.",
                 name);
        set_status(message);
        SetTimer(internet.window, ID_INTERNET_TIMER, 15000, NULL);
        return;
    }

    if (result->operation == INTERNET_OPERATION_PRESENCE) {
        bool online = strstr(result->response, "\"peerOnline\":true") != NULL;
        WCHAR name[VR_INTERNET_MAX_DEVICE_NAME];
        WCHAR message[320];
        utf8_to_wide(internet.device_name, name,
                     sizeof(name) / sizeof(name[0]));
        swprintf(message, sizeof(message) / sizeof(message[0]),
                 online
                    ? L"Internet signaling ready — %ls is online."
                    : L"Paired with %ls. Waiting for the phone to come online.",
                 name);
        set_status(message);
    }
}

static LRESULT CALLBACK
internet_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_CREATE:
            vr_theme_apply_window(hwnd);
            internet.window = hwnd;
            CreateWindowW(L"STATIC", L"Internet Connect",
                          WS_CHILD | WS_VISIBLE | SS_LEFT,
                          0, 0, 0, 0, hwnd,
                          (HMENU) ID_INTERNET_HEADING,
                          internet.instance, NULL);
            CreateWindowW(
                L"STATIC",
                L"Pairing and reconnect beta via QR/device code. USB, Wi-Fi, "
                L"and Tailscale remain unchanged.",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                0, 0, 0, 0, hwnd,
                (HMENU) ID_INTERNET_SUBHEADING, internet.instance, NULL);
            CreateWindowW(L"STATIC", L"Signaling server",
                          WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
                          0, 0, 0, 0, hwnd,
                          (HMENU) ID_INTERNET_SERVER_LABEL,
                          internet.instance, NULL);
            internet.server_edit = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                0, 0, 0, 0, hwnd, (HMENU) ID_INTERNET_SERVER,
                internet.instance, NULL);
            HWND create_button = CreateWindowW(
                L"BUTTON", L"Create Pairing",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_NOTIFY,
                0, 0, 0, 0, hwnd, (HMENU) ID_INTERNET_CREATE,
                internet.instance, NULL);
            (void) create_button;
            internet.qr = CreateWindowW(
                L"VRMobileQrWindow", L"", WS_CHILD | WS_VISIBLE | WS_BORDER,
                0, 0, 0, 0, hwnd, (HMENU) ID_INTERNET_QR,
                internet.instance, NULL);
            CreateWindowW(
                L"STATIC",
                L"On Android Companion, choose Scan pairing QR. "
                L"Device code is available as a fallback:",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                0, 0, 0, 0, hwnd,
                (HMENU) ID_INTERNET_INSTRUCTIONS, internet.instance, NULL);
            internet.code = CreateWindowW(
                L"STATIC", L"\x2014", WS_CHILD | WS_VISIBLE | SS_CENTER,
                0, 0, 0, 0, hwnd, (HMENU) ID_INTERNET_CODE,
                internet.instance, NULL);
            internet.status = CreateWindowW(
                L"STATIC", L"Ready to create a pairing invitation.",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                0, 0, 0, 0, hwnd, (HMENU) ID_INTERNET_STATUS,
                internet.instance, NULL);
            CreateWindowW(L"BUTTON", L"Test Link",
                          WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_NOTIFY,
                          0, 0, 0, 0, hwnd,
                          (HMENU) ID_INTERNET_TEST,
                          internet.instance, NULL);
            CreateWindowW(L"BUTTON", L"Open Preview",
                          WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_NOTIFY,
                          0, 0, 0, 0, hwnd,
                          (HMENU) ID_INTERNET_PREVIEW,
                          internet.instance, NULL);
            CreateWindowW(L"BUTTON", L"Forget Phone",
                          WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_NOTIFY,
                          0, 0, 0, 0, hwnd,
                          (HMENU) ID_INTERNET_FORGET,
                          internet.instance, NULL);

            internet.title_font = CreateFontW(
                -26, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
                L"Segoe UI Variable Display");
            internet.code_font = CreateFontW(
                -28, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_MODERN, L"Consolas");
            internet.ui_font = CreateFontW(
                -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            for (int id = ID_INTERNET_SERVER; id <= ID_INTERNET_INSTRUCTIONS;
                    ++id) {
                HWND control = GetDlgItem(hwnd, id);
                if (control) {
                    SendMessageW(control, WM_SETFONT,
                                 (WPARAM) internet.ui_font, TRUE);
                }
            }
            SendMessageW(GetDlgItem(hwnd, ID_INTERNET_HEADING), WM_SETFONT,
                         (WPARAM) internet.title_font, TRUE);
            SendMessageW(GetDlgItem(hwnd, ID_INTERNET_CODE), WM_SETFONT,
                         (WPARAM) internet.code_font, TRUE);
            vr_theme_apply_edit(GetDlgItem(hwnd, ID_INTERNET_SERVER));
            if (internet.server_url[0]) {
                WCHAR server[VR_INTERNET_MAX_SERVER_URL];
                if (utf8_to_wide(
                        internet.server_url, server,
                        sizeof(server) / sizeof(server[0]))) {
                    SetWindowTextW(GetDlgItem(hwnd, ID_INTERNET_SERVER),
                                   server);
                }
            }
            if (internet.link_token[0]) {
                WCHAR name[VR_INTERNET_MAX_DEVICE_NAME];
                WCHAR text[256];
                utf8_to_wide(internet.device_name, name,
                             sizeof(name) / sizeof(name[0]));
                swprintf(text, sizeof(text) / sizeof(text[0]),
                         L"Trusted phone: %ls. Auto reconnect is enabled.",
                         name);
                set_status(text);
                SetTimer(hwnd, ID_INTERNET_TIMER, 15000, NULL);
            }
            resize_controls(hwnd);
            return 0;

        case WM_SIZE:
            resize_controls(hwnd);
            return 0;

        case WM_COMMAND:
            if (LOWORD(wparam) == ID_INTERNET_SERVER
                    && HIWORD(wparam) == EN_CHANGE) {
                cache_server_url((HWND) lparam);
                return 0;
            }
            switch (LOWORD(wparam)) {
                case ID_INTERNET_CREATE:
                    create_pairing(hwnd);
                    return 0;
                case ID_INTERNET_TEST:
                    if (!internet.link_token[0]) {
                        set_status(L"Pair a phone first.");
                    } else if (!start_operation(INTERNET_OPERATION_PRESENCE)) {
                        set_status(L"Another Internet request is still running.");
                    }
                    return 0;
                case ID_INTERNET_FORGET:
                    forget_link();
                    return 0;
                case ID_INTERNET_PREVIEW:
                    open_standard_preview();
                    return 0;
            }
            break;

        case WM_TIMER:
            if (wparam == ID_INTERNET_TIMER && !internet.busy) {
                if (internet.session_id[0]
                        && internet.desktop_secret[0]) {
                    start_operation(INTERNET_OPERATION_POLL);
                } else if (internet.link_token[0]) {
                    start_operation(INTERNET_OPERATION_PRESENCE);
                }
                return 0;
            }
            break;

        case WM_INTERNET_RESULT:
            {
                struct internet_result *result =
                    (struct internet_result *) lparam;
                handle_result(result);
                HeapFree(GetProcessHeap(), 0, result);
                return 0;
            }

        case WM_DRAWITEM:
            {
                int id = LOWORD(wparam);
                enum vr_theme_button_variant variant =
                    id == ID_INTERNET_CREATE
                        ? VR_THEME_BUTTON_PRIMARY
                  : id == ID_INTERNET_FORGET
                        ? VR_THEME_BUTTON_DANGER
                        : VR_THEME_BUTTON_POSITIVE;
                if (vr_theme_draw_button(
                        (DRAWITEMSTRUCT *) lparam, variant,
                        button_icon(id))) {
                    return TRUE;
                }
            }
            break;

        case WM_ERASEBKGND:
            return vr_theme_erase_background(hwnd, (HDC) wparam);

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
            return vr_theme_control_color(msg, (HDC) wparam, (HWND) lparam);

        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, ID_INTERNET_TIMER);
            internet.window = NULL;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void
vr_internet_connect_show(HINSTANCE instance, HWND owner) {
    internet.instance = instance;
    internet.owner = owner;
    if (!internet.desktop_id[0] && !load_identity()) {
        MessageBoxW(owner, L"Could not initialize the desktop identity.",
                    L"Internet Connect", MB_OK | MB_ICONERROR);
        return;
    }
    if (!internet.window) {
        WNDCLASSW qr_class;
        ZeroMemory(&qr_class, sizeof(qr_class));
        qr_class.lpfnWndProc = qr_window_proc;
        qr_class.hInstance = instance;
        qr_class.hCursor = LoadCursor(NULL, IDC_ARROW);
        qr_class.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
        qr_class.lpszClassName = L"VRMobileQrWindow";
        RegisterClassW(&qr_class);

        WNDCLASSW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = internet_window_proc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon = vr_theme_app_icon(false);
        wc.hbrBackground = vr_theme_background_brush();
        wc.lpszClassName = L"VRMobileInternetWindow";
        if (!RegisterClassW(&wc)
                && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            MessageBoxW(owner, L"Could not register Internet Connect window.",
                        L"VR Mobile", MB_OK | MB_ICONERROR);
            return;
        }
        internet.window = CreateWindowExW(
            0, wc.lpszClassName, L"VR Mobile \x2014 Internet Connect",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 820, 520,
            owner, NULL, instance, NULL);
        if (!internet.window) {
            MessageBoxW(owner, L"Could not open Internet Connect.",
                        L"VR Mobile", MB_OK | MB_ICONERROR);
            return;
        }
        SendMessageW(internet.window, WM_SETICON, ICON_SMALL,
                     (LPARAM) vr_theme_app_icon(true));
    }
    ShowWindow(internet.window, SW_SHOW);
    ShowWindow(internet.window, SW_RESTORE);
    SetForegroundWindow(internet.window);
}
