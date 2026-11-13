# Changelog

What changed in each release of the Hogs of War fix. Settings are in `hogs.ini`; the technical details of every change are in [docs/WORKLOG.md](docs/WORKLOG.md) (in Russian).

## v1.1.1 — 2026-11-14

A review of the whole code after v1.1.0. Nothing new to set up: update as usual.

### Fixed

These were found by reading the code. No player has reported any of them.

- A mirrored picture that reached over the edge of the screen would have shown the wrong part of itself.
- If resizing the window failed half way, the window would have stayed black for good. The next frame now tries again.
- If the frame could not be read back from the graphics card, stale pixels would have been written over the picture.
- A picture copied from outside its source no longer reads past the end of that source.
- `ddraw.dll` could crash at start without DXGI 1.1 or with a zero-sized game resolution, and a very large `[Render] Scale` broke its size arithmetic. Values above 16 now mean "automatic".
- A lost graphics device is written to `hogsdraw.log` once, instead of failing silently on every frame.
- A battle frame without a battle clock is drawn as the original game drew it.
- The installer put the patched exe in place before `hogs.dll`; a failed copy would have left a game that does not start. `hogs.dll` now goes first.
- A `hogs.ini` that cannot be read or written no longer stops the install half way; the installer says so and goes on.
- The music library is replaced only after its new copy is complete, so a failed copy cannot leave the game without `winmm.dll`.

### Changed

- The window is cleared only when the picture leaves black bars.
- `[Battle] FrameRate` is described as it works: 25 or less is the original rate, and with `VSync=1` there are never more frames than the monitor shows.
- The README says what the controller buttons do in menus, on the results screen and in videos.
- Comments that no longer matched the code are corrected; game addresses that several source files used are named in one place.

### Checked again

- Leaving a battle, the results screen, "Retry" and a second battle with the new battle loop: 25 game steps a second each time.
- The results screen in a wide-screen game: 4:3 in the middle, as designed.
- Pause, menus and the results screen with a (simulated) controller.

## v1.1.0 — 2026-11-14

### Added

- Computer pigs walk on their turn. In the original PC game their path finder never ran. `[Battle] ComputerWalks`
- Smooth battles: as many frames per second as the monitor shows instead of 25, at the original game speed, and no more 100 % load on one CPU core. `[Battle] FrameRate`, `Interpolation`
- Wide-screen battles: resolutions in the shape of the monitor show more to the sides; menus and videos stay 4:3. A new player starts in a wide resolution. `[Display] Widescreen`
- Xbox (XInput) controllers work without setup, with the PlayStation button layout. `[Gamepad] XInput`, `XInputDeadzone`
- `[Render] FogDistance`: how far the fog is.

### Fixed

- Menus run at their original speed; their animations were tied to the frame rate.
- The game no longer freezes after its window was minimized.
- The game starts when its Windows registry entry is missing, for example from a game folder copied from another PC.

### Changed

- `patch.cmd` adds every new setting to an existing `hogs.ini` and keeps the player's values.

## v1.0.0 — 2026-10-09

First release, for the Steam version on Windows 10 and 11.

- The game starts: the CD-music library that hung the Windows loader is replaced, and the copy protection that broke on modern Windows is taken out of the exe.
- A built-in Direct3D 11 renderer (`ddraw.dll`) instead of DirectX 7: no missing ground, no white boxes, sharp picture at any window size. `[Render] Scale`, `VSync`
- A normal window, resizable or borderless, safe to Alt+Tab; other windows are no longer disabled while the game runs. `[Display] Windowed`, `Resizable`, `Borderless`
- Start without the launcher and the intro videos. `[Startup] SkipLauncher`, `SkipIntro`
- Camera distance with the mouse wheel or numpad + and −.
- Music volume no longer changes the volume of the whole game in Windows.
- A smaller stick dead zone for joysticks. `[Gamepad] Deadzone`
- `patch.cmd` installs and updates, `uninstall.cmd` puts the original files back.
