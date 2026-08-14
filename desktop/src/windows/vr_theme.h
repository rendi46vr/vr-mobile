#ifndef VR_THEME_H
#define VR_THEME_H

#include <windows.h>
#include <commctrl.h>
#include <stdbool.h>

enum vr_theme_button_variant {
    VR_THEME_BUTTON_DEFAULT,
    VR_THEME_BUTTON_PRIMARY,
    VR_THEME_BUTTON_POSITIVE,
    VR_THEME_BUTTON_DANGER,
};

enum vr_theme_icon {
    VR_THEME_ICON_NONE,
    VR_THEME_ICON_CONNECT,
    VR_THEME_ICON_DISCONNECT,
    VR_THEME_ICON_WIRELESS,
    VR_THEME_ICON_REFRESH,
    VR_THEME_ICON_STATUS,
    VR_THEME_ICON_CLEAR,
    VR_THEME_ICON_EXIT,
    VR_THEME_ICON_NETWORK,
    VR_THEME_ICON_PHONE,
    VR_THEME_ICON_FOLDER,
    VR_THEME_ICON_DOWNLOAD,
    VR_THEME_ICON_UPLOAD,
    VR_THEME_ICON_SAVE,
    VR_THEME_ICON_PLAY,
    VR_THEME_ICON_DELETE,
    VR_THEME_ICON_ADD_FOLDER,
    VR_THEME_ICON_RENAME,
    VR_THEME_ICON_UP,
    VR_THEME_ICON_GO,
    VR_THEME_ICON_OPEN,
    VR_THEME_ICON_REPLY,
};

void
vr_theme_init(void);

HBRUSH
vr_theme_background_brush(void);

HICON
vr_theme_app_icon(bool small);

COLORREF
vr_theme_text_color(void);

COLORREF
vr_theme_muted_text_color(void);

void
vr_theme_apply_window(HWND hwnd);

void
vr_theme_apply_edit(HWND hwnd);

void
vr_theme_apply_listbox(HWND hwnd);

void
vr_theme_apply_listview(HWND hwnd);

void
vr_theme_apply_checkbox(HWND hwnd);

void
vr_theme_apply_surface_label(HWND hwnd);

bool
vr_theme_erase_background(HWND hwnd, HDC dc);

LRESULT
vr_theme_control_color(UINT msg, HDC dc, HWND control);

bool
vr_theme_draw_button(const DRAWITEMSTRUCT *item,
                     enum vr_theme_button_variant variant,
                     enum vr_theme_icon icon);

#endif
