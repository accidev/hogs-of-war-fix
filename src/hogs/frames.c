/* frames.c - when the game draws its frames: battles and menus.
 *
 * Battle: the main loop (0x47DE90) calls the step FUN_0047F930: a game tick every 40 ms, one frame
 * after it. Frames between ticks draw objects and camera between their last two tick places.
 * Menus: the loop (0x427380) draws as fast as it can; its animations are made for 25 frames a
 * second. Hooked:
 *   0x47E42E  call FUN_0047F930 (battle step, main loop)      -> battle_step
 *   0x47FC49  call FUN_0044E290 (draw, after the game ticks)  -> tick_frame
 *   0x47FCC0  jmp  FUN_0044E290 (draw, inside a game tick)    -> mid_tick_frame
 *   0x42745F  call clock() in the menu loop                   -> menu_frame_clock
 *
 * The battle loop and the menu pacing are ported from Torisutan/HogsFix (MIT license),
 * src/battle.c and src/hogsfix.c.
 */
#include "hogs.h"
#include <math.h>
#include <mmsystem.h>
#include <string.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

#define TICK_TIME (*(DWORD *)0x520884)      /* timeGetTime of the last game tick */
#define TURN_MANAGER (*(BYTE **)0x537F24)   /* one per battle; +0x314: tick length in ms */
#define RENDERER (*(BYTE **)0x51C4F8)       /* `this` of the draw function */
#define CAMERA (*(BYTE **)0x537F98)         /* +0x88: 3 angles, 4096 = a full turn */
#define DRAW_LIST (*(BYTE **)0x537FBC)      /* objects, linked by +4 */
#define HUD (*(BYTE **)0x51D38C)            /* new at 0x485E66 */
#define MESSAGES (*(BYTE **)0x51D3C4)       /* message banner, new at 0x485DF9 */
#define MESSAGE_SCROLL (*(DWORD *)0x51D3C0)
#define NESTED_STEP (*(BYTE *)0x51BCDC)     /* set while 0x448ED0 calls the step from inside a game tick */
#define NETWORK_BATTLE (*(BYTE *)0x51ABC8)
#define LOCAL_CLOCK (*(BYTE *)0x4D1900)     /* 0: a tick takes its clock and pad from the queue of the player on turn */
#define HUD_SIZE 0xE18
#define MESSAGES_SIZE 0x96C

static DWORD(__cdecl *const game_step)(void) = (DWORD(__cdecl *)(void))0x47F930;
static void(__fastcall *const game_draw)(BYTE *) = (void(__fastcall *)(BYTE *))0x44E290;
static int(__cdecl *const game_clock)(void) = (int(__cdecl *)(void))0x4AE530; /* clock(), ms */

/* ---- time ---- */

static LARGE_INTEGER g_qpc_start, g_qpc_freq;
static DWORD g_ms_start;
static LONGLONG g_ms_lead;
static HANDLE g_timer;

static LONGLONG now_us(void)
{
    LARGE_INTEGER c;

    QueryPerformanceCounter(&c);
    c.QuadPart -= g_qpc_start.QuadPart; /* in two parts: counts x 1000000 overflows within an hour at 3 GHz */
    return c.QuadPart / g_qpc_freq.QuadPart * 1000000 + c.QuadPart % g_qpc_freq.QuadPart * 1000000 / g_qpc_freq.QuadPart;
}

/* timeGetTime on the now_us clock (1 ms steps are too coarse at 144 Hz); g_ms_lead: smallest
 * offset seen between the two, +1 us per call to follow drift */
static LONGLONG tick_time_us(DWORD ms)
{
    LONGLONG lead = now_us() - (LONGLONG)(int)(timeGetTime() - g_ms_start) * 1000;

    g_ms_lead = lead < g_ms_lead ? lead : g_ms_lead + 1;
    return (LONGLONG)(int)(ms - g_ms_start) * 1000 + g_ms_lead;
}

/* Sleep until `due`; with `messages` a window message ends the wait. */
static void wait_until(LONGLONG due, BOOL messages)
{
    LARGE_INTEGER t;

    t.QuadPart = (now_us() - due) * 10; /* negative: relative, in 100 ns */
    if (t.QuadPart >= 0)
        return;
    SetWaitableTimer(g_timer, &t, 0, NULL, NULL, FALSE);
    if (messages)
        MsgWaitForMultipleObjectsEx(1, &g_timer, 100, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    else
        WaitForSingleObject(g_timer, 100);
}

/* ---- water ---- */

float *g_water_y; /* y of the first of 31x31 inner grid points; +0x68 next point, +0xD68 next row */

/* Replaces the loop in _d3d.dll FUN_1000B440 (4 units along y a frame): 100 units a second.
 * Rows are 1024 apart: after one row the points repeat. */
void __cdecl water_slide(void)
{
    static LONGLONG last;
    LONGLONG now = now_us();
    /* after a break (first frame, loading screen): the original step */
    float step = now - last > 200000 ? 4.0f : (float)(now - last) * 1e-4f, was, phase;
    int row, col;

    last = now;
    if (!g_water_y)
        return;
    was = *g_water_y + 15356.0f; /* the first inner point is built at y = 1024 - 16380 */
    was -= 1024.0f * (float)floor(was / 1024.0f);
    phase = was + step;
    if (phase >= 1024.0f)
        phase -= 1024.0f;
    for (row = 0; row < 31; row++)
        for (col = 0; col < 31; col++) {
            float *y = (float *)((BYTE *)g_water_y + row * 0xD68 + col * 0x68);

            /* from the point's grid place, so rounding does not add up */
            *y = 1024.0f * (float)floor((*y - was + 16380.0f) / 1024.0f + 0.5f) - 16380.0f + phase;
        }
}

/* ---- [Debug] FrameStats=1: one line every 5 s ---- */

static BOOL g_stats;

typedef struct {
    LONGLONG since;
    DWORD tick_time;
    int frames;
} Stats;

static void count_frame(Stats *s, const char *what, int tick_ms)
{
    LONGLONG now;
    double sec;

    if (!g_stats)
        return;
    now = now_us();
    sec = (double)(now - s->since) / 1e6;
    if (sec >= 5 && sec < 6 && tick_ms)
        say("stats: %s %.2f ticks/s, %.1f frames/s", what, (int)(TICK_TIME - s->tick_time) / (tick_ms * sec),
            s->frames / sec);
    else if (sec >= 5 && sec < 6)
        say("stats: %s %.1f frames/s", what, s->frames / sec);
    if (sec >= 5) {
        s->since = now;
        s->tick_time = TICK_TIME;
        s->frames = 0;
    }
    s->frames++;
}

/* ---- menus ---- */

static int __cdecl menu_frame_clock(void)
{
    static LONGLONG next;
    static Stats stats;
    LONGLONG now = now_us();

    if (now - next > 40000)
        next = now;
    wait_until(next, FALSE);
    next += 40000;
    count_frame(&stats, "menu", 0);
    return game_clock();
}

/* ---- battle: objects between two ticks ---- */

/* Draw-list object: +0x38 type, [+0x18] transform (pos 3 floats at +4, 3x3 matrix at +0x20).
 * Pig (type 0x1357): drawn from its heading at +0x174, 4096 = a full turn. Camera: type 0x135C. */
#define MAX_OBJECTS 512
#define PIG 0x1357

typedef struct {
    BYTE *obj;
    float *tr;
    int type;
    float pos[2][3], mat[2][9]; /* [0] the tick before, [1] the last tick */
    int heading[2];
    BOOL turns; /* small heading change: interpolate it */
    float true_pos[3], true_mat[9];
    int true_heading;
    int moved; /* by place_objects: 0 no, 1 transform, 2 transform and heading */
    BOOL seen;
} Object;

static Object g_objects[MAX_OBJECTS];
static int g_object_count;
static int g_camera[2][3], g_true_camera[3];
static int g_camera_turns; /* 0 no, 1 yes, 2 moved by place_objects */

static int angle_diff(int a, int b)
{
    return ((b - a + 2048) & 4095) - 2048;
}

static int angle_between(int a, int b, float t)
{
    float d = (float)angle_diff(a, b) * t;
    int r = a + (int)(d < 0 ? d - 0.5f : d + 0.5f);

    return (a >= 0 && a < 4096 && b >= 0 && b < 4096) ? (r & 4095) : r;
}

static void read_object(Object *o)
{
    memcpy(o->pos[1], o->tr + 1, sizeof o->pos[1]);
    memcpy(o->mat[1], o->tr + 8, sizeof o->mat[1]);
    o->heading[1] = o->type == PIG ? *(int *)(o->obj + 0x174) : 0;
}

/* Once per game tick, after the camera update. */
static void snapshot(void)
{
    BYTE *obj, *cam = CAMERA;
    Object *o;
    int i, k = 0, n;

    for (i = 0; i < g_object_count; i++)
        g_objects[i].seen = FALSE;
    for (obj = DRAW_LIST, n = 0; obj && n < 4096; obj = *(BYTE **)(obj + 4), n++) {
        float *tr = *(float **)(obj + 0x18);
        int type = *(int *)(obj + 0x38);

        if (!tr || type < 0x1357 || type > 0x1362) /* the object types the draw function knows */
            continue;
        o = k < g_object_count && g_objects[k].obj == obj ? &g_objects[k] : NULL;
        for (i = 0; !o && i < g_object_count; i++)
            if (g_objects[i].obj == obj)
                o = &g_objects[i];
        if (o && o->tr == tr && o->type == type) {
            float d2 = 0;
            int turn;

            memcpy(o->pos[0], o->pos[1], sizeof o->pos[0]);
            memcpy(o->mat[0], o->mat[1], sizeof o->mat[0]);
            o->heading[0] = o->heading[1];
            read_object(o);
            for (i = 0; i < 3; i++)
                d2 += (o->pos[1][i] - o->pos[0][i]) * (o->pos[1][i] - o->pos[0][i]);
            if (d2 > 3000.f * 3000.f) /* placed somewhere else, a camera cut: not a move */
                memcpy(o->pos[0], o->pos[1], sizeof o->pos[0]);
            for (i = 0; i < 9; i++)
                if (o->mat[1][i] - o->mat[0][i] > 0.5f || o->mat[1][i] - o->mat[0][i] < -0.5f)
                    break;
            if (i < 9) /* big turn: lerping the matrices would squash the model */
                memcpy(o->mat[0], o->mat[1], sizeof o->mat[0]);
            turn = angle_diff(o->heading[0], o->heading[1]);
            o->turns = type == PIG && turn != 0 && turn > -600 && turn < 600;
        } else {
            if (!o) {
                if (g_object_count >= MAX_OBJECTS)
                    continue;
                o = &g_objects[g_object_count++];
            }
            o->obj = obj;
            o->tr = tr;
            o->type = type;
            read_object(o);
            memcpy(o->pos[0], o->pos[1], sizeof o->pos[0]);
            memcpy(o->mat[0], o->mat[1], sizeof o->mat[0]);
            o->heading[0] = o->heading[1];
            o->turns = FALSE;
        }
        o->seen = TRUE;
        o->moved = 0;
        k = (int)(o - g_objects) + 1;
    }
    for (i = 0, k = 0; i < g_object_count; i++)
        if (g_objects[i].seen) {
            if (k != i)
                g_objects[k] = g_objects[i];
            k++;
        }
    g_object_count = k;

    g_camera_turns = 0;
    if (cam) {
        g_camera_turns = 1;
        for (i = 0; i < 3; i++) {
            int d;

            g_camera[0][i] = g_camera[1][i];
            g_camera[1][i] = *(int *)(cam + 0x88 + 4 * i);
            d = angle_diff(g_camera[0][i], g_camera[1][i]);
            if (d > 700 || d < -700)
                g_camera_turns = 0;
        }
    }
}

/* t: 0..1 from the tick before to the last tick; skips objects the game moved since */
static void place_objects(float t)
{
    BYTE *cam = CAMERA;
    Object *o;
    int i;

    for (o = g_objects; o < g_objects + g_object_count; o++) {
        float *pos = o->tr + 1, *mat = o->tr + 8;

        o->moved = 0;
        if (memcmp(pos, o->pos[1], sizeof o->pos[1]) || memcmp(mat, o->mat[1], sizeof o->mat[1]))
            continue;
        o->moved = 1;
        memcpy(o->true_pos, pos, sizeof o->true_pos);
        memcpy(o->true_mat, mat, sizeof o->true_mat);
        for (i = 0; i < 3; i++)
            pos[i] = o->pos[0][i] + (o->pos[1][i] - o->pos[0][i]) * t;
        for (i = 0; i < 9; i++)
            mat[i] = o->mat[0][i] + (o->mat[1][i] - o->mat[0][i]) * t;
        if (o->turns && *(int *)(o->obj + 0x174) == o->heading[1]) {
            o->true_heading = o->heading[1];
            *(int *)(o->obj + 0x174) = angle_between(o->heading[0], o->heading[1], t);
            o->moved = 2;
        }
    }
    if (cam && g_camera_turns && !memcmp(cam + 0x88, g_camera[1], sizeof g_camera[1])) {
        memcpy(g_true_camera, cam + 0x88, sizeof g_true_camera);
        for (i = 0; i < 3; i++)
            *(int *)(cam + 0x88 + 4 * i) = angle_between(g_camera[0][i], g_camera[1][i], t);
        g_camera_turns = 2;
    }
}

static void restore_objects(void)
{
    BYTE *cam = CAMERA;
    Object *o;

    for (o = g_objects; o < g_objects + g_object_count; o++) {
        if (!o->moved)
            continue;
        memcpy(o->tr + 1, o->true_pos, sizeof o->true_pos);
        memcpy(o->tr + 8, o->true_mat, sizeof o->true_mat);
        if (o->moved == 2)
            *(int *)(o->obj + 0x174) = o->true_heading;
        o->moved = 0;
    }
    if (cam && g_camera_turns == 2) {
        memcpy(cam + 0x88, g_true_camera, sizeof g_true_camera);
        g_camera_turns = 1;
    }
}

/* ---- battle: frames ---- */

static int g_rate;         /* [Battle] FrameRate: 0 = the monitor's refresh rate, 25 = only after a tick */
static BOOL g_interpolate; /* [Battle] Interpolation */
static BOOL g_vsync;       /* [Render] VSync */
static LONGLONG g_frame_us;   /* time between two frames; 0 = one frame per game tick */
static LONGLONG g_next_frame;
static BYTE *g_battle;        /* the turn manager the state below belongs to */
static DWORD g_snapshot_tick;
static BOOL g_hud_waits;      /* last tick's frame not drawn: HUD step pending */
static DWORD g_step_tick;
static int g_step_late;       /* ms after the tick was due when battle_step called the step */
static DWORD g_step_called;   /* timeGetTime of the last battle_step or step of this battle, 0: none yet */
static BYTE *g_turn_player;   /* network battle: the player on turn when the last turn line was written */
static int g_turn;
static Stats g_battle_stats;

static int monitor_refresh_rate(void)
{
    MONITORINFOEXA mi;
    DEVMODEA dm;

    mi.cbSize = sizeof mi;
    dm.dmSize = sizeof dm;
    dm.dmDriverExtra = 0;
    if (GetMonitorInfoA(MonitorFromWindow(GAME_WINDOW, MONITOR_DEFAULTTONEAREST), (MONITORINFO *)&mi) &&
        EnumDisplaySettingsA(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
        return (int)dm.dmDisplayFrequency;
    return 60;
}

static void start_battle(BYTE *turn_manager)
{
    int monitor = monitor_refresh_rate(), rate = g_rate ? g_rate : monitor;

    g_battle = turn_manager;
    g_object_count = 0;
    g_snapshot_tick = 0;
    g_hud_waits = FALSE;
    g_frame_us = rate > 25 ? 1000000 / rate : 0;
    /* VSync: present waits for the monitor; the timer runs a little faster, for when it does not
     * (covered window, locked screen) */
    if (g_frame_us && g_vsync && rate >= monitor)
        g_frame_us = 900000 / monitor;
    g_next_frame = now_us();
    g_step_called = 0;
    g_turn_player = NULL;
    g_turn = 0;
    if (NETWORK_BATTLE)
        net_battle_players();
    say("battle: monitor %d Hz, %s", monitor,
        !g_frame_us ? "one frame per game tick (25 a second)"
        : g_vsync && rate >= monitor ? "a frame per monitor refresh"
                                     : "frames by timer");
}

static BOOL frame_due(LONGLONG now)
{
    return !g_frame_us || now >= g_next_frame - 500;
}

static void frame_done(LONGLONG now)
{
    g_next_frame += g_frame_us;
    if (g_next_frame < now || g_next_frame > now + 2 * g_frame_us)
        g_next_frame = now + g_frame_us; /* late, or the first frame: no catching up */
}

/* HUD, message banner and blinking objects (type 0x135B) advance in each draw call: a quiet
 * frame restores them, so only one frame per tick advances. */
static void draw_frame(BYTE *renderer, LONGLONG now, BOOL quiet)
{
    static BYTE hud_copy[HUD_SIZE], messages_copy[MESSAGES_SIZE];
    BYTE *turn_manager = TURN_MANAGER, *hud = HUD, *messages = MESSAGES, *obj;
    DWORD scroll = MESSAGE_SCROLL;
    int *blink[64], blink_value[64], blinks = 0, period;
    BOOL between = g_interpolate && g_frame_us && turn_manager;

    if (quiet) {
        if (hud)
            memcpy(hud_copy, hud, HUD_SIZE);
        if (messages)
            memcpy(messages_copy, messages, MESSAGES_SIZE);
        for (obj = DRAW_LIST; obj && blinks < 64; obj = *(BYTE **)(obj + 4))
            if (*(int *)(obj + 0x38) == 0x135B && *(BYTE **)(obj + 0x58)) {
                blink[blinks] = (int *)(*(BYTE **)(obj + 0x58) + 0x54);
                blink_value[blinks] = *blink[blinks];
                blinks++;
            }
    }
    if (between) {
        float t;

        period = *(int *)(turn_manager + 0x314);
        t = (float)(now - tick_time_us(TICK_TIME)) / (float)((period > 0 ? period : 40) * 1000);
        place_objects(t < 0 ? 0 : t > 1 ? 1 : t);
    }
    game_draw(renderer);
    if (between)
        restore_objects();
    if (quiet) {
        if (hud)
            memcpy(hud, hud_copy, HUD_SIZE);
        if (messages)
            memcpy(messages, messages_copy, MESSAGES_SIZE);
        MESSAGE_SCROLL = scroll;
        while (blinks--)
            *blink[blinks] = blink_value[blinks];
    }
    count_frame(&g_battle_stats, "battle", turn_manager ? *(int *)(turn_manager + 0x314) : 0);
}

/* The step's draw call, after its ticks; other loops call the step too (computer player, 0x448ED0) */
static void __fastcall tick_frame(BYTE *renderer)
{
    BYTE *turn_manager = TURN_MANAGER;
    LONGLONG now = now_us();
    int period, advanced = (int)(TICK_TIME - g_step_tick);

    if (!turn_manager) {
        game_draw(renderer);
        return;
    }
    period = *(int *)(turn_manager + 0x314);

    /* undo g_step_late where the step restarts the tick time at its end (computer's turn, +0x318) */
    if (g_step_late > 0 && !NESTED_STEP && advanced > period && advanced < 2 * period && g_step_late <= advanced - period)
        TICK_TIME -= g_step_late;
    g_step_late = 0;

    if (turn_manager != g_battle)
        start_battle(turn_manager);
    if (TICK_TIME != g_snapshot_tick) {
        snapshot();
        g_snapshot_tick = TICK_TIME;
    }
    if (!frame_due(now)) {
        g_hud_waits = TRUE;
        return;
    }
    g_hud_waits = FALSE;
    draw_frame(renderer, now, FALSE);
    frame_done(now);
}

/* The computer player draws before and after thinking (0x441A00), mid-tick: objects would jump */
static void __fastcall mid_tick_frame(BYTE *renderer)
{
    if (!g_frame_us || !g_interpolate)
        game_draw(renderer);
}

/* ---- network battle: the other player's turn ---- */

/* The player on turn sends a tick's pad, 3 to a message (FUN_00461120); the others take one a tick (FUN_0048CE00,
 * spins in FUN_0048CEF0 when empty). Buffer: two messages. ponytail: fixed; on stutter, follow the message gaps */
enum { REMOTE_BUFFER = 6 };

static BOOL g_remote_buffering;
static BOOL g_remote_held;     /* the step was held back: the elapsed time is not a debt of ticks */
static DWORD g_remote_wait;    /* the log says the queue has been empty since then, 0: it does not */
static int g_remote_seen;      /* the queue's length at the last look, -1: no queue is played */
static DWORD g_remote_since;   /* timeGetTime when it last grew or ran empty */

/* The ticks of the player on turn that wait in the queue; -1 when the step takes none from it. */
static int remote_backlog(BYTE *turn_manager)
{
    BYTE *game = GAME_CONFIG, *player;
    int state, n;

    /* +0x478: every player has loaded the battle; +1 and +0x541: the battle is being left */
    if (!NETWORK_BATTLE || LOCAL_CLOCK || !turn_manager || !game || !game[0x478] || game[1] || turn_manager[0x541])
        return -1;
    state = *(int *)(turn_manager + 0x510);
    player = *(BYTE **)(turn_manager + 0x4FC);
    if (state == 2 || state == 0x12 || !player)
        return -1;
    n = *(int *)(player + 0x6118) - *(int *)(player + 0x611C); /* written, taken: a ring of 0x800 */
    return n & 0x7FF;
}

/* TRUE: do not call the step now. Else TICK_TIME lets the step run no more ticks than queued. */
static BOOL hold_remote_tick(BYTE *turn_manager)
{
    int backlog = remote_backlog(turn_manager), period, late, most = 1;
    DWORD ms = timeGetTime();

    if (g_remote_wait && backlog) { /* -1: no queue is played now */
        say("battle: %s after %.1f s without the input of the player on turn", backlog > 0 ? "input came" : "the wait ended",
            (ms - g_remote_wait) / 1000.0);
        g_remote_wait = 0;
    }
    if (backlog < 0) {
        g_remote_seen = -1;
        g_remote_buffering = TRUE;
        if (!g_remote_held || !turn_manager)
            return FALSE;
    } else {
        if (backlog > g_remote_seen || (!backlog && g_remote_seen))
            g_remote_since = ms;
        g_remote_seen = backlog;
        if (!backlog)
            g_remote_buffering = TRUE;
        else if (backlog >= REMOTE_BUFFER || ms - g_remote_since > 250) /* enough, or no more is coming */
            g_remote_buffering = FALSE;
        if (g_remote_buffering && (backlog || ms - g_remote_since < 1000))
            return g_remote_held = TRUE;
        if (!backlog) { /* empty for a second: the step has its own ways with a silent player */
            if (!g_remote_wait) {
                g_remote_wait = g_remote_since | 1;
                say("battle: no input from the player on turn for 1 s");
            }
            return g_remote_held = FALSE;
        }
        if (!g_remote_held)
            most = backlog; /* never more ticks than the queue has: the step would spin for the rest */
    }
    period = *(int *)(turn_manager + 0x314);
    period = period > 0 ? period : 40;
    late = (int)(ms - TICK_TIME);
    if (late >= (most + 1) * period)
        TICK_TIME = ms - most * period;
    else if (backlog > 2 * REMOTE_BUFFER && late >= period && late < 2 * period)
        TICK_TIME = ms - 2 * period;
    if (late >= period)
        g_remote_held = FALSE;
    return FALSE;
}

/* Network battle: all copies play the same ticks, so pigs stand in the same places as a turn begins.
 * ponytail: positions after the step that changed the turn; a pig still moving can differ by a tick */
static void note_turn(BYTE *turn_manager)
{
    BYTE *player = *(BYTE **)(turn_manager + 0x4FC), *obj, *place;
    DWORD sum = 0, hash;
    int pigs = 0, i, n;

    if (!player || player == g_turn_player)
        return;
    g_turn_player = player;
    for (obj = DRAW_LIST, n = 0; obj && n < 4096; obj = *(BYTE **)(obj + 4), n++) {
        if (*(DWORD *)(obj + 0x38) != 0x1357 || !(place = *(BYTE **)(obj + 0x18)))
            continue; /* not a pig */
        for (hash = 2166136261u, i = 0; i < 3; i++)
            hash = (hash ^ (DWORD)(int)((const float *)(place + 4))[i]) * 16777619u;
        sum += hash; /* a sum: the list's order is the drawing order */
        pigs++;
    }
    say("battle: turn %d, %d pigs, places %08X", ++g_turn, pigs, sum);
}

static DWORD __cdecl battle_step(void)
{
    BYTE *turn_manager = TURN_MANAGER, *renderer = RENDERER;
    DWORD tick0, result;
    LONGLONG now, next_tick;
    BOOL frames;
    int period;

    if (NETWORK_BATTLE && turn_manager && turn_manager == g_battle) {
        DWORD ms = timeGetTime(); /* a window held by the mouse, a hang: the others wait for this copy */

        if (g_step_called && ms - g_step_called > 1000)
            say("battle: the game stood still for %.1f s", (ms - g_step_called) / 1000.0);
        g_step_called = ms;
    }
    if (hold_remote_tick(turn_manager)) {
        now = now_us();
        frames = g_frame_us && renderer && turn_manager == g_battle && g_snapshot_tick == TICK_TIME &&
                 !IsIconic(GAME_WINDOW);
        if (frames && frame_due(now)) {
            draw_frame(renderer, now, !g_hud_waits);
            g_hud_waits = FALSE;
            frame_done(now);
        }
        wait_until(frames && g_next_frame < now + 2000 ? g_next_frame : now + 2000, TRUE);
        return 0;
    }
    tick0 = TICK_TIME;
    g_step_tick = tick0;
    g_step_late = turn_manager ? (int)(timeGetTime() - tick0) - *(int *)(turn_manager + 0x314) : 0;
    result = game_step();
    if (g_step_called) /* the step itself may wait for the network: those lines are the input's */
        g_step_called = timeGetTime();
    g_step_late = 0;
    turn_manager = TURN_MANAGER;
    if ((result >= 2 && result <= 5) || !turn_manager) {
        if (g_battle && NETWORK_BATTLE)
            say("battle: over, result %u", result);
        g_battle = NULL; /* the battle is over */
        g_remote_held = FALSE;
        g_remote_wait = 0;
        return result;
    }
    if (NETWORK_BATTLE && turn_manager == g_battle)
        note_turn(turn_manager);
    period = *(int *)(turn_manager + 0x314);
    if (period <= 0)
        period = 40;
    now = now_us();
    next_tick = tick_time_us(TICK_TIME) + period * 1000;
    if (now >= next_tick) {
        if (TICK_TIME == tick0)
            wait_until(now + 5000, TRUE); /* a tick is due, the step ran none (waits for the network): no spinning */
        return result;
    }

    /* only after the step drew the last tick's frame or left it to us: the game is then in a
     * state it draws itself */
    frames = g_frame_us && renderer && turn_manager == g_battle && g_snapshot_tick == TICK_TIME &&
             !IsIconic(GAME_WINDOW);
    if (frames && frame_due(now)) {
        draw_frame(renderer, now, !g_hud_waits);
        g_hud_waits = FALSE;
        frame_done(now);
    }
    wait_until(frames && g_next_frame < next_tick ? g_next_frame : next_tick, TRUE);
    return result;
}

void frames_init(BOOL vsync)
{
    /* all or none: the hooks work together, and quiet frames copy HUD and banner at known sizes */
    BOOL known = !memcmp((void *)0x47E42E, "\xE8\xFD\x14\x00\x00", 5) && !memcmp((void *)0x47FC49, "\xE8\x42\xE6\xFC\xFF", 5) &&
                 !memcmp((void *)0x47FCC0, "\xE9\xCB\xE5\xFC\xFF", 5) && !memcmp((void *)0x485E66, "\x68\x18\x0E\x00\x00", 5) &&
                 !memcmp((void *)0x485DF9, "\x68\x6C\x09\x00\x00", 5);

    QueryPerformanceFrequency(&g_qpc_freq);
    QueryPerformanceCounter(&g_qpc_start);
    g_ms_start = timeGetTime();
    g_ms_lead = 1000000;
    timeBeginPeriod(1); /* 1 ms steps for timeGetTime (the game's tick clock) and the timer */
    g_timer = CreateWaitableTimerExA(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!g_timer) /* before Windows 10 1803 */
        g_timer = CreateWaitableTimerA(NULL, FALSE, NULL);

    g_stats = GetPrivateProfileIntA("Debug", "FrameStats", 0, g_ini);
    g_rate = GetPrivateProfileIntA("Battle", "FrameRate", 0, g_ini);
    g_rate = g_rate < 0 ? 0 : g_rate > 1000 ? 1000 : g_rate;
    g_interpolate = GetPrivateProfileIntA("Battle", "Interpolation", 1, g_ini);
    g_vsync = vsync;

    hook(0x42745F, 0xE8, "\xE8\xCC\x70\x08\x00", menu_frame_clock, "menus at their speed, 25 frames a second");

    if (!known) {
        say("skip %-40s %08X: unexpected bytes, unknown game build?", "battle: no busy loop, more frames", 0x47E42E);
        return;
    }
    hook(0x47E42E, 0xE8, "\xE8\xFD\x14\x00\x00", battle_step, "battle: wait for the next tick or frame");
    hook(0x47FC49, 0xE8, "\xE8\x42\xE6\xFC\xFF", tick_frame, "battle: frames between game ticks");
    hook(0x47FCC0, 0xE9, "\xE9\xCB\xE5\xFC\xFF", mid_tick_frame, "battle: no frame from inside a tick");
}
