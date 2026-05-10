//
// WinUIHost — Phase 0 stub.
//
// Phase 0 only wires the bootstrapper. Dispatcher/Application lifetime
// is implemented in Phase 1 once the start menu window needs them.
//

#include "WinUIHost.h"

#ifdef USE_WINUI3

// Forward-declare the single global flag we read so this stub doesn't have
// to drag globals.h (and its precompiled-header dependencies) into the
// compilation unit. Phase 1 will replace this stub with real WinUI 3 code
// that includes the full headers.
extern bool g_winui3_available_flag;

namespace winui {

bool WinUIHost::IsAvailable()
{
    return g_winui3_available_flag;
}

bool WinUIHost::EnsureDispatcher()
{
    // Implemented in Phase 1.
    return false;
}

bool WinUIHost::EnsureApplication()
{
    // Implemented in Phase 1.
    return false;
}

void WinUIHost::Shutdown()
{
    // Implemented in Phase 1.
}

} // namespace winui

#endif // USE_WINUI3
