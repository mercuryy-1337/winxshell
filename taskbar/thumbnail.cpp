
#include <Windows.h>
#include <dwmapi.h>

#include <map>
#include <vector>

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

struct ThumbnailEntry {
    HTHUMBNAIL _thumbnail;
    HWND _source;
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
        CancelDestroyTimer();
        TRACKMOUSEEVENT mouse_event;
        mouse_event.cbSize = sizeof(TRACKMOUSEEVENT);
        mouse_event.dwFlags = TME_LEAVE;
        mouse_event.hwndTrack = hwnd;
        mouse_event.dwHoverTime = 0;
        TrackMouseEvent(&mouse_event);
        return 0;

    case WM_MOUSELEAVE:
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
        ActivateThumbnailSource(GetThumbnailSource(hwnd));
        return 0;

    case WM_MBUTTONUP: {
        HWND source = GetThumbnailSource(hwnd);
        if (source && IsWindow(source))
            PostMessage(source, WM_SYSCOMMAND, SC_CLOSE, 0);
        DestoryThumbnailWindow();
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

    size.cx = tmp_w + 20;
    size.cy = tmp_h + 20;
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
    wincl.hbrBackground = (HBRUSH)GetStockObject(DKGRAY_BRUSH);

    RegisterClassEx(&wincl);
    HWND dwmThumbnailWnd = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, dwmWndClassName,
        NULL, WS_POPUP, outer_rect.left, outer_rect.top,
        outer_rect.right - outer_rect.left, outer_rect.bottom - outer_rect.top,
        NULL, NULL, instance, NULL);

    return dwmThumbnailWnd;
}

HRESULT BindThumbnailWindow(HWND hWnd, HTHUMBNAIL thumbnail) {
    HRESULT hr = S_OK;
    RECT rc;
    GetClientRect(hWnd, &rc);
    rc.left += 10;
    rc.right -= 10;
    rc.top += 10;
    rc.bottom -= 10;
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
                ThumbnailEntry entry = { thumbnail, sources[index] };
                thumbnail_map.insert(make_pair(preview_hwnd, entry));
                BindThumbnailWindow(preview_hwnd, thumbnail);
                ShowWindow(preview_hwnd, SW_SHOWNOACTIVATE);
            } else {
                ThumbnailEntry entry = { (HTHUMBNAIL)NULL, sources[index] };
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

