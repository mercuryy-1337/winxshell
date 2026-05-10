#pragma once

//
// ThemeBridge — translates JCFG theme values into WinUI 3 resource
// dictionary entries so the existing JS_THEMES configuration drives the
// new XAML UI.
//
// The bridge is read-only on the WinUI side: callers query bridged colors,
// fonts, and metrics and the bridge handles the JCFG/Win32 lookup. When
// USE_WINUI3 is off or the host isn't initialized, accessors return
// sensible Win32 fallbacks.
//
// Phase 0 ships only the contract; Phase 1 wires actual JCFG_THEME_* reads
// to Microsoft.UI.Xaml.ResourceDictionary updates.
//

#ifdef USE_WINUI3

namespace winui {

struct ThemeColor {
    BYTE a;  // alpha
    BYTE r;
    BYTE g;
    BYTE b;
};

class ThemeBridge {
public:
    // True if the active JCFG theme requests light mode.
    static bool IsLightTheme();

    // Read a color from JCFG_THEME (e.g., "JS_TASKBAR" / "taskbar" /
    // "highlight_color"), falling back to the supplied default.
    // Phase 1 maps these into the WinUI resource dictionary.
    static ThemeColor GetThemeColor(LPCSTR section, LPCSTR theme_key,
                                    LPCSTR property, COLORREF fallback);

    // Apply currently-loaded theme values to the global WinUI resource
    // dictionary. Called once after Application is created and again on
    // theme changes. Implemented in Phase 1.
    static void ApplyToApplication();

    // Re-read JCFG and re-apply. Hooked to the JCFG reload path so
    // live-editing the config updates open WinUI windows.
    static void Reload();

private:
    ThemeBridge() = delete;
};

} // namespace winui

#endif // USE_WINUI3
