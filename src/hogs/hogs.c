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
