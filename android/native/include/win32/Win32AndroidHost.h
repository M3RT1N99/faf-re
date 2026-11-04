#pragma once

// The Android host's side of the message layer (include/win32/WinUser.h): what Windows' display
// and input system does for a Win32 program, the activity does here. Any thread may call these;
// what reaches a window does so on the window's thread, as Win32 messages, the way Windows
// delivers them. src/win32/User32Android.cpp implements it.

#include <Windows.h>

struct ANativeWindow;

/// The screen's size in pixels: the activity's surface. Maximized windows take the new size.
void Win32Android_SetScreenSize(int width, int height);

/// The activity's surface, which is the screen every window is shown on (null: there is none).
/// The layer holds a reference to it while it is the screen's.
///
/// With null, the visible top-level windows are minimized, as Windows minimizes a full-screen
/// program's when the display is taken from it, and the next surface restores them. Android allows
/// no use of a surface after its surfaceDestroyed returns, so null waits until the surface taken
/// from the layer (below) is released, and says whether that came in time: two seconds, as the
/// activity's thread cannot wait for a program that is busy for longer.
BOOL Win32Android_SetScreenSurface(ANativeWindow* surface);

/// For what draws on the screen: the surface `window` is shown on, the screen's, as on Windows a
/// window is part of the desktop. Null while there is none or `window` is no window. The pointer
/// stays valid while it is the screen's surface; a Vulkan surface made from it holds a reference of
/// its own. The taker stands on the surface until it releases it, which it does as soon as it can
/// when the surface stops being the screen's.
ANativeWindow* Win32Android_TakeWindowSurface(HWND window);
BOOL Win32Android_TakenSurfaceIsCurrent();
void Win32Android_ReleaseTakenSurface();

/// Waits for the screen to have a surface: the program can start while the activity is in the back.
void Win32Android_WaitForScreenSurface();

/// What draws on the screen says that a frame went to it, and the host hears of the first one.
void Win32Android_FramePresented();
void Win32Android_SetFirstFrameHandler(void (*handler)());

/// The activity came to the front (TRUE) or went to the back (FALSE): the application's
/// activation (WM_ACTIVATEAPP, WM_ACTIVATE, the focus). Coming back, the window that was active
/// is activated again, as Windows does when the user switches to an application; before any was,
/// the top visible window, and before there is one, the first window shown.
void Win32Android_SetForeground(BOOL foreground);

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
