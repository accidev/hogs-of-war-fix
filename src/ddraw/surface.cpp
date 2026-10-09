#include "objects.h"
#include "d3d7.h"
#include <algorithm>
#include <intrin.h>
#include <string.h>

std::vector<Surface *> g_surfaces;

void rgb565_format(DDPIXELFORMAT &pf)
{
    memset(&pf, 0, sizeof pf);
    pf.dwSize = sizeof pf;
    pf.dwFlags = DDPF_RGB;
    pf.dwRGBBitCount = 16;
    pf.dwRBitMask = 0xF800;
    pf.dwGBitMask = 0x07E0;
    pf.dwBBitMask = 0x001F;
}

Surface::Surface(const DDSURFACEDESC2 &d) : desc(d)
{
    DWORD caps = d.ddsCaps.dwCaps;
    if (caps & DDSCAPS_PRIMARYSURFACE)
        kind = Kind::Primary;
    else if (caps & DDSCAPS_ZBUFFER)
        kind = Kind::ZBuffer;
    else if (caps & DDSCAPS_TEXTURE)
        kind = Kind::Texture;
    else if (caps & (DDSCAPS_3DDEVICE | DDSCAPS_BACKBUFFER))
        kind = Kind::Back;
    desc.dwSize = sizeof desc;
    if (kind == Kind::Primary || !(d.dwFlags & DDSD_WIDTH)) {
        desc.dwWidth = g_ddraw ? g_ddraw->mode_w : 640;
        desc.dwHeight = g_ddraw ? g_ddraw->mode_h : 480;
    }
    if (!(d.dwFlags & DDSD_PIXELFORMAT) && kind != Kind::ZBuffer)
        rgb565_format(desc.ddpfPixelFormat);
    desc.dwFlags |= DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_CAPS | DDSD_PITCH;
    if (!(caps & (DDSCAPS_SYSTEMMEMORY | DDSCAPS_VIDEOMEMORY)))
        desc.ddsCaps.dwCaps |= DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM;
    w = desc.dwWidth;
    h = desc.dwHeight;
    bpp = desc.ddpfPixelFormat.dwRGBBitCount ? desc.ddpfPixelFormat.dwRGBBitCount : 16;
    if (kind == Kind::Back)
        cpu_valid = false;
    make_storage();
    g_surfaces.push_back(this);
}

Surface::~Surface()
{
    g_surfaces.erase(std::remove(g_surfaces.begin(), g_surfaces.end(), this), g_surfaces.end());
    for (Surface *s : g_surfaces)
        if (s->attached == this) {
            s->attached = nullptr;
            s->owns_attached = false;
        }
    for (Device *d : g_devices) {
        if (d->target == this)
            d->target = nullptr;
        for (Surface *&t : d->tex)
            if (t == this)
                t = nullptr;
    }
    if (owns_attached && attached)
        attached->Release();
    gpu::forget(this);
    gpu::texture_free(gpu_tex);
    if (dc) {
        SelectObject(dc, old_bitmap);
        DeleteDC(dc);
    }
    if (dib)
        DeleteObject(dib);
}

void Surface::make_storage()
{
    if (kind == Kind::Primary || kind == Kind::ZBuffer)
        return;
    struct {
        BITMAPINFOHEADER h;
        union { DWORD masks[3]; RGBQUAD colors[256]; };
    } bi = {};
    bi.h.biSize = sizeof bi.h;
    bi.h.biWidth = w;
    bi.h.biHeight = -h; /* top-down, like a DirectDraw surface */
    bi.h.biPlanes = 1;
    bi.h.biBitCount = (WORD)bpp;
    if (bpp == 16 || bpp == 32) {
        bi.h.biCompression = BI_BITFIELDS;
        bi.masks[0] = desc.ddpfPixelFormat.dwRBitMask;
        bi.masks[1] = desc.ddpfPixelFormat.dwGBitMask;
        bi.masks[2] = desc.ddpfPixelFormat.dwBBitMask;
    } else {
        bi.h.biCompression = BI_RGB;
        bi.h.biClrUsed = 256;
    }
    void *p = nullptr;
    dib = CreateDIBSection(nullptr, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &p, nullptr, 0);
    if (!dib) {
        log_printf("surface %dx%dx%d: CreateDIBSection failed", w, h, bpp);
        return;
    }
    bits = (uint8_t *)p;
    pitch = ((w * bpp + 31) / 32) * 4;
    dc = CreateCompatibleDC(nullptr);
    old_bitmap = SelectObject(dc, dib);
    desc.lPitch = pitch;
}

void Surface::sync_from_gpu()
{
    if (kind == Kind::Back && !cpu_valid && bits && gpu::ready() && gpu::width() == w && gpu::height() == h) {
        gpu::read_back((uint16_t *)bits, pitch);
        cpu_valid = true;
    }
}

void Surface::wrote_cpu()
{
    version++;
    if (g_capturing && kind == Kind::Back)
        capture_note("CPU write to the back buffer (Lock, GetDC or blit): whole frame uploaded");
    if (kind == Kind::Back && bits && gpu::ready()) {
        gpu::write_back((const uint16_t *)bits, pitch);
        cpu_valid = true;
    }
}

gpu::Texture *Surface::texture()
{
    if (kind != Kind::Texture || !bits)
        return nullptr;
    if (!gpu_tex)
        gpu_tex = gpu::texture_create(w, h);
    if (gpu_tex && gpu_tex_version != version) {
        const DDPIXELFORMAT &pf = desc.ddpfPixelFormat;
        if (pf.dwRGBBitCount != 16 || pf.dwRBitMask != 0x7C00) {
            static bool once;
            if (!once)
                log_printf("texture format %lu bpp R%08lX A%08lX not A1R5G5B5: colours will be wrong",
                           pf.dwRGBBitCount, pf.dwRBitMask, pf.dwRGBAlphaBitMask);
            once = true;
        }
        gpu::texture_upload(gpu_tex, (const uint16_t *)bits, pitch);
        gpu_tex_version = version;
    }
    return gpu_tex;
}

HRESULT STDMETHODCALLTYPE Surface::QueryInterface(REFIID riid, void **out)
{
    if (riid == IID_IUnknown || riid == IID_IDirectDrawSurface7) {
        AddRef();
        *out = this;
        return S_OK;
    }
    unknown_interface("IDirectDrawSurface7", riid);
    *out = nullptr;
    return E_NOINTERFACE;
}

/* Clip d to the surface (dw x dh) and cut s by the same share, so a stretched blit keeps its proportions. */
static bool clip(RECT &d, RECT &s, int dw, int dh)
{
    if (d.right <= d.left || d.bottom <= d.top || s.right <= s.left || s.bottom <= s.top)
        return false;
    double fx = double(s.right - s.left) / (d.right - d.left), fy = double(s.bottom - s.top) / (d.bottom - d.top);
    if (d.left < 0) { s.left += LONG(-d.left * fx); d.left = 0; }
    if (d.top < 0) { s.top += LONG(-d.top * fy); d.top = 0; }
    if (d.right > dw) { s.right -= LONG((d.right - dw) * fx); d.right = dw; }
    if (d.bottom > dh) { s.bottom -= LONG((d.bottom - dh) * fy); d.bottom = dh; }
    return d.right > d.left && d.bottom > d.top && s.right > s.left && s.bottom > s.top;
}

/* CPU blit with nearest-neighbour stretch, optional source colour key and mirroring. */
static void cpu_blit(Surface *dst, const RECT &d, Surface *src, const RECT &s, int key, bool mx, bool my)
{
    if (!dst->bits || !src->bits || dst->bpp != src->bpp || (dst->bpp != 16 && dst->bpp != 8)) {
        log_printf("blit %dbpp -> %dbpp not supported", src->bpp, dst->bpp);
        return;
    }
    int dw = d.right - d.left, dh = d.bottom - d.top, sw = s.right - s.left, sh = s.bottom - s.top;
    if (sw == dw && sh == dh && key < 0 && !mx && !my) { /* plain copy, e.g. a texture page upload */
        int bytes = dw * dst->bpp / 8;
        for (int y = 0; y < dh; y++)
            memmove(dst->bits + (d.top + y) * dst->pitch + d.left * dst->bpp / 8,
                    src->bits + (s.top + y) * src->pitch + s.left * src->bpp / 8, bytes);
        return;
    }
    for (int y = 0; y < dh; y++) {
        int sy = s.top + (my ? (dh - 1 - y) : y) * sh / dh;
        uint8_t *drow = dst->bits + (d.top + y) * dst->pitch;
        const uint8_t *srow = src->bits + sy * src->pitch;
        for (int x = 0; x < dw; x++) {
            int sx = s.left + (mx ? (dw - 1 - x) : x) * sw / dw;
            if (dst->bpp == 16) {
                uint16_t p = ((const uint16_t *)srow)[sx];
                if (key < 0 || p != key)
                    ((uint16_t *)drow)[d.left + x] = p;
            } else {
                uint8_t p = srow[sx];
                if (key < 0 || p != key)
                    drow[d.left + x] = p;
            }
        }
    }
}

static void cpu_fill(Surface *dst, const RECT &d, DWORD color)
{
    if (!dst->bits)
        return;
    for (int y = d.top; y < d.bottom; y++) {
        uint8_t *row = dst->bits + y * dst->pitch;
        for (int x = d.left; x < d.right; x++) {
            if (dst->bpp == 16)
                ((uint16_t *)row)[x] = (uint16_t)color;
            else if (dst->bpp == 8)
                row[x] = (uint8_t)color;
        }
    }
}

/* Common path for Blt and BltFast. */
static HRESULT do_blit(Surface *dst, RECT d, Surface *src, RECT s, int key, bool mx, bool my)
{
    if (dst->kind == Kind::Primary) { /* windowed present: back buffer -> window */
        present_frame();
        return DD_OK;
    }
    if (!clip(d, s, dst->w, dst->h))
        return DD_OK;
    if (dst->kind == Kind::Back) {
        if (src->kind == Kind::Back) { /* back -> back: do it on the CPU copy */
            src->sync_from_gpu();
            cpu_blit(dst, d, src, s, key, mx, my);
            dst->wrote_cpu();
        } else if (src->bpp == 16 && src->bits) {
            gpu::blit(d, src, src->version, (const uint16_t *)src->bits, src->pitch, src->w, src->h, s, key, mx, my);
            dst->cpu_valid = false;
        } else {
            log_printf("blit to back buffer from %dbpp surface not supported", src->bpp);
        }
        return DD_OK;
    }
    src->sync_from_gpu();
    cpu_blit(dst, d, src, s, key, mx, my);
    dst->wrote_cpu();
    return DD_OK;
}

HRESULT STDMETHODCALLTYPE Surface::Blt(LPRECT dr, LPDIRECTDRAWSURFACE7 srcp, LPRECT sr, DWORD flags, LPDDBLTFX fx)
{
    RECT d = dr ? *dr : RECT{ 0, 0, w, h };
    if (g_capturing)
        capture_note("Blt from %p to kind %d %dx%d (%ld,%ld)-(%ld,%ld) src %p flags %08lX", _ReturnAddress(), (int)kind,
                     w, h, d.left, d.top, d.right, d.bottom, (void *)srcp, flags);
    if (flags & DDBLT_COLORFILL) {
        DWORD color = fx ? fx->dwFillColor : 0;
        RECT s = d;
        if (kind == Kind::Primary || !clip(d, s, w, h))
            return DD_OK;
        if (kind == Kind::Back) {
            gpu::fill(d, (uint16_t)color);
            cpu_valid = false;
        } else {
            cpu_fill(this, d, color);
            wrote_cpu();
        }
        return DD_OK;
    }
    Surface *src = (Surface *)srcp;
    if (!src) {
        log_printf("Blt without source, flags %08lX", flags);
        return DD_OK;
    }
    RECT s = sr ? *sr : RECT{ 0, 0, src->w, src->h };
    int key = -1;
    if ((flags & DDBLT_KEYSRC) && src->has_src_key)
        key = src->key_value();
    else if ((flags & DDBLT_KEYSRCOVERRIDE) && fx)
        key = (uint16_t)fx->ddckSrcColorkey.dwColorSpaceLowValue;
    bool mx = false, my = false;
    if ((flags & DDBLT_DDFX) && fx) {
        mx = (fx->dwDDFX & DDBLTFX_MIRRORLEFTRIGHT) != 0;
        my = (fx->dwDDFX & DDBLTFX_MIRRORUPDOWN) != 0;
    }
    return do_blit(this, d, src, s, key, mx, my);
}

HRESULT STDMETHODCALLTYPE Surface::BltFast(DWORD x, DWORD y, LPDIRECTDRAWSURFACE7 srcp, LPRECT sr, DWORD flags)
{
    Surface *src = (Surface *)srcp;
    if (!src)
        return DDERR_INVALIDPARAMS;
    RECT s = sr ? *sr : RECT{ 0, 0, src->w, src->h };
    RECT d = { (LONG)x, (LONG)y, (LONG)x + s.right - s.left, (LONG)y + s.bottom - s.top };
    int key = (flags & DDBLTFAST_SRCCOLORKEY) && src->has_src_key ? src->key_value() : -1;
    if (g_capturing)
        capture_note("BltFast from %p to kind %d (%ld,%ld)-(%ld,%ld) src %p %dx%d key %d", _ReturnAddress(), (int)kind,
                     d.left, d.top, d.right, d.bottom, (void *)src, src->w, src->h, key);
    return do_blit(this, d, src, s, key, false, false);
}

HRESULT STDMETHODCALLTYPE Surface::Flip(LPDIRECTDRAWSURFACE7, DWORD)
{
    present_frame();
    return DD_OK;
}

HRESULT STDMETHODCALLTYPE Surface::GetAttachedSurface(LPDDSCAPS2 caps, LPDIRECTDRAWSURFACE7 FAR *out)
{
    if (attached && (!caps || (attached->desc.ddsCaps.dwCaps & caps->dwCaps) == caps->dwCaps)) {
        attached->AddRef();
        *out = attached;
        return DD_OK;
    }
    *out = nullptr;
    return DDERR_NOTFOUND;
}

HRESULT STDMETHODCALLTYPE Surface::AddAttachedSurface(LPDIRECTDRAWSURFACE7 s)
{
    if (!s)
        return DDERR_INVALIDPARAMS;
    if (owns_attached && attached)
        attached->Release();
    attached = (Surface *)s; /* weak: the game releases the z buffer itself */
    owns_attached = false;
    return DD_OK;
}

HRESULT STDMETHODCALLTYPE Surface::DeleteAttachedSurface(DWORD, LPDIRECTDRAWSURFACE7 s)
{
    if (attached && (!s || s == attached)) {
        if (owns_attached)
            attached->Release();
        attached = nullptr;
        owns_attached = false;
    }
    return DD_OK;
}

HRESULT STDMETHODCALLTYPE Surface::GetCaps(LPDDSCAPS2 caps)
{
    *caps = desc.ddsCaps;
    return DD_OK;
}

HRESULT STDMETHODCALLTYPE Surface::GetColorKey(DWORD flags, LPDDCOLORKEY key)
{
    if (!(flags & DDCKEY_SRCBLT) || !has_src_key)
        return DDERR_NOCOLORKEY;
    *key = src_key;
    return DD_OK;
}

HRESULT STDMETHODCALLTYPE Surface::SetColorKey(DWORD flags, LPDDCOLORKEY key)
{
    if (flags & DDCKEY_SRCBLT) {
        has_src_key = key != nullptr;
        if (key)
            src_key = *key;
    } else {
        log_printf("SetColorKey flags %08lX ignored", flags);
    }
    return DD_OK;
}

HRESULT STDMETHODCALLTYPE Surface::GetDC(HDC FAR *out)
{
    if (!dc)
        return DDERR_CANTCREATEDC;
    sync_from_gpu();
    *out = dc;
    return DD_OK;
}

HRESULT STDMETHODCALLTYPE Surface::ReleaseDC(HDC)
{
    GdiFlush();
    wrote_cpu();
    return DD_OK;
}

HRESULT STDMETHODCALLTYPE Surface::GetPixelFormat(LPDDPIXELFORMAT pf)
{
    *pf = desc.ddpfPixelFormat;
    return DD_OK;
}

HRESULT STDMETHODCALLTYPE Surface::GetSurfaceDesc(LPDDSURFACEDESC2 d)
{
    DWORD size = d->dwSize ? d->dwSize : sizeof desc;
    memcpy(d, &desc, size < sizeof desc ? size : sizeof desc);
    d->dwSize = size;
    d->lpSurface = nullptr;
    return DD_OK;
}

HRESULT STDMETHODCALLTYPE Surface::Lock(LPRECT r, LPDDSURFACEDESC2 d, DWORD flags, HANDLE)
{
    if (!bits)
        return DDERR_GENERIC;
    /* even for DDLOCK_WRITEONLY: Bink writes only the rectangles that changed, so the rest
     * of the surface must hold the previous contents */
    sync_from_gpu();
    GetSurfaceDesc(d);
    d->lpSurface = bits + (r ? r->top * pitch + r->left * (bpp / 8) : 0);
    d->dwFlags |= DDSD_LPSURFACE;
    locked = !(flags & DDLOCK_READONLY);
    return DD_OK;
}

HRESULT STDMETHODCALLTYPE Surface::Unlock(LPRECT)
{
    if (locked)
        wrote_cpu();
    locked = false;
    return DD_OK;
}

HRESULT STDMETHODCALLTYPE Surface::SetPalette(LPDIRECTDRAWPALETTE p)
{
    palette = (Palette *)p; /* weak, cleared by ~Palette */
    if (palette) {
        if (dc && bpp == 8) {
            RGBQUAD q[256];
            for (int i = 0; i < 256; i++)
                q[i] = { palette->entries[i].peBlue, palette->entries[i].peGreen, palette->entries[i].peRed, 0 };
            SetDIBColorTable(dc, 0, 256, q);
        }
    }
    return DD_OK;
}

HRESULT STDMETHODCALLTYPE Surface::GetPalette(LPDIRECTDRAWPALETTE FAR *out)
{
    if (!palette)
        return DDERR_NOPALETTEATTACHED;
    palette->AddRef();
    *out = palette;
    return DD_OK;
}

HRESULT STDMETHODCALLTYPE Surface::GetDDInterface(LPVOID FAR *out)
{
    if (!g_ddraw)
        return DDERR_GENERIC;
    g_ddraw->AddRef();
    *out = (IDirectDraw7 *)g_ddraw;
    return DD_OK;
}

Palette::~Palette()
{
    for (Surface *s : g_surfaces)
        if (s->palette == this)
            s->palette = nullptr;
}

HRESULT STDMETHODCALLTYPE Palette::QueryInterface(REFIID riid, void **out)
{
    if (riid == IID_IUnknown || riid == IID_IDirectDrawPalette) {
        AddRef();
        *out = this;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE Palette::GetEntries(DWORD, DWORD base, DWORD n, LPPALETTEENTRY e)
{
    if (base + n > 256)
        return DDERR_INVALIDPARAMS;
    memcpy(e, entries + base, n * sizeof *e);
    return DD_OK;
}

HRESULT STDMETHODCALLTYPE Palette::SetEntries(DWORD, DWORD base, DWORD n, LPPALETTEENTRY e)
{
    if (base + n > 256)
        return DDERR_INVALIDPARAMS;
    memcpy(entries + base, e, n * sizeof *e);
    return DD_OK;
}

HRESULT STDMETHODCALLTYPE Clipper::QueryInterface(REFIID riid, void **out)
{
    if (riid == IID_IUnknown || riid == IID_IDirectDrawClipper) {
        AddRef();
        *out = this;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE Clipper::SetHWnd(DWORD, HWND h)
{
    hwnd = h;
    return DD_OK;
}
