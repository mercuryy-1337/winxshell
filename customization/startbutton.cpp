
#include <precomp.h>
#include <Uxtheme.h>
#include "startbutton.h"
#include "../utility/taskbar_draw.h"


void PictureButton2::DrawItem(LPDRAWITEMSTRUCT dis)
{
    UINT state = DFCS_BUTTONPUSH;
    int style = GetWindowStyle(_hwnd);
    HICON drawIcon = _hIcon;
    HBRUSH drawBrush = _hBrush;
    if (dis->itemState & ODS_DISABLED)
        state |= DFCS_INACTIVE;

    POINT imagePos;

    // horizontal centered, vertical centered
    imagePos.x = (dis->rcItem.left + dis->rcItem.right - _cx) / 2;
    imagePos.y = (dis->rcItem.top + dis->rcItem.bottom - _cy) / 2;

    if (_hovered && _hHoverIcon)
        drawIcon = _hHoverIcon;

    if (dis->itemState & ODS_SELECTED) {
        state |= DFCS_PUSHED;
        if (_hPressedIcon)
            drawIcon = _hPressedIcon;
        drawBrush = _hHotBrush;
    }

    if (taskbar_draw::IsModernTaskbarEnabled()) {
        FillRect(dis->hDC, &dis->rcItem, _hBrush);

        RECT highlight_rect = {
            dis->rcItem.left + DPI_SX(1),
            dis->rcItem.top + DPI_SY(2),
            dis->rcItem.right - DPI_SX(1),
            dis->rcItem.bottom - DPI_SY(2)
        };
        if (highlight_rect.bottom > highlight_rect.top) {
            if (_hovered) {
                taskbar_draw::FillRoundedRect(
                    dis->hDC,
                    highlight_rect,
                    taskbar_draw::GetHighlightRadius(),
                    taskbar_draw::GetHoverColor(),
                    taskbar_draw::GetHoverAlpha());
            }
            if (dis->itemState & ODS_SELECTED) {
                taskbar_draw::FillRoundedRect(
                    dis->hDC,
                    highlight_rect,
                    taskbar_draw::GetHighlightRadius(),
                    taskbar_draw::GetHighlightColor(),
                    taskbar_draw::GetHighlightAlpha());
            }
        }
    } else if (_flat) {
        FillRect(dis->hDC, &dis->rcItem, drawBrush);

        if (style & BS_FLAT)    // Only with BS_FLAT set, there will be drawn a frame without highlight.
            DrawEdge(dis->hDC, &dis->rcItem, EDGE_RAISED, BF_RECT | BF_FLAT);
    } else {
        FillRect(dis->hDC, &dis->rcItem, drawBrush);
        HTHEME hTheme = OpenThemeData(_hwnd, L"Button");
        if (hTheme)
            DrawThemeBackground(hTheme, dis->hDC, DFC_BUTTON, state, &dis->rcItem, NULL);
        else
            DrawFrameControl(dis->hDC, &dis->rcItem, DFC_BUTTON, state);
    }

    if (drawIcon)
        DrawIconEx(dis->hDC, imagePos.x, imagePos.y, drawIcon, _cx, _cy, 0, drawBrush, DI_NORMAL);
    else {
        MemCanvas mem_dc;
        BitmapSelection sel(mem_dc, _hBmp);
        BitBlt(dis->hDC, imagePos.x, imagePos.y, _cx, _cy, mem_dc, 0, 0, SRCCOPY);
    }

    if (dis->itemState & ODS_FOCUS) {
        RECT rect = {
            dis->rcItem.left + 3, dis->rcItem.top + 3,
            dis->rcItem.right - dis->rcItem.left - 4, dis->rcItem.bottom - dis->rcItem.top - 4
        };
        DrawFocusRect(dis->hDC, &rect);
    }
}
