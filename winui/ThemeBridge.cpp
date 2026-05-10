//
// ThemeBridge — Phase 0 stub.
//
// JCFG -> WinUI resource translation lands in Phase 1 alongside the first
// XAML window. For now we just provide the read-only accessors so callers
// can compile against the bridge.
//

#include <windows.h>
#include "ThemeBridge.h"

#ifdef USE_WINUI3

namespace winui {

bool ThemeBridge::IsLightTheme()
{
    // Phase 1 will route through TASKBAR_THEMESTYLE() / JCFG. Default to
    // dark mode (the legacy default) until that wiring lands.
    return false;
}

ThemeColor ThemeBridge::GetThemeColor(LPCSTR /*section*/, LPCSTR /*theme_key*/,
                                      LPCSTR /*property*/, COLORREF fallback)
{
    // Phase 1 will route through JCFG_THEME_DEF; for now return the fallback.
    ThemeColor c;
    c.a = 0xFF;
    c.r = GetRValue(fallback);
    c.g = GetGValue(fallback);
    c.b = GetBValue(fallback);
    return c;
}

void ThemeBridge::ApplyToApplication()
{
    // Phase 1.
}

void ThemeBridge::Reload()
{
    // Phase 1.
}

} // namespace winui

#endif // USE_WINUI3
