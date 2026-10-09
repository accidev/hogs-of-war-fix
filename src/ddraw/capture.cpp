/* Frame dump for renderer bugs, on with hogs.ini [Debug] FrameDump=1. F12 writes the next
 * frame to hogsdraw_dump\ next to the game:
 *   NNN.txt           every draw, blit and clear in order, with the calling code address,
 *                     the D3D states and the vertex ranges of each draw;
 *   NNN.bmp           the finished frame at game resolution;
 *   tex_XXXXXXXX.bmp  each texture the frame drew with (transparent texels in magenta). */
#include "objects.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <set>

bool g_capturing;
static FILE *g_file;
static int g_number, g_draws;
static std::set<Surface *> g_textures;
static char g_dir[MAX_PATH];

void capture_note(const char *fmt, ...)
{
    if (!g_file)
        return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_file, fmt, ap);
    va_end(ap);
    fputc('\n', g_file);
}

/* "_d3d.dll+1234" for a code address, so it can be looked up in the disassembly */
static void where(const void *code, char *out, size_t size)
{
    MEMORY_BASIC_INFORMATION m = {};
    char name[MAX_PATH] = "?";
    VirtualQuery(code, &m, sizeof m);
    if (m.AllocationBase)
        GetModuleFileNameA((HMODULE)m.AllocationBase, name, MAX_PATH);
    const char *file = strrchr(name, '\\') ? strrchr(name, '\\') + 1 : name;
    snprintf(out, size, "%s+%X", file, (unsigned)((const char *)code - (const char *)m.AllocationBase));
}

void capture_draw(const void *caller, D3DPRIMITIVETYPE type, const D3DTLVERTEX *v, DWORD n, const gpu::DrawState &s,
                  Surface *tex)
{
    float lo[6] = { 1e30f, 1e30f, 1e30f, 1e30f, 1e30f, 1e30f }, hi[6] = { -1e30f, -1e30f, -1e30f, -1e30f, -1e30f, -1e30f };
    int a_lo = 255, a_hi = 0;
    for (DWORD i = 0; i < n; i++) {
        const float f[6] = { v[i].sx, v[i].sy, v[i].sz, v[i].rhw, v[i].tu, v[i].tv };
        for (int k = 0; k < 6; k++) {
            lo[k] = std::min(lo[k], f[k]);
            hi[k] = std::max(hi[k], f[k]);
        }
        a_lo = std::min(a_lo, (int)(v[i].color >> 24));
        a_hi = std::max(a_hi, (int)(v[i].color >> 24));
    }
    if (tex)
        g_textures.insert(tex);
    char from[MAX_PATH + 16];
    where(caller, from, sizeof from);
    capture_note("draw %d from %s: prim %d n %lu tex %p %dx%d | blend %d %d/%d z %d%d%d cull %d atest %d/%d fog %d "
                 "flat %d colorop %d(%d,%d) alphaop %d(%d,%d) | x %.1f..%.1f y %.1f..%.1f z %.3g..%.3g rhw %.3g..%.3g "
                 "u %.3f..%.3f v %.3f..%.3f alpha %d..%d color0 %08lX",
                 g_draws++, from, type, n, (void *)tex, tex ? tex->w : 0, tex ? tex->h : 0, s.blend, s.src_blend,
                 s.dst_blend, s.ztest, s.zwrite, s.zfunc, s.cull, s.alpha_test ? s.alpha_func : 0, s.alpha_ref, s.fog,
                 s.flat, s.color_op, s.color_arg1, s.color_arg2, s.alpha_op, s.alpha_arg1, s.alpha_arg2, lo[0], hi[0],
                 lo[1], hi[1], lo[2], hi[2], lo[3], hi[3], lo[4], hi[4], lo[5], hi[5], a_lo, a_hi, n ? v[0].color : 0);
}

/* 24-bit BMP; pixel(x, y) returns 0xRRGGBB */
template <class F> static void write_bmp(const char *path, int w, int h, F pixel)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    DWORD row = (w * 3 + 3) & ~3u, head = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    BITMAPFILEHEADER fh = { 0x4D42, head + row * h, 0, 0, head };
    BITMAPINFOHEADER ih = { sizeof ih, w, h, 1, 24 };
    fwrite(&fh, sizeof fh, 1, f);
    fwrite(&ih, sizeof ih, 1, f);
    std::vector<uint8_t> line(row);
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            uint32_t c = pixel(x, y);
            line[x * 3] = (uint8_t)c;
            line[x * 3 + 1] = (uint8_t)(c >> 8);
            line[x * 3 + 2] = (uint8_t)(c >> 16);
        }
        fwrite(line.data(), 1, row, f);
    }
    fclose(f);
}

static uint32_t expand(uint32_t r, uint32_t g, uint32_t b, int gbits)
{
    return (r << 3 | r >> 2) << 16 | (gbits == 6 ? (g << 2 | g >> 4) : (g << 3 | g >> 2)) << 8 | (b << 3 | b >> 2);
}

static void finish()
{
    char path[MAX_PATH];
    capture_note("end of frame: %d game draws sent as %d Direct3D 11 draws", g_draws, gpu::frame_draws());
    fclose(g_file);
    g_file = nullptr;
    g_capturing = false;

    int w = gpu::width(), h = gpu::height();
    std::vector<uint16_t> frame((size_t)w * h);
    gpu::read_back(frame.data(), w * 2);
    snprintf(path, sizeof path, "%s%03d.bmp", g_dir, g_number);
    write_bmp(path, w, h, [&](int x, int y) {
        uint32_t p = frame[(size_t)y * w + x];
        return expand(p >> 11, (p >> 5) & 63, p & 31, 6);
    });
    for (Surface *t : g_textures) {
        if (std::find(g_surfaces.begin(), g_surfaces.end(), t) == g_surfaces.end() || !t->bits)
            continue;
        snprintf(path, sizeof path, "%stex_%p.bmp", g_dir, (void *)t);
        write_bmp(path, t->w, t->h, [&](int x, int y) {
            uint32_t p = ((const uint16_t *)(t->bits + y * t->pitch))[x];
            return p & 0x8000 ? expand((p >> 10) & 31, (p >> 5) & 31, p & 31, 5) : 0xFF00FFu;
        });
    }
    g_textures.clear();
    log_printf("frame dump %03d written to %s", g_number, g_dir);
}

void capture_frame_end()
{
    if (g_capturing)
        finish();
    if (!g_config.frame_dump)
        return;
    static bool was_down;
    static int frames;
    bool down = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
    bool at = g_config.frame_dump_at && ++frames == g_config.frame_dump_at; /* for unattended tests */
    if (((down && !was_down) || at) && gpu::ready()) {
        char path[MAX_PATH];
        if (!g_dir[0]) {
            GetModuleFileNameA(nullptr, g_dir, MAX_PATH);
            strcpy(strrchr(g_dir, '\\') + 1, "hogsdraw_dump\\");
            CreateDirectoryA(g_dir, nullptr);
        }
        do /* keep the dumps of earlier runs */
            snprintf(path, sizeof path, "%s%03d.txt", g_dir, ++g_number);
        while (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES);
        g_file = fopen(path, "w");
        g_capturing = g_file != nullptr;
        g_draws = 0;
        capture_note("frame dump %03d: game %dx%d, _d3d.dll at %p, warhogs_.exe at %p (blit callers are absolute)",
                     g_number, gpu::width(), gpu::height(), (void *)GetModuleHandleA("_d3d.dll"),
                     (void *)GetModuleHandleA(nullptr));
    }
    was_down = down;
}
