/* Runtime fixes for Hogs of War (2000), warhogs_.exe v1.2, built into ddraw.dll with the renderer.
 *
 * The patched warhogs_.exe imports CallDLL from ddraw.dll in the slot of LaserLock's wh32lib.dll,
 * and imports DirectDraw from it too, so Windows maps it and runs DllMain (src/ddraw/main.cpp)
 * before the game's entry point; DllMain calls hogs_init(). The unpatched exe and any other program
 * started from the game folder can load the same ddraw.dll, so the fixes go in only for an exe
 * that imports CallDLL from it.
 * Every fix checks the original bytes first and leaves a build it does not know alone.
 * Settings: hogs.ini next to the exe. Log: hogs.log next to the exe.
 */
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

/* Overwrite len bytes at va, but only if they still hold `old`. */
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

/* Write a jmp (op 0xE9) or call (0xE8) to fn over the 5 bytes at va. A jmp over a function's
 * first bytes routes every call of it to fn; fn must have the same calling convention and
 * arguments as the code it replaces. */
BOOL hook(DWORD va, BYTE op, const void *old5, const void *fn, const char *what)
{
    BYTE jmp[5] = { op };
    DWORD rel = (DWORD)(ULONG_PTR)fn - (va + 5);

    memcpy(jmp + 1, &rel, 4);
    return patch(va, old5, jmp, 5, what);
}

/* Point an import slot at repl, after checking it holds dll!fn; *orig (if given) gets the real one. */
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

/* The renderer, Data\_d3d.dll, is loaded with LoadLibraryA while the game starts up
 * (0x4ADB03), so its fixes are applied right after that call returns. */
static HMODULE(WINAPI *g_load_library)(LPCSTR);

/* [Display] Widescreen=1: a battle in a resolution wider than 4:3 shows more to the sides (Hor+)
 * instead of a stretched picture. The battle resolution is the launcher's choice: FUN_00481230
 * copies it to cfg+0x44C / +0x450 right after the dialog, before the renderer is started. */
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

/* The renderer loads its 10 terrain visibility masks (language\tims\nview00N.bmp, 33x33,
 * black = draw that block of 4x4 tiles) with LoadImageA as device-dependent bitmaps, copies
 * 2 KB of each with GetBitmapBits and reads one 16-bit entry per pixel. That only holds on a
 * 16-bit desktop: on a 32-bit one every pixel filled two entries, the masks came out skewed
 * and the ground near the camera vanished in stripes. Copy the bits as a 16-bit bitmap holds them. */
static LONG WINAPI bitmap_bits_16bpp(HBITMAP bm, LONG size, LPVOID out)
{
    BITMAPINFO bi = { { sizeof bi.bmiHeader } };
    BITMAP b;
    BYTE *dib;
    HDC dc;
    LONG row, stride, y, n = 0;
    int w, h;

    /* The masks are wedges cut for the 4:3 view angle: a wider view shows ground they leave out.
     * They only save work, so a wide battle gets every block. The map is 16x16 blocks, a 4:3
     * view from a map corner draws all of them too, and the renderer still skips each tile
     * behind the camera (FUN_10003380: depth of all 4 corners <= -15). */
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
    row = b.bmWidth * 2;     /* GetBitmapBits rows are WORD aligned: never padded at 16 bpp */
    stride = (row + 3) & ~3; /* GetDIBits rows are DWORD aligned */
    bi.bmiHeader.biWidth = b.bmWidth;
    bi.bmiHeader.biHeight = -b.bmHeight; /* top-down, like a device-dependent bitmap */
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

/* The game resolution of the current screen (menus 640x480, battles the launcher's choice),
 * set by size_game_window() on every screen change. */
static int g_game_w, g_game_h;

/* _d3d.dll sizes its back buffer from GetClientRect (FUN_10005820) and blits each frame to the
 * client rect (CopyToScreen; hogsdraw ignores that rect and scales the frame into the window
 * itself). Report the game resolution for the game window, so the window can have any size. */
static BOOL WINAPI game_client_rect(HWND hwnd, LPRECT r)
{
    if (!g_game_w || hwnd != GAME_WINDOW)
        return GetClientRect(hwnd, r);
    return SetRect(r, 0, 0, g_game_w, g_game_h);
}

/* Hor+. The renderer has two focal lengths: models use height x zoom / 15 for both axes, but
 * the ground, the water and everything else placed in the world (FUN_10013830) take
 * fx = width x zoom x 0.05 and fy = height x zoom / 15, which are equal only at 4:3. The 0.05
 * is one float, read by the four places that compute fx (SetDisplayWindow, FUN_10005820,
 * FUN_100075D0, afSetDDGlobals) and by nothing else. It becomes height / (15 x width) of the
 * current screen: fx = fy at any shape, and still exactly 0.05 at 4:3. */
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

    /* every RVA below must lie inside the image before patch() compares its bytes */
    if (nt->OptionalHeader.SizeOfImage != 0x1EE9000) {
        say("skip renderer fixes: unknown _d3d.dll (image size %08X)", nt->OptionalHeader.SizeOfImage);
        return;
    }

    /* CopyToScreen, windowed path: the client origin comes from
     * AdjustWindowRectEx(&rc, style, bMenu=TRUE, exstyle), but the game window has no menu,
     * so every frame was blitted SM_CYMENU (~20 px) too low. push 1 -> push 0 (bMenu=FALSE). */
    patch(base + 0x321B, "\x50\x6A\x01\x8D\x4C\x24\x24", "\x50\x6A\x00\x8D\x4C\x24\x24", 7,
          "renderer: blit at the real client origin");

    /* GetBitmapBits has one caller, the nview mask loader FUN_10014120 */
    hook_import(base + 0x1ED719C, "gdi32.dll", "GetBitmapBits", bitmap_bits_16bpp, NULL,
                "renderer: 16-bit terrain masks");

    /* Objects (trees, buildings, the sky dome) are transformed by FUN_10011810, which clamps
     * view z to at least 1.527e-5. A vertex behind the camera then projects to a huge screen
     * position, and its triangle is stretched across the screen. With the occluder fade, a tree
     * next to the camera became flickering translucent bands. The triangle emitters already skip
     * any triangle with a vertex at z < 0 (that is how hog models are drawn), so store -1.0
     * instead of the clamp value: triangles that reach behind the camera are dropped. The
     * visibility test, the other caller, needs z > 15 either way. */
    patch(base + 0x118D8, "\xC7\x02\x13\x18\x80\x37", "\xC7\x02\x00\x00\x80\xBF", 6,
          "renderer: drop objects behind the camera");

    hook_import(base + 0x1ED7298, "user32.dll", "GetClientRect", game_client_rect, NULL,
                "renderer: game resolution as client size");

    /* The water plane, FUN_1000B440: the loop that moves its grid one step per frame
     * (0xB4C2..0xB514) becomes a call of water_slide and a jump over the rest of it. The first
     * instruction holds the address of the grid, moved with the DLL. */
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
        /* 0.05f, and behind it the 1/15 of fy */
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

/* Camera distance: mouse wheel or numpad +/-, 10 % a step, 50-200 %. Every camera mode takes
 * its distance and height from the int16 table {distance, height, pitch} x 20 at 0x4D9528,
 * read every tick (FUN_004A0960 and the mode handlers), so scaling the table moves the camera
 * smoothly and keeps its angle. g_camera is the original table, zero when it was not found. */
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

/* Typing a name (team, pig). While the name screen is the current menu (menu+0x24 is 0),
 * MainWndProc gives the keys A-Z and Space to FUN_0042E440, which finds the key's Latin letter
 * in the letter grid on screen with strchr and presses that cell. Two changes:
 * - It took the strchr result for a cell without a check. A grid without the letter (the one
 *   of a translation, with another alphabet) gave a negative cell: the name screen closed as
 *   if "End" was pressed, the cell number of "End" went into the menu behind it, and that menu
 *   indexed a table with it and crashed (write at 0x420001). A key without a cell now presses
 *   the cell after "End", for which FUN_0042AF50 does nothing.
 * - A key means the character it types in the keyboard layout, if the grid has it: Russian
 *   letters with the Russian translation, digits and the grid's signs in any game. If not, it
 *   is the Latin letter of the key, as before.
 * A text byte is the ASCII code - 0x1F. The Russian translation (github.com/accidev/hogs-of-war-ru)
 * keeps the letters that look Latin on the Latin codes and has the other 22 from 0x6F on. */
#define MENU (*(const int **)0x51AAD8)       /* the menu the keys go to */
#define NAME_GRID (*(const char **)0x51AAF4) /* the letter grid, as the name screen last drew it */
#define KEY_WANTED (*(DWORD *)0x51AC50)      /* Options, Controls waits for the key to bind */

static BOOL g_name_cell; /* name_cell() is in the game */
static char g_cell;      /* text byte of the key MainWndProc has now, 0: the Latin letter of the key */

static char key_cell(UINT vk, LPARAM lp)
{
    /* U+0410..U+042F as ASCII codes of the translation */
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
    /* flag 4: leave the dead-key state of the layout alone (Windows 10 1607 and later) */
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

/* In place of FUN_0042E440's strchr(grid, letter of the key). */
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
            wp = 'A'; /* a key MainWndProc passes to the name screen; name_cell() takes g_cell */
        else if (wp == VK_ADD || wp == VK_SUBTRACT)
            zoom_camera(wp == VK_ADD ? -10 : 10);
    }
    r = CallWindowProcA(g_game_proc, hwnd, msg, wp, lp);
    g_cell = 0;
    return r;
}

/* Windowed mode: FUN_0044D040(w, h) sized the game window to a w x h client area on every
 * screen change (menus 640x480, battles the launcher's resolution), a small window on today's
 * monitors. Now the window is placed once, since the renderer copes with any size (see
 * game_client_rect). [Display] Borderless=1 covers the monitor, otherwise it is the largest
 * window that fits the work area, in the shape of a wide battle or else of the first screen:
 * the menus are always 4:3 and get bars at the sides of a wide window. After that it keeps the
 * size and state the player gives it. */
static BOOL g_borderless;

/* The name over a pig stands on a box of 182400 / depth pixels around it (FUN_00459B20,
 * mov eax, 0x2C880 at 0x459D6D): the size of a pig at 480 lines. With more lines the pig is
 * larger, the box was not, and the name sat on the pig's head. 182400 is 380 x 480, so the
 * number becomes 380 x the lines of the current screen. */
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

/* The loading screen and the "press a key" screen before a battle (FUN_0045C460, FUN_0045CBB0,
 * FUN_0045D2E0) are already in the battle resolution: they stretch a 640x480 picture and its
 * progress bar over all of it, x by width / 640. In a wide battle both stay 4:3 in the middle,
 * with black at the sides. This takes the place of the game's Blt wrapper FUN_0044D5F0(dst,
 * src, dst rect, src rect, flags, fx) in those five calls. */
static HRESULT __stdcall blit_4x3(void *dst, void *src, RECT *d, RECT *s, DWORD flags, void *fx)
{
    HRESULT(__stdcall *blit)(void *, void *, RECT *, RECT *, DWORD, void *) = (void *)0x44D5F0;
    RECT r = *d; /* a copy: the game passes the same rectangle again for the next frame */
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

/* The game asks for adapter, resolution and detail level in the "LauncherBox" dialog on every
 * start (FUN_00481230). Its WM_INITDIALOG (FUN_004815D0) selects the choice saved in launch.bin
 * and stores it in the game config; Start (IDOK) only ends the dialog with 1, and the game then
 * writes launch.bin.
 * - First start, no launch.bin yet: the dialog selects 640x480. Select the first mode of its
 *   list instead, where ddraw.dll puts the one that suits the monitor. cfg+0x4F0 / +0x4F4 are
 *   the width and height the dialog hands over, each item's data is width << 16 | height.
 * - [Startup] SkipLauncher=1: the dialog ends right after WM_INITDIALOG, before it is shown.
 *   Shift held while the game starts shows it as before. */
static char g_launch_bin[MAX_PATH];
static DLGPROC g_launcher_proc;
static BOOL g_skip_launcher, g_first_start;

static INT_PTR CALLBACK launcher_start(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    INT_PTR r = g_launcher_proc(dlg, msg, wp, lp);

    if (msg == WM_INITDIALOG) {
        LRESULT mode = SendDlgItemMessageA(dlg, 0x3EA, CB_GETITEMDATA, 0, 0);
        int *cfg = (int *)GAME_CONFIG;

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

/* The game reads its install folder, source folder and language from the registry
 * (HKLM\...\Uninstall\Hogs Of War, FUN_00481D00) and exits with an error box when a value is
 * missing. Steam writes that key in its first-time setup, so a game folder copied from another
 * PC never started. The install folder is where the exe is; the game makes it the working
 * directory (0x47F317). For the rest the registry still wins, and what it lacks gets the values
 * of an English install. */
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

/* [Startup] SkipIntro=1: the start-up chain (FUN_0047DE90) plays infologo.bik, sheff.bik, a 3 s
 * title screen, fmv_01.bik and fmv_02.bik, and stops as soon as byte [0x520848] is set, which
 * Esc does in the window procedure. The first video returns as if Esc had been pressed. */
static int __cdecl skip_intro(const char *video)
{
    (void)video;
    *(BYTE *)0x520848 = 1;
    return 1;
}

/* The game disables every top-level window in the system while it runs: the EnumWindows
 * callback at 0x44CE80 calls EnableWindow on each foreign window, and the windows are
 * re-enabled only on a clean exit. A crash or Alt+Tab left the whole desktop dead. */
static BOOL CALLBACK keep_foreign_windows(HWND hwnd, LPARAM arg)
{
    (void)hwnd;
    (void)arg;
    return TRUE;
}

/* Music volume. The game sets the CD-audio volume with mixerSetControlDetails on the mixer's
 * CD line, and at start-up takes its music volume from that control (FUN_004398E0). Windows
 * 10/11 has no CD line: the calls reach the game's own slider in the Windows volume mixer, so
 * every music fade faded all sound, and a game closed during a fade-out started the next time
 * with all sound at 0. ogg-winmm plays the CD tracks and has its own CD volume (auxSetVolume),
 * so the value goes there. The game's control details are one channel, one DWORD, 0..0xFFDC. */
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

/* [Cheats] PromotionPoints=1: F11 gives the team on screen 10 more promotion points. [0x51C560]
 * is the record of the team being edited (0x2A8 bytes, as saved in savearmyN); its promotion
 * points are the int16 at +0x50 that the promote code checks and spends (0x42BDB6, 0x42BDD5). */
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

/* The game's message loops (menus 0x427380, battle 0x47DE90, 0x480780) poll with PeekMessageA
 * and never wait. A minimized window draws nothing, so nothing slowed the menu loop down and it
 * kept one core busy: wait for the next message there. The gamepad is read from here too, in
 * the game's thread. */
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
    /* call [SystemParametersInfoA] -> add esp,16 (the stdcall's 4 arguments) */
    static const BYTE spi_call[] = { 0xFF, 0x15, 0xE8, 0xF5, 0x54, 0x00 };
    static const BYTE spi_skip[] = { 0x83, 0xC4, 0x10, 0x90, 0x90, 0x90 };
    int resizable = GetPrivateProfileIntA("Display", "Resizable", 1, g_ini);
    int deadzone = GetPrivateProfileIntA("Gamepad", "Deadzone", 15, g_ini);
    BYTE deadzone_mov[8] = { 0xC7, 0x44, 0x24, 0x10 }; /* mov dword [esp+0x10], deadzone */
    DWORD deadzone_value = (DWORD)(deadzone < 0 ? 0 : deadzone > 100 ? 100 : deadzone) * 100;

    hook(0x44CE80, 0xE9, "\x8B\x44\x24\x08\x33", keep_foreign_windows, "leave foreign windows enabled");
    hook(0x481D00, 0xE9, "\xA1\x68\x06\x52\x00", install_info, "start without the registry key");

    /* SystemParametersInfoA(SPI_SCREENSAVERRUNNING) is a Win9x trick to block Alt+Tab and
     * Ctrl+Alt+Del; the game sets it on window creation and clears it on WM_DESTROY. */
    patch(0x44CFC1, spi_call, spi_skip, 6, "no SPI_SCREENSAVERRUNNING on start");
    patch(0x47EA97, spi_call, spi_skip, 6, "no SPI_SCREENSAVERRUNNING on exit");

    /* Init (0x47F0B0) hardcodes cfg+0x314 ("fullscreen") = 1 with `mov [edx+0x314],ebx`.
     * The config block was zeroed just before, so dropping the store selects the developers'
     * windowed mode: DDSCL_NORMAL, mode index -1, a framed window. There is no setting for the
     * other mode: the renderer has no exclusive fullscreen, and the game window stayed hidden
     * in it. Full screen is [Display] Borderless. The launcher forces fullscreen for a
     * non-primary adapter only, and ddraw.dll lists one adapter. */
    patch(0x47F174, "\x89\x9A\x14\x03\x00\x00", "\x90\x90\x90\x90\x90\x90", 6, "windowed mode");

    /* Main window style WS_CAPTION|WS_SYSMENU -> WS_OVERLAPPEDWINDOW: resizable, with
     * minimize/maximize. WM_SIZE already re-reads the client rect the frame is blitted to. */
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
    /* The launcher lists modes up to 1024x768 only (FUN_004815D0); a window takes any size. */
    patch(0x481726, "\x3D\x00\x04\x00\x00", "\x3D\x00\x10\x00\x00", 5, "launcher: modes up to 4096 wide");
    patch(0x481740, "\x81\xF9\x00\x03\x00\x00", "\x81\xF9\x00\x06\x00\x00", 6, "launcher: modes up to 1536 high");

    /* g_camera switches the zoom on: fill it only if table entries 0 and 1 are the known ones */
    if (memcmp((void *)0x4D9528, "\x00\x0C\x00\x03\x00\x00\x00\x0C\x00\x04\x00\x00", 12) == 0) {
        memcpy(g_camera, (void *)0x4D9528, sizeof g_camera);
        say("ok   %-40s %08X", "camera zoom: mouse wheel, numpad +/-", 0x4D9528);
    } else {
        say("skip %-40s %08X: unexpected bytes, unknown game build?", "camera zoom: mouse wheel, numpad +/-", 0x4D9528);
    }

    /* without this, Windows stretches the window at 125 % or 150 % display scaling: blurry */
    SetProcessDPIAware();

    g_skip_launcher = GetPrivateProfileIntA("Startup", "SkipLauncher", 1, g_ini);
    hook_import(0x54F55C, "user32.dll", "DialogBoxParamA", launcher_box, NULL,
                g_skip_launcher ? "start without the launcher (Shift: show)" : "launcher: mode for the monitor at first");
    if (GetPrivateProfileIntA("Startup", "SkipIntro", 1, g_ini))
        hook(0x47DF2B, 0xE8, "\xE8\xE0\x0E\xFC\xFF", skip_intro, "no intro videos");

    /* Gamepad sticks: the joystick setup (callback 0x44C700) sets DIPROP_DEADZONE to 5000 of
     * 10000, so the first half of the stick travel did nothing. [Gamepad] Deadzone is in %. */
    memcpy(deadzone_mov + 4, &deadzone_value, 4);
    patch(0x44C7FC, "\xC7\x44\x24\x10\x88\x13\x00\x00", deadzone_mov, 8, "gamepad: stick dead zone");

    hook_import(0x54F434, "kernel32.dll", "LoadLibraryA", load_library, &g_load_library,
                "fix the renderer when it is loaded");

    hook_import(0x54F60C, "winmm.dll", "mixerSetControlDetails", cd_volume_set, NULL,
                "music volume: set on the CD player");
    hook_import(0x54F608, "winmm.dll", "mixerGetControlDetailsA", cd_volume_get, NULL,
                "music volume: read from the CD player");

    /* MainWndProc clears the "window is active" flag (config+0x310) on WM_SIZE(SIZE_MINIMIZED)
     * and nothing sets it again: once the window had been minimized, the menus stayed frozen.
     * Every WM_SIZE now sets the flag, 0 when minimized and 1 otherwise:
     * cmp edi,1 / jne / mov eax,[cfg] / mov byte [eax+0x310],0
     * -> mov eax,[cfg] / cmp edi,1 / setne byte [eax+0x310] / nop / nop */
    patch(0x47EA43, "\x83\xFF\x01\x75\x0C\xA1\x68\x06\x52\x00\xC6\x80\x10\x03\x00\x00\x00",
          "\xA1\x68\x06\x52\x00\x83\xFF\x01\x0F\x95\x80\x10\x03\x00\x00\x90\x90", 17, "menus go on after a minimize");
    hook_import(0x54F560, "user32.dll", "PeekMessageA", peek_message, &g_peek_message,
                "idle while minimized, read the gamepad");

    g_name_cell = hook(0x42E4A3, 0xE8, "\xE8\xE8\x01\x08\x00", name_cell, "name typing: no crash, keyboard layout");

    /* Computer pigs never walked. The path request FUN_00461A80 ran the path finder only for a
     * destination outside the walk grid (x 0xC00..0x7000, y 0xE00..0x7000) and returned "no
     * path" for one inside, and its only caller (0x44A170) clamps every destination to the
     * inside. Four jumps of the range check swapped: outside -> no path, inside -> find one. */
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

/* The exe still imports CallDLL (LaserLock's old entry point). All 325 call sites that went
 * through it were rewritten to call the real APIs, so nothing calls this. */
int CallDLL(void)
{
    say("CallDLL called - a LaserLock call site was missed");
    return 0;
}

/* The fixes read and write fixed addresses of warhogs_.exe v1.2, so they need that exe. Only the
 * patcher makes an exe import CallDLL from ddraw.dll, and only from that build; a renamed copy
 * still does. The unpatched exe or a tool that loads this DLL gets the renderer only. */
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

/* DllMain (src/ddraw/main.cpp) passes [Render] VSync and [Display] Widescreen as the renderer
 * read them, so the fixes and the renderer go by the same values. */
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
    g_log = fopen(log, "w"); /* open until the game exits; say() flushes every line */
    say("ddraw.dll %s %s", __DATE__, __TIME__);
    apply_fixes(vsync, widescreen);
}
