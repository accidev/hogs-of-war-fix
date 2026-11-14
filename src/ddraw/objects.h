/* DirectX 7 objects for the game: generated stub classes (stubs.h), used methods overridden */
#pragma once
#include "stubs.h"
#include "gpu.h"
#include <stdint.h>
#include <vector>

/* Release deletes the concrete type, no virtual destructor. The game frees with `while (x->Release() != 0)`,
 * so objects hold no references on each other: weak links, cleared on delete (g_surfaces, g_devices);
 * exception: a primary made with a back buffer owns it (owns_attached) */
#define HOGS_IUNKNOWN(Class)                                                                  \
    LONG refs = 1;                                                                            \
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override;               \
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }         \
    ULONG STDMETHODCALLTYPE Release() override                                                \
    {                                                                                         \
        LONG n = InterlockedDecrement(&refs);                                                 \
        if (n == 0)                                                                           \
            delete this;                                                                      \
        return n;                                                                             \
    }

struct Palette;

enum class Kind { Primary, Back, ZBuffer, Texture, Plain };

struct Surface : IDirectDrawSurface7Stubs {
    HOGS_IUNKNOWN(Surface)

    Kind kind = Kind::Plain;
    DDSURFACEDESC2 desc = {};      /* as reported to the game */
    int w = 0, h = 0, bpp = 16;

    /* CPU pixels, a DIB section so GetDC works; back buffer: copy of the GPU target, valid while cpu_valid */
    HBITMAP dib = nullptr;
    HDC dc = nullptr;
    HGDIOBJ old_bitmap = nullptr;
    uint8_t *bits = nullptr;
    int pitch = 0;
    uint32_t version = 1;          /* bumped on every CPU-side write */
    bool cpu_valid = true;

    DDCOLORKEY src_key = {};
    bool has_src_key = false;
    Palette *palette = nullptr;    /* weak */
    Surface *attached = nullptr;   /* primary -> back buffer, back buffer -> z buffer */
    bool owns_attached = false;    /* flip chain back buffer made with the primary */
    bool locked = false;

    gpu::Texture *gpu_tex = nullptr; /* textures only */
    uint32_t gpu_tex_version = 0;

    Surface(const DDSURFACEDESC2 &d);
    ~Surface();
    void make_storage();
    void sync_from_gpu();
    void wrote_cpu();
    uint16_t key_value() const { return (uint16_t)src_key.dwColorSpaceLowValue; }
    gpu::Texture *texture();       /* GPU copy, uploaded when stale */

    HRESULT STDMETHODCALLTYPE Blt(LPRECT, LPDIRECTDRAWSURFACE7, LPRECT, DWORD, LPDDBLTFX) override;
    HRESULT STDMETHODCALLTYPE BltFast(DWORD, DWORD, LPDIRECTDRAWSURFACE7, LPRECT, DWORD) override;
    HRESULT STDMETHODCALLTYPE Flip(LPDIRECTDRAWSURFACE7, DWORD) override;
    HRESULT STDMETHODCALLTYPE GetAttachedSurface(LPDDSCAPS2, LPDIRECTDRAWSURFACE7 FAR *) override;
    HRESULT STDMETHODCALLTYPE AddAttachedSurface(LPDIRECTDRAWSURFACE7) override;
    HRESULT STDMETHODCALLTYPE DeleteAttachedSurface(DWORD, LPDIRECTDRAWSURFACE7) override;
    HRESULT STDMETHODCALLTYPE GetBltStatus(DWORD) override { return DD_OK; }
    HRESULT STDMETHODCALLTYPE GetFlipStatus(DWORD) override { return DD_OK; }
    HRESULT STDMETHODCALLTYPE GetCaps(LPDDSCAPS2) override;
    HRESULT STDMETHODCALLTYPE GetColorKey(DWORD, LPDDCOLORKEY) override;
    HRESULT STDMETHODCALLTYPE SetColorKey(DWORD, LPDDCOLORKEY) override;
    HRESULT STDMETHODCALLTYPE GetDC(HDC FAR *) override;
    HRESULT STDMETHODCALLTYPE ReleaseDC(HDC) override;
    HRESULT STDMETHODCALLTYPE GetPixelFormat(LPDDPIXELFORMAT) override;
    HRESULT STDMETHODCALLTYPE GetSurfaceDesc(LPDDSURFACEDESC2) override;
    HRESULT STDMETHODCALLTYPE IsLost() override { return DD_OK; }
    HRESULT STDMETHODCALLTYPE Restore() override { return DD_OK; }
    HRESULT STDMETHODCALLTYPE Lock(LPRECT, LPDDSURFACEDESC2, DWORD, HANDLE) override;
    HRESULT STDMETHODCALLTYPE Unlock(LPRECT) override;
    HRESULT STDMETHODCALLTYPE SetClipper(LPDIRECTDRAWCLIPPER) override { return DD_OK; }
    HRESULT STDMETHODCALLTYPE GetClipper(LPDIRECTDRAWCLIPPER FAR *) override { return DDERR_NOCLIPPERATTACHED; }
    HRESULT STDMETHODCALLTYPE SetPalette(LPDIRECTDRAWPALETTE) override;
    HRESULT STDMETHODCALLTYPE GetPalette(LPDIRECTDRAWPALETTE FAR *) override;
    HRESULT STDMETHODCALLTYPE GetDDInterface(LPVOID FAR *) override;
    HRESULT STDMETHODCALLTYPE PageLock(DWORD) override { return DD_OK; }
    HRESULT STDMETHODCALLTYPE PageUnlock(DWORD) override { return DD_OK; }
};

struct Palette : IDirectDrawPaletteStubs {
    HOGS_IUNKNOWN(Palette)
    ~Palette();
    PALETTEENTRY entries[256] = {};
    HRESULT STDMETHODCALLTYPE GetEntries(DWORD, DWORD, DWORD, LPPALETTEENTRY) override;
    HRESULT STDMETHODCALLTYPE SetEntries(DWORD, DWORD, DWORD, LPPALETTEENTRY) override;
    HRESULT STDMETHODCALLTYPE GetCaps(LPDWORD caps) override { *caps = DDPCAPS_8BIT; return DD_OK; }
};

struct Clipper : IDirectDrawClipperStubs {
    HOGS_IUNKNOWN(Clipper)
    HWND hwnd = nullptr;
    HRESULT STDMETHODCALLTYPE SetHWnd(DWORD, HWND h) override;
    HRESULT STDMETHODCALLTYPE GetHWnd(HWND FAR *h) override { *h = hwnd; return DD_OK; }
};

/* IDirect3D7 comes from QueryInterface on this */
struct Direct3D;
struct DirectDraw : IDirectDraw7Stubs {
    HOGS_IUNKNOWN(DirectDraw)
    ~DirectDraw();
    Direct3D *d3d = nullptr;
    HWND hwnd = nullptr;
    DWORD coop = 0;
    int mode_w = 640, mode_h = 480;

    HRESULT STDMETHODCALLTYPE CreateClipper(DWORD, LPDIRECTDRAWCLIPPER FAR *, IUnknown FAR *) override;
    HRESULT STDMETHODCALLTYPE CreatePalette(DWORD, LPPALETTEENTRY, LPDIRECTDRAWPALETTE FAR *, IUnknown FAR *) override;
    HRESULT STDMETHODCALLTYPE CreateSurface(LPDDSURFACEDESC2, LPDIRECTDRAWSURFACE7 FAR *, IUnknown FAR *) override;
    HRESULT STDMETHODCALLTYPE EnumDisplayModes(DWORD, LPDDSURFACEDESC2, LPVOID, LPDDENUMMODESCALLBACK2) override;
    HRESULT STDMETHODCALLTYPE GetCaps(LPDDCAPS, LPDDCAPS) override;
    HRESULT STDMETHODCALLTYPE GetDisplayMode(LPDDSURFACEDESC2) override;
    HRESULT STDMETHODCALLTYPE RestoreDisplayMode() override { return DD_OK; }
    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND, DWORD) override;
    HRESULT STDMETHODCALLTYPE SetDisplayMode(DWORD, DWORD, DWORD, DWORD, DWORD) override;
    HRESULT STDMETHODCALLTYPE WaitForVerticalBlank(DWORD, HANDLE) override { return DD_OK; }
    HRESULT STDMETHODCALLTYPE GetAvailableVidMem(LPDDSCAPS2, LPDWORD, LPDWORD) override;
    HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override { return DD_OK; }
    HRESULT STDMETHODCALLTYPE GetDeviceIdentifier(LPDDDEVICEIDENTIFIER2, DWORD) override;
    HRESULT STDMETHODCALLTYPE FlipToGDISurface() override { return DD_OK; }
    HRESULT STDMETHODCALLTYPE GetMonitorFrequency(LPDWORD f) override { *f = 60; return DD_OK; }
    HRESULT STDMETHODCALLTYPE GetVerticalBlankStatus(LPBOOL b) override { *b = TRUE; return DD_OK; }
};

/* hogs.ini [Render], [Display], [Debug]; DllMain also hands vsync and widescreen to hogs_init */
struct Config {
    int scale = 0;      /* game resolution x scale; 0 = cover the monitor */
    int vsync = 1;
    int widescreen = 1; /* modes in the monitor's shape; hogs.c widens the view */
    int fog_distance = 100; /* fog start and end, percent */
    int frame_dump = 0; /* F12 dumps a frame (capture.cpp) */
    int frame_dump_at = 0; /* also dump this frame number */
};
extern Config g_config;
extern DirectDraw *g_ddraw;  /* the last one created; null once released */
struct Device;
extern std::vector<Surface *> g_surfaces;  /* all live surfaces, to clear weak links */
extern std::vector<Device *> g_devices;

/* RGB565: the reported display format and the default surface format
 * (the game's 2D code writes only 8- and 16-bit pixels) */
void rgb565_format(DDPIXELFORMAT &pf);
/* windowed Blt to the primary, or Flip */
void present_frame();

/* capture.cpp: while g_capturing every draw, blit and clear is written out */
extern bool g_capturing;
void capture_note(const char *fmt, ...);
void capture_draw(const void *caller, D3DPRIMITIVETYPE type, const D3DTLVERTEX *v, DWORD count,
                  const gpu::DrawState &s, Surface *tex);
void capture_frame_end(); /* before each present: ends a dump, starts one on F12 */
