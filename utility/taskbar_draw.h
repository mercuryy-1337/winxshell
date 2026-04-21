#pragma once

#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

namespace taskbar_draw {

inline bool IsCenteredEnabled()
{
    return JCFG2_DEF("JS_TASKBAR", "centered", true).ToBool() != FALSE;
}

inline bool IsRoundedHighlightEnabled()
{
    return JCFG2_DEF("JS_TASKBAR", "rounded_highlight", true).ToBool() != FALSE;
}

inline bool IsModernTaskbarEnabled()
{
    return IsCenteredEnabled() || IsRoundedHighlightEnabled();
}

inline bool IsAnimationEnabled()
{
    return JCFG2_DEF("JS_TASKBAR", "animations", true).ToBool() != FALSE;
}

inline int GetAnimationDurationMs()
{
    int duration = JCFG2_DEF("JS_TASKBAR", "animation_duration", 200).ToInt();
    if (duration < 80)
        duration = 80;
    return duration;
}

inline int GetHighlightRadius()
{
    int radius = JCFG2_DEF("JS_TASKBAR", "highlight_radius", 0).ToInt();
    if (radius > 0)
        return radius;

    return DPI_SX(4);
}

inline int GetButtonSlotWidth()
{
    int configured = JCFG2_DEF("JS_TASKBAR", "button_width", 0).ToInt();
    if (configured > 0)
        return DPI_SX(configured);

    int min_width = TASKBAR_ICON_SIZE + DPI_SX(6);
    int fallback = DESKTOPBARBAR_HEIGHT - DPI_SX(16);
    if (fallback < min_width)
        fallback = min_width;
    return fallback;
}

inline int GetModernButtonSlotWidth()
{
    int configured = JCFG2_DEF("JS_TASKBAR", "modern_button_width", 0).ToInt();
    if (configured > 0) {
        int configured_width = DPI_SX(configured);
        if ((configured_width & 1) != 0 && configured_width > DPI_SX(30))
            --configured_width;
        return configured_width;
    }

    int preferred = DPI_SX(32);
    int legacy_width = JCFG2_DEF("JS_TASKBAR", "button_width", 0).ToInt();
    if (legacy_width > 0) {
        int configured_width = DPI_SX(legacy_width);
        if (configured_width < preferred)
            preferred = configured_width;
    }

    int min_width = DPI_SX(30);
    if (preferred < min_width)
        preferred = min_width;
    if ((preferred & 1) != 0 && preferred > min_width)
        --preferred;
    return preferred;
}

inline int GetQuickLaunchSlotWidth()
{
    int configured = JCFG2_DEF("JS_QUICKLAUNCH", "button_width", 0).ToInt();
    if (configured > 0)
        return DPI_SX(configured);

    int min_width = TASKBAR_ICON_SIZE + DPI_SX(12);
    int fallback = DESKTOPBARBAR_HEIGHT - DPI_SX(4);
    if (fallback < min_width)
        fallback = min_width;
    return fallback;
}

inline COLORREF GetFallbackOverlayColor()
{
    if (TASKBAR_THEMESTYLE().compare(TEXT("light")) == 0)
        return RGB(0, 0, 0);
    return RGB(255, 255, 255);
}

inline COLORREF GetHighlightColor()
{
    Value value = JCFG_THEME_DEF("JS_TASKBAR", "taskbar", "highlight_color", Value((int)GetFallbackOverlayColor()));
    return JValueToColor(value);
}

inline COLORREF GetHoverColor()
{
    Value value = JCFG_THEME_DEF("JS_TASKBAR", "taskbar", "hover_color", Value((int)GetFallbackOverlayColor()));
    return JValueToColor(value);
}

inline BYTE ClampAlpha(int alpha)
{
    if (alpha < 0)
        return 0;
    if (alpha > 255)
        return 255;
    return (BYTE)alpha;
}

inline BYTE GetHighlightAlpha()
{
    int alpha = TASKBAR_THEMESTYLE().compare(TEXT("light")) == 0 ? 26 : 44;
    return ClampAlpha(JCFG_THEME_VALUE("JS_TASKBAR", "taskbar", "highlight_alpha", Value(alpha)).ToInt());
}

inline BYTE GetHoverAlpha()
{
    int alpha = TASKBAR_THEMESTYLE().compare(TEXT("light")) == 0 ? 14 : 28;
    return ClampAlpha(JCFG_THEME_VALUE("JS_TASKBAR", "taskbar", "hover_alpha", Value(alpha)).ToInt());
}

inline BYTE GetIndicatorIdleAlpha()
{
    return ClampAlpha(JCFG_THEME_VALUE("JS_TASKBAR", "taskbar", "indicator_idle_alpha", Value(112)).ToInt());
}

inline BYTE GetIndicatorHotAlpha()
{
    return ClampAlpha(JCFG_THEME_VALUE("JS_TASKBAR", "taskbar", "indicator_hot_alpha", Value(172)).ToInt());
}

inline BYTE GetIndicatorActiveAlpha()
{
    return ClampAlpha(JCFG_THEME_VALUE("JS_TASKBAR", "taskbar", "indicator_active_alpha", Value(255)).ToInt());
}

inline int GetIndicatorBaseWidth()
{
    return DPI_SX(8);
}

inline int GetIndicatorHotWidth()
{
    return DPI_SX(12);
}

inline int GetIndicatorActiveWidth()
{
    return DPI_SX(20);
}

inline int GetIndicatorHeight()
{
    return DPI_SY(3);
}

inline float GetAnimationBlend()
{
    float blend = 16.0f / (float)GetAnimationDurationMs() * 3.5f;
    if (blend < 0.18f)
        blend = 0.18f;
    if (blend > 0.42f)
        blend = 0.42f;
    return blend;
}

inline float EaseTowards(float current, float target, float blend)
{
    float next = current + (target - current) * blend;
    float diff = next - target;
    if (diff < 0.0f)
        diff = -diff;

    if (diff < 0.01f)
        return target;

    return next;
}

inline int LerpInt(int from, int to, float t)
{
    return from + (int)((to - from) * t);
}

inline BYTE LerpAlpha(BYTE from, BYTE to, float t)
{
    return ClampAlpha((int)(from + (to - from) * t));
}

inline RECT DeflateRectCopy(const RECT &rc, int dx, int dy)
{
    RECT copy = rc;
    copy.left += dx;
    copy.top += dy;
    copy.right -= dx;
    copy.bottom -= dy;
    return copy;
}

inline void AddRoundedRectPath(Gdiplus::GraphicsPath &path, const RECT &rc, int radius)
{
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0)
        return;

    int diameter = radius * 2;
    if (diameter > width)
        diameter = width;
    if (diameter > height)
        diameter = height;

    if (diameter <= 1) {
        path.AddRectangle(Gdiplus::Rect(rc.left, rc.top, width, height));
        return;
    }

    path.StartFigure();
    path.AddArc((Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top, (Gdiplus::REAL)diameter, (Gdiplus::REAL)diameter, 180.0f, 90.0f);
    path.AddArc((Gdiplus::REAL)(rc.right - diameter), (Gdiplus::REAL)rc.top, (Gdiplus::REAL)diameter, (Gdiplus::REAL)diameter, 270.0f, 90.0f);
    path.AddArc((Gdiplus::REAL)(rc.right - diameter), (Gdiplus::REAL)(rc.bottom - diameter), (Gdiplus::REAL)diameter, (Gdiplus::REAL)diameter, 0.0f, 90.0f);
    path.AddArc((Gdiplus::REAL)rc.left, (Gdiplus::REAL)(rc.bottom - diameter), (Gdiplus::REAL)diameter, (Gdiplus::REAL)diameter, 90.0f, 90.0f);
    path.CloseFigure();
}

inline void FillRoundedRect(HDC hdc, const RECT &rc, int radius, COLORREF color, BYTE alpha)
{
    if (alpha == 0)
        return;

    RECT rect = rc;
    if (rect.right <= rect.left || rect.bottom <= rect.top)
        return;

    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);

    Gdiplus::GraphicsPath path;
    AddRoundedRectPath(path, rect, radius);
    Gdiplus::SolidBrush brush(Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color)));
    graphics.FillPath(&brush, &path);
}

} // namespace taskbar_draw