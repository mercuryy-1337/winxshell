//
// WinUIHost.cpp — Phase 0 stub. See header.
//
// IMPORTANT: this file is the *only* place cppwinrt may be included. As of
// Phase 0 we do not pull cppwinrt in yet — only the bootstrap presence probe
// is implemented. Phase 2 will add the DispatcherQueueController +
// Microsoft.UI.Xaml.Application bring-up here, behind the same C facade.
//

#include <windows.h>
#include <objbase.h>
#include <appmodel.h>
#include <WindowsAppSDK-VersionInfo.h>
#include "WinUIHost.h"

// Name of the Windows App SDK bootstrap DLL installed in the system PATH when
// the runtime is present. Probing for this is the cheapest test that "WinUI 3
// is plausibly usable" without committing to a specific runtime version.
static LPCSTR const BOOTSTRAP_DLL = "Microsoft.WindowsAppRuntime.Bootstrap.dll";

static BOOL g_initialized = FALSE;
static DWORD g_last_error = ERROR_NOT_READY;
static BOOL g_com_initialized = FALSE;
static BOOL g_bootstrap_initialized = FALSE;
static HMODULE g_bootstrap_module = NULL;

typedef HRESULT(WINAPI *MddBootstrapInitialize2Fn)(
    UINT32 majorMinorVersion, PCWSTR versionTag, PACKAGE_VERSION minVersion,
    UINT32 options);
typedef void(WINAPI *MddBootstrapShutdownFn)(void);

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
        g_last_error = ERROR_SUCCESS;
        return TRUE;
    }
    g_last_error = GetLastError();
    cached = 0;
    return FALSE;
}

DWORD WinUIHost_GetLastError(void)
{
    return g_last_error;
}

BOOL WinUIHost_Initialize(void)
{
    if (g_initialized)
        return TRUE;

    // The taskbar's XAML island must live on an STA. Treat an incompatible
    // apartment as a recoverable renderer failure, not a shell-start failure.
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        g_last_error = static_cast<DWORD>(hr);
        return FALSE;
    }
    g_com_initialized = (hr == S_OK);

    // Unpackaged WinXShell explicitly bootstraps the exact NuGet-pinned runtime
    // before it touches any Windows App SDK or WinUI API. Resolve its exports at
    // runtime: recent Windows App SDK packages provide the bootstrap DLL but no
    // import library for this legacy Win32 project.
    g_bootstrap_module = LoadLibraryExA(
        BOOTSTRAP_DLL, NULL,
        LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!g_bootstrap_module) {
        g_last_error = GetLastError();
        if (g_com_initialized) {
            CoUninitialize();
            g_com_initialized = FALSE;
        }
        return FALSE;
    }

    MddBootstrapInitialize2Fn bootstrap_initialize =
        reinterpret_cast<MddBootstrapInitialize2Fn>(
            GetProcAddress(g_bootstrap_module, "MddBootstrapInitialize2"));
    if (!bootstrap_initialize) {
        g_last_error = GetLastError();
        FreeLibrary(g_bootstrap_module);
        g_bootstrap_module = NULL;
        if (g_com_initialized) {
            CoUninitialize();
            g_com_initialized = FALSE;
        }
        return FALSE;
    }

    PACKAGE_VERSION minimum_version = {};
    minimum_version.Version = WINDOWSAPPSDK_RUNTIME_VERSION_UINT64;
    hr = bootstrap_initialize(
        WINDOWSAPPSDK_RELEASE_MAJORMINOR,
        WINDOWSAPPSDK_RELEASE_VERSION_TAG_W,
        minimum_version,
        0);
    if (FAILED(hr)) {
        g_last_error = static_cast<DWORD>(hr);
        FreeLibrary(g_bootstrap_module);
        g_bootstrap_module = NULL;
        if (g_com_initialized) {
            CoUninitialize();
            g_com_initialized = FALSE;
        }
        return FALSE;
    }

    g_bootstrap_initialized = TRUE;
    g_last_error = ERROR_SUCCESS;
    g_initialized = TRUE;
    return TRUE;
}

BOOL WinUIHost_IsInitialized(void)
{
    return g_initialized;
}

void WinUIHost_Shutdown(void)
{
    if (!g_initialized) return;

    // Future island/XAML objects must be released before this point. The
    // bootstrap runtime cannot be unloaded while WinUI objects remain alive.
    g_initialized = FALSE;
    if (g_bootstrap_initialized) {
        MddBootstrapShutdownFn bootstrap_shutdown =
            reinterpret_cast<MddBootstrapShutdownFn>(
                GetProcAddress(g_bootstrap_module, "MddBootstrapShutdown"));
        if (bootstrap_shutdown)
            bootstrap_shutdown();
        g_bootstrap_initialized = FALSE;
    }
    if (g_bootstrap_module) {
        FreeLibrary(g_bootstrap_module);
        g_bootstrap_module = NULL;
    }
    if (g_com_initialized) {
        CoUninitialize();
        g_com_initialized = FALSE;
    }
}
