#pragma once

//
// WinUIWindow — base class for a WinUI 3 desktop window hosted from Win32.
//
// Each subclass (StartMenu in Phase 1, Settings/About in Phase 4, etc.)
// owns a single Microsoft.UI.Xaml.Window and exposes its HWND so the
// surrounding Win32 code (taskbar, message routing, focus tracking) can
// keep talking to it through familiar window-handle APIs.
//
// Phase 0 ships only the contract; Phase 1 implements Show/Hide/Close and
// the XAML island plumbing.
//

#ifdef USE_WINUI3

namespace winui {

class WinUIWindow {
public:
    virtual ~WinUIWindow() {}

    // Create the underlying WinUI 3 Window. Returns false if the host
    // isn't available (caller should fall back to Win32 UI).
    virtual bool Create() = 0;

    // The HWND of the WinUI 3 desktop window. NULL until Create() succeeds.
    // Used for SetForegroundWindow, IsWindow checks, message routing.
    virtual HWND GetHwnd() const = 0;

    // Show/hide/destroy the window.
    virtual void Show() = 0;
    virtual void Hide() = 0;
    virtual void Close() = 0;

    // True iff the window is currently visible.
    virtual bool IsVisible() const = 0;

protected:
    WinUIWindow() = default;

private:
    WinUIWindow(const WinUIWindow &) = delete;
    WinUIWindow &operator=(const WinUIWindow &) = delete;
};

} // namespace winui

#endif // USE_WINUI3
