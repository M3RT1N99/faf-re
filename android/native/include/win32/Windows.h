#pragma once

// The Android client build's <Windows.h>: the engine's POSIX Win32 layer
// (src/platform/posix/win32/Windows.h, which the simulation build uses as it is), and the few
// names more that the client's render code calls. This directory comes ahead of the engine's on
// the include path; #include_next continues the search behind it.
//
// Additions only. If the engine's layer declares one of these names one day, the build stops at
// the duplicate, and the line goes here.

#include_next <Windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>

using USHORT = std::uint16_t;
using COLORREF = DWORD;
using LPBYTE = BYTE*;
using PWSTR = wchar_t*;
// The render backends' cursor helper (gpg/gal/backends/common/WindowsCursor.h) keeps these.
using HCURSOR = void*;
using HICON = void*;

#define E_NOTIMPL (static_cast<HRESULT>(0x80004001u))
#define E_NOINTERFACE (static_cast<HRESULT>(0x80004002u))

/// An asynchronous I/O request (the XACT file callbacks name it), with Win32's layout.
struct OVERLAPPED
{
  ULONG_PTR Internal;
  ULONG_PTR InternalHigh;
  union
  {
    struct
    {
      DWORD Offset;
      DWORD OffsetHigh;
    };
    PVOID Pointer;
  };
  HANDLE hEvent;
};

// --- COM ----------------------------------------------------------------------------------------
// The base of COM, as the audio engine and DirectSound use it. No COM class is registered here:
// CoCreateInstance fails as on Windows for a class that is not installed (src/win32/Win32Android.cpp).

struct GUID
{
  DWORD Data1;
  WORD Data2;
  WORD Data3;
  BYTE Data4[8];
};
using IID = GUID;
using CLSID = GUID;
using REFIID = const IID&;
using REFCLSID = const CLSID&;
using LPCGUID = const GUID*;

inline bool operator==(const GUID& a, const GUID& b) noexcept
{
  return a.Data1 == b.Data1 && a.Data2 == b.Data2 && a.Data3 == b.Data3 &&
    std::equal(std::begin(a.Data4), std::end(a.Data4), std::begin(b.Data4));
}

inline bool operator!=(const GUID& a, const GUID& b) noexcept
{
  return !(a == b);
}

struct IUnknown
{
  virtual HRESULT QueryInterface(REFIID iid, void** object) = 0;
  virtual ULONG AddRef() = 0;
  virtual ULONG Release() = 0;

protected:
  ~IUnknown() = default;
};
using LPUNKNOWN = IUnknown*;

#define CLSCTX_INPROC_SERVER 0x1u
#define REGDB_E_CLASSNOTREG (static_cast<HRESULT>(0x80040154u))

/// Fails with REGDB_E_CLASSNOTREG: no COM class is installed on Android.
HRESULT CoCreateInstance(REFCLSID clsid, LPUNKNOWN outer, DWORD context, REFIID iid, void** object);

// --- Registry -----------------------------------------------------------------------------------
// Android has no registry: every key is missing, as a key that was never written is on Windows.

using HKEY = struct HKEY__*;
#define HKEY_LOCAL_MACHINE (reinterpret_cast<HKEY>(static_cast<ULONG_PTR>(static_cast<LONG>(0x80000002u))))
#define REG_DWORD 4u

/// ERROR_FILE_NOT_FOUND: there is no key.
LONG RegOpenKeyExW(HKEY key, const wchar_t* subKey, DWORD options, DWORD access, HKEY* result);
/// ERROR_FILE_NOT_FOUND: there is no value.
LONG RegQueryValueExW(HKEY key, const wchar_t* valueName, DWORD* reserved, DWORD* type, BYTE* data, DWORD* dataSize);
LONG RegCloseKey(HKEY key);

/// -1, 0 or 1 as `a` is earlier than, equal to or later than `b`.
inline LONG CompareFileTime(const FILETIME* const a, const FILETIME* const b) noexcept
{
  const std::uint64_t left = (std::uint64_t{a->dwHighDateTime} << 32) | a->dwLowDateTime;
  const std::uint64_t right = (std::uint64_t{b->dwHighDateTime} << 32) | b->dwLowDateTime;
  return left < right ? -1 : (left > right ? 1 : 0);
}

// The bitmap file and DIB headers, as data: the render backends write screenshots as .bmp files
// and build cursors from them. Layouts as wingdi.h declares them.
#define BI_RGB 0L
#define BI_BITFIELDS 3L
#define DIB_RGB_COLORS 0u
#pragma pack(push, 2)
struct BITMAPFILEHEADER
{
  WORD bfType;
  DWORD bfSize;
  WORD bfReserved1;
  WORD bfReserved2;
  DWORD bfOffBits;
};
#pragma pack(pop)
struct BITMAPINFOHEADER
{
  DWORD biSize;
  LONG biWidth;
  LONG biHeight;
  WORD biPlanes;
  WORD biBitCount;
  DWORD biCompression;
  DWORD biSizeImage;
  LONG biXPelsPerMeter;
  LONG biYPelsPerMeter;
  DWORD biClrUsed;
  DWORD biClrImportant;
};
struct RGBQUAD
{
  BYTE rgbBlue;
  BYTE rgbGreen;
  BYTE rgbRed;
  BYTE rgbReserved;
};
struct BITMAPINFO
{
  BITMAPINFOHEADER bmiHeader;
  RGBQUAD bmiColors[1];
};
struct CIEXYZ
{
  LONG ciexyzX;
  LONG ciexyzY;
  LONG ciexyzZ;
};
struct CIEXYZTRIPLE
{
  CIEXYZ ciexyzRed;
  CIEXYZ ciexyzGreen;
  CIEXYZ ciexyzBlue;
};
struct BITMAPV5HEADER
{
  DWORD bV5Size;
  LONG bV5Width;
  LONG bV5Height;
  WORD bV5Planes;
  WORD bV5BitCount;
  DWORD bV5Compression;
  DWORD bV5SizeImage;
  LONG bV5XPelsPerMeter;
  LONG bV5YPelsPerMeter;
  DWORD bV5ClrUsed;
  DWORD bV5ClrImportant;
  DWORD bV5RedMask;
  DWORD bV5GreenMask;
  DWORD bV5BlueMask;
  DWORD bV5AlphaMask;
  DWORD bV5CSType;
  CIEXYZTRIPLE bV5Endpoints;
  DWORD bV5GammaRed;
  DWORD bV5GammaGreen;
  DWORD bV5GammaBlue;
  DWORD bV5Intent;
  DWORD bV5ProfileData;
  DWORD bV5ProfileSize;
  DWORD bV5Reserved;
};
struct BITMAP
{
  LONG bmType;
  LONG bmWidth;
  LONG bmHeight;
  LONG bmWidthBytes;
  WORD bmPlanes;
  WORD bmBitsPixel;
  void* bmBits;
};

/// GetCommandLineW's line, as UTF-8.
const char* GetCommandLineA();

/// No message queue here: the wait for the handles alone (MWMO_WAITALL waits for all).
#define QS_ALLINPUT 0x04FFu
#define MWMO_WAITALL 0x0001u
#define MWMO_ALERTABLE 0x0002u
#define MWMO_INPUTAVAILABLE 0x0004u
DWORD MsgWaitForMultipleObjectsEx(DWORD count, const HANDLE* handles, DWORD milliseconds, DWORD wakeMask, DWORD flags);

/// The executable's path for a null module, else the path of the shared object at `module`.
DWORD GetModuleFileNameA(HMODULE module, char* fileName, DWORD size);
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x00000002u
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x00000004u
/// With GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS only: the shared object holding the address.
BOOL GetModuleHandleExA(DWORD flags, const char* moduleName, HMODULE* module);
/// Up to `framesToCapture` return addresses, after `framesToSkip` of the caller's own.
USHORT RtlCaptureStackBackTrace(DWORD framesToSkip, DWORD framesToCapture, void** backTrace, DWORD* backTraceHash);
/// True while a tracer (a debugger) is attached (/proc/self/status TracerPid).
BOOL IsDebuggerPresent();
/// Stops in the attached debugger (SIGTRAP).
inline void DebugBreak() noexcept
{
  __builtin_debugtrap();
}

#define SHGFP_TYPE_CURRENT 0
/// SHGetFolderPathW's folders, as UTF-8.
HRESULT SHGetFolderPathA(HWND owner, int folder, HANDLE token, DWORD flags, char* path);
/// `mkdir -p`: ERROR_SUCCESS, ERROR_ALREADY_EXISTS when it was there, else the error.
int SHCreateDirectoryExA(HWND window, const char* path, const void* security);
BOOL MoveFileExA(const char* from, const char* to, DWORD flags);

/// The CRT's getenv_s: `*requiredSize` is the value's size with its NUL (0 when unset); ERANGE
/// when `buffer` is too small for it.
int getenv_s(std::size_t* requiredSize, char* buffer, std::size_t bufferSize, const char* name) noexcept;

// --- Windows ---------------------------------------------------------------------------------
// An HWND of the engine is an ANativeWindow* here: the surface an Android activity hands over
// (android/native/README.md). The client area is the window's size in pixels.

/// The size of the native window `window`, as Win32 gives a window's client area (0, 0, w, h).
BOOL GetClientRect(HWND window, RECT* rect);
/// The window the Android host last put in front with SetForegroundWindow; null while the app is
/// in the background.
HWND GetForegroundWindow();
/// The Android host's call when an activity's window comes to the front (null: the app went to
/// the background).
BOOL SetForegroundWindow(HWND window);
