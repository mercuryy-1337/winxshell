//
// WinUIHost — Phase 1 implementation.
//

#define NOMINMAX
#include <windows.h>
#include <DispatcherQueue.h>
#include "WinUIHost.h"

#ifdef USE_WINUI3

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

extern bool g_winui3_available_flag;

namespace mux  = winrt::Microsoft::UI::Xaml;
namespace mud  = winrt::Microsoft::UI::Dispatching;

namespace winui {

namespace {

// Minimal Application subclass. We need a concrete Application instance to
// satisfy WinUI 3's requirement that Application::Current be set before any
// XAML window is created. We don't override OnLaunched because we never go
// through the standard packaged-app activation pipeline — we drive Window
// creation from our own Win32 code.
//
// IXamlMetadataProvider isn't needed here because we never load XAML
// markup (no XAML compiler in this project, no XamlReader::Load calls);
// the start menu UI is built programmatically with stock controls.
struct App : mux::ApplicationT<App>
{
    App() = default;
};

bool g_apartmentInitialized = false;
bool g_ready              = false;
mud::DispatcherQueueController g_dispatcher{ nullptr };
mux::Application g_app{ nullptr };

} // namespace

bool WinUIHost::IsAvailable()
{
    return g_winui3_available_flag;
}

bool WinUIHost::IsReady()
{
    return g_ready;
}

bool WinUIHost::EnsureReady()
{
    if (g_ready)
        return true;
    if (!g_winui3_available_flag)
        return false;

    try {
        if (!g_apartmentInitialized) {
            // The shell's main thread already runs Win32 message pumps with
            // OLE init, so try to attach as STA. init_apartment is a no-op
            // if the apartment is already initialized to a compatible mode.
            winrt::init_apartment(winrt::apartment_type::single_threaded);
            g_apartmentInitialized = true;
        }

        if (!g_dispatcher) {
            // Bind a Windows.System dispatcher to the current thread. WinUI 3
            // routes XAML events through this dispatcher, so it must live on
            // the same thread that owns the start menu window.
            DispatcherQueueOptions options{};
            options.dwSize        = sizeof(DispatcherQueueOptions);
            options.threadType    = DQTYPE_THREAD_CURRENT;
            options.apartmentType = DQTAT_COM_NONE;

            ABI::Windows::System::IDispatcherQueueController* abi_controller{ nullptr };
            HRESULT hr = ::CreateDispatcherQueueController(options, &abi_controller);
            if (FAILED(hr))
                return false;

            // Hand the raw ABI pointer to the WinAppSDK projection. The
            // projection types in winrt::Microsoft::UI::Dispatching mirror
            // Windows.System; the controller object is interchangeable.
            winrt::com_ptr<ABI::Windows::System::IDispatcherQueueController> ptr;
            ptr.attach(abi_controller);
            g_dispatcher = ptr.as<mud::DispatcherQueueController>();
        }

        if (!g_app) {
            // Constructing the App instance assigns Application::Current and
            // wires up the XAML resource resolution path. We do NOT call
            // Application::Start; that helper exists for stand-alone WinUI
            // apps and would block this thread on its own message pump.
            g_app = winrt::make<App>();
        }

        g_ready = true;
        return true;
    }
    catch (winrt::hresult_error const&) {
        return false;
    }
    catch (...) {
        return false;
    }
}

void WinUIHost::Shutdown()
{
    if (!g_ready)
        return;

    try {
        // Tear down in reverse order. The Application destructor closes its
        // resource references; the dispatcher controller's ShutdownQueueAsync
        // drains pending callbacks before the runtime unloads.
        if (g_app)
            g_app = nullptr;

        if (g_dispatcher) {
            // Fire-and-forget: we don't await the async result because we're
            // running on the dispatcher's own thread.
            g_dispatcher.ShutdownQueueAsync();
            g_dispatcher = nullptr;
        }
    }
    catch (...) {
        // Shutdown is best-effort.
    }

    g_ready = false;
}

} // namespace winui

#endif // USE_WINUI3
