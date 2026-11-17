/* menu.c - the mouse in the game's menus, the Controls screen with every action on it, the keys of this PC.
 *
 * The menus know keys only. A menu is a 0x48-byte record, the current one at [0x51AAD8]: [0] items,
 * [3] cursor, [9] screen type, [14] and [15] where its board stands. An item's text box is {x, y, w, h}
 * at 0x4C1728[0x51A890[screen type] + item], in a field of 1024 x 820 that the game draws as 640 x 480
 * (FUN_0041ADB0, FUN_0041ADD0). The window procedure of hogs.c gives its messages to menu_input(), and
 * the mouse presses the keys the menu knows. Hooked:
 *   0x42DEA0  jmp, Controls (screen type 15) drawn: 6 of 12 lines, scrolling -> controls_draw
 *   0x42A140  jmp, an arrow key on that screen                               -> controls_move
 *   0x42E11A  call, keyboard read while the screen waits for a key to bind   -> wanted_keys
 *   0x426E3C  call, default keys at the first menu; then those of savekeys   -> first_keys
 */
#include "hogs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BOARDS_MOVE (*(int *)0x51AAE4) /* not 0: text is drawn at the board's place, menu[14] and [15] */
#define FIRST_BOX ((const int *)0x51A890)
#define BOXES ((const int (*)[4])0x4C1728)
#define BOX_MORE 80 /* FUN_0041AE00([0x4C0C5C]): the game makes every text box this much wider */

/* Controls: the game has 16 actions and shows 12, one per line */
#define KEYS ((WORD *)0x5208D8)             /* key code of each action, as in the 256 key bytes */
#define JOY_BUTTONS ((WORD *)0x5208F8)      /* joystick button mask of each action */
#define LINE_ACTION ((const int *)0x4D3010) /* line -> action */
#define NAMED_KEYS ((const int *)0x4C0110)  /* key codes that have a name, -1 ends; the name: string 0x169 + place */
#define TEAM (*(BYTE **)0x51C560)           /* +0xE the keys, +0x2E the joystick buttons: saved with the team */
#define KEYS_CHANGED (*(BYTE *)0x51AC54)
#define BLINK (*(int *)0x51AC4C)
#define INPUT (*(BYTE **)0x51A5B0) /* +0x148 the joystick, -1: none */
#define GRID_TEXT (*(const int **)0x51BA54)  /* the text object that draws the name grid */
#define GRID_COLUMNS (*(const int *)0x51AAE8) /* FUN_004284D0 sets both from that object every frame */
#define GRID_LETTERS (*(const int *)0x51AAF0)
#define SAVED_NAMES ((const char *)0x51A740) /* the team of each of the 8 saved games, 0x14 bytes; first byte -1: none */
enum { NAME_GRID = 0, TEAM_SCREEN = 5, LOAD = 8, SAVE = 9, CAREER = 13, CONTROLS = 15, LINES = 12, S_ACTION = 0x15D, S_KEY = 0x169, S_NO_NAME = 0x1CA };
enum { HINT_X = 350, HINT_Y = 12, HINT_W = 480 }; /* over the title, in the field of 1024 x 820 */

/* The game's text object: +0x2C, +0x30, +0x34 colour, +0x38 alpha, +0x44 top and +0x4C height of the window */
typedef void(__fastcall *window_fn)(int *text, void *, int x, int y, int w, int h);
typedef void(__fastcall *string_fn)(int *text, void *, int string, int x, int y);
typedef void(__fastcall *ascii_fn)(int *text, void *, const char *ascii, int x, int y);
typedef const char *(__fastcall *lookup_fn)(int *text, void *, int string);
static const window_fn text_window = (window_fn)0x430E00;
static const string_fn text_string = (string_fn)0x431320; /* a string of fetext by number; x, y -1: go on */
static const ascii_fn text_ascii = (ascii_fn)0x430ED0;    /* '/' is a command there */
static const lookup_fn text_of = (lookup_fn)0x431680;     /* a string of fetext, every char less 0x1F */
static void(__fastcall *const text_line)(int *text) = (void(__fastcall *)(int *))0x431AC0; /* next line */
static void(__cdecl *const sound)(int which, int volume) = (void(__cdecl *)(int, int))0x420060;
/* FUN_00427C90: the cursor of a menu went from item `was`; the board follows it */
static void(__fastcall *const cursor_moved)(int *menu, void *, int was, int opened) = (void(__fastcall *)(int *, void *, int, int))0x427C90;

int hogsdraw_game_point(int *x, int *y); /* ddraw/gpu.cpp */

static BOOL g_controls;      /* the Controls screen is ours */
static DWORD g_mouse_bound;  /* tick of a mouse button seen down while the screen waited for a key */
static int g_team_turn;      /* team screen: the point is on an arrow of a pig's class, -1 or 1 */

static void press(HWND hwnd, WPARAM vk)
{
    /* MainWndProc takes a key-down only after a key-up; the last one is for the next key of the keyboard */
    CallWindowProcA(g_game_proc, hwnd, WM_KEYUP, vk, (LPARAM)0xC0000001);
    CallWindowProcA(g_game_proc, hwnd, WM_KEYDOWN, vk, 1);
    CallWindowProcA(g_game_proc, hwnd, WM_KEYUP, vk, (LPARAM)0xC0000001);
}

/* FUN_0042AB40: on these screens only Left and Right move the cursor (4 and 12: Yes and No side by side) */
static BOOL across(const int *menu)
{
    return menu[9] == 4 || menu[9] == 12 || menu[9] == CAREER;
}

/* How many items of the menu stand in the box table one after another: the mouse can point at these. */
static int lines(const int *menu)
{
    switch (menu[9]) {
    case NAME_GRID: /* the letters, then Delete, Space and End */
        return GRID_LETTERS + 3;
    case 1:
    case 2:
    case 4: /* 4 and 12: Yes and No side by side */
    case 6:
    case 7:
    case LOAD:
    case SAVE:
    case 12:
    case 18: /* network lists */
    case 20:
    case 21: /* more items than the screen type has boxes: a list that scrolls, not known here */
        return menu[0] > 0 && menu[0] <= FIRST_BOX[menu[9] + 1] - FIRST_BOX[menu[9]] ? menu[0] : 0;
    case TEAM_SCREEN:
        return menu[0];
    case CAREER: /* menu 25, the four career paths; menu 40 fills its items itself, not known here */
        return menu[0] == 4 ? 4 : 0;
    case CONTROLS: /* [0] is 1 there */
        return g_controls ? LINES : 0;
    }
    return 0;
}

/* Name grid (screen type 0), FUN_00431380: letter n stands in column n % GRID_COLUMNS; a column is the
 * widest letter and three gaps, a row two lines. Delete, Space and End stand one column further right. */
static int grid_item_at(const int *menu, int x, int y)
{
    const int *text = GRID_TEXT;
    const short *font = (const short *)((const BYTE *)text + 0x3F88); /* widest letter, line height */
    int gap = text[5], step = font[0] + gap * 3, line = (font[1] + text[6]) * 2, col, row;

    x -= (BOXES[FIRST_BOX[NAME_GRID]][0] + menu[14]) * 640 / 1024 - (step - font[0]) / 2;
    y -= (BOXES[FIRST_BOX[NAME_GRID]][1] + menu[15]) * 480 / 820 - (line - font[1]) / 2;
    if (x < 0 || y < 0 || step <= 0 || line <= 0)
        return -1;
    col = x / step;
    row = y / line;
    if (col < GRID_COLUMNS)
        return row * GRID_COLUMNS + col < GRID_LETTERS ? row * GRID_COLUMNS + col : -1;
    return row < 3 && x >= (GRID_COLUMNS + 1) * step - gap * 4 && x < (GRID_COLUMNS + 4) * step ? GRID_LETTERS + row : -1;
}

/* The item under a point of the 640 x 480 menu picture, or -1. */
static int item_at(const int *menu, int x, int y)
{
    int n = lines(menu), i, best = -1, least = 0;
    const int (*box)[4] = BOXES + FIRST_BOX[menu[9]];
    int board_x = BOARDS_MOVE ? menu[14] : 0, board_y = BOARDS_MOVE ? menu[15] : 0;
    int step = n > 1 ? abs(box[1][1] - box[0][1]) * 480 / 820 : 0;

    if (menu[9] == TEAM_SCREEN)
        return team_item_at(menu, x, y, &g_team_turn);
    if (menu[9] == NAME_GRID)
        return grid_item_at(menu, x, y);
    if (menu[9] == CAREER) /* four pictures in a row on the board, 30 apart; their names share one box */
        return n && x >= 272 && x < 392 && y >= 405 && y < 436 ? (x - 272) / 30 : -1;
    for (i = 0; i < n; i++) {
        int width = (box[i][2] + BOX_MORE) * 640 / 1024, height = box[i][3] * 480 / 820;
        int off = abs(x - ((box[i][0] + board_x) * 640 / 1024 + width / 2));
        int reach = (step > height ? step : height * 2) / 2; /* half the way to the next line */

        /* the boxes of two columns overlap: the nearer middle wins */
        if (off * 2 <= width && abs(y - ((box[i][1] + board_y) * 480 / 820 + height / 2)) <= reach && (best < 0 || off < least)) {
            best = i;
            least = off;
        }
    }
    return best;
}

/* Moves the cursor to the item under the mouse with the arrow keys, as a player would. */
static BOOL point_at(HWND hwnd, LPARAM lp)
{
    int x = (short)LOWORD(lp), y = (short)HIWORD(lp), item, steps;
    const int *menu = MENU;

    g_team_turn = 0;
    if (!menu || !hogsdraw_game_point(&x, &y) || (item = item_at(menu, x, y)) < 0)
        return FALSE;
    if (menu[9] == NAME_GRID) { /* FUN_0042AE00 only writes the cursor */
        if (menu[3] != item)
            sound(0xD, 10);
        ((int *)menu)[3] = item;
        return TRUE;
    }
    if (menu[9] == LOAD && SAVED_NAMES[item * 0x14] == -1) /* FUN_0042A820: the arrows pass a game that is not there */
        return FALSE;
    /* FUN_0042ACE0: the arrows walk two columns of the team screen; the cursor is set as the game sets it */
    if (menu[9] == TEAM_SCREEN || menu[9] == LOAD || menu[9] == SAVE) {
        int was = menu[3];

        if (was != item) {
            ((int *)menu)[3] = item;
            cursor_moved((int *)menu, NULL, was, 0);
        }
        return TRUE;
    }
    for (steps = abs(item - menu[3]); steps > 0 && MENU == menu && menu[3] != item; steps--)
        press(hwnd, item > menu[3] ? (across(menu) ? VK_RIGHT : VK_DOWN) : (across(menu) ? VK_LEFT : VK_UP));
    return MENU == menu && menu[3] == item;
}

static void colour(int *text, BOOL chosen)
{
    text[11] = text[12] = chosen ? 0x78 : 0x50; /* the two colours of FUN_00428CF0's lists */
    text[13] = chosen ? 0x4B : 0x2D;
    text[14] = 0xFF;
}

/* The Russian translation (hogs-of-war-ru): 22 Russian letters have the codes 0x6F-0x84, "opqrstuvwxyz{|}~"
 * and on, in the order B G D Yo Zh Z I J L P U F C Ch Sh Shch, hard sign, Y, soft sign, E, Yu, Ya; the
 * other letters are the Latin ones of the same shape. */
static BOOL russian(int *text)
{
    return !strcmp(text_of(text, NULL, S_ACTION), "#Y&1&R"); /* "BxEPEq", forward, each char less 0x1F */
}

/* The key of an action, and its joystick button when a joystick is there, as text for text_ascii. */
static void key_text(int *text, int action, char *out, size_t size)
{
    int key = KEYS[action], i = 0, button = 0;

    if (key >= MOUSE_KEY && key < MOUSE_KEY + 5) {
        i = snprintf(out, size, russian(text) ? "M\x80}\x81 %d" : "MOUSE %d", key - MOUSE_KEY + 1);
    } else {
        const char *name;

        while (NAMED_KEYS[i] != -1 && NAMED_KEYS[i] != key)
            i++;
        name = text_of(text, NULL, NAMED_KEYS[i] == -1 ? S_NO_NAME : S_KEY + i);
        for (i = 0; name[i] && i < (int)size - 1; i++)
            out[i] = (char)(name[i] + 0x1F);
        out[i] = 0;
    }
    if (action < 4 || !JOY_BUTTONS[action] || *(int *)(INPUT + 0x148) == -1) /* FUN_0042E270: no button for the four directions */
        return;
    while (!(JOY_BUTTONS[action] >> button & 1))
        button++;
    snprintf(out + i, size - i, " + JOY %d", button + 1);
}

/* In place of FUN_0042DEA0(menu, text). The board has 12 slots, 6 lines of two: the game put the
 * action left and its key right. Here a slot is one action, its name over its key. */
static void __fastcall controls_draw(int *menu, void *unused, int *text)
{
    const int (*box)[4] = BOXES + FIRST_BOX[CONTROLS];
    int was[4], i;
    char key[48];

    (void)unused;
    memcpy(was, text + 11, sizeof was);
    if (--BLINK < -10)
        BLINK = 10;
    for (i = 0; i < LINES; i++) {
        text_window(text, NULL, box[i][0], box[i][1], box[i][2] + BOX_MORE, box[i][3]);
        text[17] -= 15; /* two lines in a slot, as the game does it for a key with a joystick button */
        text[19] += 30;
        colour(text, i == menu[3]);
        text_string(text, NULL, S_ACTION + i, -1, -1);
        text_line(text);
        if (i == menu[3] && KEY_WANTED && BLINK >= 0) /* the key that is awaited blinks */
            continue;
        key_text(text, LINE_ACTION[i], key, sizeof key);
        text_ascii(text, NULL, key, -1, -1);
    }
    text_window(text, NULL, HINT_X, HINT_Y, HINT_W, 30);
    colour(text, FALSE);
    /* Russian: "DEL - ISHODNYE KLAVISHI" */
    text_ascii(text, NULL, russian(text) ? "DEL - uCXOqH\x80" "E KwABu}u" : "DEL - DEFAULT KEYS", -1, -1);
    memcpy(text + 11, was, sizeof was);
}

/* In place of FUN_0042A140(menu, dx, dy): the cursor went up and down a list of 12 lines. */
static void __fastcall controls_move(int *menu, void *unused, int dx, int dy)
{
    int to = menu[3] + dy + dx * (LINES / 2);

    (void)unused;
    if (to >= 0 && to < LINES)
        menu[3] = to;
    sound(4, 100);
    if (dy)
        sound(0x11, 100);
}

static void(__cdecl *const game_default_keys)(void) = (void(__cdecl *)(void))0x482340; /* into KEYS and JOY_BUTTONS */
/* FUN_0045DE00: the menus' joystick buttons, as FUN_0042E270 sets them after a new button */
static void(__fastcall *const menu_buttons)(BYTE *input, void *, WORD *buttons) = (void(__fastcall *)(BYTE *, void *, WORD *))0x45DE00;

static void default_keys(void)
{
    if (!TEAM)
        return;
    game_default_keys();
    memcpy(TEAM + 0xE, KEYS, 32);
    memcpy(TEAM + 0x2E, JOY_BUTTONS, 32);
    menu_buttons(INPUT, NULL, JOY_BUTTONS);
    KEYS_CHANGED = 1;
    sound(4, 100);
}

/* KEYS and JOY_BUTTONS, 64 bytes, in savekeys beside the game's savearmy files. The game kept them in the
 * saved teams only: after a restart the defaults stood until a saved team was loaded. */
static WORD g_keys_kept[32];
static BOOL g_keys_ready;

/* In place of FUN_00482340 at the first menu */
static void __cdecl first_keys(void)
{
    WORD kept[32];
    FILE *f = fopen("savekeys", "rb");
    int i = 0;

    game_default_keys();
    if (f && fread(kept, sizeof kept, 1, f) == 1)
        while (i < 16 && kept[i] && kept[i] < 256)
            i++;
    if (f)
        fclose(f);
    if (i == 16) {
        memcpy(KEYS, kept, sizeof kept); /* JOY_BUTTONS follow KEYS */
        if (INPUT)
            menu_buttons(INPUT, NULL, JOY_BUTTONS);
        KEYS_CHANGED = 1; /* FUN_0042BB20: a team loaded now takes these keys and gives none */
        say("keys: those of savekeys");
    }
    memcpy(g_keys_kept, KEYS, sizeof g_keys_kept);
    g_keys_ready = TRUE;
}

void menu_keys(void)
{
    FILE *f;

    if (!g_keys_ready || !memcmp(g_keys_kept, KEYS, sizeof g_keys_kept))
        return;
    memcpy(g_keys_kept, KEYS, sizeof g_keys_kept);
    f = fopen("savekeys", "wb");
    if (!f || fwrite(g_keys_kept, sizeof g_keys_kept, 1, f) != 1)
        say("keys: savekeys not written");
    if (f)
        fclose(f);
}

/* In place of the keyboard read of FUN_0042E100, which binds the first key that is down. */
static BYTE __cdecl wanted_keys(BYTE *keys)
{
    BYTE ok = read_keys(keys);
    int i;

    for (i = 0; i < 5; i++)
        if (keys[MOUSE_KEY + i])
            g_mouse_bound = GetTickCount() | 1;
    return ok;
}

/* MainWndProc knows the arrow keys only: a key bound to a direction is that arrow. Not on the name grid
 * (type 0), where a key is a letter. The game's key code: scan code, bit 7 for an extended key */
WPARAM menu_key(WPARAM vk, LPARAM lp)
{
    static const BYTE kArrow[4] = { VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT };
    const int *menu = MENU;
    WORD key = (WORD)((lp >> 16 & 0x7F) | (lp >> 17 & 0x80));
    int i;

    if (FRONTEND && menu && menu[9] && vk != VK_RETURN && vk != VK_BACK && vk != VK_ESCAPE)
        for (i = 0; i < 4; i++)
            if (KEYS[i] == key)
                return kArrow[i];
    return vk;
}

/* TRUE: the message is done; MainWndProc must not get it. */
BOOL menu_input(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    static BOOL shown;
    static LPARAM last = -1;
    static int wheel;
    const int *menu = MENU;
    int x = (short)LOWORD(lp), y = (short)HIWORD(lp);

    switch (msg) {
    case WM_SETCURSOR:
        if (LOWORD(lp) != HTCLIENT)
            return FALSE;
        SetCursor(FRONTEND && !VIDEO ? LoadCursorA(NULL, (LPCSTR)IDC_ARROW) : NULL);
        return TRUE;
    case WM_MOUSEMOVE: /* MainWndProc lowers the cursor's show count on every move: it never gets one */
        if (!shown) {
            shown = TRUE;
            while (ShowCursor(TRUE) < 0)
                ;
        }
        /* Windows repeats the message for a mouse that lies still: that one must not undo the keys */
        if (FRONTEND && !VIDEO && !KEY_WANTED && lp != last)
            point_at(hwnd, lp);
        last = lp;
        return TRUE;
    }
    if (VIDEO) { /* MainWndProc ends a video on a key-down: a click is that key; too early, neither is */
        BOOL click = msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN;

        if ((click || msg == WM_KEYDOWN) && video_early())
            return TRUE;
        if (click)
            press(hwnd, VK_RETURN);
        return click || msg == WM_MOUSEWHEEL;
    }
    if (!FRONTEND || !menu)
        return FALSE;
    if (msg == WM_KEYDOWN && wp == VK_DELETE && menu[9] == CONTROLS && g_controls && !KEY_WANTED) {
        default_keys();
        return TRUE;
    }
    if (msg != WM_LBUTTONDOWN && msg != WM_RBUTTONDOWN && msg != WM_MOUSEWHEEL)
        return FALSE;
    /* the key to bind is read from the keyboard and the mouse buttons, not from messages; the message
     * of a button that was just bound may still come */
    if (KEY_WANTED || (g_mouse_bound && GetTickCount() - g_mouse_bound < 300))
        return TRUE;
    switch (msg) {
    case WM_LBUTTONDOWN:
        if (menu[9] == CONTROLS && g_controls && hogsdraw_game_point(&x, &y) && y < (HINT_Y + 38) * 480 / 820 &&
            abs(x - (HINT_X + HINT_W / 2) * 640 / 1024) < HINT_W * 640 / 1024 / 2)
            default_keys();
        /* on a screen of lines only a line can be clicked; elsewhere a click in the picture is Enter */
        else if (point_at(hwnd, lp) || (!lines(menu) && hogsdraw_game_point(&x, &y))) {
            if (!g_team_turn || !team_turn((int *)menu, g_team_turn))
                press(hwnd, VK_RETURN);
        }
        break;
    case WM_RBUTTONDOWN:
        press(hwnd, VK_BACK);
        break;
    case WM_MOUSEWHEEL:
        /* over a pig of a multi-player team the wheel goes through its classes */
        for (wheel += GET_WHEEL_DELTA_WPARAM(wp); wheel >= WHEEL_DELTA; wheel -= WHEEL_DELTA)
            if (menu[9] != TEAM_SCREEN || !team_turn((int *)menu, -1))
                press(hwnd, across(menu) ? VK_LEFT : VK_UP);
        for (; wheel <= -WHEEL_DELTA; wheel += WHEEL_DELTA)
            if (menu[9] != TEAM_SCREEN || !team_turn((int *)menu, 1))
                press(hwnd, across(menu) ? VK_RIGHT : VK_DOWN);
        break;
    }
    return TRUE;
}

void menu_init(void)
{
    g_controls = hook(0x42DEA0, 0xE9, "\x83\xEC\x18\x53\x55", controls_draw, "menus: every control on one screen") &&
                 hook(0x42A140, 0xE9, "\x8B\x41\x0C\x56\x8B", controls_move, "menus: four arrows on the controls screen");
    hook(0x42E11A, 0xE8, "\xE8\x91\xEA\x01\x00", wanted_keys, "menus: mouse buttons can be bound");
    hook(0x426E3C, 0xE8, "\xE8\xFF\xB4\x05\x00", first_keys, "keys: kept in savekeys between games");
}
