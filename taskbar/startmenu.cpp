/*
 * Copyright 2003, 2004 Martin Fuchs
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
// startmenu.cpp
//
// Explorer start menu
//
// Martin Fuchs, 19.08.2003
//
// Credits: Thanks to Everaldo (http://www.everaldo.com) for his nice looking icons.
//


#include <precomp.h>

#include <algorithm>
#include <gdiplus.h>
#include <uxtheme.h>

#include <dwmapi.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")

#include "../resource.h"
#include "../luaengine/WindowCompositionAttribute.h"
#include "../utility/taskbar_draw.h"

#include "desktopbar.h"
#include "startmenu.h"

#include "../dialogs/searchprogram.h"
#include "../dialogs/settings.h"

#include "../jconfig/jcfg.h"

extern int JCfg_GetDesktopBarHeightWithDPI();

#define SHELLPATH_CONTROL_PANEL     TEXT("::{21EC2020-3AEA-1069-A2DD-08002B30309D}")
#define SHELLPATH_PRINTERS          TEXT("::{2227A280-3AEA-1069-A2DE-08002B30309D}")
#define SHELLPATH_NET_CONNECTIONS   TEXT("::{7007ACC7-3202-11D1-AAD2-00805FC1270E}")

#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER     0x1501
#endif

enum {
    PM_STARTMENU_SEARCH_KEYDOWN = WM_APP + 0x40,
    PM_STARTMENU_SEARCH_WHEEL = WM_APP + 0x41,
    PM_STARTMENU_SEARCH_FOCUS = WM_APP + 0x42
};

enum {
    STARTMENU_CONTEXT_OPEN = 1,
    STARTMENU_CONTEXT_COPY,
    STARTMENU_CONTEXT_COPY_PATH
};

struct StartMenuSearchEdit : public SubclassedWindow {
    typedef SubclassedWindow super;

    StartMenuSearchEdit(HWND hwnd, HWND hwnd_parent)
        : super(hwnd),
          _hwnd_parent(hwnd_parent)
    {
    }

protected:
    HWND _hwnd_parent;

    LRESULT WndProc(UINT nmsg, WPARAM wparam, LPARAM lparam)
    {
        switch (nmsg) {
        case WM_KEYDOWN:
            switch (wparam) {
            case VK_ESCAPE:
            case VK_RETURN:
            case VK_TAB:
            case VK_UP:
            case VK_DOWN:
            case VK_PRIOR:
            case VK_NEXT:
                return SendMessage(_hwnd_parent, PM_STARTMENU_SEARCH_KEYDOWN, wparam, lparam);
            default:
                break;
            }
            break;

        case WM_MOUSEWHEEL:
            return SendMessage(_hwnd_parent, PM_STARTMENU_SEARCH_WHEEL, wparam, lparam);

        case WM_SETFOCUS:
            SendMessage(_hwnd_parent, PM_STARTMENU_SEARCH_FOCUS, 0, 0);
            break;
        }

        return super::WndProc(nmsg, wparam, lparam);
    }
};

static int CommandHook(HWND hwnd, const TCHAR *act);

StartMenu::StartMenu(HWND hwnd, int icon_size)
    :  super(hwnd),
       _icon_size(icon_size)
{
    _next_id = IDC_FIRST_MENU;
    _submenu_id = 0;

    _border_left = 0;
    _border_top = 0;
    _bottom_max = INT_MAX;

    _floating_btn = false;
    _arrow_btns = false;
    _scroll_mode = SCROLL_NOT;
    _scroll_pos = 0;
    _invisible_lines = 0;

    _last_pos = WindowRect(hwnd).pos();
#ifdef _LIGHT_STARTMENU
    _selected_id = -1;
    _last_mouse_pos = 0;
#endif

    _name_flags = 0;
}

StartMenu::StartMenu(HWND hwnd, const StartMenuCreateInfo &create_info, int icon_size)
    :  super(hwnd),
       _create_info(create_info),
       _icon_size(icon_size)
{
    for (StartMenuFolders::const_iterator it = create_info._folders.begin(); it != create_info._folders.end(); ++it)
        if (*it)
            _dirs.push_back(ShellDirectory(GetDesktopFolder(), *it, _hwnd));

    _next_id = IDC_FIRST_MENU;
    _submenu_id = 0;

    _border_left = 0;
    _border_top = create_info._border_top;
    _bottom_max = INT_MAX;

    _floating_btn = create_info._border_top ? true : false;
    _arrow_btns = false;
    _scroll_mode = SCROLL_NOT;
    _scroll_pos = 0;
    _invisible_lines = 0;

    _last_pos = WindowRect(hwnd).pos();
#ifdef _LIGHT_STARTMENU
    _selected_id = -1;
    _last_mouse_pos = 0;
#endif

    _name_flags = 0;
}

StartMenu::~StartMenu()
{
    SendParent(PM_STARTMENU_CLOSED);
}


// We need this wrapper function for s_wcStartMenu, it calls the WIN32 API,
// though static C++ initializers are not allowed for Winelib applications.
BtnWindowClass &StartMenu::GetWndClasss()
{
    static BtnWindowClass s_wcStartMenu(CLASSNAME_STARTMENU);

    return s_wcStartMenu;
}


Window::CREATORFUNC_INFO StartMenu::s_def_creator = STARTMENU_CREATOR(StartMenu);

HWND StartMenu::Create(int x, int y, const StartMenuFolders &folders, HWND hwndParent, LPCTSTR title,
                       CREATORFUNC_INFO creator, void *info, const String &filter)
{
    UINT style, ex_style;
    int top_height;

    if (hwndParent) {
        style = WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN | WS_VISIBLE;
        ex_style = 0;
        top_height = STARTMENU_TOP_BTN_SPACE;
    } else {
        style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN | WS_VISIBLE;
        ex_style = WS_EX_TOOLWINDOW;
        top_height = 0;
    }

    int icon_size = ICON_SIZE_SMALL;
    RECT rect = {x, y - STARTMENU_LINE_HEIGHT(icon_size) - top_height, x + STARTMENU_WIDTH_MIN, y};

#ifndef _LIGHT_STARTMENU
    rect.top += STARTMENU_LINE_HEIGHT(icon_size);
#endif

    AdjustWindowRectEx(&rect, style, FALSE, ex_style);

    StartMenuCreateInfo create_info;

    create_info._folders = folders;
    create_info._border_top = top_height;
    create_info._creator = creator;
    create_info._info = info;
    create_info._filter = filter;

    if (title)
        create_info._title = title;

    HWND hwnd = Window::Create(creator, &create_info, ex_style, GetWndClasss(), title,
                               style, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, hwndParent);

    // make sure the window is not off the screen
    MoveVisible(hwnd);

    return hwnd;
}


LRESULT StartMenu::Init(LPCREATESTRUCT pcs)
{
    try {
        AddEntries();

        if (super::Init(pcs))
            return 1;

        // create buttons for registered entries in _entries
        for (ShellEntryMap::const_iterator it = _entries.begin(); it != _entries.end(); ++it) {
            const StartMenuEntry &sme = it->second;
            bool hasSubmenu = false;

            for (ShellEntrySet::const_iterator it = sme._entries.begin(); it != sme._entries.end(); ++it)
                if ((*it)->_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    hasSubmenu = true;

#ifdef _LIGHT_STARTMENU
            _buttons.push_back(SMBtnInfo(sme, it->first, hasSubmenu));
#else
            AddButton(sme._title, sme._hIcon, hasSubmenu, it->first);
#endif
        }

#ifdef _LIGHT_STARTMENU
        if (_buttons.empty())
#else
        if (!GetWindow(_hwnd, GW_CHILD))
#endif
            AddButton(ResString(IDS_EMPTY), ICID_NONE, false, 0, false);

#ifdef _LIGHT_STARTMENU
        ResizeToButtons();
#endif

#ifdef _LAZY_ICONEXTRACT
        PostMessage(_hwnd, PM_UPDATE_ICONS, 0, 0);
#endif
    } catch (COMException &e) {
        HandleException(e, pcs->hwndParent);    // destroys the start menu window while switching focus
    }

    return 0;
}

void StartMenu::AddEntries()
{
    for (StartMenuShellDirs::iterator it = _dirs.begin(); it != _dirs.end(); ++it) {
        StartMenuDirectory &smd = *it;
        ShellDirectory &dir = smd._dir;

        if (!dir._scanned) {
            WaitCursor wait;

#ifdef _LAZY_ICONEXTRACT
            dir.smart_scan(SORT_NAME, SCAN_DONT_EXTRACT_ICONS); // lazy icon extraction, try to read directly from filesystem
#else
            dir.smart_scan(SORT_NAME);
#endif
        }

        AddShellEntries(dir, -1, smd._ignore);
    }
}


static LPTSTR trim_path_slash(LPTSTR path)
{
    LPTSTR p = path;

    while (*p)
        ++p;

    if (p > path && (p[-1] == '\\' || p[-1] == '/'))
        *--p = '\0';

    return path;
}

void StartMenu::AddShellEntries(const ShellDirectory &dir, int max, const String &ignore)
{
    TCHAR ignore_path[MAX_PATH], ignore_dir[MAX_PATH], ignore_name[_MAX_FNAME], ignore_ext[_MAX_EXT];
    TCHAR dir_path[MAX_PATH];

    if (!ignore.empty()) {
        _tsplitpath_s(ignore, ignore_path, COUNTOF(ignore_path), ignore_dir, COUNTOF(ignore_dir), ignore_name, COUNTOF(ignore_name), ignore_ext, COUNTOF(ignore_ext));

        _tcscat(ignore_path, ignore_dir);
        _tcscat(ignore_name, ignore_ext);

        dir.get_path(dir_path, COUNTOF(dir_path));

        if (_tcsicmp(trim_path_slash(dir_path), trim_path_slash(ignore_path)))
            *ignore_name = '\0';
    } else
        *ignore_name = '\0';

    String lwr_filter = _create_info._filter;
    lwr_filter.toLower();

    int cnt = 0;
    for (Entry *entry = dir._down; entry; entry = entry->_next) {
        // hide files like "desktop.ini"
        if (entry->_shell_attribs & SFGAO_HIDDEN)
            //not appropriate for drive roots: if (entry->_data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
            continue;

        // hide "Programs" subfolders if requested
        if (*ignore_name && !_tcsicmp(entry->_data.cFileName, ignore_name))
            continue;

        // only 'max' entries shall be added.
        if (++cnt == max)
            break;

        // filter only non-directory entries
        if (!(entry->_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !lwr_filter.empty()) {
            String lwr_name = entry->_data.cFileName;
            String lwr_disp = entry->_display_name;

            lwr_name.toLower();
            lwr_disp.toLower();

            if (!_tcsstr(lwr_name, lwr_filter) && !_tcsstr(lwr_disp, lwr_filter))
                continue;
        }

        if (entry->_etype == ET_SHELL)
            AddEntry(dir._folder, static_cast<ShellEntry *>(entry));
        else
            AddEntry(dir._folder, entry);
    }
}


LRESULT StartMenu::WndProc(UINT nmsg, WPARAM wparam, LPARAM lparam)
{
    switch (nmsg) {
    case WM_PAINT: {
        PaintCanvas canvas(_hwnd);
        Paint(canvas);
        break;
    }

    case WM_SIZE:
        ResizeButtons(LOWORD(lparam) - _border_left);
        break;

    case WM_MOVE: {
        POINTS pos;
        pos.x = LOWORD(lparam);
        pos.y = HIWORD(lparam);

        // move open submenus of floating menus
        if (_submenu) {
            int dx = pos.x - _last_pos.x;
            int dy = pos.y - _last_pos.y;

            if (dx || dy) {
                WindowRect rt(_submenu);
                SetWindowPos(_submenu, 0, rt.left + dx, rt.top + dy, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
                //MoveVisible(_submenu);
            }
        }

        _last_pos.x = pos.x;
        _last_pos.y = pos.y;
        goto def;
    }

    case WM_NCHITTEST: {
        LRESULT res = super::WndProc(nmsg, wparam, lparam);

        if (res >= HTSIZEFIRST && res <= HTSIZELAST)
            return HTCLIENT;    // disable window resizing

        return res;
    }

    case WM_LBUTTONDOWN: {
        RECT rect;

        // check mouse cursor for coordinates of floating button
        GetFloatingButtonRect(&rect);

        if (PtInRect(&rect, Point(lparam))) {
            // create a floating copy of the current start menu
            WindowRect pos(_hwnd);

            ///@todo do something similar to StartMenuRoot::TrackStartmenu() in order to automatically close submenus when clicking on the desktop background
            StartMenu::Create(pos.left + 3, pos.bottom - 3, _create_info._folders, 0, _create_info._title, _create_info._creator, _create_info._info);
            CloseStartMenu();
        }

#ifdef _LIGHT_STARTMENU
        int id = ButtonHitTest(Point(lparam));

        if (id)
            Command(id, BN_CLICKED);
#endif
        break;
    }

    case WM_SYSCOMMAND:
        if ((wparam & 0xFFF0) == SC_SIZE)
            return 0;           // disable window resizing
        goto def;

    case WM_ACTIVATEAPP:
        // close start menu when activating another application
        if (!wparam)
            CloseStartMenu();
        break;  // don't call super::WndProc in case "this" has been deleted

    case WM_CANCELMODE:
        CloseStartMenu();

#ifdef _LIGHT_STARTMENU
        if (_scroll_mode != SCROLL_NOT) {
            ReleaseCapture();
            KillTimer(_hwnd, 0);
        }
#endif
        break;

#ifdef _LIGHT_STARTMENU
    case WM_MOUSEMOVE: {
        // automatically set the focus to startmenu entries when moving the mouse over them
        if (lparam != _last_mouse_pos) { // don't process WM_MOUSEMOVE when opening submenus using keyboard navigation
            Point pt(lparam);

            if (_arrow_btns) {
                RECT rect_up, rect_down;

                GetArrowButtonRects(&rect_up, &rect_down, _icon_size);

                SCROLL_MODE scroll_mode = SCROLL_NOT;

                if (PtInRect(&rect_up, pt))
                    scroll_mode = SCROLL_UP;
                else if (PtInRect(&rect_down, pt))
                    scroll_mode = SCROLL_DOWN;

                if (scroll_mode != _scroll_mode) {
                    if (scroll_mode == SCROLL_NOT) {
                        ReleaseCapture();
                        KillTimer(_hwnd, 0);
                    } else {
                        CloseSubmenus();
                        SetTimer(_hwnd, 0, 150, NULL);  // 150 ms scroll interval
                        SetCapture(_hwnd);
                    }

                    _scroll_mode = scroll_mode;
                }
            }

            int new_id = ButtonHitTest(pt);

            if (new_id > 0 && new_id != _selected_id)
                SelectButton(new_id);

            _last_mouse_pos = lparam;
        }
        break;
    }

    case WM_TIMER:
        if (_scroll_mode == SCROLL_UP) {
            if (_scroll_pos > 0) {
                --_scroll_pos;
                InvalidateRect(_hwnd, NULL, TRUE);
            }
        } else {
            if (_scroll_pos <= _invisible_lines) {
                ++_scroll_pos;
                InvalidateRect(_hwnd, NULL, TRUE);
            }
        }
        break;

    case WM_KEYDOWN:
        ProcessKey((int)wparam);
        break;
#else
    case PM_STARTENTRY_FOCUSED: { ///@todo use TrackMouseEvent() and WM_MOUSEHOVER to wait a bit before opening submenus
        BOOL hasSubmenu = wparam;
        HWND hctrl = (HWND)lparam;

        // automatically open submenus
        if (hasSubmenu) {
            UpdateWindow(_hwnd);    // draw focused button before waiting on submenu creation
            //SendMessage(_hwnd, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hctrl),BN_CLICKED), (LPARAM)hctrl);
            Command(GetDlgCtrlID(hctrl), BN_CLICKED);
        } else {
            // close any open submenu
            CloseOtherSubmenus();
        }
        break;
    }
#endif

#ifdef _LAZY_ICONEXTRACT
    case PM_UPDATE_ICONS:
        UpdateIcons(/*wparam*/);
        break;
#endif

    case PM_STARTENTRY_LAUNCHED:
        if (GetWindowStyle(_hwnd) & WS_CAPTION) // don't automatically close floating menus
            return 0;

        // route message to the parent menu and close menus after launching an entry
        if (!SendParent(nmsg, wparam, lparam))
            CloseStartMenu();
        return 1;   // signal that we have received and processed the message

    case PM_STARTMENU_CLOSED:
        _submenu = 0;
        break;

    case PM_SELECT_ENTRY:
        SelectButtonIndex(0, wparam != 0);
        break;

#ifdef _LIGHT_STARTMENU
    case WM_CONTEXTMENU: {
        Point screen_pt(lparam), clnt_pt = screen_pt;
        ScreenToClient(_hwnd, &clnt_pt);

        int id = ButtonHitTest(clnt_pt);

        if (id) {
            StartMenuEntry &sme = _entries[id];

            for (ShellEntrySet::iterator it = sme._entries.begin(); it != sme._entries.end(); ++it) {
                Entry *entry = *it;

                if (entry) {
                    CHECKERROR(entry->do_context_menu(_hwnd, screen_pt, _cm_ifs));  // may close start menu because of focus loss
                    ///@todo refresh on successfull context menu execution?
                    break;  ///@todo handle context menu for more than one entry
                }
            }
        }
        break;
    }
#endif

default: def:
        return super::WndProc(nmsg, wparam, lparam);
    }

    return 0;
}


#ifdef _LIGHT_STARTMENU

int StartMenu::ButtonHitTest(POINT pt)
{
    ClientRect clnt(_hwnd);
    const int icon_size = _icon_size;
    RECT rect = {_border_left, _border_top, clnt.right, STARTMENU_LINE_HEIGHT(icon_size)};

    if (pt.x < rect.left || pt.x > rect.right)
        return 0;

    for (SMBtnVector::const_iterator it = _buttons.begin() + _scroll_pos; it != _buttons.end(); ++it) {
        const SMBtnInfo &info = *it;

        if (rect.top > pt.y)
            break;

        rect.bottom = rect.top + (info._id == -1 ? STARTMENU_SEP_HEIGHT(icon_size) : STARTMENU_LINE_HEIGHT(icon_size));

        if (rect.bottom > _bottom_max)
            break;

        if (pt.y < rect.bottom) // PtInRect(&rect, pt)
            return info._id;

        rect.top = rect.bottom;
    }

    return 0;
}

void StartMenu::InvalidateSelection()
{
    if (_selected_id <= 0)
        return;

    ClientRect clnt(_hwnd);
    const int icon_size = _icon_size;
    RECT rect = {_border_left, _border_top, clnt.right, STARTMENU_LINE_HEIGHT(icon_size)};

    for (SMBtnVector::const_iterator it = _buttons.begin() + _scroll_pos; it != _buttons.end(); ++it) {
        const SMBtnInfo &info = *it;

        rect.bottom = rect.top + (info._id == -1 ? STARTMENU_SEP_HEIGHT(icon_size) : STARTMENU_LINE_HEIGHT(icon_size));

        if (info._id == _selected_id) {
            InvalidateRect(_hwnd, &rect, TRUE);
            break;
        }

        rect.top = rect.bottom;
    }
}

const SMBtnInfo *StartMenu::GetButtonInfo(int id) const
{
    for (SMBtnVector::const_iterator it = _buttons.begin(); it != _buttons.end(); ++it)
        if (it->_id == id)
            return &*it;

    return NULL;
}

bool StartMenu::SelectButton(int id, bool open_sub)
{
    if (id == -1)
        return false;

    if (id == _selected_id)
        return true;

    InvalidateSelection();

    const SMBtnInfo *btn = GetButtonInfo(id);

    if (btn && btn->_enabled) {
        _selected_id = id;

        InvalidateSelection();

        // automatically open submenus
        if (btn->_hasSubmenu) {
            if (open_sub)
                OpenSubmenu();
        } else
            CloseOtherSubmenus();   // close any open submenu

        return true;
    } else {
        _selected_id = -1;
        return false;
    }
}

bool StartMenu::OpenSubmenu(bool select_first)
{
    if (_selected_id == -1)
        return false;

    InvalidateSelection();

    const SMBtnInfo *btn = GetButtonInfo(_selected_id);

    // automatically open submenus
    if (btn->_hasSubmenu) {
        //@@ allows destroying of startmenu when processing PM_UPDATE_ICONS -> GPF
        UpdateWindow(_hwnd);    // draw focused button before waiting on submenu creation
        Command(_selected_id, BN_CLICKED);

        if (select_first && _submenu)
            SendMessage(_submenu, PM_SELECT_ENTRY, (WPARAM)false, 0);

        return true;
    } else
        return false;
}


int StartMenu::GetSelectionIndex()
{
    if (_selected_id == -1)
        return -1;

    for (int i = 0; i < (int)_buttons.size(); ++i)
        if (_buttons[i]._id == _selected_id)
            return i;

    return -1;
}

bool StartMenu::SelectButtonIndex(int idx, bool open_sub)
{
    if (idx >= 0 && idx < (int)_buttons.size())
        return SelectButton(_buttons[idx]._id, open_sub);
    else
        return false;
}

void StartMenu::ProcessKey(int vk)
{
    switch (vk) {
    case VK_RETURN:
        if (_selected_id)
            Command(_selected_id, BN_CLICKED);
        break;

    case VK_UP:
        Navigate(-1);
        break;

    case VK_DOWN:
        Navigate(+1);
        break;

    case VK_HOME:
        SelectButtonIndex(0, false);
        break;

    case VK_END:
        SelectButtonIndex((int)_buttons.size() - 1, false);
        break;

    case VK_LEFT:
        if (_submenu)
            CloseOtherSubmenus();
        else if (!(GetWindowStyle(_hwnd) & WS_CAPTION)) // don't automatically close floating menus
            DestroyWindow(_hwnd);
        break;

    case VK_RIGHT:
        OpenSubmenu(true);
        break;

    case VK_ESCAPE:
        CloseStartMenu();
        break;

    default:
        if (vk >= '0' && vk <= 'Z')
            JumpToNextShortcut(vk);
    }
}

bool StartMenu::Navigate(int step)
{
    int idx = GetSelectionIndex();

    if (idx == -1) {
        if (step > 0)
            idx = 0 - step;
        else
            idx = (int)_buttons.size() - step;
    }

    for (;;) {
        idx += step;

        if (_buttons.size() <= 1 && (idx < 0 || idx > (int)_buttons.size()))
            break;

        if (idx < 0)
            idx += (int)_buttons.size();

        if (idx > (int)_buttons.size())
            idx -= (int)_buttons.size() + 1;

        if (SelectButtonIndex(idx, false))
            return true;
    }

    return false;
}

bool StartMenu::JumpToNextShortcut(TCHAR c)
{
    int cur_idx = GetSelectionIndex();

    if (cur_idx == -1)
        cur_idx = 0;

    int first_found = 0;
    int found_more = 0;

    SMBtnVector::const_iterator cur_it = _buttons.begin();
    cur_it += cur_idx + 1;

    // first search down from current item...
    SMBtnVector::const_iterator it = cur_it;
    for (; it != _buttons.end(); ++it) {
        const SMBtnInfo &btn = *it;

        if (!btn._title.empty() && toupper((TBYTE)btn._title.at(0)) == c) {
            if (!first_found)
                first_found = btn._id;
            else
                ++found_more;
        }
    }

    // ...now search from top to the current item
    it = _buttons.begin();
    for (; it != _buttons.end() && it != cur_it; ++it) {
        const SMBtnInfo &btn = *it;

        if (!btn._title.empty() && toupper((TBYTE)btn._title.at(0)) == c) {
            if (!first_found)
                first_found = btn._id;
            else
                ++found_more;
        }
    }

    if (first_found) {
        SelectButton(first_found);

        if (!found_more)
            Command(first_found, BN_CLICKED);

        return true;
    } else
        return false;
}

#endif // _LIGHT_STARTMENU


int StartMenu::GetButtonRect(int id, PRECT prect) const
{
#ifdef _LIGHT_STARTMENU
    ClientRect clnt(_hwnd);
    const int icon_size = _icon_size;
    RECT rect = {_border_left, _border_top, clnt.right, STARTMENU_LINE_HEIGHT(icon_size)};

    if (_buttons.size() == 0) {
        return -1;
    }
    for (SMBtnVector::const_iterator it = _buttons.begin() + _scroll_pos; it != _buttons.end(); ++it) {
        const SMBtnInfo &info = *it;

        rect.bottom = rect.top + (info._id == -1 ? STARTMENU_SEP_HEIGHT(icon_size) : STARTMENU_LINE_HEIGHT(icon_size));

        if (info._id == id) {
            *prect = rect;
            return 1;
        }

        rect.top = rect.bottom;
    }

    return 0;
#else
    HWND btn = GetDlgItem(_hwnd, id);

    if (btn) {
        GetWindowRect(btn, prect);
        ScreenToClient(_hwnd, prect);

        return true;
    } else
        return false;
#endif
}


void StartMenu::DrawFloatingButton(HDC hdc)
{
    static ResIconEx floatingIcon(IDI_FLOATING, 8, 4);

    ClientRect clnt(_hwnd);

    DrawIconEx(hdc, clnt.right - 12, 0, floatingIcon, 8, 4, 0, 0, DI_NORMAL);
}

void StartMenu::GetFloatingButtonRect(LPRECT prect)
{
    GetClientRect(_hwnd, prect);

    prect->right -= 4;
    prect->left = prect->right - 8;
    prect->bottom = 4;
}


void StartMenu::DrawArrows(HDC hdc, int icon_size)
{
    int cx = icon_size / 2;
    int cy = icon_size / 4;

    ResIconEx arrowUpIcon(IDI_ARROW_UP, cx, cy);
    ResIconEx arrowDownIcon(IDI_ARROW_DOWN, cx, cy);

    ClientRect clnt(_hwnd);

    DrawIconEx(hdc, clnt.right / 2 - cx / 2, _floating_btn ? 3 : 1, arrowUpIcon, cx, cy, 0, 0, DI_NORMAL);
    DrawIconEx(hdc, clnt.right / 2 - cx / 2, clnt.bottom - cy - 1, arrowDownIcon, cx, cy, 0, 0, DI_NORMAL);
}

void StartMenu::GetArrowButtonRects(LPRECT prect_up, LPRECT prect_down, int icon_size)
{
    int cx = icon_size / 2;
    int cy = icon_size / 4;

    GetClientRect(_hwnd, prect_up);
    *prect_down = *prect_up;

    //  prect_up->left = prect_up->right/2 - cx/2;
    //  prect_up->right = prect_up->left + cy;
    prect_up->right -= cx;
    prect_up->top = _floating_btn ? cy - 1 : 1;
    prect_up->bottom = prect_up->top + cy;

    //  prect_down->left = prect_down->right/2 - cx/2;
    //  prect_down->right = prect_down->left + cy;
    prect_down->right -= cx;
    prect_down->top = prect_down->bottom - cy - 1;
}


void StartMenu::Paint(PaintCanvas &canvas)
{
    if (_floating_btn)
        DrawFloatingButton(canvas);

#ifdef _LIGHT_STARTMENU
    if (_arrow_btns)
        DrawArrows(canvas, _icon_size);

    ClientRect clnt(_hwnd);
    const int icon_size = _icon_size;
    RECT rect = {_border_left, _border_top, clnt.right, STARTMENU_LINE_HEIGHT(icon_size)};

    int sep_width = rect.right - rect.left - 4;

    FontSelection font(canvas, g_Globals._hDefaultFont);
    BkMode bk_mode(canvas, TRANSPARENT);

    for (SMBtnVector::const_iterator it = _buttons.begin() + _scroll_pos; it != _buttons.end(); ++it) {
        const SMBtnInfo &btn = *it;

        if (rect.top > canvas.rcPaint.bottom)
            break;

        if (btn._id == -1) {    // a separator?
            rect.bottom = rect.top + STARTMENU_SEP_HEIGHT(icon_size);

            if (rect.bottom > _bottom_max)
                break;

            BrushSelection brush_sel(canvas, GetSysColorBrush(COLOR_BTNSHADOW));
            PatBlt(canvas, rect.left + 2, rect.top + STARTMENU_SEP_HEIGHT(icon_size) / 2 - 1, sep_width, 1, PATCOPY);

            SelectBrush(canvas, GetSysColorBrush(COLOR_BTNHIGHLIGHT));
            PatBlt(canvas, rect.left + 2, rect.top + STARTMENU_SEP_HEIGHT(icon_size) / 2, sep_width, 1, PATCOPY);
        } else {
            rect.bottom = rect.top + STARTMENU_LINE_HEIGHT(icon_size);

            if (rect.bottom > _bottom_max)
                break;

            if (rect.top >= canvas.rcPaint.top)
                DrawStartMenuButton(canvas, rect, btn._title, btn, btn._id == _selected_id, false, _icon_size);
        }

        rect.top = rect.bottom;
    }
#endif
}

#ifdef _LAZY_ICONEXTRACT
void StartMenu::UpdateIcons(/*int idx*/)
{
    UpdateWindow(_hwnd);

#ifdef _SINGLE_ICONEXTRACT

    //if (idx >= 0)
    int idx = _scroll_pos;

    for (; idx < (int)_buttons.size(); ++idx) {
        SMBtnInfo &btn = _buttons[idx];

        if (btn._icon_id == ICID_UNKNOWN && btn._id > 0) {
            StartMenuEntry &sme = _entries[btn._id];

            btn._icon_id = ICID_NONE;

            for (ShellEntrySet::iterator it = sme._entries.begin(); it != sme._entries.end(); ++it) {
                Entry *entry = *it;

                if (entry->_icon_id == ICID_UNKNOWN)
                    entry->_icon_id = entry->safe_extract_icon(ICF_FROM_ICON_SIZE(_icon_size));

                if (entry->_icon_id > ICID_NONE) {
                    btn._icon_id = (ICON_ID)/*@@*/ entry->_icon_id;

                    RECT rect;

                    if (GetButtonRect(btn._id, &rect) == -1) break;

                    if (rect.bottom > _bottom_max)
                        break;

                    WindowCanvas canvas(_hwnd);
                    DrawStartMenuButton(canvas, rect, NULL, btn, btn._id == _selected_id, false, _icon_size);

                    //InvalidateRect(_hwnd, &rect, FALSE);
                    //UpdateWindow(_hwnd);
                    //break;

                    break;
                }
            }
        }
    }

    //  if (++idx < (int)_buttons.size())
    //      PostMessage(_hwnd, PM_UPDATE_ICONS, idx, 0);

#else

    int icons_extracted = 0;
    int icons_updated = 0;

    for (StartMenuShellDirs::iterator it = _dirs.begin(); it != _dirs.end(); ++it) {
        ShellDirectory &dir = it->_dir;

        icons_extracted += dir.extract_icons(icon_size);
    }

    if (icons_extracted) {
        for (ShellEntryMap::iterator it1 = _entries.begin(); it1 != _entries.end(); ++it1) {
            StartMenuEntry &sme = it1->second;

            if (!sme._hIcon) {
                sme._hIcon = (HICON) - 1;

                for (ShellEntrySet::const_iterator it2 = sme._entries.begin(); it2 != sme._entries.end(); ++it2) {
                    const Entry *sm_entry = *it2;

                    if (sm_entry->_hIcon) {
                        sme._hIcon = sm_entry->_hIcon;
                        break;
                    }
                }
            }
        }

        for (SMBtnVector::iterator it = _buttons.begin(); it != _buttons.end(); ++it) {
            SMBtnInfo &info = *it;

            if (info._id > 0 && !info._hIcon) {
                info._hIcon = _entries[info._id]._hIcon;
                ++icons_updated;
            }
        }
    }

    if (icons_updated) {
        InvalidateRect(_hwnd, NULL, FALSE);
        UpdateWindow(_hwnd);
    }
#endif
}
#endif


// resize child button controls to accomodate for new window size
void StartMenu::ResizeButtons(int cx)
{
    HDWP hdwp = BeginDeferWindowPos(10);

    for (HWND ctrl = GetWindow(_hwnd, GW_CHILD); ctrl; ctrl = GetNextWindow(ctrl, GW_HWNDNEXT)) {
        ClientRect rt(ctrl);

        if (rt.right != cx) {
            int height = rt.bottom - rt.top;

            // special handling for separator controls
            if (!height && (GetWindowStyle(ctrl)&SS_TYPEMASK) == SS_ETCHEDHORZ)
                height = 2;

            hdwp = DeferWindowPos(hdwp, ctrl, 0, 0, 0, cx, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    EndDeferWindowPos(hdwp);
}


int StartMenu::Command(int id, int code)
{
#ifndef _LIGHT_STARTMENU
    switch (id) {
    case IDCANCEL:
        CloseStartMenu(id);
        break;

    default: {
#endif
        ShellEntryMap::iterator found = _entries.find(id);

        if (found != _entries.end()) {
            ActivateEntry(id, found->second._entries);
            return 0;
        }

        return super::Command(id, code);
#ifndef _LIGHT_STARTMENU
    }
    }

    return 0;
#endif
}


ShellEntryMap::iterator StartMenu::AddEntry(const String &title, ICON_ID icon_id, Entry *entry)
{
    // search for an already existing subdirectory entry with the same name
    if (entry->_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        for (ShellEntryMap::iterator it = _entries.begin(); it != _entries.end(); ++it) {
            StartMenuEntry &sme = it->second;

            if (!_tcsicmp(sme._title, title)) { ///@todo speed up by using a map indexed by name
                for (ShellEntrySet::iterator it2 = sme._entries.begin(); it2 != sme._entries.end(); ++it2) {
                    if ((*it2)->_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                        // merge the new shell entry with the existing of the same name
                        sme._entries.insert(entry);

                        return it;
                    }
                }
            }
        }
    }

    ShellEntryMap::iterator sme = AddEntry(title, icon_id);

    sme->second._entries.insert(entry);

    return sme;
}

ShellEntryMap::iterator StartMenu::AddEntry(const String &title, ICON_ID icon_id, int id)
{
    if (id == -1)
        id = ++_next_id;

    StartMenuEntry sme;

    sme._title = title;
    sme._icon_id = icon_id;

    ShellEntryMap::iterator it = _entries.insert(make_pair(id, sme)).first;

    return it;
}

ShellEntryMap::iterator StartMenu::AddEntry(const ShellFolder folder, ShellEntry *entry)
{
    ICON_ID icon_id;

    if (entry->_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        icon_id = ICID_APPS;
    else
        icon_id = (ICON_ID)/*@@*/ entry->_icon_id;

    return AddEntry(folder.get_name(entry->_pidl), icon_id, entry);
}

ShellEntryMap::iterator StartMenu::AddEntry(const ShellFolder folder, Entry *entry)
{
    ICON_ID icon_id;

    if (entry->_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        icon_id = ICID_APPS;
    else
        icon_id = (ICON_ID)/*@@*/ entry->_icon_id;

    String title = entry->_display_name;
    if (_name_flags & NO_EXEEXT_FLAG) {
        if (title.length() > 4) {
            String ext = title.substr(title.length() - 4);
            if (ext == TEXT(".exe")) {
                title = title.substr(0, title.length() - 4);
            }
        }
    }
    return AddEntry(title, icon_id, entry);
}


void StartMenu::AddButton(LPCTSTR title, ICON_ID icon_id, bool hasSubmenu, int id, bool enabled)
{
#ifdef _LIGHT_STARTMENU
    _buttons.push_back(SMBtnInfo(title, icon_id, id, hasSubmenu, enabled));
#else
    DWORD style = enabled ? WS_VISIBLE | WS_CHILD | BS_OWNERDRAW : WS_VISIBLE | WS_CHILD | BS_OWNERDRAW | WS_DISABLED;

    WindowRect rect(_hwnd);
    ClientRect clnt(_hwnd);

    // increase window height to make room for the new button
    rect.top -= STARTMENU_LINE_HEIGHT(icon_size);

    // move down if we are too high now
    if (rect.top < 0) {
        rect.top += STARTMENU_LINE_HEIGHT(icon_size);
        rect.bottom += STARTMENU_LINE_HEIGHT(icon_size);
    }

    WindowCanvas canvas(_hwnd);
    FontSelection font(canvas, g_Globals._hDefaultFont);

    // widen window, if it is too small
    int text_width = GetStartMenuBtnTextWidth(canvas, title, _hwnd) + icon_size + 10/*placeholder*/ + 16/*arrow*/;

    int cx = clnt.right - _border_left;
    if (text_width > cx)
        rect.right += text_width - cx;

    MoveWindow(_hwnd, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, TRUE);

    StartMenuCtrl(_hwnd, _border_left, clnt.bottom, rect.right - rect.left - _border_left,
                  title, id, g_Globals._icon_cache.get_icon(icon_id).get_hicon(), hasSubmenu, style);
#endif
}

void StartMenu::AddSeparator()
{
#ifdef _LIGHT_STARTMENU
    _buttons.push_back(SMBtnInfo(NULL, ICID_NONE, -1, false));
#else
    WindowRect rect(_hwnd);
    ClientRect clnt(_hwnd);

    // increase window height to make room for the new separator
    rect.top -= STARTMENU_SEP_HEIGHT(icon_size);

    // move down if we are too high now
    if (rect.top < 0) {
        rect.top += STARTMENU_LINE_HEIGHT(icon_size);
        rect.bottom += STARTMENU_LINE_HEIGHT(icon_size);
    }

    MoveWindow(_hwnd, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, TRUE);

    StartMenuSeparator(_hwnd, _border_left, clnt.bottom, rect.right - rect.left - _border_left);
#endif
}


bool StartMenu::CloseOtherSubmenus(int id)
{
    if (_submenu) {
        if (IsWindow(_submenu)) {
            if (_submenu_id == id)
                return false;
            else {
                _submenu_id = 0;
                DestroyWindow(_submenu);
                // _submenu should be reset automatically by PM_STARTMENU_CLOSED, but safety first...
            }
        }

        _submenu = 0;
    }

    return true;
}


void StartMenu::CreateSubmenu(int id, LPCTSTR title, CREATORFUNC_INFO creator, void *info)
{
    CreateSubmenu(id, StartMenuFolders(), title, creator, info);
}

bool StartMenu::CreateSubmenu(int id, int folder_id, LPCTSTR title, CREATORFUNC_INFO creator, void *info)
{
    try {
        SpecialFolderPath folder(folder_id, _hwnd);

        StartMenuFolders new_folders;
        new_folders.push_back(folder);

        CreateSubmenu(id, new_folders, title, creator, info);

        return true;
    } catch (COMException &) {
        // ignore Exception and don't display anything
        CloseOtherSubmenus(id);
        _buttons[GetSelectionIndex()]._enabled = false; // disable entries for non-existing folders
        return false;
    }
}

bool StartMenu::CreateSubmenu(int id, int folder_id1, int folder_id2, LPCTSTR title, CREATORFUNC_INFO creator, void *info)
{
    StartMenuFolders new_folders;

    try {
        new_folders.push_back(SpecialFolderPath(folder_id1, _hwnd));
    } catch (COMException &) {
    }

    try {
        new_folders.push_back(SpecialFolderPath(folder_id2, _hwnd));
    } catch (COMException &) {
    }

    if (!new_folders.empty()) {
        CreateSubmenu(id, new_folders, title, creator, info);
        return true;
    } else {
        CloseOtherSubmenus(id);
        _buttons[GetSelectionIndex()]._enabled = false; // disable entries for non-existing folders
        return false;
    }
}

void StartMenu::CreateSubmenu(int id, const StartMenuFolders &new_folders, LPCTSTR title, CREATORFUNC_INFO creator, void *info)
{
    // Only open one submenu at a time.
    if (!CloseOtherSubmenus(id))
        return;

    RECT rect;
    int x, y;

    int rc = -1;
    rc = GetButtonRect(id, &rect);
    if (rc == -1) return;
    if (rc == 1) {
        ClientToScreen(_hwnd, &rect);

        x = rect.right; // Submenus should overlap their parent a bit.
        const int icon_size = _icon_size;
        y = rect.top + STARTMENU_LINE_HEIGHT(icon_size) + _border_top/*own border*/ - STARTMENU_TOP_BTN_SPACE/*border of new submenu*/;
    } else {
        WindowRect pos(_hwnd);

        x = pos.right;
        y = pos.top;
    }

    _submenu_id = id;
    _submenu = StartMenu::Create(x, y, new_folders, _hwnd, title, creator, info, _create_info._filter);
}


void StartMenu::ActivateEntry(int id, const ShellEntrySet &entries)
{
    StartMenuFolders new_folders;
    String title;

    for (ShellEntrySet::const_iterator it = entries.begin(); it != entries.end(); ++it) {
        Entry *entry = const_cast<Entry *>(*it);

        if (entry->_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {

            ///@todo If the user explicitly clicked on a submenu, display this folder as floating start menu.

            if (entry->_etype == ET_SHELL)
                new_folders.push_back(entry->create_absolute_pidl());
            else {
                TCHAR path[MAX_PATH];

                if (entry->get_path(path, COUNTOF(path)))
                    new_folders.push_back(path);
            }

            if (title.empty())
                title = entry->_display_name;
        } else {
            // The entry is no subdirectory, so there can only be one shell entry.
            assert(entries.size() == 1);

            HWND hparent = GetParent(_hwnd);
            ShellPath shell_path = entry->create_absolute_pidl();

            // close start menus when launching the selected entry
            CloseStartMenu(id);

            ///@todo launch in the background; specify correct HWND for error message box titles
            SHELLEXECUTEINFO shexinfo;

            shexinfo.cbSize = sizeof(SHELLEXECUTEINFO);
            shexinfo.fMask = SEE_MASK_IDLIST;   // SEE_MASK_INVOKEIDLIST is also possible.
            shexinfo.hwnd = hparent;
            shexinfo.lpVerb = NULL;
            shexinfo.lpFile = NULL;
            shexinfo.lpParameters = NULL;
            shexinfo.lpDirectory = NULL;
            shexinfo.nShow = SW_SHOWNORMAL;

            shexinfo.lpIDList = &*shell_path;

            // add PIDL to the recent file list
            SHAddToRecentDocs(SHARD_PIDL, shexinfo.lpIDList);

            if (!ShellExecuteEx(&shexinfo))
                display_error(hparent, GetLastError());

            // we may have deleted 'this' - ensure we leave the loop and function
            return;
        }
    }

    if (!new_folders.empty()) {
        // Only open one submenu at a time.
        if (!CloseOtherSubmenus(id))
            return;

        CreateSubmenu(id, new_folders, title);
    }
}


/// close all windows of the start menu popup
void StartMenu::CloseStartMenu(int id)
{
    if (!(GetWindowStyle(_hwnd) & WS_CAPTION)) {    // don't automatically close floating menus
        if (!SendParent(PM_STARTENTRY_LAUNCHED, id, (LPARAM)_hwnd))
            DestroyWindow(_hwnd);
    } else if (_submenu)    // instead close submenus of floating parent menus
        CloseSubmenus();
}


int GetStartMenuBtnTextWidth(HDC hdc, LPCTSTR title, HWND hwnd)
{
    RECT rect = {0, 0, 0, 0};
    DrawText(hdc, title, -1, &rect, DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);

    return rect.right - rect.left;
}

#ifdef _LIGHT_STARTMENU
void DrawStartMenuButton(HDC hdc, const RECT &rect, LPCTSTR title, const SMBtnInfo &btn, bool has_focus, bool pushed, int icon_size)
#else
void DrawStartMenuButton(HDC hdc, const RECT &rect, LPCTSTR title, HICON hIcon,
                         bool hasSubmenu, bool enabled, bool has_focus, bool pushed, int icon_size);
#endif
{
    UINT style = DFCS_BUTTONPUSH;

    if (!btn._enabled)
        style |= DFCS_INACTIVE;

    POINT iconPos = {rect.left + 2, (rect.top + rect.bottom - icon_size) / 2};
    RECT textRect = {rect.left + icon_size + 4, rect.top + 2, rect.right - 4, rect.bottom - 4};

    if (pushed) {
        style |= DFCS_PUSHED;
        ++iconPos.x;        ++iconPos.y;
        ++textRect.left;    ++textRect.top;
        ++textRect.right;   ++textRect.bottom;
    }

    int bk_color_idx = COLOR_BTNFACE;
    int text_color_idx = COLOR_BTNTEXT;

    if (has_focus) {
        bk_color_idx = COLOR_HIGHLIGHT;
        text_color_idx = COLOR_HIGHLIGHTTEXT;
    }

    COLORREF bk_color = GetSysColor(bk_color_idx);
    HBRUSH bk_brush = GetSysColorBrush(bk_color_idx);

    if (title)
        FillRect(hdc, &rect, bk_brush);

    if (btn._icon_id > ICID_NONE)
        g_Globals._icon_cache.get_icon(btn._icon_id).draw(hdc, iconPos.x, iconPos.y, icon_size, icon_size, bk_color, bk_brush/*, icon_size*/);

    // draw submenu arrow at the right
    if (btn._hasSubmenu) {
        ResIconEx arrowIcon(IDI_ARROW, icon_size, icon_size);
        ResIconEx selArrowIcon(IDI_ARROW_SELECTED, icon_size, icon_size);

        DrawIconEx(hdc, rect.right - icon_size, iconPos.y,
                   has_focus ? selArrowIcon : arrowIcon,
                   icon_size, icon_size, 0, bk_brush, DI_NORMAL);
    }

    if (title) {
        BkMode bk_mode(hdc, TRANSPARENT);

        if (!btn._enabled)  // dis->itemState & (ODS_DISABLED|ODS_GRAYED)
            DrawGrayText(hdc, &textRect, title, DT_SINGLELINE | DT_NOPREFIX | DT_VCENTER);
        else {
            TextColor lcColor(hdc, GetSysColor(text_color_idx));
            DrawText(hdc, title, -1, &textRect, DT_SINGLELINE | DT_NOPREFIX | DT_VCENTER);
        }
    }
}


#ifdef _LIGHT_STARTMENU

void StartMenu::ResizeToButtons()
{
    WindowRect rect(_hwnd);

    WindowCanvas canvas(_hwnd);
    FontSelection font(canvas, g_Globals._hDefaultFont);

    const int icon_size = _icon_size;

    int max_width = STARTMENU_WIDTH_MIN;
    int height = 0;

    for (SMBtnVector::const_iterator it = _buttons.begin(); it != _buttons.end(); ++it) {
        int w = GetStartMenuBtnTextWidth(canvas, it->_title, _hwnd);

        if (w > max_width)
            max_width = w;

        if (it->_id == -1)
            height += STARTMENU_SEP_HEIGHT(icon_size);
        else
            height += STARTMENU_LINE_HEIGHT(icon_size);
    }

    // calculate new window size
    int text_width = max_width + icon_size + 10/*placeholder*/ + 16/*arrow*/;

    RECT rt_hgt = {rect.left, rect.bottom - _border_top - height, rect.left + _border_left + text_width, rect.bottom};
    AdjustWindowRectEx(&rt_hgt, GetWindowStyle(_hwnd), FALSE, GetWindowExStyle(_hwnd));

    // ignore movement, only look at the size change
    rect.right = rect.left + (rt_hgt.right - rt_hgt.left);
    rect.top = rect.bottom - (rt_hgt.bottom - rt_hgt.top);

    // move down if we are too high
    if (rect.top < 0) {
        int dy = -rect.top;
        rect.top += dy;
        rect.bottom += dy;
    }

    // enable scroll mode for long start menus, which span more than the whole screen height
    int cyscreen = GetSystemMetrics(SM_CYSCREEN);
    int bottom_max = 0;

    if (rect.bottom > cyscreen) {
        _arrow_btns = true;

        _invisible_lines = (rect.bottom - cyscreen + (STARTMENU_LINE_HEIGHT(icon_size) + 1)) / STARTMENU_LINE_HEIGHT(icon_size) + 1;
        rect.bottom -= _invisible_lines * STARTMENU_LINE_HEIGHT(icon_size);

        bottom_max = rect.bottom;

        if (_floating_btn)
            rect.bottom += 6;   // lower scroll arrow
        else {
            _border_top += 6;   // upper scroll arrow
            rect.bottom += 2 * 6; // upper+lower scroll arrow
        }
    }

    MoveWindow(_hwnd, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, TRUE);

    if (bottom_max) {
        POINT pt = {0, bottom_max};

        ScreenToClient(_hwnd, &pt);

        _bottom_max = pt.y;
    }
}

#else // _LIGHT_STARTMENU

LRESULT StartMenuButton::WndProc(UINT nmsg, WPARAM wparam, LPARAM lparam)
{
    switch (nmsg) {
    case WM_MOUSEMOVE:
        // automatically set the focus to startmenu entries when moving the mouse over them
        if (GetFocus() != _hwnd && !(GetWindowStyle(_hwnd)&WS_DISABLED))
            SetFocus(_hwnd);
        break;

    case WM_SETFOCUS:
        PostParent(PM_STARTENTRY_FOCUSED, _hasSubmenu, (LPARAM)_hwnd);
        goto def;

    case WM_CANCELMODE:
        // route WM_CANCELMODE to the startmenu window
        return SendParent(nmsg, wparam, lparam);

default: def:
        return super::WndProc(nmsg, wparam, lparam);
    }

    return 0;
}

void StartMenuButton::DrawItem(LPDRAWITEMSTRUCT dis)
{
    TCHAR title[BUFFER_LEN];

    GetWindowText(_hwnd, title, BUFFER_LEN);

    DrawStartMenuButton(dis->hDC, dis->rcItem, title, _hIcon,
                        _hasSubmenu,
                        !(dis->itemState & ODS_DISABLED),
                        dis->itemState & ODS_FOCUS ? true : false,
                        dis->itemState & ODS_SELECTED ? true : false);
}

#endif


struct ModernStartMenuMetrics {
    int _outer_padding;
    int _search_height;
    int _section_gap;
    int _section_button_height;
    int _section_button_width;
    int _section_item_gap;
    int _program_columns;
    int _program_tile_height;
    int _program_gap_x;
    int _program_gap_y;
    int _recommended_columns;
    int _recommended_tile_height;
    int _recommended_gap_x;
    int _recommended_gap_y;
    int _footer_height;
    int _avatar_size;
    int _power_size;
    int _program_icon_size;
    int _recommended_icon_size;
    int _search_icon_size;
    int _corner_radius;
};

static ModernStartMenuMetrics GetModernStartMenuMetrics()
{
    ModernStartMenuMetrics metrics = {
        DPI_SX(26),
        DPI_SY(42),
        DPI_SY(20),
        DPI_SY(24),
        DPI_SX(80),
        DPI_SY(12),
        6,
        DPI_SY(72),
        DPI_SX(2),
        DPI_SY(8),
        2,
        DPI_SY(54),
        DPI_SX(8),
        DPI_SY(6),
        DPI_SY(52),
        DPI_SX(32),
        DPI_SX(32),
        DPI_SX(26),
        DPI_SX(24),
        DPI_SX(18),
        DPI_SX(18)
    };
    return metrics;
}

static HFONT CreateModernStartMenuFontHelper(int point_size, int weight)
{
    HDC screen_dc = GetDC(NULL);
    int height = -MulDiv(point_size, GetDeviceCaps(screen_dc, LOGPIXELSY), 72);
    ReleaseDC(NULL, screen_dc);

    return CreateFont(height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, TEXT("Segoe UI"));
}

static RECT MakeRect(int left, int top, int right, int bottom)
{
    RECT rect = { left, top, right, bottom };
    return rect;
}

static RECT MakeRectWH(int left, int top, int width, int height)
{
    return MakeRect(left, top, left + width, top + height);
}

static bool IsNonEmptyRect(const RECT &rect)
{
    return rect.right > rect.left && rect.bottom > rect.top;
}

static void AddRoundedRectPath(Gdiplus::GraphicsPath &path, const RECT &rect, int radius)
{
    int diameter = max(1, radius * 2);
    Gdiplus::Rect rounded_rect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);

    if (rounded_rect.Width <= diameter || rounded_rect.Height <= diameter) {
        path.AddRectangle(rounded_rect);
        return;
    }

    path.AddArc(rounded_rect.X, rounded_rect.Y, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(rounded_rect.GetRight() - diameter, rounded_rect.Y, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(rounded_rect.GetRight() - diameter, rounded_rect.GetBottom() - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(rounded_rect.X, rounded_rect.GetBottom() - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
}

static void FillRoundedRectPrimitive(HDC hdc, const RECT &rect, COLORREF fill, int radius, COLORREF border = CLR_INVALID)
{
    if (!IsNonEmptyRect(rect))
        return;

    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    Gdiplus::GraphicsPath path;
    AddRoundedRectPath(path, rect, radius / 2);

    Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(fill), GetGValue(fill), GetBValue(fill)));
    graphics.FillPath(&brush, &path);

    if (border != CLR_INVALID) {
        Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(border), GetGValue(border), GetBValue(border)), 1.0f);
        graphics.DrawPath(&pen, &path);
    }
}

static void DrawSearchGlyphPrimitive(HDC hdc, const RECT &rect, COLORREF color)
{
    if (!IsNonEmptyRect(rect))
        return;

    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)), 1.6f);
    pen.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);

    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    int lens = min(width, height) - 5;
    if (lens < 6)
        lens = min(width, height);

    Gdiplus::RectF lens_rect((Gdiplus::REAL)rect.left + 1.5f,
        (Gdiplus::REAL)rect.top + 1.0f,
        (Gdiplus::REAL)lens,
        (Gdiplus::REAL)lens);
    graphics.DrawEllipse(&pen, lens_rect);

    Gdiplus::PointF handle_start(lens_rect.GetRight() - 1.5f, lens_rect.GetBottom() - 1.5f);
    Gdiplus::PointF handle_end((Gdiplus::REAL)rect.right - 1.5f, (Gdiplus::REAL)rect.bottom - 1.5f);
    graphics.DrawLine(&pen, handle_start, handle_end);
}

static void DrawVerticalScrollIndicator(HDC hdc, const RECT &list_rect, int item_count, int visible_count, int scroll_offset)
{
    if (item_count <= visible_count || visible_count <= 0 || !IsNonEmptyRect(list_rect))
        return;

    RECT track_rect = list_rect;
    track_rect.left = max(track_rect.left, track_rect.right - DPI_SX(5));
    track_rect.right -= DPI_SX(1);
    track_rect.top += DPI_SY(4);
    track_rect.bottom -= DPI_SY(4);
    if (!IsNonEmptyRect(track_rect))
        return;

    FillRoundedRectPrimitive(hdc, track_rect, RGB(60, 64, 69), DPI_SX(4));

    int track_height = track_rect.bottom - track_rect.top;
    int thumb_height = max(DPI_SY(24), track_height * visible_count / max(1, item_count));
    int max_offset = max(1, item_count - visible_count);
    int thumb_offset = (track_height - thumb_height) * scroll_offset / max_offset;
    RECT thumb_rect = MakeRectWH(track_rect.left, track_rect.top + thumb_offset,
        track_rect.right - track_rect.left, thumb_height);
    FillRoundedRectPrimitive(hdc, thumb_rect, RGB(154, 159, 166), DPI_SX(4));
}

static void DrawChevronRightPrimitive(HDC hdc, const RECT &rect, COLORREF color)
{
    HPEN pen = CreatePen(PS_SOLID, max(1, DPI_SX(2)), color);
    HGDIOBJ old_pen = SelectObject(hdc, pen);
    int center_x = (rect.left + rect.right) / 2;
    int center_y = (rect.top + rect.bottom) / 2;
    int size = max(2, DPI_SX(4));

    MoveToEx(hdc, center_x - size, center_y - size, NULL);
    LineTo(hdc, center_x, center_y);
    LineTo(hdc, center_x - size, center_y + size);

    SelectObject(hdc, old_pen);
    DeleteObject(pen);
}

static void DrawChevronLeftPrimitive(HDC hdc, const RECT &rect, COLORREF color)
{
    HPEN pen = CreatePen(PS_SOLID, max(1, DPI_SX(2)), color);
    HGDIOBJ old_pen = SelectObject(hdc, pen);
    int center_x = (rect.left + rect.right) / 2;
    int center_y = (rect.top + rect.bottom) / 2;
    int size = max(2, DPI_SX(4));

    MoveToEx(hdc, center_x + size, center_y - size, NULL);
    LineTo(hdc, center_x, center_y);
    LineTo(hdc, center_x + size, center_y + size);

    SelectObject(hdc, old_pen);
    DeleteObject(pen);
}

static RECT GetSectionActionChevronRect(const RECT &button_rect)
{
    return MakeRectWH(button_rect.right - DPI_SX(15),
        button_rect.top,
        DPI_SX(10),
        button_rect.bottom - button_rect.top);
}

static RECT GetSectionActionTextRect(const RECT &button_rect)
{
    RECT text_rect = button_rect;
    RECT chevron_rect = GetSectionActionChevronRect(button_rect);
    text_rect.left += DPI_SX(12);
    text_rect.right = max(text_rect.left, chevron_rect.left - DPI_SX(8));
    return text_rect;
}

static void DrawSectionActionButtonContent(HDC hdc, const RECT &button_rect, const TCHAR *label, HFONT font, COLORREF color, bool back_chevron)
{
    RECT text_rect = GetSectionActionTextRect(button_rect);
    RECT chevron_rect = GetSectionActionChevronRect(button_rect);
    SelectObject(hdc, font ? font : g_Globals._hDefaultFont);
    SetTextColor(hdc, color);
    DrawText(hdc, label, -1, &text_rect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    if (back_chevron)
        DrawChevronLeftPrimitive(hdc, chevron_rect, color);
    else
        DrawChevronRightPrimitive(hdc, chevron_rect, color);
}

static BOOL ApplyBlurToPopupWindow(HWND hwnd, COLORREF color, BYTE alpha)
{
    static pfnSetWindowCompositionAttribute set_window_composition_attribute = NULL;

    if (!set_window_composition_attribute) {
        HMODULE user32 = GetModuleHandle(TEXT("user32.dll"));
        if (user32)
            set_window_composition_attribute = (pfnSetWindowCompositionAttribute)GetProcAddress(user32, "SetWindowCompositionAttribute");
    }

    if (!set_window_composition_attribute)
        return FALSE;

    ACCENT_POLICY accent = { ACCENT_ENABLE_BLURBEHIND, 0, color | ((DWORD)alpha << 24), 0 };
    WINDOWCOMPOSITIONATTRIBDATA data = { WCA_ACCENT_POLICY, &accent, sizeof(accent) };
    return set_window_composition_attribute(hwnd, &data);
}

static String FormatRecentItemMeta(Entry *entry)
{
    if (!entry)
        return TEXT("Recent item");

    const FILETIME &file_time = entry->_data.ftLastWriteTime;
    if (!file_time.dwLowDateTime && !file_time.dwHighDateTime)
        return TEXT("Recent item");

    FILETIME now_ft;
    GetSystemTimeAsFileTime(&now_ft);

    ULARGE_INTEGER now_value;
    now_value.LowPart = now_ft.dwLowDateTime;
    now_value.HighPart = now_ft.dwHighDateTime;

    ULARGE_INTEGER item_value;
    item_value.LowPart = file_time.dwLowDateTime;
    item_value.HighPart = file_time.dwHighDateTime;

    if (item_value.QuadPart >= now_value.QuadPart)
        return TEXT("Just now");

    ULONGLONG diff_minutes = (now_value.QuadPart - item_value.QuadPart) / (10000000ULL * 60ULL);
    TCHAR buffer[64] = { 0 };

    if (diff_minutes < 1)
        return TEXT("Just now");

    if (diff_minutes < 60) {
        _stprintf_s(buffer, TEXT("%um ago"), (unsigned int)diff_minutes);
        return String(buffer);
    }

    if (diff_minutes < 60 * 24) {
        _stprintf_s(buffer, TEXT("%uh ago"), (unsigned int)(diff_minutes / 60));
        return String(buffer);
    }

    if (diff_minutes < 60 * 24 * 7) {
        _stprintf_s(buffer, TEXT("%ud ago"), (unsigned int)(diff_minutes / (60 * 24)));
        return String(buffer);
    }

    FILETIME local_time;
    SYSTEMTIME local_system_time;
    if (FileTimeToLocalFileTime(&file_time, &local_time) && FileTimeToSystemTime(&local_time, &local_system_time) &&
        GetDateFormat(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &local_system_time, NULL, buffer, COUNTOF(buffer))) {
        return String(buffer);
    }

    return TEXT("Recent item");
}

static bool CompareStartMenuItemByTitle(const ModernStartMenuItem &left, const ModernStartMenuItem &right)
{
    return _tcsicmp(left._title.c_str(), right._title.c_str()) < 0;
}

static bool StringContainsInsensitive(const String &value, const String &needle_lower)
{
    if (needle_lower.empty())
        return true;

    String lower_value = value;
    lower_value.toLower();
    return lower_value.find(needle_lower) != String::npos;
}

static String EnsureTrailingBackslash(String path)
{
    if (!path.empty() && path.at(path.length() - 1) != TEXT('\\'))
        path += TEXT("\\");

    return path;
}

static bool TryExpandSearchEnvironmentCandidate(const String &candidate, String &expanded)
{
    TCHAR buffer[MAX_PATH * 8] = { 0 };
    DWORD count = ExpandEnvironmentStrings(candidate.c_str(), buffer, COUNTOF(buffer));
    if (count == 0 || count > COUNTOF(buffer))
        return false;

    if (!_tcscmp(buffer, candidate.c_str()))
        return false;

    expanded = buffer;
    return true;
}

static String ExpandSearchEnvironment(String query)
{
    String expanded = query;
    String candidate_expansion;

    if (TryExpandSearchEnvironmentCandidate(expanded, candidate_expansion))
        expanded = candidate_expansion;

    size_t separator = expanded.find_first_of(TEXT("\\/"));
    size_t prefix_length = separator == String::npos ? expanded.length() : separator;
    if (prefix_length == 0)
        return expanded;

    String prefix = expanded.substr(0, prefix_length);
    if (prefix.find(TEXT('%')) != String::npos || prefix.find(TEXT(':')) != String::npos)
        return expanded;

    String env_candidate = TEXT("%") + prefix + TEXT("%");
    if (separator != String::npos)
        env_candidate += expanded.substr(separator);

    if (TryExpandSearchEnvironmentCandidate(env_candidate, candidate_expansion))
        return candidate_expansion;

    return expanded;
}

static String NormalizeSearchPath(String query)
{
    for (size_t index = 0; index < query.length(); ++index) {
        if (query.at(index) == TEXT('/'))
            query.at(index) = TEXT('\\');
    }

    query = ExpandSearchEnvironment(query);

    for (size_t index = 0; index < query.length(); ++index) {
        if (query.at(index) == TEXT('/'))
            query.at(index) = TEXT('\\');
    }

    if (query.length() == 2 && query.at(1) == TEXT(':'))
        query += TEXT("\\");

    return query;
}

static bool LooksLikePathQuery(const String &query)
{
    return query.find(TEXT(":\\")) != String::npos || query.find(TEXT("\\")) != String::npos || query.find(TEXT("/")) != String::npos;
}

static bool ResolvePathQueryParts(const String &query, String &base_dir, String &fragment)
{
    String normalized = NormalizeSearchPath(query);
    if (normalized.empty())
        return false;

    if (PathFileExists(normalized.c_str()) && PathIsDirectory(normalized.c_str())) {
        base_dir = EnsureTrailingBackslash(normalized);
        fragment.erase();
        return true;
    }

    size_t slash_pos = normalized.find_last_of(TEXT("\\"));
    if (slash_pos == String::npos)
        return false;

    base_dir = normalized.substr(0, slash_pos + 1);
    fragment = normalized.substr(slash_pos + 1);

    return PathFileExists(base_dir.c_str()) && PathIsDirectory(base_dir.c_str()) != FALSE;
}

static bool SearchResultExists(const vector<ModernStartMenuItem> &items, const ModernStartMenuItem &candidate)
{
    for (size_t index = 0; index < items.size(); ++index) {
        if (!candidate._path.empty()) {
            if (!_tcsicmp(items[index]._path.c_str(), candidate._path.c_str()))
                return true;
        } else if (!_tcsicmp(items[index]._title.c_str(), candidate._title.c_str())) {
            return true;
        }
    }

    return false;
}

static bool HasRenderableIcon(const Icon &icon)
{
    return icon.get_icontype() == IT_SYSCACHE || icon.get_hicon() != NULL;
}

static ICON_ID ExtractPathResultIcon(const String &path, int icon_size)
{
    if (path.empty())
        return ICID_NONE;

    SHFILEINFO sfi = { 0 };
    UINT shgfi_flags = SHGFI_ICON | (icon_size >= ICON_SIZE_LARGE ? SHGFI_LARGEICON : SHGFI_SMALLICON);
    if (SHGetFileInfo(path.c_str(), 0, &sfi, sizeof(sfi), shgfi_flags) && sfi.hIcon)
        return (ICON_ID)g_Globals._icon_cache.add(sfi.hIcon, IT_CACHED);

    sfi = SHFILEINFO();
    shgfi_flags = SHGFI_SYSICONINDEX | (icon_size >= ICON_SIZE_LARGE ? SHGFI_LARGEICON : SHGFI_SMALLICON);
    HIMAGELIST himl = (HIMAGELIST)SHGetFileInfo(path.c_str(), 0, &sfi, sizeof(sfi), shgfi_flags);
    if (himl) {
        HICON hicon = ImageList_GetIcon(himl, sfi.iIcon, ILD_NORMAL);
        if (hicon)
            return (ICON_ID)g_Globals._icon_cache.add(hicon, IT_CACHED);
    }

    try {
        ShellPath shell_path(path.c_str());
        if ((LPCITEMIDLIST)shell_path) {
            const Icon &pidl_icon = g_Globals._icon_cache.extract((LPCITEMIDLIST)shell_path, ICF_FROM_ICON_SIZE(icon_size));
            if (HasRenderableIcon(pidl_icon))
                return (ICON_ID)pidl_icon;
        }
    } catch (COMException &) {
    }

    return ICID_NONE;
}

static bool ModernStartMenuHasMeta(const vector<ModernStartMenuItem> &items, const String &meta_text)
{
    for (size_t index = 0; index < items.size(); ++index) {
        if (!_tcsicmp(items[index]._meta_text.c_str(), meta_text.c_str()))
            return true;
    }

    return false;
}

static bool TryGetEntryLaunchPath(Entry *entry, String &launch_path)
{
    TCHAR entry_path[MAX_PATH] = { 0 };
    if (!entry || !entry->get_path(entry_path, COUNTOF(entry_path)) || !entry_path[0])
        return false;

    launch_path = entry_path;
    if (PathMatchSpec(entry_path, TEXT("*.lnk"))) {
        TCHAR target_path[MAX_PATH] = { 0 };
        GetShortcutPath(entry_path, target_path, COUNTOF(target_path));
        if (target_path[0])
            launch_path = target_path;
    }

    return !launch_path.empty();
}

static bool IsExecutableLaunchPath(const String &launch_path)
{
    return !launch_path.empty() && PathMatchSpec(launch_path.c_str(), TEXT("*.exe"));
}

static bool TryGetRecentApplicationPath(Entry *entry, String &launch_path)
{
    return TryGetEntryLaunchPath(entry, launch_path) && IsExecutableLaunchPath(launch_path);
}

static bool TryResolveStartMenuItemPath(const ModernStartMenuItem &item, String &resolved_path)
{
    if (!item._path.empty()) {
        resolved_path = NormalizeSearchPath(item._path);
        return true;
    }

    if (item._entry)
        return TryGetEntryLaunchPath(item._entry, resolved_path);

    return false;
}

static String TrimTrailingBackslash(String path)
{
    while (path.length() > 3 && path.at(path.length() - 1) == TEXT('\\'))
        path.erase(path.length() - 1);

    return path;
}

static String GetSearchResultTitleText(const ModernStartMenuItem &item)
{
    String resolved_path;
    if (TryResolveStartMenuItemPath(item, resolved_path)) {
        DWORD attributes = GetFileAttributes(resolved_path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
            return EnsureTrailingBackslash(resolved_path);
        return resolved_path;
    }

    return item._title;
}

static String GetSearchResultLocationText(const ModernStartMenuItem &item)
{
    String resolved_path;
    if (TryResolveStartMenuItemPath(item, resolved_path))
        return TrimTrailingBackslash(resolved_path);

    return item._meta_text;
}

static String GetSearchResultTypeText(const ModernStartMenuItem &item)
{
    String resolved_path;
    if (TryResolveStartMenuItemPath(item, resolved_path)) {
        DWORD attributes = GetFileAttributes(resolved_path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
            return TEXT("File folder");

        SHFILEINFO sfi = { 0 };
        DWORD attr = attributes == INVALID_FILE_ATTRIBUTES ? FILE_ATTRIBUTE_NORMAL : attributes;
        if (SHGetFileInfo(resolved_path.c_str(), attr, &sfi, sizeof(sfi), SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES) && sfi.szTypeName[0])
            return String(sfi.szTypeName);
    }

    if (!item._meta_text.empty())
        return item._meta_text;

    return TEXT("Search result");
}

static bool CopyTextToClipboard(HWND hwnd, const String &text)
{
    if (text.empty() || !OpenClipboard(hwnd))
        return false;

    EmptyClipboard();

    SIZE_T byte_count = (text.length() + 1) * sizeof(TCHAR);
    HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, byte_count);
    if (!global) {
        CloseClipboard();
        return false;
    }

    void *memory = GlobalLock(global);
    if (!memory) {
        GlobalFree(global);
        CloseClipboard();
        return false;
    }

    memcpy(memory, text.c_str(), byte_count);
    GlobalUnlock(global);

#ifdef UNICODE
    if (!SetClipboardData(CF_UNICODETEXT, global)) {
#else
    if (!SetClipboardData(CF_TEXT, global)) {
#endif
        GlobalFree(global);
        CloseClipboard();
        return false;
    }

    CloseClipboard();
    return true;
}

static bool CopyItemToClipboard(HWND hwnd, const String &path)
{
    if (path.empty() || !PathFileExists(path.c_str()) || !OpenClipboard(hwnd))
        return false;

    EmptyClipboard();

    SIZE_T byte_count = sizeof(DROPFILES) + (path.length() + 2) * sizeof(TCHAR);
    HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, byte_count);
    if (!global) {
        CloseClipboard();
        return false;
    }

    DROPFILES *drop_files = (DROPFILES *)GlobalLock(global);
    if (!drop_files) {
        GlobalFree(global);
        CloseClipboard();
        return false;
    }

    drop_files->pFiles = sizeof(DROPFILES);
#ifdef UNICODE
    drop_files->fWide = TRUE;
#endif
    memcpy((LPBYTE)drop_files + sizeof(DROPFILES), path.c_str(), path.length() * sizeof(TCHAR));
    GlobalUnlock(global);

    if (!SetClipboardData(CF_HDROP, global)) {
        GlobalFree(global);
        CloseClipboard();
        return false;
    }

    UINT preferred_drop_effect = RegisterClipboardFormat(CFSTR_PREFERREDDROPEFFECT);
    HGLOBAL effect_memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DWORD));
    if (effect_memory) {
        DWORD *effect = (DWORD *)GlobalLock(effect_memory);
        if (effect) {
            *effect = DROPEFFECT_COPY;
            GlobalUnlock(effect_memory);
            SetClipboardData(preferred_drop_effect, effect_memory);
        } else {
            GlobalFree(effect_memory);
        }
    }

    CloseClipboard();
    return true;
}

struct StartMenuContextMenuEntry {
    int     _command;
    LPCTSTR _label;
    bool    _enabled;
};

struct StartMenuContextMenuCreateInfo {
    StartMenuContextMenuCreateInfo()
        : _owner(NULL), _font(NULL), _screen_anchor(), _can_copy(false), _can_copy_path(false)
    {
    }

    HWND    _owner;
    HFONT   _font;
    POINT   _screen_anchor;
    bool    _can_copy;
    bool    _can_copy_path;
};

struct StartMenuContextMenuPopup : public Window {
    typedef Window super;

    StartMenuContextMenuPopup(HWND hwnd, const StartMenuContextMenuCreateInfo &info)
        : super(hwnd),
          _owner(info._owner),
          _font(info._font ? info._font : g_Globals._hDefaultFont),
          _screen_anchor(info._screen_anchor),
          _result(0),
          _hot_index(-1),
          _tracking_mouse(false)
    {
        _items.push_back(StartMenuContextMenuEntry{ STARTMENU_CONTEXT_OPEN, TEXT("Open"), true });
        _items.push_back(StartMenuContextMenuEntry{ STARTMENU_CONTEXT_COPY, TEXT("Copy"), info._can_copy });
        _items.push_back(StartMenuContextMenuEntry{ STARTMENU_CONTEXT_COPY_PATH, TEXT("Copy path"), info._can_copy_path });
    }

    static WindowClass &GetWndClass()
    {
        static WindowClass s_wc(TEXT("ExplauncherObjectContextMenu"), CS_HREDRAW | CS_VREDRAW);
        s_wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        s_wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
        return s_wc;
    }

    static int Show(HWND owner, HFONT font, POINT screen_anchor, bool can_copy, bool can_copy_path)
    {
        StartMenuContextMenuCreateInfo info;
        info._owner = owner;
        info._font = font;
        info._screen_anchor = screen_anchor;
        info._can_copy = can_copy;
        info._can_copy_path = can_copy_path;

        SIZE size = MeasurePopup(font);
        RECT rect = MakeRectWH(screen_anchor.x, screen_anchor.y, size.cx, size.cy);
        MONITORINFO monitor_info = { sizeof(monitor_info) };
        GetMonitorInfo(MonitorFromPoint(screen_anchor, MONITOR_DEFAULTTONEAREST), &monitor_info);

        if (rect.right > monitor_info.rcWork.right)
            OffsetRect(&rect, monitor_info.rcWork.right - rect.right, 0);
        if (rect.bottom > monitor_info.rcWork.bottom)
            OffsetRect(&rect, 0, monitor_info.rcWork.bottom - rect.bottom);
        if (rect.left < monitor_info.rcWork.left)
            OffsetRect(&rect, monitor_info.rcWork.left - rect.left, 0);
        if (rect.top < monitor_info.rcWork.top)
            OffsetRect(&rect, 0, monitor_info.rcWork.top - rect.top);

        HWND hwnd = Window::Create(WINDOW_CREATOR_INFO(StartMenuContextMenuPopup, StartMenuContextMenuCreateInfo), &info,
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST, GetWndClass(), TEXT(""),
            WS_POPUP, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, owner);
        StartMenuContextMenuPopup *popup = GET_WINDOW(StartMenuContextMenuPopup, hwnd);
        return popup ? popup->ShowModal() : 0;
    }

protected:
    static SIZE MeasurePopup(HFONT font)
    {
        SIZE result = { DPI_SX(196), DPI_SY(8) * 2 + 3 * DPI_SY(34) + 2 * DPI_SY(4) };
        HDC screen_dc = GetDC(NULL);
        HFONT old_font = (HFONT)SelectObject(screen_dc, font ? font : g_Globals._hDefaultFont);
        LPCTSTR labels[] = { TEXT("Open"), TEXT("Copy"), TEXT("Copy path") };

        for (int index = 0; index < COUNTOF(labels); ++index) {
            SIZE text_size = { 0 };
            GetTextExtentPoint32(screen_dc, labels[index], _tcslen(labels[index]), &text_size);
            result.cx = max(result.cx, text_size.cx + DPI_SX(48));
        }

        SelectObject(screen_dc, old_font);
        ReleaseDC(NULL, screen_dc);
        return result;
    }

    RECT GetPanelRect() const
    {
        ClientRect client(_hwnd);
        return MakeRect(0, 0, client.right, client.bottom);
    }

    RECT GetItemRect(int index) const
    {
        RECT panel_rect = GetPanelRect();
        int item_height = DPI_SY(34);
        int item_gap = DPI_SY(4);
        int padding = DPI_SY(8);
        return MakeRectWH(panel_rect.left + DPI_SX(8),
            panel_rect.top + padding + index * (item_height + item_gap),
            (panel_rect.right - panel_rect.left) - DPI_SX(16),
            item_height);
    }

    void UpdateHotIndex(POINT pt)
    {
        int hot_index = -1;
        for (int index = 0; index < (int)_items.size(); ++index) {
            RECT item_rect = GetItemRect(index);
            if (PtInRect(&item_rect, pt)) {
                hot_index = index;
                break;
            }
        }

        if (hot_index != _hot_index) {
            _hot_index = hot_index;
            InvalidateRect(_hwnd, NULL, FALSE);
        }
    }

    void ApplyWindowRegion()
    {
        ClientRect client(_hwnd);
        HRGN region = CreateRoundRectRgn(0, 0, client.right + 1, client.bottom + 1, DPI_SX(16), DPI_SX(16));
        if (region)
            SetWindowRgn(_hwnd, region, TRUE);
    }

    void Paint(HDC canvas)
    {
        RECT panel_rect = GetPanelRect();
        COLORREF panel_fill = RGB(34, 37, 42);
        COLORREF panel_border = RGB(91, 96, 102);
        COLORREF item_hover_fill = RGB(72, 77, 84);
        COLORREF item_text = RGB(239, 242, 245);
        COLORREF item_disabled = RGB(138, 143, 149);

        FillRoundedRectPrimitive(canvas, panel_rect, panel_fill, DPI_SX(16), panel_border);

        HFONT old_font = (HFONT)SelectObject(canvas, _font ? _font : g_Globals._hDefaultFont);
        int old_bk_mode = SetBkMode(canvas, TRANSPARENT);

        for (int index = 0; index < (int)_items.size(); ++index) {
            const StartMenuContextMenuEntry &item = _items[index];
            RECT item_rect = GetItemRect(index);
            if (_hot_index == index && item._enabled)
                FillRoundedRectPrimitive(canvas, item_rect, item_hover_fill, DPI_SX(10));

            RECT text_rect = item_rect;
            text_rect.left += DPI_SX(14);
            SetTextColor(canvas, item._enabled ? item_text : item_disabled);
            DrawText(canvas, item._label, -1, &text_rect,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }

        SetBkMode(canvas, old_bk_mode);
        SelectObject(canvas, old_font);
    }

    int ShowModal()
    {
        ShowWindow(_hwnd, SW_SHOW);
        UpdateWindow(_hwnd);
        SetForegroundWindow(_hwnd);
        SetFocus(_hwnd);
        SetCapture(_hwnd);

        MSG msg;
        while (IsWindow(_hwnd) && IsWindowVisible(_hwnd)) {
            if (!GetMessage(&msg, 0, 0, 0)) {
                PostQuitMessage((int)msg.wParam);
                break;
            }

            try {
                if (pretranslate_msg(&msg))
                    continue;

                if (dispatch_dialog_msg(&msg))
                    continue;

                TranslateMessage(&msg);
                DispatchMessage(&msg);
            } catch (COMException &e) {
                HandleException(e, _hwnd);
            }
        }

        if (GetCapture() == _hwnd)
            ReleaseCapture();

        return _result;
    }

    void CloseMenu(int result)
    {
        _result = result;
        if (IsWindow(_hwnd))
            DestroyWindow(_hwnd);
    }

    virtual LRESULT Init(LPCREATESTRUCT pcs)
    {
        UNREFERENCED_PARAMETER(pcs);
        ApplyWindowRegion();
        ApplyBlurToPopupWindow(_hwnd, RGB(34, 37, 42), 220);
        return 0;
    }

    virtual LRESULT WndProc(UINT nmsg, WPARAM wparam, LPARAM lparam)
    {
        switch (nmsg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            BufferedPaintCanvas canvas(_hwnd);
            Paint(canvas);
            return 0;
        }

        case WM_SIZE:
            ApplyWindowRegion();
            return 0;

        case WM_MOUSEMOVE: {
            if (!_tracking_mouse) {
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, _hwnd, 0 };
                TrackMouseEvent(&tme);
                _tracking_mouse = true;
            }
            UpdateHotIndex(Point(lparam));
            return 0;
        }

        case WM_MOUSELEAVE:
            _tracking_mouse = false;
            _hot_index = -1;
            InvalidateRect(_hwnd, NULL, FALSE);
            return 0;

        case WM_LBUTTONUP:
        case WM_RBUTTONUP: {
            POINT pt = Point(lparam);
            UpdateHotIndex(pt);
            if (_hot_index >= 0 && _hot_index < (int)_items.size() && _items[_hot_index]._enabled)
                CloseMenu(_items[_hot_index]._command);
            else
                CloseMenu(0);
            return 0;
        }

        case WM_CAPTURECHANGED:
            if ((HWND)lparam != _hwnd)
                CloseMenu(0);
            return 0;

        case WM_KILLFOCUS:
            if ((HWND)wparam != _owner)
                CloseMenu(0);
            return 0;

        case WM_KEYDOWN:
            switch (wparam) {
            case VK_ESCAPE:
                CloseMenu(0);
                return 0;
            case VK_RETURN:
                if (_hot_index >= 0 && _hot_index < (int)_items.size() && _items[_hot_index]._enabled)
                    CloseMenu(_items[_hot_index]._command);
                return 0;
            case VK_DOWN:
                if (_hot_index < (int)_items.size() - 1)
                    ++_hot_index;
                else
                    _hot_index = 0;
                InvalidateRect(_hwnd, NULL, FALSE);
                return 0;
            case VK_UP:
                if (_hot_index > 0)
                    --_hot_index;
                else
                    _hot_index = (int)_items.size() - 1;
                InvalidateRect(_hwnd, NULL, FALSE);
                return 0;
            default:
                break;
            }
            break;

        case WM_NCHITTEST:
            return HTCLIENT;
        }

        return super::WndProc(nmsg, wparam, lparam);
    }

    HWND    _owner;
    HFONT   _font;
    POINT   _screen_anchor;
    vector<StartMenuContextMenuEntry> _items;
    int     _result;
    int     _hot_index;
    bool    _tracking_mouse;
};

static ICON_ID GetPathQueryIconId(LPCTSTR path, DWORD file_attributes)
{
    if (file_attributes & FILE_ATTRIBUTE_DIRECTORY)
        return ICID_FOLDER;

    if (PathMatchSpec(path, TEXT("*.exe")) || PathMatchSpec(path, TEXT("*.lnk")))
        return ICID_APP;

    return ICID_SEARCH_DOC;
}

static Entry *GetPrimaryStartMenuEntry(const StartMenuEntry &entry)
{
    Entry *fallback = NULL;

    for (ShellEntrySet::const_iterator it = entry._entries.begin(); it != entry._entries.end(); ++it) {
        Entry *candidate = const_cast<Entry *>(*it);

        if (!fallback)
            fallback = candidate;

        if (!(candidate->_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            return candidate;
    }

    return fallback;
}

static bool StartMenuEntryCanLaunch(const StartMenuEntry &entry)
{
    for (ShellEntrySet::const_iterator it = entry._entries.begin(); it != entry._entries.end(); ++it) {
        if (!((*it)->_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            return true;
    }

    return false;
}

static bool ModernStartMenuHasTitle(const vector<ModernStartMenuItem> &items, LPCTSTR title)
{
    for (size_t index = 0; index < items.size(); ++index) {
        if (!_tcsicmp(items[index]._title.c_str(), title))
            return true;
    }

    return false;
}

static String GetModernStartMenuUserName()
{
    TCHAR user_name[256] = { 0 };
    DWORD user_name_len = COUNTOF(user_name);

    if (GetUserName(user_name, &user_name_len) && user_name[0])
        return String(user_name);

    return TEXT("User");
}

static RECT CalculateModernStartMenuRect(HWND hwnd_start_button = NULL)
{
    int screen_width = GetSystemMetrics(SM_CXSCREEN);
    int screen_height = GetSystemMetrics(SM_CYSCREEN);
    int width = min(DPI_SX(660), screen_width - DPI_SX(32));
    int height = min(DPI_SY(720), screen_height - JCfg_GetDesktopBarHeightWithDPI() - DPI_SY(20));

    if (width < DPI_SX(480))
        width = screen_width - DPI_SX(16);
    if (height < DPI_SY(560))
        height = screen_height - JCfg_GetDesktopBarHeightWithDPI() - DPI_SY(10);

    int x = (screen_width - width) / 2;
    if (!taskbar_draw::IsCenteredEnabled()) {
        x = DPI_SX(8);
        if (hwnd_start_button && IsWindow(hwnd_start_button)) {
            RECT button_rect = { 0 };
            if (GetWindowRect(hwnd_start_button, &button_rect))
                x = max(DPI_SX(8), button_rect.left + DPI_SX(8));
        }
    }

    int max_x = screen_width - width - DPI_SX(8);
    if (x > max_x)
        x = max_x;
    if (x < DPI_SX(8))
        x = DPI_SX(8);

    int y = screen_height - JCfg_GetDesktopBarHeightWithDPI() - height - DPI_SY(8);
    if (y < DPI_SY(8))
        y = DPI_SY(8);

    return MakeRectWH(x, y, width, height);
}

static void RedrawStartMenuWindowNow(HWND hwnd)
{
    RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_FRAME);
}

static double GetAnimationClockMilliseconds()
{
    static LARGE_INTEGER frequency = { 0 };
    if (frequency.QuadPart == 0)
        QueryPerformanceFrequency(&frequency);

    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return frequency.QuadPart ? ((double)counter.QuadPart * 1000.0 / (double)frequency.QuadPart) : 0.0;
}

static void WaitForAnimationFrame()
{
    if (FAILED(DwmFlush()))
        Sleep(1);
}

static float EaseOutCubic(float t)
{
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

static void AnimateStartMenuSlide(HWND hwnd, HWND insert_after, const RECT &target_rect, bool showing)
{
    const int width = target_rect.right - target_rect.left;
    const int height = target_rect.bottom - target_rect.top;
    const int offset = max(DPI_SY(36), height / 14);
    const int start_top = showing ? target_rect.top + offset : target_rect.top;
    const int end_top = showing ? target_rect.top : target_rect.top + offset;
    const double duration_ms = 190.0;

    if (showing) {
        SetWindowPos(hwnd, insert_after, target_rect.left, start_top, width, height,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
        RedrawStartMenuWindowNow(hwnd);
    }

    double start_ms = GetAnimationClockMilliseconds();
    for (;;) {
        double elapsed = GetAnimationClockMilliseconds() - start_ms;
        if (elapsed > duration_ms)
            elapsed = duration_ms;

        float t = duration_ms > 0.0 ? (float)(elapsed / duration_ms) : 1.0f;
        float eased = EaseOutCubic(t);
        int current_top = start_top + (int)((end_top - start_top) * eased + (end_top >= start_top ? 0.5f : -0.5f));

        SetWindowPos(hwnd, insert_after, target_rect.left, current_top, width, height, SWP_NOACTIVATE);
        RedrawStartMenuWindowNow(hwnd);

        if (elapsed >= duration_ms)
            break;

        WaitForAnimationFrame();
    }

    if (!showing) {
        ShowWindow(hwnd, SW_HIDE);
        SetWindowPos(hwnd, insert_after, target_rect.left, target_rect.top, width, height, SWP_NOACTIVATE);
    }
}

StartMenuRoot::StartMenuRoot(HWND hwnd, const StartMenuRootCreateInfo &info)
    :  super(hwnd, info._icon_size),
       _hwndStartButton(0),
       _panel_width(0),
       _panel_height(0),
       _program_icon_size(GetModernStartMenuMetrics()._program_icon_size),
       _recommended_icon_size(GetModernStartMenuMetrics()._recommended_icon_size),
         _search_result_scroll(0),
         _all_program_scroll(0),
         _drive_folder_scroll(0),
                 _recent_document_scroll(0),
         _search_selected_index(-1),
         _hwndSearchEdit(NULL),
         _search_edit_brush(NULL),
                 _search_edit_fill(CLR_INVALID),
       _show_all_programs(false),
             _show_all_recents(false),
         _search_active(false),
       _hot_area(HOT_NONE),
       _hot_index(-1),
       _tracking_mouse(false),
       _title_font(NULL),
       _section_font(NULL),
             _search_font(NULL),
       _item_font(NULL),
       _meta_font(NULL),
         _search_query(),
       _user_name(GetModernStartMenuUserName())
{
    if (!g_Globals._SHRestricted || !SHRestricted(REST_NOCOMMONGROUPS))
        try {
            ShellDirectory cmn_startmenu(GetDesktopFolder(), SpecialFolderPath(CSIDL_COMMON_STARTMENU, _hwnd), _hwnd);
            _dirs.push_back(StartMenuDirectory(cmn_startmenu, (LPCTSTR)SpecialFolderFSPath(CSIDL_COMMON_PROGRAMS, _hwnd)));
        } catch (COMException &) {
        }

    try {
        ShellDirectory usr_startmenu(GetDesktopFolder(), SpecialFolderPath(CSIDL_STARTMENU, _hwnd), _hwnd);
        _dirs.push_back(StartMenuDirectory(usr_startmenu, (LPCTSTR)SpecialFolderFSPath(CSIDL_PROGRAMS, _hwnd)));
    } catch (COMException &) {
    }
}

StartMenuRoot::~StartMenuRoot()
{
    if (_search_edit_brush)
        DeleteObject(_search_edit_brush);
    if (_title_font)
        DeleteObject(_title_font);
    if (_section_font)
        DeleteObject(_section_font);
    if (_search_font)
        DeleteObject(_search_font);
    if (_item_font)
        DeleteObject(_item_font);
    if (_meta_font)
        DeleteObject(_meta_font);
}

HWND StartMenuRoot::Create(HWND hwndOwner, int icon_size)
{
    RECT rect = CalculateModernStartMenuRect(hwndOwner);
    StartMenuRootCreateInfo create_info;
    create_info._icon_size = icon_size;

    return Window::Create(WINDOW_CREATOR_INFO(StartMenuRoot, StartMenuRootCreateInfo), &create_info,
        WS_EX_TOOLWINDOW, GetWndClasss(), TITLE_STARTMENU,
        WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
    rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, 0);
}

HFONT StartMenuRoot::CreateMenuFont(int point_size, int weight) const
{
    return CreateModernStartMenuFontHelper(point_size, weight);
}

int StartMenuRoot::GetVisibleProgramCount() const
{
    if (_show_all_programs || _show_all_recents || IsSearchResultsVisible() || IsSearchHomeVisible())
        return 0;

    return min(12, (int)_program_items.size());
}

int StartMenuRoot::GetVisibleRecommendedCount() const
{
    if (_show_all_programs || _show_all_recents || IsSearchResultsVisible() || IsSearchHomeVisible())
        return 0;

    return min(6, (int)_recommended_items.size());
}

int StartMenuRoot::GetVisibleRecentDocumentCount() const
{
    if (!_show_all_recents || IsSearchResultsVisible() || IsSearchHomeVisible())
        return 0;

    RECT list_rect = GetRecentDocumentsListRect();
    int row_height = DPI_SY(54);
    int row_gap = DPI_SY(6);
    int row_pitch = row_height + row_gap;
    int available = list_rect.bottom - list_rect.top;
    int count = row_pitch > 0 ? (available + row_gap) / row_pitch : 0;

    return min(max(0, count), max(0, (int)_recommended_items.size() - _recent_document_scroll));
}

bool StartMenuRoot::IsSearchResultsVisible() const
{
    return !_search_query.empty();
}

bool StartMenuRoot::IsSearchHomeVisible() const
{
    return _search_active && _search_query.empty();
}

int StartMenuRoot::GetSelectedSearchResultIndex() const
{
    if (_search_results.empty())
        return -1;

    if (_search_selected_index >= 0 && _search_selected_index < (int)_search_results.size())
        return _search_selected_index;

    return 0;
}

COLORREF StartMenuRoot::GetSearchFillColor() const
{
    return (_search_active || IsSearchResultsVisible()) ? RGB(46, 49, 54) : RGB(39, 42, 46);
}

static int GetSearchResultRowHeight()
{
    return DPI_SY(52);
}

static int GetSearchResultRowGap(bool show_separator_after)
{
    return DPI_SY(6) + (show_separator_after ? DPI_SY(16) : 0);
}

int StartMenuRoot::GetVisibleAllProgramCount() const
{
    RECT list_rect = GetAllAppsListRect();
    int row_height = DPI_SY(34);
    int row_gap = DPI_SY(6);
    int row_pitch = row_height + row_gap;
    int available = list_rect.bottom - list_rect.top;
    int count = row_pitch > 0 ? (available + row_gap) / row_pitch : 0;

    return min(max(0, count), max(0, (int)_all_program_items.size() - _all_program_scroll));
}

int StartMenuRoot::GetVisibleDriveFolderCount() const
{
    RECT list_rect = GetDriveFoldersListRect();
    int row_height = DPI_SY(34);
    int row_gap = DPI_SY(6);
    int row_pitch = row_height + row_gap;
    int available = list_rect.bottom - list_rect.top;
    int count = row_pitch > 0 ? (available + row_gap) / row_pitch : 0;

    return min(max(0, count), max(0, (int)_drive_folder_items.size() - _drive_folder_scroll));
}

int StartMenuRoot::GetVisibleSearchResultCount() const
{
    RECT list_rect = GetSearchResultsRect();
    int row_height = GetSearchResultRowHeight();
    int top = list_rect.top;
    int count = 0;

    for (int index = _search_result_scroll; index < (int)_search_results.size(); ++index) {
        if (top + row_height > list_rect.bottom)
            break;

        ++count;
        top += row_height + GetSearchResultRowGap(_search_results[index]._show_separator_after);
    }

    return count;
}

int StartMenuRoot::GetVisibleSearchHomeRecentCount() const
{
    RECT list_rect = GetSearchHomeRecentListRect();
    int row_height = DPI_SY(40);
    int row_gap = DPI_SY(8);
    int row_pitch = row_height + row_gap;
    int available = list_rect.bottom - list_rect.top;
    int count = row_pitch > 0 ? (available + row_gap) / row_pitch : 0;

    return min(max(0, count), (int)_search_recent_items.size());
}

int StartMenuRoot::GetVisibleSearchHomeTopAppCount() const
{
    if (!_all_program_items.empty())
        return min(6, (int)_all_program_items.size());

    return min(6, (int)_program_items.size());
}

RECT StartMenuRoot::GetSearchRect() const
{
    ClientRect client(_hwnd);
    ModernStartMenuMetrics metrics = GetModernStartMenuMetrics();
    return MakeRectWH(metrics._outer_padding, metrics._outer_padding,
        client.right - metrics._outer_padding * 2, metrics._search_height);
}

RECT StartMenuRoot::GetSearchResultsBodyRect() const
{
    ClientRect client(_hwnd);
    ModernStartMenuMetrics metrics = GetModernStartMenuMetrics();
    RECT footer_rect = GetFooterRect();
    RECT header_rect = GetProgramsHeaderRect();

    return MakeRectWH(metrics._outer_padding,
        header_rect.bottom + metrics._section_item_gap,
        client.right - metrics._outer_padding * 2,
        max(0, footer_rect.top - metrics._outer_padding - (header_rect.bottom + metrics._section_item_gap)));
}

RECT StartMenuRoot::GetSearchEditRect() const
{
    RECT search_rect = GetSearchRect();
    int edit_height = DPI_SY(24);
    int top = search_rect.top + max(0, ((search_rect.bottom - search_rect.top - edit_height) / 2));

    return MakeRectWH(search_rect.left + DPI_SX(46),
        top,
        max(0, (search_rect.right - search_rect.left) - DPI_SX(60)),
        edit_height);
}

RECT StartMenuRoot::GetSearchResultsRect() const
{
    RECT body_rect = GetSearchResultsBodyRect();
    int column_gap = DPI_SX(18);
    int details_min_width = DPI_SX(240);
    int body_width = body_rect.right - body_rect.left;
    int list_width = max(DPI_SX(250), min(DPI_SX(330), (body_width - column_gap) * 43 / 100));

    if (body_width - list_width - column_gap < details_min_width)
        list_width = max(DPI_SX(220), body_width - column_gap - details_min_width);

    return MakeRectWH(body_rect.left,
        body_rect.top,
        max(0, list_width),
        body_rect.bottom - body_rect.top);
}

RECT StartMenuRoot::GetSearchResultRect(int index) const
{
    RECT list_rect = GetSearchResultsRect();
    int row_height = GetSearchResultRowHeight();
    int top = list_rect.top;

    for (int item_index = _search_result_scroll; item_index < index; ++item_index)
        top += row_height + GetSearchResultRowGap(_search_results[item_index]._show_separator_after);

    return MakeRectWH(list_rect.left,
        top,
        max(0, (list_rect.right - list_rect.left) - DPI_SX(10)),
        row_height);
}

RECT StartMenuRoot::GetSearchDetailsRect() const
{
    RECT body_rect = GetSearchResultsBodyRect();
    RECT list_rect = GetSearchResultsRect();
    int column_gap = DPI_SX(18);

    return MakeRectWH(list_rect.right + column_gap,
        body_rect.top,
        max(0, body_rect.right - (list_rect.right + column_gap)),
        body_rect.bottom - body_rect.top);
}

RECT StartMenuRoot::GetSearchDetailsActionRect(int index) const
{
    RECT details_rect = GetSearchDetailsRect();
    int outer_padding = DPI_SX(18);
    int item_gap = DPI_SY(8);
    int action_height = DPI_SY(34);
    int section_height = action_height * 2 + item_gap + DPI_SY(18);
    RECT section_rect = MakeRectWH(details_rect.left + outer_padding,
        details_rect.bottom - outer_padding - section_height,
        max(0, (details_rect.right - details_rect.left) - outer_padding * 2),
        section_height);
    int top = section_rect.top + DPI_SY(18) + index * (action_height + item_gap);

    return MakeRectWH(section_rect.left,
        top,
        section_rect.right - section_rect.left,
        action_height);
}

RECT StartMenuRoot::GetSearchHomeRecentHeaderRect() const
{
    ClientRect client(_hwnd);
    ModernStartMenuMetrics metrics = GetModernStartMenuMetrics();
    RECT search_rect = GetSearchRect();
    int column_gap = DPI_SX(18);
    int content_width = client.right - metrics._outer_padding * 2;
    int recent_width = max(DPI_SX(220), min(DPI_SX(250), (content_width - column_gap) * 36 / 100));

    return MakeRectWH(metrics._outer_padding,
        search_rect.bottom + metrics._section_gap,
        recent_width,
        metrics._section_button_height);
}

RECT StartMenuRoot::GetSearchHomeRecentListRect() const
{
    ModernStartMenuMetrics metrics = GetModernStartMenuMetrics();
    RECT footer_rect = GetFooterRect();
    RECT header_rect = GetSearchHomeRecentHeaderRect();

    return MakeRectWH(header_rect.left,
        header_rect.bottom + metrics._section_item_gap,
        header_rect.right - header_rect.left,
        max(0, footer_rect.top - metrics._outer_padding - (header_rect.bottom + metrics._section_item_gap)));
}

RECT StartMenuRoot::GetSearchHomeRecentRowRect(int index) const
{
    RECT list_rect = GetSearchHomeRecentListRect();
    int row_height = DPI_SY(40);
    int row_gap = DPI_SY(8);

    return MakeRectWH(list_rect.left,
        list_rect.top + index * (row_height + row_gap),
        list_rect.right - list_rect.left,
        row_height);
}

RECT StartMenuRoot::GetSearchHomeTopAppsHeaderRect() const
{
    ClientRect client(_hwnd);
    ModernStartMenuMetrics metrics = GetModernStartMenuMetrics();
    RECT recent_header_rect = GetSearchHomeRecentHeaderRect();
    int column_gap = DPI_SX(18);

    return MakeRectWH(recent_header_rect.right + column_gap,
        recent_header_rect.top,
        max(0, client.right - metrics._outer_padding - (recent_header_rect.right + column_gap)),
        recent_header_rect.bottom - recent_header_rect.top);
}

RECT StartMenuRoot::GetSearchHomeTopAppsGridRect() const
{
    RECT header_rect = GetSearchHomeTopAppsHeaderRect();
    ModernStartMenuMetrics metrics = GetModernStartMenuMetrics();
    int rows = (GetVisibleSearchHomeTopAppCount() + 2) / 3;
    int tile_height = DPI_SY(90);
    int tile_gap = DPI_SX(10);
    int height = rows > 0 ? rows * tile_height + (rows - 1) * tile_gap : 0;

    return MakeRectWH(header_rect.left,
        header_rect.bottom + metrics._section_item_gap,
        header_rect.right - header_rect.left,
        height);
}

RECT StartMenuRoot::GetSearchHomeTopAppRect(int index) const
{
    RECT grid_rect = GetSearchHomeTopAppsGridRect();
    int columns = 3;
    int tile_gap = DPI_SX(10);
    int tile_height = DPI_SY(90);
    int tile_width = (grid_rect.right - grid_rect.left - tile_gap * (columns - 1)) / columns;
    int row = index / columns;
    int col = index % columns;

    return MakeRectWH(grid_rect.left + col * (tile_width + tile_gap),
        grid_rect.top + row * (tile_height + tile_gap),
        tile_width,
        tile_height);
}

RECT StartMenuRoot::GetProgramsHeaderRect() const
{
    ClientRect client(_hwnd);
    ModernStartMenuMetrics metrics = GetModernStartMenuMetrics();
    RECT search_rect = GetSearchRect();
    return MakeRectWH(metrics._outer_padding, search_rect.bottom + metrics._section_gap,
        client.right - metrics._outer_padding * 2, metrics._section_button_height);
}

RECT StartMenuRoot::GetProgramsButtonRect() const
{
    RECT header_rect = GetProgramsHeaderRect();
    ModernStartMenuMetrics metrics = GetModernStartMenuMetrics();
    return MakeRectWH(header_rect.right - metrics._section_button_width,
        header_rect.top,
        metrics._section_button_width,
        metrics._section_button_height);
}

RECT StartMenuRoot::GetProgramsGridRect() const
{
    ClientRect client(_hwnd);
    ModernStartMenuMetrics metrics = GetModernStartMenuMetrics();
    RECT header_rect = GetProgramsHeaderRect();
    int rows = (GetVisibleProgramCount() + metrics._program_columns - 1) / metrics._program_columns;
    int height = rows > 0 ? rows * metrics._program_tile_height + (rows - 1) * metrics._program_gap_y : 0;

    return MakeRectWH(metrics._outer_padding,
        header_rect.bottom + metrics._section_item_gap,
        client.right - metrics._outer_padding * 2,
        height);
}

RECT StartMenuRoot::GetProgramTileRect(int index) const
{
    RECT grid_rect = GetProgramsGridRect();
    ModernStartMenuMetrics metrics = GetModernStartMenuMetrics();
    int tile_width = (grid_rect.right - grid_rect.left - metrics._program_gap_x * (metrics._program_columns - 1)) / metrics._program_columns;
    int row = index / metrics._program_columns;
    int col = index % metrics._program_columns;

    return MakeRectWH(grid_rect.left + col * (tile_width + metrics._program_gap_x),
        grid_rect.top + row * (metrics._program_tile_height + metrics._program_gap_y),
        tile_width,
        metrics._program_tile_height);
}

RECT StartMenuRoot::GetRecommendedHeaderRect() const
{
    ClientRect client(_hwnd);
    ModernStartMenuMetrics metrics = GetModernStartMenuMetrics();
    RECT programs_grid_rect = GetProgramsGridRect();
    return MakeRectWH(metrics._outer_padding,
        programs_grid_rect.bottom + metrics._section_gap,
        client.right - metrics._outer_padding * 2,
        metrics._section_button_height);
}

RECT StartMenuRoot::GetRecommendedButtonRect() const
{
    RECT header_rect = GetRecommendedHeaderRect();
    ModernStartMenuMetrics metrics = GetModernStartMenuMetrics();
    return MakeRectWH(header_rect.right - metrics._section_button_width,
        header_rect.top,
        metrics._section_button_width,
        metrics._section_button_height);
}

RECT StartMenuRoot::GetRecommendedGridRect() const
{
    ClientRect client(_hwnd);
    ModernStartMenuMetrics metrics = GetModernStartMenuMetrics();
    RECT header_rect = GetRecommendedHeaderRect();
    int rows = (GetVisibleRecommendedCount() + metrics._recommended_columns - 1) / metrics._recommended_columns;
    int height = rows > 0 ? rows * metrics._recommended_tile_height + (rows - 1) * metrics._recommended_gap_y : 0;

    return MakeRectWH(metrics._outer_padding,
        header_rect.bottom + metrics._section_item_gap,
        client.right - metrics._outer_padding * 2,
        height);
}

RECT StartMenuRoot::GetRecommendedTileRect(int index) const
{
    RECT grid_rect = GetRecommendedGridRect();
    ModernStartMenuMetrics metrics = GetModernStartMenuMetrics();
    int tile_width = (grid_rect.right - grid_rect.left - metrics._recommended_gap_x * (metrics._recommended_columns - 1)) / metrics._recommended_columns;
    int row = index / metrics._recommended_columns;
    int col = index % metrics._recommended_columns;

    return MakeRectWH(grid_rect.left + col * (tile_width + metrics._recommended_gap_x),
        grid_rect.top + row * (metrics._recommended_tile_height + metrics._recommended_gap_y),
        tile_width,
        metrics._recommended_tile_height);
}

RECT StartMenuRoot::GetRecentDocumentsListRect() const
{
    ClientRect client(_hwnd);
    ModernStartMenuMetrics metrics = GetModernStartMenuMetrics();
    RECT footer_rect = GetFooterRect();
    RECT header_rect = GetRecommendedHeaderRect();

    return MakeRectWH(metrics._outer_padding,
        header_rect.bottom + metrics._section_item_gap,
        client.right - metrics._outer_padding * 2,
        max(0, footer_rect.top - metrics._outer_padding - (header_rect.bottom + metrics._section_item_gap)));
}

RECT StartMenuRoot::GetRecentDocumentRowRect(int index) const
{
    RECT list_rect = GetRecentDocumentsListRect();
    int row_height = DPI_SY(54);
    int row_gap = DPI_SY(6);
    int visible_index = index - _recent_document_scroll;

    return MakeRectWH(list_rect.left,
        list_rect.top + visible_index * (row_height + row_gap),
        max(0, (list_rect.right - list_rect.left) - DPI_SX(10)),
        row_height);
}

RECT StartMenuRoot::GetFooterRect() const
{
    ClientRect client(_hwnd);
    ModernStartMenuMetrics metrics = GetModernStartMenuMetrics();
    return MakeRectWH(0, client.bottom - metrics._footer_height, client.right, metrics._footer_height);
}

RECT StartMenuRoot::GetProfileRect() const
{
    RECT footer_rect = GetFooterRect();
    ModernStartMenuMetrics metrics = GetModernStartMenuMetrics();
    return MakeRectWH(metrics._outer_padding,
        footer_rect.top,
        footer_rect.right - metrics._outer_padding * 2,
        footer_rect.bottom - footer_rect.top);
}

RECT StartMenuRoot::GetAllAppsListRect() const
{
    ClientRect client(_hwnd);
    ModernStartMenuMetrics metrics = GetModernStartMenuMetrics();
    RECT footer_rect = GetFooterRect();
    RECT header_rect = GetProgramsHeaderRect();
    int top = header_rect.bottom + metrics._section_item_gap;
    int available = footer_rect.top - top - metrics._section_gap - metrics._section_button_height - metrics._section_item_gap - metrics._outer_padding;
    int height = max(DPI_SY(220), available * 2 / 3);
    int max_height = max(0, available);
    if (height > max_height)
        height = max_height;

    return MakeRectWH(metrics._outer_padding,
        top,
        client.right - metrics._outer_padding * 2,
        max(0, height));
}

RECT StartMenuRoot::GetAllProgramRowRect(int index) const
{
    RECT list_rect = GetAllAppsListRect();
    int row_height = DPI_SY(34);
    int row_gap = DPI_SY(6);
    int visible_index = index - _all_program_scroll;

    return MakeRectWH(list_rect.left,
        list_rect.top + visible_index * (row_height + row_gap),
        max(0, (list_rect.right - list_rect.left) - DPI_SX(10)),
        row_height);
}

RECT StartMenuRoot::GetDriveFoldersHeaderRect() const
{
    ClientRect client(_hwnd);
    ModernStartMenuMetrics metrics = GetModernStartMenuMetrics();
    RECT apps_rect = GetAllAppsListRect();

    return MakeRectWH(metrics._outer_padding,
        apps_rect.bottom + metrics._section_gap,
        client.right - metrics._outer_padding * 2,
        metrics._section_button_height);
}

RECT StartMenuRoot::GetDriveFoldersListRect() const
{
    ClientRect client(_hwnd);
    ModernStartMenuMetrics metrics = GetModernStartMenuMetrics();
    RECT footer_rect = GetFooterRect();
    RECT header_rect = GetDriveFoldersHeaderRect();

    return MakeRectWH(metrics._outer_padding,
        header_rect.bottom + metrics._section_item_gap,
        client.right - metrics._outer_padding * 2,
        max(0, footer_rect.top - metrics._outer_padding - (header_rect.bottom + metrics._section_item_gap)));
}

RECT StartMenuRoot::GetDriveFolderRowRect(int index) const
{
    RECT list_rect = GetDriveFoldersListRect();
    int row_height = DPI_SY(34);
    int row_gap = DPI_SY(6);
    int visible_index = index - _drive_folder_scroll;

    return MakeRectWH(list_rect.left,
        list_rect.top + visible_index * (row_height + row_gap),
        max(0, (list_rect.right - list_rect.left) - DPI_SX(10)),
        row_height);
}

void StartMenuRoot::ApplyWindowRegion()
{
    ClientRect client(_hwnd);
    ModernStartMenuMetrics metrics = GetModernStartMenuMetrics();
    HRGN region = CreateRoundRectRgn(0, 0, client.right + 1, client.bottom + 1, metrics._corner_radius, metrics._corner_radius);
    if (region)
        SetWindowRgn(_hwnd, region, TRUE);
}

void StartMenuRoot::UpdatePlacement()
{
    RECT rect = CalculateModernStartMenuRect(_hwndStartButton);
    _panel_width = rect.right - rect.left;
    _panel_height = rect.bottom - rect.top;

    HWND insert_after = HWND_TOP;
    if (_hwndStartButton) {
        HWND desktopbar = GetParent(_hwndStartButton);
        if (desktopbar)
            insert_after = desktopbar;
    }

    SetWindowPos(_hwnd, insert_after, rect.left, rect.top, _panel_width, _panel_height, SWP_NOACTIVATE);
    ApplyWindowRegion();
    LayoutSearchEdit();
}

void StartMenuRoot::LayoutSearchEdit()
{
    if (!_hwndSearchEdit)
        return;

    RECT rect = GetSearchEditRect();
    SetWindowPos(_hwndSearchEdit, HWND_TOP, rect.left, rect.top,
        rect.right - rect.left, rect.bottom - rect.top,
        SWP_NOACTIVATE | SWP_NOZORDER);
}

void StartMenuRoot::RefreshSearchEditBrush()
{
    COLORREF fill = GetSearchFillColor();
    if (_search_edit_brush && fill == _search_edit_fill)
        return;

    if (_search_edit_brush)
        DeleteObject(_search_edit_brush);

    _search_edit_brush = CreateSolidBrush(fill);
    _search_edit_fill = fill;

    if (_hwndSearchEdit)
        InvalidateRect(_hwndSearchEdit, NULL, FALSE);
}

void StartMenuRoot::SyncSearchEditText()
{
    if (!_hwndSearchEdit)
        return;

    TCHAR buffer[1024] = { 0 };
    GetWindowText(_hwndSearchEdit, buffer, COUNTOF(buffer));
    if (_tcscmp(buffer, _search_query.c_str())) {
        SetWindowText(_hwndSearchEdit, _search_query.c_str());
        SendMessage(_hwndSearchEdit, EM_SETSEL, _search_query.length(), _search_query.length());
    }
}

void StartMenuRoot::AddFallbackProgramItems()
{
    struct FallbackItem {
        int id;
        String title;
        ICON_ID icon_id;
        String path;
        bool is_command;
    };

    vector<FallbackItem> fallback_items;
    TCHAR peazip_path[MAX_PATH] = { 0 };
    if (TryGetPeaZipPath(peazip_path, COUNTOF(peazip_path))) {
        ICON_ID peazip_icon_id = (ICON_ID)g_Globals._icon_cache.extract(peazip_path, ICF_LARGE | ICF_NOLINKOVERLAY);
        if (peazip_icon_id == ICID_NONE)
            peazip_icon_id = ICID_APP;
        fallback_items.push_back(FallbackItem{ IDC_EXPLORE, TEXT("PeaZip"), peazip_icon_id, peazip_path, false });
    } else {
        fallback_items.push_back(FallbackItem{ IDC_EXPLORE, ResString(IDS_EXPLORE), ICID_EXPLORER, TEXT(""), true });
    }
    fallback_items.push_back(FallbackItem{ IDC_SETTINGS, ResString(IDS_SETTINGS), ICID_CONFIG, TEXT(""), true });
    fallback_items.push_back(FallbackItem{ IDC_LAUNCH, ResString(IDS_LAUNCH), ICID_ACTION, TEXT(""), true });

    for (size_t index = 0; index < fallback_items.size() && _program_items.size() < 8; ++index) {
        if (!ModernStartMenuHasTitle(_program_items, fallback_items[index].title.c_str())) {
            ModernStartMenuItem item(fallback_items[index].id,
                fallback_items[index].title.c_str(),
                fallback_items[index].icon_id,
                NULL,
                fallback_items[index].is_command);
            item._path = fallback_items[index].path;
            _program_items.push_back(item);
        }
    }
}

void StartMenuRoot::BuildProgramItems()
{
    _name_flags = NO_EXEEXT_FLAG;

    for (StartMenuShellDirs::iterator it = _dirs.begin(); it != _dirs.end(); ++it) {
        StartMenuDirectory &start_dir = *it;
        ShellDirectory &dir = start_dir._dir;

        if (!dir._scanned) {
            WaitCursor wait;
            dir.smart_scan(SORT_NAME, SCAN_DONT_EXTRACT_ICONS);
        }

        AddShellEntries(dir, -1, start_dir._ignore);
    }

    _name_flags = 0;

    for (ShellEntryMap::const_iterator it = _entries.begin(); it != _entries.end(); ++it) {
        if (!StartMenuEntryCanLaunch(it->second))
            continue;

        Entry *entry = GetPrimaryStartMenuEntry(it->second);
        ModernStartMenuItem item(it->first, it->second._title, it->second._icon_id, entry, false);
        _program_items.push_back(item);
        _all_program_items.push_back(item);
    }

    std::sort(_all_program_items.begin(), _all_program_items.end(), CompareStartMenuItemByTitle);

    AddFallbackProgramItems();
}

void StartMenuRoot::BuildRecommendedItems()
{
    try {
        ShellDirectory recent_dir(GetDesktopFolder(), SpecialFolderPath(CSIDL_RECENT, _hwnd), _hwnd);
        _recent_dirs.push_back(StartMenuDirectory(recent_dir));
    } catch (COMException &) {
        return;
    }

    for (StartMenuShellDirs::iterator it = _recent_dirs.begin(); it != _recent_dirs.end(); ++it) {
        StartMenuDirectory &start_dir = *it;
        ShellDirectory &dir = start_dir._dir;
        int added = 0;

        if (!dir._scanned) {
            WaitCursor wait;
            dir.smart_scan(SORT_NAME, SCAN_DONT_EXTRACT_ICONS);
        }

        dir.sort_directory(SORT_DATE);

        for (Entry *entry = dir._down; entry && added < RECENT_DOCS_COUNT; entry = entry->_next) {
            if (entry->_shell_attribs & SFGAO_HIDDEN)
                continue;
            if (entry->_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;

            int id = ++_next_id;
            String meta_text = FormatRecentItemMeta(entry);
            ShellEntryMap::iterator added_entry = AddEntry(entry->_display_name, (ICON_ID)entry->_icon_id, id);
            added_entry->second._entries.insert(entry);
            _recommended_items.push_back(ModernStartMenuItem(id, added_entry->second._title, added_entry->second._icon_id, entry, false, meta_text.c_str()));
            ++added;
        }
    }
}

void StartMenuRoot::BuildSearchRecentItems()
{
    for (StartMenuShellDirs::iterator it = _recent_dirs.begin(); it != _recent_dirs.end(); ++it) {
        StartMenuDirectory &start_dir = *it;
        ShellDirectory &dir = start_dir._dir;
        int added = 0;

        if (!dir._scanned) {
            WaitCursor wait;
            dir.smart_scan(SORT_NAME, SCAN_DONT_EXTRACT_ICONS);
        }

        dir.sort_directory(SORT_DATE);

        for (Entry *entry = dir._down; entry && added < 10; entry = entry->_next) {
            if (entry->_shell_attribs & SFGAO_HIDDEN)
                continue;
            if (entry->_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;

            String launch_path;
            if (!TryGetRecentApplicationPath(entry, launch_path))
                continue;
            if (ModernStartMenuHasTitle(_search_recent_items, entry->_display_name) || ModernStartMenuHasMeta(_search_recent_items, launch_path))
                continue;

            if (entry->_icon_id == ICID_UNKNOWN)
                entry->_icon_id = entry->safe_extract_icon(ICF_FROM_ICON_SIZE(DPI_SX(20)) | ICF_NOLINKOVERLAY);

            _search_recent_items.push_back(ModernStartMenuItem(0,
                entry->_display_name,
                (ICON_ID)entry->_icon_id,
                entry,
                false,
                launch_path.c_str()));
            ++added;
        }
    }

    if (_search_recent_items.empty()) {
        for (size_t index = 0; index < _all_program_items.size() && _search_recent_items.size() < 8; ++index) {
            ModernStartMenuItem item = _all_program_items[index];
            item._meta_text = TEXT("Installed app");
            _search_recent_items.push_back(item);
        }
    }
}

void StartMenuRoot::BuildDriveFolderItems()
{
    WIN32_FIND_DATA find_data;
    HANDLE find_handle = FindFirstFile(TEXT("C:\\*"), &find_data);
    if (find_handle == INVALID_HANDLE_VALUE)
        return;

    do {
        if (!_tcscmp(find_data.cFileName, TEXT(".")) || !_tcscmp(find_data.cFileName, TEXT("..")))
            continue;
        if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM)
            continue;

        ModernStartMenuItem item(0, find_data.cFileName, ICID_FOLDER, NULL, false, TEXT("C:\\"));
        item._path = String(TEXT("C:\\")) + find_data.cFileName;
        item._autocomplete_text = EnsureTrailingBackslash(item._path);
        _drive_folder_items.push_back(item);
    } while (FindNextFile(find_handle, &find_data));

    FindClose(find_handle);
    std::sort(_drive_folder_items.begin(), _drive_folder_items.end(), CompareStartMenuItemByTitle);
}

void StartMenuRoot::RebuildModernContent()
{
    _entries.clear();
    _program_items.clear();
    _all_program_items.clear();
    _recommended_items.clear();
    _drive_folder_items.clear();
    _search_results.clear();
    _search_recent_items.clear();
    _recent_dirs.clear();
    _next_id = IDC_FIRST_MENU;
    _search_result_scroll = 0;
    _all_program_scroll = 0;
    _drive_folder_scroll = 0;
    _recent_document_scroll = 0;

    BuildProgramItems();
    BuildRecommendedItems();
    BuildSearchRecentItems();
    BuildDriveFolderItems();
    UpdateSearchResults();
}

void StartMenuRoot::EnsureItemIcon(ModernStartMenuItem &item, int icon_size)
{
    if (!item._path.empty() &&
        (item._icon_id <= ICID_NONE || item._icon_id == ICID_FOLDER || item._icon_id == ICID_APP || item._icon_id == ICID_SEARCH_DOC)) {
        String normalized_path = NormalizeSearchPath(item._path);
        DWORD file_attributes = GetFileAttributes(normalized_path.c_str());

        if (file_attributes != INVALID_FILE_ATTRIBUTES) {
            ICON_ID extracted_icon = ExtractPathResultIcon(normalized_path, icon_size);
            if (extracted_icon > ICID_NONE) {
                item._icon_id = extracted_icon;
                return;
            }

            if (file_attributes & FILE_ATTRIBUTE_DIRECTORY) {
                item._icon_id = ICID_FOLDER;
                return;
            }

            item._icon_id = PathMatchSpec(normalized_path.c_str(), TEXT("*.exe")) ? ICID_APP : ICID_SEARCH_DOC;
            return;
        }
    }

    if (item._icon_id > ICID_NONE)
        return;

    if (item._entry) {
        if (item._entry->_icon_id == ICID_UNKNOWN)
            item._entry->_icon_id = item._entry->safe_extract_icon(ICF_FROM_ICON_SIZE(icon_size) | ICF_NOLINKOVERLAY);

        item._icon_id = (ICON_ID)item._entry->_icon_id;
    }

    if (item._icon_id <= ICID_NONE)
        item._icon_id = item._entry ? ICID_APP : ICID_ACTION;
}

bool StartMenuRoot::ExecuteItem(const ModernStartMenuItem &item)
{
    if (item._is_command) {
        Command(item._id, BN_CLICKED);
        return true;
    }

    String resolved_path;
    if (TryResolveStartMenuItemPath(item, resolved_path)) {
        CloseStartMenu(item._id);
        return launch_file(_hwnd, resolved_path.c_str()) ? true : false;
    }

    ShellEntryMap::const_iterator found = _entries.find(item._id);
    if (found != _entries.end()) {
        ActivateEntry(item._id, found->second._entries);
        return true;
    }

    if (item._entry) {
        CloseStartMenu(item._id);
        item._entry->launch_entry(_hwnd);
        return true;
    }

    return false;
}

bool StartMenuRoot::ExecuteSearchSelection()
{
    if (!IsSearchResultsVisible()) {
        String normalized_query = NormalizeSearchPath(_search_query);
        if (!normalized_query.empty() && PathFileExists(normalized_query.c_str())) {
            CloseStartMenu();
            return launch_file(_hwnd, normalized_query.c_str()) ? true : false;
        }

        return false;
    }

    int selected_index = GetSelectedSearchResultIndex();
    if (selected_index >= 0 && selected_index < (int)_search_results.size())
        return ExecuteItem(_search_results[selected_index]);

    if (!_search_results.empty())
        return ExecuteItem(_search_results[0]);

    return false;
}

bool StartMenuRoot::AutocompleteSearchSelection()
{
    int selected_index = GetSelectedSearchResultIndex();
    if (selected_index < 0 || selected_index >= (int)_search_results.size())
        return false;

    const ModernStartMenuItem &item = _search_results[selected_index];
    if (item._autocomplete_text.empty())
        return false;

    SetSearchQuery(item._autocomplete_text);
    return true;
}

void StartMenuRoot::AnimateShow()
{
    RECT rect = CalculateModernStartMenuRect(_hwndStartButton);
    HWND insert_after = _hwndStartButton ? GetParent(_hwndStartButton) : HWND_TOP;
    AnimateStartMenuSlide(_hwnd, insert_after, rect, true);
}

void StartMenuRoot::AnimateHide()
{
    RECT rect = CalculateModernStartMenuRect(_hwndStartButton);
    HWND insert_after = _hwndStartButton ? GetParent(_hwndStartButton) : HWND_TOP;
    AnimateStartMenuSlide(_hwnd, insert_after, rect, false);
}

bool StartMenuRoot::TryGetContextMenuItem(HOT_AREA area, int index, ModernStartMenuItem **item)
{
    if (item)
        *item = NULL;

    ModernStartMenuItem *resolved_item = NULL;

    switch (area) {
    case HOT_SEARCH_RESULT:
        if (index >= 0 && index < (int)_search_results.size())
            resolved_item = &_search_results[index];
        break;
    case HOT_SEARCH_HOME_RECENT:
        if (index >= 0 && index < (int)_search_recent_items.size())
            resolved_item = &_search_recent_items[index];
        break;
    case HOT_SEARCH_HOME_TOP_APP:
        if (!_all_program_items.empty()) {
            if (index >= 0 && index < (int)_all_program_items.size())
                resolved_item = &_all_program_items[index];
        } else if (index >= 0 && index < (int)_program_items.size()) {
            resolved_item = &_program_items[index];
        }
        break;
    case HOT_PROGRAM:
        if (index >= 0 && index < (int)_program_items.size())
            resolved_item = &_program_items[index];
        break;
    case HOT_RECOMMENDED:
        if (index >= 0 && index < (int)_recommended_items.size())
            resolved_item = &_recommended_items[index];
        break;
    case HOT_ALL_PROGRAM:
        if (index >= 0 && index < (int)_all_program_items.size())
            resolved_item = &_all_program_items[index];
        break;
    case HOT_DRIVE_FOLDER:
        if (index >= 0 && index < (int)_drive_folder_items.size())
            resolved_item = &_drive_folder_items[index];
        break;
    default:
        break;
    }

    if (!resolved_item)
        return false;

    if (item)
        *item = resolved_item;

    return true;
}

bool StartMenuRoot::ShowContextMenuForHotArea(HOT_AREA area, int index, POINT screen_pt)
{
    ModernStartMenuItem *item = NULL;
    if (!TryGetContextMenuItem(area, index, &item) || !item)
        return false;

    if (area == HOT_SEARCH_RESULT && index >= 0) {
        _search_selected_index = index;
        _hot_area = area;
        _hot_index = index;
        RECT details_rect = GetSearchDetailsRect();
        InvalidateRect(_hwnd, &details_rect, FALSE);
        InvalidateHotArea(area, index);
    }

    String resolved_path;
    bool has_resolved_path = TryResolveStartMenuItemPath(*item, resolved_path);
    bool path_exists = has_resolved_path && PathFileExists(resolved_path.c_str()) != FALSE;
    int command = StartMenuContextMenuPopup::Show(_hwnd,
        _item_font ? _item_font : g_Globals._hDefaultFont,
        screen_pt,
        path_exists,
        has_resolved_path);

    switch (command) {
    case STARTMENU_CONTEXT_OPEN:
        return ExecuteItem(*item);
    case STARTMENU_CONTEXT_COPY:
        return path_exists ? CopyItemToClipboard(_hwnd, resolved_path) : false;
    case STARTMENU_CONTEXT_COPY_PATH:
        return has_resolved_path ? CopyTextToClipboard(_hwnd, TrimTrailingBackslash(resolved_path)) : false;
    default:
        return false;
    }
}

void StartMenuRoot::UpdateSearchResults()
{
    _search_results.clear();
    _search_result_scroll = 0;

    if (_search_query.empty())
        return;

    String normalized_query = NormalizeSearchPath(_search_query);
    bool path_query = LooksLikePathQuery(normalized_query);
    const int max_results = path_query ? 96 : 32;
    String query_lower = _search_query;
    query_lower.toLower();
    bool query_path_exists = PathFileExists(normalized_query.c_str()) != FALSE;
    bool query_is_directory = query_path_exists && PathIsDirectory(normalized_query.c_str()) != FALSE;

    if (query_path_exists) {
        String display_path = normalized_query;
        while (display_path.length() > 3 && display_path.at(display_path.length() - 1) == TEXT('\\'))
            display_path.erase(display_path.length() - 1);

        String title = display_path;
        size_t slash_pos = display_path.find_last_of(TEXT("\\"));
        if (slash_pos != String::npos && slash_pos + 1 < display_path.length())
            title = display_path.substr(slash_pos + 1);

        ModernStartMenuItem path_item(0, title.c_str(), query_is_directory ? ICID_FOLDER : GetPathQueryIconId(normalized_query.c_str(), 0), NULL, false,
            query_is_directory ? TEXT("Open folder") : TEXT("Open path"));
        path_item._path = normalized_query;
        path_item._autocomplete_text = query_is_directory ? EnsureTrailingBackslash(normalized_query) : normalized_query;
        path_item._show_separator_after = query_is_directory;
        _search_results.push_back(path_item);
    }

    if (path_query) {
        String base_dir;
        String fragment;

        if (ResolvePathQueryParts(_search_query, base_dir, fragment)) {
            String fragment_lower = fragment;
            fragment_lower.toLower();
            String search_pattern = base_dir + TEXT("*");
            WIN32_FIND_DATA find_data;
            HANDLE find_handle = FindFirstFile(search_pattern.c_str(), &find_data);

            if (find_handle != INVALID_HANDLE_VALUE) {
                do {
                    if (!_tcscmp(find_data.cFileName, TEXT(".")) || !_tcscmp(find_data.cFileName, TEXT("..")))
                        continue;

                    if (!fragment_lower.empty()) {
                        String name_lower = find_data.cFileName;
                        name_lower.toLower();
                        if (name_lower.find(fragment_lower) != 0)
                            continue;
                    }

                    bool is_directory = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    ModernStartMenuItem item(0, find_data.cFileName, GetPathQueryIconId(find_data.cFileName, find_data.dwFileAttributes), NULL, false, base_dir.c_str());
                    item._path = base_dir + find_data.cFileName;
                    item._autocomplete_text = is_directory ? EnsureTrailingBackslash(item._path) : item._path;

                    if (!SearchResultExists(_search_results, item))
                        _search_results.push_back(item);
                } while ((int)_search_results.size() < max_results && FindNextFile(find_handle, &find_data));

                FindClose(find_handle);
            }
        }

        if (!_search_results.empty() && _search_results[0]._show_separator_after && _search_results.size() < 2)
            _search_results[0]._show_separator_after = false;

        return;
    }

    for (size_t index = 0; index < _search_recent_items.size() && (int)_search_results.size() < max_results; ++index) {
        ModernStartMenuItem item = _search_recent_items[index];
        if (!StringContainsInsensitive(item._title, query_lower) && !StringContainsInsensitive(item._meta_text, query_lower))
            continue;

        item._autocomplete_text = item._title;
        if (!SearchResultExists(_search_results, item))
            _search_results.push_back(item);
    }

    for (size_t index = 0; index < _recommended_items.size() && (int)_search_results.size() < max_results; ++index) {
        ModernStartMenuItem item = _recommended_items[index];
        if (!StringContainsInsensitive(item._title, query_lower))
            continue;

        item._autocomplete_text = item._title;
        if (!SearchResultExists(_search_results, item))
            _search_results.push_back(item);
    }

    for (size_t index = 0; index < _all_program_items.size() && (int)_search_results.size() < max_results; ++index) {
        ModernStartMenuItem item = _all_program_items[index];
        if (!StringContainsInsensitive(item._title, query_lower))
            continue;

        item._meta_text = TEXT("Installed app");
        item._autocomplete_text = item._title;
        if (item._entry) {
            TCHAR entry_path[MAX_PATH] = { 0 };
            item._entry->get_path(entry_path, COUNTOF(entry_path));
            if (entry_path[0])
                item._meta_text = entry_path;
        }

        if (!SearchResultExists(_search_results, item))
            _search_results.push_back(item);
    }
}

void StartMenuRoot::SetSearchQuery(const String &query, bool sync_edit)
{
    _search_query = query;
    _search_active = !_search_query.empty() || GetFocus() == _hwndSearchEdit;
    if (_search_active || !_search_query.empty()) {
        _show_all_programs = false;
        _show_all_recents = false;
    }
    UpdateSearchResults();

    if (IsSearchResultsVisible() && !_search_results.empty()) {
        _hot_area = HOT_SEARCH_RESULT;
        _hot_index = 0;
        _search_selected_index = 0;
    } else {
        _hot_area = HOT_SEARCH;
        _hot_index = -1;
        _search_selected_index = -1;
    }

    if (sync_edit)
        SyncSearchEditText();

    RefreshSearchEditBrush();

    InvalidateRect(_hwnd, NULL, FALSE);
}

bool StartMenuRoot::AdjustScrollOffset(int *offset, int item_count, int visible_count, int delta_lines)
{
    int max_offset = max(0, item_count - visible_count);
    int new_offset = *offset + delta_lines;

    if (new_offset < 0)
        new_offset = 0;
    if (new_offset > max_offset)
        new_offset = max_offset;

    if (new_offset == *offset)
        return false;

    *offset = new_offset;
    return true;
}

bool StartMenuRoot::HandleMouseWheel(short wheel_delta, POINT screen_pt)
{
    POINT client_pt = screen_pt;
    ScreenToClient(_hwnd, &client_pt);

    UINT scroll_lines = 3;
    SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &scroll_lines, 0);
    int delta_lines = wheel_delta > 0 ? -(int)scroll_lines : (int)scroll_lines;
    if (delta_lines == 0)
        delta_lines = wheel_delta > 0 ? -1 : 1;

    bool changed = false;

    if (IsSearchResultsVisible()) {
        changed = AdjustScrollOffset(&_search_result_scroll, (int)_search_results.size(), GetVisibleSearchResultCount(), delta_lines);
    } else if (_show_all_recents) {
        changed = AdjustScrollOffset(&_recent_document_scroll, (int)_recommended_items.size(), GetVisibleRecentDocumentCount(), delta_lines);
    } else if (_show_all_programs) {
        RECT apps_rect = GetAllAppsListRect();
        RECT drive_rect = GetDriveFoldersListRect();
        RECT header_rect = GetProgramsHeaderRect();

        if (PtInRect(&drive_rect, client_pt))
            changed = AdjustScrollOffset(&_drive_folder_scroll, (int)_drive_folder_items.size(), GetVisibleDriveFolderCount(), delta_lines);
        else if (PtInRect(&apps_rect, client_pt) || PtInRect(&header_rect, client_pt))
            changed = AdjustScrollOffset(&_all_program_scroll, (int)_all_program_items.size(), GetVisibleAllProgramCount(), delta_lines);
    }

    if (changed) {
        ClearHotState();
        InvalidateRect(_hwnd, NULL, FALSE);
    }

    return changed;
}

LRESULT StartMenuRoot::HandleSearchEditKeyDown(WPARAM wparam, LPARAM lparam)
{
    UNREFERENCED_PARAMETER(lparam);

    if (wparam == VK_ESCAPE) {
        CloseStartMenu();
        return 0;
    }

    if (IsSearchHomeVisible()) {
        if (wparam == VK_DOWN) {
            if (GetVisibleSearchHomeRecentCount() > 0) {
                if (_hot_area != HOT_SEARCH_HOME_RECENT)
                    _hot_index = 0;
                else if (_hot_index < GetVisibleSearchHomeRecentCount() - 1)
                    ++_hot_index;

                _hot_area = HOT_SEARCH_HOME_RECENT;
                InvalidateRect(_hwnd, NULL, FALSE);
            }
            return 0;
        }

        if (wparam == VK_UP) {
            if (GetVisibleSearchHomeRecentCount() > 0) {
                if (_hot_area != HOT_SEARCH_HOME_RECENT)
                    _hot_index = 0;
                else if (_hot_index > 0)
                    --_hot_index;

                _hot_area = HOT_SEARCH_HOME_RECENT;
                InvalidateRect(_hwnd, NULL, FALSE);
            }
            return 0;
        }

        if (wparam == VK_RETURN) {
            if (_hot_area == HOT_SEARCH_HOME_RECENT && _hot_index >= 0 && _hot_index < GetVisibleSearchHomeRecentCount())
                return ExecuteItem(_search_recent_items[_hot_index]) ? 0 : 1;
            if (_hot_area == HOT_SEARCH_HOME_TOP_APP && _hot_index >= 0 && _hot_index < GetVisibleSearchHomeTopAppCount()) {
                ModernStartMenuItem &item = !_all_program_items.empty() ? _all_program_items[_hot_index] : _program_items[_hot_index];
                return ExecuteItem(item) ? 0 : 1;
            }
        }
    }

    if (IsSearchResultsVisible()) {
        int visible_count = GetVisibleSearchResultCount();

        if (wparam == VK_DOWN) {
            if (!_search_results.empty()) {
                if (_hot_area != HOT_SEARCH_RESULT)
                    _hot_index = _search_result_scroll;
                else if (_hot_index < (int)_search_results.size() - 1)
                    ++_hot_index;

                if (_hot_index >= _search_result_scroll + visible_count)
                    ++_search_result_scroll;

                _hot_area = HOT_SEARCH_RESULT;
                _search_selected_index = _hot_index;
                InvalidateRect(_hwnd, NULL, FALSE);
            }
            return 0;
        }

        if (wparam == VK_UP) {
            if (!_search_results.empty()) {
                if (_hot_area != HOT_SEARCH_RESULT)
                    _hot_index = _search_result_scroll;
                else if (_hot_index > 0)
                    --_hot_index;

                if (_hot_index < _search_result_scroll)
                    --_search_result_scroll;

                _hot_area = HOT_SEARCH_RESULT;
                _search_selected_index = _hot_index;
                InvalidateRect(_hwnd, NULL, FALSE);
            }
            return 0;
        }

        if (wparam == VK_NEXT) {
            if (visible_count > 0 && AdjustScrollOffset(&_search_result_scroll, (int)_search_results.size(), visible_count, visible_count)) {
                _hot_index = min((int)_search_results.size() - 1, _search_result_scroll);
                _hot_area = HOT_SEARCH_RESULT;
                _search_selected_index = _hot_index;
                InvalidateRect(_hwnd, NULL, FALSE);
            }
            return 0;
        }

        if (wparam == VK_PRIOR) {
            if (visible_count > 0 && AdjustScrollOffset(&_search_result_scroll, (int)_search_results.size(), visible_count, -visible_count)) {
                _hot_index = _search_result_scroll;
                _hot_area = HOT_SEARCH_RESULT;
                _search_selected_index = _hot_index;
                InvalidateRect(_hwnd, NULL, FALSE);
            }
            return 0;
        }

        if (wparam == VK_TAB && AutocompleteSearchSelection())
            return 0;

        if (wparam == VK_RETURN && ExecuteSearchSelection())
            return 0;
    }

    return 1;
}

void StartMenuRoot::BeginMouseTrack()
{
    if (_tracking_mouse)
        return;

    TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, _hwnd, 0 };
    TrackMouseEvent(&tme);
    _tracking_mouse = true;
}

void StartMenuRoot::ClearHotState()
{
    HOT_AREA old_area = _hot_area;
    int old_index = _hot_index;

    _hot_area = HOT_NONE;
    _hot_index = -1;
    InvalidateHotArea(old_area, old_index);
}

RECT StartMenuRoot::GetHotRect(HOT_AREA area, int index) const
{
    switch (area) {
    case HOT_SEARCH:
        return GetSearchRect();
    case HOT_SEARCH_RESULT:
        if (index >= _search_result_scroll && index < _search_result_scroll + GetVisibleSearchResultCount())
            return GetSearchResultRect(index);
        break;
    case HOT_SEARCH_DETAIL_OPEN:
        return GetSearchDetailsActionRect(0);
    case HOT_SEARCH_DETAIL_COPY:
        return GetSearchDetailsActionRect(1);
    case HOT_SEARCH_HOME_RECENT:
        if (index >= 0 && index < GetVisibleSearchHomeRecentCount())
            return GetSearchHomeRecentRowRect(index);
        break;
    case HOT_SEARCH_HOME_TOP_APP:
        if (index >= 0 && index < GetVisibleSearchHomeTopAppCount())
            return GetSearchHomeTopAppRect(index);
        break;
    case HOT_PROGRAMS_BUTTON:
        return GetProgramsButtonRect();
    case HOT_RECOMMENDED_BUTTON:
        return GetRecommendedButtonRect();
    case HOT_PROGRAM:
        if (index >= 0 && index < GetVisibleProgramCount())
            return GetProgramTileRect(index);
        break;
    case HOT_RECOMMENDED:
        if (_show_all_recents) {
            if (index >= _recent_document_scroll && index < _recent_document_scroll + GetVisibleRecentDocumentCount())
                return GetRecentDocumentRowRect(index);
        } else if (index >= 0 && index < GetVisibleRecommendedCount()) {
            return GetRecommendedTileRect(index);
        }
        break;
    case HOT_ALL_PROGRAM:
        if (index >= _all_program_scroll && index < _all_program_scroll + GetVisibleAllProgramCount())
            return GetAllProgramRowRect(index);
        break;
    case HOT_DRIVE_FOLDER:
        if (index >= _drive_folder_scroll && index < _drive_folder_scroll + GetVisibleDriveFolderCount())
            return GetDriveFolderRowRect(index);
        break;
    case HOT_PROFILE:
        return GetProfileRect();
    default:
        break;
    }

    return MakeRect(0, 0, 0, 0);
}

void StartMenuRoot::InvalidateHotArea(HOT_AREA area, int index)
{
    RECT rect = GetHotRect(area, index);
    if (!IsNonEmptyRect(rect))
        return;

    InflateRect(&rect, DPI_SX(3), DPI_SY(3));
    InvalidateRect(_hwnd, &rect, FALSE);
}

bool StartMenuRoot::HitTest(POINT pt, HOT_AREA *area, int *index) const
{
    if (area)
        *area = HOT_NONE;
    if (index)
        *index = -1;

    RECT rect = GetSearchRect();
    if (PtInRect(&rect, pt)) {
        if (area) *area = HOT_SEARCH;
        return true;
    }

    rect = GetProfileRect();
    if (PtInRect(&rect, pt)) {
        if (area) *area = HOT_PROFILE;
        return true;
    }

    if (IsSearchHomeVisible()) {
        for (int item_index = 0; item_index < GetVisibleSearchHomeRecentCount(); ++item_index) {
            rect = GetSearchHomeRecentRowRect(item_index);
            if (PtInRect(&rect, pt)) {
                if (area) *area = HOT_SEARCH_HOME_RECENT;
                if (index) *index = item_index;
                return true;
            }
        }

        for (int item_index = 0; item_index < GetVisibleSearchHomeTopAppCount(); ++item_index) {
            rect = GetSearchHomeTopAppRect(item_index);
            if (PtInRect(&rect, pt)) {
                if (area) *area = HOT_SEARCH_HOME_TOP_APP;
                if (index) *index = item_index;
                return true;
            }
        }

        return false;
    }

    if (IsSearchResultsVisible()) {
        for (int item_index = _search_result_scroll; item_index < _search_result_scroll + GetVisibleSearchResultCount(); ++item_index) {
            rect = GetSearchResultRect(item_index);
            if (PtInRect(&rect, pt)) {
                if (area) *area = HOT_SEARCH_RESULT;
                if (index) *index = item_index;
                return true;
            }
        }

        rect = GetSearchDetailsActionRect(0);
        if (PtInRect(&rect, pt)) {
            if (area) *area = HOT_SEARCH_DETAIL_OPEN;
            return true;
        }

        rect = GetSearchDetailsActionRect(1);
        if (PtInRect(&rect, pt)) {
            if (area) *area = HOT_SEARCH_DETAIL_COPY;
            return true;
        }

        return false;
    }

    rect = GetProgramsButtonRect();
    if (PtInRect(&rect, pt)) {
        if (area) *area = HOT_PROGRAMS_BUTTON;
        return true;
    }

    if (_show_all_programs) {
        for (int item_index = _all_program_scroll; item_index < _all_program_scroll + GetVisibleAllProgramCount(); ++item_index) {
            rect = GetAllProgramRowRect(item_index);
            if (PtInRect(&rect, pt)) {
                if (area) *area = HOT_ALL_PROGRAM;
                if (index) *index = item_index;
                return true;
            }
        }

        for (int item_index = _drive_folder_scroll; item_index < _drive_folder_scroll + GetVisibleDriveFolderCount(); ++item_index) {
            rect = GetDriveFolderRowRect(item_index);
            if (PtInRect(&rect, pt)) {
                if (area) *area = HOT_DRIVE_FOLDER;
                if (index) *index = item_index;
                return true;
            }
        }

        return false;
    }

    if (_show_all_recents) {
        rect = GetRecommendedButtonRect();
        if (PtInRect(&rect, pt)) {
            if (area) *area = HOT_RECOMMENDED_BUTTON;
            return true;
        }

        for (int item_index = _recent_document_scroll; item_index < _recent_document_scroll + GetVisibleRecentDocumentCount(); ++item_index) {
            rect = GetRecentDocumentRowRect(item_index);
            if (PtInRect(&rect, pt)) {
                if (area) *area = HOT_RECOMMENDED;
                if (index) *index = item_index;
                return true;
            }
        }

        return false;
    }

    if (!_show_all_programs) {
        rect = GetRecommendedButtonRect();
        if (PtInRect(&rect, pt)) {
            if (area) *area = HOT_RECOMMENDED_BUTTON;
            return true;
        }
    }

    for (int item_index = 0; item_index < GetVisibleProgramCount(); ++item_index) {
        rect = GetProgramTileRect(item_index);
        if (PtInRect(&rect, pt)) {
            if (area) *area = HOT_PROGRAM;
            if (index) *index = item_index;
            return true;
        }
    }

    for (int item_index = 0; item_index < GetVisibleRecommendedCount(); ++item_index) {
        rect = GetRecommendedTileRect(item_index);
        if (PtInRect(&rect, pt)) {
            if (area) *area = HOT_RECOMMENDED;
            if (index) *index = item_index;
            return true;
        }
    }

    return false;
}

void StartMenuRoot::UpdateHotState(POINT pt)
{
    HOT_AREA hot_area = HOT_NONE;
    int hot_index = -1;
    int old_selected_index = _search_selected_index;

    HitTest(pt, &hot_area, &hot_index);

    if (hot_area == HOT_SEARCH_RESULT && hot_index >= 0)
        _search_selected_index = hot_index;

    if (hot_area != _hot_area || hot_index != _hot_index) {
        HOT_AREA old_area = _hot_area;
        int old_index = _hot_index;
        _hot_area = hot_area;
        _hot_index = hot_index;
        InvalidateHotArea(old_area, old_index);
        InvalidateHotArea(_hot_area, _hot_index);
    }

    if (old_selected_index != _search_selected_index && IsSearchResultsVisible()) {
        RECT details_rect = GetSearchDetailsRect();
        InvalidateRect(_hwnd, &details_rect, FALSE);
    }
}

int StartMenuRoot::Command(int id, int code)
{
    if (id == IDC_FILTER && code == EN_CHANGE) {
        TCHAR buffer[1024] = { 0 };
        GetWindowText(_hwndSearchEdit, buffer, COUNTOF(buffer));
        SetSearchQuery(buffer, false);
        return 0;
    }

    switch (id) {
    case IDC_PROGRAMS:
        _show_all_programs = !_show_all_programs;
        _show_all_recents = false;
        SetSearchQuery(TEXT(""));
        _all_program_scroll = 0;
        _drive_folder_scroll = 0;
        RefreshSearchEditBrush();
        return 0;

    case IDC_RECENT:
        CloseOtherSubmenus();
        SetSearchQuery(TEXT(""));
        _show_all_programs = false;
        _show_all_recents = !_show_all_recents;
        _recent_document_scroll = 0;
        RefreshSearchEditBrush();
        InvalidateRect(_hwnd, NULL, FALSE);
        return 0;

    case IDC_SETTINGS:
        CloseStartMenu(id);
        if (!launch_file(_hwnd, TEXT("ms-settings:")))
            return super::Command(IDC_CONTROL_PANEL, code);
        return 0;

    case IDC_SEARCH:
        _search_active = true;
        _show_all_programs = false;
        _show_all_recents = false;
        _hot_area = HOT_SEARCH;
        _hot_index = -1;
        RefreshSearchEditBrush();
        if (_hwndSearchEdit)
            SetFocus(_hwndSearchEdit);
        InvalidateRect(_hwnd, NULL, FALSE);
        return 0;

    default: {
        ShellEntryMap::const_iterator found = _entries.find(id);

        if (found != _entries.end()) {
            ActivateEntry(id, found->second._entries);
            return 0;
        }

        return super::Command(id, code);
    }
    }
}

LRESULT StartMenuRoot::Init(LPCREATESTRUCT pcs)
{
    UNREFERENCED_PARAMETER(pcs);

    _title_font = CreateMenuFont(22, FW_SEMIBOLD);
    _section_font = CreateMenuFont(12, FW_SEMIBOLD);
    _search_font = CreateMenuFont(11, FW_NORMAL);
    _item_font = CreateMenuFont(10, FW_NORMAL);
    _meta_font = CreateMenuFont(9, FW_NORMAL);

    _hwndSearchEdit = CreateWindowEx(0, WC_EDIT, TEXT(""),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0,
        _hwnd, (HMENU)IDC_FILTER, g_Globals._hInstance, NULL);
    if (_hwndSearchEdit) {
        new StartMenuSearchEdit(_hwndSearchEdit, _hwnd);
        SetWindowTheme(_hwndSearchEdit, L"", L"");
        SendMessage(_hwndSearchEdit, WM_SETFONT, (WPARAM)(_search_font ? _search_font : g_Globals._hDefaultFont), FALSE);
        SendMessage(_hwndSearchEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(0, 0));
        SendMessage(_hwndSearchEdit, EM_SETCUEBANNER, FALSE, (LPARAM)TEXT("Search for apps, settings, and documents"));
    }

    RefreshSearchEditBrush();

    RebuildModernContent();
    UpdatePlacement();
    return 0;
}

void StartMenuRoot::TrackStartmenu()
{
    MSG msg;
    HWND hwnd = _hwnd;

    _show_all_programs = false;
    _show_all_recents = false;
    _all_program_scroll = 0;
    _drive_folder_scroll = 0;
    _recent_document_scroll = 0;
    SetSearchQuery(TEXT(""));
    ClearHotState();
    RebuildModernContent();
    _search_active = false;
    RefreshSearchEditBrush();
    UpdatePlacement();

    AnimateShow();
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);

    while (IsWindow(hwnd) && IsWindowVisible(hwnd)) {
        if (!GetMessage(&msg, 0, 0, 0)) {
            PostQuitMessage((int)msg.wParam);
            break;
        }

        if (msg.message == WM_LBUTTONDOWN || msg.message == WM_MBUTTONDOWN || msg.message == WM_RBUTTONDOWN) {
            StartMenu *menu_wnd = NULL;

            for (HWND menu_hwnd = msg.hwnd; menu_hwnd; menu_hwnd = GetParent(menu_hwnd)) {
                menu_wnd = WINDOW_DYNAMIC_CAST(StartMenu, menu_hwnd);

                if (menu_wnd)
                    break;
            }

            if (!menu_wnd) {
                CloseStartMenu();
                break;
            }
        }

        try {
            if (pretranslate_msg(&msg))
                continue;

            if (dispatch_dialog_msg(&msg))
                continue;

            TranslateMessage(&msg);

            try {
                DispatchMessage(&msg);
            } catch (COMException &e) {
                HandleException(e, _hwnd);
            }
        } catch (COMException &e) {
            HandleException(e, _hwnd);
        }
    }
}

LRESULT StartMenuRoot::WndProc(UINT nmsg, WPARAM wparam, LPARAM lparam)
{
    switch (nmsg) {
    case PM_STARTMENU_SEARCH_KEYDOWN:
        return HandleSearchEditKeyDown(wparam, lparam);

    case PM_STARTMENU_SEARCH_WHEEL:
        return HandleMouseWheel(GET_WHEEL_DELTA_WPARAM(wparam), Point(lparam)) ? 0 : 1;

    case PM_STARTMENU_SEARCH_FOCUS:
        _search_active = true;
        _show_all_programs = false;
        _show_all_recents = false;
        _hot_area = HOT_SEARCH;
        _hot_index = -1;
        RefreshSearchEditBrush();
        InvalidateRect(_hwnd, NULL, FALSE);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        BufferedPaintCanvas canvas(_hwnd);
        Paint(canvas);
        return 0;
    }

    case WM_SIZE:
        _panel_width = LOWORD(lparam);
        _panel_height = HIWORD(lparam);
        ApplyWindowRegion();
        LayoutSearchEdit();
        return 0;

    case WM_DISPLAYCHANGE:
        UpdatePlacement();
        return 0;

    case WM_MOUSEMOVE:
        BeginMouseTrack();
        UpdateHotState(Point(lparam));
        return 0;

    case WM_MOUSELEAVE:
        _tracking_mouse = false;
        ClearHotState();
        return 0;

    case WM_LBUTTONDOWN:
        {
            RECT search_rect = GetSearchRect();
            if (_hwndSearchEdit && PtInRect(&search_rect, Point(lparam)))
                SetFocus(_hwndSearchEdit);
            else
                SetFocus(_hwnd);
        }
        return 0;

    case WM_MOUSEWHEEL:
        if (HandleMouseWheel(GET_WHEEL_DELTA_WPARAM(wparam), Point(lparam)))
            return 0;
        break;

    case WM_CTLCOLOREDIT:
        if ((HWND)lparam == _hwndSearchEdit) {
            COLORREF search_fill = GetSearchFillColor();
            SetBkMode((HDC)wparam, OPAQUE);
            SetTextColor((HDC)wparam, RGB(238, 241, 244));
            SetBkColor((HDC)wparam, search_fill);
            return (LRESULT)_search_edit_brush;
        }
        break;

    case WM_LBUTTONUP: {
        HOT_AREA hot_area = HOT_NONE;
        int hot_index = -1;
        HitTest(Point(lparam), &hot_area, &hot_index);

        switch (hot_area) {
        case HOT_SEARCH:
            if (_hwndSearchEdit)
                SetFocus(_hwndSearchEdit);
            Command(IDC_SEARCH, BN_CLICKED);
            break;

        case HOT_SEARCH_RESULT:
            if (hot_index >= 0 && hot_index < (int)_search_results.size()) {
                _hot_area = HOT_SEARCH_RESULT;
                _hot_index = hot_index;
                _search_selected_index = hot_index;
                ExecuteSearchSelection();
            }
            break;

        case HOT_SEARCH_DETAIL_OPEN: {
            int selected_index = GetSelectedSearchResultIndex();
            if (selected_index >= 0 && selected_index < (int)_search_results.size())
                ExecuteItem(_search_results[selected_index]);
            break;
        }

        case HOT_SEARCH_DETAIL_COPY: {
            int selected_index = GetSelectedSearchResultIndex();
            if (selected_index >= 0 && selected_index < (int)_search_results.size())
                CopyTextToClipboard(_hwnd, GetSearchResultLocationText(_search_results[selected_index]));
            break;
        }

        case HOT_SEARCH_HOME_RECENT:
            if (hot_index >= 0 && hot_index < GetVisibleSearchHomeRecentCount())
                ExecuteItem(_search_recent_items[hot_index]);
            break;

        case HOT_SEARCH_HOME_TOP_APP:
            if (hot_index >= 0 && hot_index < GetVisibleSearchHomeTopAppCount()) {
                ModernStartMenuItem &item = !_all_program_items.empty() ? _all_program_items[hot_index] : _program_items[hot_index];
                ExecuteItem(item);
            }
            break;

        case HOT_PROGRAMS_BUTTON:
            Command(IDC_PROGRAMS, BN_CLICKED);
            break;

        case HOT_RECOMMENDED_BUTTON:
            Command(IDC_RECENT, BN_CLICKED);
            break;

        case HOT_PROGRAM:
            if (hot_index >= 0 && hot_index < (int)_program_items.size())
                ExecuteItem(_program_items[hot_index]);
            break;

        case HOT_RECOMMENDED:
            if (hot_index >= 0 && hot_index < (int)_recommended_items.size())
                ExecuteItem(_recommended_items[hot_index]);
            break;

        case HOT_ALL_PROGRAM:
            if (hot_index >= 0 && hot_index < (int)_all_program_items.size())
                ExecuteItem(_all_program_items[hot_index]);
            break;

        case HOT_DRIVE_FOLDER:
            if (hot_index >= 0 && hot_index < (int)_drive_folder_items.size())
                ExecuteItem(_drive_folder_items[hot_index]);
            break;

        case HOT_PROFILE:
            CloseStartMenu();
            try {
                launch_file(_hwnd, SpecialFolderFSPath(CSIDL_PERSONAL, _hwnd));
            } catch (COMException &) {
            }
            break;

        default:
            break;
        }
        return 0;
    }

    case WM_CONTEXTMENU: {
        if ((HWND)wparam == _hwndSearchEdit)
            break;

        HOT_AREA hot_area = HOT_NONE;
        int hot_index = -1;
        POINT screen_pt = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };

        if (screen_pt.x == -1 && screen_pt.y == -1) {
            hot_area = _hot_area;
            hot_index = _hot_index;

            if (hot_area == HOT_NONE && IsSearchResultsVisible()) {
                hot_area = HOT_SEARCH_RESULT;
                hot_index = GetSelectedSearchResultIndex();
            }

            RECT anchor_rect = GetHotRect(hot_area, hot_index);
            if (IsNonEmptyRect(anchor_rect)) {
                POINT anchor_pt = { anchor_rect.left, anchor_rect.bottom };
                ClientToScreen(_hwnd, &anchor_pt);
                screen_pt = anchor_pt;
            }
        } else {
            POINT client_pt = screen_pt;
            ScreenToClient(_hwnd, &client_pt);
            HitTest(client_pt, &hot_area, &hot_index);
        }

        if (ShowContextMenuForHotArea(hot_area, hot_index, screen_pt))
            return 0;

        return 0;
    }

    case WM_KEYDOWN:
        if (GetFocus() == _hwndSearchEdit)
            return 0;

        if (wparam == VK_ESCAPE) {
            CloseStartMenu();
            return 0;
        }

        if (IsSearchResultsVisible()) {
            if (wparam == VK_DOWN) {
                if (!_search_results.empty()) {
                    int visible_count = GetVisibleSearchResultCount();
                    if (_hot_area != HOT_SEARCH_RESULT)
                        _hot_index = _search_result_scroll;
                    else if (_hot_index < (int)_search_results.size() - 1)
                        ++_hot_index;

                    if (visible_count > 0 && _hot_index >= _search_result_scroll + visible_count)
                        ++_search_result_scroll;

                    _hot_area = HOT_SEARCH_RESULT;
                    _search_selected_index = _hot_index;
                    InvalidateRect(_hwnd, NULL, FALSE);
                }
                return 0;
            }

            if (wparam == VK_UP) {
                if (!_search_results.empty()) {
                    int visible_count = GetVisibleSearchResultCount();
                    if (_hot_area != HOT_SEARCH_RESULT)
                        _hot_index = _search_result_scroll;
                    else if (_hot_index > 0)
                        --_hot_index;

                    if (visible_count > 0 && _hot_index < _search_result_scroll)
                        --_search_result_scroll;

                    _hot_area = HOT_SEARCH_RESULT;
                    _search_selected_index = _hot_index;
                    InvalidateRect(_hwnd, NULL, FALSE);
                }
                return 0;
            }

            if (wparam == VK_TAB && AutocompleteSearchSelection())
                return 0;

            if (wparam == VK_RETURN && ExecuteSearchSelection())
                return 0;
        }

        if (wparam == VK_RETURN) {
            switch (_hot_area) {
            case HOT_SEARCH:
                Command(IDC_SEARCH, BN_CLICKED);
                return 0;
            case HOT_PROGRAMS_BUTTON:
                Command(IDC_PROGRAMS, BN_CLICKED);
                return 0;
            case HOT_RECOMMENDED_BUTTON:
                Command(IDC_RECENT, BN_CLICKED);
                return 0;
            case HOT_PROGRAM:
                if (_hot_index >= 0 && _hot_index < (int)_program_items.size())
                    ExecuteItem(_program_items[_hot_index]);
                return 0;
            case HOT_RECOMMENDED:
                if (_hot_index >= 0 && _hot_index < (int)_recommended_items.size())
                    ExecuteItem(_recommended_items[_hot_index]);
                return 0;

            case HOT_SEARCH_HOME_RECENT:
                if (_hot_index >= 0 && _hot_index < GetVisibleSearchHomeRecentCount())
                    ExecuteItem(_search_recent_items[_hot_index]);
                return 0;

            case HOT_SEARCH_HOME_TOP_APP:
                if (_hot_index >= 0 && _hot_index < GetVisibleSearchHomeTopAppCount()) {
                    ModernStartMenuItem &item = !_all_program_items.empty() ? _all_program_items[_hot_index] : _program_items[_hot_index];
                    ExecuteItem(item);
                }
                return 0;

            case HOT_ALL_PROGRAM:
                if (_hot_index >= 0 && _hot_index < (int)_all_program_items.size())
                    ExecuteItem(_all_program_items[_hot_index]);
                return 0;

            case HOT_DRIVE_FOLDER:
                if (_hot_index >= 0 && _hot_index < (int)_drive_folder_items.size())
                    ExecuteItem(_drive_folder_items[_hot_index]);
                return 0;

            default:
                break;
            }
        }
        return 0;

    case WM_CHAR:
        if (GetFocus() == _hwndSearchEdit)
            return 0;

        if (GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_MENU) < 0)
            return 0;

        if (wparam == VK_BACK) {
            if (!_search_query.empty()) {
                String next_query = _search_query.substr(0, _search_query.length() - 1);
                SetSearchQuery(next_query);
            }
            return 0;
        }

        if (wparam >= 32) {
            String next_query = _search_query;
            next_query += (TCHAR)wparam;
            _search_active = true;
            _show_all_programs = false;
            _show_all_recents = false;
            if (_hwndSearchEdit)
                SetFocus(_hwndSearchEdit);
            SetSearchQuery(next_query);
            return 0;
        }
        return 0;

    case WM_ACTIVATEAPP:
        if (!wparam)
            CloseStartMenu();
        return 0;

    case WM_CANCELMODE:
        return 0;

    case WM_NCHITTEST:
        return HTCLIENT;
    }

    return DefWindowProc(_hwnd, nmsg, wparam, lparam);
}

void StartMenuRoot::Paint(HDC canvas)
{
    ClientRect client(_hwnd);
    ModernStartMenuMetrics metrics = GetModernStartMenuMetrics();
    RECT client_rect = MakeRect(0, 0, client.right, client.bottom);
    COLORREF panel_color = RGB(36, 39, 43);
    COLORREF border_color = RGB(82, 86, 91);
    COLORREF section_text = RGB(244, 246, 248);
    COLORREF item_text = RGB(238, 241, 244);
    COLORREF meta_text = RGB(173, 177, 182);
    bool search_highlight = _search_active || IsSearchResultsVisible();
    COLORREF search_fill = GetSearchFillColor();
    COLORREF search_border = search_highlight ? RGB(85, 101, 123) : RGB(61, 65, 71);
    COLORREF search_icon = search_highlight ? RGB(70, 168, 231) : RGB(167, 172, 178);
    COLORREF action_fill = RGB(78, 82, 88);
    COLORREF action_hover_fill = RGB(94, 98, 104);
    COLORREF action_border = RGB(98, 102, 108);
    COLORREF program_hover_fill = RGB(64, 68, 74);
    COLORREF recommended_fill = RGB(44, 47, 52);
    COLORREF recommended_border = RGB(57, 61, 66);
    COLORREF recommended_hover_fill = RGB(61, 65, 71);
    COLORREF footer_fill = RGB(43, 46, 50);
    COLORREF footer_border = RGB(82, 86, 91);
    COLORREF profile_hover_fill = RGB(60, 64, 69);
    COLORREF list_fill = RGB(44, 47, 52);
    COLORREF list_hover_fill = RGB(61, 65, 71);
    COLORREF list_border = RGB(57, 61, 66);
    COLORREF list_separator = RGB(112, 117, 123);

    FillRoundedRectPrimitive(canvas, client_rect, panel_color, metrics._corner_radius, border_color);

    RECT search_rect = GetSearchRect();
    FillRoundedRectPrimitive(canvas, search_rect, search_fill, DPI_SX(20), search_border);

    RECT search_icon_rect = MakeRectWH(search_rect.left + DPI_SX(14),
        search_rect.top + ((search_rect.bottom - search_rect.top - metrics._search_icon_size) / 2),
        metrics._search_icon_size,
        metrics._search_icon_size);
    DrawSearchGlyphPrimitive(canvas, search_icon_rect, search_icon);

    HFONT old_font = (HFONT)SelectObject(canvas, _item_font ? _item_font : g_Globals._hDefaultFont);
    int old_bk_mode = SetBkMode(canvas, TRANSPARENT);
    COLORREF old_text_color = SetTextColor(canvas, RGB(171, 176, 182));
    if (!_hwndSearchEdit) {
        RECT search_text_rect = search_rect;
        search_text_rect.left += DPI_SX(42);
        DrawText(canvas, _search_query.empty() ? TEXT("Search for apps, settings, and documents") : _search_query.c_str(), -1, &search_text_rect,
            DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    RECT programs_header_rect = GetProgramsHeaderRect();
    SelectObject(canvas, _section_font ? _section_font : g_Globals._hDefaultFont);
    SetTextColor(canvas, section_text);

    if (IsSearchResultsVisible()) {
        DrawText(canvas, TEXT("Results"), -1, &programs_header_rect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        int selected_index = GetSelectedSearchResultIndex();

        for (int index = _search_result_scroll; index < _search_result_scroll + GetVisibleSearchResultCount(); ++index) {
            RECT item_rect = GetSearchResultRect(index);
            ModernStartMenuItem &item = _search_results[index];
            EnsureItemIcon(item, DPI_SX(20));

            bool is_hot = _hot_area == HOT_SEARCH_RESULT && _hot_index == index;
            COLORREF tile_color = is_hot ? list_hover_fill : list_fill;
            FillRoundedRectPrimitive(canvas, item_rect, tile_color, DPI_SX(12), list_border);

            HBRUSH tile_brush = CreateSolidBrush(tile_color);
            int icon_left = item_rect.left + DPI_SX(14);
            int icon_top = item_rect.top + ((item_rect.bottom - item_rect.top - DPI_SX(20)) / 2);
            g_Globals._icon_cache.get_icon(item._icon_id).draw(canvas, icon_left, icon_top,
                DPI_SX(20), DPI_SX(20), tile_color, tile_brush);
            DeleteObject(tile_brush);

            RECT title_rect = item_rect;
            title_rect.left += DPI_SX(46);
            title_rect.top += DPI_SY(8);
            title_rect.right -= DPI_SX(12);
            title_rect.bottom = title_rect.top + DPI_SY(18);
            SelectObject(canvas, _item_font ? _item_font : g_Globals._hDefaultFont);
            SetTextColor(canvas, item_text);
            DrawText(canvas, item._title.c_str(), -1, &title_rect,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            RECT meta_rect = item_rect;
            meta_rect.left += DPI_SX(46);
            meta_rect.top += DPI_SY(28);
            meta_rect.right -= DPI_SX(12);
            SelectObject(canvas, _meta_font ? _meta_font : g_Globals._hDefaultFont);
            SetTextColor(canvas, meta_text);
            DrawText(canvas, item._meta_text.empty() ? TEXT("Suggestion") : item._meta_text.c_str(), -1, &meta_rect,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            if (item._show_separator_after && index + 1 < (int)_search_results.size()) {
                int separator_gap = GetSearchResultRowGap(true);
                HPEN separator_pen = CreatePen(PS_SOLID, max(2, DPI_SX(2)), list_separator);
                HGDIOBJ old_pen = SelectObject(canvas, separator_pen);
                int line_y = item_rect.bottom + (separator_gap / 2);
                MoveToEx(canvas, item_rect.left + DPI_SX(8), line_y, NULL);
                LineTo(canvas, item_rect.right - DPI_SX(8), line_y);
                SelectObject(canvas, old_pen);
                DeleteObject(separator_pen);
            }
        }

        DrawVerticalScrollIndicator(canvas, GetSearchResultsRect(), (int)_search_results.size(), GetVisibleSearchResultCount(), _search_result_scroll);

        RECT details_rect = GetSearchDetailsRect();
        FillRoundedRectPrimitive(canvas, details_rect, recommended_fill, DPI_SX(14), recommended_border);

        if (selected_index >= 0 && selected_index < (int)_search_results.size()) {
            ModernStartMenuItem &selected_item = _search_results[selected_index];
            EnsureItemIcon(selected_item, DPI_SX(32));

            String title_text = GetSearchResultTitleText(selected_item);
            String type_text = GetSearchResultTypeText(selected_item);
            String location_text = GetSearchResultLocationText(selected_item);
            RECT content_rect = details_rect;
            InflateRect(&content_rect, -DPI_SX(18), -DPI_SY(18));

            HBRUSH details_brush = CreateSolidBrush(recommended_fill);
            int icon_left = content_rect.left + ((content_rect.right - content_rect.left - DPI_SX(40)) / 2);
            int icon_top = content_rect.top + DPI_SY(8);
            g_Globals._icon_cache.get_icon(selected_item._icon_id).draw(canvas, icon_left, icon_top,
                DPI_SX(40), DPI_SX(40), recommended_fill, details_brush);
            DeleteObject(details_brush);

            RECT title_rect = content_rect;
            title_rect.top = icon_top + DPI_SY(56);
            title_rect.bottom = title_rect.top + DPI_SY(40);
            SelectObject(canvas, _section_font ? _section_font : g_Globals._hDefaultFont);
            SetTextColor(canvas, item_text);
            DrawText(canvas, title_text.c_str(), -1, &title_rect,
                DT_CENTER | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);

            RECT type_rect = content_rect;
            type_rect.top = title_rect.bottom + DPI_SY(4);
            type_rect.bottom = type_rect.top + DPI_SY(18);
            SelectObject(canvas, _meta_font ? _meta_font : g_Globals._hDefaultFont);
            SetTextColor(canvas, meta_text);
            DrawText(canvas, type_text.c_str(), -1, &type_rect,
                DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            RECT separator_rect = MakeRect(content_rect.left, type_rect.bottom + DPI_SY(18), content_rect.right, type_rect.bottom + DPI_SY(19));
            HBRUSH separator_brush = CreateSolidBrush(recommended_border);
            FillRect(canvas, &separator_rect, separator_brush);

            RECT location_label_rect = content_rect;
            location_label_rect.top = separator_rect.bottom + DPI_SY(14);
            location_label_rect.bottom = location_label_rect.top + DPI_SY(16);
            SelectObject(canvas, _meta_font ? _meta_font : g_Globals._hDefaultFont);
            SetTextColor(canvas, meta_text);
            DrawText(canvas, TEXT("Location"), -1, &location_label_rect,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

            RECT location_rect = content_rect;
            location_rect.top = location_label_rect.bottom + DPI_SY(6);
            location_rect.bottom = location_rect.top + DPI_SY(34);
            SelectObject(canvas, _item_font ? _item_font : g_Globals._hDefaultFont);
            SetTextColor(canvas, item_text);
            DrawText(canvas, location_text.c_str(), -1, &location_rect,
                DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);

            RECT action_separator_rect = MakeRect(content_rect.left, GetSearchDetailsActionRect(0).top - DPI_SY(12), content_rect.right, GetSearchDetailsActionRect(0).top - DPI_SY(11));
            FillRect(canvas, &action_separator_rect, separator_brush);
            DeleteObject(separator_brush);

            RECT open_rect = GetSearchDetailsActionRect(0);
            COLORREF open_fill = _hot_area == HOT_SEARCH_DETAIL_OPEN ? action_hover_fill : action_fill;
            FillRoundedRectPrimitive(canvas, open_rect, open_fill, DPI_SX(10), action_border);
            RECT open_text_rect = open_rect;
            open_text_rect.left += DPI_SX(14);
            SelectObject(canvas, _item_font ? _item_font : g_Globals._hDefaultFont);
            SetTextColor(canvas, RGB(236, 239, 242));
            DrawText(canvas, TEXT("Open"), -1, &open_text_rect,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            RECT copy_rect = GetSearchDetailsActionRect(1);
            COLORREF copy_fill = _hot_area == HOT_SEARCH_DETAIL_COPY ? action_hover_fill : action_fill;
            FillRoundedRectPrimitive(canvas, copy_rect, copy_fill, DPI_SX(10), action_border);
            RECT copy_text_rect = copy_rect;
            copy_text_rect.left += DPI_SX(14);
            DrawText(canvas, TEXT("Copy path"), -1, &copy_text_rect,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        if (GetVisibleSearchResultCount() == 0) {
            RECT empty_rect = GetSearchResultsRect();
            SelectObject(canvas, _item_font ? _item_font : g_Globals._hDefaultFont);
            SetTextColor(canvas, meta_text);
            DrawText(canvas, TEXT("No matches found."), -1, &empty_rect,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        }
    } else if (IsSearchHomeVisible()) {
        RECT recent_header_rect = GetSearchHomeRecentHeaderRect();
        DrawText(canvas, TEXT("Recent apps"), -1, &recent_header_rect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        for (int index = 0; index < GetVisibleSearchHomeRecentCount(); ++index) {
            RECT item_rect = GetSearchHomeRecentRowRect(index);
            ModernStartMenuItem &item = _search_recent_items[index];
            EnsureItemIcon(item, DPI_SX(20));

            COLORREF tile_color = (_hot_area == HOT_SEARCH_HOME_RECENT && _hot_index == index) ? list_hover_fill : panel_color;
            if (_hot_area == HOT_SEARCH_HOME_RECENT && _hot_index == index)
                FillRoundedRectPrimitive(canvas, item_rect, tile_color, DPI_SX(12));

            HBRUSH tile_brush = CreateSolidBrush(tile_color);
            int icon_left = item_rect.left + DPI_SX(6);
            int icon_top = item_rect.top + ((item_rect.bottom - item_rect.top - DPI_SX(20)) / 2);
            g_Globals._icon_cache.get_icon(item._icon_id).draw(canvas, icon_left, icon_top,
                DPI_SX(20), DPI_SX(20), tile_color, tile_brush);
            DeleteObject(tile_brush);

            RECT title_rect = item_rect;
            title_rect.left += DPI_SX(36);
            title_rect.top += DPI_SY(3);
            title_rect.right -= DPI_SX(6);
            title_rect.bottom = title_rect.top + DPI_SY(18);
            SelectObject(canvas, _item_font ? _item_font : g_Globals._hDefaultFont);
            SetTextColor(canvas, item_text);
            DrawText(canvas, item._title.c_str(), -1, &title_rect,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            RECT meta_rect = item_rect;
            meta_rect.left += DPI_SX(36);
            meta_rect.top += DPI_SY(21);
            meta_rect.right -= DPI_SX(6);
            SelectObject(canvas, _meta_font ? _meta_font : g_Globals._hDefaultFont);
            SetTextColor(canvas, meta_text);
            DrawText(canvas, item._meta_text.empty() ? TEXT("Recent app") : item._meta_text.c_str(), -1, &meta_rect,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }

        if (GetVisibleSearchHomeRecentCount() == 0) {
            RECT empty_rect = GetSearchHomeRecentListRect();
            SelectObject(canvas, _item_font ? _item_font : g_Globals._hDefaultFont);
            SetTextColor(canvas, meta_text);
            DrawText(canvas, TEXT("No recent apps yet."), -1, &empty_rect,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        }

        RECT top_apps_header_rect = GetSearchHomeTopAppsHeaderRect();
        SelectObject(canvas, _section_font ? _section_font : g_Globals._hDefaultFont);
        SetTextColor(canvas, section_text);
        DrawText(canvas, TEXT("Top apps"), -1, &top_apps_header_rect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        for (int index = 0; index < GetVisibleSearchHomeTopAppCount(); ++index) {
            RECT item_rect = GetSearchHomeTopAppRect(index);
            ModernStartMenuItem &item = !_all_program_items.empty() ? _all_program_items[index] : _program_items[index];
            EnsureItemIcon(item, DPI_SX(24));

            COLORREF tile_color = (_hot_area == HOT_SEARCH_HOME_TOP_APP && _hot_index == index) ? recommended_hover_fill : recommended_fill;
            FillRoundedRectPrimitive(canvas, item_rect, tile_color, DPI_SX(12), recommended_border);

            HBRUSH tile_brush = CreateSolidBrush(tile_color);
            int icon_left = item_rect.left + ((item_rect.right - item_rect.left - DPI_SX(24)) / 2);
            int icon_top = item_rect.top + DPI_SY(14);
            g_Globals._icon_cache.get_icon(item._icon_id).draw(canvas, icon_left, icon_top,
                DPI_SX(24), DPI_SX(24), tile_color, tile_brush);
            DeleteObject(tile_brush);

            RECT text_rect = item_rect;
            text_rect.top = icon_top + DPI_SX(24) + DPI_SY(10);
            text_rect.left += DPI_SX(10);
            text_rect.right -= DPI_SX(10);
            text_rect.bottom -= DPI_SY(10);
            SelectObject(canvas, _item_font ? _item_font : g_Globals._hDefaultFont);
            SetTextColor(canvas, item_text);
            DrawText(canvas, item._title.c_str(), -1, &text_rect,
                DT_CENTER | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
    } else if (_show_all_programs) {
        DrawText(canvas, TEXT("Installed apps"), -1, &programs_header_rect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        RECT programs_button_rect = GetProgramsButtonRect();
        FillRoundedRectPrimitive(canvas, programs_button_rect,
            _hot_area == HOT_PROGRAMS_BUTTON ? action_hover_fill : action_fill,
            DPI_SX(12), action_border);
        DrawSectionActionButtonContent(canvas, programs_button_rect, TEXT("Back"), _meta_font,
            RGB(236, 239, 242), true);

        for (int index = _all_program_scroll; index < _all_program_scroll + GetVisibleAllProgramCount(); ++index) {
            RECT item_rect = GetAllProgramRowRect(index);
            ModernStartMenuItem &item = _all_program_items[index];
            EnsureItemIcon(item, DPI_SX(18));

            COLORREF tile_color = (_hot_area == HOT_ALL_PROGRAM && _hot_index == index) ? list_hover_fill : list_fill;
            FillRoundedRectPrimitive(canvas, item_rect, tile_color, DPI_SX(10), list_border);

            HBRUSH tile_brush = CreateSolidBrush(tile_color);
            int icon_left = item_rect.left + DPI_SX(12);
            int icon_top = item_rect.top + ((item_rect.bottom - item_rect.top - DPI_SX(18)) / 2);
            g_Globals._icon_cache.get_icon(item._icon_id).draw(canvas, icon_left, icon_top,
                DPI_SX(18), DPI_SX(18), tile_color, tile_brush);
            DeleteObject(tile_brush);

            RECT title_rect = item_rect;
            title_rect.left += DPI_SX(42);
            title_rect.right -= DPI_SX(10);
            SelectObject(canvas, _item_font ? _item_font : g_Globals._hDefaultFont);
            SetTextColor(canvas, item_text);
            DrawText(canvas, item._title.c_str(), -1, &title_rect,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }

        if (GetVisibleAllProgramCount() == 0) {
            RECT empty_rect = GetAllAppsListRect();
            SelectObject(canvas, _item_font ? _item_font : g_Globals._hDefaultFont);
            SetTextColor(canvas, meta_text);
            DrawText(canvas, TEXT("No installed apps found."), -1, &empty_rect,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        }

        DrawVerticalScrollIndicator(canvas, GetAllAppsListRect(), (int)_all_program_items.size(), GetVisibleAllProgramCount(), _all_program_scroll);

        RECT drive_header_rect = GetDriveFoldersHeaderRect();
        SelectObject(canvas, _section_font ? _section_font : g_Globals._hDefaultFont);
        SetTextColor(canvas, section_text);
        DrawText(canvas, TEXT("Folders in C:\\"), -1, &drive_header_rect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        for (int index = _drive_folder_scroll; index < _drive_folder_scroll + GetVisibleDriveFolderCount(); ++index) {
            RECT item_rect = GetDriveFolderRowRect(index);
            ModernStartMenuItem &item = _drive_folder_items[index];

            COLORREF tile_color = (_hot_area == HOT_DRIVE_FOLDER && _hot_index == index) ? list_hover_fill : list_fill;
            FillRoundedRectPrimitive(canvas, item_rect, tile_color, DPI_SX(10), list_border);

            HBRUSH tile_brush = CreateSolidBrush(tile_color);
            int icon_left = item_rect.left + DPI_SX(12);
            int icon_top = item_rect.top + ((item_rect.bottom - item_rect.top - DPI_SX(18)) / 2);
            g_Globals._icon_cache.get_icon(item._icon_id).draw(canvas, icon_left, icon_top,
                DPI_SX(18), DPI_SX(18), tile_color, tile_brush);
            DeleteObject(tile_brush);

            RECT title_rect = item_rect;
            title_rect.left += DPI_SX(42);
            title_rect.top += DPI_SY(4);
            title_rect.right -= DPI_SX(10);
            title_rect.bottom = title_rect.top + DPI_SY(16);
            SelectObject(canvas, _item_font ? _item_font : g_Globals._hDefaultFont);
            SetTextColor(canvas, item_text);
            DrawText(canvas, item._title.c_str(), -1, &title_rect,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            RECT meta_rect = item_rect;
            meta_rect.left += DPI_SX(42);
            meta_rect.top += DPI_SY(18);
            meta_rect.right -= DPI_SX(10);
            SelectObject(canvas, _meta_font ? _meta_font : g_Globals._hDefaultFont);
            SetTextColor(canvas, meta_text);
            DrawText(canvas, item._path.c_str(), -1, &meta_rect,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }

        if (GetVisibleDriveFolderCount() == 0) {
            RECT empty_rect = GetDriveFoldersListRect();
            SelectObject(canvas, _item_font ? _item_font : g_Globals._hDefaultFont);
            SetTextColor(canvas, meta_text);
            DrawText(canvas, TEXT("No visible folders in C:\\."), -1, &empty_rect,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        }

        DrawVerticalScrollIndicator(canvas, GetDriveFoldersListRect(), (int)_drive_folder_items.size(), GetVisibleDriveFolderCount(), _drive_folder_scroll);
    } else if (_show_all_recents) {
        RECT recommended_header_rect = GetRecommendedHeaderRect();
        SelectObject(canvas, _section_font ? _section_font : g_Globals._hDefaultFont);
        SetTextColor(canvas, section_text);
        DrawText(canvas, TEXT("Recents"), -1, &recommended_header_rect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        RECT recommended_button_rect = GetRecommendedButtonRect();
        FillRoundedRectPrimitive(canvas, recommended_button_rect,
            _hot_area == HOT_RECOMMENDED_BUTTON ? action_hover_fill : action_fill,
            DPI_SX(12), action_border);
        DrawSectionActionButtonContent(canvas, recommended_button_rect, TEXT("Back"), _meta_font,
            RGB(236, 239, 242), true);

        for (int index = _recent_document_scroll; index < _recent_document_scroll + GetVisibleRecentDocumentCount(); ++index) {
            RECT item_rect = GetRecentDocumentRowRect(index);
            ModernStartMenuItem &item = _recommended_items[index];
            EnsureItemIcon(item, DPI_SX(20));

            COLORREF tile_color = (_hot_area == HOT_RECOMMENDED && _hot_index == index) ? list_hover_fill : list_fill;
            FillRoundedRectPrimitive(canvas, item_rect, tile_color, DPI_SX(10), list_border);

            HBRUSH tile_brush = CreateSolidBrush(tile_color);
            int icon_left = item_rect.left + DPI_SX(12);
            int icon_top = item_rect.top + ((item_rect.bottom - item_rect.top - DPI_SX(20)) / 2);
            g_Globals._icon_cache.get_icon(item._icon_id).draw(canvas, icon_left, icon_top,
                DPI_SX(20), DPI_SX(20), tile_color, tile_brush);
            DeleteObject(tile_brush);

            RECT title_rect = item_rect;
            title_rect.left += DPI_SX(44);
            title_rect.top += DPI_SY(7);
            title_rect.right -= DPI_SX(10);
            title_rect.bottom = title_rect.top + DPI_SY(18);
            SelectObject(canvas, _item_font ? _item_font : g_Globals._hDefaultFont);
            SetTextColor(canvas, item_text);
            DrawText(canvas, item._title.c_str(), -1, &title_rect,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            RECT meta_rect = item_rect;
            meta_rect.left += DPI_SX(44);
            meta_rect.top += DPI_SY(28);
            meta_rect.right -= DPI_SX(10);
            SelectObject(canvas, _meta_font ? _meta_font : g_Globals._hDefaultFont);
            SetTextColor(canvas, meta_text);
            DrawText(canvas, item._meta_text.empty() ? TEXT("Recent item") : item._meta_text.c_str(), -1, &meta_rect,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }

        if (GetVisibleRecentDocumentCount() == 0) {
            RECT empty_rect = GetRecentDocumentsListRect();
            SelectObject(canvas, _item_font ? _item_font : g_Globals._hDefaultFont);
            SetTextColor(canvas, meta_text);
            DrawText(canvas, TEXT("No recents yet."), -1, &empty_rect,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        }

        DrawVerticalScrollIndicator(canvas, GetRecentDocumentsListRect(), (int)_recommended_items.size(), GetVisibleRecentDocumentCount(), _recent_document_scroll);
    } else {
        DrawText(canvas, TEXT("Pinned"), -1, &programs_header_rect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        RECT programs_button_rect = GetProgramsButtonRect();
        FillRoundedRectPrimitive(canvas, programs_button_rect,
            _hot_area == HOT_PROGRAMS_BUTTON ? action_hover_fill : action_fill,
            DPI_SX(12), action_border);
        DrawSectionActionButtonContent(canvas, programs_button_rect, TEXT("All"), _meta_font,
            RGB(236, 239, 242), false);

        for (int index = 0; index < GetVisibleProgramCount(); ++index) {
            RECT item_rect = GetProgramTileRect(index);
            ModernStartMenuItem &item = _program_items[index];
            EnsureItemIcon(item, _program_icon_size);

            bool is_hot = _hot_area == HOT_PROGRAM && _hot_index == index;
            COLORREF tile_color = is_hot ? program_hover_fill : panel_color;
            if (is_hot)
                FillRoundedRectPrimitive(canvas, item_rect, tile_color, DPI_SX(14));

            HBRUSH tile_brush = CreateSolidBrush(tile_color);
            int icon_left = item_rect.left + ((item_rect.right - item_rect.left - _program_icon_size) / 2);
            int icon_top = item_rect.top + DPI_SY(8);
            g_Globals._icon_cache.get_icon(item._icon_id).draw(canvas, icon_left, icon_top,
                _program_icon_size, _program_icon_size, tile_color, tile_brush);
            DeleteObject(tile_brush);

            RECT text_rect = item_rect;
            text_rect.top = icon_top + _program_icon_size + DPI_SY(8);
            text_rect.left += DPI_SX(6);
            text_rect.right -= DPI_SX(6);
            text_rect.bottom -= DPI_SY(8);
            SelectObject(canvas, _item_font ? _item_font : g_Globals._hDefaultFont);
            SetTextColor(canvas, item_text);
            UINT title_format = DT_CENTER | DT_NOPREFIX | DT_END_ELLIPSIS;
            if (item._title.length() <= 10 || item._title.find(TEXT(" ")) == String::npos)
                title_format |= DT_SINGLELINE | DT_VCENTER;
            else
                title_format |= DT_TOP | DT_WORDBREAK;
            DrawText(canvas, item._title.c_str(), -1, &text_rect,
                title_format);
        }

        if (GetVisibleProgramCount() == 0) {
            RECT empty_rect = GetProgramsGridRect();
            SelectObject(canvas, _item_font ? _item_font : g_Globals._hDefaultFont);
            SetTextColor(canvas, meta_text);
            DrawText(canvas, TEXT("No applications found."), -1, &empty_rect,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        }

        RECT recommended_header_rect = GetRecommendedHeaderRect();
        SelectObject(canvas, _section_font ? _section_font : g_Globals._hDefaultFont);
        SetTextColor(canvas, section_text);
        DrawText(canvas, TEXT("Recents"), -1, &recommended_header_rect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        RECT recommended_button_rect = GetRecommendedButtonRect();
        FillRoundedRectPrimitive(canvas, recommended_button_rect,
            _hot_area == HOT_RECOMMENDED_BUTTON ? action_hover_fill : action_fill,
            DPI_SX(12), action_border);
        DrawSectionActionButtonContent(canvas, recommended_button_rect, TEXT("More"), _meta_font,
            RGB(236, 239, 242), false);

        for (int index = 0; index < GetVisibleRecommendedCount(); ++index) {
            RECT item_rect = GetRecommendedTileRect(index);
            ModernStartMenuItem &item = _recommended_items[index];
            EnsureItemIcon(item, _recommended_icon_size);

            COLORREF tile_color = (_hot_area == HOT_RECOMMENDED && _hot_index == index) ? recommended_hover_fill : recommended_fill;
            FillRoundedRectPrimitive(canvas, item_rect, tile_color, DPI_SX(12), recommended_border);

            HBRUSH tile_brush = CreateSolidBrush(tile_color);
            int icon_left = item_rect.left + DPI_SX(14);
            int icon_top = item_rect.top + ((item_rect.bottom - item_rect.top - _recommended_icon_size) / 2);
            g_Globals._icon_cache.get_icon(item._icon_id).draw(canvas, icon_left, icon_top,
                _recommended_icon_size, _recommended_icon_size, tile_color, tile_brush);
            DeleteObject(tile_brush);

            RECT title_rect = item_rect;
            title_rect.left += DPI_SX(44);
            title_rect.top += DPI_SY(9);
            title_rect.right -= DPI_SX(10);
            title_rect.bottom = title_rect.top + DPI_SY(18);
            SelectObject(canvas, _item_font ? _item_font : g_Globals._hDefaultFont);
            SetTextColor(canvas, item_text);
            DrawText(canvas, item._title.c_str(), -1, &title_rect,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            RECT meta_rect = item_rect;
            meta_rect.left += DPI_SX(44);
            meta_rect.top += DPI_SY(30);
            meta_rect.right -= DPI_SX(10);
            SelectObject(canvas, _meta_font ? _meta_font : g_Globals._hDefaultFont);
            SetTextColor(canvas, meta_text);
            DrawText(canvas, item._meta_text.empty() ? TEXT("Recent item") : item._meta_text.c_str(), -1, &meta_rect,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }

        if (GetVisibleRecommendedCount() == 0) {
            RECT empty_rect = GetRecommendedGridRect();
            SelectObject(canvas, _meta_font ? _meta_font : g_Globals._hDefaultFont);
            SetTextColor(canvas, meta_text);
            DrawText(canvas, TEXT("No recents yet."), -1, &empty_rect,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    RECT footer_rect = GetFooterRect();
    HBRUSH footer_brush = CreateSolidBrush(footer_fill);
    FillRect(canvas, &footer_rect, footer_brush);
    DeleteObject(footer_brush);
    HBRUSH separator_brush = CreateSolidBrush(footer_border);
    RECT separator_rect = MakeRect(footer_rect.left, footer_rect.top, footer_rect.right, footer_rect.top + 1);
    FillRect(canvas, &separator_rect, separator_brush);
    DeleteObject(separator_brush);

    RECT profile_rect = GetProfileRect();
    RECT profile_chip_rect = profile_rect;
    profile_chip_rect.top += DPI_SY(6);
    profile_chip_rect.bottom -= DPI_SY(6);
    profile_chip_rect.right -= DPI_SX(6);
    if (_hot_area == HOT_PROFILE)
        FillRoundedRectPrimitive(canvas, profile_chip_rect, profile_hover_fill, DPI_SX(12));

    RECT avatar_rect = MakeRectWH(profile_rect.left,
        footer_rect.top + ((footer_rect.bottom - footer_rect.top - metrics._avatar_size) / 2),
        metrics._avatar_size,
        metrics._avatar_size);
    HBRUSH avatar_brush = CreateSolidBrush(RGB(86, 112, 170));
    HPEN avatar_pen = CreatePen(PS_NULL, 0, 0);
    HGDIOBJ old_brush = SelectObject(canvas, avatar_brush);
    HGDIOBJ old_pen = SelectObject(canvas, avatar_pen);
    Ellipse(canvas, avatar_rect.left, avatar_rect.top, avatar_rect.right, avatar_rect.bottom);
    SelectObject(canvas, old_pen);
    SelectObject(canvas, old_brush);
    DeleteObject(avatar_pen);
    DeleteObject(avatar_brush);

    String initials;
    if (!_user_name.empty()) {
        initials += _user_name.at(0);
        size_t split_pos = _user_name.find(TEXT(' '));
        if (split_pos != String::npos && split_pos + 1 < _user_name.length())
            initials += _user_name.at(split_pos + 1);
    } else {
        initials = TEXT("U");
    }

    RECT avatar_text_rect = avatar_rect;
    SelectObject(canvas, _item_font ? _item_font : g_Globals._hDefaultFont);
    SetTextColor(canvas, RGB(255, 255, 255));
    DrawText(canvas, initials.c_str(), -1, &avatar_text_rect,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    RECT user_text_rect = profile_rect;
    user_text_rect.left = avatar_rect.right + DPI_SX(12);
    SelectObject(canvas, _item_font ? _item_font : g_Globals._hDefaultFont);
    SetTextColor(canvas, item_text);
    DrawText(canvas, _user_name.c_str(), -1, &user_text_rect,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    SetTextColor(canvas, old_text_color);
    SetBkMode(canvas, old_bk_mode);
    SelectObject(canvas, old_font);
}

void StartMenuRoot::CloseStartMenu(int id)
{
    UNREFERENCED_PARAMETER(id);

    _tracking_mouse = false;
    ClearHotState();
    _search_active = false;
    RefreshSearchEditBrush();

    if (IsStartMenuVisible())
        AnimateHide();
}

bool StartMenuRoot::IsStartMenuVisible() const
{
    return IsWindowVisible(_hwnd) != FALSE;
}


int StartMenuHandler::Command(int id, int code)
{
    switch (id) {

    // start menu root

    case IDC_PROGRAMS:
        CreateSubmenu(id, CSIDL_COMMON_PROGRAMS, CSIDL_PROGRAMS, ResString(IDS_PROGRAMS));
        break;

    case IDC_EXPLORE:
        CloseStartMenu(id);
        {
            TCHAR peazip_path[MAX_PATH] = { 0 };
            if (TryGetPeaZipPath(peazip_path, COUNTOF(peazip_path)))
                launch_file(_hwnd, peazip_path, SW_SHOWNORMAL);
            else
                explorer_show_frame(SW_SHOWNORMAL);
        }
        break;

    case IDC_LAUNCH:
        CloseStartMenu(id);
        ShowLaunchDialog(g_Globals._hwndDesktopBar);
        break;

    case IDC_DOCUMENTS:
        CreateSubmenu(id, CSIDL_PERSONAL, ResString(IDS_DOCUMENTS));
        break;

    case IDC_RECENT:
        CreateSubmenu(id, CSIDL_RECENT, ResString(IDS_RECENT), STARTMENU_CREATOR(RecentStartMenu));
        break;

    case IDC_FAVORITES:
        CreateSubmenu(id, CSIDL_COMMON_FAVORITES, CSIDL_FAVORITES, ResString(IDS_FAVORITES));
        break;

    case IDC_BROWSE:
        CreateSubmenu(id, ResString(IDS_BROWSE), STARTMENU_CREATOR(BrowseMenu));
        break;

    case IDC_SETTINGS:
        CreateSubmenu(id, ResString(IDS_SETTINGS), STARTMENU_CREATOR(SettingsMenu));
        break;

    case IDC_SEARCH:
        CreateSubmenu(id, ResString(IDS_SEARCH), STARTMENU_CREATOR(SearchMenu));
        break;

    case IDC_START_HELP:
        CloseStartMenu(id);
        MessageBox(g_Globals._hwndDesktopBar, TEXT("Help not yet implemented"), ResString(IDS_TITLE), MB_OK);
        break;

    case IDC_LOGOFF:
        CloseStartMenu(id);
        if (g_Globals._lua) {
            if (g_Globals._lua->call("Startmenu:Logoff") == 0) break;
        }
        if (CommandHook(g_Globals._hwndDesktop, TEXT("logoff")) == 1) break;
        ShowLogoffDialog(g_Globals._hwndDesktop);
        break;

    case IDC_RESTART:
        CloseStartMenu(id);
        if (g_Globals._lua) {
            if (g_Globals._lua->call("Startmenu:Reboot") == 0) break;
        }
        if (CommandHook(g_Globals._hwndDesktop, TEXT("reboot")) == 1) break;
        ShowRestartDialog(g_Globals._hwndDesktop, EWX_REBOOT);
        /* An alternative way to do restart without shell32 help */
        //launch_file(_hwnd, TEXT("shutdown.exe"), SW_HIDE, TEXT("-r"));
        break;

    case IDC_SHUTDOWN:
        CloseStartMenu(id);
        if (g_Globals._lua) {
            if (g_Globals._lua->call("Startmenu:Shutdown") == 0) break;
        }
        if (CommandHook(g_Globals._hwndDesktop, TEXT("shutdown")) == 1) break;
        ShowExitWindowsDialog(g_Globals._hwndDesktop);
        break;

#ifndef __REACTOS__
    case IDC_TERMINATE:
        DestroyWindow(g_Globals._hwndDesktopBar);
        DestroyWindow(g_Globals._hwndDesktop);
        break;
#endif

    // settings menu

    case ID_DESKTOPBAR_SETTINGS:
        CloseStartMenu(id);
        ExplorerPropertySheet(g_Globals._hwndDesktopBar);
        break;

    case IDC_CONTROL_PANEL: {
        CloseStartMenu(id);

        if (g_Globals._lua) {
            if (g_Globals._lua->call("Startmenu:ControlPanel") == 0) break;
        }
        if (CommandHook(_hwnd, TEXT("control")) == 1) break;

        //explorer_open_frame(SW_SHOWNORMAL, SHELLPATH_CONTROL_PANEL);
        launch_file(_hwnd, SHELLPATH_CONTROL_PANEL);
        break;
    }

    case IDC_SETTINGS_MENU:
        CreateSubmenu(id, CSIDL_CONTROLS, ResString(IDS_SETTINGS_MENU));
        break;

    case IDC_PRINTERS: {
        CloseStartMenu(id);

#ifndef ROSSHELL
        explorer_open_frame(SW_SHOWNORMAL, SHELLPATH_PRINTERS);
#else
        launch_file(_hwnd, SHELLPATH_PRINTERS);
#endif
        break;
    }

#if 0   ///@todo use printer start menu folder per default and allow opening "printers" cabinet window using the context menu
    case IDC_PRINTERS_MENU:
        CreateSubmenu(id, CSIDL_PRINTERS, CSIDL_PRINTHOOD, ResString(IDS_PRINTERS));
        /*      StartMenuFolders new_folders;

                try {
                    new_folders.push_back(ShellPath(TEXT("::{20D04FE0-3AEA-1069-A2D8-08002B30309D}\\::{21EC2020-3AEA-1069-A2DD-08002B30309D}\\::{2227A280-3AEA-1069-A2DE-08002B30309D}")));
                } catch(COMException&) {
                }

                CreateSubmenu(id, new_folders, ResString(IDS_PRINTERS));*/
        break;
#endif

    case IDC_ADMIN:
#ifndef ROSSHELL
        CreateSubmenu(id, CSIDL_COMMON_ADMINTOOLS, CSIDL_ADMINTOOLS, ResString(IDS_ADMIN));
        //CloseStartMenu(id);
        //MainFrame::Create(SpecialFolderPath(CSIDL_COMMON_ADMINTOOLS, _hwnd), OWM_PIDL);
#else
        launch_file(_hwnd, SpecialFolderFSPath(CSIDL_COMMON_ADMINTOOLS, _hwnd));
#endif
        break;

    case IDC_CONNECTIONS: {
        CloseStartMenu(id);
#ifndef ROSSHELL
        explorer_open_frame(SW_SHOWNORMAL, SHELLPATH_NET_CONNECTIONS);
#else
        launch_file(_hwnd, SHELLPATH_NET_CONNECTIONS);
#endif
        break;
    }
    case IDC_CONNECTIONS_FOLDER:
        CreateSubmenu(id, CSIDL_CONNECTIONS, ResString(IDS_CONNECTIONS));
        break;


    // browse menu

    case IDC_NETWORK:
#ifdef __REACTOS__  ///@todo to be removed when network browsing will be implemented in shell namespace
        MessageBox(0, TEXT("not yet implemented"), ResString(IDS_TITLE), MB_OK);
#else
        CreateSubmenu(id, CSIDL_NETWORK, ResString(IDS_NETWORK));
#endif
        break;

    case IDC_DRIVES:
        ///@todo exclude removable drives
        CreateSubmenu(id, CSIDL_DRIVES, ResString(IDS_DRIVES));
        break;

    // search menu
    case IDC_SEARCH_FILES:
        CloseStartMenu(id);
        ShowSearchDialog();
        break;

    case IDC_SEARCH_COMPUTER:
        CloseStartMenu(id);
        ShowSearchComputer();
        break;


    default:
        return super::Command(id, code);
    }

    return 0;
}


void StartMenuHandler::ShowSearchDialog()
{
#ifndef __REACTOS__ ///@todo to be removed when SHFindFiles() will be implemented in shell32.dll
    static DynamicFct<SHFINDFILES> SHFindFiles(TEXT("SHELL32"), 90);

    if (SHFindFiles)
        (*SHFindFiles)(NULL, NULL);
    else
#endif
        MessageBox(0, TEXT("SHFindFiles() not yet implemented in SHELL32"), ResString(IDS_TITLE), MB_OK);
}

void StartMenuHandler::ShowSearchComputer()
{
#ifndef __REACTOS__ ///@todo to be removed when SHFindComputer() will be implemented in shell32.dll
    static DynamicFct<SHFINDCOMPUTER> SHFindComputer(TEXT("SHELL32"), 91);

    if (SHFindComputer)
        (*SHFindComputer)(NULL, NULL);
    else
#endif
        MessageBox(0, TEXT("SHFindComputer() not yet implemented in SHELL32"), ResString(IDS_TITLE), MB_OK);
}

struct RunDialogThread : public Thread {
    HWND _hwnd;
    int Run();
};

int RunDialogThread::Run()
{
    static DynamicFct<RUNFILEDLG> RunFileDlg(TEXT("SHELL32"), 61);

    // RunFileDlg needs owner window to properly position dialog
    // that window will be disabled so we can't use DesktopBar
    RECT rect = {0};
#ifndef TASKBAR_AT_TOP
    rect.top = GetSystemMetrics(SM_CYSCREEN) - DESKTOPBARBAR_HEIGHT;
#endif
    rect.right = GetSystemMetrics(SM_CXSCREEN);
    rect.bottom = rect.top + DESKTOPBARBAR_HEIGHT;
    Static dlgOwner(0, 0, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, 0, 0);
    _hwnd = dlgOwner;
    // Show "Run..." dialog
    TCHAR home_dir[MAX_PATH] = { 0 };
    GetEnvironmentVariable(TEXT("USERPROFILE"), home_dir, MAX_PATH);
    if (RunFileDlg) {
        (*RunFileDlg)(dlgOwner, 0, home_dir, NULL, NULL, RFF_CALCDIRECTORY);
    }
    DestroyWindow(dlgOwner);
    return 0;
}

void ShowLaunchDialog(HWND hwndOwner)
{
    static RunDialogThread *rdt = NULL;
    if (rdt) {
        if (rdt->is_alive()) {
            HWND hwnd = GetNextWindow(rdt->_hwnd, GW_HWNDPREV);
            SwitchToThisWindow(hwnd, TRUE);
            return;
        } else {
            delete rdt;
            rdt = NULL;
        }
    }

    rdt = new RunDialogThread();
    rdt->Start();
}

static int CommandHook(HWND hwnd, const TCHAR *act)
{
    String cmd = TEXT("");
    INT showflags = SW_SHOWNORMAL;
    String parameters = TEXT("");
    if (g_Globals._isWinPE) {
        cmd = JCfg_GetValue(&g_JCfg, TEXT("JS_STARTMENU"), TEXT("commands"), act, TEXT("command"), Value(TEXT(""))).ToString();
    }
    if (cmd != TEXT("")) {
        showflags = JCfg_GetValue(&g_JCfg, TEXT("JS_STARTMENU"), TEXT("commands"), act, TEXT("showflags"), Value(showflags)).ToInt();
        parameters = JCfg_GetValue(&g_JCfg, TEXT("JS_STARTMENU"), TEXT("commands"), act, TEXT("parameters"), Value(TEXT(""))).ToString();
        launch_file(hwnd, cmd, showflags, parameters);
        return 1;
    }
    return 0;
}

void ShowLogoffDialog(HWND hwndOwner)
{
    static DynamicFct<LOGOFFWINDOWSDIALOG> LogoffWindowsDialog(TEXT("SHELL32"), 54);
    //  static DynamicFct<RESTARTWINDOWSDLG> RestartDialog(TEXT("SHELL32"), 59);

    if (LogoffWindowsDialog)
        (*LogoffWindowsDialog)(0);
    /* The RestartDialog function prompts about some system setting change. This is not what we want to display here.
        else if (RestartDialog)
            return (*RestartDialog)(hwndOwner, (LPWSTR)L"You selected <Log Off>.\n\n", EWX_LOGOFF) == 1;    ///@todo ANSI string conversion if needed
    */
    else
        MessageBox(hwndOwner, TEXT("LogoffWindowsDialog() not yet implemented in SHELL32"), ResString(IDS_TITLE), MB_OK);
}

void ShowExitWindowsDialog(HWND hwndOwner)
{
    static DynamicFct<EXITWINDOWSDLG> ExitWindowsDialog(TEXT("SHELL32"), 60);

    if (ExitWindowsDialog)
        (*ExitWindowsDialog)(hwndOwner);
    else
        MessageBox(hwndOwner, TEXT("ExitWindowsDialog() not yet implemented in SHELL32"), ResString(IDS_TITLE), MB_OK);
}

void StartMenuHandler::ShowRestartDialog(HWND hwndOwner, UINT flags)
{
    static DynamicFct<RESTARTWINDOWSDLG> RestartDlg(TEXT("SHELL32"), 59);

    if (RestartDlg)
        (*RestartDlg)(hwndOwner, (LPWSTR)L"You selected restart.\n\n", flags);
    else
        MessageBox(hwndOwner, TEXT("RestartDlg() not yet implemented in SHELL32"), ResString(IDS_TITLE), MB_OK);
}

void SettingsMenu::AddEntries()
{
    super::AddEntries();

#if defined(ROSSHELL) || defined(__REACTOS__)   // __REACTOS__ to be removed when printers will be implemented
    //TODO  AddButton(ResString(IDS_PRINTERS),          ICID_PRINTER, false, IDC_PRINTERS_MENU);
#else
    //TODO  AddButton(ResString(IDS_PRINTERS),          ICID_PRINTER, true, IDC_PRINTERS_MENU);
#endif

    AddButton(ResString(IDS_CONNECTIONS),       ICID_NETCONNS, false, IDC_CONNECTIONS);

    AddButton(ResString(IDS_ADMIN),             ICID_ADMIN, true, IDC_ADMIN);

    /* if (!g_Globals._SHRestricted || !SHRestricted(REST_NOCONTROLPANEL))
        AddButton(ResString(IDS_SETTINGS_MENU), ICID_CONFIG, true, IDC_SETTINGS_MENU); */

    AddButton(ResString(IDS_DESKTOPBAR_SETTINGS), ICID_DESKSETTING, false, ID_DESKTOPBAR_SETTINGS);

    AddButton(ResString(IDS_PRINTERS),          ICID_PRINTER, false, IDC_PRINTERS);

    if (!g_Globals._SHRestricted || !SHRestricted(REST_NOCONTROLPANEL))
        AddButton(ResString(IDS_CONTROL_PANEL), ICID_CONTROLPAN, false, IDC_CONTROL_PANEL);
}

void BrowseMenu::AddEntries()
{
    super::AddEntries();

    //if (!g_Globals._SHRestricted || !SHRestricted(REST_NONETHOOD))  // or REST_NOENTIRENETWORK ?
    if (!JCFG2_DEF("JS_STARTMENU", "nobrowse_network", true).ToBool())
#if defined(ROSSHELL) || defined(__REACTOS__)   // __REACTOS__ to be removed when printer/network will be implemented
        AddButton(ResString(IDS_NETWORK),       ICID_NETWORK, false, IDC_NETWORK);
#else
        AddButton(ResString(IDS_NETWORK),       ICID_NETWORK, true, IDC_NETWORK);
#endif

    AddButton(ResString(IDS_DRIVES),            ICID_FOLDER, true, IDC_DRIVES);
}

void SearchMenu::AddEntries()
{
    super::AddEntries();

    AddButton(ResString(IDS_SEARCH_FILES),      ICID_SEARCH_DOC, false, IDC_SEARCH_FILES);

    if (!g_Globals._SHRestricted || !SHRestricted(REST_HASFINDCOMPUTERS))
        AddButton(ResString(IDS_SEARCH_COMPUTER), ICID_COMPUTER, false, IDC_SEARCH_COMPUTER);
}


void RecentStartMenu::AddEntries()
{
    for (StartMenuShellDirs::iterator it = _dirs.begin(); it != _dirs.end(); ++it) {
        StartMenuDirectory &smd = *it;
        ShellDirectory &dir = smd._dir;

        if (!dir._scanned) {
            WaitCursor wait;

#ifdef _LAZY_ICONEXTRACT
            dir.smart_scan(SORT_NAME, SCAN_DONT_EXTRACT_ICONS);
#else
            dir.smart_scan(SORT_NAME);
#endif
        }

        dir.sort_directory(SORT_DATE);
        AddShellEntries(dir, RECENT_DOCS_COUNT, smd._ignore);   ///@todo read max. count of entries from registry
    }
}

