/**
 * platform_compat.hpp
 * ===================
 * Resolves Raylib vs Windows SDK declaration conflicts on MSVC.
 *
 * Root cause:
 *   - raylib.h declares CloseWindow() and ShowCursor() as extern "C"
 *   - winuser.h then declares CloseWindow(HWND) and ShowCursor(BOOL)
 *   - MSVC treats this as an illegal overload of an extern-C function (C2733)
 *
 * Fix:
 *   Hijack the WinAPI identifiers with macros DURING the windows.h parse
 *   so they get registered under different names.  Then undef the hijacks.
 */
#pragma once

// ── Step 1: Raylib (registers its extern-C declarations first) ────────────────
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

// ── Step 2: Windows.h ─────────────────────────────────────────────────────────
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

// Redirect conflicting WinAPI declarations to private names during parse:
#define CloseWindow  _Win32_CloseWindow
#define ShowCursor   _Win32_ShowCursor

#include <windows.h>

// Restore: our code should call Raylib's CloseWindow / ShowCursor.
// The WinAPI originals are now accessible via _Win32_CloseWindow etc.
#undef CloseWindow
#undef ShowCursor

// Inline forwarders in case any translation unit needs the WinAPI versions:
inline BOOL Win32_CloseWindow(HWND h) { return _Win32_CloseWindow(h); }
inline int  Win32_ShowCursor (BOOL b) { return _Win32_ShowCursor(b);  }

// ── Step 3: Undef other macro wrappers that shadow Raylib ─────────────────────
#ifdef Rectangle
#  undef Rectangle
#endif
#ifdef DrawText
#  undef DrawText
#endif
#ifdef DrawTextEx
#  undef DrawTextEx
#endif
#ifdef CreateWindow
#  undef CreateWindow
#endif
#ifdef LoadImage
#  undef LoadImage
#endif
#ifdef PlaySound
#  undef PlaySound
#endif
