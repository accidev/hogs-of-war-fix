/* gamepad.c - XInput controllers (Xbox pads and compatibles) with the PlayStation layout.
 *
 * The game reads input in two places:
 * - Battle: the human player's input function (0x45D700) reads the DirectInput keyboard into
 *   256 key bytes (FUN_0044CBB0) and builds a 16-bit pad mask in PlayStation order from the
 *   player's key bindings (16 DIK codes at player+0x108, set in Options, Controls). Bits:
 *   0 up, 1 down, 2 left, 3 right, 4 triangle, 5 cross, 6 square, 7 circle, 8 L1, 9 L2, 10 R2,
 *   11 R1, 12 L3, 13 R3, 14 start, 15 select. The pause and weapon menus run on that mask too.
 *   The keyboard read (0x45D72D) is wrapped: a pad button sets the key byte of the key bound
 *   to it, so the game decides what a button does, as on the PlayStation.
 * - Menus: MainWndProc handles WM_KEYDOWN (Return, Backspace, arrows) while the frontend flag
 *   [0x51F120] is set, and Space and Escape on the results screens. Pad buttons are posted to
 *   the window as key presses.
 * The game's own joystick support (DirectInput, first joystick found) would see an XInput pad
 * as well, with another button order: its enumeration callback (pushed at 0x44C8DE) is wrapped
 * to skip XInput devices. Other joysticks stay with the game, [Gamepad] Deadzone included.
 *
 * Ported from Torisutan/HogsFix (MIT license), src/gamepad.c.
 */
#include "hogs.h"
#include <mmsystem.h>
#include <string.h>
#include <xinput.h>
#define DIRECTINPUT_VERSION 0x0700
#include <dinput.h>

#define FRONTEND (*(DWORD *)0x51F120)                      /* not 0: a menu is on screen */
#define RESULTS_WAIT (*(*(BYTE **)0x520668 + 0x479))       /* a results screen waits for Space or Escape */
#define DIRECT_INPUT (*(IDirectInput7A **)0x51BCF0)

enum {
    PS_UP = 1, PS_DOWN = 2, PS_LEFT = 4, PS_RIGHT = 8, PS_TRIANGLE = 0x10, PS_CROSS = 0x20, PS_SQUARE = 0x40,
    PS_CIRCLE = 0x80, PS_L1 = 0x100, PS_L2 = 0x200, PS_R2 = 0x400, PS_R1 = 0x800, PS_L3 = 0x1000, PS_R3 = 0x2000,
    PS_START = 0x4000, PS_SELECT = 0x8000
};

/* XINPUT_GAMEPAD.wButtons bit -> the PlayStation button at the same place on the pad */
static const WORD kButtons[16] = {
    PS_UP, PS_DOWN, PS_LEFT, PS_RIGHT, /* d-pad */
    PS_START, PS_SELECT, PS_L3, PS_R3, /* Start, Back, stick clicks */
    PS_L1, PS_R1, 0, 0,                /* LB, RB */
    PS_CROSS, PS_CIRCLE, PS_SQUARE, PS_TRIANGLE, /* A, B, X, Y */
};

static BOOL g_enabled; /* [Gamepad] XInput, and the hooks are in */
static DWORD(WINAPI *g_get_state)(DWORD, XINPUT_STATE *);
static int g_slot = -1;           /* the connected controller, -1 none */
static int g_stick;               /* left stick: this far from the centre is a direction */
static XINPUT_GAMEPAD *g_test;    /* [Debug] PadTest: a pad written by a test script */
static BOOL g_test_wanted;
static WORD g_held, g_was_held;   /* PlayStation bits now and at the poll before */
static WORD g_pressed;            /* held at any time since the battle last read the pad */
static WORD g_repeat;             /* the direction that repeats in a menu */
static DWORD g_repeat_at, g_next_scan, g_polled;

static WORD ps_buttons(const XINPUT_GAMEPAD *p)
{
    WORD m = 0;
    int i;

    for (i = 0; i < 16; i++)
        if (p->wButtons & (1 << i))
            m |= kButtons[i];
    if (p->bLeftTrigger > 64)
        m |= PS_L2;
    if (p->bRightTrigger > 64)
        m |= PS_R2;
    if (p->sThumbLY > g_stick)
        m |= PS_UP;
    if (p->sThumbLY < -g_stick)
        m |= PS_DOWN;
    if (p->sThumbLX < -g_stick)
        m |= PS_LEFT;
    if (p->sThumbLX > g_stick)
        m |= PS_RIGHT;
    if (p->sThumbRY > 16000) /* right stick up and down: R1 and R2, the trajectory */
        m |= PS_R1;
    if (p->sThumbRY < -16000)
        m |= PS_R2;
    return m;
}

/* A key press for MainWndProc. It takes a WM_KEYDOWN only after a WM_KEYUP (its guard against
 * auto-repeat), so the key-up goes first: the first press on the title screen counts too. */
static void press_key(HWND hwnd, UINT vk)
{
    DWORD lp = 1 | MapVirtualKeyA(vk, MAPVK_VK_TO_VSC) << 16 | (vk >= VK_PRIOR && vk <= VK_DOWN ? 1 << 24 : 0);

    PostMessageA(hwnd, WM_KEYUP, vk, (LPARAM)(lp | 0xC0000000));
    PostMessageA(hwnd, WM_KEYDOWN, vk, (LPARAM)lp);
    PostMessageA(hwnd, WM_KEYUP, vk, (LPARAM)(lp | 0xC0000000));
}

static WORD read_pad(DWORD now)
{
    static DWORD probe;
    XINPUT_STATE s;

    if (g_test)
        return ps_buttons(g_test);
    if (g_slot < 0 && (int)(now - g_next_scan) >= 0) {
        HANDLE map;

        /* A controller may be plugged in later: look once a second, at one slot each time,
         * because asking an empty slot can take milliseconds. */
        g_next_scan = now + 1000;
        if (g_test_wanted && (map = OpenFileMappingA(FILE_MAP_READ, FALSE, "HogsPadTest")) != NULL &&
            (g_test = (XINPUT_GAMEPAD *)MapViewOfFile(map, FILE_MAP_READ, 0, 0, sizeof *g_test)) != NULL) {
            say("gamepad: test pad HogsPadTest");
            return 0;
        }
        if (g_get_state(probe, &s) == ERROR_SUCCESS) {
            g_slot = (int)probe;
            say("gamepad: XInput controller %d", g_slot);
        }
        probe = (probe + 1) % XUSER_MAX_COUNT;
    }
    if (g_slot < 0)
        return 0;
    if (g_get_state(g_slot, &s) != ERROR_SUCCESS) {
        say("gamepad: XInput controller %d is gone", g_slot);
        g_slot = -1;
        return 0;
    }
    return ps_buttons(&s.Gamepad);
}

/* XInput is loaded at the first poll, not in gamepad_init: that one runs inside DllMain. */
static BOOL load_xinput(void)
{
    static const char *const kDlls[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
    HMODULE dll = NULL;
    int i;

    for (i = 0; !dll && i < 3; i++)
        dll = LoadLibraryA(kDlls[i]);
    g_get_state = dll ? (DWORD(WINAPI *)(DWORD, XINPUT_STATE *))GetProcAddress(dll, "XInputGetState") : NULL;
    if (!g_get_state)
        say("gamepad: no XInput on this Windows");
    return g_get_state != NULL;
}

void gamepad_poll(void)
{
    static const struct {
        WORD button;
        BYTE vk;
    } kMenu[] = { { PS_UP, VK_UP },         { PS_DOWN, VK_DOWN },    { PS_LEFT, VK_LEFT },   { PS_RIGHT, VK_RIGHT },
                  { PS_CROSS, VK_RETURN },  { PS_START, VK_RETURN }, { PS_CIRCLE, VK_BACK }, { PS_TRIANGLE, VK_BACK } };
    DWORD now = timeGetTime();
    HWND hwnd = GAME_WINDOW;
    WORD down;
    int i;

    if (!g_enabled || now - g_polled < 4) /* the message loops call this far more often than a pad changes */
        return;
    g_polled = now;
    if (!g_get_state && !load_xinput()) {
        g_enabled = FALSE;
        return;
    }
    g_held = read_pad(now);
    if (!hwnd || (!g_test && GetForegroundWindow() != hwnd)) /* XInput also reports while another window has the focus */
        g_held = 0;
    down = g_held & ~g_was_held;
    g_was_held = g_held;
    if (!hwnd)
        return;
    if (FRONTEND) {
        g_pressed = 0; /* a menu press must not reach the battle */
        for (i = 0; i < (int)(sizeof kMenu / sizeof kMenu[0]); i++)
            if (down & kMenu[i].button) {
                press_key(hwnd, kMenu[i].vk);
                if (i < 4) { /* a held direction repeats, like a held key */
                    g_repeat = kMenu[i].button;
                    g_repeat_at = now + 400;
                }
            }
        if (!(g_held & g_repeat))
            g_repeat = 0;
        if (g_repeat && (int)(now - g_repeat_at) >= 0) {
            g_repeat_at = now + 110;
            press_key(hwnd, g_repeat == PS_UP ? VK_UP : g_repeat == PS_DOWN ? VK_DOWN : g_repeat == PS_LEFT ? VK_LEFT : VK_RIGHT);
        }
        return;
    }
    g_repeat = 0;
    g_pressed |= g_held;
    if (RESULTS_WAIT) { /* cross: continue, triangle: retry */
        if (down & PS_CROSS)
            press_key(hwnd, VK_SPACE);
        if (down & PS_TRIANGLE)
            press_key(hwnd, VK_ESCAPE);
    } else if (down & (PS_CROSS | PS_START | PS_CIRCLE)) {
        press_key(hwnd, VK_ESCAPE); /* skips videos and the title screen; a battle does not use the message */
    }
}

/* Replaces the keyboard read of the human player's input function; edi there is the player. */
static BYTE __cdecl keyboard_state(BYTE *keys, const BYTE *player)
{
    static BYTE(__cdecl *const read_keyboard)(BYTE *) = (BYTE(__cdecl *)(BYTE *))0x44CBB0;
    const WORD *bound = (const WORD *)(player + 0x108);
    BYTE ok = read_keyboard(keys);
    WORD buttons;
    int i;

    g_polled -= 4; /* read the pad now */
    gamepad_poll();
    buttons = g_held | g_pressed;
    g_pressed = 0;
    if (!buttons)
        return ok;
    if (!ok)
        memset(keys, 0, 256);
    for (i = 0; i < 16; i++)
        if ((buttons & (1 << i)) && bound[i] && bound[i] < 256)
            keys[bound[i]] = 0x80;
    return 1;
}

static __declspec(naked) void keyboard_state_stub(void)
{
    __asm {
        push edi
        push dword ptr [esp + 8]
        call keyboard_state
        add esp, 8
        ret
    }
}

/* An XInput device has "IG_" in its device path (Microsoft's documented test). */
static BOOL CALLBACK enum_joystick(const DIDEVICEINSTANCEA *joystick, void *ref)
{
    static BOOL(CALLBACK *const game_callback)(const DIDEVICEINSTANCEA *, void *) =
        (BOOL(CALLBACK *)(const DIDEVICEINSTANCEA *, void *))0x44C700;
    IDirectInput7A *di = DIRECT_INPUT;
    IDirectInputDeviceA *dev = NULL;
    DIPROPGUIDANDPATH path;
    BOOL xinput = FALSE;

    memset(&path, 0, sizeof path);
    path.diph.dwSize = sizeof path;
    path.diph.dwHeaderSize = sizeof path.diph;
    path.diph.dwHow = DIPH_DEVICE;
    if (di && SUCCEEDED(IDirectInput7_CreateDevice(di, &joystick->guidInstance, &dev, NULL)) && dev) {
        if (SUCCEEDED(IDirectInputDevice_GetProperty(dev, DIPROP_GUIDANDPATH, &path.diph)))
            xinput = wcsstr(path.wszPath, L"IG_") != NULL || wcsstr(path.wszPath, L"ig_") != NULL;
        IDirectInputDevice_Release(dev);
    }
    say("gamepad: joystick \"%s\"%s", joystick->tszProductName, xinput ? " is an XInput controller, hidden from the game" : "");
    return xinput ? DIENUM_CONTINUE : game_callback(joystick, ref);
}

void gamepad_init(void)
{
    BYTE push[5] = { 0x68 }; /* push imm32 */
    void *callback = (void *)enum_joystick;
    int deadzone = GetPrivateProfileIntA("Gamepad", "XInputDeadzone", 30, g_ini);

    if (!GetPrivateProfileIntA("Gamepad", "XInput", 1, g_ini))
        return;
    g_stick = (deadzone < 5 ? 5 : deadzone > 90 ? 90 : deadzone) * 32767 / 100;
    g_test_wanted = GetPrivateProfileIntA("Debug", "PadTest", 0, g_ini);
    g_next_scan = timeGetTime();

    memcpy(push + 1, &callback, 4);
    g_enabled = hook(0x45D72D, 0xE8, "\xE8\x7E\xF4\xFE\xFF", keyboard_state_stub, "gamepad: XInput buttons as bound keys") &&
                patch(0x44C8DE, "\x68\x00\xC7\x44\x00", push, 5, "gamepad: XInput pads not as joysticks");
}
