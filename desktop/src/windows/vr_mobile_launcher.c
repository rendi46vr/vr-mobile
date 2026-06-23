#define UNICODE
#define _UNICODE

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "launcher_commands.h"
#include "launcher_json.h"

#define ID_BUTTON_CONNECT 1001
#define ID_BUTTON_WIRELESS 1002
#define ID_BUTTON_STATUS 1003
#define ID_BUTTON_REFRESH 1004
#define ID_BUTTON_DISCONNECT 1005
#define ID_BUTTON_CLEAR 1006
#define ID_CHECK_AUTOSTART 1007
#define ID_BUTTON_PROFILE_REFRESH 1008
#define ID_BUTTON_PROFILE_SAVE 1009
#define ID_BUTTON_PROFILE_RUN 1010
#define ID_BUTTON_PROFILE_DELETE 1011
#define ID_BUTTON_EXIT 1012
#define ID_BUTTON_TAILSCALE 1013
#define ID_BUTTON_PULL_FILE 1014
#define ID_LOG 1101
#define ID_STATUS 1102
#define ID_DEVICE_LIST 1103
#define ID_DEVICE_DETAIL 1104
#define ID_PROFILE_LIST 1105
#define ID_PROFILE_NAME 1106
#define ID_PROFILE_ARGS 1107
#define ID_FILE_DROP 1108
#define ID_TAILSCALE_ADDR 1109
#define ID_PULL_REMOTE_PATH 1110

#define ID_TRAY_OPEN 2001
#define ID_TRAY_CONNECT 2002
#define ID_TRAY_DISCONNECT 2003
#define ID_TRAY_REFRESH 2004
#define ID_TRAY_STATUS 2005
#define ID_TRAY_WIRELESS 2006
#define ID_TRAY_AUTOSTART 2007
#define ID_TRAY_EXIT 2008
#define ID_TRAY_TAILSCALE 2009

#define WM_VR_LOG (WM_APP + 1)
#define WM_VR_DONE (WM_APP + 2)
#define WM_VR_TRAY (WM_APP + 3)

#define VR_STATUS_DLL_NOT_FOUND 0xC0000135UL

#ifndef MSGFLT_ALLOW
# define MSGFLT_ALLOW 1
#endif

#ifndef WM_COPYGLOBALDATA
# define WM_COPYGLOBALDATA 0x0049
#endif

#define MAX_FILE_QUEUE 64
#define MAX_FILE_PATH_CHARS 32768
#define MAX_PROFILE_NAME_CHARS 128
#define VR_AUTOSTART_VALUE_NAME L"VR Mobile"
#define VR_FILE_DROP_TARGET L"/sdcard/Download/VR Phone Mirror/"

typedef BOOL (WINAPI *change_window_message_filter_ex_fn)(HWND, UINT, DWORD,
                                                          void *);

struct output_buffer {
    char *data;
    size_t len;
    size_t cap;
};

struct command_runner {
    HWND hwnd;
    enum vr_launcher_command command;
    WCHAR scrcpy_path[MAX_PATH];
    char serial[VR_LAUNCHER_MAX_SERIAL_LEN];
    char profile_name[MAX_PROFILE_NAME_CHARS];
    WCHAR file_path[MAX_FILE_PATH_CHARS];
    WCHAR remote_path[MAX_FILE_PATH_CHARS];
    WCHAR tailscale_addr[512];
    bool has_serial;
    bool has_profile;
    bool has_file;
    bool has_tailscale_addr;
    bool use_connect_manager;
    bool mirror;
};

struct command_done {
    DWORD exit_code;
    enum vr_launcher_command command;
    bool mirror;
    char *output;
    size_t output_len;
};

static HINSTANCE app_instance;
static HWND main_window;
static HWND title_label;
static HWND status_label;
static HWND autostart_check;
static HWND tailscale_label;
static HWND tailscale_addr_edit;
static HWND devices_heading;
static HWND detail_heading;
static HWND log_heading;
static HWND profile_heading;
static HWND profile_list;
static HWND profile_name_edit;
static HWND profile_args_edit;
static HWND file_drop_heading;
static HWND file_drop_edit;
static HWND pull_remote_path_edit;
static HWND device_list;
static HWND detail_edit;
static HWND log_edit;
static HFONT title_font;
static HFONT ui_font;
static HFONT mono_font;
static HANDLE mirror_process;
static HANDLE utility_process;
static CRITICAL_SECTION process_lock;
static struct vr_launcher_device_info devices[VR_LAUNCHER_MAX_DEVICES];
static size_t device_count;
static WCHAR queued_files[MAX_FILE_QUEUE][MAX_FILE_PATH_CHARS];
static size_t queued_file_count;
static NOTIFYICONDATAW tray_icon;
static bool tray_added;
static bool exiting;
static WNDPROC file_drop_edit_wndproc;

static void
show_dashboard(void);

static void
enqueue_file(HWND hwnd, const WCHAR *path);

static void
set_status(const WCHAR *text) {
    SetWindowTextW(status_label, text);
}

static void
enable_drag_drop(HWND hwnd) {
    DragAcceptFiles(hwnd, TRUE);

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) {
        return;
    }

    union {
        FARPROC proc;
        change_window_message_filter_ex_fn fn;
    } change_filter = {
        .proc = GetProcAddress(user32, "ChangeWindowMessageFilterEx"),
    };
    if (!change_filter.fn) {
        return;
    }

    change_filter.fn(hwnd, WM_DROPFILES, MSGFLT_ALLOW, NULL);
    change_filter.fn(hwnd, WM_COPYDATA, MSGFLT_ALLOW, NULL);
    change_filter.fn(hwnd, WM_COPYGLOBALDATA, MSGFLT_ALLOW, NULL);
}

static void
disable_drag_drop(HWND hwnd) {
    if (hwnd) {
        DragAcceptFiles(hwnd, FALSE);
    }
}

static void
set_detail(const WCHAR *text) {
    SetWindowTextW(detail_edit, text);
}

static void
append_edit_text(HWND edit, const WCHAR *text) {
    int length = GetWindowTextLengthW(edit);
    SendMessageW(edit, EM_SETSEL, (WPARAM) length, (LPARAM) length);
    SendMessageW(edit, EM_REPLACESEL, FALSE, (LPARAM) text);
}

static void
append_log_text(const WCHAR *text) {
    append_edit_text(log_edit, text);
}

static void
append_log_line(const WCHAR *text) {
    append_log_text(text);
    append_log_text(L"\r\n");
}

static WCHAR *
dup_wide(const WCHAR *text) {
    size_t len = wcslen(text) + 1;
    WCHAR *copy = HeapAlloc(GetProcessHeap(), 0, len * sizeof(*copy));
    if (!copy) {
        return NULL;
    }
    memcpy(copy, text, len * sizeof(*copy));
    return copy;
}

static void
post_log(HWND hwnd, const WCHAR *text) {
    WCHAR *copy = dup_wide(text);
    if (!copy) {
        return;
    }
    PostMessageW(hwnd, WM_VR_LOG, 0, (LPARAM) copy);
}

static void
append_file_transfer_line(const WCHAR *text) {
    append_edit_text(file_drop_edit, text);
    append_edit_text(file_drop_edit, L"\r\n");
}

static bool
output_buffer_append(struct output_buffer *buf, const char *data, size_t len) {
    if (!len) {
        return true;
    }

    if (buf->len + len + 1 > buf->cap) {
        size_t new_cap = buf->cap ? buf->cap : 4096;
        while (new_cap < buf->len + len + 1) {
            new_cap *= 2;
        }

        char *new_data = buf->data
                       ? HeapReAlloc(GetProcessHeap(), 0, buf->data, new_cap)
                       : HeapAlloc(GetProcessHeap(), 0, new_cap);
        if (!new_data) {
            return false;
        }
        buf->data = new_data;
        buf->cap = new_cap;
    }

    memcpy(&buf->data[buf->len], data, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
    return true;
}

static void
post_log_utf8(HWND hwnd, const char *text, int len) {
    if (len <= 0) {
        return;
    }

    int wide_len = MultiByteToWideChar(CP_UTF8, 0, text, len, NULL, 0);
    UINT codepage = CP_UTF8;
    if (wide_len <= 0) {
        codepage = CP_ACP;
        wide_len = MultiByteToWideChar(codepage, 0, text, len, NULL, 0);
    }
    if (wide_len <= 0) {
        return;
    }

    WCHAR *wide = HeapAlloc(GetProcessHeap(), 0,
                            ((size_t) wide_len + 1) * sizeof(*wide));
    if (!wide) {
        return;
    }

    MultiByteToWideChar(codepage, 0, text, len, wide, wide_len);
    wide[wide_len] = L'\0';
    PostMessageW(hwnd, WM_VR_LOG, 0, (LPARAM) wide);
}

static void
path_dirname(WCHAR *path) {
    WCHAR *slash = wcsrchr(path, L'\\');
    if (slash) {
        *slash = L'\0';
    }
}

static bool
file_exists(const WCHAR *path) {
    DWORD attrs = GetFileAttributesW(path);
    return attrs != INVALID_FILE_ATTRIBUTES
        && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static bool
dir_exists(const WCHAR *path) {
    DWORD attrs = GetFileAttributesW(path);
    return attrs != INVALID_FILE_ATTRIBUTES
        && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static bool
get_launcher_path(WCHAR *out, size_t out_len) {
    DWORD len = GetModuleFileNameW(NULL, out, (DWORD) out_len);
    return len && len < out_len;
}

static bool
token_is_elevated(HANDLE token) {
    TOKEN_ELEVATION elevation;
    DWORD size;
    return GetTokenInformation(token, TokenElevation, &elevation,
                               sizeof(elevation), &size)
        && elevation.TokenIsElevated;
}

static bool
process_is_elevated(void) {
    HANDLE token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }

    bool elevated = token_is_elevated(token);
    CloseHandle(token);
    return elevated;
}

static bool
relaunch_with_explorer_token(void) {
    HWND shell_window = GetShellWindow();
    if (!shell_window) {
        return false;
    }

    DWORD shell_pid;
    GetWindowThreadProcessId(shell_window, &shell_pid);
    if (!shell_pid) {
        return false;
    }

    HANDLE shell_process =
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, shell_pid);
    if (!shell_process) {
        return false;
    }

    HANDLE shell_token;
    bool token_opened =
        OpenProcessToken(shell_process,
                         TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_QUERY,
                         &shell_token);
    CloseHandle(shell_process);
    if (!token_opened) {
        return false;
    }

    // Avoid a relaunch loop if Explorer itself is running elevated.
    if (token_is_elevated(shell_token)) {
        CloseHandle(shell_token);
        return false;
    }

    WCHAR executable[MAX_FILE_PATH_CHARS];
    if (!get_launcher_path(executable,
                           sizeof(executable) / sizeof(executable[0]))) {
        CloseHandle(shell_token);
        return false;
    }

    WCHAR command_line[MAX_FILE_PATH_CHARS + 3];
    int written = swprintf(command_line,
                           sizeof(command_line) / sizeof(command_line[0]),
                           L"\"%ls\"", executable);
    if (written <= 0) {
        CloseHandle(shell_token);
        return false;
    }

    WCHAR cwd[MAX_FILE_PATH_CHARS];
    DWORD cwd_len = GetCurrentDirectoryW(
        sizeof(cwd) / sizeof(cwd[0]), cwd);
    const WCHAR *working_directory =
        cwd_len && cwd_len < sizeof(cwd) / sizeof(cwd[0]) ? cwd : NULL;

    STARTUPINFOW startup;
    ZeroMemory(&startup, sizeof(startup));
    startup.cb = sizeof(startup);

    PROCESS_INFORMATION process;
    ZeroMemory(&process, sizeof(process));

    bool launched = CreateProcessWithTokenW(
        shell_token, 0, executable, command_line, CREATE_UNICODE_ENVIRONMENT,
        NULL, working_directory, &startup, &process);
    CloseHandle(shell_token);
    if (!launched) {
        return false;
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

static bool
find_scrcpy_path(WCHAR *out, size_t out_len) {
    WCHAR module_path[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, module_path, MAX_PATH);
    if (!len || len >= MAX_PATH) {
        return false;
    }

    path_dirname(module_path);

    if (swprintf(out, out_len, L"%ls\\scrcpy.exe", module_path) > 0
            && file_exists(out)) {
        return true;
    }

    if (swprintf(out, out_len, L"%ls\\..\\app\\scrcpy.exe", module_path) > 0
            && file_exists(out)) {
        return true;
    }

    WCHAR cwd[MAX_PATH];
    len = GetCurrentDirectoryW(MAX_PATH, cwd);
    if (len && len < MAX_PATH) {
        if (swprintf(out, out_len, L"%ls\\build\\app\\scrcpy.exe", cwd) > 0
                && file_exists(out)) {
            return true;
        }
        if (swprintf(out, out_len, L"%ls\\app\\scrcpy.exe", cwd) > 0
                && file_exists(out)) {
            return true;
        }
    }

    return false;
}

static bool
find_server_path_from_scrcpy(const WCHAR *scrcpy_path, WCHAR *out,
                             size_t out_len) {
    WCHAR scrcpy_dir[MAX_FILE_PATH_CHARS];
    wcsncpy(scrcpy_dir, scrcpy_path,
            sizeof(scrcpy_dir) / sizeof(scrcpy_dir[0]) - 1);
    scrcpy_dir[sizeof(scrcpy_dir) / sizeof(scrcpy_dir[0]) - 1] = L'\0';
    path_dirname(scrcpy_dir);

    if (swprintf(out, out_len, L"%ls\\scrcpy-server", scrcpy_dir) > 0
            && file_exists(out)) {
        return true;
    }

    if (swprintf(out, out_len, L"%ls\\..\\server\\scrcpy-server",
                 scrcpy_dir) > 0 && file_exists(out)) {
        return true;
    }

    WCHAR cwd[MAX_PATH];
    DWORD len = GetCurrentDirectoryW(MAX_PATH, cwd);
    if (len && len < MAX_PATH) {
        if (swprintf(out, out_len, L"%ls\\build\\server\\scrcpy-server",
                     cwd) > 0 && file_exists(out)) {
            return true;
        }
        if (swprintf(out, out_len, L"%ls\\server\\scrcpy-server",
                     cwd) > 0 && file_exists(out)) {
            return true;
        }
    }

    return false;
}

static void
set_server_path_env_if_available(const WCHAR *scrcpy_path) {
    WCHAR current[MAX_FILE_PATH_CHARS];
    DWORD len = GetEnvironmentVariableW(L"SCRCPY_SERVER_PATH", current,
                                        sizeof(current) / sizeof(current[0]));
    if (len && len < sizeof(current) / sizeof(current[0])
            && file_exists(current)) {
        return;
    }

    WCHAR server_path[MAX_FILE_PATH_CHARS];
    if (find_server_path_from_scrcpy(scrcpy_path, server_path,
                                     sizeof(server_path)
                                         / sizeof(server_path[0]))) {
        SetEnvironmentVariableW(L"SCRCPY_SERVER_PATH", server_path);
    }
}

static bool
append_path_dir(WCHAR *buf, size_t buf_len, const WCHAR *dir) {
    if (!dir_exists(dir)) {
        return true;
    }

    size_t used = wcslen(buf);
    size_t dir_len = wcslen(dir);
    size_t extra = dir_len + (used ? 1 : 0);
    if (used + extra + 1 > buf_len) {
        return false;
    }

    if (used) {
        buf[used++] = L';';
    }
    memcpy(&buf[used], dir, (dir_len + 1) * sizeof(*buf));
    return true;
}

static void
append_scrcpy_dir(WCHAR *buf, size_t buf_len, const WCHAR *scrcpy_path) {
    WCHAR dir[MAX_FILE_PATH_CHARS];
    wcsncpy(dir, scrcpy_path, sizeof(dir) / sizeof(dir[0]) - 1);
    dir[sizeof(dir) / sizeof(dir[0]) - 1] = L'\0';
    path_dirname(dir);
    append_path_dir(buf, buf_len, dir);
}

static void
append_platform_tools_dir(WCHAR *buf, size_t buf_len) {
    WCHAR base[MAX_PATH];
    WCHAR path[MAX_PATH];

    DWORD len = GetEnvironmentVariableW(L"ANDROID_HOME", base,
                                        sizeof(base) / sizeof(base[0]));
    if (len && len < sizeof(base) / sizeof(base[0])) {
        swprintf(path, sizeof(path) / sizeof(path[0]),
                 L"%ls\\platform-tools", base);
        append_path_dir(buf, buf_len, path);
    }

    len = GetEnvironmentVariableW(L"ANDROID_SDK_ROOT", base,
                                  sizeof(base) / sizeof(base[0]));
    if (len && len < sizeof(base) / sizeof(base[0])) {
        swprintf(path, sizeof(path) / sizeof(path[0]),
                 L"%ls\\platform-tools", base);
        append_path_dir(buf, buf_len, path);
    }

    len = GetEnvironmentVariableW(L"LOCALAPPDATA", base,
                                  sizeof(base) / sizeof(base[0]));
    if (len && len < sizeof(base) / sizeof(base[0])) {
        swprintf(path, sizeof(path) / sizeof(path[0]),
                 L"%ls\\Android\\Sdk\\platform-tools", base);
        append_path_dir(buf, buf_len, path);
    }
}

static void
set_adb_env_if_available(void) {
    WCHAR adb[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"ADB", adb,
                                        sizeof(adb) / sizeof(adb[0]));
    if (len && len < sizeof(adb) / sizeof(adb[0])) {
        return;
    }

    WCHAR base[MAX_PATH];
    len = GetEnvironmentVariableW(L"LOCALAPPDATA", base,
                                  sizeof(base) / sizeof(base[0]));
    if (!len || len >= sizeof(base) / sizeof(base[0])) {
        return;
    }

    swprintf(adb, sizeof(adb) / sizeof(adb[0]),
             L"%ls\\Android\\Sdk\\platform-tools\\adb.exe", base);
    if (file_exists(adb)) {
        SetEnvironmentVariableW(L"ADB", adb);
    }
}

static void
prepare_child_environment(const WCHAR *scrcpy_path) {
    WCHAR prefix[8192] = L"";
    append_scrcpy_dir(prefix, sizeof(prefix) / sizeof(prefix[0]), scrcpy_path);
    append_path_dir(prefix, sizeof(prefix) / sizeof(prefix[0]),
                    L"C:\\msys64\\mingw64\\bin");
    append_path_dir(prefix, sizeof(prefix) / sizeof(prefix[0]),
                    L"C:\\msys64\\ucrt64\\bin");
    append_path_dir(prefix, sizeof(prefix) / sizeof(prefix[0]),
                    L"C:\\msys64\\usr\\bin");
    append_platform_tools_dir(prefix, sizeof(prefix) / sizeof(prefix[0]));

    WCHAR current[MAX_FILE_PATH_CHARS] = L"";
    GetEnvironmentVariableW(L"PATH", current,
                            sizeof(current) / sizeof(current[0]));

    WCHAR merged[MAX_FILE_PATH_CHARS];
    if (prefix[0] && current[0]) {
        swprintf(merged, sizeof(merged) / sizeof(merged[0]), L"%ls;%ls",
                 prefix, current);
    } else if (prefix[0]) {
        swprintf(merged, sizeof(merged) / sizeof(merged[0]), L"%ls", prefix);
    } else {
        return;
    }

    SetEnvironmentVariableW(L"PATH", merged);
    set_adb_env_if_available();
    set_server_path_env_if_available(scrcpy_path);
}

static bool
get_vr_config_dir(WCHAR *out, size_t out_len, bool create) {
    DWORD len = GetEnvironmentVariableW(L"SC_VR_CONFIG_DIR", out,
                                        (DWORD) out_len);
    if (len && len < out_len) {
        if (!create || CreateDirectoryW(out, NULL)
                || GetLastError() == ERROR_ALREADY_EXISTS) {
            return true;
        }
        return false;
    }

    WCHAR appdata[MAX_PATH];
    len = GetEnvironmentVariableW(L"APPDATA", appdata,
                                  (DWORD) (sizeof(appdata) / sizeof(appdata[0])));
    if (!len || len >= sizeof(appdata) / sizeof(appdata[0])) {
        return false;
    }

    if (swprintf(out, out_len, L"%ls\\VR Mobile", appdata) <= 0) {
        return false;
    }

    return !create || CreateDirectoryW(out, NULL)
        || GetLastError() == ERROR_ALREADY_EXISTS;
}

static bool
get_profiles_dir(WCHAR *out, size_t out_len, bool create) {
    WCHAR config[MAX_PATH];
    if (!get_vr_config_dir(config, sizeof(config) / sizeof(config[0]), create)) {
        return false;
    }

    if (swprintf(out, out_len, L"%ls\\profiles", config) <= 0) {
        return false;
    }

    return !create || CreateDirectoryW(out, NULL)
        || GetLastError() == ERROR_ALREADY_EXISTS;
}

static bool
profile_name_is_valid_w(const WCHAR *name) {
    if (!name || !*name) {
        return false;
    }

    for (const WCHAR *p = name; *p; ++p) {
        WCHAR c = *p;
        bool ok = (c >= L'a' && c <= L'z')
               || (c >= L'A' && c <= L'Z')
               || (c >= L'0' && c <= L'9')
               || c == L'-' || c == L'_' || c == L'.';
        if (!ok) {
            return false;
        }
    }

    return true;
}

static bool
get_profile_path(const WCHAR *name, WCHAR *out, size_t out_len, bool create) {
    if (!profile_name_is_valid_w(name)) {
        return false;
    }

    WCHAR dir[MAX_PATH];
    if (!get_profiles_dir(dir, sizeof(dir) / sizeof(dir[0]), create)) {
        return false;
    }

    return swprintf(out, out_len, L"%ls\\%ls.profile", dir, name) > 0;
}

static bool
wide_to_utf8(const WCHAR *wide, char *out, size_t out_len) {
    int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, (int) out_len,
                                  NULL, NULL);
    return len > 0 && (size_t) len <= out_len;
}

static void
trim_wide_in_place(WCHAR *s) {
    WCHAR *start = s;
    while (*start == L' ' || *start == L'\t' || *start == L'\r'
            || *start == L'\n') {
        ++start;
    }
    if (start != s) {
        memmove(s, start, (wcslen(start) + 1) * sizeof(*s));
    }

    WCHAR *end = s + wcslen(s);
    while (end > s && (end[-1] == L' ' || end[-1] == L'\t'
            || end[-1] == L'\r' || end[-1] == L'\n')) {
        --end;
    }
    *end = L'\0';
}

static const char *
skip_ascii_spaces(const char *s) {
    while (*s == ' ' || *s == '\t') {
        ++s;
    }
    return s;
}

static bool
line_starts_with_arg(const char *line, const char *arg) {
    size_t len = strlen(arg);
    if (strncmp(line, arg, len)) {
        return false;
    }

    return !line[len] || line[len] == '\r' || line[len] == '\n'
        || line[len] == ' ' || line[len] == '\t' || line[len] == '=';
}

static bool
profile_line_has_device_selector(const char *line) {
    line = skip_ascii_spaces(line);
    if (!*line || *line == '#') {
        return false;
    }

    return line_starts_with_arg(line, "--serial")
        || line_starts_with_arg(line, "-s")
        || line_starts_with_arg(line, "--select-usb")
        || line_starts_with_arg(line, "-d")
        || line_starts_with_arg(line, "--select-tcpip")
        || line_starts_with_arg(line, "-e")
        || line_starts_with_arg(line, "--tcpip")
        || line_starts_with_arg(line, "--connect-manager");
}

static bool
profile_has_device_selector_w(const WCHAR *name) {
    WCHAR path[MAX_PATH];
    if (!get_profile_path(name, path, sizeof(path) / sizeof(path[0]), false)) {
        return false;
    }

    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD size = GetFileSize(file, NULL);
    if (size > 65536) {
        size = 65536;
    }

    char *bytes = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size + 1);
    if (!bytes) {
        CloseHandle(file);
        return false;
    }

    DWORD read = 0;
    bool ok = ReadFile(file, bytes, size, &read, NULL);
    CloseHandle(file);
    if (!ok) {
        HeapFree(GetProcessHeap(), 0, bytes);
        return false;
    }
    bytes[read] = '\0';

    bool found = false;
    char *line = bytes;
    while (*line) {
        char *next = strchr(line, '\n');
        if (next) {
            *next = '\0';
        }

        if (profile_line_has_device_selector(line)) {
            found = true;
            break;
        }

        if (!next) {
            break;
        }
        line = next + 1;
    }

    HeapFree(GetProcessHeap(), 0, bytes);
    return found;
}

static bool
is_autostart_enabled(void) {
    HKEY key;
    LONG r = RegOpenKeyExW(HKEY_CURRENT_USER,
                           L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                           0, KEY_READ, &key);
    if (r != ERROR_SUCCESS) {
        return false;
    }

    WCHAR value[MAX_FILE_PATH_CHARS + 4];
    DWORD size = sizeof(value);
    r = RegQueryValueExW(key, VR_AUTOSTART_VALUE_NAME, NULL, NULL,
                         (LPBYTE) value, &size);
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

static bool
set_autostart_enabled(bool enabled) {
    HKEY key;
    LONG r = RegCreateKeyExW(HKEY_CURRENT_USER,
                             L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                             0, NULL, 0, KEY_WRITE, NULL, &key, NULL);
    if (r != ERROR_SUCCESS) {
        return false;
    }

    bool ok;
    if (enabled) {
        WCHAR launcher[MAX_FILE_PATH_CHARS];
        WCHAR value[MAX_FILE_PATH_CHARS + 4];
        ok = get_launcher_path(launcher,
                               sizeof(launcher) / sizeof(launcher[0]))
          && swprintf(value, sizeof(value) / sizeof(value[0]), L"\"%ls\"",
                      launcher) > 0;
        if (ok) {
            r = RegSetValueExW(key, VR_AUTOSTART_VALUE_NAME, 0, REG_SZ,
                               (const BYTE *) value,
                               (DWORD) ((wcslen(value) + 1)
                                        * sizeof(value[0])));
            ok = r == ERROR_SUCCESS;
        }
    } else {
        r = RegDeleteValueW(key, VR_AUTOSTART_VALUE_NAME);
        ok = r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
    }

    RegCloseKey(key);
    return ok;
}

static void
sync_autostart_check(void) {
    SendMessageW(autostart_check, BM_SETCHECK,
                 is_autostart_enabled() ? BST_CHECKED : BST_UNCHECKED, 0);
}

static bool
append_quoted_arg(WCHAR *cmdline, size_t len, const WCHAR *arg) {
    size_t used = wcslen(cmdline);
    if (used + 3 >= len) {
        return false;
    }

    cmdline[used++] = L'"';

    unsigned backslashes = 0;
    for (const WCHAR *c = arg; *c; ++c) {
        switch (*c) {
            case L'"':
                while (backslashes) {
                    if (used + 2 >= len) {
                        return false;
                    }
                    cmdline[used++] = L'\\';
                    cmdline[used++] = L'\\';
                    --backslashes;
                }
                if (used + 2 >= len) {
                    return false;
                }
                cmdline[used++] = L'\\';
                cmdline[used++] = L'"';
                break;
            case L'\\':
                ++backslashes;
                break;
            default:
                while (backslashes) {
                    if (used + 1 >= len) {
                        return false;
                    }
                    cmdline[used++] = L'\\';
                    --backslashes;
                }
                if (used + 1 >= len) {
                    return false;
                }
                cmdline[used++] = *c;
                break;
        }
    }

    while (backslashes) {
        if (used + 2 >= len) {
            return false;
        }
        cmdline[used++] = L'\\';
        cmdline[used++] = L'\\';
        --backslashes;
    }

    if (used + 2 >= len) {
        return false;
    }
    cmdline[used++] = L'"';
    cmdline[used] = L'\0';
    return true;
}

static bool
append_wide_arg(WCHAR *cmdline, size_t len, const WCHAR *arg) {
    size_t used = wcslen(cmdline);
    if (used + 1 >= len) {
        return false;
    }
    cmdline[used++] = L' ';
    cmdline[used] = L'\0';

    return append_quoted_arg(cmdline, len, arg);
}

static bool
append_ascii_arg(WCHAR *cmdline, size_t len, const char *arg) {
    WCHAR wide[512];
    int converted = MultiByteToWideChar(CP_UTF8, 0, arg, -1, wide,
                                        (int) (sizeof(wide) / sizeof(wide[0])));
    if (!converted) {
        return false;
    }

    return append_wide_arg(cmdline, len, wide);
}

static bool
append_serial_arg(WCHAR *cmdline, size_t len, const char *serial) {
    char arg[VR_LAUNCHER_MAX_SERIAL_LEN + 16];
    int written = snprintf(arg, sizeof(arg), "--serial=%s", serial);
    if (written < 0 || (size_t) written >= sizeof(arg)) {
        return false;
    }

    return append_ascii_arg(cmdline, len, arg);
}

static bool
append_ascii_option_arg(WCHAR *cmdline, size_t len, const char *prefix,
                        const char *value) {
    char arg[512];
    int written = snprintf(arg, sizeof(arg), "%s%s", prefix, value);
    if (written < 0 || (size_t) written >= sizeof(arg)) {
        return false;
    }

    return append_ascii_arg(cmdline, len, arg);
}

static bool
append_wide_option_arg(WCHAR *cmdline, size_t len, const WCHAR *prefix,
                       const WCHAR *value) {
    size_t prefix_len = wcslen(prefix);
    size_t value_len = wcslen(value);
    WCHAR *arg = HeapAlloc(GetProcessHeap(), 0,
                           (prefix_len + value_len + 1) * sizeof(*arg));
    if (!arg) {
        return false;
    }

    memcpy(arg, prefix, prefix_len * sizeof(*arg));
    memcpy(&arg[prefix_len], value, (value_len + 1) * sizeof(*arg));

    bool ok = append_wide_arg(cmdline, len, arg);
    HeapFree(GetProcessHeap(), 0, arg);
    return ok;
}

static bool
append_launcher_args(WCHAR *cmdline, size_t len,
                     enum vr_launcher_command command) {
    const char *const *args = vr_launcher_command_args(command);
    if (!args) {
        return false;
    }

    while (*args) {
        if (!append_ascii_arg(cmdline, len, *args)) {
            return false;
        }
        ++args;
    }

    return true;
}

static bool
build_command_line(const struct command_runner *runner,
                   WCHAR *cmdline, size_t len) {
    cmdline[0] = L'\0';
    if (!append_quoted_arg(cmdline, len, runner->scrcpy_path)) {
        return false;
    }

    if (runner->has_serial) {
        if (!append_serial_arg(cmdline, len, runner->serial)) {
            return false;
        }

        if (runner->command == VR_LAUNCHER_COMMAND_CONNECT) {
            return true;
        }

        if (runner->command == VR_LAUNCHER_COMMAND_DEVICE_STATUS) {
            return append_ascii_arg(cmdline, len, "--device-status")
                && append_ascii_arg(cmdline, len, "--output-format=json");
        }
    }

    if (runner->command == VR_LAUNCHER_COMMAND_RUN_PROFILE) {
        if (runner->use_connect_manager
                && !append_ascii_arg(cmdline, len, "--connect-manager")) {
            return false;
        }

        return runner->has_profile
            && append_ascii_option_arg(cmdline, len, "--profile=",
                                       runner->profile_name);
    }

    if (runner->command == VR_LAUNCHER_COMMAND_TAILSCALE_CONNECT) {
        return runner->has_tailscale_addr
            ? append_wide_option_arg(cmdline, len, L"--tailscale=",
                                     runner->tailscale_addr)
            : append_ascii_arg(cmdline, len, "--tailscale");
    }

    if (runner->command == VR_LAUNCHER_COMMAND_SEND_FILE) {
        if (!runner->has_serial
                && !append_ascii_arg(cmdline, len, "--connect-manager")) {
            return false;
        }

        return runner->has_file
            && append_wide_option_arg(cmdline, len, L"--send-file=",
                                      runner->file_path);
    }

    if (runner->command == VR_LAUNCHER_COMMAND_PULL_FILE) {
        if (!runner->has_serial) {
            if (runner->has_tailscale_addr) {
                if (!append_wide_option_arg(cmdline, len, L"--tailscale=",
                                            runner->tailscale_addr)) {
                    return false;
                }
            } else if (!append_ascii_arg(cmdline, len,
                                         "--connect-manager")) {
                return false;
            }
        }

        return runner->has_file
            && append_wide_option_arg(cmdline, len, L"--pull-file=",
                                      runner->remote_path)
            && append_wide_option_arg(cmdline, len, L"--pull-target=",
                                      runner->file_path);
    }

    return append_launcher_args(cmdline, len, runner->command);
}

static void
clear_process_slot(HANDLE process) {
    EnterCriticalSection(&process_lock);
    if (mirror_process == process) {
        mirror_process = NULL;
    }
    if (utility_process == process) {
        utility_process = NULL;
    }
    LeaveCriticalSection(&process_lock);
}

static DWORD WINAPI
command_thread(LPVOID userdata) {
    struct command_runner *runner = userdata;

    WCHAR cmdline[4096];
    if (!build_command_line(runner, cmdline,
                            sizeof(cmdline) / sizeof(cmdline[0]))) {
        post_log(runner->hwnd, L"Failed to build scrcpy command line.\r\n");
        goto end_without_process;
    }

    SECURITY_ATTRIBUTES pipe_attrs = {
        .nLength = sizeof(pipe_attrs),
        .lpSecurityDescriptor = NULL,
        .bInheritHandle = TRUE,
    };

    HANDLE read_pipe;
    HANDLE write_pipe;
    if (!CreatePipe(&read_pipe, &write_pipe, &pipe_attrs, 0)) {
        post_log(runner->hwnd, L"Failed to create output pipe.\r\n");
        goto end_without_process;
    }
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup;
    ZeroMemory(&startup, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = write_pipe;
    startup.hStdError = write_pipe;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION process;
    ZeroMemory(&process, sizeof(process));

    prepare_child_environment(runner->scrcpy_path);

    BOOL ok = CreateProcessW(runner->scrcpy_path, cmdline, NULL, NULL, TRUE,
                             CREATE_NO_WINDOW, NULL, NULL, &startup, &process);
    CloseHandle(write_pipe);

    if (!ok) {
        CloseHandle(read_pipe);
        post_log(runner->hwnd, L"Failed to start scrcpy.exe.\r\n");
        goto end_without_process;
    }

    EnterCriticalSection(&process_lock);
    if (runner->mirror) {
        mirror_process = process.hProcess;
    } else {
        utility_process = process.hProcess;
    }
    LeaveCriticalSection(&process_lock);

    CloseHandle(process.hThread);

    struct output_buffer output = {0};
    char buffer[2048];
    DWORD read;
    while (ReadFile(read_pipe, buffer, sizeof(buffer), &read, NULL) && read) {
        if (!runner->mirror) {
            output_buffer_append(&output, buffer, read);
        }
        post_log_utf8(runner->hwnd, buffer, (int) read);
    }
    CloseHandle(read_pipe);

    WaitForSingleObject(process.hProcess, INFINITE);

    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    clear_process_slot(process.hProcess);
    CloseHandle(process.hProcess);

    struct command_done *done = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                          sizeof(*done));
    if (done) {
        done->exit_code = exit_code;
        done->command = runner->command;
        done->mirror = runner->mirror;
        done->output = output.data;
        done->output_len = output.len;
        PostMessageW(runner->hwnd, WM_VR_DONE, 0, (LPARAM) done);
    } else {
        HeapFree(GetProcessHeap(), 0, output.data);
    }

    HeapFree(GetProcessHeap(), 0, runner);
    return 0;

end_without_process:
    {
        struct command_done *done = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                              sizeof(*done));
        if (done) {
            done->exit_code = 1;
            done->command = runner->command;
            done->mirror = runner->mirror;
            PostMessageW(runner->hwnd, WM_VR_DONE, 0, (LPARAM) done);
        }
    }
    HeapFree(GetProcessHeap(), 0, runner);
    return 1;
}

static bool
is_mirror_running(void) {
    EnterCriticalSection(&process_lock);
    bool running = mirror_process != NULL;
    LeaveCriticalSection(&process_lock);
    return running;
}

static bool
is_utility_running(void) {
    EnterCriticalSection(&process_lock);
    bool running = utility_process != NULL;
    LeaveCriticalSection(&process_lock);
    return running;
}

static bool
get_selected_serial(char *out, size_t out_len) {
    int selected = (int) SendMessageW(device_list, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR || selected < 0 || (size_t) selected >= device_count) {
        return false;
    }

    snprintf(out, out_len, "%s", devices[selected].serial);
    return true;
}

static void
update_device_summary(void) {
    char serial[VR_LAUNCHER_MAX_SERIAL_LEN];
    WCHAR text[512];
    if (get_selected_serial(serial, sizeof(serial))) {
        int selected = (int) SendMessageW(device_list, LB_GETCURSEL, 0, 0);
        const struct vr_launcher_device_info *device = &devices[selected];
        swprintf(text, sizeof(text) / sizeof(text[0]),
                 L"Selected: %hs  |  State: %hs  |  Type: %hs",
                 device->serial, device->state, device->type);
    } else if (device_count) {
        swprintf(text, sizeof(text) / sizeof(text[0]),
                 L"%zu device(s) found. Select one, or Connect will use auto mode.",
                 device_count);
    } else {
        swprintf(text, sizeof(text) / sizeof(text[0]),
                 L"No device list yet. Click Refresh Devices.");
    }
    set_status(text);
}

static void
device_list_add(const struct vr_launcher_device_info *device) {
    if (device_count >= VR_LAUNCHER_MAX_DEVICES) {
        return;
    }

    devices[device_count] = *device;

    WCHAR label[512];
    swprintf(label, sizeof(label) / sizeof(label[0]), L"%hs    [%hs]    %hs",
             device->serial, device->state, device->type);
    SendMessageW(device_list, LB_ADDSTRING, 0, (LPARAM) label);
    ++device_count;
}

static void
update_devices_from_health(const char *output) {
    char previously_selected[VR_LAUNCHER_MAX_SERIAL_LEN];
    bool had_selection = get_selected_serial(previously_selected,
                                             sizeof(previously_selected));

    SendMessageW(device_list, LB_RESETCONTENT, 0, 0);
    device_count = 0;

    struct vr_launcher_device_info parsed[VR_LAUNCHER_MAX_DEVICES];
    size_t parsed_count = 0;
    char last_wifi_serial[VR_LAUNCHER_MAX_SERIAL_LEN] = "";
    char last_tailscale_serial[VR_LAUNCHER_MAX_SERIAL_LEN] = "";
    bool parsed_ok =
        vr_launcher_parse_connection_health(output, parsed,
                                            VR_LAUNCHER_MAX_DEVICES,
                                            &parsed_count, last_wifi_serial,
                                            sizeof(last_wifi_serial),
                                            last_tailscale_serial,
                                            sizeof(last_tailscale_serial));
    if (!parsed_ok) {
        set_detail(L"Could not parse device list. See log for raw output.");
        update_device_summary();
        return;
    }

    int restore_index = -1;
    for (size_t i = 0; i < parsed_count; ++i) {
        if (had_selection && !strcmp(previously_selected, parsed[i].serial)) {
            restore_index = (int) device_count;
        }
        device_list_add(&parsed[i]);
    }

    if (device_count) {
        SendMessageW(device_list, LB_SETCURSEL,
                     restore_index >= 0 ? restore_index : 0, 0);
        set_detail(L"Device list refreshed. Select a device, then click "
                   L"Device Status or Connect.");
    } else {
        WCHAR detail[1024];
        if (last_wifi_serial[0] || last_tailscale_serial[0]) {
            swprintf(detail, sizeof(detail) / sizeof(detail[0]),
                     L"No Android devices found by ADB.\r\n\r\n"
                     L"Last saved Wi-Fi device: %hs\r\n"
                     L"Last saved Tailscale device: %hs\r\n\r\n"
                     L"If the LAN Wi-Fi address is stale, connect via USB and "
                     L"run Wireless Setup again. For Tailscale, make sure "
                     L"Tailscale is online on both devices and ADB TCP/IP is "
                     L"enabled on the phone.",
                     last_wifi_serial[0] ? last_wifi_serial : "-",
                     last_tailscale_serial[0] ? last_tailscale_serial : "-");
        } else {
            swprintf(detail, sizeof(detail) / sizeof(detail[0]),
                     L"No Android devices found by ADB.\r\n\r\n"
                     L"Connect via USB, enable Developer Options and USB "
                     L"debugging, allow the RSA prompt on the phone, then "
                     L"click Refresh Devices.");
        }
        set_detail(detail);
    }

    update_device_summary();
}

static void
update_detail_from_status(const char *output) {
    struct vr_launcher_device_status status;
    if (!vr_launcher_parse_device_status(output, &status)) {
        set_detail(L"Could not parse device status. See log for raw output.");
        return;
    }

    WCHAR detail[4096];
    swprintf(detail, sizeof(detail) / sizeof(detail[0]),
             L"Serial: %hs\r\n"
             L"Device: %hs %hs\r\n"
             L"Android: %hs\r\n"
             L"Wi-Fi IP: %hs\r\n"
             L"Screen: %hs\r\n"
             L"Battery: %hs\r\n"
             L"Storage: %hs\r\n\r\n"
             L"Raw JSON tetap ada di panel log untuk debugging.",
             status.serial[0] ? status.serial : "-",
             status.manufacturer[0] ? status.manufacturer : "-",
             status.model[0] ? status.model : "-",
             status.android_version[0] ? status.android_version : "-",
             status.wifi_ip[0] ? status.wifi_ip : "-",
             status.screen_line[0] ? status.screen_line : "-",
             status.battery_level[0] ? status.battery_level : "-",
             status.storage_line[0] ? status.storage_line : "-");

    set_detail(detail);
}

static bool
get_profile_name_w(WCHAR *out, size_t out_len) {
    int selected = (int) SendMessageW(profile_list, LB_GETCURSEL, 0, 0);
    if (selected != LB_ERR) {
        SendMessageW(profile_list, LB_GETTEXT, (WPARAM) selected,
                     (LPARAM) out);
        return profile_name_is_valid_w(out);
    }

    GetWindowTextW(profile_name_edit, out, (int) out_len);
    return profile_name_is_valid_w(out);
}

static void
load_profile_into_editor(const WCHAR *name) {
    WCHAR path[MAX_PATH];
    if (!get_profile_path(name, path, sizeof(path) / sizeof(path[0]), false)) {
        return;
    }

    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD size = GetFileSize(file, NULL);
    char *bytes = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size + 1);
    if (!bytes) {
        CloseHandle(file);
        return;
    }

    DWORD read = 0;
    bool ok = ReadFile(file, bytes, size, &read, NULL);
    CloseHandle(file);
    if (!ok) {
        HeapFree(GetProcessHeap(), 0, bytes);
        return;
    }
    bytes[read] = '\0';

    const char *content = bytes;
    if (!strncmp(content, "# VR Mobile device profile:", 27)) {
        const char *line_end = strchr(content, '\n');
        content = line_end ? line_end + 1 : "";
    }

    int wide_len = MultiByteToWideChar(CP_UTF8, 0, content, -1, NULL, 0);
    if (wide_len <= 0) {
        wide_len = MultiByteToWideChar(CP_ACP, 0, content, -1, NULL, 0);
    }

    WCHAR *wide = HeapAlloc(GetProcessHeap(), 0,
                            (size_t) wide_len * sizeof(*wide));
    if (wide) {
        if (!MultiByteToWideChar(CP_UTF8, 0, content, -1, wide, wide_len)) {
            MultiByteToWideChar(CP_ACP, 0, content, -1, wide, wide_len);
        }
        SetWindowTextW(profile_args_edit, wide);
        HeapFree(GetProcessHeap(), 0, wide);
    }

    SetWindowTextW(profile_name_edit, name);
    HeapFree(GetProcessHeap(), 0, bytes);
}

static void
refresh_profiles(void) {
    SendMessageW(profile_list, LB_RESETCONTENT, 0, 0);

    WCHAR dir[MAX_PATH];
    if (!get_profiles_dir(dir, sizeof(dir) / sizeof(dir[0]), true)) {
        append_log_line(L"Could not open VR Mobile profiles directory.");
        return;
    }

    WCHAR pattern[MAX_PATH];
    swprintf(pattern, sizeof(pattern) / sizeof(pattern[0]), L"%ls\\*.profile",
             dir);

    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) {
        set_status(L"No profiles yet. Create one on the right panel.");
        return;
    }

    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }

        WCHAR name[MAX_PROFILE_NAME_CHARS];
        wcsncpy(name, data.cFileName, MAX_PROFILE_NAME_CHARS - 1);
        name[MAX_PROFILE_NAME_CHARS - 1] = L'\0';
        WCHAR *suffix = wcsrchr(name, L'.');
        if (suffix && !wcscmp(suffix, L".profile")) {
            *suffix = L'\0';
            SendMessageW(profile_list, LB_ADDSTRING, 0, (LPARAM) name);
        }
    } while (FindNextFileW(find, &data));

    FindClose(find);
    set_status(L"Profiles refreshed.");
}

static void
save_profile_from_editor(void) {
    WCHAR name[MAX_PROFILE_NAME_CHARS];
    GetWindowTextW(profile_name_edit, name, (int) (sizeof(name) / sizeof(name[0])));
    if (!profile_name_is_valid_w(name)) {
        append_log_line(L"Invalid profile name. Use letters, numbers, dot, dash or underscore.");
        set_status(L"Invalid profile name");
        return;
    }

    WCHAR path[MAX_PATH];
    if (!get_profile_path(name, path, sizeof(path) / sizeof(path[0]), true)) {
        append_log_line(L"Could not create profile path.");
        set_status(L"Could not save profile");
        return;
    }

    int args_len = GetWindowTextLengthW(profile_args_edit);
    WCHAR *args = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                            ((size_t) args_len + 1) * sizeof(*args));
    if (!args) {
        append_log_line(L"Out of memory while saving profile.");
        return;
    }
    GetWindowTextW(profile_args_edit, args, args_len + 1);

    char profile_name[MAX_PROFILE_NAME_CHARS];
    wide_to_utf8(name, profile_name, sizeof(profile_name));

    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, args, -1, NULL, 0, NULL,
                                       NULL);
    char *utf8_args = HeapAlloc(GetProcessHeap(), 0, (size_t) utf8_len);
    if (!utf8_args) {
        HeapFree(GetProcessHeap(), 0, args);
        append_log_line(L"Out of memory while saving profile.");
        return;
    }
    WideCharToMultiByte(CP_UTF8, 0, args, -1, utf8_args, utf8_len, NULL, NULL);

    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        HeapFree(GetProcessHeap(), 0, utf8_args);
        HeapFree(GetProcessHeap(), 0, args);
        append_log_line(L"Could not write profile file.");
        set_status(L"Could not save profile");
        return;
    }

    char header[256];
    int header_len = snprintf(header, sizeof(header),
                              "# VR Mobile device profile: %s\n",
                              profile_name);
    DWORD written;
    bool ok = WriteFile(file, header, (DWORD) header_len, &written, NULL);
    if (ok && utf8_args[0]) {
        ok = WriteFile(file, utf8_args, (DWORD) strlen(utf8_args), &written,
                       NULL);
        if (ok && utf8_args[strlen(utf8_args) - 1] != '\n') {
            ok = WriteFile(file, "\n", 1, &written, NULL);
        }
    }
    CloseHandle(file);

    HeapFree(GetProcessHeap(), 0, utf8_args);
    HeapFree(GetProcessHeap(), 0, args);

    if (ok) {
        append_log_line(L"Profile saved.");
        set_status(L"Profile saved");
        refresh_profiles();
    } else {
        append_log_line(L"Could not save complete profile file.");
        set_status(L"Could not save profile");
    }
}

static void
delete_selected_profile(void) {
    WCHAR name[MAX_PROFILE_NAME_CHARS];
    if (!get_profile_name_w(name, sizeof(name) / sizeof(name[0]))) {
        append_log_line(L"Select a profile to delete.");
        return;
    }

    WCHAR path[MAX_PATH];
    if (!get_profile_path(name, path, sizeof(path) / sizeof(path[0]), false)
            || !DeleteFileW(path)) {
        append_log_line(L"Could not delete profile.");
        set_status(L"Could not delete profile");
        return;
    }

    SetWindowTextW(profile_name_edit, L"");
    SetWindowTextW(profile_args_edit, L"");
    refresh_profiles();
    set_status(L"Profile deleted");
}

static bool
prepare_runner_common(HWND hwnd, enum vr_launcher_command command,
                      bool mirror, struct command_runner **runner_out) {
    if (mirror && is_mirror_running()) {
        append_log_line(L"Mirror is already running. Click Disconnect first.");
        set_status(L"Mirror already running");
        return false;
    }

    if (!mirror && is_utility_running()) {
        return false;
    }

    WCHAR scrcpy_path[MAX_PATH];
    if (!find_scrcpy_path(scrcpy_path,
                          sizeof(scrcpy_path) / sizeof(scrcpy_path[0]))) {
        append_log_line(L"scrcpy.exe was not found next to the launcher or in "
                        L"build\\app.");
        set_status(L"scrcpy.exe not found");
        return false;
    }

    struct command_runner *runner =
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*runner));
    if (!runner) {
        append_log_line(L"Out of memory.");
        return false;
    }

    runner->hwnd = hwnd;
    runner->command = command;
    runner->mirror = mirror;
    wcscpy(runner->scrcpy_path, scrcpy_path);
    *runner_out = runner;
    return true;
}

static bool
start_runner(struct command_runner *runner) {
    HANDLE thread = CreateThread(NULL, 0, command_thread, runner, 0, NULL);
    if (!thread) {
        HeapFree(GetProcessHeap(), 0, runner);
        append_log_line(L"Failed to create command worker thread.");
        set_status(L"Command failed");
        return false;
    }

    CloseHandle(thread);
    return true;
}

static void
start_launcher_command(HWND hwnd, enum vr_launcher_command command) {
    bool mirror = command == VR_LAUNCHER_COMMAND_CONNECT
               || command == VR_LAUNCHER_COMMAND_TAILSCALE_CONNECT
               || command == VR_LAUNCHER_COMMAND_RUN_PROFILE;

    if (mirror && is_mirror_running()) {
        append_log_line(L"Mirror is already running. Click Disconnect first.");
        set_status(L"Mirror already running");
        return;
    }

    if (!mirror && is_utility_running()) {
        append_log_line(L"Another utility command is still running.");
        set_status(L"Utility command still running");
        return;
    }

    if (command == VR_LAUNCHER_COMMAND_WIRELESS_SETUP && is_mirror_running()) {
        append_log_line(L"Disconnect mirror before Wireless Setup.");
        set_status(L"Disconnect mirror before Wireless Setup");
        return;
    }

    WCHAR scrcpy_path[MAX_PATH];
    if (!find_scrcpy_path(scrcpy_path,
                          sizeof(scrcpy_path) / sizeof(scrcpy_path[0]))) {
        append_log_line(L"scrcpy.exe was not found next to the launcher or in "
                        L"build\\app.");
        set_status(L"scrcpy.exe not found");
        return;
    }

    struct command_runner *runner =
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*runner));
    if (!runner) {
        append_log_line(L"Out of memory.");
        return;
    }

    runner->hwnd = hwnd;
    runner->command = command;
    runner->mirror = mirror;
    wcscpy(runner->scrcpy_path, scrcpy_path);

    if (command == VR_LAUNCHER_COMMAND_CONNECT
            || command == VR_LAUNCHER_COMMAND_DEVICE_STATUS
            || command == VR_LAUNCHER_COMMAND_RUN_PROFILE) {
        runner->has_serial = get_selected_serial(runner->serial,
                                                 sizeof(runner->serial));
    }

    if (command == VR_LAUNCHER_COMMAND_TAILSCALE_CONNECT) {
        GetWindowTextW(tailscale_addr_edit, runner->tailscale_addr,
                       (int) (sizeof(runner->tailscale_addr)
                              / sizeof(runner->tailscale_addr[0])));
        trim_wide_in_place(runner->tailscale_addr);
        runner->has_tailscale_addr = runner->tailscale_addr[0] != L'\0';
    }

    WCHAR header[512];
    if (command == VR_LAUNCHER_COMMAND_TAILSCALE_CONNECT
            && runner->has_tailscale_addr) {
        swprintf(header, sizeof(header) / sizeof(header[0]),
                 L"> Tailscale connect (%ls)", runner->tailscale_addr);
    } else if (command == VR_LAUNCHER_COMMAND_TAILSCALE_CONNECT) {
        swprintf(header, sizeof(header) / sizeof(header[0]),
                 L"> Tailscale connect (last saved address)");
    } else if (runner->has_serial) {
        swprintf(header, sizeof(header) / sizeof(header[0]), L"> %hs (%hs)",
                 vr_launcher_command_label(command), runner->serial);
    } else {
        swprintf(header, sizeof(header) / sizeof(header[0]), L"> %hs",
                 vr_launcher_command_label(command));
    }
    append_log_line(header);

    switch (command) {
        case VR_LAUNCHER_COMMAND_CONNECT:
            set_status(runner->has_serial
                           ? L"Mirroring selected device..."
                           : L"Auto connecting and mirroring...");
            break;
        case VR_LAUNCHER_COMMAND_CONNECTION_HEALTH:
            set_status(L"Refreshing devices...");
            break;
        case VR_LAUNCHER_COMMAND_DEVICE_STATUS:
            set_status(L"Reading device status...");
            break;
        case VR_LAUNCHER_COMMAND_WIRELESS_SETUP:
            set_status(L"Running wireless setup...");
            break;
        case VR_LAUNCHER_COMMAND_TAILSCALE_CONNECT:
            set_status(runner->has_tailscale_addr
                           ? L"Connecting through Tailscale..."
                           : L"Connecting to saved Tailscale device...");
            break;
        case VR_LAUNCHER_COMMAND_RUN_PROFILE:
            set_status(L"Running selected profile...");
            break;
        case VR_LAUNCHER_COMMAND_SEND_FILE:
            set_status(L"Sending file...");
            break;
        case VR_LAUNCHER_COMMAND_PULL_FILE:
            set_status(L"Receiving file...");
            break;
    }

    HANDLE thread = CreateThread(NULL, 0, command_thread, runner, 0, NULL);
    if (!thread) {
        HeapFree(GetProcessHeap(), 0, runner);
        append_log_line(L"Failed to create command worker thread.");
        set_status(L"Command failed");
        return;
    }

    CloseHandle(thread);
}

static void
run_selected_profile(HWND hwnd) {
    struct command_runner *runner;
    if (!prepare_runner_common(hwnd, VR_LAUNCHER_COMMAND_RUN_PROFILE, true,
                               &runner)) {
        return;
    }

    WCHAR profile_name_w[MAX_PROFILE_NAME_CHARS];
    if (!get_profile_name_w(profile_name_w,
                            sizeof(profile_name_w) / sizeof(profile_name_w[0]))
            || !wide_to_utf8(profile_name_w, runner->profile_name,
                             sizeof(runner->profile_name))) {
        HeapFree(GetProcessHeap(), 0, runner);
        append_log_line(L"Select or enter a valid profile name first.");
        set_status(L"No profile selected");
        return;
    }

    runner->has_profile = true;
    runner->has_serial = get_selected_serial(runner->serial,
                                             sizeof(runner->serial));
    runner->use_connect_manager =
        !runner->has_serial && !profile_has_device_selector_w(profile_name_w);

    WCHAR header[256];
    if (runner->has_serial) {
        swprintf(header, sizeof(header) / sizeof(header[0]),
                 L"> Run profile (%hs) on %hs", runner->profile_name,
                 runner->serial);
    } else if (runner->use_connect_manager) {
        swprintf(header, sizeof(header) / sizeof(header[0]),
                 L"> Run profile (%hs) with auto connect",
                 runner->profile_name);
    } else {
        swprintf(header, sizeof(header) / sizeof(header[0]),
                 L"> Run profile (%hs)", runner->profile_name);
    }
    append_log_line(header);
    set_status(L"Running selected profile...");
    start_runner(runner);
}

static bool
start_send_file_now(HWND hwnd, const WCHAR *path) {
    struct command_runner *runner;
    if (!prepare_runner_common(hwnd, VR_LAUNCHER_COMMAND_SEND_FILE, false,
                               &runner)) {
        return false;
    }

    wcsncpy(runner->file_path, path,
            sizeof(runner->file_path) / sizeof(runner->file_path[0]) - 1);
    runner->has_file = true;
    runner->has_serial = get_selected_serial(runner->serial,
                                             sizeof(runner->serial));

    WCHAR header[512];
    if (runner->has_serial) {
        swprintf(header, sizeof(header) / sizeof(header[0]),
                 L"> Send file to %hs: %ls", runner->serial, path);
    } else {
        swprintf(header, sizeof(header) / sizeof(header[0]),
                 L"> Send file with auto connect: %ls", path);
    }
    append_log_line(header);
    set_status(L"Sending file...");
    return start_runner(runner);
}

static const WCHAR *
remote_basename(const WCHAR *path) {
    const WCHAR *separator = wcsrchr(path, L'/');
    const WCHAR *backslash = wcsrchr(path, L'\\');
    if (backslash && (!separator || backslash > separator)) {
        separator = backslash;
    }
    return separator ? separator + 1 : path;
}

static bool
choose_pull_destination(HWND hwnd, const WCHAR *remote, WCHAR *out,
                        size_t out_len) {
    const WCHAR *name = remote_basename(remote);
    if (!*name || wcslen(name) >= out_len) {
        return false;
    }
    wcscpy(out, name);

    OPENFILENAMEW dialog;
    ZeroMemory(&dialog, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpstrFile = out;
    dialog.nMaxFile = (DWORD) out_len;
    dialog.lpstrFilter = L"All files\0*.*\0\0";
    dialog.nFilterIndex = 1;
    dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT
                 | OFN_PATHMUSTEXIST;
    dialog.lpstrTitle = L"Save file from Android";
    return GetSaveFileNameW(&dialog);
}

static void
receive_file_from_phone(HWND hwnd) {
    struct command_runner *runner;
    if (!prepare_runner_common(hwnd, VR_LAUNCHER_COMMAND_PULL_FILE, false,
                               &runner)) {
        append_log_line(L"Another utility command is still running.");
        set_status(L"Utility command still running");
        return;
    }

    GetWindowTextW(pull_remote_path_edit, runner->remote_path,
                   (int) (sizeof(runner->remote_path)
                          / sizeof(runner->remote_path[0])));
    trim_wide_in_place(runner->remote_path);
    size_t remote_len = wcslen(runner->remote_path);
    if (!remote_len || runner->remote_path[remote_len - 1] == L'/') {
        HeapFree(GetProcessHeap(), 0, runner);
        append_log_line(L"Enter a complete Android file path first.");
        set_status(L"Android file path required");
        return;
    }

    if (!choose_pull_destination(
            hwnd, runner->remote_path, runner->file_path,
            sizeof(runner->file_path) / sizeof(runner->file_path[0]))) {
        HeapFree(GetProcessHeap(), 0, runner);
        set_status(L"Receive file cancelled");
        return;
    }

    runner->has_file = true;
    runner->has_serial = get_selected_serial(runner->serial,
                                             sizeof(runner->serial));
    if (!runner->has_serial) {
        GetWindowTextW(tailscale_addr_edit, runner->tailscale_addr,
                       (int) (sizeof(runner->tailscale_addr)
                              / sizeof(runner->tailscale_addr[0])));
        trim_wide_in_place(runner->tailscale_addr);
        runner->has_tailscale_addr = runner->tailscale_addr[0] != L'\0';
    }

    WCHAR header[MAX_FILE_PATH_CHARS * 2];
    swprintf(header, sizeof(header) / sizeof(header[0]),
             L"> Receive %ls -> %ls", runner->remote_path,
             runner->file_path);
    append_log_line(header);
    set_status(L"Receiving file...");
    start_runner(runner);
}

static void
refresh_file_queue_display(void) {
    SetWindowTextW(file_drop_edit, L"");
    if (!queued_file_count) {
        append_file_transfer_line(L"Drop files here to send to phone.");
        append_file_transfer_line(L"Target: " VR_FILE_DROP_TARGET);
        append_file_transfer_line(L"APK files will be installed by the core send-file command.");
        return;
    }

    append_file_transfer_line(L"Queued files:");
    for (size_t i = 0; i < queued_file_count; ++i) {
        WCHAR line[MAX_FILE_PATH_CHARS + 16];
        swprintf(line, sizeof(line) / sizeof(line[0]), L"%zu. %ls", i + 1,
                 queued_files[i]);
        append_file_transfer_line(line);
    }
}

static void
start_next_queued_file(HWND hwnd) {
    if (!queued_file_count || is_utility_running()) {
        return;
    }

    WCHAR path[MAX_FILE_PATH_CHARS];
    wcscpy(path, queued_files[0]);
    for (size_t i = 1; i < queued_file_count; ++i) {
        wcscpy(queued_files[i - 1], queued_files[i]);
    }
    --queued_file_count;
    refresh_file_queue_display();

    if (!start_send_file_now(hwnd, path)) {
        if (queued_file_count < MAX_FILE_QUEUE) {
            for (size_t i = queued_file_count; i > 0; --i) {
                wcscpy(queued_files[i], queued_files[i - 1]);
            }
            wcscpy(queued_files[0], path);
            ++queued_file_count;
            refresh_file_queue_display();
        }
    }
}

static void
enqueue_file(HWND hwnd, const WCHAR *path) {
    if (queued_file_count >= MAX_FILE_QUEUE) {
        append_file_transfer_line(L"File queue is full.");
        set_status(L"File queue is full");
        return;
    }

    WCHAR log_line[MAX_FILE_PATH_CHARS + 64];
    swprintf(log_line, sizeof(log_line) / sizeof(log_line[0]),
             L"Drop detected: %ls", path);
    append_log_line(log_line);

    wcsncpy(queued_files[queued_file_count], path, MAX_FILE_PATH_CHARS - 1);
    queued_files[queued_file_count][MAX_FILE_PATH_CHARS - 1] = L'\0';
    ++queued_file_count;
    refresh_file_queue_display();
    set_status(L"File drop detected");
    start_next_queued_file(hwnd);
}

static void
process_drop_files(HWND hwnd, HDROP drop) {
    UINT count = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
    if (!count) {
        append_log_line(L"Drop ignored: no files were provided by Windows.");
        append_file_transfer_line(L"Drop ignored: no files were provided by Windows.");
        set_status(L"Drop ignored");
        DragFinish(drop);
        show_dashboard();
        return;
    }

    WCHAR summary[96];
    swprintf(summary, sizeof(summary) / sizeof(summary[0]),
             L"Drop accepted: %u file(s)", count);
    append_log_line(summary);
    append_file_transfer_line(summary);

    for (UINT i = 0; i < count; ++i) {
        WCHAR path[MAX_FILE_PATH_CHARS];
        if (DragQueryFileW(drop, i, path,
                           sizeof(path) / sizeof(path[0]))) {
            enqueue_file(hwnd, path);
        }
    }
    DragFinish(drop);
    show_dashboard();
}

static LRESULT CALLBACK
file_drop_edit_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_DROPFILES) {
        HWND target = main_window ? main_window : GetParent(hwnd);
        process_drop_files(target, (HDROP) wparam);
        return 0;
    }

    return CallWindowProcW(file_drop_edit_wndproc, hwnd, msg, wparam, lparam);
}

static void
terminate_process_slot(HANDLE *slot) {
    EnterCriticalSection(&process_lock);
    HANDLE process = *slot;
    if (process) {
        TerminateProcess(process, 1);
    }
    LeaveCriticalSection(&process_lock);
}

static void
disconnect_mirror(void) {
    if (!is_mirror_running()) {
        append_log_line(L"No active mirror process.");
        return;
    }

    terminate_process_slot(&mirror_process);
    append_log_line(L"Disconnect requested.");
    set_status(L"Disconnecting mirror...");
}

static HWND
create_button(HWND parent, const WCHAR *label, int id) {
    return CreateWindowW(L"BUTTON", label,
                         WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                         0, 0, 0, 0, parent, (HMENU) (uintptr_t) id,
                         app_instance, NULL);
}

static HWND
create_label(HWND parent, const WCHAR *label) {
    return CreateWindowW(L"STATIC", label, WS_CHILD | WS_VISIBLE | SS_LEFT,
                         0, 0, 0, 0, parent, NULL, app_instance, NULL);
}

static void
set_font(HWND hwnd, HFONT font) {
    SendMessageW(hwnd, WM_SETFONT, (WPARAM) font, TRUE);
}

static void
resize_controls(HWND hwnd) {
    RECT rect;
    GetClientRect(hwnd, &rect);

    const int margin = 16;
    const int gap = 10;
    const int title_h = 32;
    const int status_h = 24;
    const int button_h = 34;
    const int heading_h = 22;
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    int x = margin;
    int y = margin;
    MoveWindow(title_label, x, y, width - (2 * margin), title_h, TRUE);

    y += title_h + 4;
    MoveWindow(status_label, x, y, width - (2 * margin) - 240, status_h, TRUE);
    MoveWindow(autostart_check, width - margin - 220, y, 220, status_h, TRUE);

    y += status_h + gap;
    int button_w = (width - (2 * margin) - (6 * gap)) / 7;
    MoveWindow(GetDlgItem(hwnd, ID_BUTTON_CONNECT), x, y, button_w, button_h,
               TRUE);
    x += button_w + gap;
    MoveWindow(GetDlgItem(hwnd, ID_BUTTON_DISCONNECT), x, y, button_w, button_h,
               TRUE);
    x += button_w + gap;
    MoveWindow(GetDlgItem(hwnd, ID_BUTTON_WIRELESS), x, y, button_w, button_h,
               TRUE);
    x += button_w + gap;
    MoveWindow(GetDlgItem(hwnd, ID_BUTTON_REFRESH), x, y, button_w, button_h,
               TRUE);
    x += button_w + gap;
    MoveWindow(GetDlgItem(hwnd, ID_BUTTON_STATUS), x, y, button_w, button_h,
               TRUE);
    x += button_w + gap;
    MoveWindow(GetDlgItem(hwnd, ID_BUTTON_CLEAR), x, y, button_w, button_h,
               TRUE);
    x += button_w + gap;
    MoveWindow(GetDlgItem(hwnd, ID_BUTTON_EXIT), x, y, button_w, button_h,
               TRUE);

    y += button_h + gap;
    x = margin;
    int tailscale_label_w = 130;
    int tailscale_button_w = 170;
    MoveWindow(tailscale_label, x, y, tailscale_label_w, button_h, TRUE);
    x += tailscale_label_w + gap;
    MoveWindow(tailscale_addr_edit, x, y,
               width - (2 * margin) - tailscale_label_w
                   - tailscale_button_w - (2 * gap),
               button_h, TRUE);
    x = width - margin - tailscale_button_w;
    MoveWindow(GetDlgItem(hwnd, ID_BUTTON_TAILSCALE), x, y,
               tailscale_button_w, button_h, TRUE);

    y += button_h + gap;

    int log_h = height / 3;
    if (log_h < 150) {
        log_h = 150;
    }
    if (log_h > 260) {
        log_h = 260;
    }

    int main_h = height - y - log_h - (2 * gap) - margin - heading_h;
    if (main_h < 120) {
        main_h = 120;
    }

    int content_w = width - (2 * margin);
    int left_w = 300;
    int right_w = 310;
    if (width < 980) {
        left_w = 260;
        right_w = 260;
    }
    int middle_w = content_w - left_w - right_w - (2 * gap);
    if (middle_w < 260) {
        middle_w = 260;
        right_w = content_w - left_w - middle_w - (2 * gap);
    }

    int left_x = margin;
    int middle_x = left_x + left_w + gap;
    int right_x = middle_x + middle_w + gap;

    MoveWindow(devices_heading, left_x, y, left_w, heading_h, TRUE);
    MoveWindow(detail_heading, middle_x, y, middle_w, heading_h,
               TRUE);
    MoveWindow(profile_heading, right_x, y, right_w, heading_h, TRUE);
    y += heading_h;

    MoveWindow(device_list, left_x, y, left_w, main_h, TRUE);

    int detail_h = (main_h - gap - heading_h) * 55 / 100;
    int file_y = y + detail_h + gap;
    MoveWindow(detail_edit, middle_x, y, middle_w, detail_h, TRUE);
    MoveWindow(file_drop_heading, middle_x, file_y, middle_w, heading_h, TRUE);
    int transfer_y = file_y + heading_h;
    int receive_button_w = 90;
    MoveWindow(pull_remote_path_edit, middle_x, transfer_y,
               middle_w - receive_button_w - gap, button_h, TRUE);
    MoveWindow(GetDlgItem(hwnd, ID_BUTTON_PULL_FILE),
               middle_x + middle_w - receive_button_w, transfer_y,
               receive_button_w, button_h, TRUE);
    transfer_y += button_h + gap;
    MoveWindow(file_drop_edit, middle_x, transfer_y, middle_w,
               y + main_h - transfer_y, TRUE);

    int profile_list_h = 105;
    int profile_button_w = (right_w - (3 * gap)) / 4;
    MoveWindow(profile_list, right_x, y, right_w, profile_list_h, TRUE);
    int profile_y = y + profile_list_h + gap;
    MoveWindow(profile_name_edit, right_x, profile_y, right_w, button_h, TRUE);
    profile_y += button_h + gap;
    MoveWindow(GetDlgItem(hwnd, ID_BUTTON_PROFILE_REFRESH), right_x,
               profile_y, profile_button_w, button_h, TRUE);
    MoveWindow(GetDlgItem(hwnd, ID_BUTTON_PROFILE_SAVE),
               right_x + profile_button_w + gap, profile_y,
               profile_button_w, button_h, TRUE);
    MoveWindow(GetDlgItem(hwnd, ID_BUTTON_PROFILE_RUN),
               right_x + ((profile_button_w + gap) * 2), profile_y,
               profile_button_w, button_h, TRUE);
    MoveWindow(GetDlgItem(hwnd, ID_BUTTON_PROFILE_DELETE),
               right_x + ((profile_button_w + gap) * 3), profile_y,
               profile_button_w, button_h, TRUE);
    profile_y += button_h + gap;
    MoveWindow(profile_args_edit, right_x, profile_y, right_w,
               y + main_h - profile_y, TRUE);

    y += main_h + gap;
    MoveWindow(log_heading, margin, y, width - (2 * margin), heading_h, TRUE);
    y += heading_h;
    MoveWindow(log_edit, margin, y, width - (2 * margin),
               height - y - margin, TRUE);
}

static void
create_fonts(void) {
    title_font = CreateFontW(-24, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    ui_font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    mono_font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_MODERN, L"Consolas");
}

static void
apply_fonts(HWND hwnd) {
    set_font(title_label, title_font);
    set_font(status_label, ui_font);
    set_font(tailscale_label, ui_font);
    set_font(tailscale_addr_edit, ui_font);
    set_font(devices_heading, ui_font);
    set_font(detail_heading, ui_font);
    set_font(log_heading, ui_font);
    set_font(device_list, mono_font);
    set_font(detail_edit, ui_font);
    set_font(log_edit, mono_font);
    set_font(GetDlgItem(hwnd, ID_BUTTON_CONNECT), ui_font);
    set_font(GetDlgItem(hwnd, ID_BUTTON_WIRELESS), ui_font);
    set_font(GetDlgItem(hwnd, ID_BUTTON_TAILSCALE), ui_font);
    set_font(GetDlgItem(hwnd, ID_BUTTON_STATUS), ui_font);
    set_font(GetDlgItem(hwnd, ID_BUTTON_REFRESH), ui_font);
    set_font(GetDlgItem(hwnd, ID_BUTTON_DISCONNECT), ui_font);
    set_font(GetDlgItem(hwnd, ID_BUTTON_CLEAR), ui_font);
    set_font(GetDlgItem(hwnd, ID_BUTTON_EXIT), ui_font);
    set_font(GetDlgItem(hwnd, ID_CHECK_AUTOSTART), ui_font);
    set_font(GetDlgItem(hwnd, ID_BUTTON_PROFILE_REFRESH), ui_font);
    set_font(GetDlgItem(hwnd, ID_BUTTON_PROFILE_SAVE), ui_font);
    set_font(GetDlgItem(hwnd, ID_BUTTON_PROFILE_RUN), ui_font);
    set_font(GetDlgItem(hwnd, ID_BUTTON_PROFILE_DELETE), ui_font);
    set_font(profile_heading, ui_font);
    set_font(profile_list, ui_font);
    set_font(profile_name_edit, ui_font);
    set_font(profile_args_edit, mono_font);
    set_font(file_drop_heading, ui_font);
    set_font(file_drop_edit, ui_font);
    set_font(pull_remote_path_edit, ui_font);
    set_font(GetDlgItem(hwnd, ID_BUTTON_PULL_FILE), ui_font);
}

static void
show_dashboard(void) {
    ShowWindow(main_window, SW_SHOW);
    ShowWindow(main_window, SW_RESTORE);
    SetForegroundWindow(main_window);
}

static void
add_tray_icon(HWND hwnd) {
    if (tray_added) {
        return;
    }

    ZeroMemory(&tray_icon, sizeof(tray_icon));
    tray_icon.cbSize = sizeof(tray_icon);
    tray_icon.hWnd = hwnd;
    tray_icon.uID = 1;
    tray_icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    tray_icon.uCallbackMessage = WM_VR_TRAY;
    tray_icon.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy(tray_icon.szTip, L"VR Mobile");

    tray_added = Shell_NotifyIconW(NIM_ADD, &tray_icon);
}

static void
remove_tray_icon(void) {
    if (tray_added) {
        Shell_NotifyIconW(NIM_DELETE, &tray_icon);
        tray_added = false;
    }
}

static void
show_tray_menu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }

    AppendMenuW(menu, MF_STRING, ID_TRAY_OPEN, L"Open dashboard");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_TRAY_CONNECT, L"Connect");
    AppendMenuW(menu, MF_STRING, ID_TRAY_TAILSCALE, L"Tailscale connect");
    AppendMenuW(menu, MF_STRING, ID_TRAY_DISCONNECT, L"Disconnect");
    AppendMenuW(menu, MF_STRING, ID_TRAY_REFRESH, L"Refresh devices");
    AppendMenuW(menu, MF_STRING, ID_TRAY_STATUS, L"Device status");
    AppendMenuW(menu, MF_STRING, ID_TRAY_WIRELESS, L"Wireless setup");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu,
                MF_STRING | (is_autostart_enabled() ? MF_CHECKED : 0),
                ID_TRAY_AUTOSTART, L"Start with Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(menu);
}

static void
toggle_autostart(void) {
    bool enable = !is_autostart_enabled();
    if (set_autostart_enabled(enable)) {
        sync_autostart_check();
        set_status(enable ? L"Auto start enabled" : L"Auto start disabled");
    } else {
        sync_autostart_check();
        set_status(L"Could not update auto start");
        append_log_line(L"Could not update Windows auto start registry value.");
    }
}

static void
exit_application(HWND hwnd) {
    exiting = true;
    terminate_process_slot(&mirror_process);
    terminate_process_slot(&utility_process);
    DestroyWindow(hwnd);
}

static LRESULT CALLBACK
window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_CREATE:
            create_fonts();

            title_label = create_label(hwnd, L"VR Mobile Dashboard");
            status_label = create_label(hwnd,
                                        L"Ready. Click Refresh Devices first.");
            autostart_check =
                CreateWindowW(L"BUTTON", L"Start with Windows",
                              WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                              0, 0, 0, 0, hwnd,
                              (HMENU) (uintptr_t) ID_CHECK_AUTOSTART,
                              app_instance, NULL);

            create_button(hwnd, L"Connect", ID_BUTTON_CONNECT);
            create_button(hwnd, L"Disconnect", ID_BUTTON_DISCONNECT);
            create_button(hwnd, L"Wireless Setup", ID_BUTTON_WIRELESS);
            create_button(hwnd, L"Refresh Devices", ID_BUTTON_REFRESH);
            create_button(hwnd, L"Device Status", ID_BUTTON_STATUS);
            create_button(hwnd, L"Clear Log", ID_BUTTON_CLEAR);
            create_button(hwnd, L"Exit", ID_BUTTON_EXIT);
            create_button(hwnd, L"Tailscale Connect", ID_BUTTON_TAILSCALE);

            tailscale_label = create_label(hwnd, L"Tailscale address");
            tailscale_addr_edit =
                CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                0, 0, 0, 0, hwnd,
                                (HMENU) ID_TAILSCALE_ADDR, app_instance,
                                NULL);

            devices_heading = create_label(hwnd, L"Devices");
            detail_heading = create_label(hwnd, L"Device Status");
            file_drop_heading = create_label(hwnd, L"File Transfer");
            profile_heading = create_label(hwnd, L"Profiles");
            log_heading = create_label(hwnd, L"Log");

            device_list =
                CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                    LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                                0, 0, 0, 0, hwnd, (HMENU) ID_DEVICE_LIST,
                                app_instance, NULL);

            detail_edit =
                CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                    ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL |
                                    ES_READONLY,
                                0, 0, 0, 0, hwnd, (HMENU) ID_DEVICE_DETAIL,
                                app_instance, NULL);

            file_drop_edit =
                CreateWindowExW(WS_EX_CLIENTEDGE | WS_EX_ACCEPTFILES,
                                L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                    ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL |
                                    ES_READONLY,
                                0, 0, 0, 0, hwnd, (HMENU) ID_FILE_DROP,
                                app_instance, NULL);

            pull_remote_path_edit =
                CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                                VR_FILE_DROP_TARGET,
                                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                0, 0, 0, 0, hwnd,
                                (HMENU) ID_PULL_REMOTE_PATH, app_instance,
                                NULL);
            create_button(hwnd, L"Receive", ID_BUTTON_PULL_FILE);

            profile_list =
                CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                    LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                                0, 0, 0, 0, hwnd, (HMENU) ID_PROFILE_LIST,
                                app_instance, NULL);

            profile_name_edit =
                CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                0, 0, 0, 0, hwnd, (HMENU) ID_PROFILE_NAME,
                                app_instance, NULL);

            create_button(hwnd, L"Refresh", ID_BUTTON_PROFILE_REFRESH);
            create_button(hwnd, L"Save", ID_BUTTON_PROFILE_SAVE);
            create_button(hwnd, L"Run", ID_BUTTON_PROFILE_RUN);
            create_button(hwnd, L"Delete", ID_BUTTON_PROFILE_DELETE);

            profile_args_edit =
                CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                    ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL,
                                0, 0, 0, 0, hwnd, (HMENU) ID_PROFILE_ARGS,
                                app_instance, NULL);

            log_edit =
                CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                    ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL |
                                    ES_READONLY,
                                0, 0, 0, 0, hwnd, (HMENU) ID_LOG,
                                app_instance, NULL);

            apply_fonts(hwnd);
            set_detail(L"Refresh devices to populate this panel.");
            refresh_file_queue_display();
            refresh_profiles();
            sync_autostart_check();
            enable_drag_drop(hwnd);
            enable_drag_drop(file_drop_edit);
            file_drop_edit_wndproc =
                (WNDPROC) SetWindowLongPtrW(file_drop_edit, GWLP_WNDPROC,
                                            (LONG_PTR) file_drop_edit_proc);
            add_tray_icon(hwnd);
            resize_controls(hwnd);
            return 0;

        case WM_SIZE:
            resize_controls(hwnd);
            return 0;

        case WM_COMMAND:
            if (LOWORD(wparam) == ID_DEVICE_LIST
                    && HIWORD(wparam) == LBN_SELCHANGE) {
                update_device_summary();
                return 0;
            }

            if (LOWORD(wparam) == ID_PROFILE_LIST
                    && HIWORD(wparam) == LBN_SELCHANGE) {
                WCHAR name[MAX_PROFILE_NAME_CHARS];
                if (get_profile_name_w(name,
                                       sizeof(name) / sizeof(name[0]))) {
                    load_profile_into_editor(name);
                }
                return 0;
            }

            switch (LOWORD(wparam)) {
                case ID_BUTTON_CONNECT:
                case ID_TRAY_CONNECT:
                    show_dashboard();
                    start_launcher_command(hwnd, VR_LAUNCHER_COMMAND_CONNECT);
                    return 0;
                case ID_BUTTON_TAILSCALE:
                case ID_TRAY_TAILSCALE:
                    show_dashboard();
                    start_launcher_command(hwnd,
                                           VR_LAUNCHER_COMMAND_TAILSCALE_CONNECT);
                    return 0;
                case ID_BUTTON_WIRELESS:
                case ID_TRAY_WIRELESS:
                    show_dashboard();
                    start_launcher_command(hwnd,
                                           VR_LAUNCHER_COMMAND_WIRELESS_SETUP);
                    return 0;
                case ID_BUTTON_STATUS:
                case ID_TRAY_STATUS:
                    show_dashboard();
                    start_launcher_command(hwnd,
                                           VR_LAUNCHER_COMMAND_DEVICE_STATUS);
                    return 0;
                case ID_BUTTON_REFRESH:
                case ID_TRAY_REFRESH:
                    show_dashboard();
                    start_launcher_command(hwnd,
                                           VR_LAUNCHER_COMMAND_CONNECTION_HEALTH);
                    return 0;
                case ID_BUTTON_DISCONNECT:
                case ID_TRAY_DISCONNECT:
                    disconnect_mirror();
                    return 0;
                case ID_BUTTON_CLEAR:
                    SetWindowTextW(log_edit, L"");
                    return 0;
                case ID_BUTTON_EXIT:
                    exit_application(hwnd);
                    return 0;
                case ID_CHECK_AUTOSTART:
                case ID_TRAY_AUTOSTART:
                    toggle_autostart();
                    return 0;
                case ID_BUTTON_PROFILE_REFRESH:
                    refresh_profiles();
                    return 0;
                case ID_BUTTON_PROFILE_SAVE:
                    save_profile_from_editor();
                    return 0;
                case ID_BUTTON_PROFILE_RUN:
                    run_selected_profile(hwnd);
                    return 0;
                case ID_BUTTON_PROFILE_DELETE:
                    delete_selected_profile();
                    return 0;
                case ID_BUTTON_PULL_FILE:
                    receive_file_from_phone(hwnd);
                    return 0;
                case ID_TRAY_OPEN:
                    show_dashboard();
                    return 0;
                case ID_TRAY_EXIT:
                    exit_application(hwnd);
                    return 0;
            }
            break;

        case WM_DROPFILES:
            process_drop_files(hwnd, (HDROP) wparam);
            return 0;

        case WM_VR_TRAY:
            if (lparam == WM_LBUTTONDBLCLK) {
                show_dashboard();
            } else if (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU) {
                show_tray_menu(hwnd);
            }
            return 0;

        case WM_VR_LOG:
            append_log_text((WCHAR *) lparam);
            HeapFree(GetProcessHeap(), 0, (void *) lparam);
            return 0;

        case WM_VR_DONE:
            {
                struct command_done *done = (struct command_done *) lparam;
                WCHAR message[160];
                swprintf(message, sizeof(message) / sizeof(message[0]),
                         L"%hs finished with exit code %lu.",
                         vr_launcher_command_label(done->command),
                         done->exit_code);
                append_log_line(message);

                if (done->exit_code == VR_STATUS_DLL_NOT_FOUND) {
                    append_log_line(L"scrcpy.exe could not start because a DLL "
                                    L"dependency was not found.");
                    append_log_line(L"Make sure MSYS2 mingw64/bin and Android "
                                    L"SDK platform-tools are installed, then "
                                    L"restart VR Mobile.");
                }

                if (done->command == VR_LAUNCHER_COMMAND_CONNECTION_HEALTH
                        && done->exit_code == 0 && done->output) {
                    update_devices_from_health(done->output);
                } else if (done->command == VR_LAUNCHER_COMMAND_DEVICE_STATUS
                        && done->exit_code == 0 && done->output) {
                    update_detail_from_status(done->output);
                    update_device_summary();
                } else if (done->command == VR_LAUNCHER_COMMAND_SEND_FILE) {
                    bool ok = done->exit_code == 0;
                    set_status(ok ? L"File sent" : L"File transfer failed");
                    append_file_transfer_line(
                        ok ? L"Transfer completed. Check " VR_FILE_DROP_TARGET
                           : L"Transfer failed. Check the Log panel above.");
                    start_next_queued_file(hwnd);
                } else if (done->command == VR_LAUNCHER_COMMAND_PULL_FILE) {
                    bool ok = done->exit_code == 0;
                    set_status(ok ? L"File received"
                                  : L"Receive file failed");
                    append_file_transfer_line(
                        ok ? L"File received on computer."
                           : L"Receive failed. Check the Log panel above.");
                } else if (done->mirror) {
                    set_status(done->exit_code == 0 ? L"Mirror stopped"
                                                     : L"Mirror failed");
                } else {
                    set_status(done->exit_code == 0 ? L"Ready"
                                                     : L"Command failed");
                }

                HeapFree(GetProcessHeap(), 0, done->output);
                HeapFree(GetProcessHeap(), 0, done);
            }
            return 0;

        case WM_CLOSE:
            if (exiting) {
                exit_application(hwnd);
            } else {
                ShowWindow(hwnd, SW_HIDE);
                set_status(L"Running in tray");
            }
            return 0;

        case WM_DESTROY:
            disable_drag_drop(hwnd);
            disable_drag_drop(file_drop_edit);
            if (file_drop_edit_wndproc) {
                SetWindowLongPtrW(file_drop_edit, GWLP_WNDPROC,
                                  (LONG_PTR) file_drop_edit_wndproc);
            }
            remove_tray_icon();
            DeleteObject(title_font);
            DeleteObject(ui_font);
            DeleteObject(mono_font);
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

int WINAPI
WinMain(HINSTANCE instance, HINSTANCE prev_instance, LPSTR cmdline,
        int show_cmd) {
    (void) prev_instance;
    (void) cmdline;

    if (process_is_elevated()) {
        if (relaunch_with_explorer_token()) {
            return 0;
        }

        MessageBoxW(NULL,
                    L"VR Mobile is running as Administrator. Windows may "
                    L"block file drag and drop from Explorer. Close VR "
                    L"Mobile and start it without Run as administrator.",
                    L"VR Mobile permissions", MB_OK | MB_ICONWARNING);
    }

    app_instance = instance;
    InitializeCriticalSection(&process_lock);

    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);
    wc.lpszClassName = L"VRMobileLauncherWindow";

    if (!RegisterClassW(&wc)) {
        DeleteCriticalSection(&process_lock);
        return 1;
    }

    main_window = CreateWindowExW(0, wc.lpszClassName, L"VR Mobile",
                                  WS_OVERLAPPEDWINDOW,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 1040, 680,
                                  NULL, NULL, instance, NULL);
    if (!main_window) {
        DeleteCriticalSection(&process_lock);
        return 1;
    }

    ShowWindow(main_window, show_cmd);
    UpdateWindow(main_window);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DeleteCriticalSection(&process_lock);
    return (int) msg.wParam;
}
