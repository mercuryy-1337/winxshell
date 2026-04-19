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
// Explorer and Desktop clone
//
// taskbar.h
//
// Martin Fuchs, 16.08.2003
//


//#include "shellhook.h"


#define CLASSNAME_TASKBAR       TEXT("MSTaskSwWClass")
#define TITLE_TASKBAR           TEXT("Running Applications")

#define IDC_FIRST_APP   0x2000

//#define TASKBAR_AT_TOP

#define TASKBUTTONWIDTH_MIN     DPI_SX(38)
#define TASKBUTTONWIDTH_MAX     DPI_SX(160)


#define IDW_TASKTOOLBAR 100


#define PM_GET_LAST_ACTIVE  (WM_APP+0x1D)
#define PM_QUERY_GROUP_STATE (WM_APP+0x1E)


struct TaskbarGroupStateQuery {
    LPCTSTR _app_key;
    HWND    _primary_hwnd;
    int     _window_count;
    BOOL    _active;
    HWND   *_windows;
    int     _window_capacity;
};


/// internal task bar button management entry
struct TaskBarEntry {
    TaskBarEntry();

    int     _id;    // ID for WM_COMMAND
    HBITMAP _hbmp;
    int     _bmp_idx;
    int     _used;
    int     _btn_idx;
    String  _title;
    BYTE    _fsState;
    float   _hover_progress;
    float   _active_progress;
    DWORD   _pid;
    int     _window_group_count;
    HWND    _primary_hwnd;
    String  _app_key;
    bool    _pinned;
    vector<HWND> _windows;
};

/// map for managing the task bar buttons, mapped by application window handle
struct TaskBarMap : public map<String, TaskBarEntry> {
    ~TaskBarMap();

    iterator find_id(int id);
};


/// Taskbar window
struct TaskBar : public Window {
    typedef Window super;

    TaskBar(HWND hwnd);
    ~TaskBar();

    static HWND Create(HWND hwndParent);

protected:
    WindowHandle _htoolbar;
    TaskBarMap  _map;
    int         _next_id;
    WindowHandle _last_foreground_wnd;
    int         _last_btn_width;
    MINIMIZEDMETRICS _mmMetrics_org;
    bool        _thumbnail;
    static RECT _icon_area;
    bool        _no_task_title;
    bool        _task_close_button;
    bool        _rounded_highlight;
    bool        _centered_layout;
    bool        _animate_highlights;
    bool        _animation_timer_running;
    int         _preferred_btn_width;
    set<String> _pinned_app_keys;
    const UINT WM_SHELLHOOK;

    void InitTaskbarStyle();
    LRESULT Init(LPCREATESTRUCT pcs);
    LRESULT WndProc(UINT nmsg, WPARAM wparam, LPARAM lparam);
    int     Command(int id, int code);
    int     Notify(int id, NMHDR *pnmh);

    void    ActivateApp(TaskBarMap::iterator it, bool can_minimize = true, bool can_restore = true);
    void    ShowAppSystemMenu(TaskBarMap::iterator it);

    static BOOL CALLBACK EnumWndProc(HWND hwnd, LPARAM lparam);

    void    ApplyBackgroundStyle();
    void    Refresh();
    void    ResizeButtons();
    int     GetPreferredWidth() const;
    bool    HasRunningButtons() const;
    bool    IsAnimationRequired() const;
    bool    AdvanceAnimations();
    void    RefreshAnimationTimer(bool invalidate = true);
    void    SyncAnimationState(bool snap_to_target);
};
