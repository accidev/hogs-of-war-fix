/* _d3d.dll trace proxy - diagnostic tool, not shipped to players.
 *
 * Install: rename the game's Data\_d3d.dll to Data\_d3d_orig.dll and put this DLL there as
 * _d3d.dll. Every export is forwarded unchanged to the original; on the way the proxy logs
 * the first calls with their stack arguments and keeps per-export call counts, so the
 * renderer API can be checked against the static analysis in docs/renderer/.
 *
 * Output next to this DLL:
 *   d3dtrace.log         call sequence: the first SEQUENCE calls, then the first FIRST_CALLS
 *                        calls of each export (return address + 8 stack dwords)
 *   d3dtrace_counts.txt  call counts per export, rewritten every 2 seconds
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define SEQUENCE 4000
#define FIRST_CALLS 16
#define ARGS 8

static const char *const g_names[] = {
#define X(i, name, ord, internal) name,
#include "exports.h"
#undef X
};

/* Export every stub under the original name and ordinal. */
#define X(i, name, ord, internal) __pragma(comment(linker, "/EXPORT:" name "=" internal ",@" #ord))
#include "exports.h"
#undef X
#define N (sizeof g_names / sizeof g_names[0])

FARPROC g_real[N]; /* read by the stubs' inline asm */
static LONG g_calls[N];
static LONG g_seq;
static int g_loaded;
static DWORD g_last_dump;
static FILE *g_log;
static char g_dir[MAX_PATH];
static CRITICAL_SECTION g_cs;

static void load_real(void)
{
    char path[MAX_PATH];
    HMODULE orig;
    unsigned i;

    sprintf(path, "%s_d3d_orig.dll", g_dir);
    orig = LoadLibraryA(path);
    if (!orig) {
        MessageBoxA(NULL, path, "d3dtrace: cannot load the original renderer", MB_ICONERROR);
        ExitProcess(1);
    }
    for (i = 0; i < N; i++) {
        g_real[i] = GetProcAddress(orig, g_names[i]);
        if (!g_real[i])
            fprintf(g_log, "missing export %s\n", g_names[i]);
    }
}

static void dump_counts(void)
{
    char path[MAX_PATH];
    FILE *f;
    unsigned i;

    sprintf(path, "%sd3dtrace_counts.txt", g_dir);
    f = fopen(path, "w");
    if (!f)
        return;
    for (i = 0; i < N; i++)
        fprintf(f, "%10ld  %s\n", g_calls[i], g_names[i]);
    fclose(f);
}

/* frame[0] = return address into the game, frame[1..] = the call's stack arguments */
static void __cdecl trace(int i, const DWORD *frame)
{
    LONG n = InterlockedIncrement(&g_calls[i]);
    LONG seq = InterlockedIncrement(&g_seq);
    int k;

    EnterCriticalSection(&g_cs);
    if (!g_loaded) {
        load_real();
        g_loaded = 1;
    }
    if (seq <= SEQUENCE || n <= FIRST_CALLS) {
        fprintf(g_log, "%6ld %-28s #%-4ld ret=%08lX", seq, g_names[i], n, frame[0]);
        for (k = 1; k <= ARGS; k++)
            fprintf(g_log, " %08lX", frame[k]);
        fputc('\n', g_log);
        fflush(g_log);
    }
    if (GetTickCount() - g_last_dump > 2000) {
        g_last_dump = GetTickCount();
        dump_counts();
    }
    LeaveCriticalSection(&g_cs);
}

/* pushad saves all registers (32 bytes), so [esp+32] is the game's return address. */
#define X(i, name, ord, internal)                             \
    __declspec(naked) void stub_##i(void)                     \
    {                                                         \
        __asm pushad                                          \
        __asm lea eax, [esp + 32]                             \
        __asm push eax                                        \
        __asm push i                                          \
        __asm call trace                                      \
        __asm add esp, 8                                      \
        __asm popad                                           \
        __asm jmp dword ptr [g_real + 4 * i]                  \
    }
#include "exports.h"
#undef X

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    char path[MAX_PATH];
    char *slash;

    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&g_cs);
        GetModuleFileNameA(inst, g_dir, MAX_PATH);
        slash = strrchr(g_dir, '\\');
        if (slash)
            slash[1] = '\0';
        sprintf(path, "%sd3dtrace.log", g_dir);
        g_log = fopen(path, "w");
    } else if (reason == DLL_PROCESS_DETACH && g_log) {
        dump_counts();
        fclose(g_log);
    }
    return TRUE;
}
