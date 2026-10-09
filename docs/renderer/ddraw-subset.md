# DirectDraw 7 / Direct3D 7 subset used by Hogs of War

Specification for the replacement `DDRAW.dll` (Direct3D 11 back end). Static analysis only: nothing was run, nothing in the Ghidra programs was renamed, retyped or commented.

## Legend and sources

| Tag | Meaning |
|---|---|
| **[V]** | Verified in this session in Ghidra: decompile, disassembly, xrefs or raw bytes of `warhogs_unwrapped.exe` (base `0x400000`), `_d3d.dll` (base `0x10000000`) or `binkw32.dll` (imported this session, base `0x10000000`). Bulk call-site argument tables were produced by a script that disassembled the same on-disk images; sites were spot-checked in Ghidra. Still tagged **[V]**. |
| **[R]** | Taken from reports A to D in this folder, not re-checked. |
| **[V,R]** | A report claim that I re-checked. |
| **[U]** | Unknown or an assumption. |

Conventions: vtable offsets are bytes (x86-32, 4 bytes per slot). Addresses `0x004xxxxx` are in the exe, `0x1000xxxx` in `_d3d.dll`, except where "bink" is named. `FUN_xxxxxxxx` names are Ghidra's auto names. `back` = the back-buffer surface, `screen` = the exe's screen object (`DAT_0052083C`, alias `DAT_0051C1F4`).

---

## 0. Findings that change the design

1. **The exe uses DirectDraw 7 directly, in three places**, not only through `_d3d.dll` **[V]**:
   (a) start-up DX7 probe `FUN_004811B0`;
   (b) the `LauncherBox` dialog (`FUN_00481230`): `DirectDrawEnumerateExA`, `DirectDrawCreateEx`, `GetDisplayMode`, `EnumDisplayModes`, `QueryInterface(IID_IDirect3D7)`, `IDirect3D7::EnumDevices`;
   (c) all 2D (BMP backgrounds, sprites, HUD, transitions, Bink video) on surfaces it creates from the `IDirectDraw7*` it receives from `afGetDDHandles`.
2. **GDI must work on DirectDraw surfaces** **[V]**. Every BMP file or resource is loaded with `LoadImageA` + `GetDC` + `StretchBlt(SRCCOPY)` (`FUN_0044AC60`/`FUN_0044AD60`). BMPs embedded in `.mad` files are written pixel by pixel after `Lock` (`FUN_0044DA70`, 8 and 16 bpp only). The colour-key value is found with `GetDC` + `SetPixel`/`GetPixel` on pixel (0,0) and then `Lock` (`FUN_0044B070`). Back each CPU surface with a 16-bit RGB565 DIB section that can be selected into a DC.
3. **Four exports must exist by name** **[V]**: `DirectDrawCreateEx`, `DirectDrawEnumerateExA` (exe IAT), `DirectDrawEnumerateA`, `DirectDrawCreate` (`_d3d.dll` resolves them with `GetProcAddress`; a NULL result is fatal). `DirectDrawCreate` is stored and never called.
4. **`Release` is called in `while (Release() != 0)` loops** (exe, 10+ places) **[V]**. It must return the true post-decrement count, never a constant non-zero.
5. **`Lock` is called with `DDSURFACEDESC.dwSize = 0x6C`** by `afOverwriteTexture` (0x1000F7AA, 0x1000F7BF) **[V]**. It must accept 0x6C and 0x7C and fill `lPitch` (+0x10), `lpSurface` (+0x24) and `ddpfPixelFormat` (+0x48) at the same offsets. The rest of that descriptor is not zeroed.
6. **A non-zero result from the primary's `Blt` or `Flip` makes `_d3d.dll` rebuild the whole device on the next frame** (`CopyToScreen`, tests at 0x100032BA and 0x100031BE) **[V]**. Always return `DD_OK` from those two, even when minimised.
7. **The back buffer is both a `Blt` destination and a `Blt` source** **[V]**. Menu transitions snapshot it into a 640x480 `OFFSCREENPLAIN|SYSTEMMEMORY` surface (`FUN_0047FCD0` 0x0047FEE5, `FUN_004820E0` 0x0048211A, `FUN_00483010` 0x0048304F, `FUN_004833B0` 0x0048346D, `FUN_00483980` 0x004839D1) and blit it back each frame. That needs a GPU read-back and a flush of pending D3D draws. It is never `Lock`ed or `GetDC`ed.
8. **DirectDraw blits happen inside `BeginScene`/`EndScene`** **[V,R]**. Front-end sprites (`FUN_00418470`, called from `FUN_00427380` @0x004275F3) and the in-game HUD are blitted between `Begin3D` and `End2D`: `FUN_0044E290` calls `FUN_00459550` @0x0044E5A7, `FUN_0045EE30` @0x0044E5B2 and `FUN_00459AC0` @0x0044E5BD, then `End3D` @0x0044E5C2, `End2D` @0x0044E5C8, `CopyToScreen` @0x0044E5D5 **[V]**. Background BMPs are blitted before `Begin2D`. Loading screens and Bink frames have no scene at all. All of them write the same back buffer in call order.
9. **Most results are unchecked** **[V]**. A failing `CreateSurface` for a BMP returns NULL and is dereferenced at once (`FUN_0044D9C0`, 0x0044DA25). Texture `CreateSurface` or `Lock` failure is fatal in the DLL (section 5).
10. **Formats** **[V]**. Report everything the exe can see as RGB565 (`F800/07E0/001F`, 16 bpp, no alpha). The only texture format needed is A1R5G5B5 (`7C00/03E0/001F/8000`). The back buffer must not report `DDPF_ALPHAPIXELS` (fatal `Terminate(1,2)`).
11. **Enumeration limits in the DLL**:
    - the mode record array holds exactly 100 records (`DDrawDevInfo` stride 0x9B8, records at +0x58, 0x18 bytes each **[R]**) and the callback 0x1000CE10 has no bounds check **[V]**;
    - the filtered per-driver mode array holds 50 records **[R]**;
    - the Z-format array (`0x1013EA00`) holds 24 entries and is never reset: one append per device creation (`FUN_10006240`), so enumerate **one** Z format. Enumerate 16-bit modes only, a handful of them.
12. **`SetCooperativeLevel` must change nothing** **[V]**. `Analyse` calls it with `0x51` (FULLSCREEN|EXCLUSIVE|ALLOWMODEX) on the real game window while enumerating, then with 8 (NORMAL). The game restores the window itself.
13. **Palettes** **[V]**. `CreatePalette(DDPCAPS_8BIT, 256 entries)` is called for every BMP and the result is attached with `SetPalette` to a surface that is not palettized. The palette is never queried and never released.
14. **Bink needs one method** **[V]**: `GetPixelFormat`. If `BinkDDSurfaceType` returns -1 or 6 (8-bit palettized), `FUN_0043EE10` releases the surface and skips the video without error. Handy for early bring-up.
15. **FPU control word** **[V,R]**. DX7 sets x87 to 24-bit precision at device creation. The exe checks the control word itself after each mode set (`FUN_004820A0`, called from 0x0044F936 and 0x0044E0EF) and forces `PC=24-bit, RC=nearest, all exceptions masked` if it differs. Do the same in `CreateDevice` (unless `DDSCL_FPUPRESERVE`, which the game never passes).
16. **Dead exports**. `afSetDDGlobals` (mangled C++ export, ordinal 2, takes two `IDirectDrawSurface7*`; no caller inside the DLL **[V]**) and `afDrawGroundTile2d` are never requested by the exe **[R]**.

---

## 1. DDRAW.dll imports, per module

### 1.1 Static imports (PE import tables, Ghidra `list_program_items` imports/external_locations, plus a PE parser) **[V]**

| Module | Imported name | IAT slot | Thunk and call sites |
|---|---|---|---|
| `warhogs_.exe` (identical in `warhogs_unwrapped.exe`) | `DirectDrawCreateEx` | 0x0054F3E0 | thunk 0x004AB010; one caller, `FUN_0044D370` @0x0044D3F7 (launcher) |
| `warhogs_.exe` | `DirectDrawEnumerateExA` | 0x0054F3E4 | thunk 0x004AB016; one caller, `FUN_0044D370` @0x0044D37E: `(cb=0x0044D120, ctx=screen, flags=7)` |
| `_d3d.dll` | `DirectDrawCreateEx` | 0x11ED7190 | thunk 0x100141A0; callers `FUN_10005590`, `FUN_10007030`, `FUN_1000C8D0` |
| `binkw32.dll` (both copies, MD5 `cca7bfed...`) | none | | KERNEL32, USER32, WINMM, GDI32 only |

`HOWMenu.exe` and `wh32LIB.DLL` import no DirectDraw. Every static import is by name, not by ordinal.

### 1.2 Run-time lookups **[V]**

| Module | Where | What |
|---|---|---|
| exe | `FUN_004811B0` (called from `FUN_0047DE90` @0x0047DEC1) | `LoadLibraryA("DDRAW.DLL")`, `GetProcAddress("DirectDrawCreateEx")`, `DirectDrawCreateEx(NULL, &dd, IID_IDirectDraw7, NULL)`, one `dd->Release()`, `FreeLibrary`. Failure shows an error box and exits. |
| `_d3d.dll` | `LoadDirectDraw` 0x1000B8D0 (called by `PowerUp`, `Analyse`, `FUN_10007030`) | `LoadLibraryA("DDRAW.DLL")`; `GetProcAddress("DirectDrawCreate")` (stored at `0x11B6A358`, **never read**); `GetProcAddress("DirectDrawEnumerateA")` (stored at `0x11B6A328`, called once at 0x1000C909 path in `FUN_1000C8D0`). Either NULL leads to `Terminate(0x43,0xA3,1)`. `UnloadDirectDraw` does `FreeLibrary`. |
| `binkw32.dll` | `FUN_10005F70` (<- `FUN_10005E30` <- `BinkBufferOpen`) | `LoadLibraryA("DDRAW.DLL")`, `GetProcAddress("DirectDrawCreate")`. Only in the `BinkBuffer*` API, which the exe does not import. Irrelevant. |

Exe imports from `binkw32.dll` (10): `BinkOpen`, `BinkDoFrame`, `BinkCopyToBuffer`, `BinkNextFrame`, `BinkOpenDirectSound`, `BinkSetSoundSystem`, `BinkDDSurfaceType`, `BinkGetError`, `BinkWait`, `BinkClose`. Only `BinkDDSurfaceType` touches DirectDraw. `BinkCopyToBuffer` writes to the pointer it is given and calls no COM method.

**Required exports (by name, undecorated, `stdcall`)**: `DirectDrawCreateEx(GUID*, void**, REFIID, IUnknown*)`, `DirectDrawEnumerateExA(cb, ctx, flags)`, `DirectDrawEnumerateA(cb, ctx)`, `DirectDrawCreate(GUID*, void**, IUnknown*)`. The imports and `GetProcAddress` strings carry no `@n` suffix **[V]**, and the callee pops the arguments (no stack clean-up after the exe's call at 0x0044D3F7), so build with a `.def` file that maps the plain names. Ship the DLL next to `warhogs_.exe`; `_d3d.dll`'s later `LoadLibraryA("DDRAW.DLL")` then returns the same module. The DLL is loaded with `LoadLibrary` several times and freed again, so it must survive refcounted unloads (the exe's static import keeps one reference).

---

## 2. Interfaces and methods actually called

### 2.0 Objects and conventions

- **Screen object** **[V]** (`new(0x3D8)`, constructor `FUN_0044CC70`, pointer in `DAT_0052083C` = `DAT_0051C1F4`): `+0x00 IDirectDraw7*`, `+0x04 primary*`, `+0x08 back*` (in the game's object these three are written only by `afGetDDHandles`, 0x100097D0, called from `FUN_0044DE10` after every mode set and at the top of `FUN_0045CBB0`; no decompiled consumer reads `+0x04` **[V, partial]**, and report A says none does **[R]**). The launcher also uses `+0x10 IDirect3D7*`, `+0x14/+0x18 DDSURFACEDESC2[]` (stride 0x7C) and count, `+0x1C/+0x20 D3D device list` (stride 0x30C), `+0x24/+0x28 DD device list` (stride 0x210: GUID at +0, description at +0x10, driver name at +0x110), `+0x58 DDSURFACEDESC2` (current display mode).
- **Pointer identity matters** **[V]**: `FUN_0044D560` chooses `BltFast` when `dest == screen->back`; `FUN_00418470` compares queued sources with `DAT_00511764`; `SetTexture` caching in the DLL (`DAT_11AB9490`) compares surface pointers. After each `SelectDriverMode` the exe re-reads `back` through `afGetDDHandles`. The `IDirectDraw7` object stays valid across mode switches (the DLL only releases it in `ReleaseDD`/`doexit`), and exe-owned surfaces stay valid.
- **Structure sizes used** **[V]**: `DDSURFACEDESC2` 0x7C (and `DDSURFACEDESC` 0x6C in `Lock`), `DDPIXELFORMAT` 0x20, `DDCAPS_DX7` 0x17C, `DDBLTFX` 100 (dwFillColor at +0x50), `DDCOLORKEY` 8, `DDSCAPS2` 0x10, `D3DVIEWPORT7` 0x18, `D3DTLVERTEX` 0x20 (`sx, sy, sz, rhw, diffuse, specular, tu, tv`).
- **Method counts** (distinct vtable methods actually called, incl. `QueryInterface`/`Release`): IDirectDraw7 11, IDirectDrawSurface7 18, IDirectDrawClipper 2, IDirectDrawPalette 0, IDirect3D7 4, IDirect3DDevice7 11, total 46. `AddRef` is never called but must work.
- **Vtable layout**: the DX7 layouts must be exact (all calls are `CALL [reg+offset]`, stdcall). Bink calls `GetPixelFormat` through the *`IDirectDrawSurface` (v1)* layout; that slot (0x54) is the same in `IDirectDrawSurface7`, so no separate v1 object is needed. No other interface is used (no gamma/colour control, no `IDirect3DTexture`, no vertex buffers). The exe also uses DirectSound, DirectInput and DirectPlay, which are out of scope.
- **Search method**: every `CALL [reg+disp]`, `CALL reg` (register-cached vtable slots such as `FUN_0044B070`'s `Lock`) and `JMP [reg+disp]` in `_d3d.dll` and in the exe's DirectDraw-touching functions was enumerated (297 + 665 register-indirect calls scanned; the 36 `JMP` hits in `_d3d.dll` are switch tables). Functions were classified by following where the receiver comes from (`screen`, results of the helpers, Bink surface). Calls on DirectSound/DirectInput/DirectPlay objects and game C++ virtuals were excluded. A second, independent linear sweep of all executable bytes of both images (including code Ghidra has not disassembled, such as the callbacks at 0x0044D200 and 0x1000CE10) found no further register-indirect call: `_d3d.dll` uses exactly the vtable offsets 0x00, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x20, 0x28, 0x2C, 0x30, 0x34, 0x48, 0x4C, 0x50, 0x54, 0x58, 0x60, 0x64, 0x6C, 0x70, 0x80, 0x8C, 0x94 (all accounted for in 2.1 to 2.6); the exe sweep added only four game virtuals (0x0048DCC8, 0x0048E401, 0x0048E50A, 0x0048E6F1, offsets +0x44/+0x4C).

### 2.1 IDirectDraw7

| Off | Method | Module(s) | Example call site | Arguments actually used | Path |
|---|---|---|---|---|---|
| 0x00 | QueryInterface | exe launcher, `_d3d` | exe 0x0044D442; `_d3d` 0x10006266 (`FUN_10006240`), 0x1000705F (`FUN_10007030`) | `riid` = IID_IDirect3D7 only (section 4.1). The launcher stores the result at screen +0x10 and never releases it, yet its `while (dd->Release() != 0)` loop must still terminate (separate per-interface counts, or one shared count that reaches 0). | init |
| 0x08 | Release | both | exe 0x0044D3BA/0x0044D3C6 (loop); `_d3d` 0x1000C893 | loop until it returns 0 | init/shutdown |
| 0x10 | CreateClipper | `_d3d` | 0x10005D26 | `(0, &clipper, NULL)`, windowed mode only | init |
| 0x14 | CreatePalette | exe | `FUN_0044AE80` 0x0044B055 | `(DDPCAPS_8BIT 0x04, 256 PALETTEENTRY {r,g,b,0}, &pal, NULL)`; entries come from the BMP colour table or a generated 3-3-2 table | asset load |
| 0x18 | CreateSurface | both | section 3 | always `(&ddsd, &surf, NULL)` | |
| 0x20 | EnumDisplayModes | exe launcher, `_d3d` | exe 0x0044D431; `_d3d` 0x1000CABB, 0x1000CAEE | `(0, NULL, ctx, cb)`: no flags, no filter. `_d3d` calls it twice per device (under cooperative level 0x51, then 8). | init |
| 0x2C | GetCaps | `_d3d` | 0x100057A6 (`FUN_10005590`) | `(&drvCaps, &helCaps)`, both with `dwSize = 0x17C`; only `drvCaps.dwCaps & DDCAPS_NOHARDWARE (0x02000000)` is read | init |
| 0x30 | GetDisplayMode | exe launcher | 0x0044D418 | `(&ddsd)` with `dwSize = 0x7C`, stored at launcher +0x58 | init |
| 0x4C | RestoreDisplayMode | `_d3d` | 0x100052DF (`PowerDown`), 0x100054B1 | `()` | mode switch, shutdown |
| 0x50 | SetCooperativeLevel | `_d3d` | 0x100057E7, 0x10005870, 0x100058BA, 0x1000CA5A, 0x1000CACE, 0x100054DD, 0x1000530C | `(hwnd, 8)` DDSCL_NORMAL, or `(hwnd, 0x51)` = FULLSCREEN\|EXCLUSIVE\|ALLOWMODEX. Never FPUSETUP/FPUPRESERVE/MULTITHREADED. | init |
| 0x54 | SetDisplayMode | `_d3d` | 0x1000589B (`FUN_10005820`) | `(w, h, bpp, 0, 0)` with `w, h, bpp` from the selected mode record (bpp is 16 because the exe asks `afGetModeNumber(drv,w,h,16)`), fullscreen only (refresh 0, flags 0) | fullscreen |

### 2.2 IDirectDrawSurface7

| Off | Method | Module(s) | Example call site | Arguments actually used | Path |
|---|---|---|---|---|---|
| 0x08 | Release | both | exe 0x0045CC80/0x0045CC8F, 0x0043F01A/0x0043F029 (Bink surface); `_d3d` 0x1000C83F, 0x1000E7FF | loop until 0 in the exe | |
| 0x0C | AddAttachedSurface | `_d3d` | 0x10003078 | `back->AddAttachedSurface(zbuf)` once per device creation | init |
| 0x14 | Blt | both | table 2.2a | | |
| 0x1C | BltFast | exe | `FUN_0044D560` 0x0044D597 | `(x, y, src, srcRect, 0x11 = DDBLTFAST_SRCCOLORKEY\|DDBLTFAST_WAIT)`, only when `dest == screen->back`; the caller pre-clips | frame |
| 0x2C | Flip | `_d3d` | 0x100031BB (`CopyToScreen`), 0x10005A18 | `(NULL, DDFLIP_WAIT 1)` on the primary, fullscreen only | fullscreen |
| 0x30 | GetAttachedSurface | `_d3d` | 0x100059B9 | `(&DDSCAPS2{dwCaps = DDSCAPS_BACKBUFFER 4}, &back)`, fullscreen only | init |
| 0x44 | GetDC | exe | `FUN_0044AD60` 0x0044AE01; `FUN_0044B070` 0x0044B09D, 0x0044B13C; `FUN_0044D6C0` 0x0044D6D3, 0x0044D6E6 | `(&HDC)`; the DC is used with `StretchBlt` onto it from a memory DC (SRCCOPY 0xCC0020 in `FUN_0044AD60`), `SetPixel(0,0,rgb)`, `GetPixel(0,0)`, and in `FUN_0044D6C0` a `StretchBlt` between two surface DCs (its caller `FUN_0042F410`, used once per sprite sheet by `FUN_00421AD0` @0x00421F5B, passes ROP 0; source and destination are exe off-screen sprite surfaces). Only on exe-created off-screen surfaces. | asset load |
| 0x48 | GetFlipStatus | `_d3d` | `Begin2D` 0x10006678, 0x1000668C | `(DDGFS_ISFLIPDONE 2)` on the primary; spins only while it returns `DDERR_WASSTILLDRAWING`. Return `DD_OK`. | frame |
| 0x54 | GetPixelFormat | binkw32 | 0x10006A55 (`BinkDDSurfaceType`) | `(&DDPIXELFORMAT)` with `dwSize = 0x20`. Reads `dwFourCC` (+8): 'UYVY'/'YV12'/'YUY2' -> types 8/9/7; `dwRGBBitCount` (+0xC): 8 -> 6, 24 -> 0, 32 -> 1; masks (+0x10,+0x14,+0x18): 565 -> 3, 555 -> 2. | video |
| 0x58 | GetSurfaceDesc | both | exe 0x0044D50D, 0x0044D796, 0x0044D82D, 0x0044D8DA, 0x0044D968, 0x0041AEAB; `_d3d` 0x10005417, 0x100055CD, 0x1000BAC2 | `(&ddsd)`, `dwSize = 0x7C`. Fields read: section 2.2b | |
| 0x60 | IsLost | both | exe 0x0044D4F2 (before every `FUN_0044D4D0` blit), 0x0045CC6A, 0x0045CCA8; `_d3d` 0x1000E898 | return `DD_OK` always | restore check |
| 0x64 | Lock | exe, `_d3d` | exe 0x0043F0BB (Bink), 0x0044B0E7, 0x0044DB25; `_d3d` 0x1000E45F, 0x1000746B, 0x1000A32D, 0x1000FFE3, 0x1000F7AA | `(NULL, &ddsd, flags, NULL)`: flags = `DDLOCK_WAIT 1` in `_d3d` and Bink, **0** in `FUN_0044B070`/`FUN_0044DA70`. `dwSize` 0x7C, except `afOverwriteTexture` 0x6C. Bink path: `BinkCopyToBuffer(bink, lpSurface, lPitch, bink->Height, x, y, surfaceType)` has no `BINKCOPYALL` flag, so only changed rectangles are written each frame and the surface memory must persist between locks. | |
| 0x68 | ReleaseDC | exe | 0x0044AE4A, 0x0044B0CB, 0x0044B15B, 0x0044D744, 0x0044D751 | `(hdc)` | asset load |
| 0x6C | Restore | both | exe 0x0044D4FA (only if `IsLost != 0`), 0x0043F0CD (after `DDERR_SURFACELOST`), 0x0044AD8B (unconditional, result ignored); `_d3d` 0x1000E8A4 | `()`; `DD_OK` no-op | **restore path** |
| 0x70 | SetClipper | `_d3d` | 0x100053E4 (NULL), 0x10005D68 (clipper) | windowed only | init, mode switch |
| 0x74 | SetColorKey | exe | `FUN_0044B170` 0x0044B198 | `(DDCKEY_SRCBLT 8, &DDCOLORKEY{low = high = pixel})` | asset load |
| 0x7C | SetPalette | exe | `FUN_0044D9C0` 0x0044DA25 | `(palette)` on a non-palettized surface; result ignored | asset load |
| 0x80 | Unlock | both | exe 0x0043F124, 0x0044B125, 0x0044DD1E; `_d3d` 0x1000750D, 0x1000E517 | `(NULL)` | |

**2.2a Blt flags actually used** **[V]**. Signature `(destRect, src, srcRect, flags, fx)`.

| Flags | Meaning | Where |
|---|---|---|
| `0x00000000` | copy or stretch | `_d3d` texture upload `tex->Blt(NULL, sys, NULL, 0, NULL)` (0x1000E53D, 0x1000E93C, 0x100075B4, 0x1000F8EE); exe `FUN_0044D4D0` callers such as `FUN_0047FCD0` 0x0047FEE5 (snapshot and restore), `FUN_0045CBB0` 0x0045CE24 |
| `0x00008000` KEYSRC | source colour key | exe HUD `FUN_00459550` 0x004597BC, 0x00459825, 0x00459A9C; `FUN_0045C700` 0x0045CACD, 0x0045CAF3 (its first blit, 0x0045CAA6, uses 0); `FUN_004845E0` 0x004849F0 |
| `0x01000000` WAIT | stretch/present | `_d3d` `CopyToScreen` windowed present 0x100032B7: `primary->Blt(&dst, back, &src={0,0,bbW,bbH}, WAIT, NULL)`; Bink frame 0x0043EFD1; loading screens 0x0045C6B7, 0x0045C6DE |
| `0x01008000` WAIT\|KEYSRC | colour-keyed sprite | `FUN_00418BC0` 0x00418BD8; `FUN_00418470` 0x004185B2/0x00418874; `FUN_0041B010` 0x0041B0C8, 0x0041B15B |
| `0x01008800` WAIT\|KEYSRC\|DDFX | mirrored sprite | `FUN_0044D5F0` (called by `FUN_00418470`): an inverted **destination** rect (right < left or bottom < top) is swapped and `fx.dwDDFX \|= 2` (MIRRORLEFTRIGHT) / `4` (MIRRORUPDOWN), `fx.dwSize = 100` |
| `0x00000400` COLORFILL | fill, `dwFillColor` = raw pixel (0) | `_d3d` 0x10005AF5, 0x10005B14 |
| `0x01000400` WAIT\|COLORFILL | fill | exe `FUN_0044D760` 0x0044D7ED (whole surface), `FUN_0044D800` 0x0044D8B1; `_d3d` 0x1000336E, 0x10005471, 0x10005632, 0x100056CB, 0x1000599C, 0x10005A09 |

Rect conventions of the exe helpers **[V]**:
- `FUN_0044D4D0(dest, src, x, y, flags)`: `if (dest->IsLost() != 0) dest->Restore();` then `Blt(dest, {x, y, x + srcW, y + srcH}, src, NULL, flags, NULL)`, with `srcW/srcH` from `src->GetSurfaceDesc`.
- `FUN_0044D560(dest, src, x, y, srcRect, flags)`: `BltFast(x, y, src, srcRect, 0x11)` if `dest == screen->back`, else `Blt(dest, {x, y, x + srcRectW, y + srcRectH}, src, srcRect, flags, NULL)`.
- `FUN_0044D5F0(dest, src, destRect, srcRect, flags, fx)`: rects passed through, so stretching happens whenever the sizes differ (loading screens stretch 640x480 to the game size, Bink stretches 320x240). Inverted `destRect` = mirror (see above). Use nearest-neighbour sampling.
- `FUN_0044D760(surf, color)`: full-surface `COLORFILL`; `FUN_0044D800` is the same with a colour built from 5/6/5 components (the three inputs are always 0).

Never used: DDBLT_ASYNC, DDBLT_KEYDEST, ROP, alpha, depth blits. `Blt` results are never checked. A KEYSRC blit from a surface with no colour key would fail on real DirectDraw (`DDERR_NOCOLORKEY`); every keyed source is loaded through `FUN_0044D9C0`/`FUN_0044DA70`/`FUN_0044D900(…,1)`, which set one.

**2.2b `DDSURFACEDESC2` fields consumed by the game** **[V]**

| Producer | Fields read |
|---|---|
| `EnumDisplayModes` callback, `_d3d` (0x1000CE10, 0x1000CE60) | `dwWidth` (+0xC), `dwHeight` (+8), `ddpfPixelFormat.dwRGBBitCount` (+0x54) |
| `EnumDisplayModes` callback, exe (0x0044D200) | copies all 0x7C bytes; `FUN_004815D0` reads +0xC, +8, +0x54 |
| `GetSurfaceDesc`, `_d3d` `FUN_1000BA90` (back buffer) | `ddpf.dwFlags` (+0x4C: bit 1 ALPHAPIXELS = fatal, 8 PALETTEINDEXED4, 0x20 PALETTEINDEXED8), `dwRGBBitCount` (+0x54), R/G/B masks (+0x58,+0x5C,+0x60) |
| `GetSurfaceDesc`, `_d3d` others | `ddsCaps.dwCaps` (+0x68, bit 0x4000 VIDEOMEMORY), `lPitch` (+0x10), `lpSurface` (+0x24), `dwRGBBitCount` (primary -> bytes per pixel) |
| `GetSurfaceDesc`, exe | `dwHeight` (+8), `dwWidth` (+0xC), `ddpf.dwRBitMask` (+0x58: `== 0x7C00` selects 555 else 565, in `FUN_0044D900` and `FUN_0044D800`) |
| `Lock`, exe and `_d3d` | `lpSurface` (+0x24), `lPitch` (+0x10); exe also `dwRGBBitCount` and `dwRBitMask` (`FUN_0044DA70` writes only 8 and 16 bpp, `FUN_0044B070` masks the key to the bit depth); `afOverwriteTexture` compares the R masks of source and target |

### 2.3 IDirectDrawClipper (windowed mode only)

| Off | Method | Module | Example | Arguments | Path |
|---|---|---|---|---|---|
| 0x08 | Release | `_d3d` | 0x1000C869 (`FUN_1000C830`) | | shutdown, mode switch |
| 0x20 | SetHWnd | `_d3d` | 0x10005D45 | `(0, hwnd)`, then `primary->SetClipper(clipper)` | init |

### 2.4 IDirectDrawPalette

No method is called. Only `CreatePalette` (exe `FUN_0044AE80`) and `SetPalette` (`FUN_0044D9C0`). Return a non-NULL object. The palette is never released.

### 2.5 IDirect3D7

| Off | Method | Module | Example | Arguments | Path |
|---|---|---|---|---|---|
| 0x08 | Release | `_d3d` | 0x1000636A (`FUN_10006340`, after the device), 0x100070AA | | shutdown, mode switch |
| 0x0C | EnumDevices | `_d3d`, exe launcher | `_d3d` 0x10007097 (`FUN_10007030`, called by `Analyse`); exe 0x0044D44F | `(callback, ctx)`; `_d3d` callback 0x10006850, exe callback `FUN_0044D270`. Both expect `D3DENUMRET`-style return 1. Fields read: section 4.3 | init |
| 0x10 | CreateDevice | `_d3d` | 0x1000629A (`FUN_10006240`) | `(IID_IDirect3DHALDevice, back, &dev)` always the HAL IID; `back` is the 3DDEVICE render target that already has the Z-buffer attached, and the same pointer `afGetDDHandles` hands to the exe | init, each mode set |
| 0x18 | EnumZBufferFormats | `_d3d` | 0x100030F7 (`FUN_100030C0`) | `(&deviceGUID from the EnumDevices record, cb 0x10003090, NULL)`. The callback appends the 32-byte `DDPIXELFORMAT` to `0x1013EA00[n++]` and returns 1. | init |

### 2.6 IDirect3DDevice7

| Off | Method | Example call site | Arguments actually used |
|---|---|---|---|
| 0x08 | Release | 0x10006351 (`FUN_10006340`) | at `SelectDriverMode`/`PowerDown`, before the surfaces are released |
| 0x10 | EnumTextureFormats | 0x10006337 | `(cb 0x10005F80, NULL)`, once per device creation; section 3.2 |
| 0x14 | BeginScene | `Begin2D` 0x10006641 | once per 3D frame |
| 0x18 | EndScene | `End2D` 0x10006809 | |
| 0x28 | Clear | `FUN_10002FB0` 0x10002FCA (<- `Begin2D` 0x100065DB, `DisplayCurrentHole` 0x10004F73) | `(0, NULL, D3DCLEAR_ZBUFFER 2, 0, 1.0f, 0)`. Colour is never cleared. Issued **before** `BeginScene` in `Begin2D` (first statement) and again inside the scene in `DisplayCurrentHole`. |
| 0x2C | SetTransform | `afSetFog` 0x10009730 | `(D3DTRANSFORMSTATE_PROJECTION 3, &m)` with m from `FUN_10009660(near 100.0f, far 500.0f, fov pi/4)`: `_11 = _22 = cos(fov/2)`, `_33 = Q = sin(fov/2)/(1-near/far)`, `_34 = sin(fov/2)`, `_43 = -Q*near`. No other transform is ever set. |
| 0x34 | SetViewport | `FUN_100075D0` 0x10007612 | `&{dwX, dwY, dwWidth, dwHeight, 0.0f, 1.0f}`: `(0,0,bbW,bbH)` from device creation and `Begin3D` |
| 0x50 | SetRenderState | 100+ sites | table 2.6a |
| 0x64 | DrawPrimitive | ~35 sites | table 2.6c |
| 0x8C | SetTexture | ~26 sites | `(0, pSurface)` or `(0, NULL)`; surfaces are the `tex` surfaces of section 3 (TEXTURE caps, A1R5G5B5, 256x256) |
| 0x94 | SetTextureStageState | ~30 sites | table 2.6b |

Never called on the device: `GetCaps`, `SetRenderTarget`, `GetViewport`, `Set/GetMaterial`, `SetLight`, `ValidateDevice`, state blocks, `Load`, `PreLoad`, vertex buffers.

**2.6a Render states** (ID, name, values). All results ignored. **[V]**

| ID | Name | Values used | First site |
|---|---|---|---|
| 2 | ANTIALIAS | 0 | 0x10006431 |
| 4 | TEXTUREPERSPECTIVE | 1 | 0x10006413 |
| 7 | ZENABLE | 1 | 0x100063AA |
| 8 | FILLMODE | 3 SOLID | 0x1000644F |
| 9 | SHADEMODE | 1 FLAT, 2 GOURAUD | 0x1000649D, 0x1000349D |
| 14 | ZWRITEENABLE | 1 | 0x1000638C |
| 15 | ALPHATESTENABLE | 1 | 0x100063B9, 0x10006547 |
| 19 | SRCBLEND | 5 SRCALPHA (default), 2 ONE (additive), 1 ZERO (multiply, dead path **[R]**; restored to 5) | 0x100063E6, 0x10003FB3, 0x1000452C, 0x10004564 |
| 20 | DESTBLEND | 6 INVSRCALPHA, 2 ONE, 3 SRCCOLOR (restored to 6) | 0x100063F5, 0x10003FC2, 0x1000453B, 0x10004573 |
| 22 | CULLMODE | 3 CCW | 0x10006404 |
| 23 | ZFUNC | 4 LESSEQUAL | 0x1000639B |
| 24 | ALPHAREF | 0, then 8 | 0x100063D7, 0x10006556 |
| 25 | ALPHAFUNC | 5 GREATER, then 7 GREATEREQUAL | 0x100063C8, 0x10006565 |
| 26 | DITHERENABLE | 1; 0 or 1 from `afSetWeatherValues` | 0x10006440, 0x1000FC32 |
| 27 | ALPHABLENDENABLE | 1; 0 for the opaque preset | 0x100064AC, 0x10004797 |
| 28 | FOGENABLE | 0 or 1 (`afSetFog`, sky objects force 0 then 1, `afSetWeatherValues`) | 0x1000974B, 0x1000D4D4, 0x1000D7AD, 0x1000FC09 |
| 29 | SPECULARENABLE | 0 | 0x10006422 |
| 33 | STIPPLEDALPHA | 0, or 1 if the device only has stippled alpha | 0x1000646D, 0x1000658C |
| 34 | FOGCOLOR | D3DCOLOR | 0x10009760, 0x1000FC74 |
| 35 | FOGTABLEMODE | 3 LINEAR | 0x1000976F |
| 36 | FOGTABLESTART | float bits | 0x10009784, 0x10003451 (sky fade) |
| 37 | FOGTABLEEND | float bits | 0x10009799, 0x10003463 |
| 39 | STIPPLEENABLE | 0, or 1 as above | 0x1000645E, 0x1000657D |
| 41 | COLORKEYENABLE | 0 | 0x1000647C |
| 137 | LIGHTING | 0 | 0x1000648E |

Device defaults are set by `FUN_10006380` (from `FUN_10006240` and `afIfaceInit`). Fog: `FOGTABLEMODE = LINEAR` with the projection set only so the driver does W-based table fog on TL vertices **[U]**: W-based vs Z-based on real hardware is unconfirmed (reports C and D).

**2.6b Texture stage states** (stage 0 in every case) **[V]**

| Type | Value(s) used | Site |
|---|---|---|
| 4 ALPHAOP | 4 MODULATE (default and most presets), 2 SELECTARG1 (opaque/untextured preset), 12 BLENDDIFFUSEALPHA (additive preset) | 0x100064BD, 0x100047A8, 0x10003FF1, 0x1000EB7E |
| 2 COLORARG1 | 2 TEXTURE | 0x100064D1 |
| 3 COLORARG2 | 0 DIFFUSE | 0x100064E5 |
| 5 ALPHAARG1 | 2 TEXTURE | 0x100064F9 |
| 6 ALPHAARG2 | 0 DIFFUSE | 0x1000650D |
| 16 MAGFILTER | 2 LINEAR | 0x10006521 |
| 17 MINFILTER | 2 LINEAR | 0x10006535 |

Never set: COLOROP (stays at the stage-0 default MODULATE), TEXCOORDINDEX, address modes (default WRAP), MIPFILTER (none), border colour, stages above 0. A few sites pass a register as stage; it is 0.
**[U]** What a NULL texture does under D3D7 (`SetTexture(0, NULL)` for untextured fans and quads, with COLOROP MODULATE and ALPHAARG1 = TEXTURE): output is assumed to be the diffuse colour and alpha.

**2.6c DrawPrimitive** (always `dwFlags = 0`, FVF `0x1C4` = XYZRHW\|DIFFUSE\|SPECULAR\|TEX1, vertex stride 32) **[V]**

| Primitive | Vertex count | Example sites |
|---|---|---|
| TRIANGLELIST (4) | multiple of 3: batch set A up to 25,500 vertices per call (`0x1013ED08 + slot*0xC7380`), set B up to 512 (`0x11A26520 + slot*0x4000`), 6 for quads, text up to 6 x 128 | ground flush 0x10003BC5; objects 0x1000D66B (set A), 0x1000D6BC (set B), 0x1000D713, 0x1000D77C; text 0x1000E1E7; 2D poly 0x1000F488; radar 0x10009E73, 0x1000A21E |
| TRIANGLEFAN (6) | 4 (placed polys: n <= 8) | backdrop/sky 0x1000B806, 0x1000B8BE; water/placed polys 0x10003E22, 0x10004069, 0x100042D6, 0x10004556, 0x100046B3; prebuilt 2D 0x1000EB08, 0x1000EBEF, 0x1000ECA9; 0x100101F9 |
| LINELIST (2) | 2 | 0x100047F5, 0x1000F711 (reachable only through exports the exe never calls **[R]**) |

Vertex semantics **[R]** (report D sections 2.3, 2.4, 8.1): `sx, sy` are pixels relative to the viewport origin, `sz` is linear depth in [0,1] (`z*2e-6`, floor 1.527e-5, for 3D; 1.526e-4 or 0.0 for screen-space 2D; 0.9 for sky objects; 0.99999 for the backdrop), `rhw` is `8/z` for 3D, 15.0 for screen-space 2D and 0.2 for the backdrop, `diffuse` is ARGB (alpha drives blending, no clamping), `specular` is 0xFFFFFFFF and ignored (SPECULARENABLE 0), `tu,tv` are page-normalised texel centres. Triangles are clockwise on screen with `CULLMODE = CCW`. There is no near-plane clipping in the DLL, so very large and partly behind-the-camera triangles reach the driver (D3D7 guard-band clipped them). FLAT presets always use one colour per primitive, so the provoking-vertex rule does not matter. All device methods are on the normal path; nothing in section 2.6 is restore-only except `Release`.

---

## 3. CreateSurface requests

All requests set `dwSize = 0x7C`, are issued as `(&ddsd, &surf, NULL)` and use no `DDSCAPS2`/mip/`TEXTUREMANAGE` flags. `dwFlags` bits: CAPS 1, HEIGHT 2, WIDTH 4, BACKBUFFERCOUNT 0x20, PIXELFORMAT 0x1000. `ddsCaps.dwCaps` bits: PRIMARYSURFACE 0x200, OFFSCREENPLAIN 0x40, 3DDEVICE 0x2000, VIDEOMEMORY 0x4000, SYSTEMMEMORY 0x800, TEXTURE 0x1000, ZBUFFER 0x20000, FLIP 0x10, COMPLEX 8, ALLOCONLOAD 0x4000000.

### 3.1 Table **[V]**

| # | Purpose | Module and site | dwFlags | Size | Pixel format | Caps |
|---|---|---|---|---|---|---|
| 1 | Primary, fullscreen flip chain | `_d3d` 0x1000592F | 0x21 (CAPS\|BACKBUFFERCOUNT), `dwBackBufferCount = 1` | display mode | display mode (16 bpp) | 0x2218 (PRIMARY\|FLIP\|COMPLEX\|3DDEVICE), plus SYSTEMMEMORY 0x800 if the mode record flag is set |
| 2 | Primary, windowed | `_d3d` 0x10005C04 | 1 | - | desktop | 0x6200 (PRIMARY\|3DDEVICE\|VIDEOMEMORY). Failure sets the lost flag and returns success. |
| 3 | Back buffer, windowed | `_d3d` 0x10005C64 | 7 | client width x height (`DAT_1002C4F0/4F4`) | **none requested** (desktop format) | 0x2040 (OFFSCREENPLAIN\|3DDEVICE) |
| 4 | Back buffer, fullscreen | `GetAttachedSurface(BACKBUFFER)` 0x100059B9 | - | - | - | - |
| 5 | Z-buffer | `FUN_10002FD0` 0x10003066 | 0x1007 | back-buffer size (`DAT_1002C500/504`) | the enumerated Z format whose `dwZBufferBitDepth` (+0xC) is 16, else 24, else 32 | 0x24000 (ZBUFFER\|VIDEOMEMORY); then `back->AddAttachedSurface` |
| 6 | TIM page, master copy | `FUN_1000E370` 0x1000E3E1 | 0x1007 | 256x256 | the chosen texture format (`0x1013E420`) | 0x1800 (TEXTURE\|SYSTEMMEMORY) |
| 7 | TIM page, bound texture | `FUN_1000E370` 0x1000E44C | 0x1007 | 256x256 | same | 0x5000 (TEXTURE\|VIDEOMEMORY); 0x1800 if the driver record bit 0 were clear (never) |
| 8 | TIM page or ground page re-creation | `afRestoreTextureSurfaces` 0x1000E912, 0x1000E9EB | 0x1007 | 256x256 | same | 0x4005000 (TEXTURE\|VIDEOMEMORY\|ALLOCONLOAD); result unchecked |
| 9 | Ground tile page, master | `FUN_10007210` 0x10007430 | 0x1007 | 256x256 | same | 0x1800 |
| 10 | Ground page, bound texture | `afUploadGroundTextures` 0x100075A0 | 0x1007 | 256x256 | same | 0x5000 |
| 11 | Radar source | `afInitScanner` 0x1000A2FE | 0x1007 | 64x64 | same | 0x1800 |
| 12 | BMP file or resource (ddutil `DDLoadBitmap`) | exe `FUN_0044AC60` 0x0044AD1B | 7 | bitmap size | none (display format) | 0x40 (OFFSCREENPLAIN) |
| 13 | Generic off-screen (Bink, text strip, transitions) | exe `FUN_0044D900` 0x0044D959 | 7 | `(w, h)` args: 320x240 (Bink, 0x0043EEC6), 1024x23 (`FUN_0045E530`, with colour key), 640x480 (0x00483035, 0x004833C9, 0x0048399C, 0x004839B7), and others | none | 0x840 (OFFSCREENPLAIN\|SYSTEMMEMORY). Afterwards `GetSurfaceDesc`, colour fill with 0x7C1F (555) or 0xF81F (565), optional key 0xFF00FF. |
| 14 | Sprite from an in-memory BMP | exe `FUN_0044DA70` 0x0044DAE3 | 7 | BMP header (`biWidth` at +0x12, `biHeight` at +0x16) | none | 0x840. About 260 sites via `FUN_0042F4A0` <- `FUN_00421AD0` **[R]**; 69 via `FUN_00457690` (menutims.mad, **[V]**). |
| 15 | Sprite clone | exe `FUN_0044DD60` 0x0044DDCB | 7 | source surface size | none | 0x40; key 0xFF00FF |
| 16 | 1x1 solid-colour source | exe `FUN_00418400` 0x00418462 | 7 | 1x1 (`DAT_00511764`) | none | 0x840; refilled with `FUN_0044D800` and stretched |

"None" means the exe depends on getting the primary's format (RGB565 in this design). Surfaces 6 to 11 use the texture format; the code only works with A1R5G5B5 (section 3.2).

### 3.2 Format selection **[V]**

- **Textures** (`EnumTextureFormats` callback 0x10005F80): rejects any format with `DDPF_PALETTEINDEXED1/2/4/8/TO8`; needs `DDPF_RGB` and non-zero R, G, B masks; formats without `DDPF_ALPHAPIXELS` are ignored. Rank (best seen wins, initial -1): **A1R5G5B5 = 3**, then A4R4G4B4 (A, G, B bit counts of 4) = 2, any other 16-bit alpha format = 1, any 32-bit alpha format = 0. It copies the winner to `0x1013E420` and sets `DAT_11AB948C = 1`; always returns 1. The surface-writing code is only correct for A1R5G5B5. Enumerate exactly that format: `DDPIXELFORMAT{dwSize 32, dwFlags 0x41 (DDPF_RGB\|DDPF_ALPHAPIXELS), dwFourCC 0, dwRGBBitCount 16, R 0x7C00, G 0x03E0, B 0x001F, A 0x8000}`.
- **Z-buffer** (`EnumZBufferFormats` callback 0x10003090): no filtering, append only. `FUN_10002FD0` matches `dwZBufferBitDepth` (+0xC) == 16, then 24, then 32. Enumerate one 16-bit format: `DDPIXELFORMAT{dwSize 32, dwFlags 0x400 (DDPF_ZBUFFER), dwZBufferBitDepth 16, dwStencilBitDepth 0, dwZBitMask 0xFFFF, dwStencilBitMask 0}`. The whole 32-byte record is copied into the Z surface's `ddsd.ddpfPixelFormat`.
- **Back buffer** (`FUN_1000BA90` after `GetSurfaceDesc`): fatal if `DDPF_ALPHAPIXELS` is set; otherwise reads RGB masks and bit count (RGB565 expected).

---

## 4. IIDs, device caps and enumeration

### 4.1 GUIDs **[V]** (bytes read in both images)

| Use | GUID | Where |
|---|---|---|
| `IID_IDirectDraw7` | {15E65EC0-3B9C-11D2-B92F-00609797EA5B} | `DirectDrawCreateEx` 3rd arg: `_d3d` 0x10020338; exe 0x004BCBB8 (launcher `FUN_0044D370` and the DX7 probe `FUN_004811B0`) |
| `IID_IDirect3D7` (`dd->QueryInterface`) | {F5049E77-4861-11D2-A407-00A0C90629A8} | `_d3d` 0x10020328, exe 0x004BC9D8 |
| `IID_IDirect3DHALDevice` (`CreateDevice`) | {84E63DE0-46AA-11CF-816F-0000C020156E} | `_d3d` 0x10020318. T&L HAL is never requested. |

No other IID is passed anywhere. Surfaces, palettes and the device are never queried. `DirectDrawCreateEx`'s `lpGUID` is NULL or a GUID taken from `DirectDrawEnumerate*`; `DDCREATE_*` constants are not used.

### 4.2 Device description **[V]**

`DirectDrawEnumerateA` (`_d3d`, callback 0x1000CD80, 4 args): copies the description string (max 0x40 bytes) and the GUID (`lpGUID == NULL` marks the primary display); returns TRUE. `DirectDrawEnumerateExA` (exe, callback `FUN_0044D120`, 5 args incl. `hMonitor`): copies GUID if non-NULL, then two NUL-terminated strings **without a length check** into 0x100-byte fields; returns TRUE. Report one primary device with short strings.

### 4.3 `IDirect3D7::EnumDevices` callback (`_d3d` 0x10006850) **[V]**

Reads only these `D3DDEVICEDESC7` fields (offsets from the structure start):

| Offset | Field | Value to report and effect |
|---|---|---|
| 0x00 | `dwDevCaps` | **Required**: `D3DDEVCAPS_HWRASTERIZATION` 0x80000, else the device is dropped (entry flag 0x001) |
| 0x74 | `dwDeviceRenderBitDepth` | **Required**: `DDBD_16` 0x400 (entry flag 0x10), so 16-bit modes survive. Windowed 3D is enabled only if the bit matching the primary's bytes per pixel is set (`DDBD_8` 0x800 / `DDBD_16` 0x400 / `DDBD_24` 0x200 / `DDBD_32` 0x100 -> entry flags 0x08 / 0x10 / 0x20 / 0x40) |
| 0x5C | `dpcTriCaps.dwTextureCaps` | **Required**: `D3DPTEXTURECAPS_PERSPECTIVE` 0x1, else the device is dropped. When set (entry flag 0x2) `SelectDriverMode` insists on an alpha texture format |
| 0x58 | `dpcTriCaps.dwShadeCaps` | Report `D3DPSHADECAPS_ALPHAFLATBLEND` 0x1000 (entry flag 0x100). With only `ALPHAFLATSTIPPLED` 0x2000 the DLL switches on STIPPLEENABLE/STIPPLEDALPHA; with neither, the entry is penalised **[R]** |
| 0x40 | `dpcTriCaps.dwMiscCaps` | Report `D3DPMISCCAPS_CULLNONE` 0x10 (entry flag 0x80); no visible effect found |
| 0x60 | `dpcTriCaps.dwTextureFilterCaps` | Report `D3DPTFILTERCAPS_LINEAR` 0x2; only feeds the golf-flag block and the entry score **[R]** |
| 0xC4 | `deviceGUID` (16 bytes) | stored in the driver record and later passed to `EnumZBufferFormats` (any GUID works) |

The exe's launcher callback `FUN_0044D270` copies the two strings (unbounded) and `deviceGUID` and `dwDevCaps`; nothing is displayed from this list.

### 4.4 Display-mode enumeration **[V]**

- **`_d3d` `Analyse`** (`FUN_1000C8D0`): per device, `DirectDrawCreateEx`, `SetCooperativeLevel(hwnd, 0x51)` (only if `hwnd != NULL`), `EnumDisplayModes(0, NULL, rec, 0x1000CE10)` recording `{w, h, bpp, flag = 1}` for every mode, `SetCooperativeLevel(hwnd, 8)`, `EnumDisplayModes(0, NULL, rec, 0x1000CE60)` clearing the flag on modes still listed. List the **same** modes both times, otherwise modes keep flag 1 and the fullscreen primary asks for `DDSCAPS_SYSTEMMEMORY`. Failure for the primary device is fatal (section 5). The later filter in the `EnumDevices` callback keeps a mode only if the device's render-depth flag for its bpp is set and the size is <= 1024x768 (**[V]**: jump table at 0x10006BAC, compares against 0x400/0x300 unless the never-written global `0x1013E6A8` is set); modes that follow a 16-bit mode of the same size are dropped **[R]**. So only 16-bit modes survive, and `afGetModeNumber(drv, w, h, 16)` must find the game's size or the exe falls back to `SelectDriverMode(-1)` (windowed).
- **Launcher** (`FUN_004815D0`): lists a mode only if `640 <= dwWidth <= 1024`, `480 <= dwHeight <= 768` and `ddpf.dwRGBBitCount == 16`, formatted `"%dx%d %dbpp"`, in the order delivered; default pick is the entry matching the stored choice (`launch.bin`: `d<drv>x<w>y<h>l<lang>`), else the first. If none qualifies, the stored size stays 0 and the game later divides by zero at 0x0044E1C5 **[R]**. Provide at least 640x480x16, 800x600x16 and 1024x768x16.
- The launcher callback (0x0044D200, `ret 8`) reallocates and copies the full 0x7C-byte record; it returns 1.
- Recommended record content (as real DX7 returns): `dwSize` 0x7C, `dwFlags` = HEIGHT\|WIDTH\|PITCH\|PIXELFORMAT\|REFRESHRATE, `dwHeight`, `dwWidth`, `lPitch`, `dwRefreshRate` (60), `ddpfPixelFormat` = {32, `DDPF_RGB` 0x40, 0, 16, 0xF800, 0x07E0, 0x001F, 0}. Only the three fields in 2.2b are read.

---

## 5. Return-value expectations

HRESULT values: `DD_OK` 0; `DDERR_SURFACELOST` 0x887601C2; `DDERR_SURFACEBUSY` 0x887601AE; `DDERR_WASSTILLDRAWING` 0x8876021C. `Terminate(module, code, hr)` (`_d3d.dll` 0x1000C5D0) shows a message box, appends to `verbose.log` and exits the process **[R]**.

| Call | What the code checks | Reaction |
|---|---|---|
| `IDirect3DDevice7::BeginScene` (`Begin2D` 0x10006644-0x10006666) **[V]** | `== SURFACELOST` or `== SURFACEBUSY` | sets the lost flag, returns 0; any other non-zero -> `Terminate(0x54,0xCC)` |
| `IDirect3DDevice7::EndScene` (`End2D` 0x1000680C-0x1000682B) **[V]** | same pair | lost flag; other error -> `Terminate(0x55,0xCD)` |
| primary `GetFlipStatus` (`Begin2D` 0x1000667B-0x10006694) **[V]** | `== WASSTILLDRAWING` | spins; any other value (including errors) proceeds |
| primary `Flip` / `Blt` (`CopyToScreen` tests at 0x100031BE, 0x100032BA) **[V]** | any non-zero | lost flag; next `CopyToScreen`/`Begin2D` runs `SelectDriverMode(savedHwnd, savedMode)` and `afRestoreTextureSurfaces` (full rebuild) |
| `afRestoreTextureSurfaces` **[V]** | `IsLost != 0`, `Restore != 0` | on restore failure: `Release`, `CreateSurface(caps 0x4005000)` unchecked, then `tex->Blt(NULL, sys, ...)` unchecked (NULL dereference if creation failed). Every page is re-blitted even if nothing was lost. |
| texture `CreateSurface` / `Lock` (sys copy, ground, scanner) **[V]** | any non-zero | `Terminate(0x57,0xCF,hr)` / `Terminate(0x57,0x32,hr)`; the `tex` surface and its `Blt` are unchecked |
| `DirectDrawCreateEx` in `FUN_10005590` **[V]** | non-zero | `Terminate(3,5)` (GUID) or `Terminate(3,8)` (NULL) |
| `GetCaps` (0x100057A6) **[V]** | non-zero result; then `drvCaps.dwCaps & DDCAPS_NOHARDWARE` | `Terminate(0x4F,0x2D)`; NOHARDWARE makes `FUN_10005590` return 0 and `SelectDriverMode` fail |
| `SetCooperativeLevel(NORMAL)` **[V]** | non-zero | `Terminate(3,4)`; fullscreen `0x51` failure `Terminate(6,0x16)` |
| `SetDisplayMode`, fullscreen primary `CreateSurface` **[V]** | non-zero | `SelectDriverMode` returns failure; the exe then calls `FUN_0044B1A0(0x50,0xC4)` |
| `GetAttachedSurface`, `CreateClipper`, `SetHWnd`, `SetClipper`, `GetSurfaceDesc` in `FUN_10005820` **[V]** | non-zero | `Terminate(6, code)`: `GetAttachedSurface` 0x18 (0x100059C5), `GetSurfaceDesc` 0x1A (0x10005A49) / 0x1E (0x10005CBB) / 0x22 (0x10005DC2), `CreateClipper` 0x1F (0x10005D32), `SetClipper` 0x21 (0x10005D74); `SetHWnd` failure also terminates (0x10005D51, code not decoded) |
| `QueryInterface(IID_IDirect3D7)`, `CreateDevice` **[V]** | non-zero | `Terminate(0x53,199)`, `Terminate(0x53,200)` |
| `GetSurfaceDesc` + alpha test in `FUN_1000BA90` **[V]** | non-zero / `DDPF_ALPHAPIXELS` | `Terminate(1,1)` / `Terminate(1,2)` |
| `Analyse` `DirectDrawCreateEx` / `EnumDisplayModes` / `SetCooperativeLevel(0x51)` **[V]** | non-zero | fatal for the primary device (`Terminate(0x4B,0xB5)`, `(0x4B,0xB6)`, `(0xC,0x16)`); a secondary device just ends the loop |
| `SetRenderState`, `SetTexture`, `SetTextureStageState`, `DrawPrimitive`, `Clear`, `SetViewport`, `SetTransform`, `EnumTextureFormats`, `EnumZBufferFormats` **[V]** | results ignored | a missing Z format or alpha texture format breaks rendering silently or disables 3D (section 3.2) |
| exe `Lock` in Bink frame (`FUN_0043F080`) **[V]** | `== SURFACELOST` | `Restore()`; if that succeeds, retry `Lock` in a loop; if it fails, skip the frame. Other errors are not checked (garbage pointer passed to Bink). |
| exe `Lock` in `FUN_0044B070` **[V]** | `== WASSTILLDRAWING` | retry loop; non-zero otherwise -> key stays `0xFFFFFFFF` |
| exe `IsLost` in `FUN_0044D4D0` **[V]** | `!= 0` | `Restore()` then `Blt` |
| exe `IsLost` in `FUN_0045CBB0` **[V]** | `== SURFACELOST` | release the BMP surface (loop) and reload it |
| exe `CreateSurface` in `FUN_0044AC60`, `FUN_0044DA70` **[V]** | non-zero | returns NULL (and `FUN_0044D9C0` dereferences it) |
| exe `CreateSurface` in `FUN_0044D900`, `FUN_0044DD60`, `FUN_00418400` **[V]** | not checked | the returned pointer is used at once (`GetSurfaceDesc`, `Blt`) |
| exe `GetDC` in `FUN_0044AD60`, `FUN_0044B070` **[V]** | `== 0` | GDI copy or pixel access runs; otherwise it is skipped |
| exe `Blt`, `BltFast`, `SetColorKey`, `SetPalette`, `GetSurfaceDesc`, `Unlock` **[V]** | results ignored | |
| exe launcher `DirectDrawCreateEx` (`FUN_0044D370`) **[V]** | `dd == NULL` | returns false (launcher continues with empty lists) |
| exe launcher `QueryInterface(IID_IDirect3D7)` (0x0044D442) **[V]** | not checked | the result is called at once (`EnumDevices`, 0x0044D44F); a NULL pointer crashes, so this QI must succeed |
| `GetPixelFormat` inside `BinkDDSurfaceType` (bink 0x10006A55) **[V]** | not checked (the 0x20-byte struct is pre-zeroed) | a failure leaves zeros -> type -1 -> `FUN_0043EE10` skips the video |

Recommended behaviour: never return `DDERR_SURFACELOST`, `SURFACEBUSY` or `WASSTILLDRAWING` from anything above; `IsLost` always `DD_OK`; `Restore` a no-op `DD_OK`.

---

## 6. Minimum implementation checklist

Order of first use at run time: DLL loads -> start-up DX7 probe and `LauncherBox` (exe) -> `Analyse`/`PowerUp`/`SelectDriverMode(hwnd,-1)`/`afGetDDHandles` (`_d3d.dll`) -> Bink intros -> loading screen -> front-end menu (BMP backgrounds, colour-keyed sprites, 3D hog model, 2D polys, text) -> a round (mode switch to game size, terrain, fog, HUD). **Menu** items are needed to reach the main menu, in the order listed; **Game** items are only needed inside a round.

### 6.1 Module level and cross-cutting
- **Menu**
  - Exports by name: `DirectDrawCreateEx`, `DirectDrawEnumerateExA`, `DirectDrawEnumerateA`, `DirectDrawCreate` (stub is enough). Refcounted load/unload (`LoadLibrary`/`FreeLibrary` pairs from `_d3d.dll`).
  - Exact DX7 vtable layouts; `AddRef`/`Release` with true post-decrement counts; stable object pointers (identity is compared by the game).
  - Everything the exe sees is RGB565; `DDSURFACEDESC2` is 0x7C, `DDSURFACEDESC` 0x6C accepted by `Lock`.
  - Presentation: primary `Blt` or `Flip` always `DD_OK`; show the back buffer stretched over the window client area (ignore the `dst` offset the DLL computes); add a frame limiter (the original menu is unthrottled, ~1460 FPS, report `WORKLOG.md`).
  - GDI: surfaces that the exe `GetDC`s are DIB-section backed.
- **Game**
  - x87 control word set to 24-bit precision in `CreateDevice` (the exe re-asserts it after each mode set, `FUN_004820A0`).
  - Optional fullscreen mode (section 2 rows marked "fullscreen").

### 6.2 IDirectDraw7 (11 methods)
- **Menu**: `QueryInterface(IID_IDirect3D7)`; `Release` loops; `GetDisplayMode` and `EnumDisplayModes` (>= 640x480, 800x600, 1024x768 at 16 bpp, complete `DDSURFACEDESC2`, <= ~20 modes, identical list in both `Analyse` enumerations); `GetCaps` (`dwSize` 0x17C both, no `NOHARDWARE`); `SetCooperativeLevel` accepting 8 and 0x51 as no-ops; `CreateClipper`; `CreatePalette` (any non-NULL object); `CreateSurface` for every row 1 to 3, 5 to 7 and 12 to 16 of section 3.1 (windowed primary 0x6200, windowed back buffer 0x2040, Z 0x24000, TEXTURE 0x1800/0x5000, OFFSCREENPLAIN 0x40/0x840).
- **Game**: `CreateSurface` rows 9 to 11 (ground pages, radar) and the second back buffer after a mode switch (`dd` stays valid, surfaces of the exe stay valid); optional `SetDisplayMode`, `RestoreDisplayMode`, fullscreen primary (row 1).

### 6.3 IDirectDrawSurface7 (18 methods)
- **Menu**
  - `Release`, `GetSurfaceDesc` (fields of 2.2b), `IsLost`/`Restore` (`DD_OK`), `GetFlipStatus` (`DD_OK`).
  - `Lock`/`Unlock` (`NULL, &ddsd, 0 or 1, NULL`; fill `lpSurface`, `lPitch`, `ddpfPixelFormat`; CPU-visible memory for sprites, Bink surface, texture masters).
  - `GetDC`/`ReleaseDC` with a GDI-compatible DC (`StretchBlt`, `SetPixel`, `GetPixel`).
  - `SetColorKey(DDCKEY_SRCBLT)` with an exact 16-bit compare; `SetPalette` (accept, ignore).
  - `Blt`: every flag combination of 2.2a (0, KEYSRC, WAIT, WAIT\|KEYSRC, DDFX mirror via inverted dest rect, COLORFILL with and without WAIT), stretch, NULL source and NULL source rect; destination = back buffer or CPU surface; **source = back buffer** (flush D3D, read back); texture upload `tex->Blt(NULL, sys, NULL, 0, NULL)`; present Blt on the primary.
  - `BltFast(x, y, src, rect, 0x11)` onto the back buffer.
  - `GetPixelFormat` (Bink): RGB565 -> type 3.
  - `AddAttachedSurface` (Z), `SetClipper` (accept, remember the window).
- **Game**
  - Cheap, bit-exact `Lock`/`Unlock` on the ground-page masters (thousands of calls per frame through `afIsPointWatery`).
  - `Lock` with `dwSize` 0x6C (`afOverwriteTexture`), with the R mask reported correctly so the 565 -> 1555 conversion triggers.
  - `afRestoreTextureSurfaces` pass: `IsLost` -> `DD_OK` for every page, then `tex->Blt(NULL, sys, ...)` for every page.
  - Back-buffer / primary objects re-created by a second `SelectDriverMode` (new pointers).
  - Optional fullscreen: `Flip(NULL, 1)`, `GetAttachedSurface(BACKBUFFER)`.

### 6.4 IDirectDrawClipper (2 methods) and IDirectDrawPalette (0 methods)
- **Menu**: `SetHWnd(0, hwnd)`, `Release`; palette is an inert object returned by `CreatePalette`.
- **Game**: nothing more.

### 6.5 IDirect3D7 (4 methods)
- **Menu**: `EnumDevices` (one HAL device with the 4.3 values; both callbacks, return 1 from each); `CreateDevice(IID_IDirect3DHALDevice, back, &dev)`; `EnumZBufferFormats` (one 16-bit format); `Release`.
- **Game**: nothing more.

### 6.6 IDirect3DDevice7 (11 methods)
- **Menu**
  - `EnumTextureFormats` (A1R5G5B5 only); `BeginScene`/`EndScene`; `Clear(0, NULL, D3DCLEAR_ZBUFFER, 0, 1.0f, 0)`; `SetViewport`.
  - `SetRenderState` for every row of 2.6a (unknown or unneeded rows may be ignored, fog maths can wait): default block, FLAT/GOURAUD, ALPHABLEND on/off, SRC/DEST blend ONE/ONE for the additive preset and back to SRCALPHA/INVSRCALPHA, ALPHATEST >= 8, ZFUNC LESSEQUAL, CULL CCW, DITHER, FOGENABLE/FOGCOLOR writes (set by `afSetWeatherValues` already in the front end).
  - `SetTextureStageState` stage 0: ALPHAOP 4/2/12, COLORARG1/2, ALPHAARG1/2, MAG/MIN LINEAR.
  - `SetTexture(0, surf-or-NULL)`; `DrawPrimitive` TRIANGLELIST and TRIANGLEFAN, FVF 0x1C4, flags 0; `Release`.
- **Game**
  - `SetTransform(PROJECTION)` plus the fog states: `FOGENABLE`, `FOGCOLOR`, `FOGTABLEMODE = LINEAR`, `FOGTABLESTART`, `FOGTABLEEND` (per-pixel linear fog on `1/rhw`, sky objects toggle `FOGENABLE` 0 then 1 **[U: W- or Z-based]**).
  - Large batches (25,500 vertices per call) and TL triangles that are partly behind the camera. The multiply blend (ZERO/SRCCOLOR, placed-poly mode 6) is in the DLL but the exe never selects it **[R]**; implementing it is optional.
  - `LINELIST` is not reached by the exe.
