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
// desktopbar.cpp
//
// Martin Fuchs, 22.08.2003
//


#include <precomp.h>

#include <gdiplus.h>
#include <urlmon.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "urlmon.lib")

#include "../resource.h"

// #include "../DUI/Helper.h"
#include "desktopbar.h"
#include "taskbar.h"
#include "startmenu.h"
#include "traynotify.h"
#include "quicklaunch.h"
#include "../utility/taskbar_draw.h"

#include "../dialogs/settings.h"
#include "../customization/startbutton.h"

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


DesktopBar::DesktopBar(HWND hwnd)
    :  super(hwnd),
    _start_button_width(0),
    _start_button_gap(0),
    _centered_layout(false),
    _traySndVolIcon(hwnd, ID_TRAY_VOLUME),
    _trayNetworkIcon(hwnd, ID_TRAY_NETWORK)
{
    SetWindowIcon(hwnd, IDI_WINXSHELL);

    SystemParametersInfo(SPI_GETWORKAREA, 0, &_work_area_org, 0);
}

DesktopBar::~DesktopBar()
{
    if (_hbmQuickLaunchBack) DeleteObject(_hbmQuickLaunchBack);
    RegisterHotkeys(TRUE);
    // restore work area to the previous size
    SystemParametersInfo(SPI_SETWORKAREA, 0, &_work_area_org, 0);
    PostMessage(HWND_BROADCAST, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0);

    // exit application after destroying desktop window
    PostQuitMessage(0);
}

#define DESKTOPBAR_TOP GetSystemMetrics(SM_CYSCREEN) - DESKTOPBARBAR_HEIGHT
HWND DesktopBar::Create()
{
    static BtnWindowClass wcDesktopBar(CLASSNAME_EXPLORERBAR);
    wcDesktopBar.hbrBackground = TASKBAR_BRUSH();

    RECT rect;

    rect.left = 0;
#ifdef TASKBAR_AT_TOP
    rect.top = 0;
#else
    rect.top = DESKTOPBAR_TOP;
#endif
    rect.right = GetSystemMetrics(SM_CXSCREEN);
    return Window::Create(WINDOW_CREATOR(DesktopBar), WS_EX_PALETTEWINDOW,
                          wcDesktopBar, TITLE_EXPLORERBAR,
                          WS_POPUP | WS_CLIPCHILDREN | WS_VISIBLE,
                          rect.left, rect.top, rect.right - rect.left, DESKTOPBARBAR_HEIGHT, 0);
}

static HBITMAP
CreateSolidBitmap(HDC hdc, int width, int height, COLORREF cref)
{
    // Create compatible memory DC and bitmap, and a solid brush
    MemCanvas canvas;
    HBITMAP hbmp = CreateCompatibleBitmap(hdc, width, height);
    HBRUSH hbrushFill = CreateSolidBrush(cref);
    BitmapSelection sel(canvas, hbmp);

    // Fill the whole area with solid color
    RECT rect = { 0, 0, width, height };
    FillRect(canvas, &rect, hbrushFill);

    // Delete brush
    DeleteObject(hbrushFill);

    // Set preferred dimensions
    SetBitmapDimensionEx(hbmp, width, height, NULL);

    return hbmp;
}

static UINT GetStartButtonPngFrameCount(Gdiplus::Bitmap &source)
{
    UINT width = source.GetWidth();
    UINT height = source.GetHeight();

    if (width == 0 || height == 0)
        return 0;

    if (height > width && (height % width) == 0)
        return height / width;

    if (width > height && (width % height) == 0)
        return width / height;

    return 1;
}

static int GetStartButtonIconSize()
{
    int icon_size = TASKBAR_ICON_SIZE + DPI_SX(2);
    int max_icon_size = DESKTOPBARBAR_HEIGHT - DPI_SY(10);

    if (icon_size > max_icon_size)
        icon_size = max_icon_size;

    if (icon_size < TASKBAR_ICON_SIZE)
        icon_size = TASKBAR_ICON_SIZE;

    return icon_size;
}

static Gdiplus::Rect GetStartButtonPngContentRect(Gdiplus::Bitmap &source, const Gdiplus::Rect &frame_rect)
{
    int min_x = frame_rect.X + frame_rect.Width;
    int min_y = frame_rect.Y + frame_rect.Height;
    int max_x = frame_rect.X - 1;
    int max_y = frame_rect.Y - 1;

    for (int y = frame_rect.Y; y < frame_rect.Y + frame_rect.Height; ++y) {
        for (int x = frame_rect.X; x < frame_rect.X + frame_rect.Width; ++x) {
            Gdiplus::Color pixel;
            if (source.GetPixel(x, y, &pixel) == Gdiplus::Ok && pixel.GetAlpha() > 8) {
                if (x < min_x)
                    min_x = x;
                if (y < min_y)
                    min_y = y;
                if (x > max_x)
                    max_x = x;
                if (y > max_y)
                    max_y = y;
            }
        }
    }

    if (max_x < min_x || max_y < min_y)
        return frame_rect;

    if (min_x > frame_rect.X)
        --min_x;
    if (min_y > frame_rect.Y)
        --min_y;
    if (max_x + 1 < frame_rect.X + frame_rect.Width)
        ++max_x;
    if (max_y + 1 < frame_rect.Y + frame_rect.Height)
        ++max_y;

    return Gdiplus::Rect(min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
}

static String GetExecutableDirectory();

static bool EnsureStartButtonPngInExecutableDirectory(const String &exe_dir)
{
    static bool attempted_download = false;

    if (exe_dir.empty())
        return false;

    String png_path = exe_dir + TEXT("\\win11.png");
    if (PathFileExists(png_path.c_str()))
        return true;

    if (attempted_download)
        return false;

    attempted_download = true;

    HRESULT hr = URLDownloadToFile(NULL,
        TEXT("https://savegfn.geforcenowspecs.cloud/win11.png"),
        png_path.c_str(),
        0,
        NULL);
    if (FAILED(hr)) {
        DeleteFile(png_path.c_str());
        return false;
    }

    return PathFileExists(png_path.c_str()) != FALSE;
}

static String ResolveStartButtonPngPath(const TCHAR *wkPath)
{
    String exe_dir = GetExecutableDirectory();
    String candidate;

    if (!exe_dir.empty()) {
        candidate = exe_dir + TEXT("\\win11.png");
        if (PathFileExists(candidate.c_str()) || EnsureStartButtonPngInExecutableDirectory(exe_dir))
            return candidate;
    }

    candidate = FmtString(TEXT("%s\\taskbar\\icons\\win11.png"), wkPath);
    if (PathFileExists(candidate.c_str()))
        return candidate;

    if (!exe_dir.empty()) {
        candidate = exe_dir + TEXT("\\W11SE Original.png");
        if (PathFileExists(candidate.c_str()))
            return candidate;
    }

    candidate = FmtString(TEXT("%s\\taskbar\\icons\\W11SE Original.png"), wkPath);
    if (PathFileExists(candidate.c_str()))
        return candidate;

    return String();
}

static HICON LoadStartButtonPngIcon(LPCTSTR path, int icon_size, UINT frame_index)
{
    if (!path || !*path)
        return NULL;

    Gdiplus::Bitmap source(path);
    if (source.GetLastStatus() != Gdiplus::Ok)
        return NULL;

    UINT frame_count = GetStartButtonPngFrameCount(source);
    if (frame_count == 0)
        return NULL;

    if (frame_index >= frame_count)
        frame_index = frame_count - 1;

    bool vertical_strip = source.GetHeight() > source.GetWidth() && (source.GetHeight() % source.GetWidth()) == 0;
    UINT frame_size = vertical_strip ? source.GetWidth() : source.GetHeight();
    Gdiplus::Rect src_rect(
        vertical_strip ? 0 : (INT)(frame_index * frame_size),
        vertical_strip ? (INT)(frame_index * frame_size) : 0,
        frame_size,
        frame_size);
    src_rect = GetStartButtonPngContentRect(source, src_rect);

    Gdiplus::Bitmap scaled(icon_size, icon_size, PixelFormat32bppARGB);
    if (scaled.GetLastStatus() != Gdiplus::Ok)
        return NULL;

    {
        Gdiplus::Graphics graphics(&scaled);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
        graphics.DrawImage(&source,
            Gdiplus::Rect(0, 0, icon_size, icon_size),
            src_rect.X,
            src_rect.Y,
            src_rect.Width,
            src_rect.Height,
            Gdiplus::UnitPixel);
    }

    HICON hIcon = NULL;
    if (scaled.GetHICON(&hIcon) != Gdiplus::Ok)
        return NULL;

    return hIcon;
}

static String GetExecutableDirectory()
{
    TCHAR module_path[MAX_PATH] = { 0 };
    if (!GetModuleFileName(NULL, module_path, COUNTOF(module_path)) || !module_path[0])
        return String();

    PathRemoveFileSpec(module_path);
    return String(module_path);
}

LRESULT DesktopBar::Init(LPCREATESTRUCT pcs)
{
    if (super::Init(pcs))
        return 1;

    // create start button
    string_t start_str(JCFG2("JS_STARTMENU", "text").ToString());
    WindowCanvas canvas(_hwnd);
    FontSelection font(canvas, g_Globals._hDefaultFont);
    RECT rect = {0, 0};
    DrawText(canvas, start_str.c_str(), -1, &rect, DT_SINGLELINE | DT_CALCRECT);

    _deskbar_pos_y = DESKTOPBAR_TOP;
    _centered_layout = taskbar_draw::IsCenteredEnabled() && JCFG_TB(2, "userebar").ToBool() == FALSE;

    int start_icon_size = GetStartButtonIconSize();
    int start_btn_width = DESKTOPBARBAR_HEIGHT + 8; //DPI_SX((TASKBAR_ICON_SIZE + rect.right + (TASKBAR_ICON_SIZE / 4)));
    if (_centered_layout) {
        start_btn_width = taskbar_draw::GetButtonSlotWidth();
    }

    string_t start_icon = JCFG2_DEF("JS_STARTMENU", "start_icon", TEXT("custom")).ToString();
    int start_btn_padding = JCFG2_DEF("JS_STARTMENU", "start_padding", 0).ToInt();
    Value configured_start_width = JCFG2("JS_STARTMENU", "start_width");
    if (configured_start_width.GetType() == IntVal) {
        _start_button_width = _centered_layout ? DPI_SX(configured_start_width.ToInt()) : configured_start_width.ToInt();
    } else {
        _start_button_width = start_btn_width;
    }
    _start_button_gap = DPI_SX(start_btn_padding) + 1;
    _taskbar_pos = _start_button_width + _start_button_gap;

    {
        string_t def_value = TEXT("");
        string_t start_command = TEXT("");

        memset(_startAction, 0, sizeof(_startAction));
        if (start_icon.compare(TEXT("empty")) == 0) {
            def_value = TEXT("none");
        }
        start_command = JCFG2_DEF("JS_STARTMENU", "start_command", def_value).ToString();
        if (start_command == TEXT("")) {
            if (def_value == TEXT("none")) {
                strcpy(_startAction, "none");
            }
        } else if (start_command.compare(TEXT("none")) == 0) {
            strcpy(_startAction, "none");
        } else {
            strcpy(_startAction, (w2s(start_command)).c_str());
        }
    }
    // create "Start" button
    static WNDCLASS wc;
    GetClassInfo(NULL, TEXT("BUTTON"), &wc);
    wc.lpszClassName = TEXT("Start");
    wc.hInstance = NULL;
    RegisterClass(&wc);
    HWND hwndStart = SWButton(_hwnd, start_str.c_str(), 0, 0, _start_button_width, DESKTOPBARBAR_HEIGHT, IDC_START, WS_VISIBLE | WS_CHILD | BS_OWNERDRAW);
    SetWindowFont(hwndStart, g_Globals._hDefaultFont, FALSE);

    UINT idStartIcon = IDI_STARTMENU_B;
    string_t sThemeStyle = TASKBAR_THEMESTYLE();
    if (start_icon.compare(TEXT("empty")) == 0) {
        idStartIcon = IDI_EMPTY;
    } else if (start_icon.compare(TEXT("custom")) == 0) {
        idStartIcon = IDI_SM_CUSTOM_1;
    } else if (start_icon.compare(TEXT("files")) == 0) {
        idStartIcon = 0;
    } else {
        if (sThemeStyle.compare(TEXT("dark")) == 0) {
            idStartIcon = IDI_STARTMENU_W;
        }
    }

    HICON starticon_normal = NULL, starticon_hover = NULL, starticon_pushed = NULL;
    const TCHAR *wkPath = JVAR("JVAR_MODULEPATH").ToString().c_str();
#ifdef _DEBUG
    wkPath = TEXT(".");
#endif // _DEBUG

    String start_png_path = ResolveStartButtonPngPath(wkPath);

    if (start_icon.compare(TEXT("empty")) != 0 && !start_png_path.empty() && PathFileExists(start_png_path.c_str())) {
        Gdiplus::Bitmap stack_image(start_png_path.c_str());
        UINT frame_count = stack_image.GetLastStatus() == Gdiplus::Ok ? GetStartButtonPngFrameCount(stack_image) : 0;

        if (frame_count >= 3) {
            starticon_normal = LoadStartButtonPngIcon(start_png_path.c_str(), start_icon_size, 1);
            starticon_hover = LoadStartButtonPngIcon(start_png_path.c_str(), start_icon_size, 2);
            starticon_pushed = LoadStartButtonPngIcon(start_png_path.c_str(), start_icon_size, 0);
        } else if (frame_count == 2) {
            starticon_normal = LoadStartButtonPngIcon(start_png_path.c_str(), start_icon_size, 0);
            starticon_hover = LoadStartButtonPngIcon(start_png_path.c_str(), start_icon_size, 1);
            starticon_pushed = LoadStartButtonPngIcon(start_png_path.c_str(), start_icon_size, 1);
        } else if (frame_count == 1) {
            starticon_normal = LoadStartButtonPngIcon(start_png_path.c_str(), start_icon_size, 0);
            if (starticon_normal) {
                starticon_hover = CopyIcon(starticon_normal);
                starticon_pushed = CopyIcon(starticon_normal);
            }
        }
    }

    if (!starticon_normal && idStartIcon == 0) {
        String sPath = FmtString(TEXT("%s\\Resources\\%s\\Start_Normal.ico"), wkPath, sThemeStyle.c_str());
        starticon_normal = (HICON)LoadImage(NULL, sPath.c_str(), IMAGE_ICON, start_icon_size, start_icon_size,
            LR_DEFAULTCOLOR | LR_CREATEDIBSECTION | LR_LOADFROMFILE);

        sPath = FmtString(TEXT("%s\\Resources\\%s\\Start_Pushed.ico"), wkPath, sThemeStyle.c_str());
        starticon_pushed = (HICON)LoadImage(NULL, sPath.c_str(), IMAGE_ICON, start_icon_size, start_icon_size,
            LR_DEFAULTCOLOR | LR_CREATEDIBSECTION | LR_LOADFROMFILE);
    }

    COLORREF clrSWButtonPushed = JValueToColor(JCFG2_DEF("JS_STARTMENU", "start_pushed_bkcolor", (int)RGB(51, 53, 55)));
    HBRUSH hbrSWButtonPushed = CreateSolidBrush(clrSWButtonPushed);
    if (starticon_normal) {
        if (!starticon_hover)
            starticon_hover = CopyIcon(starticon_normal);
        if (!starticon_pushed)
            starticon_pushed = CopyIcon(starticon_hover ? starticon_hover : starticon_normal);
        new StartButton(hwndStart, starticon_normal, starticon_hover, starticon_pushed, TASKBAR_BRUSH(), hbrSWButtonPushed, TASKBAR_TEXTCOLOR(), true);
    } else if (idStartIcon != 0) {
        new StartButton(hwndStart, idStartIcon, TASKBAR_BRUSH(), hbrSWButtonPushed, TASKBAR_TEXTCOLOR(), true);
    } else {
        new StartButton(hwndStart, starticon_normal, starticon_pushed, TASKBAR_BRUSH(), hbrSWButtonPushed, TASKBAR_TEXTCOLOR(), true);
    }

    /* Save the handle to the window, needed for push-state handling */
    _hwndStartButton = hwndStart;

    // disable double clicks
    SetClassLongPtr(hwndStart, GCL_STYLE, GetClassLongPtr(hwndStart, GCL_STYLE) & ~CS_DBLCLKS);

    // create task bar
    _hwndTaskBar = TaskBar::Create(_hwnd);

    if (!g_Globals._SHRestricted || !SHRestricted(REST_NOTRAYITEMSDISPLAY))
        // create tray notification area
        _hwndNotify = NotifyArea::Create(_hwnd);


    // notify all top level windows about the successfully created desktop bar
    SendNotifyMessage(HWND_BROADCAST, WM_TASKBARCREATED, 0, 0);

    //LoadSSO(); /* load in main() by SSOThread */

    _hwndQuickLaunch = 0;
    _iQuickLaunchPadding = 0;

    SetTimer(_hwnd, 0, 1000, NULL);

    if (_hwndQuickLaunch && JCFG_TB(2, "userebar").ToBool() == TRUE) {
        JCFG_QL_SET(2, "maxiconsinrow") = 0;
        // create rebar window to manage task and quick launch bar
        _hwndrebar = CreateWindowEx(WS_EX_TOOLWINDOW, REBARCLASSNAME, NULL,
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
            RBS_VARHEIGHT | RBS_AUTOSIZE | RBS_DBLCLKTOGGLE | RBS_REGISTERDROP |
            CCS_NODIVIDER | CCS_NOPARENTALIGN | CCS_TOP | TBSTYLE_LIST | TBSTYLE_TOOLTIPS | TBSTYLE_WRAPABLE,
            _start_button_width + 1, 1, 0, 0, _hwnd, 0, g_Globals._hInstance, 0);

        REBARBANDINFO rbBand;
        rbBand.cbSize = sizeof(REBARBANDINFO);
        rbBand.fMask = RBBIM_TEXT | RBBS_FIXEDBMP | RBBIM_STYLE | RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_SIZE | RBBIM_ID | RBBIM_IDEALSIZE;
        {
            rbBand.fMask |= RBBIM_COLORS | RBBIM_BACKGROUND;
            rbBand.clrBack = 0;
            HDC hdc = GetDC(_hwnd);
            _hbmQuickLaunchBack = CreateSolidBitmap(hdc, 768, 16, TASKBAR_BKCOLOR());
            ReleaseDC(_hwnd, hdc);
            rbBand.hbmBack = _hbmQuickLaunchBack;
            //rbBand.hbmBack = LoadBitmap(g_Globals._hInstance, MAKEINTRESOURCE(IDB_TB_SH_DEF_16));
        }
        rbBand.cyChild = REBARBAND_HEIGHT - 5;
        rbBand.cyMaxChild = (ULONG)-1;
        rbBand.cyMinChild = REBARBAND_HEIGHT;
        rbBand.cyIntegral = REBARBAND_HEIGHT + 3;   //@@ OK?
        rbBand.fStyle = RBBS_VARIABLEHEIGHT | RBBS_FIXEDBMP | RBBS_HIDETITLE;
        if (JCFG_TB(2, "rebarlock").ToBool() == TRUE) {
            rbBand.fStyle |= RBBS_NOGRIPPER;
        } else {
            rbBand.fStyle |= RBBS_GRIPPERALWAYS;
        }
        TCHAR QuickLaunchBand[] = TEXT("Quicklaunch");
        rbBand.lpText = QuickLaunchBand;
        rbBand.cch = sizeof(QuickLaunchBand);
        rbBand.hwndChild = _hwndQuickLaunch;
        rbBand.cx = 100;
        rbBand.cxMinChild = 100;
        //rbBand.hbmBack = LoadBitmap(g_Globals._hInstance, MAKEINTRESOURCE(IDB_TB_SH_DEF_16));
        rbBand.wID = IDW_QUICKLAUNCHBAR;
        SendMessage(_hwndrebar, RB_INSERTBAND, (WPARAM)-1, (LPARAM)&rbBand);

        TCHAR TaskbarBand[] = TEXT("Taskbar");
        rbBand.lpText = TaskbarBand;
        rbBand.cch = sizeof(TaskbarBand);
        rbBand.hwndChild = _hwndTaskBar;
        rbBand.cx = 200;    //pcs->cx-_taskbar_pos-quicklaunch_width-(notifyarea_width+1);
        rbBand.cxMinChild = 50;
        rbBand.wID = IDW_TASKTOOLBAR;
        SendMessage(_hwndrebar, RB_INSERTBAND, (WPARAM)-1, (LPARAM)&rbBand);
    }

    RegisterHotkeys();

    // prepare Startmenu, but hide it for now
    _startMenuRoot = GET_WINDOW(StartMenuRoot, StartMenuRoot::Create(_hwndStartButton, STARTMENUROOT_ICON_SIZE));
    _startMenuRoot->_hwndStartButton = _hwndStartButton;

    return 0;
}


StartButton::StartButton(HWND hwnd, UINT nid, HBRUSH hbrush, HBRUSH hbrush2,
    COLORREF textcolor, bool flat)
    : PictureButton2(hwnd, SizeIcon(nid, GetStartButtonIconSize()),
        SizeIcon(nid + 1, GetStartButtonIconSize()), hbrush, hbrush2, textcolor, flat)
{
}

StartButton::StartButton(HWND hwnd, HICON hIcon, HICON hIcon2, HBRUSH hbrush, HBRUSH hbrush2,
    COLORREF textcolor, bool flat)
    : PictureButton2(hwnd, hIcon, hIcon2, hbrush, hbrush2, textcolor, flat)
{
}

StartButton::StartButton(HWND hwnd, HICON hIcon, HICON hIconHover, HICON hIconPressed, HBRUSH hbrush, HBRUSH hbrush2,
    COLORREF textcolor, bool flat)
    : PictureButton2(hwnd, hIcon, hIconHover, hIconPressed, hbrush, hbrush2, textcolor, flat)
{
}

extern int VK_WIN_HOOK();

LRESULT StartButton::WndProc(UINT nmsg, WPARAM wparam, LPARAM lparam)
{
    switch (nmsg) {
    // one click activation: handle button-down message, don't wait for button-up
    case WM_LBUTTONDOWN:
        if (!Button_GetState(_hwnd)) {
            Button_SetState(_hwnd, TRUE);
            SetCapture(_hwnd);
            if (VK_WIN_HOOK() == 0) {
                SendMessage(GetParent(_hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(_hwnd), 0), 0);
            }
        }
        Button_SetState(_hwnd, FALSE);
        break;

    // re-target mouse move messages while moving the mouse cursor through the start menu
    case WM_MOUSEMOVE:
        if (!_hovered) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, _hwnd, 0 };
            TrackMouseEvent(&tme);
            _hovered = true;
            InvalidateRect(_hwnd, NULL, FALSE);
        }
        if (GetCapture() == _hwnd) {
            POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};

            ClientToScreen(_hwnd, &pt);
            HWND hwnd = WindowFromPoint(pt);

            if (hwnd && hwnd != _hwnd) {
                ScreenToClient(hwnd, &pt);
                SendMessage(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(pt.x, pt.y));
            }
        }
        break;

    case WM_MOUSELEAVE:
        if (_hovered) {
            _hovered = false;
            InvalidateRect(_hwnd, NULL, FALSE);
        }
        break;

    case WM_LBUTTONUP:
        if (GetCapture() == _hwnd) {
            ReleaseCapture();

            POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};

            ClientToScreen(_hwnd, &pt);
            HWND hwnd = WindowFromPoint(pt);

            if (hwnd && hwnd != _hwnd) {
                ScreenToClient(hwnd, &pt);
                PostMessage(hwnd, WM_LBUTTONDOWN, 0, MAKELPARAM(pt.x, pt.y));
                PostMessage(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(pt.x, pt.y));
            }
        }
        break;

    case WM_CANCELMODE:
        ReleaseCapture();
        break;

    default:
        return super::WndProc(nmsg, wparam, lparam);
    }

    return 0;
}

#define AUTOREGISTERHOTKEY(unreg, hwnd, id,fsModifiers, vk)\
  if (!unreg) RegisterHotKey(hwnd, id,fsModifiers, vk);\
  else UnregisterHotKey(hwnd, id);

void DesktopBar::RegisterHotkeys(BOOL unreg)
{
    // register hotkey CONTROL+ESC opening STARTMENU
    AUTOREGISTERHOTKEY(unreg, _hwnd, IDHK_STARTMENU, MOD_CONTROL, VK_ESCAPE);
    AUTOREGISTERHOTKEY(unreg, _hwnd, IDHK_DESKTOP, MOD_WIN, 'D');
}

void DesktopBar::ProcessHotKey(int id_hotkey)
{
    switch (id_hotkey) {
    case IDHK_DESKTOP: {
        if (_startMenuRoot && _startMenuRoot->IsStartMenuVisible()) {
            ShowOrHideStartMenu(_startAction);
        } else {
            g_Globals._desktop.ToggleMinimize();
        }
        break;
    }

    case IDHK_STARTMENU:
        ShowOrHideStartMenu(_startAction);
        break;
    }
}

//is not PECMD TEXT window
#define DEF_IGNORE_WINDOWS TEXT(";[PECMD;#32770]")
//is not BrightnessMaskLayerWindow, InstallShield, PangolinScreenBrightness ...
#define DEF_IGNORE_WINDOW_CLASSES TEXT(";wxsBrightnessMaskLayerWindow;InstallShield_Win;FadeLensScrClass")

static BOOL IsIgnoredWindow(HWND hwnd)
{
    String ignore_window_titles = JCFG2_DEF("JS_TASKBAR", "IGNORE_WINDOW_TITLES", TEXT("")).ToString();
    String ignore_window_classes = JCFG2_DEF("JS_TASKBAR", "IGNORE_WINDOW_CLASSES", TEXT("")).ToString();
    String ignore_windows = JCFG2_DEF("JS_TASKBAR", "IGNORE_WINDOWS", TEXT("")).ToString();
    ignore_window_classes.append(DEF_IGNORE_WINDOW_CLASSES);
    ignore_windows.append(DEF_IGNORE_WINDOWS);
    TCHAR title_buffer[BUFFER_LEN] = { 0 };
    TCHAR class_buffer[BUFFER_LEN] = { 0 };
    String strWindow;
    if (!GetWindowText(hwnd, title_buffer, BUFFER_LEN))
        title_buffer[0] = '\0';
    if (!ignore_window_titles.empty() && ignore_window_titles.find(title_buffer) != String::npos) {
        return TRUE;
    }
    if (!GetClassName(hwnd, class_buffer, BUFFER_LEN))
        class_buffer[0] = '\0';
    if (!ignore_window_classes.empty() && ignore_window_classes.find(class_buffer) != String::npos) {
        return TRUE;
    }
    strWindow.printf(TEXT("[%s;%s]"), title_buffer, class_buffer);
    LOG(strWindow);
    if (!ignore_windows.empty() && ignore_windows.find(strWindow) != String::npos) {
        return TRUE;
    }
    return FALSE;
}

static void HideForFullScreenWindow(HWND hwnd)
{
    static bool hiddenState = false;
    bool hasFullScreenWindow = false;

    HMONITOR hwndMonitor, taskbarMonitor;
    MONITORINFO hwndMonitorInfo;
    hwndMonitorInfo.cbSize = sizeof(MONITORINFO);
    RECT wndRect;
    HWND hTopWindow = GetForegroundWindow();
    if (!hTopWindow) return;
    taskbarMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    hwndMonitor = MonitorFromWindow(hTopWindow, MONITOR_DEFAULTTONEAREST);
    GetMonitorInfo(hwndMonitor, &hwndMonitorInfo);

    GetWindowRect(hTopWindow, &wndRect);

    // A full screen window is on the same monitor as the taskbar...
    if ((hwndMonitor == taskbarMonitor) &&
        // and hwnd is not an taskbar...
        (hwnd != hTopWindow) &&
        // and hwnd is not the Explorer desktop...
        (g_Globals._hwndDesktop != hTopWindow) &&
        // and hwnd is visible...
        IsWindowVisible(hTopWindow) &&
        // and hwnd size is greater than the resolution of the monitor which the
        // applet is on, hwnd is full screen.
        (wndRect.left <= hwndMonitorInfo.rcMonitor.left) &&
        (wndRect.top <= hwndMonitorInfo.rcMonitor.top) &&
        (wndRect.right >= hwndMonitorInfo.rcMonitor.right) &&
        (wndRect.bottom >= hwndMonitorInfo.rcMonitor.bottom) &&
        // and is not ignore window.
        !IsIgnoredWindow(hTopWindow)) {
        hasFullScreenWindow = true;
    }

    if (hasFullScreenWindow) {
        if (!hiddenState) {
            LOG(FmtString(TEXT("EXSTYLE:0x%x"), GetWindowExStyle(hTopWindow)));
            SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER |
                SWP_NOACTIVATE | SWP_HIDEWINDOW);
            hiddenState = true;
        }
    } else {
        if (hiddenState) {
            LOG(FmtString(TEXT("EXSTYLE:0x%x"), GetWindowExStyle(hTopWindow)));
            SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER |
                SWP_NOACTIVATE | SWP_SHOWWINDOW);
            hiddenState = false;
        }
    }
}

static void OnTraySndVol(HWND hwnd, UINT id)
{
    if (hwnd) KillTimer(hwnd, id); // finish one-click timer
    //launch volume control in rightbottom(x:4096, y:4096)
    launch_file(hwnd, TEXT("SndVol.exe"), SW_SHOWNORMAL, TEXT("-m 268439552"));
}

static void OnTrayNetwork(HWND hwnd, UINT id)
{
    if (hwnd) KillTimer(hwnd, id);
    LPCTSTR selfexe = JVAR("JVAR_MODULEFILENAME").ToString().c_str();
    launch_file(hwnd, selfexe, SW_SHOWNORMAL, _T("-ui -jcfg UI_WIFI\\main.jcfg"));
}

static void NotifySetWorkArea(HWND hwnd) {
    WindowRect rect(hwnd);
    RECT work_area = { 0, 0, GetSystemMetrics(SM_CXSCREEN), rect.top };
    RECT rt = { 0 };
    SystemParametersInfo(SPI_GETWORKAREA, 0, &rt, 0);
    // reset the WORKAREA
    _log_(FmtString(TEXT("WORKAREA CHANGED NOTIFYTION: %d, %d, %d, %d"), rt.left, rt.top, rt.right, rt.bottom));
    if (memcmp(&rt, &work_area, sizeof(RECT)) != 0) {
        SystemParametersInfo(SPI_SETWORKAREA, 0, &work_area, 0);
        PostMessage(HWND_BROADCAST, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0);
        SendMessage(g_Globals._hwndShellView, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0);
    }
}

LRESULT DesktopBar::WndProc(UINT nmsg, WPARAM wparam, LPARAM lparam)
{
    if (g_Globals._isDebug) {
        if (nmsg != WM_SETCURSOR && nmsg != WM_TIMER && nmsg != PM_RESIZE_CHILDREN && nmsg != WM_ENTERIDLE &&
            nmsg != WM_COPYDATA && nmsg != PM_RESIZE_CHILDREN)
            LOG(FmtString(TEXT("NMSG - 0x%x"), nmsg));
    }
    switch (nmsg) {
    case WM_NCHITTEST: {
#ifndef TASKBAR_AT_TOP
        POINTS pts = MAKEPOINTS(lparam);
        RECT rc;
        GetWindowRect(_hwnd, &rc);
        if (pts.y <= rc.top + 8)
            return HTTOP;
#endif
        LRESULT res = super::WndProc(nmsg, wparam, lparam);

        if (res >= HTSIZEFIRST && res <= HTSIZELAST) {
#ifdef TASKBAR_AT_TOP
            if (res == HTBOTTOM)    // enable vertical resizing at the lower border
#else
            if (res == HTTOP)       // enable vertical resizing at the upper border
#endif
                return res;
            else
                return HTCLIENT;    // disable any other resizing
        }
        return res;
    }

    case WM_SYSCOMMAND:
        if ((wparam & 0xFFF0) == SC_SIZE) {
#ifdef TASKBAR_AT_TOP
            if (wparam == SC_SIZE + 6) // enable vertical resizing at the lower border
#else
            if (wparam == SC_SIZE + 3) // enable vertical resizing at the upper border
#endif
                goto def;
            else
                return 0;           // disable any other resizing
        } else if (wparam == SC_TASKLIST)
            ShowOrHideStartMenu(_startAction);
        goto def;

    case WM_SIZE:
        Resize(LOWORD(lparam), HIWORD(lparam));
        break;

    case WM_SIZING:
        ControlResize(wparam, lparam);
        break;

    case PM_RESIZE_CHILDREN: {
        ClientRect size(_hwnd);
        Resize(size.right, size.bottom);
        break;
    }

    case WM_CLOSE:
        ShowExitWindowsDialog(_hwnd);
        break;

    case WM_HOTKEY:
        ProcessHotKey((int)wparam);
        break;

    case WM_COPYDATA:
        return ProcessCopyData((COPYDATASTRUCT *)lparam);

    case WM_CONTEXTMENU: {
        POINTS p;
        p.x = LOWORD(lparam);
        p.y = HIWORD(lparam);
        PopupMenu menu(IDM_DESKTOPBAR);
        SetMenuDefaultItem(menu, 0, MF_BYPOSITION);
        if (GetKeyState(VK_SHIFT) < 0) {
            menu.Append(0, NULL, MF_SEPARATOR);
            menu.Append(ID_ABOUT_EXPLORER, ResString(IDS_ABOUT_EXPLORER));
            menu.Append(FCIDM_SHVIEWLAST - 1, ResString(IDS_TERMINATE));
        }
        menu.TrackPopupMenu(_hwnd, p);
        break;
    }

    case PM_GET_LAST_ACTIVE:
        if (_hwndTaskBar)
            return SendMessage(_hwndTaskBar, nmsg, wparam, lparam);
        break;

    case PM_REFRESH_CONFIG:   ///@todo read desktop bar settings
        SendMessage(_hwndNotify, PM_REFRESH_CONFIG, 0, 0);
        break;

    case WM_TIMER:
        if (wparam == 0) {
            if (JCFG2_DEF("JS_TASKBAR", "hideforfullscreenwindow", true).ToBool() != FALSE) {
                HideForFullScreenWindow(_hwnd);
            }
        } else if (wparam == ID_TRAY_VOLUME) {
            OnTraySndVol(_hwnd, (UINT)wparam);
        } else if (wparam == ID_TRAY_NETWORK) {
            OnTrayNetwork(_hwnd, (UINT)wparam);
        }
        break;

    case PM_GET_NOTIFYAREA:
        return (LRESULT)(HWND)_hwndNotify;

    case WM_SYSCOLORCHANGE:
        /* Forward WM_SYSCOLORCHANGE to common controls */
        if (_hwndrebar) {
            SendMessage(_hwndrebar, WM_SYSCOLORCHANGE, 0, 0);
        }
        if (_hwndQuickLaunch) SendMessage(_hwndQuickLaunch, WM_SYSCOLORCHANGE, 0, 0);
        SendMessage(_hwndTaskBar, WM_SYSCOLORCHANGE, 0, 0);
        break;
    case WM_SETTINGCHANGE: {
        if (wparam == SPI_SETWORKAREA) {
            NotifySetWorkArea(_hwnd);
        }
        break;
    }
    case WM_DISPLAYCHANGE: {
        NotifySetWorkArea(_hwnd);
        //fallthough
    }
default: def:
        return super::WndProc(nmsg, wparam, lparam);
    }

    return 0;
}

int DesktopBar::Notify(int id, NMHDR *pnmh)
{
    if (_hwndQuickLaunch && pnmh->hwndFrom == _hwndQuickLaunch && pnmh->code == NM_CUSTOMDRAW && taskbar_draw::IsRoundedHighlightEnabled()) {
        LPNMTBCUSTOMDRAW lptbcd = (LPNMTBCUSTOMDRAW)pnmh;
        switch (lptbcd->nmcd.dwDrawStage) {
        case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT:
            return TBCDRF_NOBACKGROUND | TBCDRF_NOEDGES | TBCDRF_NOOFFSET |
                TBCDRF_NOETCHEDEFFECT | CDRF_NOTIFYPOSTPAINT | CDRF_USECDCOLORS;
        case CDDS_ITEMPOSTPAINT: {
            bool is_hot = (lptbcd->nmcd.uItemState & CDIS_HOT) == CDIS_HOT;
            bool is_selected = (lptbcd->nmcd.uItemState & CDIS_SELECTED) == CDIS_SELECTED;
            if (!is_hot && !is_selected)
                return CDRF_DODEFAULT;

            RECT highlight_rect = taskbar_draw::DeflateRectCopy(lptbcd->nmcd.rc, DPI_SX(2), DPI_SY(4));
            highlight_rect.bottom -= DPI_SY(4);
            if (highlight_rect.bottom <= highlight_rect.top)
                return CDRF_DODEFAULT;

            if (is_hot) {
                taskbar_draw::FillRoundedRect(
                    lptbcd->nmcd.hdc,
                    highlight_rect,
                    taskbar_draw::GetHighlightRadius(),
                    taskbar_draw::GetHoverColor(),
                    taskbar_draw::GetHoverAlpha());
            }
            if (is_selected) {
                taskbar_draw::FillRoundedRect(
                    lptbcd->nmcd.hdc,
                    highlight_rect,
                    taskbar_draw::GetHighlightRadius(),
                    taskbar_draw::GetHighlightColor(),
                    taskbar_draw::GetHighlightAlpha());
            }
            return CDRF_DODEFAULT;
        }
        }
    }

    if (pnmh->code == RBN_CHILDSIZE) {
        /* align the task bands to the top, so it's in row with the Start button */
        NMREBARCHILDSIZE *childSize = (NMREBARCHILDSIZE *)pnmh;

        if (childSize->wID == IDW_TASKTOOLBAR) {
            int cy = childSize->rcChild.top - childSize->rcBand.top;

            if (cy) {
                childSize->rcChild.bottom -= cy;
                childSize->rcChild.top -= cy;
            }
        }
    }

    return 0;
}

static int CommandHook(HWND hwnd, const TCHAR *act)
{
    String cmd = TEXT("");
    INT showflags = SW_SHOWNORMAL;
    String parameters = TEXT("");
    if (!hwnd) return 0;
    cmd = JCFG_CMD("JS_TASKBAR", act, "command", TEXT("")).ToString();
    if (cmd != TEXT("")) {
        showflags = JCFG_CMD("JS_TASKBAR", act, "showflags", showflags).ToInt();
        parameters = JCFG_CMD("JS_TASKBAR", act, "parameters", TEXT("")).ToString();
        launch_file(hwnd, cmd, showflags, parameters);
        return 1;
    }
    return 0;
}

void DesktopBar::Resize(int cx, int cy)
{
    ///@todo general children resizing algorithm
    int quicklaunch_width = 0;
    if (_hwndQuickLaunch) {
        quicklaunch_width = (int)SendMessage(_hwndQuickLaunch, PM_GET_WIDTH, 0, 0);
    }
    int taskbar_width = 0;
    if (_hwndTaskBar) {
        taskbar_width = (int)SendMessage(_hwndTaskBar, PM_GET_WIDTH, 0, 0);
    }
    int notifyarea_width = 0;
    if (_hwndNotify) {
        notifyarea_width = (int)SendMessage(_hwndNotify, PM_GET_WIDTH, 0, 0);
    }
    //_log_(FmtString("Resize - %d,%d\r\n", cx, cy));
    HDWP hdwp = BeginDeferWindowPos(4);

    if (_hwndrebar) {
        if (_hwndStartButton) {
            DeferWindowPos(hdwp, _hwndStartButton, 0, 0, 0, _start_button_width, cy, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        DeferWindowPos(hdwp, _hwndrebar, 0, _taskbar_pos, 1, cx - _taskbar_pos - (notifyarea_width + 1), cy - 2, SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        int quicklaunch_padding = 0;
        if (!taskbar_draw::IsModernTaskbarEnabled() || !taskbar_draw::IsCenteredEnabled())
            quicklaunch_padding = (quicklaunch_width > 0 && taskbar_width > 0) ? _iQuickLaunchPadding : 0;
        int start_gap = (_hwndQuickLaunch || taskbar_width > 0) ? _start_button_gap : 0;
        int group_width = _start_button_width + start_gap + quicklaunch_width + quicklaunch_padding + taskbar_width;
        int group_left = 0;
        int right_wall = cx - notifyarea_width;
        bool can_center = _centered_layout && group_width > 0;

        if (can_center) {
            group_left = (cx - group_width) / 2;
            if (group_left + group_width > right_wall)
                group_left = right_wall - group_width;
            if (group_left < 0)
                group_left = 0;
        }

        if (can_center) {
            int next_x = group_left;

            if (_hwndStartButton) {
                DeferWindowPos(hdwp, _hwndStartButton, 0, next_x, 0, _start_button_width, cy, SWP_NOZORDER | SWP_NOACTIVATE);
            }
            next_x += _start_button_width + start_gap;

            if (_hwndQuickLaunch) {
                DeferWindowPos(hdwp, _hwndQuickLaunch, 0, next_x, 0, quicklaunch_width, cy, SWP_NOZORDER | SWP_NOACTIVATE);
                next_x += quicklaunch_width + quicklaunch_padding;
            }

            if (_hwndTaskBar) {
                DeferWindowPos(hdwp, _hwndTaskBar, 0, next_x, 0, taskbar_width, cy, SWP_NOZORDER | SWP_NOACTIVATE);
            }
        } else {
            if (_hwndStartButton) {
                DeferWindowPos(hdwp, _hwndStartButton, 0, 0, 0, _start_button_width, cy, SWP_NOZORDER | SWP_NOACTIVATE);
            }

            if (quicklaunch_width > 0)
                quicklaunch_width += _iQuickLaunchPadding;
            if (_hwndQuickLaunch)
                DeferWindowPos(hdwp, _hwndQuickLaunch, 0, _taskbar_pos, 1, quicklaunch_width, cy - 2, SWP_NOZORDER | SWP_NOACTIVATE);

            bool modern_taskbar = taskbar_draw::IsModernTaskbarEnabled();
            int tb_y = modern_taskbar ? 0 : 1;
            int tb_h = modern_taskbar ? cy : cy - 2;
            if (_hwndTaskBar)
                DeferWindowPos(hdwp, _hwndTaskBar, 0, _taskbar_pos + quicklaunch_width, tb_y, cx - _taskbar_pos - quicklaunch_width - (notifyarea_width + 1), tb_h, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    if (_hwndNotify)
        DeferWindowPos(hdwp, _hwndNotify, 0, cx - notifyarea_width, 0, notifyarea_width, cy, SWP_NOZORDER | SWP_NOACTIVATE);

    EndDeferWindowPos(hdwp);

    WindowRect rect(_hwnd);
    RECT work_area = {0, 0, GetSystemMetrics(SM_CXSCREEN), rect.top};
    if (memcmp(&_work_area, &work_area, sizeof(RECT)) != 0) {
        SystemParametersInfo(SPI_SETWORKAREA, 0, &work_area, 0);    // don't use SPIF_SENDCHANGE because then we have to wait for any message being delivered
        PostMessage(HWND_BROADCAST, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0);
        SendMessage(g_Globals._hwndShellView, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0);
        _work_area = work_area;
        CommandHook(g_Globals._hwndDesktop, TEXT("onDisplayChanged"));
    }
}

/*

#include <Shldisp.h>

CoInitialize(NULL);
//Create an instance of the shell class
IShellDispatch *pShellDisp = NULL;
CoCreateInstance(CLSID_Shell, NULL, CLSCTX_SERVER, IID_IDispatch, (LPVOID *)&pShellDisp);
pShellDisp->FileRun();
pShellDisp->ToggleDesktop();
pShellDisp->Release();
=============================================
CShellDispatch::WindowsSecurity:1389h

CShellDispatch::UpgradeWindowsBottom:1A6h ,2
CShellDispatch::UpgradeWindowsTop:1A6h,3

CShellDispatch::RefreshMenu:0A220h
CShellDispatch::FindComputer:0A086h
CShellDispatch::FindFiles:0A085h

CShellDispatch::CascadeWindows:193h
CShellDispatch::TileVertically:194h
CShellDispatch::TileHorizontally:195h
CShellDispatch::Suspend:199h
CShellDispatch::SetTime:198h
CShellDispatch::EjectPC:19Ah
CShellDispatch::WindowSwitcher:19Bh
CShellDispatch::TrayProperties:19Dh
CShellDispatch::MinimizeAll: 19Fh
CShellDispatch::UndoMinimizeALL:1A0h
CShellDispatch::ShutdownWindows:1FAh
*/
#define IDC_FILERUN        0x191
#define IDC_TOGGLEDESKTOP  0x197

extern void send_wxs_protocol_url(PWSTR pszName);

int DesktopBar::Command(int id, int code)
{
    if (id == IDC_TOGGLEDESKTOP) id = ID_MINIMIZE_ALL;
    switch (id) {
    case IDC_FILERUN:
        _startMenuRoot->Command(IDC_LAUNCH, 0);
        break;
    case IDC_START: {
        ShowOrHideStartMenu(_startAction);
        break;
    }
    case ID_ABOUT_EXPLORER:
        explorer_about(g_Globals._hwndDesktop);
        break;

    case ID_DESKTOPBAR_SETTINGS: {
        if (!g_Globals._isWinPE && g_Globals._winvers[0] >= 10) {
            TCHAR sysPathBuff[MAX_PATH] = { 0 };
            GetWindowsDirectory(sysPathBuff, MAX_PATH);
            String dllPath = sysPathBuff;
#ifdef _WIN64
            dllPath.append(_T("\\System32\\ieframe.dll"));
#else
            dllPath.append(_T("\\SysNative\\ieframe.dll"));
#endif
            if (PathFileExists(dllPath)) {
                launch_file(g_Globals._hwndDesktop, TEXT("ms-settings:taskbar"));
            } else {
                send_wxs_protocol_url(L"ms-settings:taskbar");
            }
        } else {
            send_wxs_protocol_url(L"ms-settings:taskbar");
            //ExplorerPropertySheet(g_Globals._hwndDesktop);
        }
        break;
    }
    case ID_MINIMIZE_ALL:
        g_Globals._desktop.ToggleMinimize();
        break;

    case ID_EXPLORE: {
        explorer_open_frame(SW_SHOWNORMAL, NULL, EXPLORER_OPEN_QUICKLAUNCH);
        break;
    }
    case ID_TASKMGR:
        launch_file(_hwnd, TEXT("taskmgr.exe"), SW_SHOWNORMAL);
        break;
    case FCIDM_SHVIEWLAST - 1: {
        DestroyWindow(g_Globals._hwndDesktopBar);
        DestroyWindow(g_Globals._hwndDesktop);
        break;
    }
    case ID_TRAY_VOLUME:
        OnTraySndVol(NULL, 0);
        break;

    case ID_VOLUME_PROPERTIES:
        launch_cpanel(_hwnd, TEXT("mmsys.cpl"));
        break;

    default:
        if (_hwndQuickLaunch)
            return (int)SendMessage(_hwndQuickLaunch, WM_COMMAND, MAKEWPARAM(id, code), 0);
        else
            return 1;
    }

    return 0;
}


void DesktopBar::ShowOrHideStartMenu(const char *startAction)
{
    if (startAction[0] != '\0') {
        if (stricmp(startAction, "none") == 0) return;

        if (g_Globals._lua) {
            g_Globals._lua->call(startAction);
            return;
        }
    }

    if (_startMenuRoot) {
        // set the Button, if not set
        if (!Button_GetState(_hwndStartButton))
            Button_SetState(_hwndStartButton, TRUE);

        if (_startMenuRoot->IsStartMenuVisible())
            _startMenuRoot->CloseStartMenu();
        else
            _startMenuRoot->TrackStartmenu();

        // StartMenu was closed, release button state
        Button_SetState(_hwndStartButton, FALSE);
    }
}


// copy data structure for tray notifications
struct TrayNotifyCDS {
    DWORD   cookie;
    DWORD   notify_code;
    NOTIFYICONDATA nicon_data;
};

static LRESULT IconIdentifierEvent(COPYDATASTRUCT *pcds)
{
    POINT cursorPos;
    GetCursorPos(&cursorPos);

    TrayNotifyCDS *ptr = (TrayNotifyCDS *)pcds->lpData;
    switch (ptr->notify_code)
    {
    case 2:
        return MAKELONG(16, 16);
    default:
        return MAKELONG(cursorPos.x, cursorPos.y);
    }

    return 0;
}

typedef struct _APPBARDATA_WIN10
{
    DWORD cbSize;
    DWORD hWnd;
    UINT uCallbackMessage;
    UINT uEdge;
    RECT rc;
    LPARAM lParam; // message specific
#ifndef _WIN64
    DWORD  dwPadding1;
#endif
}APPBARDATA_WIN10, *PAPPBARDATA_WIN10;

/* Win10 */
typedef struct
{
    APPBARDATA_WIN10 abd;
    DWORD  dwMessage;
    DWORD  dwPadding1;
    HANDLE hSharedMemory;
#ifndef _WIN64
    DWORD  dwPadding2;
#endif
    DWORD  dwSourceProcessId;
    DWORD  dwPadding3;
}APPBARMSGDATA_WIN10, *PAPPBARMSGDATA_WIN10;
typedef const APPBARMSGDATA_WIN10 *PCAPPBARMSGDATA_WIN10;

// Data sent with AppBar Message
typedef struct _SHELLAPPBARDATA
{
    _SHELLAPPBARDATA(APPBARDATA_WIN10& abdsrc) :abd(abdsrc) {}

    const APPBARDATA_WIN10& abd;
    /**/
    DWORD  dwMessage;
    HANDLE hSharedMemory;
    DWORD  dwSourceProcessId;
    /**/
} SHELLAPPBARDATA, *PSHELLAPPBARDATA;

static LRESULT HandleAppBarMessage(PSHELLAPPBARDATA psad)
{
    LRESULT lResult = 0;
    if (psad == NULL) return 0;
    switch (psad->dwMessage) {
    case ABM_GETSTATE:
        /* Returns the current TaskBar state(0:autohide, 1:alwaysontop) */
        break;
    case ABM_GETTASKBARPOS: {
        PAPPBARDATA_WIN10 pabd = NULL;
        if (psad->hSharedMemory) {
            pabd = (APPBARDATA_WIN10 *)SHLockShared(psad->hSharedMemory, psad->dwSourceProcessId);
        }
        if (pabd) {
            GetWindowRect(g_Globals._hwndDesktopBar, &(pabd->rc));
            pabd->uEdge = ABE_BOTTOM;
            SHUnlockShared(psad);
            lResult = 1;
        }
        break;
    }
    }

    return lResult;
}

static LRESULT ProcessAppBarMessage(COPYDATASTRUCT *pcds)
{
    LRESULT lResult = 0;

    switch (pcds->cbData) {
    case sizeof(APPBARMSGDATA_WIN10) : {
        PAPPBARMSGDATA_WIN10 pamd = (PAPPBARMSGDATA_WIN10)pcds->lpData;
        //TRACE("APPBARDATA_WIN10: %u", pamd->abd.cbSize);
        if (sizeof(APPBARDATA_WIN10) != pamd->abd.cbSize)
            break;
        //TRACE("dwMessage: %u", pamd->dwMessage);
        SHELLAPPBARDATA sbd((APPBARDATA_WIN10&)(pamd->abd));
        sbd.dwMessage = pamd->dwMessage;
        sbd.hSharedMemory = pamd->hSharedMemory;
        sbd.dwSourceProcessId = pamd->dwSourceProcessId;
        lResult = HandleAppBarMessage(&sbd);
        break;
    }
    default: {
        //TRACE("Unknown APPBARMSGDATA size: %u", pcds->cbData);
    }
    }
    return lResult;
}

#define SH_APPBAR_DATA 0
#define SH_TRAY_DATA 1
#define SH_TRAY_ICON_IDENT_DATA 3

LRESULT DesktopBar::ProcessCopyData(COPYDATASTRUCT *pcds)
{
    // Is this a tray notification message?
    if (pcds->dwData == SH_APPBAR_DATA) {
        return ProcessAppBarMessage(pcds);
    } else if (pcds->dwData == SH_TRAY_DATA) {
        TrayNotifyCDS *ptr = (TrayNotifyCDS *)pcds->lpData;
        NotifyArea *notify_area = GET_WINDOW(NotifyArea, _hwndNotify);
        if (notify_area)
            return notify_area->ProcessTrayNotification(ptr->notify_code, &ptr->nicon_data);
    } else if (pcds->dwData == SH_TRAY_ICON_IDENT_DATA) {
        return IconIdentifierEvent(pcds);
    }

    return FALSE;
}


void DesktopBar::ControlResize(WPARAM wparam, LPARAM lparam)
{
    PRECT dragRect = (PRECT) lparam;
    //int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    ///@todo write code for taskbar being at sides or top.

    switch (wparam) {
    case WMSZ_BOTTOM: ///@todo Taskbar is at the top of the screen
        break;

    case WMSZ_TOP:    // Taskbar is at the bottom of the screen
        dragRect->top = screenHeight - ((screenHeight - dragRect->top) / DESKTOPBARBAR_HEIGHT) * DESKTOPBARBAR_HEIGHT;
        if (dragRect->top < screenHeight / 2)
            dragRect->top = screenHeight - ((screenHeight / 2 / DESKTOPBARBAR_HEIGHT) * DESKTOPBARBAR_HEIGHT);
        else if (dragRect->top > screenHeight - DESKTOPBARBAR_HEIGHT)
            dragRect->top = screenHeight - DESKTOPBARBAR_HEIGHT;
        break;

    case WMSZ_RIGHT:  ///@todo Taskbar is at the left of the screen
        break;

    case WMSZ_LEFT:   ///@todo Taskbar is at the right of the screen
        break;
    }
}


void DesktopBar::AddTrayIcons()
{
    HICON icon = NULL;
    if (JCFG2_DEF("JS_TRAYNOTIFY", "soundicon", false).ToBool() != FALSE) {
        icon = g_Globals._icon_cache.get_icon(ICID_TRAY_SND_NONE).get_hicon();
        _traySndVolIcon.Add(icon, ResString(IDS_VOLUME));
    }
    if (JCFG2_DEF("JS_TRAYNOTIFY", "networkicon", false).ToBool() != FALSE) {
        icon = g_Globals._icon_cache.get_icon(ICID_TRAY_NET_WIRED_LAN).get_hicon();
        _trayNetworkIcon.Add(icon, ResString(IDS_NETWORK));
    }
}

void DesktopBar::TrayClick(UINT id, int btn)
{
    switch (id) {
    case ID_TRAY_VOLUME:
        if (btn == TRAYBUTTON_LEFT) {
            SetTimer(_hwnd, ID_TRAY_VOLUME, 500, NULL); // wait a bit to correctly handle double clicks
        } else {
            PopupMenu menu(IDM_VOLUME);
            SetMenuDefaultItem(menu, 0, MF_BYPOSITION);
            menu.TrackPopupMenuAtPos(_hwnd, GetMessagePos());
        }
        break;
    case ID_TRAY_NETWORK:
        if (btn == TRAYBUTTON_LEFT) {
            SetTimer(_hwnd, ID_TRAY_NETWORK, 500, NULL); // wait a bit to correctly handle double clicks
        }
        break;
    }
}

void DesktopBar::TrayDblClick(UINT id, int btn)
{
    switch (id) {
    case ID_TRAY_VOLUME:
        OnTraySndVol(_hwnd, id);
        break;
    case ID_TRAY_NETWORK:
        OnTrayNetwork(_hwnd, id);
        break;
    }
}

