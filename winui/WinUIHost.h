#pragma once

//
// WinUI 3 host singleton — owns the per-process DispatcherQueueController
// and the WinUI 3 Application object, and gates access on whether the
// Windows App SDK runtime was successfully bootstrapped.
//
// All WinUI 3 entry points must check WinUIHost::IsAvailable() before
// touching XAML or composition APIs. When the bootstrapper isn't
// initialized (no runtime installed, fallback build, etc.), callers must
// fall back to the legacy Win32 path.
//

#ifdef USE_WINUI3

namespace winui {

class WinUIHost {
public:
    // True iff the Windows App SDK runtime was bootstrapped at startup.
    // Mirrors g_Globals._winui3_available; this accessor exists so the
    // winui/ folder doesn't have to depend on globals.h directly.
    static bool IsAvailable();

    // Lazily creates the DispatcherQueueController on the calling thread.
    // Returns false if WinUI 3 is unavailable or the controller can't be
    // created (e.g., wrong COM apartment).
    //
    // Implemented in Phase 1 once the first WinUI 3 window is needed.
    static bool EnsureDispatcher();

    // Lazily creates the global xaml::Application instance. Required before
    // any WinUI 3 Window can be shown. Implemented in Phase 1.
    static bool EnsureApplication();

    // Tears down the dispatcher and application. Called at process exit
    // before MddBootstrapShutdown. Implemented in Phase 1.
    static void Shutdown();

private:
    WinUIHost() = delete;
};

} // namespace winui

#endif // USE_WINUI3
