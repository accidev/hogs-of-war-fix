/* hogs.dll - runtime fixes for Hogs of War (2000), warhogs_.exe v1.2.
 *
 * The patched warhogs_.exe imports this DLL in the slot that used to load LaserLock's
 * wh32lib.dll, so Windows maps it and runs DllMain before the game's entry point.
 * Every fix checks the original bytes first and leaves a build it does not know alone.
 * Settings: hogs.ini next to the exe. Log: hogs.log next to the exe.
 */
#include <windows.h>
#include <mmsystem.h>
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

/* Write a jmp (op 0xE9) or call (0xE8) to fn over the 5 bytes at va. A jmp over a function's
 * first bytes routes every call of it to fn; fn must have the same calling convention and
 * arguments as the code it replaces. */
static BOOL hook(DWORD va, BYTE op, const void *old5, const void *fn, const char *what)
{
    BYTE jmp[5] = { op };
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

/* The game resolution of the current screen (menus 640x480, battles the launcher's choice),
 * set by size_game_window() on every screen change. */
static int g_game_w, g_game_h;

/* _d3d.dll sizes its back buffer from GetClientRect (FUN_10005820) and blits each frame to the
 * client rect (CopyToScreen; hogsdraw ignores that rect and scales the frame into the window
 * itself). Report the game resolution for the game window, so the window can have any size. */
static BOOL WINAPI game_client_rect(HWND hwnd, LPRECT r)
{
    if (!g_game_w || hwnd != *(HWND *)0x520860)
        return GetClientRect(hwnd, r);
    return SetRect(r, 0, 0, g_game_w, g_game_h);
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

static WNDPROC g_game_proc;

static LRESULT CALLBACK game_window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_MOUSEWHEEL) {
        for (g_wheel += GET_WHEEL_DELTA_WPARAM(wp); g_wheel >= WHEEL_DELTA; g_wheel -= WHEEL_DELTA)
            zoom_camera(-10); /* wheel forward: closer */
        for (; g_wheel <= -WHEEL_DELTA; g_wheel += WHEEL_DELTA)
            zoom_camera(10);
    } else if (msg == WM_KEYDOWN && (wp == VK_ADD || wp == VK_SUBTRACT)) {
        zoom_camera(wp == VK_ADD ? -10 : 10);
    }
    return CallWindowProcA(g_game_proc, hwnd, msg, wp, lp);
}

/* Windowed mode: FUN_0044D040(w, h) sized the game window to a w x h client area on every
 * screen change (menus 640x480, battles the launcher's resolution), a small window on today's
 * monitors. The renderer gets the game resolution from game_client_rect() and scales the frame
 * into any window, so the window is placed once: [Display] Borderless=1 covers the monitor,
 * otherwise it is the largest window with the game's aspect ratio that fits the work area.
 * After that it keeps the size and state the player gives it. */
static BOOL g_borderless;

static void __stdcall size_game_window(int w, int h)
{
    HWND hwnd = *(HWND *)0x520860;
    MONITORINFO mi = { sizeof mi };
    RECT r = { 0, 0, w, h };
    int fw, fh, cw, ch;

    g_game_w = w;
    g_game_h = h;
    if (!hwnd || g_game_proc)
        return;
    g_game_proc = (WNDPROC)SetWindowLongA(hwnd, GWL_WNDPROC, (LONG)game_window_proc);
    GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    if (g_borderless) {
        SetWindowLongA(hwnd, GWL_STYLE, (GetWindowLongA(hwnd, GWL_STYLE) & WS_VISIBLE) | WS_POPUP);
        SetWindowPos(hwnd, NULL, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        return;
    }
    AdjustWindowRectEx(&r, (DWORD)GetWindowLongA(hwnd, GWL_STYLE), FALSE, (DWORD)GetWindowLongA(hwnd, GWL_EXSTYLE));
    fw = r.right - r.left - w; /* frame and caption */
    fh = r.bottom - r.top - h;
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

/* [Startup] SkipLauncher=1: the game asks for adapter, resolution and detail level in the
 * "LauncherBox" dialog on every start (FUN_00481230). Its WM_INITDIALOG (FUN_004815D0) selects
 * the choice saved in launch.bin and stores it in the game config; Start (IDOK) only ends the
 * dialog with 1. So with a launch.bin from an earlier start, the dialog ends right after
 * WM_INITDIALOG, before it is shown. Shift held while the game starts shows it as before. */
static char g_launch_bin[MAX_PATH];
static DLGPROC g_launcher_proc;

static INT_PTR CALLBACK launcher_start(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    INT_PTR r = g_launcher_proc(dlg, msg, wp, lp);

    if (msg == WM_INITDIALOG)
        EndDialog(dlg, 1);
    return r;
}

static INT_PTR WINAPI launcher_box(HINSTANCE inst, LPCSTR name, HWND parent, DLGPROC proc, LPARAM lp)
{
    if (!IS_INTRESOURCE(name) && strcmp(name, "LauncherBox") == 0 && !(GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
        GetFileAttributesA(g_launch_bin) != INVALID_FILE_ATTRIBUTES) {
        g_launcher_proc = proc;
        proc = launcher_start;
    }
    return DialogBoxParamA(inst, name, parent, proc, lp);
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

static void apply_fixes(void)
{
    /* call [SystemParametersInfoA] -> add esp,16 (the stdcall's 4 arguments) */
    static const BYTE spi_call[] = { 0xFF, 0x15, 0xE8, 0xF5, 0x54, 0x00 };
    static const BYTE spi_skip[] = { 0x83, 0xC4, 0x10, 0x90, 0x90, 0x90 };
    int windowed = GetPrivateProfileIntA("Display", "Windowed", 1, g_ini);
    int resizable = GetPrivateProfileIntA("Display", "Resizable", 1, g_ini);
    int deadzone = GetPrivateProfileIntA("Gamepad", "Deadzone", 15, g_ini);
    BYTE deadzone_mov[8] = { 0xC7, 0x44, 0x24, 0x10 }; /* mov dword [esp+0x10], deadzone */
    DWORD deadzone_value = (DWORD)(deadzone < 0 ? 0 : deadzone > 100 ? 100 : deadzone) * 100;

    hook(0x44CE80, 0xE9, "\x8B\x44\x24\x08\x33", keep_foreign_windows, "leave foreign windows enabled");

    /* SystemParametersInfoA(SPI_SCREENSAVERRUNNING) is a Win9x trick to block Alt+Tab and
     * Ctrl+Alt+Del; the game sets it on window creation and clears it on WM_DESTROY. */
    patch(0x44CFC1, spi_call, spi_skip, 6, "no SPI_SCREENSAVERRUNNING on start");
    patch(0x47EA97, spi_call, spi_skip, 6, "no SPI_SCREENSAVERRUNNING on exit");

    if (windowed) {
        /* Init (0x47F0B0) hardcodes cfg+0x314 ("fullscreen") = 1 with `mov [edx+0x314],ebx`.
         * The config block was zeroed just before, so dropping the store selects the
         * developers' windowed mode: DDSCL_NORMAL, mode index -1, a framed window. The
         * launcher still forces fullscreen for a non-primary adapter. */
        patch(0x47F174, "\x89\x9A\x14\x03\x00\x00", "\x90\x90\x90\x90\x90\x90", 6, "windowed mode");

        /* Main window style WS_CAPTION|WS_SYSMENU -> WS_OVERLAPPEDWINDOW: resizable, with
         * minimize/maximize. WM_SIZE already re-reads the client rect the frame is blitted to. */
        if (resizable)
            patch(0x44CF58, "\x68\x00\x00\xC8\x00", "\x68\x00\x00\xCF\x00", 5, "resizable window");

        g_borderless = GetPrivateProfileIntA("Display", "Borderless", 0, g_ini);
        hook(0x44D040, 0xE9, "\xA1\x60\x08\x52\x00", size_game_window, "big game window, keeps its size");

        /* The launcher lists modes up to 1024x768 only (FUN_004815D0); a window takes any size. */
        patch(0x481726, "\x3D\x00\x04\x00\x00", "\x3D\x00\x08\x00\x00", 5, "launcher: modes up to 2048 wide");
        patch(0x481740, "\x81\xF9\x00\x03\x00\x00", "\x81\xF9\x00\x06\x00\x00", 6, "launcher: modes up to 1536 high");

        /* the camera zoom lives in the window procedure; table entries 0 and 1 must match */
        if (memcmp((void *)0x4D9528, "\x00\x0C\x00\x03\x00\x00\x00\x0C\x00\x04\x00\x00", 12) == 0) {
            memcpy(g_camera, (void *)0x4D9528, sizeof g_camera);
            say("ok   %-40s %08X", "camera zoom: mouse wheel, numpad +/-", 0x4D9528);
        } else {
            say("skip %-40s %08X: unexpected bytes, unknown game build?", "camera zoom: mouse wheel, numpad +/-", 0x4D9528);
        }

        /* without this, Windows stretches the window at 125 % or 150 % display scaling: blurry */
        SetProcessDPIAware();
    }

    if (GetPrivateProfileIntA("Startup", "SkipLauncher", 1, g_ini))
        hook_import(0x54F55C, "user32.dll", "DialogBoxParamA", launcher_box, NULL,
                    "start without the launcher (Shift: show)");
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

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    char dir[MAX_PATH], log[MAX_PATH];
    char *slash;

    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameA(NULL, dir, MAX_PATH);
        slash = strrchr(dir, '\\');
        if (slash)
            slash[1] = '\0';
        snprintf(g_ini, sizeof g_ini, "%shogs.ini", dir);
        snprintf(log, sizeof log, "%shogs.log", dir);
        snprintf(g_launch_bin, sizeof g_launch_bin, "%slaunch.bin", dir);
        g_log = fopen(log, "w");
        say("hogs.dll %s %s", __DATE__, __TIME__);
        apply_fixes();
    } else if (reason == DLL_PROCESS_DETACH && g_log) {
        fclose(g_log);
    }
    return TRUE;
}
