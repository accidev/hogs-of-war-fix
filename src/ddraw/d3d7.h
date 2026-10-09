/* IDirect3D7 and IDirect3DDevice7 on top of gpu.h. */
#pragma once
#include "objects.h"

/* Part of the DirectDraw object (DirectX 7 aggregates them): same reference count, deleted
 * together with it. */
struct Direct3D : IDirect3D7Stubs {
    DirectDraw *dd;
    explicit Direct3D(DirectDraw *d) : dd(d) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override;
    ULONG STDMETHODCALLTYPE AddRef() override { return dd->AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override { return dd->Release(); }

    HRESULT STDMETHODCALLTYPE EnumDevices(LPD3DENUMDEVICESCALLBACK7, LPVOID) override;
    HRESULT STDMETHODCALLTYPE CreateDevice(REFCLSID, LPDIRECTDRAWSURFACE7, LPDIRECT3DDEVICE7 *) override;
    HRESULT STDMETHODCALLTYPE EnumZBufferFormats(REFCLSID, LPD3DENUMPIXELFORMATSCALLBACK, LPVOID) override;
    HRESULT STDMETHODCALLTYPE EvictManagedTextures() override { return D3D_OK; }
};

struct Device : IDirect3DDevice7Stubs {
    HOGS_IUNKNOWN(Device)
    Direct3D *d3d;                 /* weak links, cleared when the objects go away */
    Surface *target;
    Surface *tex[8] = {};
    DWORD rs[256] = {};            /* D3DRENDERSTATETYPE values used by DX7 are below 256 */
    DWORD tss[8][32] = {};         /* D3DTEXTURESTAGESTATETYPE */
    D3DVIEWPORT7 vp = {};

    Device(Direct3D *d, Surface *rt);
    ~Device();

    HRESULT STDMETHODCALLTYPE GetCaps(LPD3DDEVICEDESC7) override;
    HRESULT STDMETHODCALLTYPE EnumTextureFormats(LPD3DENUMPIXELFORMATSCALLBACK, LPVOID) override;
    HRESULT STDMETHODCALLTYPE BeginScene() override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE EndScene() override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetDirect3D(LPDIRECT3D7 *) override;
    HRESULT STDMETHODCALLTYPE GetRenderTarget(LPDIRECTDRAWSURFACE7 *) override;
    HRESULT STDMETHODCALLTYPE Clear(DWORD, LPD3DRECT, DWORD, D3DCOLOR, D3DVALUE, DWORD) override;
    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE, LPD3DMATRIX) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE SetViewport(LPD3DVIEWPORT7) override;
    HRESULT STDMETHODCALLTYPE GetViewport(LPD3DVIEWPORT7) override;
    HRESULT STDMETHODCALLTYPE SetMaterial(LPD3DMATERIAL7) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE SetLight(DWORD, LPD3DLIGHT7) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE LightEnable(DWORD, BOOL) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE, DWORD) override;
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE, LPDWORD) override;
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD, LPDIRECTDRAWSURFACE7) override;
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD, LPDIRECTDRAWSURFACE7 *) override;
    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD, D3DTEXTURESTAGESTATETYPE, DWORD) override;
    HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD, D3DTEXTURESTAGESTATETYPE, LPDWORD) override;
    HRESULT STDMETHODCALLTYPE ValidateDevice(LPDWORD passes) override { *passes = 1; return D3D_OK; }
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE, DWORD, LPVOID, DWORD, DWORD) override;
};
