/*
 * Copyright 2003, 2004, 2005 Martin Fuchs
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */


//
// Explorer clone
//
// taskbar.cpp
//
// Martin Fuchs, 16.08.2003
//


#include <precomp.h>
#include <WinUser.h>
#include "taskbar.h"
#include "quicklaunch.h"
#include "taskbar_identity.h"
#include "traynotify.h" // for NOTIFYAREA_WIDTH_DEF
#include "../resource.h"
#include "../utility/taskbar_draw.h"

#include <Uxtheme.h>
#include <dwmapi.h>
#include <math.h>
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")

extern HRESULT CreateShortcut(PTSTR lnk, PTSTR target, PTSTR param, PTSTR icon, int iIcon, int iShowCmd);


DynamicFct<BOOL (WINAPI *)(HWND hwnd)> g_SetTaskmanWindow(TEXT("user32"), "SetTaskmanWindow");
DynamicFct<BOOL (WINAPI *)(HWND hwnd)> g_RegisterShellHookWindow(TEXT("user32"), "RegisterShellHookWindow");
DynamicFct<BOOL (WINAPI *)(HWND hwnd)> g_DeregisterShellHookWindow(TEXT("user32"), "DeregisterShellHookWindow");


DynamicFct<BOOL (WINAPI*)(HWND hWnd, DWORD dwType)> g_RegisterShellHook(TEXT("shell32"), (LPCSTR)0xb5);

#define ID_TIMER_DESTORYTHUMBNAIL 101
#define ID_TIMER_ANIMATEBUTTONS   102
extern void InitThumbnailWindow(HWND taskbar, HWND toolbar);
extern int DrawThumbnailWindows(HINSTANCE hInstance, const HWND *hwnds, int hwnd_count, HWND hwndToolbar, int buttonIndex);
extern void DestoryThumbnailWindow();
extern bool IsThumbnailCursorInRegion();

extern void TaskbarTransparency(HWND hwnd, const TCHAR *mode, UINT transparency, COLORREF color);

// constants for RegisterShellHook()
#define RSH_UNREGISTER          0
#define RSH_REGISTER            1
#define RSH_TASKMGR             3


#ifdef _WIN64
#define GCL_HICON GCLP_HICON
#define GCL_HICONSM GCLP_HICONSM
#endif

#ifndef TBCDRF_NOEDGES
#define TBCDRF_NOEDGES       0x00010000
#endif

#ifndef TBCDRF_NOOFFSET
#define TBCDRF_NOOFFSET      0x00040000
#endif

#ifndef TBCDRF_NOETCHEDEFFECT
#define TBCDRF_NOETCHEDEFFECT 0x00100000
#endif

#ifndef TBCDRF_NOBACKGROUND
#define TBCDRF_NOBACKGROUND  0x00400000
#endif

#ifndef CDRF_USECDCOLORS
#define CDRF_USECDCOLORS     0x00800000
#endif

static HBRUSH hbrTaskLine = NULL;

static double GetTaskbarAnimationClockMilliseconds()
{
    static LARGE_INTEGER frequency = { 0 };
    if (frequency.QuadPart == 0)
        QueryPerformanceFrequency(&frequency);

    LARGE_INTEGER now = { 0 };
    QueryPerformanceCounter(&now);

    if (frequency.QuadPart == 0)
        return 0.0;

    return (double)now.QuadPart * 1000.0 / (double)frequency.QuadPart;
}

static bool HasVisibleOrderKey(const vector<String> &order, const String &key)
{
    for (size_t index = 0; index < order.size(); ++index)
        if (order[index] == key)
            return true;

    return false;
}

static bool TryGetRemovalOnlyIndices(const vector<String> &previous_order, const vector<String> &desired_visible_order,
    vector<int> *removed_indices)
{
    if (!removed_indices)
        return false;

    removed_indices->clear();
    if (desired_visible_order.size() >= previous_order.size())
        return false;

    size_t desired_index = 0;
    for (size_t previous_index = 0; previous_index < previous_order.size(); ++previous_index) {
        if (desired_index < desired_visible_order.size() && previous_order[previous_index] == desired_visible_order[desired_index]) {
            ++desired_index;
        } else {
            removed_indices->push_back((int)previous_index);
        }
    }

    return desired_index == desired_visible_order.size() && !removed_indices->empty();
}

    static bool TryGetAdditionOnlyIndices(const vector<String> &previous_order, const vector<String> &desired_visible_order,
        vector<int> *added_indices)
    {
        if (!added_indices)
            return false;

        added_indices->clear();
        if (desired_visible_order.size() <= previous_order.size())
            return false;

        size_t previous_index = 0;
        for (size_t desired_index = 0; desired_index < desired_visible_order.size(); ++desired_index) {
            if (previous_index < previous_order.size() && desired_visible_order[desired_index] == previous_order[previous_index]) {
                ++previous_index;
            } else {
                added_indices->push_back((int)desired_index);
            }
        }

        return previous_index == previous_order.size() && !added_indices->empty();
    }

static bool AreTrailingAddedIndices(const vector<String> &previous_order, const vector<int> &added_indices)
{
    if (added_indices.empty())
        return false;

    for (size_t index = 0; index < added_indices.size(); ++index) {
        if (added_indices[index] != (int)(previous_order.size() + index))
            return false;
    }

    return true;
}

static String ResolvePinnedAlias(const map<String, String> &aliases, const String &key)
{
    if (key.empty())
        return key;

    String resolved = key;
    for (int guard = 0; guard < 8; ++guard) {
        map<String, String>::const_iterator found = aliases.find(resolved);
        if (found == aliases.end() || found->second.empty() || found->second == resolved)
            break;

        resolved = found->second;
    }

    return resolved;
}

static void RegisterPinnedAlias(map<String, String> &aliases, const String &canonical_key, const String &alias_key)
{
    if (canonical_key.empty() || alias_key.empty() || canonical_key == alias_key)
        return;

    aliases[alias_key] = canonical_key;
}

static String GetPinnedShortcutDisplayName(LPCTSTR path)
{
    SHFILEINFO sfi = { 0 };
    if (SHGetFileInfo(path, 0, &sfi, sizeof(sfi), SHGFI_DISPLAYNAME) && sfi.szDisplayName[0])
        return sfi.szDisplayName;

    TCHAR display_name[MAX_PATH] = { 0 };
    lstrcpyn(display_name, PathFindFileName(path), COUNTOF(display_name));
    PathRemoveExtension(display_name);
    return display_name;
}

static bool IsPeaZipPath(LPCTSTR path)
{
    if (!path || !*path)
        return false;

    TCHAR peazip_path[MAX_PATH] = { 0 };
    return TryGetPeaZipPath(peazip_path, COUNTOF(peazip_path)) && !_tcsicmp(path, peazip_path);
}

static String ResolvePeaZipFamilyAppKey(LPCTSTR path)
{
    if (!path || !*path || !taskbar_identity::IsPeaZipProcessPath(path))
        return String();

    String peazip_app_key = taskbar_identity::GetPeaZipAppKey();
    if (!peazip_app_key.empty())
        return peazip_app_key;

    return taskbar_identity::MakePathKey(path);
}

static bool TryRewritePinnedShortcutToPeaZip(LPCTSTR shortcut_path, LPCTSTR peazip_path)
{
    if (!shortcut_path || !*shortcut_path || !peazip_path || !*peazip_path)
        return false;

    TCHAR shortcut_copy[MAX_PATH] = { 0 };
    TCHAR target_copy[MAX_PATH] = { 0 };
    TCHAR icon_copy[MAX_PATH] = { 0 };
    lstrcpyn(shortcut_copy, shortcut_path, COUNTOF(shortcut_copy));
    lstrcpyn(target_copy, peazip_path, COUNTOF(target_copy));
    lstrcpyn(icon_copy, peazip_path, COUNTOF(icon_copy));

    return SUCCEEDED(CreateShortcut(shortcut_copy, target_copy, NULL, icon_copy, 0, SW_SHOWNORMAL));
}

static bool IsExplorerPinnedShortcut(LPCTSTR shortcut_path, const String &pin_title, LPCTSTR target_path, const String &shortcut_app_id)
{
    if (IsPeaZipPath(target_path))
        return false;

    if (!shortcut_app_id.empty() && shortcut_app_id == taskbar_identity::GetExplorerAppIdKey())
        return true;

    if (target_path && *target_path && taskbar_identity::IsExplorerProcessPath(target_path))
        return true;

    if (shortcut_path && *shortcut_path) {
        LPCTSTR file_name = PathFindFileName(shortcut_path);
        if (file_name) {
            if (!_tcsicmp(file_name, TEXT("File Explorer.lnk")) ||
                !_tcsicmp(file_name, TEXT("Explorer.lnk")) ||
                !_tcsicmp(file_name, TEXT("My Computer.lnk")) ||
                !_tcsicmp(file_name, TEXT("Computer.lnk")) ||
                !_tcsicmp(file_name, TEXT("This PC.lnk")))
                return true;
        }
    }

    String explorer_title = ResString(IDS_TITLE);
    return !pin_title.empty() && (
        !_tcsicmp(pin_title.c_str(), explorer_title.c_str()) ||
        !_tcsicmp(pin_title.c_str(), TEXT("Explorer")) ||
        !_tcsicmp(pin_title.c_str(), TEXT("File Explorer")) ||
        !_tcsicmp(pin_title.c_str(), TEXT("My Computer")) ||
        !_tcsicmp(pin_title.c_str(), TEXT("Computer")) ||
        !_tcsicmp(pin_title.c_str(), TEXT("This PC")));
}

static String GetPinnedEntryLaunchPathKey(const TaskBarEntry &entry)
{
    if (entry._launch_kind != TASKBAR_LAUNCH_SHORTCUT || entry._launch_path.empty())
        return String();

    if (PathMatchSpec(entry._launch_path.c_str(), TEXT("*.lnk"))) {
        TCHAR target_path[MAX_PATH] = { 0 };
        GetShortcutPath(entry._launch_path.c_str(), target_path, COUNTOF(target_path));
        if (!target_path[0])
            return String();

        String peazip_key = ResolvePeaZipFamilyAppKey(target_path);
        if (!peazip_key.empty())
            return peazip_key;

        return taskbar_identity::MakePathKey(target_path);
    }

    String peazip_key = ResolvePeaZipFamilyAppKey(entry._launch_path.c_str());
    if (!peazip_key.empty())
        return peazip_key;

    return taskbar_identity::MakePathKey(entry._launch_path.c_str());
}

String TaskBar::ResolvePinnedLaunchAppKey(LPCTSTR process_path) const
{
    if (!process_path || !*process_path)
        return String();

    String process_key = ResolvePeaZipFamilyAppKey(process_path);
    if (process_key.empty())
        process_key = taskbar_identity::MakePathKey(process_path);

    if (process_key.empty())
        return String();

    for (TaskBarMap::const_iterator it = _map.begin(); it != _map.end(); ++it) {
        const TaskBarEntry &entry = it->second;
        if (!entry._pinned)
            continue;

        String launch_key = GetPinnedEntryLaunchPathKey(entry);
        if (!launch_key.empty() && launch_key == process_key)
            return entry._app_key;
    }

    return String();
}

void TaskBar::MergePinnedProcessMatches()
{
    for (TaskBarMap::iterator it = _map.begin(); it != _map.end(); ++it) {
        TaskBarEntry &entry = it->second;
        if (!entry._used || entry._pinned || entry._windows.empty())
            continue;

        String pinned_app_key;
        for (size_t index = 0; index < entry._windows.size(); ++index) {
            TCHAR process_path[MAX_PATH] = { 0 };
            if (!taskbar_identity::GetWindowProcessPath(entry._windows[index], process_path, COUNTOF(process_path)))
                continue;

            pinned_app_key = ResolvePinnedLaunchAppKey(process_path);
            if (!pinned_app_key.empty())
                break;
        }

        if (pinned_app_key.empty() || pinned_app_key == it->first)
            continue;

        TaskBarMap::iterator pinned_found = _map.find(pinned_app_key);
        if (pinned_found == _map.end())
            continue;

        TaskBarEntry &pinned_entry = pinned_found->second;
        pinned_entry._used += entry._used;
        pinned_entry._pinned = true;

        if ((entry._fsState & (TBSTATE_PRESSED | TBSTATE_CHECKED)) != 0)
            pinned_entry._fsState = entry._fsState;

        if ((!pinned_entry._primary_hwnd || !IsWindow(pinned_entry._primary_hwnd)) &&
            entry._primary_hwnd && IsWindow(entry._primary_hwnd)) {
            pinned_entry._primary_hwnd = entry._primary_hwnd;
        }

        if (pinned_entry._title.empty() && !entry._title.empty())
            pinned_entry._title = entry._title;
        if (pinned_entry._pid == 0)
            pinned_entry._pid = entry._pid;

        for (size_t window_index = 0; window_index < entry._windows.size(); ++window_index)
            pinned_entry._windows.push_back(entry._windows[window_index]);

        pinned_entry._window_group_count = (int)pinned_entry._windows.size();

        entry._used = 0;
        entry._window_group_count = 0;
        entry._primary_hwnd = 0;
        entry._fsState = TBSTATE_ENABLED;
        entry._windows.clear();
    }
}

static bool IsExcludedTaskbarClass(LPCTSTR class_name)
{
    if (!class_name || !*class_name)
        return false;

    return !_tcsicmp(class_name, TEXT("Progman")) ||
        !_tcsicmp(class_name, TEXT("WorkerW")) ||
        !_tcsicmp(class_name, TEXT("Shell_TrayWnd")) ||
        !_tcsicmp(class_name, TEXT("Shell_SecondaryTrayWnd")) ||
        !_tcsicmp(class_name, TEXT("MSTaskSwWClass")) ||
        !_tcsicmp(class_name, TEXT("dwmTaskThumbnailWnd"));
}

static void DrawTaskbarButtonHighlight(HDC hdc, const RECT &item_rect, float hover_progress, float active_progress)
{
    int hl_pad_y = DPI_SY(2);
    int hl_margin_x = DPI_SX(1);
    RECT highlight_rect = {
        item_rect.left + hl_margin_x,
        item_rect.top + hl_pad_y,
        item_rect.right - hl_margin_x,
        item_rect.bottom - hl_pad_y
    };

    if (highlight_rect.bottom <= highlight_rect.top)
        return;

    if (hover_progress > 0.0f) {
        taskbar_draw::FillRoundedRect(
            hdc,
            highlight_rect,
            taskbar_draw::GetHighlightRadius(),
            taskbar_draw::GetHoverColor(),
            taskbar_draw::LerpAlpha(0, taskbar_draw::GetHoverAlpha(), hover_progress));
    }

    if (active_progress > 0.0f) {
        taskbar_draw::FillRoundedRect(
            hdc,
            highlight_rect,
            taskbar_draw::GetHighlightRadius(),
            taskbar_draw::GetHighlightColor(),
            taskbar_draw::LerpAlpha(0, taskbar_draw::GetHighlightAlpha(), active_progress));
    }
}

static bool UseDirectTaskbarIconDraw(bool rounded_highlight, bool no_task_title)
{
    return rounded_highlight && no_task_title;
}

static void DrawTaskbarButtonIndicators(HDC hdc, const RECT &item_rect, int wnd_count, float active_progress)
{
    if (wnd_count <= 0)
        return;

    COLORREF indicator_color = TASKBAR_TASKLINECOLOR();
    if (indicator_color == MAXDWORD)
        return;

    BYTE dot_alpha = taskbar_draw::LerpAlpha(
        taskbar_draw::GetIndicatorIdleAlpha(),
        taskbar_draw::GetIndicatorActiveAlpha(),
        active_progress);

    int dot_d = DPI_SX(4);
    int dot_r = dot_d / 2;
    int dot_y = item_rect.bottom - DPI_SY(5) - dot_d;
    int btn_cx = (item_rect.left + item_rect.right) / 2;

    if (wnd_count >= 2) {
        int gap = DPI_SX(3);
        RECT d1 = { btn_cx - gap / 2 - dot_d, dot_y, btn_cx - gap / 2, dot_y + dot_d };
        RECT d2 = { btn_cx + (gap + 1) / 2, dot_y, btn_cx + (gap + 1) / 2 + dot_d, dot_y + dot_d };
        taskbar_draw::FillRoundedRect(hdc, d1, dot_r, indicator_color, dot_alpha);
        taskbar_draw::FillRoundedRect(hdc, d2, dot_r, indicator_color, dot_alpha);
    } else {
        RECT dot = { btn_cx - dot_r, dot_y, btn_cx + dot_r, dot_y + dot_d };
        taskbar_draw::FillRoundedRect(hdc, dot, dot_r, indicator_color, dot_alpha);
    }
}

static void DrawTaskbarButtonIndicators(HDC hdc, const RECT &item_rect, int wnd_count, float active_progress, float visibility_progress)
{
    if (visibility_progress <= 0.0f)
        return;

    if (visibility_progress >= 0.99f) {
        DrawTaskbarButtonIndicators(hdc, item_rect, wnd_count, active_progress);
        return;
    }

    DrawTaskbarButtonIndicators(hdc, item_rect, wnd_count, active_progress * visibility_progress);
}

struct TaskbarResolvedIcon {
    TaskbarResolvedIcon()
        : _icon(NULL),
          _destroy(false)
    {
    }

    HICON   _icon;
    bool    _destroy;
};

static void ReleaseTaskbarResolvedIcon(TaskbarResolvedIcon *resolved)
{
    if (!resolved)
        return;

    if (resolved->_destroy && resolved->_icon)
        DestroyIcon(resolved->_icon);

    resolved->_icon = NULL;
    resolved->_destroy = false;
}

static bool TryResolveTaskbarIconFromPath(LPCTSTR path, TaskbarResolvedIcon *resolved)
{
    if (!resolved || !path || !*path)
        return false;

    if (PathMatchSpec(path, TEXT("*.exe")) || PathMatchSpec(path, TEXT("*.ico"))) {
        HICON extracted_icon = NULL;
        int requested_size = TASKBAR_ICON_SIZE * 2;
        if (requested_size < DPI_SX(40))
            requested_size = DPI_SX(40);
        if (requested_size < 48)
            requested_size = 48;

        UINT extracted = PrivateExtractIcons(path, 0, requested_size, requested_size, &extracted_icon, NULL, 1, LR_LOADFROMFILE);
        if (extracted > 0 && extracted_icon) {
            resolved->_icon = extracted_icon;
            resolved->_destroy = true;
            return true;
        }
    }

    const Icon &icon = g_Globals._icon_cache.extract(path, ICF_LARGE | ICF_NOLINKOVERLAY);
    if ((ICON_ID)icon != ICID_NONE && (ICON_ID)icon != ICID_UNKNOWN && icon.get_hicon()) {
        resolved->_icon = icon.get_hicon();
        resolved->_destroy = false;
        return true;
    }

    SHFILEINFO sfi = { 0 };
    if (SHGetFileInfo(path, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON) && sfi.hIcon) {
        resolved->_icon = sfi.hIcon;
        resolved->_destroy = true;
        return true;
    }

    return false;
}

static TaskbarResolvedIcon ResolveTaskbarEntryIcon(const TaskBarEntry &entry)
{
    TaskbarResolvedIcon resolved;

    String peazip_app_key = taskbar_identity::GetPeaZipAppKey();
    if (!peazip_app_key.empty() && entry._app_key == peazip_app_key) {
        TCHAR peazip_path[MAX_PATH] = { 0 };
        if (TryGetPeaZipPath(peazip_path, COUNTOF(peazip_path)) && TryResolveTaskbarIconFromPath(peazip_path, &resolved))
            return resolved;
    }

    if (entry._launch_kind == TASKBAR_LAUNCH_EXPLORER) {
        resolved._icon = g_Globals._icon_cache.get_icon(ICID_EXPLORER).get_hicon();
        return resolved;
    }

    if (entry._launch_kind == TASKBAR_LAUNCH_SHORTCUT && !entry._launch_path.empty()) {
        if (TryResolveTaskbarIconFromPath(entry._launch_path.c_str(), &resolved))
            return resolved;

        if (PathMatchSpec(entry._launch_path.c_str(), TEXT("*.lnk"))) {
            TCHAR target_path[MAX_PATH] = { 0 };
            GetShortcutPath(entry._launch_path.c_str(), target_path, COUNTOF(target_path));
            if (TryResolveTaskbarIconFromPath(target_path, &resolved))
                return resolved;
        }
    }

    String icon_path = entry._launch_path;
    if (icon_path.empty()) {
        LPCTSTR app_key = entry._app_key.c_str();
        if (!_tcsncmp(app_key, TEXT("path:"), 5))
            icon_path = app_key + 5;
    }

    if (TryResolveTaskbarIconFromPath(icon_path.c_str(), &resolved))
        return resolved;

    if (entry._primary_hwnd && IsWindow(entry._primary_hwnd)) {
        TCHAR process_path[MAX_PATH] = { 0 };
        if (taskbar_identity::GetWindowProcessPath(entry._primary_hwnd, process_path, COUNTOF(process_path)) &&
            TryResolveTaskbarIconFromPath(process_path, &resolved)) {
            return resolved;
        }
    }

    if (entry._primary_hwnd && IsWindow(entry._primary_hwnd)) {
        TCHAR class_name[BUFFER_LEN] = { 0 };
        if (GetClassName(entry._primary_hwnd, class_name, COUNTOF(class_name)) &&
            !_tcsicmp(class_name, TEXT("ConsoleWindowClass"))) {
            resolved._icon = g_Globals._icon_cache.get_icon(ICID_CMDEXE).get_hicon();
            return resolved;
        }

        resolved._icon = get_window_icon_big(entry._primary_hwnd, true);
        if (resolved._icon)
            return resolved;
    }

    resolved._icon = LoadIcon(0, IDI_APPLICATION);
    return resolved;
}

static RECT GetDirectTaskbarIconRect(const RECT &item_rect)
{
    int icon_size = TASKBAR_ICON_SIZE + DPI_SX(3);
    int max_width = item_rect.right - item_rect.left - DPI_SX(4);
    int max_height = item_rect.bottom - item_rect.top - DPI_SY(14);
    int max_size = min(max_width, max_height);

    if (max_size < DPI_SX(20))
        max_size = min(item_rect.right - item_rect.left, item_rect.bottom - item_rect.top);

    if (icon_size < DPI_SX(20))
        icon_size = DPI_SX(20);
    if (icon_size > max_size)
        icon_size = max_size;
    if (icon_size < 1)
        icon_size = 1;

    int item_width = item_rect.right - item_rect.left;
    if (((item_width - icon_size) & 1) != 0) {
        if (icon_size > DPI_SX(20))
            --icon_size;
        else if (icon_size < max_size)
            ++icon_size;
    }

    int left = item_rect.left + ((item_rect.right - item_rect.left) - icon_size) / 2;
    int top = item_rect.top + ((item_rect.bottom - item_rect.top) - icon_size) / 2 - DPI_SY(1);
    int bottom_margin = DPI_SY(7);
    if (top + icon_size > item_rect.bottom - bottom_margin)
        top = item_rect.bottom - bottom_margin - icon_size;

    RECT rect = { left, top, left + icon_size, top + icon_size };
    return rect;
}

static RECT GetAnimatedDirectTaskbarIconRect(const RECT &item_rect, const TaskBarEntry &entry)
{
    RECT rect = GetDirectTaskbarIconRect(item_rect);

    if (entry._icon_animating_in && entry._icon_visibility < 1.0f) {
        int offset = max(DPI_SY(10), (rect.bottom - rect.top) / 2);
        int shift = (int)((1.0f - entry._icon_visibility) * offset + 0.5f);
        OffsetRect(&rect, 0, shift);
    }

    return rect;
}

static void DrawDirectTaskbarEntryIcon(HDC hdc, const RECT &item_rect, const TaskBarEntry &entry)
{
    TaskbarResolvedIcon resolved = ResolveTaskbarEntryIcon(entry);
    if (resolved._icon) {
        RECT icon_rect = GetAnimatedDirectTaskbarIconRect(item_rect, entry);
        BYTE alpha = taskbar_draw::ClampAlpha((int)(entry._icon_visibility * 255.0f + 0.5f));
        draw_icon_high_quality(hdc, resolved._icon, icon_rect, alpha);
    }
    ReleaseTaskbarResolvedIcon(&resolved);
}

TaskBarEntry::TaskBarEntry()
{
    _id = 0;
    _hbmp = 0;
    _bmp_idx = 0;
    _used = 0;
    _btn_idx = 0;
    _fsState = 0;
    _hover_progress = 0.0f;
    _active_progress = 0.0f;
    _icon_visibility = 1.0f;
    _pid = 0;
    _window_group_count = 0;
    _primary_hwnd = 0;
    _launch_kind = TASKBAR_LAUNCH_NONE;
    _pinned = false;
    _icon_animating_in = false;
    _icon_animating_out = false;
    _pending_remove = false;
}

TaskBarMap::~TaskBarMap()
{
    while (!empty()) {
        iterator it = begin();
        DeleteBitmap(it->second._hbmp);
        erase(it);
    }
}

RECT TaskBar::_icon_area = { 1, 0, TASKBAR_ICON_SIZE + 4, DESKTOPBARBAR_HEIGHT - 4 };

void TaskBar::InitTaskbarStyle()
{
    _centered_layout = taskbar_draw::IsCenteredEnabled();
    _rounded_highlight = taskbar_draw::IsRoundedHighlightEnabled();
    _animate_highlights = taskbar_draw::IsAnimationEnabled();
    _animation_timer_running = false;
    _preferred_btn_width = taskbar_draw::GetButtonSlotWidth();

    _no_task_title = false;
    _task_close_button = false;
    bool show_task_line = false;
    COLORREF clrTaskLine = TASKBAR_TASKLINECOLOR();

    _icon_area.left = 1;
    _icon_area.top = 0;
    _icon_area.right = TASKBAR_ICON_SIZE + DPI_SX(4);
    _icon_area.bottom = DESKTOPBARBAR_HEIGHT - 4;

    bool prefer_icon_only = _centered_layout || _rounded_highlight;
    if (JCFG2_DEF("JS_TASKBAR", "no_task_title", prefer_icon_only).ToBool() != FALSE) {
        _no_task_title = true;
        _icon_area.left = 0;
        _icon_area.top = 0;
        _icon_area.right = TASKBAR_ICON_SIZE + DPI_SX(4);
        _icon_area.bottom = DESKTOPBARBAR_HEIGHT;
    } else {
        if (!_rounded_highlight && JCFG2_DEF("JS_TASKBAR", "task_close_button", false).ToBool() != FALSE) {
            _task_close_button = true;
        }
    }

    if (UseDirectTaskbarIconDraw(_rounded_highlight, _no_task_title)) {
        _preferred_btn_width = taskbar_draw::GetModernButtonSlotWidth();
    }

    if (!_rounded_highlight && clrTaskLine != MAXDWORD) {
        show_task_line = true;
        _icon_area.top = -1;
        _icon_area.bottom -= 3;
    }

    String msstyle_taskbutton = JCFG2_DEF("JS_TASKBAR", "msstyle_taskbutton", TEXT("auto")).ToString();
    if (msstyle_taskbutton == TEXT("auto")) {
        if (_rounded_highlight) {
            msstyle_taskbutton = TEXT("");
        } else if (_task_close_button) {
            msstyle_taskbutton = TEXT("BB");
        } else if (TASKBAR_THEMESTYLE().compare(TEXT("light")) == 0) {
            msstyle_taskbutton = TEXT("BB");
        } else {
            msstyle_taskbutton = TEXT("DarkMode");
            JCFG_QL_SET(2, "hide_fixedsep") = true;
        }
    }
    if (msstyle_taskbutton != TEXT("")) {
        JCFG_TB_SET(2, "msstyle_taskbutton") = msstyle_taskbutton;
        JCFG_QL_SET(2, "msstyle_button") = msstyle_taskbutton;
    }
}

TaskBar::TaskBar(HWND hwnd)
    :  super(hwnd),
       WM_SHELLHOOK(RegisterWindowMessage(WINMSG_SHELLHOOK))
{
    _himl = 0;
    _last_btn_width = 0;
    _pending_reserved_button_count = 0;
    _pending_add_commit = false;
    _committing_pending_adds = false;

    _mmMetrics_org.cbSize = sizeof(MINIMIZEDMETRICS);

    SystemParametersInfo(SPI_GETMINIMIZEDMETRICS, sizeof(_mmMetrics_org), &_mmMetrics_org, 0);

    // configure the window manager to hide windows when they are minimized
    // This is neccessary to enable shell hook messages.
    if (!(_mmMetrics_org.iArrange & ARW_HIDE)) {
        MINIMIZEDMETRICS _mmMetrics_new = _mmMetrics_org;

        _mmMetrics_new.iArrange |= ARW_HIDE;

        SystemParametersInfo(SPI_SETMINIMIZEDMETRICS, sizeof(_mmMetrics_new), &_mmMetrics_new, 0);
    }

    InitTaskbarStyle();
    _last_animation_clock_ms = 0.0;
}

void TaskBar::ClearPendingAddReservation()
{
    _pending_reserved_button_count = 0;
    _pending_add_commit = false;
    _committing_pending_adds = false;
}

bool TaskBar::GetVisualButtonRect(const TaskBarEntry &entry, RECT *item_rect) const
{
    if (!item_rect || !_htoolbar || !IsWindow(_htoolbar) || entry._btn_idx < 0)
        return false;

    if (entry._id) {
        int button_index = (int)SendMessage(_htoolbar, TB_COMMANDTOINDEX, entry._id, 0);
        if (button_index < 0)
            return false;

        return SendMessage(_htoolbar, TB_GETITEMRECT, button_index, (LPARAM)item_rect) != FALSE;
    }

    if (!_pending_add_commit || _committing_pending_adds || entry._icon_visibility <= 0.0f)
        return false;

    ClientRect toolbar_client(_htoolbar);
    int slot_width = _last_btn_width > 0 ? _last_btn_width : _preferred_btn_width;
    if (slot_width <= 0)
        slot_width = taskbar_draw::GetModernButtonSlotWidth();

    int origin_left = 0;
    RECT first_rect = { 0 };
    if (SendMessage(_htoolbar, TB_BUTTONCOUNT, 0, 0) > 0 &&
        SendMessage(_htoolbar, TB_GETITEMRECT, 0, (LPARAM)&first_rect)) {
        origin_left = first_rect.left;
        int rect_width = first_rect.right - first_rect.left;
        if (rect_width > 0)
            slot_width = rect_width;
    }

    item_rect->left = origin_left + entry._btn_idx * slot_width;
    item_rect->top = 0;
    item_rect->right = item_rect->left + slot_width;
    item_rect->bottom = toolbar_client.bottom;
    return item_rect->right > item_rect->left && item_rect->bottom > item_rect->top;
}

TaskBar::~TaskBar()
{
    if (g_RegisterShellHook)
        (*g_RegisterShellHook)(_hwnd, RSH_UNREGISTER);

    if (g_DeregisterShellHookWindow)
        (*g_DeregisterShellHookWindow)(_hwnd);
    else
        KillTimer(_hwnd, 0);

    if (g_SetTaskmanWindow)
        (*g_SetTaskmanWindow)(0);

    if (_himl)
        ImageList_Destroy(_himl);

    SystemParametersInfo(SPI_GETMINIMIZEDMETRICS, sizeof(_mmMetrics_org), &_mmMetrics_org, 0);
}

HWND TaskBar::Create(HWND hwndParent)
{
    ClientRect clnt(hwndParent);

    int taskbar_pos = 80;   // This start position will be adjusted in DesktopBar::Resize().
    bool modern_style = taskbar_draw::IsModernTaskbarEnabled();
    int taskbar_y = modern_style ? clnt.top : clnt.top + 1;
    int taskbar_h = modern_style ? clnt.bottom : clnt.bottom - 2;
    static BtnWindowClass wcTaskBar(CLASSNAME_TASKBAR);
    wcTaskBar.hbrBackground = TASKBAR_BRUSH();
    return Window::Create(WINDOW_CREATOR(TaskBar), 0,
                          wcTaskBar, TITLE_TASKBAR,
                          WS_CHILD | WS_VISIBLE | CCS_TOP | CCS_NODIVIDER | CCS_NORESIZE,
                          taskbar_pos, taskbar_y, clnt.right - taskbar_pos - (NOTIFYAREA_WIDTH_DEF + 1), taskbar_h, hwndParent);
}

//#include <Uxtheme.h>

LRESULT TaskBar::Init(LPCREATESTRUCT pcs)
{
    if (super::Init(pcs))
        return 1;

    //hbrTaskLine = GetSysColorBrush(COLOR_BTNFACE);
    COLORREF clrTaskLine = TASKBAR_TASKLINECOLOR();
    if (clrTaskLine != MAXDWORD) {
        hbrTaskLine = CreateSolidBrush(clrTaskLine);
    }

    /* FIXME: There's an internal padding for non-flat toolbar. Get rid of it somehow. */
    DWORD ws = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CCS_TOP | TBSTYLE_TRANSPARENT |
        CCS_NODIVIDER | TBSTYLE_LIST | TBSTYLE_TOOLTIPS | TBSTYLE_FLAT;

    //_htoolbar = CreateToolbarEx(_hwnd, ws /* |TBSTYLE_AUTOSIZE */, IDW_TASKTOOLBAR, 0, 0, 0, NULL,
    //                            0, 0, 0, DESKTOPBARBAR_HEIGHT - 4, DESKTOPBARBAR_HEIGHT, sizeof(TBBUTTON));

    // Create the toolbar.
    _htoolbar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL, ws, 0, 0,
        DESKTOPBARBAR_HEIGHT - 4, DESKTOPBARBAR_HEIGHT, _hwnd, NULL, g_Globals._hInstance, NULL);


    SendMessage(_htoolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
    SendMessage(_htoolbar, TB_SETBUTTONWIDTH, 0, MAKELPARAM(TASKBUTTONWIDTH_MAX, TASKBUTTONWIDTH_MAX));

    _himl = ImageList_Create(_icon_area.right, _icon_area.bottom, ILC_COLOR32, 16, 16);
    if (_himl) {
        ImageList_SetBkColor(_himl, CLR_NONE);
        SendMessage(_htoolbar, TB_SETIMAGELIST, 0, (LPARAM)_himl);
    }

    DWORD toolbar_exstyle = 0;
    if (_no_task_title) {
        // show only icons — full-slot bitmaps, no text display
        toolbar_exstyle = TBSTYLE_EX_MIXEDBUTTONS;
        SendMessage(_htoolbar, TB_SETPADDING, 0, MAKELPARAM(0, 0));
    } else if (_task_close_button) {
        toolbar_exstyle = TBSTYLE_EX_DRAWDDARROWS;
    }

    if (toolbar_exstyle)
        SendMessage(_htoolbar, TB_SETEXTENDEDSTYLE, 0, toolbar_exstyle);

    SendMessage(_htoolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(_icon_area.right, _icon_area.bottom));

    String msstyle_taskbutton = JCFG2_DEF("JS_TASKBAR", "msstyle_taskbutton", TEXT("")).ToString();
    if (_rounded_highlight) {
        SetWindowTheme(_htoolbar, L"", L"");
    } else if (msstyle_taskbutton != TEXT("")) {
        SetWindowTheme(_htoolbar, msstyle_taskbutton, L"Toolbar"); //TaskBar
    }

    //SendMessage(_htoolbar, TB_SETDRAWTEXTFLAGS, DT_CENTER|DT_VCENTER, DT_CENTER|DT_VCENTER);
    //SetWindowFont(_htoolbar, GetStockFont(DEFAULT_GUI_FONT), FALSE);
    //SendMessage(_htoolbar, TB_SETPADDING, 0, MAKELPARAM(8,8));

    HWND hwndToolTip = (HWND)SendMessage(_htoolbar, TB_GETTOOLTIPS, 0, 0);
    SetWindowStyle(hwndToolTip, GetWindowStyle(hwndToolTip) | TTS_ALWAYSTIP);

    // set metrics for the Taskbar toolbar to enable button spacing
    TBMETRICS metrics;

    metrics.cbSize = sizeof(TBMETRICS);
    metrics.dwMask = TBMF_BARPAD | TBMF_BUTTONSPACING;
    metrics.cxBarPad = 0;
    metrics.cyBarPad = (_no_task_title && _rounded_highlight) ? 0 : JCFG_TB(2, "padding-top").ToInt();
    metrics.cxButtonSpacing = (_no_task_title && _rounded_highlight) ? 0 : 1;
    metrics.cyButtonSpacing = 0;

    SendMessage(_htoolbar, TB_SETMETRICS, 0, (LPARAM)&metrics);

    _next_id = IDC_FIRST_APP;

    // register the taskbar window as task manager window to make the following call to RegisterShellHookWindow working
    if (g_SetTaskmanWindow)
        (*g_SetTaskmanWindow)(_hwnd);

    if (g_RegisterShellHookWindow) {
        LOG(TEXT("Using shell hooks for notification of shell events."));

        (*g_RegisterShellHookWindow)(_hwnd);
    } else {
        LOG(TEXT("Shell hooks not available."));

        SetTimer(_hwnd, 0, 200, NULL);
    }

    if (g_RegisterShellHook) {
        (*g_RegisterShellHook)(NULL, RSH_REGISTER);
        (*g_RegisterShellHook)(_hwnd, RSH_TASKMGR);
    }
    Refresh();
    SyncAnimationState(true);

    _thumbnail = JCfg_TaskThumbnailEnabled();
    if (_thumbnail) {
        InitThumbnailWindow(_hwnd, _htoolbar);
    }

    ApplyBackgroundStyle();
    return 0;
}

LRESULT TaskBar::WndProc(UINT nmsg, WPARAM wparam, LPARAM lparam)
{
    switch (nmsg) {
    case WM_SIZE:
        SendMessage(_htoolbar, WM_SIZE, 0, 0);
        ResizeButtons();
        RefreshAnimationTimer();
        // ApplyBackgroundStyle();
        break;

    case PM_REFRESH_CONFIG:
        InitTaskbarStyle();
        ResizeButtons();
        SyncAnimationState(true);
        RefreshAnimationTimer(false);
        InvalidateRect(_hwnd, NULL, FALSE);
        if (_htoolbar)
            InvalidateRect(_htoolbar, NULL, FALSE);
        return 0;

    case WM_TIMER:
        if (wparam == 0) {
            Refresh();
        } else if (wparam == ID_TIMER_ANIMATEBUTTONS) {
            if (!AdvanceAnimations()) {
                KillTimer(_hwnd, ID_TIMER_ANIMATEBUTTONS);
                _animation_timer_running = false;
                RedrawWindow(_htoolbar, NULL, NULL, RDW_INVALIDATE | RDW_NOERASE);
            } else {
                InvalidateAnimatedButtons();
            }
        } else if (wparam == ID_TIMER_DESTORYTHUMBNAIL) {
            KillTimer(_hwnd, ID_TIMER_DESTORYTHUMBNAIL);
            if (!IsThumbnailCursorInRegion())
                DestoryThumbnailWindow();
            else
                SetTimer(_hwnd, ID_TIMER_DESTORYTHUMBNAIL, 200, NULL);
        }
        return 0;

    case WM_CONTEXTMENU: {
        Point pt(lparam);
        ScreenToClient(_htoolbar, &pt);

        if ((HWND)wparam == _htoolbar && SendMessage(_htoolbar, TB_HITTEST, 0, (LPARAM)&pt) >= 0)
            break;  // avoid displaying context menu for application button _and_ desktop bar at the same time

        goto def;
    }

    case PM_GET_LAST_ACTIVE:
        return (LRESULT)(HWND)_last_foreground_wnd;

    case PM_QUERY_GROUP_STATE: {
        TaskbarGroupStateQuery *query = (TaskbarGroupStateQuery *)lparam;
        if (!query || !query->_app_key)
            return 0;

        TaskBarMap::iterator found = _map.find(String(query->_app_key));
        if (found == _map.end() || !found->second._used) {
            query->_primary_hwnd = 0;
            query->_window_count = 0;
            query->_active = FALSE;
            if (query->_windows && query->_window_capacity > 0)
                query->_windows[0] = 0;
            return 0;
        }

        query->_primary_hwnd = found->second._primary_hwnd;
        query->_window_count = (int)found->second._windows.size();

        // Check active by comparing against actual foreground window
        HWND fg = GetForegroundWindow();
        BOOL is_active = FALSE;
        for (size_t wi = 0; wi < found->second._windows.size(); ++wi) {
            if (found->second._windows[wi] == fg) {
                is_active = TRUE;
                break;
            }
            // Also check if foreground is owned by a group window
            if (fg) {
                HWND owner = GetWindow(fg, GW_OWNER);
                if (owner == found->second._windows[wi]) {
                    is_active = TRUE;
                    break;
                }
            }
        }
        query->_active = is_active;

        if (query->_windows && query->_window_capacity > 0) {
            int copy_count = query->_window_count;
            if (copy_count > query->_window_capacity)
                copy_count = query->_window_capacity;

            for (int index = 0; index < copy_count; ++index)
                query->_windows[index] = found->second._windows[index];

            if (copy_count < query->_window_capacity)
                query->_windows[copy_count] = 0;
        }

        return query->_window_count;
    }

    case PM_TASKBAR_COMMIT_PENDING_ADDS:
        if (_pending_add_commit) {
            _committing_pending_adds = true;
            Refresh();
        }
        return 0;

    case PM_GET_WIDTH:
        return GetPreferredWidth();

    case WM_SYSCOLORCHANGE:
        SendMessage(_htoolbar, WM_SYSCOLORCHANGE, 0, 0);
        break;

    default: def:
        if (nmsg == WM_SHELLHOOK) {
            switch (wparam) {
            case HSHELL_WINDOWCREATED:
            case HSHELL_WINDOWDESTROYED:
            case HSHELL_WINDOWACTIVATED:
            case HSHELL_REDRAW:
#ifdef HSHELL_FLASH
            case HSHELL_FLASH:
#endif
/*
already be handled by HSHELL_WINDOWCREATED
#ifdef HSHELL_RUDEAPPACTIVATED
            case HSHELL_RUDEAPPACTIVATED:
#endif
*/
                Refresh();
                break;
            }
        } else {
            return super::WndProc(nmsg, wparam, lparam);
        }
    }

    return 0;
}

int TaskBar::Command(int id, int code)
{
    TaskBarMap::iterator found = _map.find_id(id);

    if (found != _map.end()) {
        if (code == WM_CLOSE) {
            if (found->second._primary_hwnd)
                PostMessage(found->second._primary_hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
        } else if (!found->second._used) {
            LaunchEntry(found);
        } else {
            ActivateApp(found);
        }
        return 0;
    }

    return super::Command(id, code);
}

int TaskBar::Notify(int id, NMHDR *pnmh)
{
    if (pnmh->hwndFrom == _htoolbar) {
        if (g_Globals._isDebug) {
            _log_(FmtString(TEXT("TaskBar::Notify(%d)"), pnmh->code));
        }
        switch (pnmh->code) {
        case NM_RCLICK: {
            TBBUTTONINFO btninfo;
            TaskBarMap::iterator it;
            Point pt(GetMessagePos());
            ScreenToClient(_htoolbar, &pt);

            btninfo.cbSize = sizeof(TBBUTTONINFO);
            btninfo.dwMask = TBIF_BYINDEX | TBIF_COMMAND;

            int idx = (int)SendMessage(_htoolbar, TB_HITTEST, 0, (LPARAM)&pt);

            if (idx >= 0 &&
                SendMessage(_htoolbar, TB_GETBUTTONINFO, idx, (LPARAM)&btninfo) != -1 &&
                (it = _map.find_id(btninfo.idCommand)) != _map.end()) {
                //TaskBarEntry& entry = it->second;

                //ActivateApp(it, false, false);  // don't restore minimized windows on right button click

                static DynamicFct<DWORD(STDAPICALLTYPE *)(RESTRICTIONS)> pSHRestricted(TEXT("SHELL32"), "SHRestricted");
                if (pSHRestricted && !(*pSHRestricted)(REST_NOTRAYCONTEXTMENU))
                    ShowAppSystemMenu(it);
            }
            break;
        }
        case TBN_DROPDOWN: {
            // Get the coordinates of the button.
            Point pt(GetMessagePos());
            ScreenToClient(pnmh->hwndFrom, &pt);
            TBBUTTONINFO btninfo;
            btninfo.cbSize = sizeof(TBBUTTONINFO);
            btninfo.dwMask = TBIF_BYINDEX | TBIF_COMMAND;
            int idx = (int)SendMessage(_htoolbar, TB_HITTEST, 0, (LPARAM)&pt);
            if (idx >= 0 &&
                SendMessage(_htoolbar, TB_GETBUTTONINFO, idx, (LPARAM)&btninfo) != -1) {
                DestoryThumbnailWindow();
                Command(btninfo.idCommand, WM_CLOSE);
            }
            break;
        }
        case NM_CUSTOMDRAW: {
            LPNMTBCUSTOMDRAW lptbcd = (LPNMTBCUSTOMDRAW)pnmh;
            switch (lptbcd->nmcd.dwDrawStage) {
            case CDDS_PREPAINT:
                RefreshAnimationTimer(false);
                return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
            case CDDS_ITEMPREPAINT: {
                lptbcd->clrText = TASKBAR_TEXTCOLOR();
                if (_rounded_highlight) {
                    TaskBarMap::iterator found = _map.find_id((int)lptbcd->nmcd.dwItemSpec);
                    if (found == _map.end() && (int)lptbcd->nmcd.dwItemSpec >= 0) {
                        TBBUTTON button = {0};
                        if (SendMessage(_htoolbar, TB_GETBUTTON, lptbcd->nmcd.dwItemSpec, (LPARAM)&button) != -1)
                            found = _map.find_id(button.idCommand);
                    }

                    float hover_progress = 0.0f;
                    float active_progress = 0.0f;
                    float icon_visibility = 1.0f;
                    if (found != _map.end()) {
                        hover_progress = found->second._hover_progress;
                        active_progress = found->second._active_progress;
                        icon_visibility = found->second._icon_visibility;
                    }
                    if (!_animate_highlights) {
                        hover_progress = ((lptbcd->nmcd.uItemState & CDIS_HOT) == CDIS_HOT) ? 1.0f : 0.0f;
                        active_progress = ((lptbcd->nmcd.uItemState & CDIS_CHECKED) == CDIS_CHECKED) ? 1.0f : 0.0f;
                    }

                    if (UseDirectTaskbarIconDraw(_rounded_highlight, _no_task_title)) {
                        hover_progress *= icon_visibility;
                        active_progress *= icon_visibility;
                    }

                    DrawTaskbarButtonHighlight(lptbcd->nmcd.hdc, lptbcd->nmcd.rc, hover_progress, active_progress);

                    if (UseDirectTaskbarIconDraw(_rounded_highlight, _no_task_title)) {
                        if (found != _map.end()) {
                            DrawDirectTaskbarEntryIcon(lptbcd->nmcd.hdc, lptbcd->nmcd.rc, found->second);
                            DrawTaskbarButtonIndicators(lptbcd->nmcd.hdc, lptbcd->nmcd.rc,
                                found->second._window_group_count, active_progress, icon_visibility);
                        }
                        return CDRF_SKIPDEFAULT;
                    }

                    return TBCDRF_NOBACKGROUND | TBCDRF_NOEDGES | TBCDRF_NOOFFSET |
                        TBCDRF_NOETCHEDEFFECT | CDRF_NOTIFYPOSTPAINT | CDRF_USECDCOLORS;
                }
                return CDRF_NOTIFYPOSTPAINT | CDRF_USECDCOLORS; //Windows vista later
            }
            case CDDS_ITEMPOSTPAINT: {
                if (g_Globals._isDebug) {
                    _log_(FmtString(TEXT("TaskBar::Notify(NM_CUSTOMDRAW) %d"), lptbcd->nmcd.uItemState));
                }

                TaskBarMap::iterator found = _map.find_id((int)lptbcd->nmcd.dwItemSpec);
                if (found == _map.end() && (int)lptbcd->nmcd.dwItemSpec >= 0) {
                    TBBUTTON button = {0};
                    if (SendMessage(_htoolbar, TB_GETBUTTON, lptbcd->nmcd.dwItemSpec, (LPARAM)&button) != -1) {
                        found = _map.find_id(button.idCommand);
                    }
                }
                float hover_progress = 0.0f;
                float active_progress = 0.0f;
                if (found != _map.end()) {
                    hover_progress = found->second._hover_progress;
                    active_progress = found->second._active_progress;
                }
                if (!_animate_highlights) {
                    hover_progress = ((lptbcd->nmcd.uItemState & CDIS_HOT) == CDIS_HOT) ? 1.0f : 0.0f;
                    active_progress = ((lptbcd->nmcd.uItemState & CDIS_CHECKED) == CDIS_CHECKED) ? 1.0f : 0.0f;
                }

                if (_rounded_highlight) {
                    int wnd_count = 0;
                    if (found != _map.end())
                        wnd_count = found->second._window_group_count;

                    DrawTaskbarButtonIndicators(lptbcd->nmcd.hdc, lptbcd->nmcd.rc, wnd_count, active_progress);
                } else if (hbrTaskLine) {
                    RECT rect = lptbcd->nmcd.rc;
                    rect.top = DESKTOPBARBAR_HEIGHT - 4;
                    rect.bottom = rect.top + 2;
                    if (((lptbcd->nmcd.uItemState & CDIS_CHECKED) != CDIS_CHECKED) &&
                        ((lptbcd->nmcd.uItemState & CDIS_HOT) != CDIS_HOT)) {
                        rect.left += 4;
                        rect.right -= 4;
                    } else if (_task_close_button) {
                        rect.left -= 2;
                        rect.right += 2;
                    }
                    FillRect(lptbcd->nmcd.hdc, &rect, hbrTaskLine);
                }
                return CDRF_DODEFAULT;
            }
            case CDDS_POSTPAINT:
                if (_rounded_highlight && UseDirectTaskbarIconDraw(_rounded_highlight, _no_task_title) &&
                    _pending_add_commit && !_committing_pending_adds) {
                    for (TaskBarMap::const_iterator it = _map.begin(); it != _map.end(); ++it) {
                        if (it->second._id || it->second._icon_visibility <= 0.0f)
                            continue;

                        RECT item_rect = { 0 };
                        if (!GetVisualButtonRect(it->second, &item_rect))
                            continue;

                        float active_progress = (it->second._fsState & (TBSTATE_CHECKED | TBSTATE_PRESSED)) ?
                            it->second._icon_visibility : 0.0f;
                        DrawTaskbarButtonHighlight(lptbcd->nmcd.hdc, item_rect, 0.0f, active_progress);
                        DrawDirectTaskbarEntryIcon(lptbcd->nmcd.hdc, item_rect, it->second);
                        DrawTaskbarButtonIndicators(lptbcd->nmcd.hdc, item_rect,
                            it->second._window_group_count, active_progress, it->second._icon_visibility);
                    }
                }
                return CDRF_DODEFAULT;
            default:
                return CDRF_DODEFAULT;
            }
        }
        case TBN_HOTITEMCHANGE: {
            RefreshAnimationTimer();
            if (_thumbnail) {
                LPNMTBHOTITEM hotitem = (LPNMTBHOTITEM)pnmh;

                // If hot item is leaving (no new item), schedule preview cleanup
                if (hotitem->dwFlags & HICF_LEAVING) {
                    // Don't immediately destroy — let the thumbnail module check
                    // if cursor moved to the preview popup
                    SetTimer(_hwnd, ID_TIMER_DESTORYTHUMBNAIL, 300, NULL);
                    return super::Notify(id, pnmh);
                }

                TBBUTTONINFO btninfo;
                TaskBarMap::iterator it;
                Point pt(GetMessagePos());
                ScreenToClient(_htoolbar, &pt);

                btninfo.cbSize = sizeof(TBBUTTONINFO);
                btninfo.dwMask = TBIF_BYINDEX | TBIF_COMMAND;

                int idx = (int)SendMessage(_htoolbar, TB_HITTEST, 0, (LPARAM)&pt);

                if (idx >= 0 &&
                    SendMessage(_htoolbar, TB_GETBUTTONINFO, idx, (LPARAM)&btninfo) != -1 &&
                    (it = _map.find_id(btninfo.idCommand)) != _map.end()) {
                    KillTimer(_hwnd, ID_TIMER_DESTORYTHUMBNAIL);

                    _log_(FmtString(TEXT("TaskBar::Notify(TBN_HOTITEMCHANGE) %d"), idx));
                    if (!it->second._windows.empty())
                        DrawThumbnailWindows(g_Globals._hInstance, &it->second._windows[0], (int)it->second._windows.size(), _htoolbar, idx);
                }
            }
            return super::Notify(id, pnmh);
        }
        case TBN_GETINFOTIPA:
        case TBN_GETINFOTIPW:
            if (_thumbnail) {
                KillTimer(_hwnd, ID_TIMER_DESTORYTHUMBNAIL);
            }
            if (_no_task_title) {
                // Provide tooltip text for icon-only buttons
                NMTBGETINFOTIP *tip = (NMTBGETINFOTIP *)pnmh;
                TaskBarMap::iterator it = _map.find_id(tip->iItem);
                if (it != _map.end() && tip->pszText && tip->cchTextMax > 0) {
                    lstrcpyn(tip->pszText, it->second._title.c_str(), tip->cchTextMax);
                }
            }
            break;
        default:
            _log_(FmtString(TEXT("TaskBar::Notify(%d)"), pnmh->code));
            return super::Notify(id, pnmh);
        }
    }
    return 0;
}


void TaskBar::ActivateApp(TaskBarMap::iterator it, bool can_minimize, bool can_restore)
{
    HWND hwnd = it->second._primary_hwnd;
    if (!hwnd || !IsWindow(hwnd))
        return;

    bool minimize_it = can_minimize && !IsIconic(hwnd) &&
                       (hwnd == GetForegroundWindow() || hwnd == _last_foreground_wnd);

    // switch to selected application window
    if (can_restore && !minimize_it)
        if (IsIconic(hwnd))
            PostMessage(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);

    // In case minimize_it is true, we _have_ to switch to the app before
    // posting SW_MINIMIZE to be compatible with some applications (e.g. "Sleipnir")
    SetForegroundWindow(hwnd);

    if (minimize_it) {
        PostMessage(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
        _last_foreground_wnd = 0;
    } else
        _last_foreground_wnd = hwnd;

    Refresh();
}

void TaskBar::LaunchEntry(TaskBarMap::iterator it)
{
    if (it == _map.end())
        return;

    const TaskBarEntry &entry = it->second;
    String peazip_app_key = taskbar_identity::GetPeaZipAppKey();
    if (!peazip_app_key.empty() && entry._app_key == peazip_app_key) {
        TCHAR peazip_path[MAX_PATH] = { 0 };
        if (TryGetPeaZipPath(peazip_path, COUNTOF(peazip_path))) {
            launch_file(_hwnd, peazip_path, SW_SHOWNORMAL);
            return;
        }
    }

    if (entry._launch_kind == TASKBAR_LAUNCH_EXPLORER) {
        explorer_open_frame(SW_SHOWNORMAL, NULL, EXPLORER_OPEN_QUICKLAUNCH);
    } else if (entry._launch_kind == TASKBAR_LAUNCH_SHORTCUT && !entry._launch_path.empty()) {
        launch_file(_hwnd, entry._launch_path.c_str(), SW_SHOWNORMAL);
    }
}

#define ENABLESYSMENUITEM(m, item, cond) EnableMenuItem((m), (item), \
MF_BYCOMMAND | ((cond)?MF_ENABLED:MF_GRAYED))

void TaskBar::ShowAppSystemMenu(TaskBarMap::iterator it)
{
    HWND hTaskWindow = it->second._primary_hwnd;
    if (!hTaskWindow || !IsWindow(hTaskWindow))
        return;

    HMENU hmenu = GetSystemMenu(hTaskWindow, FALSE);

    if (hmenu) {
        POINT pt;
        GetCursorPos(&pt);

        WINDOWPLACEMENT wndpl;
        wndpl.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(hTaskWindow, &wndpl);
        UINT showCmd = wndpl.showCmd;
        DWORD taskbarThreadID = GetWindowThreadProcessId(_hwnd, NULL);
        DWORD taskWindowThreadID = GetWindowThreadProcessId(hTaskWindow, NULL);

        AttachThreadInput(taskbarThreadID, taskWindowThreadID, TRUE);
        SetWindowPos(hTaskWindow, HWND_TOP, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
        AttachThreadInput(taskbarThreadID, taskWindowThreadID, FALSE);

        if (GetClassLongPtr(hTaskWindow, GCL_STYLE) & CS_NOCLOSE)
            DeleteMenu(hmenu, SC_CLOSE, MF_BYCOMMAND);

        ENABLESYSMENUITEM(hmenu, SC_RESTORE, showCmd != SW_SHOWNORMAL);
        ENABLESYSMENUITEM(hmenu, SC_MOVE, showCmd == SW_SHOWNORMAL);
        ENABLESYSMENUITEM(hmenu, SC_SIZE, showCmd == SW_SHOWNORMAL);
        ENABLESYSMENUITEM(hmenu, SC_MINIMIZE, showCmd != SW_SHOWMINIMIZED);
        ENABLESYSMENUITEM(hmenu, SC_MAXIMIZE, showCmd != SW_SHOWMAXIMIZED);

        SendMessage(hTaskWindow, WM_INITMENUPOPUP, (WPARAM)hmenu, MAKELPARAM(0, TRUE));
        SendMessage(hTaskWindow, WM_INITMENU, (WPARAM)hmenu, 0);

        int cmd = TrackPopupMenu(hmenu, TPM_RETURNCMD | TPM_RECURSE, pt.x, pt.y, 0, _hwnd, NULL);

        if (cmd) {
            //ActivateApp(it, false, false);  // reactivate window after the context menu has closed
            PostMessage(hTaskWindow, WM_SYSCOMMAND, cmd, 0);
        }
    }
}


HICON get_window_icon_small(HWND hwnd)
{
    HICON hIcon = 0;

    SendMessageTimeout(hwnd, WM_GETICON, ICON_SMALL2, 0, SMTO_ABORTIFHUNG, 1000, (PDWORD_PTR)&hIcon);

    if (!hIcon)
        SendMessageTimeout(hwnd, WM_GETICON, ICON_SMALL, 0, SMTO_ABORTIFHUNG, 1000, (PDWORD_PTR)&hIcon);

    if (!hIcon)
        SendMessageTimeout(hwnd, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG, 1000, (PDWORD_PTR)&hIcon);

    if (!hIcon)
        hIcon = (HICON)GetClassLongPtr(hwnd, GCL_HICONSM);

    if (!hIcon)
        hIcon = (HICON)GetClassLongPtr(hwnd, GCL_HICON);

    if (!hIcon)
        SendMessageTimeout(hwnd, WM_QUERYDRAGICON, 0, 0, 0, 1000, (PDWORD_PTR)&hIcon);

    return hIcon;
}

HICON get_window_icon_big(HWND hwnd, bool allow_from_class)
{
    HICON hIcon = 0;

    SendMessageTimeout(hwnd, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG, 1000, (PDWORD_PTR)&hIcon);

    if (!hIcon)
        SendMessageTimeout(hwnd, WM_GETICON, ICON_SMALL2, 0, SMTO_ABORTIFHUNG, 1000, (PDWORD_PTR)&hIcon);

    if (!hIcon)
        SendMessageTimeout(hwnd, WM_GETICON, ICON_SMALL, 0, SMTO_ABORTIFHUNG, 1000, (PDWORD_PTR)&hIcon);

    if (allow_from_class) {
        if (!hIcon)
            hIcon = (HICON)GetClassLongPtr(hwnd, GCL_HICON);

        if (!hIcon)
            hIcon = (HICON)GetClassLongPtr(hwnd, GCL_HICONSM);
    }

    if (!hIcon)
        SendMessageTimeout(hwnd, WM_QUERYDRAGICON, 0, 0, 0, 1000, (PDWORD_PTR)&hIcon);

    return hIcon;
}

HBITMAP TaskBar::CreateEntryBitmap(const TaskBarEntry &entry)
{
    RECT rect = _icon_area;
    WindowCanvas canvas(_htoolbar);
    TaskbarResolvedIcon resolved = ResolveTaskbarEntryIcon(entry);
    HBITMAP hbmp = 0;
    if (resolved._icon)
        hbmp = create_bitmap_from_icon(resolved._icon, NULL, canvas, TASKBAR_ICON_SIZE, rect);
    ReleaseTaskbarResolvedIcon(&resolved);
    return hbmp;
}


static int isTopWindow(HWND hwnd)
{
    if (GetParent(hwnd)) return 0;
    if (GetWindow(hwnd, GW_OWNER)) return 0;
    return 1;
}

// fill task bar with buttons for enumerated top level windows
BOOL CALLBACK TaskBar::EnumWndProc(HWND hwnd, LPARAM lparam)
{
    TaskBar *pThis = (TaskBar *)lparam;

    DWORD style = GetWindowStyle(hwnd);
    DWORD ex_style = GetWindowExStyle(hwnd);

    if ((style & WS_VISIBLE) && !(ex_style & WS_EX_TOOLWINDOW) &&
        ((ex_style & WS_EX_APPWINDOW) | isTopWindow(hwnd))) {
        TCHAR title[BUFFER_LEN] = {0};
        TCHAR strbuffer[BUFFER_LEN] = {0};

        if (!GetWindowText(hwnd, title, BUFFER_LEN))
            title[0] = '\0';

        //do not show 'Windows Shell Experience Host' Window
        //TODO:check not only titlename but processname
        String str_title = title;
        if (str_title.find(TEXT("Windows Shell Experience ")) != String::npos) {
            return TRUE;
        }

        //do not show 'Windows 10's Metro Window
        if (!GetClassName(hwnd, strbuffer, BUFFER_LEN))
            strbuffer[0] = '\0';
        if (IsExcludedTaskbarClass(strbuffer))
            return TRUE;
        str_title = strbuffer;

        // Skip cloaked UWP windows (invisible ApplicationFrameHost placeholders)
        if (str_title.find(TEXT("ApplicationFrameWindow")) != String::npos) {
            DWORD cloaked = 0;
            if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
                return TRUE;
            // Uncloaked ApplicationFrameWindow = real visible UWP app, let it through
        }
        if (str_title.find(TEXT("Windows.UI.Core.CoreWindow")) != String::npos) {
            return TRUE;
        }

        TCHAR process_path[MAX_PATH] = { 0 };
        bool has_process_path = taskbar_identity::GetWindowProcessPath(hwnd, process_path, COUNTOF(process_path));

        String raw_window_app_key = taskbar_identity::GetWindowAppKey(hwnd);
        String app_key = raw_window_app_key;
        String peazip_process_key;
        String pinned_process_key;
        if (has_process_path) {
            peazip_process_key = ResolvePeaZipFamilyAppKey(process_path);
            if (!peazip_process_key.empty())
                app_key = peazip_process_key;

            pinned_process_key = pThis->ResolvePinnedLaunchAppKey(process_path);
            if (!pinned_process_key.empty())
                app_key = pinned_process_key;
        }
        String aliased_app_key = ResolvePinnedAlias(pThis->_pinned_aliases, app_key);

        app_key = aliased_app_key;
        if (app_key.empty())
            return TRUE;

        bool pinned_group = pThis->_pinned_app_keys.find(app_key) != pThis->_pinned_app_keys.end();
        TaskBarMap::iterator found = pThis->_map.find(app_key);
        if (found == pThis->_map.end()) {
            TaskBarEntry entry;
            entry._app_key = app_key;
            entry._pinned = pinned_group;
            pThis->_map[app_key] = entry;
            found = pThis->_map.find(app_key);
        }

        TaskBarEntry &entry = found->second;
        if (entry._used == 0 && !entry._pinned && !HasVisibleOrderKey(pThis->_visible_order, app_key))
            pThis->_visible_order.push_back(app_key);

        ++entry._used;
        entry._windows.push_back(hwnd);
        entry._window_group_count = (int)entry._windows.size();
        entry._pinned = pinned_group;
        GetWindowThreadProcessId(hwnd, &entry._pid);

        if (entry._title.empty() && !entry._pin_title.empty())
            entry._title = entry._pin_title;

        HWND foreground = GetForegroundWindow();
        bool is_active_window = (hwnd == foreground);
        // Also check if foreground is an owned dialog of this window
        if (!is_active_window && foreground) {
            HWND owner = GetWindow(foreground, GW_OWNER);
            if (owner == hwnd)
                is_active_window = true;
        }

        bool prefer_primary = !entry._primary_hwnd || !IsWindow(entry._primary_hwnd);
        if (!prefer_primary && is_active_window)
            prefer_primary = true;
        if (!prefer_primary && entry._primary_hwnd && IsIconic(entry._primary_hwnd) && !IsIconic(hwnd))
            prefer_primary = true;
        if (!prefer_primary && entry._title.empty() && title[0])
            prefer_primary = true;

        if (prefer_primary) {
            entry._primary_hwnd = hwnd;
            if (title[0])
                entry._title = title;
            else if (!entry._pin_title.empty())
                entry._title = entry._pin_title;
        } else if (title[0] && entry._title.empty()) {
            entry._title = title;
        }

        // Only set active flags on the FIRST active window found, not accumulate
        if (is_active_window) {
            entry._fsState = TBSTATE_ENABLED | TBSTATE_PRESSED | TBSTATE_CHECKED;
            pThis->_last_foreground_wnd = hwnd;
        }

#ifdef __REACTOS__  // now handled by activating the ARW_HIDE flag with SystemParametersInfo(SPI_SETMINIMIZEDMETRICS)
        // move minimized windows out of sight
        if (IsIconic(hwnd)) {
            RECT rect;

            GetWindowRect(hwnd, &rect);

            if (rect.bottom > 0)
                SetWindowPos(hwnd, 0, -32000, -32000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
#endif
    }

    return TRUE;
}

void TaskBar::ApplyBackgroundStyle()
{
    static String bkmode = TEXT("-");
    int transparency = 100;
    COLORREF transparency_color = 0;
    if (bkmode == TEXT("")) return;

    if (bkmode == TEXT("-")) {
        bkmode = TASKBAR_GETBKMODE().ToString();
        if (bkmode == TEXT("opaque")) {
            bkmode = TEXT("");
            return;
        }
        transparency = TASKBAR_GETBKTRANSPARENCY(100);
        transparency_color = TASKBAR_GETBKTRANSPARENCYCOLOR();
    }
    TaskbarTransparency(GetParent(_hwnd), bkmode.c_str(), transparency, transparency_color);
}

void TaskBar::LoadPinnedEntries()
{
    _pinned_app_keys.clear();
    _pinned_aliases.clear();

    TCHAR peazip_path[MAX_PATH] = { 0 };
    bool has_peazip = TryGetPeaZipPath(peazip_path, COUNTOF(peazip_path)) != FALSE;
    String peazip_app_key = has_peazip ? taskbar_identity::GetPeaZipAppKey() : String();

    if (!peazip_app_key.empty()) {
        RegisterPinnedAlias(_pinned_aliases, peazip_app_key, taskbar_identity::GetExplorerAppKey());
        RegisterPinnedAlias(_pinned_aliases, peazip_app_key, taskbar_identity::GetExplorerAppIdKey());
    }

    if (!JCFG2_DEF("JS_QUICKLAUNCH", "hide_fileexplorer", false).ToBool()) {
        String app_key = !peazip_app_key.empty() ? peazip_app_key : taskbar_identity::GetExplorerAppKey();
        if (!app_key.empty()) {
            TaskBarEntry &entry = _map[app_key];
            entry._app_key = app_key;
            entry._pinned = true;
            entry._pin_title = !peazip_app_key.empty() ? TEXT("PeaZip") : ResString(IDS_TITLE);
            entry._title = entry._pin_title;
            entry._launch_path = !peazip_app_key.empty() ? peazip_path : TEXT("");
            entry._launch_kind = !peazip_app_key.empty() ? TASKBAR_LAUNCH_SHORTCUT : TASKBAR_LAUNCH_EXPLORER;
            _pinned_app_keys.insert(app_key);
            if (peazip_app_key.empty())
                RegisterPinnedAlias(_pinned_aliases, app_key, taskbar_identity::MakeAppIdKey(TEXT("Microsoft.Windows.Explorer")));
            if (!HasVisibleOrderKey(_visible_order, app_key))
                _visible_order.push_back(app_key);
        }
    }

    if (JCFG2_DEF("JS_QUICKLAUNCH", "hide_usericons", false).ToBool())
        return;

    SpecialFolderFSPath app_data(CSIDL_APPDATA, NULL);
    String pinned_dir;
    pinned_dir.printf(TEXT("%s\\%s"), (LPCTSTR)app_data, QUICKLAUNCH_FOLDER);

    String search_pattern = pinned_dir + TEXT("\\*");
    WIN32_FIND_DATA find_data;
    HANDLE find_handle = FindFirstFile(search_pattern.c_str(), &find_data);
    if (find_handle == INVALID_HANDLE_VALUE)
        return;

    do {
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
            continue;
        if (!_tcsicmp(find_data.cFileName, TEXT("Shows Desktop.lnk")))
            continue;
        if (!_tcsicmp(find_data.cFileName, TEXT("Window Switcher.lnk")))
            continue;

        String shortcut_path = pinned_dir + TEXT("\\") + find_data.cFileName;
        String pin_title = GetPinnedShortcutDisplayName(shortcut_path.c_str());
        String app_key = taskbar_identity::GetShortcutAppKey(shortcut_path.c_str());
        String shortcut_path_key = taskbar_identity::MakePathKey(shortcut_path.c_str());

        String shortcut_app_id = taskbar_identity::ReadShortcutAppId(shortcut_path.c_str());
        TCHAR target_path[MAX_PATH] = { 0 };
        GetShortcutPath(shortcut_path.c_str(), target_path, COUNTOF(target_path));
        String target_path_key;
        if (target_path[0])
            target_path_key = taskbar_identity::MakePathKey(target_path);
        String peazip_target_key = ResolvePeaZipFamilyAppKey(target_path);

        String original_shortcut_path_key = shortcut_path_key;
        String original_shortcut_app_id = shortcut_app_id;
        String original_target_path_key = target_path_key;

        if (IsExplorerPinnedShortcut(shortcut_path.c_str(), pin_title, target_path, shortcut_app_id)) {
            if (!peazip_app_key.empty()) {
                TryRewritePinnedShortcutToPeaZip(shortcut_path.c_str(), peazip_path);

                app_key = taskbar_identity::GetShortcutAppKey(shortcut_path.c_str());
                pin_title = TEXT("PeaZip");
                shortcut_path_key = taskbar_identity::MakePathKey(shortcut_path.c_str());
                shortcut_app_id = taskbar_identity::ReadShortcutAppId(shortcut_path.c_str());
                GetShortcutPath(shortcut_path.c_str(), target_path, COUNTOF(target_path));
                target_path_key = target_path[0] ? taskbar_identity::MakePathKey(target_path) : String();

                if (app_key.empty())
                    app_key = peazip_app_key;
            } else {
                app_key = taskbar_identity::GetExplorerAppKey();
            }
        }

        if (!peazip_target_key.empty())
            app_key = peazip_target_key;

        app_key = ResolvePinnedAlias(_pinned_aliases, app_key);
        shortcut_path_key = ResolvePinnedAlias(_pinned_aliases, shortcut_path_key);
        if (!target_path_key.empty())
            target_path_key = ResolvePinnedAlias(_pinned_aliases, target_path_key);
        if (!shortcut_app_id.empty())
            shortcut_app_id = ResolvePinnedAlias(_pinned_aliases, shortcut_app_id);

        if (app_key.empty())
            continue;

        if (_pinned_app_keys.find(app_key) != _pinned_app_keys.end()) {
            RegisterPinnedAlias(_pinned_aliases, app_key, original_shortcut_path_key);
            RegisterPinnedAlias(_pinned_aliases, app_key, original_shortcut_app_id);
            RegisterPinnedAlias(_pinned_aliases, app_key, original_target_path_key);
            RegisterPinnedAlias(_pinned_aliases, app_key, shortcut_path_key);
            RegisterPinnedAlias(_pinned_aliases, app_key, shortcut_app_id);
            RegisterPinnedAlias(_pinned_aliases, app_key, target_path_key);
            continue;
        }

        TaskBarEntry &entry = _map[app_key];
        entry._app_key = app_key;
        entry._pinned = true;
        entry._pin_title = pin_title;
        entry._title = entry._pin_title;
        entry._launch_path = shortcut_path;
        entry._launch_kind = TASKBAR_LAUNCH_SHORTCUT;
        _pinned_app_keys.insert(app_key);
        RegisterPinnedAlias(_pinned_aliases, app_key, original_shortcut_path_key);
        RegisterPinnedAlias(_pinned_aliases, app_key, original_shortcut_app_id);
        RegisterPinnedAlias(_pinned_aliases, app_key, original_target_path_key);
        RegisterPinnedAlias(_pinned_aliases, app_key, shortcut_path_key);
        RegisterPinnedAlias(_pinned_aliases, app_key, shortcut_app_id);
        RegisterPinnedAlias(_pinned_aliases, app_key, target_path_key);
        if (!HasVisibleOrderKey(_visible_order, app_key))
            _visible_order.push_back(app_key);
    } while (FindNextFile(find_handle, &find_data));

    FindClose(find_handle);
}

void TaskBar::Refresh()
{
    vector<String> previous_order = _visible_order;

    for (TaskBarMap::iterator it = _map.begin(); it != _map.end(); ++it) {
        it->second._used = 0;
        it->second._window_group_count = 0;
        it->second._primary_hwnd = 0;
        it->second._fsState = TBSTATE_ENABLED;
        it->second._pinned = false;
        it->second._title = TEXT("");
        it->second._pin_title = TEXT("");
        it->second._launch_path = TEXT("");
        it->second._launch_kind = TASKBAR_LAUNCH_NONE;
        it->second._windows.clear();
    }

    _visible_order.clear();
    LoadPinnedEntries();
    for (size_t index = 0; index < previous_order.size(); ++index) {
        if (_pinned_app_keys.find(previous_order[index]) == _pinned_app_keys.end() &&
            !HasVisibleOrderKey(_visible_order, previous_order[index])) {
            _visible_order.push_back(previous_order[index]);
        }
    }

    EnumWindows(EnumWndProc, (LPARAM)this);
    MergePinnedProcessMatches();
    //EnumDesktopWindows(GetThreadDesktop(GetCurrentThreadId()), EnumWndProc, (LPARAM)_htoolbar);

    vector<String> desired_visible_order;
    bool rebuild_buttons = false;
    bool direct_icon_draw = UseDirectTaskbarIconDraw(_rounded_highlight, _no_task_title);
    bool incremental_button_updates = _centered_layout && direct_icon_draw;
    bool animate_taskbar_icons = incremental_button_updates && _animate_highlights;
    bool defer_reflow_repaint = false;

    for (size_t index = 0; index < _visible_order.size(); ++index) {
        TaskBarMap::iterator found = _map.find(_visible_order[index]);
        if (found == _map.end())
            continue;

        TaskBarEntry &entry = found->second;
        bool currently_visible = entry._pinned || entry._used;
        if (!currently_visible) {
            if (animate_taskbar_icons && entry._id && !entry._pending_remove) {
                if (!entry._icon_animating_out) {
                    entry._icon_animating_in = false;
                    entry._icon_animating_out = true;
                    entry._icon_visibility = 1.0f;
                }
            } else {
                continue;
            }
        } else {
            if (entry._pending_remove)
                entry._pending_remove = false;

            if (entry._icon_animating_out) {
                entry._icon_animating_out = false;
                entry._icon_visibility = 1.0f;
            }

            if (!entry._id) {
                if (animate_taskbar_icons && !previous_order.empty() && !HasVisibleOrderKey(previous_order, _visible_order[index])) {
                    entry._icon_visibility = 0.0f;
                    entry._icon_animating_in = true;
                } else {
                    entry._icon_visibility = 1.0f;
                    entry._icon_animating_in = false;
                }
            } else if (!entry._icon_animating_in) {
                entry._icon_visibility = 1.0f;
            }
        }

        if (entry._title.empty() && !entry._pin_title.empty())
            entry._title = entry._pin_title;

        entry._btn_idx = (int)desired_visible_order.size();
        desired_visible_order.push_back(_visible_order[index]);
        if (!entry._id)
            rebuild_buttons = true;
    }

    if (!rebuild_buttons) {
        if (desired_visible_order.size() != previous_order.size()) {
            rebuild_buttons = true;
        } else {
            for (size_t index = 0; index < desired_visible_order.size(); ++index) {
                if (desired_visible_order[index] != previous_order[index]) {
                    rebuild_buttons = true;
                    break;
                }
            }
        }
    }

    vector<int> added_button_indices;
    vector<int> removed_button_indices;
    bool add_only_rebuild = rebuild_buttons && incremental_button_updates &&
        TryGetAdditionOnlyIndices(previous_order, desired_visible_order, &added_button_indices);
    if (add_only_rebuild) {
        size_t add_index = 0;
        for (size_t index = 0; index < desired_visible_order.size(); ++index) {
            bool is_added = add_index < added_button_indices.size() && added_button_indices[add_index] == (int)index;
            TaskBarMap::iterator found = _map.find(desired_visible_order[index]);
            if (found == _map.end()) {
                add_only_rebuild = false;
                break;
            }

            if (is_added) {
                if (found->second._id)
                    add_only_rebuild = false;
                ++add_index;
            } else if (!found->second._id) {
                add_only_rebuild = false;
                break;
            }
        }
    }

    bool remove_only_rebuild = rebuild_buttons && incremental_button_updates &&
        TryGetRemovalOnlyIndices(previous_order, desired_visible_order, &removed_button_indices);
    if (remove_only_rebuild) {
        for (size_t index = 0; index < desired_visible_order.size(); ++index) {
            TaskBarMap::iterator found = _map.find(desired_visible_order[index]);
            if (found == _map.end() || !found->second._id) {
                remove_only_rebuild = false;
                break;
            }
        }
    }

    bool trailing_add_only_rebuild = add_only_rebuild && AreTrailingAddedIndices(previous_order, added_button_indices);

    bool pending_add_reservation_cleared = false;
    if (_pending_add_commit && !_committing_pending_adds && !trailing_add_only_rebuild) {
        ClearPendingAddReservation();
        pending_add_reservation_cleared = true;
    }

    bool defer_add_only_commit = trailing_add_only_rebuild && !_committing_pending_adds;
    bool pending_add_reservation_changed = false;
    if (defer_add_only_commit) {
        int reserved_button_count = (int)desired_visible_order.size();
        pending_add_reservation_changed = !_pending_add_commit || _pending_reserved_button_count != reserved_button_count;
        _pending_add_commit = true;
        _pending_reserved_button_count = reserved_button_count;
    }

    bool request_parent_reflow = _centered_layout &&
        (pending_add_reservation_cleared || pending_add_reservation_changed ||
            (rebuild_buttons && !defer_add_only_commit && !_committing_pending_adds));

    if (rebuild_buttons && !defer_add_only_commit) {
        DestoryThumbnailWindow();
        RECT partial_redraw_rect = { 0 };
        bool use_partial_redraw = false;
        int first_changed_index = -1;
        if (add_only_rebuild && !added_button_indices.empty())
            first_changed_index = added_button_indices[0];
        else if (remove_only_rebuild && !removed_button_indices.empty())
            first_changed_index = removed_button_indices[0];

        defer_reflow_repaint = false;

        if (first_changed_index >= 0) {
            ClientRect toolbar_client(_htoolbar);
            partial_redraw_rect.left = 0;
            partial_redraw_rect.top = 0;
            partial_redraw_rect.right = toolbar_client.right;
            partial_redraw_rect.bottom = toolbar_client.bottom;
        }

        SendMessage(_htoolbar, WM_SETREDRAW, FALSE, 0);
        if (add_only_rebuild) {
            for (size_t index = 0; index < desired_visible_order.size(); ++index) {
                TaskBarMap::iterator found = _map.find(desired_visible_order[index]);
                if (found == _map.end())
                    continue;

                TaskBarEntry &entry = found->second;
                entry._btn_idx = (int)index;
                if (entry._id)
                    continue;

                entry._bmp_idx = -2;
                entry._id = _next_id++;

                BYTE button_state = entry._used ? entry._fsState : (BYTE)TBSTATE_ENABLED;
                TBBUTTON btn = { -2, entry._id, button_state, BTNS_BUTTON, {0, 0}, 0, 0 };
                if (_task_close_button && entry._used)
                    btn.fsStyle = BTNS_DROPDOWN;
                if (entry._title.length() && !_no_task_title)
                    btn.iString = (INT_PTR)entry._title.c_str();

                btn.iBitmap = -2;
                SendMessage(_htoolbar, TB_INSERTBUTTON, entry._btn_idx, (LPARAM)&btn);
            }
        } else if (remove_only_rebuild) {
            for (int removed_index = (int)removed_button_indices.size() - 1; removed_index >= 0; --removed_index) {
                int toolbar_index = removed_button_indices[removed_index];
                if (toolbar_index < 0)
                    continue;

                if ((size_t)toolbar_index < previous_order.size()) {
                    TaskBarMap::iterator removed_found = _map.find(previous_order[toolbar_index]);
                    if (removed_found != _map.end()) {
                        removed_found->second._id = 0;
                        removed_found->second._btn_idx = 0;
                        removed_found->second._bmp_idx = 0;
                    }
                }

                SendMessage(_htoolbar, TB_DELETEBUTTON, toolbar_index, 0);
            }

            for (size_t index = 0; index < desired_visible_order.size(); ++index) {
                TaskBarMap::iterator found = _map.find(desired_visible_order[index]);
                if (found == _map.end())
                    continue;

                found->second._btn_idx = (int)index;
            }
        } else {
            // Build set of keys that will remain visible
            set<String> visible_keys;
            for (size_t i = 0; i < desired_visible_order.size(); ++i)
                visible_keys.insert(desired_visible_order[i]);

            for (TaskBarMap::iterator it = _map.begin(); it != _map.end(); ++it) {
                // Only delete bitmaps for entries that won't be visible (prevents blank icons)
                if (visible_keys.find(it->first) == visible_keys.end()) {
                    if (it->second._hbmp)
                        DeleteObject(it->second._hbmp);
                    it->second._hbmp = 0;
                }
                it->second._bmp_idx = 0;
                it->second._btn_idx = 0;
                it->second._id = 0;
            }

            int button_count = (int)SendMessage(_htoolbar, TB_BUTTONCOUNT, 0, 0);
            while (button_count-- > 0)
                SendMessage(_htoolbar, TB_DELETEBUTTON, 0, 0);

            if (_himl)
                ImageList_RemoveAll(_himl);

            _next_id = IDC_FIRST_APP;

            for (size_t index = 0; index < desired_visible_order.size(); ++index) {
                TaskBarMap::iterator found = _map.find(desired_visible_order[index]);
                if (found == _map.end())
                    continue;

                TaskBarEntry &entry = found->second;
                HBITMAP hbmp = 0;
                if (!direct_icon_draw) {
                    hbmp = entry._hbmp ? entry._hbmp : CreateEntryBitmap(entry);
                    if (_himl)
                        entry._bmp_idx = ImageList_Add(_himl, hbmp, 0);
                    if (!_himl || entry._bmp_idx == -1) {
                        TBADDBITMAP ab = {0, (UINT_PTR)hbmp};
                        entry._bmp_idx = (int)SendMessage(_htoolbar, TB_ADDBITMAP, 1, (LPARAM)&ab);
                    }
                } else {
                    if (entry._hbmp)
                        DeleteObject(entry._hbmp);
                    entry._bmp_idx = -2;
                }

                entry._hbmp = hbmp;
                entry._id = _next_id++;

                BYTE button_state = entry._used ? entry._fsState : (BYTE)TBSTATE_ENABLED;
                TBBUTTON btn = { -2, entry._id, button_state, BTNS_BUTTON, {0, 0}, 0, 0 };
                if (_task_close_button && entry._used)
                    btn.fsStyle = BTNS_DROPDOWN;
                if (entry._title.length() && !_no_task_title)
                    btn.iString = (INT_PTR)entry._title.c_str();

                btn.iBitmap = direct_icon_draw ? -2 : entry._bmp_idx;
                entry._btn_idx = (int)index;
                SendMessage(_htoolbar, TB_INSERTBUTTON, entry._btn_idx, (LPARAM)&btn);
            }
        }

        SendMessage(_htoolbar, WM_SETREDRAW, TRUE, 0);
        if (first_changed_index >= 0) {
            int query_index = first_changed_index;
            int button_count = (int)SendMessage(_htoolbar, TB_BUTTONCOUNT, 0, 0);
            if (button_count > 0) {
                if (query_index >= button_count)
                    query_index = button_count - 1;

                RECT item_rect = { 0 };
                if (SendMessage(_htoolbar, TB_GETITEMRECT, query_index, (LPARAM)&item_rect)) {
                    partial_redraw_rect.left = item_rect.left;
                    use_partial_redraw = partial_redraw_rect.right > partial_redraw_rect.left &&
                        partial_redraw_rect.bottom > partial_redraw_rect.top;
                }
            }
        }

        if (!defer_reflow_repaint) {
            if (use_partial_redraw)
                RedrawWindow(_htoolbar, &partial_redraw_rect, NULL, RDW_INVALIDATE | RDW_NOERASE | RDW_UPDATENOW);
            else
                InvalidateRect(_htoolbar, NULL, FALSE);
        }
    }

    const vector<String> &toolbar_state_order = defer_add_only_commit ? previous_order : desired_visible_order;
    for (size_t index = 0; index < toolbar_state_order.size(); ++index) {
        TaskBarMap::iterator found = _map.find(toolbar_state_order[index]);
        if (found == _map.end() || !found->second._id)
            continue;

        TaskBarEntry &entry = found->second;
        BYTE state = entry._used ? entry._fsState : TBSTATE_ENABLED;
        SendMessage(_htoolbar, TB_SETSTATE, entry._id, MAKELONG(state, 0));

        if (!_no_task_title) {
            TBBUTTONINFO info;
            info.cbSize = sizeof(TBBUTTONINFO);
            info.dwMask = TBIF_TEXT;
            info.pszText = (LPTSTR)(entry._title.length() ? entry._title.c_str() : TEXT(""));
            SendMessage(_htoolbar, TB_SETBUTTONINFO, entry._id, (LPARAM)&info);
        }
    }

    set<String> keys_to_delete;

    for (TaskBarMap::iterator it = _map.begin(); it != _map.end(); ++it) {
        if (!it->second._used && !it->second._pinned && !it->second._icon_animating_out)
            keys_to_delete.insert(it->first);
    }

    for (set<String>::const_iterator it = keys_to_delete.begin(); it != keys_to_delete.end(); ++it) {
        TaskBarMap::iterator found = _map.find(*it);
        if (found != _map.end()) {
            DeleteBitmap(found->second._hbmp);
            _map.erase(found);
        }
    }

    if (!defer_add_only_commit)
        _visible_order.swap(desired_visible_order);

    ResizeButtons();

    if ((!defer_add_only_commit && rebuild_buttons) || !_animate_highlights)
        SyncAnimationState(true);
    RefreshAnimationTimer(!defer_reflow_repaint);

    if (_committing_pending_adds)
        ClearPendingAddReservation();

    // Trigger parent resize so centered layout recalculates with new button count
    if (request_parent_reflow) {
        HWND parent = GetParent(_hwnd);
        if (parent) {
            RECT rc;
            GetClientRect(parent, &rc);
            PostMessage(parent, PM_RESIZE_CHILDREN, rc.right, rc.bottom);
        }
    }
}

TaskBarMap::iterator TaskBarMap::find_id(int id)
{
    for (iterator it = begin(); it != end(); ++it)
        if (it->second._id == id)
            return it;

    return end();
}

void TaskBar::ResizeButtons()
{
    int btns = 0;
    bool direct_icon_draw = UseDirectTaskbarIconDraw(_rounded_highlight, _no_task_title);
    for (TaskBarMap::const_iterator it = _map.begin(); it != _map.end(); ++it)
        if (it->second._id)
            ++btns;

    if (btns > 0) {
        int bar_width = ClientRect(_hwnd).right;
        if (_task_close_button)
            bar_width -= btns * 20;

        int btn_width = _preferred_btn_width;
        if (_no_task_title || _centered_layout) {
            int fit_width = bar_width / btns;
            if (fit_width < btn_width)
                btn_width = fit_width;
        } else {
            btn_width = (bar_width / btns) - 3;
        }

        int min_btn_width = TASKBUTTONWIDTH_MIN;
        if (direct_icon_draw)
            min_btn_width = taskbar_draw::GetModernButtonSlotWidth();
        else if (_no_task_title)
            min_btn_width = TASKBAR_ICON_SIZE + DPI_SX(4);

        if (btn_width < min_btn_width)
            btn_width = min_btn_width;
        else if (btn_width > TASKBUTTONWIDTH_MAX)
            btn_width = TASKBUTTONWIDTH_MAX;

        if (btn_width != _last_btn_width) {
            _last_btn_width = btn_width;

            SendMessage(_htoolbar, TB_SETBUTTONWIDTH, 0, MAKELONG(btn_width, btn_width));
            SendMessage(_htoolbar, TB_AUTOSIZE, 0, 0);
        }

        if (direct_icon_draw && _centered_layout && _htoolbar) {
            for (TaskBarMap::const_iterator it = _map.begin(); it != _map.end(); ++it) {
                if (!it->second._id || it->second._btn_idx < 0)
                    continue;

                TBBUTTONINFO btninfo = { 0 };
                btninfo.cbSize = sizeof(TBBUTTONINFO);
                btninfo.dwMask = TBIF_BYINDEX | TBIF_SIZE;
                btninfo.cx = btn_width;
                SendMessage(_htoolbar, TB_SETBUTTONINFO, it->second._btn_idx, (LPARAM)&btninfo);
            }
            SendMessage(_htoolbar, TB_AUTOSIZE, 0, 0);
        }
    }
}

int TaskBar::GetPreferredWidth() const
{
    bool direct_icon_draw = UseDirectTaskbarIconDraw(_rounded_highlight, _no_task_title);
    if (_htoolbar && IsWindow(_htoolbar)) {
        if (_centered_layout && direct_icon_draw) {
            int max_right = 0;
            int min_left = 0;
            bool have_rect = false;
            int actual_button_count = 0;

            for (TaskBarMap::const_iterator it = _map.begin(); it != _map.end(); ++it) {
                if (!it->second._id || it->second._btn_idx < 0)
                    continue;

                ++actual_button_count;

                RECT item_rect = { 0 };
                if (!SendMessage(_htoolbar, TB_GETITEMRECT, it->second._btn_idx, (LPARAM)&item_rect))
                    continue;

                if (!have_rect || item_rect.left < min_left)
                    min_left = item_rect.left;
                if (!have_rect || item_rect.right > max_right)
                    max_right = item_rect.right;

                have_rect = true;
            }

            int current_width = 0;
            if (have_rect)
                current_width = max_right + max(DPI_SX(2), min_left);

            if (_pending_add_commit && !_committing_pending_adds && _pending_reserved_button_count > actual_button_count) {
                int logical_btn_width = _preferred_btn_width;
                if (_last_btn_width > 0 && _last_btn_width < logical_btn_width)
                    logical_btn_width = _last_btn_width;
                if (logical_btn_width <= 0)
                    logical_btn_width = taskbar_draw::GetModernButtonSlotWidth();

                if (current_width > 0)
                    return current_width + (_pending_reserved_button_count - actual_button_count) * logical_btn_width;

                return _pending_reserved_button_count * logical_btn_width;
            }

            if (current_width > 0)
                return current_width;
        } else {
            SIZE max_size = { 0 };
            if (SendMessage(_htoolbar, TB_GETMAXSIZE, 0, (LPARAM)&max_size) && max_size.cx > 0)
                return max_size.cx + DPI_SX(4);
        }
    }

    int btns = 0;
    for (TaskBarMap::const_iterator it = _map.begin(); it != _map.end(); ++it)
        if (it->second._id)
            ++btns;

    if (btns <= 0)
        return 0;

    int btn_width = _preferred_btn_width;
    if (_last_btn_width > 0 && _last_btn_width < btn_width)
        btn_width = _last_btn_width;

    return btns * btn_width;
}

bool TaskBar::HasRunningButtons() const
{
    for (TaskBarMap::const_iterator it = _map.begin(); it != _map.end(); ++it)
        if (it->second._id)
            return true;

    return false;
}

bool TaskBar::IsAnimationRequired() const
{
    if (!_htoolbar)
        return false;

    bool animate_taskbar_icons = _animate_highlights && _centered_layout && UseDirectTaskbarIconDraw(_rounded_highlight, _no_task_title);
    if (!_animate_highlights && !animate_taskbar_icons)
        return false;

    int hot_index = (int)SendMessage(_htoolbar, TB_GETHOTITEM, 0, 0);
    for (TaskBarMap::const_iterator it = _map.begin(); it != _map.end(); ++it) {
        bool pending_virtual_add = !it->second._id && _pending_add_commit && !_committing_pending_adds &&
            it->second._icon_visibility < 1.0f;
        if (!it->second._id && !pending_virtual_add)
            continue;

        LONG state = it->second._id ? (LONG)SendMessage(_htoolbar, TB_GETSTATE, it->second._id, 0) : 0;
        float target_hover = it->second._id && it->second._btn_idx == hot_index ? 1.0f : 0.0f;
        float target_active = ((state & TBSTATE_CHECKED) || (state & TBSTATE_PRESSED)) ? 1.0f : 0.0f;

        float hover_diff = it->second._hover_progress - target_hover;
        if (hover_diff < 0.0f)
            hover_diff = -hover_diff;

        float active_diff = it->second._active_progress - target_active;
        if (active_diff < 0.0f)
            active_diff = -active_diff;

        if (animate_taskbar_icons && (it->second._icon_animating_in || it->second._icon_animating_out)) {
            float target_visibility = it->second._icon_animating_out ? 0.0f : 1.0f;
            float visibility_diff = it->second._icon_visibility - target_visibility;
            if (visibility_diff < 0.0f)
                visibility_diff = -visibility_diff;

            if (visibility_diff > 0.01f)
                return true;
        }

        if (hover_diff > 0.01f || active_diff > 0.01f)
            return true;
    }

    return false;
}

bool TaskBar::AdvanceAnimations()
{
    if (!_htoolbar)
        return false;

    bool needs_more = false;
    bool needs_refresh = false;
    bool animate_taskbar_icons = _animate_highlights && _centered_layout && UseDirectTaskbarIconDraw(_rounded_highlight, _no_task_title);
    double now_ms = GetTaskbarAnimationClockMilliseconds();
    double delta_ms = 16.0;
    if (_last_animation_clock_ms > 0.0 && now_ms > _last_animation_clock_ms)
        delta_ms = now_ms - _last_animation_clock_ms;
    _last_animation_clock_ms = now_ms;

    if (delta_ms < 1.0)
        delta_ms = 1.0;
    if (delta_ms > 80.0)
        delta_ms = 80.0;

    float blend = taskbar_draw::GetAnimationBlend();
    float frame_scale = (float)(delta_ms / 16.0);
    float adjusted_blend = 1.0f - (float)pow(1.0f - blend, frame_scale);
    if (adjusted_blend < 0.05f)
        adjusted_blend = 0.05f;
    if (adjusted_blend > 0.68f)
        adjusted_blend = 0.68f;

    // Keep icon visibility transitions gentler than hover/active to avoid pop/flash during group reflow.
    float icon_blend = adjusted_blend;
    if (icon_blend > 0.38f)
        icon_blend = 0.38f;
    int hot_index = (int)SendMessage(_htoolbar, TB_GETHOTITEM, 0, 0);

    for (TaskBarMap::iterator it = _map.begin(); it != _map.end(); ++it) {
        bool pending_virtual_add = !it->second._id && _pending_add_commit && !_committing_pending_adds &&
            it->second._icon_animating_in;
        if (!it->second._id && !pending_virtual_add)
            continue;

        LONG state = it->second._id ? (LONG)SendMessage(_htoolbar, TB_GETSTATE, it->second._id, 0) : 0;
        float target_hover = it->second._id && it->second._btn_idx == hot_index ? 1.0f : 0.0f;
        float target_active = ((state & TBSTATE_CHECKED) || (state & TBSTATE_PRESSED)) ? 1.0f : 0.0f;

        if (it->second._id) {
            it->second._hover_progress = taskbar_draw::EaseTowards(it->second._hover_progress, target_hover, adjusted_blend);
            it->second._active_progress = taskbar_draw::EaseTowards(it->second._active_progress, target_active, adjusted_blend);
        } else {
            it->second._hover_progress = 0.0f;
            it->second._active_progress = target_active;
        }

        float hover_diff = it->second._hover_progress - target_hover;
        if (hover_diff < 0.0f)
            hover_diff = -hover_diff;

        float active_diff = it->second._active_progress - target_active;
        if (active_diff < 0.0f)
            active_diff = -active_diff;

        if (animate_taskbar_icons && (it->second._icon_animating_in || it->second._icon_animating_out)) {
            float target_visibility = it->second._icon_animating_out ? 0.0f : 1.0f;
            it->second._icon_visibility = taskbar_draw::EaseTowards(it->second._icon_visibility, target_visibility, icon_blend);

            float visibility_diff = it->second._icon_visibility - target_visibility;
            if (visibility_diff < 0.0f)
                visibility_diff = -visibility_diff;

            if (visibility_diff > 0.01f) {
                needs_more = true;
            } else {
                it->second._icon_visibility = target_visibility;
                if (it->second._icon_animating_in)
                    it->second._icon_animating_in = false;
                if (it->second._icon_animating_out) {
                    it->second._icon_animating_out = false;
                    it->second._pending_remove = true;
                    needs_refresh = true;
                }
            }
        }

        if (hover_diff > 0.01f || active_diff > 0.01f)
            needs_more = true;
    }

    if (needs_refresh) {
        Refresh();
        return IsAnimationRequired();
    }

    return needs_more;
}

void TaskBar::InvalidateAnimatedButtons(bool fallback_to_full)
{
    if (!_htoolbar)
        return;

    RECT dirty_rect = { 0 };
    bool has_dirty_rect = false;
    int hot_index = (int)SendMessage(_htoolbar, TB_GETHOTITEM, 0, 0);
    bool animate_taskbar_icons = _animate_highlights && _centered_layout && UseDirectTaskbarIconDraw(_rounded_highlight, _no_task_title);

    for (TaskBarMap::const_iterator it = _map.begin(); it != _map.end(); ++it) {
        bool pending_virtual_add = !it->second._id && _pending_add_commit && !_committing_pending_adds &&
            it->second._icon_visibility < 1.0f;
        if (!it->second._id && !pending_virtual_add)
            continue;

        LONG state = it->second._id ? (LONG)SendMessage(_htoolbar, TB_GETSTATE, it->second._id, 0) : 0;
        float target_hover = it->second._id && it->second._btn_idx == hot_index ? 1.0f : 0.0f;
        float target_active = ((state & TBSTATE_CHECKED) || (state & TBSTATE_PRESSED)) ? 1.0f : 0.0f;

        float hover_diff = it->second._hover_progress - target_hover;
        if (hover_diff < 0.0f)
            hover_diff = -hover_diff;

        float active_diff = it->second._active_progress - target_active;
        if (active_diff < 0.0f)
            active_diff = -active_diff;

        bool redraw_entry = (_animate_highlights && (hover_diff > 0.01f || active_diff > 0.01f));
        if (!redraw_entry && animate_taskbar_icons)
            redraw_entry = it->second._icon_animating_in || it->second._icon_animating_out;

        if (!redraw_entry)
            continue;

        RECT item_rect = { 0 };
        if (!GetVisualButtonRect(it->second, &item_rect))
            continue;

        if (!has_dirty_rect) {
            dirty_rect = item_rect;
            has_dirty_rect = true;
        } else {
            UnionRect(&dirty_rect, &dirty_rect, &item_rect);
        }
    }

    if (has_dirty_rect) {
        RedrawWindow(_htoolbar, &dirty_rect, NULL, RDW_INVALIDATE | RDW_NOERASE);
    } else if (fallback_to_full) {
        RedrawWindow(_htoolbar, NULL, NULL, RDW_INVALIDATE | RDW_NOERASE);
    }
}

void TaskBar::RefreshAnimationTimer(bool invalidate)
{
    if (!_animate_highlights)
        return;

    if (IsAnimationRequired()) {
        if (!_animation_timer_running) {
            SetTimer(_hwnd, ID_TIMER_ANIMATEBUTTONS, 16, NULL);
            _animation_timer_running = true;
            _last_animation_clock_ms = GetTaskbarAnimationClockMilliseconds();
        }
        if (invalidate)
            InvalidateAnimatedButtons(true);
    } else if (_animation_timer_running) {
        KillTimer(_hwnd, ID_TIMER_ANIMATEBUTTONS);
        _animation_timer_running = false;
        _last_animation_clock_ms = 0.0;
    }
}

void TaskBar::SyncAnimationState(bool snap_to_target)
{
    if (!_htoolbar)
        return;

    bool needs_refresh = false;

    int hot_index = (int)SendMessage(_htoolbar, TB_GETHOTITEM, 0, 0);
    for (TaskBarMap::iterator it = _map.begin(); it != _map.end(); ++it) {
        LONG state = it->second._id ? (LONG)SendMessage(_htoolbar, TB_GETSTATE, it->second._id, 0) : 0;
        float target_hover = it->second._id && it->second._btn_idx == hot_index ? 1.0f : 0.0f;
        float target_active = it->second._id ?
            (((state & TBSTATE_CHECKED) || (state & TBSTATE_PRESSED)) ? 1.0f : 0.0f) : 0.0f;
        if (snap_to_target || !_animate_highlights) {
            it->second._hover_progress = target_hover;
            it->second._active_progress = target_active;
        }

        if (!_animate_highlights) {
            if (it->second._icon_animating_in) {
                it->second._icon_animating_in = false;
                it->second._icon_visibility = 1.0f;
            }

            if (it->second._icon_animating_out) {
                it->second._icon_animating_out = false;
                it->second._icon_visibility = 0.0f;
                it->second._pending_remove = true;
                needs_refresh = true;
            }
        }
    }

    if (needs_refresh)
        Refresh();
}