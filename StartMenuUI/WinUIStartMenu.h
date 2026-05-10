#pragma once

//
// WinUI 3 start menu — C-style facade.
//
// The legacy Win32 code (desktopbar.cpp, startmenu.cpp) uses these
// functions to control the new start menu without pulling cppwinrt
// headers into the rest of the codebase.
//
// All functions are no-ops / return false when WinUI 3 is unavailable
// (g_Globals._winui3_available == false). Callers must keep the legacy
// StartMenuRoot path alive as a fallback.
//

#ifdef USE_WINUI3

#ifdef __cplusplus
extern "C" {
#endif

// One-time process initialization. Safe to call repeatedly.
// Returns nonzero on success.
int WinUIStartMenu_Initialize(void);

// Show the start menu, anchored above the supplied start button HWND.
// The taskbar HWND is used to compute the work area / monitor placement.
// Returns nonzero on success.
int WinUIStartMenu_Show(HWND hwnd_start_button, HWND hwnd_taskbar);

// Hide the start menu (does not destroy it).
void WinUIStartMenu_Hide(void);

// Returns nonzero if the start menu is currently visible.
int WinUIStartMenu_IsVisible(void);

// HWND of the WinUI 3 desktop window backing the start menu.
// NULL until the window has been created. Useful for routing focus,
// IsWindow checks, and excluding from taskbar enumeration.
HWND WinUIStartMenu_GetHwnd(void);

// Tear down the menu. Called once at process exit before WinUIHost::Shutdown.
void WinUIStartMenu_Shutdown(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // USE_WINUI3
