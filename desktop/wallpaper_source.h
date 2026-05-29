#pragma once
//
// wallpaper_source.h
//
// Phase 1 of REVAMP.md: single source of truth for "what is the user's current
// wallpaper file." Reads HKCU\Control Panel\Desktop\Wallpaper (REG_SZ) directly
// rather than going through SystemParametersInfo(SPI_GETDESKWALLPAPER), and
// expands environment strings in the value.
//
// All consumers (desktop background composition, Lua desktop::getwallpaper,
// future WinUI 3 acrylic-tint sampling, etc.) must go through this header.
//
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// Reads HKCU\Control Panel\Desktop\Wallpaper, expands env strings into out
// (cch is its capacity in TCHARs, must be >= MAX_PATH). Writes an empty string
// and returns FALSE if the value is missing, empty, or the file does not exist
// on disk.
BOOL WallpaperSource_Get(LPTSTR out, DWORD cch);

// Starts a background watcher on HKCU\Control Panel\Desktop. When the
// Wallpaper value (or any sibling value) changes, PostMessage(hwndNotify, msg, 0, 0)
// is invoked. Returns FALSE if a watcher is already running or setup failed.
// Safe to call from the main thread only.
BOOL WallpaperSource_StartWatch(HWND hwndNotify, UINT msg);

// Stops the watcher started by WallpaperSource_StartWatch. Idempotent.
void WallpaperSource_StopWatch(void);

#ifdef __cplusplus
}
#endif
