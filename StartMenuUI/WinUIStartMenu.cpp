//
// WinUI 3 start menu — Phase 1.1 + 1.2.
//
// Builds a Windows 11-style start menu programmatically in C++/WinRT:
// borderless transparent window with Mica/Acrylic backdrop, search box at
// top, pinned-apps grid, recommended-items list, and footer with profile
// and power buttons. Phase 1.3+ wires up real data and click handlers;
// for now the layout uses placeholder content so the visual treatment
// can be reviewed end-to-end.
//

#define NOMINMAX
#include <windows.h>
#include <ShellScalingApi.h>
#include <Microsoft.UI.Xaml.Window.h>  // IWindowNative

#include "WinUIStartMenu.h"
#include "../winui/WinUIHost.h"

#ifdef USE_WINUI3

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Composition.SystemBackdrops.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Documents.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <Lmcons.h>  // UNLEN

namespace mux   = winrt::Microsoft::UI::Xaml;
namespace muxc  = winrt::Microsoft::UI::Xaml::Controls;
namespace muxm  = winrt::Microsoft::UI::Xaml::Media;
namespace muxs  = winrt::Microsoft::UI::Xaml::Shapes;
namespace muxsb = winrt::Microsoft::UI::Composition::SystemBackdrops;
namespace muw   = winrt::Microsoft::UI::Windowing;
namespace mucomp = winrt::Microsoft::UI::Composition;
namespace foundation = winrt::Windows::Foundation;

namespace {

// -----------------------------------------------------------------------------
// Layout constants — sized to match the Windows 11 start menu reasonably
// closely. All values are logical pixels (96 DPI); WinUI 3 handles per-monitor
// scaling, the outer Win32 placement code applies its own DPI scaling for the
// HWND geometry.
// -----------------------------------------------------------------------------

constexpr int    kMenuWidth         = 640;
constexpr int    kMenuHeight        = 740;
constexpr int    kMenuMargin        = 12;   // gap between taskbar and menu
constexpr int    kMenuCornerRadius  = 8;
constexpr int    kPaddingX          = 56;
constexpr int    kSearchHeight      = 38;
constexpr int    kPinnedColumns     = 6;
constexpr int    kPinnedRows        = 3;
constexpr int    kPinnedTileSize    = 76;
constexpr int    kPinnedTileSpacing = 4;
constexpr int    kRecommendedRows   = 2;

// -----------------------------------------------------------------------------
// State.
// -----------------------------------------------------------------------------

bool                  g_initialized   = false;
bool                  g_visible       = false;
HWND                  g_hwnd          = nullptr;
mux::Window           g_window        { nullptr };
muxc::Grid            g_root_grid     { nullptr };
muxc::AutoSuggestBox  g_search_box    { nullptr };
muxsb::MicaController g_mica          { nullptr };
muxsb::DesktopAcrylicController g_acrylic { nullptr };
mux::Window::Activated_revoker g_activated_revoker;

// -----------------------------------------------------------------------------
// Helpers.
// -----------------------------------------------------------------------------

HWND GetHwnd(mux::Window const& window)
{
    HWND hwnd = nullptr;
    auto native = window.try_as<::IWindowNative>();
    if (native)
        native->get_WindowHandle(&hwnd);
    return hwnd;
}

UINT GetDpiForHwndSafe(HWND hwnd)
{
    // GetDpiForWindow is Windows 10 1607+; we already require 1809.
    UINT dpi = ::GetDpiForWindow(hwnd);
    if (dpi == 0)
        dpi = 96;
    return dpi;
}

int LogicalToPhysical(int logical, UINT dpi)
{
    return MulDiv(logical, (int)dpi, 96);
}

winrt::Windows::UI::Color RgbColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
{
    return { a, r, g, b };
}

muxm::SolidColorBrush MakeBrush(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
{
    return muxm::SolidColorBrush(RgbColor(r, g, b, a));
}

// -----------------------------------------------------------------------------
// XAML tree construction. Each builder returns the root of a section so the
// caller can assemble the final layout into a single Grid.
// -----------------------------------------------------------------------------

muxc::AutoSuggestBox BuildSearchBox()
{
    muxc::AutoSuggestBox box;
    box.PlaceholderText(L"Search for apps, settings, and documents");
    box.QueryIcon(muxc::SymbolIcon(muxc::Symbol::Find));
    box.Height((double)kSearchHeight);
    box.HorizontalAlignment(mux::HorizontalAlignment::Stretch);
    box.VerticalAlignment(mux::VerticalAlignment::Center);
    box.Margin({ (double)kPaddingX, 24, (double)kPaddingX, 0 });
    return box;
}

muxc::Border BuildPinnedTile(winrt::hstring const& glyph, winrt::hstring const& label)
{
    muxc::Grid tile_grid;
    tile_grid.RowDefinitions().Append(muxc::RowDefinition{});
    tile_grid.RowDefinitions().Append(muxc::RowDefinition{});

    muxc::FontIcon icon;
    icon.Glyph(glyph);
    icon.FontFamily(muxm::FontFamily(L"Segoe Fluent Icons,Segoe MDL2 Assets"));
    icon.FontSize(28);
    icon.HorizontalAlignment(mux::HorizontalAlignment::Center);
    icon.VerticalAlignment(mux::VerticalAlignment::Center);
    icon.Margin({ 0, 14, 0, 0 });
    muxc::Grid::SetRow(icon, 0);
    tile_grid.Children().Append(icon);

    muxc::TextBlock text;
    text.Text(label);
    text.FontSize(12);
    text.TextAlignment(mux::TextAlignment::Center);
    text.TextWrapping(mux::TextWrapping::NoWrap);
    text.TextTrimming(mux::TextTrimming::CharacterEllipsis);
    text.HorizontalAlignment(mux::HorizontalAlignment::Center);
    text.VerticalAlignment(mux::VerticalAlignment::Center);
    text.Margin({ 4, 4, 4, 8 });
    muxc::Grid::SetRow(text, 1);
    tile_grid.Children().Append(text);

    muxc::Border outer;
    outer.Width((double)kPinnedTileSize);
    outer.Height((double)kPinnedTileSize);
    outer.CornerRadius(mux::CornerRadius{ 6, 6, 6, 6 });
    outer.Background(MakeBrush(255, 255, 255, 12));
    outer.Margin({ (double)kPinnedTileSpacing, (double)kPinnedTileSpacing,
                   (double)kPinnedTileSpacing, (double)kPinnedTileSpacing });
    outer.Child(tile_grid);
    return outer;
}

muxc::StackPanel BuildSectionHeader(winrt::hstring const& title, winrt::hstring const& action)
{
    muxc::StackPanel header;
    header.Orientation(muxc::Orientation::Horizontal);
    header.Margin({ (double)kPaddingX, 24, (double)kPaddingX, 8 });

    muxc::TextBlock title_text;
    title_text.Text(title);
    title_text.FontSize(13);
    title_text.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    title_text.VerticalAlignment(mux::VerticalAlignment::Center);
    header.Children().Append(title_text);

    muxc::Button action_button;
    {
        muxc::StackPanel content;
        content.Orientation(muxc::Orientation::Horizontal);

        muxc::TextBlock action_text;
        action_text.Text(action);
        action_text.FontSize(12);
        action_text.VerticalAlignment(mux::VerticalAlignment::Center);
        content.Children().Append(action_text);

        muxc::FontIcon caret;
        caret.Glyph(L"");
        caret.FontFamily(muxm::FontFamily(L"Segoe Fluent Icons,Segoe MDL2 Assets"));
        caret.FontSize(10);
        caret.Margin({ 6, 0, 0, 0 });
        caret.VerticalAlignment(mux::VerticalAlignment::Center);
        content.Children().Append(caret);

        action_button.Content(content);
    }
    action_button.Background(nullptr);
    action_button.BorderThickness({ 0, 0, 0, 0 });
    action_button.Padding({ 10, 4, 10, 4 });
    action_button.HorizontalAlignment(mux::HorizontalAlignment::Right);
    action_button.Margin({ 12, 0, 0, 0 });

    muxc::Grid header_grid;
    header_grid.ColumnDefinitions().Append(muxc::ColumnDefinition{});
    {
        muxc::ColumnDefinition cd;
        cd.Width(mux::GridLengthHelper::FromValueAndType(1.0, mux::GridUnitType::Star));
        header_grid.ColumnDefinitions().Append(cd);
    }
    muxc::ColumnDefinition autoCol;
    autoCol.Width(mux::GridLengthHelper::Auto());
    header_grid.ColumnDefinitions().Append(autoCol);

    muxc::Grid::SetColumn(title_text, 0);
    muxc::Grid::SetColumn(action_button, 1);
    header_grid.Children().Append(title_text);
    header_grid.Children().Append(action_button);
    header_grid.Margin({ (double)kPaddingX, 24, (double)kPaddingX, 8 });
    return [&] {
        muxc::StackPanel sp;
        sp.Children().Append(header_grid);
        return sp;
    }();
}

muxc::ItemsRepeater BuildPinnedGrid()
{
    // Phase 1.2: placeholder tiles. Phase 1.3 will replace with real apps
    // bound through an ItemsSource.
    muxc::ItemsRepeater repeater;
    muxc::ItemsRepeaterScrollHost scroll_host;

    // ItemsRepeater needs a Layout; UniformGridLayout gives us a fixed-column
    // grid that wraps on row breaks — exactly the Windows 11 pinned look.
    muxc::UniformGridLayout layout;
    layout.MinItemWidth((double)(kPinnedTileSize + kPinnedTileSpacing * 2));
    layout.MinItemHeight((double)(kPinnedTileSize + kPinnedTileSpacing * 2));
    layout.MaximumRowsOrColumns(kPinnedColumns);
    layout.ItemsStretch(muxc::UniformGridLayoutItemsStretch::None);
    layout.Orientation(muxc::Orientation::Horizontal);
    repeater.Layout(layout);

    // Static placeholder content. The 18 tiles fill the 6x3 default Pinned
    // section. Phase 1.3 swaps this for ObservableCollection<AppEntry>.
    auto items = winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>();
    static const wchar_t* kGlyphs[] = {
        L"", L"", L"", L"", L"", L"",
        L"", L"", L"", L"", L"", L"",
        L"", L"", L"", L"", L"", L"",
    };
    static const wchar_t* kLabels[] = {
        L"Search",  L"Mail",     L"Photos",   L"Find",    L"Calculator", L"Camera",
        L"Help",    L"Edit",     L"Settings", L"Calendar",L"Documents",  L"Music",
        L"Movies",  L"Games",    L"Phone",    L"Maps",    L"World",      L"Info",
    };
    for (size_t i = 0; i < std::size(kGlyphs); ++i) {
        items.Append(BuildPinnedTile(kGlyphs[i], kLabels[i]));
    }
    repeater.ItemsSource(items);

    return repeater;
}

muxc::ListView BuildRecommendedList()
{
    muxc::ListView list;
    list.SelectionMode(muxc::ListViewSelectionMode::None);
    list.IsItemClickEnabled(true);
    list.HorizontalAlignment(mux::HorizontalAlignment::Stretch);
    list.Padding({ 0, 0, 0, 0 });

    // Placeholder rows.
    static const wchar_t* kTitles[] = {
        L"Quick Start Guide.pdf",  L"Vacation photos",
        L"Project notes",          L"Recent download",
    };
    static const wchar_t* kSubs[] = {
        L"3 hours ago",  L"Yesterday",
        L"Last week",    L"Today",
    };
    for (size_t i = 0; i < std::size(kTitles); ++i) {
        muxc::Grid row;
        row.ColumnDefinitions().Append(muxc::ColumnDefinition{});
        {
            muxc::ColumnDefinition cd;
            cd.Width(mux::GridLengthHelper::FromValueAndType(1.0, mux::GridUnitType::Star));
            row.ColumnDefinitions().Append(cd);
        }

        muxc::FontIcon icon;
        icon.Glyph(L"");
        icon.FontFamily(muxm::FontFamily(L"Segoe Fluent Icons,Segoe MDL2 Assets"));
        icon.FontSize(20);
        icon.Width(36);
        icon.Height(36);
        icon.VerticalAlignment(mux::VerticalAlignment::Center);
        muxc::Grid::SetColumn(icon, 0);
        row.Children().Append(icon);

        muxc::StackPanel text_stack;
        text_stack.Orientation(muxc::Orientation::Vertical);
        text_stack.Margin({ 12, 0, 0, 0 });
        text_stack.VerticalAlignment(mux::VerticalAlignment::Center);

        muxc::TextBlock title;
        title.Text(kTitles[i]);
        title.FontSize(13);
        title.TextTrimming(mux::TextTrimming::CharacterEllipsis);
        text_stack.Children().Append(title);

        muxc::TextBlock subtitle;
        subtitle.Text(kSubs[i]);
        subtitle.FontSize(11);
        subtitle.Opacity(0.65);
        text_stack.Children().Append(subtitle);

        muxc::Grid::SetColumn(text_stack, 1);
        row.Children().Append(text_stack);
        row.Padding({ 8, 6, 8, 6 });

        list.Items().Append(row);
    }

    return list;
}

muxc::Grid BuildFooter()
{
    muxc::Grid footer;
    footer.Height(56);
    footer.Background(MakeBrush(0, 0, 0, 24));
    footer.Padding({ (double)kPaddingX, 0, (double)kPaddingX, 0 });

    muxc::ColumnDefinition profile_col;
    profile_col.Width(mux::GridLengthHelper::FromValueAndType(1.0, mux::GridUnitType::Star));
    footer.ColumnDefinitions().Append(profile_col);

    muxc::ColumnDefinition power_col;
    power_col.Width(mux::GridLengthHelper::Auto());
    footer.ColumnDefinitions().Append(power_col);

    // Profile button: circular avatar + display name.
    muxc::Button profile_btn;
    {
        muxc::StackPanel sp;
        sp.Orientation(muxc::Orientation::Horizontal);

        muxs::Ellipse avatar;
        avatar.Width(28);
        avatar.Height(28);
        avatar.Fill(MakeBrush(80, 130, 220));
        sp.Children().Append(avatar);

        TCHAR user_name[UNLEN + 1] = { 0 };
        DWORD user_size = (DWORD)std::size(user_name);
        ::GetUserName(user_name, &user_size);

        muxc::TextBlock name_text;
        name_text.Text(winrt::hstring(user_name[0] ? user_name : TEXT("User")));
        name_text.FontSize(13);
        name_text.VerticalAlignment(mux::VerticalAlignment::Center);
        name_text.Margin({ 12, 0, 0, 0 });
        sp.Children().Append(name_text);

        profile_btn.Content(sp);
    }
    profile_btn.Background(nullptr);
    profile_btn.BorderThickness({ 0, 0, 0, 0 });
    profile_btn.Padding({ 6, 4, 12, 4 });
    profile_btn.VerticalAlignment(mux::VerticalAlignment::Center);
    muxc::Grid::SetColumn(profile_btn, 0);
    footer.Children().Append(profile_btn);

    // Power button: just the glyph.
    muxc::Button power_btn;
    {
        muxc::FontIcon power_icon;
        power_icon.Glyph(L"");  // Power
        power_icon.FontFamily(muxm::FontFamily(L"Segoe Fluent Icons,Segoe MDL2 Assets"));
        power_icon.FontSize(16);
        power_btn.Content(power_icon);
    }
    power_btn.Background(nullptr);
    power_btn.BorderThickness({ 0, 0, 0, 0 });
    power_btn.Padding({ 12, 8, 12, 8 });
    power_btn.VerticalAlignment(mux::VerticalAlignment::Center);
    muxc::Grid::SetColumn(power_btn, 1);
    footer.Children().Append(power_btn);

    return footer;
}

muxc::Grid BuildRoot()
{
    muxc::Grid root;
    root.RowDefinitions().Append([] { muxc::RowDefinition rd; rd.Height(mux::GridLengthHelper::Auto()); return rd; }());                           // search
    root.RowDefinitions().Append([] { muxc::RowDefinition rd; rd.Height(mux::GridLengthHelper::Auto()); return rd; }());                           // pinned header
    root.RowDefinitions().Append([] { muxc::RowDefinition rd; rd.Height(mux::GridLengthHelper::Auto()); return rd; }());                           // pinned grid
    root.RowDefinitions().Append([] { muxc::RowDefinition rd; rd.Height(mux::GridLengthHelper::Auto()); return rd; }());                           // recommended header
    root.RowDefinitions().Append([] { muxc::RowDefinition rd; rd.Height(mux::GridLengthHelper::FromValueAndType(1.0, mux::GridUnitType::Star)); return rd; }());  // recommended list
    root.RowDefinitions().Append([] { muxc::RowDefinition rd; rd.Height(mux::GridLengthHelper::Auto()); return rd; }());                           // footer

    g_search_box = BuildSearchBox();
    muxc::Grid::SetRow(g_search_box, 0);
    root.Children().Append(g_search_box);

    auto pinned_header = BuildSectionHeader(L"Pinned", L"All apps");
    muxc::Grid::SetRow(pinned_header, 1);
    root.Children().Append(pinned_header);

    auto pinned_grid = BuildPinnedGrid();
    auto pinned_host = muxc::ScrollViewer{};
    pinned_host.HorizontalScrollMode(muxc::ScrollMode::Disabled);
    pinned_host.VerticalScrollMode(muxc::ScrollMode::Auto);
    pinned_host.VerticalScrollBarVisibility(muxc::ScrollBarVisibility::Auto);
    pinned_host.Content(pinned_grid);
    pinned_host.Margin({ (double)(kPaddingX - kPinnedTileSpacing), 0,
                         (double)(kPaddingX - kPinnedTileSpacing), 12 });
    pinned_host.MaxHeight((double)(kPinnedRows * (kPinnedTileSize + kPinnedTileSpacing * 2) + 8));
    muxc::Grid::SetRow(pinned_host, 2);
    root.Children().Append(pinned_host);

    auto recommended_header = BuildSectionHeader(L"Recommended", L"More");
    muxc::Grid::SetRow(recommended_header, 3);
    root.Children().Append(recommended_header);

    auto recommended_list = BuildRecommendedList();
    recommended_list.Margin({ (double)kPaddingX, 0, (double)kPaddingX, 12 });
    muxc::Grid::SetRow(recommended_list, 4);
    root.Children().Append(recommended_list);

    auto footer = BuildFooter();
    muxc::Grid::SetRow(footer, 5);
    root.Children().Append(footer);

    return root;
}

// -----------------------------------------------------------------------------
// Window plumbing.
// -----------------------------------------------------------------------------

void RemoveWindowChrome(HWND hwnd)
{
    LONG_PTR style    = ::GetWindowLongPtr(hwnd, GWL_STYLE);
    LONG_PTR ex_style = ::GetWindowLongPtr(hwnd, GWL_EXSTYLE);

    // Strip the title bar and resize border. Keep WS_POPUP so the window
    // doesn't behave like a child of the desktop.
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZE | WS_MAXIMIZE | WS_SYSMENU);
    style |=  WS_POPUP;
    ::SetWindowLongPtr(hwnd, GWL_STYLE, style);

    // WS_EX_TOOLWINDOW hides the window from Alt+Tab and the taskbar.
    // We deliberately do NOT set WS_EX_NOACTIVATE — the menu must accept
    // focus so the WindowActivated/Deactivated events fire on click-away,
    // and so the search box can receive keyboard input.
    ex_style |= WS_EX_TOOLWINDOW;
    ex_style &= ~(WS_EX_APPWINDOW);
    ::SetWindowLongPtr(hwnd, GWL_EXSTYLE, ex_style);

    ::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

void ApplyBackdrop()
{
    if (!g_window)
        return;

    // Mica is the Windows 11 system backdrop; fall back to acrylic on
    // Windows 10. Both controllers need a SystemBackdropConfiguration to
    // know about theme + activation state.
    if (muxsb::MicaController::IsSupported()) {
        g_mica = muxsb::MicaController();
        g_mica.SetSystemBackdropConfiguration(muxsb::SystemBackdropConfiguration());
        g_mica.AddSystemBackdropTarget(g_window.try_as<mucomp::ICompositionSupportsSystemBackdrop>());
    }
    else if (muxsb::DesktopAcrylicController::IsSupported()) {
        g_acrylic = muxsb::DesktopAcrylicController();
        g_acrylic.SetSystemBackdropConfiguration(muxsb::SystemBackdropConfiguration());
        g_acrylic.AddSystemBackdropTarget(g_window.try_as<mucomp::ICompositionSupportsSystemBackdrop>());
    }
}

void OnActivated(foundation::IInspectable const&, mux::WindowActivatedEventArgs const& args)
{
    if (!g_visible)
        return;

    if (args.WindowActivationState() == mux::WindowActivationState::Deactivated) {
        // Click-away dismiss. Hide via the public API so any future
        // animation/teardown stays in one place.
        WinUIStartMenu_Hide();
    }
}

bool EnsureWindowCreated()
{
    if (g_window)
        return true;

    if (!winui::WinUIHost::EnsureReady())
        return false;

    try {
        g_window = mux::Window();

        // Build XAML tree once; we only swap visibility on subsequent shows.
        g_root_grid = BuildRoot();
        g_window.Content(g_root_grid);

        g_hwnd = GetHwnd(g_window);
        if (!g_hwnd)
            return false;

        // Treat the WinUI 3 window as a popup chrome-less surface. The
        // start menu anchors itself; the user never resizes or minimizes.
        RemoveWindowChrome(g_hwnd);

        ApplyBackdrop();

        // Track Activated so we hide on focus loss (= click-away).
        g_activated_revoker = g_window.Activated(winrt::auto_revoke, OnActivated);
    }
    catch (winrt::hresult_error const&) {
        g_window    = nullptr;
        g_root_grid = nullptr;
        g_hwnd      = nullptr;
        return false;
    }

    return true;
}

void GetMonitorWorkAreaForButton(HWND hwnd_anchor, RECT* work)
{
    HMONITOR mon = ::MonitorFromWindow(hwnd_anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info = { sizeof(MONITORINFO) };
    if (mon && ::GetMonitorInfo(mon, &info))
        *work = info.rcWork;
    else
        ::SystemParametersInfo(SPI_GETWORKAREA, 0, work, 0);
}

void PositionWindow(HWND hwnd_start_button, HWND hwnd_taskbar)
{
    if (!g_hwnd)
        return;

    UINT dpi = GetDpiForHwndSafe(hwnd_start_button ? hwnd_start_button : hwnd_taskbar);

    int width_px  = LogicalToPhysical(kMenuWidth,  dpi);
    int height_px = LogicalToPhysical(kMenuHeight, dpi);
    int margin_px = LogicalToPhysical(kMenuMargin, dpi);

    RECT work = {};
    GetMonitorWorkAreaForButton(hwnd_start_button ? hwnd_start_button : hwnd_taskbar, &work);

    // Default: center horizontally over the start button if we know it,
    // otherwise center over the work area. Vertical placement is just
    // above the taskbar with a small gap.
    int target_x;
    int target_y;

    RECT btn_rect = {};
    bool have_button_rect = (hwnd_start_button && ::GetWindowRect(hwnd_start_button, &btn_rect));

    if (have_button_rect) {
        int btn_center_x = (btn_rect.left + btn_rect.right) / 2;
        target_x = btn_center_x - width_px / 2;
    }
    else {
        target_x = work.left + (work.right - work.left - width_px) / 2;
    }

    // Clamp horizontally to the work area.
    if (target_x < work.left + margin_px)
        target_x = work.left + margin_px;
    if (target_x + width_px > work.right - margin_px)
        target_x = work.right - width_px - margin_px;

    RECT tb_rect = {};
    bool have_tb_rect = (hwnd_taskbar && ::GetWindowRect(hwnd_taskbar, &tb_rect));

    if (have_tb_rect) {
        // Anchor to the top edge of the taskbar (taskbar at bottom is the
        // common case; when the taskbar is at the top or side we anchor to
        // its inside edge).
        int tb_height = tb_rect.bottom - tb_rect.top;
        int tb_width  = tb_rect.right  - tb_rect.left;

        if (tb_height < tb_width) {
            // Horizontal taskbar.
            if (tb_rect.top <= work.top + 8) {
                // Top: open below it.
                target_y = tb_rect.bottom + margin_px;
            }
            else {
                // Bottom: open above it.
                target_y = tb_rect.top - height_px - margin_px;
            }
        }
        else {
            // Vertical taskbar — open at the inside edge, vertically centered
            // on the work area.
            if (tb_rect.left <= work.left + 8) {
                target_x = tb_rect.right + margin_px;
            }
            else {
                target_x = tb_rect.left - width_px - margin_px;
            }
            target_y = work.top + (work.bottom - work.top - height_px) / 2;
        }
    }
    else {
        target_y = work.bottom - height_px - margin_px;
    }

    // Clamp vertically.
    if (target_y < work.top + margin_px)
        target_y = work.top + margin_px;
    if (target_y + height_px > work.bottom - margin_px)
        target_y = work.bottom - height_px - margin_px;

    ::SetWindowPos(g_hwnd, HWND_TOPMOST, target_x, target_y, width_px, height_px,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

} // namespace

// -----------------------------------------------------------------------------
// C-style API.
// -----------------------------------------------------------------------------

extern "C" int WinUIStartMenu_Initialize(void)
{
    if (g_initialized)
        return 1;
    if (!winui::WinUIHost::EnsureReady())
        return 0;

    g_initialized = true;
    return 1;
}

extern "C" int WinUIStartMenu_Show(HWND hwnd_start_button, HWND hwnd_taskbar)
{
    if (!winui::WinUIHost::EnsureReady())
        return 0;
    if (!EnsureWindowCreated())
        return 0;

    PositionWindow(hwnd_start_button, hwnd_taskbar);
    g_window.Activate();
    g_visible = true;

    // Move focus into the search box once the window is up; matches the
    // Windows 11 behavior where typing immediately filters the menu.
    if (g_search_box)
        g_search_box.Focus(mux::FocusState::Programmatic);

    // Ensure we're on top of the taskbar.
    ::SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ::SetForegroundWindow(g_hwnd);

    return 1;
}

extern "C" void WinUIStartMenu_Hide(void)
{
    if (!g_visible || !g_hwnd)
        return;

    ::ShowWindow(g_hwnd, SW_HIDE);
    g_visible = false;
}

extern "C" int WinUIStartMenu_IsVisible(void)
{
    return g_visible ? 1 : 0;
}

extern "C" HWND WinUIStartMenu_GetHwnd(void)
{
    return g_hwnd;
}

extern "C" void WinUIStartMenu_Shutdown(void)
{
    if (g_visible)
        WinUIStartMenu_Hide();

    g_activated_revoker.revoke();

    if (g_mica) {
        g_mica.Close();
        g_mica = nullptr;
    }
    if (g_acrylic) {
        g_acrylic.Close();
        g_acrylic = nullptr;
    }

    g_root_grid = nullptr;

    if (g_window) {
        g_window.Close();
        g_window = nullptr;
    }

    g_hwnd        = nullptr;
    g_initialized = false;
}

#endif // USE_WINUI3
