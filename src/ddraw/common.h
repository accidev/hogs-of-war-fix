/* hogsdraw: DDRAW.dll for Hogs of War, DirectDraw 7 / Direct3D 7 on Direct3D 11, plus the game fixes
 * of src/hogs (DllMain in main.cpp starts them in the patched game only) */
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define DIRECTDRAW_VERSION 0x0700
#define DIRECT3D_VERSION 0x0700
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>

/* hogsdraw.log next to the game */
void log_printf(const char *fmt, ...);
/* a called DirectX 7 method that is not implemented, logged once each */
void unimplemented(const char *what);
void unknown_interface(const char *on, REFIID riid);
