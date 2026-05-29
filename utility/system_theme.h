#pragma once
//
// system_theme.h
//
// Detects Windows' dark/light app theme. The jcfg theme name "auto" is
// resolved to "dark" or "light" by reading
// HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize
// (DWORD value SystemUsesLightTheme; 1 == light, 0 == dark).
//
// Live updates: Windows broadcasts WM_SETTINGCHANGE with
// lParam == TEXT("ImmersiveColorSet") whenever the user toggles the theme.
// The taskbar listens for that and rebuilds its cached style.
//
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// 1 if the system is in light theme, 0 if dark. Defaults to dark if the
// registry value is missing (matches Windows 10/11 default).
int SystemTheme_IsLight(void);

// Returns "light" or "dark" as a TCHAR string literal. Stable address.
LPCTSTR SystemTheme_Name(void);

// Returns TRUE if lParam from a WM_SETTINGCHANGE message indicates an
// ImmersiveColorSet (dark/light theme toggle) broadcast.
BOOL SystemTheme_IsImmersiveColorSetChange(LPARAM lparam);

#ifdef __cplusplus
}
#endif
