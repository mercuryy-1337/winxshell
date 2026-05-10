#pragma once

//
// WinUI 3 host singleton — owns the per-process DispatcherQueueController
// and the Microsoft.UI.Xaml.Application singleton, and gates access on
// whether the Windows App SDK runtime was successfully bootstrapped.
//
// All WinUI 3 entry points must check WinUIHost::IsAvailable() before
// touching XAML or composition APIs. When the bootstrapper isn't
// initialized (no runtime installed, fallback build, etc.), callers must
// fall back to the legacy Win32 path.
//
// Lifetime model:
//
//   1. WinAppSdkSession (RAII in WinMain) bootstraps the runtime and sets
//      g_winui3_available_flag. WinUI 3 is now usable.
//
//   2. The first call to WinUIHost::EnsureReady() creates a
//      DispatcherQueueController on the calling thread (must be the main
//      UI thread / STA), then constructs the Microsoft.UI.Xaml.Application
//      singleton. Subsequent calls are no-ops.
//
//   3. WinUIHost::Shutdown() is called once at process exit, before the
//      WinAppSdkSession destructor runs the bootstrap shutdown.
//

#ifdef USE_WINUI3

namespace winui {

class WinUIHost {
public:
    // True iff the Windows App SDK runtime was bootstrapped at startup.
    // Mirrors g_Globals._winui3_available; this accessor exists so the
    // winui/ folder doesn't have to depend on globals.h directly.
    static bool IsAvailable();

    // Lazy-initializes the DispatcherQueueController and Application
    // singleton on the calling thread. Returns false if WinUI 3 is
    // unavailable or initialization fails (in which case callers must
    // fall back to the legacy Win32 path). Safe to call multiple times.
    //
    // Must be called from the main UI thread (an STA apartment).
    static bool EnsureReady();

    // True if EnsureReady() has previously succeeded on this process.
    static bool IsReady();

    // Tears down the application + dispatcher. Called once at process
    // exit before MddBootstrapShutdown.
    static void Shutdown();

private:
    WinUIHost() = delete;
};

} // namespace winui

#endif // USE_WINUI3
