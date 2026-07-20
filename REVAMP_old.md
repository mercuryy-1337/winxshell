# WinUI 3 Revamp

Phased plan for migrating WinXShell / Explauncher's UI layer to [WinUI 3](https://learn.microsoft.com/en-us/windows/apps/winui/winui3/) while preserving the project's lean, portable-shell character.

## Hard constraints

These apply to every phase. Any change that violates them must be reverted or reworked.

- **Release `.zip` install size must stay under 10 MB.** Each phase must be measured against `release/explauncher/explauncher.zip` before merge. If a WinUI 3 dependency would blow the budget, prefer a Win32/Direct2D/Composition fallback or load it dynamically.
- **Wallpaper source of truth is `HKEY_CURRENT_USER\Control Panel\Desktop` → `Wallpaper` (REG_SZ).** Replace `SystemParametersInfo(SPI_GETDESKWALLPAPER, ...)` and any JCFG-cached path reads with a direct registry read (with `RegNotifyChangeKeyValue` for live updates). Applies to desktop background, start menu acrylic source image, lock surfaces, and any other surface that needs the user wallpaper.

---

## Phase 0 — Foundations & guardrails

- [x] Add a script that fails if the shipped surface exceeds 10 MB. See [scripts/check_size.ps1](scripts/check_size.ps1). Scope: `Theme/Explauncher.exe` + `Theme/wxsStub*.dll` + `Theme/wxsUI/*` (whatever we ship as part of the shell). Bundled third-party tools under `Explorer/` are excluded. Current measurement: **5.37 MB / 10 MB** — ~4.6 MB headroom for WinUI 3 deps. Runs locally without admin.
- [x] **Deployment model decision: framework-dependent against the Windows App SDK runtime, with a runtime-presence probe at startup.** Self-contained deployment would add ~30+ MB of `Microsoft.UI.Xaml*.dll` / `Microsoft.WindowsAppRuntime.*.dll` to the shipped surface — blows the 10 MB budget by 3×. Framework-dependent costs nothing in the zip; the trade-off is that the test VM may not have the runtime installed, which is why we need the probe + Win32 fallback in the next bullet. Future: if we ever need to guarantee WinUI 3 on stock systems, revisit by *selectively* vendoring the Bootstrap DLL only (~1 MB) and letting it download the runtime on first launch.
- [x] Add a runtime capability check (`g_Globals._winui3_available`) that downgrades gracefully to the legacy Win32/GDI+ path when WinUI 3 / Windows App SDK isn't installed. Implemented in [globals.cpp](globals.cpp) via [WinUIHost_IsAvailable](winui/WinUIHost.cpp), which `LoadLibraryEx`s `Microsoft.WindowsAppRuntime.Bootstrap.dll` from system search paths.
- [x] Introduce a thin C facade (`winui/WinUIHost.{h,cpp}`) so legacy translation units never include `cppwinrt` headers (the previous attempt overflowed `.bsc` size — keep that boundary). [Stub in place](winui/WinUIHost.h); `Initialize`/`Shutdown` are no-ops until Phase 2.

## Phase 1 — Wallpaper unification

- [x] Add `WallpaperSource` helper that reads `HKCU\Control Panel\Desktop\Wallpaper` (REG_SZ), expands env strings, and returns a normalized path. See [desktop/wallpaper_source.h](desktop/wallpaper_source.h) / [.cpp](desktop/wallpaper_source.cpp). Also verifies the file exists on disk (returns FALSE on missing file rather than handing back a stale path).
- [x] Add `RegNotifyChangeKeyValue` watcher that re-emits a `WM_WALLPAPER_CHANGED` style notification (or callback) on change. `WallpaperSource_StartWatch(hwnd, msg)` runs a background thread, posts `msg` to `hwnd` on any value change under `HKCU\Control Panel\Desktop`. Not yet wired into the desktop window (existing `WM_SETTINGCHANGE`/`SPI_SETDESKWALLPAPER` path already covers the in-session change case); will be wired by future WinUI surfaces that need live updates.
- [x] Replace `SystemParametersInfo(SPI_GETDESKWALLPAPER, ...)` in [desktop.cpp](desktop/desktop.cpp) (`UpdateWallpaper`) with `WallpaperSource`. Also stopped writing the result back to `JS_DESKTOP.wallpaper` — the JCFG cache was a staleness risk; the helper is now the single read.
- [x] Repurpose `JS_DESKTOP.wallpaper` as an *optional* override. If set in JCFG, it wins; otherwise the registry is read directly via `WallpaperSource_Get`.
- [x] Audit [DesktopHelper.cpp](luaengine/DesktopHelper.cpp) — `desktop::getwallpaper` now goes through `WallpaperSource_Get`. [daemon.cpp](features/daemon.cpp:265) only *writes* (`SPI_SETDESKWALLPAPER` to broadcast change); no read to migrate.
- [ ] Wire the same source into any acrylic/Mica fallback that samples the wallpaper for tinting (start menu, future flyouts). Deferred to Phase 3 when the start menu lands.

## Phase 2 — WinUI 3 hosting skeleton

Design and build the host fresh. The earlier attempt at a `WinUIHost` was reverted because the implementation was broken — do **not** restore it as a reference. Treat the bullets below as requirements for a new implementation, not a port.

- [ ] Stand up a `DispatcherQueueController` + `Microsoft.UI.Xaml.Application` singleton on the main thread that cooperates with the existing Win32 message loop (does not hijack or replace it).
- [ ] Add lifetime hooks in [explorer.cpp](explorer.cpp) so WinUI surfaces tear down **before** bootstrap unloads. (Past attempts crashed on shutdown — design for clean teardown from the start, with explicit ownership and a documented shutdown order.)
- [ ] Verify Debug build keeps `BrowseInformation` disabled (cppwinrt header surface overflowed BSCMAKE last time).
- [ ] Confirm size budget still under 10 MB with the WinUI host present but no surfaces shown.

## Phase 3 — Start menu (first real surface)

Build the Windows-11-style start menu from scratch against the host from Phase 2. Required behavior:

- [ ] Borderless `WS_POPUP | WS_EX_TOOLWINDOW` window
- [ ] `AutoSuggestBox` search with Find icon, autofocus on show
- [ ] Pinned grid: `ItemsRepeater` + `UniformGridLayout`, real data (no placeholder tiles in shipped code)
- [ ] Recommended `ListView`: icon + title + timestamp rows, real recents data
- [ ] Footer: circular avatar (real username) + power glyph wired to existing power handlers
- [ ] Mica backdrop on Win11, Acrylic fallback on Win10
- [ ] Multi-monitor placement against the taskbar edge (top/bottom/left/right)
- [ ] Dismiss on `Window.Activated`/`Deactivated`
- [ ] Route `ShowOrHideStartMenu` in [desktopbar.cpp](taskbar/desktopbar.cpp) to the WinUI menu when `g_Globals._winui3_available`; keep legacy `StartMenuRoot` as the fallback.
- [ ] Pinned apps source: JCFG. Recents source: MRU. Power actions: existing handlers. No placeholders.
- [ ] Use `WallpaperSource` (Phase 1) for any acrylic tint sampling.

## Phase 4 — Taskbar surfaces

- [ ] Migrate the tray flyout (clock/calendar, network, volume) to WinUI 3 popups hosted by the existing Win32 taskbar window. Taskbar window stays Win32 — only the flyout content becomes XAML.
- [ ] Migrate window thumbnails ([thumbnail.cpp](taskbar/thumbnail.cpp)) to a XAML-hosted preview using `Microsoft.UI.Xaml.Hosting` if size budget allows; otherwise keep DWM thumbnail path.
- [ ] Keep the taskbar window itself Win32 — do **not** rewrite `taskbar.cpp` as XAML (size + perf risk).

## Phase 5 — Desktop & dialogs

- [ ] Replace GDI+ wallpaper composition in `DesktopShellView` with a `CompositionBrush` / `SpriteVisual` chain (via `Microsoft.UI.Composition`) if it fits the size budget. Otherwise keep GDI+ but ensure it reads from `WallpaperSource`.
- [ ] Port settings / preferences dialogs in [systemsettings/](systemsettings) and [dialogs/](dialogs) to XAML one at a time. Each port must show a before/after `.zip` size delta in its PR.

## Phase 6 — Cleanup & polish

- [ ] Remove dead legacy code paths *only* once the WinUI 3 equivalent has shipped and been validated on Win10 + Win11.
- [ ] Resolve animation jitter (the timer-based GDI+ approach noted in project memory) by moving animated surfaces onto `Microsoft.UI.Composition` animations.
- [ ] Document the final architecture (which surfaces are Win32, which are WinUI 3, how `WinUIHost` bridges them) in `doc/`.
- [ ] Final size audit: target ≤ 9 MB to leave headroom.

---

## Out of scope (for now)

- Packaging as MSIX. The project ships as a portable `.zip`; that stays.
- Rewriting the Lua scripting layer or JCFG config system.
- Replacing the Win32 message loop. WinUI 3 surfaces cooperate with it; they do not replace it.
