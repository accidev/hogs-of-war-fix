#include "gpu.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <string.h>
#include <map>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace gpu {

struct Texture {
    ID3D11Texture2D *tex;
    ID3D11ShaderResourceView *srv;
    int w, h;
};

/* All shaders: D3D7 transformed-and-lit vertices, the stage-0 texture combiner, 2D blits of
 * RGB565 surfaces with an exact colour key, and the final present. */
static const char kShaders[] = R"HLSL(
cbuffer Frame : register(b0) { float4 g_size; };   /* game resolution in xy */

cbuffer Draw : register(b1) {
    uint4 g_color;      /* stage 0: colorop, colorarg1, colorarg2, has_texture */
    uint4 g_alpha;      /* stage 0: alphaop, alphaarg1, alphaarg2, alphafunc (0 = no test) */
    float4 g_fog;       /* enabled, start, end, alpharef (0..255) */
    float4 g_fog_color;
    float4 g_tfactor;
};

cbuffer Blit : register(b2) {
    float4 g_dst;       /* NDC x0, y0, x1, y1 */
    float4 g_src;       /* source texels (or UVs for present) x0, y0, x1, y1 */
    uint4 g_key;        /* key, use_key, fill, fill_rgb565 */
};

Texture2D g_tex : register(t0);
Texture2D<uint> g_bits : register(t1);
SamplerState g_smp : register(s0);

struct VIn { float4 pos : POSITION; float4 col : COLOR0; float4 spec : COLOR1; float2 uv : TEXCOORD0; };
struct VOut {
    float4 pos : SV_Position;
#ifdef FLAT
    nointerpolation float4 col : COLOR0;   /* D3D7 flat shading uses the first vertex */
#else
    float4 col : COLOR0;
#endif
    float2 uv : TEXCOORD0;
    float depth : TEXCOORD1;
};

VOut vs_tl(VIn v)
{
    VOut o;
    /* rhw < 0 is a point behind the camera: footprints and other ground decals near the eye
     * are projected that way (PlacePolyInWorld). A negative w keeps it behind, so the
     * rasterizer clips the polygon at the eye plane instead of stretching it over the screen. */
    float w = v.pos.w != 0 ? 1.0 / v.pos.w : 1.0;
    /* D3D7 pixel centres sit on integer coordinates, D3D11 ones on .5 */
    float2 ndc = float2((v.pos.x + 0.5) / g_size.x * 2 - 1, 1 - (v.pos.y + 0.5) / g_size.y * 2);
    o.pos = float4(ndc * w, v.pos.z * w, w);
    o.col = v.col.bgra;                    /* D3DCOLOR bytes are B, G, R, A */
    o.uv = v.uv;
    o.depth = w;                           /* 1/rhw: what D3D7 table fog uses for TL vertices */
    return o;
}

float4 arg(uint a, float4 tex, float4 dif)
{
    float4 r;
    switch (a & 15) {
    case 0: r = dif; break;                /* D3DTA_DIFFUSE */
    case 1: r = dif; break;                /* D3DTA_CURRENT (= diffuse in stage 0) */
    case 2: r = tex; break;                /* D3DTA_TEXTURE */
    case 3: r = g_tfactor; break;          /* D3DTA_TFACTOR */
    default: r = float4(1, 1, 1, 1); break;/* D3DTA_SPECULAR: always white in this game */
    }
    if (a & 16) r = 1 - r;                 /* D3DTA_COMPLEMENT */
    if (a & 32) r = r.aaaa;                /* D3DTA_ALPHAREPLICATE */
    return r;
}

float4 op(uint o, float4 a1, float4 a2, float4 tex, float4 dif)
{
    switch (o) {
    case 2: return a1;                     /* SELECTARG1 */
    case 3: return a2;                     /* SELECTARG2 */
    case 4: return a1 * a2;                /* MODULATE */
    case 5: return saturate(a1 * a2 * 2);  /* MODULATE2X */
    case 6: return saturate(a1 * a2 * 4);  /* MODULATE4X */
    case 7: return saturate(a1 + a2);      /* ADD */
    case 8: return saturate(a1 + a2 - 0.5);/* ADDSIGNED */
    case 12: return lerp(a2, a1, dif.a);   /* BLENDDIFFUSEALPHA */
    case 13: return lerp(a2, a1, tex.a);   /* BLENDTEXTUREALPHA */
    default: return dif;                   /* DISABLE in stage 0: diffuse */
    }
}

float4 ps_tl(VOut i) : SV_Target
{
    /* an unbound texture reads as opaque white, so untextured primitives show their diffuse */
    float4 tex = g_color.w ? g_tex.Sample(g_smp, i.uv) : float4(1, 1, 1, 1);
    float4 c = op(g_color.x, arg(g_color.y, tex, i.col), arg(g_color.z, tex, i.col), tex, i.col);
    float4 a = op(g_alpha.x, arg(g_alpha.y, tex, i.col), arg(g_alpha.z, tex, i.col), tex, i.col);
    float4 r = float4(c.rgb, a.a);
    if (g_alpha.w) {
        float v = round(r.a * 255), ref = g_fog.w;
        bool ok;
        switch (g_alpha.w) {
        case 1: ok = false; break;
        case 2: ok = v < ref; break;
        case 3: ok = v == ref; break;
        case 4: ok = v <= ref; break;
        case 5: ok = v > ref; break;
        case 6: ok = v != ref; break;
        case 7: ok = v >= ref; break;
        default: ok = true; break;
        }
        if (!ok)
            discard;
    }
    if (g_fog.x > 0) {
        float f = saturate((g_fog.z - i.depth) / max(g_fog.z - g_fog.y, 1e-6));
        r.rgb = lerp(g_fog_color.rgb, r.rgb, f);
    }
    return r;
}

struct BOut { float4 pos : SV_Position; float2 st : TEXCOORD0; };

BOut vs_quad(uint id : SV_VertexID)
{
    float2 t = float2(id & 1, id >> 1);    /* triangle strip of 4 */
    BOut o;
    o.pos = float4(lerp(g_dst.xy, g_dst.zw, t), 0, 1);
    o.st = lerp(g_src.xy, g_src.zw, t);
    return o;
}

float3 rgb565(uint p) { return float3((p >> 11) & 31, (p >> 5) & 63, p & 31) / float3(31, 63, 31); }

float4 ps_blit(BOut i) : SV_Target
{
    if (g_key.z)
        return float4(rgb565(g_key.w), 1);
    uint p = g_bits.Load(int3(floor(i.st), 0));
    if (g_key.y && p == g_key.x)
        discard;
    return float4(rgb565(p), 1);
}

float4 ps_present(BOut i) : SV_Target { return float4(g_tex.Sample(g_smp, i.st).rgb, 1); }
)HLSL";

struct BlitSource {
    ID3D11Texture2D *tex;
    ID3D11ShaderResourceView *srv;
    int w, h;
    uint32_t version;
};

struct DrawConstants {
    uint32_t color[4];
    uint32_t alpha[4];
    float fog[4];
    float fog_color[4];
    float tfactor[4];
};

struct BlitConstants {
    float dst[4];
    float src[4];
    uint32_t key[4];
};

static ID3D11Device *g_dev;
static ID3D11DeviceContext *g_ctx;
static IDXGISwapChain1 *g_swap;
static ID3D11RenderTargetView *g_swap_rtv;
static HWND g_hwnd;
static int g_scale_cfg, g_scale = 1, g_w, g_h, g_swap_w, g_swap_h;

static RECT g_clip = { 0, 0, 16384, 16384 }; /* D3D7 viewport, game pixels */
static ID3D11Texture2D *g_back;            /* the game's back buffer, g_w*g_scale x g_h*g_scale */
static ID3D11RenderTargetView *g_back_rtv;
static ID3D11ShaderResourceView *g_back_srv;
static ID3D11Texture2D *g_depth;
static ID3D11DepthStencilView *g_dsv;
static ID3D11Texture2D *g_small, *g_staging; /* read_back: g_w x g_h */
static ID3D11RenderTargetView *g_small_rtv;

static ID3D11VertexShader *g_vs_tl[2], *g_vs_quad;
static ID3D11PixelShader *g_ps_tl[2], *g_ps_blit, *g_ps_present;
static ID3D11InputLayout *g_layout;
static ID3D11Buffer *g_cb_frame, *g_cb_draw, *g_cb_blit, *g_vb;
static ID3D11SamplerState *g_samplers[4];  /* [linear][wrap] */
static ID3D11RasterizerState *g_raster[3]; /* D3DCULL_NONE, CW, CCW */
static ID3D11DepthStencilState *g_no_depth;
static ID3D11BlendState *g_no_blend;
static std::map<uint32_t, ID3D11BlendState *> g_blend;
static std::map<uint32_t, ID3D11DepthStencilState *> g_depth_states;
static std::map<const void *, BlitSource> g_sources;

static const UINT kVbBytes = 4 << 20;
static UINT g_vb_pos;

static bool check(HRESULT hr, const char *what)
{
    if (FAILED(hr))
        log_printf("gpu: %s failed (0x%08lX)", what, hr);
    return SUCCEEDED(hr);
}

template <class T> static void release(T *&p)
{
    if (p) {
        p->Release();
        p = nullptr;
    }
}

static ID3DBlob *compile(const char *entry, const char *target, bool flat)
{
    D3D_SHADER_MACRO defs[] = { { "FLAT", "1" }, { nullptr, nullptr } };
    ID3DBlob *code = nullptr, *errors = nullptr;
    HRESULT hr = D3DCompile(kShaders, sizeof kShaders - 1, "hogsdraw", flat ? defs : defs + 1, nullptr,
                            entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(hr))
        log_printf("gpu: shader %s: %s", entry, errors ? (const char *)errors->GetBufferPointer() : "?");
    release(errors);
    return code;
}

static ID3D11Buffer *constant_buffer(UINT bytes)
{
    D3D11_BUFFER_DESC d = { bytes, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE };
    ID3D11Buffer *b = nullptr;
    check(g_dev->CreateBuffer(&d, nullptr, &b), "CreateBuffer(cb)");
    return b;
}

static void upload(ID3D11Buffer *b, const void *data, UINT bytes)
{
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(g_ctx->Map(b, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        memcpy(m.pData, data, bytes);
        g_ctx->Unmap(b, 0);
    }
}

static bool create_pipeline()
{
    for (int flat = 0; flat < 2; flat++) {
        ID3DBlob *vs = compile("vs_tl", "vs_4_0", flat != 0), *ps = compile("ps_tl", "ps_4_0", flat != 0);
        if (!vs || !ps)
            return false;
        g_dev->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &g_vs_tl[flat]);
        g_dev->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &g_ps_tl[flat]);
        if (!flat) {
            /* D3DTLVERTEX: sx sy sz rhw, color, specular, tu tv (FVF 0x1C4) */
            D3D11_INPUT_ELEMENT_DESC il[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            };
            check(g_dev->CreateInputLayout(il, 4, vs->GetBufferPointer(), vs->GetBufferSize(), &g_layout),
                  "CreateInputLayout");
        }
        release(vs);
        release(ps);
    }
    ID3DBlob *vq = compile("vs_quad", "vs_4_0", false), *pb = compile("ps_blit", "ps_4_0", false),
             *pp = compile("ps_present", "ps_4_0", false);
    if (!vq || !pb || !pp)
        return false;
    g_dev->CreateVertexShader(vq->GetBufferPointer(), vq->GetBufferSize(), nullptr, &g_vs_quad);
    g_dev->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &g_ps_blit);
    g_dev->CreatePixelShader(pp->GetBufferPointer(), pp->GetBufferSize(), nullptr, &g_ps_present);
    release(vq);
    release(pb);
    release(pp);

    g_cb_frame = constant_buffer(16);
    g_cb_draw = constant_buffer(sizeof(DrawConstants));
    g_cb_blit = constant_buffer(sizeof(BlitConstants));
    D3D11_BUFFER_DESC vb = { kVbBytes, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE };
    check(g_dev->CreateBuffer(&vb, nullptr, &g_vb), "CreateBuffer(vb)");

    for (int i = 0; i < 4; i++) {
        D3D11_SAMPLER_DESC s = {};
        s.Filter = (i & 1) ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_MAG_MIP_POINT;
        s.AddressU = s.AddressV = s.AddressW = (i & 2) ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
        s.MaxLOD = D3D11_FLOAT32_MAX;
        g_dev->CreateSamplerState(&s, &g_samplers[i]);
    }
    for (int i = 0; i < 3; i++) {
        D3D11_RASTERIZER_DESC r = {};
        r.FillMode = D3D11_FILL_SOLID;
        r.CullMode = i == 0 ? D3D11_CULL_NONE : i == 1 ? D3D11_CULL_FRONT : D3D11_CULL_BACK;
        r.DepthClipEnable = FALSE; /* D3D7 did not clip transformed vertices against z */
        r.ScissorEnable = TRUE;    /* the D3D7 viewport, or the whole target for 2D */
        g_dev->CreateRasterizerState(&r, &g_raster[i]);
    }
    D3D11_DEPTH_STENCIL_DESC nd = {};
    g_dev->CreateDepthStencilState(&nd, &g_no_depth);
    D3D11_BLEND_DESC nb = {};
    nb.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    g_dev->CreateBlendState(&nb, &g_no_blend);
    return true;
}

bool init(HWND hwnd, int scale)
{
    static bool failed;
    if (g_dev || failed)
        return g_dev != nullptr;
    failed = true; /* cleared below on success: one attempt only */
    g_hwnd = hwnd;
    g_scale_cfg = scale;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    if (!check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 3, D3D11_SDK_VERSION,
                                 &g_dev, nullptr, &g_ctx),
               "D3D11CreateDevice"))
        return false;

    IDXGIDevice *xd = nullptr;
    IDXGIAdapter *ad = nullptr;
    IDXGIFactory2 *fac = nullptr;
    g_dev->QueryInterface(__uuidof(IDXGIDevice), (void **)&xd);
    xd->GetAdapter(&ad);
    ad->GetParent(__uuidof(IDXGIFactory2), (void **)&fac);
    RECT rc;
    GetClientRect(hwnd, &rc);
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = g_swap_w = rc.right > 0 ? rc.right : 640;
    sd.Height = g_swap_h = rc.bottom > 0 ? rc.bottom : 480;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    bool ok = check(fac->CreateSwapChainForHwnd(g_dev, hwnd, &sd, nullptr, nullptr, &g_swap), "CreateSwapChainForHwnd");
    if (ok)
        fac->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    release(fac);
    release(ad);
    release(xd);
    if (!ok || !create_pipeline()) {
        /* leave nothing half-built: without g_dev every entry point is a no-op */
        release(g_swap);
        release(g_ctx);
        release(g_dev);
        log_printf("gpu: initialisation failed, nothing will be drawn");
        return false;
    }
    ID3D11Texture2D *bb = nullptr;
    g_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb);
    g_dev->CreateRenderTargetView(bb, nullptr, &g_swap_rtv);
    release(bb);
    log_printf("gpu: Direct3D 11 ready, window %dx%d", g_swap_w, g_swap_h);
    failed = false;
    return true;
}

bool ready() { return g_dev && g_back; }
int width() { return g_w; }
int height() { return g_h; }

void set_mode(int w, int h)
{
    if (!g_dev || (w == g_w && h == g_h && g_back))
        return;
    release(g_back_srv);
    release(g_back_rtv);
    release(g_back);
    release(g_dsv);
    release(g_depth);
    release(g_small_rtv);
    release(g_small);
    release(g_staging);
    g_w = w;
    g_h = h;
    g_scale = g_scale_cfg;
    if (g_scale <= 0) { /* auto: as many whole multiples as fit the monitor */
        MONITORINFO mi = { sizeof mi };
        GetMonitorInfo(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        int sx = (mi.rcMonitor.right - mi.rcMonitor.left) / w, sy = (mi.rcMonitor.bottom - mi.rcMonitor.top) / h;
        g_scale = sx < sy ? sx : sy;
        if (g_scale < 1)
            g_scale = 1;
    }
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = w * g_scale;
    d.Height = h * g_scale;
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    check(g_dev->CreateTexture2D(&d, nullptr, &g_back), "CreateTexture2D(back)");
    g_dev->CreateRenderTargetView(g_back, nullptr, &g_back_rtv);
    g_dev->CreateShaderResourceView(g_back, nullptr, &g_back_srv);
    d.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    check(g_dev->CreateTexture2D(&d, nullptr, &g_depth), "CreateTexture2D(depth)");
    g_dev->CreateDepthStencilView(g_depth, nullptr, &g_dsv);
    d.Width = w;
    d.Height = h;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.BindFlags = D3D11_BIND_RENDER_TARGET;
    check(g_dev->CreateTexture2D(&d, nullptr, &g_small), "CreateTexture2D(small)");
    g_dev->CreateRenderTargetView(g_small, nullptr, &g_small_rtv);
    d.BindFlags = 0;
    d.Usage = D3D11_USAGE_STAGING;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    check(g_dev->CreateTexture2D(&d, nullptr, &g_staging), "CreateTexture2D(staging)");
    float size[4] = { (float)w, (float)h, 0, 0 };
    upload(g_cb_frame, size, sizeof size);
    clear_target(0);
    clear_depth(1.0f);
    log_printf("gpu: mode %dx%d, rendering at %dx%d", w, h, w * g_scale, h * g_scale);
}

static void bind_back(bool depth)
{
    D3D11_VIEWPORT vp = { 0, 0, (float)(g_w * g_scale), (float)(g_h * g_scale), 0, 1 };
    g_ctx->OMSetRenderTargets(1, &g_back_rtv, depth ? g_dsv : nullptr);
    g_ctx->RSSetViewports(1, &vp);
}

void set_clip(int x, int y, int w, int h)
{
    g_clip = { x, y, x + w, y + h };
}

void clear_depth(float z)
{
    if (g_dsv)
        g_ctx->ClearDepthStencilView(g_dsv, D3D11_CLEAR_DEPTH, z, 0);
}

static void unpack(uint32_t c, float out[4])
{
    out[0] = ((c >> 16) & 255) / 255.f;
    out[1] = ((c >> 8) & 255) / 255.f;
    out[2] = (c & 255) / 255.f;
    out[3] = (c >> 24) / 255.f;
}

void clear_target(uint32_t color)
{
    float c[4];
    unpack(color, c);
    c[3] = 1;
    if (g_back_rtv)
        g_ctx->ClearRenderTargetView(g_back_rtv, c);
}

static ID3D11BlendState *blend_state(const DrawState &s)
{
    static const D3D11_BLEND map[] = { D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_ONE,
        D3D11_BLEND_SRC_COLOR, D3D11_BLEND_INV_SRC_COLOR, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA,
        D3D11_BLEND_DEST_ALPHA, D3D11_BLEND_INV_DEST_ALPHA, D3D11_BLEND_DEST_COLOR, D3D11_BLEND_INV_DEST_COLOR,
        D3D11_BLEND_SRC_ALPHA_SAT };
    if (!s.blend)
        return g_no_blend;
    uint32_t key = s.src_blend | s.dst_blend << 8;
    auto it = g_blend.find(key);
    if (it != g_blend.end())
        return it->second;
    D3D11_BLEND_DESC d = {};
    D3D11_RENDER_TARGET_BLEND_DESC &rt = d.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = s.src_blend < 12 ? map[s.src_blend] : D3D11_BLEND_ONE;
    rt.DestBlend = s.dst_blend < 12 ? map[s.dst_blend] : D3D11_BLEND_ZERO;
    rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D11_BLEND_ONE;
    rt.DestBlendAlpha = D3D11_BLEND_ZERO;
    rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ID3D11BlendState *b = nullptr;
    g_dev->CreateBlendState(&d, &b);
    return g_blend[key] = b;
}

static ID3D11DepthStencilState *depth_state(const DrawState &s)
{
    uint32_t key = s.ztest | s.zwrite << 1 | s.zfunc << 2;
    auto it = g_depth_states.find(key);
    if (it != g_depth_states.end())
        return it->second;
    D3D11_DEPTH_STENCIL_DESC d = {};
    d.DepthEnable = s.ztest ? TRUE : FALSE;
    d.DepthWriteMask = s.zwrite ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    /* D3DCMPFUNC and D3D11_COMPARISON_FUNC share the values 1..8 */
    d.DepthFunc = (D3D11_COMPARISON_FUNC)(s.zfunc >= 1 && s.zfunc <= 8 ? s.zfunc : D3D11_COMPARISON_LESS_EQUAL);
    ID3D11DepthStencilState *ds = nullptr;
    g_dev->CreateDepthStencilState(&d, &ds);
    return g_depth_states[key] = ds;
}

static UINT push_vertices(const void *data, UINT bytes)
{
    D3D11_MAPPED_SUBRESOURCE m;
    D3D11_MAP mode = D3D11_MAP_WRITE_NO_OVERWRITE;
    if (g_vb_pos + bytes > kVbBytes) {
        g_vb_pos = 0;
        mode = D3D11_MAP_WRITE_DISCARD;
    }
    if (FAILED(g_ctx->Map(g_vb, 0, mode, 0, &m)))
        return ~0u;
    memcpy((char *)m.pData + g_vb_pos, data, bytes);
    g_ctx->Unmap(g_vb, 0);
    UINT at = g_vb_pos;
    g_vb_pos += bytes;
    return at;
}

void draw(D3DPRIMITIVETYPE type, const void *verts, uint32_t count, const DrawState &s, Texture *tex)
{
    if (!ready() || !count)
        return;
    static std::vector<D3DTLVERTEX> fan;
    D3D11_PRIMITIVE_TOPOLOGY topo;
    const D3DTLVERTEX *v = (const D3DTLVERTEX *)verts;
    switch (type) {
    case D3DPT_POINTLIST: topo = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST; break;
    case D3DPT_LINELIST: topo = D3D11_PRIMITIVE_TOPOLOGY_LINELIST; break;
    case D3DPT_LINESTRIP: topo = D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP; break;
    case D3DPT_TRIANGLELIST: topo = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST; break;
    case D3DPT_TRIANGLESTRIP: topo = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP; break;
    case D3DPT_TRIANGLEFAN: /* no fans in D3D11: expand to a list, keeping the first vertex first */
        if (count < 3)
            return;
        fan.clear();
        for (uint32_t i = 1; i + 1 < count; i++) {
            fan.push_back(v[0]);
            fan.push_back(v[i]);
            fan.push_back(v[i + 1]);
        }
        v = fan.data();
        count = (uint32_t)fan.size();
        topo = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        break;
    default:
        log_printf("gpu: primitive type %d not supported", type);
        return;
    }

    DrawConstants dc = {};
    dc.color[0] = s.color_op;
    dc.color[1] = s.color_arg1;
    dc.color[2] = s.color_arg2;
    dc.color[3] = tex != nullptr;
    dc.alpha[0] = s.alpha_op;
    dc.alpha[1] = s.alpha_arg1;
    dc.alpha[2] = s.alpha_arg2;
    dc.alpha[3] = s.alpha_test ? s.alpha_func : 0;
    dc.fog[0] = s.fog ? 1.f : 0.f;
    dc.fog[1] = s.fog_start;
    dc.fog[2] = s.fog_end;
    dc.fog[3] = s.alpha_ref;
    unpack(s.fog_color, dc.fog_color);
    unpack(s.tfactor, dc.tfactor);
    upload(g_cb_draw, &dc, sizeof dc);

    UINT at = push_vertices(v, count * sizeof(D3DTLVERTEX));
    if (at == ~0u)
        return;
    UINT stride = sizeof(D3DTLVERTEX), offset = 0;
    bind_back(true);
    g_ctx->IASetInputLayout(g_layout);
    g_ctx->IASetVertexBuffers(0, 1, &g_vb, &stride, &offset);
    g_ctx->IASetPrimitiveTopology(topo);
    g_ctx->VSSetShader(g_vs_tl[s.flat ? 1 : 0], nullptr, 0);
    g_ctx->VSSetConstantBuffers(0, 1, &g_cb_frame);
    g_ctx->PSSetShader(g_ps_tl[s.flat ? 1 : 0], nullptr, 0);
    g_ctx->PSSetConstantBuffers(1, 1, &g_cb_draw);
    ID3D11ShaderResourceView *srv = tex ? tex->srv : nullptr;
    g_ctx->PSSetShaderResources(0, 1, &srv);
    g_ctx->PSSetSamplers(0, 1, &g_samplers[(s.linear ? 1 : 0) | (s.wrap ? 2 : 0)]);
    g_ctx->RSSetState(g_raster[s.cull >= 1 && s.cull <= 3 ? s.cull - 1 : 0]);
    D3D11_RECT sc = { g_clip.left * g_scale, g_clip.top * g_scale, g_clip.right * g_scale, g_clip.bottom * g_scale };
    g_ctx->RSSetScissorRects(1, &sc);
    g_ctx->OMSetBlendState(blend_state(s), nullptr, 0xFFFFFFFF);
    g_ctx->OMSetDepthStencilState(depth_state(s), 0);
    g_ctx->Draw(count, at / sizeof(D3DTLVERTEX));
}

/* A 4-vertex strip over dst (game pixels) with the blit pixel shader or present shader. */
static void quad(const RECT &dst, float tw, float th, const float src[4], const uint32_t key[4])
{
    BlitConstants bc;
    bc.dst[0] = dst.left * 2.f / tw - 1;
    bc.dst[1] = 1 - dst.top * 2.f / th;
    bc.dst[2] = dst.right * 2.f / tw - 1;
    bc.dst[3] = 1 - dst.bottom * 2.f / th;
    memcpy(bc.src, src, sizeof bc.src);
    memcpy(bc.key, key, sizeof bc.key);
    upload(g_cb_blit, &bc, sizeof bc);
    g_ctx->IASetInputLayout(nullptr);
    g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    g_ctx->VSSetShader(g_vs_quad, nullptr, 0);
    g_ctx->VSSetConstantBuffers(2, 1, &g_cb_blit);
    g_ctx->PSSetConstantBuffers(2, 1, &g_cb_blit);
    g_ctx->RSSetState(g_raster[0]);
    D3D11_RECT all = { 0, 0, 16384, 16384 }; /* DirectDraw blits ignore the D3D viewport */
    g_ctx->RSSetScissorRects(1, &all);
    g_ctx->OMSetBlendState(g_no_blend, nullptr, 0xFFFFFFFF);
    g_ctx->OMSetDepthStencilState(g_no_depth, 0);
    g_ctx->Draw(4, 0);
}

void fill(const RECT &dst, uint16_t rgb565)
{
    if (!ready())
        return;
    float src[4] = {};
    uint32_t key[4] = { 0, 0, 1, rgb565 };
    bind_back(false);
    g_ctx->PSSetShader(g_ps_blit, nullptr, 0);
    quad(dst, (float)g_w, (float)g_h, src, key);
}

static BlitSource *source(const void *owner, uint32_t version, const uint16_t *bits, int pitch, int w, int h)
{
    BlitSource &s = g_sources[owner];
    if (s.tex && (s.w != w || s.h != h)) {
        release(s.srv);
        release(s.tex);
    }
    if (!s.tex) {
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = w;
        d.Height = h;
        d.MipLevels = d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R16_UINT;
        d.SampleDesc.Count = 1;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (!check(g_dev->CreateTexture2D(&d, nullptr, &s.tex), "CreateTexture2D(blit)"))
            return nullptr;
        g_dev->CreateShaderResourceView(s.tex, nullptr, &s.srv);
        s.w = w;
        s.h = h;
        s.version = version - 1;
    }
    if (s.version != version) {
        g_ctx->UpdateSubresource(s.tex, 0, nullptr, bits, pitch, 0);
        s.version = version;
    }
    return &s;
}

void blit(const RECT &dst, const void *owner, uint32_t version, const uint16_t *bits, int pitch, int w, int h,
          const RECT &src, int key, bool mirror_x, bool mirror_y)
{
    if (!ready())
        return;
    BlitSource *s = source(owner, version, bits, pitch, w, h);
    if (!s)
        return;
    float r[4] = { (float)src.left, (float)src.top, (float)src.right, (float)src.bottom };
    if (mirror_x) {
        r[0] = (float)src.right;
        r[2] = (float)src.left;
    }
    if (mirror_y) {
        r[1] = (float)src.bottom;
        r[3] = (float)src.top;
    }
    uint32_t k[4] = { (uint32_t)(key & 0xFFFF), key >= 0 ? 1u : 0u, 0, 0 };
    bind_back(false);
    g_ctx->PSSetShader(g_ps_blit, nullptr, 0);
    g_ctx->PSSetShaderResources(1, 1, &s->srv);
    quad(dst, (float)g_w, (float)g_h, r, k);
}

void forget(const void *owner)
{
    auto it = g_sources.find(owner);
    if (it == g_sources.end())
        return;
    release(it->second.srv);
    release(it->second.tex);
    g_sources.erase(it);
}

void read_back(uint16_t *bits, int pitch)
{
    if (!ready())
        return;
    /* scale down into g_small with the present shader, then copy to the CPU */
    D3D11_VIEWPORT vp = { 0, 0, (float)g_w, (float)g_h, 0, 1 };
    g_ctx->OMSetRenderTargets(1, &g_small_rtv, nullptr);
    g_ctx->RSSetViewports(1, &vp);
    g_ctx->PSSetShader(g_ps_present, nullptr, 0);
    g_ctx->PSSetShaderResources(0, 1, &g_back_srv);
    g_ctx->PSSetSamplers(0, 1, &g_samplers[1]);
    RECT all = { 0, 0, g_w, g_h };
    float uv[4] = { 0, 0, 1, 1 };
    uint32_t k[4] = {};
    quad(all, (float)g_w, (float)g_h, uv, k);
    ID3D11ShaderResourceView *none = nullptr;
    g_ctx->PSSetShaderResources(0, 1, &none);
    g_ctx->CopyResource(g_staging, g_small);
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(g_ctx->Map(g_staging, 0, D3D11_MAP_READ, 0, &m)))
        return;
    for (int y = 0; y < g_h; y++) {
        const uint8_t *s = (const uint8_t *)m.pData + y * m.RowPitch;
        uint16_t *d = (uint16_t *)((uint8_t *)bits + y * pitch);
        for (int x = 0; x < g_w; x++, s += 4)
            d[x] = (uint16_t)((s[2] >> 3) << 11 | (s[1] >> 2) << 5 | s[0] >> 3);
    }
    g_ctx->Unmap(g_staging, 0);
}

void write_back(const uint16_t *bits, int pitch)
{
    RECT all = { 0, 0, g_w, g_h };
    static const int kOwner = 0;
    static uint32_t version;
    blit(all, &kOwner, ++version, bits, pitch, g_w, g_h, all, -1, false, false);
}

static void resize_swap_chain()
{
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0 || (w == g_swap_w && h == g_swap_h))
        return;
    release(g_swap_rtv);
    g_ctx->OMSetRenderTargets(0, nullptr, nullptr);
    if (!check(g_swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0), "ResizeBuffers"))
        return;
    g_swap_w = w;
    g_swap_h = h;
    ID3D11Texture2D *bb = nullptr;
    g_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb);
    g_dev->CreateRenderTargetView(bb, nullptr, &g_swap_rtv);
    release(bb);
}

void present(bool vsync)
{
    if (!ready() || IsIconic(g_hwnd))
        return;
    resize_swap_chain();
    if (!g_swap_rtv)
        return;
    /* keep the game's aspect ratio inside the window: black bars on the sides or top */
    float black[4] = { 0, 0, 0, 1 };
    g_ctx->ClearRenderTargetView(g_swap_rtv, black);
    int w = g_swap_w, h = g_swap_w * g_h / g_w;
    if (h > g_swap_h) {
        h = g_swap_h;
        w = g_swap_h * g_w / g_h;
    }
    RECT dst = { (g_swap_w - w) / 2, (g_swap_h - h) / 2, (g_swap_w - w) / 2 + w, (g_swap_h - h) / 2 + h };
    D3D11_VIEWPORT vp = { 0, 0, (float)g_swap_w, (float)g_swap_h, 0, 1 };
    g_ctx->OMSetRenderTargets(1, &g_swap_rtv, nullptr);
    g_ctx->RSSetViewports(1, &vp);
    g_ctx->PSSetShader(g_ps_present, nullptr, 0);
    g_ctx->PSSetShaderResources(0, 1, &g_back_srv);
    g_ctx->PSSetSamplers(0, 1, &g_samplers[1]);
    float uv[4] = { 0, 0, 1, 1 };
    uint32_t k[4] = {};
    quad(dst, (float)g_swap_w, (float)g_swap_h, uv, k);
    ID3D11ShaderResourceView *none = nullptr;
    g_ctx->PSSetShaderResources(0, 1, &none);
    g_swap->Present(vsync ? 1 : 0, 0);
}

Texture *texture_create(int w, int h)
{
    if (!g_dev)
        return nullptr;
    Texture *t = new Texture();
    D3D11_TEXTURE2D_DESC d = {};
    d.Width = w;
    d.Height = h;
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (!check(g_dev->CreateTexture2D(&d, nullptr, &t->tex), "CreateTexture2D(texture)")) {
        delete t;
        return nullptr;
    }
    g_dev->CreateShaderResourceView(t->tex, nullptr, &t->srv);
    t->w = w;
    t->h = h;
    return t;
}

void texture_upload(Texture *t, const uint16_t *bits, int pitch)
{
    static std::vector<uint32_t> px;
    px.resize((size_t)t->w * t->h);
    for (int y = 0; y < t->h; y++) {
        const uint16_t *s = (const uint16_t *)((const uint8_t *)bits + y * pitch);
        for (int x = 0; x < t->w; x++) {
            uint32_t p = s[x], r = (p >> 10) & 31, g = (p >> 5) & 31, b = p & 31;
            px[(size_t)y * t->w + x] = (p & 0x8000 ? 0xFF000000u : 0) | (r << 3 | r >> 2) << 16 |
                                       (g << 3 | g >> 2) << 8 | (b << 3 | b >> 2);
        }
    }
    g_ctx->UpdateSubresource(t->tex, 0, nullptr, px.data(), t->w * 4, 0);
}

void texture_free(Texture *t)
{
    if (!t)
        return;
    release(t->srv);
    release(t->tex);
    delete t;
}

} // namespace gpu
