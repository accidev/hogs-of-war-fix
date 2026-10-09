/* hogs.dll - runtime fixes for Hogs of War (2000), warhogs_.exe v1.2.
 *
 * The patched warhogs_.exe imports this DLL in the slot that used to load LaserLock's
 * wh32lib.dll, so Windows maps it and runs DllMain before the game's entry point.
 * Every fix checks the original bytes first and leaves a build it does not know alone.
 * Settings: hogs.ini next to the exe. Log: hogs.log next to the exe.
 */
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *g_log;
static char g_ini[MAX_PATH];

static void say(const char *fmt, ...)
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
static BOOL patch(DWORD va, const void *old, const void *new_bytes, SIZE_T len, const char *what)
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

/* Route every call of the original function at va to fn: a jmp over its first 5 bytes.
 * fn must have the same calling convention and arguments as the original. */
static BOOL hook(DWORD va, const void *old5, const void *fn, const char *what)
{
    BYTE jmp[5] = { 0xE9 };
    DWORD rel = (DWORD)(ULONG_PTR)fn - (va + 5);

    memcpy(jmp + 1, &rel, 4);
    return patch(va, old5, jmp, 5, what);
}

/* Point an import slot at repl, after checking it holds dll!fn; *orig (if given) gets the real one. */
static BOOL hook_import(DWORD slot_va, const char *dll, const char *fn, const void *repl, void *orig, const char *what)
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

static void fix_renderer(HMODULE renderer)
{
    DWORD base = (DWORD)(ULONG_PTR)renderer;

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

/* Windowed mode: FUN_0044D040(w, h) sizes the game window for a w x h client area and centres
 * it. The original adds system metrics by hand (frame, caption and a menu bar the window does
 * not have) and is a few pixels off on Windows 10/11; the renderer sizes its windowed back
 * buffer from the client area, so 640x480 was rendered into 638x478 or 640x500. */
static void __stdcall size_game_window(int w, int h)
{
    HWND hwnd = *(HWND *)0x520860;
    RECT r = { 0, 0, w, h };
    MONITORINFO mi = { sizeof mi };

    if (!hwnd)
        return;
    if (IsZoomed(hwnd)) /* a maximized window would keep its size */
        ShowWindow(hwnd, SW_RESTORE);
    AdjustWindowRectEx(&r, (DWORD)GetWindowLongA(hwnd, GWL_STYLE), FALSE, (DWORD)GetWindowLongA(hwnd, GWL_EXSTYLE));
    GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    w = r.right - r.left;
    h = r.bottom - r.top;
    SetWindowPos(hwnd, NULL, mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2,
                 mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - h) / 2, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
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

static void apply_fixes(void)
{
    /* call [SystemParametersInfoA] -> add esp,16 (the stdcall's 4 arguments) */
    static const BYTE spi_call[] = { 0xFF, 0x15, 0xE8, 0xF5, 0x54, 0x00 };
    static const BYTE spi_skip[] = { 0x83, 0xC4, 0x10, 0x90, 0x90, 0x90 };
    int windowed = GetPrivateProfileIntA("Display", "Windowed", 1, g_ini);
    int resizable = GetPrivateProfileIntA("Display", "Resizable", 1, g_ini);

    hook(0x44CE80, "\x8B\x44\x24\x08\x33", keep_foreign_windows, "leave foreign windows enabled");

    /* SystemParametersInfoA(SPI_SCREENSAVERRUNNING) is a Win9x trick to block Alt+Tab and
     * Ctrl+Alt+Del; the game sets it on window creation and clears it on WM_DESTROY. */
    patch(0x44CFC1, spi_call, spi_skip, 6, "no SPI_SCREENSAVERRUNNING on start");
    patch(0x47EA97, spi_call, spi_skip, 6, "no SPI_SCREENSAVERRUNNING on exit");

    if (windowed) {
        /* Init (0x47F0B0) hardcodes cfg+0x314 ("fullscreen") = 1 with `mov [edx+0x314],ebx`.
         * The config block was zeroed just before, so dropping the store selects the
         * developers' windowed mode: DDSCL_NORMAL, mode index -1, framed window sized to the
         * game resolution. The launcher still forces fullscreen for a non-primary adapter. */
        patch(0x47F174, "\x89\x9A\x14\x03\x00\x00", "\x90\x90\x90\x90\x90\x90", 6, "windowed mode");

        /* Main window style WS_CAPTION|WS_SYSMENU -> WS_OVERLAPPEDWINDOW: resizable, with
         * minimize/maximize. WM_SIZE already re-reads the client rect the frame is blitted to. */
        if (resizable)
            patch(0x44CF58, "\x68\x00\x00\xC8\x00", "\x68\x00\x00\xCF\x00", 5, "resizable window");

        /* Exact client area for the game resolution. Pairs with the renderer's bMenu fix in
         * fix_renderer(): one without the other just moves the black band. */
        hook(0x44D040, "\xA1\x60\x08\x52\x00", size_game_window, "exact window size");
    }

    hook_import(0x54F434, "kernel32.dll", "LoadLibraryA", load_library, &g_load_library,
                "fix the renderer when it is loaded");
}

/* The exe still imports CallDLL (LaserLock's old entry point). All 325 call sites that went
 * through it were rewritten to call the real APIs, so nothing calls this. */
int CallDLL(void)
{
    say("CallDLL called - a LaserLock call site was missed");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    char log[MAX_PATH];
    char *slash;

    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameA(NULL, g_ini, MAX_PATH);
        slash = strrchr(g_ini, '\\');
        if (slash)
            slash[1] = '\0';
        strcpy(log, g_ini);
        strcat(g_ini, "hogs.ini");
        strcat(log, "hogs.log");
        g_log = fopen(log, "w");
        say("hogs.dll %s %s", __DATE__, __TIME__);
        apply_fixes();
    } else if (reason == DLL_PROCESS_DETACH && g_log) {
        fclose(g_log);
    }
    return TRUE;
}
