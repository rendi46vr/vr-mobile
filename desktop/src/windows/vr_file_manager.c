#define UNICODE
#define _UNICODE

#include "vr_file_manager.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

#include "file_manager_protocol.h"
#include "launcher_json.h"
#include "vr_file_cache.h"
#include "vr_theme.h"

#define ID_FM_PATH 5101
#define ID_FM_GO 5102
#define ID_FM_UP 5103
#define ID_FM_REFRESH 5104
#define ID_FM_LIST 5105
#define ID_FM_NAME 5106
#define ID_FM_DOWNLOAD 5107
#define ID_FM_UPLOAD 5108
#define ID_FM_NEW_FOLDER 5109
#define ID_FM_RENAME 5110
#define ID_FM_DELETE 5111
#define ID_FM_STATUS 5112
#define ID_FM_HEADING 5113
#define ID_FM_SUBHEADING 5114
#define ID_FM_LOAD_ALL 5115

#define WM_FM_DONE (WM_APP + 31)
#define WM_FM_INDEX_PROCESSING (WM_APP + 32)

#define FM_COMMAND_LINE_CHARS 32768
#define FM_LOCAL_PATH_CHARS 32768
#define FM_SCRIPT_CHARS 24576

enum fm_operation {
    FM_OPERATION_LIST,
    FM_OPERATION_INDEX_ALL,
    FM_OPERATION_PULL,
    FM_OPERATION_PUSH,
    FM_OPERATION_MKDIR,
    FM_OPERATION_RENAME,
    FM_OPERATION_DELETE,
};

struct fm_output_buffer {
    char *data;
    size_t len;
    size_t cap;
};

struct fm_runner {
    HWND hwnd;
    enum fm_operation operation;
    WCHAR adb_path[FM_LOCAL_PATH_CHARS];
    char serial[VR_LAUNCHER_MAX_SERIAL_LEN];
    char remote_path[VR_FILE_MANAGER_MAX_PATH];
    char second_remote_path[VR_FILE_MANAGER_MAX_PATH];
    WCHAR local_path[FM_LOCAL_PATH_CHARS];
};

struct fm_done {
    enum fm_operation operation;
    DWORD exit_code;
    char *output;
    char remote_path[VR_FILE_MANAGER_MAX_PATH];
    struct vr_file_cache *index_cache;
};

struct fm_context {
    HINSTANCE instance;
    HWND owner;
    HWND window;
    HWND heading;
    HWND subheading;
    HWND path_edit;
    HWND list;
    HWND name_edit;
    HWND status;
    WCHAR adb_path[FM_LOCAL_PATH_CHARS];
    char serial[VR_LAUNCHER_MAX_SERIAL_LEN];
    char current_path[VR_FILE_MANAGER_MAX_PATH];
    struct vr_file_manager_entry entries[VR_FILE_MANAGER_MAX_ENTRIES];
    size_t entry_count;
    struct vr_file_cache cache;
    bool cache_initialized;
    bool busy;
    HFONT title_font;
    HFONT ui_font;
};

static struct fm_context fm;
static bool fm_com_initialized;

static void
fm_set_status(const WCHAR *text) {
    if (fm.status) {
        SetWindowTextW(fm.status, text);
    }
}

static bool
fm_output_append(struct fm_output_buffer *buf, const char *data, size_t len) {
    if (!len) {
        return true;
    }
    if (buf->len + len + 1 > buf->cap) {
        size_t cap = buf->cap ? buf->cap : 4096;
        while (cap < buf->len + len + 1) {
            cap *= 2;
        }
        char *new_data = buf->data
            ? HeapReAlloc(GetProcessHeap(), 0, buf->data, cap)
            : HeapAlloc(GetProcessHeap(), 0, cap);
        if (!new_data) {
            return false;
        }
        buf->data = new_data;
        buf->cap = cap;
    }
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
    return true;
}

static bool
fm_append_quoted_arg(WCHAR *cmdline, size_t len, const WCHAR *arg) {
    size_t used = wcslen(cmdline);
    if (used + 2 >= len) {
        return false;
    }
    if (used) {
        cmdline[used++] = L' ';
    }
    cmdline[used++] = L'"';

    unsigned backslashes = 0;
    for (const WCHAR *p = arg; *p; ++p) {
        if (*p == L'\\') {
            ++backslashes;
            continue;
        }
        if (*p == L'"') {
            while (backslashes) {
                --backslashes;
                if (used + 2 >= len) {
                    return false;
                }
                cmdline[used++] = L'\\';
                cmdline[used++] = L'\\';
            }
            if (used + 2 >= len) {
                return false;
            }
            cmdline[used++] = L'\\';
            cmdline[used++] = L'"';
            backslashes = 0;
            continue;
        }
        while (backslashes) {
            --backslashes;
            if (used + 1 >= len) {
                return false;
            }
            cmdline[used++] = L'\\';
        }
        backslashes = 0;
        if (used + 1 >= len) {
            return false;
        }
        cmdline[used++] = *p;
    }

    while (backslashes) {
        --backslashes;
        if (used + 2 >= len) {
            return false;
        }
        cmdline[used++] = L'\\';
        cmdline[used++] = L'\\';
    }
    if (used + 2 >= len) {
        return false;
    }
    cmdline[used++] = L'"';
    cmdline[used] = L'\0';
    return true;
}

static bool
fm_append_utf8_arg(WCHAR *cmdline, size_t len, const char *arg) {
    int required = MultiByteToWideChar(CP_UTF8, 0, arg, -1, NULL, 0);
    if (required <= 0) {
        return false;
    }
    WCHAR *wide = HeapAlloc(GetProcessHeap(), 0,
                            (size_t) required * sizeof(*wide));
    if (!wide) {
        return false;
    }
    bool ok = MultiByteToWideChar(CP_UTF8, 0, arg, -1, wide, required) > 0
           && fm_append_quoted_arg(cmdline, len, wide);
    HeapFree(GetProcessHeap(), 0, wide);
    return ok;
}

static bool
fm_wide_to_utf8(const WCHAR *wide, char *out, size_t out_len) {
    return WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, (int) out_len,
                               NULL, NULL) > 0;
}

static bool
fm_utf8_to_wide(const char *utf8, WCHAR *out, size_t out_len) {
    return MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out,
                               (int) out_len) > 0;
}

static bool
fm_shell_quote(const char *input, char *out, size_t out_len) {
    if (out_len < 3) {
        return false;
    }
    size_t pos = 0;
    out[pos++] = '\'';
    for (const char *p = input; *p; ++p) {
        if (*p == '\'') {
            static const char escaped[] = "'\\''";
            if (pos + sizeof(escaped) - 1 >= out_len) {
                return false;
            }
            memcpy(out + pos, escaped, sizeof(escaped) - 1);
            pos += sizeof(escaped) - 1;
        } else {
            if (pos + 1 >= out_len) {
                return false;
            }
            out[pos++] = *p;
        }
    }
    if (pos + 2 > out_len) {
        return false;
    }
    out[pos++] = '\'';
    out[pos] = '\0';
    return true;
}

static bool
fm_base64_encode(const char *input, char *out, size_t out_len) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t len = strlen(input);
    size_t required = ((len + 2) / 3) * 4;
    if (required + 1 > out_len) {
        return false;
    }

    size_t i = 0;
    size_t j = 0;
    while (i < len) {
        unsigned a = (unsigned char) input[i++];
        unsigned b = i < len ? (unsigned char) input[i++] : 0;
        unsigned c = i < len ? (unsigned char) input[i++] : 0;
        unsigned value = (a << 16) | (b << 8) | c;
        out[j++] = alphabet[(value >> 18) & 63];
        out[j++] = alphabet[(value >> 12) & 63];
        out[j++] = alphabet[(value >> 6) & 63];
        out[j++] = alphabet[value & 63];
    }

    size_t remainder = len % 3;
    if (remainder) {
        out[j - 1] = '=';
        if (remainder == 1) {
            out[j - 2] = '=';
        }
    }
    out[j] = '\0';
    return true;
}

static bool
fm_build_shell_script(const struct fm_runner *runner, char *script,
                      size_t script_len) {
    char quoted[VR_FILE_MANAGER_MAX_PATH * 4 + 8];
    char quoted_second[VR_FILE_MANAGER_MAX_PATH * 4 + 8];
    if (!fm_shell_quote(runner->remote_path, quoted, sizeof(quoted))) {
        return false;
    }

    int written;
    switch (runner->operation) {
        case FM_OPERATION_LIST:
            written = snprintf(
                script, script_len,
                "dir=%s\n"
                "[ -d \"$dir\" ] || { echo 'VRFM_ERROR|Directory not found'; "
                "exit 2; }\n"
                "count=0\n"
                "for p in \"$dir\"/* \"$dir\"/.[!.]* \"$dir\"/..?*; do\n"
                "  [ -e \"$p\" ] || continue\n"
                "  kind=$(toybox stat -c '%%F' \"$p\" 2>/dev/null) || continue\n"
                "  case \"$kind\" in\n"
                "    directory) type=d ;;\n"
                "    *symbolic*) type=l ;;\n"
                "    *) type=f ;;\n"
                "  esac\n"
                "  size=$(toybox stat -c '%%s' \"$p\" 2>/dev/null) || size=0\n"
                "  modified=$(toybox stat -c '%%Y' \"$p\" 2>/dev/null) || "
                "modified=0\n"
                "  name=${p##*/}\n"
                "  encoded=$(printf '%%s' \"$name\" | base64 | "
                "tr -d '\\r\\n')\n"
                "  printf '%%s|%%s|%%s|%%s\\n' \"$type\" \"$size\" "
                "\"$modified\" \"$encoded\"\n"
                "  count=$((count + 1))\n"
                "  [ \"$count\" -ge %d ] && break\n"
                "done\n"
                "exit 0\n",
                quoted, VR_FILE_MANAGER_MAX_ENTRIES);
            break;
        case FM_OPERATION_INDEX_ALL:
            written = snprintf(
                script, script_len,
                "root=%s\n"
                "[ -d \"$root\" ] || { echo 'VRFM_ERROR|Storage not found'; exit 2; }\n"
                "find -H \"$root\" -mindepth 1 "
                "-printf '%%M\\t%%s\\t%%T@\\t%%p\\n' 2>/dev/null || true\n"
                "exit 0\n",
                quoted);
            break;
        case FM_OPERATION_MKDIR:
            written = snprintf(
                script, script_len,
                "target=%s\n"
                "[ ! -e \"$target\" ] || { echo 'VRFM_ERROR|Already exists'; "
                "exit 3; }\n"
                "mkdir -p \"$target\"\n",
                quoted);
            break;
        case FM_OPERATION_RENAME:
            if (!fm_shell_quote(runner->second_remote_path, quoted_second,
                                sizeof(quoted_second))) {
                return false;
            }
            written = snprintf(
                script, script_len,
                "source=%s\n"
                "target=%s\n"
                "[ -e \"$source\" ] || { echo 'VRFM_ERROR|Source missing'; "
                "exit 3; }\n"
                "[ ! -e \"$target\" ] || { echo 'VRFM_ERROR|Target exists'; "
                "exit 4; }\n"
                "mv \"$source\" \"$target\"\n",
                quoted, quoted_second);
            break;
        case FM_OPERATION_DELETE:
            written = snprintf(
                script, script_len,
                "target=%s\n"
                "case \"$target\" in\n"
                "  /sdcard/*) rm -rf \"$target\" ;;\n"
                "  *) echo 'VRFM_ERROR|Unsafe delete refused'; exit 4 ;;\n"
                "esac\n",
                quoted);
            break;
        default:
            return false;
    }
    return written > 0 && (size_t) written < script_len;
}

static bool
fm_build_command_line(const struct fm_runner *runner, WCHAR *cmdline,
                      size_t cmdline_len) {
    cmdline[0] = L'\0';
    if (!fm_append_quoted_arg(cmdline, cmdline_len, runner->adb_path)
            || !fm_append_utf8_arg(cmdline, cmdline_len, "-s")
            || !fm_append_utf8_arg(cmdline, cmdline_len, runner->serial)) {
        return false;
    }

    if (runner->operation == FM_OPERATION_PULL
            || runner->operation == FM_OPERATION_PUSH) {
        if (!fm_append_utf8_arg(cmdline, cmdline_len,
                                runner->operation == FM_OPERATION_PULL
                                    ? "pull" : "push")) {
            return false;
        }
        if (runner->operation == FM_OPERATION_PULL) {
            return fm_append_utf8_arg(cmdline, cmdline_len,
                                      runner->remote_path)
                && fm_append_quoted_arg(cmdline, cmdline_len,
                                        runner->local_path);
        }
        return fm_append_quoted_arg(cmdline, cmdline_len, runner->local_path)
            && fm_append_utf8_arg(cmdline, cmdline_len, runner->remote_path);
    }

    char script[FM_SCRIPT_CHARS];
    if (!fm_build_shell_script(runner, script, sizeof(script))) {
        return false;
    }
    size_t encoded_len = ((strlen(script) + 2) / 3) * 4 + 1;
    char *encoded = HeapAlloc(GetProcessHeap(), 0, encoded_len);
    if (!encoded) {
        return false;
    }
    bool encoded_ok = fm_base64_encode(script, encoded, encoded_len);
    if (!encoded_ok) {
        HeapFree(GetProcessHeap(), 0, encoded);
        return false;
    }

    size_t remote_len = strlen(encoded) + 32;
    char *remote_command = HeapAlloc(GetProcessHeap(), 0, remote_len);
    if (!remote_command) {
        HeapFree(GetProcessHeap(), 0, encoded);
        return false;
    }
    snprintf(remote_command, remote_len, "echo %s | base64 -d | sh", encoded);

    bool ok = fm_append_utf8_arg(cmdline, cmdline_len, "shell")
           && fm_append_utf8_arg(cmdline, cmdline_len, remote_command);
    HeapFree(GetProcessHeap(), 0, remote_command);
    HeapFree(GetProcessHeap(), 0, encoded);
    return ok;
}

static DWORD WINAPI
fm_worker_thread(LPVOID userdata) {
    struct fm_runner *runner = userdata;
    WCHAR cmdline[FM_COMMAND_LINE_CHARS];
    DWORD exit_code = 1;
    struct fm_output_buffer output = {0};
    struct vr_file_cache *index_cache = NULL;

    if (!fm_build_command_line(runner, cmdline,
                               sizeof(cmdline) / sizeof(cmdline[0]))) {
        fm_output_append(&output, "Could not build ADB command.", 28);
        goto done;
    }

    SECURITY_ATTRIBUTES attrs = {
        .nLength = sizeof(attrs),
        .lpSecurityDescriptor = NULL,
        .bInheritHandle = TRUE,
    };
    HANDLE read_pipe;
    HANDLE write_pipe;
    if (!CreatePipe(&read_pipe, &write_pipe, &attrs, 0)) {
        goto done;
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
    BOOL started = CreateProcessW(
        runner->adb_path, cmdline, NULL, NULL, TRUE, CREATE_NO_WINDOW,
        NULL, NULL, &startup, &process);
    CloseHandle(write_pipe);
    if (!started) {
        CloseHandle(read_pipe);
        fm_output_append(&output, "Could not start adb.exe.", 24);
        goto done;
    }
    CloseHandle(process.hThread);

    char buffer[4096];
    DWORD read;
    while (ReadFile(read_pipe, buffer, sizeof(buffer), &read, NULL) && read) {
        if (!fm_output_append(&output, buffer, read)) {
            break;
        }
    }
    CloseHandle(read_pipe);
    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hProcess);
    if (!exit_code && runner->operation == FM_OPERATION_INDEX_ALL) {
        PostMessageW(runner->hwnd, WM_FM_INDEX_PROCESSING, 0, 0);
    }

done:
    {
        if (!exit_code && runner->operation == FM_OPERATION_INDEX_ALL) {
            index_cache = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                    sizeof(*index_cache));
            bool cache_ok = index_cache != NULL;
            if (cache_ok) {
                vr_file_cache_init(index_cache, runner->serial);
                cache_ok = vr_file_cache_replace_all_output(
                               index_cache, output.data ? output.data : "")
                        && vr_file_cache_save(index_cache);
            }
            if (cache_ok) {
                HeapFree(GetProcessHeap(), 0, output.data);
                output.data = NULL;
                output.len = 0;
                output.cap = 0;
            } else {
                if (index_cache) {
                    vr_file_cache_destroy(index_cache);
                    HeapFree(GetProcessHeap(), 0, index_cache);
                    index_cache = NULL;
                }
                HeapFree(GetProcessHeap(), 0, output.data);
                output = (struct fm_output_buffer) {0};
                fm_output_append(&output,
                    "Could not build or save the local file index.", 45);
                exit_code = ERROR_WRITE_FAULT;
            }
        }
        struct fm_done *result =
            HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*result));
        if (result) {
            result->operation = runner->operation;
            result->exit_code = exit_code;
            result->output = output.data;
            result->index_cache = index_cache;
            snprintf(result->remote_path, sizeof(result->remote_path), "%s",
                     runner->remote_path);
            if (!PostMessageW(runner->hwnd, WM_FM_DONE, 0,
                              (LPARAM) result)) {
                HeapFree(GetProcessHeap(), 0, result->output);
                if (result->index_cache) {
                    vr_file_cache_destroy(result->index_cache);
                    HeapFree(GetProcessHeap(), 0, result->index_cache);
                }
                HeapFree(GetProcessHeap(), 0, result);
            }
        } else {
            HeapFree(GetProcessHeap(), 0, output.data);
            if (index_cache) {
                vr_file_cache_destroy(index_cache);
                HeapFree(GetProcessHeap(), 0, index_cache);
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, runner);
    return exit_code;
}

static void
fm_set_busy(bool busy) {
    fm.busy = busy;
    EnableWindow(GetDlgItem(fm.window, ID_FM_GO), !busy);
    EnableWindow(GetDlgItem(fm.window, ID_FM_UP), !busy);
    EnableWindow(GetDlgItem(fm.window, ID_FM_REFRESH), !busy);
    EnableWindow(GetDlgItem(fm.window, ID_FM_LOAD_ALL), !busy);
    EnableWindow(GetDlgItem(fm.window, ID_FM_DOWNLOAD), !busy);
    EnableWindow(GetDlgItem(fm.window, ID_FM_UPLOAD), !busy);
    EnableWindow(GetDlgItem(fm.window, ID_FM_NEW_FOLDER), !busy);
    EnableWindow(GetDlgItem(fm.window, ID_FM_RENAME), !busy);
    EnableWindow(GetDlgItem(fm.window, ID_FM_DELETE), !busy);
}

static bool
fm_start_runner(struct fm_runner *runner, const WCHAR *status) {
    if (fm.busy) {
        HeapFree(GetProcessHeap(), 0, runner);
        return false;
    }
    fm_set_busy(true);
    fm_set_status(status);
    HANDLE thread = CreateThread(NULL, 0, fm_worker_thread, runner, 0, NULL);
    if (!thread) {
        fm_set_busy(false);
        fm_set_status(L"Could not start worker thread.");
        HeapFree(GetProcessHeap(), 0, runner);
        return false;
    }
    CloseHandle(thread);
    return true;
}

static struct fm_runner *
fm_create_runner(enum fm_operation operation) {
    struct fm_runner *runner =
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*runner));
    if (!runner) {
        return NULL;
    }
    runner->hwnd = fm.window;
    runner->operation = operation;
    wcscpy(runner->adb_path, fm.adb_path);
    snprintf(runner->serial, sizeof(runner->serial), "%s", fm.serial);
    return runner;
}

static int
fm_compare_entries(const void *a, const void *b) {
    const struct vr_file_manager_entry *left = a;
    const struct vr_file_manager_entry *right = b;
    if (left->type == VR_FILE_MANAGER_ENTRY_DIRECTORY
            && right->type != VR_FILE_MANAGER_ENTRY_DIRECTORY) {
        return -1;
    }
    if (right->type == VR_FILE_MANAGER_ENTRY_DIRECTORY
            && left->type != VR_FILE_MANAGER_ENTRY_DIRECTORY) {
        return 1;
    }
    return _stricmp(left->name, right->name);
}

static void
fm_format_size(const struct vr_file_manager_entry *entry, WCHAR *out,
               size_t out_len) {
    if (entry->type == VR_FILE_MANAGER_ENTRY_DIRECTORY) {
        wcscpy(out, L"");
        return;
    }
    static const WCHAR *units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    double value = (double) entry->size;
    size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < sizeof(units) / sizeof(units[0])) {
        value /= 1024.0;
        ++unit;
    }
    if (!unit) {
        swprintf(out, out_len, L"%llu B",
                 (unsigned long long) entry->size);
    } else {
        swprintf(out, out_len, L"%.1f %ls", value, units[unit]);
    }
}

static void
fm_format_modified(int64_t unix_time, WCHAR *out, size_t out_len) {
    if (unix_time <= 0) {
        wcscpy(out, L"-");
        return;
    }
    time_t value = (time_t) unix_time;
    struct tm local;
    if (localtime_s(&local, &value)) {
        wcscpy(out, L"-");
        return;
    }
    wcsftime(out, out_len, L"%Y-%m-%d %H:%M", &local);
}

static void
fm_populate_list(void) {
    ListView_DeleteAllItems(fm.list);
    qsort(fm.entries, fm.entry_count, sizeof(fm.entries[0]),
          fm_compare_entries);

    for (size_t i = 0; i < fm.entry_count; ++i) {
        const struct vr_file_manager_entry *entry = &fm.entries[i];
        WCHAR name[VR_FILE_MANAGER_MAX_NAME];
        if (!fm_utf8_to_wide(entry->name, name,
                             sizeof(name) / sizeof(name[0]))) {
            wcscpy(name, L"(invalid UTF-8 name)");
        }

        LVITEMW item;
        ZeroMemory(&item, sizeof(item));
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = (int) i;
        item.pszText = name;
        item.lParam = (LPARAM) i;
        int row = ListView_InsertItem(fm.list, &item);

        const WCHAR *type =
            entry->type == VR_FILE_MANAGER_ENTRY_DIRECTORY ? L"Folder"
          : entry->type == VR_FILE_MANAGER_ENTRY_SYMLINK ? L"Link"
                                                        : L"File";
        ListView_SetItemText(fm.list, row, 1, (WCHAR *) type);

        WCHAR size[64];
        fm_format_size(entry, size, sizeof(size) / sizeof(size[0]));
        ListView_SetItemText(fm.list, row, 2, size);

        WCHAR modified[64];
        fm_format_modified(entry->modified, modified,
                           sizeof(modified) / sizeof(modified[0]));
        ListView_SetItemText(fm.list, row, 3, modified);
    }
}

static const struct vr_file_manager_entry *
fm_get_selected_entry(void) {
    int row = ListView_GetNextItem(fm.list, -1, LVNI_SELECTED);
    if (row < 0) {
        return NULL;
    }
    LVITEMW item;
    ZeroMemory(&item, sizeof(item));
    item.mask = LVIF_PARAM;
    item.iItem = row;
    if (!ListView_GetItem(fm.list, &item)
            || item.lParam < 0 || (size_t) item.lParam >= fm.entry_count) {
        return NULL;
    }
    return &fm.entries[item.lParam];
}

static bool
fm_selected_remote_path(char *out, size_t out_len) {
    const struct vr_file_manager_entry *entry = fm_get_selected_entry();
    return entry
        && vr_file_manager_join_path(fm.current_path, entry->name, out,
                                     out_len);
}

static void
fm_show_cached_folder(void) {
    fm.entry_count = vr_file_cache_list_folder(
        &fm.cache, fm.current_path, fm.entries, VR_FILE_MANAGER_MAX_ENTRIES);
    fm_populate_list();
    WCHAR status[320];
    if (fm.cache.count) {
        swprintf(status, sizeof(status) / sizeof(status[0]),
                 L"%zu cached item(s) — %hs  •  Refresh updates this folder only",
                 fm.entry_count, fm.current_path);
    } else {
        wcscpy(status, L"No file index yet — choose Load all files");
    }
    fm_set_status(status);
}

static void
fm_refresh(void) {
    struct fm_runner *runner = fm_create_runner(FM_OPERATION_LIST);
    if (!runner) {
        return;
    }
    snprintf(runner->remote_path, sizeof(runner->remote_path), "%s",
             fm.current_path);
    fm_start_runner(runner, L"Reading phone files...");
}

static void
fm_load_all(void) {
    struct fm_runner *runner = fm_create_runner(FM_OPERATION_INDEX_ALL);
    if (!runner) {
        return;
    }
    snprintf(runner->remote_path, sizeof(runner->remote_path), "%s",
             "/sdcard");
    fm_start_runner(runner, L"Indexing all files and folders on the phone...");
}

static void
fm_navigate_to(const char *path) {
    if (!vr_file_manager_path_is_safe(path)) {
        MessageBoxW(fm.window,
                    L"VR Mobile File Manager is limited to /sdcard and its "
                    L"subfolders.", L"Unsafe Android path",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    snprintf(fm.current_path, sizeof(fm.current_path), "%s", path);
    WCHAR wide[VR_FILE_MANAGER_MAX_PATH];
    if (fm_utf8_to_wide(path, wide, sizeof(wide) / sizeof(wide[0]))) {
        SetWindowTextW(fm.path_edit, wide);
    }
    fm_show_cached_folder();
}

static void
fm_go_from_editor(void) {
    WCHAR wide[VR_FILE_MANAGER_MAX_PATH];
    char utf8[VR_FILE_MANAGER_MAX_PATH];
    GetWindowTextW(fm.path_edit, wide,
                   (int) (sizeof(wide) / sizeof(wide[0])));
    if (!fm_wide_to_utf8(wide, utf8, sizeof(utf8))) {
        return;
    }
    size_t len = strlen(utf8);
    while (len > strlen("/sdcard") && utf8[len - 1] == '/') {
        utf8[--len] = '\0';
    }
    fm_navigate_to(utf8);
}

static void
fm_go_up(void) {
    if (!strcmp(fm.current_path, "/sdcard")) {
        return;
    }
    char parent[VR_FILE_MANAGER_MAX_PATH];
    snprintf(parent, sizeof(parent), "%s", fm.current_path);
    char *slash = strrchr(parent, '/');
    if (slash && slash > parent + strlen("/sdcard") - 1) {
        *slash = '\0';
    } else {
        strcpy(parent, "/sdcard");
    }
    fm_navigate_to(parent);
}

static bool
fm_choose_save_file(const char *remote, WCHAR *out, size_t out_len) {
    const char *name_utf8 = strrchr(remote, '/');
    name_utf8 = name_utf8 ? name_utf8 + 1 : remote;
    if (!fm_utf8_to_wide(name_utf8, out, out_len)) {
        return false;
    }

    OPENFILENAMEW dialog;
    ZeroMemory(&dialog, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = fm.window;
    dialog.lpstrFile = out;
    dialog.nMaxFile = (DWORD) out_len;
    dialog.lpstrFilter = L"All files\0*.*\0\0";
    dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT
                 | OFN_PATHMUSTEXIST;
    dialog.lpstrTitle = L"Download from Android";
    return GetSaveFileNameW(&dialog);
}

static bool
fm_choose_folder(WCHAR *out, size_t out_len) {
    BROWSEINFOW browse;
    ZeroMemory(&browse, sizeof(browse));
    browse.hwndOwner = fm.window;
    browse.lpszTitle = L"Choose a folder for the Android download";
    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&browse);
    if (!item) {
        return false;
    }
    bool ok = SHGetPathFromIDListW(item, out)
           && wcslen(out) < out_len;
    CoTaskMemFree(item);
    return ok;
}

static void
fm_download_selected(void) {
    const struct vr_file_manager_entry *entry = fm_get_selected_entry();
    char remote[VR_FILE_MANAGER_MAX_PATH];
    if (!entry || !fm_selected_remote_path(remote, sizeof(remote))) {
        fm_set_status(L"Select a file or folder first.");
        return;
    }

    struct fm_runner *runner = fm_create_runner(FM_OPERATION_PULL);
    if (!runner) {
        return;
    }
    snprintf(runner->remote_path, sizeof(runner->remote_path), "%s", remote);

    bool chosen = entry->type == VR_FILE_MANAGER_ENTRY_DIRECTORY
        ? fm_choose_folder(runner->local_path,
                           sizeof(runner->local_path)
                               / sizeof(runner->local_path[0]))
        : fm_choose_save_file(
              remote, runner->local_path,
              sizeof(runner->local_path) / sizeof(runner->local_path[0]));
    if (!chosen) {
        HeapFree(GetProcessHeap(), 0, runner);
        return;
    }
    fm_start_runner(runner, L"Downloading from phone...");
}

static bool
fm_choose_upload_file(WCHAR *out, size_t out_len) {
    out[0] = L'\0';
    OPENFILENAMEW dialog;
    ZeroMemory(&dialog, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = fm.window;
    dialog.lpstrFile = out;
    dialog.nMaxFile = (DWORD) out_len;
    dialog.lpstrFilter = L"All files\0*.*\0\0";
    dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_FILEMUSTEXIST
                 | OFN_PATHMUSTEXIST;
    dialog.lpstrTitle = L"Upload file to Android";
    return GetOpenFileNameW(&dialog);
}

static void
fm_upload(void) {
    struct fm_runner *runner = fm_create_runner(FM_OPERATION_PUSH);
    if (!runner) {
        return;
    }
    if (!fm_choose_upload_file(
            runner->local_path,
            sizeof(runner->local_path) / sizeof(runner->local_path[0]))) {
        HeapFree(GetProcessHeap(), 0, runner);
        return;
    }

    const WCHAR *base = wcsrchr(runner->local_path, L'\\');
    base = base ? base + 1 : runner->local_path;
    char name[VR_FILE_MANAGER_MAX_NAME];
    if (!fm_wide_to_utf8(base, name, sizeof(name))
            || !vr_file_manager_join_path(
                fm.current_path, name, runner->remote_path,
                sizeof(runner->remote_path))) {
        MessageBoxW(fm.window, L"The selected filename is not supported.",
                    L"Upload", MB_OK | MB_ICONERROR);
        HeapFree(GetProcessHeap(), 0, runner);
        return;
    }

    if (MessageBoxW(fm.window,
                    L"Upload this file to the current Android folder?\n\n"
                    L"An existing file with the same name may be overwritten.",
                    L"Confirm upload",
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
        HeapFree(GetProcessHeap(), 0, runner);
        return;
    }
    fm_start_runner(runner, L"Uploading to phone...");
}

static bool
fm_get_name_from_editor(char *name, size_t name_len) {
    WCHAR wide[VR_FILE_MANAGER_MAX_NAME];
    GetWindowTextW(fm.name_edit, wide,
                   (int) (sizeof(wide) / sizeof(wide[0])));
    return fm_wide_to_utf8(wide, name, name_len)
        && vr_file_manager_name_is_safe(name);
}

static void
fm_create_folder(void) {
    char name[VR_FILE_MANAGER_MAX_NAME];
    if (!fm_get_name_from_editor(name, sizeof(name))) {
        MessageBoxW(fm.window,
                    L"Enter a valid folder name in the Name field.",
                    L"New folder", MB_OK | MB_ICONWARNING);
        return;
    }

    struct fm_runner *runner = fm_create_runner(FM_OPERATION_MKDIR);
    if (!runner) {
        return;
    }
    if (!vr_file_manager_join_path(
            fm.current_path, name, runner->remote_path,
            sizeof(runner->remote_path))) {
        HeapFree(GetProcessHeap(), 0, runner);
        return;
    }
    fm_start_runner(runner, L"Creating folder...");
}

static void
fm_rename_selected(void) {
    char source[VR_FILE_MANAGER_MAX_PATH];
    char name[VR_FILE_MANAGER_MAX_NAME];
    if (!fm_selected_remote_path(source, sizeof(source))
            || !fm_get_name_from_editor(name, sizeof(name))) {
        MessageBoxW(fm.window,
                    L"Select an item and enter its new name in the Name field.",
                    L"Rename", MB_OK | MB_ICONWARNING);
        return;
    }

    struct fm_runner *runner = fm_create_runner(FM_OPERATION_RENAME);
    if (!runner) {
        return;
    }
    snprintf(runner->remote_path, sizeof(runner->remote_path), "%s", source);
    if (!vr_file_manager_join_path(
            fm.current_path, name, runner->second_remote_path,
            sizeof(runner->second_remote_path))) {
        HeapFree(GetProcessHeap(), 0, runner);
        return;
    }
    if (!strcmp(runner->remote_path, runner->second_remote_path)) {
        HeapFree(GetProcessHeap(), 0, runner);
        fm_set_status(L"The name has not changed.");
        return;
    }
    fm_start_runner(runner, L"Renaming item...");
}

static void
fm_delete_selected(void) {
    const struct vr_file_manager_entry *entry = fm_get_selected_entry();
    char remote[VR_FILE_MANAGER_MAX_PATH];
    if (!entry || !fm_selected_remote_path(remote, sizeof(remote))
            || !strcmp(remote, "/sdcard")) {
        fm_set_status(L"Select a file or folder first.");
        return;
    }

    WCHAR name[VR_FILE_MANAGER_MAX_NAME];
    if (!fm_utf8_to_wide(entry->name, name,
                         sizeof(name) / sizeof(name[0]))) {
        wcscpy(name, L"selected item");
    }
    WCHAR prompt[VR_FILE_MANAGER_MAX_NAME + 180];
    swprintf(prompt, sizeof(prompt) / sizeof(prompt[0]),
             L"Permanently delete \"%ls\" from the phone?\n\n"
             L"Folders and their contents will be removed recursively. "
             L"This cannot be undone.",
             name);
    if (MessageBoxW(fm.window, prompt, L"Confirm permanent delete",
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
        return;
    }

    struct fm_runner *runner = fm_create_runner(FM_OPERATION_DELETE);
    if (!runner) {
        return;
    }
    snprintf(runner->remote_path, sizeof(runner->remote_path), "%s", remote);
    fm_start_runner(runner, L"Deleting from phone...");
}

static void
fm_update_name_from_selection(void) {
    const struct vr_file_manager_entry *entry = fm_get_selected_entry();
    WCHAR name[VR_FILE_MANAGER_MAX_NAME];
    if (entry && fm_utf8_to_wide(entry->name, name,
                                 sizeof(name) / sizeof(name[0]))) {
        SetWindowTextW(fm.name_edit, name);
    }
}

static void
fm_activate_selected(void) {
    const struct vr_file_manager_entry *entry = fm_get_selected_entry();
    char path[VR_FILE_MANAGER_MAX_PATH];
    if (!entry || entry->type != VR_FILE_MANAGER_ENTRY_DIRECTORY
            || !vr_file_manager_join_path(fm.current_path, entry->name,
                                          path, sizeof(path))) {
        return;
    }
    fm_navigate_to(path);
}

static void
fm_show_error(const struct fm_done *done) {
    WCHAR output[1024] = L"ADB file operation failed.";
    if (done->output && *done->output) {
        int byte_count = (int) strlen(done->output);
        if (byte_count > 900) {
            byte_count = 900;
        }
        int converted = MultiByteToWideChar(
            CP_UTF8, 0, done->output, byte_count, output,
            (int) (sizeof(output) / sizeof(output[0])) - 2);
        if (converted > 0) {
            output[converted] = L'\0';
            if ((size_t) converted + 4 < sizeof(output) / sizeof(output[0])
                    && done->output[byte_count]) {
                wcscat(output, L"\n...");
            }
        } else {
            wcscpy(output, L"ADB file operation failed. See the VR Mobile log for details.");
        }
    }
    MessageBoxW(fm.window, output, L"VR Mobile File Manager",
                MB_OK | MB_ICONERROR);
}

static void
fm_handle_done(struct fm_done *done) {
    fm_set_busy(false);
    if (done->exit_code != 0) {
        fm_show_error(done);
        fm_set_status(L"File operation failed.");
        return;
    }

    if (done->operation == FM_OPERATION_LIST) {
        fm.entry_count = vr_file_manager_parse_listing(
            done->output ? done->output : "", fm.entries,
            VR_FILE_MANAGER_MAX_ENTRIES);
        vr_file_cache_replace_folder(&fm.cache, done->remote_path, fm.entries,
                                     fm.entry_count);
        vr_file_cache_save(&fm.cache);
        fm_show_cached_folder();
        WCHAR status[256];
        swprintf(status, sizeof(status) / sizeof(status[0]),
                 L"%zu item(s) refreshed — %hs", fm.entry_count,
                 done->remote_path);
        fm_set_status(status);
        return;
    }

    if (done->operation == FM_OPERATION_INDEX_ALL) {
        if (!done->index_cache) {
            fm_set_status(L"The local file index could not be prepared.");
            return;
        }
        vr_file_cache_destroy(&fm.cache);
        fm.cache = *done->index_cache;
        HeapFree(GetProcessHeap(), 0, done->index_cache);
        done->index_cache = NULL;
        fm_show_cached_folder();
        WCHAR status[256];
        swprintf(status, sizeof(status) / sizeof(status[0]),
                 L"All storage indexed — %zu files and folders cached",
                 fm.cache.count);
        fm_set_status(status);
        return;
    }

    switch (done->operation) {
        case FM_OPERATION_PULL:
            fm_set_status(L"Download completed.");
            break;
        case FM_OPERATION_PUSH:
            fm_set_status(L"Upload completed.");
            fm_refresh();
            break;
        case FM_OPERATION_MKDIR:
            fm_set_status(L"Folder created.");
            SetWindowTextW(fm.name_edit, L"");
            fm_refresh();
            break;
        case FM_OPERATION_RENAME:
            fm_set_status(L"Item renamed.");
            SetWindowTextW(fm.name_edit, L"");
            fm_refresh();
            break;
        case FM_OPERATION_DELETE:
            fm_set_status(L"Item deleted.");
            SetWindowTextW(fm.name_edit, L"");
            fm_refresh();
            break;
        default:
            break;
    }
}

static void
fm_resize(HWND hwnd) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    int width = rect.right;
    int height = rect.bottom;
    const int margin = 22;
    const int gap = 10;
    const int button_h = 38;
    const int path_button_w = 92;

    int x = margin;
    int y = margin;
    MoveWindow(fm.heading, x, y, width - 2 * margin, 34, TRUE);
    y += 34;
    MoveWindow(fm.subheading, x, y, width - 2 * margin, 24, TRUE);
    y += 24 + gap;
    const int load_all_w = 142;
    int path_w = width - 2 * margin - 3 * path_button_w - load_all_w
               - 4 * gap;
    MoveWindow(fm.path_edit, x, y, path_w, button_h, TRUE);
    x += path_w + gap;
    MoveWindow(GetDlgItem(hwnd, ID_FM_GO), x, y, path_button_w, button_h,
               TRUE);
    x += path_button_w + gap;
    MoveWindow(GetDlgItem(hwnd, ID_FM_UP), x, y, path_button_w, button_h,
               TRUE);
    x += path_button_w + gap;
    MoveWindow(GetDlgItem(hwnd, ID_FM_REFRESH), x, y, path_button_w, button_h,
               TRUE);
    x += path_button_w + gap;
    MoveWindow(GetDlgItem(hwnd, ID_FM_LOAD_ALL), x, y, load_all_w, button_h,
               TRUE);

    y += button_h + gap;
    int bottom_h = button_h * 2 + gap * 3 + 24;
    MoveWindow(fm.list, margin, y, width - 2 * margin,
               height - y - bottom_h - margin, TRUE);

    y = height - bottom_h;
    int label_w = 50;
    int action_w = 115;
    MoveWindow(GetDlgItem(hwnd, 5199), margin, y, label_w, button_h, TRUE);
    MoveWindow(fm.name_edit, margin + label_w, y,
               width - 2 * margin - label_w - 3 * action_w - 3 * gap,
               button_h, TRUE);
    x = width - margin - 3 * action_w - 2 * gap;
    MoveWindow(GetDlgItem(hwnd, ID_FM_NEW_FOLDER), x, y, action_w, button_h,
               TRUE);
    x += action_w + gap;
    MoveWindow(GetDlgItem(hwnd, ID_FM_RENAME), x, y, action_w, button_h, TRUE);
    x += action_w + gap;
    MoveWindow(GetDlgItem(hwnd, ID_FM_DELETE), x, y, action_w, button_h, TRUE);

    y += button_h + gap;
    MoveWindow(GetDlgItem(hwnd, ID_FM_DOWNLOAD), margin, y, 120, button_h,
               TRUE);
    MoveWindow(GetDlgItem(hwnd, ID_FM_UPLOAD), margin + 120 + gap, y, 120,
               button_h, TRUE);
    MoveWindow(fm.status, margin + 248 + gap, y,
               width - margin - (margin + 248 + gap), button_h, TRUE);
}

static HWND
fm_create_button(HWND parent, const WCHAR *text, int id) {
    return CreateWindowW(L"BUTTON", text,
                         WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_NOTIFY,
                         0, 0, 0, 0, parent,
                         (HMENU) (uintptr_t) id, fm.instance, NULL);
}

static enum vr_theme_button_variant
fm_button_variant(int id) {
    switch (id) {
        case ID_FM_GO:
        case ID_FM_UPLOAD:
            return VR_THEME_BUTTON_PRIMARY;
        case ID_FM_REFRESH:
        case ID_FM_LOAD_ALL:
        case ID_FM_DOWNLOAD:
        case ID_FM_NEW_FOLDER:
            return VR_THEME_BUTTON_POSITIVE;
        case ID_FM_DELETE:
            return VR_THEME_BUTTON_DANGER;
        default:
            return VR_THEME_BUTTON_DEFAULT;
    }
}

static enum vr_theme_icon
fm_button_icon(int id) {
    switch (id) {
        case ID_FM_GO:
            return VR_THEME_ICON_GO;
        case ID_FM_UP:
            return VR_THEME_ICON_UP;
        case ID_FM_REFRESH:
            return VR_THEME_ICON_REFRESH;
        case ID_FM_LOAD_ALL:
            return VR_THEME_ICON_DOWNLOAD;
        case ID_FM_DOWNLOAD:
            return VR_THEME_ICON_DOWNLOAD;
        case ID_FM_UPLOAD:
            return VR_THEME_ICON_UPLOAD;
        case ID_FM_NEW_FOLDER:
            return VR_THEME_ICON_ADD_FOLDER;
        case ID_FM_RENAME:
            return VR_THEME_ICON_RENAME;
        case ID_FM_DELETE:
            return VR_THEME_ICON_DELETE;
        default:
            return VR_THEME_ICON_NONE;
    }
}

static LRESULT CALLBACK
fm_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_CREATE:
            vr_theme_apply_window(hwnd);
            fm.heading = CreateWindowW(
                L"STATIC", L"Phone Storage",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                0, 0, 0, 0, hwnd, (HMENU) ID_FM_HEADING,
                fm.instance, NULL);
            fm.subheading = CreateWindowW(
                L"STATIC",
                L"Secure ADB access  \x2022  Shared storage (/sdcard)",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                0, 0, 0, 0, hwnd, (HMENU) ID_FM_SUBHEADING,
                fm.instance, NULL);
            fm.path_edit =
                CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"/sdcard",
                                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                0, 0, 0, 0, hwnd, (HMENU) ID_FM_PATH,
                                fm.instance, NULL);
            fm_create_button(hwnd, L"Go", ID_FM_GO);
            fm_create_button(hwnd, L"Up", ID_FM_UP);
            fm_create_button(hwnd, L"Refresh", ID_FM_REFRESH);
            fm_create_button(hwnd, L"Load all files", ID_FM_LOAD_ALL);

            fm.list =
                CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL
                                    | LVS_REPORT | LVS_SINGLESEL
                                    | LVS_SHOWSELALWAYS,
                                0, 0, 0, 0, hwnd, (HMENU) ID_FM_LIST,
                                fm.instance, NULL);
            ListView_SetExtendedListViewStyle(
                fm.list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

            LVCOLUMNW column;
            ZeroMemory(&column, sizeof(column));
            column.mask = LVCF_TEXT | LVCF_WIDTH;
            column.pszText = L"Name";
            column.cx = 430;
            ListView_InsertColumn(fm.list, 0, &column);
            column.pszText = L"Type";
            column.cx = 90;
            ListView_InsertColumn(fm.list, 1, &column);
            column.pszText = L"Size";
            column.cx = 110;
            ListView_InsertColumn(fm.list, 2, &column);
            column.pszText = L"Modified";
            column.cx = 150;
            ListView_InsertColumn(fm.list, 3, &column);

            CreateWindowW(L"STATIC", L"Name:",
                          WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
                          0, 0, 0, 0, hwnd, (HMENU) 5199,
                          fm.instance, NULL);
            fm.name_edit =
                CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                0, 0, 0, 0, hwnd, (HMENU) ID_FM_NAME,
                                fm.instance, NULL);
            fm_create_button(hwnd, L"New Folder", ID_FM_NEW_FOLDER);
            fm_create_button(hwnd, L"Rename", ID_FM_RENAME);
            fm_create_button(hwnd, L"Delete", ID_FM_DELETE);
            fm_create_button(hwnd, L"Download", ID_FM_DOWNLOAD);
            fm_create_button(hwnd, L"Upload", ID_FM_UPLOAD);
            fm.status = CreateWindowW(
                L"STATIC", L"Ready", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
                0, 0, 0, 0, hwnd, (HMENU) ID_FM_STATUS, fm.instance, NULL);

            fm.title_font = CreateFontW(
                -26, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
                L"Segoe UI Variable Display");
            fm.ui_font = CreateFontW(
                -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            SendMessageW(fm.heading, WM_SETFONT, (WPARAM) fm.title_font, TRUE);
            SendMessageW(fm.subheading, WM_SETFONT, (WPARAM) fm.ui_font, TRUE);
            int font_controls[] = {
                ID_FM_PATH, ID_FM_GO, ID_FM_UP, ID_FM_REFRESH,
                ID_FM_LOAD_ALL, ID_FM_LIST,
                5199, ID_FM_NAME, ID_FM_NEW_FOLDER, ID_FM_RENAME,
                ID_FM_DELETE, ID_FM_DOWNLOAD, ID_FM_UPLOAD, ID_FM_STATUS,
            };
            for (size_t i = 0;
                    i < sizeof(font_controls) / sizeof(font_controls[0]);
                    ++i) {
                SendMessageW(GetDlgItem(hwnd, font_controls[i]), WM_SETFONT,
                             (WPARAM) fm.ui_font, TRUE);
            }
            vr_theme_apply_edit(fm.path_edit);
            vr_theme_apply_listview(fm.list);
            vr_theme_apply_edit(fm.name_edit);
            fm_resize(hwnd);
            return 0;

        case WM_SIZE:
            fm_resize(hwnd);
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wparam)) {
                case ID_FM_GO:
                    fm_go_from_editor();
                    return 0;
                case ID_FM_UP:
                    fm_go_up();
                    return 0;
                case ID_FM_REFRESH:
                    fm_refresh();
                    return 0;
                case ID_FM_LOAD_ALL:
                    fm_load_all();
                    return 0;
                case ID_FM_DOWNLOAD:
                    fm_download_selected();
                    return 0;
                case ID_FM_UPLOAD:
                    fm_upload();
                    return 0;
                case ID_FM_NEW_FOLDER:
                    fm_create_folder();
                    return 0;
                case ID_FM_RENAME:
                    fm_rename_selected();
                    return 0;
                case ID_FM_DELETE:
                    fm_delete_selected();
                    return 0;
            }
            break;

        case WM_DRAWITEM:
            if (vr_theme_draw_button((DRAWITEMSTRUCT *) lparam,
                                     fm_button_variant(LOWORD(wparam)),
                                     fm_button_icon(LOWORD(wparam)))) {
                return TRUE;
            }
            break;

        case WM_ERASEBKGND:
            return vr_theme_erase_background(hwnd, (HDC) wparam);

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            return vr_theme_control_color(msg, (HDC) wparam, (HWND) lparam);

        case WM_NOTIFY:
            {
                NMHDR *header = (NMHDR *) lparam;
                if (header->idFrom == ID_FM_LIST) {
                    if (header->code == LVN_ITEMCHANGED) {
                        fm_update_name_from_selection();
                    } else if (header->code == NM_DBLCLK) {
                        fm_activate_selected();
                    }
                }
            }
            return 0;

        case WM_FM_DONE:
            {
                struct fm_done *done = (struct fm_done *) lparam;
                fm_handle_done(done);
                HeapFree(GetProcessHeap(), 0, done->output);
                if (done->index_cache) {
                    vr_file_cache_destroy(done->index_cache);
                    HeapFree(GetProcessHeap(), 0, done->index_cache);
                }
                HeapFree(GetProcessHeap(), 0, done);
            }
            return 0;

        case WM_FM_INDEX_PROCESSING:
            fm_set_status(L"Phone scan complete — building the local cache...");
            return 0;

        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;

        case WM_DESTROY:
            fm.window = NULL;
            fm.heading = NULL;
            fm.subheading = NULL;
            fm.path_edit = NULL;
            fm.list = NULL;
            fm.name_edit = NULL;
            fm.status = NULL;
            DeleteObject(fm.title_font);
            DeleteObject(fm.ui_font);
            fm.title_font = NULL;
            fm.ui_font = NULL;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void
vr_file_manager_show(HINSTANCE instance, HWND owner, const WCHAR *adb_path,
                     const char *serial) {
    fm.instance = instance;
    fm.owner = owner;
    wcsncpy(fm.adb_path, adb_path,
            sizeof(fm.adb_path) / sizeof(fm.adb_path[0]) - 1);
    fm.adb_path[sizeof(fm.adb_path) / sizeof(fm.adb_path[0]) - 1] = L'\0';
    snprintf(fm.serial, sizeof(fm.serial), "%s", serial);
    if (!fm.cache_initialized || strcmp(fm.cache.serial, serial)) {
        if (fm.cache_initialized) {
            vr_file_cache_destroy(&fm.cache);
        }
        vr_file_cache_init(&fm.cache, serial);
        fm.cache_initialized = true;
        vr_file_cache_load(&fm.cache);
    }

    if (!fm.window) {
        if (!fm_com_initialized) {
            HRESULT result = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
            fm_com_initialized = SUCCEEDED(result)
                              || result == RPC_E_CHANGED_MODE;
        }

        INITCOMMONCONTROLSEX controls = {
            .dwSize = sizeof(controls),
            .dwICC = ICC_LISTVIEW_CLASSES,
        };
        InitCommonControlsEx(&controls);

        WNDCLASSW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = fm_window_proc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon = vr_theme_app_icon(false);
        wc.hbrBackground = vr_theme_background_brush();
        wc.lpszClassName = L"VRMobileFileManagerWindow";
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            MessageBoxW(owner, L"Could not register File Manager window.",
                        L"VR Mobile", MB_OK | MB_ICONERROR);
            return;
        }

        fm.window = CreateWindowExW(
            0, wc.lpszClassName, L"VR Mobile \x2014 Phone Storage",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1040, 720,
            owner, NULL, instance, NULL);
        if (!fm.window) {
            MessageBoxW(owner, L"Could not create File Manager window.",
                        L"VR Mobile", MB_OK | MB_ICONERROR);
            return;
        }
        SendMessageW(fm.window, WM_SETICON, ICON_SMALL,
                     (LPARAM) vr_theme_app_icon(true));
    }

    snprintf(fm.current_path, sizeof(fm.current_path), "%s", "/sdcard");
    SetWindowTextW(fm.path_edit, L"/sdcard");
    ShowWindow(fm.window, SW_SHOW);
    ShowWindow(fm.window, SW_RESTORE);
    SetForegroundWindow(fm.window);
    fm_show_cached_folder();
}
