/* hogs.dll - what its source files share. hogs.c has the log and the patch helpers. */
#include <windows.h>

#define GAME_WINDOW (*(HWND *)0x520860)

extern char g_ini[MAX_PATH]; /* hogs.ini next to the exe */

void say(const char *fmt, ...);
BOOL patch(DWORD va, const void *old, const void *new_bytes, SIZE_T len, const char *what);
BOOL hook(DWORD va, BYTE op, const void *old5, const void *fn, const char *what);
BOOL hook_import(DWORD slot_va, const char *dll, const char *fn, const void *repl, void *orig, const char *what);

/* frames.c: battle loop without a busy spin, frames between the game ticks, menu speed */
void frames_init(void);

/* gamepad.c: XInput controllers */
void gamepad_init(void);
void gamepad_poll(void); /* from the game's message loops, see peek_message() in hogs.c */
