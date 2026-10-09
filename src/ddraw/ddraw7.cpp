#include "objects.h"
#include "d3d7.h"
#include <string.h>

DirectDraw *g_ddraw;

/* Modes offered to the game; it only uses 16-bit modes up to 1024x768 (launcher filter). */
static const struct { int w, h; } kModes[] = { { 640, 480 }, { 800, 600 }, { 1024, 768 } };

static HWND game_window()
{
    return g_ddraw ? g_ddraw->hwnd : nullptr;
}

void present_frame()
{
    capture_frame_end();
    gpu::present(g_config.vsync != 0);
}

DirectDraw::~DirectDraw()
{
    if (g_ddraw == this)
        g_ddraw = nullptr;
    for (Device *dev : g_devices)
        if (dev->d3d == d3d)
            dev->d3d = nullptr;
    delete d3d;
}

HRESULT STDMETHODCALLTYPE DirectDraw::QueryInterface(REFIID riid, void **out)
{
    if (riid == IID_IUnknown || riid == IID_IDirectDraw7 || riid == IID_IDirectDraw) {
        AddRef();
        *out = this;
        return S_OK;
    }
    if (riid == IID_IDirect3D7) {
        /* one COM object: IDirect3D7 shares this reference count, as in DirectX 7. The
         * launcher never releases it but still loops dd->Release() down to 0. */
        if (!d3d)
            d3d = new Direct3D(this);
        AddRef();
        *out = d3d;
        return S_OK;
    }
    unknown_interface("IDirectDraw7", riid);
    *out = nullptr;
    return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE DirectDraw::SetCooperativeLevel(HWND h, DWORD flags)
{
    log_printf("SetCooperativeLevel(%p, %08lX)", h, flags);
    if (h)
        hwnd = h;
    coop = flags;
    return DD_OK;
}

HRESULT STDMETHODCALLTYPE DirectDraw::SetDisplayMode(DWORD w, DWORD h, DWORD bpp, DWORD, DWORD)
{
    /* the real display mode is never changed: the game runs in a window */
    log_printf("SetDisplayMode(%lu, %lu, %lu)", w, h, bpp);
    mode_w = w;
    mode_h = h;
    return DD_OK;
}

HRESULT STDMETHODCALLTYPE DirectDraw::GetDisplayMode(LPDDSURFACEDESC2 d)
{
    /* report a 16-bit desktop: the game's windowed path draws its 2D only into 8/16-bit
     * surfaces and needs the 3D device to render at the desktop depth */
    memset(d, 0, sizeof *d);
    d->dwSize = sizeof *d;
    d->dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH | DDSD_PIXELFORMAT | DDSD_REFRESHRATE;
    d->dwWidth = mode_w;
    d->dwHeight = mode_h;
    d->lPitch = mode_w * 2;
    d->dwRefreshRate = 60;
    rgb565_format(d->ddpfPixelFormat);
    return DD_OK;
}

HRESULT STDMETHODCALLTYPE DirectDraw::EnumDisplayModes(DWORD, LPDDSURFACEDESC2, LPVOID ctx, LPDDENUMMODESCALLBACK2 cb)
{
    for (auto &m : kModes) {
        DDSURFACEDESC2 d = {};
        d.dwSize = sizeof d;
        d.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH | DDSD_PIXELFORMAT | DDSD_REFRESHRATE;
        d.dwWidth = m.w;
        d.dwHeight = m.h;
        d.lPitch = m.w * 2;
        d.dwRefreshRate = 60;
        rgb565_format(d.ddpfPixelFormat);
        if (cb(&d, ctx) == DDENUMRET_CANCEL)
            break;
    }
    return DD_OK;
}

HRESULT STDMETHODCALLTYPE DirectDraw::CreateSurface(LPDDSURFACEDESC2 d, LPDIRECTDRAWSURFACE7 FAR *out, IUnknown FAR *)
{
    Surface *s = new Surface(*d);
    log_printf("CreateSurface(flags %08lX caps %08lX %lux%lu %lubpp) -> kind %d", d->dwFlags, d->ddsCaps.dwCaps,
               s->desc.dwWidth, s->desc.dwHeight, s->desc.ddpfPixelFormat.dwRGBBitCount, (int)s->kind);
    if (s->kind == Kind::Primary || s->kind == Kind::Back) {
        HWND h = game_window();
        if (h)
            gpu::init(h, g_config.scale);
    }
    if (s->kind == Kind::Primary && (d->ddsCaps.dwCaps & DDSCAPS_FLIP) && (d->dwFlags & DDSD_BACKBUFFERCOUNT)) {
        /* fullscreen flip chain: one back buffer attached to the primary */
        DDSURFACEDESC2 bd = {};
        bd.dwSize = sizeof bd;
        bd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
        bd.ddsCaps.dwCaps = DDSCAPS_BACKBUFFER | DDSCAPS_FLIP | DDSCAPS_COMPLEX | DDSCAPS_3DDEVICE |
                            DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM;
        bd.dwWidth = s->w;
        bd.dwHeight = s->h;
        s->attached = new Surface(bd);
        s->owns_attached = true;
        gpu::set_mode(s->w, s->h);
    }
    if (s->kind == Kind::Back)
        gpu::set_mode(s->w, s->h);
    *out = s;
    return DD_OK;
}

HRESULT STDMETHODCALLTYPE DirectDraw::CreateClipper(DWORD, LPDIRECTDRAWCLIPPER FAR *out, IUnknown FAR *)
{
    *out = new Clipper();
    return DD_OK;
}

HRESULT STDMETHODCALLTYPE DirectDraw::CreatePalette(DWORD, LPPALETTEENTRY e, LPDIRECTDRAWPALETTE FAR *out, IUnknown FAR *)
{
    Palette *p = new Palette();
    if (e)
        memcpy(p->entries, e, sizeof p->entries);
    *out = p;
    return DD_OK;
}

HRESULT STDMETHODCALLTYPE DirectDraw::GetCaps(LPDDCAPS hal, LPDDCAPS hel)
{
    LPDDCAPS both[2] = { hal, hel };
    for (LPDDCAPS c : both) {
        if (!c)
            continue;
        DWORD size = c->dwSize ? c->dwSize : sizeof(DDCAPS);
        memset(c, 0, size);
        c->dwSize = size;
        c->dwCaps = DDCAPS_3D | DDCAPS_BLT | DDCAPS_BLTCOLORFILL | DDCAPS_BLTSTRETCH | DDCAPS_COLORKEY |
                    DDCAPS_BLTDEPTHFILL | DDCAPS_CANBLTSYSMEM;
        c->dwCaps2 = DDCAPS2_WIDESURFACES | DDCAPS2_CANRENDERWINDOWED;
        c->dwCKeyCaps = DDCKEYCAPS_SRCBLT;
        c->dwFXCaps = DDFXCAPS_BLTMIRRORLEFTRIGHT | DDFXCAPS_BLTMIRRORUPDOWN | DDFXCAPS_BLTSTRETCHX | DDFXCAPS_BLTSTRETCHY;
        c->dwVidMemTotal = c->dwVidMemFree = 256 << 20;
        c->ddsCaps.dwCaps = DDSCAPS_3DDEVICE | DDSCAPS_TEXTURE | DDSCAPS_ZBUFFER | DDSCAPS_FLIP | DDSCAPS_PRIMARYSURFACE |
                            DDSCAPS_OFFSCREENPLAIN | DDSCAPS_BACKBUFFER | DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM;
    }
    return DD_OK;
}

HRESULT STDMETHODCALLTYPE DirectDraw::GetAvailableVidMem(LPDDSCAPS2, LPDWORD total, LPDWORD free_)
{
    if (total)
        *total = 256 << 20;
    if (free_)
        *free_ = 256 << 20;
    return DD_OK;
}

HRESULT STDMETHODCALLTYPE DirectDraw::GetDeviceIdentifier(LPDDDEVICEIDENTIFIER2 id, DWORD)
{
    memset(id, 0, sizeof *id);
    strcpy(id->szDriver, "hogsdraw");
    strcpy(id->szDescription, "Hogs of War on Direct3D 11");
    return DD_OK;
}

/* ---- exports ---- */

extern "C" HRESULT WINAPI DirectDrawCreateEx(GUID FAR *, LPVOID *out, REFIID iid, IUnknown FAR *)
{
    if (iid != IID_IDirectDraw7) {
        log_printf("DirectDrawCreateEx: interface other than IDirectDraw7 requested");
        return DDERR_INVALIDPARAMS;
    }
    if (g_ddraw)
        log_printf("DirectDrawCreateEx: another DirectDraw object (previous one still alive)");
    g_ddraw = new DirectDraw();
    *out = (IDirectDraw7 *)g_ddraw;
    return DD_OK;
}

/* The renderer (_d3d.dll) loads DDRAW.DLL itself and uses this old entry point, then asks
 * for IDirectDraw7 with QueryInterface. IDirectDraw's vtable is a prefix of IDirectDraw7's. */
extern "C" HRESULT WINAPI DirectDrawCreate(GUID FAR *, LPDIRECTDRAW FAR *out, IUnknown FAR *)
{
    g_ddraw = new DirectDraw();
    *out = (LPDIRECTDRAW)(IDirectDraw7 *)g_ddraw;
    return DD_OK;
}

extern "C" HRESULT WINAPI DirectDrawEnumerateExA(LPDDENUMCALLBACKEXA cb, LPVOID ctx, DWORD)
{
    /* one adapter: the primary display */
    cb(nullptr, (LPSTR) "Primary Display Driver", (LPSTR) "display", ctx, nullptr);
    return DD_OK;
}

extern "C" HRESULT WINAPI DirectDrawEnumerateA(LPDDENUMCALLBACKA cb, LPVOID ctx)
{
    cb(nullptr, (LPSTR) "Primary Display Driver", (LPSTR) "display", ctx);
    return DD_OK;
}
