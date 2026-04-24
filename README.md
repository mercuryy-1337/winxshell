# Explauncher

Explauncher is a native Win32 shell replacement and launcher for Windows. It combines a desktop shell, taskbar, start menu, search experience, Lua scripting support, and a packaged theme/UI system in a single C++ codebase.

This repository is the rebranded Explauncher line and the current public app version is 1.0.6.

## Notable 1.0.6 changes

- Bumped the public Explauncher version to `1.0.6`.
- Added a taskbar right-click toggle for setting PeaZip as the per-user default app for `.zip` and `.rar` files.
- The toggle writes user-level associations under `HKCU\\Software\\Classes`, so the change does not require admin rights.

## Notable 1.0.5 changes

- Bumped the public Explauncher version to `1.0.5`.
- Rounded the taskbar alignment popup option highlights so the option and submenu boxes match the popup's rounded surfaces more closely.

## What Explauncher includes

- A custom taskbar and modern start menu written with Win32 and GDI/GDI+.
- Search that can launch apps, resolve file system paths, and browse matching folders/files.
- Shell integration helpers for startup, daemon behavior, system UI hooks, and quick actions.
- A Lua runtime and packaged UI components for dialogs, utilities, overlays, and tray experiences.
- A release theme bundle under `release/explauncher/Theme` with the runtime script, configuration, and UI assets.

## Repository layout

- `Explauncher_VS2022.sln`: main Visual Studio solution.
- `Explauncher.vcxproj`: main GUI executable project that builds `Explauncher.exe`.
- `WinXShellC/ExplauncherC.vcxproj`: console/helper executable that builds `ExplauncherC.exe`.
- `taskbar/`, `desktop/`, `shell/`: core shell UI and interaction layers.
- `luaengine/`, `lua_helper/`: Lua runtime bindings and helper libraries.
- `release/explauncher/Theme`: packaged theme/config/runtime files intended for distribution.

## Building

### Requirements

- Windows
- Visual Studio 2022 with C++ desktop build tools
- MSBuild from the Visual Studio 2022 toolchain

### Build commands

Build x64 Release (default):

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" `
  "c:\Users\user\Documents\Cpp\winxshell\Explauncher_VS2022.sln" `
  /t:Build /p:Configuration=Release /p:Platform=x64 /m
```

Successful Release builds output `Explauncher.exe` and `ExplauncherC.exe` into `x64/Release`.

## Common runtime modes

- `-shell`: start Explauncher as the shell workflow.
- `-daemon`: run daemon/property-handler behavior without starting the full shell UI.
- `-ui` or `-jcfg`: launch a packaged UI definition directly.
- `-code` or `-script`: execute Lua code or a Lua file.
- `-console`: open the debug console for script/code runs.
- `-log`: write a log file while running.
- `-regist` or `-regist_only`: register the executable in App Paths.

## Configuration and theme files

Preferred rebranded runtime files now live in the release theme package:

- `release/explauncher/Theme/Explauncher.jcfg`
- `release/explauncher/Theme/Explauncher.lua`
- `release/explauncher/Theme/Explauncher.zh-CN.jcfg`

The codebase still contains compatibility fallbacks for earlier WinXShell names in several places so older deployments do not break immediately.

## Notable 1.0.4 changes

- Rebased visible product versioning to `1.0.4` for the Explauncher line.
- The taskbar now creates WinXShell's notify area from its own config by default instead of inheriting Explorer's `REST_NOTRAYITEMSDISPLAY` restriction, which restores the tray and clock in stripped-down environments without requiring admin rights.
- Taskbar and pinned-app icons now render through alpha-capable toolbar image lists so modern taskbar buttons no longer show black bitmap squares behind icons.
- The desktop shell now prefers `wallpaper.jpg` from the executable directory and keeps that as the active shell wallpaper source when the file is present.
- File-system folders and archive files such as `.zip` now launch the packaged `../Explorer/peazip.exe` binary and pass the selected path as the argument.
- Replaced the modern start menu and taskbar Explorer-style fallback pins with `PeaZip` when the packaged binary is present.
- Added blurred right-click object menus in the modern start menu with `Open`, `Copy`, and `Copy path` actions.
- Improved search-result icon resolution for file paths and executable targets.
- Improved result grouping and spacing for the focused folder entry in path-search mode.
- Updated release theme package naming and documentation to the Explauncher brand.

## License

This project is distributed under the GNU Lesser General Public License version 2.1. See `LICENSE.md` for the full license text.