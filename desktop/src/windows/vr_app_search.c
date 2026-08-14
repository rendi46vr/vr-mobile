#define UNICODE
#define _UNICODE

#include "vr_app_search.h"

#include "vr_file_cache.h"
#include "vr_theme.h"

#include <dwmapi.h>
#include <gdiplus/gdiplus.h>
#include <shellapi.h>

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define VR_APP_SEARCH_HOTKEY_ID 0x565246
#define WM_VR_APP_LIST_READY (WM_APP + 41)
#define WM_VR_APP_LAUNCH_DONE (WM_APP + 42)
#define WM_VR_APP_ICONS_READY (WM_APP + 44)
#define WM_VR_FILE_OPEN_DONE (WM_APP + 45)
#define WM_VR_FILE_CACHE_READY (WM_APP + 46)
#define ID_SEARCH_EDIT 5101
#define ID_SEARCH_LIST 5102
#define ID_SEARCH_CLOSE 5103
#define MAX_REMOTE_APPS 768
#define MAX_SERIAL_LEN 256
#define MAX_PACKAGE_LEN 256
#define SEARCH_WIDTH 680
#define SEARCH_COLLAPSED_HEIGHT 96
#define SEARCH_EXPANDED_HEIGHT 520
#define VR_HOTKEY_REPLAY_MARKER ((ULONG_PTR) 0x5652464D)

#ifndef MOD_NOREPEAT
# define MOD_NOREPEAT 0x4000
#endif

struct remote_app {
    WCHAR name[192];
    char package_name[MAX_PACKAGE_LEN];
    int user_id;
    GpImage *icon;
};

struct list_runner {
    HWND hwnd;
    WCHAR scrcpy_path[32768];
    char serial[MAX_SERIAL_LEN];
};

struct list_result {
    DWORD exit_code;
    char serial[MAX_SERIAL_LEN];
    struct remote_app apps[MAX_REMOTE_APPS];
    size_t count;
};

struct launch_runner {
    HWND hwnd;
    WCHAR adb_path[32768];
    char serial[MAX_SERIAL_LEN];
    char package_name[MAX_PACKAGE_LEN];
    WCHAR app_name[192];
    int user_id;
};

struct launch_result {
    DWORD exit_code;
    WCHAR app_name[192];
};

struct icon_runner {
    HWND hwnd;
    WCHAR scrcpy_path[32768];
    char serial[MAX_SERIAL_LEN];
};

struct icon_result {
    DWORD exit_code;
    char serial[MAX_SERIAL_LEN];
    size_t saved_count;
};

struct file_open_runner {
    HWND hwnd;
    WCHAR adb_path[32768];
    char serial[MAX_SERIAL_LEN];
    char remote_path[VR_FILE_MANAGER_MAX_PATH];
    WCHAR local_path[32768];
    uint64_t size;
    int64_t modified;
};

struct file_open_result {
    DWORD exit_code;
    char serial[MAX_SERIAL_LEN];
    char remote_path[VR_FILE_MANAGER_MAX_PATH];
    WCHAR local_path[32768];
    uint64_t size;
    int64_t modified;
    char detail[768];
};

struct file_cache_runner {
    HWND hwnd;
    char serial[MAX_SERIAL_LEN];
};

struct file_cache_result {
    char serial[MAX_SERIAL_LEN];
    struct vr_file_cache *cache;
};

static HWND search_window;
static HWND search_edit;
static HWND search_list;
static HWND search_hint;
static HWND search_close;
static HFONT search_font;
static HFONT search_small_font;
static WNDPROC search_edit_original_proc;
static WNDPROC search_list_original_proc;
static struct remote_app remote_apps[MAX_REMOTE_APPS];
static size_t remote_app_count;
static size_t filtered_indices[MAX_REMOTE_APPS];
static size_t filtered_count;
static struct vr_file_cache file_cache;
static bool file_cache_initialized;
static size_t filtered_file_indices[VR_FILE_CACHE_SEARCH_LIMIT];
static bool file_search_mode;
static char active_serial[MAX_SERIAL_LEN];
static WCHAR active_adb_path[32768];
static WCHAR active_scrcpy_path[32768];
static HHOOK shortcut_hook;
static HWND shortcut_owner;
static bool hotkey_registered;
static bool list_loading;
static bool search_expanded;
static bool pending_win;
static bool consumed_win_f;
static DWORD pending_win_vk;
static bool ctrl_down;
static bool alt_down;
static bool fn_down;
static bool consumed_tailscale_m;
static bool icons_loading;
static bool file_open_loading;
static bool file_cache_loading;
static ULONG_PTR gdiplus_token;
static vr_app_search_status_fn status_callback;
static vr_app_search_focus_fn remote_focus_callback;

static DWORD WINAPI
launch_thread(LPVOID userdata);

static void
open_selected_file(void);

static void
start_file_cache_refresh(void);

static bool
valid_package_name(const char *name);

static int __cdecl
compare_apps(const void *a, const void *b);

static void
set_search_expanded(bool expanded);

static void
copy_wide(WCHAR *dst, size_t dst_len, const WCHAR *src) {
    if (!dst_len) {
        return;
    }
    wcsncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = L'\0';
}

static void
copy_ascii(char *dst, size_t dst_len, const char *src) {
    if (!dst_len) {
        return;
    }
    strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

static bool
build_cache_path(const char *serial, WCHAR *path, size_t path_len) {
    WCHAR base[32768];
    DWORD len = GetEnvironmentVariableW(
        L"LOCALAPPDATA", base, sizeof(base) / sizeof(base[0]));
    if (!len || len >= sizeof(base) / sizeof(base[0])) {
        return false;
    }

    WCHAR app_dir[32768];
    WCHAR cache_dir[32768];
    if (swprintf(app_dir, sizeof(app_dir) / sizeof(app_dir[0]),
                 L"%ls\\VR Mobile", base) <= 0
            || swprintf(cache_dir,
                        sizeof(cache_dir) / sizeof(cache_dir[0]),
                        L"%ls\\Cache", app_dir) <= 0) {
        return false;
    }
    CreateDirectoryW(app_dir, NULL);
    CreateDirectoryW(cache_dir, NULL);

    WCHAR safe_serial[MAX_SERIAL_LEN];
    size_t i = 0;
    for (; serial[i] && i + 1 < sizeof(safe_serial) / sizeof(safe_serial[0]);
         ++i) {
        unsigned char ch = (unsigned char) serial[i];
        safe_serial[i] = isalnum(ch) || ch == '-' || ch == '_'
                       ? (WCHAR) ch : L'_';
    }
    safe_serial[i] = L'\0';
    return swprintf(path, path_len, L"%ls\\apps-%ls.vr-cache",
                    cache_dir, safe_serial) > 0;
}

static bool
build_icon_path(const char *serial, const char *package_name,
                WCHAR *path, size_t path_len) {
    WCHAR base[32768];
    DWORD len = GetEnvironmentVariableW(
        L"LOCALAPPDATA", base, sizeof(base) / sizeof(base[0]));
    if (!len || len >= sizeof(base) / sizeof(base[0])
            || !valid_package_name(package_name)) {
        return false;
    }

    WCHAR app_dir[32768];
    WCHAR cache_dir[32768];
    WCHAR icons_dir[32768];
    WCHAR device_dir[32768];
    WCHAR safe_serial[MAX_SERIAL_LEN];
    size_t i = 0;
    for (; serial[i] && i + 1 < sizeof(safe_serial) / sizeof(safe_serial[0]);
         ++i) {
        unsigned char ch = (unsigned char) serial[i];
        safe_serial[i] = isalnum(ch) || ch == '-' || ch == '_'
                       ? (WCHAR) ch : L'_';
    }
    safe_serial[i] = L'\0';

    if (swprintf(app_dir, sizeof(app_dir) / sizeof(app_dir[0]),
                 L"%ls\\VR Mobile", base) <= 0
            || swprintf(cache_dir,
                        sizeof(cache_dir) / sizeof(cache_dir[0]),
                        L"%ls\\Cache", app_dir) <= 0
            || swprintf(icons_dir,
                        sizeof(icons_dir) / sizeof(icons_dir[0]),
                        L"%ls\\icons", cache_dir) <= 0
            || swprintf(device_dir,
                        sizeof(device_dir) / sizeof(device_dir[0]),
                        L"%ls\\%ls", icons_dir, safe_serial) <= 0) {
        return false;
    }
    CreateDirectoryW(app_dir, NULL);
    CreateDirectoryW(cache_dir, NULL);
    CreateDirectoryW(icons_dir, NULL);
    CreateDirectoryW(device_dir, NULL);
    return swprintf(path, path_len, L"%ls\\%hs.png",
                    device_dir, package_name) > 0;
}

static void
free_app_icons(void) {
    for (size_t i = 0; i < remote_app_count; ++i) {
        if (remote_apps[i].icon) {
            GdipDisposeImage(remote_apps[i].icon);
            remote_apps[i].icon = NULL;
        }
    }
}

static void
load_cached_icon(size_t index) {
    if (index >= remote_app_count || remote_apps[index].icon) {
        return;
    }
    WCHAR path[32768];
    if (build_icon_path(active_serial, remote_apps[index].package_name,
                        path, sizeof(path) / sizeof(path[0]))) {
        GpBitmap *bitmap = NULL;
        if (GdipCreateBitmapFromFile(path, &bitmap) == Ok) {
            remote_apps[index].icon = (GpImage *) bitmap;
        }
    }
}

static bool
write_all(HANDLE file, const void *data, DWORD size) {
    const char *cursor = data;
    while (size) {
        DWORD written = 0;
        if (!WriteFile(file, cursor, size, &written, NULL) || !written) {
            return false;
        }
        cursor += written;
        size -= written;
    }
    return true;
}

static void
save_app_cache(const char *serial, const struct remote_app *apps,
               size_t count) {
    WCHAR path[32768];
    if (!build_cache_path(serial, path, sizeof(path) / sizeof(path[0]))) {
        return;
    }
    WCHAR temp_path[32768];
    if (swprintf(temp_path,
                 sizeof(temp_path) / sizeof(temp_path[0]),
                 L"%ls.tmp", path) <= 0) {
        return;
    }

    HANDLE file = CreateFileW(temp_path, GENERIC_WRITE, 0, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    bool ok = write_all(file, "VRAPP1\n", 7);
    for (size_t i = 0; ok && i < count; ++i) {
        char name[768];
        int converted = WideCharToMultiByte(
            CP_UTF8, 0, apps[i].name, -1, name, sizeof(name), NULL, NULL);
        if (!converted) {
            continue;
        }
        for (char *p = name; *p; ++p) {
            if (*p == '\t' || *p == '\r' || *p == '\n') {
                *p = ' ';
            }
        }

        char prefix[384];
        int prefix_len = snprintf(prefix, sizeof(prefix), "%d\t%s\t",
                                  apps[i].user_id, apps[i].package_name);
        ok = prefix_len > 0 && (size_t) prefix_len < sizeof(prefix)
          && write_all(file, prefix, (DWORD) prefix_len)
          && write_all(file, name, (DWORD) strlen(name))
          && write_all(file, "\n", 1);
    }
    CloseHandle(file);

    if (ok) {
        MoveFileExW(temp_path, path,
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    } else {
        DeleteFileW(temp_path);
    }
}

static size_t
load_app_cache(const char *serial, struct remote_app *apps, size_t max_apps) {
    WCHAR path[32768];
    if (!build_cache_path(serial, path, sizeof(path) / sizeof(path[0]))) {
        return 0;
    }
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0
            || size.QuadPart > 4 * 1024 * 1024) {
        CloseHandle(file);
        return 0;
    }
    char *data = HeapAlloc(GetProcessHeap(), 0, (SIZE_T) size.QuadPart + 1);
    if (!data) {
        CloseHandle(file);
        return 0;
    }
    DWORD read = 0;
    bool ok = ReadFile(file, data, (DWORD) size.QuadPart, &read, NULL);
    CloseHandle(file);
    if (!ok) {
        HeapFree(GetProcessHeap(), 0, data);
        return 0;
    }
    data[read] = '\0';

    size_t count = 0;
    char *context = NULL;
    char *line = strtok_s(data, "\r\n", &context);
    if (!line || strcmp(line, "VRAPP1")) {
        HeapFree(GetProcessHeap(), 0, data);
        return 0;
    }
    while ((line = strtok_s(NULL, "\r\n", &context)) && count < max_apps) {
        char *first_tab = strchr(line, '\t');
        if (!first_tab) {
            continue;
        }
        *first_tab++ = '\0';
        char *second_tab = strchr(first_tab, '\t');
        if (!second_tab) {
            continue;
        }
        *second_tab++ = '\0';
        char *end = NULL;
        long user_id = strtol(line, &end, 10);
        if (!end || *end || user_id < 0 || user_id > INT32_MAX
                || !valid_package_name(first_tab) || !second_tab[0]) {
            continue;
        }

        struct remote_app *app = &apps[count];
        int converted = MultiByteToWideChar(
            CP_UTF8, 0, second_tab, -1, app->name,
            sizeof(app->name) / sizeof(app->name[0]));
        if (!converted) {
            continue;
        }
        copy_ascii(app->package_name, sizeof(app->package_name), first_tab);
        app->user_id = (int) user_id;
        ++count;
    }
    HeapFree(GetProcessHeap(), 0, data);
    qsort(apps, count, sizeof(apps[0]), compare_apps);
    return count;
}

static void
set_hint(const WCHAR *text) {
    if (search_hint) {
        SetWindowTextW(search_hint, text);
    }
}

static void
activate_search_window(void) {
    HWND foreground = GetForegroundWindow();
    DWORD foreground_thread = foreground
                            ? GetWindowThreadProcessId(foreground, NULL) : 0;
    DWORD current_thread = GetCurrentThreadId();
    bool attached = foreground_thread && foreground_thread != current_thread
                 && AttachThreadInput(current_thread, foreground_thread, TRUE);

    ShowWindow(search_window, SW_SHOW);
    BringWindowToTop(search_window);
    SetForegroundWindow(search_window);
    SetActiveWindow(search_window);
    SetFocus(search_edit);

    if (attached) {
        AttachThreadInput(current_thread, foreground_thread, FALSE);
    }
}

static bool
contains_case_insensitive(const WCHAR *haystack, const WCHAR *needle) {
    if (!*needle) {
        return true;
    }

    size_t needle_len = wcslen(needle);
    for (const WCHAR *cursor = haystack; *cursor; ++cursor) {
        if (!_wcsnicmp(cursor, needle, needle_len)) {
            return true;
        }
    }
    return false;
}

static int __cdecl
compare_apps(const void *a, const void *b) {
    const struct remote_app *left = a;
    const struct remote_app *right = b;
    return _wcsicmp(left->name, right->name);
}

static void
refresh_filter(void) {
    WCHAR query[256] = L"";
    GetWindowTextW(search_edit, query, sizeof(query) / sizeof(query[0]));

    SendMessageW(search_list, LB_RESETCONTENT, 0, 0);
    filtered_count = 0;
    file_search_mode = !_wcsicmp(query, L"/files")
                    || !_wcsnicmp(query, L"/files ", 7);
    if (file_search_mode) {
        const WCHAR *file_query = query + 6;
        while (*file_query == L' ') {
            ++file_query;
        }
        filtered_count = vr_file_cache_search_files(
            &file_cache, file_query, filtered_file_indices,
            VR_FILE_CACHE_SEARCH_LIMIT);
        for (size_t i = 0; i < filtered_count; ++i) {
            const char *path = file_cache.entries[filtered_file_indices[i]].path;
            const char *name = strrchr(path, '/');
            WCHAR wide[VR_FILE_MANAGER_MAX_NAME];
            MultiByteToWideChar(CP_UTF8, 0, name ? name + 1 : path, -1,
                                wide, sizeof(wide) / sizeof(wide[0]));
            SendMessageW(search_list, LB_ADDSTRING, 0, (LPARAM) wide);
        }
        if (filtered_count) {
            SendMessageW(search_list, LB_SETCURSEL, 0, 0);
            WCHAR hint[160];
            swprintf(hint, sizeof(hint) / sizeof(hint[0]),
                     L"%zu cached file%s  •  Enter to download/open",
                     filtered_count, filtered_count == 1 ? L"" : L"s");
            set_hint(hint);
        } else if (file_cache_loading) {
            set_hint(L"Loading the file index in the background...");
        } else if (!file_cache.count) {
            set_hint(L"No file index yet — use Load all files in File Manager");
        } else {
            set_hint(L"No matching file in the local index");
        }
        InvalidateRect(search_list, NULL, TRUE);
        return;
    }

    for (size_t i = 0; i < remote_app_count; ++i) {
        WCHAR package_wide[MAX_PACKAGE_LEN];
        MultiByteToWideChar(CP_UTF8, 0, remote_apps[i].package_name, -1,
                            package_wide,
                            sizeof(package_wide) / sizeof(package_wide[0]));
        if (!contains_case_insensitive(remote_apps[i].name, query)
                && !contains_case_insensitive(package_wide, query)) {
            continue;
        }

        filtered_indices[filtered_count] = i;
        SendMessageW(search_list, LB_ADDSTRING, 0,
                     (LPARAM) remote_apps[i].name);
        ++filtered_count;
    }

    if (filtered_count) {
        SendMessageW(search_list, LB_SETCURSEL, 0, 0);
        WCHAR hint[128];
        swprintf(hint, sizeof(hint) / sizeof(hint[0]),
                 L"%zu app%s  \x2022  Enter to open  \x2022  Esc to close",
                 filtered_count, filtered_count == 1 ? L"" : L"s");
        set_hint(hint);
    } else if (!list_loading) {
        set_hint(L"No matching application found on this phone");
    }
    InvalidateRect(search_list, NULL, TRUE);
}

static bool
valid_package_name(const char *name) {
    if (!name[0]) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *) name; *p; ++p) {
        if (!isalnum(*p) && *p != '.' && *p != '_') {
            return false;
        }
    }
    return true;
}

static bool
valid_component_name(const char *name) {
    if (!name[0] || !strchr(name, '/')) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *) name; *p; ++p) {
        if (!isalnum(*p) && *p != '.' && *p != '_' && *p != '/'
                && *p != '$') {
            return false;
        }
    }
    return true;
}

static void
launch_selected_app(void) {
    if (file_search_mode) {
        open_selected_file();
        return;
    }
    int selected = (int) SendMessageW(search_list, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR || selected < 0
            || (size_t) selected >= filtered_count) {
        return;
    }

    const struct remote_app *app =
        &remote_apps[filtered_indices[(size_t) selected]];
    if (!valid_package_name(app->package_name)) {
        set_hint(L"This application has an invalid package name");
        return;
    }

    struct launch_runner *runner = HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*runner));
    if (!runner) {
        return;
    }
    runner->hwnd = search_window;
    copy_wide(runner->adb_path,
              sizeof(runner->adb_path) / sizeof(runner->adb_path[0]),
              active_adb_path);
    copy_ascii(runner->serial, sizeof(runner->serial), active_serial);
    copy_ascii(runner->package_name, sizeof(runner->package_name),
               app->package_name);
    copy_wide(runner->app_name,
              sizeof(runner->app_name) / sizeof(runner->app_name[0]),
              app->name);
    runner->user_id = app->user_id;

    WCHAR status[256];
    swprintf(status, sizeof(status) / sizeof(status[0]), L"Opening %ls...",
             app->name);
    set_hint(status);
    if (status_callback) {
        status_callback(status);
    }

    ShowWindow(search_window, SW_HIDE);
    bool remote_handles_launch = remote_focus_callback
        && remote_focus_callback(app->package_name, app->user_id);
    if (remote_handles_launch) {
        // --start-app is attached to the new Tailscale scrcpy process.
        HeapFree(GetProcessHeap(), 0, runner);
        return;
    }

    HANDLE thread = CreateThread(NULL, 0, launch_thread, runner, 0, NULL);
    if (!thread) {
        HeapFree(GetProcessHeap(), 0, runner);
        set_hint(L"Could not start the application command");
        return;
    }
    CloseHandle(thread);
}

static void
move_list_selection(int delta) {
    int count = (int) SendMessageW(search_list, LB_GETCOUNT, 0, 0);
    if (count <= 0) {
        return;
    }
    int selected = (int) SendMessageW(search_list, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR) {
        selected = 0;
    } else {
        selected += delta;
        if (selected < 0) {
            selected = 0;
        } else if (selected >= count) {
            selected = count - 1;
        }
    }
    SendMessageW(search_list, LB_SETCURSEL, (WPARAM) selected, 0);
    SendMessageW(search_list, LB_SETTOPINDEX,
                 (WPARAM) (selected > 2 ? selected - 2 : 0), 0);
}

static LRESULT CALLBACK
search_child_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (hwnd == search_edit && msg == WM_LBUTTONDOWN) {
        set_search_expanded(true);
    }
    if (msg == WM_KEYDOWN) {
        switch (wparam) {
            case VK_ESCAPE:
                ShowWindow(search_window, SW_HIDE);
                return 0;
            case VK_RETURN:
                if (search_expanded && filtered_count) {
                    launch_selected_app();
                } else {
                    set_search_expanded(true);
                }
                return 0;
            case VK_DOWN:
                set_search_expanded(true);
                move_list_selection(1);
                SetFocus(search_edit);
                return 0;
            case VK_UP:
                set_search_expanded(true);
                move_list_selection(-1);
                SetFocus(search_edit);
                return 0;
        }
    }

    WNDPROC original = hwnd == search_edit
                      ? search_edit_original_proc
                      : search_list_original_proc;
    return CallWindowProcW(original, hwnd, msg, wparam, lparam);
}

static char *
capture_process(const WCHAR *application, WCHAR *command_line,
                DWORD *exit_code) {
    SECURITY_ATTRIBUTES attrs = {
        .nLength = sizeof(attrs),
        .lpSecurityDescriptor = NULL,
        .bInheritHandle = TRUE,
    };
    HANDLE read_pipe;
    HANDLE write_pipe;
    if (!CreatePipe(&read_pipe, &write_pipe, &attrs, 0)) {
        return NULL;
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
    BOOL started = CreateProcessW(application, command_line, NULL, NULL, TRUE,
                                  CREATE_NO_WINDOW, NULL, NULL, &startup,
                                  &process);
    CloseHandle(write_pipe);
    if (!started) {
        CloseHandle(read_pipe);
        return NULL;
    }
    CloseHandle(process.hThread);

    size_t length = 0;
    size_t capacity = 8192;
    char *output = HeapAlloc(GetProcessHeap(), 0, capacity);
    if (!output) {
        CloseHandle(read_pipe);
        WaitForSingleObject(process.hProcess, INFINITE);
        CloseHandle(process.hProcess);
        return NULL;
    }

    char buffer[4096];
    DWORD read;
    while (ReadFile(read_pipe, buffer, sizeof(buffer), &read, NULL) && read) {
        if (length + read + 1 > capacity) {
            size_t next = capacity * 2;
            while (next < length + read + 1) {
                next *= 2;
            }
            char *grown = HeapReAlloc(GetProcessHeap(), 0, output, next);
            if (!grown) {
                HeapFree(GetProcessHeap(), 0, output);
                output = NULL;
                break;
            }
            output = grown;
            capacity = next;
        }
        memcpy(output + length, buffer, read);
        length += read;
    }
    CloseHandle(read_pipe);
    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, exit_code);
    CloseHandle(process.hProcess);
    if (output) {
        output[length] = '\0';
    }
    return output;
}

static bool
append_quoted_command_arg(WCHAR *command, size_t command_len,
                          const WCHAR *arg) {
    size_t used = wcslen(command);
    if (used && used + 1 < command_len) command[used++] = L' ';
    if (used + 2 >= command_len) return false;
    command[used++] = L'"';
    unsigned slashes = 0;
    for (const WCHAR *p = arg; *p; ++p) {
        if (*p == L'\\') { ++slashes; continue; }
        if (*p == L'"') {
            while (slashes) {
                if (used + 2 >= command_len) return false;
                command[used++] = L'\\'; command[used++] = L'\\';
                --slashes;
            }
            if (used + 2 >= command_len) return false;
            command[used++] = L'\\'; command[used++] = L'"';
            slashes = 0;
            continue;
        }
        while (slashes) {
            if (used + 1 >= command_len) return false;
            command[used++] = L'\\'; --slashes;
        }
        if (used + 1 >= command_len) return false;
        command[used++] = *p;
    }
    while (slashes) {
        if (used + 2 >= command_len) return false;
        command[used++] = L'\\'; command[used++] = L'\\';
        --slashes;
    }
    if (used + 2 >= command_len) return false;
    command[used++] = L'"'; command[used] = L'\0';
    return true;
}

static DWORD WINAPI
file_open_thread(LPVOID userdata) {
    struct file_open_runner *runner = userdata;
    struct file_open_result *result = HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*result));
    if (!result) {
        HeapFree(GetProcessHeap(), 0, runner);
        return 1;
    }
    copy_ascii(result->serial, sizeof(result->serial), runner->serial);
    copy_ascii(result->remote_path, sizeof(result->remote_path),
               runner->remote_path);
    copy_wide(result->local_path,
              sizeof(result->local_path) / sizeof(result->local_path[0]),
              runner->local_path);
    result->size = runner->size;
    result->modified = runner->modified;

    WCHAR remote[VR_FILE_MANAGER_MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, runner->remote_path, -1, remote,
                        sizeof(remote) / sizeof(remote[0]));
    WCHAR serial[MAX_SERIAL_LEN];
    MultiByteToWideChar(CP_UTF8, 0, runner->serial, -1, serial,
                        sizeof(serial) / sizeof(serial[0]));
    result->exit_code = 1;

    if (strchr(runner->serial, ':')) {
        WCHAR connect_command[32768] = L"";
        bool connect_ok = append_quoted_command_arg(
                              connect_command,
                              sizeof(connect_command) / sizeof(connect_command[0]),
                              runner->adb_path)
                       && append_quoted_command_arg(
                              connect_command,
                              sizeof(connect_command) / sizeof(connect_command[0]),
                              L"connect")
                       && append_quoted_command_arg(
                              connect_command,
                              sizeof(connect_command) / sizeof(connect_command[0]),
                              serial);
        if (connect_ok) {
            DWORD connect_exit = 1;
            char *connect_output = capture_process(
                runner->adb_path, connect_command, &connect_exit);
            HeapFree(GetProcessHeap(), 0, connect_output);
        }
    }

    for (unsigned attempt = 0; attempt < 3 && result->exit_code; ++attempt) {
        WCHAR command[32768] = L"";
        bool command_ok = append_quoted_command_arg(
                              command, sizeof(command) / sizeof(command[0]),
                              runner->adb_path)
                       && append_quoted_command_arg(
                              command, sizeof(command) / sizeof(command[0]), L"-s")
                       && append_quoted_command_arg(
                              command, sizeof(command) / sizeof(command[0]), serial)
                       && append_quoted_command_arg(
                              command, sizeof(command) / sizeof(command[0]), L"pull")
                       && append_quoted_command_arg(
                              command, sizeof(command) / sizeof(command[0]), remote)
                       && append_quoted_command_arg(
                              command, sizeof(command) / sizeof(command[0]),
                              runner->local_path);
        if (!command_ok) {
            snprintf(result->detail, sizeof(result->detail),
                     "Could not build the ADB pull command.");
            break;
        }
        char *output = capture_process(runner->adb_path, command,
                                       &result->exit_code);
        if (output && output[0]) {
            snprintf(result->detail, sizeof(result->detail), "%s", output);
        }
        HeapFree(GetProcessHeap(), 0, output);
        if (result->exit_code && attempt < 2) {
            Sleep(250 + attempt * 250);
        }
    }
    if (!result->exit_code
            && GetFileAttributesW(result->local_path)
                    == INVALID_FILE_ATTRIBUTES) {
        result->exit_code = ERROR_FILE_NOT_FOUND;
        snprintf(result->detail, sizeof(result->detail),
                 "ADB reported success, but the downloaded file was not found.");
    }
    DWORD thread_exit = result->exit_code;
    PostMessageW(runner->hwnd, WM_VR_FILE_OPEN_DONE, 0, (LPARAM) result);
    HeapFree(GetProcessHeap(), 0, runner);
    return thread_exit;
}

static DWORD WINAPI
file_cache_thread(LPVOID userdata) {
    struct file_cache_runner *runner = userdata;
    struct file_cache_result *result = HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*result));
    if (!result) {
        HeapFree(GetProcessHeap(), 0, runner);
        return 1;
    }
    copy_ascii(result->serial, sizeof(result->serial), runner->serial);
    result->cache = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                              sizeof(*result->cache));
    if (result->cache) {
        vr_file_cache_init(result->cache, runner->serial);
        vr_file_cache_load(result->cache);
    }
    if (!PostMessageW(runner->hwnd, WM_VR_FILE_CACHE_READY, 0,
                      (LPARAM) result)) {
        if (result->cache) {
            vr_file_cache_destroy(result->cache);
            HeapFree(GetProcessHeap(), 0, result->cache);
        }
        HeapFree(GetProcessHeap(), 0, result);
    }
    HeapFree(GetProcessHeap(), 0, runner);
    return 0;
}

static void
start_file_cache_refresh(void) {
    if (file_cache_loading || !search_window || !active_serial[0]) {
        return;
    }
    struct file_cache_runner *runner = HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*runner));
    if (!runner) return;
    runner->hwnd = search_window;
    copy_ascii(runner->serial, sizeof(runner->serial), active_serial);
    HANDLE thread = CreateThread(NULL, 0, file_cache_thread, runner, 0, NULL);
    if (!thread) {
        HeapFree(GetProcessHeap(), 0, runner);
        return;
    }
    file_cache_loading = true;
    CloseHandle(thread);
}

static void
open_selected_file(void) {
    if (file_open_loading) {
        return;
    }
    int selected = (int) SendMessageW(search_list, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR || selected < 0
            || (size_t) selected >= filtered_count) return;
    const struct vr_file_cache_entry *entry =
        &file_cache.entries[filtered_file_indices[(size_t) selected]];
    WCHAR local[32768];
    if (vr_file_cache_download_is_current(active_serial, entry, local,
                                           sizeof(local) / sizeof(local[0]))) {
        if ((INT_PTR) ShellExecuteW(search_window, L"open", local, NULL, NULL,
                                    SW_SHOWNORMAL) > 32) {
            ShowWindow(search_window, SW_HIDE);
        } else {
            set_hint(L"Windows could not open this downloaded file");
        }
        return;
    }
    if (!vr_file_cache_download_path(active_serial, entry, local,
                                     sizeof(local) / sizeof(local[0]))) {
        set_hint(L"Could not prepare the local download cache");
        return;
    }
    struct file_open_runner *runner = HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*runner));
    if (!runner) return;
    runner->hwnd = search_window;
    copy_wide(runner->adb_path,
              sizeof(runner->adb_path) / sizeof(runner->adb_path[0]),
              active_adb_path);
    copy_wide(runner->local_path,
              sizeof(runner->local_path) / sizeof(runner->local_path[0]), local);
    copy_ascii(runner->serial, sizeof(runner->serial), active_serial);
    copy_ascii(runner->remote_path, sizeof(runner->remote_path), entry->path);
    runner->size = entry->size;
    runner->modified = entry->modified;
    file_open_loading = true;
    EnableWindow(search_edit, FALSE);
    EnableWindow(search_list, FALSE);
    set_hint(L"Connecting and downloading the file from the phone...");
    HANDLE thread = CreateThread(NULL, 0, file_open_thread, runner, 0, NULL);
    if (!thread) {
        HeapFree(GetProcessHeap(), 0, runner);
        file_open_loading = false;
        EnableWindow(search_edit, TRUE);
        EnableWindow(search_list, TRUE);
        set_hint(L"Could not start the file download");
        return;
    }
    CloseHandle(thread);
}

static void
trim_ascii(char *text) {
    char *start = text;
    while (*start && isspace((unsigned char) *start)) {
        ++start;
    }
    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }
    size_t len = strlen(text);
    while (len && isspace((unsigned char) text[len - 1])) {
        text[--len] = '\0';
    }
}

static void
parse_app_line(struct list_result *result, char *line) {
    trim_ascii(line);
    if (line[0] != '*' && line[0] != '-') {
        return;
    }
    ++line;
    trim_ascii(line);

    char *package = strrchr(line, ' ');
    if (!package) {
        package = strrchr(line, '\t');
    }
    if (!package) {
        return;
    }
    *package++ = '\0';
    trim_ascii(line);
    trim_ascii(package);
    if (!line[0] || !valid_package_name(package)
            || result->count >= MAX_REMOTE_APPS) {
        return;
    }

    struct remote_app *app = &result->apps[result->count++];
    MultiByteToWideChar(CP_UTF8, 0, line, -1, app->name,
                        sizeof(app->name) / sizeof(app->name[0]));
    copy_ascii(app->package_name, sizeof(app->package_name), package);
    app->user_id = 0;
}

static DWORD WINAPI
list_thread(LPVOID userdata) {
    struct list_runner *runner = userdata;
    struct list_result *result = HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*result));
    if (!result) {
        HeapFree(GetProcessHeap(), 0, runner);
        return 1;
    }
    copy_ascii(result->serial, sizeof(result->serial), runner->serial);

    WCHAR command_line[32768];
    swprintf(command_line,
             sizeof(command_line) / sizeof(command_line[0]),
             L"\"%ls\" --serial=\"%hs\" --list-apps",
             runner->scrcpy_path, runner->serial);
    char *output = capture_process(runner->scrcpy_path, command_line,
                                   &result->exit_code);
    if (output) {
        char *context = NULL;
        for (char *line = strtok_s(output, "\r\n", &context); line;
             line = strtok_s(NULL, "\r\n", &context)) {
            parse_app_line(result, line);
        }
        HeapFree(GetProcessHeap(), 0, output);
    }

    bool has_whatsapp = false;
    for (size_t i = 0; i < result->count; ++i) {
        if (!strcmp(result->apps[i].package_name, "com.whatsapp")) {
            has_whatsapp = true;
            break;
        }
    }
    if (has_whatsapp && result->count < MAX_REMOTE_APPS) {
        struct remote_app *clone = &result->apps[result->count++];
        copy_wide(clone->name,
                  sizeof(clone->name) / sizeof(clone->name[0]),
                  L"WhatsApp 2  \x2022  Clone");
        copy_ascii(clone->package_name, sizeof(clone->package_name),
                   "com.whatsapp");
        clone->user_id = 999;
    }
    qsort(result->apps, result->count, sizeof(result->apps[0]), compare_apps);

    PostMessageW(runner->hwnd, WM_VR_APP_LIST_READY, 0, (LPARAM) result);
    HeapFree(GetProcessHeap(), 0, runner);
    return result->exit_code;
}

static bool
save_icon_line(const char *serial, char *line) {
    char *marker = strstr(line, "@ICON\t");
    if (!marker) {
        return false;
    }
    char *package_name = marker + strlen("@ICON\t");
    char *separator = strchr(package_name, '\t');
    if (!separator) {
        return false;
    }
    *separator++ = '\0';
    if (!valid_package_name(package_name) || !separator[0]) {
        return false;
    }

    DWORD decoded_size = 0;
    if (!CryptStringToBinaryA(separator, 0, CRYPT_STRING_BASE64,
                              NULL, &decoded_size, NULL, NULL)
            || !decoded_size || decoded_size > 1024 * 1024) {
        return false;
    }
    BYTE *decoded = HeapAlloc(GetProcessHeap(), 0, decoded_size);
    if (!decoded) {
        return false;
    }
    bool decoded_ok = CryptStringToBinaryA(
        separator, 0, CRYPT_STRING_BASE64, decoded, &decoded_size,
        NULL, NULL);

    WCHAR path[32768];
    bool saved = false;
    if (decoded_ok
            && build_icon_path(serial, package_name, path,
                               sizeof(path) / sizeof(path[0]))) {
        WCHAR temp_path[32768];
        if (swprintf(temp_path,
                     sizeof(temp_path) / sizeof(temp_path[0]),
                     L"%ls.tmp", path) > 0) {
            HANDLE file = CreateFileW(temp_path, GENERIC_WRITE, 0, NULL,
                                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                      NULL);
            if (file != INVALID_HANDLE_VALUE) {
                saved = write_all(file, decoded, decoded_size);
                CloseHandle(file);
                if (saved) {
                    saved = MoveFileExW(
                        temp_path, path,
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
                }
                if (!saved) {
                    DeleteFileW(temp_path);
                }
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, decoded);
    return saved;
}

static DWORD WINAPI
icon_thread(LPVOID userdata) {
    struct icon_runner *runner = userdata;
    struct icon_result *result = HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*result));
    if (!result) {
        HeapFree(GetProcessHeap(), 0, runner);
        return 1;
    }
    copy_ascii(result->serial, sizeof(result->serial), runner->serial);

    WCHAR command_line[32768];
    swprintf(command_line,
             sizeof(command_line) / sizeof(command_line[0]),
             L"\"%ls\" --serial=\"%hs\" --list-app-icons",
             runner->scrcpy_path, runner->serial);
    char *output = capture_process(runner->scrcpy_path, command_line,
                                   &result->exit_code);
    if (output) {
        char *context = NULL;
        for (char *line = strtok_s(output, "\r\n", &context); line;
             line = strtok_s(NULL, "\r\n", &context)) {
            if (save_icon_line(runner->serial, line)) {
                ++result->saved_count;
            }
        }
        HeapFree(GetProcessHeap(), 0, output);
    }

    PostMessageW(runner->hwnd, WM_VR_APP_ICONS_READY, 0, (LPARAM) result);
    HeapFree(GetProcessHeap(), 0, runner);
    return result->exit_code;
}

static void
start_icon_refresh(void) {
    if (icons_loading || !search_window) {
        return;
    }
    struct icon_runner *runner = HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*runner));
    if (!runner) {
        return;
    }
    runner->hwnd = search_window;
    copy_wide(runner->scrcpy_path,
              sizeof(runner->scrcpy_path) / sizeof(runner->scrcpy_path[0]),
              active_scrcpy_path);
    copy_ascii(runner->serial, sizeof(runner->serial), active_serial);
    HANDLE thread = CreateThread(NULL, 0, icon_thread, runner, 0, NULL);
    if (!thread) {
        HeapFree(GetProcessHeap(), 0, runner);
        return;
    }
    icons_loading = true;
    CloseHandle(thread);
}

static DWORD WINAPI
launch_thread(LPVOID userdata) {
    struct launch_runner *runner = userdata;
    struct launch_result *result = HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*result));
    if (!result) {
        HeapFree(GetProcessHeap(), 0, runner);
        return 1;
    }
    copy_wide(result->app_name,
              sizeof(result->app_name) / sizeof(result->app_name[0]),
              runner->app_name);

    WCHAR command_line[32768];
    swprintf(command_line,
             sizeof(command_line) / sizeof(command_line[0]),
             L"\"%ls\" -s \"%hs\" shell cmd package resolve-activity "
             L"--brief --user %d -a android.intent.action.MAIN "
             L"-c android.intent.category.LAUNCHER \"%hs\"",
             runner->adb_path, runner->serial, runner->user_id,
             runner->package_name);

    DWORD resolve_exit = 1;
    char component[512] = "";
    for (unsigned attempt = 0; attempt < 6 && !component[0]; ++attempt) {
        char *resolve_output = capture_process(runner->adb_path, command_line,
                                               &resolve_exit);
        if (!resolve_exit && resolve_output) {
            char *context = NULL;
            for (char *line = strtok_s(resolve_output, "\r\n", &context);
                 line; line = strtok_s(NULL, "\r\n", &context)) {
                trim_ascii(line);
                if (valid_component_name(line)) {
                    copy_ascii(component, sizeof(component), line);
                }
            }
        }
        HeapFree(GetProcessHeap(), 0, resolve_output);
        if (!component[0] && attempt < 5) {
            Sleep(120 + attempt * 80);
        }
    }

    if (!component[0]) {
        result->exit_code = resolve_exit ? resolve_exit : 1;
    } else {
        swprintf(command_line,
                 sizeof(command_line) / sizeof(command_line[0]),
                 L"\"%ls\" -s \"%hs\" shell am start --user %d "
                 L"-n \"%hs\"",
                 runner->adb_path, runner->serial, runner->user_id,
                 component);
        result->exit_code = 1;
        for (unsigned attempt = 0;
             attempt < 4 && result->exit_code; ++attempt) {
            char *start_output = capture_process(
                runner->adb_path, command_line, &result->exit_code);
            HeapFree(GetProcessHeap(), 0, start_output);
            if (result->exit_code && attempt < 3) {
                Sleep(120 + attempt * 80);
            }
        }
    }

    PostMessageW(runner->hwnd, WM_VR_APP_LAUNCH_DONE, 0, (LPARAM) result);
    HeapFree(GetProcessHeap(), 0, runner);
    return result->exit_code;
}

static void
draw_list_item(const DRAWITEMSTRUCT *item) {
    if (item->itemID == (UINT) -1 || item->itemID >= filtered_count) {
        return;
    }
    size_t app_index = file_search_mode ? 0 : filtered_indices[item->itemID];
    if (!file_search_mode) {
        load_cached_icon(app_index);
    }
    const struct remote_app *app = file_search_mode ? NULL
        : &remote_apps[app_index];
    const struct vr_file_cache_entry *file = file_search_mode
        ? &file_cache.entries[filtered_file_indices[item->itemID]] : NULL;
    bool selected = (item->itemState & ODS_SELECTED) != 0;
    COLORREF background = selected ? RGB(54, 54, 54) : RGB(28, 28, 28);
    COLORREF primary = selected ? RGB(255, 255, 255) : RGB(236, 236, 236);
    COLORREF secondary = selected ? RGB(210, 210, 210) : RGB(163, 163, 163);

    HBRUSH background_brush = CreateSolidBrush(background);
    FillRect(item->hDC, &item->rcItem, background_brush);
    DeleteObject(background_brush);
    RECT icon_rect = item->rcItem;
    icon_rect.left += 14;
    icon_rect.right = icon_rect.left + 34;
    icon_rect.top += 9;
    icon_rect.bottom = icon_rect.top + 34;
    HBRUSH icon_brush = CreateSolidBrush(selected ? RGB(72, 72, 72)
                                                   : RGB(38, 38, 38));
    HPEN icon_pen = CreatePen(PS_SOLID, 1, RGB(72, 72, 72));
    HGDIOBJ old_brush = SelectObject(item->hDC, icon_brush);
    HGDIOBJ old_pen = SelectObject(item->hDC, icon_pen);
    RoundRect(item->hDC, icon_rect.left, icon_rect.top, icon_rect.right,
              icon_rect.bottom, 10, 10);
    SelectObject(item->hDC, old_pen);
    SelectObject(item->hDC, old_brush);
    DeleteObject(icon_pen);
    DeleteObject(icon_brush);

    HGDIOBJ old_font = SelectObject(item->hDC, search_font);
    SetBkMode(item->hDC, TRANSPARENT);
    if (app && app->icon) {
        GpGraphics *graphics = NULL;
        if (GdipCreateFromHDC(item->hDC, &graphics) == Ok) {
            GdipSetInterpolationMode(graphics,
                                     InterpolationModeHighQualityBicubic);
            GdipDrawImageRectI(graphics, app->icon,
                               icon_rect.left + 2, icon_rect.top + 2,
                               icon_rect.right - icon_rect.left - 4,
                               icon_rect.bottom - icon_rect.top - 4);
            GdipDeleteGraphics(graphics);
        }
    } else {
        WCHAR initial[2] = {
            file ? L'F' : (app && app->name[0] ? app->name[0] : L'?'), L'\0'
        };
        SetTextColor(item->hDC, RGB(255, 255, 255));
        DrawTextW(item->hDC, initial, -1, &icon_rect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    RECT name_rect = item->rcItem;
    name_rect.left += 62;
    name_rect.right -= 12;
    name_rect.top += 7;
    name_rect.bottom = name_rect.top + 22;
    SetTextColor(item->hDC, primary);
    WCHAR file_name[VR_FILE_MANAGER_MAX_NAME] = L"";
    if (file) {
        const char *base = strrchr(file->path, '/');
        MultiByteToWideChar(CP_UTF8, 0, base ? base + 1 : file->path, -1,
                            file_name,
                            sizeof(file_name) / sizeof(file_name[0]));
    }
    DrawTextW(item->hDC, file ? file_name : app->name, -1, &name_rect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    WCHAR package[320] = L"";
    if (file) {
        MultiByteToWideChar(CP_UTF8, 0, file->path,
                            (int) (strlen(file->path) > 315 ? 315
                                                          : strlen(file->path)),
                            package,
                            sizeof(package) / sizeof(package[0]) - 1);
    } else {
        MultiByteToWideChar(CP_UTF8, 0, app->package_name, -1, package,
                            sizeof(package) / sizeof(package[0]));
    }
    if (app && app->user_id == 999) {
        wcscat_s(package, sizeof(package) / sizeof(package[0]),
                 L"  \x2022  Dual app");
    }
    RECT package_rect = item->rcItem;
    package_rect.left += 62;
    package_rect.right -= 12;
    package_rect.top += 28;
    package_rect.bottom -= 4;
    SelectObject(item->hDC, search_small_font);
    SetTextColor(item->hDC, secondary);
    DrawTextW(item->hDC, package, -1, &package_rect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(item->hDC, old_font);

    if (item->itemState & ODS_FOCUS) {
        DrawFocusRect(item->hDC, &item->rcItem);
    }
}

static void
resize_search_controls(HWND hwnd) {
    RECT client;
    GetClientRect(hwnd, &client);
    int width = client.right;
    int height = client.bottom;
    MoveWindow(search_edit, 22, 20, width - 86, 56, TRUE);
    MoveWindow(search_close, width - 54, 29, 34, 34, TRUE);
    if (search_expanded) {
        MoveWindow(search_list, 22, 92, width - 44, height - 142, TRUE);
        MoveWindow(search_hint, 26, height - 40, width - 52, 22, TRUE);
    }
    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, 34, 34);
    SetWindowRgn(hwnd, region, TRUE);
}

static void
set_search_expanded(bool expanded) {
    if (!search_window || search_expanded == expanded) {
        return;
    }
    search_expanded = expanded;
    ShowWindow(search_list, expanded ? SW_SHOW : SW_HIDE);
    ShowWindow(search_hint, expanded ? SW_SHOW : SW_HIDE);

    RECT rect;
    GetWindowRect(search_window, &rect);
    int height = expanded ? SEARCH_EXPANDED_HEIGHT : SEARCH_COLLAPSED_HEIGHT;
    SetWindowPos(search_window, HWND_TOPMOST, rect.left, rect.top,
                 SEARCH_WIDTH, height, SWP_NOACTIVATE);
    resize_search_controls(search_window);
}

static void
draw_close_button(const DRAWITEMSTRUCT *item) {
    HBRUSH background = CreateSolidBrush(RGB(21, 21, 21));
    FillRect(item->hDC, &item->rcItem, background);
    DeleteObject(background);

    RECT circle = item->rcItem;
    InflateRect(&circle, -3, -3);
    HBRUSH circle_brush = CreateSolidBrush(
        item->itemState & ODS_SELECTED ? RGB(76, 76, 76)
                                       : RGB(38, 38, 38));
    HPEN border_pen = CreatePen(PS_SOLID, 1, RGB(72, 72, 72));
    HGDIOBJ old_brush = SelectObject(item->hDC, circle_brush);
    HGDIOBJ old_pen = SelectObject(item->hDC, border_pen);
    Ellipse(item->hDC, circle.left, circle.top, circle.right, circle.bottom);
    SelectObject(item->hDC, old_pen);
    SelectObject(item->hDC, old_brush);
    DeleteObject(border_pen);
    DeleteObject(circle_brush);

    HPEN x_pen = CreatePen(PS_SOLID, 2, RGB(203, 213, 225));
    old_pen = SelectObject(item->hDC, x_pen);
    MoveToEx(item->hDC, circle.left + 8, circle.top + 8, NULL);
    LineTo(item->hDC, circle.right - 8, circle.bottom - 8);
    MoveToEx(item->hDC, circle.right - 8, circle.top + 8, NULL);
    LineTo(item->hDC, circle.left + 8, circle.bottom - 8);
    SelectObject(item->hDC, old_pen);
    DeleteObject(x_pen);
}

static LRESULT CALLBACK
search_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_CREATE:
            vr_theme_apply_window(hwnd);
            {
                GdiplusStartupInput input = {
                    .GdiplusVersion = 1,
                    .DebugEventCallback = NULL,
                    .SuppressBackgroundThread = FALSE,
                    .SuppressExternalCodecs = FALSE,
                };
                GdiplusStartup(&gdiplus_token, &input, NULL);
            }
            {
                int rounded = 2;
                DwmSetWindowAttribute(hwnd, 33, &rounded, sizeof(rounded));
            }
            search_edit = CreateWindowExW(
                0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                0, 0, 0, 0, hwnd, (HMENU) (uintptr_t) ID_SEARCH_EDIT,
                GetModuleHandleW(NULL), NULL);
            search_list = CreateWindowExW(
                0, L"LISTBOX", L"",
                WS_CHILD | WS_VSCROLL | LBS_NOTIFY
                    | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED
                    | LBS_HASSTRINGS,
                0, 0, 0, 0, hwnd, (HMENU) (uintptr_t) ID_SEARCH_LIST,
                GetModuleHandleW(NULL), NULL);
            search_hint = CreateWindowW(
                L"STATIC", L"Loading applications from phone...",
                WS_CHILD, 0, 0, 0, 0, hwnd, NULL,
                GetModuleHandleW(NULL), NULL);
            search_close = CreateWindowW(
                L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                0, 0, 0, 0, hwnd,
                (HMENU) (uintptr_t) ID_SEARCH_CLOSE,
                GetModuleHandleW(NULL), NULL);

            search_font = CreateFontW(
                -20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            search_small_font = CreateFontW(
                -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            SendMessageW(search_edit, WM_SETFONT, (WPARAM) search_font, TRUE);
            SendMessageW(search_list, WM_SETFONT, (WPARAM) search_font, TRUE);
            SendMessageW(search_list, LB_SETITEMHEIGHT, 0, 52);
            SendMessageW(search_hint, WM_SETFONT,
                         (WPARAM) search_small_font, TRUE);
            SendMessageW(search_edit, EM_SETCUEBANNER, TRUE,
                         (LPARAM) L"Search apps, or type /files filename...");
            SendMessageW(search_edit, EM_SETMARGINS,
                         EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(16, 16));
            vr_theme_apply_edit(search_edit);
            vr_theme_apply_listbox(search_list);
            vr_theme_apply_surface_label(search_hint);
            search_edit_original_proc = (WNDPROC) SetWindowLongPtrW(
                search_edit, GWLP_WNDPROC, (LONG_PTR) search_child_proc);
            search_list_original_proc = (WNDPROC) SetWindowLongPtrW(
                search_list, GWLP_WNDPROC, (LONG_PTR) search_child_proc);
            resize_search_controls(hwnd);
            return 0;

        case WM_SIZE:
            resize_search_controls(hwnd);
            return 0;

        case WM_COMMAND:
            if (LOWORD(wparam) == ID_SEARCH_EDIT
                    && HIWORD(wparam) == EN_CHANGE) {
                if (GetWindowTextLengthW(search_edit) > 0) {
                    set_search_expanded(true);
                }
                refresh_filter();
                return 0;
            }
            if (LOWORD(wparam) == ID_SEARCH_CLOSE
                    && HIWORD(wparam) == BN_CLICKED) {
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            if (LOWORD(wparam) == ID_SEARCH_LIST
                    && HIWORD(wparam) == LBN_DBLCLK) {
                launch_selected_app();
                return 0;
            }
            break;

        case WM_DRAWITEM:
            if (wparam == ID_SEARCH_LIST) {
                draw_list_item((const DRAWITEMSTRUCT *) lparam);
                return TRUE;
            }
            if (wparam == ID_SEARCH_CLOSE) {
                draw_close_button((const DRAWITEMSTRUCT *) lparam);
                return TRUE;
            }
            break;

        case WM_ERASEBKGND:
            return vr_theme_erase_background(hwnd, (HDC) wparam);

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            return vr_theme_control_color(msg, (HDC) wparam,
                                          (HWND) lparam);

        case WM_VR_APP_LIST_READY:
            {
                struct list_result *result = (struct list_result *) lparam;
                bool same_device = !strcmp(result->serial, active_serial);
                if (!result->exit_code && result->count) {
                    save_app_cache(result->serial, result->apps,
                                   result->count);
                    if (same_device
                            && (!IsWindowVisible(hwnd)
                                || !remote_app_count)) {
                        remote_app_count = result->count;
                        memcpy(remote_apps, result->apps,
                               result->count * sizeof(result->apps[0]));
                        if (IsWindowVisible(hwnd)) {
                            refresh_filter();
                        }
                    }
                } else if (same_device && !remote_app_count
                        && IsWindowVisible(hwnd)) {
                    refresh_filter();
                    set_hint(L"Phone is offline. No local app cache is available yet.");
                }
                list_loading = false;
                HeapFree(GetProcessHeap(), 0, result);
            }
            return 0;

        case WM_VR_APP_LAUNCH_DONE:
            {
                struct launch_result *result =
                    (struct launch_result *) lparam;
                WCHAR message[256];
                if (!result->exit_code) {
                    swprintf(message,
                             sizeof(message) / sizeof(message[0]),
                             L"%ls opened on the phone", result->app_name);
                    if (remote_focus_callback) {
                        remote_focus_callback(NULL, -1);
                    }
                } else {
                    swprintf(message,
                             sizeof(message) / sizeof(message[0]),
                             L"Could not open %ls (ADB exit %lu)",
                             result->app_name, result->exit_code);
                    set_hint(message);
                }
                if (status_callback) {
                    status_callback(message);
                }
                HeapFree(GetProcessHeap(), 0, result);
            }
            return 0;

        case WM_VR_APP_ICONS_READY:
            {
                struct icon_result *result = (struct icon_result *) lparam;
                icons_loading = false;
                if (!strcmp(result->serial, active_serial)
                        && result->saved_count) {
                    InvalidateRect(search_list, NULL, TRUE);
                }
                HeapFree(GetProcessHeap(), 0, result);
            }
            return 0;

        case WM_VR_FILE_OPEN_DONE:
            {
                struct file_open_result *result =
                    (struct file_open_result *) lparam;
                file_open_loading = false;
                EnableWindow(search_edit, TRUE);
                EnableWindow(search_list, TRUE);
                WCHAR message[1024];
                if (!result->exit_code) {
                    struct vr_file_cache_entry entry = {
                        .type = VR_FILE_MANAGER_ENTRY_FILE,
                        .size = result->size,
                        .modified = result->modified,
                        .path = result->remote_path,
                    };
                    vr_file_cache_mark_downloaded(result->serial, &entry);
                    const char *base = strrchr(result->remote_path, '/');
                    WCHAR name[VR_FILE_MANAGER_MAX_NAME];
                    MultiByteToWideChar(CP_UTF8, 0, base ? base + 1
                                                        : result->remote_path,
                                        -1, name,
                                        sizeof(name) / sizeof(name[0]));
                    if ((INT_PTR) ShellExecuteW(NULL, L"open",
                                                result->local_path, NULL, NULL,
                                                SW_SHOWNORMAL) > 32) {
                        swprintf(message,
                                 sizeof(message) / sizeof(message[0]),
                                 L"%ls downloaded and opened", name);
                        ShowWindow(search_window, SW_HIDE);
                    } else {
                        swprintf(message,
                                 sizeof(message) / sizeof(message[0]),
                                 L"%ls downloaded, but Windows has no app to open it",
                                 name);
                        ShowWindow(search_window, SW_SHOW);
                        activate_search_window();
                    }
                } else {
                    WCHAR detail[640] = L"";
                    if (result->detail[0]) {
                        MultiByteToWideChar(CP_UTF8, 0, result->detail,
                                            (int) strnlen(result->detail, 600),
                                            detail,
                                            sizeof(detail) / sizeof(detail[0]) - 1);
                    }
                    swprintf(message, sizeof(message) / sizeof(message[0]),
                             L"Could not download the file (ADB exit %lu).\n%ls",
                             result->exit_code, detail);
                    ShowWindow(search_window, SW_SHOW);
                    activate_search_window();
                    MessageBoxW(search_window, message,
                                L"VR Finder — File download failed",
                                MB_OK | MB_ICONERROR);
                }
                set_hint(message);
                if (status_callback) status_callback(message);
                HeapFree(GetProcessHeap(), 0, result);
            }
            return 0;

        case WM_VR_FILE_CACHE_READY:
            {
                struct file_cache_result *result =
                    (struct file_cache_result *) lparam;
                file_cache_loading = false;
                if (result->cache && !strcmp(result->serial, active_serial)) {
                    if (file_cache_initialized) {
                        vr_file_cache_destroy(&file_cache);
                    }
                    file_cache = *result->cache;
                    file_cache_initialized = true;
                    HeapFree(GetProcessHeap(), 0, result->cache);
                    result->cache = NULL;
                    if (file_search_mode && IsWindowVisible(search_window)) {
                        refresh_filter();
                    }
                }
                if (result->cache) {
                    vr_file_cache_destroy(result->cache);
                    HeapFree(GetProcessHeap(), 0, result->cache);
                }
                HeapFree(GetProcessHeap(), 0, result);
                if (!file_cache_initialized
                        || strcmp(file_cache.serial, active_serial)) {
                    start_file_cache_refresh();
                }
            }
            return 0;

        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;

        case WM_DESTROY:
            free_app_icons();
            if (file_cache_initialized) {
                vr_file_cache_destroy(&file_cache);
                file_cache_initialized = false;
            }
            file_cache_loading = false;
            if (gdiplus_token) {
                GdiplusShutdown(gdiplus_token);
                gdiplus_token = 0;
            }
            DeleteObject(search_font);
            DeleteObject(search_small_font);
            search_window = NULL;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static bool
create_search_window(HINSTANCE instance, HWND owner) {
    (void) owner;
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = search_window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = vr_theme_app_icon(false);
    wc.hbrBackground = vr_theme_background_brush();
    wc.lpszClassName = L"VRMobileAppSearchWindow";
    RegisterClassW(&wc);

    search_window = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
        wc.lpszClassName, L"Find on VR Mobile", WS_POPUP,
        0, 0, SEARCH_WIDTH, SEARCH_COLLAPSED_HEIGHT,
        0, NULL, instance, NULL);
    if (search_window) {
        SetLayeredWindowAttributes(search_window, 0, 242, LWA_ALPHA);
    }
    return search_window != NULL;
}

static LRESULT CALLBACK
low_level_keyboard_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code != HC_ACTION) {
        return CallNextHookEx(shortcut_hook, code, wparam, lparam);
    }

    const KBDLLHOOKSTRUCT *event = (const KBDLLHOOKSTRUCT *) lparam;
    if (event->dwExtraInfo == VR_HOTKEY_REPLAY_MARKER) {
        return CallNextHookEx(shortcut_hook, code, wparam, lparam);
    }

    bool down = wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN;
    bool up = wparam == WM_KEYUP || wparam == WM_SYSKEYUP;
    bool win = event->vkCode == VK_LWIN || event->vkCode == VK_RWIN;

    if (pending_win && !win && event->vkCode != 'F'
            && !(GetAsyncKeyState((int) pending_win_vk) & 0x8000)) {
        // Recover if Windows dropped a key-up event or removed a slow hook.
        pending_win = false;
        consumed_win_f = false;
        pending_win_vk = 0;
    }

    if (event->vkCode == VK_LCONTROL || event->vkCode == VK_RCONTROL
            || event->vkCode == VK_CONTROL) {
        ctrl_down = down ? true : up ? false : ctrl_down;
    } else if (event->vkCode == VK_LMENU || event->vkCode == VK_RMENU
            || event->vkCode == VK_MENU) {
        alt_down = down ? true : up ? false : alt_down;
    } else if (event->vkCode == 0xFF) {
        // Some OEM keyboard drivers expose the physical Fn key as 0xFF.
        fn_down = down ? true : up ? false : fn_down;
        return 1;
    }

    if (event->vkCode == 'M' && (fn_down || (ctrl_down && alt_down))) {
        if (down && !consumed_tailscale_m) {
            consumed_tailscale_m = true;
            PostMessageW(shortcut_owner, WM_VR_TAILSCALE_HOTKEY, 0, 0);
        } else if (up) {
            consumed_tailscale_m = false;
        }
        return 1;
    }

    if (win && down) {
        if (!pending_win) {
            pending_win = true;
            consumed_win_f = false;
            pending_win_vk = event->vkCode;
        }
        return 1;
    }

    if (pending_win && event->vkCode == 'F') {
        if (down && !consumed_win_f) {
            consumed_win_f = true;
        }
        return 1;
    }

    if (win && up && pending_win) {
        bool open_finder = consumed_win_f;
        if (!open_finder) {
            INPUT inputs[2];
            ZeroMemory(inputs, sizeof(inputs));
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki.wVk = (WORD) pending_win_vk;
            inputs[0].ki.dwExtraInfo = VR_HOTKEY_REPLAY_MARKER;
            inputs[1] = inputs[0];
            inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(2, inputs, sizeof(inputs[0]));
        }
        pending_win = false;
        consumed_win_f = false;
        pending_win_vk = 0;
        if (open_finder) {
            // Open only after the physical Windows key has been released, so
            // the first search character cannot inherit the Win modifier.
            PostMessageW(shortcut_owner, WM_VR_APP_SEARCH_HOTKEY, 0, 0);
        }
        return 1;
    }

    if (pending_win && down) {
        if (consumed_win_f) {
            pending_win = false;
            consumed_win_f = false;
            pending_win_vk = 0;
            PostMessageW(shortcut_owner, WM_VR_APP_SEARCH_HOTKEY, 0, 0);
            return CallNextHookEx(shortcut_hook, code, wparam, lparam);
        }
        INPUT input;
        ZeroMemory(&input, sizeof(input));
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = (WORD) pending_win_vk;
        input.ki.dwExtraInfo = VR_HOTKEY_REPLAY_MARKER;
        SendInput(1, &input, sizeof(input));
        pending_win = false;
        consumed_win_f = false;
        pending_win_vk = 0;
    }
    return CallNextHookEx(shortcut_hook, code, wparam, lparam);
}

bool
vr_app_search_enable_shortcut(HWND owner) {
    shortcut_owner = owner;
    shortcut_hook = SetWindowsHookExW(WH_KEYBOARD_LL,
                                      low_level_keyboard_proc,
                                      GetModuleHandleW(NULL), 0);
    return shortcut_hook != NULL;
}

void
vr_app_search_disable_shortcut(HWND owner) {
    if (hotkey_registered) {
        UnregisterHotKey(owner, VR_APP_SEARCH_HOTKEY_ID);
        hotkey_registered = false;
    }
    if (shortcut_hook) {
        UnhookWindowsHookEx(shortcut_hook);
        shortcut_hook = NULL;
    }
    pending_win = false;
    consumed_win_f = false;
    pending_win_vk = 0;
    ctrl_down = false;
    alt_down = false;
    fn_down = false;
    consumed_tailscale_m = false;
    shortcut_owner = NULL;
}

bool
vr_app_search_is_hotkey(WPARAM hotkey_id) {
    return hotkey_id == VR_APP_SEARCH_HOTKEY_ID;
}

void
vr_app_search_show(HINSTANCE instance, HWND owner, const char *serial,
                   const WCHAR *adb_path, const WCHAR *scrcpy_path,
                   vr_app_search_status_fn callback,
                   vr_app_search_focus_fn focus_callback) {
    if (!search_window && !create_search_window(instance, owner)) {
        return;
    }

    if (IsWindowVisible(search_window)) {
        ShowWindow(search_window, SW_HIDE);
        return;
    }

    copy_ascii(active_serial, sizeof(active_serial), serial);
    copy_wide(active_adb_path,
              sizeof(active_adb_path) / sizeof(active_adb_path[0]), adb_path);
    copy_wide(active_scrcpy_path,
              sizeof(active_scrcpy_path) / sizeof(active_scrcpy_path[0]),
              scrcpy_path);
    status_callback = callback;
    remote_focus_callback = focus_callback;

    free_app_icons();
    remote_app_count = load_app_cache(active_serial, remote_apps,
                                      MAX_REMOTE_APPS);
    // Cached icon decoding can be expensive on large app lists. Keep the
    // hotkey path fast; icons are loaded when the background refresh returns.
    if (!file_cache_initialized
            || strcmp(file_cache.serial, active_serial)) {
        if (file_cache_initialized) {
            vr_file_cache_destroy(&file_cache);
        }
        vr_file_cache_init(&file_cache, active_serial);
        file_cache_initialized = true;
    }
    SetWindowTextW(search_edit, L"");
    refresh_filter();
    if (remote_app_count) {
        set_hint(L"Local app list ready  \x2022  Refreshing quietly in background");
    } else {
        set_hint(L"Building the local app list for the first time...");
    }
    set_search_expanded(false);

    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int width = SEARCH_WIDTH;
    int height = SEARCH_COLLAPSED_HEIGHT;
    int x = work.left + (work.right - work.left - width) / 2;
    int y = work.top + (work.bottom - work.top - height) / 3;
    SetWindowPos(search_window, HWND_TOPMOST, x, y, width, height,
                 SWP_SHOWWINDOW);
    activate_search_window();
    start_file_cache_refresh();

    if (list_loading) {
        start_icon_refresh();
        return;
    }

    list_loading = true;

    struct list_runner *runner = HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*runner));
    if (!runner) {
        list_loading = false;
        return;
    }
    runner->hwnd = search_window;
    copy_wide(runner->scrcpy_path,
              sizeof(runner->scrcpy_path) / sizeof(runner->scrcpy_path[0]),
              active_scrcpy_path);
    copy_ascii(runner->serial, sizeof(runner->serial), active_serial);
    HANDLE thread = CreateThread(NULL, 0, list_thread, runner, 0, NULL);
    if (!thread) {
        HeapFree(GetProcessHeap(), 0, runner);
        list_loading = false;
        set_hint(L"Could not start application discovery");
        return;
    }
    CloseHandle(thread);
    start_icon_refresh();
}

void
vr_app_search_shutdown(void) {
    if (search_window) {
        DestroyWindow(search_window);
    }
}
