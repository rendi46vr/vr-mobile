#define UNICODE
#define _UNICODE

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "launcher_commands.h"

#define ID_BUTTON_CONNECT 1001
#define ID_BUTTON_WIRELESS 1002
#define ID_BUTTON_STATUS 1003
#define ID_BUTTON_HEALTH 1004
#define ID_BUTTON_DISCONNECT 1005
#define ID_BUTTON_CLEAR 1006
#define ID_LOG 1101
#define ID_STATUS 1102

#define WM_VR_LOG (WM_APP + 1)
#define WM_VR_DONE (WM_APP + 2)

struct command_runner {
    HWND hwnd;
    enum vr_launcher_command command;
    WCHAR scrcpy_path[MAX_PATH];
};

struct command_done {
    DWORD exit_code;
    enum vr_launcher_command command;
};

static HINSTANCE app_instance;
static HWND main_window;
static HWND status_label;
static HWND log_edit;
static HANDLE active_process;
static CRITICAL_SECTION process_lock;

static void
set_status(const WCHAR *text) {
    SetWindowTextW(status_label, text);
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
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
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
append_ascii_arg(WCHAR *cmdline, size_t len, const char *arg) {
    WCHAR wide[256];
    int converted = MultiByteToWideChar(CP_UTF8, 0, arg, -1, wide,
                                        (int) (sizeof(wide) / sizeof(wide[0])));
    if (!converted) {
        return false;
    }

    size_t used = wcslen(cmdline);
    if (used + 1 >= len) {
        return false;
    }
    cmdline[used++] = L' ';
    cmdline[used] = L'\0';

    return append_quoted_arg(cmdline, len, wide);
}

static bool
build_command_line(const WCHAR *scrcpy_path,
                   enum vr_launcher_command command,
                   WCHAR *cmdline, size_t len) {
    cmdline[0] = L'\0';
    if (!append_quoted_arg(cmdline, len, scrcpy_path)) {
        return false;
    }

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

static void
clear_active_process(HANDLE process) {
    EnterCriticalSection(&process_lock);
    if (active_process == process) {
        active_process = NULL;
    }
    LeaveCriticalSection(&process_lock);
}

static DWORD WINAPI
command_thread(LPVOID userdata) {
    struct command_runner *runner = userdata;

    WCHAR cmdline[4096];
    if (!build_command_line(runner->scrcpy_path, runner->command, cmdline,
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
    active_process = process.hProcess;
    LeaveCriticalSection(&process_lock);

    CloseHandle(process.hThread);

    char buffer[2048];
    DWORD read;
    while (ReadFile(read_pipe, buffer, sizeof(buffer), &read, NULL) && read) {
        post_log_utf8(runner->hwnd, buffer, (int) read);
    }
    CloseHandle(read_pipe);

    WaitForSingleObject(process.hProcess, INFINITE);

    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    clear_active_process(process.hProcess);
    CloseHandle(process.hProcess);

    struct command_done *done = HeapAlloc(GetProcessHeap(), 0, sizeof(*done));
    if (done) {
        done->exit_code = exit_code;
        done->command = runner->command;
        PostMessageW(runner->hwnd, WM_VR_DONE, 0, (LPARAM) done);
    }

    HeapFree(GetProcessHeap(), 0, runner);
    return 0;

end_without_process:
    {
        struct command_done *done = HeapAlloc(GetProcessHeap(), 0, sizeof(*done));
        if (done) {
            done->exit_code = 1;
            done->command = runner->command;
            PostMessageW(runner->hwnd, WM_VR_DONE, 0, (LPARAM) done);
        }
    }
    HeapFree(GetProcessHeap(), 0, runner);
    return 1;
}

static bool
is_command_running(void) {
    EnterCriticalSection(&process_lock);
    bool running = active_process != NULL;
    LeaveCriticalSection(&process_lock);
    return running;
}

static void
start_launcher_command(HWND hwnd, enum vr_launcher_command command) {
    if (is_command_running()) {
        append_log_line(L"Another VR Mobile command is still running.");
        return;
    }

    WCHAR scrcpy_path[MAX_PATH];
    if (!find_scrcpy_path(scrcpy_path, sizeof(scrcpy_path) / sizeof(scrcpy_path[0]))) {
        append_log_line(L"scrcpy.exe was not found next to the launcher or in build\\app.");
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
    wcscpy(runner->scrcpy_path, scrcpy_path);

    WCHAR header[256];
    swprintf(header, sizeof(header) / sizeof(header[0]), L"> %hs",
             vr_launcher_command_label(command));
    append_log_line(header);
    set_status(command == VR_LAUNCHER_COMMAND_CONNECT
                   ? L"Connecting and mirroring..."
                   : L"Running command...");

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
disconnect_active_process(void) {
    EnterCriticalSection(&process_lock);
    HANDLE process = active_process;
    if (process) {
        TerminateProcess(process, 1);
    }
    LeaveCriticalSection(&process_lock);

    if (process) {
        append_log_line(L"Disconnect requested.");
        set_status(L"Disconnecting...");
    } else {
        append_log_line(L"No active scrcpy process.");
    }
}

static HWND
create_button(HWND parent, const WCHAR *label, int id, int x, int y, int w,
              int h) {
    return CreateWindowW(L"BUTTON", label,
                         WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                         x, y, w, h, parent, (HMENU) (uintptr_t) id,
                         app_instance, NULL);
}

static void
resize_controls(HWND hwnd) {
    RECT rect;
    GetClientRect(hwnd, &rect);

    const int margin = 16;
    const int button_h = 32;
    const int gap = 8;
    const int status_h = 26;
    int width = rect.right - rect.left;
    int log_y = margin + button_h + gap + status_h + gap;
    int log_h = rect.bottom - log_y - margin;

    int button_w = (width - (2 * margin) - (5 * gap)) / 6;
    int x = margin;
    const int y = margin;

    MoveWindow(GetDlgItem(hwnd, ID_BUTTON_CONNECT), x, y, button_w, button_h,
               TRUE);
    x += button_w + gap;
    MoveWindow(GetDlgItem(hwnd, ID_BUTTON_WIRELESS), x, y, button_w, button_h,
               TRUE);
    x += button_w + gap;
    MoveWindow(GetDlgItem(hwnd, ID_BUTTON_STATUS), x, y, button_w, button_h,
               TRUE);
    x += button_w + gap;
    MoveWindow(GetDlgItem(hwnd, ID_BUTTON_HEALTH), x, y, button_w, button_h,
               TRUE);
    x += button_w + gap;
    MoveWindow(GetDlgItem(hwnd, ID_BUTTON_DISCONNECT), x, y, button_w, button_h,
               TRUE);
    x += button_w + gap;
    MoveWindow(GetDlgItem(hwnd, ID_BUTTON_CLEAR), x, y, button_w, button_h,
               TRUE);

    MoveWindow(status_label, margin, margin + button_h + gap,
               width - (2 * margin), status_h, TRUE);
    MoveWindow(log_edit, margin, log_y, width - (2 * margin), log_h, TRUE);
}

static LRESULT CALLBACK
window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_CREATE:
            create_button(hwnd, L"Connect", ID_BUTTON_CONNECT, 0, 0, 0, 0);
            create_button(hwnd, L"Wireless Setup", ID_BUTTON_WIRELESS, 0, 0, 0,
                          0);
            create_button(hwnd, L"Device Status", ID_BUTTON_STATUS, 0, 0, 0, 0);
            create_button(hwnd, L"Health", ID_BUTTON_HEALTH, 0, 0, 0, 0);
            create_button(hwnd, L"Disconnect", ID_BUTTON_DISCONNECT, 0, 0, 0,
                          0);
            create_button(hwnd, L"Clear Log", ID_BUTTON_CLEAR, 0, 0, 0, 0);

            status_label =
                CreateWindowW(L"STATIC", L"Ready",
                              WS_CHILD | WS_VISIBLE | SS_LEFT,
                              0, 0, 0, 0, hwnd, (HMENU) ID_STATUS,
                              app_instance, NULL);

            log_edit =
                CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                    ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL |
                                    ES_READONLY,
                                0, 0, 0, 0, hwnd, (HMENU) ID_LOG,
                                app_instance, NULL);

            SendMessageW(log_edit, WM_SETFONT,
                         (WPARAM) GetStockObject(DEFAULT_GUI_FONT), TRUE);
            SendMessageW(status_label, WM_SETFONT,
                         (WPARAM) GetStockObject(DEFAULT_GUI_FONT), TRUE);
            resize_controls(hwnd);
            return 0;

        case WM_SIZE:
            resize_controls(hwnd);
            return 0;

        case WM_COMMAND:
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
                case ID_BUTTON_HEALTH:
                    start_launcher_command(hwnd,
                                           VR_LAUNCHER_COMMAND_CONNECTION_HEALTH);
                    return 0;
                case ID_BUTTON_DISCONNECT:
                    disconnect_active_process();
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
                WCHAR message[128];
                swprintf(message, sizeof(message) / sizeof(message[0]),
                         L"%hs finished with exit code %lu.",
                         vr_launcher_command_label(done->command),
                         done->exit_code);
                append_log_line(message);
                set_status(done->exit_code == 0 ? L"Ready" : L"Command failed");
                HeapFree(GetProcessHeap(), 0, done);
            }
            return 0;

        case WM_CLOSE:
            disconnect_active_process();
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
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
                                  CW_USEDEFAULT, CW_USEDEFAULT, 900, 560,
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
