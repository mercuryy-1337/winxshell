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
#include "../utility/taskbar_draw.h"

#include <Uxtheme.h>
#include <dwmapi.h>
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")


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

static void BuildPinnedTaskbarAppKeySet(set<String> &keys)
{
    keys.clear();

    if (!JCFG2_DEF("JS_QUICKLAUNCH", "hide_fileexplorer", false).ToBool())
        keys.insert(taskbar_identity::GetExplorerAppKey());

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
        String app_key = taskbar_identity::GetShortcutAppKey(shortcut_path.c_str());
        if (!app_key.empty())
            keys.insert(app_key);
    } while (FindNextFile(find_handle, &find_data));

    FindClose(find_handle);
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
    int btn_h = item_rect.bottom - item_rect.top;
    int hl_pad_y = DPI_SY(4);
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
    _pid = 0;
    _window_group_count = 0;
    _primary_hwnd = 0;
    _pinned = false;
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

    bool prefer_icon_only = _centered_layout || _rounded_highlight;
    if (JCFG2_DEF("JS_TASKBAR", "no_task_title", prefer_icon_only).ToBool() != FALSE) {
        _no_task_title = true;
        _icon_area.left = 0;
        _icon_area.top = 0;
        _icon_area.right = _preferred_btn_width;
        _icon_area.bottom = DESKTOPBARBAR_HEIGHT;
    } else {
        if (!_rounded_highlight && JCFG2_DEF("JS_TASKBAR", "task_close_button", false).ToBool() != FALSE) {
            _task_close_button = true;
        }
    }

    if (clrTaskLine != MAXDWORD) {
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
    _last_btn_width = 0;

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

    SystemParametersInfo(SPI_GETMINIMIZEDMETRICS, sizeof(_mmMetrics_org), &_mmMetrics_org, 0);
}

HWND TaskBar::Create(HWND hwndParent)
{
    ClientRect clnt(hwndParent);

    int taskbar_pos = 80;   // This start position will be adjusted in DesktopBar::Resize().
    static BtnWindowClass wcTaskBar(CLASSNAME_TASKBAR);
    wcTaskBar.hbrBackground = TASKBAR_BRUSH();
    return Window::Create(WINDOW_CREATOR(TaskBar), 0,
                          wcTaskBar, TITLE_TASKBAR,
                          WS_CHILD | WS_VISIBLE | CCS_TOP | CCS_NODIVIDER | CCS_NORESIZE,
                          taskbar_pos, clnt.top + 1, clnt.right - taskbar_pos - (NOTIFYAREA_WIDTH_DEF + 1), clnt.bottom - 2, hwndParent);
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
        CCS_NODIVIDER | TBSTYLE_LIST | TBSTYLE_TOOLTIPS | TBSTYLE_WRAPABLE | TBSTYLE_FLAT;

    //_htoolbar = CreateToolbarEx(_hwnd, ws /* |TBSTYLE_AUTOSIZE */, IDW_TASKTOOLBAR, 0, 0, 0, NULL,
    //                            0, 0, 0, DESKTOPBARBAR_HEIGHT - 4, DESKTOPBARBAR_HEIGHT, sizeof(TBBUTTON));

    // Create the toolbar.
    _htoolbar = CreateWindowEx(0, TOOLBARCLASSNAME, NULL, ws, 0, 0,
        DESKTOPBARBAR_HEIGHT - 4, DESKTOPBARBAR_HEIGHT, _hwnd, NULL, g_Globals._hInstance, NULL);


    SendMessage(_htoolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
    SendMessage(_htoolbar, TB_SETBUTTONWIDTH, 0, MAKELPARAM(TASKBUTTONWIDTH_MAX, TASKBUTTONWIDTH_MAX));

    if (_no_task_title) {
        // show only icons — full-slot bitmaps, no text display
        SendMessage(_htoolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS);
        SendMessage(_htoolbar, TB_SETPADDING, 0, MAKELPARAM(0, 0));
    } else {
        if (_task_close_button) {
            SendMessage(_htoolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DRAWDDARROWS);
        }
    }
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

    case WM_TIMER:
        if (wparam == 0) {
            Refresh();
        } else if (wparam == ID_TIMER_ANIMATEBUTTONS) {
            if (!AdvanceAnimations()) {
                KillTimer(_hwnd, ID_TIMER_ANIMATEBUTTONS);
                _animation_timer_running = false;
            }
            InvalidateRect(_htoolbar, NULL, FALSE);
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
            RECT rc;
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
                return CDRF_NOTIFYITEMDRAW;
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
                    if (found != _map.end()) {
                        hover_progress = found->second._hover_progress;
                        active_progress = found->second._active_progress;
                    }
                    if (!_animate_highlights) {
                        hover_progress = ((lptbcd->nmcd.uItemState & CDIS_HOT) == CDIS_HOT) ? 1.0f : 0.0f;
                        active_progress = ((lptbcd->nmcd.uItemState & CDIS_CHECKED) == CDIS_CHECKED) ? 1.0f : 0.0f;
                    }

                    DrawTaskbarButtonHighlight(lptbcd->nmcd.hdc, lptbcd->nmcd.rc, hover_progress, active_progress);
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
                    // Draw running-app dot indicator(s)
                    COLORREF indicator_color = TASKBAR_TASKLINECOLOR();
                    if (indicator_color != MAXDWORD) {
                        int wnd_count = 1;
                        if (found != _map.end())
                            wnd_count = found->second._window_group_count;

                        BYTE dot_alpha = taskbar_draw::LerpAlpha(
                            taskbar_draw::GetIndicatorIdleAlpha(),
                            taskbar_draw::GetIndicatorActiveAlpha(),
                            active_progress);
                        if (!_animate_highlights) {
                            dot_alpha = ((lptbcd->nmcd.uItemState & CDIS_CHECKED) == CDIS_CHECKED)
                                ? taskbar_draw::GetIndicatorActiveAlpha()
                                : taskbar_draw::GetIndicatorIdleAlpha();
                        }

                        int dot_d = DPI_SX(4);
                        int dot_r = dot_d / 2;
                        int dot_y = lptbcd->nmcd.rc.bottom - DPI_SY(5) - dot_d;
                        int btn_cx = (lptbcd->nmcd.rc.left + lptbcd->nmcd.rc.right) / 2;

                        if (wnd_count >= 2) {
                            int gap = DPI_SX(3);
                            RECT d1 = { btn_cx - gap / 2 - dot_d, dot_y, btn_cx - gap / 2, dot_y + dot_d };
                            RECT d2 = { btn_cx + (gap + 1) / 2, dot_y, btn_cx + (gap + 1) / 2 + dot_d, dot_y + dot_d };
                            taskbar_draw::FillRoundedRect(lptbcd->nmcd.hdc, d1, dot_r, indicator_color, dot_alpha);
                            taskbar_draw::FillRoundedRect(lptbcd->nmcd.hdc, d2, dot_r, indicator_color, dot_alpha);
                        } else {
                            RECT dot = { btn_cx - dot_r, dot_y, btn_cx + dot_r, dot_y + dot_d };
                            taskbar_draw::FillRoundedRect(lptbcd->nmcd.hdc, dot, dot_r, indicator_color, dot_alpha);
                        }
                    }
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
                    _tcsncpy(tip->pszText, it->second._title.c_str(), tip->cchTextMax - 1);
                    tip->pszText[tip->cchTextMax - 1] = 0;
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

        String app_key = taskbar_identity::GetWindowAppKey(hwnd);
        if (app_key.empty())
            return TRUE;

        bool pinned_group = pThis->_pinned_app_keys.find(app_key) != pThis->_pinned_app_keys.end();
        TaskBarMap::iterator found = pThis->_map.find(app_key);
        if (found == pThis->_map.end()) {
            TaskBarEntry entry;
            entry._app_key = app_key;
            entry._title = title;
            entry._pinned = pinned_group;
            pThis->_map[app_key] = entry;
            found = pThis->_map.find(app_key);
        }

        TaskBarEntry &entry = found->second;
        BYTE old_state = entry._fsState;
        String old_title = entry._title;

        ++entry._used;
        entry._windows.push_back(hwnd);
        entry._window_group_count = (int)entry._windows.size();
        entry._pinned = pinned_group;
        GetWindowThreadProcessId(hwnd, &entry._pid);

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
        } else if (title[0] && entry._title.empty()) {
            entry._title = title;
        }

        // Only set active flags on the FIRST active window found, not accumulate
        if (is_active_window) {
            entry._fsState = TBSTATE_ENABLED | TBSTATE_PRESSED | TBSTATE_CHECKED;
            pThis->_last_foreground_wnd = hwnd;
        }

        if (!entry._pinned && entry._id == 0) {
            HBITMAP hbmp = NULL;
            HICON hIcon = NULL;
            BOOL delete_icon = FALSE;

            if (str_title == TEXT("ConsoleWindowClass"))
                hIcon = g_Globals._icon_cache.get_icon(ICID_CMDEXE).get_hicon();

            if (!hIcon)
                hIcon = get_window_icon_big(hwnd);

            if (!hIcon) {
                hIcon = LoadIcon(0, IDI_APPLICATION);
                delete_icon = TRUE;
            }

            if (hIcon) {
                RECT rect = _icon_area;
                hbmp = create_bitmap_from_icon(hIcon, TASKBAR_BRUSH(), WindowCanvas(pThis->_htoolbar), TASKBAR_ICON_SIZE, rect);
                if (delete_icon)
                    DestroyIcon(hIcon);
            }

            TBADDBITMAP ab = {0, (UINT_PTR)hbmp};
            entry._bmp_idx = (int)SendMessage(pThis->_htoolbar, TB_ADDBITMAP, 1, (LPARAM)&ab);
            entry._hbmp = hbmp;
            entry._id = pThis->_next_id++;

            TBBUTTON btn = { -2, 0, entry._fsState, BTNS_BUTTON, {0, 0}, 0, 0 };
            if (pThis->_task_close_button)
                btn.fsStyle = BTNS_DROPDOWN;
            if (entry._title.length() && !pThis->_no_task_title)
                btn.iString = (INT_PTR)entry._title.c_str();

            btn.idCommand = entry._id;
            btn.iBitmap = entry._bmp_idx;
            entry._btn_idx = (int)SendMessage(pThis->_htoolbar, TB_BUTTONCOUNT, 0, 0);
            SendMessage(pThis->_htoolbar, TB_INSERTBUTTON, entry._btn_idx, (LPARAM)&btn);

            _log_(FmtString(TEXT("TaskBar::AddButton %s"), str_title.c_str()));
        } else if (!entry._pinned && entry._id) {
            if (entry._fsState != old_state)
                SendMessage(pThis->_htoolbar, TB_SETSTATE, entry._id, MAKELONG(entry._fsState, 0));

            if (entry._title != old_title) {
                TBBUTTONINFO info;
                info.cbSize = sizeof(TBBUTTONINFO);
                info.dwMask = TBIF_TEXT;
                info.pszText = (LPTSTR)entry._title.c_str();
                SendMessage(pThis->_htoolbar, TB_SETBUTTONINFO, entry._id, (LPARAM)&info);
            }
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

void TaskBar::Refresh()
{
    BuildPinnedTaskbarAppKeySet(_pinned_app_keys);

    for (TaskBarMap::iterator it = _map.begin(); it != _map.end(); ++it) {
        it->second._used = 0;
        it->second._window_group_count = 0;
        it->second._primary_hwnd = 0;
        it->second._fsState = TBSTATE_ENABLED;
        it->second._pinned = false;
        it->second._windows.clear();
    }

    EnumWindows(EnumWndProc, (LPARAM)this);
    //EnumDesktopWindows(GetThreadDesktop(GetCurrentThreadId()), EnumWndProc, (LPARAM)_htoolbar);

    set<int> btn_idx_to_delete;
    set<HBITMAP> hbmp_to_delete;
    set<String> keys_to_delete;

    for (TaskBarMap::iterator it = _map.begin(); it != _map.end(); ++it) {
        TaskBarEntry &entry = it->second;

        if ((!entry._used || entry._pinned) && entry._id) {
            btn_idx_to_delete.insert(entry._btn_idx);
            hbmp_to_delete.insert(entry._hbmp);
            entry._id = 0;
            entry._btn_idx = 0;
            entry._bmp_idx = 0;
            entry._hbmp = 0;
        }

        if (!entry._used)
            keys_to_delete.insert(it->first);
    }

    if (!btn_idx_to_delete.empty()) {
        // remove buttons from right to left
        for (set<int>::reverse_iterator it = btn_idx_to_delete.rbegin(); it != btn_idx_to_delete.rend(); ++it) {
            int idx = *it;

            if (!SendMessage(_htoolbar, TB_DELETEBUTTON, idx, 0))
                MessageBoxW(NULL, L"failed to delete button", NULL, MB_OK);


            for (TaskBarMap::iterator it2 = _map.begin(); it2 != _map.end(); ++it2) {
                TaskBarEntry &entry = it2->second;

                // adjust button indexes
                if (entry._btn_idx > idx) {
                    --entry._btn_idx;
#if 0
                    --entry._bmp_idx;

                    TBBUTTONINFO info;

                    info.cbSize = sizeof(TBBUTTONINFO);
                    info.dwMask = TBIF_IMAGE;
                    info.iImage = entry._bmp_idx;

                    if (!SendMessage(_htoolbar, TB_SETBUTTONINFO, entry._id, (LPARAM)&info))
                        MessageBoxW(NULL, L"failed to set button info", NULL, MB_OK);
#endif
                }
            }

        }

        for (set<HBITMAP>::iterator it = hbmp_to_delete.begin(); it != hbmp_to_delete.end(); ++it)
            if (*it)
                DeleteObject(*it);
    }

    for (set<String>::const_iterator it = keys_to_delete.begin(); it != keys_to_delete.end(); ++it)
        _map.erase(*it);

    ResizeButtons();

    if (!_animate_highlights)
        SyncAnimationState(true);
    RefreshAnimationTimer();

    HWND hwnd_quicklaunch = GetDlgItem(GetParent(_hwnd), IDW_QUICKLAUNCHBAR);
    if (hwnd_quicklaunch)
        PostMessage(hwnd_quicklaunch, PM_UPDATE_DESKTOP, 0, 0);

    // Trigger parent resize so centered layout recalculates with new button count
    if (_centered_layout) {
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

        if (btn_width < TASKBUTTONWIDTH_MIN)
            btn_width = TASKBUTTONWIDTH_MIN;
        else if (btn_width > TASKBUTTONWIDTH_MAX)
            btn_width = TASKBUTTONWIDTH_MAX;

        if (btn_width != _last_btn_width) {
            _last_btn_width = btn_width;

            SendMessage(_htoolbar, TB_SETBUTTONWIDTH, 0, MAKELONG(btn_width, btn_width));
            SendMessage(_htoolbar, TB_AUTOSIZE, 0, 0);
        }
    }
}

int TaskBar::GetPreferredWidth() const
{
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
    if (!_animate_highlights || !_htoolbar)
        return false;

    int hot_index = (int)SendMessage(_htoolbar, TB_GETHOTITEM, 0, 0);
    for (TaskBarMap::const_iterator it = _map.begin(); it != _map.end(); ++it) {
        if (!it->second._id)
            continue;

        LONG state = (LONG)SendMessage(_htoolbar, TB_GETSTATE, it->second._id, 0);
        float target_hover = it->second._btn_idx == hot_index ? 1.0f : 0.0f;
        float target_active = ((state & TBSTATE_CHECKED) || (state & TBSTATE_PRESSED)) ? 1.0f : 0.0f;

        float hover_diff = it->second._hover_progress - target_hover;
        if (hover_diff < 0.0f)
            hover_diff = -hover_diff;

        float active_diff = it->second._active_progress - target_active;
        if (active_diff < 0.0f)
            active_diff = -active_diff;

        if (hover_diff > 0.01f || active_diff > 0.01f)
            return true;
    }

    return false;
}

bool TaskBar::AdvanceAnimations()
{
    if (!_animate_highlights || !_htoolbar)
        return false;

    bool needs_more = false;
    float blend = taskbar_draw::GetAnimationBlend();
    int hot_index = (int)SendMessage(_htoolbar, TB_GETHOTITEM, 0, 0);

    for (TaskBarMap::iterator it = _map.begin(); it != _map.end(); ++it) {
        if (!it->second._id)
            continue;

        LONG state = (LONG)SendMessage(_htoolbar, TB_GETSTATE, it->second._id, 0);
        float target_hover = it->second._btn_idx == hot_index ? 1.0f : 0.0f;
        float target_active = ((state & TBSTATE_CHECKED) || (state & TBSTATE_PRESSED)) ? 1.0f : 0.0f;

        it->second._hover_progress = taskbar_draw::EaseTowards(it->second._hover_progress, target_hover, blend);
        it->second._active_progress = taskbar_draw::EaseTowards(it->second._active_progress, target_active, blend);

        float hover_diff = it->second._hover_progress - target_hover;
        if (hover_diff < 0.0f)
            hover_diff = -hover_diff;

        float active_diff = it->second._active_progress - target_active;
        if (active_diff < 0.0f)
            active_diff = -active_diff;

        if (hover_diff > 0.01f || active_diff > 0.01f)
            needs_more = true;
    }

    return needs_more;
}

void TaskBar::RefreshAnimationTimer(bool invalidate)
{
    if (!_animate_highlights)
        return;

    if (IsAnimationRequired()) {
        if (!_animation_timer_running) {
            SetTimer(_hwnd, ID_TIMER_ANIMATEBUTTONS, 16, NULL);
            _animation_timer_running = true;
        }
        if (invalidate)
            InvalidateRect(_htoolbar, NULL, FALSE);
    } else if (_animation_timer_running) {
        KillTimer(_hwnd, ID_TIMER_ANIMATEBUTTONS);
        _animation_timer_running = false;
    }
}

void TaskBar::SyncAnimationState(bool snap_to_target)
{
    if (!_htoolbar)
        return;

    int hot_index = (int)SendMessage(_htoolbar, TB_GETHOTITEM, 0, 0);
    for (TaskBarMap::iterator it = _map.begin(); it != _map.end(); ++it) {
        LONG state = (LONG)SendMessage(_htoolbar, TB_GETSTATE, it->second._id, 0);
        float target_hover = it->second._btn_idx == hot_index ? 1.0f : 0.0f;
        float target_active = ((state & TBSTATE_CHECKED) || (state & TBSTATE_PRESSED)) ? 1.0f : 0.0f;
        if (snap_to_target || !_animate_highlights) {
            it->second._hover_progress = target_hover;
            it->second._active_progress = target_active;
        }
    }
}