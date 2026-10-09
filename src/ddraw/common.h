/* hogsdraw: a DDRAW.dll for Hogs of War that implements the part of DirectDraw 7 /
 * Direct3D 7 the game uses, on top of Direct3D 11. */
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
/* a DirectX 7 method the game called that hogsdraw does not implement (logged once each) */
void unimplemented(const char *what);
/* QueryInterface for an interface hogsdraw does not provide */
void unknown_interface(const char *on, REFIID riid);
