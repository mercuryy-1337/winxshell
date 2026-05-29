//
// system_theme.cpp - see header.
//
#include <windows.h>
#include <tchar.h>
#include "system_theme.h"

static LPCTSTR const PERSONALIZE_KEY =
    TEXT("Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize");

int SystemTheme_IsLight(void)
{
    HKEY hKey = NULL;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, PERSONALIZE_KEY, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
        return 0;
    }
    DWORD value = 0;
    DWORD cb = sizeof(value);
    DWORD type = 0;
    // SystemUsesLightTheme drives the taskbar/Start in real Win11.
    // AppsUseLightTheme drives apps. We follow the system one.
    LONG r = RegQueryValueEx(hKey, TEXT("SystemUsesLightTheme"), NULL, &type, (LPBYTE)&value, &cb);
    if (r != ERROR_SUCCESS || type != REG_DWORD) {
        // Fall back to AppsUseLightTheme if SystemUsesLightTheme is absent
        // (older Win10 builds only had the apps key).
        value = 0;
        cb = sizeof(value);
        r = RegQueryValueEx(hKey, TEXT("AppsUseLightTheme"), NULL, &type, (LPBYTE)&value, &cb);
        if (r != ERROR_SUCCESS || type != REG_DWORD) {
            RegCloseKey(hKey);
            return 0;
        }
    }
    RegCloseKey(hKey);
    return value ? 1 : 0;
}

LPCTSTR SystemTheme_Name(void)
{
    return SystemTheme_IsLight() ? TEXT("light") : TEXT("dark");
}

BOOL SystemTheme_IsImmersiveColorSetChange(LPARAM lparam)
{
    if (lparam == 0) return FALSE;
    LPCTSTR s = (LPCTSTR)lparam;
    // The broadcast strings are always narrow on the wire but get translated
    // to WCHAR before the wndproc sees them in a Unicode build.
    return _tcscmp(s, TEXT("ImmersiveColorSet")) == 0 ? TRUE : FALSE;
}
