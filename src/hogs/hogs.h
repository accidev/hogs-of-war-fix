/* The game fixes in ddraw.dll - what their source files share. hogs.c has the log and the patch helpers. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define GAME_WINDOW (*(HWND *)0x520860)
#define GAME_CONFIG (*(BYTE **)0x520668) /* the game's settings block, allocated at start-up */
#define FRONTEND (*(DWORD *)0x51F120)    /* not 0: a menu is on screen */
#define MENU (*(const int **)0x51AAD8)   /* the menu the keys go to, a 0x48-byte record */
#define KEY_WANTED (*(DWORD *)0x51AC50)  /* Options, Controls waits for the key to bind */
#define VIDEO (GAME_CONFIG && GAME_CONFIG[2]) /* PlayBinkVideo plays; the menu under it is not on the screen */

extern char g_ini[MAX_PATH];

void say(const char *fmt, ...);
BOOL patch(DWORD va, const void *old, const void *new_bytes, SIZE_T len, const char *what);
BOOL hook(DWORD va, BYTE op, const void *old5, const void *fn, const char *what);
BOOL hook_import(DWORD slot_va, const char *dll, const char *fn, const void *repl, void *orig, const char *what);

/* hogs.c: TRUE for a key-down or click, the message now in the window procedure, too early to end the video */
BOOL video_early(void);

/* frames.c: battle loop, frames between game ticks, menu speed; vsync = [Render] VSync */
void frames_init(BOOL vsync);
extern float *g_water_y;      /* renderer's water grid, set when _d3d.dll is patched */
void __cdecl water_slide(void); /* called from _d3d.dll in place of its loop */

/* net.c: network games through a lobby server, [Network] Server */
void net_init(void);

/* menu.c: the mouse in the menus and the Controls screen; menu_input: TRUE = the message is done */
extern WNDPROC g_game_proc; /* MainWndProc, once hogs.c has taken the window's procedure over */
void menu_init(void);
BOOL menu_input(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
WPARAM menu_key(WPARAM vk, LPARAM lp); /* the key MainWndProc is to see for a WM_KEYDOWN */
void menu_keys(void);                  /* in the menus: savekeys follows the keys of Options, Controls */

/* team.c: the team screen (screen type 5) drawn and walked by our code; x, y: the 640 x 480 picture */
void team_init(void);
int team_item_at(const int *menu, int x, int y, int *turn); /* *turn: -1 or 1 on an arrow of a pig's class */
BOOL team_turn(int *menu, int by);                          /* the class of the pig under the cursor; FALSE: none */

/* ace.c: the Ace's own look, a tenth pig model */
extern BOOL g_ace_look; /* the models now loaded have it */
void ace_init(void);
void ace_renderer(BYTE *renderer); /* _d3d.dll, of the build fix_renderer() knows */

/* gamepad.c: XInput controllers, and mouse buttons as keys */
#define MOUSE_KEY 0xF0 /* the five mouse buttons in the game's 256 key bytes: codes no keyboard has */
void gamepad_init(void);
void gamepad_poll(void);            /* from the game's message loops, peek_message() */
BYTE __cdecl read_keys(BYTE *keys); /* the game's keyboard read (FUN_0044CBB0) and the mouse buttons */
