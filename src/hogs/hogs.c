/* Runtime fixes for Hogs of War (2000), warhogs_.exe v1.2, built into ddraw.dll with the renderer.
 * Patched exe imports CallDLL + DirectDraw from ddraw.dll: DllMain (src/ddraw/main.cpp) runs before the
 * entry point and calls hogs_init(). Fixes go in only for an exe importing CallDLL; each checks the
 * original bytes first. Settings hogs.ini, log hogs.log, next to the exe. */
#include "hogs.h"
#include <mmsystem.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *g_log;
char g_ini[MAX_PATH];

void say(const char *fmt, ...)
{
    va_list ap;
    if (!g_log)
        return;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

BOOL patch(DWORD va, const void *old, const void *new_bytes, SIZE_T len, const char *what)
{
    BYTE *p = (BYTE *)(ULONG_PTR)va;
    DWORD prot;

    if (memcmp(p, old, len) != 0) {
        say("skip %-40s %08X: unexpected bytes, unknown game build?", what, va);
        return FALSE;
    }
    VirtualProtect(p, len, PAGE_EXECUTE_READWRITE, &prot);
    memcpy(p, new_bytes, len);
    VirtualProtect(p, len, prot, &prot);
    FlushInstructionCache(GetCurrentProcess(), p, len);
    say("ok   %-40s %08X", what, va);
    return TRUE;
}

/* jmp (0xE9) or call (0xE8) to fn over 5 bytes at va; fn takes the replaced code's convention and arguments */
BOOL hook(DWORD va, BYTE op, const void *old5, const void *fn, const char *what)
{
    BYTE jmp[5] = { op };
    DWORD rel = (DWORD)(ULONG_PTR)fn - (va + 5);

    memcpy(jmp + 1, &rel, 4);
    return patch(va, old5, jmp, 5, what);
}

/* import slot -> repl if it holds dll!fn; *orig gets the real function */
BOOL hook_import(DWORD slot_va, const char *dll, const char *fn, const void *repl, void *orig, const char *what)
{
    FARPROC *slot = (FARPROC *)(ULONG_PTR)slot_va;
    FARPROC real = GetProcAddress(GetModuleHandleA(dll), fn);
    DWORD prot;

    if (!real || *slot != real) {
        say("skip %-40s %08X: slot does not hold %s!%s", what, slot_va, dll, fn);
        return FALSE;
    }
    if (orig)
        *(FARPROC *)orig = real;
    VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &prot);
    *slot = (FARPROC)repl;
    VirtualProtect(slot, sizeof *slot, prot, &prot);
    say("ok   %-40s %08X", what, slot_va);
    return TRUE;
}

/* Data\_d3d.dll is loaded by LoadLibraryA at 0x4ADB03; fix_renderer() runs right after */
static HMODULE(WINAPI *g_load_library)(LPCSTR);

/* [Display] Widescreen=1 (Hor+). Battle resolution: cfg+0x44C width, +0x450 height, set by FUN_00481230 */
static int g_widescreen;

static BOOL wide_battle(int *w, int *h)
{
    const int *cfg = (const int *)GAME_CONFIG;

    if (!g_widescreen || !cfg || cfg[0x44C / 4] * 3 <= cfg[0x450 / 4] * 4)
        return FALSE;
    *w = cfg[0x44C / 4];
    *h = cfg[0x450 / 4];
    return TRUE;
}

/* Terrain masks language\tims\nview00N.bmp, 33x33, black = draw that block of 4x4 tiles: the renderer reads
 * 16-bit entries via GetBitmapBits, which a 32-bit desktop breaks. Return the bits of a 16-bit bitmap. */
static LONG WINAPI bitmap_bits_16bpp(HBITMAP bm, LONG size, LPVOID out)
{
    BITMAPINFO bi = { { sizeof bi.bmiHeader } };
    BITMAP b;
    BYTE *dib;
    HDC dc;
    LONG row, stride, y, n = 0;
    int w, h;

    /* masks are 4:3 wedges, only an optimisation: a wide battle draws all 16x16 blocks;
     * FUN_10003380 still culls tiles behind the camera */
    if (wide_battle(&w, &h)) {
        static BOOL said;

        if (!said)
            say("battles are %dx%d: all ground blocks are drawn", w, h);
        said = TRUE;
        memset(out, 0, size);
        return size;
    }
    if (!GetObjectA(bm, sizeof b, &b))
        return 0;
    row = b.bmWidth * 2;     /* WORD aligned, no padding at 16 bpp */
    stride = (row + 3) & ~3; /* DWORD aligned */
    bi.bmiHeader.biWidth = b.bmWidth;
    bi.bmiHeader.biHeight = -b.bmHeight; /* top-down */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 16; /* BI_RGB: X1R5G5B5 */
    dib = (BYTE *)malloc((size_t)stride * b.bmHeight);
    if (!dib)
        return 0;
    dc = GetDC(NULL);
    if (GetDIBits(dc, bm, 0, b.bmHeight, dib, &bi, DIB_RGB_COLORS))
        for (y = 0; y < b.bmHeight && n < size; y++) {
            LONG k = row < size - n ? row : size - n;
            memcpy((BYTE *)out + n, dib + y * stride, k);
            n += k;
        }
    ReleaseDC(NULL, dc);
    free(dib);
    return n;
}

/* resolution of the current screen (menus 640x480), set by size_game_window() */
static int g_game_w, g_game_h;

/* _d3d.dll sizes its back buffer from GetClientRect (FUN_10005820); hogsdraw scales the frame into the
 * window, so report the game resolution */
static BOOL WINAPI game_client_rect(HWND hwnd, LPRECT r)
{
    if (!g_game_w || hwnd != GAME_WINDOW)
        return GetClientRect(hwnd, r);
    return SetRect(r, 0, 0, g_game_w, g_game_h);
}

/* Hor+: world geometry (FUN_10013830) takes fx = width x zoom x 0.05, fy = height x zoom / 15 (equal at 4:3).
 * The 0.05 float is shared by SetDisplayWindow, FUN_10005820, FUN_100075D0, afSetDDGlobals: set to h / (15 w) */
static float *g_fx_factor;

static void set_fx_factor(int w, int h)
{
    DWORD prot;

    if (!g_fx_factor || w <= 0)
        return;
    VirtualProtect(g_fx_factor, sizeof *g_fx_factor, PAGE_READWRITE, &prot);
    *g_fx_factor = (float)h / (15.0f * (float)w);
    VirtualProtect(g_fx_factor, sizeof *g_fx_factor, prot, &prot);
}

static void fix_renderer(HMODULE renderer)
{
    DWORD base = (DWORD)(ULONG_PTR)renderer;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + ((const IMAGE_DOS_HEADER *)renderer)->e_lfanew);

    /* patch() reads the RVAs below: they must lie inside the image */
    if (nt->OptionalHeader.SizeOfImage != 0x1EE9000) {
        say("skip renderer fixes: unknown _d3d.dll (image size %08X)", nt->OptionalHeader.SizeOfImage);
        return;
    }

    /* CopyToScreen: AdjustWindowRectEx bMenu=TRUE, but the window has no menu; push 1 -> push 0 */
    patch(base + 0x321B, "\x50\x6A\x01\x8D\x4C\x24\x24", "\x50\x6A\x00\x8D\x4C\x24\x24", 7,
          "renderer: blit at the real client origin");

    /* GetBitmapBits: one caller, the nview mask loader FUN_10014120 */
    hook_import(base + 0x1ED719C, "gdi32.dll", "GetBitmapBits", bitmap_bits_16bpp, NULL,
                "renderer: 16-bit terrain masks");

    /* FUN_10011810 clamps view z to 1.527e-5: vertices behind the camera stretch their triangles. Store -1.0,
     * the emitters skip z < 0; the other caller (visibility test) needs z > 15 either way */
    patch(base + 0x118D8, "\xC7\x02\x13\x18\x80\x37", "\xC7\x02\x00\x00\x80\xBF", 6,
          "renderer: drop objects behind the camera");

    hook_import(base + 0x1ED7298, "user32.dll", "GetClientRect", game_client_rect, NULL,
                "renderer: game resolution as client size");

    /* FUN_1000B440 water plane: grid loop 0xB4C2..0xB514 -> call water_slide, jmp over the rest;
     * the first instruction holds the grid address (relocated with the DLL) */
    {
        BYTE loop[7] = { 0xBB, 0, 0, 0, 0, 0x8B, 0xF3 }; /* mov ebx, grid / mov esi, ebx */
        BYTE call[7] = { 0xE8, 0, 0, 0, 0, 0xEB, 0x4C }; /* call water_slide / jmp 0xB515 */
        DWORD grid = base + 0x1ABEF74, rel = (DWORD)(ULONG_PTR)water_slide - (base + 0xB4C2 + 5);

        memcpy(loop + 1, &grid, 4);
        memcpy(call + 1, &rel, 4);
        g_water_y = (float *)(ULONG_PTR)grid;
        if (!patch(base + 0xB4C2, loop, call, 7, "renderer: water moves by the clock"))
            g_water_y = NULL;
    }

    if (g_widescreen) {
        /* 0.05f, then 1/15 of fy */
        if (memcmp((void *)(ULONG_PTR)(base + 0x200B8), "\xCD\xCC\x4C\x3D\x89\x88\x88\x3D", 8) == 0) {
            g_fx_factor = (float *)(ULONG_PTR)(base + 0x200B8);
            set_fx_factor(g_game_w, g_game_h);
            say("ok   %-40s %08X", "renderer: wide view (Hor+)", base + 0x200B8);
        } else {
            say("skip %-40s %08X: unexpected bytes, unknown game build?", "renderer: wide view (Hor+)", base + 0x200B8);
        }
    }
}

static HMODULE WINAPI load_library(LPCSTR name)
{
    HMODULE m = g_load_library(name);
    const char *file = name;
    const char *p;

    for (p = name; p && *p; p++)
        if (*p == '\\' || *p == '/')
            file = p + 1;
    if (m && file && _stricmp(file, "_d3d.dll") == 0)
        fix_renderer(m);
    return m;
}

/* Camera distance, wheel or numpad +/-, 10 % steps, 50-200 %: scales the int16 table {distance, height, pitch} x 20
 * at 0x4D9528, read every tick (FUN_004A0960, mode handlers). g_camera: the original */
static short g_camera[20][3];
static int g_zoom = 100, g_wheel;

static void zoom_camera(int step)
{
    short (*table)[3] = (short (*)[3])0x4D9528;
    int i, z = g_zoom + step;

    if (!g_camera[0][0] || z < 50 || z > 200)
        return;
    g_zoom = z;
    for (i = 0; i < 20; i++) {
        table[i][0] = (short)(g_camera[i][0] * z / 100); /* at most 15000 x 2 */
        table[i][1] = (short)(g_camera[i][1] * z / 100);
    }
    say("camera distance %d%%", z);
}

/* Name screen (menu+0x24 == 0): MainWndProc gives A-Z and Space to FUN_0042E440, which presses the cell of
 * the Latin letter in the grid (strchr). A key now means the character the layout types, if the grid has it. */
#define MENU (*(const int **)0x51AAD8)       /* the menu the keys go to */
#define NAME_GRID (*(const char **)0x51AAF4) /* the letter grid, as the name screen last drew it */
#define KEY_WANTED (*(DWORD *)0x51AC50)      /* Options, Controls waits for the key to bind */

static BOOL g_name_cell; /* name_cell() hooked */
static char g_cell;      /* text byte of the current key, 0: its Latin letter */

static char key_cell(UINT vk, LPARAM lp)
{
    /* U+0410..U+042F -> codes of the Russian translation (github.com/accidev/hogs-of-war-ru): Latin-looking
     * letters keep Latin codes, the other 22 from 0x6F. Text byte = code - 0x1F */
    static const BYTE kRussian[32] = { 'A',  0x6F, 'B',  0x70, 0x71, 'E',  0x73, 0x74, 0x75, 0x76, 'K',
                                       0x77, 'M',  'H',  'O',  0x78, 'P',  'C',  'T',  0x79, 0x7A, 'X',
                                       0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0x80, 0x81, 0x82, 0x83, 0x84 };
    static const BYTE no_shift[256] = { 0 }; /* the grid has capitals only */
    const int *menu = MENU;
    const char *grid = NAME_GRID;
    WCHAR c = 0;
    int code = 0;

    if (!g_name_cell || !FRONTEND || KEY_WANTED || !menu || menu[9] || !grid)
        return 0;
    /* flag 4: keep the layout's dead-key state (Windows 10 1607+) */
    if (ToUnicodeEx(vk, (UINT)(lp >> 16) & 0xFF, no_shift, &c, 1, 4, GetKeyboardLayout(0)) != 1)
        return 0;
    if (c >= 'a' && c <= 'z')
        code = c - 0x20;
    else if (c > ' ' && c < 'a')
        code = c;
    else if (strchr(grid, 0x6F - 0x1F)) /* a grid with the Russian letters */
        code = c >= 0x430 && c <= 0x44F ? kRussian[c - 0x430] : c == 0x451 ? 0x72 : 0;
    return code && strchr(grid, code - 0x1F) ? (char)(code - 0x1F) : 0;
}

/* Replaces strchr(grid, letter) in FUN_0042E440; a miss gives the cell after "End", which FUN_0042AF50
 * ignores (a negative cell crashed the menu behind, write at 0x420001) */
static const char *__cdecl name_cell(const char *grid, int letter)
{
    const char *at = strchr(grid, g_cell ? g_cell : letter);

    return at ? at : grid + strlen(grid) + 3;
}

static WNDPROC g_game_proc;

static LRESULT CALLBACK game_window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    LRESULT r;

    if (msg == WM_MOUSEWHEEL) {
        for (g_wheel += GET_WHEEL_DELTA_WPARAM(wp); g_wheel >= WHEEL_DELTA; g_wheel -= WHEEL_DELTA)
            zoom_camera(-10); /* wheel forward: closer */
        for (; g_wheel <= -WHEEL_DELTA; g_wheel += WHEEL_DELTA)
            zoom_camera(10);
    } else if (msg == WM_KEYDOWN) {
        if ((g_cell = key_cell((UINT)wp, lp)) != 0)
            wp = 'A'; /* any letter for MainWndProc; name_cell() takes g_cell */
        else if (wp == VK_ADD || wp == VK_SUBTRACT)
            zoom_camera(wp == VK_ADD ? -10 : 10);
    }
    r = CallWindowProcA(g_game_proc, hwnd, msg, wp, lp);
    g_cell = 0;
    return r;
}

/* FUN_0044D040(w, h) sized the window on every screen change; now placed once. [Display] Borderless=1: the
 * monitor, else the largest window in the work area, shaped like a wide battle or the first screen */
static BOOL g_borderless;

/* Pig name box: 182400 / depth pixels = mov eax, 0x2C880 at 0x459D6D in FUN_00459B20;
 * 182400 = 380 x 480 lines, so it becomes 380 x lines */
static DWORD *g_name_box;

static void set_name_box(int lines)
{
    DWORD prot;

    if (!g_name_box || lines <= 0)
        return;
    VirtualProtect(g_name_box, sizeof *g_name_box, PAGE_EXECUTE_READWRITE, &prot);
    *g_name_box = 380 * (DWORD)lines;
    VirtualProtect(g_name_box, sizeof *g_name_box, prot, &prot);
    FlushInstructionCache(GetCurrentProcess(), g_name_box, sizeof *g_name_box);
}

static void __stdcall size_game_window(int w, int h)
{
    HWND hwnd = GAME_WINDOW;
    MONITORINFO mi = { sizeof mi };
    RECT r = { 0, 0, 0, 0 };
    int fw, fh, cw, ch;

    g_game_w = w;
    g_game_h = h;
    set_fx_factor(w, h);
    set_name_box(h);
    if (!hwnd || g_game_proc)
        return;
    wide_battle(&w, &h);
    g_game_proc = (WNDPROC)SetWindowLongA(hwnd, GWL_WNDPROC, (LONG)game_window_proc);
    GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    if (g_borderless) {
        SetWindowLongA(hwnd, GWL_STYLE, (GetWindowLongA(hwnd, GWL_STYLE) & WS_VISIBLE) | WS_POPUP);
        SetWindowPos(hwnd, NULL, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        return;
    }
    AdjustWindowRectEx(&r, (DWORD)GetWindowLongA(hwnd, GWL_STYLE), FALSE, (DWORD)GetWindowLongA(hwnd, GWL_EXSTYLE));
    fw = r.right - r.left; /* frame and caption */
    fh = r.bottom - r.top;
    cw = mi.rcWork.right - mi.rcWork.left - fw;
    ch = mi.rcWork.bottom - mi.rcWork.top - fh;
    if (cw * h > ch * w)
        cw = ch * w / h;
    else
        ch = cw * h / w;
    SetWindowPos(hwnd, NULL, mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - cw - fw) / 2,
                 mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - ch - fh) / 2, cw + fw, ch + fh,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

/* Loading and "press a key" screens (FUN_0045C460, FUN_0045CBB0, FUN_0045D2E0) stretch 640x480 art over the
 * battle resolution; keep it 4:3. Replaces the Blt wrapper FUN_0044D5F0(dst, src, dst rect, src rect, flags, fx) */
static HRESULT __stdcall blit_4x3(void *dst, void *src, RECT *d, RECT *s, DWORD flags, void *fx)
{
    HRESULT(__stdcall *blit)(void *, void *, RECT *, RECT *, DWORD, void *) = (void *)0x44D5F0;
    RECT r = *d; /* copy: the game reuses the rect */
    int w = g_game_w, h = g_game_h, picture = h * 4 / 3, side = (w - picture) / 2;

    if (w * 3 > h * 4) {
        if (r.left == 0 && r.right == w) { /* the picture: nothing else paints the sides */
            DWORD black[25] = { sizeof black }; /* a DDBLTFX, dwFillColor = 0 */
            RECT bar = { 0, 0, side, h };

            blit(dst, NULL, &bar, NULL, 0x01000400, black); /* DDBLT_WAIT | DDBLT_COLORFILL */
            SetRect(&bar, side + picture, 0, w, h);
            blit(dst, NULL, &bar, NULL, 0x01000400, black);
        }
        r.left = side + r.left * picture / w;
        r.right = side + r.right * picture / w;
    }
    return blit(dst, src, &r, s, flags, fx);
}

/* "LauncherBox" dialog (FUN_00481230): WM_INITDIALOG (FUN_004815D0) selects the choice from launch.bin;
 * Start ends it with 1, then the game writes launch.bin */
static char g_launch_bin[MAX_PATH];
static DLGPROC g_launcher_proc;
static BOOL g_skip_launcher, g_first_start;

static INT_PTR CALLBACK launcher_start(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    INT_PTR r = g_launcher_proc(dlg, msg, wp, lp);

    if (msg == WM_INITDIALOG) {
        LRESULT mode = SendDlgItemMessageA(dlg, 0x3EA, CB_GETITEMDATA, 0, 0);
        int *cfg = (int *)GAME_CONFIG;

        /* no launch.bin: first mode (the monitor's). Item data = width << 16 | height;
         * cfg+0x4F0 / +0x4F4 = width, height handed over */
        if (g_first_start && mode != CB_ERR && cfg) {
            SendDlgItemMessageA(dlg, 0x3EA, CB_SETCURSEL, 0, 0);
            cfg[0x4F0 / 4] = HIWORD(mode);
            cfg[0x4F4 / 4] = LOWORD(mode);
        }
        if (g_skip_launcher && !(GetAsyncKeyState(VK_SHIFT) & 0x8000))
            EndDialog(dlg, 1);
    }
    return r;
}

static INT_PTR WINAPI launcher_box(HINSTANCE inst, LPCSTR name, HWND parent, DLGPROC proc, LPARAM lp)
{
    if (!IS_INTRESOURCE(name) && strcmp(name, "LauncherBox") == 0) {
        g_first_start = GetFileAttributesA(g_launch_bin) == INVALID_FILE_ATTRIBUTES;
        g_launcher_proc = proc;
        proc = launcher_start;
    }
    return DialogBoxParamA(inst, name, parent, proc, lp);
}

/* FUN_00481D00 reads folders and language from HKLM\...\Uninstall\Hogs Of War (written by Steam's setup)
 * and exits if a value is missing. Install folder = the exe's (working directory, 0x47F317) */
static BYTE __cdecl install_info(void)
{
    BYTE(__cdecl *reg_string)(const char *, BYTE *) = (void *)0x47F6C0;
    BYTE(__cdecl *reg_dword)(const char *, BYTE *) = (void *)0x47F740;
    void(__cdecl *load_launcher_text)(void) = (void *)0x481D80; /* in the language just read */
    BYTE *cfg = GAME_CONFIG;
    char *folder = (char *)cfg + 0x10C, *slash;

    if (!reg_string("InstallSource", cfg + 0xC))
        strcpy((char *)cfg + 0xC, ".\\");
    if (!reg_string("LanguageLetter", cfg + 0x20C))
        strcpy((char *)cfg + 0x20C, "E");
    if (!reg_dword("Footprint", cfg + 0x30C))
        *(DWORD *)(cfg + 0x30C) = 1;
    if (!GetModuleFileNameA(NULL, folder, 256))
        folder[0] = '\0';
    slash = strrchr(folder, '\\');
    if (slash)
        slash[1] = '\0';
    load_launcher_text();
    return 1;
}

/* [Startup] SkipIntro=1: the start-up chain FUN_0047DE90 stops when byte [0x520848] is set (Esc, in the
 * window procedure); set it at the first video */
static int __cdecl skip_intro(const char *video)
{
    (void)video;
    *(BYTE *)0x520848 = 1;
    return 1;
}

/* EnumWindows callback 0x44CE80 disables foreign top-level windows, re-enabled on a clean exit only */
static BOOL CALLBACK keep_foreign_windows(HWND hwnd, LPARAM arg)
{
    (void)hwnd;
    (void)arg;
    return TRUE;
}

/* Music volume: mixerSetControlDetails on the CD line (one channel, one DWORD, 0..0xFFDC), read at start-up
 * (FUN_004398E0). Windows 10/11 has no CD line, the calls hit the system slider: use ogg-winmm's auxSetVolume */
static MMRESULT WINAPI cd_volume_set(HMIXEROBJ mixer, LPMIXERCONTROLDETAILS d, DWORD flags)
{
    DWORD v = *(DWORD *)d->paDetails;

    (void)mixer;
    (void)flags;
    return auxSetVolume(0, v | v << 16);
}

static MMRESULT WINAPI cd_volume_get(HMIXEROBJ mixer, LPMIXERCONTROLDETAILS d, DWORD flags)
{
    DWORD v;

    (void)mixer;
    (void)flags;
    if (auxGetVolume(0, &v) != MMSYSERR_NOERROR)
        v = 0xFFFF;
    *(DWORD *)d->paDetails = LOWORD(v);
    return MMSYSERR_NOERROR;
}

/* [Cheats] PromotionPoints=1: F11 adds 10. [0x51C560] = record of the team being edited (0x2A8 bytes, as in
 * savearmyN); +0x50 int16 promotion points, checked and spent at 0x42BDB6, 0x42BDD5 */
static DWORD WINAPI promotion_points_key(LPVOID unused)
{
    BOOL was = FALSE;

    (void)unused;
    for (;;) {
        BOOL down = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
        BYTE *team = *(BYTE **)0x51C560;
        DWORD pid = 0;

        GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        if (down && !was && team && pid == GetCurrentProcessId()) {
            short *pp = (short *)(team + 0x50);
            short old = *pp;

            *pp = old < 989 ? old + 10 : 999;
            say("cheat: promotion points %d -> %d", old, *pp);
        }
        was = down;
        Sleep(30);
    }
}

/* Game message loops (menus 0x427380, battle 0x47DE90, 0x480780) poll PeekMessageA without waiting: wait
 * here when minimized. The gamepad is read here, in the game's thread */
static BOOL(WINAPI *g_peek_message)(LPMSG, HWND, UINT, UINT, UINT);

static BOOL WINAPI peek_message(LPMSG msg, HWND hwnd, UINT first, UINT last, UINT remove)
{
    HWND game = GAME_WINDOW;

    if (game && IsIconic(game))
        MsgWaitForMultipleObjectsEx(0, NULL, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    gamepad_poll();
    return g_peek_message(msg, hwnd, first, last, remove);
}

static void apply_fixes(int vsync, int widescreen)
{
    /* call [SystemParametersInfoA] -> add esp,16 (4 stdcall arguments) */
    static const BYTE spi_call[] = { 0xFF, 0x15, 0xE8, 0xF5, 0x54, 0x00 };
    static const BYTE spi_skip[] = { 0x83, 0xC4, 0x10, 0x90, 0x90, 0x90 };
    int resizable = GetPrivateProfileIntA("Display", "Resizable", 1, g_ini);
    int deadzone = GetPrivateProfileIntA("Gamepad", "Deadzone", 15, g_ini);
    BYTE deadzone_mov[8] = { 0xC7, 0x44, 0x24, 0x10 }; /* mov dword [esp+0x10], deadzone */
    DWORD deadzone_value = (DWORD)(deadzone < 0 ? 0 : deadzone > 100 ? 100 : deadzone) * 100;

    hook(0x44CE80, 0xE9, "\x8B\x44\x24\x08\x33", keep_foreign_windows, "leave foreign windows enabled");
    hook(0x481D00, 0xE9, "\xA1\x68\x06\x52\x00", install_info, "start without the registry key");

    /* SPI_SCREENSAVERRUNNING: Win9x trick to block Alt+Tab and Ctrl+Alt+Del; set at window creation,
     * cleared at WM_DESTROY */
    patch(0x44CFC1, spi_call, spi_skip, 6, "no SPI_SCREENSAVERRUNNING on start");
    patch(0x47EA97, spi_call, spi_skip, 6, "no SPI_SCREENSAVERRUNNING on exit");

    /* Init 0x47F0B0 `mov [edx+0x314],ebx` sets cfg+0x314 ("fullscreen") = 1 in a zeroed block; no store = windowed
     * (DDSCL_NORMAL, mode index -1). No exclusive fullscreen in the renderer: [Display] Borderless */
    patch(0x47F174, "\x89\x9A\x14\x03\x00\x00", "\x90\x90\x90\x90\x90\x90", 6, "windowed mode");

    /* style WS_CAPTION|WS_SYSMENU -> WS_OVERLAPPEDWINDOW; WM_SIZE already re-reads the client rect */
    if (resizable)
        patch(0x44CF58, "\x68\x00\x00\xC8\x00", "\x68\x00\x00\xCF\x00", 5, "resizable window");

    g_borderless = GetPrivateProfileIntA("Display", "Borderless", 0, g_ini);
    if (hook(0x44D040, 0xE9, "\xA1\x60\x08\x52\x00", size_game_window, "big game window, keeps its size")) {
        if (memcmp((void *)0x459D6D, "\xB8\x80\xC8\x02\x00", 5) == 0) {
            g_name_box = (DWORD *)0x459D6E;
            say("ok   %-40s %08X", "names above the pigs at any resolution", 0x459D6D);
        } else {
            say("skip %-40s %08X: unexpected bytes, unknown game build?", "names above the pigs at any resolution", 0x459D6D);
        }
    }

    g_widescreen = widescreen;
    if (g_widescreen) {
        hook(0x45C6B7, 0xE8, "\xE8\x34\x0F\xFF\xFF", blit_4x3, "wide battle: loading picture 4:3");
        hook(0x45C6DE, 0xE8, "\xE8\x0D\x0F\xFF\xFF", blit_4x3, "wide battle: loading bar 4:3");
        hook(0x45CE24, 0xE8, "\xE8\xC7\x07\xFF\xFF", blit_4x3, "wide battle: loading picture 4:3");
        hook(0x45CE56, 0xE8, "\xE8\x95\x07\xFF\xFF", blit_4x3, "wide battle: loading bar 4:3");
        hook(0x45D488, 0xE8, "\xE8\x63\x01\xFF\xFF", blit_4x3, "wide battle: briefing picture 4:3");
    }
    /* the launcher (FUN_004815D0) lists modes up to 1024x768 only */
    patch(0x481726, "\x3D\x00\x04\x00\x00", "\x3D\x00\x10\x00\x00", 5, "launcher: modes up to 4096 wide");
    patch(0x481740, "\x81\xF9\x00\x03\x00\x00", "\x81\xF9\x00\x06\x00\x00", 6, "launcher: modes up to 1536 high");

    /* g_camera enables the zoom: fill it only if table entries 0 and 1 are the known ones */
    if (memcmp((void *)0x4D9528, "\x00\x0C\x00\x03\x00\x00\x00\x0C\x00\x04\x00\x00", 12) == 0) {
        memcpy(g_camera, (void *)0x4D9528, sizeof g_camera);
        say("ok   %-40s %08X", "camera zoom: mouse wheel, numpad +/-", 0x4D9528);
    } else {
        say("skip %-40s %08X: unexpected bytes, unknown game build?", "camera zoom: mouse wheel, numpad +/-", 0x4D9528);
    }

    /* else Windows stretches the window at 125 % / 150 % scaling */
    SetProcessDPIAware();

    g_skip_launcher = GetPrivateProfileIntA("Startup", "SkipLauncher", 1, g_ini);
    hook_import(0x54F55C, "user32.dll", "DialogBoxParamA", launcher_box, NULL,
                g_skip_launcher ? "start without the launcher (Shift: show)" : "launcher: mode for the monitor at first");
    if (GetPrivateProfileIntA("Startup", "SkipIntro", 1, g_ini))
        hook(0x47DF2B, 0xE8, "\xE8\xE0\x0E\xFC\xFF", skip_intro, "no intro videos");

    /* joystick setup (callback 0x44C700): DIPROP_DEADZONE 5000 of 10000; [Gamepad] Deadzone is in % */
    memcpy(deadzone_mov + 4, &deadzone_value, 4);
    patch(0x44C7FC, "\xC7\x44\x24\x10\x88\x13\x00\x00", deadzone_mov, 8, "gamepad: stick dead zone");

    hook_import(0x54F434, "kernel32.dll", "LoadLibraryA", load_library, &g_load_library,
                "fix the renderer when it is loaded");

    hook_import(0x54F60C, "winmm.dll", "mixerSetControlDetails", cd_volume_set, NULL,
                "music volume: set on the CD player");
    hook_import(0x54F608, "winmm.dll", "mixerGetControlDetailsA", cd_volume_get, NULL,
                "music volume: read from the CD player");

    /* MainWndProc cleared the active flag (config+0x310) on WM_SIZE(SIZE_MINIMIZED) for good; now every
     * WM_SIZE sets it: mov eax,[cfg] / cmp edi,1 / setne byte [eax+0x310] / nop / nop */
    patch(0x47EA43, "\x83\xFF\x01\x75\x0C\xA1\x68\x06\x52\x00\xC6\x80\x10\x03\x00\x00\x00",
          "\xA1\x68\x06\x52\x00\x83\xFF\x01\x0F\x95\x80\x10\x03\x00\x00\x90\x90", 17, "menus go on after a minimize");
    hook_import(0x54F560, "user32.dll", "PeekMessageA", peek_message, &g_peek_message,
                "idle while minimized, read the gamepad");

    g_name_cell = hook(0x42E4A3, 0xE8, "\xE8\xE8\x01\x08\x00", name_cell, "name typing: no crash, keyboard layout");

    /* FUN_00461A80 finds a path only outside the walk grid (x 0xC00..0x7000, y 0xE00..0x7000); its only
     * caller 0x44A170 clamps inside. Swap the 4 jumps of the range check */
    if (GetPrivateProfileIntA("Battle", "ComputerWalks", 1, g_ini))
        patch(0x461AEA,
              "\x7E\x26\x3D\x00\x70\x00\x00\x7D\x1F\x8B\xC2\x25\x00\xFE\xFF\xFF\x3D\x00\x0E\x00\x00\x7E\x11\x3D\x00\x70\x00\x00\x7D\x0A",
              "\x7E\x1C\x3D\x00\x70\x00\x00\x7D\x15\x8B\xC2\x25\x00\xFE\xFF\xFF\x3D\x00\x0E\x00\x00\x7E\x07\x3D\x00\x70\x00\x00\x7C\x0A",
              30, "computer pigs find their way");

    frames_init(vsync);
    gamepad_init();
    net_init();

    if (GetPrivateProfileIntA("Cheats", "PromotionPoints", 0, g_ini)) {
        if (memcmp((void *)0x42BDB6, "\x8B\x15\x60\xC5\x51\x00", 6) || memcmp((void *)0x42BDD5, "\x66\x8B\x42\x50", 4))
            say("skip %-40s %08X: unexpected bytes, unknown game build?", "cheat: F11 adds promotion points", 0x42BDB6);
        else if (CreateThread(NULL, 0, promotion_points_key, NULL, 0, NULL))
            say("ok   %-40s %08X", "cheat: F11 adds promotion points", 0x51C560);
    }
}

/* LaserLock's old entry point, still imported by the exe; all 325 call sites were rewritten */
int CallDLL(void)
{
    say("CallDLL called - a LaserLock call site was missed");
    return 0;
}

/* Fixed addresses of warhogs_.exe v1.2. Only the patcher makes an exe import CallDLL from ddraw.dll
 * (a renamed copy too); anything else loading this DLL gets the renderer only */
static BOOL patched_game(void)
{
    const BYTE *exe = (const BYTE *)GetModuleHandleA(NULL);
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(exe + ((const IMAGE_DOS_HEADER *)exe)->e_lfanew);
    const IMAGE_DATA_DIRECTORY *imports = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    const IMAGE_IMPORT_DESCRIPTOR *d;
    const FARPROC *slot;

    if (nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT || !imports->VirtualAddress)
        return FALSE;
    /* the loader has filled the exe's import slots before any DllMain runs */
    for (d = (const IMAGE_IMPORT_DESCRIPTOR *)(exe + imports->VirtualAddress); d->FirstThunk; d++)
        for (slot = (const FARPROC *)(exe + d->FirstThunk); *slot; slot++)
            if (*slot == (FARPROC)CallDLL)
                return TRUE;
    return FALSE;
}

/* DllMain passes [Render] VSync and [Display] Widescreen as the renderer read them */
void hogs_init(int vsync, int widescreen)
{
    char dir[MAX_PATH], log[MAX_PATH];
    char *slash;

    if (!patched_game())
        return; /* before hogs.log is opened: the log of the last game run stays */
    GetModuleFileNameA(NULL, dir, MAX_PATH);
    slash = strrchr(dir, '\\');
    if (slash)
        slash[1] = '\0';
    snprintf(g_ini, sizeof g_ini, "%shogs.ini", dir);
    snprintf(log, sizeof log, "%shogs.log", dir);
    snprintf(g_launch_bin, sizeof g_launch_bin, "%slaunch.bin", dir);
    g_log = fopen(log, "w"); /* stays open; say() flushes */
    say("ddraw.dll %s %s", __DATE__, __TIME__);
    apply_fixes(vsync, widescreen);
}
