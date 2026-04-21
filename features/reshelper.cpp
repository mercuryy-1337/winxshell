
#include <shlwapi.h>
#include <gdiplus.h>
#include "reshelper.h"
#include "../resource.h"
#include "../globals.h"
#include "../jconfig/jcfg.h"

extern ExplorerGlobals g_Globals;

Icon::Icon()
    : _id(ICID_UNKNOWN),
    _itype(IT_STATIC),
    _hicon(0)
{
}

Icon::Icon(ICON_ID id, UINT nid)    //, int cx, int cy
    : _id(id),
    _itype(IT_STATIC),
    _hicon(ResIcon(nid))    // ResIconEx(nid, cx, cy)
{
}

Icon::Icon(ICON_ID id, UINT nid, int icon_size)
    : _id(id),
    _itype(IT_STATIC),
    _hicon(ResIconEx(nid, icon_size, icon_size))
{
}

Icon::Icon(ICON_TYPE itype, int id, HICON hIcon)
    : _id((ICON_ID)id),
    _itype(itype),
    _hicon(hIcon)
{
}

Icon::Icon(ICON_TYPE itype, int id, int sys_idx)
    : _id((ICON_ID)id),
    _itype(itype),
    _sys_idx(sys_idx)
{
}

void Icon::draw(HDC hdc, int x, int y, int cx, int cy, COLORREF bk_color, HBRUSH bk_brush) const
{
    if (_itype == IT_SYSCACHE)
        ImageList_DrawEx(g_Globals._icon_cache.get_sys_imagelist(), _sys_idx, hdc, x, y, cx, cy, bk_color, CLR_DEFAULT, ILD_NORMAL);
    else
        DrawIconEx(hdc, x, y, _hicon, cx, cy, 0, bk_brush, DI_NORMAL);
}

HBITMAP    Icon::create_bitmap(COLORREF bk_color, HBRUSH hbrBkgnd, HDC hdc_wnd, int icon_size) const
{
    RECT rect = { 0, 0, icon_size, icon_size };
    return create_bitmap(bk_color, hbrBkgnd, hdc_wnd, icon_size, rect);
}

HBITMAP    Icon::create_bitmap(COLORREF bk_color, HBRUSH hbrBkgnd, HDC hdc_wnd, int icon_size, RECT rect) const
{
    if (_itype == IT_SYSCACHE) {
        HIMAGELIST himl = g_Globals._icon_cache.get_sys_imagelist();

        HICON hIcon = ImageList_GetIcon(himl, _sys_idx, ILD_NORMAL);
        if (!hIcon)
            return 0;

        HBITMAP hbmp = create_bitmap_from_icon(hIcon, hbrBkgnd, hdc_wnd, icon_size, rect);
        DestroyIcon(hIcon);
        return hbmp;
    } else
        return create_bitmap_from_icon(_hicon, hbrBkgnd, hdc_wnd, icon_size, rect);
}

int Icon::add_to_imagelist(HIMAGELIST himl, HDC hdc_wnd, COLORREF bk_color, HBRUSH bk_brush) const
{
    int ret;

    if (_itype == IT_SYSCACHE) {
        HIMAGELIST himl = g_Globals._icon_cache.get_sys_imagelist();

        int cx, cy;
        ImageList_GetIconSize(himl, &cx, &cy);

        HBITMAP hbmp = CreateCompatibleBitmap(hdc_wnd, cx, cy);
        HDC hdc = CreateCompatibleDC(hdc_wnd);
        HBITMAP hbmp_old = SelectBitmap(hdc, hbmp);
        ImageList_DrawEx(himl, _sys_idx, hdc, 0, 0, cx, cy, bk_color, CLR_DEFAULT, ILD_NORMAL);
        SelectBitmap(hdc, hbmp_old);
        DeleteDC(hdc);

        ret = ImageList_Add(himl, hbmp, 0);

        DeleteObject(hbmp);
    } else
        ret = ImageList_AddAlphaIcon(himl, _hicon, bk_brush, hdc_wnd);

    return ret;
}

static bool GetIconContentBounds(HICON hIcon, Gdiplus::Bitmap **bitmap_out, Gdiplus::Rect *bounds_out)
{
    Gdiplus::Bitmap *source = Gdiplus::Bitmap::FromHICON(hIcon);
    if (!source || source->GetLastStatus() != Gdiplus::Ok) {
        delete source;
        return false;
    }

    UINT width = source->GetWidth();
    UINT height = source->GetHeight();
    if (width == 0 || height == 0) {
        delete source;
        return false;
    }

    Gdiplus::Bitmap *raster = new Gdiplus::Bitmap(width, height, PixelFormat32bppARGB);
    if (!raster || raster->GetLastStatus() != Gdiplus::Ok) {
        delete raster;
        delete source;
        return false;
    }

    {
        Gdiplus::Graphics graphics(raster);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
        graphics.DrawImage(source, 0, 0, width, height);
    }
    delete source;

    Gdiplus::BitmapData data;
    Gdiplus::Rect lock_rect(0, 0, width, height);
    if (raster->LockBits(&lock_rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
        delete raster;
        return false;
    }

    int left = (int)width;
    int top = (int)height;
    int right = -1;
    int bottom = -1;

    for (UINT y = 0; y < height; ++y) {
        const Gdiplus::ARGB *row = (const Gdiplus::ARGB *)((const BYTE *)data.Scan0 + y * data.Stride);
        for (UINT x = 0; x < width; ++x) {
            Gdiplus::ARGB pixel = row[x];
            BYTE alpha = (BYTE)((pixel >> 24) & 0xFF);
            if (alpha > 8 || (alpha == 0 && (pixel & 0x00FFFFFF) != 0)) {
                if ((int)x < left)
                    left = (int)x;
                if ((int)x > right)
                    right = (int)x;
                if ((int)y < top)
                    top = (int)y;
                if ((int)y > bottom)
                    bottom = (int)y;
            }
        }
    }

    raster->UnlockBits(&data);

    if (right < left || bottom < top) {
        delete raster;
        return false;
    }

    *bounds_out = Gdiplus::Rect(left, top, right - left + 1, bottom - top + 1);
    *bitmap_out = raster;
    return true;
}

HBITMAP create_bitmap_from_icon(HICON hIcon, HBRUSH hbrush_bkgnd, HDC hdc_wnd, int icon_size)
{
    RECT rect = { 0, 0, icon_size, icon_size };
    return create_bitmap_from_icon(hIcon, hbrush_bkgnd, hdc_wnd, icon_size, rect);
}

HBITMAP create_small_bitmap_from_icon(HICON hIcon, HBRUSH hbrush_bkgnd, HDC hdc_wnd)
{
    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);
    HBITMAP hbmp = CreateCompatibleBitmap(hdc_wnd, cx, cy);

    MemCanvas canvas;
    BitmapSelection sel(canvas, hbmp);

    RECT rect = { 0, 0, cx, cy };
    FillRect(canvas, &rect, hbrush_bkgnd);

    DrawIconEx(canvas, 0, 0, hIcon, cx, cy, 0, hbrush_bkgnd, DI_NORMAL);

    return hbmp;
}

static HBITMAP CreateAlphaBitmap(int width, int height, void **bits_out)
{
    if (bits_out)
        *bits_out = NULL;

    if (width <= 0 || height <= 0)
        return 0;

    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    return CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, bits_out, NULL, 0);
}

HBITMAP create_bitmap_from_icon(HICON hIcon, HBRUSH hbrush_bkgnd, HDC hdc_wnd, int icon_size, RECT rect)
{
    int w = rect.right;
    int h = rect.bottom;

    int draw_width = icon_size;
    int draw_height = icon_size;
    if (draw_width > w)
        draw_width = w;
    if (draw_height > h)
        draw_height = h;

    int draw_x = rect.left + (w - draw_width) / 2;
    int draw_y = rect.top + (h - draw_height) / 2;

    Gdiplus::Bitmap *icon_bitmap = NULL;
    Gdiplus::Rect content_bounds;

    if (!hbrush_bkgnd) {
        void *dib_bits = NULL;
        HBITMAP hbmp = CreateAlphaBitmap(w, h, &dib_bits);
        if (!hbmp || !dib_bits) {
            if (hbmp)
                DeleteObject(hbmp);
            return 0;
        }

        ZeroMemory(dib_bits, (size_t)w * (size_t)h * 4);

        MemCanvas canvas;
        BitmapSelection sel(canvas, hbmp);

        if (GetIconContentBounds(hIcon, &icon_bitmap, &content_bounds)) {
            Gdiplus::Graphics graphics(canvas);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
            graphics.DrawImage(icon_bitmap,
                Gdiplus::Rect(draw_x, draw_y, draw_width, draw_height),
                content_bounds.X,
                content_bounds.Y,
                content_bounds.Width,
                content_bounds.Height,
                Gdiplus::UnitPixel);
            delete icon_bitmap;
        } else {
            DrawIconEx(canvas, draw_x, draw_y, hIcon, draw_width, draw_height, 0, NULL, DI_NORMAL);
        }

        return hbmp;
    }

    HBITMAP hbmp = CreateCompatibleBitmap(hdc_wnd, w, h);

    MemCanvas canvas;
    BitmapSelection sel(canvas, hbmp);

    RECT rect2 = { 0, 0, w, h };
    FillRect(canvas, &rect2, hbrush_bkgnd);

    if (GetIconContentBounds(hIcon, &icon_bitmap, &content_bounds)) {
        Gdiplus::Graphics graphics(canvas);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
        graphics.DrawImage(icon_bitmap,
            Gdiplus::Rect(draw_x, draw_y, draw_width, draw_height),
            content_bounds.X,
            content_bounds.Y,
            content_bounds.Width,
            content_bounds.Height,
            Gdiplus::UnitPixel);
        delete icon_bitmap;
    } else {
        DrawIconEx(canvas, draw_x, draw_y, hIcon, draw_width, draw_height, 0, hbrush_bkgnd, DI_NORMAL);
    }

    return hbmp;
}

BOOL draw_icon_high_quality(HDC hdc, HICON hIcon, const RECT &rect)
{
    if (!hdc || !hIcon)
        return FALSE;

    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0)
        return FALSE;

    Gdiplus::Bitmap *icon_bitmap = NULL;
    Gdiplus::Rect content_bounds;
    if (GetIconContentBounds(hIcon, &icon_bitmap, &content_bounds)) {
        int draw_width = width;
        int draw_height = height;
        if (content_bounds.Width > 0 && content_bounds.Height > 0) {
            if ((long long)content_bounds.Width * draw_height > (long long)content_bounds.Height * draw_width) {
                draw_height = max(1, MulDiv(draw_width, content_bounds.Height, content_bounds.Width));
            } else {
                draw_width = max(1, MulDiv(draw_height, content_bounds.Width, content_bounds.Height));
            }
        }

        int draw_x = rect.left + (width - draw_width) / 2;
        int draw_y = rect.top + (height - draw_height) / 2;

        int sample_scale = 2;
        int sampled_width = max(1, draw_width * sample_scale);
        int sampled_height = max(1, draw_height * sample_scale);
        Gdiplus::Bitmap sampled(sampled_width, sampled_height, PixelFormat32bppPARGB);
        if (sampled.GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Graphics sampled_graphics(&sampled);
            sampled_graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
            sampled_graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            sampled_graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
            sampled_graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
            sampled_graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
            sampled_graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
            sampled_graphics.DrawImage(icon_bitmap,
                Gdiplus::Rect(0, 0, sampled_width, sampled_height),
                content_bounds.X,
                content_bounds.Y,
                content_bounds.Width,
                content_bounds.Height,
                Gdiplus::UnitPixel);

            Gdiplus::Graphics graphics(hdc);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
            graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
            graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
            graphics.DrawImage(&sampled,
                Gdiplus::Rect(draw_x, draw_y, draw_width, draw_height),
                0,
                0,
                sampled_width,
                sampled_height,
                Gdiplus::UnitPixel);
        } else {
            Gdiplus::Graphics graphics(hdc);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
            graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
            graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
            graphics.DrawImage(icon_bitmap,
                Gdiplus::Rect(draw_x, draw_y, draw_width, draw_height),
                content_bounds.X,
                content_bounds.Y,
                content_bounds.Width,
                content_bounds.Height,
                Gdiplus::UnitPixel);
        }
        delete icon_bitmap;
        return TRUE;
    }

    SIZE icon_size = { width, height };
    ICONINFO icon_info = { 0 };
    if (GetIconInfo(hIcon, &icon_info)) {
        BITMAP bmp = { 0 };
        if (icon_info.hbmColor && GetObject(icon_info.hbmColor, sizeof(bmp), &bmp) == sizeof(BITMAP)) {
            icon_size.cx = bmp.bmWidth;
            icon_size.cy = bmp.bmHeight;
        } else if (icon_info.hbmMask && GetObject(icon_info.hbmMask, sizeof(bmp), &bmp) == sizeof(BITMAP)) {
            icon_size.cx = bmp.bmWidth;
            icon_size.cy = bmp.bmHeight / 2;
        }

        if (icon_info.hbmColor)
            DeleteObject(icon_info.hbmColor);
        if (icon_info.hbmMask)
            DeleteObject(icon_info.hbmMask);
    }

    int draw_width = width;
    int draw_height = height;
    if (icon_size.cx > 0 && icon_size.cy > 0) {
        if ((long long)icon_size.cx * draw_height > (long long)icon_size.cy * draw_width) {
            draw_height = max(1, MulDiv(draw_width, icon_size.cy, icon_size.cx));
        } else {
            draw_width = max(1, MulDiv(draw_height, icon_size.cx, icon_size.cy));
        }
    }

    int draw_x = rect.left + (width - draw_width) / 2;
    int draw_y = rect.top + (height - draw_height) / 2;
    return DrawIconEx(hdc, draw_x, draw_y, hIcon, draw_width, draw_height, 0, NULL, DI_NORMAL);
}


int ImageList_AddAlphaIcon(HIMAGELIST himl, HICON hIcon, HBRUSH hbrush_bkgnd, HDC hdc_wnd)
{
    HBITMAP hbmp = create_bitmap_from_icon(hIcon, hbrush_bkgnd, hdc_wnd);

    int ret = ImageList_Add(himl, hbmp, 0);

    DeleteObject(hbmp);

    return ret;
}


int IconCache::s_next_id = ICID_DYNAMIC;

typedef map<int, Icon> IconMap;
void init_trayicons(IconMap &icons,int id, int res = 0) {
    static int diff_id = 0;
    if (res != 0) diff_id = res - id;
    icons[id] = Icon(IT_STATIC, id, SmallIcon(id + diff_id));
}

void IconCache::init()
{
    int icon_size = STARTMENUROOT_ICON_SIZE;

    _icons[ICID_NONE] = Icon(IT_STATIC, ICID_NONE, (HICON)0);

    _icons[ICID_FOLDER] = Icon(ICID_FOLDER, IDI_FOLDER);
    //_icons[ICID_DOCUMENT]    = Icon(ICID_DOCUMENT,    IDI_DOCUMENT);
    _icons[ICID_EXPLORER] = Icon(ICID_EXPLORER, IDI_EXPLORER);
    //_icons[ICID_APP]        = Icon(ICID_APP,        IDI_APPICON);

    _icons[ICID_CONFIG] = Icon(ICID_CONFIG, IDI_CONFIG, icon_size);
    _icons[ICID_DOCUMENTS] = Icon(ICID_DOCUMENTS, IDI_DOCUMENTS, icon_size);
    _icons[ICID_FAVORITES] = Icon(ICID_FAVORITES, IDI_FAVORITES, icon_size);
    _icons[ICID_INFO] = Icon(ICID_INFO, IDI_INFO, icon_size);
    _icons[ICID_APPS] = Icon(ICID_APPS, IDI_APPS, icon_size);
    _icons[ICID_SEARCH] = Icon(ICID_SEARCH, IDI_SEARCH, icon_size);
    _icons[ICID_ACTION] = Icon(ICID_ACTION, IDI_ACTION, icon_size);
    _icons[ICID_SEARCH_DOC] = Icon(ICID_SEARCH_DOC, IDI_SEARCH_DOC, icon_size);
    _icons[ICID_PRINTER] = Icon(ICID_PRINTER, IDI_PRINTER, icon_size);
    _icons[ICID_NETWORK] = Icon(ICID_NETWORK, IDI_NETWORK, icon_size);
    _icons[ICID_COMPUTER] = Icon(ICID_COMPUTER, IDI_COMPUTER, icon_size);
    _icons[ICID_LOGOFF] = Icon(ICID_LOGOFF, IDI_LOGOFF, icon_size);
    _icons[ICID_SHUTDOWN] = Icon(ICID_SHUTDOWN, IDI_SHUTDOWN, icon_size);
    _icons[ICID_TERMINATE] = Icon(ICID_TERMINATE, IDI_TERMINATE, icon_size);
    _icons[ICID_RESTART] = Icon(ICID_RESTART, IDI_RESTART, icon_size);
    _icons[ICID_BOOKMARK] = Icon(ICID_BOOKMARK, IDI_DOT_TRANS, icon_size);
    _icons[ICID_MINIMIZE] = Icon(ICID_MINIMIZE, IDI_MINIMIZE, icon_size);
    _icons[ICID_CONTROLPAN] = Icon(ICID_CONTROLPAN, IDI_CONTROLPAN, icon_size);
    _icons[ICID_DESKSETTING] = Icon(ICID_DESKSETTING, IDI_DESKSETTING, icon_size);
    _icons[ICID_NETCONNS] = Icon(ICID_NETCONNS, IDI_NETCONNS, icon_size);
    _icons[ICID_ADMIN] = Icon(ICID_ADMIN, IDI_ADMIN, icon_size);
    _icons[ICID_RECENT] = Icon(ICID_RECENT, IDI_RECENT, icon_size);

    init_trayicons(_icons, ICID_TRAY_SND_NONE, IDI_TRAY_SND_NONE);
    int i = ICID_TRAY_SND_NONE + 1;
    for (; i <= ICID_TRAY_DARK_SND_LARGE; i++) init_trayicons(_icons, i);

    init_trayicons(_icons, ICID_TRAY_NET_WIRED_DIS, IDI_TRAY_NET_WIRED_DIS);
    i = ICID_TRAY_NET_WIRED_DIS + 1;
    for (; i <= ICID_TRAY_DARK_NET_SIGNAL_QUARTER4; i++) init_trayicons(_icons, i);

    /*
    TCHAR sysPathBuff[MAX_PATH] = { 0 };
    GetWindowsDirectory(sysPathBuff, MAX_PATH);
    String sPath = sysPathBuff;
    sPath.append(_T("\\System32\\cmd.exe"));
    _icons[ICID_CMDEXE] = extract(sPath, 0, ICF_MIDDLE);
    */
    if (icon_size == 24) {
    _icons[ICID_CMDEXE] = Icon(IT_STATIC, ICID_CMDEXE, SizeIcon(IDI_CMD24, icon_size));
    } else {
        _icons[ICID_CMDEXE] = Icon();
    }

}


const Icon &IconCache::extract(LPCTSTR path, UINT flags)
{
    // search for matching icon with unchanged flags in the cache
    CacheKey mapkey(path, flags);
    PathCacheMap::iterator found = _pathCache.find(mapkey);

    if (found != _pathCache.end())
        return _icons[found->second];

    // search for matching icon with handle
    CacheKey mapkey_hicon(path, flags | ICF_HICON);
    if (flags != mapkey_hicon.second) {
        found = _pathCache.find(mapkey_hicon);

        if (found != _pathCache.end())
            return _icons[found->second];
    }

    // search for matching icon in the system image list cache
    CacheKey mapkey_syscache(path, flags | ICF_SYSCACHE);
    if (flags != mapkey_syscache.second) {
        found = _pathCache.find(mapkey_syscache);

        if (found != _pathCache.end())
            return _icons[found->second];
    }

    SHFILEINFO sfi;

    int shgfi_flags = 0;

    if (flags & ICF_NOLINKOVERLAY) {
        shgfi_flags = SHGFI_SYSICONINDEX;
        if (flags & ICF_LARGE) {
            shgfi_flags |= SHGFI_LARGEICON;
        } else {
            shgfi_flags |= SHGFI_SMALLICON;
        }
        HIMAGELIST himl = (HIMAGELIST)SHGetFileInfo(path, 0, &sfi, sizeof(sfi), shgfi_flags);
        if (himl) {
            HICON hicon = ImageList_GetIcon(himl, sfi.iIcon, ILD_NORMAL);
            const Icon &icon = add(hicon, IT_CACHED);

            ///@todo limit cache size
            _pathCache[mapkey_hicon] = icon;

            return icon;
        }
    }

    shgfi_flags = 0;

    if (flags & ICF_OPEN)
        shgfi_flags |= SHGFI_OPENICON;

    if ((flags & (ICF_LARGE | ICF_MIDDLE | ICF_OVERLAYS | ICF_HICON)) && !(flags & ICF_SYSCACHE)) {
        shgfi_flags |= SHGFI_ICON;

        if (!(flags & (ICF_LARGE | ICF_MIDDLE)))
            shgfi_flags |= SHGFI_SMALLICON;

        if (flags & ICF_OVERLAYS)
            shgfi_flags |= SHGFI_ADDOVERLAYS;

        // get small/big icons with/without overlays
        if (SHGetFileInfo(path, 0, &sfi, sizeof(sfi), shgfi_flags)) {
            const Icon &icon = add(sfi.hIcon, IT_CACHED);

            ///@todo limit cache size
            _pathCache[mapkey_hicon] = icon;

            return icon;
        }
    } else {
        assert(!(flags & ICF_OVERLAYS));

        shgfi_flags |= SHGFI_SYSICONINDEX | SHGFI_SMALLICON;

        // use system image list - the "search program dialog" needs it
        HIMAGELIST himlSys_small = (HIMAGELIST)SHGetFileInfo(path, 0, &sfi, sizeof(sfi), shgfi_flags);

        if (himlSys_small) {
            _himlSys_small = himlSys_small;

            const Icon &icon = add(sfi.iIcon/*, IT_SYSCACHE*/);

            ///@todo limit cache size
            _pathCache[mapkey_syscache] = icon;

            return icon;
        }
    }

    return _icons[ICID_NONE];
}

const Icon &IconCache::extract(LPCTSTR path, int icon_idx, UINT flags)
{
    IdxCacheKey key(path, make_pair(icon_idx, (flags | ICF_HICON) & ~ICF_SYSCACHE));

    key.first.toLower();

    IdxCacheMap::iterator found = _idxCache.find(key);

    if (found != _idxCache.end())
        return _icons[found->second];

    HICON hIcon;

    if ((int)ExtractIconEx(path, icon_idx, NULL, &hIcon, 1) > 0) {
        const Icon &icon = add(hIcon, IT_CACHED);

        _idxCache[key] = icon;

        return icon;
    } else {

        ///@todo retreive "http://.../favicon.ico" format icons

        return _icons[ICID_NONE];
    }
}

const Icon &IconCache::extract(IExtractIcon *pExtract, LPCTSTR path, int icon_idx, UINT flags)
{
    HICON hIconLarge = 0;
    HICON hIcon;

    int icon_size = ICON_SIZE_FROM_ICF(flags);
    HRESULT hr = pExtract->Extract(path, icon_idx, &hIconLarge, &hIcon, MAKELONG(GetSystemMetrics(SM_CXICON), icon_size));

    if (hr == NOERROR) {    //@@ oder SUCCEEDED(hr) ?
        if (icon_size > ICON_SIZE_SMALL) {    //@@ OK?
            if (hIcon)
                DestroyIcon(hIcon);

            hIcon = hIconLarge;
        } else {
            if (hIconLarge)
                DestroyIcon(hIconLarge);
        }

        if (hIcon)
            return add(hIcon);    //@@ When do we want not to free this icons?
    }

    return _icons[ICID_NONE];
}

const Icon &IconCache::extract(LPCITEMIDLIST pidl, UINT flags)
{
    // search for matching icon with unchanged flags in the cache
    PidlCacheKey mapkey(pidl, flags);
    PidlCacheMap::iterator found = _pidlcache.find(mapkey);

    if (found != _pidlcache.end())
        return _icons[found->second];

    // search for matching icon with handle
    PidlCacheKey mapkey_hicon(pidl, flags | ICF_HICON);
    if (flags != mapkey_hicon.second) {
        found = _pidlcache.find(mapkey_hicon);

        if (found != _pidlcache.end())
            return _icons[found->second];
    }

    // search for matching icon in the system image list cache
    PidlCacheKey mapkey_syscache(pidl, flags | ICF_SYSCACHE);
    if (flags != mapkey_syscache.second) {
        found = _pidlcache.find(mapkey_syscache);

        if (found != _pidlcache.end())
            return _icons[found->second];
    }

    SHFILEINFO sfi;

    int shgfi_flags = SHGFI_PIDL;

    if (!(flags & (ICF_LARGE | ICF_MIDDLE)))
        shgfi_flags |= SHGFI_SMALLICON;

    if (flags & ICF_OPEN)
        shgfi_flags |= SHGFI_OPENICON;

    if (flags & ICF_SYSCACHE) {
        assert(!(flags & ICF_OVERLAYS));

        HIMAGELIST himlSys = (HIMAGELIST)SHGetFileInfo((LPCTSTR)pidl, 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | shgfi_flags);
        if (himlSys) {
            const Icon &icon = add(sfi.iIcon/*, IT_SYSCACHE*/);

            ///@todo limit cache size
            _pidlcache[mapkey_syscache] = icon;

            return icon;
        }
    } else {
        if (flags & ICF_OVERLAYS)
            shgfi_flags |= SHGFI_ADDOVERLAYS;

        if (SHGetFileInfo((LPCTSTR)pidl, 0, &sfi, sizeof(sfi), SHGFI_ICON | shgfi_flags)) {
            const Icon &icon = add(sfi.hIcon, IT_CACHED);

            ///@todo limit cache size
            _pidlcache[mapkey_hicon] = icon;

            return icon;
        }
    }

    return _icons[ICID_NONE];
}


const Icon &IconCache::add(HICON hIcon, ICON_TYPE type)
{
    int id = ++s_next_id;

    return _icons[id] = Icon(type, id, hIcon);
}

const Icon    &IconCache::add(int sys_idx/*, ICON_TYPE type=IT_SYSCACHE*/)
{
    int id = ++s_next_id;

    return _icons[id] = SysCacheIcon(id, sys_idx);
}

const Icon &IconCache::get_icon(int id)
{
    return _icons[id];
}

IconCache::~IconCache()
{
    /* We don't need to free cached resources - they are automatically freed at process termination
    for (int index = s_next_id; index >= 0; index--) {
    IconMap::iterator found = _icons.find(index);

    if (found != _icons.end()) {
    Icon& icon = found->second;

    if ((icon.get_icontype() == IT_DYNAMIC) ||
    (icon.get_icontype() == IT_CACHED))
    {
    DestroyIcon(icon.get_hicon());
    _icons.erase(found);
    }
    }
    }
    */
}

void IconCache::free_icon(int icon_id)
{
    IconMap::iterator found = _icons.find(icon_id);

    if (found != _icons.end()) {
        Icon &icon = found->second;

        if (icon.destroy())
            _icons.erase(found);
    }
}


ResString::ResString(UINT nid)
{
    TCHAR buffer[BUFFER_LEN];

    int len = LoadString(g_Globals._hInstance, nid, buffer, sizeof(buffer) / sizeof(TCHAR));

    super::assign(buffer, len);
}


ResIcon::ResIcon(UINT nid)
{
    _hicon = LoadIcon(g_Globals._hInstance, MAKEINTRESOURCE(nid));
}

SmallIcon::SmallIcon(UINT nid)
{
    _hicon = (HICON)LoadImage(g_Globals._hInstance, MAKEINTRESOURCE(nid), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
}

SizeIcon::SizeIcon(UINT nid, int size)
{
    _hicon = (HICON)LoadImage(g_Globals._hInstance, MAKEINTRESOURCE(nid), IMAGE_ICON, size, size, LR_SHARED);
}

ResIconEx::ResIconEx(UINT nid, int w, int h)
{
    _hicon = (HICON)LoadImage(g_Globals._hInstance, MAKEINTRESOURCE(nid), IMAGE_ICON, w, h, LR_SHARED);
}


void SetWindowIcon(HWND hwnd, UINT nid)
{
    HICON hIcon = ResIcon(nid);
    (void)Window_SetIcon(hwnd, ICON_BIG, hIcon);

    HICON hIconSmall = SmallIcon(nid);
    (void)Window_SetIcon(hwnd, ICON_SMALL, hIconSmall);
}


ResBitmap::ResBitmap(UINT nid)
{
    _hBmp = LoadBitmap(g_Globals._hInstance, MAKEINTRESOURCE(nid));
}
