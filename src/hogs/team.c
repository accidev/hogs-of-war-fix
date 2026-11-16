/* team.c - the team screen (screen type 5: menu 12 of a campaign, menu 34 of a multi-player game) in our
 * code: its picture, the walk of its cursor, and where its items are for the mouse of menu.c.
 *
 * Items: first the buttons ([0x4C0D44] of them: 2 in a campaign, 4 in a multi-player game), then the pigs,
 * 5 a column. A team is 0x2A8 bytes: +0x50 promotion points, +0x70 + 0x40 n pig n: [0] in the team, [1] class.
 * The menu pictures are surfaces put into a list of blits, FUN_0041AFA0 and others, in 640 x 480. Replaced:
 *   0x41E73C  switch of FUN_0041BEF0(screen type), type 5: the picture of the screen  -> team_art
 *   0x42ACE0  jmp, FUN_0042ACE0(menu, dx, dy): an arrow key on the screen             -> team_move
 *   0x427B7A  call of FUN_00428CF0(menu, item, text, chosen): the text of an item     -> item_text
 *   0x4289E7  call of FUN_00431320 in FUN_004285A0: the class under a pig's name      -> class_name
 */
#include "hogs.h"
#include <math.h>
#include <stdio.h>

#define MENUS (*(const int **)0x51AAE0)      /* the menu records, 0x48 bytes each */
#define ROOT (*(const int **)0x51AADC)       /* the menu of the screen; MENU may be a menu opened over it */
#define TEAM (*(const BYTE **)0x51C560)      /* the team on the screen */
#define TEAMS (*(const BYTE *)0x51C568)      /* teams in the game: 1 in a campaign */
#define BUTTONS (*(const int *)0x4C0D44)
#define PART ((const int *)0x512C18)         /* how far each part of the screen has come in; [0] 6: all is in */
#define SHOWN_PIG (*(const int *)0x4C0C58)   /* the pig whose model is shown, -1: none; its portrait turns */
#define TURN (*(int *)0x512E54)              /* angle of that portrait, 0x1000 a turn */
#define SURFACE(va) (*(void *const *)(va))
#define PORTRAITS ((void *const *)0x5128E4)
#define CLASS_PICTURES ((void *const *)0x5119B0) /* by the first byte of a class's three in the tables below */
#define RANK_PICTURES ((void *const *)0x5119CC)  /* by the second byte */
#define CLASS_LOOK ((const BYTE *)0x4D29C0)      /* campaign: {picture, rank, ?} of each class */
#define CLASS_LOOK_NET ((const BYTE *)0x4D29F8)
#define PROMOTIONS ((BYTE *)0x4D29A8)            /* how many classes a class can become */
#define FIRST_PROMOTION ((BYTE *)0x51AAC0)       /* the first of them in the list below; FUN_00482440 fills it */
#define PROMOTION_COST ((const BYTE *)0x4D2981)  /* {class, cost} pairs: the cost bytes */
#define SESSION (*(const int *)0x51ABC4)         /* not 0: a network game; its class names are fetext 482 + class */
#define FIRST_TEAM (*(const BYTE **)0x51C55C)
#define NETWORK (*(const int *)0x51AC48)
#define BOXES ((const int (*)[4])0x4C1728)  /* {x, y, w, h} of every item's text, in the field of 1024 x 820 */
#define FIRST_BOX ((const int *)0x51A890)   /* by screen type */
#define TEXT_FADE (*(const int *)0x512EA0)  /* 0: the text of the screen is shown */
enum { TEAM_SCREEN = 5, NET_TEAM = 34, FIELD_W = 1024, FIELD_H = 820 };

/* The game's text object: [9] how far the text is faded, [11]-[13] colour, [14] alpha */
static void(__fastcall *const text_window)(int *text, void *, int x, int y, int w, int h) = (void(__fastcall *)(int *, void *, int, int, int, int))0x430E00;
/* both return the text object */
static int *(__fastcall *const text_string)(int *text, void *, int string, int x, int y) = (int *(__fastcall *)(int *, void *, int, int, int))0x431320;
/* chars as the game keeps a name */
static int *(__fastcall *const text_chars)(int *text, void *, const BYTE *chars, int x, int y) = (int *(__fastcall *)(int *, void *, const BYTE *, int, int))0x431350;
static void(__fastcall *const text_picture)(int *text, void *, int which) = (void(__fastcall *)(int *, void *, int))0x431C30;
static void(__fastcall *const text_lines)(int *text) = (void(__fastcall *)(int *))0x431BA0; /* FUN_00431B80(text, 2), before a menu line */

static void(__cdecl *const blit)(int x, int y, void *surface, const int *part) = (void(__cdecl *)(int, int, void *, const int *))0x41AFA0;
/* w or h below 0: mirrored */
static void(__cdecl *const blit_sized)(int x, int y, void *surface, const int *part, int w, int h) =
    (void(__cdecl *)(int, int, void *, const int *, int, int))0x41AF70;
static void(__cdecl *const blit_whole)(int x, int y, void *surface) = (void(__cdecl *)(int, int, void *))0x41AFC0;
static int *(__cdecl *const whole)(int *part, void *surface) = (int *(__cdecl *)(int *, void *))0x41AE60; /* {0, 0, w, h} */
static void(__fastcall *const cursor_moved)(int *menu, void *, int was, int opened) = (void(__fastcall *)(int *, void *, int, int))0x427C90;
/* FUN_0042B6D0: Left or Right on an item that holds a value; on a pig of menu 34 its next class */
static void(__fastcall *const turn_value)(int *menu, void *, int by) = (void(__fastcall *)(int *, void *, int))0x42B6D0;

static int sx(int field_x) { return field_x * 640 / FIELD_W; } /* FUN_0041ADB0 */
static int sy(int field_y) { return field_y * 480 / FIELD_H; } /* FUN_0041ADD0 */

static BOOL net_team(void)
{
    return (ROOT - MENUS) / 18 == NET_TEAM;
}

/* +0x28E is 100: the pigs are those of the team record before this one */
static const BYTE *shown_team(void)
{
    return TEAM[0x28E] == 100 ? TEAM - 0x2A8 : TEAM;
}

/* Classes 15 Ace and 16 Legend: the game shut them with counts of 0. Open once a team has finished a
 * campaign, +0x54 not 0 (FUN_00450970): a saved game savearmy0-7 (a team record), or the team on screen */
static BOOL g_late;     /* the list has our costs of the two */
static BOOL g_finished; /* such a team was seen */

static void saved_teams(void)
{
    char name[] = "savearmy0";
    BYTE team[0x55];
    FILE *file;

    for (; name[8] <= '7'; name[8]++)
        if ((file = fopen(name, "rb")) != NULL) {
            g_finished |= fread(team, 1, sizeof team, file) == sizeof team && team[0x54];
            fclose(file);
        }
}

static void late_promotions(void)
{
    if (!g_late)
        return;
    g_finished |= TEAM[0x54] != 0;
    PROMOTIONS[14] = PROMOTIONS[15] = TEAMS == 1 && g_finished;
    FIRST_PROMOTION[14] = 17;
    FIRST_PROMOTION[15] = 18;
}

/* gtext (battle text) 78 ACE, 79 LEGEND, chars as the game keeps them (code - 0x1F); .ofs 16-bit offsets
 * into .bin. The menus load only fetext */
static BYTE g_late_name[2][24] = { { 'A' - 0x1F, 'C' - 0x1F, 'E' - 0x1F },
                                   { 'L' - 0x1F, 'E' - 0x1F, 'G' - 0x1F, 'E' - 0x1F, 'N' - 0x1F, 'D' - 0x1F } };

static void late_names(void)
{
    FILE *ofs = fopen("Language/Text/gtext.ofs", "rb"), *bin = fopen("Language/Text/gtext.bin", "rb");
    WORD at[2];
    int i;

    if (ofs && bin && !fseek(ofs, 78 * 2, SEEK_SET) && fread(at, 2, 2, ofs) == 2)
        for (i = 0; i < 2; i++) {
            BYTE name[24] = { 0 };

            if (!fseek(bin, at[i], SEEK_SET) && fread(name, 1, sizeof name - 1, bin) && name[0])
                memcpy(g_late_name[i], name, sizeof name);
        }
    if (ofs)
        fclose(ofs);
    if (bin)
        fclose(bin);
}

/* In place of the call of FUN_00431320(text, string, x, y) for a pig's class on the board. A campaign takes
 * fetext 467 + class; 482, 483 are names of a multi-player game: Ace and Legend have none there */
static int *__fastcall class_name(int *text, void *unused, int string, int x, int y)
{
    (void)unused;
    if (TEAMS == 1 && !SESSION && (string == 482 || string == 483))
        return text_chars(text, NULL, g_late_name[string - 482], x, y);
    return text_string(text, NULL, string, x, y);
}

/* In place of the type 5 part of FUN_0041BEF0. Columns of pigs stand 462 apart, rows 74; a multi-player
 * team has one column of 5, a campaign team 5 and 3. */
static void __cdecl team_art(void)
{
    static const int kDrop[7] = { 0, 0, 0, 0, 0, 0, -32 };
    static const int kCut[7] = { 0xB3, 0xA5, 0xA9, 0xB0, 0xB3, 0xB3, 0xB3 };
    static const int kFoot[7] = { 0x50, 0x50, 0x1F, 0x21, 0x26, 0x20, 0x50 };
    static const int kGap[7] = { 0x50, 0x50, 0x1E, 0x2D, 0x30, 0x30, 0x50 };
    static const int kButtonTop[5] = { 0xBB, 0xD7, 0xF3, 0x144, 0 }, kButtonPart[5] = { 7, 8, 9, 10, 11 };
    BOOL net = net_team();
    int board = sx(*(const int *)0x512CD0), x0 = board - 10, part[4], col, row, i, top;
    const BYTE *team = shown_team();
    void *frame = SURFACE(0x512910), *tab = SURFACE(0x5119E8), *rail = SURFACE(0x512880), *plate = SURFACE(0x511DF4);

    late_promotions();
    for (col = 0; PART[0] == 6 && col < (net ? 1 : 2); col++)
        for (row = 0; row < (col ? 3 : 5); row++) {
            int n = col * 5 + row, turn = 0x1000, w, x;
            int y = ((team[0x70 + n * 0x40] ? row < 4 : -1000) + row * 37) * 2 + (row < 3);

            if (n == SHOWN_PIG) {
                turn = (int)(cos(TURN * *(const double *)0x4BC430 * *(const double *)0x4BC438) * *(const double *)0x4BC428);
                TURN = (TURN + 100) % 0x1000;
            }
            whole(part, frame);
            w = turn * part[2] / 0x1000;
            x = (part[2] - w) / 2 + x0 + col * 462 + 0x43;
            blit_sized(x, y + 0x48, PORTRAITS[n], part, w, part[3]);
            blit_sized(x, y + 0x4B, frame, part, w, part[3]);
            blit_sized(x, y + 74, SURFACE(0x51290C), part, w, part[3]);
        }
    for (col = 0; PART[21] && PART[0] == 6 && col < (net ? 1 : 2); col++)
        for (row = 0; row < (col ? 3 : 5); row++) {
            const BYTE *pig = team + 0x70 + (col * 5 + row) * 0x40;
            const BYTE *look = (net ? CLASS_LOOK_NET : CLASS_LOOK) + pig[1] * 3;
            int line = (pig[0] ? row < 2 : -1000) + row * 37, y = (row == 4) + line * 2 + 0x74;
            int x = x0 + col * 417, off = col ? -69 : 82;
            short points = *(const short *)(team + 0x50);

            if (look[1]) {
                blit_whole(off + x + 0x6D, y, RANK_PICTURES[CLASS_LOOK[pig[1] * 3 + 1]]);
                off -= 10;
                whole(part, tab);
                part[0] += 0x16;
                blit_sized(off + x + 0x7F, y, tab, part, part[2] - part[0], part[3]);
            }
            if (pig[1] || net)
                blit_whole(off + x + 0x59, y, CLASS_PICTURES[look[0]]);
            blit_whole(off + x + 0x55, y, tab);
            /* campaign: the mark of a pig that the team's points can promote */
            if (TEAMS == 1 && PART[22] && PART[21] == 12 && points > 0) {
                const BYTE *cost = PROMOTION_COST + FIRST_PROMOTION[pig[1]] * 2;
                BOOL can = FALSE;

                for (i = PROMOTIONS[pig[1]]; i > 0; i--, cost += 2)
                    if (*cost <= points)
                        can = TRUE;
                off = (look[1] ? 10 : 0) + PART[22] - 0x26;
                y = (row == 4) + (line + (PART[22] - 23) / 6) * 2 + 0x7A;
                if (can && !col)
                    blit_whole(off + x0 + 0xE4, y, SURFACE(0x5119EC));
                else if (can && !net)
                    blit_whole(x0 - off + 417 - 0x10, y, SURFACE(0x511FB8));
            }
        }
    /* the rail at the left; a campaign has it mirrored at the right, in two pieces */
    top = kDrop[PART[0]] + 0x14;
    whole(part, rail);
    part[2] -= 2;
    blit(board - 0x20, top, rail, part);
    if (!net) {
        part[3] -= kCut[PART[0]];
        blit_sized(0x27E - (board - 0x22), top, rail, part, -part[2], part[3]);
        top += part[3];
        part[3] = kFoot[PART[0]] + 0x14;
        part[2] -= 0xB2;
        blit_sized(0x27E - (board - 0x22), kGap[PART[0]] + top, rail, part, -part[2], -part[3]);
    }
    blit_whole(0x78, (sy(*(const int *)0x512954) - 7) * 2, SURFACE(0x512894));
    for (i = BUTTONS - 1; i >= 0; i--) {
        int y = kButtonTop[i] - (BUTTONS > 2 ? 0x78 : 0) + sy(*(const int *)0x51295C) + 0x1B;
        void *lamp = ((void *const *)0x5128A4)[PART[kButtonPart[i]]];

        blit_whole(0x1AD, y * 2 - 0x4B, lamp);
        blit_whole(0x259, y * 2 - 0x4B, lamp);
        blit_whole(0x1AC, y * 2 - 0x5B, plate);
        whole(part, plate);
        part[1] = part[3] - 0x14;
        blit_sized(0x1AC, (part[3] / 2 + y) * 2 - 0x5B, plate, part, part[2], 0x78);
    }
    blit_whole(0xE8, (sy(*(const int *)0x512968) + 0x9C) * 2 - 8, SURFACE(0x5128E0));
    ((void(__cdecl *)(void))0x4267A0)(); /* the frames of the portraits that turn */
}

/* In place of FUN_0042A560(pig, text, chosen): the name on a pig's plate, a mark where the team has no pig. */
static void pig_text(int n, int *text, int chosen)
{
    const BYTE *pig = shown_team() + 0x70 + n * 0x40;

    text_window(text, NULL, n / 5 * 424 + 102, n % 5 * 125 + 168, 400, 30);
    text[11] = text[12] = chosen ? 0x78 : 0x50;
    text[13] = chosen ? 0x4B : 0x2D;
    text[14] = 0xFF;
    text[9] = chosen == 2 && !TEXT_FADE ? *(const int *)0x512EAC : TEXT_FADE;
    if (!pig[0]) {
        text_window(text, NULL, n / 5 * 733 + 130, n % 5 * 125 + 169, 120, 30);
        text_picture(text, NULL, 2);
        text_window(text, NULL, -1000, -1000, 100, 100);
    }
    if (PART[0] == 6) {
        text_lines(text);
        text_chars(text, NULL, pig + 0x30, -1, -1);
    }
}

/* In place of the call of FUN_00428CF0(menu, item, text, chosen) in FUN_00427770: the text of one item, ours
 * on the team screen. A button is the string menu[2] + item; one that cannot be chosen is three times darker. */
static void __fastcall item_text(int *menu, void *unused, int item, int *text, int chosen)
{
    const int *box = BOXES[FIRST_BOX[TEAM_SCREEN] + item];
    int wide = *(const int *)0x4C0C5C * FIELD_W / 640, shade;

    (void)unused;
    if (menu[9] != TEAM_SCREEN) {
        ((void(__fastcall *)(int *, void *, int, int *, int))0x428CF0)(menu, NULL, item, text, chosen);
        return;
    }
    text_window(text, NULL, *(const int *)0x51AC58 + box[0], *(const int *)0x51AC5C + box[1], box[2] + wide, box[3]);
    text_lines(text);
    if (item >= BUTTONS) {
        pig_text(item - BUTTONS, text, chosen);
        return;
    }
    if (TEXT_FADE)
        return;
    shade = ((const int *)(ULONG_PTR)menu[6])[item] == 0x16 ? 3 : 1;
    text[11] = text[12] = (chosen ? 0x78 : 0x50) / shade;
    text[13] = (chosen ? 0x4B : 0x2D) / shade;
    text[14] = 0xFF;
    /* the last player of a multi-player game: the string after the buttons in place of Next Player */
    if ((menu - MENUS) / 18 == NET_TEAM && item == BUTTONS - 1 && (TEAMS <= (TEAM - FIRST_TEAM) / 0x2A8 + 1 || NETWORK)) {
        text_string(text, NULL, menu[2] + BUTTONS, -1, -1);
        return;
    }
    if (BUTTONS < 3) /* a campaign: the two buttons stand 410 under their boxes */
        text_window(text, NULL, box[0], box[1] + 410, box[2] + wide, box[3]);
    text_string(text, NULL, menu[2] + item, -1, -1);
}

/* In place of FUN_0042ACE0(menu, dx, dy). Campaign: the buttons end the right column. Multi-player: their own
 * column, Left and Right on a pig change its class; the game let the cursor there only by Change Ranks, we by Left too */
static void __fastcall team_move(int *menu, void *unused, int dx, int dy)
{
    static const BYTE kPigBeside[4] = { 0, 1, 2, 4 };
    int was = menu[3], buttons = BUTTONS, low = buttons > 2 ? 0 : 3, row = was - buttons, column = 0;

    (void)unused;
    if (dx && dy)
        dx = 0;
    if (buttons > 2) {
        if (dx && MENU[3] >= buttons)
            turn_value(menu, NULL, dx);
        else if (dx < 0 && was < buttons && was < 4) {
            menu[3] = buttons + kPigBeside[was];
            cursor_moved(menu, NULL, was, 0);
            return;
        }
        dx = 0;
    }
    if (was < buttons) {
        column = 1;
        row = low + was;
    } else if (row >= 5) {
        column = 1;
        row -= 5;
    }
    if (dx)
        column = column + dx < 0 ? 0 : column + dx > 1 ? 1 : column + dx;
    else
        row = row + dy < 0 ? 0 : row + dy > 4 ? 4 : row + dy;
    if (!column)
        menu[3] = row + buttons;
    else if (!low)
        menu[3] = row > 3 ? 3 : row; /* four buttons */
    else
        menu[3] = row < low ? row + 5 + buttons : row - low; /* three pigs, then two buttons */
    cursor_moved(menu, NULL, was, 0);
}

/* The item at a point of the 640 x 480 picture, or -1; *turn: -1 or 1 on the arrows of a pig's class (multi-player).
 * Field: pig n's name at {102 + 424 column, 168 + 125 row} (FUN_0042A560); two buttons stand 410 under their boxes */
int team_item_at(const int *menu, int x, int y, int *turn)
{
    const int (*box)[4] = BOXES + FIRST_BOX[TEAM_SCREEN];
    int buttons = BUTTONS, moved = *(const int *)0x51AAE4, i;
    int fx = x * FIELD_W / 640, fy = y * FIELD_H / 480;

    *turn = 0;
    for (i = buttons; i < menu[0]; i++) {
        int left = (i - buttons) / 5 ? 600 : 60, row = (i - buttons) % 5, top = 123 + row * 125;

        if (fx >= left && fx < left + 370 && fy >= top && fy < top + 125) {
            if (buttons > 2 && y >= 118 + row * 74 && x >= 140 && x < 232) /* team_art: the class under the name */
                *turn = x < 186 ? -1 : 1;
            return i;
        }
    }
    for (i = 0; i < buttons && i < menu[0]; i++) {
        int left = box[i][0] + (buttons < 3 || !moved ? 0 : menu[14]);
        int top = box[i][1] + (buttons < 3 ? 410 : moved ? menu[15] : 0);

        if (fx >= left && fx < left + box[i][2] + 80 && fy >= top - 25 && fy < top + box[i][3] + 25)
            return i;
    }
    return -1;
}

/* The mouse changes the class of the pig under the cursor: an arrow clicked, or the wheel. */
BOOL team_turn(int *menu, int by)
{
    if (menu != MENU || BUTTONS < 3 || menu[3] < BUTTONS)
        return FALSE;
    turn_value(menu, NULL, by);
    return TRUE;
}

void team_init(void)
{
    static BYTE *go; /* call team_art, then the end of FUN_0041BEF0 */
    DWORD rel;

    go = VirtualAlloc(NULL, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!go)
        return;
    go[0] = 0xE8;
    rel = (DWORD)(ULONG_PTR)team_art - (DWORD)(ULONG_PTR)(go + 5);
    memcpy(go + 1, &rel, 4);
    go[5] = 0xE9;
    rel = 0x41DB92 - (DWORD)(ULONG_PTR)(go + 10);
    memcpy(go + 6, &rel, 4);
    patch(0x41E73C, "\x85\xD2\x41\x00", &go, 4, "team screen: drawn by our code");
    hook(0x42ACE0, 0xE9, "\x53\x55\x56\x57\x8B", team_move, "team screen: the cursor goes from the buttons to the pigs");
    hook(0x427B7A, 0xE8, "\xE8\x71\x11\x00\x00", item_text, "team screen: its names and buttons by our code");
    /* list entries 17 {Ace, cost} and 18 {Legend, cost} */
    g_late = patch(0x4D29A2, "\x0F\x00\x10\x00", "\x0F\x08\x10\x08", 4, "promotions: Hero to Ace to Legend, 8 points each");
    saved_teams();
    patch(0x4D29ED, "\x05\x00\x01\x05\x00\x02", "\x05\x01\x01\x05\x02\x02", 6, "team screen: rank stripes of Ace and Legend");
    if (hook(0x4289E7, 0xE8, "\xE8\x34\x89\x00\x00", class_name, "team screen: the names of Ace and Legend"))
        late_names();
}
