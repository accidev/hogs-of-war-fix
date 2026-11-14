/* menu.c - the mouse in the game's menus.
 *
 * The menus know keys only. A menu is a 0x48-byte record, the current one at [0x51AAD8]: [0] items,
 * [3] cursor, [4] first shown line of a scrolling list, [9] screen type, [14] and [15] where its board
 * stands. An item's text box is {x, y, w, h} at 0x4C1728[0x51A890[screen type] + item], in a field of
 * 1024 x 820 that the game draws as 640 x 480 (FUN_0041ADB0, FUN_0041ADD0).
 * No hooks: the window procedure of hogs.c gives the mouse messages to menu_mouse(), and the mouse
 * presses the keys the menu knows.
 */
#include "hogs.h"
#include <stdlib.h>

#define BOARDS_MOVE (*(int *)0x51AAE4) /* not 0: text is drawn at the board's place, menu[14] and [15] */
#define FIRST_BOX ((const int *)0x51A890)
#define BOXES ((const int (*)[4])0x4C1728)

int hogsdraw_game_point(int *x, int *y); /* ddraw/gpu.cpp */

static void press(HWND hwnd, WPARAM vk)
{
    /* MainWndProc takes a key-down only after a key-up; the last one is for the next key of the keyboard */
    CallWindowProcA(g_game_proc, hwnd, WM_KEYUP, vk, (LPARAM)0xC0000001);
    CallWindowProcA(g_game_proc, hwnd, WM_KEYDOWN, vk, 1);
    CallWindowProcA(g_game_proc, hwnd, WM_KEYUP, vk, (LPARAM)0xC0000001);
}

/* The lines the mouse can point at: where the first one is in the box table, and which item it is.
 * Only screens whose items stand one under another in the table; 0: not such a screen. */
static int lines(const int *menu, int *first_item)
{
    *first_item = 0;
    switch (menu[9]) {
    case 1:
    case 2:
    case 4: /* 4 and 12: Yes and No side by side */
    case 6:
    case 7:
    case 12:
    case 18: /* network lists */
    case 20:
    case 21: /* more items than the screen type has boxes: a list that scrolls, not known here */
        return menu[0] > 0 && menu[0] <= FIRST_BOX[menu[9] + 1] - FIRST_BOX[menu[9]] ? menu[0] : 0;
    case 15: /* Controls: six lines of a longer list, [0] is 1 */
        *first_item = menu[4];
        return 6;
    }
    return 0;
}

/* The item under a point of the 640 x 480 menu picture, or -1. */
static int item_at(const int *menu, int x, int y)
{
    int first, n = lines(menu, &first), i;
    const int (*box)[4] = BOXES + FIRST_BOX[menu[9]];
    int board_x = BOARDS_MOVE ? menu[14] : 0, board_y = BOARDS_MOVE ? menu[15] : 0;
    int step = n > 1 ? abs(box[1][1] - box[0][1]) * 480 / 820 : 0;

    for (i = 0; i < n; i++) {
        int left = (box[i][0] + board_x) * 640 / 1024, width = (box[i][2] + 80) * 640 / 1024; /* 80: FUN_0041AE00(50) */
        int top = (box[i][1] + board_y) * 480 / 820, height = box[i][3] * 480 / 820;
        int reach = (step > height ? step : height * 2) / 2; /* half the way to the next line */

        if (x >= left && x < left + width && abs(y - (top + height / 2)) <= reach)
            return first + i;
    }
    return -1;
}

/* Moves the cursor to the item under the mouse with the arrow keys, as a player would. */
static BOOL point_at(HWND hwnd, LPARAM lp)
{
    int x = (short)LOWORD(lp), y = (short)HIWORD(lp), item, steps;
    const int *menu = MENU;
    BOOL across;

    if (!menu || !hogsdraw_game_point(&x, &y) || (item = item_at(menu, x, y)) < 0)
        return FALSE;
    across = menu[9] == 4 || menu[9] == 12; /* FUN_0042AB40: only Left and Right move there */
    for (steps = abs(item - menu[3]); steps > 0 && MENU == menu && menu[3] != item; steps--)
        press(hwnd, item > menu[3] ? (across ? VK_RIGHT : VK_DOWN) : (across ? VK_LEFT : VK_UP));
    return MENU == menu && menu[3] == item;
}

/* TRUE: the message is done; MainWndProc must not get it. */
BOOL menu_mouse(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    static BOOL shown;
    static LPARAM last = -1;
    static int wheel;
    const int *menu = MENU;
    int first, x = (short)LOWORD(lp), y = (short)HIWORD(lp);

    switch (msg) {
    case WM_SETCURSOR:
        if (LOWORD(lp) != HTCLIENT)
            return FALSE;
        SetCursor(FRONTEND ? LoadCursorA(NULL, (LPCSTR)IDC_ARROW) : NULL);
        return TRUE;
    case WM_MOUSEMOVE: /* MainWndProc lowers the cursor's show count on every move: it never gets one */
        if (!shown) {
            shown = TRUE;
            while (ShowCursor(TRUE) < 0)
                ;
        }
        /* Windows repeats the message for a mouse that lies still: that one must not undo the keys */
        if (FRONTEND && !KEY_WANTED && lp != last)
            point_at(hwnd, lp);
        last = lp;
        return TRUE;
    }
    if (!FRONTEND || !menu || (msg != WM_LBUTTONDOWN && msg != WM_RBUTTONDOWN && msg != WM_MOUSEWHEEL))
        return FALSE;
    if (KEY_WANTED)
        return TRUE;
    switch (msg) {
    case WM_LBUTTONDOWN:
        /* on a screen of lines only a line can be clicked; elsewhere a click in the picture is Enter */
        if (point_at(hwnd, lp) || (!lines(menu, &first) && hogsdraw_game_point(&x, &y)))
            press(hwnd, VK_RETURN);
        break;
    case WM_RBUTTONDOWN:
        press(hwnd, VK_BACK);
        break;
    case WM_MOUSEWHEEL:
        for (wheel += GET_WHEEL_DELTA_WPARAM(wp); wheel >= WHEEL_DELTA; wheel -= WHEEL_DELTA)
            press(hwnd, VK_UP);
        for (; wheel <= -WHEEL_DELTA; wheel += WHEEL_DELTA)
            press(hwnd, VK_DOWN);
        break;
    }
    return TRUE;
}
