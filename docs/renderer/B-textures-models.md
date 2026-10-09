# Group B: textures, texture pages, models and asset upload (`Data\_d3d.dll`)

**Scope.** This report covers 18 exports of `_d3d.dll` (image base `0x10000000`) and how `warhogs_unwrapped.exe` (image base `0x400000`) calls them.

**Method.** Read-only static analysis in Ghidra: the decompiler, disassembly and cross-references. Formats were checked against the shipped data in `Chars\`, `Maps\` and `Language\Tims\`. Nothing was run, and nothing in Ghidra was changed.

**Confidence tags**
- **[c]**: confirmed from code.
- **[d]**: confirmed against the data files.
- **[s]**: confirmed by an offline re-implementation run on game data.
- **[u]**: uncertain or inferred.

---

## 1. Key facts

1. **The renderer is DirectX 7, not DX6.** [c]
   - The DLL uses `DirectDrawCreateEx` with `IID_IDirectDraw7` (`0x10020338`) and `QueryInterface(IID_IDirect3D7)` (`0x10020328`).
   - It creates the device with `IDirect3D7::CreateDevice(IID_IDirect3DHALDevice)` (`0x10020318`).
   - It binds textures with `IDirect3DDevice7::SetTexture(0, IDirectDrawSurface7*)` (vtable +0x8c).
   - There are no `IDirect3DTexture2` objects and no texture handles anywhere in this group.
2. **Calling convention.** All 18 exports are `__cdecl`: the DLL returns with a plain `RET` and the exe does `ADD ESP,n` after each call. [c]
3. **Texture pages.** Textures are 256×256 *pages*, at most 32. The DLL repacks PSX TIMs into these pages itself and ignores the TIM VRAM and CLUT coordinates. [c]
   - Each page has two surfaces: a system-memory master copy (`DDSCAPS_TEXTURE|DDSCAPS_SYSTEMMEMORY`) and the texture that gets bound (normally `DDSCAPS_TEXTURE|DDSCAPS_VIDEOMEMORY`; system memory on drivers without the hardware flag).
   - Content always reaches the bound texture by `Blt` from the system-memory copy.
4. **Texel format.** In practice the format is **A1R5G5B5**. [c]
   - The format enumeration prefers 1555.
   - The colour path is hard-wired to 555, because `DAT_1002bf88 == 15` is a constant that is never written.
   - Transparency is a 1-bit alpha, used as a colour key. In a TIM, the palette value `0x0000` is transparent.
   - Drawing uses alpha test (ALPHAREF 8, GREATEREQUAL), SRCALPHA/INVSRCALPHA blending and bilinear filtering, with no mipmaps.
5. **Model faces are relocated in place.** `afCreateObj2` rewrites the face records inside the caller's `.MAD` buffer: the TIM index becomes a page slot, and transparency and alpha fields are added. [c]
   - The buffer must outlive the object.
   - The buffer must never be relocated twice. For repeat instances the exe passes `timBase = -1`.
6. **Lost surfaces are restored from the system-memory copies** (`afRestoreTextureSurfaces`). [c]
   - The code never checks the results of `CreateSurface` or `Blt`.
   - If re-creating a texture fails, the code dereferences a NULL pointer. This is a real crash path on Alt+Tab or mode switches. Whether it is *the* observed crash is [u].
7. **Team skins are swapped by page, not by TIM.** The first team plus `FACES.MTD` fill pages 0 and 1. Every other team uploads only its first page, into slot 2, 3, and so on, with exactly the same layout. [c] [s]
8. **Exports the exe never calls** [c]:
   - `afOverwriteTPage`
   - `afGetActualTextureFormat`
   - `afAddToGroundTextures` (a stub that returns 0)
   - `afGetAnimModelBBox`
9. **Possible memory corruption in long matches** [c] [u]: the model vertex pool only grows, has no bounds check and is shared by "ammo" objects. See §9.

---

## 2. Export table

| Export | DLL address | Ord | exe global pointer | exe call sites | C prototype (cdecl) |
|---|---|---|---|---|---|
| afLoadTims | 0x1000e200 | 72 | 0x00537fe4 | 22 (6 functions) | `int afLoadTims(const char *path, const void *memPkg, const ColourAdjust *adj)` |
| afGetPageFittedTim | 0x1000f910 | 65 | 0x0054c5bc | 23 (9 functions) | `PageFittedTim *afGetPageFittedTim(int tim)` |
| afD3dUploadAllTextures | 0x1000e550 | 49 | 0x00538040 | 9 (6 functions) | `uint8_t afD3dUploadAllTextures(int firstTim, int makeTexture, uint8_t sortBySize, int firstPageSlot)` |
| afD3dDeleteAllTextures | 0x1000e7d0 | 48 | 0x0054c5cc | 2 | `void afD3dDeleteAllTextures(void)` |
| afOverwriteTPage | 0x1000f720 | 73 | 0x005386e0 | 0 | `int afOverwriteTPage(int srcSlot, int dstSlot)` |
| afOverwriteTexture | 0x1000f750 | 74 | 0x00538078 | 1, plus 1 inside the DLL | `int afOverwriteTexture(IDirectDrawSurface7 *src, int unused, int tim)` |
| afRestoreTextureSurfaces | 0x1000e870 | 77 | 0x0053805c | 1, plus 1 inside the DLL | `void afRestoreTextureSurfaces(void)` |
| afGetActualTextureFormat | 0x1000ea40 | 57 | 0x0054cfe0 | 0 | `DDPIXELFORMAT *afGetActualTextureFormat(void)` |
| afAddToGroundTextures | 0x1001c5a0 | 43 | 0x005381a4 | 0 | `int afAddToGroundTextures(void)` (always 0) |
| afAddToGroundTextures2 | 0x100076e0 | 44 | 0x00538020 | 4 | `int afAddToGroundTextures2(const void *tim, void *unused, int x, int y, int tileIndex)` |
| afUploadGroundTextures | 0x10007530 | 93 | 0x005381c4 | 1 | `void afUploadGroundTextures(void)` |
| afDeleteGroundTextures | 0x10007f60 | 50 | 0x0053808c | 1 | `void afDeleteGroundTextures(void)` |
| afLoadAnimModels | 0x1000d7c0 | 71 | 0x00537fec | 1 | `void afLoadAnimModels(int pigTimBase)` |
| afReleaseAnimModels | 0x1000d990 | 75 | 0x00538154 | 1 | `void afReleaseAnimModels(void)` |
| afCreateObj2 | 0x1000d190 | 47 | 0x00538110 | 11 (5 functions) | `ObjInst *afCreateObj2(void *madBase, const PkgEntry *dir3, int timBase, char flipY, char dropColourKeyedFaces)` |
| afGetKeyFrameList | 0x1000f9d0 | 62 | 0x005380f0 | 2 | `AnimKey *afGetKeyFrameList(int anim)` |
| afGetAnimModelBBox | 0x1000f9f0 | 58 | 0x0053889c | 0 | `float (*afGetAnimModelBBox(int pigClass))[4]` (8 corners) |
| afSetAmmoModelFlag | 0x1000d9f0 | 80 | 0x0053809c | 3 | `void afSetAmmoModelFlag(int useAmmoRing)` |

`FUN_004ac430` in the exe binds every pointer with `GetProcAddress`; that write is not counted as a call site.

---

## 3. File formats as the DLL consumes them

### 3.1 Package container (`.MTD`, `.MAD`, sky `*.MAD`, `language\tims\*.MAD`) [c] [d]
- The file starts with a directory of 24-byte entries:

  ```c
  struct PkgEntry { char name[16]; uint32_t offset; uint32_t size; };
  ```

- The entry count is `entry[0].offset / 24`; the directory is followed directly by the data.
- `.MTD` files and the sky or "language" `.MAD` files contain only TIMs. Model `.MAD` files contain `VTX`, `NO2` and `FAC` triples (§3.4).

### 3.2 PSX TIM, as parsed by `FUN_10012f90` [c] [d]

| Offset | Field | How the DLL uses it |
|---|---|---|
| +0x00 | u32 magic `0x10` | not checked |
| +0x04 | u32 flags | `flags & 7` is stored. The CLUT bit 3 is **assumed** and not checked. |
| +0x08 | u32 CLUT block length | ignored |
| +0x0c | u16 x, u16 y (CLUT VRAM position) | ignored |
| +0x10 | u16 w, u16 h (CLUT size) | `numColours = w \| (h-1)<<16`. Only `h == 1` with 16 or 256 colours works. |
| +0x14 | u16 clut[numColours] | PSX colour: bit 15 STP, bits 10-14 B, 5-9 G, 0-4 R |
| img +0 | u32 length | ignored. The image block is found at `+0x14 + ((numColours*2) & ~3)`. |
| img +4 | u16 x, u16 y | ignored (VRAM position) |
| img +8 | u16 w (16-bit units), u16 h | Width in pixels is `w*4` if `numColours == 16`, otherwise `w*2`. |
| img +0xc | pixels | 4 bpp with the low nibble as the left pixel, or 8 bpp |

- 16- and 24-bpp TIMs, and CLUTs with `h > 1`, are not supported. With them, the palette copy overruns and no pixels are packed.
- The shipped data has neither:
  - Team MTDs: 4 bpp, CLUT 16×1.
  - Sky TIMs: 250×250 at 8 bpp.
  - Fonts: 256×48 and 256×160 at 4 bpp.

### 3.3 Ground tiles [c] [d]
- `MAPS\<map>.PTG` is a `u32 count` followed by `count` TIMs of 576 bytes each: 32×32, 4 bpp, CLUT 16×1. `ARCHI.PTG` has 238 tiles.
- The exe reads the file and passes each TIM to `afAddToGroundTextures2`.
- It also passes `language\tims\wat01.TIM` (tile 0), `wat02.tim` (tile 1) and `mine.tim` (the last tile).

### 3.4 Models: `.MAD` containing VTX/NO2/FAC triples [c] [d]
- **Layout.** Each model is three consecutive directory entries in the order **VTX, NO2, FAC**. In the code, `dir3` points at the VTX entry and the next model is at `dir3 + 0x48`.
- **VTX (8 bytes per vertex).** `int16 x, y, z; uint16 bone;`
- **NO2 (16 bytes per normal).** `float nx, ny, nz, bone;` The normal count equals the vertex count; for example `pcace_hi` has VTX 5264 bytes and NO2 10528 bytes.
- **FAC.** Six blocks, each a `u32 count` followed by fixed-size records of 8, 12, 24, 28, 32 and 36 bytes.
  - Blocks 1-4 are PSX-style primitives (flat or gouraud triangles and quads with colour, textured triangles and quads with byte UVs). **Every PC file has count 0 for them**; the "16 zero bytes header" seen in other tools is exactly these counts.
  - `afDrawObj` assumes blocks 1-4 are empty and reads the triangle count at FAC+0x10.

**Triangle record, 32 bytes**

| Offset | Content on disk | Content after load |
|---|---|---|
| +0x00 | `u8 uv[3][2]` (texel coordinates inside the TIM) | unchanged |
| +0x06 | `u16 v[3]` | unchanged |
| +0x0c | `u16 n[3]` | unchanged |
| +0x12 | `u16 pad` | unchanged |
| +0x14 | `i32 tex`: TIM index relative to the package, -1 for none | page **slot**, or -1 |
| +0x18 | 0 | global TIM index |
| +0x1c | 0 | `hasColourKey`; overwritten with the per-object alpha on every draw |

**Quad record, 36 bytes**

| Offset | Content on disk | Content after load |
|---|---|---|
| +0x00 | `u8 uv[4][2]` | unchanged |
| +0x08 | `u16 v[4]` | unchanged |
| +0x10 | `u16 n[4]` | unchanged |
| +0x18 | `i32 tex` | slot, or -1 |
| +0x1c | 0 | global TIM index |
| +0x20 | 0 | `hasColourKey`, later the alpha |

- Quad corners are in perimeter order: the quad is drawn as triangles (v0,v1,v2) and (v2,v3,v0) (`FUN_100085e0`).
- Faces whose slot is -1 are **skipped entirely**; they are not drawn untextured.

### 3.5 Skeleton and motion capture [c] [d]
- **`chars\pig.hir`**: 300 bytes, 15 bones × 20 bytes: `u32 parent; i16 x, y, z; i16 pad[5];`. It is copied to `DAT_11c948e8`.
- **`Chars\MCAP.MAD`** is not a normal package. It is a table of `{u32 offset, u32 size}` pairs with `count = entry[0].offset / 8` (93 entries).
  - Each entry is `frameCount × 272` bytes.
  - A frame is a 32-byte header followed by 15 × `float4`; the floats look like Euler angles in radians with `w = 1.0` [u].
- **`AnimInfo[93]`** is a static table at `0x1002c770`, 0x58 bytes per entry:

  ```c
  struct AnimKey  { int32_t phase; int32_t event; int32_t param; };   // phase 0..4095 [u: event/param meaning]
  struct AnimInfo {               // 0x1002c770 + a*0x58
      int32_t frameCount;         // +0x00  (afGetFrameCount)
      int32_t frameStep;          // +0x04  ~4096/frameCount (afGetFrameStep; clamps a>82 to entry 0)
      AnimKey keys[6];            // +0x08  afGetKeyFrameList returns &keys[0]; list ends at phase==0 for i>0
      int32_t boneCount;          // +0x50  15
      void   *mcapData;           // +0x54  set by afLoadAnimModels from MCAP.MAD
  };
  ```

  Example, entry 0: `frameCount 17`, `step 240`, keys `(0,2,0) (1200,25,0) (1920,1,0) (3120,26,0)`.

---

## 4. DLL global state

### 4.1 TIM tables (global TIM index space, `t` = 0..1535)

| Address | Type and size | Meaning |
|---|---|---|
| 0x11ec0f60 | int | TIM count; this is the next TIM index (max 0x600 = 1536) |
| 0x10112cc0 | `TimInfo[1536]`, 0x1c each | `+0` pixel pointer (into the package buffer), `+4` `flags&7`, `+8` unused [u], `+0xc` numColours, `+0x10` width in pixels, `+0x14` height, `+0x18` CLUT pointer (into the package buffer, modified in place) |
| 0x1004e370 | `u16 pal[1536][256]` | per-TIM palette copy, converted to X1R5G5B5 with bit 15 = 0 |
| 0x11ec1180 | `PageFittedTim[1536]`, 0x28 each | see `afGetPageFittedTim` |
| 0x11ec0f68 | `void *blob[]`, count at 0x11ec1178 | file buffers kept alive until `afD3dDeleteAllTextures`; about 132 slots and no bounds check [u] |
| 0x11c92fe0 | `int[]` | TIM base of each sub-package in the unused "package of packages" path (written, never read) |

```c
struct PageFittedTim {            // 0x11ec1180 + t*0x28
    uint8_t  x, y;                // +0x00 texel position inside the 256x256 page (after the 1-texel gap)
    uint16_t w, h;                // +0x02 / +0x04 size in texels
    uint16_t pad;
    float    u0, v0;              // +0x08 / +0x0c  x/256, y/256
    float    du, dv;              // +0x10 / +0x14  w/256, h/256
    uint8_t *indexPage;           // +0x18 0x11cc0f58 + page*0x10000 (8-bit index page)
    int32_t  slot;                // +0x1c texture slot 0..31 (written by upload)
    int32_t  hasColourKey;        // +0x20 1 if a *used* palette entry is 0x0000
    int32_t  unk24;               // +0x24 never seen written [u]
};
```

### 4.2 Pages and surfaces

| Address | Meaning |
|---|---|
| 0x11a25d20 | `TexSlot slot[32] = { IDirectDrawSurface7 *tex; IDirectDrawSurface7 *sys; }`. `tex` is bound for drawing; `sys` is the master copy. |
| 0x11c948e0 | number of pages / slots in use (next free page) |
| 0x11cc0f58 | `u8 indexPage[32][256][256]`: 8-bit palette indices per page (CPU only) |
| 0x11b6a570 | `u16 staging[256][256]` used while uploading |
| 0x11cc0f40 | 2 MB occupancy map, `calloc`'d and freed again during each packing |
| 0x11ab9490 | device texture cache (last pointer passed to `SetTexture`); reset by device creation, **not** by `afD3dDeleteAllTextures` |
| 0x11b6a2f0 | `IDirectDraw7*` |
| 0x11ab9474 | `IDirect3D7*` |
| 0x11ab9470 | `IDirect3DDevice7*` |

### 4.3 Texture pixel format (written by the enumeration callback at 0x10005f80, §5.5)

| Address | Meaning |
|---|---|
| 0x1013e420 | chosen `DDPIXELFORMAT`, 32 bytes (`afGetActualTextureFormat`) |
| 0x10131b5c | total bit count (16 or 32) |
| 0x11ab8c9c / 0x11ab8c98 / 0x11ab8b10 / 0x11ab8b0c | bit counts for R / G / B / A |
| 0x1013ed04 / 0x11a25d08 / 0x1013e9f8 / 0x10131b18 | shifts for R / G / B / A |
| 0x1011db00 / 0x10131b50 / 0x11ab8ed0 / 0x11ab1ca4 | masks for R / G / B / A |
| 0x11ab9488 | best rank found so far (starts at -1) |
| 0x11ab948c | "format found" flag. Checked only in `SelectDriverMode`, and only when `DAT_1002bf50 != 0`: then the device is released and `cfg+0xbc = 1` (3D disabled) [u]. |
| 0x1002bf88 | **constant 15**: the colour path is X1R5G5B5. The 16-bit (565) and 0 (8-bit palettised) code paths are dead. |

### 4.4 Ground pages

| Address | Meaning |
|---|---|
| 0x1004cf90 | `GroundPage gp[8]`, 0xd4 bytes each (layout below) |
| 0x11ab94b0 | ground pages in use |
| 0x1004cf80 | tiles added so far; the next automatic tile goes to page `n/49`, cell `n%49` |
| 0x10022050 / 0x10022054 | page and cell of the first "all-STP" tile (mode 2, water) |
| 0x10032c08..0x10032c24 | UV corners of that water tile (u0, u1, u1, u0 / v0, v0, v1, v1), with a half-texel inset |
| 0x1002204c | `tileIndex + 2` of the first indexed tile; this is the PTG tile base used by `afSetMap` and `afAdjustMapTile` |
| 0x11ab94d4 | count of tiles wider than 32 |
| 0x11b6a564 | `ColourAdjust*` used for ground tiles (set by `afIfaceInit`) |
| 0x10022058 | constant **3**: global brightness bias |
| 0x1002205c | constant **8**: global tint bias |
| 0x11ab9464 | 64×64 scanner (radar) surface, released by `afDeleteGroundTextures` |

```c
struct GroundPage {               // 0x1004cf90 + p*0xd4
    IDirectDrawSurface7 *tex;     // +0x00 TEXTURE|VIDEOMEMORY, made by afUploadGroundTextures
    IDirectDrawSurface7 *sys;     // +0x04 TEXTURE|SYSTEMMEMORY, written per tile, read by afIsPointWatery
    int32_t unk08, unk0c;         // [u]
    int32_t tileMode[49];         // +0x10  0 opaque, 1 colour-keyed, 2 all-STP ("water")
};
struct ColourAdjust {             // exe: DAT_00520708 (level) or DAT_004d524c (default {5,0,2,0,{0,0,0}})
    int32_t skyType;              // +0x00 not used by the DLL (exe uses it to pick sky and snow/rain)
    int32_t desatSteps;           // +0x04 0..5, where 5 means fully grey
    int32_t brightness;           // +0x08 (+3 global bias)
    int32_t tintPercent;          // +0x0c 0..100
    uint8_t tintR, tintG, tintB;  // +0x10..+0x12 (+8 global bias)
};
```

### 4.5 Model pools [c]

| Address | Meaning |
|---|---|
| 0x11b8a7f8 | `float vert[][4]` (x, y, z, bone); count at 0x11c94854. Capacity is about 68,095 up to 0x11c947f0. **No bounds check; it only grows until `afReleaseAnimModels`.** |
| 0x11c94f58 | `Mesh[]`, 0x94 each; count at 0x11cc0578; about 674 fit before 0x11cad568; no check |
| 0x11cad568 | `ObjInst[]`, 0x5c each; count at 0x11cc056c; 512 fit; no check |
| 0x11cb8d68 / 0x11cbd768 | ammo ring for `Mesh` / `ObjInst`, 128 each; indices at 0x11cc057c / 0x11cc0570; wraps to 0 |
| 0x11cc0574 | ammo flag (`afSetAmmoModelFlag`) |
| 0x11cc0568 | flag "drop colour-keyed faces" (5th argument of `afCreateObj2`) |
| 0x11c94860 | `ObjInst *pig[27]`, indexed `class*3 + {0 body_hi, 1 body_me, 2 accessory_hi}` |
| 0x11c948d8 | `chars\british.mad` buffer (DLL-owned) |
| 0x11cc06f0 | `MCAP.MAD` buffer (DLL-owned) |
| 0x11c948e8 | skeleton copy, 300 bytes |
| 0x11c947f0 | default 3×4 matrix (identity), set by `afIfaceInit` |

```c
struct Mesh {                     // 0x94
    float (*verts)[4];            // +0x00 into the vertex pool
    uint8_t *fac;                 // +0x04 into the caller's .MAD buffer (relocated in place)
    float (*normals)[4];          // +0x08 into the .MAD buffer
    int32_t numNormals;           // +0x0c
    int32_t numVerts;             // +0x10
    float   bbox[8][4];           // +0x14 corners: (mn,mn,mn)(mn,mn,MX)(mn,MX,mn)(mn,MX,MX)(MX,mn,mn)(MX,mn,MX)(MX,MX,mn)(MX,MX,MX)
};
struct ObjInst {                  // 0x5c
    int32_t unk00;                // [u]
    float   m[12];                // +0x04 3x3 rotation + translation (afSetObjPos/afScaleObj usually overwrite it)
    int32_t unk34[3];             // +0x34 [u] (scale?)
    int32_t numVerts;             // +0x40
    Mesh   *mesh;                 // +0x44
    int32_t type;                 // +0x48 3 = animated pig part, 2 = sky; NOT initialised by afCreateObj2
    int32_t a4c;                  // +0x4c 0xff
    int32_t alpha;                // +0x50 0xff, copied into the faces' alpha field when drawn
    int32_t unk54;                // +0x54 0 for pigs, -1 for sky
    int32_t unk58;
};
```

---

## 5. Colour, transparency, packing and texture references (reproduce exactly)

### 5.1 CLUT adjustment ("weather" colour grading)
This runs only if `adj != NULL`. [c]
- For TIM packages (`FUN_10012f90`), only **CLUT entries 0..15** are adjusted, **even for 8-bit TIMs** (an original bug).
- For ground tiles (`afAddToGroundTextures2`), all CLUT entries are adjusted.
- Entries equal to `0x0000` are skipped. `r = c&31`, `g = (c>>5)&31`, `b = (c>>10)&31`, and bit 15 (STP) is kept.

```
pass 1  if adj->desatSteps != 0:
          avg = (r+g+b)/3                       (unsigned)
          if steps == 5: r = g = b = avg
          else repeat steps: for each comp: if comp > avg: comp--; if comp < avg: comp++
          if r==g==b==0: r = 1
pass 2  k = adj->brightness + 3; if k != 0:
          r,g,b = clamp(comp + k, 0, 31); if all 0: r = 1
pass 3  p = adj->tintPercent; if p > 0:
          avg = (r+g+b)/3
          tr = min(31, ((adj->tintR + 8) * avg) >> 4); likewise tg, tb
          r = (tr*p + (100-p)*r)/100 (C truncation); likewise g, b; if all 0: r = 1
store   c = stp | b<<10 | g<<5 | r
```

- The front-end default `{5, 0, 2, 0}` brightens the first 16 colours of the team, weapon and hat TIMs by **+5 on the 5-bit scale**.
- In a level, the values come from the level block `DAT_00520708`, filled by the exe [u].
- A side effect: a CLUT entry `0x8000` becomes `0x8001` whenever any pass is active. It then counts as opaque near-black, not as transparent (see §5.2).

### 5.2 TIM path (`FUN_10012f90`, run by `afLoadTims`) [c]
1. **Colour-key flag.** `hasColourKey[t] = 1` if any palette index *used* by the image has the raw, post-adjust CLUT value `0x0000`. "Used" means every nibble or byte of the image is scanned.
2. **Index swap.** The first used non-zero index with value 0 is swapped with index 0, in both the pixels and the palette copy. This keeps colours unchanged and **can be ignored**.
3. **Conversion.** Both CLUT copies are converted: `c555 = r<<10 | g<<5 | b`. **The STP bit is dropped.**

### 5.3 Page texel (`afD3dUploadAllTextures`) [c]

```
texel = (c555 == 0 && hasColourKey[t]) ? 0x0000 : (c555 | 0x8000)     // A1R5G5B5
```

- The staging page is cleared to `0x0000`, so every gap is transparent black.
- Consequences:
  - Black used as colour 0 is transparent only if the TIM uses a real `0x0000` entry.
  - A PSX "opaque black" (`0x8000`) also becomes transparent in such a TIM, unless an adjustment pass turned it into `0x8001`.
  - STP semi-transparency is never used for blending.

### 5.4 Ground tile path (`afAddToGroundTextures2`) [c]
- **Mode.** After the adjustment, the CLUT is classified using only the non-zero entries:
  - `mode = hasNonStp ? (hasStp ? 1 : 0) : 2`
  - `tileIndex == -1` forces mode 1.
- **Texel.** `A=0, RGB=0` if `mode==1 && (c&0x8000 || c==0)`; otherwise `A=1, R=c&31, G=(c>>5)&31, B=(c>>10)&31`. For the mode-1 transparent case the texel is `0x0000`; otherwise it is `A<<15|R<<10|G<<5|B`.

| Mode | Meaning | Transparent texels |
|---|---|---|
| 0 | no STP colours | none; `0x0000` is opaque black |
| 1 | mixed | STP colours and `0x0000` |
| 2 | every colour has STP | none; the tile is treated as "water" |

- **Placement.** The page is a 7×7 grid. Cell `s` (`s = n % 49`) goes to `x = (s%7)*(size+1)`, `y = (s/7)*(size+2)`. For 32-pixel tiles that is **x = 33·col, y = 34·row**. An explicit `x, y` replaces this, but the exe always passes -1.
- **Water tile.** The first mode-2 tile stores its page and cell and a UV rectangle: `u0 = col*33/256 + 1/512`, `u1 = u0 + size/256 - 1/256`, and the same for v with 34. The 33 and 34 are hard-coded.
- **Who uses `tileMode`.** The ground renderer (`FUN_10003380`, group C/D) and `afIsPointWatery`:
  - mode 2: the point is water.
  - mode 1: the system-memory page is locked and texels are sampled; a texel `== 0x0000` means water.
- **Quirk.** Palette lookup uses a *signed char* index, so 8-bit tiles with an index ≥ 128 read garbage. All shipped tiles are 4-bit.

### 5.5 Texture-format enumeration (`IDirect3DDevice7::EnumTextureFormats` callback, code at 0x10005f80, no Ghidra function defined) [c]
- **Called from.** `FUN_10006240` (device creation, under `SelectDriverMode`).
- **Rejected formats.** Palettised formats (`DDPF_PALETTEINDEXED1/2/4/8/TO8`), non-RGB formats, and formats without `DDPF_ALPHAPIXELS` are rejected.
- **Ranking** (higher wins):

  | Rank | Format |
  |---|---|
  | 3 | A1R5G5B5 |
  | 2 | A4R4G4B4 (A, G and B bit counts checked) |
  | 1 | any other 16-bit format with alpha |
  | 0 | a 32-bit format with alpha |

- On a match it copies the `DDPIXELFORMAT` to 0x1013e420 and stores the masks, shifts and counts. It always returns `D3DENUMRET_OK`.
- **Only 1555 actually works.**
  - With any other 16-bit format, `FUN_1000e370` copies the 1555 staging buffer as-is, so colours come out wrong.
  - With a 32-bit format it converts through `FUN_10007110(p, 0)`. That function decodes the input **as RGB565**, wrongly for 1555 input, and ORs in the alpha mask unless `p == 0`.
  - If no alpha format is found and `SelectDriverMode` does not catch it (`DAT_1002bf50 == 0`), `DDPIXELFORMAT` stays empty, so the first `CreateSurface` fails and the game terminates with `Terminate(0x57, 0xcf, hr)`.

### 5.6 Page packer (`FUN_10012a30` → `FUN_10012510`, with `FUN_10012410` testing and `FUN_100124b0` marking) [c] [s]
- **Input.** TIMs `[firstTim, count)` and a first page = current page count.
- **Sort.** If `sortBySize != 0`, they are bubble-sorted by area, largest first. For each `i`, `j` runs from the end down to `i+1`, swapping if `area[i] < area[j]`; the sort is not stable.
- **Gap.** Each TIM reserves `(w+gx) × (h+gy)` with `gx = (w != 256)` and `gy = (h != 256)`, and sits at `(px+gx, py+gy)`. That is a 1-texel transparent gap on the left and top.
- **Search.** For each TIM, pages `firstPage..31` are tried in order; within a page, positions step by **2**.
  - If `h < w`: x in the outer loop, y in the inner loop.
  - Otherwise: y outer, x inner.
  - The first free position wins.
- **Failure.** If nothing fits: `"ERROR: Cannot fit tims. Not enough TPAGE space"` and `exit(1)`.
- **Copy into the page.** In hi-colour mode the 8-bit indices are copied; 4-bit indices are expanded to bytes with the low nibble first (`FUN_10012960` / `FUN_100129c0`).
- **Check on real data** [s]:
  - Packing `british.mtd` plus `FACES.MTD` from page 0 without sorting puts TIMs 0..117 (team-specific) on **page 0**, and TIMs 118..133 (`eyes000`, `gobs000` and all of `FACES.MTD`) on **page 1**.
  - Packing `AMERICAN.MTD` from page 2 gives exactly the page-0 layout on page 2.

### 5.7 How textures are referenced when drawing [c]
- **Binding.** A texture is a *slot* 0..31 (`PageFittedTim.slot`), bound with `SetTexture(0, slot[k].tex)` and cached in 0x11ab9490.
- **Vertices.** All geometry is `D3DFVF_TLVERTEX` (FVF `0x1c4`: XYZRHW, DIFFUSE, SPECULAR, TEX1) batched per slot.
  - Opaque batch: `0x1013ed08 + slot*0xc7380`, 25,500 vertices.
  - Colour-keyed batch: `0x11a26520 + slot*0x4000`, 512 vertices, no overflow check.
  - The choice is `hasColourKey[tim]`.
- **UVs are texel-centred in page space**: `u = (uv.u + tim.x + 0.5) / 256`, `v = (uv.v + tim.y + 0.5) / 256` (`FUN_10008080` and `FUN_100085e0`).
- **The exe also builds 2D UVs itself** from `afGetPageFittedTim`: `(x+0.5)/256 .. (x+w+0.5)/256`. The right and bottom edge therefore samples the next (gap) texel.
- **Face swap.** For type-3 (pig) instances, global TIM **118 (`eyes000`)** is replaced by `120 + eyeFrame` and **119 (`gobs000`)** by `127 + mouthFrame`. The frames come from the anim-model fields `+0x354` and `+0x356`.
  - This needs `british.mtd`-style team TIMs at index 0 and `FACES.MTD` right after them. The exe guarantees that (§7).
- **Team skin.** `afDrawObj(inst, param)` with `param != 0` binds slot `(param>>8)&0xf` for batch 0 (the team page) and slot 1 for batch 1 (faces). See group D. [c]/[u]
- **Render states** (`FUN_10006380`): ZENABLE; ZWRITE; ZFUNC LESSEQUAL; ALPHATESTENABLE with ALPHAREF 8 and ALPHAFUNC GREATEREQUAL; ALPHABLENDENABLE with SRCALPHA / INVSRCALPHA; DITHER on; SHADEMODE GOURAUD; LIGHTING off; COLORKEYENABLE off. Stage 0: ALPHAOP MODULATE (TEXTURE, DIFFUSE); COLORARG1 TEXTURE, COLORARG2 DIFFUSE (COLOROP left at the default MODULATE); MAG and MIN filter LINEAR; no mip filter; address mode not set (WRAP by default).

---

## 6. Exports

### afLoadTims (0x1000e200)
1. **Signature.** `int __cdecl afLoadTims(const char *path, const void *memPkg, const ColourAdjust *adj)`. It returns the **TIM base**: the TIM count before the call, which is the global index of the first TIM loaded. It returns 0 if the file is empty, which is ambiguous with base 0. A missing file is fatal: `MessageBox("Failed to find file: %s", "Assertion Failed!")` and `exit(1)` in `FUN_1000d0a0`. Plain `RET`.
2. **exe usage.** Pointer `0x00537fe4`, **22 call sites** in `FUN_00482a00`, `FUN_00483980`, `FUN_004852c0` (11), `FUN_00486030`, `FUN_00486670` and `FUN_004866b0`. `memPkg` is always 0.
   - `FUN_00486030` @`0x004861e0`: `base = afLoadTims(nationMtd[i], 0, adj)`, where `adj = &DAT_00520708` (level weather block) or `&DAT_004d524c` (default). The first team's base goes to `game+0x3e4` and is then used for `afLoadAnimModels`.
   - Right after the first team, the same function loads `afLoadTims("chars\\faces.mtd", 0, adj)`.
   - `FUN_004852c0` @`0x00485412`: `afLoadTims("MAPS\\<map>.MTD", 0, &DAT_00520708)` goes to `game+0x3e0`.
   - The front-end and HUD packages are loaded with `adj = 0`: `mapicons.mtd`, `sight.mtd`, `expltims.mad`, `flagtims.mad`, `fonttims.mad`, `tboxtims.mad`, `snow.mtd`/`rain.mtd`, `dashtims.mad`, `facetims.mad`, `pigmap\*.mad`, `fefxtims.mtd`, `propoint.mtd` and `debrief\dbpill.mtd`. The sky TIM packs are `chars\<weather>.mad`.
3. **Behaviour.** [c]
   - **`memPkg != NULL` (unused by the exe).** Every directory entry of `memPkg` is treated as a TIM package: its pointer goes into the blob list and it is parsed. **Bug:** `afD3dDeleteAllTextures` later calls `free()` on these interior pointers, which corrupts the heap.
   - **Otherwise.** The whole file is read into a buffer the DLL allocates (`malloc`). Then it calls `strstr(_strupr(buffer), "MAD")`, which tests the **first directory entry name** in the buffer, not the file name.
     - If found: every entry is copied into its own block and parsed as a TIM package, `0x11c92fe0[k]` gets the base, and the outer buffer is freed. No shipped file triggers this; checked in `chars\`, `maps\` and `language\tims\`.
     - If not found (the normal case): the buffer is the TIM package. It goes into the blob list and stays alive until `afD3dDeleteAllTextures`.
   - **Per TIM** (`FUN_10012f90`, §5.1-5.2):
     - limit 1536 (`"ERROR: Too many tims"` and exit);
     - fill `TimInfo[t]`;
     - adjust CLUT entries 0..15 in place;
     - copy the palette to `pal[t]`;
     - compute `hasColourKey`;
     - swap the transparent index;
     - convert both CLUT copies to X1R5G5B5.

   **Nothing is created on the GPU here.**
4. **Globals.** `0x11ec0f60`, `0x10112cc0`, `0x1004e370`, `0x11ec1180` (`hasColourKey` at +0x20), `0x11ec0f68`/`0x11ec1178`, `0x11c92fe0`, `0x1002bf88`, `0x10022058`, `0x1002205c`.
5. **D3D11.**
   - Keep the global TIM index space and the order of loads exactly (the face swap and the team layout depend on it), and keep the parsed CPU data (indices and palettes).
   - Reproduce §5.1-5.2 bit-exactly, including the "first 16 entries" quirk.
   - The `memPkg` path and the "MAD" path can be dropped, or `memPkg` can be implemented without freeing it.

### afGetPageFittedTim (0x1000f910)
1. **Signature.** `PageFittedTim *__cdecl afGetPageFittedTim(int tim)`, which returns `0x11ec1180 + tim*0x28`. No bounds check.
2. **exe usage.** Pointer `0x0054c5bc`, **23 call sites**: `FUN_0044bc80`, `FUN_0044c070`, `FUN_0044c390`, `FUN_0044eba0`, `FUN_0044fa50`, `FUN_0044fe00`, `FUN_004544e0` (15, HUD; not analysed in detail [u]), `FUN_00458a80` and `FUN_00483010`.
   - `FUN_0044fa50` @`0x0044fada` (rain and snow): `r = afGetPageFittedTim(game->weatherTimBase(+0x41c) + (i&7)/2)`. It builds 128 TLVERTEX quads with `u = (r->x + 0.5)/256` and `(r->x + r->w + 0.5)/256`, and the same for v, then calls `afAdd2dPolyToSortList(..., 0x80)`. The TIM index is put into the poly record, and the DLL turns it into a slot through `PageFittedTim.slot`.
   - `FUN_00483010` (pig-map screen) uses `w` and `h` for the quad size.
3. **Behaviour.** A pure accessor. The fields are valid after `afD3dUploadAllTextures` has packed the TIM: `x`, `y`, `w`, `h`, the floats and `indexPage` are set while packing; `slot` and `hasColourKey` while uploading and loading. Layout in §4.1.
4. **D3D11.** This is ABI the exe reads directly: keep the 0x28-byte layout and the 256×256 page coordinates (bytes x/y, u16 w/h). Any packing works for 2D drawing, but team pages need identical layouts (§5.6), so porting the packer as-is is the simplest option.

### afD3dUploadAllTextures (0x1000e550)
1. **Signature.** `uint8_t __cdecl afD3dUploadAllTextures(int firstTim, int makeTexture, uint8_t sortBySize, int firstPageSlot)`. The result is in AL and the exe ignores it:
   - 1: the call started at page 32 or more, or all 32 pages were used;
   - 0: it stopped at the first page without TIMs, which is the normal case, or returned early after an override.
2. **exe usage.** Pointer `0x00538040`, **9 call sites**: `FUN_00482a00` (2), `FUN_00483980`, `FUN_004852c0`, `FUN_00486030` (3), `FUN_00486670` and `FUN_004866b0`.
   - **Normal:** `afD3dUploadAllTextures(base, 1, 0xff, -1)`. Examples: `FUN_00486670` (fefxtims) and the level batch in `FUN_004852c0` @`0x0048547d` with `base = mapicons` base, which uploads all nine level packages together, sorted.
   - **Teams** (`FUN_00486030`): the first team uses `(base0, 1, 0, 0)` after `faces.mtd`; teams 2..N use `(base_i, 1, 0, slot)` with `slot = 2, 3, …`.
3. **Behaviour.**
   1. Pack `[firstTim, TIMcount)` starting at page `P0 = 0x11c948e0` (§5.6). The `indexPage`, `x`, `y`, `w`, `h` and float fields are set.
   2. If `P0 >= 32`, return 1.
   3. For each page `p = P0, P0+1, …`:
      - Zero the staging buffer.
      - For every TIM `t >= firstTim` whose `indexPage == page p`: set `slot[t] = p` and write its texels (§5.3).
      - If no TIM lands on page `p`, **return 0**.
      - Create the surfaces with `FUN_1000e370(staging, 256, 256, slotIdx, 0, hadColourKey, makeTexture)`. Arguments 2-5 except `slotIdx` are unused.
      - `slotIdx = p`, except on the first page when `firstPageSlot != -1`: then `slotIdx = firstPageSlot`, and if `firstPageSlot != 0` the page count is incremented and the function returns 0 (only one page is uploaded).
      - **Note:** the TIM records still say `slot = p`. The exe keeps `p == firstPageSlot` in practice: team 1 uses pages 0-1, so team 2 gets page 2 and slot 2. [c] [s]
   4. **`FUN_1000e370` (surface creation):**
      - `DDSURFACEDESC2 {dwSize 0x7c, dwFlags 0x1007 (CAPS|HEIGHT|WIDTH|PIXELFORMAT), 256×256, ddpf = 0x1013e420}`.
      - `sys`: caps `0x1800` (TEXTURE|SYSTEMMEMORY), created into `slot[i].sys`; on failure `Terminate(0x57, 0xcf, hr)`.
      - If `makeTexture`: `tex` with caps `0x5000` (TEXTURE|VIDEOMEMORY) when bit 0 of the driver record at `DAT_11b65e9c + 0x6a8 + drv*0x608` is set (probably "hardware device" [u]), else `0x1800`. The result is **unchecked**.
      - Lock `sys` (`DDLOCK_WAIT`; on failure `Terminate(0x57, 0x32)`); copy rows (16 bpp) or convert through `FUN_10007110` (32 bpp, §5.5); unlock.
      - If `makeTexture`: `tex->Blt(NULL, sys, NULL, 0, NULL)`.

   No mipmaps, no `DDSCAPS2_TEXTUREMANAGE`, no colour keys on the surfaces. The exe always passes `makeTexture = 1`.
4. **Globals.** `0x11c948e0`, `0x11a25d20`, `0x11b6a570`, `0x11cc0f58`, `0x11cc0f40`, `0x11ec1180`, `0x1004e370`, `0x1013e420`, `0x10131b5c`.
5. **D3D11.**
   - **Format.** One 256×256 `Texture2D` per slot, one mip:
     - `DXGI_FORMAT_B5G5R5A1_UNORM` has the same bits, but needs DXGI 1.2 support (check `CheckFormatSupport`);
     - otherwise expand to `B8G8R8A8_UNORM` with `c8 = c<<3 | c>>2` and alpha 0/255.
   - **Upload.** `UpdateSubresource` from the CPU staging page. Keep the 16-bit CPU page per slot as the master copy; it replaces the `sys` surface.
   - **Must keep.** The packer (§5.6) and the "first page to a given slot" rule.
   - **Drop.** The `sys`/`tex` pair, the format ranking and the 32-bit and 4444 paths.

### afD3dDeleteAllTextures (0x1000e7d0)
1. **Signature.** `void __cdecl afD3dDeleteAllTextures(void)`.
2. **exe usage.** Pointer `0x0054c5cc`, 2 call sites.
   - `FUN_004864c0` @`0x00486517`: the level/front-end teardown, right after `afReleaseAnimModels` and after the exe frees its `weapons.mad`/`fhats.mad` buffers.
   - `FUN_00482c30` @`0x00482cc0`: after the pig-map and debrief screens.
3. **Behaviour.**
   - `SetTexture(0, NULL)`.
   - For `i < pageCount`: release `slot[i].sys` and then `slot[i].tex`, setting both to NULL.
   - `free()` every blob in the list.
   - Reset the page count, blob count and TIM count to 0, and clear `_DAT_11ec106c` [u].
   - It does **not** reset the `SetTexture` cache 0x11ab9490. A new surface at the same address could skip a `SetTexture` (cosmetic, rare). Ground pages are not touched.
4. **Globals.** `0x11a25d20`, `0x11c948e0`, `0x11ec0f68`, `0x11ec1178`, `0x11ec0f60`.
5. **D3D11.** Release the slot SRVs and textures and the CPU TIM data, and reset the counters. Also reset any bound-SRV cache.

### afOverwriteTPage (0x1000f720)
1. **Signature.** `int __cdecl afOverwriteTPage(int srcSlot, int dstSlot)`, which returns `dstSlot`. (Ghidra's decompiler wrongly shows the `sys` pointer as the return value; the disassembly reloads `[esp+8]`.)
2. **exe usage.** Pointer `0x005386e0`. **Not called** (bind only).
3. **Behaviour.** `slot[dstSlot].tex->Blt(NULL, slot[srcSlot].sys, NULL, 0, NULL)`: copies one page's master into another page's texture. This is probably the golf-engine way of swapping skins; this game swaps the bound slot instead (§5.7).
4. **D3D11.** `CopyResource` or a re-upload from the CPU page. Can be a trivial stub.

### afOverwriteTexture (0x1000f750)
1. **Signature.** `int __cdecl afOverwriteTexture(IDirectDrawSurface7 *src, int unused, int tim)`, which returns 1.
2. **exe usage.** Pointer `0x00538078`, **1 call site**: `FUN_00459550` @`0x00459608` calls `afOverwriteTexture(hud->surf[idx], 0, game->faceTimsBase(+0x404))`. The source is an off-screen surface the exe made for the HUD; the target is the first TIM of `FACETIMS.MAD` (`aburst.tim`, 56×34).
   - Called inside the DLL by `afInitScanner(tim)` with the 64×64 radar surface `0x11ab9464`, which is in the texture format.
3. **Behaviour.**
   - Locks `src` and the page master `slot[PageFittedTim[tim].slot].sys` (`DDLOCK_WAIT`). It uses **DDSURFACEDESC with `dwSize 0x6c` on DX7 surfaces, an uninitialised descriptor and an unchecked HRESULT**. If `Lock` fails, it writes through garbage pointers.
   - Copies a `w×h` rectangle (the TIM size) from `src(0,0)` to the page at `(tim.x, tim.y)`:
     - if the R masks of the two surfaces differ (`src` is 565): `d = ((s>>1)&0x7fe0) | (s&0x1f)`, then `if (d) d |= 0x8000`, so black becomes transparent;
     - otherwise a raw copy.
   - Unlocks both, then re-`Blt`s the whole page into `slot.tex`.
4. **Globals.** `0x11ec1180`, `0x11a25d20`.
5. **D3D11.** Convert into the CPU page and `UpdateSubresource` the TIM rectangle (or the whole page). The source object is exe-owned 2D surface data; how the exe gets its surfaces is group A's topic. Keep the 565→555 rule and "black = transparent".

### afRestoreTextureSurfaces (0x1000e870)
1. **Signature.** `void __cdecl afRestoreTextureSurfaces(void)`.
2. **Callers.**
   - exe: pointer `0x0053805c`, 1 call site, `FUN_0047de90` @`0x0047e5d0`: after a match, the exe switches the video mode back to 640×480 (`FUN_0044f840`) and then calls this.
   - DLL: `CopyToScreen` (0x10003140), on the frame after a failed Flip/Blt, after `SelectDriverMode` (§8). It runs only if `IDirect3D7` (0x11ab9474) exists.
3. **Behaviour.** For each TIM slot `i < pageCount`, then each ground page `i < groundPageCount`:
   - If `tex` is NULL, skip the entry.
   - If `tex->IsLost()` and `tex->Restore()` fails: `tex->Release()`, `tex = NULL`, then `CreateSurface(256×256, ddpf, caps 0x4005000 = TEXTURE|VIDEOMEMORY|ALLOCONLOAD)`. **The result is not checked.**
   - If `sys->IsLost()`: `sys->Restore()`.
   - **Always** `tex->Blt(NULL, sys, NULL, 0, NULL)`, unchecked. Every page is re-uploaded even if nothing was lost.
   - If `CreateSurface` failed, `tex` is NULL and the `Blt` faults inside `_d3d.dll` at 0x1000e92b..0x1000e93c (TIM slots) or 0x1000ea04..0x1000ea15 (ground pages).
4. **Globals.** `0x11a25d20`, `0x11c948e0`, `0x1004cf90`, `0x11ab94b0`, `0x1013e420`.
5. **D3D11.** No equivalent is needed: D3D11 resources are not lost on Alt+Tab. Implement it as a no-op, or as "re-upload all pages from the CPU copies" after re-creating the device on `DXGI_ERROR_DEVICE_REMOVED`/`RESET`.

### afGetActualTextureFormat (0x1000ea40)
1. **Signature.** `DDPIXELFORMAT *__cdecl afGetActualTextureFormat(void)`, which returns `&0x1013e420`.
2. **exe usage.** Pointer `0x0054cfe0`. **Not called.**
3. **Behaviour.** Returns the chosen texture pixel format (§5.5).
4. **D3D11.** Return a static `DDPIXELFORMAT` describing A1R5G5B5: size 32, flags RGB|ALPHAPIXELS, 16 bits, masks 0x7c00/0x03e0/0x001f/0x8000.

### afAddToGroundTextures (0x1001c5a0)
1. **Signature.** `int __cdecl afAddToGroundTextures(void)`. The body is `xor eax,eax; ret`; the linker folded it with an identical CRT stub (Ghidra shows a caller, `FUN_1001b510`, which is CRT code).
2. **exe usage.** Pointer `0x005381a4`. **Not called.**
3. **Behaviour.** None; it returns 0. This is a leftover from the golf engine.
4. **D3D11.** A stub that returns 0.

### afAddToGroundTextures2 (0x100076e0)
1. **Signature.** `int __cdecl afAddToGroundTextures2(const void *tim, void *unused, int x, int y, int tileIndex)`, which always returns 0. Argument 2 is never read.
2. **exe usage.** Pointer `0x00538020`, **4 call sites** in the map loader `FUN_004a5250`:
   - `(wat01.tim, tileTab, -1, -1, 0)`
   - `(wat02.tim, tileTab, -1, -1, 1)`
   - `(ptg + 4 + i*576, tileTab + i*8, -1, -1, 2+i)` for every PTG tile; the exe also de-duplicates CLUTs for its own use
   - `(mine.tim, 0, -1, -1, lastIndex)`

   The exe frees each TIM buffer after the call. `afIfaceInit(adj, …)` has set `0x11b6a564 = adj` before this.
3. **Behaviour.**
   - Copies the TIM header, the CLUT (16 or 256 entries) and the pixels into temporary buffers. The pixel copy over-reads for 4 bpp.
   - Applies the adjustment to **all** CLUT entries.
   - Classifies the mode, builds a 4-byte palette and a 16-bit ARGB1555 tile (§5.4).
   - Puts the tile into a ground page with `FUN_10007210`:
     - If the page's `sys` surface does not exist yet: create `sys` 256×256 with caps 0x1800 (failure: `Terminate(0x57, 0xcf)`) and increment the ground page count.
     - Lock, copy the rows, unlock.
     - `tileMode[cell] = mode`, tile counter + 1.
   - On the first mode-2 tile it stores the water tile and its UVs (`0x10022050/54`, `0x10032c08..`). `0x1002204c = tileIndex + 2` for the first indexed tile. It counts tiles wider than 32.
   - There is no bound on 8 pages (392 tiles); `ARCHI` uses 241 tiles, 5 pages.
4. **Globals.** `0x1004cf90`, `0x1004cf80`, `0x11ab94b0`, `0x10022050/54`, `0x1002204c`, `0x10032c08..c24`, `0x11ab94d4`, `0x11b6a564`, `0x10022058`, `0x1002205c`.
5. **D3D11.**
   - Reproduce the mode rule and the colour math.
   - Keep the CPU ground pages: `afIsPointWatery` reads texels from them, and only `== 0x0000` counts.
   - The 33×34 grid can stay (simplest), or become an array texture if the ground renderer (group C) and `afIsPointWatery` change together.

### afUploadGroundTextures (0x10007530)
1. **Signature.** `void __cdecl afUploadGroundTextures(void)`.
2. **exe usage.** Pointer `0x005381c4`, **1 call site**: `FUN_004a5250` @`0x004a6847`, at the end of map loading.
3. **Behaviour.**
   - For each page `p < groundPageCount`: `CreateSurface(256×256, ddpf, caps 0x5000 = TEXTURE|VIDEOMEMORY)` into `gp[p].tex` (**unchecked**), then `gp[p].tex->Blt(NULL, gp[p].sys, …)`.
   - It always uses video memory, unlike the TIM pages.
   - Calling it twice leaks the old `tex`; a failed `CreateSurface` means a NULL `Blt`, which crashes.
4. **Globals.** `0x1004cf90`, `0x11ab94b0`, `0x1013e420`.
5. **D3D11.** Create or update the textures from the CPU pages.

### afDeleteGroundTextures (0x10007f60)
1. **Signature.** `void __cdecl afDeleteGroundTextures(void)`.
2. **exe usage.** Pointer `0x0053808c`, **1 call site**: `FUN_004a69c0` @`0x004a6b53` (map unload: frees the map objects and buffers; called from `FUN_004a5100` and before a reload in `FUN_004a5250`).
3. **Behaviour.**
   - Resets `0x1002204c` and `0x10022050` to -1, the water UV to -1.0f, the ground page count, the tile counter and the "big tile" count.
   - `SetTexture(0, NULL)`.
   - Releases `sys` then `tex` for **all 8** ground pages.
   - Releases the scanner surface `0x11ab9464`.
   - `tileMode[]` is not cleared.
4. **D3D11.** Free the ground textures and CPU pages, and the radar texture.

### afLoadAnimModels (0x1000d7c0)
1. **Signature.** `void __cdecl afLoadAnimModels(int pigTimBase)`.
2. **exe usage.** Pointer `0x00537fec`, **1 call site**: `FUN_00486030` @`0x00486261` calls `afLoadAnimModels(game->pigTimBase(+0x3e4))`, the base returned for the first team's MTD. That is 0 in practice, because `afD3dDeleteAllTextures` always runs first.
3. **Behaviour.**
   - Loads `chars\pig.hir` and copies 300 bytes to `0x11c948e8`, then frees the file.
   - Loads `Chars\MCAP.MAD` (kept in `0x11cc06f0`) and sets `AnimInfo[i].mcapData` for its 93 entries.
   - Loads `chars\british.mad` (kept in `0x11c948d8`). For each of its 27 triples `k`:
     - append the VTX vertices as `float(x, y, z, bone)` to the pool, **with no Y flip**;
     - `mesh = FUN_10011240(&pool[start], fac, pigTimBase, vtxSize/8)`, which relocates the FAC (`FUN_10012ee0`), converts it (`FUN_10012c00`) and computes the bounding box (`FUN_10010fa0`);
     - set `mesh->normals` (NO2) and `numNormals = size/16`;
     - `inst = FUN_100110e0(mesh, 0,0,0, 0,0,0)`;
     - `pig[k] = inst`, `type = 3`, `+0x4c = +0x50 = 0xff`, `+0x54 = 0`.
   - The classes are ordered ace, grunt, heavy, legend, medic, sapper, saboteur, sniper, spy, each with `pc*_hi`, `pc*_me` and `*_hi`. [d]
   - Pig meshes use *team-1 TIM indices*. Other teams are drawn by binding their page (§5.7).
4. **Globals.** `0x11c948e8`, `0x11cc06f0`, `0x11c948d8`, `0x11c94860`, the pools (§4.5), `0x1002c770` table.
5. **D3D11.**
   - Port as-is; this is plain data work. Keep the pool semantics, or replace them with dynamic arrays (the exe keeps the `ObjInst*` it gets).
   - Vertex buffers can be built once per mesh, but faces are re-batched per draw (CPU lighting and projection; group D).

### afReleaseAnimModels (0x1000d990)
1. **Signature.** `void __cdecl afReleaseAnimModels(void)`.
2. **exe usage.** Pointer `0x00538154`, **1 call site**: `FUN_004864c0` @`0x00486511`, after freeing the exe's `weapons.mad`/`fhats.mad` and before `afD3dDeleteAllTextures`.
3. **Behaviour.**
   - Resets the mesh and instance counts, the ammo ring indices and the ammo flag to 0, and the vertex pool count to 0.
   - Frees the `british.mad` and MCAP buffers.
   - Leaves dangling: `AnimInfo.mcapData`, `pig[]`, and every `ObjInst*` the exe still holds; the slots are reused.
   - `ObjInst.type`/`+0x54` of reused slots keep stale values (`afCreateObj2` does not write them).
4. **D3D11.** Same semantics. Optionally clear the stale fields to make the port deterministic.

### afCreateObj2 (0x1000d190)
1. **Signature.** `ObjInst *__cdecl afCreateObj2(void *madBase, const PkgEntry *dir3, int timBase, char flipY, char dropColourKeyedFaces)`. It returns the instance (EAX from `FUN_100110e0`; Ghidra shows `void`).
2. **exe usage.** Pointer `0x00538110`, **11 call sites**: `FUN_004544e0` (2), `FUN_0045e3a0` (4), `FUN_00482a00`, `FUN_00486030` (2) and `FUN_004866b0` (2). Examples:
   - **Sky** (`FUN_004866b0` @`0x004867b5`): `afCreateObj2(sky, sky, skyTimBase, 1, 1)`, and the same with `sky + 0x48` for the second dome. The exe then calls `afSetObjPos(o, 0,0,0, 0,0x800,0)` and `afScaleObj(o, 0x100000, 0x80000, 0x100000)`, and sets `o->type = 2` and `o->+0x54 = -1`.
   - **Hats and weapons** (`FUN_00486030`): `afCreateObj2(pkg, pkg + i*0x48, fhatsBase or weaponsBase, 0, 0)`, followed by `afScaleObj(...0x1000...)` and `afSetObjPos`.
   - **Map objects and ammo** (`FUN_0045e3a0`): `afCreateObj2(mapMad, mapMad + m*0x48, mapTimBase, 1, objFlag)` the first time a model is used. Later instances of the same model use `timBase = -1`, because the FAC was already relocated.
3. **Behaviour.**
   - `DAT_11cc0568 = dropColourKeyedFaces`.
   - Vertex count = `(FAC.offset - VTX.offset)/24`. This assumes VTX is immediately followed by NO2 with one normal per vertex.
   - Appends float vertices `(x, flipY ? -y : y, z)` to the pool. The bone (`w`) is **not written** and keeps stale pool contents.
   - `mesh = FUN_10011240(...)`. With `timBase != -1`:
     - `tex += timBase` (if `tex != -1`) for every triangle and quad;
     - then `r18`/`r1c` (TIM index, colour-key flag) are set and `tex` is replaced by `PageFittedTim[tex].slot`;
     - if `dropColourKeyedFaces` is set and the TIM has a colour key, `tex = -1`, so **the face is never drawn**;
     - blocks 1-2 (unused on PC) would also get their colours converted.
   - `mesh->normals` and `numNormals` are set as for anim models.
   - `inst = FUN_100110e0(mesh, 0,0,0, 0.5f, π, 0)`, `+0x4c = +0x50 = 0xff`.
   - If `afSetAmmoModelFlag(1)` is active, the mesh and instance come from the 128-entry rings, which wrap. Otherwise they come from the permanent pools.
   - **Ownership:** the FAC and NO2 stay in the caller's buffer, and the FAC is modified in place (the alpha field again on every draw). The exe keeps `weapons.mad`, `fhats.mad`, `skydome.mad` and map `.MAD` buffers until teardown. `FUN_00482a00` frees `propoint.mad` when its screen ends.
4. **Globals.** `0x11b8a7f8`/`0x11c94854`, the pools, `0x11cc0574`, `0x11cc0568`, `0x11ec1180`.
5. **D3D11.**
   - Must keep: in-place relocation semantics (or an equivalent cache keyed by `dir3`, so that `timBase == -1` reuses the converted data), `flipY` and the drop rule.
   - **Fix:** bound the vertex pool or reclaim it for the ring (§9).

### afGetKeyFrameList (0x1000f9d0)
1. **Signature.** `AnimKey *__cdecl afGetKeyFrameList(int anim)`, which returns `0x1002c778 + anim*0x58` (that is, `&AnimInfo[anim].keys`). No bounds check (93 entries).
2. **exe usage.** Pointer `0x005380f0`, 2 call sites: `FUN_00440e30` @`0x00440ec4` and `FUN_00440f10` @`0x00440f59`. These set the current and secondary animation of a pig:
   - the index is clamped (`>82` becomes 0) and stored with `afGetFrameStep(anim)+1`;
   - `list = afGetKeyFrameList(anim)` (NULL if `anim < 0`);
   - the exe counts keys until `keys[i].phase == 0` for `i > 0`, up to 6.
3. **Behaviour.** A static table accessor (§3.5). The meaning of `event` and `param` is [u]; they look like footstep or sound events.
4. **D3D11.** Copy the table verbatim (bytes `0x1002c770..0x1002e767`) and keep the pointer semantics.

### afGetAnimModelBBox (0x1000f9f0)
1. **Signature.** `float (*__cdecl afGetAnimModelBBox(int pigClass))[4]`, which returns `pig[pigClass*3]->mesh->bbox`: the 8 corners of the class's high-detail body (layout in §4.5).
2. **exe usage.** Pointer `0x0053889c`. **Not called.**
3. **D3D11.** Trivial; keep it for completeness.

### afSetAmmoModelFlag (0x1000d9f0)
1. **Signature.** `void __cdecl afSetAmmoModelFlag(int useAmmoRing)`, which sets `0x11cc0574`.
2. **exe usage.** Pointer `0x0053809c`, 3 call sites in `FUN_0045e3a0`:
   - `afSetAmmoModelFlag(0x184 <= objType && objType <= 0x1ba)`, i.e. projectile types, at @`0x0045e3bd`;
   - then 1-2 calls to `afCreateObj2`;
   - then `afSetAmmoModelFlag(0)` at @`0x0045e503` or @`0x0045e51c`.
3. **Behaviour.** While the flag is set, `FUN_10011240`/`FUN_100110e0` take meshes and instances from the 128-entry rings, which overwrite the oldest silently. `afReleaseAnimModels` clears the flag.
4. **D3D11.** Same semantics. A proper free-list is safe, but more than 128 live projectiles alias in the original as well.

---

## 7. Texture lifecycle (exe order)

1. **Device and format.** `SelectDriverMode` calls `FUN_10006240`, which creates the `IDirect3D7` and the HAL device and runs `EnumTextureFormats`, which picks A1R5G5B5 (§5.5).
2. **Front end** (`FUN_0047de90`):
   - `FUN_00486030(1)`:
     1. `afIfaceInit(adj, …)` sets `0x11b6a564`.
     2. For each of the 7 nations: `afLoadTims(nation.mtd, 0, adj)`.
        - Nation 0 is followed by `afLoadTims(faces.mtd)` and `afD3dUploadAllTextures(base0, 1, 0, 0)`, which fills **slots 0-1**.
        - Nation `i ≥ 1` uses `afD3dUploadAllTextures(base_i, 1, 0, i+1)`; only its first page goes to **slot i+1**.
     3. `afLoadAnimModels(base0)`.
     4. `weapons.mtd` and `fhats.mtd` go through `afLoadTims`, then `afD3dUploadAllTextures(weaponsBase, 1, 0xff, -1)`.
     5. Hat and weapon objects through `afCreateObj2`.
   - `FUN_00486670`: `fefxtims.mtd` is loaded and uploaded.
3. **Level load** (`FUN_004852c0`, after teardown):
   - `FUN_00486030(0)` loads the teams in the match.
   - Nine `afLoadTims` calls (HUD, fonts, map MTD with weather `adj`, …), then **one** sorted `afD3dUploadAllTextures`.
   - The map: `afDeleteGroundTextures` if a map was loaded, `afAddToGroundTextures2` for water, PTG and mine tiles, `afSetMap`, the objects, then `afUploadGroundTextures`.
   - The sky: `afLoadTims(<weather>.mad)` plus an upload, and two `afCreateObj2` calls.
4. **Runtime.**
   - `afCreateObj2` for map objects and projectiles (ammo ring).
   - `afOverwriteTexture` for the HUD surface into a FACETIMS TIM.
   - `afInitScanner` uses `afOverwriteTexture` internally for the radar.
   - `CopyToScreen` triggers the restore on surface loss (§8).
5. **Overwrite.** `afOverwriteTexture` updates the master page and re-`Blt`s it. `afOverwriteTPage` is unused.
6. **Restore.** `afRestoreTextureSurfaces` re-creates lost textures and always re-uploads from the masters. It runs after the post-match mode switch (exe) and after a failed present (DLL).
7. **Teardown** (`FUN_004864c0`): the exe frees its MAD buffers, then `afReleaseAnimModels`, then `afD3dDeleteAllTextures`. The ground textures go away with the map object (`afDeleteGroundTextures`).

The page budget is 32. Every load set must fit, or the game exits with `"Cannot fit tims"`. For example, the pig-map and debrief screens (`FUN_00483980`, `FUN_00482a00`) upload on top of the front-end pages without deleting first.

---

## 8. Surface loss and Alt+Tab

1. **`CopyToScreen` (0x10003140).**
   - The present is `primary->Blt` in windowed mode and `primary->Flip(NULL, DDFLIP_WAIT)` in full screen. **Any** non-zero HRESULT, not only `DDERR_SURFACELOST`, sets `DAT_11ad9c38 = 1`.
   - On the next call: clear the flag, set the saved mode to -1, call `SelectDriverMode(hwnd, savedMode)` and then `afRestoreTextureSurfaces()`.
   - `SelectDriverMode` re-creates the display surfaces and the device and re-enumerates formats; the `IDirectDraw7` is kept, so the texture surfaces stay valid but lost.
   - There is no `WM_ACTIVATEAPP` or `TestCooperativeLevel` gating. While the game is minimised or not in the foreground, this repeats every frame.
2. **`afRestoreTextureSurfaces` crash path** [c]:
   - If `Restore()` fails (for example `DDERR_WRONGMODE` or `DDERR_NOEXCLUSIVEMODE` while another application owns the display), the texture is released and re-created without a result check.
   - If that `CreateSurface` fails, `slot.tex` is NULL and the following `tex->Blt` dereferences NULL inside `_d3d.dll`.
   - The same unchecked pattern is in `afUploadGroundTextures` and in `FUN_1000e370` (texture surface). `afOverwriteTexture`'s unchecked `Lock` writes through an uninitialised descriptor.
3. **Relation to the logged crash** [u]. `WORKLOG.md` records crashes in `DDRAW.dll` (dgVoodoo) at +0x2f3ce. This group cannot be proven to be the trigger: a fault on NULL `tex` would sit in `_d3d.dll`. A `Blt` or `Lock` on a surface whose device lost exclusive mode would sit in ddraw.
4. **D3D11.** Nothing equivalent is needed. Flip-model swap chains do not lose resources; skip `Present` while minimised and re-create everything from the CPU copies only after device removal.

---

## 9. D3D11 reimplementation notes

**Recommended formats**
- Pages: `DXGI_FORMAT_B5G5R5A1_UNORM` if `CheckFormatSupport` reports `SHADER_SAMPLE`; otherwise `DXGI_FORMAT_B8G8R8A8_UNORM` with 5→8 bit replication (`c<<3|c>>2`) and alpha 0/255. Ground pages and the radar use the same.
- No mipmaps.
- Sampler: linear min/mag, point mip, WRAP addressing.
- Pixel shader: `clip(texA*diffA - 8/255)`.
- Blend: SRC_ALPHA / INV_SRC_ALPHA.
- Keep the half-texel UV formulas as they are. For XYZRHW positions, apply the D3D7→D3D10 pixel-centre shift (−0.5) in the vertex shader (group D).

**Must reproduce exactly**
- The global TIM numbering and load order, including 118/119 → 120+e / 127+m.
- The CLUT adjustment, including the "first 16 entries only" quirk for TIM packages and the +3/+8 biases.
- STP dropping and the colour-key rule (§5.3), and the ground tile modes and texel rule (§5.4).
- The packer (gap, step 2, scan order, sort, first fit) and the "first page to slot N" rule; the team pages depend on identical layouts.
- The `PageFittedTim` layout (the exe reads it).
- FAC relocation semantics (`timBase -1`, drop rule, `flipY`), the ammo ring wrap and the bounding-box corner order.
- `AnimInfo` and the keys table contents.

**Can be simplified or dropped**
- Format enumeration, the `sys`/`tex` surface pair, the 565, P8 and 32-bit conversion paths (dead or buggy), and the index swap (colour-preserving).
- The `memPkg` and "MAD" paths of `afLoadTims` (unused).
- `afAddToGroundTextures`, `afOverwriteTPage`, `afGetActualTextureFormat` and `afGetAnimModelBBox`: stubs or trivial versions.
- `afRestoreTextureSurfaces`: a no-op.

**Keep CPU copies** of every page (TIM and ground). They are needed for `afOverwriteTexture` partial updates, `afIsPointWatery` texel reads and device-removed recovery.

**Original bugs worth fixing, without changing visible behaviour**
- **Vertex pool overflow.**
  - `afCreateObj2` appends vertices for *every* instance, including ammo-ring and `timBase = -1` repeats, and the pool only resets in `afReleaseAnimModels`. About 68K vertices fit, with no check. [u: depends on how often projectiles are created]
  - Past the end lie the identity template (0x11c947f0), the pool counter, the `pig[]` pointers and the texture page count (0x11c948e0), so an overflow would corrupt them.
  - Likely to hit only in very long matches. Use growable storage.
- **Unbounded tables.** Meshes (674), instances (512), ground tiles (392), the blob list and the 512-vertex colour-keyed batches have no bounds checks.
- **Unchecked results.** `CreateSurface`, `Blt` and `Lock` results are ignored (crash paths in §8).
- **Heap misuse.** The `memPkg` free bug.

---

## 10. Open questions [u]
- Bit 0 of the driver record at `DAT_11b65e9c + 0x6a8 + drv*0x608`: is it "hardware device"? It decides whether TIM textures go to video or system memory (group A).
- `AnimKey.event` / `param`, and the 32-byte MCAP frame header.
- `ObjInst` fields +0x00, +0x34..+0x3c, +0x54 and +0x58; `PageFittedTim +0x24`; `TimInfo +0x08`; `GroundPage +0x08/+0x0c`; `_DAT_11ec106c`.
- How `FUN_004544e0` (HUD, 15 `afGetPageFittedTim` calls and 2 `afCreateObj2` calls) uses its results. Not analysed in detail.
- Which `afDrawObj` argument values the exe passes for team pages (group D).
- The actual site of the logged Alt+Tab crash (`DDRAW.dll`+0x2f3ce).
