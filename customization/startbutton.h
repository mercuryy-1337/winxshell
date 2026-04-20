#pragma once

#include <Windows.h>
#include "../utility/window.h"

struct PictureButton2 : public PictureButton {
    typedef PictureButton super;

    PictureButton2(HWND hwnd, HICON hIcon, HICON hIcon2, HBRUSH hbrush, HBRUSH hbrush2,
        COLORREF textcolor = -1, bool flat = false)
        : super(hwnd, hIcon, hbrush, textcolor, flat),
        _hIcon(hIcon), _hHoverIcon(hIcon2), _hPressedIcon(hIcon2), _hBmp(0), _hBrush(hbrush), _hHotBrush(hbrush2), _flat(flat), _hovered(false)
    {
        _cx = super::_cx;
        _cy = super::_cy;
        }

    PictureButton2(HWND hwnd, HICON hIcon, HICON hHoverIcon, HICON hPressedIcon, HBRUSH hbrush, HBRUSH hbrush2,
        COLORREF textcolor = -1, bool flat = false)
        : super(hwnd, hIcon, hbrush, textcolor, flat),
        _hIcon(hIcon), _hHoverIcon(hHoverIcon), _hPressedIcon(hPressedIcon), _hBmp(0), _hBrush(hbrush), _hHotBrush(hbrush2), _flat(flat), _hovered(false)
    {
        _cx = super::_cx;
        _cy = super::_cy;
        }

protected:
    void DrawItem(LPDRAWITEMSTRUCT dis);
    HICON   _hIcon;
    HICON   _hHoverIcon;
    HICON   _hPressedIcon;
    HBITMAP _hBmp;
    HBRUSH  _hBrush;
    HBRUSH  _hHotBrush;

    int     _cx;
    int     _cy;

    COLORREF _textColor;
    bool    _flat;
    bool    _hovered;
};

