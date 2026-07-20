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
// This boundary owns runtime diagnostics and lifecycle decisions. The real
// DispatcherQueueController + Microsoft.UI.Xaml.Application implementation is
// intentionally isolated here so legacy shell code never depends on C++/WinRT.
//
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns TRUE if the Windows App SDK bootstrap DLL can be loaded on this
// machine. Cheap presence check only — does NOT initialize anything.
// Safe to call before WinUIHost_Initialize.
BOOL WinUIHost_IsAvailable(void);

// Last error captured by the availability probe or initialization. It is a
// Win32 error for LoadLibrary failures or an HRESULT represented as DWORD for
// COM/bootstrap failures. ERROR_SUCCESS means the last operation completed.
DWORD WinUIHost_GetLastError(void);

// Brings up the singleton dispatcher + Application instance on the calling
// thread. Returns TRUE on success. Must be called on the main UI thread,
// after Win32 message-loop init but before any WinUI 3 surface is created.
// It first initializes COM and the exact NuGet-pinned Windows App SDK runtime.
// The hosted XAML surface is added by the taskbar-surface lifecycle.
BOOL WinUIHost_Initialize(void);

// TRUE only after a WinUI surface can safely be created. This is deliberately
// separate from IsAvailable: a loadable bootstrap DLL is not yet an initialized
// Windows App SDK runtime.
BOOL WinUIHost_IsInitialized(void);

// Hosts the first WinUI taskbar surface inside Shell_TrayWnd. The surface is
// deliberately opaque to legacy controls: while the WinUI renderer is active,
// the old GDI/common-controls taskbar remains alive only as the shell bridge
// and fallback implementation.
BOOL WinUIHost_AttachTaskbar(HWND parent);

// Keeps the island child HWND in sync with Shell_TrayWnd's client rectangle.
// Safe to call for every WM_SIZE, including while no island is attached.
void WinUIHost_ResizeTaskbar(int width, int height);

// Applies the persisted icon-group position. When animate is TRUE, the group
// moves with a short spring rather than snapping; callers can use FALSE while
// constructing or restoring the taskbar.
void WinUIHost_SetTaskbarIconAlignment(BOOL centered, BOOL animate);

// Releases the hosted surface before the parent HWND or runtime disappears.
// This must precede WinUIHost_Shutdown.
void WinUIHost_DetachTaskbar(void);

// Tears down everything brought up by WinUIHost_Initialize. Must be called
// before bootstrap unloads, otherwise we crash on shutdown (lesson from the
// reverted 651d666 attempt). Idempotent.
void WinUIHost_Shutdown(void);

#ifdef __cplusplus
}
#endif
