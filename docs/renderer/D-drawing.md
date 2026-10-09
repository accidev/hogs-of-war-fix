# Renderer group D: object drawing, sort list, 2D/3D primitives, text, scanner

Source: `Data\_d3d.dll` (Ghidra program `_d3d.dll`) and `warhogs_unwrapped.exe`. All addresses are hex. `obj`, `model`, `poly` and the other names are my own; Ghidra still shows `FUN_`/`DAT_` names (I renamed nothing in Ghidra).

Notation: "TL vertex" means `D3DTLVERTEX` (FVF `0x1C4` = `XYZRHW|DIFFUSE|SPECULAR|TEX1`, 32 bytes: `sx, sy, sz, rhw, color, specular, tu, tv`). The device is `IDirect3DDevice7*` in `DAT_11ab9470`. Vtable offsets used: `+0x50` SetRenderState, `+0x64` DrawPrimitive, `+0x8C` SetTexture, `+0x94` SetTextureStageState.

---

## 1. Key facts

- **Every primitive in this group is a pre-transformed TL vertex** (FVF `0x1C4`), drawn with `DrawPrimitive` and `dwFlags = 0`. The DLL does all transform, lighting and projection on the CPU. It never calls `SetTransform`, and it sets `D3DRENDERSTATE_LIGHTING` to FALSE.
- **Primitive types:** `TRIANGLELIST` for objects, text, 2D quads, the scanner and the batched ground; `TRIANGLEFAN` (4 vertices) for prebuilt 2D quads, placed polys and the background; `LINELIST` for lines.
- **The sort list is a PSX-style ordering table.**
  - 10 000 buckets. Bucket = `|viewDepth| >> 3`.
  - Each bucket is a singly linked list in LIFO order. Nodes come from a pool of 8 000.
  - It is flushed once per frame by `FUN_10003380(9999, 0)`, called from `DisplayCurrentHole(1,…)`. The flush walks buckets **9999 → 0**, so far is drawn before near, and within a bucket the **last item added is drawn first**.
- **Render state is driven by a small cache, `DAT_1002bf4c`.** It holds one of five "state blocks" (§3). The only blend modes ever used are:
  - alpha blend (`SRCALPHA/INVSRCALPHA`, the default),
  - additive (`ONE/ONE`),
  - multiplicative (`ZERO/SRCCOLOR`, present but not used by the exe),
  - opaque.
- Alpha test is always on (`ALPHAREF 8`, `GREATEREQUAL`). The z-buffer is always on (`LESSEQUAL`, write enabled).
- **PSX semi-transparency (ABR) modes are not carried per primitive.** Instead:
  - per-object alpha in the vertex diffuse (0..255) gives normal alpha blending;
  - a per-texture "contains the transparent colour 0x0000" flag only moves those triangles to a second batch drawn after the opaque batch;
  - additive and multiply are chosen per 2D poly or placed poly by flags.

---

## 2. Common conventions

### 2.1 Calling convention

All 19 exports in this group end in a plain `RET`, so they are `__cdecl` and the caller cleans the stack. The exe calls them through function-pointer globals filled by `FUN_004ac430` with `GetProcAddress`.

### 2.2 Coordinates and fixed point

- World units are integers, carried as `int16` in most APIs.
- 4096 = 1.0 for scales and shears.
- Angles: 4096 = 360°. The DLL converts with `-(a * π / 2048)`.
- **Camera position:** `DAT_1002c524` = X, `DAT_1002c52c` = Y (height), `DAT_1002c528` = Z. The exe passes them through `AddDisplayWindow` as (x, z, y); see report A.
- **Camera basis** (focal length already folded in) — each row lists the coefficients that multiply the X, Y and Z offset from the camera:
  - right = (`DAT_11abd964`, `DAT_11abe188`, `DAT_11abd970`)
  - up = (`DAT_11abd968`, `DAT_11abe18c`, `DAT_11abe178`)
  - forward = (`DAT_11abd96c`, `DAT_11abe190`, `DAT_11abe180`)
- **Depth used everywhere:** `d = dot(p − cam, forward)`.

### 2.3 Two projection paths (they must agree in a port)

**Path 1: objects and models** (`FUN_10008080` and `FUN_100085e0`)

Vertices are multiplied by `obj.matrix × view` (`DAT_11b8a570`), then:

- `sx = cx + x·f/z`, `sy = cy − y·f/z`
- `f` = `DAT_10131b1c` = `(2·cy/15)·zoom` (set in `Begin2D`)
- `cx` = `DAT_11ab8ca0` = width/2, `cy` = `DAT_11ab8d30` = height/2 (set in `afIfaceInit`)

**Path 2: world-anchored 2D polys, 3D text, 3D lines, placed polys, SetLines**

- `sx = c51c + dot(p−cam, right)/d`
- `sy = H − (c520 + dot(p−cam, up)/d)`
- `c51c`/`c520` = `DAT_1002c51c`/`DAT_1002c520` (view centre); `H` = `DAT_1002c4fc` (viewport height)

Placed polys and SetLines also add the window offset `DAT_11ab8b00`/`DAT_11ab8afc` (from `SetDisplayWindow`) and round sx/sy to integers, which gives a PSX-like snap.

### 2.4 TL vertex z / rhw conventions

| Producer | sz | rhw |
|---|---|---|
| Object/model vertex (type 0, 3, 4) | `z·2e-6` (linear; far plane ≈ 500 000 units) | `8/z` |
| Object type 1 (screen overlay) | `0.0` | `8/z` |
| Object type 2 (sky) | `0.9` | `8/z` |
| Placed poly | `max(d·2e-6, 1.527e-5)` | `8/d` |
| 3D text, 3D line | `d·2e-6` | `8/d` |
| World-anchored 2D poly (flag 2, flag 1 clear) | `d·2e-6` | `15.0` |
| Screen 2D poly / 2D text | `1.526e-4` (`0x39200340`), or `0.0` when "topmost" | `15.0` |
| 2D line | `1.526e-5` | `15.0` |
| Scanner map and blips | `0.0` | `15.0` |
| Background clear quad (`FUN_1000b6e0`) | `0.99999` | `0.2` |

- Specular is always `0xFFFFFFFF`, and `SPECULARENABLE` is FALSE.
- Diffuse is ARGB `0xAARRGGBB`.
- UVs are page-normalised, with 256 texels = 1.0. Most producers add a half-texel inset of `±1/512`.

### 2.5 Texture pages

- 32 texture slots: `DAT_11a25d20[slot*2]`, each `{IDirectDrawSurface7* vidmem, IDirectDrawSurface7* sysmem}`.
- `SetTexture(0, …)` is skipped when the surface equals the last one bound (`DAT_11ab9490`).
- Each sub-texture ("page-fitted TIM") has an entry in `DAT_11ec1180`, stride `0x28`, at most `0x600` entries:

| Off | Type | Meaning |
|---|---|---|
| +0x00 | u8 | u0, texel x in the page |
| +0x01 | u8 | v0 |
| +0x02 | u16 | width in texels (as read by the exe through `afGetPageFittedTim`) |
| +0x04 | u16 | height in texels (same source) |
| +0x08 | float | u0/256 |
| +0x0C | float | v0/256 |
| +0x10 | float | w/256 |
| +0x14 | float | h/256 |
| +0x1C | int | page slot 0..31 → `DAT_11a25d20` |
| +0x20 | int | 1 if the TIM's CLUT has colour `0x0000` **and** that index is used. This is the colour-key / "transparent" flag set by `FUN_10012f90` from `afLoadTims`. |

---

## 3. Render-state blocks (`DAT_1002bf4c` cache)

`FUN_10006240` (device creation) sets the cache to −1 (invalid). Every change increments the statistics counter `DAT_11ab945c`.

| Id | Set by | SHADEMODE | ALPHABLENDENABLE | TSS0 ALPHAOP | Extra | Used for |
|---|---|---|---|---|---|---|
| `0xD` | flush cases 10/11, sky, `afDrawAnimModel(frontend)`, untextured 2D poly, scanner, water | GOURAUD (2) | TRUE | MODULATE (4) | – | objects, anim models, untextured 2D polys, scanner |
| `5` | text, textured 2D poly, placed poly mode 1/6/0xB | FLAT (1) | TRUE | MODULATE (4) | – | text, textured 2D polys, placed polys |
| `3` | 2D poly flag `0x40`, placed poly mode `0xE` | FLAT (1) | TRUE | BLENDDIFFUSEALPHA (12) | `SRCBLEND=ONE(2)`, `DESTBLEND=ONE(2)` | additive sprites. After the draw, `SRCBLEND=SRCALPHA(5)`/`DESTBLEND=INVSRCALPHA(6)` are restored, but the cache stays `3` and the guard is `!= 0`, so these states are always re-sent. |
| `1` | lines (types 4 and 0xF), background quad | FLAT (1) | FALSE | SELECTARG1 (2) | – | opaque untextured |
| (no id) | placed poly mode 6 | (state 5) | TRUE | (state 5) | `SRCBLEND=ZERO(1)`, `DESTBLEND=SRCCOLOR(3)`, then restored to 5/6 | multiplicative shadows (unused by the exe) |

`afDrawObj`, `afDrawText` and `afDraw2dPolyIn3dWorld`'s caller sometimes **do not set a state block themselves**:

- `afDrawObj` and `afDrawText` rely on the flush (`FUN_10003380`) to have set block `0xD` or `5`.
- When the exe calls them directly (frontend), they **inherit whatever block was current**. For example, after an additive 2D poly that is FLAT + BLENDDIFFUSEALPHA.

---

## 4. Exports

### afSetObjPos — `0x1000d2f0` (ordinal 85)

```c
void __cdecl afSetObjPos(AfObj *obj, int x, int y, int z, int rotX, int rotY, int rotZ);
```

**What it does**

1. Sets `obj->m[0..8]` to identity. It multiplies the identity at `DAT_11c947f0` by itself through `FUN_10010910`.
2. Sets `obj->m[9..11] = (float)x, y, z`, the translation at `+0x28/+0x2C/+0x30`.
3. For each non-zero angle it calls `FUN_10010d80(obj->model, axis, -(angle·π/2048))` with axis 0 = X, 1 = Y, 2 = Z.
   - This **rotates the model's vertex array in place**. Each component is rounded to an integer by `FUN_10010d40` and stored back as float.
   - It then recomputes the 8-corner bounding box (`FUN_10010fa0`).

Rotation is therefore baked into the geometry and accumulates over calls; only position lives in the matrix.

**Exe callers**

- Global `DAT_00538014`, 7 call sites: `FUN_004544e0`, `FUN_00482a00`, `FUN_004582f0`, `FUN_00486030` ×2, `FUN_004866b0` ×2.
- Load-time examples:
  - `afSetObjPos(sky, 0,0,0, 0,0x800,0)` (`FUN_004866b0`, 180° about Y)
  - `afSetObjPos(scannerTop, 0,0,0, 0,0x400,0)` (`FUN_004544e0`)
- Per frame: `afSetObjPos(scannerTop, x, y, z, 0,0,0)` (`FUN_004582f0`).
- Rotations are only passed at load time, so baking is safe in practice.

**D3D11 notes:** Keep a CPU-side vertex copy per instance and replicate the baked rotation, or apply it as a matrix. Results differ only by the integer rounding.

### afScaleObj — `0x1000d3f0` (ordinal 79)

```c
void __cdecl afScaleObj(AfObj *obj, int sx, int sy, int sz);   /* 4096 = 1.0 */
```

**What it does:** Calls `FUN_10010f30(obj->model, sx, sy, sz)`. That multiplies every model vertex by `s/4096` per axis, **in place and cumulatively**, then recomputes the bounding box. The matrix is not touched.

**Exe callers**

- Global `DAT_00538114`, 8 call sites: `FUN_004544e0`, `FUN_00482a00`, `FUN_00486030` ×2, `FUN_004866b0` ×2, `FUN_0045e3a0` ×2.
- Examples:
  - `afScaleObj(sky, 0x100000, 0x80000, 0x100000)`, i.e. ×256, ×128, ×256.
  - `afScaleObj(o, 0x1000,0x1000,0x1000)` (no-op).
  - Map objects use the per-object scale from map data (`this+0x60/+0x64/+0x68`).

Each `afCreateObj2` instance has its own vertex copy (float pool `DAT_11b8a7f8`), so scaling is per instance.

### afShearObj — `0x1000d410` (ordinal 90)

```c
void __cdecl afShearObj(AfObj *obj, int shearX, int shearZ);   /* 4096 = 1.0 */
```

**What it does:**

- `obj->m[1] += shearX/4096` (field `+0x08`)
- `obj->m[7] += shearZ/4096` (field `+0x20`)

With the row-vector convention (§5.1) this adds `x·shearX + z·shearZ` to the world Y, i.e. it tilts an object onto a slope.

**Exe callers:** global `DAT_005380e4`; **not called**. It is a golf leftover.

### afDrawObj — `0x1000d440` (ordinal 54)

```c
void __cdecl afDrawObj(AfObj *obj, uint32_t skinFlags);
/* skinFlags == 0: rigid object.
   skinFlags != 0: skinned mesh, (texSlot << 8) | 1, only from FUN_10011a80. */
```

The object is drawn **immediately**; nothing is sorted here.

**Rigid path (`skinFlags == 0`)**

1. **Sky objects.** If `obj->type == 2`: `SetRenderState(FOGENABLE, FALSE)`. At the end it calls `SetRenderState(FOGENABLE, TRUE)` **unconditionally**, even if fog had been globally off. This is a quirk.
2. **Visibility test** (types other than 2): `FUN_10010610`.
   - Transform the origin to view space and store it at `obj+0x34`.
   - Reject if `z <= 15` or `z >= far` (`DAT_1002c538`).
   - Accept if `|x·DAT_1013e6b4| < z` and `|y·DAT_1013e6b8| < z`.
   - Otherwise test the 8 bounding-box corners.
   - Rejected objects are skipped unless `DAT_1004cf88` is set (set by `afSetMapFlag`, "draw everything").
3. **Lighting** (`FUN_10011600`), per normal:
   - `I = ambient + max(0, (n·R)·L)·scale`
   - clamped to `0x1F0000`, then `×1/8192`, giving 0..248
   - ambient `_DAT_11c94b70`, scale `_DAT_11c94bbc`, L = `_DAT_11c94b98/9c/a0`
   - result stored in `DAT_11ca77a0` (stride 12)
4. **Transform** all vertices with `obj.matrix × view` (`FUN_10011810`). The result goes to `DAT_11c7eee8` (stride 12: x, y, z) with z clamped to at least `1.527e-5`. **There is no near-plane clipping.**
5. **Occluder fade.** Applies only if `obj->fadeMode (+0x54) == 3`, the object's view z < the active hog's view z (`DAT_11c94834`), and the active hog's y (`DAT_11c94830`) < `obj.y` (`+0x2C`).
   - `obj->alphaTarget (+0x4C) = clamp(|vx|·255/|vz|, 50, 255)`
   - So buildings near the screen centre between camera and hog become translucent.
   - **`afForceTransparencyOff` has no effect on this; see that section.**
6. **Alpha smoothing:** `alphaCur (+0x50) = (int)(cur + (target − cur)·0.16667)` on every call.

**Skinned path (`skinFlags != 0`)**

1. Lighting with a single bone matrix: `FUN_100114e0(DAT_11cc0c50, …)`.
2. Skinning (`FUN_10011710`):
   - The bone index is taken from each vertex's 4th float.
   - It is remapped through `DAT_1002ec28[skeletonType*15 + bone]`.
   - Up to 15 bone matrices (`DAT_11cc0c50 = animModel+0x7C`) are each concatenated with the view.
   - No z clamp here; triangles with any `z < 0` are dropped by the emitter.
3. `alphaTarget = DAT_1002c688` (the anim model's alpha).
4. **Note:** LOD meshes are shared by every hog of a type, so the `+0x50` smoothing state is shared too.

**Emission** (both paths)

- Primitive block (`model->prims`):
  - Section 5: triangles, 32 B each.
  - Section 6: quads, 36 B each (§5.3).
  - Sections 1–4 are assumed empty, because the code reads the section-5 count at `+0x10`.
- Before emitting, each primitive's alpha field (`+0x1C` tri / `+0x20` quad) is overwritten with `alphaCur`. Primitives with `slot == -1` are skipped.
- Emitters: `FUN_10008080` (tri, 3 vertices) and `FUN_100085e0` (quad → 6 vertices in order `v0,v1,v2, v2,v3,v0`).
- **Batch choice:**
  - If `texinfo[prim.tex].hasTransparency` → **set B**: `DAT_11ab8cb0[slot]` counts, buffers `DAT_11a26520 + slot*0x4000` (512 vertices per slot, **no overflow check**).
  - Otherwise → **set A**: `DAT_1013e440[slot]` counts, buffers `DAT_1013ed08 + slot*0xC7380` (25 500 vertices per slot; shared with ground batching).
- **Vertex fields:**
  - Position and rhw: see §2.3/§2.4.
  - `tu = (u + texinfo.u0)/256 + 1/512`, `tv = (v + texinfo.v0)/256 + 1/512`.
  - Colour, types 0/1/3/4: `A = alphaCur & 0xFF`, `R = (int)(I/255·lightR + ambR)`. Likewise G and B. `lightRGB` = `DAT_11ab8b08/8b04/8af8` (float), `ambRGB` = `DAT_1013e6a0/e65c/e648` (int), both set by `SetGroundLightDirection`. **There is no clamp.**
  - Colour, type 2 (sky): triangles = `A | (s,s,s)` with `s = DAT_1002be68` (sky brightness 0..255); quads = `A | 0xFFFFFF`. This tri/quad mismatch is in the original.
  - Type 3 (character mesh): texinfo `0x76` → `DAT_11c9483c + 0x78`, `0x77` → `DAT_11c94840 + 0x7F`. These are per-hog texture swaps from `animModel+0x354/+0x356`; only the UV origin changes, not the page.
  - `prim.alpha >> 20` and `(prim.alpha >> 8) & 0xFFF` are added as screen offsets. They are always 0 for real objects.

**Flush** at the end of the call

- **Rigid:** set A slots 0..31, then set B slots 0..31. Each: `SetTexture(DAT_11a25d20[slot*2])` (cached), then `DrawPrimitive(TRIANGLELIST, 0x1C4, buf, count, 0)`. Counts reset.
- **Skinned:**
  - set A slot 0 drawn with texture slot `(skinFlags>>8)&0xF` (team skin page),
  - set A slot 1 with texture slot 1,
  - then all set B.
  - Set A slots 2..31 are **not** flushed in skinned mode.
- No render state is set except FOGENABLE (sky only); the caller provides block `0xD`.

**Exe callers**

- Global `DAT_00538184`, 1 site: `FUN_00480bc0` (frontend).
- It builds a matrix by hand: rotation about Y from a 4096-step sin/cos table, row 1 = (0, −1, 0), translation (x, y, z); alpha 100 or 255 into `+0x4C/+0x50`.
- It then calls `afDrawObj(DAT_00520990, 0)` between `Begin3D`/`End3D`.
- In-game objects are drawn by the sort-list flush (case 10).

**D3D11 notes**

- One draw per object is enough if pages are a `Texture2DArray[32]` and the page index travels with each vertex. Append set A then set B vertices in that order.
- Port with one alpha-blend + Gouraud state: blend `SRC_ALPHA/INV_SRC_ALPHA`, `clip(a − 8/255)`.
- With GPU transform you would gain proper near clipping. The original instead clamps z to `1.527e-5`, which produces huge triangles that D3D7 then guard-band clips.
- To match colours bit-for-bit, keep the unclamped colour packing, including byte overflow.

### afDrawAnimModel — `0x1000da10` (ordinal 53)

```c
void __cdecl afDrawAnimModel(AfAnimModel *m, char bonesOnly, char frontend);
```

**What it does**

1. Globals: `DAT_11c9483c = m->swapA (+0x354)`, `DAT_11c94840 = m->swapB (+0x356)`, `DAT_1002c688 = m->alpha (+0x35C)`. The position `(int16)` comes from `m->matrix.t` (`+0x70/+0x74/+0x78`).
2. If `!frontend`: `m->inWater (+0x358) = afIsPointWatery(x, z)`.
   If `frontend`: force block `0xD` and set `inWater = 0`.
3. `viewPos = FUN_10011900(pos)` (view transform).
4. **Active hog** (`m->flags (+0x3C) & 1`):
   - `DAT_11c92fd8/da/dc` = position,
   - `DAT_11c94830` = y,
   - `DAT_11c94834` = view z,
   - `DAT_11c94838 = m->+0x34C`.
   These feed the occluder fade in `afDrawObj`.
5. If `bonesOnly`: `FUN_10011e80(m, viewPos, 0)` computes the skeleton (animation blend in `FUN_10012050`) without drawing, then returns.
6. Otherwise, if inside the view frustum (`z > 0` and the same FOV slopes): `FUN_10011e80(m, viewPos, 1)`.
   - **LOD:** `DAT_11c94860 + type*0xC` holds 3 meshes. Mesh 0 if `z < DAT_1002eca0`, mesh 1 if `z < DAT_1002eca4`, else mesh 2.
   - Thresholds from `afIfaceInit` mode: (1500, 4500) / (−1, 1500) / (−1, −1).
   - Bones are built (`FUN_10011a80`), then `afDrawObj(lodMesh, (m->skinSlot (+0x350) << 8) | 1)`.
   - LOD meshes are created by `afLoadAnimModels` with type 3.
7. **Attachments:** `+0x40` (weapon) on bone 5, `+0x44` (hat) on bone 2, `+0x48` on bone 1. Each gets `alphaTarget = m->alpha`, its matrix is copied from the bone, then `afDrawObj(att, 0)`.

**Exe callers**

- Global `DAT_0054c5d8`, 2 sites:
  - `FUN_004808c0` (frontend): `afDrawAnimModel(model, 0, 1)` between `Begin3D`/`End3D`.
  - `FUN_004410a0`: `afDrawAnimModel(hog->model, 1, 0)`, bones only, for gameplay (weapon or muzzle positions).
- In-game drawing goes through the sort list (case 11) as `afDrawAnimModel(m, 0, 0)`.

**D3D11 notes**

- Skinning (15 bones, one bone per vertex, rigid) maps directly onto a vertex shader with a bone constant buffer.
- Keep the 3-level LOD switch by view z.

### afDrawText — `0x1000dc80` (ordinal 55)

```c
void __cdecl afDrawText(AfText *t);   /* struct in §5.5; 0x6044-byte stack frame via _chkstk */
```

**Setup**

- Texture: `slot = texinfo[t->tex].slot`; UV origin `texinfo.u0f/v0f`.
- **3D text** (`flags & 2`):
  - Project `(wx, wy, wz)` with path 2.
  - If behind the camera, return.
  - Store `sx = round(...)` and `sy = round(... − 8)` **back into `t+0x90/+0x92`**.
  - Vertices: `sz = d·2e-6`, `rhw = 8/d`.
- **2D text:** `sz = 0` if `flags & 8`, else `1.526e-4`; `rhw = 15`.

**Glyph loop**, over at most `strlen` characters (the stack buffer holds 128 glyphs)

- Space: x += 8.
- `/N`: x = `t->sx`, y += 22.
- `/C r g b`: three raw bytes. The new colour is `(old & 0xFF000000) | r<<16 | g<<8 | b`. It is also written to `t->color` and to the current glyph's vertices.
- `/` followed by any other character: that character is drawn as a glyph.

**Glyph geometry**

- `g = font[(char)(c − 0x1F)]` (4 × int16: `u, v, w, h`). `font[0]` = origin `(u, v)`. `font[2].h` (short index 11) is the line height **H** for all glyphs.
- Quad positions: `(x, y)`, `(x+w, y)`, `(x+w, y+min(2H, 45))`, `(x, y+min(2H, 45))`. Fonts are stretched 2× vertically.
- UV:
  - `u0 = (g.u − font[0].u)/256 + texU + 1/512`
  - `u1 = (g.u − font[0].u + g.w)/256 + texU`
  - `v0 = (g.v − font[0].v)/256 + texV`
  - `v1 = v0 + (H−1)/256`
- Emitted as 6 vertices `v0,v1,v2, v2,v3,v0`. x advances by `g.w`.

**Draw:** one `DrawPrimitive(TRIANGLELIST, 0x1C4, stackBuf, 6·n, 0)`. No state is set here; the flush sets block 5 (FLAT, blend, MODULATE).

**Exe callers**

- Global `DAT_005388a0`, 1 site: `FUN_00481c20(font, string, x, y, centre)`. It is a frontend immediate draw.
- It builds the struct on the stack:
  - `font = fontObj + 0x3D7C`
  - `x, y` (centred if requested)
  - `color = 0xFFFFFFFF`
  - `flags = 9`
  - `tex = *(short*)(DAT_00520668+0x3F8) + fontId`
- In-game text uses `afAddTextToSortList` instead.
- The glyph table comes from `"%s.TAB"` (loaded by exe `FUN_00430d50`). The font images are `language\tims\fonttims.mad`, `smlfont.bmp` and so on.

**D3D11 notes:** Use a dynamic vertex buffer of glyph quads. All of bucket 1 (text and lines) can be batched in one draw per texture page. Keep the 2× vertical stretch and the 45-pixel cap.

### afAddObjectToSortList — `0x10008ca0` (ordinal 40)

```c
void __cdecl afAddObjectToSortList(AfObj *obj, char transparent);
```

**What it does**

- `d = dot(obj.t − cam, forward)`.
- Bucket:
  - `9999` if `obj->type == 2` (sky),
  - else `|d| >> 3`,
  - plus `0x200` if `transparent && obj->alphaTarget != 0xFF`.
  - Adding `0x200` makes the object drawn **earlier**, not later.
- Dropped if `bucket ≥ 10000` or the pool is full (8000 nodes).
- Node `{obj, type 10, next}` is pushed at the front of the bucket.
- `DAT_11b6a278 = max(bucket)` is informational only.

**Exe callers**

- Global `DAT_00538064`, 4 sites, always with `transparent = 0`:
  - `FUN_004501b0` ×2: the two sky objects (`DAT_00537fa0+4`, `+8`; type 2), repositioned each frame.
  - `FUN_004582f0`: the scanner "top" object.
  - `FUN_0045e110`: every map object (crates, buildings and so on). Its matrix is copied from the game object and transposed.
- Object type and fade mode are set by the exe (`FUN_0045de90`):
  - ids `0x14..0x17`: type 4, `+0x54 = ((id−0x14)<<8) | 4` (scanner blips);
  - ids `0x1C..0x17B`: type 0, `+0x54 = 3` (fading occluders);
  - all others: type 0, `+0x54 = −1`.

### afAddAnimModelToSortList — `0x10008db0` (ordinal 38)

```c
void __cdecl afAddAnimModelToSortList(AfAnimModel *m);
```

**What it does**

- `d` is computed from the **int16 position at `m+0x2E/+0x30/+0x32`**.
- Bucket = `(|d| >> 3) + 0x200`, i.e. treated as 4096 units farther away so it is drawn earlier.
- Node type 11.
- **Uncertain:** the only exe writer of `m+0x2E..0x32` I found is the creation function `FUN_004407e0` (spawn position). The sort key may therefore be stale.

**Exe callers**

- Global `DAT_005381bc`, 3 sites, all in `FUN_00440a20` (per hog per frame).
- `FUN_00440a20` first sets `m->flags (+0x3C)`:
  - 1 or 3 = active hog,
  - 4 = normal,
  - 0xFF = no blip,
  - negated when the hog is invisible,
  - −1/−2 in view mode 9.
- It also sets the matrix and translation `(x, y+0x41, z)`, attachments, frame numbers and alpha.

**Flush behaviour (case 11)**

- Drawn only if `(int16)flags > 0`: state `0xD`, then `afDrawAnimModel(m, 0, 0)`.
- Unless `flags == 0xFF`, a scanner blip is appended (§4 scanner) with colour white if `|flags| & 2`, else the team colour `0xFF000000 | R<<16 | G<<8 | B` from `DAT_1002be70/74/78[team*3]`.

### afAddSkyToSortList — `0x10008eb0` (ordinal 41)

```c
void __cdecl afAddSkyToSortList(void);
```

**What it does:** Pushes a node of **type 12 into bucket 9999 without setting its item pointer**. The node keeps whatever pointer was left in that pool slot.

- The flush handles bucket 9999 specially: before the ground, it draws `afDrawObj(head->item)` and `afDrawObj(head->next->item)` **with no NULL checks**, then clears the bucket.
- Type 12 has no case in the walk.

**Exe callers:** global `DAT_00538160`; **not called**. Sky objects go in through `afAddObjectToSortList` (type 2 → bucket 9999), and the exe always adds exactly two.

**D3D11 notes:** Draw the first two type-2 objects first (in LIFO order, i.e. `DAT_00537fa0+8` before `+4`). Guard against fewer than two.

### afAddTextToSortList — `0x10008f10` (ordinal 42)

```c
void __cdecl afAddTextToSortList(AfText *t);
```

**What it does:** Pushes `{t, type 13}` into **bucket 1**, the same for 2D and 3D text, so text draws after almost everything. The flush sets block 5, then calls `afDrawText(t)`. The struct is referenced, not copied, so it must stay alive until the flush.

**Exe callers**

- Global `DAT_00538188`, 1 site: `FUN_00430ed0`, the exe's text printer.
- It fills one of 100 ring records (`this + i*0x9C + 0x84`):
  - `flags = 2` for world text, else 1; `|= 8` when topmost; `|= 4` always;
  - `color = A<<24 | R<<16 | G<<8 | B` from `this+0x38/0x2C/0x30/0x34`.

### afAdd2dPolyToSortList — `0x10008fa0` (ordinal 37)

```c
void __cdecl afAdd2dPolyToSortList(AfPoly2D *p, int count);   /* struct in §5.4 */
```

**What it does**

- If `p[0].flags & 0x20` (prebuilt-vertex mode): loop over `count` elements of stride 0x28 and copy only `tex (+0x10)`, `flags (+0x1C)` and `verts (+0x20)` into the pool.
- Otherwise: copy **one** descriptor (0x1E bytes); `count` is ignored.
- The pool is `DAT_1011db08`, stride 0x28, index `DAT_11ab9450` (reset in `Begin2D`). It has **no bounds check**; about 2300 entries fit before the next global.
- **Bucket:**
  - flag 2 (world-anchored): `|d|>>3`, with `d` from `(x, y, z)` int16. Flag `0x40` subtracts 100, clamped at 0.
  - otherwise **bucket 8**.
- Node type 14. The flush calls `afDraw2dPolyIn3dWorld(poolEntry)`.

All screen-space HUD polys share bucket 8 and come back **in reverse submission order**. Because they also share sz `1.526e-4` with `LESSEQUAL`, the last submitted is drawn first, and earlier submissions overdraw it. **This ordering must be reproduced.**

**Exe callers**

- Global `DAT_0054c5c8`, 47 sites in 20 functions, mostly the in-game HUD under `FUN_00457840`.
- Examples:
  - `FUN_00457eb0`: `{x=0, y=0, w=0xF0, h=screenH, tex=-1, color=0xFF000000, flags=1}` — black side bars.
  - `FUN_00440a20` (view mode 9): `{x, y, z, w=-1, h=-1, tex=*(DAT_00520668+0x3F4), color=0xFF<<24|team RGB, flags=0x13}` — world-anchored, centred, constant-depth hog marker.
  - `FUN_0044fa50`/`FUN_0044fe00`: `afAdd2dPolyToSortList(base+0x10004, 0x80)` — 128 prebuilt fans, an array with flag `0x20` (weather-like particles; uncertain).
- Flag values seen: `1`, `5`, `0x81`, `0x85`, `0x101`, `0x105`, `0x181`, `0x13`, `0x41`, `0x501`, `0x901`, plus arrays with `0x20`.

### afAddLinesToSortList — `0x100092b0` (ordinal 39)

```c
void __cdecl afAddLinesToSortList(AfLine *lines, int count);   /* 24-byte records, §5.6 */
```

**What it does:** Copies each line to the pool `DAT_101342e0` (stride 0x18, index `DAT_11ab9458`, reset in `Begin2D`, no bounds check). Each is pushed as type 15 into **bucket 1**. The flush sets block 1 (opaque, flat) and calls `afDraw3dLine`.

**Exe callers:** global `DAT_005380b0`; **not called**.

### afDraw2dPolyIn3dWorld — `0x1000ea50` (ordinal 51)

```c
void __cdecl afDraw2dPolyIn3dWorld(AfPoly2D *p);
```

**Prebuilt path** (`flags & 0x20`): `DrawPrimitive(TRIANGLEFAN, 0x1C4, p->verts, 4, 0)`.

- `tex == -1`: block `0xD` and `SetTexture(NULL)`.
- otherwise, if `!(flags & 0x40)`: block 5 + page texture;
- otherwise: block 3 (additive) + page texture, then restore `SRCBLEND=5`, `DESTBLEND=6`.

**Normal path**

1. **State:** flag `0x40` → block 3 (additive); else `tex == -1` → block `0xD`; else block 5.
2. **UVs** (textured), in the order TL, TR, BR, BL:
   - `(u0+½t, v0+½t)`, `(u0+w−½t, v0+½t)`, `(u0+w−½t, v0+h−½t)`, `(u0+½t, v0+h−½t)`, with `½t = 1/512`.
   - Flag 4 mirrors U. In the original code the mirrored insets go the "wrong way": `u0+w+½t` / `u0+½t`.
   - Flag `0x80` mirrors V by swapping V0↔V3 and V1↔V2.
   - The default size is `w = texW·256 − 1`, `h = texH·256 − 1` pixels (1:1 texel mapping). It is used when `p->w == -1`.
3. **Position**
   - Flag 2: project `(x, y, z)` (path 2). If behind the camera, return (restoring the blend if `0x40`).
   - Size: flag `0x40` → `w = p->w/d`, `h = p->h/d`; else flag 8 → `w = texW·1000/d`, `h = texH·1000/d`.
   - Otherwise `(x, y)` are screen pixels.
   - Flag `0x10` centres the quad on `(x, y)` (`±w/2`, `±h/2`).
4. **Colour:** all four vertices get `p->color`.
   - Flag `0x400`: the top edge (V0, V1) gets `p->color2`.
   - Flag `0x800`: the bottom edge (V2, V3) gets `p->color2`.
   - Gradients are only visible when untextured (Gouraud block `0xD`); textured uses FLAT block 5.
5. **Depth**
   - Flag 1: `sz = 0` if flag `0x100`, else `1.526e-4`; `rhw = 15`.
   - Without flag 1: `sz = d·2e-6`, `rhw = 15`. If flag 2 is also clear, `d` is uninitialised stack data, so callers always set flag 1 for screen polys.
6. `DrawPrimitive(TRIANGLELIST, 0x1C4, v, 6, 0)` with order `V0, V1, V2, V2, V3, V0`. If `0x40`, the blend is then restored to 5/6.

**Exe callers**

- Global `DAT_0054c5b0`, 22 sites. These are immediate draws, all in the frontend between `FUN_00481bb0` (`Begin2D`, `AddDisplayWindow`, `DisplayCurrentHole(0)`, `Begin3D`) and `FUN_00481c10` (`End3D`, `End2D`):
  - `FUN_0047fcd0` (transition), `FUN_004820e0`, `FUN_00482cd0` ×4, `FUN_00483010` ×3, `FUN_004833b0` ×7, `FUN_00483980` ×2, `FUN_00483cc0`;
  - three sites in code not inside any defined Ghidra function: `0x4196c3`, `0x4199ac`, `0x419bed`.
- **Representative call** (`FUN_0047fcd0`, fade to black over the 3D hog):

  ```c
  AfPoly2D p = { .x=0, .y=0, .w=screenW, .h=screenH, .tex=-1,
                 .color = alpha<<24 /* black */, .flags = 0x101 };
  afDraw2dPolyIn3dWorld(&p);
  ```

- Menu text images (`FUN_00482cd0`) use `{x, y, w=-1, h=-1, tex=item, color=0xFF|menuRGB or 0xFFFFFFFF (blink), flags=0x41}`, i.e. **additive**.
- The undefined-code sites use flags `0x51` with 40×40 additive glows of colour `0x7F7F7F7F`.

### afDraw3dLine — `0x1000f510` (ordinal 52)

```c
void __cdecl afDraw3dLine(AfLine *l);
```

**What it does**

- `flags & 1` (screen): vertices at `(x, y)`, `sz = 1.526e-5`, `rhw = 15`.
- Otherwise: project both endpoints with path 2. If either is behind the camera, return. `sz = d·2e-6`, `rhw = 8/d`.
- Colour = `l->color`, spec `−1`.
- `SetTexture(NULL)`, then `DrawPrimitive(LINELIST, 0x1C4, v, 2, 0)`.
- It sets no state itself; the flush sets block 1 (opaque).

**Exe callers:** global `DAT_00538104`; **not called**. It is only reached through `afAddLinesToSortList`, which is also unused.

### PlacePolyInWorld — `0x1000be10` (ordinal 20)

```c
void __cdecl PlacePolyInWorld(const AfPlaceVert *v, int n, int texinfo, int flipU, int flipV, int mode);
/* AfPlaceVert (36 B) = { float x, float z, float y, float u, float v, int a, int r, int g, int b } */
```

**What it does**

- Appends a 0x274-byte record to the pool `*DAT_11ad9f9c`, count `DAT_11b6a2ac`. There is no bounds check, and at most 8 vertices fit per record.
- Counts are reset in `Begin2D`, `Begin3D` and `DisplayCurrentHole`.

**At flush time** (`FUN_1000c040`, called after the ground and before the bucket walk)

- Per vertex: `d`, `rhw = 8/d`, `sz = max(d·2e-6, 1.527e-5)`, integer screen x/y (path 2).
- Copy u/v.
- Insert the record as type 3 at bucket `|d(v0)| >> 3`, i.e. the first vertex's depth.

**Draw (case 3)**

Vertices are `sx + DAT_11ab8b00`, `(H − sy) + DAT_11ab8afc`; colour = `A<<24 | R<<16 | G<<8 | B` from the ints, with **no clamp**; spec `−1`.

| mode | State | Texture/UV | Primitive |
|---|---|---|---|
| 1 | block 5 (alpha blend) | page of `texinfo`. Per-vertex u/v with a `±1/512` inset whose direction depends on `flipU`/`flipV` (v0, v1 inset +u; v2, v3 −u; v0, v3 +v; v1, v2 −v; flipped when the flag is non-zero). | FAN, 4 vertices |
| 6 | block 5 + `SRC=ZERO`, `DEST=SRCCOLOR`, then restored to 5/6 | as mode 1 | FAN, 4 vertices |
| 0xB | block 5, `SetTexture(NULL)` | none | FAN, `n` vertices |
| 0xE | block 3 (additive), restored after | fixed 64×64-texel cell: `(u0,v0)`–`(u0+0.25, v0+0.25)` at the texinfo origin; per-vertex u/v ignored | FAN, 4 vertices |
| other | not drawn | – | – |

**Exe callers**

- Global `DAT_00538090`, 14 sites:
  - `FUN_0044eba0` ×6, **blob shadow** under hogs and objects: the ground quad (split at 512-unit tile edges) at height `ground+5`, `tex = *(DAT_00520668+0x3F0)+0xF`, colour `A=0x40, RGB=0xFF`, mode 1.
  - `FUN_0044b400` ×7, **scorch/crater decals**: grid quads, mode from decal `+0x28` (always 1), alpha fading over lifetime from `0x4A`/`0x80`.
  - `FUN_0048a0c0` ×1, **expanding ring**: mode `0xE` (additive), `tex+2`, flipU=2, flipV=1, colour from an RGB555 value ×400/(k+1), alpha `0xFF`.
- Modes 6 and `0xB` are unused.
- At the decal call site the exe pushes `flipV` from `DL` without zero-extending EDX. The DLL tests the full dword, so the inset direction may come from garbage upper bits. Low impact; uncertain.

### SetLines — `0x1000c2d0` (ordinal 31)

```c
void __cdecl SetLines(const AfLineVert *verts /*36 B: float x, z, y, …*/, int nVerts /*≤1000, else Terminate(0x1D,0x60,1)*/,
                      const AfLineIdx *lines /*12 B: int i0, int i1, uint32 color*/, int nLines);
```

**What it does:** Only stores pointers and counts:

- `DAT_11b65e98` = verts, `DAT_11b6a2c0` = nVerts
- `DAT_11b65ea0` = lines, `DAT_11b6a2bc` = nLines

At flush time `FUN_1000c310` projects the vertices into `DAT_11ab94fc` (20 B each: int sx, int sy, float d, float rhw, float sz). Each line with both depths ≥ near (`DAT_1002c53c`) becomes a type-4 node at bucket `max(0, d>>3)`. Case 4 draws block 1, `SetTexture(NULL)`, `LINELIST` with 2 vertices, colour = `line.color`.

**Exe callers:** global `DAT_0054c7fc`; **not called** (golf leftover).

### afForceTransparencyOff — `0x1000da00` (ordinal 56)

```c
void __cdecl afForceTransparencyOff(void *gameObject);
```

**What it does:** Stores the argument in `DAT_11c94838`. `afDrawAnimModel` also writes the active hog's `+0x34C` (its target object) there. **Nothing in the DLL reads `0x11c94838`.** I searched all instructions; this is a dead store.

The intent (presumably from the PSX version) was "do not fade this object". In the PC build the occluder fade ignores it.

**Exe callers:** global `DAT_0053804c`, 1 site: `FUN_0044e290`. It is called for map objects of class `0x1359` when the object is the current hog's target (`hog+0x170`): `afForceTransparencyOff(gameObj)`, then the object is added to the sort list.

**D3D11 notes:** To restore the intended behaviour, skip the fade for the DLL object `gameObj+0x58`. This is a behaviour change, so flag it as optional.

### afInitScanner — `0x1000a260` (ordinal 69)

```c
void __cdecl afInitScanner(int texinfo);   /* exe passes 3 args; only the first is read */
```

**What it does** (if `texinfo != -1`)

1. Release the old surface `DAT_11ab9464`.
2. Create a 64×64 system-memory texture surface: `DDSD = 0x1007`, caps `0x1800`, pixel format `DAT_1013e420`.
3. Lock it and fill one texel per map tile (64×64):
   - colour = terrain palette `DAT_1002beb8/bc/c0[(tile.flags & 0x1F)*3]` × height shade, then `>>9`;
   - shade = `130·(h − minH)/(maxH − minH) + 64`;
   - tiles with `flags & 0x40` → pure red;
   - clamp to 31 and pack as RGB555 with A=1 when the R mask is `0x7C00`, else 565.
4. Unlock, then `afOverwriteTexture(surface, 0, texinfo)`, which copies the 64×64 image into the page at that TIM's location.
5. `DAT_1002bf48 = texinfo`. Set up the map mesh:
   - 16 vertices = 2×2 quads covering `±size` in XZ: `DAT_11ab1a10…`, stride 16 B;
   - UVs `u0 + {1/512, 32/256, 63.5/256}` and the same for v: `DAT_11ab8ee0`/`DAT_11ab1ce8`.

**Exe callers:** global `DAT_00538000`, 1 site, `FUN_004544e0` (level load):

```c
afInitScanner(*(DAT_00520668+0x40C), *(DAT_00537f24+0x4E8), *(DAT_00537f24+0x4EC))
```

The same function loads `chars\top.mad` as the scanner object: `afCreateObj2`, scale 1.0, Y rotation 0x400, `type = 1`, `+0x4C = (xOff<<20)|(yOff<<8)|0xFF`.

**Scanner drawing** (`FUN_10009810`, always called at the end of the bucket walk with `local_b4`)

- `local_b4` is the `+0x4C` of the last type-10 object whose `(+0x4C & 0xFFF00000) != 0`. That is the scanner object, which is then **not drawn as a mesh**. It is only a carrier for the screen offset.
- The current size `DAT_1002c684` moves towards the target `DAT_1002c680` by 0.0015 per frame. World→scanner scale = `DAT_11b8a7c0 = 18884/size`.
- **Map plane:** projected in a camera-yaw-relative frame (tables `DAT_11ada2e0`/`DAT_11b66050[DAT_1002c530]`, constant 480.0, centre `DAT_1002c51c/520`). Offset by `−(param>>20 & 0xFFF)` in x and `+(param>>8 & 0xFFF)` in y. `sz = 0`, `rhw = 15`, colour `0xFFFFFFFF`. Drawn with block `0xD`, texture `texinfo[DAT_1002bf48]`, `TRIANGLELIST` 24 vertices. The exact plane placement is uncertain.
- **Blips:** the list is built during the walk:
  - type-10 objects with `type == 4`: code `abs(+0x54)`;
  - anim models with `flags != 0xFF`: code 0, colour from team/white.
  - Each blip is a 10×11-pixel quad at `(sx−5, sy−5)`, `sz = 0`, `rhw = 15`.
  - Icon = `texinfo[DAT_1002bf48 + k]`:
    - k = 1 for hogs (team colour),
    - code 4 / variant 0 or 2 → k = 3, colour `0xFFA9943F`,
    - variant 1 → k = 2, colour `0xFFFF4079`,
    - variant 3 → k = 4, colour `0xFFDB8835`.
  - Blips are appended to set A batches, then all set A batches are flushed with the object texture table.

### afSetScannerSizeSmall — `0x1000ffa0` (ordinal 86)

```c
void __cdecl afSetScannerSizeSmall(int small);
```

**What it does:** `DAT_1002c680 = small ? 0x3DF7F499 (0.1211f) : 0x3E1AB29E (0.1511f)`. The scanner animates towards the new size. `afIfaceInit` calls it with 0.

**Exe callers:** global `DAT_00538044`, 1 site: wrapper `FUN_0044f990(char)`. The wrapper is called from 8 sites in 5 functions (`FUN_0048f490`, `FUN_00493bb0`, `FUN_00493e40`, `FUN_00494430`, `FUN_0049f740`), toggled with the HUD flag `hud+0xC68`.

---

## 5. Structures

### 5.1 `AfObj` — object instance, 0x5C bytes

Pools: `DAT_11cad568` (static) or `DAT_11cbd768` (ring of 128), chosen by `DAT_11cc0574`. Created by `afCreateObj2` → `FUN_100110e0`.

| Off | Type | Meaning |
|---|---|---|
| +0x00 | ? | unused by group D |
| +0x04 | float m[12] | rows `m[0..2]`, `m[3..5]`, `m[6..8]` (3×3) and translation `m[9..11]` (+0x28 x, +0x2C y, +0x30 z). Row-vector convention: `world = x·row0 + y·row1 + z·row2 + t`. |
| +0x34 | float[3] | view-space origin (written by the culling test) |
| +0x40 | int | vertex count (copy of `model+0x10`) |
| +0x44 | AfModel* | geometry |
| +0x48 | int | type: 0 normal; 1 screen overlay (sz=0, scanner object); 2 sky (bucket 9999, fog off, sz 0.9, sky-brightness colour); 3 character mesh (texture swap); 4 normal + scanner blip |
| +0x4C | uint32 | target alpha (bits 0–7). Bits 8–19 y offset, bits 20–31 x offset; non-zero high bits make the flush treat it as the scanner carrier. |
| +0x50 | uint32 | current (smoothed) alpha; low byte goes to vertex A |
| +0x54 | int | fade mode: 3 = occluder fade. For type 4: blip code `(variant<<8)|4`. −1 = none. |
| +0x58 | ? | not used by group D |

### 5.2 `AfModel` — 0x94 bytes

Pools `DAT_11c94f58` / `DAT_11cb8d68`.

| Off | Type | Meaning |
|---|---|---|
| +0x00 | float4* | vertices (x, y, z, w = bone index for skinned meshes) |
| +0x04 | int* | primitive block |
| +0x08 | float4* | normals |
| +0x0C | int | normal count |
| +0x10 | int | vertex count |
| +0x14 | float4[8] | bounding-box corners |

### 5.3 Model primitives (block sections 5 and 6)

**Triangle, 32 B**

| Off | Field |
|---|---|
| +0 | u8 uv[3][2] |
| +6 | u16 vtx[3] |
| +0xC | u16 nrm[3] |
| +0x12 | pad |
| +0x14 | int slot (−1 = skip) |
| +0x18 | int texinfo |
| +0x1C | uint alpha / transparency |

**Quad, 36 B**

| Off | Field |
|---|---|
| +0 | u8 uv[4][2] |
| +8 | u16 vtx[4] |
| +0x10 | u16 nrm[4] |
| +0x18 | int slot |
| +0x1C | int texinfo |
| +0x20 | uint alpha |

Quad order is cyclic, so it splits as `(0,1,2)`, `(2,3,0)`. `afCreateObj2(..., dropTransparent=1)` sets the slot to −1 for colour-keyed primitives (used for the sky).

### 5.4 `AfPoly2D` — 2D poly descriptor (`afDraw2dPolyIn3dWorld`, `afAdd2dPolyToSortList`)

| Off | Type | Meaning |
|---|---|---|
| +0x00 | int16 x, y, z | screen pixels, or world position when flag 2 |
| +0x06 | int16 | pad |
| +0x08 | int32 w | pixels; −1 = texture size (texels−1) |
| +0x0C | int32 h | |
| +0x10 | int32 tex | texinfo index; −1 = untextured |
| +0x14 | uint32 color | ARGB |
| +0x18 | uint32 color2 | gradient colour |
| +0x1C | uint16 flags | see below |
| +0x1E | uint16 | pad |
| +0x20 | D3DTLVERTEX* verts | flag `0x20` only (4-vertex fan); array element stride 0x28 |

Flags:

| Flag | Meaning |
|---|---|
| `0x001` | screen depth (sz = 1.526e-4, rhw = 15) |
| `0x002` | world-anchored |
| `0x004` | mirror U |
| `0x008` | size ×1000/d |
| `0x010` | centred |
| `0x020` | prebuilt vertices |
| `0x040` | additive (ONE/ONE), size /d, sort −100 |
| `0x080` | mirror V |
| `0x100` | sz = 0 |
| `0x400` | color2 on top edge |
| `0x800` | color2 on bottom edge |

### 5.5 `AfText` — 0x9C bytes

| Off | Type | Meaning |
|---|---|---|
| +0x00 | u32 | unused |
| +0x04 | char[128] | NUL-terminated string |
| +0x84 | int16* | glyph table: entries `{u, v, w, h}`; entry 0 = origin, entry `c − 0x1F` = glyph |
| +0x88 | int16 wx, wy, wz | world anchor (flag 2) |
| +0x8E | int16 | pad |
| +0x90 | int16 sx, sy | screen position (overwritten for 3D text) |
| +0x94 | u32 color | ARGB (changed by `/C`) |
| +0x98 | u16 flags | 2 = world, 8 = sz 0; 1 and 4 are set by the exe but not tested |
| +0x9A | u16 tex | texinfo index of the font image |

### 5.6 `AfLine` — 0x18 bytes

`int16 x0, y0, z0, pad, x1, y1, z1, pad; uint32 color; uint16 flags (bit 0 = screen); uint16 pad`.

### 5.7 `AfAnimModel` — 0x360 bytes (allocated by the exe)

Fields used by the DLL:

| Off | Meaning |
|---|---|
| +0x02 | int16 character type (LOD and scale tables) |
| +0x04 | int16 team (scanner colour) |
| +0x08…+0x28 | animation frame, blend and counter state |
| +0x2C | int16 skeleton type → `DAT_11cc0584` |
| +0x2E/+0x30/+0x32 | int16 sort position |
| +0x3C | int16 draw/scanner flags |
| +0x40 / +0x44 / +0x48 | attachment `AfObj*` on bones 5 / 2 / 1 |
| +0x4C…+0x7B | float m[12] world matrix (translation at +0x70/+0x74/+0x78) |
| +0x7C…+0x34B | 15 bone matrices (12 floats each) |
| +0x34C | target game object (fade-exemption pointer) |
| +0x350 | int skin page slot |
| +0x354 / +0x356 | int16 texture swaps |
| +0x358 | int in-water flag (written by the DLL) |
| +0x35C | int alpha |

### 5.8 Ordering table and pools

| Global | Meaning |
|---|---|
| `DAT_11ab9950` | `int**` → bucket heads `[10000]` (allocated by `FUN_1000c730`) |
| `DAT_11ada2cc` | node pool, 8000 × `{void *item; int type; node *next}` (12 B) |
| `DAT_11b6a2c4` | node count, reset in `Begin3D`. Heads are cleared **only by the walk**. |
| `DAT_11b6a278` | highest bucket used (not used by the walk) |
| `DAT_1011db08` / `DAT_11ab9450` | 2D poly pool (stride 0x28) / index |
| `DAT_101342e0` / `DAT_11ab9458` | line pool (stride 0x18) / index |
| `*DAT_11ad9f9c` / `DAT_11b6a2ac` | placed-poly records (0x274) / count |
| `DAT_1013e440[32]`, `DAT_1013ed08 + s*0xC7380` | set A counts / buffers (opaque; also ground and blips) |
| `DAT_11ab8cb0[32]`, `DAT_11a26520 + s*0x4000` | set B counts / buffers (colour-keyed textures) |
| `DAT_11ab8f20` (int16 x, y, z, pad), `DAT_1013e4c0`, `DAT_11ab8b18`, `DAT_11ab94c0` | scanner blip positions, codes, colours, count |

Node types: 0 (ground-cell quad; no producer in this build), 3 placed poly, 4 SetLines line, 10 object, 11 anim model, 12 sky (broken), 13 text, 14 2D poly, 15 line.

---

## 6. Frame order (in-game)

The exe (`FUN_0044e290`) calls `Begin2D` (BeginScene, pool resets, focal), then `AddDisplayWindow`, then `Begin3D` (sort-list reset), then adds everything to the sort list, then `DisplayCurrentHole(1,…)`, which runs `FUN_10003380(9999, 0)`:

1. **Sky fade.** If a fade is running (`DAT_11c94824` from `afStartSkyFade`): brightness `DAT_1002be68` moves ±16 per frame, and `FOGSTART = DAT_11ab8ca8 + (255−b)·30`, `FOGEND = DAT_1011d6d8 + (255−b)·30`.
2. **Background** (`FUN_1000b6e0`): an opaque untextured fan at `sz 0.99999`, `rhw 0.2`. Its colour comes from a computed global; it is probably the fog/background colour (uncertain). When brightness < 255 a second quad is drawn: black with alpha `255−b`, block 5.
3. **Sky:** two nodes from bucket 9999 (block `0xD`, fog off/on), then the bucket is cleared.
4. **Water** (`FUN_1000b440`) if the map has water. Ground tiles are batched into set A and flushed per ground texture (`DAT_1004cf90`, stride 0xD4). Both belong to other groups.
5. **Placed polys and SetLines** are projected and inserted.
6. **Bucket walk 9999 → 0**, LIFO per bucket, immediate draws with the state switches of §3.
7. **Scanner** (map plane + blips).

The exe then blits HUD bitmaps with DirectDraw (`FUN_00459550`, `FUN_0045ee30`, `FUN_00459ac0` through `FUN_0044d4d0`/`FUN_0044d560`), calls `End3D` (no-op), `End2D` (EndScene) and `CopyToScreen`.

**Frontend:** `DisplayCurrentHole(0,…)` does not flush. `afDrawObj`, `afDrawAnimModel`, `afDraw2dPolyIn3dWorld` and `afDrawText` draw immediately, in call order.

---

## 7. Every render state and texture stage state set by the DLL

Initial block (`FUN_10006380`, from device creation `FUN_10006240` and `afIfaceInit`):

| State | Value |
|---|---|
| ZWRITEENABLE (14) | TRUE |
| ZFUNC (23) | LESSEQUAL (4) |
| ZENABLE (7) | TRUE (1) |
| ALPHATESTENABLE (15) | TRUE |
| ALPHAFUNC (25) | GREATER (5), then **GREATEREQUAL (7)** |
| ALPHAREF (24) | 0, then **8** |
| SRCBLEND (19) | SRCALPHA (5) |
| DESTBLEND (20) | INVSRCALPHA (6) |
| CULLMODE (22) | CCW (3) |
| TEXTUREPERSPECTIVE (4) | TRUE |
| SPECULARENABLE (29) | FALSE |
| ANTIALIAS (2) | NONE (0) |
| DITHERENABLE (26) | TRUE |
| FILLMODE (8) | SOLID (3) |
| STIPPLEENABLE (39) | FALSE; TRUE if `DAT_11ab1d28` (set in PowerUp; probably "device cannot alpha-blend", uncertain) |
| STIPPLEDALPHA (33) | FALSE; TRUE if `DAT_11ab1d28` |
| COLORKEYENABLE (41) | FALSE |
| LIGHTING (137) | FALSE |
| SHADEMODE (9) | GOURAUD (2) |
| ALPHABLENDENABLE (27) | TRUE |
| TSS0 ALPHAOP | MODULATE (4) |
| TSS0 COLORARG1 | TEXTURE (2) |
| TSS0 COLORARG2 | DIFFUSE (0) |
| TSS0 ALPHAARG1 | TEXTURE (2) |
| TSS0 ALPHAARG2 | DIFFUSE (0) |
| TSS0 MAGFILTER | LINEAR (2) |
| TSS0 MINFILTER | LINEAR (2) |

These are never set, so D3D7 defaults apply: COLOROP MODULATE, MIPFILTER NONE, ADDRESS WRAP.

Changed at runtime:

| State | Values used |
|---|---|
| SHADEMODE | FLAT (1) / GOURAUD (2) |
| ALPHABLENDENABLE | 0 / 1 |
| TSS0 ALPHAOP | SELECTARG1 (2) / MODULATE (4) / BLENDDIFFUSEALPHA (12) |
| SRCBLEND / DESTBLEND | ONE/ONE (additive); ZERO/SRCCOLOR (multiply); restored SRCALPHA/INVSRCALPHA |
| FOGENABLE (28) | 0/1 (`afDrawObj` sky, `afSetFog`, `afSetWeatherValues`) |
| FOGCOLOR (34), FOGTABLEMODE (35) | `FOGTABLEMODE` = LINEAR (3); colour set by `afSetFog`/`afSetWeatherValues` |
| FOGSTART (36), FOGEND (37) | `afSetFog`, sky fade |
| DITHERENABLE (26) | 0/1 (`afSetWeatherValues`) |

The `+0x50` vtable calls in `PowerDown`, `SelectDriverMode`, `FUN_10005590`, `FUN_10005820` and `FUN_1000c8d0` are `IDirectDraw7::SetCooperativeLevel` (values 8 / 0x51), not render states.

---

## 8. Notes for a Direct3D 11 reimplementation

### 8.1 Vertex path

- Keep CPU transform and feed TL vertices. This is simplest and exact.
- VS: `w = 1/rhw`; `pos = float4((sx + 0.5)/W·2 − 1, 1 − (sy + 0.5)/H·2, sz, 1) · w`. The +0.5 maps D3D7 pixel centres to D3D10+.
- Pass colour, uv and the page index (Texture2DArray of 32 × 256²).

### 8.2 Pixel shaders

| Variant | Colour | Alpha |
|---|---|---|
| Modulate (blocks `0xD`/5) | `tex·diff` | `tex.a·diff.a` |
| Additive (block 3) | `tex·diff` | `tex.a·diff.a + diff.a·(1−diff.a)` (BLENDDIFFUSEALPHA) |
| Opaque (block 1) | `tex·diff` | `tex.a` |
| Untextured (`SetTexture(NULL)`) | `diff` | `diff.a` |

- Every variant ends with `clip(alpha − 8/255)`.
- For untextured draws, the original assumes a NULL texture reads as opaque white. That is my assumption for the D3D7 behaviour.

### 8.3 Fixed-function states to recreate

- **Blend states:**
  - Opaque
  - Alpha (`SRC_ALPHA/INV_SRC_ALPHA`)
  - Additive (`ONE/ONE`)
  - Multiply (`ZERO/SRC_COLOR`, only if placed-poly mode 6 is kept)
- **Depth:** one state: test LESS_EQUAL, write on.
- **Rasterizer:** `CULL_BACK` with `FrontCounterClockwise = FALSE` (= D3D7 `CULL_CCW` in screen space; all quads above are clockwise on screen).
- **Sampler:** linear min/mag, no mips, WRAP.
- **Flat shading** (blocks 5/3/1): `nointerpolation` colour. Producers that use FLAT always give all vertices the same colour, so the provoking-vertex convention does not matter.
- **Fog** is table/W-based linear fog on `w = 1/rhw`. That is an assumption, and fog belongs to another group. Sky is unfogged. HUD (`rhw = 15`) is effectively unfogged.

### 8.4 Exact draw order

- Implement the 10 000-bucket ordering table with LIFO lists and walk 9999→0.
- Special steps: background → two sky nodes → water/ground → placed-poly/line insertion → walk → scanner.
- Within an object: set A slots 0..31, then set B slots 0..31.
- **Clear bucket heads in `Begin3D`.** The original clears them only in the walk, which is unsafe if a frame adds nodes without flushing.

### 8.5 Batching

- All producers within one blend/shading state can go into one dynamic VB. Split only at state changes.
- The z-buffer plus in-order rasterisation keeps results identical. Texture changes disappear with the texture array.
- Typical frame: a handful of draws (alpha/Gouraud, alpha/flat, additive runs, opaque lines).

### 8.6 Quirks to keep or decide on deliberately

- HUD reverse order inside bucket 8.
- Unclamped colour packing.
- Sky triangles use the brightness grey; sky quads stay white.
- FOGENABLE is forced on after the sky.
- Shared LOD-mesh alpha smoothing.
- Hog sort key probably stale.
- `afForceTransparencyOff` is a no-op.
- Direct frontend `afDrawObj`/`afDrawText` inherit the previous state. Recommend block `0xD` for objects and block 5 for text.

### 8.7 Capacity limits in the original

- 8000 nodes
- 512 vertices per set-B slot
- 25 500 vertices per set-A slot
- ~2300 2D polys
- 8 vertices per placed poly
- 128 glyphs per string
- 1000 SetLines vertices

Use growable buffers, but cap the sort list at 8000 to stay identical.
