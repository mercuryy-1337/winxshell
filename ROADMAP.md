# WinUI 3 Migration Roadmap

Migration of Explauncher from Win32/GDI+ to Windows App SDK (WinUI 3).

**Constraint:** Every phase must produce a single distributable `Explauncher.exe` (unpackaged deployment). No MSIX, no sideloading, no installer-only distribution.

**Minimum OS:** Windows 10 1809 (build 17763) — same as today.

---

## Phase 0 — Foundation: Windows App SDK Integration

Set up the build system so the existing Win32 exe can load the Windows App SDK runtime and create WinUI 3 content, without breaking anything that works today.

### 0.1 NuGet & Project Configuration
- [x] Add `Microsoft.WindowsAppSDK` NuGet package to `Explauncher.vcxproj`
- [x] Add `Microsoft.Windows.CppWinRT` NuGet package for C++/WinRT projections
- [x] Configure unpackaged deployment (`<WindowsPackageType>None</WindowsPackageType>`)
- [x] Set C++ language standard to `/std:c++17` (required by C++/WinRT)
- [x] Verify `/MT` (static CRT) still works with Windows App SDK libs
- [x] Verify all three platforms still build: Win32, x64, ARM *(Win32 + x64 verified; ARM needs an ARM toolchain to validate)*

### 0.2 Bootstrap Initialization
- [x] Add Windows App SDK bootstrapper call (`MddBootstrapInitialize`) early in `WinMain` (in `explorer.cpp`)
- [x] Add `MddBootstrapShutdown` at process exit
- [ ] Add auto-install/download logic for the Windows App SDK runtime if not present *(deferred — current behavior is to ship the bootstrap DLL alongside the exe; runtime auto-download lands in 0.3)*
- [x] Handle the bootstrapper failure case gracefully (fall back to pure Win32 mode)
- [x] Update `Explauncher.exe.manifest` with `maxversiontested` for Windows 10/11

### 0.3 Single-EXE Deployment Validation
- [ ] Confirm the exe runs on a clean machine with only the WinAppSDK runtime installed
- [ ] Document the runtime dependency (user needs Windows App SDK runtime, or we auto-install)
- [ ] Test that all existing features (taskbar, tray, shell hooks, Lua engine) still work identically
- [ ] Test shell replacement mode (`-shell`) still works
- [ ] Test daemon mode (`-daemon`) still works
- [x] Verify no MSIX packaging is required — pure unpackaged exe *(`WindowsPackageType=None`, `EnableCoreMrtTooling=false`, `AppxPackage=false`)*

### 0.4 Abstraction Layer
- [x] Create `winui/WinUIHost.h` — singleton that manages the `DispatcherQueueController` and `Application` object
- [x] Create `winui/WinUIWindow.h` — base class for hosting a WinUI 3 `DesktopWindow` from Win32 code
- [x] Create `winui/ThemeBridge.h` — bridges JCFG theme values to WinUI resource dictionaries
- [x] Add build-time `#define USE_WINUI3` flag so WinUI 3 features can be toggled off for fallback builds

**Exit criteria:** Project builds and runs identically to before. WinUI 3 headers are available. No visible changes to the user.

**Phase 0 implementation notes:**
- `packages.config` pins `Microsoft.WindowsAppSDK 1.6.241114003`, `Microsoft.Windows.CppWinRT 2.0.240405.15`, plus the transitive deps `Microsoft.Web.WebView2 1.0.2651.64` and `Microsoft.Windows.SDK.BuildTools 10.0.22621.756`. Restore from the project root: `nuget restore packages.config -PackagesDirectory packages`.
- `Microsoft.WindowsAppRuntime.Bootstrap.dll` is delay-loaded **and** loaded dynamically via `LoadLibrary` from the WinMain RAII guard `WinAppSdkSession`. If the DLL is missing or `MddBootstrapInitialize2` fails, `g_Globals._winui3_available` stays `false` and the shell continues in pure Win32 mode.
- The exe sets `_HAS_STD_BYTE=0` to avoid the `std::byte` vs GDI+ `byte` clash that `/std:c++17` exposes.
- Two pre-existing C++17 incompatibilities were patched as part of the toolchain bump: `bool++` increments in `taskbar/traynotify.cpp` (now `= true`) and `std::ptr_fun`/`std::not1` in `vendor/json.cpp` (now lambdas).
- Phase 0 items in section 0.3 marked as TODO require runtime testing on a clean install — see `Phase 0 — Validation Steps` below.

**Phase 0 — Validation steps (manual smoke-test):**
1. Copy `x64/Release/Explauncher.exe` and `x64/Release/Microsoft.WindowsAppRuntime.Bootstrap.dll` plus the existing `Theme/`, `Explorer/`, `Wallpapers/` folders to a target machine.
2. Install the Windows App SDK 1.6 runtime from <https://aka.ms/windowsappsdk/1.6/latest/windowsappruntimeinstall-x64.exe>.
3. Launch `Explauncher.exe -console` and check the log: should see `WinAppSDK bootstrap initialized`. If you see `WinAppSDK bootstrap DLL not found` or `MddBootstrapInitialize2 failed`, the runtime install didn't take.
4. Launch `Explauncher.exe -shell` to confirm shell replacement still works.
5. Launch `Explauncher.exe -daemon` to confirm daemon mode still works.
6. Smoke test the taskbar (window switching, tray icons, clock) and the existing start menu — Phase 0 should not regress any of these.
7. Delete `Microsoft.WindowsAppRuntime.Bootstrap.dll` from the install folder and relaunch — Explauncher should still start in compatibility mode (log will show `WinAppSDK bootstrap DLL not found`).

---

## Phase 1 — Start Menu Rewrite (WinUI 3)

Replace the hand-coded GDI+ start menu with a full WinUI 3 window. This is the highest-impact change — the current start menu is the most unfinished component and benefits the most from XAML layout and composition animations.

### 1.1 Start Menu Window Shell
- [x] ~~Create `StartMenuUI/StartMenuWindow.xaml`~~ — built programmatically in C++/WinRT instead (no XAML compiler in this project)
- [x] ~~Create `StartMenuUI/StartMenuWindow.xaml.cpp`~~ — replaced by [StartMenuUI/WinUIStartMenu.cpp](StartMenuUI/WinUIStartMenu.cpp)
- [x] Implement unpackaged `DesktopWindow` creation from the existing Win32 `DesktopBar`
- [x] Implement borderless, transparent window (no title bar, no chrome)
- [x] Match the current rounded-corner window region with XAML `CornerRadius` *(done via tile-level CornerRadius; outer window rounding will get DwmSetWindowAttribute pass in 1.6)*
- [x] Position the window above the taskbar, aligned to the start button
- [x] Handle multi-monitor placement
- [x] Implement click-away dismiss (via `Window.Activated` Deactivated event)
- [x] Wire up the start button toggle (open/close) to the new window *(in [DesktopBar::ShowOrHideStartMenu](taskbar/desktopbar.cpp:2603))*

### 1.2 Start Menu Layout
- [x] ~~Create `StartMenuUI/StartMenuPage.xaml`~~ — programmatic tree built by `BuildRoot()` in [WinUIStartMenu.cpp](StartMenuUI/WinUIStartMenu.cpp)
- [x] Search bar at top using `AutoSuggestBox`
- [x] "Pinned" section header with "All apps >" button
- [x] Pinned apps grid using `ItemsRepeater` + `UniformGridLayout` (6 columns × 3 rows of placeholder tiles)
- [x] "Recommended" section header with "More >" button
- [x] Recommended items list using `ListView`
- [x] Footer bar with user profile button (real Windows username) and power menu button
- [x] Scrollable regions with proper `ScrollViewer` integration

**Phase 1.1 + 1.2 implementation notes:**
- `WinUIHost` ([winui/WinUIHost.cpp](winui/WinUIHost.cpp)) owns the `DispatcherQueueController` + `Application` singleton on the main UI thread. We don't call `Application::Start` (it would hijack the message loop) — instead we construct `App` via `winrt::make<>` which is enough to register `Application::Current`.
- All WinUI 3 surfaces are gated on `g_Globals._winui3_available`; when bootstrap fails the legacy `StartMenuRoot` keeps working unchanged.
- The window is borderless `WS_POPUP | WS_EX_TOOLWINDOW`, anchored above the taskbar with monitor-aware placement (handles bottom, top, left, and right taskbars).
- Mica is applied when supported, falling back to DesktopAcrylic on Windows 10. Both go through `MicaController` / `DesktopAcrylicController` with a default `SystemBackdropConfiguration`.
- BrowseInformation was disabled in Debug configs (`BSCMAKE BK1520` overflow) — the legacy `.bsc` browse database can't fit cppwinrt's header surface. IntelliSense uses its own DB, so nothing user-visible is lost.

**Still to do for Phase 1 polish (deferred to 1.3+):**
- Real pinned-app data (currently 18 placeholder tiles)
- Real recommended documents (currently 4 placeholder rows)
- Click handlers for tiles, rows, profile button, power button
- Outer window corner rounding via `DwmSetWindowAttribute(DWMWA_WINDOW_CORNER_PREFERENCE, DWMWCP_ROUND)`
- Theme-driven coloring (currently hardcoded white-on-translucent)
- Icon extraction pipeline (currently uses Segoe Fluent Icon glyphs as placeholders)

### 1.3 Start Menu Data & Interaction
- [ ] Create `StartMenuUI/StartMenuViewModel.h` — data model for the menu
- [ ] Port `BuildProgramItems()` logic — enumerate Start Menu folders, extract icons
- [ ] Port `BuildRecommendedItems()` logic — recent documents, frequent apps
- [ ] Port `BuildDriveFolderItems()` logic — quick access drives/folders
- [ ] Implement icon extraction pipeline: `ICON_ID` -> `BitmapImage` or `SoftwareBitmapSource`
- [ ] Implement app launch on click (`ExecuteItem` logic)
- [ ] Implement right-click context menu using `MenuFlyout`
- [ ] Implement drag-and-drop for pinned tile reordering

### 1.4 Search
- [ ] Wire `AutoSuggestBox` query events to search logic
- [ ] Port `UpdateSearchResults()` — file/app search with Shell APIs
- [ ] Search results list with app icon, title, and path
- [ ] Search detail panel (open file location, copy path, run as admin)
- [ ] Keyboard navigation: arrow keys to browse results, Enter to launch
- [ ] Search home screen (recent searches, top apps) when search box is focused but empty

### 1.5 All Apps View
- [ ] "All apps" page with alphabetical `ListView` and letter group headers
- [ ] `SemanticZoom` for jumping between letter groups
- [ ] Back button to return to pinned view
- [ ] Folder expansion for sub-menus (e.g., "Windows Accessories")

### 1.6 Start Menu Animations
- [ ] Open animation: scale-up + fade-in using `ConnectedAnimation` or composition
- [ ] Close animation: scale-down + fade-out
- [ ] Page transition animation between Pinned view and All Apps view
- [ ] Item entrance animations on first show (staggered `EntranceThemeTransition`)
- [ ] Hover/press states using built-in `PointerOver` / `Pressed` visual states
- [ ] Smooth scrolling with composition `InteractionTracker` if needed

### 1.7 Theming & Styling
- [ ] Apply Mica or Acrylic backdrop to the start menu window
- [ ] Read colors from JCFG theme config and apply via WinUI resource overrides
- [ ] Support light/dark mode switching based on `TASKBAR_THEMESTYLE()`
- [ ] Custom `ItemTemplate` data templates for pinned tiles and recommended items
- [ ] Match current font choices (`_title_font`, `_section_font`, etc.) via XAML `FontFamily`/`FontSize`

### 1.8 Legacy Cleanup
- [ ] Remove `StartMenuRoot` class and its GDI+ rendering code
- [ ] Remove `ModernStartMenuItem` struct (replaced by XAML data model)
- [ ] Remove `AnimateShow()` / `AnimateHide()` GDI+ animation code
- [ ] Remove the `_hwndSearchEdit` Win32 edit control (replaced by `AutoSuggestBox`)
- [ ] Keep `StartMenuHandler` command routing (`ShowRestartDialog`, `ShowSearchDialog`, etc.)
- [ ] Update `StartmenuHelper.cpp` Lua bindings to talk to the new WinUI 3 start menu

**Exit criteria:** Start menu opens with smooth animation, displays pinned apps and recommended items, search works, apps launch correctly. Single exe, no regressions in taskbar/tray.

---

## Phase 2 — Taskbar Animation Overhaul

Replace the GDI+ timer-based animation system with WinUI 3 composition animations while keeping the Win32 shell integration layer (shell hooks, `SetTaskmanWindow`, window enumeration).

### 2.1 Composition Visual Layer for Taskbar
- [ ] Create a `Windows.UI.Composition` `Compositor` attached to the taskbar HWND
- [ ] Create a `ContainerVisual` root that sits on top of the taskbar window
- [ ] Replace `FillRoundedRect()` GDI+ calls with `ShapeVisual` + `CompositionRoundedRectangleGeometry`
- [ ] Replace `EaseTowards()` tick-based animation with `CompositionAnimation` (spring or cubic bezier)
- [ ] Each `TaskBarEntry` gets its own `SpriteVisual` for icon + highlight background

### 2.2 Button Hover & Active Animations
- [ ] Hover highlight: `ScalarKeyFrameAnimation` on background visual opacity
- [ ] Active highlight: `ColorKeyFrameAnimation` transitioning from hover to active color
- [ ] Indicator bar width animation using `Vector2KeyFrameAnimation`
- [ ] Remove `_hover_progress` / `_active_progress` float fields — the compositor owns the state now
- [ ] Remove `WM_TIMER` animation tick handler
- [ ] Remove `GetTaskbarAnimationClockMilliseconds()` and `GetAnimationBlend()` — no longer needed

### 2.3 Icon Animations
- [ ] Icon appear: `SpringVector3NaturalMotionAnimation` scale from 0 -> 1
- [ ] Icon disappear: scale 1 -> 0 + fade out, then remove the visual
- [ ] Replace `_icon_animating_in` / `_icon_animating_out` booleans with composition animation completion events
- [ ] Window group count badge (if implemented) as an overlay `SpriteVisual`

### 2.4 Taskbar Layout Animation
- [ ] Animated reflow when buttons are added/removed (position `Vector3KeyFrameAnimation`)
- [ ] Smooth centering animation when layout changes (for centered mode)
- [ ] Drag-to-reorder with `InteractionTracker` (stretch goal)

### 2.5 Thumbnail Preview
- [ ] Replace GDI+ thumbnail window with a WinUI 3 `Popup` or lightweight window
- [ ] Use `DesktopWindowXamlSource` (XAML Island) or a separate WinUI 3 window
- [ ] Animate in/out with composition fade + translate
- [ ] Show live DWM thumbnail via `DwmRegisterThumbnail` composited into the visual tree

### 2.6 Legacy Animation Cleanup
- [ ] Remove `utility/taskbar_draw.h` animation functions (`EaseTowards`, `LerpInt`, `LerpAlpha`, `GetAnimationBlend`)
- [ ] Keep `utility/taskbar_draw.h` layout/sizing functions (`GetButtonSlotWidth`, `GetHighlightRadius`, etc.) — these still drive config
- [ ] Remove `WM_TIMER`-based render loop from `taskbar.cpp`
- [ ] Update JCFG `animation_duration` to map to composition animation duration instead of blend factor

**Exit criteria:** Taskbar buttons animate smoothly at compositor framerate. No jitter. Icon add/remove is fluid. All shell integration (window switching, grouping, pinning) works identically.

---

## Phase 3 — System Tray & Clock Modernization

### 3.1 Tray Notification Area
- [ ] Replace the tray icon area with composition visuals (or XAML Island)
- [ ] Animated icon insertion/removal
- [ ] Overflow flyout as a WinUI 3 popup window
- [ ] Tooltip rendering with WinUI 3 `ToolTip` control

### 3.2 Clock & Calendar Flyout
- [ ] Clock text rendered as a `TextBlock` in the composition tree
- [ ] Calendar flyout as a WinUI 3 window with `CalendarView` control
- [ ] Smooth open/close animation

### 3.3 Quick Settings Flyout
- [ ] Volume slider using WinUI 3 `Slider` control
- [ ] Brightness slider
- [ ] Wi-Fi toggle and network list
- [ ] Battery indicator (if applicable)
- [ ] Acrylic/Mica backdrop

**Exit criteria:** Tray area, clock, and quick settings match modern Windows 11 aesthetics with smooth animations.

---

## Phase 4 — Dialogs & Secondary UI

### 4.1 Settings Dialog
- [ ] Rewrite `dialogs/settings.cpp` as a WinUI 3 `Window` with `NavigationView`
- [ ] Settings categories: Appearance, Taskbar, Start Menu, Shell, About
- [ ] Live preview of theme changes
- [ ] Mica backdrop

### 4.2 About Dialog
- [ ] Rewrite `dialogs/about.cpp` as a WinUI 3 `ContentDialog`
- [ ] Version info, license, links

### 4.3 Run Dialog
- [ ] Modernize the Run dialog with `AutoSuggestBox` for command history
- [ ] WinUI 3 styled

### 4.4 Power Menu
- [ ] Replace `customization/powermenu.cpp` with a WinUI 3 `MenuFlyout`
- [ ] Shutdown, restart, sleep, sign out, lock

### 4.5 Context Menus
- [ ] Desktop right-click context menu as WinUI 3 `MenuFlyout`
- [ ] Taskbar right-click context menu as WinUI 3 `MenuFlyout`
- [ ] Consistent styling and animation across all context menus

**Exit criteria:** All dialogs and secondary UI are WinUI 3. Consistent look and feel across the entire shell.

---

## Phase 5 — Desktop & File Explorer

### 5.1 Desktop Shell
- [ ] Desktop icon rendering with composition visuals
- [ ] Animated icon selection and drag-drop
- [ ] Wallpaper management integration

### 5.2 File Explorer Integration
- [ ] Modernize the built-in file browser UI (if keeping it)
- [ ] Or delegate to PeaZip/external explorer and remove the built-in one

**Exit criteria:** Desktop experience is cohesive with the rest of the modernized shell.

---

## Phase 6 — Polish & Performance

### 6.1 Performance
- [ ] Profile startup time — WinUI 3 bootstrap adds latency; minimize it
- [ ] Lazy-initialize WinUI 3 (don't load XAML framework until first UI element is needed)
- [ ] Profile memory usage vs. the GDI+ baseline
- [ ] Ensure compositor animations don't block the UI thread
- [ ] Test on low-end hardware (2GB RAM, integrated GPU)

### 6.2 Accessibility
- [ ] Verify `AutomationPeer` integration for all custom controls
- [ ] Keyboard navigation: Tab, arrow keys, Enter, Escape through all menus
- [ ] High contrast theme support
- [ ] Screen reader announcements for state changes

### 6.3 DPI & Multi-Monitor
- [ ] Per-monitor DPI awareness with WinUI 3 (should be automatic, but verify)
- [ ] Test taskbar spanning across monitors with different DPI
- [ ] Verify start menu positions correctly on each monitor

### 6.4 Stability
- [ ] Stress test: rapid start menu open/close
- [ ] Stress test: rapid window creation/destruction (taskbar button churn)
- [ ] Handle WinUI 3 thread apartment issues (STA/MTA)
- [ ] Crash recovery: if WinUI 3 subsystem fails, fall back to Win32 rendering

### 6.5 Lua API Updates
- [ ] Update `TaskbarHelper.cpp` Lua bindings for any changed APIs
- [ ] Update `StartmenuHelper.cpp` Lua bindings for the new WinUI 3 start menu
- [ ] Update `DialogHelper.cpp` for new dialog creation
- [ ] Document new Lua API surface changes for theme/extension authors

**Exit criteria:** Ship-ready. Smooth, stable, accessible, and performant on Windows 10 1809+.

---

## Phase 7 — Final Migration & GDI+ Removal

### 7.1 Remove GDI+ Dependency
- [ ] Audit all remaining `#include <gdiplus.h>` usage
- [ ] Replace any remaining GDI+ drawing with composition visuals or Direct2D
- [ ] Remove `gdiplus.lib` from linker dependencies
- [ ] Remove GDI+ initialization/shutdown from `WinMain`

### 7.2 Remove Legacy Win32 UI Code
- [ ] Remove old `StartMenu` / `StartMenuRoot` classes (if not done in Phase 1)
- [ ] Remove `OwnerDrawParent` / `OwnerdrawnButton` classes if no longer used
- [ ] Remove `DrawStartMenuButton()` and related GDI drawing functions
- [ ] Remove legacy `_LIGHT_STARTMENU` / non-light code paths
- [ ] Clean up `precomp.h` — remove unused Win32 control headers

### 7.3 Remove `#define USE_WINUI3` Toggle
- [ ] Once stable, make WinUI 3 the only path — remove the fallback `#ifdef` guards
- [ ] Final cleanup pass on dead code

**Exit criteria:** No GDI+ dependency. Clean codebase. Single rendering path via WinUI 3 composition + XAML.

---

## Architecture Reference

```
┌─────────────────────────────────────────────────┐
│                  Explauncher.exe                 │
│              (unpackaged, single exe)            │
├─────────────────────────────────────────────────┤
│  WinUI 3 Layer          │  Win32 Layer          │
│  ─────────────          │  ───────────          │
│  Start Menu (XAML)      │  Shell Hooks          │
│  Settings Dialog        │  SetTaskmanWindow     │
│  Quick Settings         │  Window Enumeration   │
│  Thumbnail Preview      │  RegisterShellHook    │
│  Context Menus          │  Tray Icon Protocol   │
│  Calendar Flyout        │  Message Loop         │
├─────────────────────────┤                       │
│  Composition Animations │  Taskbar HWND Host    │
│  (Taskbar visuals)      │  (Win32 window)       │
├─────────────────────────┴───────────────────────┤
│  Shared Services                                │
│  ──────────────                                 │
│  JCFG Config  │  Lua Engine  │  Shell Browser   │
│  Theme System │  Icon Cache  │  File Type Mgr   │
└─────────────────────────────────────────────────┘
```

## Deployment Model

```
Explauncher/
├── Explauncher.exe          ← single exe (links WinAppSDK statically or loads DLLs)
├── Theme/
│   ├── WinXShell.jcfg
│   ├── WinXShell.lua
│   └── ...
├── Explorer/                ← PeaZip file manager (optional)
└── Wallpapers/              ← wallpapers (optional)
```

The Windows App SDK runtime (`Microsoft.WindowsAppRuntime.*.dll`) must be present on the system. Options:
1. **Runtime installed system-wide** — user installs the redistributable once (smallest exe size)
2. **DLLs shipped alongside** — place WinAppSDK DLLs next to `Explauncher.exe` (self-contained, larger)
3. **Auto-installer** — `MddBootstrapInitialize` with auto-download flag (requires internet on first run)

Option 2 is recommended for a shell replacement since it must work reliably at login before any other software runs.
