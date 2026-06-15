#define UNICODE
#define _UNICODE

#include <windows.h>

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
#define ID_LOG 1101
#define ID_STATUS 1102
#define ID_DEVICE_LIST 1103
#define ID_DEVICE_DETAIL 1104

#define WM_VR_LOG (WM_APP + 1)
#define WM_VR_DONE (WM_APP + 2)

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
    bool has_serial;
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
static HWND devices_heading;
static HWND detail_heading;
static HWND log_heading;
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

static void
set_status(const WCHAR *text) {
    SetWindowTextW(status_label, text);
}

static void
set_detail(const WCHAR *text) {
    SetWindowTextW(detail_edit, text);
}

static void
append_log_text(const WCHAR *text) {
    int length = GetWindowTextLengthW(log_edit);
    SendMessageW(log_edit, EM_SETSEL, (WPARAM) length, (LPARAM) length);
    SendMessageW(log_edit, EM_REPLACESEL, FALSE, (LPARAM) text);
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
    size_t parsed_count =
        vr_launcher_parse_devices(output, parsed, VR_LAUNCHER_MAX_DEVICES);
    if (!parsed_count) {
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
        set_detail(L"No Android devices found. Connect via USB, allow USB "
                   L"debugging, then refresh.");
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

static void
start_launcher_command(HWND hwnd, enum vr_launcher_command command) {
    bool mirror = command == VR_LAUNCHER_COMMAND_CONNECT;

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
            || command == VR_LAUNCHER_COMMAND_DEVICE_STATUS) {
        runner->has_serial = get_selected_serial(runner->serial,
                                                 sizeof(runner->serial));
    }

    WCHAR header[512];
    if (runner->has_serial) {
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
    MoveWindow(status_label, x, y, width - (2 * margin), status_h, TRUE);

    y += status_h + gap;
    int button_w = (width - (2 * margin) - (5 * gap)) / 6;
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

    int left_w = 320;
    if (width < 760) {
        left_w = (width - (2 * margin) - gap) / 2;
    }
    int right_w = width - (2 * margin) - gap - left_w;

    MoveWindow(devices_heading, margin, y, left_w, heading_h, TRUE);
    MoveWindow(detail_heading, margin + left_w + gap, y, right_w, heading_h,
               TRUE);
    y += heading_h;

    MoveWindow(device_list, margin, y, left_w, main_h, TRUE);
    MoveWindow(detail_edit, margin + left_w + gap, y, right_w, main_h, TRUE);

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
    set_font(devices_heading, ui_font);
    set_font(detail_heading, ui_font);
    set_font(log_heading, ui_font);
    set_font(device_list, mono_font);
    set_font(detail_edit, ui_font);
    set_font(log_edit, mono_font);
    set_font(GetDlgItem(hwnd, ID_BUTTON_CONNECT), ui_font);
    set_font(GetDlgItem(hwnd, ID_BUTTON_WIRELESS), ui_font);
    set_font(GetDlgItem(hwnd, ID_BUTTON_STATUS), ui_font);
    set_font(GetDlgItem(hwnd, ID_BUTTON_REFRESH), ui_font);
    set_font(GetDlgItem(hwnd, ID_BUTTON_DISCONNECT), ui_font);
    set_font(GetDlgItem(hwnd, ID_BUTTON_CLEAR), ui_font);
}

static LRESULT CALLBACK
window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_CREATE:
            create_fonts();

            title_label = create_label(hwnd, L"VR Mobile Dashboard");
            status_label = create_label(hwnd,
                                        L"Ready. Click Refresh Devices first.");

            create_button(hwnd, L"Connect", ID_BUTTON_CONNECT);
            create_button(hwnd, L"Disconnect", ID_BUTTON_DISCONNECT);
            create_button(hwnd, L"Wireless Setup", ID_BUTTON_WIRELESS);
            create_button(hwnd, L"Refresh Devices", ID_BUTTON_REFRESH);
            create_button(hwnd, L"Device Status", ID_BUTTON_STATUS);
            create_button(hwnd, L"Clear Log", ID_BUTTON_CLEAR);

            devices_heading = create_label(hwnd, L"Devices");
            detail_heading = create_label(hwnd, L"Device Status");
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

            log_edit =
                CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                    ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL |
                                    ES_READONLY,
                                0, 0, 0, 0, hwnd, (HMENU) ID_LOG,
                                app_instance, NULL);

            apply_fonts(hwnd);
            set_detail(L"Refresh devices to populate this panel.");
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

            switch (LOWORD(wparam)) {
                case ID_BUTTON_CONNECT:
                    start_launcher_command(hwnd, VR_LAUNCHER_COMMAND_CONNECT);
                    return 0;
                case ID_BUTTON_WIRELESS:
                    start_launcher_command(hwnd,
                                           VR_LAUNCHER_COMMAND_WIRELESS_SETUP);
                    return 0;
                case ID_BUTTON_STATUS:
                    start_launcher_command(hwnd,
                                           VR_LAUNCHER_COMMAND_DEVICE_STATUS);
                    return 0;
                case ID_BUTTON_REFRESH:
                    start_launcher_command(hwnd,
                                           VR_LAUNCHER_COMMAND_CONNECTION_HEALTH);
                    return 0;
                case ID_BUTTON_DISCONNECT:
                    disconnect_mirror();
                    return 0;
                case ID_BUTTON_CLEAR:
                    SetWindowTextW(log_edit, L"");
                    return 0;
            }
            break;

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

                if (done->command == VR_LAUNCHER_COMMAND_CONNECTION_HEALTH
                        && done->exit_code == 0 && done->output) {
                    update_devices_from_health(done->output);
                } else if (done->command == VR_LAUNCHER_COMMAND_DEVICE_STATUS
                        && done->exit_code == 0 && done->output) {
                    update_detail_from_status(done->output);
                    update_device_summary();
                } else if (done->mirror) {
                    set_status(done->exit_code == 0 ? L"Mirror stopped"
                                                     : L"Mirror stopped");
                } else {
                    set_status(done->exit_code == 0 ? L"Ready"
                                                     : L"Command failed");
                }

                HeapFree(GetProcessHeap(), 0, done->output);
                HeapFree(GetProcessHeap(), 0, done);
            }
            return 0;

        case WM_CLOSE:
            terminate_process_slot(&mirror_process);
            terminate_process_slot(&utility_process);
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
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
