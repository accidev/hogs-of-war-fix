# Renderer `_d3d.dll` — Group C: terrain/map, sky, fog, weather, lighting, camera & transforms

Sources: Ghidra programs `_d3d.dll` (image base `0x10000000`) and `warhogs_unwrapped.exe` (image base `0x00400000`). The analysis was read-only. Exe call sites were found by scanning the exe's `.text` for the 4-byte address of each import pointer (all hits are `FF 15` = `CALL [ptr]`, apart from the single store in `FUN_004ac430`). DLL cross-references were found the same way, by scanning the DLL code for absolute addresses.

Labels used below: **[fact]** means read directly from the code, **[inferred]** means a strong deduction, **[uncertain]** means it needs runtime confirmation.

---

## 0. Key findings

1. **The DLL's own height queries are dead code in Hogs of War.** `GroundZ` and `afGetMapHeight` are bound but **never called** by the exe. Gameplay heights come from the exe's own integer routine `FUN_004a5140(map, short x, short z)` (§2.4), which reads the same 64×64 tile array that the exe passes to `afSetMap`. The DLL only renders those heights. The rendered triangle split matches the exe's height function.
2. **`afIsPointWatery` is the only export in this group that affects gameplay.** The exe calls it from 15 gameplay functions through `FUN_004a6fa0` (whenever a tile's attribute byte has bit 5 set), and also uses it to suppress decals. It answers by **reading 9 texels of the ground texture's system-memory copy**. A reimplementation must reproduce it bit for bit, including the tile rotation and mirroring and the 9-point sample pattern.
3. **All transform and lighting work is done on the CPU.** Terrain, water, sky backdrop and fog quads are submitted as `D3DTLVERTEX` (FVF `0x1C4`) through `IDirect3DDevice7::DrawPrimitive`. The only D3D transform ever set is a projection matrix in `afSetFog`, which is used only so that the driver performs W-based table fog.
4. **The projection is custom and PSX-style.** If view depth `Zv <= 937`, `1/Zv` is replaced by its tangent line at 937: `r = 2/937 − Zv/937²`. With this, vertices near or behind the camera never blow up and no near-plane clipping is needed. Output is `rhw = 8·r` and `z = Zv·2e-6` (z is linear in view depth).
5. **Coordinates.** DLL world = right-handed, **Z-up**: `X = exe x`, `Y = exe z`, `Z = exe y` (height). Angles are 12-bit (4096 = 360°) and are read from float sin/cos tables of 4096 entries. The map is 64×64 tiles of 512 units, centred on the origin (±16384).
6. **Lighting** is computed per terrain vertex once, when the map or the weather changes. It uses central-difference normals and the intensity `I = 0.4 + 0.6·max(0,N·L)^15.875`, quantised to 1/255. The result is a lerp between a "dark" and a "lit" RGB that come from `afSetWeatherValues`.
7. **Sky.** `afSetSky` and its 16×64 sky-tile grid are golf leftovers: the exe never calls `afSetSky` and nothing reads the grid. The HoW sky is drawn in three parts:
   - a full-window **fog-coloured backdrop quad**;
   - **two `skydome.mad` objects** (object type 2). `afAddObjectToSortList` puts every type-2 object into bucket 9999. Each frame the exe's `FUN_004501b0` resets both domes to an identity rotation at X = Z = 0, with a vertical offset that is a linear function of a value converted with `ftol` (probably the camera height) [inferred], and queues them [fact]. They are drawn first, with fog off;
   - optionally a black overlay during the "sky fade" (overview-map camera).
8. **Rain and snow particles are not in the DLL** (the exe has `UpdateRain`, `rain.mtd` and `snow.mtd`). `afSetWeatherValues` only sets colours, the fog colour and the light direction. Six "sky colour" values in its struct are stored but never read.
9. **Terrain visibility in normal play comes from 10 bitmaps** (`language\tims\nview000..009.bmp`). `SetGolfFlags` loads them as side effect (`FUN_10014120`). The exe calls `GetGolfFlags`→`SetGolfFlags` once, without changing anything, only to trigger this load and to set the draw distance to 30000.
10. **FPU precision risk [inferred].** `SetCooperativeLevel` uses `0x51`/`0x08`, i.e. no `DDSCL_FPUPRESERVE` and no `DDSCL_FPUSETUP`. Under DX7 the x87 FPU is therefore most likely left in 24-bit (single) precision for the whole process, **including the exe's floating-point gameplay**. A replacement DLL that keeps the original exe should set `_controlfp(_PC_24, _MCW_PC)` after device creation, so the exe's float math behaves as before. Verify this at runtime.

---

## 1. Conventions

| Item | Value |
|---|---|
| Calling convention | Every export in this group is `__cdecl` (plain `RET`; the exe pops the arguments). [fact] |
| World axes (DLL) | `X` = exe x, `Y` = exe z (second ground axis), `Z`/"H" = exe y (height, up). `X×Y = Z`, so right-handed. The exe's `(x, y-up, z)` is the same physical space relabelled (left-handed, Y-up). [fact: `AddDisplayWindow(..., (float)x, (float)z, (float)y, ...)` and `afTransformPoint(x, y, z)` reading arg 2 as the height] |
| Map | 64×64 tiles, tile = 512 units, X,Y ∈ [−16384, +16384]. 65×65 vertices. |
| Exe tile ↔ DLL quad | Exe tile `(col,row)`, with row index = `63 − ((Y+16384)>>9)` ↔ DLL quad `(xi=col, yi=63−row)`. Exe vertex `(col,row)` (the tile's corner on the min-row side, i.e. its +Y edge) ↔ DLL vertex `(xi=col, yi=64−row)`. |
| Heights | DLL `H = 2 × tile.height` (int16 in the 20-byte record). The exe has already halved the PMG value (`/2`, truncating toward zero), so world height = `2·trunc(pmg/2)`. |
| Angles | `uint & 0xFFF`, 4096 = 2π. Tables `cos[i]=cos(i·2π/4096)` at `0x11ada2e0` and `sin[i]` at `0x11b66050` (`float[4096]`), filled by `FUN_10010410` (called from `InitialiseGolfLibraryI`, which `PowerUp` calls) with x87 `fcos`/`fsin`, then rounded to float. |
| Camera | `yaw` measured from +X toward +Y; `pitch` > 0 looks up; roll `DAT_11b6a270` is never written (always 0). Forward `F=(cp·cy, cp·sy, sp)`, screen-right `R=(sy, −cy, 0)`, screen-up `U=(−cy·sp, −sy·sp, cp)`, where `c*`/`s*` are cos/sin of yaw (y) and pitch (p). |
| Screen | `sx = cx + SX·r`, `sy = cy − SY·r`, where `SX = fx·(d·R)`, `SY = fy·(d·U)`, `Zv = d·F`, and `d = P − cam`. `cx = W/2`, `cy = H/2` (integers). `fx = W·zoom·sx_scale/20`, `fy = H·zoom·sy_scale/15`; default zoom is 15, so `fx = 0.75·W` and `fy = H`. That gives an H-FOV of 67.4° and a V-FOV of 53.1°. |
| Depth | `z = max(Zv·2e-6, 1.527e-5)` (linear in eye depth); `rhw = 8·r` (`r = 1/Zv`, or the tangent-line value when `Zv ≤ 937`). |
| Fixed point | Only in the interfaces: exe positions as int/short; afSetZoom 0..4096; colours as 0..255 ints; texture slots on a 33×34-texel grid. Internally the DLL uses float/x87. |

---

## 2. Data structures

### 2.1 Tile record passed by the exe (`MapTile20`, 20 bytes, array `[64][64]`, row-major)
The exe builds it in `FUN_004a5250` (Map::Load), from the 16×16 PMG chunks (0x170 bytes each): 4×4 tiles per chunk. Each record takes the corner vertex at `(row,col)` of the chunk's 5×5 vertex grid, plus 16 bytes copied from `chunk+0x6c+16·tile`.

| Off | Type | Meaning (renderer view) |
|---|---|---|
| +0x00 | int16 | Vertex height. The PMG value was halved by the exe; the DLL multiplies by 2. |
| +0x02 | u8 | Vertex light from the PMG (scaled ×3/4 or ×1/2 according to the map mode `map+0x34`). **Not used by this DLL.** |
| +0x03 | u8 | Set by the exe to 200/90/100 depending on mode. Not used by this DLL. |
| +0x04..0x0D | — | Not used by the renderer. |
| +0x0E | u8 | Tile attributes. Bits 0-4 = surface type (**11 = toxic water**, which turns the water plane green); bit 5 = "may contain water" (the exe tests it before calling `afIsPointWatery`); bit 6 and the sign bit of the u16 are used only by the exe (`FUN_0049ba10` spawns object 0x1AC/0x1AD on them; probably mines [inferred]). The DLL also keeps `&0x7F` for the radar. |
| +0x0F | u8 | Second attribute byte (the exe's "slip"); the DLL keeps it inside the 16-bit word at vertex +0x24. |
| +0x12 | u16 | Texture word: bit 0 = mirror U; bits 1-2 = rotation (×90°); bits 8-15 = texture number relative to the map texture base `DAT_1002204c`. |

### 2.2 DLL terrain vertex/quad record (`TerrainVtx`, 0x68 = 104 bytes)
Grid `TerrainVtx g[65*65]` at **`0x11afaa30`**, index = `xi*65 + yi` (xi is the major index). Each record is both the vertex `(xi,yi)` and the quad `(xi..xi+1, yi..yi+1)` whose tile data it holds.

| Off | Type | Meaning | Written by |
|---|---|---|---|
| +0x00 | float | X = 512·xi − 16384 | afSetMap |
| +0x04 | float | Y = 512·yi − 16384 | afSetMap |
| +0x08 | float | H (height) | afSetMap / afAdjustMapHeights |
| +0x0C | D3DCOLOR | Lit vertex colour (`0xFF000000\|RGB`) | SetGroundLightDirection |
| +0x10 | float | Screen x (window-relative) | FUN_10013f20 / FUN_10013a00 |
| +0x14 | float | Screen y | 〃 |
| +0x20 | int | Frame stamp for the transform cache (`DAT_11ed0180`) | 〃 |
| +0x24 | u32 | Copy of tile word +0x0E (u16) | afSetMap |
| +0x28 | float | View depth Zv | transform |
| +0x2C | float | rhw | transform |
| +0x30 | float | z (device) | transform |
| +0x34 | int | UV flags `((w>>1&3) \| (w&1)<<3) << 8` (rot in bits 8-9, vflip in bit 10 (never set from data), mirror in bit 11) | afSetMap / afAdjustMapTile |
| +0x38 | int | `tile[+0x0E] & 0x7F` (read by `afInitScanner`, the radar) | afSetMap |
| +0x3C | int | Global ground-texture number = `base + (w>>8)` | afSetMap / afAdjustMapTile |
| +0x40 | int16 | Texture page = tex/49 | 〃 |
| +0x42 | int16 | Slot within the page = tex%49 (7×7 grid) | 〃 |
| +0x44 | int16 | Wave flag (0 for terrain, 1 for water-grid vertices) | afSetMap / FUN_1000b330 |
| +0x48..0x54 | float[4] | u of corners 0..3 (TL, TR, BR, BL) | afSetMap / FUN_10001000 |
| +0x58..0x64 | float[4] | v of corners 0..3 | 〃 |

Slot UVs: `col=slot%7`, `row=slot/7`; `u0=33·col/256+1/512`, `u1=33·col/256+0.125−1/512`, `v0=34·row/256+1/512`, `v1=34·row/256+0.125−1/512`. Pages are 256×256 with 32×32 texels per slot at a 33/34-texel pitch, inset by half a texel. Before transformation the corners are TL=(u0,v0), TR=(u1,v0), BR=(u1,v1), BL=(u0,v1). Then `FUN_10001000` applies, in this order:
- mirror: swap corners 0↔1 and 2↔3;
- vflip: swap 0↔3 and 1↔2;
- rotation 1: new = (old3, old0, old1, old2); rotation 2: swap 0↔2 and 1↔3; rotation 3: new = (old1, old2, old3, old0).

World mapping: corner TL = vertex `(xi, yi+1)`, TR = `(xi+1, yi+1)`, BR = `(xi+1, yi)`, BL = `(xi, yi)`. So u grows with +X and v grows with −Y.

### 2.3 Other grids
- **Water grid**, 33×33 `TerrainVtx` at `0x11abe1a0` (stride 33), built by `FUN_1000b330`: X = 1024·i − 16384, Y = 1024·j − 16380, H = `minH − 256` inside and `minH` on the border. +0x34 is set to 0xFFFFFFFF (not used for water), texture #1 (`wat02`, page 0, slot 1; u ∈ [0.1309, 0.2520], v ∈ [0.00195, 0.12305]), wave flag = 1, and type 31 when the map contains toxic tiles.
- **Sky grid**, 16×64 `TerrainVtx` at `0x10032f80` (`afSetSky`). It is never read (dead).

### 2.4 The exe's gameplay height function (for reference) — `FUN_004a5140(map, short x, short z)`
```
clamp x,z to ±0x3DFF (=15871)
fx = x & 511;  fz = (-z) & 511;  col = (x+16384)>>9;  row = (16384-z)>>9      // tile rows grow toward -z
h(c,r) = tiles[r*64+c].height (int16)
if (fz < 512-fx)  H = h(col,row) + ((h(col+1,row)-h00)*fx>>9) + ((h(col,row+1)-h00)*fz>>9)
else              H = h11 + ((h(col,row+1)-h11)*(512-fx)>>9) + ((h(col+1,row)-h11)*(512-fz)>>9)   // h11=h(col+1,row+1)
return H*2
```
This is the authoritative height for gameplay (decals use it too, with a +5 offset). Its triangle diagonal runs from `(col+1,row)` to `(col,row+1)`. That is exactly the diagonal of the DLL's terrain triangles (§3.4).

### 2.5 Important globals (this group)

| Address | Type | Meaning |
|---|---|---|
| `0x1002c524/528/52c` | float | Camera X, Y, Z (defaults −100, 16384, 80000) |
| `0x1002c530` | int | Camera yaw (default 0x400) |
| `0x11b6a26c` | int | Camera pitch |
| `0x1002c518` / `0x1002be6c` | int | Current zoom / target zoom (15..60) |
| `0x11ab9510` / `0x11ab9508` | float | fx / fy (focal lengths in pixels) |
| `0x1002c51c` / `0x1002c520` | int | cx, cy |
| `0x11ad9c44..0x11ad9c64` | float | Unscaled view rotation (FUN_10013830) |
| `0x11abd964,968,96c,970`, `0x11abe178,180,188,18c,190` | float | Scaled view rows: SX = {964,970,188}·d, SY = {968,178,18c}·d, Zv = {96c,180,190}·d |
| `0x11b8a570` / `0x11b8a7c8` | float[12] | Look-at matrix and its inverse in (x,h,y) order, for models (FUN_10002c00) |
| `0x10032c28` | int | Animation tick (4th argument of DisplayCurrentHole), drives the water waves |
| `0x11ed0180` | int | Frame counter / transform-cache stamp |
| `0x11ed0188` | int16[10][1024] | nview visibility masks (2 KB each) |
| `0x1004cf88` | int | Map flag (afSetMapFlag) |
| `0x11c94824` / `0x1002be68` | int | Sky-fade request / fade level 0..255 (initial 255) |
| `0x10022040` | int | Terrain path: 1 = near path (masks), 0 = overview path (frustum footprint). Initial 1. |
| `0x11c94844` / `0x1002c540` | int / float | Ground subdivision on / tile cull depth (−15 or −768) |
| `0x11c947e0`, `0x11ab8ca8`, `0x1011d6d8` | D3DCOLOR, float, float | Fog colour (also the backdrop colour), fog start, fog end |
| `0x11b8a6f8` | int[15] | Copy of the weather values |
| `0x11c94828` / `0x11c9482c` | int | Light azimuth / light elevation |
| `0x11ab9244/48/40` | float | Light vector L = −(ca·cb, sa·cb, sb); scaled by −255 at the end (afterwards unused) |
| `0x1013e654/64c/650` | float | Ambient p3, (1−p3), exponent k |
| `0x1013e6a0/65c/648`, `0x11ab8b08/04/0af8` | int / float | Dark RGB base and (lit−dark) deltas. These are also read by model lighting (`FUN_10008080`, `FUN_100085e0`). [inferred] |
| `0x10022048` / `0x1004cf8c` | float | Minimum / maximum terrain height (minimum initialised to 262144.0 in afIfaceInit) |
| `0x1004d630` / `0x1004d634` | int | Map contains toxic tiles / map uses water textures (the latter is never cleared: "sticky") |
| `0x1002204c` / `0x1004cf80` | int | First map-texture number / count of loaded ground textures |
| `0x1004cf90 + p*0xD4` | struct ×32 | Ground page: `+0` IDirectDrawSurface7* (texture), `+4` system-memory copy (DDSCAPS 0x1800), `+0x10` `int waterMode[49]` (0 = solid, 1 = partly transparent, 2 = full water) |
| `0x1013e440` / `0x1013ed08 + p*0xC7380` | int[32] / D3DTLVERTEX[25500] | Per-page batch counts and buffers |
| `0x11ab9470` | IDirect3DDevice7* | Device |
| `0x1002bf4c` / `0x11ab9490` | int / ptr | Render-state preset cache / current texture |
| `0x1002bfe8` | int[19] | "Golf flags" (default `{3,1,…,1}`) |
| `0x1002c534` / `0x1002c538` | float | Far distances (terrain footprint / object cull): 400000 by default, 30000 after SetGolfFlags or afGetDDHandles |
| `0x11c7eee8` | float[3][…] | Static output buffer shared by afRotPtsInt and afDrawObj |

---

## 3. Pipeline: how the world is drawn

### 3.1 Level setup (exe `FUN_00485550` → `FUN_004852c0` → Map::Load)
1. Sky/time type `DAT_00520708` (0 coldsky/snow, 1 desert, 2-4 and 10 night, 5 ominous, 6 sunny, 7 sunrise, 8 sunset, 9 toy) selects the fog values. For example, type 0 gives fog 238..4524 with colour F8F8F8; type 5 gives 425..2125 with colour 8FAFCD.
2. `afSetFog(on, start, end, colour)`, then `afSetWeatherValues(&env, 19, fogOn, G[0x4d0], 1)`. The `1` re-lights the terrain.
3. Map::Load `FUN_004a5250`: reads the PMG, then calls `afAddToGroundTextures2` for wat01, wat02, the PTG textures and mine.tim. It builds `MapTile20[64][64]`, then calls **`afSetMap(tiles)`** and `afUploadGroundTextures()`.
4. `FUN_004866b0`: sky texture set plus `chars\skydome.mad`. It creates **two** objects with `afCreateObj2`, scales them by (16, 8, 16) and sets `type(+0x48) = 2`.

### 3.2 Per frame (exe `FUN_0044e290`)
`Begin2D` (Z clear, zoom easing) → `AddDisplayWindow(W, H, 0, 0, 1, 1, x, z, y, yaw=cam+0x8c, pitch=cam+0x88)` → `Begin3D` (sets the viewport; loads the camera from window 0 via `FUN_100075d0`; `FUN_10013830(yaw, pitch, 0)` builds the view rows) → the exe queues objects (sky objects go to bucket 9999) → **`DisplayCurrentHole(1, …, tick)`** → `End3D`, `End2D`, `CopyToScreen`.

`DisplayCurrentHole` → `FUN_10001540` → `FUN_10003380(9999, 0)` does the following:
1. **Overview fade.** If `afStartSkyFade(1)` is active, the level goes down by 16 per frame (the near path is switched off immediately). If it is 0, the level goes up by 16 per frame, and when it reaches 255 the near path is switched back on. While the fade runs, `FOGTABLESTART/END = start/end + (255−level)·30`.
2. **Backdrop** (`FUN_1000b6e0`): a TRIANGLEFAN covering window 0, untextured, coloured with the fog colour, z = 0.99999899, rhw = 0.2, no blending. If level < 255, a second quad is drawn in black with alpha (255−level), alpha-blended.
3. **Sky**: the first two entries of bucket 9999 are drawn with `afDrawObj` (type 2 means fog off). There is no NULL check, so the exe must always queue both. The bucket is then cleared.
4. Frame stamp + 1.
5. **Water plane** (`FUN_1000b440`, only if `DAT_1004d634`). Each frame, interior grid points slide +4 units in Y, with a wrap of −1020 every 1024. Each vertex goes through the wave transform (§3.3). It is drawn as 32×32 TRIANGLEFANs with page-0 texture, diffuse `0x808484FF`, or `0x8000FF00` if the map is toxic. Blending is on.
6. **Terrain** (§3.4), batched by texture page, then `DrawPrimitive(TRIANGLELIST, 0x1C4, buf, n, 0)` for each page.
7. `FUN_1000c040` / `FUN_1000c310` (PlacePolyInWorld / SetLines, other group), then sort buckets 9999→0 (objects, models, text, lines, 2D polygons in the world), then the radar `FUN_10009810`.

Render-state preset `0xd` (used for terrain, water and sky): SHADEMODE = GOURAUD, ALPHABLENDENABLE = 1, TSS0 ALPHAOP = MODULATE. Preset 1 (used for the backdrop): FLAT, blending off, ALPHAOP = SELECTARG1, no texture. Device init (`FUN_10006380`, group A/B) sets TEXTUREPERSPECTIVE = 1, CULLMODE = CCW, ZFUNC = LESSEQUAL, alpha test, and LIGHTING = FALSE.

### 3.3 Vertex transform (`FUN_10013f20`; `FUN_10013a00` is an inlined copy)
```
if (v.stamp == frame) return;                        // per-frame cache
x' = v.x + 128*cos[(int)(((4*tick)%4095) + v.x + 16384) & 0xFFF] * v.waveFlag   // sign-preserving &0xFFF
y' = v.y + 100*sin[(int)(((2*tick)%4095) + v.x + 16384) & 0xFFF] * v.waveFlag   // both phases use x
d  = (x'-camX, y'-camY, v.h-camZ)
SX = d·(M964,M970,M188);  SY = d·(M968,M178,M18C);  Zv = d·(M96C,M180,M190)
r  = (Zv > 937) ? 1/Zv : 0.0021344717 - Zv*1.138992379e-6        // tangent of 1/z at 937
v.sx = cx + SX*r;  v.sy = cy - SY*r;  v.Zv = Zv;  v.rhw = 8*r;  v.z = max(Zv*2e-6, 1.527e-5)
```
`r > 0` for every Zv (its minimum is 1/937 at Zv = 937). Points behind the camera are therefore projected "in front" without mirroring, which is why per-tile culling (§3.4) is required.

### 3.4 Terrain renderer
- **Per quad** (`FUN_1000a730` / the inline loop): skip the quad if the texture's `waterMode == 2` (full water; the water plane shows through). Skip it as well if `max(Zv of 4 corners) ≤ cullDepth` (−15, or −768 when subdivision is on).
- **Emission** (`FUN_1000a910`). 4 TL vertices, each `(sx, sy, z, rhw, colour = vtx+0x0C, specular = 0xFFFFFFFF, u, v)`, ordered v0 = `(xi+1,yi+1)` with UV corner 1, v1 = `(xi+1,yi)` with corner 2, v2 = `(xi,yi)` with corner 3, v3 = `(xi,yi+1)` with corner 0. Triangles `(v0,v1,v2)` and `(v2,v3,v0)`: clockwise on screen, diagonal from `(xi,yi)` to `(xi+1,yi+1)`.
- **LOD** (only with `afSetGroundSubDiv(1)`): if max Zv < 1000, split into 4×4 sub-quads; if < 2000, into 2×2. The split is done **in screen space**, interpolating sx, sy, z, rhw, u, v linearly. Colours are not interpolated: each sub-vertex copies the colour of an original corner.
- **Near path** (normal play, `DAT_10022040 = 1`). Blocks are 4×4 tiles (2048 units). The window is 33×33 blocks centred on the camera block `((camX+16384)/2048, (camY+16384)/2048)`. A block is drawn when `nview[m][idx] == 0`, where:
  - `m = |trunc(9·c)|`, with `c = cos(yaw)` snapped (|c| > 0.95 becomes ±1, |c| < 0.05 becomes 0);
  - the quadrant `q` is chosen from `0x400 < yaw < 0xC00` and `yaw < 0x800`;
  - `idx` is `(34−r)·33−c`, `(r+1)·33−c`, `c+(33−r)·33` or `c+r·33` for q = 0..3 (r, c = window row/column, as decompiled).
- **Overview path** (`afStartSkyFade(1)`, `DAT_10022040 = 0`). `FUN_10001760` computes a ground trapezoid from the camera: lateral extent ±dist·160/zoom, far distance `DAT_1002c534`. With `afSetMapFlag(1)` the trapezoid is replaced by the fixed tile range [2..62]. `FUN_100019b0` converts it into per-row column spans and `FUN_10013a00` pre-transforms the vertices.

---

## 4. Exports that affect GAMEPLAY

### afIsPointWatery — `0x10010210`
- **Prototype:** `BOOL8 __cdecl afIsPointWatery(int x, int y)`, with exe `(x, z)`. The result is returned in AL. [fact]
- **Exe:** pointer `0x00538128`, 2 call sites:
  - `0x004a6fe2` in `FUN_004a6fa0(map, short x, short z)` → `afIsPointWatery((int)x, (int)z)`. It is only called when `tiles[(63−((z+16384)>>9&63))*64 + ((x+16384)>>9&63)].byte[0x0E]` has bit 5 set. That function has **15 gameplay callers** (FUN_00436e40, FUN_004377d0, FUN_0044c070, FUN_00466200, FUN_0046afc0, FUN_0046b1e0, FUN_0046cb50, FUN_0046fd50, FUN_00470d10, FUN_00471350, FUN_00476d30, FUN_00479f60, FUN_00496e00, FUN_0049c430, FUN_0049d4c0).
  - `0x0044bcbb` in `FUN_0044bc80` (ground decal builder): no decal is placed on water.
- **Algorithm** [fact]:
  1. `ix=(x+16384)/512`, `iy=(y+16384)/512` (division truncating toward zero). Take quad `g[ix*65+iy]`, its page `p` and slot `s`, and `mode = waterMode[p][s]`. Return 0 if `mode == 0` and 1 if `mode == 2`.
  2. Otherwise (mode 1): `u = (x+16384) mod 512` (sign-preserving modulo); `v = 512 − ((y+16384) mod 512)`. Flags `f = g.uvflags>>8`, `rot = f&3`. If `f&8` (mirror): `u = 512−u` and `rot = (−rot)&3`. If `f&4`: `v = 512−v`. Then rotation 1: `(u,v) → (v, 512−u)`; rotation 2: `→ (512−u, 512−v)`; rotation 3: `→ (512−v, u)`. This matches the UV permutation of `FUN_10001000`.
  3. Texel position: `tx = (u>>4) + 33·(s%7)`, `ty = (v>>4) + 34·(s/7)`.
  4. For each of the 9 offsets `(0,0) (0,−4) (3,−3) (4,0) (3,3) (0,4) (−3,3) (−4,0) (−3,−3)` (table `0x1002c6a0`): clamp to `[33·col, 33·col+32] × [34·row, 34·row+32]`. Read the 16-bit texel from the page's **system-memory surface** (`FUN_1000ffc0`: `Lock(DDLOCK_WAIT)`, `*(u16*)(bits + (pitch·ty)/2·2 + tx·2)`, `Unlock`). If any texel ≠ 0, return 0 (not watery).
  5. If all 9 texels are 0, return 1.
- **Globals:** `g[]` (+0x34 / +0x40 / +0x42), page table `0x1004cf90` (+4 = sysmem surface, +0x10 = waterMode).
- **D3D11:** must be **bit-exact**. Keep a CPU copy of each 256×256 ground page in the same 16-bit layout, or a 1-bit "texel == 0" mask per texel, with the same slot packing (33/34 pitch, gutter at +32). **Risk:** "texel == 0" depends on the 16-bit format the original enumerated (`DDPIXELFORMAT` at `0x1013e420`, chosen in `FUN_10005820`). In 565 an opaque black texel is also 0; in 1555/4444 only transparent black is 0. The source TIMs use 0x0000 as "transparent", so derive the mask from TIM texels == 0 and verify it against the original. [uncertain] `waterMode` comes from `afAddToGroundTextures2` (texture group).

### afTransformPoint — `0x1000fe60`  *(UI/HUD; not simulation)*
- **Prototype:** `void __cdecl afTransformPoint(int x, int yUp, int z, int unused, short out[3])` — exe axis order (arg 2 = height). [fact]
- **Exe:** pointer `0x005380bc`, 1 call site `0x0044f977` inside the pass-through wrapper `FUN_0044f950(x,y,z,unused,out)`. That wrapper is used by `FUN_00459b20` (hog name/health labels: `afTransformPoint((short)pos.x, (short)pos.y, (short)pos.z, ?, &s)`, kept if `s.depth > 0` and the point is on screen, label size = `182400/depth`) and by `FUN_00489c40` (particle sprites of type `'#'`).
- **What it does:**
  - `d = (x−camX, z−camY, yUp−camZ)`;
  - `out[0] = (short)trunc(cx + SX/Zv)`;
  - `out[1] = (short)trunc((winH−1) − cy − SY/Zv)`, which is `sy − 1` for even heights;
  - `out[2] = (short)trunc(Zv)`.
  It uses a plain `1/Zv` (no 937 hack; Zv ≤ 0 gives garbage) and the matrices from the last `Begin3D`.
- **D3D11:** reproduce on the CPU with the same matrices and truncation. It affects HUD placement and label sizes.

### afRotPtsInt — `0x1000fa10`  *(visual decals, computed for exe logic)*
- **Prototype:** `float* __cdecl afRotPtsInt(const float m[12], int n, const float* pts /*stride 4 floats*/)` returns `0x11c7eee8` (`float[3]` per point). [fact]
- **Math:** `out.x = p.x·m0 + p.y·m3 + p.z·m6 + m9`; `out.y = p.x·m1 + p.y·m4 + p.z·m7 + m10`; `out.z = p.x·m2 + p.y·m5 + p.z·m8 + m11`. `m` is row-major 3×3 followed by a translation, which is the exe's object-matrix layout.
- **Exe:** pointer `0x0053814c`, 1 call site `0x0049c2aa` in `FUN_0049ba10` (vehicle/hog movement update): `afRotPtsInt(&model->matrix (model+4), 2, model->pts+0x14)`. The two rotated points place track/footprint decals (`FUN_0044b2b0`).
- **D3D11:** trivial CPU function. Keep float semantics. The output buffer is shared with `afDrawObj`, so callers must consume it immediately.

### afSetMap — `0x100024c0`  *(renderer copy of the map; feeds afIsPointWatery)*
- **Prototype:** `void __cdecl afSetMap(const MapTile20 tiles[64*64])`. [fact]
- **Exe:** pointer `0x0053807c`, 1 call site `0x004a584a` in Map::Load `FUN_004a5250`: `afSetMap(this->tiles /*this+0x14*/)`. Called once per map.
- **What it does:**
  1. For every vertex `(xi, yi)`, set X and Y. For `yi ≥ 1`, also set `H = 2·tiles[(64−yi)*64 + xi].height` and `+0x38 = byte0E & 0x7F`, and update the min/max heights.
  2. Copy row `yi = 63` to `yi = 64` and column `xi = 63` to `xi = 64`.
     **Edge quirk:** row `yi = 0` (the missing PMG row 64) keeps height 0, and the real PMG row 0 is overwritten by row 1. This is outside the gameplay clamp of ±15871. [fact; it looks like an original off-by-one]
  3. For every quad: tile `t = (yi==64) ? xi : (63−yi)*64+xi` (clamped to 0xFFF); set the UV flags, `tex = DAT_1002204c + (w>>8)`, the page and slot, the UVs, and apply `FUN_10001000`; set `+0x24` = word +0x0E and wave = 0.
  4. Set the toxic flag when `type == 11`, and the water flag when `waterMode[page][slot] != 0`.
  5. `FUN_100011c0()` (full re-light). If the map has water, `FUN_1000b330()` builds the water grid.
- **D3D11:** build a static 65×65 vertex buffer (positions, UVs, page) and keep the per-quad page/slot/flags on the CPU for `afIsPointWatery`. Reproducing the edge quirk is optional (it is a visual border only).

### afAdjustMapTile — `0x100028e0`  *(runtime texture change: craters/mines; feeds afIsPointWatery)*
- **Prototype:** `void __cdecl afAdjustMapTile(int col, int row, unsigned texWord)`. Here `row` is the exe tile row; the DLL quad is `(xi=col, yi=63−row)`. [fact]
- **Exe:** pointer `0x00538050`, 4 call sites:
  - `0x004a6e22` in `FUN_004a6de0(map, x, z, word)`: writes `tiles[(63−z)*64+x].word12 = word`, then `afAdjustMapTile(x, 63−z, word)`;
  - `0x004a6e83`, `0x004a6eb7` and `0x004a6eeb` in `FUN_004a6e30(map, x, z, kind, rot)`: `afAdjustMapTile(x, 63−z, (map->specialTex[kind] << 8) | (rot<<1))`, where `specialTex` = map+0x5C / +0x58 / +0x60 (the crater/mine textures).
- **What it does:** recomputes the quad's UV flags, texture number, page, slot and UVs, then calls `FUN_10001000`. Texture number = `(w>>8) + DAT_1002204c`; if that exceeds the loaded count `DAT_1004cf80`, it uses `(w>>8) + 1` instead (an absolute index for the special textures). It does **not** touch heights, lighting or the water flag.
- **D3D11:** update the quad's texture and UVs in a dynamic buffer and the CPU tile table (water test).

---

## 5. Height queries (gameplay-shaped, but NOT used by HoW)

### GroundZ — `0x100015a0`
- **Prototype:** `float __cdecl GroundZ(float x, float y)` (DLL axes; the result is returned in ST0). [fact]
- **Exe:** pointer `0x005386e8`. **Not called.** No internal caller either.
- **Algorithm** [fact] — weighted by **Chebyshev distance** (a pyramid), not bilinear:
  1. `fx = clamp(x+16384, 0, 32767)`, `fy = clamp(y+16384, 0, 32767)`;
     `ix = ftol(fx/512)`, `iy = ftol(fy/512)`;
     `ox = ftol(fx − 512·ix)`, `oy = ftol(fy − 512·iy)`.
  2. Weights: `wA = 512 − max(ox, oy)` for `(ix, iy)`, `wB = 512 − max(512−ox, oy)` for `(ix+1, iy)`, `wC = 512 − max(ox, 512−oy)` for `(ix, iy+1)`, `wD = 512 − max(512−ox, 512−oy)` for `(ix+1, iy+1)`. Sum `s = ((wD+wC)+wB)+wA` (exact in float).
  3. Result `= ((wD/s·HD + wA/s·HA) + wB/s·HB) + wC/s·HC`, evaluated in x87.
- **D3D11:** implement only for API completeness. It does **not** match the gameplay height (§2.4) and will differ from it by several units on slopes.

### afGetMapHeight — `0x10002be0`
- **Prototype:** `float __cdecl afGetMapHeight(int xi, int yi)`. Returns `g[xi*65+yi].H` (= 2·tile height) with no bounds check. [fact]
- **Exe:** pointer `0x005380a4`. **Not called.**
- **D3D11:** trivial (a grid lookup).

### afAdjustMapHeights — `0x10002820`
- **Prototype:** `void __cdecl afAdjustMapHeights(const MapTile20* tiles, int xi0, int yi0, int nx, int ny)`. [fact]
- **Exe:** pointer `0x005388a4`. **Not called.**
- **What it does:** re-reads `H` for vertices in `[xi0, xi0+nx) × [yi0, yi0+ny)` using the same `(64−yi)` mapping as afSetMap, then re-lights the **whole** map. It does not repeat the edge fix-up, the min/max heights or the water grid.
- **D3D11:** optional. If implemented, update the heights and re-light locally (normals change one vertex outside the range).

---

## 6. Purely visual exports

### SetGroundLightDirection — `0x10004ca0`
- **Prototype:** `void __cdecl SetGroundLightDirection(int unused0, int unused1, float ambient, float levelAtHalf)`. Arguments 1 and 2 are never read. [fact]
- **Exe:** pointer `0x00538158`, **not called**. Called internally only, through `FUN_100011c0()`, as `SetGroundLightDirection(0, 0xD00, 0.4f, 0.40000999f)` from `afSetMap`, `afAdjustMapHeights` and `afSetWeatherValues(…, relight != 0)`.
- **Algorithm** [fact]:
  - `a = DAT_11c94828` (azimuth), `b = DAT_11c9482c` (elevation); `L = −(cos a·cos b, sin a·cos b, sin b)`. This is the direction the light travels, so flat ground is lit when `sin b < 0`, e.g. b = 0xD00.
  - `k = ln((levelAtHalf−ambient)/(1−ambient)) / ln 0.5`, which gives **15.875** with these constants.
  - For every vertex: `N = normalize(0.5·[(C−P)×(D−P) + (A−P)×(B−P)])`, with A = +Y neighbour, B = −X, C = −Y, D = +X. A missing neighbour is extrapolated ±512 with the same height. This is equivalent to `normalize(h[x−1]−h[x+1], h[y−1]−h[y+1], 1024)`.
  - `d = max(0, N·L)`; `T = ftol(255·(ambient + (1−ambient)·pow(d,k)))`; `t = T/255` (as float).
  - Per channel: `C = ftol((lit−dark)·t) + dark`, with lit = W[9..11] and dark = W[12..14]. Store `0xFF000000|R<<16|G<<8|B` at vertex +0x0C.
  - Brightness examples: d = 1 gives 255, 0.95 gives 169, 0.9 gives 130, ≤ 0.5 gives ≈ 102. In practice the ground is mostly at 40 % brightness, with highlights on slopes facing the light.
- **D3D11:** compute on the CPU at map load or weather change (cheap), or in a vertex shader with the same quantisation. This is not gameplay-relevant, but must match visually.

### afSetWeatherValues — `0x1000fba0`
- **Prototype:** `void __cdecl afSetWeatherValues(const int env[19], int count /*19, unused*/, BOOL8 fogEnable, BOOL8 dither, BOOL8 relight)`. [fact]
- **Exe:** pointer `0x005380c8`, 1 call site `0x00486652` in `FUN_00486550(char)` (called from level setup `FUN_00485550` and from `FUN_00486030`): `afSetWeatherValues(&DAT_005209a0, 0x13, G[0x4ac], G[0x4d0], 1)`. `G = DAT_00520668`.
- **Struct (from the exe copy):**

  | Index | Content | Source in `G` |
  |---|---|---|
  | 0-2 | Sky colour A | `G+0x4d4..`, from table `0x4d5e80[type*8]` |
  | 3-5 | Sky colour B | `G+0x4e0..` |
  | 6-8 | Fog RGB | `G+0x498..` |
  | 9-11 | Terrain lit RGB | `G+0x4b0..` |
  | 12-14 | Terrain dark RGB | `G+0x4bc..` |
  | 15-16 | Fog start / end (int) | `G+0x4a4/4a8` |
  | 17 | Light azimuth | `G+0x4c8` |
  | 18 | Light elevation | `G+0x4cc` |

- **What it does:**
  - copies `env[0..14]` to `0x11b8a6f8`, then adds 30 to `env[0..5]` (clamped to 255) — these six values are **never read anywhere** [fact];
  - stores `env[17]` and `env[18]` as the light angles;
  - `SetRenderState(FOGENABLE, fogEnable)`, `SetRenderState(DITHERENABLE, dither)`, `SetRenderState(FOGCOLOR, 0xFF<<24 | env6<<16 | env7<<8 | env8)`;
  - if `relight`, calls `FUN_100011c0`;
  - `FUN_100112f0(env[18])`: resets the light/shadow constants used by the model code. The argument is ignored. [inferred]
- **Not in the DLL:** rain and snow particles (the exe allocates 128 particles in `FUN_0044e080` and uses `rain.mtd`/`snow.mtd`).
- **D3D11:** keep the fog colour, the light angles and the two terrain colours. `env[0..5]` can be ignored.

### afSetFog — `0x100096f0`
- **Prototype:** `void __cdecl afSetFog(BOOL8 enable, float start, float end, D3DCOLOR colour)`. [fact]
- **Exe:** pointer `0x00538058`, 1 call site `0x004859e4` in `FUN_00485550`: `afSetFog(G[0x4ac], (float)G[0x4a4], (float)G[0x4a8], 0xFF000000|R<<16|G<<8|B)` (values in §3.1).
- **What it does:**
  - `SetTransform(PROJECTION, M)`, where M comes from `FUN_10009660(near=100, far=500, fov=π/4)` (DX5 SDK style): `_11=_22=cos(fov/2)`, `_33=Q=sin(fov/2)/(1−near/far)`, `_34=sin(fov/2)`, `_43=−Q·near`. This exists only so that the driver performs **W-based** table fog with TL vertices.
  - `FOGENABLE = enable`, `FOGCOLOR = colour`, `FOGTABLEMODE = D3DFOG_LINEAR`, `FOGTABLESTART = start`, `FOGTABLEEND = end`.
  - Keeps copies in `0x11c947e0` (the colour is also the backdrop colour), `0x11ab8ca8` and `0x1011d6d8`.
- **D3D11:** compute per-pixel linear fog `f = saturate((end − w)/(end − start))` with `w = 1/rhw`. For terrain that is `Zv/8` beyond 937. Then `out = lerp(fogColour, colour, f)`. **[uncertain]** whether the original drivers used W (eye depth/8) or Z; Z-based fog would effectively be off, since start ≥ 238 ≫ 1. Calibrate against a real run or dgVoodoo. Also add the fade offset of §3.2.

### afStartSkyFade — `0x1000f980`
- **Prototype:** `void __cdecl afStartSkyFade(int on)`, stored in `DAT_11c94824`. [fact]
- **Exe:** pointer `0x0054c5d4`, 1 call site `0x0044ea0c` in `FUN_0044e9f0(gfx, on)`, always paired with `afSetMapFlag`. Camera `SetMode` (`FUN_0049f740`) calls it with 1 for **mode 9** (overview camera) and with 0 when entering any mode other than 7 or 9.
- **What it does:** see §3.2.1–2: a 16-frame fade of the backdrop to black, the fog range pushed out by up to 7650, and a switch to the overview terrain path.
- **D3D11:** reproduce the counter (steps of 16 per frame), the fog offset and the black overlay.

### afSetMapFlag — `0x10002bb0`
- **Prototype:** `void __cdecl afSetMapFlag(int on)`. [fact]
- **Exe:** pointer `0x005381c0`, 1 call site `0x0044ea1e` (same caller as afStartSkyFade).
- **What it does:** `DAT_1004cf88 = on`. In the overview path the visible footprint becomes the whole map (tiles 2..62), and `afDrawObj` stops skipping objects that its distance/frustum test (`FUN_10010610`) rejects. It also writes `DAT_10022044 = on ? 16 : 20`, which nothing reads.
- **D3D11:** in overview mode, draw the whole terrain and all objects.

### afSetGroundSubDiv — `0x1000ff70`
- **Prototype:** `void __cdecl afSetGroundSubDiv(int on)`. Sets `DAT_11c94844 = on` and the cull depth `DAT_1002c540` = −768 (on) or −15 (off). [fact]
- **Exe:** pointer `0x00538190`, 2 call sites in camera `SetMode` `FUN_0049f740`:
  - `0x0049f8e7`: `afSetGroundSubDiv(1)` when entering camera mode 0xE or 0x11 (aiming/scope views);
  - `0x0049f8b7`: `afSetGroundSubDiv(0)` when leaving them, right after `afResetZoom`.
- **What it does:** see §3.4. Tiles are subdivided in screen space by depth (<1000 → 16 pieces, <2000 → 4 pieces). The wider cull depth keeps near or behind tiles that the tangent projection still maps on screen.
- **D3D11:** with a vertex-shader port of §3.3 the subdivision is only needed to match the non-perspective "tangent" region exactly. For a pixel-exact look, tessellate the near tiles 4×/16× (CPU or hull shader); otherwise ignore it. Keep the cull rule.

### afSetSky — `0x10002a70`  *(dead)*
- **Prototype:** `void __cdecl afSetSky(const MapTile20 sky[16*64])`. [fact]
- **Exe:** pointer `0x005380d8`, **not called**.
- **What it does:** fills the 1024 sky records at `0x10032f80`: flags (stored as float), texture number (as float, no base), UVs and `FUN_10001000`. The flag float bits are passed as int, which is a golf-era bug. It also sets `DAT_1004cf84 = 1`. **Nothing reads these records.**
- **D3D11:** a no-op stub. The HoW sky is the two type-2 skydome objects plus the fog-coloured backdrop (§3.2).

### SetView — `0x10002440`  *(dead)*
- **Prototype:** `void __cdecl SetView(float x, float y, float z, int yaw, int pitch)`, in DLL axes. [fact]
- **Exe:** pointer `0x0054c590`, **not called**.
- **What it does:** writes `0x1002c524/528/52c` (camera X, Y, Z), `0x1002c530` (yaw) and `0x11b6a26c` (pitch), and sets `0x11b6a280 = 1`, a dirty flag that nothing reads. It does **not** rebuild the rotation, and the next `Begin3D` overwrites all values from display window 0.
- **D3D11:** store the values. In HoW the camera comes from `AddDisplayWindow` window 0.

### GetView — `0x10002480`  *(dead)*
- **Prototype:** `void __cdecl GetView(float* x, float* y, float* z, int* yaw, int* pitch)`. [fact]
- **Exe:** pointer `0x0054c5e8`, **not called**.
- **What it does:** returns the five camera globals listed under SetView (the floats via FLD/FSTP, yaw and pitch as ints).
- **D3D11:** return the current camera.

### GetViewAngles — `0x1000cfa0`  *(dead utility)*
- **Prototype:** `void __cdecl GetViewAngles(float x0, float y0, float z0, float x1, float y1, float z1, int* yaw, int* pitch)`. [fact]
- **What it does:** `yaw = ftol(atan2(dy,dx)·2048/π) & 0xFFF` (0 if dx = dy = 0); `pitch = ftol(atan2(dz, √(dx²+dy²))·2048/π) & 0xFFF`. This matches the camera convention.
- **Exe:** pointer `0x0053806c`, not called.

### afGetViewRots — `0x1000fae0`  *(dead)*
- **Prototype:** `void __cdecl afGetViewRots(float m[9])`. Writes `{R.x, U.x, F.x, R.y, U.y, F.y, R.z, U.z, F.z}` (the scaled rows divided by fx/fy). [fact]
- **Exe:** pointer `0x00538074`, not called.

### afSetZoom — `0x1000fb60`
- **Prototype:** `void __cdecl afSetZoom(int z /*0..4096*/)`. Sets the target `DAT_1002be6c = 15 − ftol(z/4096·(−45))`, i.e. 15..60. [fact]
- **Exe:** pointer `0x00537fd4`, 3 call sites:
  - `0x0044f7cf` in `FUN_0044f790`: zoom += delta, clamped to a weapon-specific maximum or 0x1000;
  - `0x0044f811` and `0x0044f835` in `FUN_0044f7f0`: set, clamped to 0..0x1000.
- **Effect:** in `Begin2D` the current zoom `DAT_1002c518` moves toward the target by `ftol(Δ·0.333)` per frame, or ±1 when that step is 0 (frame-rate dependent). The focal lengths `fx = W·zoom/20` and `fy = H·zoom/15` are rebuilt in `Begin3D`. A ×4 zoom narrows the H-FOV from 67.4° to 18.9°. The sprite scale `DAT_10131b1c = (2·(H/2)/15)·zoom` changes as well.
- **D3D11:** keep the integer zoom and its per-frame easing, and derive the projection from it.

### afResetZoom — `0x1000fb90`
- **Prototype:** `void __cdecl afResetZoom(void)`. Sets both the current zoom `DAT_1002c518` and the target `DAT_1002be6c` to 15 immediately, with no easing. [fact]
- **Exe:** pointer `0x005388ac`, 1 call site `0x0049f8b0` in camera `SetMode` `FUN_0049f740`, when leaving the aim modes 0xE/0x11 (right before `afSetGroundSubDiv(0)`).
- **D3D11:** reset both values.

### DisplayCurrentHole — `0x10004f50`
- **Prototype:** `void __cdecl DisplayCurrentHole(BOOL8 render, int unusedA, int unusedB, int tick)`. [fact]
- **Exe:** pointer `0x0054c58c`, 2 call sites:
  - `0x0044e59a` in the main 3D frame `FUN_0044e290`: `DisplayCurrentHole(1, level[0x4e8], level[0x4ec], DAT_00520878)`. `DAT_00520878` is most likely a global frame/tick counter. [uncertain]
  - `0x00481bfa` in `FUN_00481bb0` (2D-only frames): `DisplayCurrentHole(0,0,0,0)`.
- **What it does:**
  - stores A and B in `0x11ab9238/34` (never read) and the tick in `0x10032c28` (water phase);
  - `Clear(D3DCLEAR_ZBUFFER)`;
  - `FUN_10007ff0`: target = camera + 1024·F; `FUN_10002c00(target, eye)` builds the look-at matrices `0x11b8a570`/`0x11b8a7c8` in (x,h,y) order for models;
  - if `render`, `FUN_10001540` (overview footprint when needed) and then `FUN_10003380` — the whole world frame of §3.2;
  - resets the window and sort counters.
- **D3D11:** this is the "render scene" entry point. Implement §3.2 in order. The tick must drive the water waves.

### SetGolfFlags — `0x100065a0`
- **Prototype:** `void __cdecl SetGolfFlags(GolfFlags f)`. Takes a **76-byte struct by value** (19 dwords; the caller reserves `0x4C` bytes on the stack and pops them). [fact]
- **Exe:** pointer `0x0053812c`, 1 call site `0x0044e12a` in the graphics-manager constructor `FUN_0044e080`. That constructor first calls `GetGolfFlags(&tmp)` and then `SetGolfFlags(tmp)` with no change: a round trip whose only purpose is the side effects.
- **What it does:**
  - copies the 19 dwords to `0x1002bfe8`;
  - sets `0x1002c534 = 0x1002c538 = 30000.0f` (far distances: terrain footprint and object cull; the default is 400000);
  - calls `FUN_10014120`: for each of 10 masks, fills 2 KB of `0x11ed0188` with 0xFF, then loads `language\tims\nview000.bmp … nview009.bmp` into it (`LoadImageA(LR_LOADFROMFILE)` + `GetBitmapBits`; 1024 int16 entries each). These are the near-path terrain visibility masks (§3.4).
- **Flag meaning:** golf-era options, default `{3, 1×18}`. They are read by `PowerUp` (flag 5 forced by a device cap), by `FUN_1000c8d0` (saved per driver mode) and by an unreferenced code block at `0x10006850`. HoW does not depend on their values. [inferred]
- **D3D11:** keep a 19-dword store. If the nview masks are reproduced, load them and set the 30000 draw distance here. If the masks fail to load, the near path draws **no terrain at all**, because the buffer is pre-filled with 0xFFFF.

### GetGolfFlags — `0x1000bd40`
- **Prototype:** `GolfFlags* __cdecl GetGolfFlags(GolfFlags* out)`. Copies the 19 dwords from `0x1002bfe8` into `out`. EAX still holds `out` on return, and the exe relies on that: it copies from EAX into the by-value argument for SetGolfFlags. [fact]
- **Exe:** pointer `0x005381b8`, 1 call site `0x0044e116` in `FUN_0044e080` (see SetGolfFlags).
- **D3D11:** return the stored struct and return `out`.

---

## 7. Notes for a Direct3D 11 reimplementation

**Must be exact (gameplay):**
- `afIsPointWatery`: the texel test, the 9 samples, the clamp box, the rotation/mirror mapping and the `waterMode` table. Store the ground pages on the CPU exactly as the original sampled them (see the 16-bit format caveat).
- The tile-to-quad mapping (`yi = 63 − row`) in `afSetMap`, `afAdjustMapTile` and `afIsPointWatery`, and the texture-number base rule in `afAdjustMapTile`.
- `afTransformPoint` (truncations, `sy−1` quirk, depth) — it drives HUD placement and label scaling. `afRotPtsInt` matrix layout.
- Gameplay heights are **not** in the DLL. Render the terrain with the diagonal `(xi,yi)–(xi+1,yi+1)` (exe `(col,row+1)–(col+1,row)`) so that the visible surface matches `FUN_004a5140`.
- x87 precision: if the original exe is kept, match DX7's single-precision FPU mode (see §0.10).

**Can move to shaders:**
- Vertex transform: port §3.3 to a vertex shader with constants (camera, R/U/F rows × fx/fy, cx/cy, tick). Output `clip = (ndc.x·w, ndc.y·w, z·w, w)` with `w = 1/(8r)` and `z = max(Zv·2e-6, 1.527e-5)`. This reproduces DX7 TL interpolation exactly: perspective-correct UVs follow rhw, and z is screen-linear. w is always > 0, so turn off depth clip if `z` could exceed 1, since Zv beyond 500000 cannot occur on a 32768-unit map.
- **Culling must stay on the CPU:** keep the per-tile rule `max(Zv) > −15/−768`; otherwise the tangent projection pulls geometry from behind the camera onto the screen. Mask-based visibility can be replaced by "draw everything that passes the tile rule" plus fog. That is a visual change only.
- Water waves and scrolling: a vertex shader with the tick (x-phase `4·tick`, y-phase `2·tick`, amplitudes 128/100, tables of 4096 entries — use the same float tables for an exact look). Draw the water plane before the terrain, with 50 % alpha.
- Lighting: pre-compute on the CPU as in §6, or in a vertex shader. Fog: per pixel (§6 afSetFog), plus the fade offset.
- Draw order: backdrop (fog colour) → sky objects, fog off → water → terrain (alpha blend + alpha test as in the device init) → sort buckets 9999→0 → radar. Terrain triangles are clockwise on screen with CULLMODE = CCW, i.e. D3D11 `FrontCounterClockwise = FALSE` and back-face culling.

**Can be stubbed (unused by HoW):** `GroundZ`, `afGetMapHeight`, `afAdjustMapHeights`, `afSetSky`, `SetView`/`GetView` (store only), `GetViewAngles`, `afGetViewRots`, `SetGroundLightDirection` as an export (keep it as an internal routine), sky colours `env[0..5]`, and the two unused `DisplayCurrentHole` arguments.

**Uncertain points:** W- vs Z-based fog distance on the original hardware; the meaning of the tick `DAT_00520878`; the 16-bit format of the system-memory pages (it changes what "texel == 0" means); the exact semantics of the golf flags; `_DAT_11b6a324` (written as 0 in `afSetMap`; no reader found).
