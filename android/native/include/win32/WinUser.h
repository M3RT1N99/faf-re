#pragma once

// The message layer (android/native/WX_LITE.md): the part of Win32's USER the client and wx-lite
// use, on Android. Windows are objects of this layer, each with a window procedure, a rectangle,
// a parent, a style and a text; each thread that creates one has a message queue. Messages are
// posted, sent (across threads as well), retrieved and dispatched as Win32 does; WM_PAINT comes
// from a window's update region, WM_QUIT from PostQuitMessage. The Android host feeds the input
// (include/win32/Win32AndroidHost.h): it reaches the windows as Win32's input does, by capture,
// position and focus, and updates the key state when the message is taken from the queue.
//
// There is one screen: the activity's surface, whose size the host gives. Windows have no frame:
// a window's client area is the whole of its rectangle. Types, constants and structures have
// Win32's names, values and layouts. src/win32/User32Android.cpp implements it.
//
// Included by include/win32/Windows.h.

#include <cstdint>

// --- Types ------------------------------------------------------------------------------------

using WPARAM = UINT_PTR;
using LPARAM = LONG_PTR;
using LRESULT = LONG_PTR;
using ATOM = WORD;
using SHORT = std::int16_t;
using HMENU = struct HMENU__*;
using HHOOK = struct HHOOK__*;
using HMONITOR = struct HMONITOR__*;
using HGLOBAL = void*;
using PRECT = RECT*;
using LPRECT = RECT*;
using LPCRECT = const RECT*;
using LPPOINT = POINT*;

using WNDPROC = LRESULT (*)(HWND, UINT, WPARAM, LPARAM);
using DLGPROC = INT_PTR (*)(HWND, UINT, WPARAM, LPARAM);
using HOOKPROC = LRESULT (*)(int code, WPARAM wParam, LPARAM lParam);

// LOBYTE, HIBYTE and MAKEWORD come from the engine's platform/PosixCompat.h.
#define LOWORD(l) (static_cast<WORD>(static_cast<DWORD_PTR>(l) & 0xffff))
#define HIWORD(l) (static_cast<WORD>((static_cast<DWORD_PTR>(l) >> 16) & 0xffff))
#define MAKELONG(a, b) (static_cast<LONG>(static_cast<WORD>(a) | (static_cast<DWORD>(static_cast<WORD>(b)) << 16)))
#define MAKEWPARAM(l, h) (static_cast<WPARAM>(static_cast<DWORD>(MAKELONG(l, h))))
#define MAKELPARAM(l, h) (static_cast<LPARAM>(static_cast<DWORD>(MAKELONG(l, h))))
#define MAKELRESULT(l, h) (static_cast<LRESULT>(static_cast<DWORD>(MAKELONG(l, h))))
#define MAKEINTRESOURCEW(i) (reinterpret_cast<const wchar_t*>(static_cast<ULONG_PTR>(static_cast<WORD>(i))))
#define IS_INTRESOURCE(r) ((static_cast<ULONG_PTR>(reinterpret_cast<ULONG_PTR>(r)) >> 16) == 0)
// windowsx.h
#define GET_X_LPARAM(lp) (static_cast<int>(static_cast<short>(LOWORD(lp))))
#define GET_Y_LPARAM(lp) (static_cast<int>(static_cast<short>(HIWORD(lp))))
#define GET_WHEEL_DELTA_WPARAM(wParam) (static_cast<short>(HIWORD(wParam)))
#define GET_KEYSTATE_WPARAM(wParam) (LOWORD(wParam))
#define GET_XBUTTON_WPARAM(wParam) (HIWORD(wParam))
#define WHEEL_DELTA 120

struct tagMSG
{
  HWND hwnd;
  UINT message;
  WPARAM wParam;
  LPARAM lParam;
  DWORD time;
  POINT pt;
};
using MSG = tagMSG;
using LPMSG = MSG*;

struct WNDCLASSW
{
  UINT style;
  WNDPROC lpfnWndProc;
  int cbClsExtra;
  int cbWndExtra;
  HINSTANCE hInstance;
  HICON hIcon;
  HCURSOR hCursor;
  HBRUSH hbrBackground;
  const wchar_t* lpszMenuName;
  const wchar_t* lpszClassName;
};

struct CREATESTRUCTW
{
  LPVOID lpCreateParams;
  HINSTANCE hInstance;
  HMENU hMenu;
  HWND hwndParent;
  int cy;
  int cx;
  int y;
  int x;
  LONG style;
  const wchar_t* lpszName;
  const wchar_t* lpszClass;
  DWORD dwExStyle;
};

struct WINDOWPOS
{
  HWND hwnd;
  HWND hwndInsertAfter;
  int x;
  int y;
  int cx;
  int cy;
  UINT flags;
};

struct PAINTSTRUCT
{
  HDC hdc;
  BOOL fErase;
  RECT rcPaint;
  BOOL fRestore;
  BOOL fIncUpdate;
  BYTE rgbReserved[32];
};

struct KBDLLHOOKSTRUCT
{
  DWORD vkCode;
  DWORD scanCode;
  DWORD flags;
  DWORD time;
  ULONG_PTR dwExtraInfo;
};

struct MONITORINFO
{
  DWORD cbSize;
  RECT rcMonitor;
  RECT rcWork;
  DWORD dwFlags;
};

// --- Messages ---------------------------------------------------------------------------------

#define WM_NULL 0x0000u
#define WM_CREATE 0x0001u
#define WM_DESTROY 0x0002u
#define WM_MOVE 0x0003u
#define WM_SIZE 0x0005u
#define WM_ACTIVATE 0x0006u
#define WM_SETFOCUS 0x0007u
#define WM_KILLFOCUS 0x0008u
#define WM_ENABLE 0x000Au
#define WM_SETTEXT 0x000Cu
#define WM_GETTEXT 0x000Du
#define WM_GETTEXTLENGTH 0x000Eu
#define WM_PAINT 0x000Fu
#define WM_CLOSE 0x0010u
#define WM_QUERYENDSESSION 0x0011u
#define WM_QUIT 0x0012u
#define WM_ERASEBKGND 0x0014u
#define WM_ENDSESSION 0x0016u
#define WM_SHOWWINDOW 0x0018u
#define WM_ACTIVATEAPP 0x001Cu
#define WM_SETCURSOR 0x0020u
#define WM_MOUSEACTIVATE 0x0021u
#define WM_GETMINMAXINFO 0x0024u
#define WM_WINDOWPOSCHANGING 0x0046u
#define WM_WINDOWPOSCHANGED 0x0047u
#define WM_NCCREATE 0x0081u
#define WM_NCDESTROY 0x0082u
#define WM_NCHITTEST 0x0084u
#define WM_KEYFIRST 0x0100u
#define WM_KEYDOWN 0x0100u
#define WM_KEYUP 0x0101u
#define WM_CHAR 0x0102u
#define WM_DEADCHAR 0x0103u
#define WM_SYSKEYDOWN 0x0104u
#define WM_SYSKEYUP 0x0105u
#define WM_SYSCHAR 0x0106u
#define WM_SYSDEADCHAR 0x0107u
#define WM_KEYLAST 0x0109u
#define WM_INITDIALOG 0x0110u
#define WM_COMMAND 0x0111u
#define WM_SYSCOMMAND 0x0112u
#define WM_TIMER 0x0113u
#define WM_HSCROLL 0x0114u
#define WM_VSCROLL 0x0115u
#define WM_MOUSEFIRST 0x0200u
#define WM_MOUSEMOVE 0x0200u
#define WM_LBUTTONDOWN 0x0201u
#define WM_LBUTTONUP 0x0202u
#define WM_LBUTTONDBLCLK 0x0203u
#define WM_RBUTTONDOWN 0x0204u
#define WM_RBUTTONUP 0x0205u
#define WM_RBUTTONDBLCLK 0x0206u
#define WM_MBUTTONDOWN 0x0207u
#define WM_MBUTTONUP 0x0208u
#define WM_MBUTTONDBLCLK 0x0209u
#define WM_MOUSEWHEEL 0x020Au
#define WM_XBUTTONDOWN 0x020Bu
#define WM_XBUTTONUP 0x020Cu
#define WM_XBUTTONDBLCLK 0x020Du
#define WM_MOUSELAST 0x020Du
#define WM_CAPTURECHANGED 0x0215u
#define WM_ENTERSIZEMOVE 0x0231u
#define WM_EXITSIZEMOVE 0x0232u
#define WM_MOUSELEAVE 0x02A3u
#define WM_USER 0x0400u
#define WM_APP 0x8000u

// WM_ACTIVATE
#define WA_INACTIVE 0
#define WA_ACTIVE 1
#define WA_CLICKACTIVE 2

// WM_SIZE
#define SIZE_RESTORED 0
#define SIZE_MINIMIZED 1
#define SIZE_MAXIMIZED 2

// WM_NCHITTEST, WM_SETCURSOR
#define HTERROR (-2)
#define HTTRANSPARENT (-1)
#define HTNOWHERE 0
#define HTCLIENT 1
#define HTCAPTION 2

// WM_SYSCOMMAND
#define SC_SIZE 0xF000u
#define SC_MOVE 0xF010u
#define SC_MINIMIZE 0xF020u
#define SC_MAXIMIZE 0xF030u
#define SC_CLOSE 0xF060u
#define SC_KEYMENU 0xF100u
#define SC_RESTORE 0xF120u

// WM_HSCROLL, WM_VSCROLL
#define SB_LINEUP 0
#define SB_LINEDOWN 1
#define SB_PAGEUP 2
#define SB_PAGEDOWN 3
#define SB_THUMBPOSITION 4
#define SB_THUMBTRACK 5
#define SB_TOP 6
#define SB_BOTTOM 7
#define SB_ENDSCROLL 8

// The key state of the mouse messages' wParam
#define MK_LBUTTON 0x0001u
#define MK_RBUTTON 0x0002u
#define MK_SHIFT 0x0004u
#define MK_CONTROL 0x0008u
#define MK_MBUTTON 0x0010u
#define MK_XBUTTON1 0x0020u
#define MK_XBUTTON2 0x0040u
#define XBUTTON1 0x0001u
#define XBUTTON2 0x0002u

// PeekMessage
#define PM_NOREMOVE 0x0000u
#define PM_REMOVE 0x0001u
#define PM_NOYIELD 0x0002u

// --- Virtual keys -----------------------------------------------------------------------------

#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_CANCEL 0x03
#define VK_MBUTTON 0x04
#define VK_XBUTTON1 0x05
#define VK_XBUTTON2 0x06
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_CLEAR 0x0C
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_SELECT 0x29
#define VK_PRINT 0x2A
#define VK_EXECUTE 0x2B
#define VK_SNAPSHOT 0x2C
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_HELP 0x2F
#define VK_LWIN 0x5B
#define VK_RWIN 0x5C
#define VK_APPS 0x5D
#define VK_NUMPAD0 0x60
#define VK_NUMPAD1 0x61
#define VK_NUMPAD2 0x62
#define VK_NUMPAD3 0x63
#define VK_NUMPAD4 0x64
#define VK_NUMPAD5 0x65
#define VK_NUMPAD6 0x66
#define VK_NUMPAD7 0x67
#define VK_NUMPAD8 0x68
#define VK_NUMPAD9 0x69
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SEPARATOR 0x6C
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_F13 0x7C
#define VK_F14 0x7D
#define VK_F15 0x7E
#define VK_F16 0x7F
#define VK_F17 0x80
#define VK_F18 0x81
#define VK_F19 0x82
#define VK_F20 0x83
#define VK_F21 0x84
#define VK_F22 0x85
#define VK_F23 0x86
#define VK_F24 0x87
#define VK_NUMLOCK 0x90
#define VK_SCROLL 0x91
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define VK_OEM_1 0xBA
#define VK_OEM_PLUS 0xBB
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2 0xBF
#define VK_OEM_3 0xC0
#define VK_OEM_4 0xDB
#define VK_OEM_5 0xDC
#define VK_OEM_6 0xDD
#define VK_OEM_7 0xDE

// --- Window classes and windows ---------------------------------------------------------------

// Class styles
#define CS_VREDRAW 0x0001u
#define CS_HREDRAW 0x0002u
#define CS_DBLCLKS 0x0008u
#define CS_OWNDC 0x0020u
#define CS_CLASSDC 0x0040u
#define CS_PARENTDC 0x0080u
#define CS_NOCLOSE 0x0200u
#define CS_SAVEBITS 0x0800u

// Window styles
#define WS_OVERLAPPED 0x00000000L
#define WS_POPUP 0x80000000L
#define WS_CHILD 0x40000000L
#define WS_MINIMIZE 0x20000000L
#define WS_VISIBLE 0x10000000L
#define WS_DISABLED 0x08000000L
#define WS_CLIPSIBLINGS 0x04000000L
#define WS_CLIPCHILDREN 0x02000000L
#define WS_MAXIMIZE 0x01000000L
#define WS_CAPTION 0x00C00000L
#define WS_BORDER 0x00800000L
#define WS_DLGFRAME 0x00400000L
#define WS_VSCROLL 0x00200000L
#define WS_HSCROLL 0x00100000L
#define WS_SYSMENU 0x00080000L
#define WS_THICKFRAME 0x00040000L
#define WS_GROUP 0x00020000L
#define WS_TABSTOP 0x00010000L
#define WS_MINIMIZEBOX 0x00020000L
#define WS_MAXIMIZEBOX 0x00010000L
#define WS_OVERLAPPEDWINDOW \
  (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)
#define WS_POPUPWINDOW (WS_POPUP | WS_BORDER | WS_SYSMENU)

// Extended window styles
#define WS_EX_DLGMODALFRAME 0x00000001L
#define WS_EX_TOPMOST 0x00000008L
#define WS_EX_TRANSPARENT 0x00000020L
#define WS_EX_TOOLWINDOW 0x00000080L
#define WS_EX_WINDOWEDGE 0x00000100L
#define WS_EX_CLIENTEDGE 0x00000200L
#define WS_EX_CONTROLPARENT 0x00010000L
#define WS_EX_STATICEDGE 0x00020000L
#define WS_EX_APPWINDOW 0x00040000L

#define CW_USEDEFAULT (static_cast<int>(0x80000000))

// GetWindowLongPtr, SetWindowLongPtr
#define GWLP_WNDPROC (-4)
#define GWLP_HINSTANCE (-6)
#define GWLP_HWNDPARENT (-8)
#define GWLP_ID (-12)
#define GWL_STYLE (-16)
#define GWL_EXSTYLE (-20)
#define GWLP_USERDATA (-21)
// A dialog's own slots, at the start of its extra bytes (64-bit offsets)
#define DWLP_MSGRESULT 0
#define DWLP_DLGPROC (DWLP_MSGRESULT + static_cast<int>(sizeof(LRESULT)))
#define DWLP_USER (DWLP_DLGPROC + static_cast<int>(sizeof(DLGPROC)))

// GetAncestor
#define GA_PARENT 1
#define GA_ROOT 2
#define GA_ROOTOWNER 3

// ShowWindow
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_NORMAL 1
#define SW_SHOWMINIMIZED 2
#define SW_SHOWMAXIMIZED 3
#define SW_MAXIMIZE 3
#define SW_SHOWNOACTIVATE 4
#define SW_SHOW 5
#define SW_MINIMIZE 6
#define SW_SHOWMINNOACTIVE 7
#define SW_SHOWNA 8
#define SW_RESTORE 9
#define SW_SHOWDEFAULT 10

// SetWindowPos
#define SWP_NOSIZE 0x0001u
#define SWP_NOMOVE 0x0002u
#define SWP_NOZORDER 0x0004u
#define SWP_NOREDRAW 0x0008u
#define SWP_NOACTIVATE 0x0010u
#define SWP_FRAMECHANGED 0x0020u
#define SWP_SHOWWINDOW 0x0040u
#define SWP_HIDEWINDOW 0x0080u
#define SWP_NOOWNERZORDER 0x0200u
#define HWND_TOP (reinterpret_cast<HWND>(static_cast<LONG_PTR>(0)))
#define HWND_BOTTOM (reinterpret_cast<HWND>(static_cast<LONG_PTR>(1)))
#define HWND_TOPMOST (reinterpret_cast<HWND>(static_cast<LONG_PTR>(-1)))
#define HWND_NOTOPMOST (reinterpret_cast<HWND>(static_cast<LONG_PTR>(-2)))

// InvalidateRect et al.
#define RDW_INVALIDATE 0x0001u
#define RDW_ERASE 0x0004u
#define RDW_ALLCHILDREN 0x0080u
#define RDW_UPDATENOW 0x0100u

ATOM RegisterClassW(const WNDCLASSW* windowClass);
BOOL UnregisterClassW(const wchar_t* className, HINSTANCE instance);
BOOL GetClassInfoW(HINSTANCE instance, const wchar_t* className, WNDCLASSW* windowClass);

HWND CreateWindowExW(
  DWORD exStyle,
  const wchar_t* className,
  const wchar_t* windowName,
  DWORD style,
  int x,
  int y,
  int width,
  int height,
  HWND parent,
  HMENU menu,
  HINSTANCE instance,
  LPVOID param
);
/// Destroys `window` and its children: WM_DESTROY, then the children, then WM_NCDESTROY. On the
/// window's thread only.
BOOL DestroyWindow(HWND window);
BOOL IsWindow(HWND window);
BOOL IsChild(HWND parent, HWND window);
HWND GetParent(HWND window);
HWND SetParent(HWND child, HWND newParent);
HWND GetAncestor(HWND window, UINT flags);
DWORD GetWindowThreadProcessId(HWND window, DWORD* processId);

LONG_PTR GetWindowLongPtrW(HWND window, int index);
LONG_PTR SetWindowLongPtrW(HWND window, int index, LONG_PTR value);
LONG GetWindowLongW(HWND window, int index);
LONG SetWindowLongW(HWND window, int index, LONG value);

BOOL IsWindowVisible(HWND window);
BOOL IsWindowEnabled(HWND window);
BOOL EnableWindow(HWND window, BOOL enable);
/// Shows, hides, minimizes, maximizes or restores `window`; whether it was visible before.
BOOL ShowWindow(HWND window, int command);
BOOL IsIconic(HWND window);
BOOL IsZoomed(HWND window);
BOOL MoveWindow(HWND window, int x, int y, int width, int height, BOOL repaint);
BOOL SetWindowPos(HWND window, HWND insertAfter, int x, int y, int width, int height, UINT flags);
BOOL BringWindowToTop(HWND window);

/// The window's rectangle on the screen. A null window fails, as on Windows.
BOOL GetWindowRect(HWND window, RECT* rect);
/// (0, 0, width, height): a window has no frame here, its client area is all of it.
BOOL GetClientRect(HWND window, RECT* rect);
/// The window rectangle for a client rectangle: the same, without a frame.
BOOL AdjustWindowRectEx(RECT* rect, DWORD style, BOOL menu, DWORD exStyle);
BOOL ClientToScreen(HWND window, POINT* point);
BOOL ScreenToClient(HWND window, POINT* point);
int MapWindowPoints(HWND from, HWND to, POINT* points, UINT count);
/// The deepest visible, enabled window at the screen point, top of the z-order first.
HWND WindowFromPoint(POINT point);

BOOL SetWindowTextW(HWND window, const wchar_t* text);
/// The text as UTF-8 (the engine's narrow strings are).
BOOL SetWindowTextA(HWND window, const char* text);
int GetWindowTextW(HWND window, wchar_t* text, int maxCount);
int GetWindowTextLengthW(HWND window);

// --- Painting ---------------------------------------------------------------------------------
// A window's update region is a rectangle here; while it is not empty, the window's thread gets
// WM_PAINT when its queue has nothing else. BeginPaint sends WM_ERASEBKGND if the region asks for
// it and validates the region. A device context of this layer names its window and draws
// nothing: Android draws the game with the renderer only.

BOOL InvalidateRect(HWND window, const RECT* rect, BOOL erase);
BOOL ValidateRect(HWND window, const RECT* rect);
BOOL GetUpdateRect(HWND window, RECT* rect, BOOL erase);
BOOL UpdateWindow(HWND window);
BOOL RedrawWindow(HWND window, const RECT* rect, void* region, UINT flags);
HDC BeginPaint(HWND window, PAINTSTRUCT* paint);
BOOL EndPaint(HWND window, const PAINTSTRUCT* paint);
HDC GetDC(HWND window);
int ReleaseDC(HWND window, HDC dc);
/// The window of a device context from BeginPaint or GetDC.
HWND WindowFromDC(HDC dc);

// --- Messages ---------------------------------------------------------------------------------

/// Takes the next message for `window` (or any of the thread's) in the range, without waiting:
/// sent messages are processed first, then the posted messages come in order, then WM_QUIT, then
/// WM_PAINT. Taking an input message updates the key state (GetKeyState).
BOOL PeekMessageW(MSG* message, HWND window, UINT filterMin, UINT filterMax, UINT remove);
/// As PeekMessageW with PM_REMOVE, waiting for a message: FALSE for WM_QUIT, -1 on error.
BOOL GetMessageW(MSG* message, HWND window, UINT filterMin, UINT filterMax);
/// Queues the message for the window's thread (a null window: the calling thread's).
BOOL PostMessageW(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
BOOL PostThreadMessageW(DWORD threadId, UINT message, WPARAM wParam, LPARAM lParam);
/// Calls the window procedure, on the window's thread: from another thread, the call waits until
/// that thread takes it (in PeekMessageW or GetMessageW).
LRESULT SendMessageW(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
/// SendMessageW without the wait: another thread's window takes it as a sent message.
BOOL SendNotifyMessageW(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
/// WM_CHAR for a WM_KEYDOWN that types a character (the host gives it with the key), WM_SYSCHAR
/// for a WM_SYSKEYDOWN; TRUE if it queued one.
BOOL TranslateMessage(const MSG* message);
LRESULT DispatchMessageW(const MSG* message);
void PostQuitMessage(int exitCode);
LRESULT DefWindowProcW(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CallWindowProcW(WNDPROC procedure, HWND window, UINT message, WPARAM wParam, LPARAM lParam);
/// What the host gave with the message taken last: for a finger's mouse message, these bits, which
/// Windows documents but its headers do not define.
#define MOUSEEVENTF_FROMTOUCH 0xFF515700
LPARAM GetMessageExtraInfo();
LONG GetMessageTime();
DWORD GetMessagePos();
BOOL InSendMessage();

// --- Keyboard and mouse -----------------------------------------------------------------------

/// The window that gets the keyboard input: WM_KILLFOCUS to the old one, WM_SETFOCUS to it.
HWND SetFocus(HWND window);
HWND GetFocus();
HWND SetActiveWindow(HWND window);
HWND GetActiveWindow();
/// The active window while the activity is in front (Win32Android_SetForeground), else null.
HWND GetForegroundWindow();
/// Activates the top-level window of `window` when the activity is in front; FALSE otherwise.
BOOL SetForegroundWindow(HWND window);

/// The key's state as of the last input message taken from the queue: the high bit while down,
/// the low bit toggled by each press (Caps Lock, Num Lock, Scroll Lock).
SHORT GetKeyState(int virtualKey);
/// The key's state now.
SHORT GetAsyncKeyState(int virtualKey);

/// The window that gets the mouse input wherever the pointer is.
HWND SetCapture(HWND window);
BOOL ReleaseCapture();
HWND GetCapture();

BOOL GetCursorPos(POINT* point);
/// Moves the pointer (within the clip rectangle), as the engine's mouse scrub does. The pointer
/// the host reports next replaces it: Android has no call to move the system pointer.
BOOL SetCursorPos(int x, int y);
HCURSOR SetCursor(HCURSOR cursor);
HCURSOR GetCursor();
int ShowCursor(BOOL show);
/// Keeps the pointer in `rect` (null: anywhere), as far as this layer moves it.
BOOL ClipCursor(const RECT* rect);
BOOL GetClipCursor(RECT* rect);
UINT GetDoubleClickTime();

// --- Hooks ------------------------------------------------------------------------------------
// WH_KEYBOARD_LL: the hooks see each key the host reports, on the thread that takes it from the
// queue, before the window does; a hook that returns nonzero swallows the key.

#define WH_KEYBOARD_LL 13
#define HC_ACTION 0
#define LLKHF_EXTENDED 0x00000001u
#define LLKHF_INJECTED 0x00000010u
#define LLKHF_ALTDOWN 0x00000020u
#define LLKHF_UP 0x00000080u

HHOOK SetWindowsHookExW(int hookId, HOOKPROC procedure, HINSTANCE module, DWORD threadId);
BOOL UnhookWindowsHookEx(HHOOK hook);
LRESULT CallNextHookEx(HHOOK hook, int code, WPARAM wParam, LPARAM lParam);

// --- The screen, menus, system settings -------------------------------------------------------

#define MONITOR_DEFAULTTONULL 0x00000000u
#define MONITOR_DEFAULTTOPRIMARY 0x00000001u
#define MONITOR_DEFAULTTONEAREST 0x00000002u
#define MONITORINFOF_PRIMARY 0x00000001u

/// The one monitor: the activity's surface (MONITOR_DEFAULTTONULL: only for a point on it).
HMONITOR MonitorFromPoint(POINT point, DWORD flags);
HMONITOR MonitorFromWindow(HWND window, DWORD flags);
BOOL GetMonitorInfoW(HMONITOR monitor, MONITORINFO* info);

#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define SM_CXDOUBLECLK 36
#define SM_CYDOUBLECLK 37
#define SM_CMONITORS 80
int GetSystemMetrics(int index);

/// Android has no screen saver (the host keeps the screen on while the game runs):
/// SPI_GETSCREENSAVEACTIVE reads FALSE, SPI_SETSCREENSAVEACTIVE succeeds and changes nothing.
#define SPI_GETSCREENSAVEACTIVE 0x0010u
#define SPI_SETSCREENSAVEACTIVE 0x0011u
BOOL SystemParametersInfoW(UINT action, UINT uiParam, void* pvParam, UINT winIni);

/// A window has no system menu here: GetSystemMenu is null, and a menu call on null fails with
/// ERROR_INVALID_MENU_HANDLE.
#define MF_STRING 0x00000000u
#define MF_SEPARATOR 0x00000800u
#define ERROR_INVALID_MENU_HANDLE 1401u
HMENU GetSystemMenu(HWND window, BOOL revert);
BOOL AppendMenuW(HMENU menu, UINT flags, UINT_PTR newItem, const wchar_t* newItemText);

// --- Message boxes and dialogs ----------------------------------------------------------------
// Android shows no Win32 dialog. A message box writes its caption and text to the system log
// (logcat, tag "faf") and fails (0, ERROR_NOT_SUPPORTED): nobody could answer it, and a caller
// that asks a question takes no answer for no. There are no dialog resources: DialogBoxParamW
// fails with ERROR_RESOURCE_NAME_NOT_FOUND.

#define MB_OK 0x00000000u
#define MB_OKCANCEL 0x00000001u
#define MB_YESNOCANCEL 0x00000003u
#define MB_YESNO 0x00000004u
#define MB_ICONHAND 0x00000010u
#define MB_ICONQUESTION 0x00000020u
#define MB_ICONEXCLAMATION 0x00000030u
#define MB_ICONASTERISK 0x00000040u
#define MB_ICONERROR MB_ICONHAND
#define MB_ICONWARNING MB_ICONEXCLAMATION
#define MB_ICONINFORMATION MB_ICONASTERISK
#define MB_TASKMODAL 0x00002000u
#define MB_SETFOREGROUND 0x00010000u
#define MB_TOPMOST 0x00040000u
#define IDOK 1
#define IDCANCEL 2
#define IDABORT 3
#define IDRETRY 4
#define IDIGNORE 5
#define IDYES 6
#define IDNO 7
#define ERROR_RESOURCE_NAME_NOT_FOUND 1814u
#define ERROR_INVALID_WINDOW_HANDLE 1400u

int MessageBoxW(HWND owner, const wchar_t* text, const wchar_t* caption, UINT type);
int MessageBoxA(HWND owner, const char* text, const char* caption, UINT type);
INT_PTR DialogBoxParamW(HINSTANCE instance, const wchar_t* templateName, HWND parent, DLGPROC procedure, LPARAM initParam);
BOOL EndDialog(HWND dialog, INT_PTR result);
/// The child of `dialog` (any window) with the id `id` (GWLP_ID).
HWND GetDlgItem(HWND dialog, int id);
BOOL SetDlgItemTextW(HWND dialog, int id, const wchar_t* text);

// --- Clipboard --------------------------------------------------------------------------------
// The clipboard of this process: what the game copies it can paste. The host does not exchange
// it with Android's clipboard yet. Data is global memory (GlobalAlloc), which the clipboard owns
// once SetClipboardData took it.

#define CF_TEXT 1u
#define CF_UNICODETEXT 13u
#define GMEM_FIXED 0x0000u
#define GMEM_MOVEABLE 0x0002u
#define GMEM_ZEROINIT 0x0040u
#define GHND (GMEM_MOVEABLE | GMEM_ZEROINIT)
#define GPTR (GMEM_FIXED | GMEM_ZEROINIT)

HGLOBAL GlobalAlloc(UINT flags, SIZE_T bytes);
LPVOID GlobalLock(HGLOBAL memory);
BOOL GlobalUnlock(HGLOBAL memory);
HGLOBAL GlobalFree(HGLOBAL memory);
SIZE_T GlobalSize(HGLOBAL memory);

BOOL OpenClipboard(HWND owner);
BOOL CloseClipboard();
BOOL EmptyClipboard();
HANDLE SetClipboardData(UINT format, HANDLE data);
HANDLE GetClipboardData(UINT format);
BOOL IsClipboardFormatAvailable(UINT format);
