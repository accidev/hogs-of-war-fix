/* The game fixes in ddraw.dll - what their source files share. hogs.c has the log and the patch helpers. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define GAME_WINDOW (*(HWND *)0x520860)
#define GAME_CONFIG (*(BYTE **)0x520668) /* the game's settings block, allocated at start-up */

extern char g_ini[MAX_PATH]; /* hogs.ini next to the exe */

void say(const char *fmt, ...);
BOOL patch(DWORD va, const void *old, const void *new_bytes, SIZE_T len, const char *what);
BOOL hook(DWORD va, BYTE op, const void *old5, const void *fn, const char *what);
BOOL hook_import(DWORD slot_va, const char *dll, const char *fn, const void *repl, void *orig, const char *what);

/* frames.c: battle loop without a busy spin, frames between the game ticks, menu speed.
 * vsync: [Render] VSync, as the renderer read it. */
void frames_init(BOOL vsync);

/* gamepad.c: XInput controllers */
void gamepad_init(void);
void gamepad_poll(void); /* from the game's message loops, see peek_message() in hogs.c */
