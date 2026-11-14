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
 *   SetPlayerData, GetPlayerData   the team, 680 bytes, set every 100 ms
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

#define NET_VERSION 1 /* raise when a release changes a battle: such games must not meet */

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
enum { MAX_ROOMS = 16, MAX_PLAYERS = 8, MAX_DATA = 1024, MAX_MESSAGE = 512, QUEUE = 512 };

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
    BYTE data[MAX_DATA];
};

struct message {
    DWORD from, to, size;
    BYTE data[MAX_MESSAGE];
};

static const GUID kDirectPlay = { 0xD1EB6D20, 0x8923, 0x11D0, { 0x9D, 0x97, 0x00, 0xA0, 0xC9, 0x0A, 0x43, 0xCB } };
static const GUID kDirectPlayLobby = { 0x2FE8F810, 0xB2A5, 0x11D0, { 0xA7, 0x87, 0x00, 0x00, 0xF8, 0x03, 0xAB, 0xFC } };
static const GUID kIpx = { 0x685BC400, 0x9D2C, 0x11CF, { 0xA9, 0xCD, 0x00, 0xAA, 0x00, 0x68, 0x86, 0xE3 } };

static char g_server[128], g_port[8], g_path[128], g_name[16];
static int g_web;            /* WebSocket server: 1 ws://, 2 wss:// */
static DWORD g_compat;
static CRITICAL_SECTION g_lock; /* guards everything below and whole frames on the connection */
static SOCKET g_sock = INVALID_SOCKET;
static HINTERNET g_ws, g_ws_host, g_http;
#define CONNECTED (g_sock != INVALID_SOCKET || g_ws)
static DWORD g_epoch;        /* counts connections; a reader thread belongs to one */
static DWORD g_tried, g_sent; /* ticks: last connection attempt, last frame sent */
static DWORD g_heard, g_pinged; /* ticks: last frame received, last PING */
static DWORD g_ping;         /* round trip in ms, 0 = not measured */
static DWORD g_id;
static HANDLE g_answer;      /* CREATE or JOIN answered, or connection lost */
static int g_joined;         /* the answer: 1 in the room, -1 refused */
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

static void drop_connection(const char *why)
{
    if (!CONNECTED)
        return;
    say("network: connection to %s closed: %s", g_server, why);
    if (g_ws) {
        WinHttpCloseHandle(g_ws);
        WinHttpCloseHandle(g_ws_host);
        g_ws = g_ws_host = NULL;
    } else {
        closesocket(g_sock);
        g_sock = INVALID_SOCKET;
    }
    g_room_count = 0;
    g_ping = 0;
}

/* The lock is held. */
static BOOL send_frame(BYTE type, const BYTE *payload, int size)
{
    BYTE buf[3 + 2100];
    int done = 0, n;

    if (!CONNECTED || size > (int)sizeof buf - 3)
        return FALSE;
    put16(buf, (DWORD)size + 1);
    buf[2] = type;
    memcpy(buf + 3, payload, (size_t)size);
    size += 3;
    if (g_ws) {
        if (WinHttpWebSocketSend(g_ws, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE, buf, (DWORD)size) == NO_ERROR)
            done = size;
    } else {
        while (done < size && (n = send(g_sock, (const char *)buf + done, size - done, 0)) > 0)
            done += n;
    }
    if (done < size) {
        drop_connection("cannot send");
        return FALSE;
    }
    g_sent = GetTickCount();
    return TRUE;
}

static void send_ping(void)
{
    DWORD now = GetTickCount();

    g_pinged = now;
    send_frame(PING, (const BYTE *)&now, sizeof now);
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
            say("network: sent %u %u %u %u %u %u %u %u +%u (input %u..%u), received %u %u %u %u %u %u %u %u +%u, ping %u",
                c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8], seq[0], seq[1],
                c[9], c[10], c[11], c[12], c[13], c[14], c[15], c[16], c[17], g_ping);
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

static void push_message(DWORD from, DWORD to, const BYTE *data, DWORD size)
{
    struct message *m;

    if (size > MAX_MESSAGE || g_count == QUEUE) {
        say("network: a message of %u bytes from %X is lost%s", size, from, g_count == QUEUE ? " (the queue is full)" : "");
        return;
    }
    m = &g_queue[(g_head + g_count++) % QUEUE];
    m->from = from;
    m->to = to;
    m->size = size;
    memcpy(m->data, data, size);
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

static void leave_room(void)
{
    g_in_room = FALSE;
    g_player_count = 0;
    g_head = g_count = 0;
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
    if (pl->size > MAX_DATA)
        f->bad = TRUE;
    else if (p)
        memcpy(pl->data, p, pl->size);
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
            r.limit = (BYTE)get(&f, 1);
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
        g_room.id = get(&f, 4);
        get(&f, 4);
        g_room.flags = (BYTE)get(&f, 1);
        g_room.limit = (BYTE)get(&f, 1);
        get_str8(&f, g_room.name, sizeof g_room.name);
        take(&f, get(&f, 2));
        n = get(&f, 1);
        for (g_player_count = 0; n && !f.bad && g_player_count < MAX_PLAYERS; n--)
            get_player(&f, &g_players[g_player_count++]);
        if (f.bad)
            break;
        g_in_room = TRUE;
        g_head = g_count = 0;
        g_joined = 1;
        SetEvent(g_answer);
        say("network: in room %u \"%s\" with %d players", g_room.id, g_room.name, g_player_count);
        break;
    case FAILED:
        i = get(&f, 1);
        n = get(&f, 1);
        say("network: the server refused request %u: error %u", i, n);
        if (i == CREATE || i == JOIN) {
            g_joined = -1;
            SetEvent(g_answer);
        }
        break;
    case PLAYER_JOINED:
        get_player(&f, &joined);
        if (!f.bad && g_in_room && g_player_count < MAX_PLAYERS && !find_player(joined.id)) {
            g_players[g_player_count++] = joined;
            say("network: %s joined", joined.name);
        }
        break;
    case PLAYER_LEFT:
        id = get(&f, 4);
        if (!f.bad && g_in_room && (pl = find_player(id)) != NULL) {
            say("network: %s left", pl->name);
            *pl = g_players[--g_player_count];
            push_system(DPSYS_DESTROYPLAYERORGROUP, id);
        }
        break;
    case PLAYER_DATA:
        id = get(&f, 4);
        n = get(&f, 2);
        body = take(&f, n);
        if (!f.bad && n <= MAX_DATA && (pl = find_player(id)) != NULL) {
            pl->size = n;
            memcpy(pl->data, body, n);
        }
        break;
    case ROOM_CHANGED:
        g_room.flags = (BYTE)get(&f, 1);
        g_room.limit = (BYTE)get(&f, 1);
        break;
    case DATA:
        id = get(&f, 4);
        n = get(&f, 4);
        if (!f.bad && g_in_room) {
            count_message(TRUE, f.p, (DWORD)(f.end - f.p));
            push_message(id, n, f.p, (DWORD)(f.end - f.p));
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

struct link {
    SOCKET s;
    HINTERNET ws;
};

/* `patient`: a quiet server is no error. FALSE: no connection. */
static BOOL read_exact(const struct link *l, BYTE *to, int size, BOOL patient)
{
    int done = 0, n;

    while (done < size) {
        BOOL quiet;

        if (l->ws) {
            WINHTTP_WEB_SOCKET_BUFFER_TYPE kind;
            DWORD got = 0, error = WinHttpWebSocketReceive(l->ws, to + done, (DWORD)(size - done), &got, &kind);

            if (error == NO_ERROR && kind != WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
                done += (int)got;
                continue;
            }
            quiet = error == ERROR_WINHTTP_TIMEOUT;
        } else {
            if ((n = recv(l->s, (char *)to + done, size - done, 0)) > 0) {
                done += n;
                continue;
            }
            quiet = n < 0 && WSAGetLastError() == WSAETIMEDOUT;
        }
        if (!quiet || !patient)
            return FALSE;
        /* the server drops a silent client: keep the connection alive */
        EnterCriticalSection(&g_lock);
        if ((l->ws ? g_ws == l->ws : g_sock == l->s) && GetTickCount() - g_sent > 20000)
            send_ping();
        LeaveCriticalSection(&g_lock);
    }
    return TRUE;
}

/* The reader thread, one per connection. */
static DWORD WINAPI reader(void *arg)
{
    DWORD epoch = (DWORD)(ULONG_PTR)arg, size;
    BYTE head[2], *frame = (BYTE *)HeapAlloc(GetProcessHeap(), 0, MAX_FRAME);
    BOOL ok = frame != NULL;
    struct link l = { INVALID_SOCKET, NULL };

    EnterCriticalSection(&g_lock);
    if (epoch == g_epoch) {
        l.s = g_sock;
        l.ws = g_ws;
    }
    LeaveCriticalSection(&g_lock);
    while (ok && (l.ws || l.s != INVALID_SOCKET) && read_exact(&l, head, 2, TRUE)) {
        size = head[0] | head[1] << 8;
        if (size < 1 || size > MAX_FRAME || !read_exact(&l, frame, (int)size, TRUE))
            break;
        EnterCriticalSection(&g_lock);
        if (epoch != g_epoch) {
            LeaveCriticalSection(&g_lock);
            break;
        }
        g_heard = GetTickCount();
        ok = on_frame(frame[0], frame + 1, size - 1);
        if (GetTickCount() - g_sent > 20000)
            send_ping();
        LeaveCriticalSection(&g_lock);
    }
    HeapFree(GetProcessHeap(), 0, frame);
    EnterCriticalSection(&g_lock);
    if (epoch == g_epoch) {
        drop_connection(ok ? "the server is gone" : "a frame the client does not understand");
        if (g_in_room) {
            leave_room();
            push_system(DPSYS_SESSIONLOST, 0);
        }
        g_joined = -1;
        SetEvent(g_answer);
    }
    LeaveCriticalSection(&g_lock);
    return 0;
}

static SOCKET open_tcp(void)
{
    static BOOL started;
    struct addrinfo hints = { 0 }, *found = NULL, *a;
    DWORD timeout = 3000, nodelay = 1;
    u_long mode = 1;
    WSADATA wsa;
    SOCKET s = INVALID_SOCKET;

    if (!started && WSAStartup(MAKEWORD(2, 2), &wsa))
        return INVALID_SOCKET;
    started = TRUE;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(g_server, g_port, &hints, &found)) {
        say("network: no address for the server \"%s\"", g_server);
        return INVALID_SOCKET;
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
        return s;
    mode = 0;
    ioctlsocket(s, FIONBIO, &mode);
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelay, sizeof nodelay);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof timeout);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof timeout);
    return s;
}

/* WinHTTP does the TLS and follows the Windows proxy settings. */
static BOOL open_web(void)
{
    WCHAR host[sizeof g_server], path[sizeof g_path];
    HINTERNET request = NULL;
    int i;

    for (i = 0; (host[i] = (BYTE)g_server[i]) != 0; i++)
        ;
    for (i = 0; (path[i] = (BYTE)g_path[i]) != 0; i++)
        ;
    if (!g_http) {
        g_http = WinHttpOpen(L"HogsFix", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (g_http) /* a read returns after 10 s of silence, as on TCP */
            WinHttpSetTimeouts(g_http, 5000, 5000, 5000, 10000);
    }
    g_ws_host = g_http ? WinHttpConnect(g_http, host, (INTERNET_PORT)atoi(g_port), 0) : NULL;
    if (g_ws_host)
        request = WinHttpOpenRequest(g_ws_host, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     g_web == 2 ? WINHTTP_FLAG_SECURE : 0);
    if (request && WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0) &&
        WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, NULL))
        g_ws = WinHttpWebSocketCompleteUpgrade(request, 0);
    if (request)
        WinHttpCloseHandle(request);
    if (!g_ws && g_ws_host) {
        WinHttpCloseHandle(g_ws_host);
        g_ws_host = NULL;
    }
    return g_ws != NULL;
}

/* Blocks a few seconds; retries every 10 s (the menu calls twice a second). The lock is held. */
static BOOL connect_server(void)
{
    BYTE hello[13], answer[80];
    DWORD timeout = 10000, size;
    struct link l;
    HANDLE thread;

    if (CONNECTED)
        return TRUE;
    if (g_tried && GetTickCount() - g_tried < 10000)
        return FALSE;
    g_tried = GetTickCount() | 1;
    if (g_web ? !open_web() : (g_sock = open_tcp()) == INVALID_SOCKET) {
        say("network: the server %s:%s does not answer", g_server, g_port);
        return FALSE;
    }
    l.s = g_sock;
    l.ws = g_ws;
    memcpy(hello, "HOGS", 4);
    put32(put16(hello + 4, PROTOCOL), g_compat);
    /* WELCOME: u16 size, type, u16 protocol, u32 our id, ... */
    if (!send_frame(HELLO, hello, 10) || !read_exact(&l, answer, 2, FALSE) ||
        (size = answer[0] | answer[1] << 8) < 7 || size > sizeof answer ||
        !read_exact(&l, answer, (int)size, FALSE) || answer[0] != WELCOME) {
        drop_connection("no WELCOME: another protocol version, or the server is full");
        return FALSE;
    }
    memcpy(&g_id, answer + 3, 4);
    if (!g_name[0])
        sprintf(g_name, "PLAYER %u", (g_id - 0xFF) % 10000);
    if (!g_web) /* 10 s of silence ends a recv; the reader then sends a PING */
        setsockopt(g_sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof timeout);
    g_heard = g_pinged = GetTickCount();
    g_listing_count = g_room_count = 0;
    thread = CreateThread(NULL, 0, reader, (void *)(ULONG_PTR)++g_epoch, 0, NULL);
    if (!thread) {
        drop_connection("no thread");
        return FALSE;
    }
    CloseHandle(thread);
    say("network: connected to %s:%s as \"%s\" (player %X)", g_server, g_port, g_name, g_id);
    return TRUE;
}

/* ---- the DirectPlay object ---- */

static HRESULT unsupported(int args)
{
    say("network: a DirectPlay method with %d arguments is not supported", args);
    return DPERR_UNSUPPORTED;
}

#define A DWORD
static HRESULT __stdcall no0(void *self) { (void)self; return unsupported(0); }
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
        g_epoch++;
        g_tried = 0;
        LeaveCriticalSection(&g_lock);
    }
    return (ULONG)left;
}

static HRESULT __stdcall dp_close(void *self)
{
    (void)self;
    EnterCriticalSection(&g_lock);
    if (g_in_room)
        send_frame(LEAVE, NULL, 0);
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
    if (g_in_room) {
        send_frame(LEAVE, NULL, 0);
        leave_room();
    }
    sent = connect_server();
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
    if (g_joined != 1)
        return DPERR_GENERIC;
    *id = g_id;
    return DP_OK;
}

static HRESULT __stdcall dp_enum_connections(void *self, const GUID *application, CONNECTION_CALLBACK callback, void *ctx, DWORD flags)
{
    static BYTE address[16];
    NAME name = { sizeof name, 0, "Lobby server", NULL };

    (void)self, (void)application, (void)flags;
    /* IPX under another name: no IP asked, the list is read twice a second (TCP/IP: once) */
    callback(&kIpx, address, sizeof address, &name, 0, ctx);
    return DP_OK;
}

static HRESULT __stdcall dp_initialize_connection(void *self, void *connection, DWORD flags)
{
    (void)self, (void)connection, (void)flags;
    EnterCriticalSection(&g_lock);
    connect_server();
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
    if (connect_server()) {
        send_frame(LIST, NULL, 0);
        if (GetTickCount() - g_pinged > 2000)
            send_ping();
    }
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
    static struct player players[MAX_PLAYERS]; /* the menu thread only */
    NAME name = { sizeof name, 0, NULL, NULL };
    int i, count;

    (void)self, (void)session, (void)flags;
    EnterCriticalSection(&g_lock);
    memcpy(players, g_players, sizeof players);
    count = g_in_room ? g_player_count : 0;
    LeaveCriticalSection(&g_lock);
    for (i = 0; i < count; i++) {
        name.lpszShortNameA = players[i].name;
        if (!callback(players[i].id, 1, &name, players[i].id == g_id ? 8 : 0x10, ctx)) /* local or remote */
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

/* the game sets the data every 100 ms; the server gets it only on a change */
static HRESULT __stdcall dp_set_player_data(void *self, DWORD id, const void *data, DWORD size, DWORD flags)
{
    BYTE buf[2 + MAX_DATA];
    struct player *pl;

    (void)self;
    if (flags & DPSET_LOCAL || size > MAX_DATA || (size && !data))
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
    BYTE buf[4 + MAX_MESSAGE];

    (void)self, (void)from, (void)flags;
    if (!size || size > MAX_MESSAGE)
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

    (void)self, (void)flags;
    EnterCriticalSection(&g_lock);
    /* a waiting host hears nothing: a PING every 10 s keeps reads from timing out */
    if (CONNECTED && GetTickCount() - g_heard > 10000 && GetTickCount() - g_pinged > 10000)
        send_ping();
    m = &g_queue[g_head];
    if (!g_count) {
        r = DPERR_NOMESSAGES;
    } else if (!data || *size < m->size) {
        *size = m->size;
        r = DPERR_BUFFERTOOSMALL;
    } else {
        memcpy(data, m->data, m->size);
        *from = m->from;
        *to = m->to;
        *size = m->size;
        g_head = (g_head + 1) % QUEUE;
        g_count--;
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

void net_init(void)
{
    char name[32], *colon, *slash;
    DWORD hash = 2166136261u;
    int i;

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
    if (hook_import(0x54F698, "ole32.dll", "CoCreateInstance", co_create_instance, &g_co_create_instance,
                    "network: games through the lobby server"))
        patch(0x4C0E24, "Unkown SP!!!", "- INTERNET -", 12, "network: the server in the provider list");
}
