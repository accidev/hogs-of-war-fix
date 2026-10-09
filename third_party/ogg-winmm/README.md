# ogg-winmm

CD-audio emulator (`winmm.dll`): plays the game's CD tracks from `MUSIC\TrackNN.ogg`.

- Source: https://github.com/ayuanx/ogg-winmm
- Version: v2025.01.16, https://github.com/ayuanx/ogg-winmm/releases/download/v2025.01.16/ogg-winmm_binary.zip (SHA-256 `F77B2C01E408BF5D1445C927F54D5932CD9101F46CD0CB4593BD20EF4B1AF04B`)
- License: GPL-2.0, see `LICENSE.txt`

The binary is not stored in this repository. `tools/make_release.ps1` downloads the official release archive, checks its SHA-256 and puts `winmm.dll` and `winmm.ini`, with this license, into the release package.

Why it is needed: the Steam build of Hogs of War ships an older ogg-winmm (2014) whose `winmm.dll` deadlocks the Windows 10/11 DLL loader, so the game never starts.
