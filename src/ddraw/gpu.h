/* Direct3D 11 backend: one back buffer (integer multiple of the game resolution), the D3D7
 * fixed-function subset the game uses, 2D blits, present */
#pragma once
#include "common.h"
#include <stdint.h>

namespace gpu {

struct DrawState {
    uint8_t blend, src_blend, dst_blend;            /* ALPHABLENDENABLE, D3DBLEND */
    uint8_t ztest, zwrite, zfunc;                   /* ZENABLE, ZWRITEENABLE, D3DCMPFUNC */
    uint8_t cull;                                   /* D3DCULL */
    uint8_t flat;                                   /* SHADEMODE == D3DSHADE_FLAT */
    uint8_t alpha_test, alpha_func, alpha_ref;      /* ALPHATESTENABLE, ALPHAFUNC, ALPHAREF */
    uint8_t fog;                                    /* FOGENABLE with table fog LINEAR */
    float fog_start, fog_end;
    uint32_t fog_color, tfactor;                    /* D3DCOLOR */
    uint8_t color_op, color_arg1, color_arg2;       /* stage 0 D3DTOP / D3DTA */
    uint8_t alpha_op, alpha_arg1, alpha_arg2;
    uint8_t linear;                                 /* MAGFILTER/MINFILTER linear */
    uint8_t wrap;                                   /* ADDRESS WRAP (else clamp) */
};

struct Texture;

bool init(HWND hwnd, int scale);
bool ready();
void set_mode(int width, int height);            /* game resolution = back buffer size */
int width();
int height();

void set_clip(int x, int y, int w, int h);       /* D3D7 viewport, clips 3D primitives */
void clear_depth(float z);
void clear_target(uint32_t color);               /* D3DCOLOR */
void draw(D3DPRIMITIVETYPE type, const void *tlvertices, uint32_t count, const DrawState &s,
          Texture *tex);                         /* merged with the previous draw if the state matches */
int frame_draws();                               /* Direct3D 11 draws issued since the last present */

/* DirectDraw on the back buffer; rectangles in game pixels, pixels RGB565 */
void fill(const RECT &dst, uint16_t rgb565);
void blit(const RECT &dst, const void *owner, uint32_t version, const uint16_t *bits, int pitch,
          int w, int h, const RECT &src, int key /* -1 = none */, bool mirror_x, bool mirror_y);
void forget(const void *owner);                  /* drop the GPU copy of a blit source */
bool read_back(uint16_t *bits, int pitch);       /* back buffer -> RGB565, game resolution; false: bits untouched */
void write_back(const uint16_t *bits, int pitch); /* RGB565 -> whole back buffer */

void present(bool vsync);

Texture *texture_create(int w, int h);
void texture_upload(Texture *t, const uint16_t *a1r5g5b5, int pitch);
void texture_free(Texture *t);

} // namespace gpu
