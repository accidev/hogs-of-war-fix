#define INITGUID /* define the DirectDraw / Direct3D IIDs here */
#include "objects.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <set>
#include <string>

Config g_config;

static FILE *g_log;
static CRITICAL_SECTION g_log_lock;

void log_printf(const char *fmt, ...)
{
    if (!g_log)
        return;
    EnterCriticalSection(&g_log_lock);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_log_lock);
}

void unknown_interface(const char *on, REFIID riid)
{
    log_printf("QueryInterface on %s: interface {%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X} not supported", on,
               riid.Data1, riid.Data2, riid.Data3, riid.Data4[0], riid.Data4[1], riid.Data4[2], riid.Data4[3],
               riid.Data4[4], riid.Data4[5], riid.Data4[6], riid.Data4[7]);
}

void unimplemented(const char *what)
{
    static std::set<std::string> seen;
    if (seen.insert(what).second)
        log_printf("UNIMPLEMENTED %s", what);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&g_log_lock);
        char dir[MAX_PATH], path[MAX_PATH];
        GetModuleFileNameA(nullptr, dir, MAX_PATH);
        char *slash = strrchr(dir, '\\');
        if (slash)
            slash[1] = '\0';
        snprintf(path, sizeof path, "%shogsdraw.log", dir);
        g_log = fopen(path, "w");
        snprintf(path, sizeof path, "%shogs.ini", dir);
        g_config.scale = GetPrivateProfileIntA("Render", "Scale", 0, path);
        g_config.vsync = GetPrivateProfileIntA("Render", "VSync", 1, path);
        log_printf("hogsdraw %s %s: scale %d, vsync %d", __DATE__, __TIME__, g_config.scale, g_config.vsync);
    } else if (reason == DLL_PROCESS_DETACH && g_log) {
        fclose(g_log);
        g_log = nullptr;
    }
    return TRUE;
}
