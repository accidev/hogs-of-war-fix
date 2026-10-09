#include "d3d7.h"
#include <algorithm>
#include <float.h>
#include <string.h>

std::vector<Device *> g_devices;

/* ---- IDirect3D7 ---- */

HRESULT STDMETHODCALLTYPE Direct3D::QueryInterface(REFIID riid, void **out)
{
    if (riid == IID_IDirect3D7) {
        AddRef();
        *out = this;
        return S_OK;
    }
    if (riid == IID_IUnknown || riid == IID_IDirectDraw7 || riid == IID_IDirectDraw)
        return dd->QueryInterface(riid, out);
    unknown_interface("IDirect3D7", riid);
    *out = nullptr;
    return E_NOINTERFACE;
}

/* What the game needs to see to pick the HAL device with A1R5G5B5 textures and alpha blending. */
static void hal_caps(D3DDEVICEDESC7 &d)
{
    memset(&d, 0, sizeof d);
    d.dwDevCaps = D3DDEVCAPS_FLOATTLVERTEX | D3DDEVCAPS_EXECUTESYSTEMMEMORY | D3DDEVCAPS_TLVERTEXSYSTEMMEMORY |
                  D3DDEVCAPS_TEXTUREVIDEOMEMORY | D3DDEVCAPS_DRAWPRIMTLVERTEX | D3DDEVCAPS_CANRENDERAFTERFLIP |
                  D3DDEVCAPS_TEXTURENONLOCALVIDMEM | D3DDEVCAPS_DRAWPRIMITIVES2 | D3DDEVCAPS_DRAWPRIMITIVES2EX |
                  D3DDEVCAPS_HWRASTERIZATION | D3DDEVCAPS_CANBLTSYSTONONLOCAL;
    D3DPRIMCAPS p = {};
    p.dwSize = sizeof p;
    p.dwMiscCaps = D3DPMISCCAPS_MASKZ | D3DPMISCCAPS_CULLNONE | D3DPMISCCAPS_CULLCW | D3DPMISCCAPS_CULLCCW;
    p.dwRasterCaps = D3DPRASTERCAPS_DITHER | D3DPRASTERCAPS_ZTEST | D3DPRASTERCAPS_FOGVERTEX | D3DPRASTERCAPS_FOGTABLE |
                     D3DPRASTERCAPS_WFOG | D3DPRASTERCAPS_ZFOG | D3DPRASTERCAPS_SUBPIXEL;
    p.dwZCmpCaps = p.dwAlphaCmpCaps = 0xFF; /* all D3DPCMPCAPS_* */
    p.dwSrcBlendCaps = p.dwDestBlendCaps = 0x1FFF; /* all D3DPBLENDCAPS_* */
    p.dwShadeCaps = D3DPSHADECAPS_COLORFLATRGB | D3DPSHADECAPS_COLORGOURAUDRGB | D3DPSHADECAPS_ALPHAFLATBLEND |
                    D3DPSHADECAPS_ALPHAGOURAUDBLEND | D3DPSHADECAPS_FOGFLAT | D3DPSHADECAPS_FOGGOURAUD;
    p.dwTextureCaps = D3DPTEXTURECAPS_PERSPECTIVE | D3DPTEXTURECAPS_ALPHA | D3DPTEXTURECAPS_TRANSPARENCY |
                      D3DPTEXTURECAPS_POW2;
    p.dwTextureFilterCaps = D3DPTFILTERCAPS_NEAREST | D3DPTFILTERCAPS_LINEAR | D3DPTFILTERCAPS_MINFPOINT |
                            D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MAGFPOINT | D3DPTFILTERCAPS_MAGFLINEAR;
    p.dwTextureBlendCaps = D3DPTBLENDCAPS_MODULATE | D3DPTBLENDCAPS_MODULATEALPHA | D3DPTBLENDCAPS_ADD;
    p.dwTextureAddressCaps = D3DPTADDRESSCAPS_WRAP | D3DPTADDRESSCAPS_CLAMP | D3DPTADDRESSCAPS_INDEPENDENTUV;
    d.dpcLineCaps = d.dpcTriCaps = p;
    d.dwDeviceRenderBitDepth = DDBD_16 | DDBD_24 | DDBD_32;
    d.dwDeviceZBufferBitDepth = DDBD_16 | DDBD_24 | DDBD_32;
    d.dwMinTextureWidth = d.dwMinTextureHeight = 1;
    d.dwMaxTextureWidth = d.dwMaxTextureHeight = 2048;
    d.dwMaxTextureRepeat = d.dwMaxTextureAspectRatio = 2048;
    d.dwMaxAnisotropy = 1;
    d.dvGuardBandLeft = d.dvGuardBandTop = -4096;
    d.dvGuardBandRight = d.dvGuardBandBottom = 4096;
    d.dwFVFCaps = 8;
    d.dwTextureOpCaps = D3DTEXOPCAPS_DISABLE | D3DTEXOPCAPS_SELECTARG1 | D3DTEXOPCAPS_SELECTARG2 |
                        D3DTEXOPCAPS_MODULATE | D3DTEXOPCAPS_MODULATE2X | D3DTEXOPCAPS_MODULATE4X | D3DTEXOPCAPS_ADD |
                        D3DTEXOPCAPS_ADDSIGNED | D3DTEXOPCAPS_BLENDDIFFUSEALPHA | D3DTEXOPCAPS_BLENDTEXTUREALPHA;
    d.wMaxTextureBlendStages = d.wMaxSimultaneousTextures = 1;
    d.dwMaxActiveLights = 8;
    d.dvMaxVertexW = 1e10f;
    d.deviceGUID = IID_IDirect3DHALDevice;
}

HRESULT STDMETHODCALLTYPE Direct3D::EnumDevices(LPD3DENUMDEVICESCALLBACK7 cb, LPVOID ctx)
{
    D3DDEVICEDESC7 d;
    hal_caps(d);
    cb((LPSTR) "Direct3D HAL", (LPSTR) "hogsdraw Direct3D 11", &d, ctx);
    return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Direct3D::EnumZBufferFormats(REFCLSID, LPD3DENUMPIXELFORMATSCALLBACK cb, LPVOID ctx)
{
    /* one format only: the game appends every reported format to a list that is never reset */
    DDPIXELFORMAT pf = {};
    pf.dwSize = sizeof pf;
    pf.dwFlags = DDPF_ZBUFFER;
    pf.dwZBufferBitDepth = 16;
    pf.dwZBitMask = 0xFFFF;
    cb(&pf, ctx);
    return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Direct3D::CreateDevice(REFCLSID, LPDIRECTDRAWSURFACE7 rt, LPDIRECT3DDEVICE7 *out)
{
    if (!rt)
        return DDERR_INVALIDPARAMS;
    /* DirectX 7 switched the x87 FPU to single precision when a device was created unless
     * DDSCL_FPUPRESERVE was set. The game never sets it, and its own float maths (aiming,
     * ballistics) has always run in single precision, so keep that behaviour. */
    if (!(dd->coop & DDSCL_FPUPRESERVE))
        _controlfp(_PC_24, _MCW_PC);
    *out = new Device(this, (Surface *)rt);
    log_printf("CreateDevice on %dx%d target", ((Surface *)rt)->w, ((Surface *)rt)->h);
    return D3D_OK;
}

/* ---- IDirect3DDevice7 ---- */

Device::Device(Direct3D *d, Surface *rt) : d3d(d), target(rt)
{
    g_devices.push_back(this);
    /* Direct3D 7 defaults for the states the game relies on */
    rs[D3DRENDERSTATE_ZENABLE] = target->attached ? D3DZB_TRUE : D3DZB_FALSE;
    rs[D3DRENDERSTATE_ZWRITEENABLE] = TRUE;
    rs[D3DRENDERSTATE_ZFUNC] = D3DCMP_LESSEQUAL;
    rs[D3DRENDERSTATE_SRCBLEND] = D3DBLEND_ONE;
    rs[D3DRENDERSTATE_DESTBLEND] = D3DBLEND_ZERO;
    rs[D3DRENDERSTATE_CULLMODE] = D3DCULL_CCW;
    rs[D3DRENDERSTATE_SHADEMODE] = D3DSHADE_GOURAUD;
    rs[D3DRENDERSTATE_ALPHAFUNC] = D3DCMP_ALWAYS;
    rs[D3DRENDERSTATE_FOGTABLEMODE] = D3DFOG_NONE;
    rs[D3DRENDERSTATE_TEXTUREFACTOR] = 0xFFFFFFFF;
    float one = 1.0f;
    memcpy(&rs[D3DRENDERSTATE_FOGEND], &one, 4);
    for (int i = 0; i < 8; i++) {
        tss[i][D3DTSS_COLOROP] = i ? D3DTOP_DISABLE : D3DTOP_MODULATE;
        tss[i][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
        tss[i][D3DTSS_COLORARG2] = D3DTA_CURRENT;
        tss[i][D3DTSS_ALPHAOP] = i ? D3DTOP_DISABLE : D3DTOP_SELECTARG1;
        tss[i][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
        tss[i][D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
        tss[i][D3DTSS_ADDRESSU] = tss[i][D3DTSS_ADDRESSV] = D3DTADDRESS_WRAP;
        tss[i][D3DTSS_MAGFILTER] = D3DTFG_POINT;
        tss[i][D3DTSS_MINFILTER] = D3DTFN_POINT;
    }
    vp.dwWidth = target->w;
    vp.dwHeight = target->h;
    vp.dvMaxZ = 1;
}

Device::~Device()
{
    g_devices.erase(std::remove(g_devices.begin(), g_devices.end(), this), g_devices.end());
}

HRESULT STDMETHODCALLTYPE Device::QueryInterface(REFIID riid, void **out)
{
    if (riid == IID_IUnknown || riid == IID_IDirect3DDevice7) {
        AddRef();
        *out = this;
        return S_OK;
    }
    unknown_interface("IDirect3DDevice7", riid);
    *out = nullptr;
    return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE Device::GetCaps(LPD3DDEVICEDESC7 d)
{
    hal_caps(*d);
    return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::EnumTextureFormats(LPD3DENUMPIXELFORMATSCALLBACK cb, LPVOID ctx)
{
    DDPIXELFORMAT pf = {};
    pf.dwSize = sizeof pf;
    pf.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS; /* A1R5G5B5: the only format the game converts to */
    pf.dwRGBBitCount = 16;
    pf.dwRBitMask = 0x7C00;
    pf.dwGBitMask = 0x03E0;
    pf.dwBBitMask = 0x001F;
    pf.dwRGBAlphaBitMask = 0x8000;
    if (cb(&pf, ctx) == D3DENUMRET_CANCEL)
        return D3D_OK;
    rgb565_format(pf);
    cb(&pf, ctx);
    return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetDirect3D(LPDIRECT3D7 *out)
{
    if (d3d)
        d3d->AddRef();
    *out = d3d;
    return d3d ? D3D_OK : DDERR_GENERIC;
}

HRESULT STDMETHODCALLTYPE Device::GetRenderTarget(LPDIRECTDRAWSURFACE7 *out)
{
    if (target)
        target->AddRef();
    *out = target;
    return target ? D3D_OK : DDERR_GENERIC;
}

HRESULT STDMETHODCALLTYPE Device::Clear(DWORD, LPD3DRECT, DWORD flags, D3DCOLOR color, D3DVALUE z, DWORD)
{
    if (flags & D3DCLEAR_ZBUFFER)
        gpu::clear_depth(z);
    if (flags & D3DCLEAR_TARGET) {
        gpu::clear_target(color);
        if (target)
            target->cpu_valid = false;
    }
    return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetViewport(LPD3DVIEWPORT7 v)
{
    vp = *v;
    gpu::set_clip(v->dwX, v->dwY, v->dwWidth, v->dwHeight);
    return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetViewport(LPD3DVIEWPORT7 v)
{
    *v = vp;
    return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetRenderState(D3DRENDERSTATETYPE s, DWORD v)
{
    if ((unsigned)s < 256)
        rs[s] = v;
    return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetRenderState(D3DRENDERSTATETYPE s, LPDWORD v)
{
    *v = (unsigned)s < 256 ? rs[s] : 0;
    return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetTexture(DWORD stage, LPDIRECTDRAWSURFACE7 t)
{
    if (stage >= 8)
        return DDERR_INVALIDPARAMS;
    tex[stage] = (Surface *)t; /* weak, cleared by ~Surface */
    return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetTexture(DWORD stage, LPDIRECTDRAWSURFACE7 *out)
{
    if (stage >= 8)
        return DDERR_INVALIDPARAMS;
    if (tex[stage])
        tex[stage]->AddRef();
    *out = tex[stage];
    return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE s, DWORD v)
{
    if (stage < 8 && (unsigned)s < 32)
        tss[stage][s] = v;
    return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE s, LPDWORD v)
{
    *v = stage < 8 && (unsigned)s < 32 ? tss[stage][s] : 0;
    return D3D_OK;
}

static float as_float(DWORD v)
{
    float f;
    memcpy(&f, &v, 4);
    return f;
}

HRESULT STDMETHODCALLTYPE Device::DrawPrimitive(D3DPRIMITIVETYPE type, DWORD fvf, LPVOID verts, DWORD count, DWORD)
{
    if (fvf != D3DFVF_TLVERTEX) {
        static bool once;
        if (!once)
            log_printf("DrawPrimitive with FVF %08lX (only D3DFVF_TLVERTEX is implemented)", fvf);
        once = true;
        return D3D_OK;
    }
    gpu::DrawState s = {};
    s.blend = rs[D3DRENDERSTATE_ALPHABLENDENABLE] != 0;
    s.src_blend = (uint8_t)rs[D3DRENDERSTATE_SRCBLEND];
    s.dst_blend = (uint8_t)rs[D3DRENDERSTATE_DESTBLEND];
    s.ztest = rs[D3DRENDERSTATE_ZENABLE] != 0;
    s.zwrite = rs[D3DRENDERSTATE_ZWRITEENABLE] != 0;
    s.zfunc = (uint8_t)rs[D3DRENDERSTATE_ZFUNC];
    s.cull = (uint8_t)rs[D3DRENDERSTATE_CULLMODE];
    s.flat = rs[D3DRENDERSTATE_SHADEMODE] == D3DSHADE_FLAT;
    s.alpha_test = rs[D3DRENDERSTATE_ALPHATESTENABLE] != 0;
    s.alpha_func = (uint8_t)rs[D3DRENDERSTATE_ALPHAFUNC];
    s.alpha_ref = (uint8_t)rs[D3DRENDERSTATE_ALPHAREF];
    if (rs[D3DRENDERSTATE_FOGENABLE] && rs[D3DRENDERSTATE_FOGTABLEMODE] != D3DFOG_NONE) {
        if (rs[D3DRENDERSTATE_FOGTABLEMODE] != D3DFOG_LINEAR) {
            static bool once;
            if (!once)
                log_printf("fog table mode %lu drawn as linear", rs[D3DRENDERSTATE_FOGTABLEMODE]);
            once = true;
        }
        s.fog = 1;
        s.fog_start = as_float(rs[D3DRENDERSTATE_FOGSTART]);
        s.fog_end = as_float(rs[D3DRENDERSTATE_FOGEND]);
    }
    s.fog_color = rs[D3DRENDERSTATE_FOGCOLOR];
    s.tfactor = rs[D3DRENDERSTATE_TEXTUREFACTOR];
    const DWORD *t0 = tss[0];
    s.color_op = (uint8_t)t0[D3DTSS_COLOROP];
    s.color_arg1 = (uint8_t)t0[D3DTSS_COLORARG1];
    s.color_arg2 = (uint8_t)t0[D3DTSS_COLORARG2];
    s.alpha_op = (uint8_t)t0[D3DTSS_ALPHAOP];
    s.alpha_arg1 = (uint8_t)t0[D3DTSS_ALPHAARG1];
    s.alpha_arg2 = (uint8_t)t0[D3DTSS_ALPHAARG2];
    s.linear = t0[D3DTSS_MAGFILTER] >= D3DTFG_LINEAR || t0[D3DTSS_MINFILTER] >= D3DTFN_LINEAR;
    s.wrap = t0[D3DTSS_ADDRESSU] == D3DTADDRESS_WRAP;
    gpu::draw(type, verts, count, s, tex[0] ? tex[0]->texture() : nullptr);
    if (target)
        target->cpu_valid = false;
    return D3D_OK;
}
