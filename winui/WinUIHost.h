#pragma once
//
// WinUIHost.h
//
// Phase 0 of REVAMP.md: C facade for WinUI 3 / Windows App SDK hosting.
//
// Legacy translation units must NEVER include cppwinrt headers — they
// overflow BSCMAKE's .bsc size limit in Debug. This header keeps the
// cppwinrt surface confined to WinUIHost.cpp (and, later, to dedicated
// surface .cpp files such as WinUIStartMenu.cpp).
//
// Phase 0 only ships the probe + lifecycle stubs. The real
// DispatcherQueueController + Microsoft.UI.Xaml.Application bring-up
// lands in Phase 2.
//
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns TRUE if the Windows App SDK bootstrap DLL can be loaded on this
// machine. Cheap presence check only — does NOT initialize anything.
// Safe to call before WinUIHost_Initialize.
BOOL WinUIHost_IsAvailable(void);

// Brings up the singleton dispatcher + Application instance on the calling
// thread. Returns TRUE on success. Must be called on the main UI thread,
// after Win32 message-loop init but before any WinUI 3 surface is created.
// Stub in Phase 0 (always returns FALSE); real impl in Phase 2.
BOOL WinUIHost_Initialize(void);

// Tears down everything brought up by WinUIHost_Initialize. Must be called
// before bootstrap unloads, otherwise we crash on shutdown (lesson from the
// reverted 651d666 attempt). Idempotent.
void WinUIHost_Shutdown(void);

#ifdef __cplusplus
}
#endif
