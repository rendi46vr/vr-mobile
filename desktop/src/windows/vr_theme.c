#define UNICODE
#define _UNICODE

#include "vr_theme.h"

#include <dwmapi.h>
#include <uxtheme.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

/*
 * VR Mobile palette
 * background  #151515
 * surface     #1C1C1C
 * elevated    #262626
 * border      #3A3A3A
 * text        #ECECEC
 * muted       #A3A3A3
 * primary     #3B3B3B
 * positive    #3F8F62
 * danger      #B95858
 */
static const COLORREF VR_COLOR_BACKGROUND = RGB(21, 21, 21);
static const COLORREF VR_COLOR_SURFACE = RGB(28, 28, 28);
static const COLORREF VR_COLOR_ELEVATED = RGB(38, 38, 38);
static const COLORREF VR_COLOR_BORDER = RGB(58, 58, 58);
static const COLORREF VR_COLOR_TEXT = RGB(236, 236, 236);
static const COLORREF VR_COLOR_MUTED = RGB(163, 163, 163);
static const COLORREF VR_COLOR_PRIMARY = RGB(59, 59, 59);
static const COLORREF VR_COLOR_POSITIVE = RGB(63, 143, 98);
static const COLORREF VR_COLOR_DANGER = RGB(185, 88, 88);
static const COLORREF VR_COLOR_DISABLED = RGB(69, 69, 69);

static HBRUSH background_brush;
static HBRUSH surface_brush;
static HFONT icon_font;
static HICON app_icon_large;
static HICON app_icon_small;
static bool initialized;

static HICON
create_app_icon(int size) {
    if (size < 16) {
        size = 16;
    }

    BITMAPV5HEADER header;
    ZeroMemory(&header, sizeof(header));
    header.bV5Size = sizeof(header);
    header.bV5Width = size;
    header.bV5Height = -size;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;

    HDC screen = GetDC(NULL);
    uint32_t *pixels = NULL;
    HBITMAP color = CreateDIBSection(
        screen, (BITMAPINFO *) &header, DIB_RGB_COLORS, (void **) &pixels,
        NULL, 0);
    ReleaseDC(NULL, screen);
    if (!color || !pixels) {
        if (color) {
            DeleteObject(color);
        }
        return LoadIcon(NULL, IDI_APPLICATION);
    }

    memset(pixels, 0, (size_t) size * (size_t) size * sizeof(*pixels));
    int inset = size / 16;
    int radius = size / 4;
    int left = inset;
    int top = inset;
    int right = size - inset - 1;
    int bottom = size - inset - 1;
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            int corner_x = x < left + radius
                         ? left + radius
                         : x > right - radius ? right - radius : x;
            int corner_y = y < top + radius
                         ? top + radius
                         : y > bottom - radius ? bottom - radius : y;
            int dx = x - corner_x;
            int dy = y - corner_y;
            if (dx * dx + dy * dy > radius * radius) {
                continue;
            }

            int mix = (x + y) * 255 / (2 * size);
            int red = (64 * (255 - mix) + 42 * mix) / 255;
            int green = (64 * (255 - mix) + 110 * mix) / 255;
            int blue = (64 * (255 - mix) + 72 * mix) / 255;
            pixels[y * size + x] = 0xFF000000u
                                 | ((uint32_t) red << 16)
                                 | ((uint32_t) green << 8)
                                 | (uint32_t) blue;
        }
    }

    HDC memory = CreateCompatibleDC(NULL);
    HGDIOBJ old_bitmap = SelectObject(memory, color);
    HFONT logo_font = CreateFontW(
        -(size * 42 / 100), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HGDIOBJ old_font = SelectObject(memory, logo_font);
    SetBkMode(memory, TRANSPARENT);
    SetTextColor(memory, RGB(255, 255, 255));
    RECT label = {0, -1, size, size};
    DrawTextW(memory, L"VR", -1, &label,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(memory, old_font);
    SelectObject(memory, old_bitmap);
    DeleteObject(logo_font);
    DeleteDC(memory);

    HBITMAP mask = CreateBitmap(size, size, 1, 1, NULL);
    ICONINFO info = {
        .fIcon = TRUE,
        .hbmMask = mask,
        .hbmColor = color,
    };
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(mask);
    DeleteObject(color);
    return icon ? icon : LoadIcon(NULL, IDI_APPLICATION);
}

static COLORREF
blend_color(COLORREF from, COLORREF to, int amount) {
    int inverse = 255 - amount;
    return RGB(
        (GetRValue(from) * inverse + GetRValue(to) * amount) / 255,
        (GetGValue(from) * inverse + GetGValue(to) * amount) / 255,
        (GetBValue(from) * inverse + GetBValue(to) * amount) / 255);
}

static COLORREF
adjust_color(COLORREF color, int delta) {
    int red = (int) GetRValue(color) + delta;
    int green = (int) GetGValue(color) + delta;
    int blue = (int) GetBValue(color) + delta;
    red = red < 0 ? 0 : red > 255 ? 255 : red;
    green = green < 0 ? 0 : green > 255 ? 255 : green;
    blue = blue < 0 ? 0 : blue > 255 ? 255 : blue;
    return RGB(red, green, blue);
}

void
vr_theme_init(void) {
    if (initialized) {
        return;
    }
    background_brush = CreateSolidBrush(VR_COLOR_BACKGROUND);
    surface_brush = CreateSolidBrush(VR_COLOR_SURFACE);
    icon_font = CreateFontW(
        -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe Fluent Icons");
    app_icon_large = create_app_icon(GetSystemMetrics(SM_CXICON));
    app_icon_small = create_app_icon(GetSystemMetrics(SM_CXSMICON));
    initialized = true;
}

HBRUSH
vr_theme_background_brush(void) {
    vr_theme_init();
    return background_brush;
}

HICON
vr_theme_app_icon(bool small) {
    vr_theme_init();
    return small ? app_icon_small : app_icon_large;
}

COLORREF
vr_theme_text_color(void) {
    return VR_COLOR_TEXT;
}

COLORREF
vr_theme_muted_text_color(void) {
    return VR_COLOR_MUTED;
}

void
vr_theme_apply_window(HWND hwnd) {
    vr_theme_init();
    BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark)))) {
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    }

    /*
     * Windows 11 otherwise keeps the user's accent color on the caption,
     * which can clash with the application palette. Unsupported attributes
     * are safely ignored by older Windows releases.
     */
    DwmSetWindowAttribute(hwnd, 34, &VR_COLOR_BORDER,
                          sizeof(VR_COLOR_BORDER));
    DwmSetWindowAttribute(hwnd, 35, &VR_COLOR_BACKGROUND,
                          sizeof(VR_COLOR_BACKGROUND));
    DwmSetWindowAttribute(hwnd, 36, &VR_COLOR_TEXT,
                          sizeof(VR_COLOR_TEXT));
}

void
vr_theme_apply_edit(HWND hwnd) {
    SetWindowTheme(hwnd, L"DarkMode_Explorer", NULL);
    SendMessageW(hwnd, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(10, 10));
}

void
vr_theme_apply_listbox(HWND hwnd) {
    SetWindowTheme(hwnd, L"DarkMode_Explorer", NULL);
}

void
vr_theme_apply_listview(HWND hwnd) {
    SetWindowTheme(hwnd, L"DarkMode_Explorer", NULL);
    ListView_SetBkColor(hwnd, VR_COLOR_SURFACE);
    ListView_SetTextBkColor(hwnd, VR_COLOR_SURFACE);
    ListView_SetTextColor(hwnd, VR_COLOR_TEXT);
    HWND header = ListView_GetHeader(hwnd);
    if (header) {
        SetWindowTheme(header, L"DarkMode_Explorer", NULL);
    }
}

void
vr_theme_apply_checkbox(HWND hwnd) {
    SetWindowTheme(hwnd, L"DarkMode_Explorer", NULL);
}

void
vr_theme_apply_surface_label(HWND hwnd) {
    SetPropW(hwnd, L"VRThemeSurface", (HANDLE) (uintptr_t) 1);
}

bool
vr_theme_erase_background(HWND hwnd, HDC dc) {
    if (!hwnd || !dc) {
        return false;
    }

    vr_theme_init();
    RECT rect;
    GetClientRect(hwnd, &rect);
    FillRect(dc, &rect, background_brush);

    HGDIOBJ old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    int radius = width / 3;
    if (radius < 220) {
        radius = 220;
    }
    if (radius > 430) {
        radius = 430;
    }

    int center_x = width - 45;
    int center_y = 5;
    for (int ring = 0; ring < 24; ++ring) {
        int current = radius - ring * radius / 30;
        int amount = 4 + ring / 2;
        COLORREF color =
            blend_color(VR_COLOR_BACKGROUND, VR_COLOR_PRIMARY, amount);
        HBRUSH brush = CreateSolidBrush(color);
        HGDIOBJ old_brush = SelectObject(dc, brush);
        Ellipse(dc, center_x - current, center_y - current,
                center_x + current, center_y + current);
        SelectObject(dc, old_brush);
        DeleteObject(brush);
    }

    int teal_radius = radius * 2 / 3;
    int teal_x = 10;
    int teal_y = height - 10;
    for (int ring = 0; ring < 18; ++ring) {
        int current = teal_radius - ring * teal_radius / 24;
        int amount = 2 + ring / 3;
        COLORREF color =
            blend_color(VR_COLOR_BACKGROUND, VR_COLOR_POSITIVE, amount);
        HBRUSH brush = CreateSolidBrush(color);
        HGDIOBJ old_brush = SelectObject(dc, brush);
        Ellipse(dc, teal_x - current, teal_y - current,
                teal_x + current, teal_y + current);
        SelectObject(dc, old_brush);
        DeleteObject(brush);
    }
    SelectObject(dc, old_pen);
    return true;
}

LRESULT
vr_theme_control_color(UINT msg, HDC dc, HWND control) {
    vr_theme_init();
    SetBkMode(dc, TRANSPARENT);

    if (msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX
            || (control && GetPropW(control, L"VRThemeSurface"))) {
        SetTextColor(dc, VR_COLOR_TEXT);
        SetBkColor(dc, VR_COLOR_SURFACE);
        return (LRESULT) surface_brush;
    }

    WCHAR class_name[16];
    if (control && GetClassNameW(control, class_name,
                                 sizeof(class_name) / sizeof(class_name[0]))
            && !lstrcmpiW(class_name, L"Edit")) {
        SetTextColor(dc, VR_COLOR_TEXT);
        SetBkColor(dc, VR_COLOR_SURFACE);
        return (LRESULT) surface_brush;
    }

    SetTextColor(dc, VR_COLOR_TEXT);
    SetBkColor(dc, VR_COLOR_BACKGROUND);
    return (LRESULT) background_brush;
}

static const WCHAR *
icon_glyph(enum vr_theme_icon icon) {
    switch (icon) {
        case VR_THEME_ICON_CONNECT:
            return L"\xE703";
        case VR_THEME_ICON_DISCONNECT:
            return L"\xE8CD";
        case VR_THEME_ICON_WIRELESS:
            return L"\xE701";
        case VR_THEME_ICON_REFRESH:
            return L"\xE72C";
        case VR_THEME_ICON_STATUS:
            return L"\xE946";
        case VR_THEME_ICON_CLEAR:
            return L"\xE74D";
        case VR_THEME_ICON_EXIT:
            return L"\xE8BB";
        case VR_THEME_ICON_NETWORK:
            return L"\xE774";
        case VR_THEME_ICON_PHONE:
            return L"\xE8EA";
        case VR_THEME_ICON_FOLDER:
            return L"\xE8B7";
        case VR_THEME_ICON_DOWNLOAD:
            return L"\xE896";
        case VR_THEME_ICON_UPLOAD:
            return L"\xE898";
        case VR_THEME_ICON_SAVE:
            return L"\xE74E";
        case VR_THEME_ICON_PLAY:
            return L"\xE768";
        case VR_THEME_ICON_DELETE:
            return L"\xE74D";
        case VR_THEME_ICON_ADD_FOLDER:
            return L"\xE8F4";
        case VR_THEME_ICON_RENAME:
            return L"\xE8AC";
        case VR_THEME_ICON_UP:
            return L"\xE70E";
        case VR_THEME_ICON_GO:
            return L"\xE72A";
        case VR_THEME_ICON_OPEN:
            return L"\xE8A7";
        case VR_THEME_ICON_REPLY:
            return L"\xE97A";
        case VR_THEME_ICON_NONE:
        default:
            return NULL;
    }
}

bool
vr_theme_draw_button(const DRAWITEMSTRUCT *item,
                     enum vr_theme_button_variant variant,
                     enum vr_theme_icon icon) {
    if (!item || item->CtlType != ODT_BUTTON) {
        return false;
    }

    vr_theme_init();
    COLORREF background;
    switch (variant) {
        case VR_THEME_BUTTON_PRIMARY:
            background = VR_COLOR_PRIMARY;
            break;
        case VR_THEME_BUTTON_POSITIVE:
            background = VR_COLOR_POSITIVE;
            break;
        case VR_THEME_BUTTON_DANGER:
            background = VR_COLOR_DANGER;
            break;
        case VR_THEME_BUTTON_DEFAULT:
        default:
            background = VR_COLOR_ELEVATED;
            break;
    }

    bool disabled = (item->itemState & ODS_DISABLED) != 0;
    bool selected = (item->itemState & ODS_SELECTED) != 0;
    bool focused = (item->itemState & ODS_FOCUS) != 0;
    if (disabled) {
        background = VR_COLOR_DISABLED;
    } else if (selected) {
        background = adjust_color(background, -24);
    }

    COLORREF border = focused && !disabled ? VR_COLOR_TEXT : VR_COLOR_BORDER;
    HBRUSH brush = CreateSolidBrush(background);
    HPEN pen = CreatePen(PS_SOLID, focused ? 2 : 1, border);
    HGDIOBJ old_brush = SelectObject(item->hDC, brush);
    HGDIOBJ old_pen = SelectObject(item->hDC, pen);

    RECT rect = item->rcItem;
    FillRect(item->hDC, &rect, background_brush);
    InflateRect(&rect, -1, -1);
    RoundRect(item->hDC, rect.left, rect.top, rect.right, rect.bottom, 12, 12);

    SelectObject(item->hDC, old_pen);
    SelectObject(item->hDC, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);

    WCHAR label[256];
    GetWindowTextW(item->hwndItem, label,
                   sizeof(label) / sizeof(label[0]));
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, disabled ? VR_COLOR_MUTED : VR_COLOR_TEXT);
    if (selected) {
        OffsetRect(&rect, 0, 1);
    }

    const WCHAR *glyph = icon_glyph(icon);
    HFONT label_font =
        (HFONT) SendMessageW(item->hwndItem, WM_GETFONT, 0, 0);
    if (!label_font) {
        label_font = (HFONT) GetStockObject(DEFAULT_GUI_FONT);
    }

    if (!glyph || !icon_font) {
        HGDIOBJ old_font = SelectObject(item->hDC, label_font);
        DrawTextW(item->hDC, label, -1, &rect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(item->hDC, old_font);
        return true;
    }

    SIZE icon_size = {0};
    SIZE label_size = {0};
    HGDIOBJ old_font = SelectObject(item->hDC, icon_font);
    GetTextExtentPoint32W(item->hDC, glyph, 1, &icon_size);
    SelectObject(item->hDC, label_font);
    GetTextExtentPoint32W(item->hDC, label, (int) wcslen(label), &label_size);

    int available = rect.right - rect.left - 10;
    int spacing = 6;
    int content_width = icon_size.cx + spacing + label_size.cx;
    if (content_width > available) {
        content_width = available;
    }
    int content_left = rect.left
                     + ((rect.right - rect.left - content_width) / 2);
    RECT icon_rect = {
        content_left,
        rect.top,
        content_left + icon_size.cx,
        rect.bottom,
    };
    SelectObject(item->hDC, icon_font);
    DrawTextW(item->hDC, glyph, 1, &icon_rect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT label_rect = {
        icon_rect.right + spacing,
        rect.top,
        rect.right - 5,
        rect.bottom,
    };
    SelectObject(item->hDC, label_font);
    DrawTextW(item->hDC, label, -1, &label_rect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(item->hDC, old_font);
    return true;
}
