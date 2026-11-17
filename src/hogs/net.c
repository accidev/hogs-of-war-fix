/* net.c - network games over the internet: a stand-in for DirectPlay that talks to a lobby server.
 *
 * With [Network] Server set, CoCreateInstance returns these two objects instead of DirectPlay and
 * its lobby. Sessions, players and player data live on the server (frames: PROTOCOL.md of
 * hogs-of-war-server); messages pass through it over TCP (Server=name:port) or a WebSocket
 * (Server=wss://name/path).
 *
 * What the game uses of DirectPlay (network object at [0x51D7C0], set up by FUN_0045FF30):
 *   EnumConnections      menu 47, callback 0x460180: only TCP/IP and IPX; "Internet"/"TCP" ask for an IP
 *   InitializeConnection address from the lobby object's CreateCompoundAddress
 *   EnumSessions         menu 48, callback 0x460640 skips sessions closed to joining
 *   Open, CreatePlayer   FUN_004607A0 hosts, FUN_00460990 joins; names: Windows user, "<name>'s Game"
 *   EnumPlayers          callback 0x460C80 copies the name into 16 bytes unchecked; players by id order
 *   SetPlayerData, GetPlayerData   the team, 680 bytes, set every second (menu clock [0x520878]: 10 ms units)
 *   Send, Receive        348-byte messages; a multimedia timer thread polls Receive every 32 ms
 *   GetSessionDesc, SetSessionDesc   FUN_004619D0 closes the session when the battle starts
 *   DestroyPlayer, Close, Release
 * Everything else answers "not supported" and writes a line to the log.
 */
#include "hogs.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NET_VERSION 3 /* raise when a release changes a battle: such games must not meet */

#define DP_OK 0
#define DPERR_UNSUPPORTED ((HRESULT)0x80004001)
#define DPERR_GENERIC ((HRESULT)0x80004005)
#define DPERR_BUFFERTOOSMALL ((HRESULT)0x8877001E)
#define DPERR_NOMESSAGES ((HRESULT)0x887700BE)
#define DPERR_NOCONNECTION ((HRESULT)0x887700B4)
#define DPSESSION_JOINDISABLED 0x20
#define DPOPEN_CREATE 2
#define DPSET_LOCAL 1
#define DPSYS_DESTROYPLAYERORGROUP 5
#define DPSYS_SESSIONLOST 0x31

/* frame types */
enum { HELLO = 1, LIST, CREATE, JOIN, LEAVE, SEND, SET_ROOM, SET_PLAYER, PING };
enum { WELCOME = 0x81, ROOMS, JOINED, PLAYER_JOINED, PLAYER_LEFT, DATA, ROOM_CHANGED, PLAYER_DATA, OK, FAILED, PONG };
enum { PROTOCOL = 1, PORT = 47800, MAX_FRAME = 16384, ROOM_CLOSED = 1 };
enum { MAX_ROOMS = 16, QUEUE = 512, PING_SECONDS = 5 };
/* the game's limits: flags and pointers for 4 players at net+0x41C..0x437, 348-byte messages
 * (FUN_00461120), a 680-byte team as player data, 59 levels in the table at 0x4D1990 */
enum { MAX_PLAYERS = 4, MESSAGE = 348, TEAM = 680, LEVELS = 59 };

typedef struct { /* DPSESSIONDESC2 */
    DWORD dwSize, dwFlags;
    GUID guidInstance, guidApplication;
    DWORD dwMaxPlayers, dwCurrentPlayers;
    const char *lpszSessionNameA, *lpszPasswordA;
    DWORD dwReserved1, dwReserved2, dwUser1, dwUser2, dwUser3, dwUser4;
} SESSION;

typedef struct { /* DPNAME */
    DWORD dwSize, dwFlags;
    const char *lpszShortNameA, *lpszLongNameA;
} NAME;

typedef BOOL(PASCAL *CONNECTION_CALLBACK)(const GUID *sp, void *connection, DWORD size, const NAME *name, DWORD flags, void *ctx);
typedef BOOL(PASCAL *SESSION_CALLBACK)(const SESSION *desc, DWORD *timeout, DWORD flags, void *ctx);
typedef BOOL(PASCAL *PLAYER_CALLBACK)(DWORD id, DWORD type, const NAME *name, DWORD flags, void *ctx);

struct room {
    DWORD id, compat;
    BYTE flags, players, limit;
    char name[64];
};

struct player {
    DWORD id, size;
    char name[16];
    BYTE data[TEAM];
};

struct message {
    DWORD from, to, size;
    BYTE data[MESSAGE];
};

struct link {
    SOCKET s;
    HINTERNET ws, host;
    DWORD error; /* of the read that failed; 0: the server closed the connection */
};

static const GUID kDirectPlay = { 0xD1EB6D20, 0x8923, 0x11D0, { 0x9D, 0x97, 0x00, 0xA0, 0xC9, 0x0A, 0x43, 0xCB } };
static const GUID kDirectPlayLobby = { 0x2FE8F810, 0xB2A5, 0x11D0, { 0xA7, 0x87, 0x00, 0x00, 0xF8, 0x03, 0xAB, 0xFC } };
static const GUID kIpx = { 0x685BC400, 0x9D2C, 0x11CF, { 0xA9, 0xCD, 0x00, 0xAA, 0x00, 0x68, 0x86, 0xE3 } };

static char g_server[128], g_port[8], g_path[128], g_name[16];
static int g_web;            /* WebSocket server: 1 ws://, 2 wss:// */
static DWORD g_compat;
static CRITICAL_SECTION g_lock; /* guards everything below and whole frames on the connection */
static SOCKET g_sock = INVALID_SOCKET;
static HINTERNET g_ws, g_http;
#define CONNECTED (g_sock != INVALID_SOCKET || g_ws)
static DWORD g_epoch;        /* counts link threads and drops; a link thread belongs to one */
static BOOL g_connecting;    /* the link thread of g_epoch is still opening its connection */
static HANDLE g_linked;      /* the last attempt to connect has ended, either way */
static DWORD g_tried;        /* tick when it ended, 0 = none yet */
static DWORD g_pinged, g_waiting; /* ticks: last PING; first PING not answered by any frame, 0 = none */
static BOOL g_silent;        /* the log says the server is silent; it says so again when a frame comes */
static DWORD g_heard;        /* tick of the last frame from the server */
static DWORD g_ping;         /* round trip in ms, 0 = not measured */
static DWORD g_id;
static HANDLE g_answer;      /* CREATE or JOIN answered, or connection lost */
static int g_joined;         /* the answer: 1 in the room, -1 refused */
static int g_stale;          /* answers still to come for CREATE or JOIN requests that were given up */
static struct room g_rooms[MAX_ROOMS], g_listing[MAX_ROOMS];
static int g_room_count, g_listing_count;
static BOOL g_in_room;
static struct room g_room;
static GUID g_application;
static struct player g_players[MAX_PLAYERS];
static int g_player_count;
static DWORD g_open_room, g_open_limit;
static BOOL g_open_create;
static struct message g_queue[QUEUE];
static int g_head, g_count;
static DWORD g_listed;       /* players in the game's list when dp_receive last looked */
static DWORD g_dropped, g_drop_said, g_drop_from, g_drop_type; /* messages dropped since the last line about them */
static short g_drop_place;
static DWORD g_gone[MAX_PLAYERS]; /* who left the room since the game last stood in a menu */
static int g_gone_count;
static int g_links;          /* link threads that have not ended; a new one waits for them */
static HINTERNET g_aborted;  /* WebSocket closed under its link thread's read: not that thread's to close */
static LONG g_refs[2];

/* ---- frames ---- */

static BYTE *put16(BYTE *p, DWORD v)
{
    p[0] = (BYTE)v;
    p[1] = (BYTE)(v >> 8);
    return p + 2;
}

static BYTE *put32(BYTE *p, DWORD v)
{
    memcpy(p, &v, 4);
    return p + 4;
}

static BYTE *put_str8(BYTE *p, const char *s)
{
    size_t n = strlen(s);

    *p++ = (BYTE)n;
    memcpy(p, s, n);
    return p + n;
}

static void drop_connection(const char *why);

/* 0, or the Winsock or WinHTTP error */
static DWORD link_send(const struct link *l, BYTE type, const BYTE *payload, int size)
{
    BYTE buf[3 + 2100];
    int done = 0, n = 0;

    if (size > (int)sizeof buf - 3)
        return ERROR_INSUFFICIENT_BUFFER;
    put16(buf, (DWORD)size + 1);
    buf[2] = type;
    memcpy(buf + 3, payload, (size_t)size);
    size += 3;
    if (l->ws)
        return WinHttpWebSocketSend(l->ws, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE, buf, (DWORD)size);
    while (done < size && (n = send(l->s, (const char *)buf + done, size - done, 0)) > 0)
        done += n;
    return done == size ? 0 : n < 0 ? (DWORD)WSAGetLastError() : ERROR_GEN_FAILURE;
}

/* The lock is held. ponytail: a send to a stalled link holds the lock for its timeout (3-5 s), once:
 * the connection is dropped then. A writer thread with a queue if that ever shows in play. */
static BOOL send_frame(BYTE type, const BYTE *payload, int size)
{
    struct link l = { g_sock, g_ws, NULL, 0 };
    char why[40];
    DWORD error;

    if (!CONNECTED)
        return FALSE;
    if ((error = link_send(&l, type, payload, size)) != 0) {
        sprintf(why, "cannot send: error %u", error);
        drop_connection(why);
        return FALSE;
    }
    return TRUE;
}

/* byte 5: our ping period; the server then drops a silent player of a room after three of them */
static void send_ping(void)
{
    BYTE ping[5];

    g_pinged = GetTickCount();
    if (!g_waiting)
        g_waiting = g_pinged | 1;
    memcpy(ping, &g_pinged, 4);
    ping[4] = PING_SECONDS;
    send_frame(PING, ping, sizeof ping);
}

/* [Debug] NetStats=1. The lock is held. */
static BOOL g_net_stats;

static void count_message(BOOL received, const BYTE *data, DWORD size)
{
    static DWORD since, count[2][9], seq[2];
    DWORD type = 8, now = GetTickCount(), *c = count[0];

    if (!g_net_stats)
        return;
    if (now - since >= 1000) {
        if (since)
            say("network: sent %u %u %u %u %u %u %u %u +%u (input %u..%u), received %u %u %u %u %u %u %u %u +%u, ping %u, "
                "%d waiting for the game, %u players in its list",
                c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8], seq[0], seq[1],
                c[9], c[10], c[11], c[12], c[13], c[14], c[15], c[16], c[17], g_ping, g_count, g_listed);
        memset(count, 0, sizeof count);
        seq[0] = 0xFFFFFFFF;
        seq[1] = 0;
        since = now;
    }
    if (size >= 16)
        memcpy(&type, data, 4);
    type = type < 8 ? type : 8;
    count[received][type]++;
    if (!received && type == 4) {
        memcpy(&now, data + 12, 4);
        seq[0] = now < seq[0] ? now : seq[0];
        seq[1] = now > seq[1] ? now : seq[1];
    }
}

/* The game takes all of them every 32 ms, but not while it loads a level or shows the results. */
static struct message *push_message(DWORD from, DWORD to, const BYTE *data, DWORD size)
{
    static DWORD said;
    struct message *m;

    if (g_count == QUEUE) {
        if (GetTickCount() - said > 1000) {
            said = GetTickCount();
            say("network: the game does not read its messages: the queue of %d is full", QUEUE);
        }
        if (from) /* a player's message is lost; DirectPlay's own (player left, session lost) push the oldest out */
            return NULL;
        g_head = (g_head + 1) % QUEUE;
        g_count--;
    }
    m = &g_queue[(g_head + g_count++) % QUEUE];
    m->from = from;
    m->to = to;
    m->size = size;
    memcpy(m->data, data, size);
    return m;
}

static void push_system(DWORD type, DWORD id)
{
    DWORD msg[13] = { 0 }; /* the game reads only 5 (player destroyed, id at +8) and 0x31 (session lost) */

    msg[0] = type;
    msg[1] = 1;
    msg[2] = id;
    push_message(0, 0, (const BYTE *)msg, type == DPSYS_SESSIONLOST ? 4 : sizeof msg);
}

static struct player *find_player(DWORD id)
{
    int i;

    for (i = 0; i < g_player_count; i++)
        if (g_players[i].id == id)
            return &g_players[i];
    return NULL;
}

/* "PLAYER 1307 (61A), PLAYER 1310 (61D)": the ids name the senders in the other lines. The lock is held. */
static const char *player_list(void)
{
    static char text[MAX_PLAYERS * 32];
    char *p = text;
    int i;

    *p = 0;
    for (i = 0; i < g_player_count; i++)
        p += sprintf(p, "%s%s (%X)", i ? ", " : "", g_players[i].name, g_players[i].id);
    return text;
}

static void leave_room(void)
{
    g_in_room = FALSE;
    g_player_count = 0;
    g_head = g_count = 0;
}

static void gone(DWORD id)
{
    if (g_gone_count < MAX_PLAYERS)
        g_gone[g_gone_count++] = id;
}

/* The lock is held. shutdown() wakes the link thread's TCP read. A WebSocket read has no timeout and no
 * other way out than closing the handle; start_connect() lets no new handle take its value meanwhile. */
static void drop_connection(const char *why)
{
    g_epoch++;
    g_connecting = FALSE;
    SetEvent(g_linked);
    if (!CONNECTED)
        return;
    say("network: connection to %s closed: %s", g_server, why);
    if (g_sock != INVALID_SOCKET)
        shutdown(g_sock, SD_BOTH);
    if (g_ws && WinHttpCloseHandle(g_ws))
        g_aborted = g_ws;
    g_sock = INVALID_SOCKET;
    g_ws = NULL;
    g_room_count = 0;
    g_ping = 0;
    g_stale = 0;
    if (g_in_room) {
        DWORD ids[MAX_PLAYERS];
        int i, n = g_player_count;

        for (i = 0; i < n; i++)
            ids[i] = g_players[i].id;
        leave_room();
        /* a battle ignores "session lost" and waits for the others' turns for ever: they have left */
        for (i = 0; i < n; i++)
            if (ids[i] != g_id) {
                gone(ids[i]);
                push_system(DPSYS_DESTROYPLAYERORGROUP, ids[i]);
            }
        push_system(DPSYS_SESSIONLOST, 0);
        say("network: queued for the game: the other %d players have left, the session is lost", n - 1);
    }
    g_joined = -1;
    SetEvent(g_answer);
}

/* What the game's handlers (timer callback 0x460F50) use as an index, unchecked: [1] is the sender's
 * place in the player list; in type 4 the slot in a ring of 15 input records (0x14 bytes each from +8,
 * the place a short at +8 of a record), in type 7 the sender's player id. Type 3 carries 0x148 bytes
 * for 0x5206F0, the level number at +0x10. Type 5 is never sent: it sends the type 4 batch.
 * players: how many places the list has. */
static BOOL game_message(DWORD from, const BYTE *data, DWORD size, DWORD players)
{
    DWORD type, value, level;
    short place;

    if (size != MESSAGE)
        return FALSE;
    memcpy(&type, data, 4);
    memcpy(&value, data + 4, 4);
    switch (type) {
    case 4:
        if (value >= 15)
            return FALSE;
        memcpy(&place, data + 16 + value * 0x14, 2);
        return place >= 0 && (DWORD)place < players;
    case 7:
        return value == from;
    case 3:
        memcpy(&level, data + 8 + 0x10, 4);
        return level < LEVELS && value < players;
    case 0:
    case 1:
    case 2:
    case 6:
        return value < players;
    }
    return FALSE;
}

/* The game's own player list: [net + 0x18], 0x1C bytes a player, [net + 0x1C] of them. FUN_00460C20
 * builds it from EnumPlayers, in the menus only: in a battle it is the list of the battle's start. */
static DWORD game_players(void)
{
    const BYTE *net = NET_OBJECT;
    DWORD n = net ? *(const DWORD *)(net + 0x1C) : 0;

    return n < MAX_PLAYERS ? n : MAX_PLAYERS;
}

/* The lock is held. One line a second at most: input comes 8 messages a second. */
static void say_dropped(void)
{
    say("network: %u messages dropped that name a player the game does not have; the last from %X, type %u, "
        "place %d, the game has %u players", g_dropped, g_drop_from, g_drop_type, g_drop_place, g_listed);
    g_dropped = 0;
    g_drop_said = GetTickCount();
}

/* frames.c, as a network battle starts (DirectPlay's or ours): its players by place, as the messages name them */
void net_battle_players(void)
{
    const BYTE *net = NET_OBJECT, *list = net ? *(const BYTE *const *)(net + 0x18) : NULL;
    char text[MAX_PLAYERS * 10] = "";
    DWORD i, n = game_players();

    for (i = 0; list && i < n; i++)
        sprintf(text + strlen(text), "%s%X", i ? " " : "", *(const DWORD *)(list + i * 0x1C));
    if (net)
        say("network: the battle's players by place: %s; this one is %X", text, *(const DWORD *)(net + 0x2E4));
}

/* frames.c, the game's thread. Messages still waiting for the game can be his. */
int net_more_input(DWORD id)
{
    int more;

    if (!g_server[0])
        return -1;
    EnterCriticalSection(&g_lock);
    more = g_count || (g_in_room && find_player(id));
    LeaveCriticalSection(&g_lock);
    return more;
}

/* The lock is held. Who left while the battle loads: the game drops the notice (FUN_004613A0 wants net+0x438) and
 * stands 61 s for his "loaded" (type 2, 0x47F990). He goes to its leavers (0x51D7B0) and "loaded" comes from us. */
static struct message *loaded_for_gone(void)
{
    static struct message loaded;
    BYTE *net = NET_OBJECT, *game = GAME_CONFIG, *list;
    DWORD *left = (DWORD *)0x51D7B0, *left_count = (DWORD *)0x51D7A8, head[2] = { 2 }, n, i, k;
    int g;

    /* +0x478: this copy has loaded and waits; 0x537F30: the battle's players, the type 2 handler needs them */
    if (FRONTEND || !net || net[0x438] || !game || !game[0x478] || !*(void **)0x537F30)
        return NULL;
    list = *(BYTE **)(net + 0x18);
    n = game_players();
    for (g = 0; g < g_gone_count; g++)
        for (i = 0; list && i < n; i++) {
            if (*(DWORD *)(list + i * 0x1C) != g_gone[g])
                continue;
            for (k = 0; k < *left_count && left[k] != g_gone[g]; k++)
                ;
            if (k == *left_count && k < MAX_PLAYERS)
                left[(*left_count)++] = g_gone[g];
            if (net[0x430 + i])
                break;
            memset(&loaded, 0, sizeof loaded);
            loaded.from = g_gone[g];
            loaded.size = MESSAGE;
            head[1] = i;
            memcpy(loaded.data, head, sizeof head);
            say("network: %X left before the battle began: the game gets its \"loaded\" from us and removes him", g_gone[g]);
            return &loaded;
        }
    return NULL;
}

static BYTE seats(DWORD n)
{
    return (BYTE)(n < 1 ? 1 : n > MAX_PLAYERS ? MAX_PLAYERS : n);
}

struct fields {
    const BYTE *p, *end;
    BOOL bad;
};

static const BYTE *take(struct fields *f, DWORD n)
{
    const BYTE *p = f->p;

    if (f->bad || n > (DWORD)(f->end - f->p)) {
        f->bad = TRUE;
        return NULL;
    }
    f->p += n;
    return p;
}

static DWORD get(struct fields *f, int bytes)
{
    const BYTE *p = take(f, (DWORD)bytes);
    DWORD v = 0;

    if (p)
        memcpy(&v, p, (size_t)bytes);
    return v;
}

/* the game's font: capitals, digits, a few signs; '/' starts a command in its text */
static char name_char(char c)
{
    if (c >= 'a' && c <= 'z')
        c -= 0x20;
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '\'' ? c : '-';
}

static void get_str8(struct fields *f, char *to, DWORD room)
{
    DWORD n = get(f, 1), i;
    const BYTE *p = take(f, n);

    if (n >= room)
        n = room - 1;
    for (i = 0; p && i < n; i++)
        to[i] = name_char((char)p[i]);
    to[p ? n : 0] = 0;
}

static void get_player(struct fields *f, struct player *pl)
{
    const BYTE *p;

    pl->id = get(f, 4);
    get_str8(f, pl->name, sizeof pl->name);
    pl->size = get(f, 2);
    p = take(f, pl->size);
    if (p && pl->size == TEAM)
        memcpy(pl->data, p, TEAM);
    else
        pl->size = 0; /* not a team: the player has none yet */
}

/* The lock is held. FALSE: malformed frame. */
static BOOL on_frame(BYTE type, const BYTE *body, DWORD size)
{
    struct fields f = { body, body + size, FALSE };
    struct player joined, *pl;
    DWORD i, n, id;

    switch (type) {
    case ROOMS:
        i = get(&f, 1); /* more frames of the list follow */
        for (n = get(&f, 2); n && !f.bad; n--) {
            struct room r;

            r.id = get(&f, 4);
            r.compat = get(&f, 4);
            r.flags = (BYTE)get(&f, 1);
            r.players = (BYTE)get(&f, 1);
            r.limit = seats(get(&f, 1));
            r.players = r.players > r.limit ? r.limit : r.players;
            get_str8(&f, r.name, sizeof r.name);
            take(&f, get(&f, 1));
            take(&f, get(&f, 2));
            if (!f.bad && r.compat == g_compat && g_listing_count < MAX_ROOMS)
                g_listing[g_listing_count++] = r;
        }
        if (!i && !f.bad) {
            memcpy(g_rooms, g_listing, sizeof g_rooms);
            g_room_count = g_listing_count;
            g_listing_count = 0;
        }
        break;
    case JOINED:
        if (g_stale) { /* the answer to a request given up: a LEAVE follows it */
            g_stale--;
            break;
        }
        g_room.id = get(&f, 4);
        get(&f, 4);
        g_room.flags = (BYTE)get(&f, 1);
        g_room.limit = seats(get(&f, 1));
        get_str8(&f, g_room.name, sizeof g_room.name);
        take(&f, get(&f, 2));
        n = get(&f, 1);
        for (g_player_count = 0; n && !f.bad && g_player_count < MAX_PLAYERS; n--)
            get_player(&f, &g_players[g_player_count++]);
        if (f.bad)
            break;
        g_in_room = TRUE;
        g_head = g_count = 0;
        g_gone_count = 0;
        g_joined = 1;
        SetEvent(g_answer);
        say("network: in room %u \"%s\"%s: %s", g_room.id, g_room.name, g_room.flags & ROOM_CLOSED ? ", closed" : "",
            player_list());
        break;
    case FAILED:
        i = get(&f, 1);
        n = get(&f, 1);
        say("network: the server refused request %u: error %u", i, n);
        if (i != CREATE && i != JOIN)
            break;
        if (g_stale) {
            g_stale--;
        } else {
            g_joined = -1;
            SetEvent(g_answer);
        }
        break;
    case PLAYER_JOINED:
        get_player(&f, &joined);
        if (!f.bad && g_in_room && g_player_count < MAX_PLAYERS && !find_player(joined.id)) {
            g_players[g_player_count++] = joined;
            say("network: %s (%X) joined", joined.name, joined.id);
        }
        break;
    case PLAYER_LEFT:
        id = get(&f, 4);
        if (!f.bad && g_in_room && (pl = find_player(id)) != NULL) {
            say("network: %s (%X) left", pl->name, id);
            /* the order is the server's: the game takes a player's number from his place in the list */
            memmove(pl, pl + 1, (size_t)(&g_players[--g_player_count] - pl) * sizeof *pl);
            gone(id);
            push_system(DPSYS_DESTROYPLAYERORGROUP, id);
        }
        break;
    case PLAYER_DATA:
        id = get(&f, 4);
        n = get(&f, 2);
        body = take(&f, n);
        if (!f.bad && (n == TEAM || !n) && (pl = find_player(id)) != NULL) {
            pl->size = n;
            memcpy(pl->data, body, n);
        }
        break;
    case ROOM_CHANGED:
        n = get(&f, 1);
        if (!f.bad && (n ^ g_room.flags) & ROOM_CLOSED)
            say("network: the host has %s the room", n & ROOM_CLOSED ? "closed" : "opened");
        g_room.flags = (BYTE)n;
        g_room.limit = seats(get(&f, 1));
        break;
    case DATA:
        id = get(&f, 4);
        n = get(&f, 4);
        if (!f.bad && g_in_room) {
            static DWORD said;
            struct message *m;

            size = (DWORD)(f.end - f.p);
            count_message(TRUE, f.p, size);
            if (find_player(id) && game_message(id, f.p, size, MAX_PLAYERS)) {
                if ((m = push_message(id, n, f.p, size)) != NULL)
                    m->data[MESSAGE - 1] = 0; /* chat (type 6) is read as a string */
            } else if (GetTickCount() - said > 1000) {
                said = GetTickCount();
                say("network: a message of %u bytes, type %u, from %X is not one of the game's: dropped", size,
                    size >= 4 ? *(const DWORD *)f.p : 0, id);
            }
        }
        break;
    case PONG:
        if (size >= 4) {
            memcpy(&id, body, 4);
            g_ping = GetTickCount() - id;
            g_ping = g_ping ? g_ping : 1;
        }
        break;
    default: /* OK, and what a later server may add */
        break;
    }
    return !f.bad;
}

/* FALSE: no connection any more, l->error says why. `patient`: a timeout is no error while `epoch` is the
 * current one. Windows 11: a WebSocket read has none, WinHttpSetTimeouts does not reach it. */
static BOOL read_exact(struct link *l, DWORD epoch, BYTE *to, int size, BOOL patient)
{
    int done = 0, n;

    while (done < size) {
        if (l->ws) {
            WINHTTP_WEB_SOCKET_BUFFER_TYPE kind;
            DWORD got = 0, error = WinHttpWebSocketReceive(l->ws, to + done, (DWORD)(size - done), &got, &kind);

            if (error == NO_ERROR && kind != WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
                done += (int)got;
            } else if (error != ERROR_WINHTTP_TIMEOUT || !patient || epoch != g_epoch) {
                l->error = error;
                return FALSE;
            }
        } else if ((n = recv(l->s, (char *)to + done, size - done, 0)) > 0) {
            done += n;
        } else {
            l->error = n ? (DWORD)WSAGetLastError() : 0;
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL open_tcp(struct link *l)
{
    struct addrinfo hints = { 0 }, *found = NULL, *a;
    DWORD timeout = 3000, nodelay = 1;
    u_long mode = 1;
    SOCKET s = INVALID_SOCKET;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(g_server, g_port, &hints, &found)) {
        say("network: no address for the server \"%s\"", g_server);
        return FALSE;
    }
    for (a = found; a && s == INVALID_SOCKET; a = a->ai_next) {
        fd_set writable, failed;
        struct timeval wait = { 3, 0 };

        if ((s = socket(a->ai_family, a->ai_socktype, a->ai_protocol)) == INVALID_SOCKET)
            continue;
        ioctlsocket(s, FIONBIO, &mode); /* a dead address costs 3 s, not 20 */
        connect(s, a->ai_addr, (int)a->ai_addrlen);
        FD_ZERO(&writable);
        FD_SET(s, &writable);
        failed = writable;
        if (select(0, NULL, &writable, &failed, &wait) != 1 || !FD_ISSET(s, &writable)) {
            closesocket(s);
            s = INVALID_SOCKET;
        }
    }
    freeaddrinfo(found);
    if (s == INVALID_SOCKET)
        return FALSE;
    mode = 0;
    ioctlsocket(s, FIONBIO, &mode);
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelay, sizeof nodelay);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof timeout); /* for WELCOME */
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof timeout);
    l->s = s;
    return TRUE;
}

/* WinHTTP does the TLS and follows the Windows proxy settings. */
static BOOL open_web(struct link *l)
{
    WCHAR host[sizeof g_server], path[sizeof g_path];
    HINTERNET request = NULL;
    int i;

    for (i = 0; (host[i] = (BYTE)g_server[i]) != 0; i++)
        ;
    for (i = 0; (path[i] = (BYTE)g_path[i]) != 0; i++)
        ;
    l->host = g_http ? WinHttpConnect(g_http, host, (INTERNET_PORT)atoi(g_port), 0) : NULL;
    if (l->host)
        request = WinHttpOpenRequest(l->host, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     g_web == 2 ? WINHTTP_FLAG_SECURE : 0);
    if (request && WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0) &&
        WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, NULL))
        l->ws = WinHttpWebSocketCompleteUpgrade(request, 0);
    if (request)
        WinHttpCloseHandle(request);
    return l->ws != NULL;
}

/* One per connection: opens it without the lock (a dead server costs seconds), then reads its frames. */
static DWORD WINAPI link_thread(void *arg)
{
    DWORD epoch = (DWORD)(ULONG_PTR)arg, size = 0, forever = 0;
    BYTE head[2], hello[10], *frame = (BYTE *)HeapAlloc(GetProcessHeap(), 0, MAX_FRAME);
    struct link l = { INVALID_SOCKET, NULL, NULL, 0 };
    const char *why = NULL;
    char broke[48];
    BOOL ok = frame && (g_web ? open_web(&l) : open_tcp(&l));

    if (!ok) {
        say("network: the server %s:%s does not answer", g_server, g_port);
    } else {
        memcpy(hello, "HOGS", 4);
        put32(put16(hello + 4, PROTOCOL), g_compat);
        /* WELCOME: u16 size, type, u16 protocol, u32 our id, ... */
        ok = !link_send(&l, HELLO, hello, sizeof hello) && read_exact(&l, epoch, head, 2, FALSE) &&
             (size = head[0] | head[1] << 8) >= 7 && size <= MAX_FRAME && read_exact(&l, epoch, frame, (int)size, FALSE) &&
             frame[0] == WELCOME;
        if (!ok)
            say("network: no WELCOME from %s: another protocol version, or the server is full", g_server);
    }
    EnterCriticalSection(&g_lock);
    if (epoch != g_epoch) {
        ok = FALSE; /* the game closed the network meanwhile */
    } else {
        g_connecting = FALSE;
        g_tried = GetTickCount() | 1;
        if (ok) {
            g_sock = l.s;
            g_ws = l.ws;
            memcpy(&g_id, frame + 3, 4);
            if (!g_name[0])
                sprintf(g_name, "PLAYER %u", (g_id - 0xFF) % 10000);
            if (!g_web) /* reads wait for as long as it takes: shutdown() in drop_connection ends them */
                setsockopt(l.s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&forever, sizeof forever);
            g_pinged = g_heard = GetTickCount();
            g_waiting = 0;
            g_silent = FALSE;
            g_listing_count = g_room_count = 0;
            say("network: connected to %s:%s as \"%s\" (player %X)", g_server, g_port, g_name, g_id);
        }
        SetEvent(g_linked);
    }
    LeaveCriticalSection(&g_lock);
    while (ok && read_exact(&l, epoch, head, 2, TRUE)) {
        size = head[0] | head[1] << 8;
        if (size < 1 || size > MAX_FRAME) {
            why = "a frame of a wrong size";
            break;
        }
        if (!read_exact(&l, epoch, frame, (int)size, TRUE))
            break;
        EnterCriticalSection(&g_lock);
        if (epoch != g_epoch) {
            size = 0; /* dropped: a WebSocket still brings frames until it is closed below */
        } else {
            if (g_silent)
                say("network: the server answers again after %.1f s of silence", (GetTickCount() - g_heard) / 1000.0);
            g_heard = GetTickCount();
            g_silent = FALSE;
            g_waiting = 0;
            if (!on_frame(frame[0], frame + 1, size - 1)) {
                why = "a frame the client does not understand";
                size = 0;
            }
        }
        LeaveCriticalSection(&g_lock);
        if (!size)
            break;
    }
    if (!why) {
        sprintf(broke, l.error ? "the connection broke: error %u" : "the server closed the connection", l.error);
        why = broke;
    }
    EnterCriticalSection(&g_lock);
    if (ok && epoch == g_epoch)
        drop_connection(why);
    if (l.ws && l.ws == g_aborted)
        l.ws = g_aborted = NULL;
    LeaveCriticalSection(&g_lock);
    if (l.ws)
        WinHttpCloseHandle(l.ws);
    if (l.host)
        WinHttpCloseHandle(l.host);
    if (l.s != INVALID_SOCKET)
        closesocket(l.s);
    if (frame)
        HeapFree(GetProcessHeap(), 0, frame);
    EnterCriticalSection(&g_lock);
    g_links--;
    LeaveCriticalSection(&g_lock);
    return 0;
}

/* the game's own limit for a player on turn who sends no input (FUN_0048CEF0, 5 + 10 s) */
enum { STALL_SECONDS = 15 };

/* The server's idle limit and our own check for a dead link need pings also when the game polls
 * nothing: it reads no messages while it loads a level or shows the results. */
static DWORD WINAPI keeper(void *arg)
{
    (void)arg;
    for (;;) {
        DWORD now;

        Sleep(1000);
        EnterCriticalSection(&g_lock);
        now = GetTickCount();
        if (CONNECTED && g_waiting && now - g_waiting > 3 * PING_SECONDS * 1000) {
            drop_connection("no answer to three pings");
        } else if (CONNECTED) {
            if (g_waiting && !g_silent && now - g_waiting > 2000) { /* an answer takes 0.1-0.3 s */
                g_silent = TRUE;
                say("network: the server has been silent for %.1f s", (now - g_heard) / 1000.0);
            }
            if (now - g_pinged >= PING_SECONDS * 1000)
                send_ping();
        }
        if (g_dropped && now - g_drop_said > 1000)
            say_dropped(); /* the end of a burst */
        /* the others wait for this copy's input and cannot tell a stuck game from a slow link: it leaves */
        if (CONNECTED && g_in_room && battle_turn_stall() > STALL_SECONDS * 1000) {
            say("network: the game stood still for %d s on its own turn: the others cannot play on", STALL_SECONDS);
            drop_connection("this player leaves the room");
        }
        LeaveCriticalSection(&g_lock);
    }
}

/* The lock is held. Starts a link thread, unless one runs or has not ended yet, or the last attempt ended
 * under 10 s ago (the menu asks twice a second). g_linked tells when the attempt has ended. */
static void start_connect(void)
{
    static BOOL started;
    WSADATA wsa;
    HANDLE thread;

    if (CONNECTED || g_connecting || g_links || (g_tried && GetTickCount() - g_tried < 10000))
        return;
    if (!started) {
        started = TRUE;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        if (g_web)
            g_http = WinHttpOpen(L"HogsFix", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (g_http) /* for the connection and its upgrade */
            WinHttpSetTimeouts(g_http, 5000, 5000, 5000, 10000);
        if ((thread = CreateThread(NULL, 0, keeper, NULL, 0, NULL)) != NULL)
            CloseHandle(thread);
    }
    ResetEvent(g_linked);
    g_connecting = TRUE;
    thread = CreateThread(NULL, 0, link_thread, (void *)(ULONG_PTR)++g_epoch, 0, NULL);
    if (thread) {
        g_links++;
        CloseHandle(thread);
    } else {
        g_connecting = FALSE;
        g_tried = GetTickCount() | 1;
        SetEvent(g_linked);
    }
}

/* ---- the DirectPlay object ---- */

static HRESULT unsupported(int args)
{
    say("network: a DirectPlay method with %d arguments is not supported", args);
    return DPERR_UNSUPPORTED;
}

#define A DWORD
static HRESULT __stdcall no1(void *self, A a) { (void)self, (void)a; return unsupported(1); }
static HRESULT __stdcall no2(void *self, A a, A b) { (void)self, (void)a, (void)b; return unsupported(2); }
static HRESULT __stdcall no3(void *self, A a, A b, A c) { (void)self, (void)a, (void)b, (void)c; return unsupported(3); }
static HRESULT __stdcall no4(void *self, A a, A b, A c, A d) { (void)self, (void)a, (void)b, (void)c, (void)d; return unsupported(4); }
static HRESULT __stdcall no5(void *self, A a, A b, A c, A d, A e) { (void)self, (void)a, (void)b, (void)c, (void)d, (void)e; return unsupported(5); }
static HRESULT __stdcall no6(void *self, A a, A b, A c, A d, A e, A f) { (void)self, (void)a, (void)b, (void)c, (void)d, (void)e, (void)f; return unsupported(6); }
static HRESULT __stdcall no9(void *self, A a, A b, A c, A d, A e, A f, A g, A h, A i)
{
    (void)self, (void)a, (void)b, (void)c, (void)d, (void)e, (void)f, (void)g, (void)h, (void)i;
    return unsupported(9);
}
#undef A

/* self[1] tells the objects apart: 0 DirectPlay, 1 lobby (also the index into g_refs) */
static ULONG __stdcall add_ref(void **self)
{
    return (ULONG)InterlockedIncrement(&g_refs[self[1] != NULL]);
}

static HRESULT __stdcall query_interface(void **self, const GUID *iid, void **out)
{
    (void)iid;
    add_ref(self);
    *out = self;
    return S_OK;
}

/* The game calls this until it returns 0. */
static ULONG __stdcall release(void **self)
{
    LONG *refs = &g_refs[self[1] != NULL], left = InterlockedDecrement(refs);

    if (left < 0) { /* over-released */
        InterlockedExchange(refs, 0);
        return 0;
    }
    if (!left && !self[1]) {
        EnterCriticalSection(&g_lock);
        leave_room();
        drop_connection("the game closed the network");
        g_tried = 0;
        LeaveCriticalSection(&g_lock);
    }
    return (ULONG)left;
}

static HRESULT __stdcall dp_close(void *self)
{
    (void)self;
    EnterCriticalSection(&g_lock);
    if (g_in_room) {
        say("network: the game leaves room %u", g_room.id);
        send_frame(LEAVE, NULL, 0);
    }
    leave_room();
    LeaveCriticalSection(&g_lock);
    return DP_OK;
}

static HRESULT __stdcall dp_destroy_player(void *self, DWORD id)
{
    (void)id;
    return dp_close(self);
}

static HRESULT __stdcall dp_open(void *self, const SESSION *desc, DWORD flags)
{
    (void)self;
    EnterCriticalSection(&g_lock);
    g_open_create = (flags & DPOPEN_CREATE) != 0;
    g_open_room = desc->guidInstance.Data1;
    g_open_limit = desc->dwMaxPlayers >= 1 && desc->dwMaxPlayers <= MAX_PLAYERS ? desc->dwMaxPlayers : 4;
    g_application = desc->guidApplication;
    LeaveCriticalSection(&g_lock);
    return DP_OK;
}

/* Open only noted the wish: the room is made or joined here, with the player's name. */
static HRESULT __stdcall dp_create_player(void *self, DWORD *id, const NAME *name, HANDLE event, void *data, DWORD size, DWORD flags)
{
    BYTE buf[200], *p = buf;
    char room[64];
    BOOL sent;

    (void)self, (void)name, (void)event, (void)data, (void)size, (void)flags;
    EnterCriticalSection(&g_lock);
    start_connect();
    LeaveCriticalSection(&g_lock);
    WaitForSingleObject(g_linked, 5000); /* the menu connected long before, as a rule */
    EnterCriticalSection(&g_lock);
    if (g_in_room) {
        send_frame(LEAVE, NULL, 0);
        leave_room();
    }
    sent = CONNECTED;
    if (sent && g_open_create) {
        sprintf(room, "%s'S GAME", g_name);
        *p++ = (BYTE)g_open_limit;
        *p++ = 0;
        p = put16(put_str8(p, room), 0); /* no description */
    } else if (sent) {
        p = put32(p, g_open_room);
    }
    g_joined = 0;
    ResetEvent(g_answer);
    sent = sent && send_frame(g_open_create ? CREATE : JOIN, buf, (int)(put_str8(p, g_name) - buf));
    LeaveCriticalSection(&g_lock);
    if (!sent)
        return DPERR_NOCONNECTION;
    WaitForSingleObject(g_answer, 5000);
    EnterCriticalSection(&g_lock);
    sent = g_joined == 1;
    if (!g_joined && CONNECTED) { /* no answer yet: when it comes, it must not put us into the room */
        g_stale++;
        g_joined = -1;
        send_frame(LEAVE, NULL, 0);
    }
    LeaveCriticalSection(&g_lock);
    if (!sent)
        return DPERR_GENERIC;
    *id = g_id;
    return DP_OK;
}

static HRESULT __stdcall dp_enum_connections(void *self, const GUID *application, CONNECTION_CALLBACK callback, void *ctx, DWORD flags)
{
    static BYTE address[16];
    NAME name = { sizeof name, 0, "Lobby server", NULL };

    (void)self, (void)application, (void)flags;
    /* IPX under another name: no IP asked, the list is read again and again (TCP/IP: once) */
    callback(&kIpx, address, sizeof address, &name, 0, ctx);
    return DP_OK;
}

static HRESULT __stdcall dp_initialize_connection(void *self, void *connection, DWORD flags)
{
    (void)self, (void)connection, (void)flags;
    EnterCriticalSection(&g_lock);
    start_connect();
    LeaveCriticalSection(&g_lock);
    return DP_OK;
}

/* Rooms of the last list; asks the server for a fresh one for the next call. */
static HRESULT __stdcall dp_enum_sessions(void *self, const SESSION *wanted, DWORD timeout, SESSION_CALLBACK callback, void *ctx, DWORD flags)
{
    struct room rooms[MAX_ROOMS];
    SESSION desc = { sizeof desc };
    char label[sizeof rooms->name + 24];
    DWORD ping;
    int i, count;

    (void)self, (void)flags;
    EnterCriticalSection(&g_lock);
    start_connect();
    if (send_frame(LIST, NULL, 0) && GetTickCount() - g_pinged > 2000)
        send_ping(); /* for the ping in the list */
    memcpy(rooms, g_rooms, sizeof rooms);
    count = g_room_count;
    ping = g_ping;
    LeaveCriticalSection(&g_lock);
    desc.guidApplication = wanted->guidApplication;
    for (i = 0; i < count; i++) {
        /* the menu shows only the name: players ("2-4") and ping go into it */
        sprintf(label + sprintf(label, "%s %u-%u", rooms[i].name, rooms[i].players, rooms[i].limit),
                ping ? " %uMS" : "", ping > 999 ? 999 : ping);
        desc.dwFlags = rooms[i].flags & ROOM_CLOSED ? DPSESSION_JOINDISABLED : 0;
        desc.guidInstance.Data1 = rooms[i].id;
        desc.dwMaxPlayers = rooms[i].limit;
        desc.dwCurrentPlayers = rooms[i].players;
        desc.lpszSessionNameA = label;
        if (!callback(&desc, &timeout, 0, ctx))
            return DP_OK;
    }
    callback(NULL, &timeout, 1, ctx); /* DPESC_TIMEDOUT: the list is complete */
    return DP_OK;
}

static HRESULT __stdcall dp_enum_players(void *self, const GUID *session, PLAYER_CALLBACK callback, void *ctx, DWORD flags)
{
    char names[MAX_PLAYERS][sizeof g_players->name];
    DWORD ids[MAX_PLAYERS];
    NAME name = { sizeof name, 0, NULL, NULL };
    int i, count;

    (void)self, (void)session, (void)flags;
    EnterCriticalSection(&g_lock);
    count = g_in_room ? g_player_count : 0;
    for (i = 0; i < count; i++) {
        ids[i] = g_players[i].id;
        strcpy(names[i], g_players[i].name);
    }
    LeaveCriticalSection(&g_lock);
    for (i = 0; i < count; i++) {
        name.lpszShortNameA = names[i];
        if (!callback(ids[i], 1, &name, ids[i] == g_id ? 8 : 0x10, ctx)) /* local or remote */
            break;
    }
    return DP_OK;
}

static HRESULT __stdcall dp_get_player_data(void *self, DWORD id, void *data, DWORD *size, DWORD flags)
{
    struct player *pl;
    HRESULT r = DP_OK;

    (void)self, (void)flags;
    EnterCriticalSection(&g_lock);
    pl = find_player(id);
    if (!pl) {
        r = DPERR_GENERIC;
    } else if (!data || *size < pl->size) {
        r = pl->size ? DPERR_BUFFERTOOSMALL : DP_OK;
        *size = pl->size;
    } else {
        memcpy(data, pl->data, pl->size);
        *size = pl->size;
    }
    LeaveCriticalSection(&g_lock);
    return r;
}

/* the game sets the data every second; the server gets it only on a change */
static HRESULT __stdcall dp_set_player_data(void *self, DWORD id, const void *data, DWORD size, DWORD flags)
{
    BYTE buf[2 + TEAM];
    struct player *pl;

    (void)self;
    if (flags & DPSET_LOCAL || (size && (size != TEAM || !data)))
        return DP_OK;
    EnterCriticalSection(&g_lock);
    pl = id == g_id ? find_player(id) : NULL;
    if (pl && (pl->size != size || memcmp(pl->data, data, size))) {
        pl->size = size;
        memcpy(pl->data, data, size);
        memcpy(put16(buf, size), data, size);
        send_frame(SET_PLAYER, buf, (int)size + 2);
    }
    LeaveCriticalSection(&g_lock);
    return DP_OK;
}

static HRESULT __stdcall dp_get_session_desc(void *self, void *data, DWORD *size)
{
    SESSION *desc = (SESSION *)data;
    DWORD need;
    HRESULT r = DP_OK;

    (void)self;
    EnterCriticalSection(&g_lock);
    need = sizeof *desc + (DWORD)strlen(g_room.name) + 1;
    if (!g_in_room) {
        r = DPERR_NOCONNECTION;
    } else if (!data || *size < need) {
        r = DPERR_BUFFERTOOSMALL;
    } else {
        memset(desc, 0, sizeof *desc);
        desc->dwSize = sizeof *desc;
        desc->dwFlags = g_room.flags & ROOM_CLOSED ? DPSESSION_JOINDISABLED : 0;
        desc->guidInstance.Data1 = g_room.id;
        desc->guidApplication = g_application;
        desc->dwMaxPlayers = g_room.limit;
        desc->dwCurrentPlayers = (DWORD)g_player_count;
        desc->lpszSessionNameA = strcpy((char *)(desc + 1), g_room.name);
    }
    *size = need;
    LeaveCriticalSection(&g_lock);
    return r;
}

static HRESULT __stdcall dp_set_session_desc(void *self, const SESSION *desc, DWORD flags)
{
    BYTE buf[4];

    (void)self, (void)flags;
    EnterCriticalSection(&g_lock);
    if (g_in_room) {
        if ((desc->dwFlags & DPSESSION_JOINDISABLED ? ROOM_CLOSED : 0) != (g_room.flags & ROOM_CLOSED))
            say("network: the game %s the room", g_room.flags & ROOM_CLOSED ? "opens" : "closes");
        g_room.flags = desc->dwFlags & DPSESSION_JOINDISABLED ? ROOM_CLOSED : 0;
        buf[0] = g_room.flags;
        buf[1] = g_room.limit;
        put16(buf + 2, 0);
        send_frame(SET_ROOM, buf, 4);
    }
    LeaveCriticalSection(&g_lock);
    return DP_OK;
}

/* a failure makes the game show a message box per message; loss is "session lost" */
static HRESULT __stdcall dp_send(void *self, DWORD from, DWORD to, DWORD flags, const void *data, DWORD size)
{
    BYTE buf[4 + MESSAGE];

    (void)self, (void)from, (void)flags;
    if (size != MESSAGE)
        return DPERR_GENERIC;
    memcpy(put32(buf, to), data, size);
    EnterCriticalSection(&g_lock);
    count_message(FALSE, (const BYTE *)data, size);
    if (g_in_room)
        send_frame(SEND, buf, (int)size + 4);
    LeaveCriticalSection(&g_lock);
    return DP_OK;
}

static HRESULT __stdcall dp_receive(void *self, DWORD *from, DWORD *to, DWORD flags, void *data, DWORD *size)
{
    struct message *m;
    HRESULT r = DP_OK;
    BOOL queued;

    (void)self, (void)flags;
    EnterCriticalSection(&g_lock);
    if (FRONTEND)
        g_gone_count = 0;
    /* checked here, not when it came: the game's list can have fewer players than the room by now */
    g_listed = game_players();
    m = loaded_for_gone();
    queued = !m;
    for (; queued && g_count; g_head = (g_head + 1) % QUEUE, g_count--) {
        DWORD head[2];

        m = &g_queue[g_head];
        if (!m->from || game_message(m->from, m->data, m->size, g_listed))
            break;
        memcpy(head, m->data, 8); /* game_message has checked the size */
        g_drop_from = m->from;
        g_drop_type = head[0];
        g_drop_place = (short)head[1];
        if (head[0] == 4 && head[1] < 15)
            memcpy(&g_drop_place, m->data + 16 + head[1] * 0x14, 2);
        g_dropped++;
        if (GetTickCount() - g_drop_said > 1000)
            say_dropped();
    }
    if (queued)
        m = g_count ? &g_queue[g_head] : NULL;
    if (!m) {
        r = DPERR_NOMESSAGES;
    } else if (!data || *size < m->size) {
        *size = m->size;
        r = DPERR_BUFFERTOOSMALL;
    } else {
        memcpy(data, m->data, m->size);
        *from = m->from;
        *to = m->to;
        *size = m->size;
        if (queued) {
            g_head = (g_head + 1) % QUEUE;
            g_count--;
        }
    }
    LeaveCriticalSection(&g_lock);
    return r;
}

/* IDirectPlay4A: 53 methods in this order */
static void *const kDirectPlayMethods[53] = {
    query_interface, add_ref, release,
    no2 /* AddPlayerToGroup */, dp_close, no5 /* CreateGroup */, dp_create_player, no2 /* DeletePlayerFromGroup */,
    no1 /* DestroyGroup */, dp_destroy_player, no5 /* EnumGroupPlayers */, no4 /* EnumGroups */, dp_enum_players,
    dp_enum_sessions, no2 /* GetCaps */, no4 /* GetGroupData */, no3 /* GetGroupName */, no2 /* GetMessageCount */,
    no3 /* GetPlayerAddress */, no3 /* GetPlayerCaps */, dp_get_player_data, no3 /* GetPlayerName */, dp_get_session_desc,
    no1 /* Initialize */, dp_open, dp_receive, dp_send, no4 /* SetGroupData */, no3 /* SetGroupName */, dp_set_player_data,
    no3 /* SetPlayerName */, dp_set_session_desc,
    no2 /* AddGroupToGroup */, no6 /* CreateGroupInGroup */, no2 /* DeleteGroupFromGroup */, dp_enum_connections,
    no5 /* EnumGroupsInGroup */, no4 /* GetGroupConnectionSettings */, dp_initialize_connection, no4 /* SecureOpen */,
    no4 /* SendChatMessage */, no3 /* SetGroupConnectionSettings */, no2 /* StartSession */, no2 /* GetGroupFlags */,
    no2 /* GetGroupParent */, no4 /* GetPlayerAccount */, no2 /* GetPlayerFlags */,
    no2 /* GetGroupOwner */, no2 /* SetGroupOwner */, no9 /* SendEx */, no5 /* GetMessageQueue */, no2 /* CancelMessage */,
    no3 /* CancelPriority */,
};

/* ---- the lobby object: address only ---- */

static HRESULT __stdcall lobby_compound_address(void *self, const void *elements, DWORD count, void *address, DWORD *size)
{
    (void)self, (void)elements, (void)count;
    if (!address || *size < 16) {
        *size = 16;
        return DPERR_BUFFERTOOSMALL;
    }
    memset(address, 0, 16);
    *size = 16;
    return DP_OK;
}

static HRESULT __stdcall lobby_wait(void *self, DWORD flags)
{
    (void)self, (void)flags;
    return DP_OK;
}

/* IDirectPlayLobby3A: 19 methods in this order */
static void *const kLobbyMethods[19] = {
    query_interface, add_ref, release,
    no3 /* Connect */, no6 /* CreateAddress */, no4 /* EnumAddress */, no4 /* EnumAddressTypes */,
    no3 /* EnumLocalApplications */, no3 /* GetConnectionSettings */, no5 /* ReceiveLobbyMessage */, no4 /* RunApplication */,
    no4 /* SendLobbyMessage */, no3 /* SetConnectionSettings */, no3 /* SetLobbyMessageEvent */, lobby_compound_address,
    no4 /* ConnectEx */, no2 /* RegisterApplication */, no2 /* UnregisterApplication */, lobby_wait,
};

static const void *g_dplay[2] = { kDirectPlayMethods, NULL };
static const void *g_lobby[2] = { kLobbyMethods, (void *)1 };

static HRESULT(WINAPI *g_co_create_instance)(const GUID *, void *, DWORD, const GUID *, void **);

static HRESULT WINAPI co_create_instance(const GUID *clsid, void *outer, DWORD context, const GUID *iid, void **out)
{
    int lobby = !memcmp(clsid, &kDirectPlayLobby, sizeof *clsid);

    if (!lobby && memcmp(clsid, &kDirectPlay, sizeof *clsid))
        return g_co_create_instance(clsid, outer, context, iid, out);
    InterlockedIncrement(&g_refs[lobby]);
    *out = lobby ? (void *)g_lobby : (void *)g_dplay;
    return S_OK;
}

/* Menu 47 (FUN_00428CF0) draws the prompt as line 0 (0x429892) and the typed address as line 1 (0x42985E).
 * One provider is one line: it shows the address once a character is typed. esi: the line. */
static __declspec(naked) void address_line(void)
{
    __asm {
        test esi, esi
        jnz other
        mov eax, dword ptr ds:[0x51AAD8]
        cmp dword ptr [eax], 1
        jne prompt
        cmp byte ptr ds:[0x51ABD0], 0
        jne address
    prompt:
        mov eax, 0x429892
        jmp eax
    other:
        dec esi
        jz address
        mov eax, 0x4290BD
        jmp eax
    address:
        mov eax, 0x42985E
        jmp eax
    }
}

/* FUN_004307B0 takes a battle's sound volumes from the team in slot 0 ([0x51C55C] + 8, 9, 0xC). In a network
 * battle that is the host's team in every copy (FUN_0042EC80); this copy's own team is in the slot of its place. */
static void __cdecl battle_sound(void)
{
    BYTE *net = NET_OBJECT, *first = *(BYTE **)0x51C55C, *own = NULL, kept[5];

    if (NETWORK_BATTLE && net && first && (WORD)*(short *)(net + 0x2FC) < MAX_PLAYERS)
        own = (BYTE *)0x51F128 + *(short *)(net + 0x2FC) * 0x2A8;
    if (own) {
        memcpy(kept, first + 8, sizeof kept);
        memcpy(first + 8, own + 8, sizeof kept);
    }
    ((void(__cdecl *)(void))0x4307B0)();
    if (own)
        memcpy(first + 8, kept, sizeof kept);
}

void net_init(void)
{
    char name[32], *colon, *slash;
    DWORD hash = 2166136261u;
    int i;

    /* The game's own network play (Server empty). FUN_0042E7E0 takes this address when none is typed;
     * DirectPlay searches the local network when the address is empty. */
    patch(0x4C0E9C, "10.11.11.116", "\0" "0.11.11.116", 12, "network: empty address searches the LAN");
    hook(0x429852, 0xE9, "\x83\xEE\x00\x74\x3B", address_line, "network: the typed address is shown");
    hook(0x47E2B2, 0xE8, "\xE8\xF9\x24\xFB\xFF", battle_sound, "network: own sound volumes in a battle");
    GetPrivateProfileStringA("Network", "Server", "", g_server, sizeof g_server, g_ini);
    if (!g_server[0])
        return;
    /* name[:port] is TCP; ws://name[:port][/path] and wss://... are WebSocket */
    g_web = !_strnicmp(g_server, "wss://", 6) ? 2 : !_strnicmp(g_server, "ws://", 5) ? 1 : 0;
    if (g_web)
        memmove(g_server, g_server + 4 + g_web, strlen(g_server + 4 + g_web) + 1);
    strcpy(g_path, "/");
    if (g_web && (slash = strchr(g_server, '/')) != NULL) {
        strcpy(g_path, slash);
        *slash = 0;
    }
    sprintf(g_port, "%d", g_web == 2 ? 443 : g_web ? 80 : PORT);
    if ((colon = strrchr(g_server, ':')) != NULL && strchr(g_server, ':') == colon) {
        *colon = 0; /* name:port; more colons = IPv6, default port */
        lstrcpynA(g_port, colon + 1, sizeof g_port);
    }
    GetPrivateProfileStringA("Network", "Name", "", name, sizeof name, g_ini);
    for (i = 0; name[i] && i < (int)sizeof g_name - 1; i++)
        g_name[i] = name_char(name[i]);
    /* games meet only with the same NET_VERSION and battle settings */
    g_compat = NET_VERSION << 8 | (GetPrivateProfileIntA("Battle", "ComputerWalks", 1, g_ini) ? 1 : 0);
    /* A password is part of that number too: players with another one do not see the game.
     * ponytail: the server lists every room with its number, so a client written for it finds a
     * password's games; a real lock is a password that the server checks (a protocol change). */
    GetPrivateProfileStringA("Network", "Password", "", name, sizeof name, g_ini);
    for (i = 0; name[i]; i++)
        hash = (hash ^ (BYTE)(name[i] >= 'a' && name[i] <= 'z' ? name[i] - 0x20 : name[i])) * 16777619u;
    if (name[0])
        g_compat |= hash >> 16 ? hash & 0xFFFF0000 : 0x10000;
    g_net_stats = GetPrivateProfileIntA("Debug", "NetStats", 0, g_ini);
    InitializeCriticalSection(&g_lock);
    g_answer = CreateEventA(NULL, TRUE, FALSE, NULL);
    g_linked = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (hook_import(0x54F698, "ole32.dll", "CoCreateInstance", co_create_instance, &g_co_create_instance,
                    "network: games through the lobby server")) {
        patch(0x4C0E24, "Unkown SP!!!", "- INTERNET -", 12, "network: the server in the provider list");
        /* FUN_0042EDF0, menu 48: add esi, 500 = the next list in 5 s; 50 shows a new game at once */
        patch(0x42F01C, "\x81\xC6\xF4\x01\x00\x00", "\x81\xC6\x32\x00\x00\x00", 6, "network: game list twice a second");
    }
}
