#ifndef VR_APP_SEARCH_H
#define VR_APP_SEARCH_H

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>

#define WM_VR_APP_SEARCH_HOTKEY (WM_APP + 40)
#define WM_VR_TAILSCALE_HOTKEY (WM_APP + 43)

typedef void (*vr_app_search_status_fn)(const WCHAR *message);
typedef bool (*vr_app_search_focus_fn)(const char *package_name, int user_id);

bool
vr_app_search_enable_shortcut(HWND owner);

void
vr_app_search_disable_shortcut(HWND owner);

bool
vr_app_search_is_hotkey(WPARAM hotkey_id);

void
vr_app_search_show(HINSTANCE instance, HWND owner, const char *serial,
                   const WCHAR *adb_path, const WCHAR *scrcpy_path,
                   vr_app_search_status_fn status_callback,
                   vr_app_search_focus_fn focus_callback);

void
vr_app_search_shutdown(void);

#endif
