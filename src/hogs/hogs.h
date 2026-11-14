/* The game fixes in ddraw.dll - what their source files share. hogs.c has the log and the patch helpers. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define GAME_WINDOW (*(HWND *)0x520860)
#define GAME_CONFIG (*(BYTE **)0x520668) /* the game's settings block, allocated at start-up */
#define FRONTEND (*(DWORD *)0x51F120)    /* not 0: a menu is on screen */

extern char g_ini[MAX_PATH];

void say(const char *fmt, ...);
BOOL patch(DWORD va, const void *old, const void *new_bytes, SIZE_T len, const char *what);
BOOL hook(DWORD va, BYTE op, const void *old5, const void *fn, const char *what);
BOOL hook_import(DWORD slot_va, const char *dll, const char *fn, const void *repl, void *orig, const char *what);

/* frames.c: battle loop, frames between game ticks, menu speed; vsync = [Render] VSync */
void frames_init(BOOL vsync);
extern float *g_water_y;      /* renderer's water grid, set when _d3d.dll is patched */
void __cdecl water_slide(void); /* called from _d3d.dll in place of its loop */

/* net.c: network games through a lobby server, [Network] Server */
void net_init(void);

/* gamepad.c: XInput controllers */
void gamepad_init(void);
void gamepad_poll(void); /* from the game's message loops, peek_message() */
