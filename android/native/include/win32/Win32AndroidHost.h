#pragma once

// The Android host's side of the message layer (include/win32/WinUser.h): what Windows' display
// and input system does for a Win32 program, the activity does here. Any thread may call these;
// what reaches a window does so on the window's thread, as Win32 messages, the way Windows
// delivers them. src/win32/User32Android.cpp implements it.

#include <Windows.h>

struct ANativeWindow;

/// The screen's size in pixels: the activity's surface. Maximized windows take the new size.
void Win32Android_SetScreenSize(int width, int height);

/// Gives `window` (a top-level window) the surface it is shown on, or takes it away (null). The
/// layer holds a reference to the surface while the window has it.
BOOL Win32Android_SetWindowSurface(HWND window, ANativeWindow* surface);

/// The surface `window` is shown on: its top-level window's. Null while there is none. The
/// pointer stays valid while the window has the surface.
ANativeWindow* Win32Android_GetWindowSurface(HWND window);

/// The activity came to the front (TRUE) or went to the back (FALSE): the application's
/// activation (WM_ACTIVATEAPP, WM_ACTIVATE, the focus), with `window` (a top-level window) as the
/// foreground window when it comes to the front.
void Win32Android_SetForeground(HWND window, BOOL foreground);

// --- Input -------------------------------------------------------------------------------------
// Positions are screen pixels. The mouse messages go to the capture window, else to the window
// under the pointer, in its client coordinates; WM_MOUSEWHEEL goes to the focus window, with the
// screen position, as Win32 sends it. A second press of a button within the double-click time and
// distance is a double click for a window whose class has CS_DBLCLKS.

#define WIN32ANDROID_BUTTON_LEFT 0
#define WIN32ANDROID_BUTTON_RIGHT 1
#define WIN32ANDROID_BUTTON_MIDDLE 2
#define WIN32ANDROID_BUTTON_X1 3
#define WIN32ANDROID_BUTTON_X2 4

void Win32Android_MouseMove(int x, int y);
void Win32Android_MouseButton(int button, BOOL down, int x, int y);
/// `delta` in WHEEL_DELTA units of 120 per notch, positive away from the user.
void Win32Android_MouseWheel(int delta, int x, int y);

/// A key went down or up: WM_KEYDOWN and WM_KEYUP (WM_SYSKEYDOWN and WM_SYSKEYUP while Alt is
/// down, and for F10) to the focus window, after the low-level keyboard hooks. `character` is
/// what the press types (0: none): TranslateMessage turns it into WM_CHAR.
void Win32Android_Key(UINT virtualKey, UINT scanCode, BOOL down, BOOL extended, wchar_t character);

/// Text without a key (an input method's commit): WM_CHAR to the focus window, one per
/// character. wchar_t holds a whole code point on Android, so a character beyond the BMP is one
/// WM_CHAR (Windows sends two UTF-16 halves).
void Win32Android_Text(const wchar_t* text);

// --- The shell ---------------------------------------------------------------------------------
// src/win32/Win32Android.cpp.

/// Shows `target`, which the game asked ShellExecuteW to open (a link), with what the system has
/// for it, and says whether something took it. Called on the game's thread.
using Win32AndroidShellOpen = BOOL (*)(const wchar_t* target);

/// Sets what ShellExecuteW's "open" calls (null: nothing, so it fails with SE_ERR_NOASSOC).
void Win32Android_SetShellOpen(Win32AndroidShellOpen open);
