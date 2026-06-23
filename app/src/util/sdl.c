#include "sdl.h"

#include <assert.h>
#include <stdlib.h>

#ifdef _WIN32
# include <windows.h>
#endif

#include "util/log.h"

#ifdef _WIN32
typedef void (WINAPI *sc_drag_accept_files_fn)(HWND, BOOL);
typedef BOOL (WINAPI *sc_change_window_message_filter_ex_fn)(
    HWND, UINT, DWORD, void *);

# ifndef MSGFLT_ALLOW
#  define MSGFLT_ALLOW 1
# endif

# ifndef WM_COPYGLOBALDATA
#  define WM_COPYGLOBALDATA 0x0049
# endif

static void
sc_sdl_enable_windows_file_drop(SDL_Window *window) {
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    if (!props) {
        LOGW("Could not get window properties for file drop: %s",
             SDL_GetError());
        return;
    }

    HWND hwnd = SDL_GetPointerProperty(
        props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    if (!hwnd) {
        LOGW("Could not get the Win32 window handle for file drop");
        return;
    }

    HMODULE shell32 = LoadLibraryW(L"shell32.dll");
    if (shell32) {
        union {
            FARPROC proc;
            sc_drag_accept_files_fn fn;
        } drag_accept = {
            .proc = GetProcAddress(shell32, "DragAcceptFiles"),
        };
        if (drag_accept.fn) {
            drag_accept.fn(hwnd, TRUE);
        }
        FreeLibrary(shell32);
    }

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) {
        return;
    }

    union {
        FARPROC proc;
        sc_change_window_message_filter_ex_fn fn;
    } change_filter = {
        .proc = GetProcAddress(user32, "ChangeWindowMessageFilterEx"),
    };
    if (!change_filter.fn) {
        return;
    }

    change_filter.fn(hwnd, WM_DROPFILES, MSGFLT_ALLOW, NULL);
    change_filter.fn(hwnd, WM_COPYDATA, MSGFLT_ALLOW, NULL);
    change_filter.fn(hwnd, WM_COPYGLOBALDATA, MSGFLT_ALLOW, NULL);
    LOGD("Windows Explorer file drop enabled for the mirror window");
}
#endif

SDL_Window *
sc_sdl_create_window(const char *title, int64_t x, int64_t y, int64_t width,
                     int64_t height, int64_t flags) {
    SDL_Window *window = NULL;

    SDL_PropertiesID props = SDL_CreateProperties();
    if (!props) {
        return NULL;
    }

    bool ok =
        SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING,
                              title);
    ok &= SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, x);
    ok &= SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, y);
    ok &= SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER,
                                width);
    ok &= SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER,
                                height);
    ok &= SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER,
                                flags);

    if (!ok) {
        SDL_DestroyProperties(props);
        return NULL;
    }

    window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    return window;
}

void
sc_sdl_enable_file_drop(SDL_Window *window) {
    assert(window);

    SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, true);
#ifdef _WIN32
    sc_sdl_enable_windows_file_drop(window);
#endif
}

struct sc_size
sc_sdl_get_window_size(SDL_Window *window) {
    int width;
    int height;
    bool ok = SDL_GetWindowSize(window, &width, &height);
    if (!ok) {
        LOGE("Could not get window size: %s", SDL_GetError());
        LOGE("Please report the error");
        // fatal error
        abort();
    }

    struct sc_size size = {
        .width = width,
        .height = height,
    };
    return size;
}

void
sc_sdl_set_window_size(SDL_Window *window, struct sc_size size) {
    bool ok = SDL_SetWindowSize(window, size.width, size.height);
    if (!ok) {
        LOGD("Could not set window size: %s", SDL_GetError());
    }
}

struct sc_point
sc_sdl_get_window_position(SDL_Window *window) {
    int x;
    int y;
    bool ok = SDL_GetWindowPosition(window, &x, &y);
    if (!ok) {
        LOGE("Could not get window position: %s", SDL_GetError());
        LOGE("Please report the error");
        // fatal error
        abort();
    }

    struct sc_point point = {
        .x = x,
        .y = y,
    };
    return point;
}

void
sc_sdl_set_window_position(SDL_Window *window, struct sc_point point) {
    bool ok = SDL_SetWindowPosition(window, point.x, point.y);
    if (!ok) {
        LOGD("Could not set window position: %s", SDL_GetError());
    }
}

void
sc_sdl_show_window(SDL_Window *window) {
    bool ok = SDL_ShowWindow(window);
    if (!ok) {
        LOGE("Could not show window: %s", SDL_GetError());
        assert(!"unexpected");
    }
}

void
sc_sdl_hide_window(SDL_Window *window) {
    bool ok = SDL_HideWindow(window);
    if (!ok) {
        LOGE("Could not hide window: %s", SDL_GetError());
        assert(!"unexpected");
    }
}

bool
sc_sdl_render_clear(SDL_Renderer *renderer) {
    bool ok = SDL_RenderClear(renderer);
    if (!ok) {
        LOGW("Could not clear rendering: %s", SDL_GetError());
    }
    return ok;
}

void
sc_sdl_render_present(SDL_Renderer *renderer) {
    bool ok = SDL_RenderPresent(renderer);
    if (!ok) {
        LOGE("Could not render: %s", SDL_GetError());
        assert(!"unexpected");
    }
}
