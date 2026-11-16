/* ace.c - the Ace's own look: the Commando's head on the Legend's body, the tenth look of a pig.
 * _d3d.dll afLoadAnimModels reads chars\british.mad: 3 files a model (.VTX .NO2 .FAC), 3 models a look (near, middle, far),
 * as many as its list has. The table has room for 10 looks, the game has 9. All looks share skeleton pig.hir: bone 2 is the head.
 *   list: name[16], offset, size; the first offset is the size of the list
 *   VTX: short x, y, z, bone.  NO2: float x, y, z, bone.  Corner k is vertex k and normal k
 *   FAC: 16 bytes, count, triangles of 32 bytes, count, quads of 36; a face: 2 bytes a corner, then the corner numbers, twice
 *   0x4407E0  call in the pig's constructor FUN_00466950: the model of a battle pig     -> pig_model
 *   0x441240  jmp, FUN_00441240(pig, mood): the eyes of a pig                           -> eyes
 *   0x4412D0  jmp, FUN_004412D0(pig, mood): the mouth of a pig                          -> mouth
 *   0x480B9F  call of afDrawAnimModel in FUN_004808C0: the pig of a menu                -> draw_menu_pig */
#include "hogs.h"
#include <string.h>

enum { HEAD = 2, FILES = 81, MOST = 1024 /* corners of a model: the largest has 668 */ };
enum { LEGEND_LOOK = 0, HERO_LOOK = 3, COMMANDO_LOOK = 6, ACE_LOOK = 9 };

#define LOOK_OF_CLASS ((short *)0x4C2E50) /* 17 classes; renderer model = look * 3, +0..2 by distance */

typedef struct {
    char name[16];
    DWORD at, size;
} file_t;

typedef struct {
    short x, y, z;
    WORD bone;
} vertex_t;

BOOL g_ace_look;
static BYTE *g_renderer;

/* Copies the faces with a corner on the head (head TRUE) or with none; to[]: the new number of each corner */
static int faces(BYTE *out, const BYTE *in, int count, int corners, const vertex_t *v, int vertices, const WORD *to, BOOL head)
{
    int size = corners == 3 ? 32 : 36, made = 0, c;

    for (; count > 0; count--, in += size) {
        const WORD *corner = (const WORD *)(in + corners * 2);
        BOOL has = FALSE;

        for (c = 0; c < corners && corner[c] < vertices; c++)
            has |= v[corner[c]].bone == HEAD;
        if (c < corners || has != head)
            continue;
        memcpy(out, in, size);
        for (c = 0; c < corners; c++)
            ((WORD *)(out + corners * 2))[c] = ((WORD *)(out + corners * 4))[c] = to[corner[c]];
        out += size;
        made++;
    }
    return made;
}

/* One Ace model at out: the body but for its head, then the head. Neck faces have body corners too: each goes to
 * the nearest corner of the new body on the same bone (the two necks are 9 units apart at most). NULL: bad files */
static BYTE *join(const BYTE *mad, const file_t *body, const file_t *head, const BYTE *start, BYTE *out, file_t *line)
{
    static WORD bto[MOST], hto[MOST];
    const vertex_t *bv = (const vertex_t *)(mad + body[0].at), *hv = (const vertex_t *)(mad + head[0].at);
    const float (*bn)[4] = (const float (*)[4])(mad + body[1].at), (*hn)[4] = (const float (*)[4])(mad + head[1].at);
    const BYTE *bt = mad + body[2].at + 20, *ht = mad + head[2].at + 20, *bq, *hq;
    int bcount = body[0].size / 8, hcount = head[0].size / 8, count = 0, i, j, n;
    vertex_t *v = (vertex_t *)out;
    float (*normal)[4];
    BYTE *fac, *p;

    if (bcount > MOST || hcount > MOST || body[1].size != (DWORD)bcount * 16 || head[1].size != (DWORD)hcount * 16 || body[2].size < 24 ||
        head[2].size < 24 || *(const DWORD *)(bt - 4) > (body[2].size - 24) / 32 || *(const DWORD *)(ht - 4) > (head[2].size - 24) / 32)
        return NULL;
    bq = bt + *(const int *)(bt - 4) * 32 + 4;
    hq = ht + *(const int *)(ht - 4) * 32 + 4;
    if (*(const DWORD *)(bq - 4) > body[2].size / 36 || *(const DWORD *)(hq - 4) > head[2].size / 36 ||
        bq + *(const int *)(bq - 4) * 36 != mad + body[2].at + body[2].size || hq + *(const int *)(hq - 4) * 36 != mad + head[2].at + head[2].size)
        return NULL;

    for (i = 0; i < bcount; i++)
        if (bv[i].bone != HEAD)
            bto[i] = (WORD)count++;
    for (j = 0; j < hcount; j++) {
        double least = -1;

        if (hv[j].bone == HEAD) {
            hto[j] = (WORD)count++;
            continue;
        }
        hto[j] = 0;
        for (i = 0; i < bcount; i++) {
            double x = bv[i].x - hv[j].x, y = bv[i].y - hv[j].y, z = bv[i].z - hv[j].z, far_ = x * x + y * y + z * z;

            if (bv[i].bone == hv[j].bone && (least < 0 || far_ < least)) {
                least = far_;
                hto[j] = bto[i];
            }
        }
    }

    normal = (float (*)[4])(v + count);
    for (i = 0; i < bcount; i++)
        if (bv[i].bone != HEAD) {
            v[bto[i]] = bv[i];
            memcpy(normal[bto[i]], bn[i], sizeof *normal);
        }
    for (j = 0; j < hcount; j++)
        if (hv[j].bone == HEAD) {
            v[hto[j]] = hv[j];
            memcpy(normal[hto[j]], hn[j], sizeof *normal);
        }

    p = fac = (BYTE *)(normal + count);
    memset(p, 0, 16);
    p += 16;
    n = faces(p + 4, bt, *(const int *)(bt - 4), 3, bv, bcount, bto, FALSE);
    n += faces(p + 4 + n * 32, ht, *(const int *)(ht - 4), 3, hv, hcount, hto, TRUE);
    *(int *)p = n;
    p += 4 + n * 32;
    n = faces(p + 4, bq, *(const int *)(bq - 4), 4, bv, bcount, bto, FALSE);
    n += faces(p + 4 + n * 36, hq, *(const int *)(hq - 4), 4, hv, hcount, hto, TRUE);
    *(int *)p = n;
    p += 4 + n * 36;

    memcpy(line, body, 3 * sizeof *line); /* the names: nothing reads them */
    line[0].at = (DWORD)((BYTE *)v - start);
    line[0].size = count * 8;
    line[1].at = (DWORD)((BYTE *)normal - start);
    line[1].size = count * 16;
    line[2].at = (DWORD)(fac - start);
    line[2].size = (DWORD)(p - fac);
    return p;
}

/* In place of the call that reads chars\british.mad into memory in afLoadAnimModels, FUN_1000D0A0(name); the
 * renderer frees what this returns. A file that is not the game's 27 models is given as it is. */
static void *__cdecl load_models(const char *name)
{
    void *(__cdecl *read)(const char *) = (void *(__cdecl *)(const char *))(g_renderer + 0xD0A0);
    void *(__cdecl *get)(size_t) = (void *(__cdecl *)(size_t))(g_renderer + 0x14E70);
    void(__cdecl *drop)(void *) = (void(__cdecl *)(void *))(g_renderer + 0x14DF0);
    BYTE *mad = read(name), *made, *out;
    const file_t *list = (const file_t *)mad;
    DWORD size = 0, more = 9 * sizeof *list, room;
    file_t *line;
    int i;

    g_ace_look = FALSE;
    LOOK_OF_CLASS[15] = HERO_LOOK;
    if (!mad || list[0].at != FILES * sizeof *list)
        return mad;
    for (i = 0; i < FILES; i++)
        if (list[i].at + list[i].size > size)
            size = list[i].at + list[i].size;
    /* a new model is no larger than the two it is made from */
    room = size + more;
    for (i = 0; i < 9; i++)
        room += list[LEGEND_LOOK * 9 + i].size + list[COMMANDO_LOOK * 9 + i].size;
    made = get(room);
    if (!made)
        return mad;

    line = (file_t *)made;
    memcpy(made, mad, list[0].at);
    for (i = 0; i < FILES; i++)
        line[i].at += more;
    memcpy(made + list[0].at + more, mad + list[0].at, size - list[0].at);
    out = made + size + more;
    for (i = 0; i < 3 && out; i++)
        out = join(mad, list + LEGEND_LOOK * 9 + i * 3, list + COMMANDO_LOOK * 9 + i * 3, made, out, line + FILES + i * 3);
    if (!out) {
        drop(made);
        return mad;
    }
    drop(mad);
    g_ace_look = TRUE;
    LOOK_OF_CLASS[15] = ACE_LOOK;
    return made;
}

/* The Commando's head has eyes and a mouth of its own, pictures 6 and 7 of FACES.MTD: the game tests class 4. */
static BOOL masked(int class_)
{
    return class_ == 4 || (class_ == 15 && g_ace_look);
}

/* In place of FUN_00441240(pig, mood): the eyes at +0x94, for the model at +0x64 less 1. +0x19C class,
 * +0x1E4 nation: nation 6 has eyes of its own at rest. */
static void __fastcall eyes(BYTE *pig, void *unused, int mood)
{
    int *now = (int *)(pig + 0x94);

    (void)unused;
    if (!masked(*(const int *)(pig + 0x19C)))
        *now = *(const int *)(pig + 0x1E4) == 6 && !mood ? 5 : mood + 1;
    else if ((unsigned)mood <= 4)
        *now = mood == 1 || mood == 2 ? 7 : 6;
    *(short *)(*(BYTE **)(pig + 0x64) + 0x354) = (short)(*now - 1);
}

/* In place of FUN_004412D0(pig, mood): the mouth at +0x98, as the eyes. */
static void __fastcall mouth(BYTE *pig, void *unused, int mood)
{
    int *now = (int *)(pig + 0x98);

    (void)unused;
    if (!masked(*(const int *)(pig + 0x19C)))
        *now = *(const int *)(pig + 0x1E4) != 6 || (unsigned)mood > 1 ? mood + 1 : mood ? 3 : 5;
    else if ((unsigned)mood <= 4)
        *now = mood == 1 ? 7 : 6;
    *(short *)(*(BYTE **)(pig + 0x64) + 0x356) = (short)(*now - 1);
}

/* In place of the call of FUN_004407E0(pig, class, ...) in the pig's constructor: it makes the model and gives
 * class 4 the faces 6, any other 1. */
static BYTE *__fastcall pig_model(BYTE *pig, void *unused, int class_, int a2, int a3, int a4, int a5, int a6, int a7, int a8,
                                  int a9, int a10, int a11)
{
    typedef BYTE *(__fastcall *make_t)(BYTE *, void *, int, int, int, int, int, int, int, int, int, int, int);
    BYTE *model;

    (void)unused;
    ((make_t)0x4407E0)(pig, NULL, class_, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
    model = *(BYTE **)(pig + 0x64);
    if (class_ == 15 && g_ace_look && model) {
        *(int *)(pig + 0x94) = *(int *)(pig + 0x98) = 6;
        *(short *)(model + 0x354) = *(short *)(model + 0x356) = 5;
    }
    return pig;
}

/* In place of the call of afDrawAnimModel(model, 0, 1) in FUN_004808C0, the pig of a menu: its faces are the
 * menu's, 5 and 6 for class 4. +2 the look, +0x354 eyes, +0x356 mouth. */
static void __cdecl draw_menu_pig(BYTE *model, int a, int b)
{
    if (*(const short *)(model + 2) == ACE_LOOK) {
        *(short *)(model + 0x354) = 5;
        *(short *)(model + 0x356) = 6;
    }
    (*(void(__cdecl **)(BYTE *, int, int))0x54C5D8)(model, a, b);
}

void ace_renderer(BYTE *renderer)
{
    g_renderer = renderer;
    hook((DWORD)(ULONG_PTR)renderer + 0xD7F2, 0xE8, "\xE8\xA9\xF8\xFF\xFF", load_models, "renderer: the Ace's own look");
}

void ace_init(void)
{
    BYTE call[6] = { 0xE8, 0, 0, 0, 0, 0x90 };
    DWORD rel = (DWORD)(ULONG_PTR)draw_menu_pig - (0x480B9F + 5);

    memcpy(call + 1, &rel, 4);
    hook(0x4669A5, 0xE8, "\xE8\x36\x9E\xFD\xFF", pig_model, "Ace: the faces of the Commando's head");
    hook(0x441240, 0xE9, "\x83\xB9\x9C\x01\x00", eyes, "Ace: the eyes of the Commando's head");
    hook(0x4412D0, 0xE9, "\x83\xB9\x9C\x01\x00", mouth, "Ace: the mouth of the Commando's head");
    patch(0x480B9F, "\xFF\x15\xD8\xC5\x54\x00", call, 6, "Ace: the faces of the Commando's head in the menus");
}
