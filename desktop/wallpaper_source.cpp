//
// wallpaper_source.cpp — implementation. See header.
//

#include <windows.h>
#include <tchar.h>
#include "wallpaper_source.h"

static LPCTSTR const DESKTOP_SUBKEY = TEXT("Control Panel\\Desktop");
static LPCTSTR const WALLPAPER_VALUE = TEXT("Wallpaper");

BOOL WallpaperSource_Get(LPTSTR out, DWORD cch)
{
    if (!out || cch < MAX_PATH) return FALSE;
    out[0] = TEXT('\0');

    HKEY hKey = NULL;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, DESKTOP_SUBKEY, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
        return FALSE;
    }

    TCHAR raw[MAX_PATH + 1] = { 0 };
    DWORD type = 0;
    DWORD cb = sizeof(raw) - sizeof(TCHAR); // leave room for terminator
    LONG r = RegQueryValueEx(hKey, WALLPAPER_VALUE, NULL, &type, (LPBYTE)raw, &cb);
    RegCloseKey(hKey);

    if (r != ERROR_SUCCESS) return FALSE;
    if (type != REG_SZ && type != REG_EXPAND_SZ) return FALSE;
    // RegQueryValueEx may or may not null-terminate; force it.
    DWORD chars = cb / sizeof(TCHAR);
    if (chars >= MAX_PATH) chars = MAX_PATH;
    raw[chars] = TEXT('\0');
    if (raw[0] == TEXT('\0')) return FALSE;

    DWORD expanded = ExpandEnvironmentStrings(raw, out, cch);
    if (expanded == 0 || expanded > cch) {
        // Fall back to the raw value if expansion failed/overflowed.
        lstrcpyn(out, raw, cch);
    }

    // Verify the file actually exists; otherwise treat as no-wallpaper.
    DWORD attr = GetFileAttributes(out);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        out[0] = TEXT('\0');
        return FALSE;
    }
    return TRUE;
}

// --- watcher ---------------------------------------------------------------

struct WallpaperWatcher {
    HANDLE  thread;
    HANDLE  stop_event;
    HKEY    hKey;
    HWND    hwndNotify;
    UINT    msg;
};

static WallpaperWatcher g_watcher = { NULL, NULL, NULL, NULL, 0 };

static DWORD WINAPI WallpaperWatchThread(LPVOID param)
{
    WallpaperWatcher *w = (WallpaperWatcher *)param;
    for (;;) {
        HANDLE change_event = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (!change_event) break;

        LONG r = RegNotifyChangeKeyValue(
            w->hKey, FALSE,
            REG_NOTIFY_CHANGE_LAST_SET,
            change_event, TRUE);
        if (r != ERROR_SUCCESS) {
            CloseHandle(change_event);
            break;
        }

        HANDLE waits[2] = { w->stop_event, change_event };
        DWORD wr = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        CloseHandle(change_event);

        if (wr == WAIT_OBJECT_0) break;            // stop requested
        if (wr != WAIT_OBJECT_0 + 1) break;        // error / abandoned

        if (w->hwndNotify && IsWindow(w->hwndNotify)) {
            PostMessage(w->hwndNotify, w->msg, 0, 0);
        }
    }
    return 0;
}

BOOL WallpaperSource_StartWatch(HWND hwndNotify, UINT msg)
{
    if (g_watcher.thread) return FALSE; // already running
    if (!hwndNotify || msg == 0) return FALSE;

    if (RegOpenKeyEx(HKEY_CURRENT_USER, DESKTOP_SUBKEY, 0, KEY_NOTIFY, &g_watcher.hKey) != ERROR_SUCCESS) {
        g_watcher.hKey = NULL;
        return FALSE;
    }
    g_watcher.stop_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_watcher.stop_event) {
        RegCloseKey(g_watcher.hKey);
        g_watcher.hKey = NULL;
        return FALSE;
    }
    g_watcher.hwndNotify = hwndNotify;
    g_watcher.msg = msg;
    g_watcher.thread = CreateThread(NULL, 0, WallpaperWatchThread, &g_watcher, 0, NULL);
    if (!g_watcher.thread) {
        CloseHandle(g_watcher.stop_event);
        g_watcher.stop_event = NULL;
        RegCloseKey(g_watcher.hKey);
        g_watcher.hKey = NULL;
        return FALSE;
    }
    return TRUE;
}

void WallpaperSource_StopWatch(void)
{
    if (!g_watcher.thread) return;
    if (g_watcher.stop_event) SetEvent(g_watcher.stop_event);
    WaitForSingleObject(g_watcher.thread, 2000);
    CloseHandle(g_watcher.thread);
    g_watcher.thread = NULL;
    if (g_watcher.stop_event) {
        CloseHandle(g_watcher.stop_event);
        g_watcher.stop_event = NULL;
    }
    if (g_watcher.hKey) {
        RegCloseKey(g_watcher.hKey);
        g_watcher.hKey = NULL;
    }
    g_watcher.hwndNotify = NULL;
    g_watcher.msg = 0;
}
