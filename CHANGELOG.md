# Changelog

What changed in each release of the Hogs of War fix. Settings are in `hogs.ini`; the technical details of every change are in [docs/WORKLOG.md](docs/WORKLOG.md) (in Russian).

## v1.4.0 — not released yet

### Added

- Network games over the internet. The game's own network play (Multi-player, Network) needs players who can reach each other directly: a local network or an open port. With `[Network] Server` set in `hogs.ini`, the game's Network menu lists the games on a lobby server instead, and the game's messages go through that server. The menus are the game's own: "- INTERNET -" in the provider list, then the list of games with "make a game" as its last line, then the lobby, where the host starts the battle with Tab.
- `[Network] Name` is the player's name in network games. The game itself used the Windows user name.
- The server address can be a web address, `Server=wss://name`: the game then connects with a WebSocket, for a lobby server that stands behind a web front and does not show its own address.
- The list of games shows the players of each game and your ping to the server: `BRAVO'S GAME 2-4 125MS`.
- `[Network] Password`: a word shared with friends. Only players with the same word see each other's games.

### Fixed

- Network battles: the other player's turn was shown at about 8 frames a second, and less over the internet, with one processor core fully busy. The player on turn sends the ticks of the battle in groups of three, and the other PCs played each group the moment it came and then waited. Now they play the ticks evenly, about a quarter of a second behind, and draw every frame as in any other battle. This also holds for the game's own network play, without a lobby server.

### Good to know

- All players of a game need the same version of the fix and the same `[Battle] ComputerWalks` setting: each PC runs the battle itself, from the same input.
- Without `[Network] Server` nothing changes in how players find each other: the game's own network play works as before, and it works with this fix.
- The password keeps other players' games out of your list and yours out of theirs. It is not a strong lock: the server does not check it.
- Tested with two copies of the game on one PC, through a server on the internet, directly and through a web front. Not tested: more than two players, two PCs.

## v1.3.2 — 2026-11-14

### Fixed

- With the Russian translation 1.1.0 the game crashed on the screen where a team or a pig is named, when one of the keys D, F, G, I, J, L, N, Q, R, S, U, V, W, Y, Z was pressed. The game looks the Latin letter of a key up in the letter grid on screen and did not check that it found it, and the grid of that translation did not have these letters. A key without a letter in the grid does nothing now, and `russian.cmd` installs release 1.1.1 of the translation, whose grid has all Latin letters.

### Added

- On the name screen a key types the character of the current keyboard layout, if the letter grid has it. With the Russian translation the Russian layout types Russian letters. In the English game the digits and the signs of the grid can be typed too; before, only A–Z and Space worked.

### Good to know

- To update a translation that is already installed, run `russian.cmd` again.
- The letter grid of the translation has no digits now: the board has room for 49 characters, and the Russian alphabet with the Latin letters takes 48.

## v1.3.1 — 2026-11-14

### Changed

- `russian.cmd` installs release 1.1.0 of the Russian translation: the whole Russian alphabet and the Latin letters in every font, the text corrected (real letters where the translation had «И» for «Й», «Е» for «Э» and the digits 3 and 4 for «З» and «Ч»), English names and the credits readable, and the help screens showing their own text. What changed in the translation is listed in [hogs-of-war-ru](https://github.com/accidev/hogs-of-war-ru#readme).

### Good to know

- To update a translation that is already installed, run `russian.cmd` again. The English files in `english.orig` are kept as they are.
- A team or pig name typed with release 1.0.0 of the translation shows in Latin letters now: type it again.
- Nothing in the game itself changed: the fixes and the renderer are those of v1.2.2.

## v1.3.0 — 2026-11-14

### Added

- The Russian translation of the game can be installed from the fix: `russian.cmd` downloads it (about 330 MB: text, fonts, videos and voices), checks it and puts it into the game folder. `russian-remove.cmd` puts the English files back. The translation is not part of the fix: it comes from [hogs-of-war-ru](https://github.com/accidev/hogs-of-war-ru).
- On a PC whose display language is Russian, `patch.cmd` asks whether to install the translation.

### Changed

- `uninstall.cmd` also removes the translation, if `russian.cmd` installed it.

### Good to know

- Nothing in the game itself changed: the fixes and the renderer are those of v1.2.2.
- `russian.cmd` is the only script of the fix that uses the internet. It accepts only the release of the translation whose checksum is written in it, and only game data: text, fonts, pictures, videos and sound in the folders `FEText`, `Language` and `Speech`.
- The English files that the translation replaces are kept in the `english.orig` folder of the game.

## v1.2.2 — 2026-11-14

### Fixed

- Water ran too fast: its speed followed the frame rate, about 6 times the original on a 144 Hz monitor and 14 times on 360 Hz. It moves at the original speed now, whatever the frame rate. The fault came with the smooth battles of v1.1.0.
- Thin see-through lines between the pieces of the HUD (the timer, the weapon window). They showed when the picture was drawn larger than the game resolution, which is the default.
- The names over the pigs sat on their heads in resolutions with more than 480 lines. The original game had this fault too.
- With `[Display] Widescreen=1`, a game that had a 4:3 resolution saved stayed 4:3, with black bars at the sides.

### Changed

- On a monitor wider than 4:3 the launcher offers only resolutions in its shape, the largest first. The monitor's own resolution is among them, up to 4096×1536.
- The first start takes the largest resolution for the monitor, not the smallest.
- The pieces of the HUD are drawn pixel for pixel, without smoothing: sharper, and as the original game showed them.

### Good to know

- If you played 4:3 on a wide monitor, the game now starts wide, in the largest resolution. For 4:3 set `[Display] Widescreen=0`.
- The HUD is drawn in game pixels: fewer lines give a larger HUD, and the picture stays as sharp. Hold Shift while the game starts to choose a resolution.

## v1.2.1 — 2026-11-14

### Fixed

- With `[Display] Windowed=0` in `hogs.ini` the game ran unseen: the music played, but there was no window and nothing on the taskbar. The setting is removed: the game always runs in a window. For full screen set `[Display] Borderless=1`.

### Good to know

- A `Windowed` line left in your `hogs.ini` does nothing now. You can delete it.

## v1.2.0 — 2026-11-14

One DLL less in the game folder. Nothing new to set up and nothing changes in the game: update as usual.

### Changed

- `hogs.dll` is gone: its fixes are in `ddraw.dll` now, with the renderer. The fix puts `ddraw.dll`, `winmm.dll` with `winmm.ini`, and `hogs.ini` into the game folder.
- `patch.cmd` moves a game with v1.0–v1.1 of the fix over by itself: it updates `warhogs_.exe`, also when `warhogs_.exe.orig` is no longer there, and removes `hogs.dll`.
- The fixed game needs the `ddraw.dll` of this fix. With dgVoodoo2 or another `ddraw.dll` put over it the game does not start; run `patch.cmd` again to repair it.
- The fixes start only in the fixed `warhogs_.exe`. Another program that loads `ddraw.dll` from the game folder gets the renderer only.

### Fixed

- An install or uninstall that fails at any step (a locked file, a full disk) leaves a game that starts. Before, a failed copy of `ddraw.dll` could leave a half-written file.

### Good to know

- Uninstall with the `uninstall.cmd` of this version. The one of v1.0–v1.1 does not know the new `warhogs_.exe`: if `warhogs_.exe.orig` is missing, it leaves a game that does not start until `patch.cmd` runs again.

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
