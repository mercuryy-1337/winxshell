//
// WinUIHost.cpp — WinUI 3 island boundary. See header.
//
// IMPORTANT: this is the only source file that includes C++/WinRT headers.
// The legacy shell continues to talk to this code through the small C facade
// in WinUIHost.h.
//

#include <windows.h>
#include <objbase.h>
#include <appmodel.h>
#include <chrono>
#include <WindowsAppSDK-VersionInfo.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
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
static HWND g_taskbar_parent = NULL;
static HWND g_taskbar_island = NULL;
static const int WINUI_START_COMMAND_ID = 0x1000; // IDC_START, isolated from legacy headers.

static winrt::Microsoft::UI::Dispatching::DispatcherQueueController g_dispatcher_queue{ nullptr };
static winrt::Microsoft::UI::Xaml::Hosting::WindowsXamlManager g_xaml_manager{ nullptr };
static winrt::Microsoft::UI::Xaml::Hosting::DesktopWindowXamlSource g_taskbar_source{ nullptr };
static winrt::Microsoft::UI::Xaml::Controls::Grid g_taskbar_root{ nullptr };
static winrt::Microsoft::UI::Xaml::Controls::StackPanel g_icon_strip{ nullptr };

typedef HRESULT(WINAPI *MddBootstrapInitialize2Fn)(
    UINT32 majorMinorVersion, PCWSTR versionTag, PACKAGE_VERSION minVersion,
    UINT32 options);
typedef void(WINAPI *MddBootstrapShutdownFn)(void);

static DWORD WinUIHost_HResultToError(HRESULT hr)
{
    return static_cast<DWORD>(hr);
}

static void WinUIHost_ClearTaskbarSurface(void)
{
    if (g_taskbar_source) {
        // Close before the parent HWND/runtime goes away. This also releases
        // the island child HWND created by DesktopWindowXamlSource.
        g_taskbar_source.Close();
        g_taskbar_source = nullptr;
    }
    g_icon_strip = nullptr;
    g_taskbar_root = nullptr;
    g_taskbar_island = NULL;
    g_taskbar_parent = NULL;
}

static void WinUIHost_UpdateIconStripAlignment(BOOL centered, BOOL animate)
{
    if (!g_icon_strip || !g_taskbar_root)
        return;

    using namespace winrt::Microsoft::UI::Xaml;
    using namespace winrt::Microsoft::UI::Xaml::Hosting;

    // Capture the old arranged position, change the layout rule, then offset
    // the visual back to its old pixels. The Composition spring brings it to
    // the newly-arranged position without a one-frame jump.
    float old_x = g_icon_strip.TransformToVisual(g_taskbar_root)
        .TransformPoint(winrt::Windows::Foundation::Point{ 0.0f, 0.0f }).X;
    g_icon_strip.HorizontalAlignment(centered ? HorizontalAlignment::Center : HorizontalAlignment::Left);
    g_taskbar_root.UpdateLayout();
    float new_x = g_icon_strip.TransformToVisual(g_taskbar_root)
        .TransformPoint(winrt::Windows::Foundation::Point{ 0.0f, 0.0f }).X;

    auto visual = ElementCompositionPreview::GetElementVisual(g_icon_strip);
    visual.Offset({ old_x - new_x, 0.0f, 0.0f });
    if (animate) {
        auto spring = visual.Compositor().CreateSpringVector3Animation();
        spring.FinalValue(winrt::Windows::Foundation::IReference<winrt::Windows::Foundation::Numerics::float3>(
            winrt::Windows::Foundation::Numerics::float3{ 0.0f, 0.0f, 0.0f }));
        spring.DampingRatio(0.72f);
        spring.Period(std::chrono::milliseconds(320));
        visual.StartAnimation(L"Offset", spring);
    }
    else {
        visual.Offset({ 0.0f, 0.0f, 0.0f });
    }
}

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

    try {
        // WinUI 3 hosting requires an App SDK dispatcher on the same STA that
        // owns Shell_TrayWnd. Create it only after bootstrap has selected the
        // exact framework runtime.
        g_dispatcher_queue =
            winrt::Microsoft::UI::Dispatching::DispatcherQueueController::CreateOnCurrentThread();
        g_xaml_manager =
            winrt::Microsoft::UI::Xaml::Hosting::WindowsXamlManager::InitializeForCurrentThread();
    }
    catch (winrt::hresult_error const& error) {
        g_last_error = WinUIHost_HResultToError(error.code());
        g_dispatcher_queue = nullptr;
        g_xaml_manager = nullptr;
        MddBootstrapShutdownFn bootstrap_shutdown =
            reinterpret_cast<MddBootstrapShutdownFn>(
                GetProcAddress(g_bootstrap_module, "MddBootstrapShutdown"));
        if (bootstrap_shutdown)
            bootstrap_shutdown();
        g_bootstrap_initialized = FALSE;
        FreeLibrary(g_bootstrap_module);
        g_bootstrap_module = NULL;
        if (g_com_initialized) {
            CoUninitialize();
            g_com_initialized = FALSE;
        }
        return FALSE;
    }

    g_last_error = ERROR_SUCCESS;
    g_initialized = TRUE;
    return TRUE;
}

BOOL WinUIHost_IsInitialized(void)
{
    return g_initialized;
}

BOOL WinUIHost_AttachTaskbar(HWND parent)
{
    if (!g_initialized || !parent) {
        g_last_error = ERROR_NOT_READY;
        return FALSE;
    }
    if (g_taskbar_source)
        return TRUE;

    try {
        using namespace winrt::Microsoft::UI;
        using namespace winrt::Microsoft::UI::Content;
        using namespace winrt::Microsoft::UI::Xaml;
        using namespace winrt::Microsoft::UI::Xaml::Controls;
        using namespace winrt::Microsoft::UI::Xaml::Hosting;
        using namespace winrt::Microsoft::UI::Xaml::Media;

        g_taskbar_parent = parent;
        g_taskbar_source = DesktopWindowXamlSource();
        g_taskbar_source.Initialize(winrt::Microsoft::UI::GetWindowIdFromWindow(parent));

        // A single XAML root owns the taskbar background. The packaged WinUI
        // version supplies AcrylicBrush's island-compatible backdrop directly;
        // do not use DesktopAcrylicBackdrop or parent-window composition here.
        g_taskbar_root = Grid();
        auto root = g_taskbar_root;
        auto acrylic = AcrylicBrush();
        acrylic.TintColor(winrt::Windows::UI::Color{ 255, 245, 245, 245 });
        acrylic.TintOpacity(0.46);
        acrylic.FallbackColor(winrt::Windows::UI::Color{ 255, 235, 235, 235 });
        root.Background(acrylic);

        g_icon_strip = StackPanel();
        auto icon_strip = g_icon_strip;
        icon_strip.Orientation(Orientation::Horizontal);
        icon_strip.HorizontalAlignment(HorizontalAlignment::Center);
        icon_strip.VerticalAlignment(VerticalAlignment::Center);
        icon_strip.Spacing(8);

        Button start_button;
        start_button.Width(44);
        start_button.Height(36);
        start_button.Padding(Thickness{ 0.0, 0.0, 0.0, 0.0 });

        // Project-owned Windows-11-style four-pane mark. Keeping it as XAML
        // geometry avoids a font-glyph dependency and the mojibake that the
        // old placeholder character produced in the legacy build encoding.
        Grid start_mark;
        start_mark.Width(18);
        start_mark.Height(18);
        for (int row = 0; row != 2; ++row) {
            start_mark.RowDefinitions().Append(RowDefinition());
            start_mark.ColumnDefinitions().Append(ColumnDefinition());
        }
        for (int row = 0; row != 2; ++row) {
            for (int column = 0; column != 2; ++column) {
                Border pane;
                pane.Background(SolidColorBrush(winrt::Windows::UI::Color{ 255, 0, 120, 212 }));
                pane.CornerRadius(CornerRadius{ 1.0, 1.0, 1.0, 1.0 });
                pane.Margin(Thickness{ 1.0, 1.0, 1.0, 1.0 });
                Grid::SetRow(pane, row);
                Grid::SetColumn(pane, column);
                start_mark.Children().Append(pane);
            }
        }
        start_button.Content(start_mark);
        start_button.Click([](auto const&, auto const&) {
            if (g_taskbar_parent)
                PostMessage(g_taskbar_parent, WM_COMMAND,
                    MAKEWPARAM(WINUI_START_COMMAND_ID, 0), 0);
        });
        icon_strip.Children().Append(start_button);

        root.Children().Append(icon_strip);

        g_taskbar_source.Content(root);

        auto bridge = g_taskbar_source.SiteBridge().as<DesktopChildSiteBridge>();
        g_taskbar_island = winrt::Microsoft::UI::GetWindowFromWindowId(bridge.WindowId());
        if (!g_taskbar_island) {
            g_last_error = ERROR_INVALID_WINDOW_HANDLE;
            WinUIHost_ClearTaskbarSurface();
            return FALSE;
        }

        RECT client = {};
        GetClientRect(parent, &client);
        WinUIHost_ResizeTaskbar(client.right - client.left, client.bottom - client.top);
        g_last_error = ERROR_SUCCESS;
        return TRUE;
    }
    catch (winrt::hresult_error const& error) {
        g_last_error = WinUIHost_HResultToError(error.code());
        WinUIHost_ClearTaskbarSurface();
        return FALSE;
    }
}

void WinUIHost_ResizeTaskbar(int width, int height)
{
    if (!g_taskbar_island)
        return;

    SetWindowPos(g_taskbar_island, HWND_TOP, 0, 0, width, height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void WinUIHost_SetTaskbarIconAlignment(BOOL centered, BOOL animate)
{
    try {
        WinUIHost_UpdateIconStripAlignment(centered, animate);
    }
    catch (winrt::hresult_error const& error) {
        g_last_error = WinUIHost_HResultToError(error.code());
    }
}

void WinUIHost_DetachTaskbar(void)
{
    WinUIHost_ClearTaskbarSurface();
}

void WinUIHost_Shutdown(void)
{
    if (!g_initialized) return;

    // Release every XAML object before bootstrap unloads. The runtime cannot
    // be unloaded safely while an island, Xaml manager, or dispatcher exists.
    WinUIHost_DetachTaskbar();
    g_xaml_manager = nullptr;
    g_dispatcher_queue = nullptr;
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
