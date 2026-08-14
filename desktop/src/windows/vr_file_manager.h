#ifndef VR_FILE_MANAGER_H
#define VR_FILE_MANAGER_H

#include <windows.h>

void
vr_file_manager_show(HINSTANCE instance, HWND owner, const WCHAR *adb_path,
                     const char *serial);

#endif
