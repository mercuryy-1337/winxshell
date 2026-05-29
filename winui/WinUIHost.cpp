//
// WinUIHost.cpp — Phase 0 stub. See header.
//
// IMPORTANT: this file is the *only* place cppwinrt may be included. As of
// Phase 0 we do not pull cppwinrt in yet — only the bootstrap presence probe
// is implemented. Phase 2 will add the DispatcherQueueController +
// Microsoft.UI.Xaml.Application bring-up here, behind the same C facade.
//

#include <windows.h>
#include "WinUIHost.h"

// Name of the Windows App SDK bootstrap DLL installed in the system PATH when
// the runtime is present. Probing for this is the cheapest test that "WinUI 3
// is plausibly usable" without committing to a specific runtime version.
static LPCSTR const BOOTSTRAP_DLL = "Microsoft.WindowsAppRuntime.Bootstrap.dll";

static BOOL g_initialized = FALSE;

BOOL WinUIHost_IsAvailable(void)
{
    // Static cache: presence does not change at runtime.
    static int cached = -1;
    if (cached != -1) return cached ? TRUE : FALSE;

    HMODULE h = LoadLibraryExA(
        BOOTSTRAP_DLL, NULL,
        LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (h) {
        FreeLibrary(h);
        cached = 1;
        return TRUE;
    }
    cached = 0;
    return FALSE;
}

BOOL WinUIHost_Initialize(void)
{
    // Phase 0 stub. Real bring-up lands in Phase 2.
    return FALSE;
}

void WinUIHost_Shutdown(void)
{
    if (!g_initialized) return;
    g_initialized = FALSE;
}
