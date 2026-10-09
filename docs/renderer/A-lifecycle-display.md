# Renderer `_d3d.dll` — Group A: lifecycle, driver/mode selection, display & frame control

Static analysis in Ghidra (programs `_d3d.dll` and `warhogs_unwrapped.exe`). Nothing was run.
Addresses: `_d3d.dll` uses image base `0x10000000`, the exe uses `0x400000`.
Markers: **[verified]** = read directly from disassembly or decompiled code; **[inferred]** = deduced from usage; **[unknown]** = not resolved.

---

## 0. Key findings

1. **The renderer is DirectX 7, not DirectX 6.** It creates `IDirectDraw7` with `DirectDrawCreateEx(…, IID_IDirectDraw7, …)`, gets `IDirect3D7` through `QueryInterface(IID_IDirect3D7)` and creates an `IDirect3DDevice7` with `IDirect3D7::CreateDevice(IID_IDirect3DHALDevice, backbuffer, …)`. All COM offsets used in group A match only the DX7 vtables (section 1). There is no `IDirect3DViewport3`: the viewport is set with `IDirect3DDevice7::SetViewport`. No `IDirect3DTexture2` appears in group-A code: textures are `IDirectDrawSurface7` with `DDSCAPS_TEXTURE`, see `afRestoreTextureSurfaces`.
2. **One shared "main variables" structure.** The exe owns it at `0x005388B8`, the DLL keeps its address in `DAT_11b65e9c`. It holds the back-buffer pixel format, the mode index (`-1` = windowed), the "3D disabled" flag, and the driver table with stride `0x608` (section 2). The exe's `DAT_005389d8` is `entry[0].modeCount` (`+0x5C`). The exe's `DAT_00538f58` is `entry[0].+0x5DC`: a slot where the **exe** writes the `HMODULE` of the DLL that serves the entry.
3. **`Analyse` builds two kinds of driver entries.**
   - Phase 1: one entry per DirectDraw device, type 300/301, library name `"_DD.DLL"`, **0 modes**.
   - Phase 2: one entry per *hardware* Direct3D device, type 200/201 plus penalties, library `"_D3D.DLL"`, with modes filtered by the device's render depths and ≤1024×768. At each resolution that has a 16-bit mode, the higher-bpp duplicates are removed.

   The exe picks type 201 in fullscreen if present, otherwise the first entry with type in 200..299 (`FUN_004add30`).
4. **Frame protocol.** The names are misleading:
   - `Begin2D` clears Z and calls `BeginScene`. It also resets the display-window list and smooths the zoom.
   - `Begin3D` sets the viewport and camera.
   - `End3D` is an empty `RET` (the same stub as `PreCloseDown` and `afIfaceDeInit`).
   - `End2D` calls `EndScene`.
   - `CopyToScreen` calls `Flip` in fullscreen, or `Blt` to the primary in windowed mode.

   Device loss is handled by rebuilding everything through `SelectDriverMode`.
5. **Windowed mode** (`mode = -1`):
   - Cooperative level is `DDSCL_NORMAL`. The primary has an `IDirectDrawClipper` bound to the HWND.
   - The back buffer is an off-screen `3DDEVICE` surface of client size **with no pixel format**, so it takes the **desktop format** (32-bit XRGB on a 32-bit desktop).
   - 3D is enabled only if the driver is the primary display and the HAL device reports render support for the desktop depth. Otherwise `mainVars+0xBC = 1` and no device is created.
   - Fullscreen always uses a 16-bit display mode: the exe asks `afGetModeNumber(drv,w,h,16)`.
6. **Windowed present is offset vertically.** `CopyToScreen` finds the client origin with `AdjustWindowRectEx(…, bMenu=TRUE, …)`, but the game window has no menu: class `lpszMenuName=NULL`, `CreateWindowExA(hMenu=NULL)`. The image therefore lands `SM_CYMENU` pixels (~19–20 px) too low.

   The exe's own window sizing (`FUN_0044d040`) also adds `SM_CYMENU`, so the client area and the back buffer are about that much taller than the game resolution. Net effect [inferred]: the game image sits about 20 px low, a band at the top of the client area is never painted, and the surplus rows at the bottom are clipped. Patching `PUSH 1` → `PUSH 0` at `0x1000321C` would fix it in the original DLL; untested.
7. **Colour-key loss on a 32-bit desktop is an exe-side bug exposed by windowed mode.** The exe's sprite converter `FUN_0044da70` writes pixels only into 8- or 16-bpp surfaces. In windowed mode every surface has the desktop format, so on a 32-bit desktop the sprites stay empty, and their colour key turns them into opaque black boxes or makes them invisible (section 6). Fix for D3D11: present every exe-visible DirectDraw surface as RGB565.
8. **Errors are fatal.** `Terminate`/`doexit` show message boxes, write `verbose.log` and call the CRT `exit()`.
9. **Possible latent bugs** (static analysis only):
   - The Z-buffer format list (`0x1013EA00`, room for 24 entries before the next global at `0x1013ED00`) is appended on every device creation and **never reset**: the only write to the count `0x11AB9468` is the increment in the callback, confirmed by xrefs. After about 24/N mode sets or device-loss recoveries (N = Z formats per enumeration), the callback writes past the array into the globals from `0x1013ED00` upward. Not verified at run time.
   - `afGetModeNumber` takes the mode count from the `driver` argument but reads the records of the *current* driver.
   - `Begin2D` dereferences the D3D device before checking "3D disabled", so it crashes if the device was never created. The front-end helper `FUN_00481bb0` calls it without checking `+0xBC`.
   - The device-loss recovery in `Begin2D` does not call `afRestoreTextureSurfaces` (the one in `CopyToScreen` does).

---

## 1. COM objects and DirectX usage

| DLL global | Object | Created in | How identified |
|---|---|---|---|
| `0x11B6A2F0` | `IDirectDraw7` | `FUN_10005590` (kept for the session), also a temporary one in `FUN_10007030` | `DirectDrawCreateEx(guid/NULL, &p, &IID @0x10020338, NULL)`. The IID bytes `C0 5E E6 15 9C 3B D2 11 B9 2F 00 60 97 97 EA 5B` are `{15E65EC0-3B9C-11D2-B92F-00609797EA5B}` = `IID_IDirectDraw7`. Offsets used: 0x10 `CreateClipper`, 0x18 `CreateSurface`, 0x20 `EnumDisplayModes`, 0x2C `GetCaps`, 0x4C `RestoreDisplayMode`, 0x50 `SetCooperativeLevel`, 0x54 `SetDisplayMode`. |
| `0x11AB9474` | `IDirect3D7` | `FUN_10006240`, `FUN_10007030` | `QueryInterface(&IID @0x10020328)` = `{F5049E77-4861-11D2-A407-00A0C90629A8}` = `IID_IDirect3D7`. Offsets: 0x0C `EnumDevices`, 0x10 `CreateDevice`, 0x18 `EnumZBufferFormats`. |
| `0x11AB9470` | `IDirect3DDevice7` | `FUN_10006240` | `CreateDevice(&IID @0x10020318, backbuffer, &p)`. The IID is `{84E63DE0-46AA-11CF-816F-0000C020156E}` = `IID_IDirect3DHALDevice`. Offsets: 0x10 `EnumTextureFormats`, 0x14 `BeginScene`, 0x18 `EndScene`, 0x28 `Clear`, 0x34 `SetViewport`, 0x50 `SetRenderState`, 0x94 `SetTextureStageState`. These fit `IDirect3DDevice7` only: in `IDirect3DDevice3`, `BeginScene` is 0x24. |
| `0x11B6A2F4` | `IDirectDrawSurface7` primary | `FUN_10005820` | Offsets: 0x14 `Blt`, 0x2C `Flip`, 0x30 `GetAttachedSurface`, 0x48 `GetFlipStatus`, 0x58 `GetSurfaceDesc`, 0x70 `SetClipper` |
| `0x11B6A300` | `IDirectDrawSurface7` back buffer / render target | `FUN_10005820` | fullscreen: `GetAttachedSurface(DDSCAPS_BACKBUFFER)`; windowed: `CreateSurface` |
| `0x11B6A2FC` | `IDirectDrawSurface7` Z-buffer | `FUN_10002fd0` | `CreateSurface` then `backbuffer->AddAttachedSurface` (0x0C) |
| `0x11B6A304` | `IDirectDrawClipper` (windowed only) | `FUN_10005820` | `IDirectDraw7::CreateClipper(0,&p,NULL)`, then `SetHWnd(0,hwnd)` (0x20), then `primary->SetClipper` |
| `0x11B6A2EC` | never used | never assigned | The only write is the `=NULL` after `Release` in `FUN_1000c830` (`0x1000C8AB`). No code stores a non-NULL value and its address is never passed anywhere (helper xref and raw-byte scan), so it is a dead leftover that is always NULL in this build. |

`DirectDrawCreateEx` is a static import from `DDRAW`; `DirectDrawEnumerateA` and `DirectDrawCreate` are resolved at run time by `LoadDirectDraw`.

HRESULTs handled specially:

| HRESULT | Name | Treatment |
|---|---|---|
| `0x887601C2` | `DDERR_SURFACELOST` | "lost" flag `DAT_11ad9c38 = 1` |
| `0x887601AE` | `DDERR_SURFACEBUSY` | same as `DDERR_SURFACELOST` |
| `0x8876021C` | `DDERR_WASSTILLDRAWING` | busy-waited on |
| any other error | — | `Terminate` |

Default device state, set by `FUN_10006380` when the device is created and again by `afIfaceInit` **[verified]**.

Render states:

| Index | State | Value |
|---|---|---|
| 14 | ZWRITEENABLE | 1 |
| 23 | ZFUNC | LESSEQUAL |
| 7 | ZENABLE | TRUE |
| 15 | ALPHATESTENABLE | 1 |
| 25 | ALPHAFUNC | GREATER at first, then GREATEREQUAL |
| 24 | ALPHAREF | 0 at first, then 8 |
| 19 | SRCBLEND | SRCALPHA |
| 20 | DESTBLEND | INVSRCALPHA |
| 22 | CULLMODE | CCW |
| 4 | TEXTUREPERSPECTIVE | 1 |
| 29 | SPECULARENABLE | 0 |
| 2 | ANTIALIAS | 0 |
| 26 | DITHERENABLE | 1 |
| 8 | FILLMODE | SOLID |
| 39 | STIPPLEENABLE | 0 (1 if the driver has only stippled alpha) |
| 33 | STIPPLEDALPHA | 0 (1 if the driver has only stippled alpha) |
| 41 | COLORKEYENABLE | **0** |
| 137 | LIGHTING | 0 |
| 9 | SHADEMODE | GOURAUD |
| 27 | ALPHABLENDENABLE | 1 |

Texture stage 0:

| Stage state | Value |
|---|---|
| COLOROP | (not set) |
| COLORARG1 | TEXTURE |
| COLORARG2 | DIFFUSE |
| ALPHAOP | MODULATE |
| ALPHAARG1 | TEXTURE |
| ALPHAARG2 | DIFFUSE |
| MAGFILTER | LINEAR |
| MINFILTER | LINEAR |

**Transparency in the 3D path comes from texture alpha (alpha test plus blend), not from colour keys.**

Texture format choice is the `EnumTextureFormats` callback at `0x10005F80` **[verified]**:
- It accepts only RGB formats with `DDPF_ALPHAPIXELS` and non-zero masks.
- Preference order: A1R5G5B5 (score 3) > A4R4G4B4 (2) > any other 16-bit alpha format (1) > 32-bit alpha (0).
- The chosen `DDPIXELFORMAT` goes to `0x1013E420`. Its masks, bit counts and shifts go to globals `0x1011DB00`, `0x10131B50`, `0x11AB8ED0`, `0x11AB1CA4`, etc.
- If nothing is found, `DAT_11ab948c` stays 0.

Z formats come from the `EnumZBufferFormats` callback at `0x10003090`. It appends each `DDPIXELFORMAT` to `0x1013EA00[DAT_11ab9468++]` (stride 0x20), with no bounds check. The count is never reset anywhere: its only writer is this increment (xrefs). The array ends at `0x1013ED00`, where the next global lives (`DAT_1013ed00`, the `Analyse` loop index; `0x1013ED04` is the texture red shift, which `EnumTextureFormats` rewrites), so it has room for 24 formats. `FUN_100030c0` enumerates only when `IDirect3D7` exists, so in practice once per `FUN_10006240`. The Z-buffer surface is then created with depth 16, else 24, else 32 (`FUN_100030c0` → `FUN_10002fd0`).

---

## 2. Shared structures

### 2.1 Main variables (`MainVars`) — exe `0x005388B8`, DLL `DAT_11b65e9c`

The exe passes the pointer to `Analyse`, `PowerUp` and `PreInitialise`. The DLL reads and writes it directly.

| Offset | Type | Meaning | Evidence |
|---|---|---|---|
| +0x000 | `DDSURFACEDESC2` (0x7C) | Back-buffer description, refreshed on every mode set | `FUN_1000ba90(backbuffer, mainVars)`. Its first argument is ignored: it always reads `DAT_11b6a300`. |
| +0x07C | byte | 1 if the back buffer is palettized (PAL8 or PAL4) | `FUN_1000ba90` |
| +0x080 / +0x084 / +0x088 | int | Red, blue and green bit counts **(note the order R, B, G)** | `FUN_1000ba90` |
| +0x08C | int | 8 for PAL8, 4 for PAL4, 0 for RGB | `FUN_1000ba90` |
| +0x090 / +0x094 / +0x098 | int | Red, green and blue shifts | `FUN_1000ba90` |
| +0x0A8 | int | `dwRGBBitCount` (8 for PAL8) | `FUN_1000ba90` |
| +0x0AC | int | Bytes per pixel | `FUN_1000ba90` |
| +0x0B0 | int | Current mode index, `-1` = windowed | `FUN_10005820`; `CopyToScreen`/`Begin2D` save it and set it to -1 during recovery |
| +0x0B4 | int | Back buffer is in video memory (`DDSCAPS_VIDEOMEMORY`) | `FUN_10005820` |
| +0x0B8 | int | Back buffer is `DDPF_PALETTEINDEXED8` | `FUN_10005820` |
| +0x0BC | int | **3D disabled**: no D3D device; `Begin2D`, `End2D`, `Begin3D` and `CopyToScreen` do nothing. The exe tests it before its in-game frame. | `FUN_10005820`, `SelectDriverMode`, exe `FUN_0044e290` |
| +0x0C0 | int | Driver entry count (exe `DAT_00538978`, zeroed by the exe before `Analyse`) | `FUN_1000c8d0`, enum callback |
| +0x0C4 | `DriverEntry[]` | Stride 0x608 (exe `DAT_0053897c`) | |
| +0x7964 | int | **Current driver index**. Written by the exe (`DAT_0054021c`, `FUN_004adda0`), read by the DLL everywhere. | |
| +0x7968 | int | Number of DirectDraw devices from `DirectDrawEnumerateA` | `FUN_1000c8d0` |
| +0x7A74 | `DDrawDevInfo[]` | Stride 0x9B8, scratch for enumeration (below) | `FUN_1000c8d0`, enum callback |

`DDrawDevInfo` (stride 0x9B8):

| Offset | Meaning |
|---|---|
| +0x00 | `char name[0x40]`: driver description from `DirectDrawEnumerateA` |
| +0x40 | `GUID` |
| +0x50 | `isPrimary` (the enumerated GUID was NULL) |
| +0x54 | `modeCount` |
| +0x58 | Mode records, 0x18 bytes each, at most 100 |

A mode record is `{w, h, bpp, modeXflag, ?, ?}`. `modeXflag` is 1 for modes that appear only when enumerating under `DDSCL_ALLOWMODEX` **[inferred]**: the second enumeration under `DDSCL_NORMAL` clears it (`FUN_1000ce60`).

### 2.2 `DriverEntry` (0x608 bytes, `MainVars+0xC4+i*0x608`)

The DLL builds each entry in a scratch copy at `0x11AA6520` (also stride 0x608) and copies it whole into the exe structure.

| Offset | Type | Meaning |
|---|---|---|
| +0x000 | `char[0x40]` | Display name. DirectDraw entries: `"DirectDraw"` or `"DirectDraw: %s"`. D3D entries: `"Direct3D: %s"` (primary) or `"Direct3D: %s, %s"` (secondary). |
| +0x040 | `char[0x10]` | Library name: `"_DD.DLL"` (phase 1) or `"_D3D.DLL"` (phase 2) |
| +0x050 | int | DirectDraw device index (into `DDrawDevInfo`) |
| +0x054 | int | **Type/score** (see below). The exe auto-selects by it (`FUN_004add30`) and qsorts the table by it in ascending order: comparator `LAB_004adc40` compares `[a+0x54]` with `[b+0x54]`, and ties go to a secondary comparison that was not decoded. |
| +0x05C | int | `modeCount` (exe `DAT_005389d8`). **0 for phase-1 entries.** |
| +0x060 | `Mode[50]` | 0x18 bytes each: `+0 width, +4 height, +8 bpp, +0xC modeX/sysmem flag`. Sorted by (w, h, bpp). Fullscreen adds `DDSCAPS_SYSTEMMEMORY` to the primary when `+0xC != 0`. |
| +0x510 | int | Zeroed during enumeration [unknown] |
| +0x514 | `GUID` | DirectDraw device GUID |
| +0x524 | `GUID` | `D3DDEVICEDESC7.deviceGUID` (HAL or T&L HAL); used only for `EnumZBufferFormats` |
| +0x534 | int | Primary display device (DirectDraw GUID was NULL) |
| +0x538 | `int[0x13]` | Copy of the DLL "golf flags" block `0x1002BFE8`: `[0]=3` (detail level: far distance is `(v+2)*50000/16`), `[1..17]` = 1 if the feature is supported, -1 if not, `[18]=0`. The same block is accessed by `GetGolfFlags` and `SetGolfFlags`. |
| +0x584 / +0x588 / +0x58C | int | 1 / 8 (phase 1) or 16 (phase 2) / 1 [unknown meaning] |
| +0x590 / +0x594 / +0x598 | int | 0. The exe writes "3D Library Initialised" to `DEBUG.TXT` if any entry has `+0x590 != 0`. |
| +0x5DC | `HMODULE` | **Written by the exe** (`DAT_00538f58`): the library that serves this entry. `FUN_004adda0` loads exports from it. |
| +0x5E4 | uint | D3D caps flags (phase 2), table below |
| +0x5E8..+0x5F0 | — | Zeroed |

Type/score at +0x054:
- 300 = DirectDraw on the primary display; 301 = DirectDraw on a secondary device (phase 1).
- 200 = HAL on the primary display; 201 = HAL on a secondary DirectDraw device such as a 3D-only card.
- Penalties added to 200/201: +0x20 without perspective-correct texturing, +0x10 without alpha blend or stipple, +8 for stippled alpha only, +4 without bilinear filtering.
- Values 400/500 belong to software devices, which are rejected earlier, so that code is dead.

Caps flags at +0x5E4, set by the `EnumDevices` callback at `0x10006850` **[verified]**:

| Bit | Source in `D3DDEVICEDESC7` | Meaning / use |
|---|---|---|
| 0x001 | `dwDevCaps & D3DDEVCAPS_HWRASTERIZATION` | Required: devices without it are rejected |
| 0x002 | `dpcTriCaps.dwTextureCaps & D3DPTEXTURECAPS_PERSPECTIVE` | `PowerUp` → `DAT_1002bf50`. If set, an alpha texture format is mandatory, otherwise `SelectDriverMode` disables 3D. |
| 0x008 / 0x010 / 0x020 / 0x040 | `dwDeviceRenderBitDepth & DDBD_8 / 16 / 24 / 32` | Mode filter, and the windowed-mode desktop-depth check |
| 0x080 | `dpcTriCaps.dwMiscCaps & D3DPMISCCAPS_CULLNONE` | `PowerUp` → `DAT_10131b20` |
| 0x100 | `dwShadeCaps & D3DPSHADECAPS_ALPHAFLATBLEND` | `PowerUp` → `DAT_1011d6dc` |
| 0x200 | `dwShadeCaps & D3DPSHADECAPS_ALPHAFLATSTIPPLED` (only when 0x100 is clear) | Enables `STIPPLEENABLE` and `STIPPLEDALPHA` |

Mode filter in phase 2:
- bpp 8 needs flag 0x08 **and** the global `DAT_1013e6a8`.
- bpp 16/24/32 need flags 0x10/0x20/0x40.
- Modes above 1024×768 need `DAT_1013e6a8`. That global has no writers at all (xrefs: 6 reads, 0 writes; image value 0), so 8-bit modes and modes above 1024×768 are **always excluded**.
- After that, every mode that follows a 16-bit mode of the same w×h is dropped, so the list is mostly 16-bit.
- The other kill switches read by the callback and by `PowerUp` (`DAT_10131b10`, `DAT_10131b14`, `DAT_11a25d18`, `DAT_11ab8ed8`) also have no writers, so they are always 0.

### 2.3 Window-state block — exe `0x0054D3F0`, DLL `DAT_11ab9844`

This is `PowerUp`'s 4th argument.

| Offset | Meaning |
|---|---|
| +0x00 | `fullscreenActive`: the DLL sets 1 when going fullscreen and 0 when coming back; the exe also zeroes it after `PowerDown` |
| +0x04 | Saved `GWL_STYLE` |
| +0x08 | Saved window `RECT` (from `GetWindowRect`) |
| +0x1C | Option bits read by `PowerUp`: 0x10 clears `DAT_1002bf54/58/5c` and `DAT_1002bffc`; 0x80 clears `DAT_1002bf58` and `DAT_1002bffc`. The exe never writes it: no xrefs to `0x0054D40C` and its address bytes appear nowhere in the image. No DLL write was seen in group-A code either, so it is 0 in practice and both branches are inactive. |

### 2.4 Other DLL globals used by group A

| Global | Meaning |
|---|---|
| `DAT_11ad9c38` | "Surfaces lost / device must be rebuilt" |
| `DAT_11ab9484` | Inside BeginScene |
| `DAT_11b6a050` | HWND saved by `SelectDriverMode` |
| `DAT_11b65ecc` | Bytes per pixel of the primary (= back buffer) |
| `DAT_1002c4f0` / `c4f4` | View width / height (`c4f8` / `c4fc` = w-1 / h-1) |
| `DAT_1002c500` / `c504` | Back-buffer width / height (`c508` / `c50c` = w-1 / h-1) |
| `DAT_1002c510` / `c514` | View scale X / Y (float) |
| `DAT_1002c518` | Zoom (int), smoothed toward `DAT_1002be6c` |
| `DAT_1002c51c` / `c520` | View centre |
| `DAT_11ab9510` / `11ab9508` | Projection scales: `w*zoom*sx*0.05` and `h*zoom*sy*0.0666667` |
| `DAT_1002c524` / `528` / `52c` | Camera position |
| `DAT_1002c530`, `DAT_11b6a26c`, `DAT_11b6a270` | Camera angles as indices into 4096-entry sin/cos tables at `0x11B66050` / `0x11ADA2E0` |
| `DAT_1002c534` / `c538` | Far distance (float; 30000.0 after `afGetDDHandles`/`SetGolfFlags`) |
| `DAT_11ab9504` | `ddsd.lpSurface` of the back buffer (normally NULL; software leftover) |
| `DAT_11abd95c` | `lPitch` of the back buffer |
| `DAT_11ad9c28` / `DAT_11ab94f8` | Screen X/Y of the client origin, computed by `CopyToScreen` |
| `DAT_11b6a268` | Display-window count |
| `0x11ADEE10` | Display-window records, stride 0x2C |
| `DAT_11b6a27c` | Library initialised |
| `DAT_11b6a2c8` | Debug software frame buffer enabled (`DebugMode`) |
| `DAT_11abd960` | Pointer to that buffer |

---

## 3. Exports

All exports are `__cdecl`: every one ends in a plain `RET` (checked), and the exe cleans the stack (e.g. `ADD ESP,0x2C` after `AddDisplayWindow`).
"Exe pointer" is the global that `FUN_004ac430` fills with `GetProcAddress`. `FUN_004ac430` binds every name. A missing export makes the exe show "Failed to find library function" and continue, so a replacement DLL must export all 94 names, stubs included.

### Analyse
- **Address:** `0x10005010` (ordinal 4).
- **Prototype:** `void __cdecl Analyse(HWND hwnd, MainVars *mv);`
- **Called from the exe:** not through pointer `0x005380d4` from `FUN_004ac430`. `FUN_004ada70` resolves it itself (`GetProcAddress(h,"Analyse")` into the same global) and calls it once per renderer DLL at start-up, at `0x004adb2c`: `Analyse(hwnd /*DAT_00520860*/, (MainVars*)0x005388B8)`. Before the call the exe zeroes `mv->driverCount`. After it, the exe stores the `HMODULE` into `entry.+0x5DC` of every new entry, qsorts the entries by type, ascending (`FUN_004aebe0`, comparator `LAB_004adc40`), and writes "3D Library Initialised" to `DEBUG.TXT` if any `+0x590` is set.
- **What it does:**
  1. `FUN_1000c8d0(hwnd, mv)`:
     - `LoadDirectDraw()`.
     - If `hwnd` is set: `GetWindowRect` + `LockWindowUpdate(hwnd)`.
     - `DirectDrawEnumerateA(cb 0x1000CD80, 0)` fills the scratch table (name, GUID, isPrimary).
     - For each device:
       - `DirectDrawCreateEx(guid|NULL, &dd, IID_IDirectDraw7)`. On failure: the primary device is fatal (`Terminate(0x4B,0xB5)`); a secondary one stops the loop.
       - `SetCooperativeLevel(hwnd, FULLSCREEN|EXCLUSIVE|ALLOWMODEX = 0x51)`.
       - `EnumDisplayModes(0, NULL, devInfo, cb 0x1000CE10)` records every mode with `modeXflag=1`.
       - `SetCooperativeLevel(hwnd, NORMAL)`.
       - `EnumDisplayModes(…, FUN_1000ce60)` clears `modeXflag` on modes that are still listed.
       - qsort of the (empty) scratch mode list, then `Release`.
       - Build a type-300/301 `"_DD.DLL"` entry and append it to `mv` (when `hwnd` is NULL only the primary entry is kept).
     - Restore the window (`LockWindowUpdate(NULL)`, `SetWindowPos` to the saved rect), then `UnloadDirectDraw()`.
  2. For each DirectDraw device, `FUN_10007030(guid|NULL, mv)`:
     - `DirectDrawCreateEx` into the session pointer `0x11B6A2F0`.
     - `QueryInterface(IID_IDirect3D7)`.
     - `IDirect3D7::EnumDevices(cb 0x10006850, mv)`.
     - Release everything and call `UnloadDirectDraw`.

     The callback keeps only hardware-rasterising devices. It computes the caps flags, adjusts the golf-flags block, filters and sorts the modes and appends a type-200+ `"_D3D.DLL"` entry (section 2.2). Software devices and devices without usable modes are skipped. HAL and T&L HAL presumably become separate entries [inferred from D3D7 `EnumDevices` behaviour], but `CreateDevice` always uses the HAL IID.
- **Globals:** `MainVars+0xC0/+0xC4/+0x7968/+0x7A74`; scratch table `0x11AA6520`; `DAT_11ab94c4` (count); `DAT_1013ed00` (loop index); `DAT_11a25d14` (scratch slot, always 0); golf flags `0x1002BFE8`.
- **D3D11 notes:**
  - Fill `MainVars` exactly as the exe expects, without real enumeration: one entry of type 200 named e.g. `"Direct3D: D3D11"`, library `"_D3D.DLL"`, `+0x534 = 1`, flags `0x1|0x2|0x10|0x40|0x80|0x100`.
  - Modes: at least every w×h the game or launcher can request, at 16 bpp (≤ 50 records).
  - Golf flags: `[0]=3`, the rest 1.
  - Set `+0xC0 = 1` and `+0x7968 = 1`.
  - Phase-1 `_DD.DLL` entries are useless (0 modes) and can be dropped.

### PreInitialise
- **Address:** `0x1000C8C0` (ord. 24).
- **Prototype:** `int __cdecl PreInitialise(MainVars *mv);` stores `DAT_11b65e9c = mv` and returns 1.
- **Called from the exe:** no. The pointer `0x0053810c` is bound but never read.
- **D3D11 notes:** a trivial stub.

### PreCloseDown
- **Address:** `0x100067D0` (ord. 12). The same one-byte `RET` stub as `End3D` and `afIfaceDeInit`.
- **Prototype:** `void __cdecl PreCloseDown(void);`
- **Called from the exe:** no.
  - `FUN_004ac430` binds `"PreCloseDown"` to `0x00538898` but nothing reads it.
  - At shutdown `FUN_004ac3b0` does `GetProcAddress(h, "PreCloseDown_")` (string at `0x004db210`, **trailing underscore**). That returns NULL, and the pointer is never called.
- **D3D11 notes:** a stub. `"PreCloseDown_"` need not exist.

### PowerUp
- **Address:** `0x10005100` (ord. 22).
- **Prototype:** `void __cdecl PowerUp(const char *dataPath, const char *dataPath2, MainVars *mv, WindowState *ws);`
- **Called from the exe:** pointer `0x00538148`, 1 site, `FUN_004adda0` at `0x004ade2f`:
  `PowerUp((char*)0x0054CFE8, (char*)0x0054D1E8, (MainVars*)0x005388B8, (WindowState*)0x0054D3F0)`.
  Both strings are `"data\\"`, set by `FUN_004ac2f0` and `FUN_004ac350` from `FUN_0044e080(…, "data\\", …)`. It runs when the driver is first selected or changed, after `FUN_004ac430` has bound all exports from `entry.+0x5DC`.
- **What it does:**
  1. `LoadDirectDraw()`.
  2. `InitialiseGolfLibraryI(dataPath, dataPath2, mv, ws)`.
  3. Zero 0x6B468 bytes at `0x11AFAA30`.
  4. Derive feature switches from `entry[cur].+0x5E4` and `ws+0x1C`:
     - `DAT_1002bf50` = flag 0x2 ("needs an alpha texture format").
     - `DAT_1002bf54/58/5c` = flag 0x2, filtered through the kill switches `DAT_10131b14`, `DAT_11a25d18`, `DAT_11ab8ed8`. Those three have no writers, so in practice all three follow flag 0x2.
     - `DAT_10131b20` (0x80); zero `0x11AB9498..0x11AB94A8` if 0x80.
     - `DAT_1011d6dc` (0x100), `DAT_11ab1d28` (0x200, stipple).

  It creates no DirectDraw objects: they are created by `SelectDriverMode`.
- **D3D11 notes:** keep the pointers to `mv` and `ws` and initialise internal state. Device creation can wait for `SelectDriverMode`.

### PowerDown
- **Address:** `0x100052A0` (ord. 21).
- **Prototype:** `void __cdecl PowerDown(void);`
- **Called from the exe:** pointer `0x00538060`, 1 site, `FUN_004ade50` at `0x004ade59` (no arguments). `FUN_004ade50` runs on a driver change (`FUN_004adda0`) and at shutdown (`FUN_004ac3b0`). The exe then sets `ws->fullscreenActive = 0` and the current driver to -1.
- **What it does:**
  1. `FUN_10006340()`: `End3D`, then release the `IDirect3DDevice7` and the `IDirect3D7`.
  2. If `ws->fullscreenActive`:
     - restore `GWL_STYLE`;
     - `IDirectDraw7::RestoreDisplayMode`, then `Sleep(100)`;
     - `SetCooperativeLevel(hwnd, DDSCL_NORMAL)`;
     - `SetWindowPos` to the saved rect.

     Errors go to `Terminate(0x48, 3|4, hr)`.
  3. `UninitialiseGolfLibraryI()`, then tail-jump to `UnloadDirectDraw()`.
- **D3D11 notes:** leave fullscreen (`SetFullscreenState(FALSE)` or restore the window), release the device and swap chain, restore the window style and rect.

### Terminate
- **Address:** `0x1000C5D0` (ord. 34).
- **Prototype:** `void __cdecl Terminate(int module, int error, HRESULT hr);` It does not return.
- **Called from the exe:** no. Used inside the DLL by 17 functions.
- **What it does:**
  1. `MessageBoxA("Terminating")`.
  2. Build `"Abnormal Termination" + moduleName[module] (table 0x10022068, 0x40 bytes per entry) + … + errorText[error] (table 0x10023968) + " Error Code: %d %03d"`.
  3. Append it to `verbose.log` (`FUN_1000cec0`) and print `"Terminate: <%s>"` and `"DX error: %X"`.
  4. `doexit(msg, hr)`.
- **D3D11 notes:** replace with logging plus a clean shutdown. Keep the export as a stub.

### doexit
- **Address:** `0x1000B9C0` (ord. 94).
- **Prototype:** `void __cdecl doexit(const char *msg, int code);` It does not return.
- **Called from the exe:** no. Called by `Terminate`.
- **What it does:**
  1. Set `DAT_11b6a318 = 1`.
  2. If initialised, `FUN_1000c830()` (release DirectDraw objects).
  3. If `code != 0`: show a message box with either `msg` or `"%s DX Error: 0x%X"`. If `msg` equals the buffer at `0x11B6A56C`, the format `"DX Error: %X"` is used without passing an argument: a bug.
  4. CRT `exit(code)` (`FUN_10014fb0` → `FUN_10015000`).
- **D3D11 notes:** a stub.

### LoadDirectDraw
- **Address:** `0x1000B8D0` (ord. 19).
- **Prototype:** `void __cdecl LoadDirectDraw(void);`
- **Called from the exe:** no. Internal: `PowerUp`, `FUN_1000c8d0`, `FUN_10007030`.
- **What it does:**
  - `DAT_11b6a560 = LoadLibraryA("DDRAW.DLL")`;
  - `DirectDrawCreate` → `0x11B6A358` (never used in group A);
  - `DirectDrawEnumerateA` → `DAT_11b6a328`;
  - failure → `Terminate(0x43, 0xA5|0xA3, 1)`.
- **D3D11 notes:** a stub.

### UnloadDirectDraw
- **Address:** `0x1000B950` (ord. 36).
- **Prototype:** `void __cdecl UnloadDirectDraw(void);` Calls `FreeLibrary(DAT_11b6a560)`.
- **Called from the exe:** no. Internal: `PowerDown`, `FUN_1000c8d0`, `FUN_10007030`.
- **D3D11 notes:** a stub.

### ReleaseDD
- **Address:** `0x1000BC00` (ord. 25). It is a 5-byte `JMP 0x1000C830`.
- **Prototype:** `void __cdecl ReleaseDD(void);`
- **Called from the exe:** no. The pointer `0x005386e4` is bound but never read.
- **What it does** (`FUN_1000c830`): release and NULL, in this order:
  1. back buffer `0x11B6A300`;
  2. the debug frame buffer (`FUN_10010590(&DAT_11abd960)`, only if `DebugMode` was on);
  3. clipper `0x11B6A304`;
  4. primary `0x11B6A2F4`;
  5. `IDirectDraw7` `0x11B6A2F0`;
  6. `0x11B6A2EC` (always NULL in this build, see section 1).

  It does **not** release the D3D device, the `IDirect3D7` or the Z-buffer `0x11B6A2FC`. `doexit` and `UninitialiseGolfLibraryI` call it as well.
- **D3D11 notes:** a stub.

### SelectDriverMode
- **Address:** `0x10005360` (ord. 27).
- **Prototype:** `BOOL __cdecl SelectDriverMode(HWND hwnd, int mode);` `mode` is an index into `entry[cur].modes`, or `-1` for windowed.
- **Called from the exe:** pointer `0x00538198`, 1 site, `FUN_004ade80` at `0x004adea5`. `FUN_004ade80` checks `-1 <= mode < entry[cur].modeCount` first.
  - Real calls come from `FUN_0044f840`:
    - `SelectDriverMode(DAT_00520860, -1)` when `cfg+0x314 == 0` (windowed);
    - otherwise `SelectDriverMode(DAT_00520860, afGetModeNumber(drv, w, h, 16))`.
  - On failure the exe calls `FUN_0044b1a0(0x50, 0xC4)`; afterwards it always calls `afGetDDHandles`.
  - Inside the DLL, `CopyToScreen` and `Begin2D` call it again for device-loss recovery, with the saved HWND and mode.
- **What it does:**
  1. `FUN_10006340()` releases the D3D device and `IDirect3D7`. Clear the lost flag. Store `hwnd` in `DAT_11b6a050`.
  2. **Windowed → fullscreen** (`ws[0]==0 && mode != -1`):
     - save the window rect and `GWL_STYLE`, set `ws[0]=1`, `SetWindowLongA(GWL_STYLE, 0)`;
     - if an old primary exists: `SetClipper(NULL)`, then colour-fill it black (`Blt` with `DDBLT_COLORFILL|DDBLT_WAIT`).
  3. **Fullscreen → windowed** (`ws[0]!=0 && mode == -1`):
     - restore the style;
     - `RestoreDisplayMode`, then `Sleep(100)`;
     - `SetCooperativeLevel(NORMAL)`;
     - `SetWindowPos` to the saved rect, `ws[0]=0`.
  4. `FUN_10005590(hwnd, mode)`:
     - Colour-fill and release the old back buffer, clipper and primary.
     - Create the `IDirectDraw7` if missing: NULL GUID for the primary display, else `entry.+0x514`.
     - `GetCaps`: fail if the HAL has `DDCAPS_NOHARDWARE`.
     - `SetCooperativeLevel(hwnd, DDSCL_NORMAL)`.
     - `FUN_10005820(hwnd, mode)`:
       - **Fullscreen:**
         - `SetCooperativeLevel(0x51)`;
         - `SetDisplayMode(w, h, bpp, 0, 0)` (returns 0 on failure);
         - `SetCooperativeLevel(0x51)` again;
         - `CreateSurface` primary with `dwFlags=DDSD_CAPS|DDSD_BACKBUFFERCOUNT` and `dwBackBufferCount=1`, caps `0x2218 = PRIMARYSURFACE|FLIP|COMPLEX|3DDEVICE`, plus `SYSTEMMEMORY` if the mode's `+0xC` flag is set;
         - clear it;
         - `GetAttachedSurface(DDSCAPS_BACKBUFFER)` → `0x11B6A300`;
         - clear it, `Flip(NULL, DDFLIP_WAIT)`, `GetSurfaceDesc` to read pitch, caps and pixel format;
         - clear both again; set `+0xB0 = mode`.
       - **Windowed:**
         - `GetClientRect`: view and back-buffer size = client size;
         - primary: `DDSD_CAPS`, caps `0x6200 = PRIMARYSURFACE|3DDEVICE|VIDEOMEMORY`. On failure: **lost flag = 1 and return success**;
         - back buffer: `DDSD_CAPS|HEIGHT|WIDTH`, caps `0x2040 = OFFSCREENPLAIN|3DDEVICE`, **no `DDSD_PIXELFORMAT`**, so it uses the desktop format;
         - `CreateClipper` → `SetHWnd(0, hwnd)` → `primary->SetClipper`;
         - `+0xB0 = -1`.
       - **Common tail:**
         - `FUN_1000ba90(backbuffer, mv)` fills `mv+0x00..+0xAC`. It aborts with `Terminate(1,2)` if the back buffer has `DDPF_ALPHAPIXELS`.
         - `DAT_11b65ecc` = primary bpp/8.
         - If `DebugMode` is on: allocate a w·h·Bpp system-memory buffer.
         - Recompute the projection scales and centre.
         - `+0xBC`: fullscreen → 0. Windowed → 1, unless the driver is the primary display **and** flag 0x08/0x10/0x20/0x40 matches the desktop's 1/2/3/4 bytes per pixel.
  5. On success, `FUN_10006240()` runs if `+0xBC == 0`:
     - `QI IDirect3D7`;
     - `FUN_100030c0` (`EnumZBufferFormats`, then create the Z-buffer with depth 16→24→32 and attach it to the back buffer);
     - `CreateDevice(IID_IDirect3DHALDevice, backbuffer)` (always HAL, even for a T&L entry);
     - `SetViewport(0, 0, w, h)`, `FUN_10006380` (default states), reset per-device caches;
     - `EnumTextureFormats(cb 0x10005F80)`.
  6. If the driver needs an alpha texture format (`DAT_1002bf50`) and none was found: release the device, set `+0xBC = 1` and **return 0**. Otherwise return 1.
- **D3D11 notes:**
  - Create or resize the swap chain on `hwnd`. Windowed: client size. Fullscreen: a borderless window at mode size, or DXGI fullscreen.
  - Create a depth buffer (D24S8).
  - Through the `MainVars` pointer the exe reads only `+0xBC` (`0x0044E2D2`). It also writes `+0xB0 = -1` itself in `FUN_004ada70`, `FUN_004adda0` and `FUN_004ade50`. `+0x00..+0xAC` may be filled with RGB565 values for consistency; nothing in the exe depends on them (helper xref check).
  - What matters for 2D is that the *DirectDraw surfaces* returned through `afGetDDHandles` report RGB565 (section 6.3).
  - Always set `+0xBC = 0`.
  - Bring the window style and rect save/restore along if you use a borderless window.

### afGetModeNumber
- **Address:** `0x1000F920` (ord. 64).
- **Prototype:** `int __cdecl afGetModeNumber(int driver, int width, int height, int bpp);` Returns the index or -1.
- **Called from the exe:** pointer `0x00538174`, 1 site, `FUN_0044f840` at `0x0044f8e6`: `afGetModeNumber(this->driver, w, h, 16)`. `w`×`h` are 640×480 for the front end, otherwise `cfg+0x44C/0x450`.
- **What it does:** a linear search for the `(w, h, bpp)` record. **Bug:** the count comes from `entry[driver].modeCount`, but the records come from `entry[mv->currentDriver]`. That is harmless because the exe calls it right after selecting the driver.
- **D3D11 notes:** reproduce the lookup over the synthetic mode list.

### afGetDDHandles
- **Address:** `0x100097D0` (ord. 59).
- **Prototype:** `void __cdecl afGetDDHandles(IDirectDraw7 **dd, IDirectDrawSurface7 **primary, IDirectDrawSurface7 **back);`
- **Side effect:** sets the far distance `DAT_1002c534 = DAT_1002c538 = 30000.0f`.
- **Called from the exe:** pointer `0x005380c0`, 1 site, `FUN_0044de10` at `0x0044de19`: `afGetDDHandles(obj, obj+4, obj+8)` with `obj = DAT_0052083c`, the exe's 2D "screen" object. `FUN_0044de10` is called after every `SelectDriverMode` (`FUN_0044f840`) and by the loading screen `FUN_0045cbb0`.
- **D3D11 notes:** **This is the critical interop point.** The exe draws 2D directly through these DirectDraw pointers (BMP backgrounds, Bink video, colour-keyed sprites; section 6). A D3D11 port must either:
  - (a) return fake COM objects that implement the `IDirectDraw7` and `IDirectDrawSurface7` methods the exe calls, backed by CPU or D3D11 textures and composited into the frame at call time; or
  - (b) hook the exe's 2D helpers (`FUN_0044d4d0`, `FUN_0044d560`, `FUN_0044d5f0`, `FUN_0044d900`, `FUN_0044d9c0`, …).

### InitialiseGolfLibraryI
- **Address:** `0x1000BC10` (ord. 18).
- **Prototype:** `void __cdecl InitialiseGolfLibraryI(const char *dataPath, const char *dataPath2, MainVars *mv, WindowState *ws);`
- **Called from the exe:** no. The pointer `0x005380e0` is bound but unused. Called by `PowerUp`.
- **What it does:**
  - On the first call (`DAT_11b6a27c == 0`):
    - copy `dataPath` to `0x1002C2F0` (default text `"golfdata\"`) and `dataPath2` to `0x11B6A060`;
    - store `mv` and `ws`;
    - `FUN_1000c730` allocates a 0xC44A0-byte arena (`DAT_11ab9950` plus sub-pointers) and zeroes it;
    - `FUN_10010410` builds 4096-entry cos/sin tables at `0x11ADA2E0` and `0x11B66050`;
    - zero `0x11AD9C70[200]` and the first 10000 dwords of the arena;
    - set "initialised";
    - far distance = `(flags[0]+2)*50000/16`.
  - Every call: reset the display-window count and the per-frame counters.
- **D3D11 notes:** fold into `PowerUp`.

### UninitialiseGolfLibraryI
- **Address:** `0x1000BD20` (ord. 35).
- **Prototype:** `void __cdecl UninitialiseGolfLibraryI(void);`
- **Called from the exe:** no (pointer `0x0054c594` unused). Called by `PowerDown`.
- **What it does:** if initialised: `FUN_1000c830()` (release DirectDraw objects), `FUN_1000c780()` (free the arena), clear the flag.
- **D3D11 notes:** fold into `PowerDown`.

### afIfaceInit
- **Address:** `0x1000FCA0` (ord. 68).
- **Prototype:** `void __cdecl afIfaceInit(void *table, int width, int height, int unused0, int mode3);`
- **Called from the exe:** pointer `0x0054c5ec`, 2 sites:
  - `FUN_00486030` at `0x00486188`: `afIfaceInit(&DAT_004d524c or &DAT_00520708, screenW, screenH, 0, DAT_0051abc8 ? 2 : cfg+0x48C)`. The same table pointer is passed as the 3rd argument of `afLoadTims`.
  - `FUN_0047de90` at `0x0047e5f8`: `afIfaceInit(0, screenW, screenH, 0, cfg+0x48C)`, at the end of a round after switching back to 640×480.
- **What it does:**
  - `_DAT_11c948e4 = arg4`; identity 3×3 matrix at `0x11C947F0`;
  - `mode3` 0/1/2 selects two floats at `0x1002ECA0/0x1002ECA4`: {1500, 4500} / {-1, 1500} / {-1, -1} [meaning unknown, probably distance ranges];
  - `DAT_11b6a564 = table`; reset some counters; `DAT_10022048 = 262144.0f`;
  - `afSetScannerSizeSmall(0)`; `FUN_100112f0(-1)`; `FUN_10011430({0,0,25000})`;
  - `DAT_10131b1c = (float)height`, half width and height, aspect ratios;
  - finally `FUN_10006380()` re-applies the default render states. **It needs a live D3D device.**
- **D3D11 notes:** reset the default pipeline state and keep the screen size. The exact meaning of `mode3` belongs to groups C/D.

### afIfaceDeInit
- **Address:** `0x100067D0` (ord. 67): an empty `RET` stub.
- **Prototype:** `void __cdecl afIfaceDeInit(void);`
- **Called from the exe:** pointer `0x00538140`, 1 site, `FUN_0047f590` at `0x0047f5c3` (shutdown).
- **D3D11 notes:** a stub.

### DebugMode
- **Address:** `0x1000C720` (ord. 9).
- **Prototype:** `void __cdecl DebugMode(void);` sets `DAT_11b6a2c8 = 1`, the only non-zero write to that flag. From then on every mode set allocates a w·h·Bpp system-memory buffer (`DAT_11abd960`) [its consumer is unknown]. `InitialiseGolfLibraryI` clears the flag on every call (`0x1000BD0D`), so `DebugMode` only takes effect if it is called after `PowerUp`.
- **Called from the exe:** no.
- **D3D11 notes:** a stub.

### GetMainVariables
- **Address:** `0x1000C7A0` (ord. 14).
- **Prototype:** `void __cdecl GetMainVariables(void **out /*≥26 entries*/);`

  | Slot | Content |
  |---|---|
  | [0] | `&cosTable` |
  | [1] | `&sinTable` |
  | [2] | `&bytesPerPixel` |
  | [3] | `&pDD7` |
  | [4] | `&pPrimary` |
  | [5] | `&pBack` |
  | [6] | `mv` |
  | [7] | `&viewW` |
  | [8] | `&viewH` |
  | [9] | `&bbW` |
  | [10] | `&bbH` |
  | [11] | `&lpSurface` |
  | [12] | `&lPitch` |
  | [13] | `&lostFlag` |
  | [18] | `ws` |
  | [20] | `&clientScreenX` |
  | [21] | `&clientScreenY` |
  | [23..25] | 0 |

- **Called from the exe:** no.
- **D3D11 notes:** a stub. The layout is recorded here in case a tool needs it.

### ResetDisplayWindows
- **Address:** `0x1000BE00` (ord. 26).
- **Prototype:** `void __cdecl ResetDisplayWindows(void);` sets `DAT_11b6a268 = 0`.
- **Called from the exe:** no. `Begin2D` resets the count every frame anyway.

### SetDisplayWindow
- **Address:** `0x10004B70` (ord. 28).
- **Prototype:** `void __cdecl SetDisplayWindow(int w, int h, int x, int y, float sx, float sy, float camX, float camY, float camZ, int yaw, int pitch);` Arguments 7–9 are stored raw, as in `AddDisplayWindow`.
- **Called from the exe:** no.
- **What it does:**
  - sets the view size, centre and projection scales;
  - stores the camera position and angles;
  - computes a software pointer `lpSurface + x*Bpp + y*pitch`;
  - `FUN_100075d0(x, y, w, h)` → `IDirect3DDevice7::SetViewport`.

  The argument names are [inferred] from `AddDisplayWindow` and `Begin3D`.
- **D3D11 notes:** a stub, or the same logic as `Begin3D`.

### AddDisplayWindow
- **Address:** `0x1000BD60` (ord. 3).
- **Prototype:** `void __cdecl AddDisplayWindow(int w, int h, int x, int y, float sx, float sy, float camX, float camY, float camZ, int yaw, int pitch);` 11 arguments; the exe cleans 0x2C bytes (`ADD ESP,0x2C` at `0x00481bed`). Arguments 7–9 are floats on both call sites. The DLL copies them as raw dwords; their type is decided by the consumers of `DAT_1002c524/528/52c` (groups C/D).
- **Called from the exe:** pointer `0x005381a8`, 2 sites:
  - `FUN_00481bb0` at `0x00481be7` (front-end frame start): `AddDisplayWindow(screenW, screenH, 0, 0, 1.0f, 1.0f, 0.0f, 0.0f, 50.0f /*0x42480000*/, 0x400, 0)`.
  - `FUN_0044e290` at `0x0044e411` (in-game): `AddDisplayWindow(screenW, screenH, 0, 0, 1.0f, 1.0f, (float)this[+0x11748], (float)this[+0x11750], (float)this[+0x1174C], cam[0x23], cam[0x22])`. The camera values come from the exe's camera object `DAT_00537f98`; note the 2nd and 3rd components are swapped relative to storage order.
- **What it does:** appends a record of 11 dwords (stride 0x2C) at `0x11ADEE10[DAT_11b6a268++]`; `yaw` and `pitch` are stored converted to float. `Begin3D` (`FUN_100075d0`) reads **only window 0**:
  - +0x18/+0x1C/+0x20 → camera position `DAT_1002c524/528/52c`;
  - +0x24/+0x28 (`ftol`) → yaw index `DAT_1002c530` and pitch index `DAT_11b6a26c`.

  The other fields (w, h, x, y, sx, sy) are stored but not read by group-A code. `DisplayCurrentHole` (group C) may read them [unknown].
- **D3D11 notes:** keep the list. The camera of window 0 drives the view matrix: angles are 4096 units per turn (0x400 = 90°).

### StaticViewMode
- **Address:** `0x10007100` (ord. 33).
- **Prototype:** `int __cdecl StaticViewMode(void);` returns 1.
- **Called from the exe:** no.

### CopyToScreen
- **Address:** `0x10003140` (ord. 8).
- **Prototype:** `int __cdecl CopyToScreen(HWND hwnd);` Returns 1 if the device was rebuilt, otherwise 0. The exe ignores the result.
- **Called from the exe:** pointer `0x0054c5a0`, **25 sites in 16 functions**. All sites inspected pass the main window: `CopyToScreen(DAT_00520860)`. Examples:
  - in-game frame `FUN_0044e290` at `0x0044e5d5`;
  - front-end sprite frame `FUN_00418470` at `0x004188a2`;
  - loading screens `FUN_0045c460` and `FUN_0045cbb0`, 4 times in a row;
  - Bink video `FUN_0043ee10` at `0x0043efdd` (once per video frame);
  - transition screens `FUN_0047fcd0` (9 sites).
- **What it does:**
  - Does nothing if `+0xBC` (3D disabled).
  - If the lost flag is set: clear it, `SelectDriverMode(savedHwnd, savedMode)` (rebuild everything), `afRestoreTextureSurfaces()` if a `IDirect3D7` exists, return 1.
  - **Windowed:**
    1. `GetClientRect`, then `AdjustWindowRectEx(&rc, GWL_STYLE, `**`bMenu=TRUE`**`, GWL_EXSTYLE)` gives the frame offsets.
    2. `GetWindowRect` gives the client origin on screen, saved in `DAT_11ad9c28/DAT_11ab94f8`.
    3. `primary->Blt(&dst=origin+clientRect, back, &src={0,0,bbW,bbH}, DDBLT_WAIT, NULL)`. It stretches if the window was resized, and the clipper clips. Failure sets the lost flag.

    The window has no menu (`FUN_0044cd50`: `lpszMenuName=NULL`; `FUN_0044cec0`: `CreateWindowExA(…, hMenu=NULL, …)`). Because of `bMenu=TRUE` (`PUSH 0x1` at `0x1000321C`) the computed origin is therefore `SM_CYMENU` pixels below the real client origin.

    The exe's `FUN_0044d040` sizes the window as `h + SM_CYMENU + 2*(SM_CYFRAME-SM_CYBORDER) + SM_CYCAPTION`, so the client area, and with it the back buffer, is also about `SM_CYMENU` taller than the game resolution. The two errors partly cancel. Net effect [inferred]: the 640×480 content stays visible but sits about 20 px low; a band at the top of the client area is never painted and the extra bottom rows are clipped.

    In the original DLL, changing `6A 01` to `6A 00` at `0x1000321C` would remove the shift (not tested).
  - **Fullscreen:** `primary->Flip(NULL, DDFLIP_WAIT)`; failure sets the lost flag.
- **D3D11 notes:**
  - `IDXGISwapChain::Present`, plus a frame limiter (the menu runs at about 1460 FPS according to `WORKLOG.md`).
  - DXGI presents to the client area, so the menu-offset bug disappears.
  - Device loss → handle `DXGI_ERROR_DEVICE_REMOVED` only.

### ClearBackBuffer
- **Address:** `0x100032E0` (ord. 7).
- **Prototype:** `void __cdecl ClearBackBuffer(uint32_t color16);`
- **What it does:** `GetSurfaceDesc(back)`, then `Blt(COLORFILL|WAIT)` over the whole surface with `dwFillColor = color & 0xFFFF`. The mask assumes a 16-bit back buffer.
- **Called from the exe:** no (pointer `0x00538028` unused).
- **D3D11 notes:** a stub, or convert RGB565 to float4 and clear.

### Begin2D
- **Address:** `0x100065D0` (ord. 5).
- **Prototype:** `int __cdecl Begin2D(void);` Returns 1 if the device was rebuilt. The exe ignores the result.
- **Called from the exe:** pointer `0x00537fe0`, 2 sites:
  - `FUN_0044e290` at `0x0044e2e0`: the in-game frame, guarded by `mv->+0xBC == 0`.
  - `FUN_00481bb0`: the front-end frame start used by 13 functions, **not guarded**.
- **What it does:**
  1. **First** `device->Clear(0, NULL, D3DCLEAR_ZBUFFER, 0, 1.0f, 0)` (`FUN_10002fb0(2)`). The colour buffer is **not** cleared. This runs even when `+0xBC=1`, where the device is NULL, so it crashes.
  2. Return 0 if `+0xBC`.
  3. If the lost flag is set, loop `SelectDriverMode(saved)` until it clears. There is no `afRestoreTextureSurfaces` here.
  4. `BeginScene`. `SURFACELOST`/`SURFACEBUSY` → lost flag and return 0; other errors → `Terminate(0x54, 0xCC)`.
  5. Spin on `primary->GetFlipStatus(DDGFS_ISFLIPDONE)` while it returns `WASSTILLDRAWING`.
  6. Set "in scene" (`DAT_11ab9484 = 1`).
  7. Reset the display-window count `DAT_11b6a268` and the batch counters `0x11B6A2AC…`, `0x11AB9450…`.
  8. Zoom smoothing: `zoom += ftol((target-zoom)*0.333)`, or ±1 when that step is 0 (`DAT_1002c518` → `DAT_1002be6c`).
  9. `DAT_10131b1c = ((2*halfH)/15)*zoom`.
- **D3D11 notes:** "begin frame":
  - clear depth to 1.0 (keep the colour: the exe pre-blits backgrounds with DirectDraw *before* `Begin2D`);
  - reset the per-frame lists;
  - smooth the zoom.

### End2D
- **Address:** `0x100067E0` (ord. 11).
- **Prototype:** `void __cdecl End2D(void);`
- **Called from the exe:** pointer `0x005380cc`, 2 sites:
  - `FUN_0044e290` at `0x0044e5c8`;
  - `FUN_00481c10` at `0x00481c16` (tail `JMP`; `FUN_00481c10` = `End3D(); End2D();`, used by 13 functions).
- **What it does:** if `+0xBC==0`, not lost and in scene: `EndScene`. `SURFACELOST`/`SURFACEBUSY` → lost flag; other errors → `Terminate(0x55, 0xCD)`; success clears "in scene".
- **D3D11 notes:** end of the 3D/2D composition for the frame. Flush any batched draws.

### Begin3D
- **Address:** `0x10006750` (ord. 6).
- **Prototype:** `void __cdecl Begin3D(void);`
- **Called from the exe:** pointer `0x00537fe8`, 2 sites:
  - `FUN_0044e290` at `0x0044e41a`;
  - `FUN_00481bb0` at `0x00481c01` (tail `JMP`).
- **What it does:** if `+0xBC==0` and not lost:
  - reset counters, set `DAT_11b6a280 = 1`;
  - `FUN_100075d0(0, 0, bbW, bbH)`: `SetViewport` {0, 0, w, h, minZ 0, maxZ 1}, load camera position and angles from display window 0, recompute view size, centre and projection scales;
  - `FUN_10013830(DAT_1002c530, DAT_11b6a26c, DAT_11b6a270)` builds the camera rotation matrix from the sin/cos tables into `0x11AD9C44…`. The names yaw/pitch/roll are [inferred].

  The viewport is always the **whole back buffer**: `AddDisplayWindow`'s x/y/w/h are not used here.
- **D3D11 notes:** set viewport and view/projection constants. The projection uses focal scales `w*zoom/20` and `h*zoom/15`, i.e. 4:3 [inferred].

### End3D
- **Address:** `0x100067D0` (ord. 12): an empty `RET`, shared with `PreCloseDown` and `afIfaceDeInit`.
- **Prototype:** `void __cdecl End3D(void);`
- **Called from the exe:** pointer `0x005380d0`, 2 sites: `FUN_0044e290` at `0x0044e5c2` and `FUN_00481c10` at `0x00481c10`. The DLL itself calls it from `FUN_10006340`.
- **D3D11 notes:** a stub. Optionally a hook point to flush 3D batches.

### afGetFrameCount
- **Address:** `0x1000F990` (ord. 60).
- **Prototype:** `int __cdecl afGetFrameCount(int anim);` Returns `animTable[anim].dw0`, with no range check.
- **Called from the exe:** pointer `0x00538018`, 1 site, `FUN_004808c0` at `0x00480b5b` (front-end hog model display): `afGetFrameCount(animIndex)`. The result feeds a float timing computation.
- **What it is:** **not a display-frame counter.** It reads a static animation table at `0x1002C770`: 0x53 entries, 0x58 bytes each, `{int frameCount; int frameStep; keyframe triplets…}`. `afGetKeyFrameList(anim)` returns `&entry.dw2`. For example, entry 0 is `{17, 240, …}` and entry 1 is `{20, 204, …}`.
- **D3D11 notes:** copy the table as it is (it belongs with the animation code, group B).

### afGetFrameStep
- **Address:** `0x1000F9B0` (ord. 61).
- **Prototype:** `int __cdecl afGetFrameStep(int anim);` Returns `animTable[anim].dw1`, or entry 0 if `anim > 0x52`.
- **Called from the exe:** pointer `0x0054c5b8`, 2 sites:
  - `FUN_00440e30` at `0x00440e64`;
  - `FUN_00440f10` at `0x00440f44`.

  Both clamp `anim > 0x52` to 0 and store `afGetFrameStep(anim) + 1` as the animation length of a hog object.
- **D3D11 notes:** same table as `afGetFrameCount`.

---

## 4. Frame lifecycle as seen from the exe

Start-up, in this order **[verified]**:

1. `FUN_0047f0b0`: create the window (class `PigsWClass`, no menu) and the 2D screen object `DAT_0052083c`.
2. `FUN_0044e080`:
   - `FUN_004ada70`: `LoadLibrary` the renderer DLL, then `Analyse(hwnd, mv)`, sort the drivers;
   - `FUN_0044f840(640, 480, fullscreen=cfg+0x314, driver=cfg+0x484)`:
     - [windowed only] `FUN_0044d040` resizes the window;
     - `FUN_004adda0(driver or FUN_004add30() auto-pick)`, which binds the exports and calls `PowerUp`;
     - `SelectDriverMode(hwnd, -1 | afGetModeNumber(drv, w, h, 16))`;
     - `afGetDDHandles(&screen->dd, &screen->primary, &screen->back)`.
3. `GetGolfFlags`, then `SetGolfFlags(…)` (`FUN_0044e080`).
4. `afIfaceInit(…)` (`FUN_00486030`).
5. Entering a round: `FUN_0044f840(cfg+0x44C, cfg+0x450, …)` switches the mode again: `SelectDriverMode` plus `afGetDDHandles`. At the end of a round it switches back to 640×480, then `afRestoreTextureSurfaces()`, then `afIfaceInit(0, …)`.
6. Shutdown (`FUN_0047f590`): `afIfaceDeInit()`, then `FUN_0044e240` → `FUN_004ac3b0` → `PowerDown()`, then `FreeLibrary`.

In-game frame, `FUN_0044e290` **[verified]**:

```
if (mv->+0xBC == 0) {
  Begin2D();                       // Z clear, BeginScene, reset display windows, zoom smoothing
  AddDisplayWindow(w,h,0,0,1.0f,1.0f, camPos0,camPos2,camPos1 /*floats*/, cam[0x23],cam[0x22]);
  Begin3D();                       // SetViewport(full back buffer), camera matrix from window 0
  ... objects -> sort lists (afAddObjectToSortList, afDrawObj, ...)
  DisplayCurrentHole(1, ..., ..., flags);   // landscape + sort-list flush (group C/D)
  ... HUD (FUN_00459550, FUN_0045ee30, FUN_00459ac0)
  End3D();                         // no-op
  End2D();                         // EndScene
  CopyToScreen(hwnd);              // Flip / Blt to primary
}
```

Front-end frames, `FUN_00481bb0` … `FUN_00481c10` **[verified]**. Example: the sprite frame in `FUN_00418470`, or a transition frame in `FUN_0047fcd0` / `FUN_004808c0`.

```
[optional] DirectDraw Blt of a background BMP surface into the back buffer   // FUN_0044d4d0, BEFORE the scene
Begin2D();
AddDisplayWindow(w,h,0,0,1.0f,1.0f,0,0,50.0f,0x400,0);
DisplayCurrentHole(0,0,0,0);
Begin3D();
  ... D3D draws (afDrawAnimModel, afDraw2dPolyIn3dWorld ...)
  ... DirectDraw Blt of sprites onto the back buffer with DDBLT_KEYSRC|DDBLT_WAIT (0x01008000)   // FUN_00418470, INSIDE the scene
End3D(); End2D();
CopyToScreen(hwnd);
```

Loading screens (`FUN_0045c460`, `FUN_0045cbb0`) and Bink videos (`FUN_0043ee10`) use **no scene at all**: they `Blt` a surface into the back buffer and call `CopyToScreen`. Loading screens do it 4 times in a row, presumably so that every flip buffer is filled [inferred].

`ClearBackBuffer` is never called. Colour clears happen only at mode set; in a frame, the background comes from a DirectDraw `Blt`, the sky or the landscape.

> `WORKLOG.md` ("Кадр меню") lists the traced calls in a different order: `Begin3D, DisplayCurrentHole, End3D, Begin2D, AddDisplayWindow…`. The disassembly of `FUN_00481bb0` is unambiguous: `CALL [Begin2D]`, `CALL [AddDisplayWindow]`, `CALL [DisplayCurrentHole]`, `JMP [Begin3D]`. Check whether the trace tool's ordinal-to-name table labels the 2D/3D pairs correctly.

## 5. Windowed vs fullscreen inside the DLL

| | Fullscreen (`mode >= 0`) | Windowed (`mode == -1`) |
|---|---|---|
| Cooperative level | `DDSCL_FULLSCREEN|EXCLUSIVE|ALLOWMODEX` (0x51) | `DDSCL_NORMAL` (8) |
| Display mode | `SetDisplayMode(w, h, bpp, 0, 0)`. bpp comes from the mode table: 16 because of `afGetModeNumber(…,16)` and the duplicate filter. | Unchanged (desktop) |
| Window | style saved, then `GWL_STYLE = 0`; restored on the way back | untouched; size comes from the exe (`FUN_0044d040`) |
| Primary | complex flip chain, 1 back buffer, caps `PRIMARY|FLIP|COMPLEX|3DDEVICE` (+`SYSTEMMEMORY` for mode-X-flagged modes) | `PRIMARY|3DDEVICE|VIDEOMEMORY`, no back buffer |
| Back buffer | attached flip surface, display-mode format (16-bit) | `OFFSCREENPLAIN|3DDEVICE`, client size, **desktop pixel format** |
| Clipper | none | `CreateClipper`, `SetHWnd(hwnd)`, `primary->SetClipper` |
| Present | `Flip(NULL, DDFLIP_WAIT)`; `Begin2D` waits for `GetFlipStatus(ISFLIPDONE)` | `Blt(primary, &clientRectOnScreen, back, &{0,0,bbW,bbH}, DDBLT_WAIT)`. Destination Y is too low by `SM_CYMENU` because of `bMenu=TRUE`. |
| 3D enabled (`+0xBC==0`) | always | only if `entry.+0x534` (primary display) **and** the device lists the desktop depth in `dwDeviceRenderBitDepth` (flags 0x08/0x10/0x20/0x40 for 8/16/24/32-bit) |
| Extra checks | — | back-buffer format must not have `DDPF_ALPHAPIXELS` (else `Terminate`); a `CreateSurface` failure on the primary returns "success" with the lost flag set, which leads to a NULL back buffer in `FUN_10006240` [possible crash path, not verified at run time] |

Desktop bit depth in windowed mode:
- **16-bit desktop:** back buffer RGB565 (or 555). The DLL writes the real format into `mv` (+0x00..+0xAC, `DAT_11b65ecc = 2`) and everything behaves as in fullscreen.
- **32-bit desktop:** back buffer X8R8G8B8, `mv+0xA8 = 32`, `+0xAC = 4`. 3D works if the device reports `DDBD_32`. Any DirectDraw surface created later without an explicit pixel format is also 32-bit. Surfaces created *with* an explicit 16-bit format can no longer be `Blt`-ed to the back buffer, because DirectDraw `Blt` does not convert formats.
- **8-bit desktop:** palettized back buffer (`+0xB8 = 1`); 3D only with `DDBD_8` (rare).

Also relevant, outside the DLL:
- The launcher lists only 16-bpp modes from 640×480 to 1024×768 (`FUN_004815d0`). At `0x0048174C` the bytes `83 7E 54 10 0F 85` decode to `CMP dword ptr [ESI+0x54],0x10 / JNZ`, which skips every mode whose `dwRGBBitCount != 16`.
- The in-game resolution `cfg+0x44C/0x450` comes from that list.
- If no 16-bpp mode is enumerated, it stays 0. That fits the division by zero at `0x0044E1C5` (`idiv [ecx+0x44C]`) seen in `WORKLOG.md` without dgVoodoo [inferred].
- `Analyse` briefly puts the main window into `DDSCL_FULLSCREEN|EXCLUSIVE` (with `LockWindowUpdate`) while it enumerates modes, even when the game will run windowed.

## 6. Why a windowed 2D overlay can lose colour-key transparency on a 32-bit desktop

**Short answer [verified].**
- In windowed mode every DirectDraw surface takes the desktop pixel format:
  - the DLL's back buffer;
  - the exe's off-screen sprite surfaces, created by `ddutil`-style helpers without `DDSD_PIXELFORMAT`.
- On a 32-bit desktop these surfaces are therefore 32 bpp.
- The exe's converter for sprites packed in `.mad` files (`FUN_0044da70`) writes pixels only into 8- or 16-bpp surfaces. On a 32-bpp surface it writes nothing and goes straight to `Unlock`.
- The colour key it then sets no longer describes the image:
  - with magenta `0xFF00FF` the never-written pixels do not match, so the sprite shows as an opaque box of whatever the allocator returned, usually black;
  - with "top-left pixel" every pixel matches, so the sprite vanishes.
- Which key is used depends on `DAT_0051f120`:
  - Its initial value is 1 (static initialiser at `0x0047DE55`), so magenta and black boxes are the normal case.
  - Several front-end functions set it to 0 only around calls to `FUN_00480780`, then restore it. Sprites loaded inside those windows key on the top-left pixel and turn invisible instead (helper xref analysis: 25 writes, 26 reads).
- The keys themselves are computed correctly for any depth.

In fullscreen the display mode is 16-bit, so the bug does not appear. That is also why dgVoodoo's `DesktopBitDepth=16` brings the menu background back (see `WORKLOG.md`).

### 6.1 Evidence on the DLL side
- The 3D/sort-list path does not use colour keys: `COLORKEYENABLE = 0` (render state 41 in `FUN_10006380`), and only alpha texture formats are accepted (callback `0x10005F80`). So the 3D-drawn overlays (`afDraw2dPolyIn3dWorld`, `afDrawText`, `afAdd2dPolyToSortList`) do not depend on the desktop depth for transparency.
- In windowed mode the back buffer has **no pixel format** (`dwFlags = DDSD_CAPS|DDSD_HEIGHT|DDSD_WIDTH`, caps `0x2040`, `FUN_10005820` at `0x10005C4E`–`0x10005C64`), so on a 32-bit desktop it is 32-bit.
- `ClearBackBuffer` masks the fill colour to 16 bits: `AND EAX,0xFFFF` at `0x10003347`. This shows the author assumed a 16-bit target, but the exe never calls it.
- The exe draws its 2D sprites with **DirectDraw** `Blt` on the back buffer it got from `afGetDDHandles`, with flags `0x01008000 = DDBLT_WAIT|DDBLT_KEYSRC` (`FUN_00418470`), i.e. colour keys of the *source* surfaces.

### 6.2 Evidence on the exe side

The exe's DirectDraw helpers live at `0x0044AC60`–`0x0044DE10` and look derived from the DirectX SDK `ddutil.cpp`. The screen object is `DAT_0052083c` (alias `DAT_0051c1f4`), with `+0` = `IDirectDraw7*`, `+4` = primary, `+8` = back buffer, all filled only by `afGetDDHandles`. No exe code was found that reads the primary.

**`FUN_0044da70` (BMP in memory → new surface) [verified by disassembly].**
- `CreateSurface` with `dwFlags = 7` (`CAPS|HEIGHT|WIDTH`, no pixel format), caps `0x840` (`OFFSCREENPLAIN|SYSTEMMEMORY`), size from the BMP header (`0x0044DAC8`–`0x0044DAE3`).
- `Lock`, then branch on `ddsd.dwRGBBitCount`:
  ```
  0044db28  MOV EAX,[ESP+0x80]     ; dwRGBBitCount
  0044db2f  CMP EAX,0x4  / JZ 0x0044dd15   ; nothing
  0044db38  CMP EAX,0x8  / JZ 0x0044dcab   ; copy palette indices
  0044db41  CMP EAX,0x10 / JNZ 0x0044dd15  ; 24/32 bpp: jump straight to Unlock, no pixels written
  ```
- The 16-bpp path builds 555 or 565 from the BMP palette (`0x0044DBF2 CMP EBX,0x7C00`).
- After `Unlock` (`0x0044DD1E`): `FUN_0044b170(surf, DAT_0051f120 ? 0xFF00FF : CLR_INVALID)` (`0x0044DD24`–`0x0044DD40`), i.e. SetColorKey with `DDCKEY_SRCBLT`.
- Users (helper xref analysis): `FUN_0042f4a0` (≈260 sites in `FUN_00421ad0`: HUD and front-end sprite sets) and `FUN_00457690` (`language\tims\menutims.mad`, 68 images).

**`FUN_0044b070` / `FUN_0044b170` (`DDColorMatch` / `DDSetColorKey`).**
- `SetPixel(0,0,rgb)` through GDI, then `Lock` and read back the raw pixel. The mask `(1<<bpp)-1` is applied only below 32 bpp.
- With `CLR_INVALID` it skips `SetPixel` and keys on the existing pixel (0,0).
- Then `SetColorKey(DDCKEY_SRCBLT)` with low = high = that value.
- So the key value is right at any depth. Only the image content is wrong.

**`FUN_0044d900` (create off-screen surface) [verified].** It creates the surface the same way (`dwFlags=7`, caps `0x840`). It pre-fills with a hard-coded 16-bit magenta: `0x7C1F` if `dwRBitMask==0x7C00`, else `0xF81F` (`0x0044D96B`–`0x0044D98D`). It optionally keys on `0xFF00FF`. At 32 bpp the fill is `0x0000F81F`, a dark colour that does not match the key `0x00FF00FF`. According to the helper analysis this is latent today: the only keyed user, the text strip from `FUN_0045e530`, is cleared before each draw.

**Blitting the sprites.**
- `FUN_0044d560`: `BltFast(…, DDBLTFAST_SRCCOLORKEY|DDBLTFAST_WAIT = 0x11)` when the target is the back buffer; otherwise `Blt(…, DDBLT_KEYSRC[|DDBLT_WAIT])`.
- `FUN_0044d4d0` and `FUN_0044d5f0`: `Blt` with `0x8000` or `0x01008000`.
- `FUN_0044d800` packs a 555/565 fill colour (its inputs are always 0, so it is harmless).

**What works at 32 bpp.**
- `.bmp` files loaded through GDI (`FUN_0044d9c0` → `LoadImageA` + `StretchBlt` on the surface DC): fonts, briefing backgrounds, the load bar.
- Bink video (`BinkDDSurfaceType` adapts to the surface).
- Plain or keyed `Blt`s of those surfaces.

**DLL pixel-format fields.** The exe never reads `MainVars+0x80..+0xAC`. The only field it reads through the `MainVars` pointer is `+0xBC` (`0x0044E2D2`). The exe's format awareness is per surface: `dwRGBBitCount` and `dwRBitMask == 0x7C00`.

The remaining windowed artefacts in `WORKLOG.md` (white rectangles behind "press any key", solid white searchlight beams) also appear with a 16-bit desktop, so this colour-key bug does not explain them. They belong to the D3D alpha path (groups B/D).

### 6.3 Consequence for the D3D11 port
- Emulate `IDirectDraw7::CreateSurface` so that surfaces created without `DDSD_PIXELFORMAT` (and the back buffer as seen by the exe) **report RGB565**: `dwRGBBitCount=16`, masks `F800/07E0/001F`, whatever the desktop or swap-chain format.
- `Lock`/`Unlock` then hand out 16-bit memory. `FUN_0044da70`, `FUN_0044d900` and `DDColorMatch` all work unchanged.
- Convert to the swap-chain format when compositing, and implement the colour key as an exact 16-bit compare (pixel-shader `discard`) at draw time.

## 7. Notes for the Direct3D 11 implementation (group A summary)

- **Exports:** all 94 names must exist. Behaviour is needed only for those the exe calls:
  - start-up and shutdown: `Analyse`, `PowerUp`, `PowerDown`, `SelectDriverMode`, `afGetModeNumber`, `afGetDDHandles`, `afIfaceInit`, `afIfaceDeInit`;
  - frame: `AddDisplayWindow`, `Begin2D`, `Begin3D`, `End3D`, `End2D`, `CopyToScreen`;
  - animation table: `afGetFrameCount`, `afGetFrameStep`.

  The others (`PreInitialise`, `PreCloseDown`, `Terminate`, `doexit`, `LoadDirectDraw`, `UnloadDirectDraw`, `ReleaseDD`, `InitialiseGolfLibraryI`, `UninitialiseGolfLibraryI`, `DebugMode`, `GetMainVariables`, `ResetDisplayWindows`, `SetDisplayWindow`, `StaticViewMode`, `ClearBackBuffer`) can be stubs.
- **State to emulate:**
  - `MainVars` (section 2): driver entry, modes, `+0xB0`, `+0xBC = 0`. The pixel-format fields `+0x00..+0xAC` are optional because the exe never reads them;
  - `WindowState` (fullscreen flag, saved style and rect);
  - per-frame display windows and camera of window 0 (yaw/pitch as 4096-step angles);
  - zoom smoothing (`DAT_1002c518` toward `DAT_1002be6c`, factor ~0.333);
  - golf flags block;
  - animation table `0x1002C770`.
- **Frame:** `Begin2D` = begin frame plus depth clear (do not clear colour); `Begin3D` = viewport and camera; `End2D` = finish; `CopyToScreen` = present plus frame limiter.
- **DirectDraw interop:** the exe keeps raw `IDirectDraw7`/`IDirectDrawSurface7` pointers and draws with them between and inside frames. Apply each `Blt`/`BltFast` to the render target at call time, in call order relative to the D3D draws. For example, the background is blitted before `Begin2D`, and `FUN_00418470` blits its sprites between `Begin3D` and `End2D`.
  - Methods the exe uses on them: `CreateSurface`, `CreatePalette`, `Blt` (`COLORFILL`, `KEYSRC`, `DDFX` mirror, stretch), `BltFast(SRCCOLORKEY)`, `Lock`/`Unlock`, `GetDC`/`ReleaseDC`, `GetSurfaceDesc`, `SetColorKey(SRCBLT)`, `SetPalette`, `IsLost`/`Restore`, `Release`.
  - Expose all of them as **RGB565** (section 6.3).
- **Can be dropped:**
  - device-loss recovery;
  - cooperative levels and mode-X;
  - clipper, `AdjustWindowRectEx` offset maths and `GetFlipStatus` spinning;
  - Z-format and texture-format enumeration (always use D24S8 and BGRA8 or B5G5R5A1);
  - the `_DD.DLL` driver entries;
  - `DebugMode` buffer and `verbose.log`.
