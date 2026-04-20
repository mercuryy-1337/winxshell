
#include <Windows.h>
#include <dwmapi.h>

#include <map>
#include <vector>

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

#ifndef DPI_SX
#define DPI_SX(x) (x)
#endif

#ifndef DPI_SY
#define DPI_SY(y) (y)
#endif

extern int JCfg_GetDesktopBarHeightWithDPI();
extern "C" {
    void _log_(LPCTSTR txt);
}

WCHAR dwmWndClassName[] = TEXT("dwmTaskThumbnailWnd");

HWND tbhwnd = NULL;

using namespace std;

#ifndef GCL_HICON
#define GCL_HICON GCLP_HICON
#endif

#ifndef GCL_HICONSM
#define GCL_HICONSM GCLP_HICONSM
#endif

struct ThumbnailEntry {
    HTHUMBNAIL _thumbnail;
    HWND _source;
    bool _hovered;
    bool _close_hot;
};

typedef map<HWND, ThumbnailEntry> ThumbnailMap;


static int ThumbnailInited = 0;
static CRITICAL_SECTION cs;
static ThumbnailMap thumbnail_map;

static HWND _taskbar = NULL;
static HWND _toolbar = NULL;
static HWND _anchor_toolbar = NULL;
static RECT _anchor_rect = { 0 };
static int currentThumbnailId = -1;

#define ID_TIMER_DESTORYTHUMBNAIL 101

struct ThumbnailLayoutMetrics {
    int _outer_padding;
    int _header_height;
    int _section_gap;
    int _preview_border;
    int _icon_size;
    int _title_gap;
    int _close_button_size;
};

static ThumbnailLayoutMetrics GetThumbnailLayoutMetrics()
{
    ThumbnailLayoutMetrics metrics = {
        DPI_SX(8),
        DPI_SY(28),
        DPI_SY(6),
        DPI_SX(1),
        DPI_SX(16),
        DPI_SX(8),
        DPI_SX(18)
    };
    return metrics;
}

void InitThumbnailWindow(HWND taskbar, HWND toolbar)
{
    if (!ThumbnailInited) {
        InitializeCriticalSection(&cs);
        ThumbnailInited = 1;
    }
    _taskbar = taskbar;
    _toolbar = toolbar;
    _anchor_toolbar = toolbar;
    SetRectEmpty(&_anchor_rect);
    currentThumbnailId = -1;

}

void DestoryThumbnailWindow()
{
    if (!ThumbnailInited) return;
    EnterCriticalSection(&cs);
    for (ThumbnailMap::iterator it = thumbnail_map.begin();
        it != thumbnail_map.end(); ++it) {
        if (it->second._thumbnail) DwmUnregisterThumbnail(it->second._thumbnail);
        //ShowWindow(it->first, SW_HIDE);
        DestroyWindow(it->first);
    }
    thumbnail_map.clear();
    currentThumbnailId = -1;
    _anchor_toolbar = NULL;
    SetRectEmpty(&_anchor_rect);
    LeaveCriticalSection(&cs);
}

static bool IsPointInsideRect(const RECT &rc, const POINT &pt)
{
    return pt.x >= rc.left && pt.x <= rc.right && pt.y >= rc.top && pt.y <= rc.bottom;
}

static HWND GetThumbnailSource(HWND preview_hwnd)
{
    HWND source = NULL;
    EnterCriticalSection(&cs);
    ThumbnailMap::const_iterator found = thumbnail_map.find(preview_hwnd);
    if (found != thumbnail_map.end())
        source = found->second._source;
    LeaveCriticalSection(&cs);
    return source;
}

static RECT GetThumbnailHeaderRect(HWND preview_hwnd)
{
    RECT client_rect = { 0 };
    GetClientRect(preview_hwnd, &client_rect);

    ThumbnailLayoutMetrics metrics = GetThumbnailLayoutMetrics();
    RECT header_rect = {
        client_rect.left + metrics._outer_padding,
        client_rect.top + metrics._outer_padding,
        client_rect.right - metrics._outer_padding,
        client_rect.top + metrics._outer_padding + metrics._header_height
    };
    return header_rect;
}

static RECT GetThumbnailPreviewFrameRect(HWND preview_hwnd)
{
    RECT client_rect = { 0 };
    GetClientRect(preview_hwnd, &client_rect);

    ThumbnailLayoutMetrics metrics = GetThumbnailLayoutMetrics();
    RECT header_rect = GetThumbnailHeaderRect(preview_hwnd);
    RECT preview_frame_rect = {
        client_rect.left + metrics._outer_padding,
        header_rect.bottom + metrics._section_gap,
        client_rect.right - metrics._outer_padding,
        client_rect.bottom - metrics._outer_padding
    };
    return preview_frame_rect;
}

static RECT GetThumbnailPreviewRect(HWND preview_hwnd)
{
    RECT preview_rect = GetThumbnailPreviewFrameRect(preview_hwnd);
    ThumbnailLayoutMetrics metrics = GetThumbnailLayoutMetrics();
    InflateRect(&preview_rect, -metrics._preview_border, -metrics._preview_border);
    return preview_rect;
}

static RECT GetThumbnailIconRect(HWND preview_hwnd)
{
    RECT header_rect = GetThumbnailHeaderRect(preview_hwnd);
    ThumbnailLayoutMetrics metrics = GetThumbnailLayoutMetrics();
    int top = header_rect.top + ((header_rect.bottom - header_rect.top - metrics._icon_size) / 2);

    RECT icon_rect = {
        header_rect.left,
        top,
        header_rect.left + metrics._icon_size,
        top + metrics._icon_size
    };
    return icon_rect;
}

static RECT GetThumbnailCloseButtonRect(HWND preview_hwnd)
{
    RECT header_rect = GetThumbnailHeaderRect(preview_hwnd);
    ThumbnailLayoutMetrics metrics = GetThumbnailLayoutMetrics();

    int size = metrics._close_button_size;
    int header_width = header_rect.right - header_rect.left;
    int header_height = header_rect.bottom - header_rect.top;
    if (size > header_width / 4)
        size = header_width / 4;
    if (size > header_height)
        size = header_height;
    if (size < DPI_SX(12))
        size = DPI_SX(12);

    int top = header_rect.top + ((header_height - size) / 2);

    RECT close_rect = {
        header_rect.right - size,
        top,
        header_rect.right,
        top + size
    };
    return close_rect;
}

static RECT GetThumbnailTitleRect(HWND preview_hwnd, bool has_icon)
{
    RECT header_rect = GetThumbnailHeaderRect(preview_hwnd);
    RECT close_rect = GetThumbnailCloseButtonRect(preview_hwnd);
    ThumbnailLayoutMetrics metrics = GetThumbnailLayoutMetrics();

    RECT title_rect = header_rect;
    title_rect.left = has_icon ? (GetThumbnailIconRect(preview_hwnd).right + metrics._title_gap) : header_rect.left;
    title_rect.right = close_rect.left - metrics._title_gap;
    if (title_rect.right < title_rect.left)
        title_rect.right = title_rect.left;
    return title_rect;
}

static HICON GetThumbnailWindowIcon(HWND hwnd)
{
    HICON hIcon = 0;

    SendMessageTimeout(hwnd, WM_GETICON, ICON_SMALL2, 0, SMTO_ABORTIFHUNG, 100, (PDWORD_PTR)&hIcon);

    if (!hIcon)
        SendMessageTimeout(hwnd, WM_GETICON, ICON_SMALL, 0, SMTO_ABORTIFHUNG, 100, (PDWORD_PTR)&hIcon);

    if (!hIcon)
        SendMessageTimeout(hwnd, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG, 100, (PDWORD_PTR)&hIcon);

    if (!hIcon)
        hIcon = (HICON)GetClassLongPtr(hwnd, GCL_HICONSM);

    if (!hIcon)
        hIcon = (HICON)GetClassLongPtr(hwnd, GCL_HICON);

    if (!hIcon)
        SendMessageTimeout(hwnd, WM_QUERYDRAGICON, 0, 0, SMTO_ABORTIFHUNG, 100, (PDWORD_PTR)&hIcon);

    return hIcon;
}

static bool IsPointInThumbnailCloseButton(HWND preview_hwnd, const POINT &pt)
{
    RECT close_rect = GetThumbnailCloseButtonRect(preview_hwnd);
    return PtInRect(&close_rect, pt) ? true : false;
}

static void UpdateThumbnailHoverState(HWND preview_hwnd, bool hovered, bool close_hot)
{
    bool invalidate = false;

    EnterCriticalSection(&cs);
    ThumbnailMap::iterator found = thumbnail_map.find(preview_hwnd);
    if (found != thumbnail_map.end()) {
        if (found->second._hovered != hovered || found->second._close_hot != close_hot) {
            found->second._hovered = hovered;
            found->second._close_hot = close_hot;
            invalidate = true;
        }
    }
    LeaveCriticalSection(&cs);

    if (invalidate)
        InvalidateRect(preview_hwnd, NULL, FALSE);
}

static void GetThumbnailHoverState(HWND preview_hwnd, bool *hovered, bool *close_hot)
{
    bool local_hovered = false;
    bool local_close_hot = false;

    EnterCriticalSection(&cs);
    ThumbnailMap::const_iterator found = thumbnail_map.find(preview_hwnd);
    if (found != thumbnail_map.end()) {
        local_hovered = found->second._hovered;
        local_close_hot = found->second._close_hot;
    }
    LeaveCriticalSection(&cs);

    if (hovered)
        *hovered = local_hovered;
    if (close_hot)
        *close_hot = local_close_hot;
}

static void DrawThumbnailCloseButton(HDC hdc, HWND preview_hwnd, bool hovered, bool hot)
{
    RECT close_rect = GetThumbnailCloseButtonRect(preview_hwnd);
    if (hovered || hot) {
        HBRUSH brush = CreateSolidBrush(hot ? RGB(196, 43, 28) : RGB(70, 70, 70));
        HPEN pen = CreatePen(PS_NULL, 0, 0);
        HGDIOBJ old_brush = SelectObject(hdc, brush);
        HGDIOBJ old_pen = SelectObject(hdc, pen);

        RoundRect(hdc, close_rect.left, close_rect.top, close_rect.right, close_rect.bottom, DPI_SX(6), DPI_SY(6));

        SelectObject(hdc, old_pen);
        SelectObject(hdc, old_brush);
        DeleteObject(pen);
        DeleteObject(brush);
    }

    HFONT close_font = CreateFont(-MulDiv(9, GetDeviceCaps(hdc, LOGPIXELSY), 72), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, TEXT("Marlett"));
    HFONT old_font = close_font ? (HFONT)SelectObject(hdc, close_font) : NULL;
    COLORREF old_color = SetTextColor(hdc, hot || hovered ? RGB(255, 255, 255) : RGB(216, 216, 216));
    int old_mode = SetBkMode(hdc, TRANSPARENT);
    RECT glyph_rect = close_rect;
    DrawText(hdc, TEXT("r"), 1, &glyph_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SetBkMode(hdc, old_mode);
    SetTextColor(hdc, old_color);
    if (old_font)
        SelectObject(hdc, old_font);
    if (close_font)
        DeleteObject(close_font);
}

static void DrawThumbnailHeader(HDC hdc, HWND preview_hwnd, bool hovered, bool close_hot)
{
    RECT header_rect = GetThumbnailHeaderRect(preview_hwnd);
    RECT client_rect = { 0 };
    GetClientRect(preview_hwnd, &client_rect);
    HWND source = GetThumbnailSource(preview_hwnd);

    HBRUSH outer_brush = CreateSolidBrush(hovered ? RGB(48, 48, 48) : RGB(40, 40, 40));
    FillRect(hdc, &client_rect, outer_brush);
    DeleteObject(outer_brush);

    HBRUSH header_brush = CreateSolidBrush(hovered ? RGB(52, 52, 52) : RGB(44, 44, 44));
    FillRect(hdc, &header_rect, header_brush);
    DeleteObject(header_brush);

    RECT preview_frame_rect = GetThumbnailPreviewFrameRect(preview_hwnd);
    RECT preview_rect = GetThumbnailPreviewRect(preview_hwnd);
    HBRUSH border_brush = CreateSolidBrush(hovered ? RGB(126, 126, 126) : RGB(102, 102, 102));
    FillRect(hdc, &preview_frame_rect, border_brush);
    DeleteObject(border_brush);

    HBRUSH preview_brush = CreateSolidBrush(RGB(18, 18, 18));
    FillRect(hdc, &preview_rect, preview_brush);
    DeleteObject(preview_brush);

    HICON icon = source ? GetThumbnailWindowIcon(source) : NULL;
    if (icon) {
        RECT icon_rect = GetThumbnailIconRect(preview_hwnd);
        DrawIconEx(hdc, icon_rect.left, icon_rect.top, icon,
            icon_rect.right - icon_rect.left, icon_rect.bottom - icon_rect.top,
            0, NULL, DI_NORMAL);
    }

    TCHAR title[256] = { 0 };
    if (source)
        GetWindowText(source, title, sizeof(title) / sizeof(title[0]));

    RECT title_rect = GetThumbnailTitleRect(preview_hwnd, icon ? true : false);
    HFONT old_font = (HFONT)SelectObject(hdc, (HFONT)GetStockObject(DEFAULT_GUI_FONT));
    COLORREF old_color = SetTextColor(hdc, RGB(245, 245, 245));
    int old_mode = SetBkMode(hdc, TRANSPARENT);
    DrawText(hdc, title, -1, &title_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SetBkMode(hdc, old_mode);
    SetTextColor(hdc, old_color);
    SelectObject(hdc, old_font);

    DrawThumbnailCloseButton(hdc, preview_hwnd, hovered, close_hot);
}

static void CancelDestroyTimer()
{
    if (_taskbar)
        KillTimer(_taskbar, ID_TIMER_DESTORYTHUMBNAIL);
}

static void ScheduleDestroyTimer(UINT delay)
{
    if (_taskbar)
        SetTimer(_taskbar, ID_TIMER_DESTORYTHUMBNAIL, delay, NULL);
}

static bool IsCursorOverPreviewRegion()
{
    POINT pt;
    GetCursorPos(&pt);

    if (!IsRectEmpty(&_anchor_rect) && IsPointInsideRect(_anchor_rect, pt))
        return true;

    EnterCriticalSection(&cs);
    for (ThumbnailMap::const_iterator it = thumbnail_map.begin(); it != thumbnail_map.end(); ++it) {
        RECT rc;
        if (IsWindow(it->first) && GetWindowRect(it->first, &rc) && IsPointInsideRect(rc, pt)) {
            LeaveCriticalSection(&cs);
            return true;
        }
    }
    LeaveCriticalSection(&cs);

    return false;
}

bool IsThumbnailCursorInRegion()
{
    if (!ThumbnailInited || thumbnail_map.empty())
        return false;
    return IsCursorOverPreviewRegion();
}

static void ActivateThumbnailSource(HWND source)
{
    if (!source || !IsWindow(source)) {
        DestoryThumbnailWindow();
        return;
    }

    if (IsIconic(source))
        PostMessage(source, WM_SYSCOMMAND, SC_RESTORE, 0);

    SetForegroundWindow(source);
    DestoryThumbnailWindow();
}

LRESULT CALLBACK ThumbnailProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
#ifdef _DEBUG
    TCHAR buff[255];
    _swprintf(buff, TEXT("ThumbnailProcedure: Message(%d)"), message);
    _log_(buff);
#endif
    switch (message) {
    case WM_MOUSEMOVE:
    {
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        UpdateThumbnailHoverState(hwnd, true, IsPointInThumbnailCloseButton(hwnd, pt));
        CancelDestroyTimer();
        TRACKMOUSEEVENT mouse_event;
        mouse_event.cbSize = sizeof(TRACKMOUSEEVENT);
        mouse_event.dwFlags = TME_LEAVE;
        mouse_event.hwndTrack = hwnd;
        mouse_event.dwHoverTime = 0;
        TrackMouseEvent(&mouse_event);
        return 0;
    }

    case WM_MOUSELEAVE:
        UpdateThumbnailHoverState(hwnd, false, false);
        if (!IsCursorOverPreviewRegion())
            ScheduleDestroyTimer(120);
        return 0;

    case WM_TIMER:
        if (wParam == ID_TIMER_DESTORYTHUMBNAIL) {
            if (!IsCursorOverPreviewRegion())
                ScheduleDestroyTimer(120);
        }
        return 0;

    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    {
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        if (IsPointInThumbnailCloseButton(hwnd, pt)) {
            HWND source = GetThumbnailSource(hwnd);
            if (source && IsWindow(source))
                PostMessage(source, WM_SYSCOMMAND, SC_CLOSE, 0);
            DestoryThumbnailWindow();
            return 0;
        }
        ActivateThumbnailSource(GetThumbnailSource(hwnd));
        return 0;
    }

    case WM_MBUTTONUP: {
        HWND source = GetThumbnailSource(hwnd);
        if (source && IsWindow(source))
            PostMessage(source, WM_SYSCOMMAND, SC_CLOSE, 0);
        DestoryThumbnailWindow();
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        bool hovered = false;
        bool close_hot = false;
        GetThumbnailHoverState(hwnd, &hovered, &close_hot);
        DrawThumbnailHeader(hdc, hwnd, hovered, close_hot);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProc(hwnd, message, wParam, lParam);
}

static bool GetToolbarItemScreenRect(HWND toolbar, int buttonIndex, RECT *screen_rect)
{
    if (!toolbar || !screen_rect || !IsWindow(toolbar))
        return false;

    RECT toolbar_rect;
    if (!GetWindowRect(toolbar, &toolbar_rect))
        return false;

    RECT item_rect = { 0 };
    if (!SendMessage(toolbar, TB_GETITEMRECT, buttonIndex, (LPARAM)&item_rect))
        return false;

    OffsetRect(&item_rect, toolbar_rect.left, toolbar_rect.top);
    *screen_rect = item_rect;
    return true;
}

static SIZE GetThumbnailOuterSize(const RECT &source_rect)
{
    ThumbnailLayoutMetrics metrics = GetThumbnailLayoutMetrics();
    SIZE size = { DPI_SX(260), DPI_SY(160) };

    int h = source_rect.bottom - source_rect.top;
    int w = source_rect.right - source_rect.left;
    if (h <= 0 || w <= 0)
        return size;

    int scr_w = GetSystemMetrics(SM_CXSCREEN);
    int scr_h = GetSystemMetrics(SM_CYSCREEN);
    int tmp_w = 0;
    int tmp_h = 0;
    float scr_v = scr_h / (scr_w + 0.0f);
    float v1 = h / (w + 0.0f);
    float v2 = w / (h + 0.0f);

    if (v1 >= scr_v) {
        tmp_h = (int)(scr_h / 8);
        tmp_w = (int)(tmp_h * v2);
    } else {
        tmp_w = (int)(scr_w / 8);
        tmp_h = (int)(tmp_w * v1);
    }

    if (tmp_w < DPI_SX(180))
        tmp_w = DPI_SX(180);
    if (tmp_h < DPI_SY(110))
        tmp_h = DPI_SY(110);

    size.cx = tmp_w + metrics._outer_padding * 2 + metrics._preview_border * 2;
    size.cy = tmp_h + metrics._outer_padding * 2 + metrics._header_height + metrics._section_gap + metrics._preview_border * 2;
    return size;
}

static HWND CreateThumbnailWindow(HINSTANCE instance, const RECT &outer_rect)
{
    WNDCLASSEX wincl;
    ZeroMemory(&wincl, sizeof(WNDCLASSEX));

    // Register the window class
    wincl.hInstance = instance;
    wincl.lpszClassName = dwmWndClassName;
    wincl.lpfnWndProc = ThumbnailProcedure;
    wincl.cbSize = sizeof(WNDCLASSEX);
    //wincl.style = CS_DROPSHADOW;
    //wincl.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    //wincl.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    wincl.hCursor = LoadCursor(NULL, IDC_ARROW);
    wincl.hbrBackground = NULL;

    RegisterClassEx(&wincl);
    HWND dwmThumbnailWnd = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, dwmWndClassName,
        NULL, WS_POPUP, outer_rect.left, outer_rect.top,
        outer_rect.right - outer_rect.left, outer_rect.bottom - outer_rect.top,
        NULL, NULL, instance, NULL);

    if (dwmThumbnailWnd) {
        int width = outer_rect.right - outer_rect.left;
        int height = outer_rect.bottom - outer_rect.top;
        int radius = DPI_SX(14);
        HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, radius, radius);
        if (region)
            SetWindowRgn(dwmThumbnailWnd, region, TRUE);

        const int corner_pref = 2;
        DwmSetWindowAttribute(dwmThumbnailWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner_pref, sizeof(corner_pref));
    }

    return dwmThumbnailWnd;
}

HRESULT BindThumbnailWindow(HWND hWnd, HTHUMBNAIL thumbnail) {
    HRESULT hr = S_OK;
    RECT rc = GetThumbnailPreviewRect(hWnd);
    DWM_THUMBNAIL_PROPERTIES dskThumbProps;
    dskThumbProps.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE;// | DWM_TNP_OPACITY;
    dskThumbProps.fVisible = TRUE;
    dskThumbProps.opacity = 255;
    dskThumbProps.rcDestination = rc;
    hr = DwmUpdateThumbnailProperties(thumbnail, &dskThumbProps);
    return hr;
}

int DrawThumbnailWindows(HINSTANCE hInstance, const HWND *hwnds, int hwnd_count, HWND hwndToolbar, int buttonIndex)
{
    if (currentThumbnailId == buttonIndex && _anchor_toolbar == hwndToolbar && !thumbnail_map.empty())
        return 0;

    if (!ThumbnailInited || !hwnds || hwnd_count <= 0)
        return 0;

    HWND toolbar = hwndToolbar ? hwndToolbar : _toolbar;
    if (!toolbar || !IsWindow(toolbar))
        return 1;

    RECT anchor_rect;
    if (!GetToolbarItemScreenRect(toolbar, buttonIndex, &anchor_rect))
        return 1;

    DestoryThumbnailWindow();

    _anchor_toolbar = toolbar;
    _anchor_rect = anchor_rect;
    currentThumbnailId = buttonIndex;

    vector<HWND> sources;
    vector<SIZE> sizes;
    int total_width = 0;
    int max_height = 0;
    const int spacing = DPI_SX(10);

    for (int index = 0; index < hwnd_count; ++index) {
        HWND source = hwnds[index];
        if (!source || !IsWindow(source))
            continue;

        WINDOWPLACEMENT wndpl;
        ZeroMemory(&wndpl, sizeof(wndpl));
        wndpl.length = sizeof(WINDOWPLACEMENT);
        RECT source_rect = { 0 };
        if (GetWindowPlacement(source, &wndpl))
            source_rect = wndpl.rcNormalPosition;
        if (IsRectEmpty(&source_rect))
            GetWindowRect(source, &source_rect);

        SIZE size = GetThumbnailOuterSize(source_rect);
        if (!sources.empty())
            total_width += spacing;
        total_width += size.cx;
        if (size.cy > max_height)
            max_height = size.cy;

        sources.push_back(source);
        sizes.push_back(size);
    }

    if (sources.empty())
        return 1;

    int scr_w = GetSystemMetrics(SM_CXSCREEN);
    int scr_h = GetSystemMetrics(SM_CYSCREEN);
    int start_x = anchor_rect.left + ((anchor_rect.right - anchor_rect.left) / 2) - (total_width / 2);
    if (start_x < DPI_SX(8))
        start_x = DPI_SX(8);
    if (start_x + total_width > scr_w - DPI_SX(8))
        start_x = scr_w - total_width - DPI_SX(8);

    int start_y = scr_h - max_height - DPI_SY(20) - JCfg_GetDesktopBarHeightWithDPI();
    if (start_y < DPI_SY(8))
        start_y = DPI_SY(8);

    EnterCriticalSection(&cs);
    int x = start_x;
    for (size_t index = 0; index < sources.size(); ++index) {
        RECT outer_rect = {
            x,
            start_y + ((max_height - sizes[index].cy) / 2),
            x + sizes[index].cx,
            start_y + ((max_height - sizes[index].cy) / 2) + sizes[index].cy
        };
        HWND preview_hwnd = CreateThumbnailWindow(hInstance, outer_rect);
        if (preview_hwnd) {
            HTHUMBNAIL thumbnail = NULL;
            HRESULT hr = DwmRegisterThumbnail(preview_hwnd, sources[index], &thumbnail);
            if (SUCCEEDED(hr)) {
                ThumbnailEntry entry = { thumbnail, sources[index], false, false };
                thumbnail_map.insert(make_pair(preview_hwnd, entry));
                BindThumbnailWindow(preview_hwnd, thumbnail);
                ShowWindow(preview_hwnd, SW_SHOWNOACTIVATE);
            } else {
                ThumbnailEntry entry = { (HTHUMBNAIL)NULL, sources[index], false, false };
                thumbnail_map.insert(make_pair(preview_hwnd, entry));
            }
        }
        x += sizes[index].cx + spacing;
    }
    LeaveCriticalSection(&cs);

    BOOL enabled = FALSE;
    DwmIsCompositionEnabled(&enabled);
    return 0;
}

int DrawThumbnailWindow(HINSTANCE hInstance, HWND hWndSrc,
    LPCTSTR lpClassName, LPCTSTR lpWindowName, int id)
{
    HWND source = hWndSrc;
    if (!source) {
        source = FindWindow(lpClassName, lpWindowName);
        if (!source)
            return 1;
    }

    return DrawThumbnailWindows(hInstance, &source, 1, _toolbar, id);
}

