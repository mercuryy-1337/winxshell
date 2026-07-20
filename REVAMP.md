# WinXShell taskbar revamp

> Status: implementation not started; existing foundations are marked complete below.
>
> Last updated: 2026-07-19
>
> Scope: replace the current Windows-10-like taskbar with a Windows-11-style WinUI 3 taskbar. The Start menu itself is explicitly deferred.

## How to use this file

- Keep every work item checkable.
- Mark an item complete only after its code, tests, and relevant documentation have landed.
- Do not mark a phase gate complete until every required acceptance check under that gate passes.
- Add the pull request or commit beside completed phase gates.
- If an implementation decision changes, update the architecture and acceptance criteria here before merging the change.

## End state

The revamp is complete when all of the following are true:

- [ ] The visible taskbar is rendered with WinUI 3 controls hosted by the WinXShell shell window.
- [ ] One continuous Desktop Acrylic backdrop covers the entire taskbar in normal operation.
- [ ] Transparency-disabled, high-contrast, unsupported-OS, and failed-runtime cases have intentional solid-color or legacy fallbacks.
- [ ] The Start button and all pinned/running app buttons form one icon-only group.
- [ ] The user can set that group to true left or center alignment and the choice persists.
- [ ] Changing alignment produces a smooth, slightly bouncy Composition spring animation without a one-frame snap.
- [ ] The Start button uses a crisp Windows-11-style four-pane glyph and still invokes the existing Start action.
- [ ] Pinned and running apps are merged and grouped by stable app identity instead of being separate Quick Launch and task toolbars.
- [ ] Task buttons use the highest-resolution shell/app icon available at the current monitor DPI.
- [ ] The right-side notification area supports real application notification icons, an overflow flyout, system status, input language, clock/date, notifications, and Show Desktop behavior.
- [ ] Notification icons remain compatible with `Shell_NotifyIcon` add, modify, delete, version, state, tooltip, mouse, keyboard, and icon-rectangle behavior.
- [ ] The taskbar behaves correctly across supported DPI scales, themes, monitors, full-screen apps, sleep/resume, and shell restart.
- [ ] Accessibility, performance, deployment, and fallback gates pass.

## Terminology

- **App icon strip**: Start plus pinned/running application buttons.
- **Notification area**: application status icons historically called the system tray.
- **System status cluster**: network, volume, battery/power, input language, clock/date, notification bell, and Show Desktop.
- **Shell bridge**: Win32 code that owns `Shell_TrayWnd`, receives shell messages, reserves work area, and performs HWND operations.
- **Taskbar surface**: the WinUI 3/XAML visual tree hosted inside the shell bridge.

## Scope boundaries

### In scope

- Bottom taskbar visual and interaction rewrite.
- WinUI 3 hosting inside the existing C++ Win32 shell process.
- Start button visual, input, accessibility, and command forwarding.
- Pinned/running application model, grouping, launch/activation, indicators, tooltips, previews, and context actions.
- Left/center alignment setting and animation.
- Whole-taskbar Acrylic and theme/transparency fallbacks.
- Application notification icons and overflow.
- Windows-11-style system status cluster and taskbar-owned flyouts.
- Per-monitor DPI correctness and multi-monitor behavior.
- Framework-dependent Windows App SDK deployment with an intentional fallback.

### Out of scope

- Rebuilding the Start menu contents. The existing Start action remains the target until a later project.
- Search, Widgets, Copilot, Chat, or other optional Windows taskbar surfaces.
- Top or side taskbar docking. The Windows 11 target is bottom-aligned.
- Rewriting the desktop, File Explorer, Lua engine, or unrelated dialogs.
- Calling private `ShellExperienceHost` entry points or simulating hotkeys to open Microsoft's private flyouts.
- Pixel-for-pixel duplication of Microsoft-owned assets. Use a project-owned vector four-pane Start glyph and Fluent system icons.
- Inventing detail that a third-party app did not supply in its notification `HICON`.

## Non-negotiable constraints

- Keep the legacy Win32 message loop and `Shell_TrayWnd` compatibility entry point.
- Keep C++/WinRT headers inside `winui/` and other explicitly isolated implementation units; do not spread them through the legacy shell.
- Preserve the current portable/unpackaged deployment model.
- Keep the shipped WinXShell surface under the existing 10 MB budget. The separately installed Windows App SDK runtime is not counted, but every bundled bootstrap binary and asset is.
- Do not use the current undocumented `SetWindowCompositionAttribute` acrylic path for the new surface.
- Never perform shell icon extraction, shortcut resolution, package lookup, or network enumeration synchronously on the XAML UI thread.
- Never run the legacy and WinUI taskbar renderers at the same time.
- Start menu work must not be pulled into this revamp to make the Start button look finished.

## Current-state audit

| Area | Current implementation | Gap to close |
| --- | --- | --- |
| Shell window | [`taskbar/desktopbar.cpp`](taskbar/desktopbar.cpp) creates a primary-screen-only `Shell_TrayWnd` at a configured 40 px-style height. | Separate shell responsibilities from rendering; use monitor/DPI-aware geometry and a WinUI child surface. |
| Running apps | [`taskbar/taskbar.cpp`](taskbar/taskbar.cpp) enumerates HWNDs into a common-controls toolbar. | Replace bitmap toolbar buttons with a grouped WinUI app model and icon-only controls. |
| Pinned apps | [`taskbar/quicklaunch.cpp`](taskbar/quicklaunch.cpp) reads the legacy pinned shortcut folder into a separate toolbar. | Merge pinned and running identities into one ordered app icon strip. |
| Task icons | Window `HICON`s are flattened into background-colored `HBITMAP`s at one global size. | Preserve alpha and decode the best available source at device resolution on a worker thread. |
| Notification icons | [`taskbar/traynotify.cpp`](taskbar/traynotify.cpp) receives `NIM_*` data but copies every icon to `NOTIFYICON_SIZE`, currently 16 px at the global DPI. | Preserve the source `HICON`, render per monitor, add real overflow UI, keyboard access, and correct geometry replies. |
| Tray updates | GDI paint and one-second polling maintain icons and clock. | Make icon/status changes event-driven and align clock refresh to actual minute/date changes. |
| Acrylic | [`luaengine/TaskbarHelper.cpp`](luaengine/TaskbarHelper.cpp) uses `SetWindowCompositionAttribute`. | Use the supported WinUI/Windows App SDK Desktop Acrylic backdrop with policy fallbacks. |
| DPI | [`WinXShell.exe.manifest`](WinXShell.exe.manifest) has no DPI declaration; JCFG stores one global screen DPI. | Declare Per-Monitor V2 and handle monitor-specific rasterization and `WM_DPICHANGED`. |
| WinUI | [`winui/WinUIHost.cpp`](winui/WinUIHost.cpp) contains a presence probe and lifecycle stubs only. | Bootstrap a pinned Windows App SDK runtime, initialize XAML, host an island, and shut it down safely. |
| Theme | [`utility/system_theme.cpp`](utility/system_theme.cpp) follows the system light/dark registry setting. | Feed that state into XAML theme resources and react to transparency, contrast, accent, and text-scale changes. |

## Target architecture

```text
Applications / Shell32
        |
        | Shell_NotifyIcon, shell hooks, WM_COPYDATA, appbar queries
        v
+--------------------------- Win32 shell boundary ---------------------------+
| TaskbarShellBridge / Shell_TrayWnd                                          |
| - owns monitor windows, work-area reservation, z-order and fullscreen hide |
| - validates compatibility messages                                          |
| - performs HWND activation/minimize/system-menu operations                   |
+-----------------------------+-----------------------------------------------+
                              |
                              | typed commands + immutable model deltas
                              v
+--------------------------- taskbar services -------------------------------+
| TaskItemRepository | NotificationIconRepository | SystemStatusService       |
| IconLoader         | TaskbarSettingsRepository  | FlyoutCoordinator         |
+-----------------------------+-----------------------------------------------+
                              |
                              | dispatch to the XAML UI thread
                              v
+-------------------------- WinUI 3 taskbar surface -------------------------+
| DesktopWindowXamlSource                                                     |
| Desktop Acrylic root                                                        |
| AppIconStrip       spacer/overlay layout       Notification/System cluster |
| Overflow, Quick Settings, Calendar, Notification flyouts                    |
+-----------------------------------------------------------------------------+
```

### Boundary rules

- The Win32 bridge owns raw HWND operations and compatibility message parsing.
- Repositories own durable identity, ordering, icon lifetime, and state transitions.
- XAML controls render view state and emit user intent; they do not enumerate processes or parse `WM_COPYDATA`.
- Cross-boundary calls use stable IDs and value objects. A XAML control must not retain a borrowed `HICON`, process handle, or pointer from another process.
- Shell callbacks are never made while a repository or XAML collection lock is held.

### Threading and lifetime

- The existing main shell thread remains STA and owns the Win32 taskbar HWND, DispatcherQueue, XAML manager, and XAML island.
- A bounded MTA worker handles shortcut resolution, package metadata, icon extraction, and other blocking shell work.
- Worker results are cancellation-aware and posted to the UI DispatcherQueue.
- Shutdown order is mandatory: stop producers -> close flyouts -> clear XAML content -> close the XAML island -> close XAML manager/application state -> drain/release dispatcher -> shut down Windows App SDK bootstrap -> destroy remaining shell resources.

## Target visual and interaction specification

These are starting design tokens. Compare them against a native Windows 11 reference capture before locking them.

| Token | Initial target |
| --- | --- |
| Taskbar height | 48 effective pixels at 100% scale |
| App button hit target | 40 x 40 effective pixels |
| Start/app icon | 24 x 24 effective pixels, device-resolution raster |
| Notification icon | 16–20 effective pixels inside a 32 x 40 hit target |
| App button spacing | 4 effective pixels |
| Hover/pressed corner radius | 4 effective pixels |
| Active indicator | Accent-colored rounded 3 px bar centered below the icon |
| Attention indicator | Theme-aware pulse/flash that respects reduced motion |
| Outer horizontal safe inset | 12 effective pixels |
| App-strip-to-tray minimum gap | 8 effective pixels |

### Whole-taskbar material

- Use one `DesktopAcrylicBackdrop`/supported system backdrop across the XAML host, not separate translucent rectangles behind each cluster.
- Keep child panels transparent so the backdrop remains visually continuous.
- Follow system light/dark, accent, transparency effects, high contrast, and text scale live.
- Fall back to an opaque theme brush when Acrylic is unsupported or transparency is disabled.
- Avoid sampling or blurring the wallpaper manually.
- Do not repaint a solid GDI background over the island.

### App icon strip

- Start is always the first logical item.
- Pinned items retain their user order.
- A running pinned app occupies its pinned slot and gains running/active state; it is not duplicated.
- An unpinned running app appears after pinned apps.
- Multiple windows with the same stable app identity group into one button.
- Buttons show icons only. Names and window counts are exposed through tooltips and accessibility properties.
- The group is true-centered against the monitor bounds when center aligned, then clamped to avoid the notification area.
- Left alignment starts at the safe inset; it does not reserve an obsolete text label or Quick Launch separator.
- When the group grows too wide, preserve Start and the active app, then introduce a task overflow control rather than drawing under the tray.

### Alignment animation

- Persist the final alignment before beginning the visual animation.
- Use a FLIP-style move: measure the old screen X, commit the final layout, apply the inverse translation, then spring `Translation.X` to zero.
- Use a `SpringVector3NaturalMotionAnimation` with a subtle default near the Windows design guidance (`DampingRatio` about `0.8`, `Period` about `50 ms`); tune against video captures.
- Retarget the running spring from its current value when the user toggles alignment repeatedly.
- Animate the group as one unit so relative app-icon spacing never changes during the move.
- Finish without overlap, clipping, a blank intermediate frame, or a final pixel correction.
- If system animations are disabled, apply the final alignment immediately.

### Start button

- Use a project-owned vector `PathIcon`/SVG-style asset with four equal panes so it remains crisp at every DPI.
- Apply theme/accent resources instead of shipping separate low-resolution light/dark bitmaps.
- Preserve the current single-click, Windows-key, and existing command routing.
- Do not instantiate or redesign a new Start menu in this project.

### Notification area and system status

| Surface | Required behavior |
| --- | --- |
| Visible app icons | Icon-only, ordered, alpha-correct, DPI-aware, hover/pressed state, tooltip, mouse/keyboard callbacks. |
| Overflow button | Chevron opens a light-dismiss Acrylic flyout containing hidden application icons; no text labels in the normal grid. |
| Icon policy | Support always show, always hide, and automatic/overflow state through one settings repository. |
| Network | Vector connectivity glyph and accessible status; opens a WinUI Quick Settings surface or a documented Settings fallback. |
| Volume | Vector volume/mute glyph; opens a working volume slider and mute control backed by the existing audio service. |
| Battery/power | Vector charging/battery state and percentage tooltip when a battery exists; omitted on batteryless systems. |
| Input language | Locale abbreviation remains text because the native signal is textual; updates on input-language changes. |
| Clock/date | Locale-aware two-line time/date; click opens the calendar flyout; no one-second repaint when seconds are hidden. |
| Notifications | Bell/quiet-state surface backed by taskbar-owned notification state; do not pretend to read private Windows history. |
| Show Desktop | Narrow far-right target with pointer feedback and the current Win+D behavior. |

## Implementation phases

## Phase 0 — Baseline, decisions, and guardrails

### Existing verified foundations

- [x] A thin C facade exists in [`winui/WinUIHost.h`](winui/WinUIHost.h).
- [x] A basic Windows App SDK runtime-presence probe exists.
- [x] The shell already owns the `Shell_TrayWnd`, `TrayNotifyWnd`, and task-switch window class names required by the current compatibility path.
- [x] The repository has a release-size guard in [`scripts/check_size.ps1`](scripts/check_size.ps1).
- [x] System light/dark detection and live `ImmersiveColorSet` handling exist.
- [x] The legacy notification receiver already models `NIM_ADD`, `NIM_MODIFY`, `NIM_DELETE`, `NIM_SETVERSION`, GUID identity, hidden state, and callbacks.

### Work

- [ ] Capture baseline screenshots and a 60 fps interaction recording at 100%, 125%, 150%, and 200% scale.
- [ ] Record baseline startup time, idle CPU, working set, taskbar zip size, and alignment/layout behavior on the reference VM.
- [ ] Add an architecture decision record for one `DesktopWindowXamlSource` hosted by each taskbar shell HWND.
- [ ] Add `JS_TASKBAR.renderer = "auto" | "winui3" | "legacy"`; default to `auto`.
- [ ] Define support policy: Windows 11 is the visual-parity target; Windows 10/WinPE may use the legacy or solid fallback.
- [ ] Replace the obsolete ARM32 expectation with an ARM64 plan. Windows App SDK runtime distributions are x86, x64, and ARM64; ARM32 remains legacy-only if retained.
- [ ] Decide the exact Windows App SDK Stable release at implementation time and pin it; never use a floating package version.
- [ ] Document where per-user taskbar state is stored and its schema/versioning.
- [ ] Add a safe rollback rule: a failed WinUI bootstrap before the surface is shown selects legacy once and does not retry in a crash loop.
- [ ] Build a small test sender that can exercise shell hooks and all supported notification-icon messages.

### Phase 0 gate

- [ ] Baseline evidence is checked in or linked.
- [ ] Supported OS/architecture/deployment choices are documented.
- [ ] The current legacy shell still builds in Debug and Release for retained platforms.
- [ ] The release size remains below 10 MB.

## Phase 1 — Real WinUI 3 host

### Build and deployment

- [ ] Add an exact `Microsoft.WindowsAppSDK` package reference to [`WinXShell.vcxproj`](WinXShell.vcxproj).
- [ ] Configure the existing executable as unpackaged.
- [ ] Disable automatic bootstrap if explicit initialization is used so startup failures can select the legacy renderer.
- [ ] Call `MddBootstrapInitialize`/the supported bootstrap API before any Windows App SDK or WinUI API.
- [ ] Replace `LoadLibraryEx` presence-only probing with a versioned initialization result and actionable log message.
- [ ] Pin and restore dependencies reproducibly.
- [ ] Ensure Windows App SDK runtime installation is documented for x86, x64, and ARM64 builds.
- [ ] Keep bundled bootstrap/runtime files inside the release-size accounting rules.

### Host lifecycle

- [ ] Initialize COM/WinRT in the correct apartment on the shell UI thread.
- [ ] Create and own a `DispatcherQueueController` that cooperates with the current message pump.
- [ ] Initialize `WindowsXamlManager`/required application state exactly once on that thread.
- [ ] Create a `DesktopWindowXamlSource` and attach it to the existing `Shell_TrayWnd`.
- [ ] Resize the island child HWND to the full client rect on every host resize and DPI change.
- [ ] Route XAML initialization failures to one logged legacy fallback.
- [ ] Add explicit `WinUIHost_Initialize`, taskbar-surface create/destroy, and `WinUIHost_Shutdown` states.
- [ ] Make every lifecycle call idempotent.
- [ ] Implement and document the mandatory shutdown order.
- [ ] Disable Browse Information for C++/WinRT-heavy translation units to avoid the known BSCMAKE overflow.
- [ ] Keep C++/WinRT includes confined to the new WinUI implementation boundary.

### DPI foundation

- [ ] Declare `PerMonitorV2` in [`WinXShell.exe.manifest`](WinXShell.exe.manifest), retaining an older-system fallback declaration where needed.
- [ ] Handle `WM_DPICHANGED` on every top-level taskbar/flyout HWND.
- [ ] Stop treating the primary display's `LOGPIXELSX/Y` values as global taskbar DPI.
- [ ] Verify XAML rasterization scale matches the monitor containing each host.

### Phase 1 gate

- [ ] A blank WinUI 3 element renders inside `Shell_TrayWnd` without a second top-level taskbar window.
- [ ] Pointer, keyboard, DispatcherQueue, and Win32 messages remain responsive.
- [ ] Create/destroy succeeds 100 consecutive times in a lifecycle test without a crash, hang, or growing island count.
- [ ] Runtime-missing and forced-failure tests select the legacy renderer once.
- [ ] Debug/Release x86 and x64 builds pass; ARM64 status is documented.
- [ ] The release size remains below 10 MB.

## Phase 2 — Shell host, layout root, and Acrylic

### Shell bridge

- [ ] Extract shell-only responsibilities from `DesktopBar` into a `TaskbarShellBridge`.
- [ ] Keep the primary compatibility window named `Shell_TrayWnd`.
- [ ] Create secondary-monitor hosts with an intentional class/ownership strategy.
- [ ] Preserve work-area reservation and `ABM_GETTASKBARPOS` compatibility without using physical-primary-screen assumptions.
- [ ] Preserve always-on-top/no-Alt-Tab behavior without stealing foreground activation from apps.
- [ ] Preserve full-screen hide/show behavior and remove the current one-second foreground polling where event-driven signals are available.
- [ ] Recompute host placement on display, work-area, orientation, DPI, and taskbar-height changes.
- [ ] Broadcast `TaskbarCreated` exactly once after the selected renderer and notification receiver are ready.

### XAML root

- [ ] Add a dedicated `winui/taskbar/` surface rather than putting XAML code in `desktopbar.cpp`.
- [ ] Create one root layout with independent app-strip and right-cluster overlays.
- [ ] Ensure center alignment is based on monitor coordinates, not on the leftover width beside the tray.
- [ ] Add design tokens for height, insets, button size, icon size, spacing, radii, and indicators.
- [ ] Apply one supported Desktop Acrylic backdrop to the full XAML host.
- [ ] Make all child taskbar regions transparent over the common backdrop.
- [ ] Add solid light, dark, and high-contrast fallback resources.
- [ ] React live to theme, accent, transparency effects, high contrast, and text scale.
- [ ] Remove GDI background painting from the WinUI renderer path.
- [ ] Keep the legacy renderer available behind the renderer selection until the deployment decision permits removal.

### Phase 2 gate

- [ ] The entire taskbar shows a continuous Acrylic material with no opaque seams.
- [ ] Transparency off and high contrast produce readable, intentional solid backgrounds.
- [ ] Theme/transparency changes do not require shell restart.
- [ ] The taskbar remains correctly placed after resolution, orientation, DPI, and primary-monitor changes.
- [ ] Full-screen enter/exit does not leave the taskbar hidden, topmost above exclusive content, or stranded off-screen.

## Phase 3 — Unified pinned/running app model and high-resolution icons

### Task identity and state

- [ ] Introduce a `TaskItem` value model with stable ID, pinned/running state, order, window list, display name, launch target, icon key, active state, and attention state.
- [ ] Resolve identity in this order: explicit window AppUserModelID, shortcut/package AppUserModelID, canonical executable/relaunch identity, then HWND fallback.
- [ ] Read AppUserModelID and relaunch properties through documented window/shortcut property stores.
- [ ] Stop excluding all `ApplicationFrameWindow`/modern app windows by class-name substring.
- [ ] Define a documented eligibility filter for owned windows, tool windows, cloaked windows, empty hosts, and explicit taskbar exclusion properties.
- [ ] Import pinned shortcuts from the existing user-pinned taskbar folder.
- [ ] Store custom order and user state in a versioned per-user taskbar state file; write through temp-file-plus-rename to avoid corruption.
- [ ] Reconcile pinned links and the state file when either changes outside WinXShell.
- [ ] Merge a running app into its pinned item.
- [ ] Group multiple windows by stable app identity and maintain most-recently-used order inside the group.
- [ ] Subscribe to shell create/destroy/activate/redraw/flash events.
- [ ] Keep a slow reconciliation pass only as recovery from missed events; do not poll at 200 ms.

### Commands and native behavior

- [ ] Launch a pinned inactive app from its resolved shortcut/launch target.
- [ ] Activate or restore a single running window.
- [ ] Minimize an already active window on click where native behavior does.
- [ ] For grouped windows, define click cycling and hover-preview selection behavior.
- [ ] Preserve right-click system menu initially, then add documented Jump List support as a separate item.
- [ ] Support middle-click new-instance behavior where a relaunch command exists.
- [ ] Reflect active, running, minimized, and attention/flash state without labels.
- [ ] Expose display name, window count, and state to tooltip and UI Automation.

### Icon pipeline

- [ ] Add an `IconLoader` with a cache key that includes app identity, source version, theme variant where required, and target device pixels.
- [ ] Resolve pinned/package icons from the shell item/shortcut at requested device size using documented Shell image APIs.
- [ ] Use a window's large icon only as a fallback after stable app/shortcut imagery.
- [ ] Extract and decode icons off the UI thread.
- [ ] Preserve alpha and premultiply correctly when converting to a WinUI image source.
- [ ] Avoid painting icons against the taskbar background before XAML composition.
- [ ] Invalidate cache entries after shortcut, package, executable, theme, or DPI changes.
- [ ] Use a deterministic generic app glyph while asynchronous extraction is pending or fails.
- [ ] Verify raster sources at 100%, 125%, 150%, 175%, 200%, and mixed-DPI monitor transitions.

### WinUI app strip

- [ ] Implement the Start button as the first item with a vector Windows-11-style four-pane glyph.
- [ ] Forward Start click and Windows-key activation to the existing Start action only.
- [ ] Render pinned/running items with an `ItemsRepeater` or equivalent virtualized layout.
- [ ] Use icon-only buttons with native-feeling hover, pressed, focus, running, active, and attention visuals.
- [ ] Add delayed tooltips and hover previews without activating the taskbar.
- [ ] Add task overflow when the icon strip cannot fit before the notification area.
- [ ] Remove the WinUI path's separate Quick Launch and running-app toolbars.

### Phase 3 gate

- [ ] Pinned and running states merge correctly for classic, packaged, multi-process, and multi-window test apps.
- [ ] Launch, activate, restore, minimize, close/context, middle-click, and flash cases pass.
- [ ] App buttons remain icon-only and expose accessible names.
- [ ] App icons are sharp and alpha-correct at every required DPI.
- [ ] Start invokes the existing menu/action and no new Start menu UI has been introduced.
- [ ] Adding/removing/activating apps does not block the UI thread.

## Phase 4 — User alignment and spring motion

### Settings

- [ ] Add `alignment = "left" | "center"` to `TaskbarSettingsRepository`.
- [ ] Use `JS_TASKBAR.alignment` as the deployer/default value when no user choice exists.
- [ ] Persist the user's choice in the per-user state store rather than rewriting the installed JCFG file.
- [ ] Add `Taskbar alignment > Left / Center` to the taskbar context menu or WinXShell taskbar settings surface.
- [ ] Validate invalid/missing values and default to center for the Windows 11 experience.
- [ ] Apply the setting consistently to every monitor host.
- [ ] Publish a single observable settings-change event; views must not poll.

### Layout

- [ ] Implement true monitor-centered placement independent of the notification area's width.
- [ ] Clamp centered placement between the safe left edge and the notification area.
- [ ] Implement left placement at the safe inset.
- [ ] Recalculate placement after app-count, overflow, tray-width, monitor, DPI, orientation, or taskbar-size changes.
- [ ] Keep Start and app items in one moving container.

### Motion

- [ ] Capture the old app-strip screen position before changing layout.
- [ ] Commit final alignment and apply the inverse translation in the same render transaction.
- [ ] Animate `Translation.X` to zero with a Composition spring.
- [ ] Tune damping/period against native Windows 11 and record the final values here.
- [ ] Retarget a running animation without snapping when the user toggles rapidly.
- [ ] Cancel/recompute safely when the monitor, DPI, tray width, or app count changes mid-animation.
- [ ] Disable the spring when `UISettings.AnimationsEnabled` is false.
- [ ] Keep pointer hit testing aligned with the moving visual for the entire animation.

### Phase 4 gate

- [ ] Left and center choices persist across shell restart and sign-in.
- [ ] Center means the app-strip midpoint matches the monitor midpoint unless collision clamping is required.
- [ ] Twenty rapid alternating changes finish at the last requested alignment with no jump, overlap, stale hit target, or lost input.
- [ ] The animation looks smooth at 60 Hz and has a subtle visible settle rather than an exaggerated wobble.
- [ ] Reduced-motion mode changes alignment immediately.
- [ ] Alignment works at all tested DPI scales and monitor layouts.

## Phase 5 — Notification compatibility bridge and model

The app-facing `Shell_NotifyIcon` API is documented, but a replacement shell's receiving wire protocol is not a stable public API. Keep the current compatibility knowledge isolated, validated, and covered by tests.

### Receiver isolation and safety

- [ ] Move cross-bitness `NOTIFYICONDATA` decoding out of `traynotify.cpp` painting code into `ShellNotifyAdapter`.
- [ ] Validate `COPYDATASTRUCT.cbData`, embedded `cbSize`, character encoding, architecture layout, flags, handles, IDs, GUIDs, and string termination before use.
- [ ] Reject malformed payloads without reading beyond the message buffer.
- [ ] Treat GUID identity as authoritative when `NIF_GUID` is present; otherwise use HWND plus ID.
- [ ] Copy supplied icons immediately because the sender may destroy them after `Shell_NotifyIcon` returns.
- [ ] Use `CopyIcon`/source-preserving ownership rather than immediately resizing every icon to 16 px.
- [ ] Destroy every owned icon exactly once on replace, delete, dead-owner cleanup, renderer switch, and shutdown.
- [ ] Remove stale icons when the callback HWND dies without iterating unsafe borrowed state.

### Model semantics

- [ ] Add a `NotificationIconItem` model with identity, callback HWND/message, negotiated version, tooltip, original icon, state, visibility policy, order, and last activity.
- [ ] Implement and test `NIM_ADD`, `NIM_MODIFY`, `NIM_DELETE`, `NIM_SETFOCUS`, and `NIM_SETVERSION`.
- [ ] Implement `NIF_MESSAGE`, `NIF_ICON`, `NIF_TIP`, `NIF_STATE`, `NIF_INFO`, `NIF_GUID`, `NIF_REALTIME`, and `NIF_SHOWTIP` behavior used by supported senders.
- [ ] Preserve stable display order across icon modification.
- [ ] Map sender hidden state and user visible/overflow policy without conflating them.
- [ ] Replace the one-second icon repaint/poll with model-change notifications plus a low-frequency dead-owner reconciliation.
- [ ] Queue model deltas to the XAML thread without blocking the shell sender.

### Callback and geometry compatibility

- [ ] Forward left, right, middle, double-click, hover/open/close, context, selection, and keyboard-selection callbacks.
- [ ] Encode callback parameters correctly for legacy versions and `NOTIFYICON_VERSION_4`.
- [ ] Call `AllowSetForegroundWindow` for the icon owner before context activation where required.
- [ ] Track the actual screen rectangle of visible and overflow icon buttons.
- [ ] Return the real icon rectangle/anchor for `Shell_NotifyIconGetRect` compatibility instead of the current cursor-position approximation.
- [ ] Send appropriate balloon lifecycle callbacks when legacy `NIF_INFO` notifications are displayed or dismissed.
- [ ] Re-register WinXShell-owned status icons after `TaskbarCreated`.

### Test harness

- [ ] Test ANSI and Unicode payloads.
- [ ] Test x86 sender to x64 shell and same-bitness combinations.
- [ ] Test HWND/ID and GUID identity.
- [ ] Test rapid add/modify/delete, icon replacement, dead owner, hidden state, set-version, tooltips, callbacks, and balloons.
- [ ] Fuzz valid-size boundaries and malformed `cbSize`/flags in a non-production test.

### Phase 5 gate

- [ ] All notification harness cases pass without leaks, invalid reads, duplicate icons, or stuck entries.
- [ ] Callback traces match documented version semantics.
- [ ] Icon rectangles point to the visible button or its overflow representation.
- [ ] Existing real-world tray apps reappear after taskbar restart and remain interactive.

## Phase 6 — WinUI notification area and system status cluster

### Application notification icons

- [ ] Render visible notification icons as WinUI icon-only buttons.
- [ ] Decode the preserved source icon at the current monitor's device pixels with correct alpha.
- [ ] If the sender supplied only a low-resolution icon, render it cleanly but do not claim synthetic high-resolution detail.
- [ ] Add native-feeling hover, pressed, focus, and attention states.
- [ ] Show standard tooltips only when sender/version flags request them.
- [ ] Add drag/reorder only after callback hit testing remains correct throughout the gesture.

### Overflow

- [ ] Add a chevron button that reflects open/closed state.
- [ ] Build a light-dismiss Acrylic overflow flyout with a wrapping icon grid.
- [ ] Keep normal overflow cells icon-only; expose names via tooltip and automation.
- [ ] Anchor the flyout to the chevron and keep it inside the owning monitor work area.
- [ ] Move always-hidden, auto-hidden, and user-hidden items into overflow according to model policy.
- [ ] Close or reposition the flyout safely when icons, DPI, taskbar geometry, or monitor topology change.

### System status

- [ ] Add vector network, volume, battery/power, and notification glyphs.
- [ ] Update network glyph/tooltip from documented connectivity events.
- [ ] Update volume/mute glyph/tooltip from endpoint-volume callbacks.
- [ ] Update power glyph/tooltip from power setting notifications; hide it on batteryless devices.
- [ ] Update input-language abbreviation on `WM_INPUTLANGCHANGE` and related session changes.
- [ ] Render locale-aware time and short date in the native two-line style.
- [ ] Schedule clock updates at the next minute/day/time-zone boundary rather than repainting every second.
- [ ] Add the far-right Show Desktop target and preserve current Win+D behavior.
- [ ] Keep system-status glyphs vector/theme-based so they remain crisp at every DPI.

### Accessibility and input

- [ ] Define logical tab/arrow navigation order from app strip through tray.
- [ ] Support Space/Enter activation and Shift+F10/context-menu keys.
- [ ] Provide accessible names, states, values, and live updates.
- [ ] Keep all targets usable with mouse, touch, pen, and keyboard.
- [ ] Preserve pointer target size even when the visible glyph is small.

### Phase 6 gate

- [ ] Visible and overflow notification icons work with mouse and keyboard.
- [ ] System status reflects real network, audio, battery, language, and time changes.
- [ ] App icons remain icon-only; clock/language text is limited to native status information.
- [ ] No tray item is clipped, blurry from premature 16 px conversion, or drawn over the app strip.
- [ ] Narrator announces every interactive tray element meaningfully.

## Phase 7 — Taskbar flyouts

### Shared flyout infrastructure

- [ ] Create one `FlyoutCoordinator` so only compatible taskbar flyouts are open at once.
- [ ] Use separate owned WinUI windows or supported popup surfaces with Acrylic, rounded corners, shadow, and light dismiss.
- [ ] Anchor flyouts to their actual taskbar button and keep them on the correct monitor.
- [ ] Handle taskbar hide, display/DPI changes, lock, session switch, and shell shutdown.
- [ ] Restore focus appropriately without activating unrelated apps.

### Quick Settings

- [ ] Build a combined network/volume/power flyout that uses only documented APIs.
- [ ] Reuse or refactor [`systemsettings/Volume.cpp`](systemsettings/Volume.cpp) for a live master-volume slider and mute toggle.
- [ ] Show current network connectivity and available public actions.
- [ ] Use documented WLAN/Network List APIs for Wi-Fi enumeration/toggle only if the target environment supports them reliably.
- [ ] Provide a visible link to the appropriate Settings page for features not safely controllable in-process.
- [ ] Show battery percentage, charging state, and a documented power/settings action where applicable.
- [ ] React live while the flyout is open.

### Clock and calendar

- [ ] Build a locale-aware calendar flyout owned by the taskbar.
- [ ] Highlight today and respond to date, locale, time-zone, and first-day-of-week changes.
- [ ] Provide a documented date/time settings action.
- [ ] Do not couple this flyout to future Start menu work.

### Notifications

- [ ] Replace legacy balloon windows with an accessible WinUI transient notification surface.
- [ ] Honor real-time, quiet-time, no-sound, large/user icon, timeout/accessibility duration, and dismissal semantics where data is available.
- [ ] Maintain a taskbar-owned session notification list for notifications received by WinXShell.
- [ ] Show a bell/quiet state only for state WinXShell can truthfully observe.
- [ ] Do not claim access to private Windows Notification Center history.

### Phase 7 gate

- [ ] Overflow, Quick Settings, calendar, and notification surfaces are Acrylic, correctly anchored, and light-dismiss.
- [ ] Volume/mute, network actions, power actions, and date/time actions work.
- [ ] Flyouts never appear on the wrong monitor or underneath the taskbar.
- [ ] Repeated open/close and monitor/DPI changes do not leak windows or crash shutdown.

## Phase 8 — Native-feel polish, accessibility, performance, and reliability

### Motion and interaction polish

- [ ] Tune hover, press, active indicator, attention, add/remove/reposition, overflow, and flyout transitions from side-by-side video.
- [ ] Remove timer-driven GDI animation from the WinUI path.
- [ ] Ensure every animation can be interrupted and retargeted.
- [ ] Respect reduced motion for all non-essential animation, not only alignment.
- [ ] Avoid excessive bounce; the alignment spring is the expressive motion, while routine state changes remain restrained.

### Theme and accessibility

- [ ] Test light, dark, custom accent, transparency off, high contrast themes, and increased text size.
- [ ] Test keyboard-only operation and Narrator reading order.
- [ ] Test focus visibility against Acrylic and solid fallbacks.
- [ ] Verify tooltip and flyout timing follows accessibility settings.
- [ ] Verify color/state is never the only signal for running, active, muted, disconnected, or charging status.
- [ ] Localize new user-facing text and test long locale/date strings plus RTL layout.

### Performance

- [ ] Establish numeric budgets from Phase 0 measurements and record final approved values here.
- [ ] Target first usable WinUI taskbar frame no more than 250 ms slower than the legacy baseline on the reference VM.
- [ ] Target near-zero idle CPU between event-driven updates; align the clock timer to the next required boundary.
- [ ] Target 60 Hz alignment and layout animation on reference hardware with no synchronous icon extraction.
- [ ] Coalesce repeated shell/model updates into one UI-frame collection update.
- [ ] Bound icon cache memory and evict by identity/DPI usage.
- [ ] Profile startup, app churn, notification storms, flyout use, mixed-DPI moves, and 24-hour idle.
- [ ] Keep the packaged WinXShell surface below 10 MB.

### Reliability

- [ ] Handle explorer/shell restart, app crash, hung windows, invalid icons, display hot-plug, GPU reset, sleep/resume, lock/unlock, and session switch.
- [ ] Time out cross-process window messages and never block the UI indefinitely.
- [ ] Log renderer selection, bootstrap version/result, surface lifecycle, monitor changes, and rejected compatibility payloads without leaking private tooltip content by default.
- [ ] Add a last-known-safe legacy launch switch for recovery.
- [ ] Run Application Verifier/handle-leak checks on icon, HWND, GDI, COM, and WinRT ownership.
- [ ] Run the shutdown stress test with every flyout open/closed state.

### Test matrix

| Dimension | Required coverage |
| --- | --- |
| OS | Current supported Windows 11 production build and the oldest explicitly supported Windows 11 build; Windows 10/WinPE fallback where retained |
| Architecture | x86, x64, ARM64; legacy-only ARM32 if retained |
| DPI | 100%, 125%, 150%, 175%, 200%, and mixed-DPI monitor transitions |
| Display | Single monitor, secondary left/right/above, primary change, hot-plug, portrait, resolution change |
| Theme | Light, dark, accent changes, transparency off, high contrast |
| Input | Mouse, touch, pen, keyboard, Narrator |
| Windows | Classic Win32, packaged app, multi-window, multi-process, elevated, hung, flashing, full-screen |
| Tray | x86/x64 sender, GUID and HWND/ID, hidden/visible, overflow, balloon, rapid churn, dead owner |
| Session | Startup, shell restart, lock/unlock, sleep/resume, RDP, sign-out/shutdown |

### Phase 8 gate

- [ ] Every end-state acceptance criterion passes on the required matrix.
- [ ] No known P0/P1 correctness, accessibility, crash, hang, or data-loss issue remains.
- [ ] Performance budgets are met or an approved exception with measurements is recorded.
- [ ] The release-size check passes.

## Phase 9 — Cutover and legacy cleanup

- [ ] Make `auto` select WinUI 3 only after all earlier phase gates pass.
- [ ] Confirm a failed bootstrap still selects one safe legacy renderer.
- [ ] Remove obsolete GDI taskbar UI only if deployment guarantees the required runtime and the fallback is intentionally retired.
- [ ] Otherwise isolate legacy UI behind its renderer factory and stop sharing mutable visual state with WinUI.
- [ ] Remove the new path's dependency on `TaskBar`, `QuickLaunchBar`, `NotifyArea::Paint`, `ClockWindow`, and `StartButton` owner-draw controls.
- [ ] Keep only the compatibility parsing/model behavior still required from legacy tray code.
- [ ] Remove the taskbar use of `SetWindowCompositionAttribute`.
- [ ] Remove global taskbar DPI/icon-size assumptions from code used by the WinUI renderer.
- [ ] Update project documentation with renderer selection, runtime installation, settings/state locations, recovery switch, architecture, and troubleshooting.
- [ ] Archive before/after screenshots, recordings, performance results, and the completed checklist.

### Phase 9 gate

- [ ] WinUI 3 is the default renderer on supported Windows 11 systems.
- [ ] Fallback behavior is intentional, tested, and documented.
- [ ] No duplicate taskbars, tray receivers, app buttons, or `TaskbarCreated` broadcasts occur.
- [ ] The completed implementation matches this document or this document has been updated to match approved decisions.

## Acceptance scenarios

### Alignment

1. Start with five pinned apps, two running, and a populated tray.
2. Select Left, then Center, then alternate twenty times during the active animation.
3. Add/remove an app and widen the tray while the spring is running.
4. Repeat with animations disabled and at mixed monitor DPI.

Pass: the app strip ends at the final requested alignment, Start remains first, spacing is unchanged, hit targets follow visuals, and no frame snaps or overlaps.

### Task apps and icons

1. Pin and launch a classic app, packaged app, multi-window app, and app with a custom AppUserModelID.
2. Exercise activate, minimize, restore, close/context, middle-click, flash, and grouped-window preview.
3. Repeat at each DPI and move the taskbar/related windows between monitors.

Pass: each app has one stable grouped icon, correct state/commands, no text label, an accessible name, and the sharpest available alpha-correct icon.

### Notification icons

1. Add GUID and HWND/ID icons from x86 and x64 senders.
2. Modify icon/tip/state/callback/version, move items into overflow, request icon rectangles, invoke all input callbacks, show/dismiss notifications, then delete/kill owners.
3. Restart the taskbar and observe re-registration.

Pass: identities/order survive modification, callbacks and rectangles are correct, source resolution is preserved, and no stale icon or handle leak remains.

### Acrylic and system status

1. Change wallpaper, light/dark mode, accent, transparency setting, and high contrast.
2. Change volume/mute, connectivity, battery/charging state, input language, locale/time zone, and date.
3. Open each taskbar flyout on each monitor.

Pass: the material and fallbacks remain readable and continuous, status is live and truthful, and every flyout is correctly anchored and functional.

## Proposed code ownership map

Names may change, but responsibility must remain separated.

| Area | Existing/new location | Responsibility |
| --- | --- | --- |
| Runtime host | `winui/WinUIHost.*` | Bootstrap, dispatcher, XAML manager, island lifetime, shutdown |
| XAML surface | `winui/taskbar/TaskbarRoot.*` | Acrylic root and high-level layout |
| App controls | `winui/taskbar/AppIconStrip.*` | Start/app buttons, indicators, overflow, alignment visual |
| Tray controls | `winui/taskbar/NotificationArea.*` | Notification icons, overflow, system cluster |
| Flyouts | `winui/taskbar/flyouts/*` | Quick Settings, calendar, notification surfaces |
| Shell bridge | `taskbar/TaskbarShellBridge.*` | HWND classes, work area, monitor hosts, shell messages |
| App model | `taskbar/TaskItemRepository.*` | Window/pin identity, grouping, order, commands |
| Tray adapter/model | `taskbar/ShellNotifyAdapter.*`, `NotificationIconRepository.*` | Validated `NIM_*` compatibility and durable tray state |
| Icons | `taskbar/IconLoader.*` | Async high-DPI extraction, conversion, caching |
| Settings | `taskbar/TaskbarSettingsRepository.*` | Renderer, alignment, pinned order, tray policy |
| System status | `taskbar/SystemStatusService.*` | Network, audio, power, language, clock events |

## Risk register

| Risk | Mitigation |
| --- | --- |
| Windows App SDK runtime is absent or wrong architecture/version. | Explicit versioned bootstrap, logged result, documented installer, one safe legacy fallback. |
| XAML island teardown crashes the shell. | Single owner, strict shutdown order, idempotent lifecycle, 100-cycle and flyout shutdown stress tests. |
| Replacement-tray receiver wire format changes because it is not a documented shell-provider contract. | Isolate it in one adapter, validate every payload, keep cross-bitness tests, fail closed, retain recovery renderer. |
| Notification icon is intrinsically low resolution. | Preserve the supplied source; never pre-shrink it; request task-app imagery from higher-resolution shell sources; document the sender limitation. |
| Acrylic is disabled or unavailable. | Supported system backdrop plus explicit opaque light/dark/high-contrast fallback. |
| Global DPI assumptions corrupt geometry or icons. | Per-Monitor V2 manifest, per-host DPI, device-pixel cache keys, mixed-DPI tests. |
| ARM32 build cannot use the Windows App SDK runtime. | Add ARM64; keep ARM32 legacy-only or retire it explicitly. |
| Pinned and running apps do not merge. | AppUserModelID-first identity, documented fallbacks, fixture apps covering hosted and multi-process cases. |
| Centered app strip collides with a large tray or many apps. | True-center then clamp; task overflow; geometry stress tests during animation. |
| Blocking shell APIs cause stutter. | Bounded worker, cancellation, cache, UI-frame coalescing, performance profiling. |

## Suggested reviewable change sequence

1. [ ] Build/deployment, Per-Monitor-V2 manifest, and explicit bootstrap.
2. [ ] Blank XAML island with lifecycle and renderer fallback.
3. [ ] Shell bridge plus Acrylic root and monitor geometry.
4. [ ] Unified task model and asynchronous icon loader.
5. [ ] WinUI app strip, Start glyph, activation, indicators, and overflow.
6. [ ] Persisted left/center setting and spring alignment.
7. [ ] Isolated notification compatibility adapter and test sender.
8. [ ] WinUI notification icons, overflow, and callback geometry.
9. [ ] System status cluster and flyouts.
10. [ ] Accessibility, DPI/multi-monitor, performance, reliability, and cutover.

## Official references

- [WinUI 3 overview](https://learn.microsoft.com/en-us/windows/apps/winui/winui3/)
- [Windows App SDK downloads and supported runtime architectures](https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/downloads)
- [Windows App SDK deployment overview](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/deploy-overview)
- [Bootstrap an unpackaged Windows App SDK application](https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/tutorial-unpackaged-deployment)
- [DesktopWindowXamlSource API](https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.ui.xaml.hosting.desktopwindowxamlsource)
- [System backdrops: Mica and Acrylic](https://learn.microsoft.com/en-us/windows/apps/develop/ui/system-backdrops)
- [XAML and Composition interoperability](https://learn.microsoft.com/en-us/windows/apps/develop/composition/xaml-comp-interop)
- [Spring animations](https://learn.microsoft.com/en-us/windows/apps/develop/composition/spring-animations)
- [Per-Monitor-V2 process manifest guidance](https://learn.microsoft.com/en-us/windows/win32/hidpi/setting-the-default-dpi-awareness-for-a-process)
- [High-DPI desktop application development](https://learn.microsoft.com/en-us/windows/win32/hidpi/high-dpi-desktop-application-development-on-windows)
- [NOTIFYICONDATA and high-DPI notification icon guidance](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ns-shellapi-notifyicondataw)
- [Shell_NotifyIcon semantics](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shell_notifyiconw)
- [NOTIFYICONIDENTIFIER and icon rectangles](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ns-shellapi-notifyiconidentifier)
- [Application User Model IDs and taskbar grouping](https://learn.microsoft.com/en-us/windows/win32/shell/appids)
- [IShellItemImageFactory::GetImage](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellitemimagefactory-getimage)
- [Segoe Fluent Icons](https://learn.microsoft.com/en-us/windows/apps/design/iconography/segoe-fluent-icons-font)
- [UISettings animation and transparency preferences](https://learn.microsoft.com/en-us/uwp/api/windows.ui.viewmanagement.uisettings)
