#pragma once

// The Android client build's <Windows.h>: the engine's POSIX Win32 layer
// (src/platform/posix/win32/Windows.h, which the simulation build uses as it is), the names more
// that the client calls, and at the end the message layer's USER API (WinUser.h). This directory
// comes ahead of the engine's on the include path; #include_next continues the search behind it.
//
// Additions only. If the engine's layer declares one of these names one day, the build stops at
// the duplicate, and the line goes here.

#include_next <Windows.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>

using USHORT = std::uint16_t;
using ULONG64 = std::uint64_t;
using COLORREF = DWORD;
using LPBYTE = BYTE*;
using PWSTR = wchar_t*;
// The render backends' cursor helper (gpg/gal/backends/common/WindowsCursor.h) keeps these.
using HCURSOR = void*;
using HICON = void*;

#define E_NOTIMPL (static_cast<HRESULT>(0x80004001u))
#define E_NOINTERFACE (static_cast<HRESULT>(0x80004002u))
#define ERROR_NO_MORE_FILES 18u
#define ERROR_NOT_FOUND 1168u

// --- Directory search ---------------------------------------------------------------------------
// FindFirstFileA as Win32 matches: `*` and `?` in the pattern's last part, letters in any case,
// "." and ".." included. Paths take either slash (src/win32/FindFileAndroid.cpp).

struct WIN32_FIND_DATAA
{
  DWORD dwFileAttributes;
  FILETIME ftCreationTime;
  FILETIME ftLastAccessTime;
  FILETIME ftLastWriteTime;
  DWORD nFileSizeHigh;
  DWORD nFileSizeLow;
  DWORD dwReserved0;
  DWORD dwReserved1;
  CHAR cFileName[MAX_PATH];
  CHAR cAlternateFileName[14];
};

HANDLE FindFirstFileA(const char* pattern, WIN32_FIND_DATAA* data);
BOOL FindNextFileA(HANDLE search, WIN32_FIND_DATAA* data);
BOOL FindClose(HANDLE search);

// --- Threads ------------------------------------------------------------------------------------
// The crash report suspends the other threads (moho/app/WinApp.cpp). A POSIX thread cannot be
// suspended from another thread: both calls fail with ERROR_NOT_SUPPORTED.

#define THREAD_SUSPEND_RESUME 0x0002u
DWORD SuspendThread(HANDLE thread);
DWORD ResumeThread(HANDLE thread);

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
// The base of COM, as the audio engine and DirectSound use it. The client's XACT engine is the one
// COM class here; CoCreateInstance fails for others as on Windows for a class that is not installed
// (src/win32/Win32Android.cpp).

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

/// The client's XACT engine; else fails with REGDB_E_CLASSNOTREG.
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

/// The wait for the handles or a message in the calling thread's queue (the message layer,
/// include/win32/WinUser.h): WAIT_OBJECT_0 + `count` for a message. With MWMO_INPUTAVAILABLE a
/// message already in the queue ends the wait at once; MWMO_WAITALL waits for all the handles.
/// MWMO_ALERTABLE has nothing to wake for: the engine's POSIX layer queues no APCs. An endless wait
/// on a queue that stays silent gets a WM_NULL, as Windows' system messages would end it.
#define QS_KEY 0x0001u
#define QS_MOUSEMOVE 0x0002u
#define QS_MOUSEBUTTON 0x0004u
#define QS_POSTMESSAGE 0x0008u
#define QS_TIMER 0x0010u
#define QS_PAINT 0x0020u
#define QS_SENDMESSAGE 0x0040u
#define QS_HOTKEY 0x0080u
#define QS_ALLPOSTMESSAGE 0x0100u
#define QS_RAWINPUT 0x0400u
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
BOOL GetModuleHandleExW(DWORD flags, const wchar_t* moduleName, HMODULE* module);
/// LoadLibraryA of the UTF-8 name: a plain dlopen (no Windows DLL exists here).
HMODULE LoadLibraryW(const wchar_t* fileName);
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

/// The CRT's _wcsicmp in the "C" locale, which the engine runs in: A to Z compare as a to z, and
/// nothing else changes case. The result is the difference of the first two that differ.
inline int _wcsicmp(const wchar_t* lhs, const wchar_t* rhs) noexcept
{
  const auto lower = [](const wchar_t c) { return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c; };
  wchar_t l;
  wchar_t r;
  do {
    l = lower(*lhs++);
    r = lower(*rhs++);
  } while (l != L'\0' && l == r);
  return static_cast<int>(l) - static_cast<int>(r);
}

// --- GDI fonts ---------------------------------------------------------------------------------
// What the font renderer (moho/render/d3d/CD3DFont.cpp) draws glyphs with: a font, a memory DC
// with a 32-bit bitmap, the font's metrics and kerning, DrawTextW, the bitmap's pixels. They render
// with FreeType, with GDI's metrics and glyph images (src/win32/GdiAndroid.cpp, GdiFonts.cpp).

using HGDIOBJ = void*;
using HBRUSH = void*;

#define BLACK_BRUSH 4

#define RGB(r, g, b) \
  (static_cast<COLORREF>(static_cast<BYTE>(r) | (static_cast<WORD>(static_cast<BYTE>(g)) << 8) | \
                         (static_cast<DWORD>(static_cast<BYTE>(b)) << 16)))
#define TMPF_TRUETYPE 0x04u

struct TEXTMETRICW
{
  LONG tmHeight;
  LONG tmAscent;
  LONG tmDescent;
  LONG tmInternalLeading;
  LONG tmExternalLeading;
  LONG tmAveCharWidth;
  LONG tmMaxCharWidth;
  LONG tmWeight;
  LONG tmOverhang;
  LONG tmDigitizedAspectX;
  LONG tmDigitizedAspectY;
  WCHAR tmFirstChar;
  WCHAR tmLastChar;
  WCHAR tmDefaultChar;
  WCHAR tmBreakChar;
  BYTE tmItalic;
  BYTE tmUnderlined;
  BYTE tmStruckOut;
  BYTE tmPitchAndFamily;
  BYTE tmCharSet;
};

struct KERNINGPAIR
{
  WORD wFirst;
  WORD wSecond;
  int iKernAmount;
};

/// Never null for a face name: as GDI it gives the closest face there is (src/win32/GdiFonts.h).
HFONT CreateFontA(
  int height, int width, int escapement, int orientation, int weight, DWORD italic, DWORD underline, DWORD strikeOut, DWORD charSet,
  DWORD outPrecision, DWORD clipPrecision, DWORD quality, DWORD pitchAndFamily, const char* faceName
);
BOOL DeleteObject(HGDIOBJ object);
HDC CreateCompatibleDC(HDC dc);
HBITMAP CreateBitmap(int width, int height, UINT planes, UINT bitsPerPixel, const void* bits);
HGDIOBJ SelectObject(HDC dc, HGDIOBJ object);
BOOL GetTextMetricsW(HDC dc, TEXTMETRICW* metrics);
DWORD GetKerningPairsW(HDC dc, DWORD pairCount, KERNINGPAIR* pairs);
int GetObjectW(HGDIOBJ object, int size, void* out);
LONG GetBitmapBits(HBITMAP bitmap, LONG size, void* bits);
BOOL GdiFlush();
UINT SetTextAlign(HDC dc, UINT align);
COLORREF SetBkColor(HDC dc, COLORREF color);
COLORREF SetTextColor(HDC dc, COLORREF color);
COLORREF GetPixel(HDC dc, int x, int y);
int DrawTextW(HDC dc, const wchar_t* text, int length, RECT* rect, UINT format);
HGDIOBJ GetStockObject(int object);
int FillRect(HDC dc, const RECT* rect, HBRUSH brush);
/// Fonts from memory (the game's own, CD3DDevice's REN_Init), for this process.
HANDLE AddFontMemResourceEx(void* fontData, DWORD size, void* reserved, DWORD* fontCount);

// --- Structured exceptions ----------------------------------------------------------------------
// The engine's crash filter (SetUnhandledExceptionFilter) runs on the signals of a fault: SIGSEGV,
// SIGBUS, SIGFPE, SIGILL and SIGTRAP, with the exception record Windows gives for it. The signal
// then goes on to the handler that was there before, so Android's debuggerd still writes its
// tombstone, whatever the filter answers (src/win32/ExceptionsAndroid.cpp).

#define EXCEPTION_MAXIMUM_PARAMETERS 15
struct EXCEPTION_RECORD
{
  DWORD ExceptionCode;
  DWORD ExceptionFlags;
  EXCEPTION_RECORD* ExceptionRecord;
  PVOID ExceptionAddress;
  DWORD NumberParameters;
  ULONG_PTR ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS];
};
using PEXCEPTION_RECORD = EXCEPTION_RECORD*;
/// The thread's state at the fault. Nothing in the engine reads it (moho::PLAT_GetCallStack takes
/// it as a pointer): its definition stays with the signal handler.
struct _CONTEXT;
using CONTEXT = _CONTEXT;
using PCONTEXT = CONTEXT*;
struct _EXCEPTION_POINTERS
{
  PEXCEPTION_RECORD ExceptionRecord;
  PCONTEXT ContextRecord;
};
using EXCEPTION_POINTERS = _EXCEPTION_POINTERS;
using PEXCEPTION_POINTERS = _EXCEPTION_POINTERS*;
using LPEXCEPTION_POINTERS = _EXCEPTION_POINTERS*;
using LPTOP_LEVEL_EXCEPTION_FILTER = LONG (*)(_EXCEPTION_POINTERS* exceptionInfo);

#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0
#define EXCEPTION_CONTINUE_EXECUTION (-1)
#define EXCEPTION_NONCONTINUABLE 0x1u

#define EXCEPTION_GUARD_PAGE 0x80000001u
#define EXCEPTION_DATATYPE_MISALIGNMENT 0x80000002u
#define EXCEPTION_BREAKPOINT 0x80000003u
#define EXCEPTION_SINGLE_STEP 0x80000004u
#define EXCEPTION_ACCESS_VIOLATION 0xC0000005u
#define EXCEPTION_IN_PAGE_ERROR 0xC0000006u
#define EXCEPTION_INVALID_HANDLE 0xC0000008u
#define EXCEPTION_ILLEGAL_INSTRUCTION 0xC000001Du
#define EXCEPTION_NONCONTINUABLE_EXCEPTION 0xC0000025u
#define EXCEPTION_INVALID_DISPOSITION 0xC0000026u
#define EXCEPTION_ARRAY_BOUNDS_EXCEEDED 0xC000008Cu
#define EXCEPTION_FLT_DENORMAL_OPERAND 0xC000008Du
#define EXCEPTION_FLT_DIVIDE_BY_ZERO 0xC000008Eu
#define EXCEPTION_FLT_INEXACT_RESULT 0xC000008Fu
#define EXCEPTION_FLT_INVALID_OPERATION 0xC0000090u
#define EXCEPTION_FLT_OVERFLOW 0xC0000091u
#define EXCEPTION_FLT_STACK_CHECK 0xC0000092u
#define EXCEPTION_FLT_UNDERFLOW 0xC0000093u
#define EXCEPTION_INT_DIVIDE_BY_ZERO 0xC0000094u
#define EXCEPTION_INT_OVERFLOW 0xC0000095u
#define EXCEPTION_PRIV_INSTRUCTION 0xC0000096u
#define EXCEPTION_STACK_OVERFLOW 0xC00000FDu

/// Installs `filter` for the faults of every thread and returns the one before (null at first).
LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER filter);

// --- Processes ----------------------------------------------------------------------------------
// An Android app starts no Windows programs: CreateProcess and ShellExecuteEx fail with
// ERROR_NOT_SUPPORTED (the engine starts dxdiag for a crash report, and GPGnet). ShellExecuteW's
// "open", with which the game opens a link, goes to the host's opener (Win32AndroidHost.h);
// without one, and for any other verb, it fails as for a file that nothing opens. TerminateProcess
// ends the calling process alone (GetCurrentProcess), with `exitCode`.

struct STARTUPINFOA
{
  DWORD cb;
  LPSTR lpReserved;
  LPSTR lpDesktop;
  LPSTR lpTitle;
  DWORD dwX;
  DWORD dwY;
  DWORD dwXSize;
  DWORD dwYSize;
  DWORD dwXCountChars;
  DWORD dwYCountChars;
  DWORD dwFillAttribute;
  DWORD dwFlags;
  WORD wShowWindow;
  WORD cbReserved2;
  LPBYTE lpReserved2;
  HANDLE hStdInput;
  HANDLE hStdOutput;
  HANDLE hStdError;
};
struct STARTUPINFOW
{
  DWORD cb;
  LPWSTR lpReserved;
  LPWSTR lpDesktop;
  LPWSTR lpTitle;
  DWORD dwX;
  DWORD dwY;
  DWORD dwXSize;
  DWORD dwYSize;
  DWORD dwXCountChars;
  DWORD dwYCountChars;
  DWORD dwFillAttribute;
  DWORD dwFlags;
  WORD wShowWindow;
  WORD cbReserved2;
  LPBYTE lpReserved2;
  HANDLE hStdInput;
  HANDLE hStdOutput;
  HANDLE hStdError;
};
struct PROCESS_INFORMATION
{
  HANDLE hProcess;
  HANDLE hThread;
  DWORD dwProcessId;
  DWORD dwThreadId;
};

BOOL CreateProcessA(
  const char* applicationName, char* commandLine, void* processAttributes, void* threadAttributes, BOOL inheritHandles,
  DWORD creationFlags, void* environment, const char* currentDirectory, STARTUPINFOA* startupInfo,
  PROCESS_INFORMATION* processInformation
);
BOOL CreateProcessW(
  const wchar_t* applicationName, wchar_t* commandLine, void* processAttributes, void* threadAttributes,
  BOOL inheritHandles, DWORD creationFlags, void* environment, const wchar_t* currentDirectory,
  STARTUPINFOW* startupInfo, PROCESS_INFORMATION* processInformation
);
BOOL TerminateProcess(HANDLE process, UINT exitCode);

// <shellapi.h>, which the engine's POSIX layer has include this header.
#define SE_ERR_NOASSOC 31
#define ERROR_NO_ASSOCIATION 1155u

/// Above 32 when `file` was opened; SE_ERR_NOASSOC (ERROR_NO_ASSOCIATION) when nothing opens it.
HINSTANCE ShellExecuteW(
  HWND window, const wchar_t* operation, const wchar_t* file, const wchar_t* parameters, const wchar_t* directory,
  int showCommand
);

#define SEE_MASK_NOCLOSEPROCESS 0x00000040u
struct SHELLEXECUTEINFOW
{
  DWORD cbSize;
  ULONG fMask;
  HWND hwnd;
  LPCWSTR lpVerb;
  LPCWSTR lpFile;
  LPCWSTR lpParameters;
  LPCWSTR lpDirectory;
  int nShow;
  HINSTANCE hInstApp;
  void* lpIDList;
  LPCWSTR lpClass;
  HKEY hkeyClass;
  DWORD dwHotKey;
  union
  {
    HANDLE hIcon;
    HANDLE hMonitor;
  };
  HANDLE hProcess;
};
BOOL ShellExecuteExW(SHELLEXECUTEINFOW* info);

// --- Registry (continued) ----------------------------------------------------------------------
// Keys cannot be created either: RegCreateKeyExA fails with ERROR_ACCESS_DENIED.

#define HKEY_CLASSES_ROOT (reinterpret_cast<HKEY>(static_cast<ULONG_PTR>(static_cast<LONG>(0x80000000u))))
#define HKEY_CURRENT_USER (reinterpret_cast<HKEY>(static_cast<ULONG_PTR>(static_cast<LONG>(0x80000001u))))
#define HKEY_USERS (reinterpret_cast<HKEY>(static_cast<ULONG_PTR>(static_cast<LONG>(0x80000003u))))
#define HKEY_PERFORMANCE_DATA (reinterpret_cast<HKEY>(static_cast<ULONG_PTR>(static_cast<LONG>(0x80000004u))))
#define HKEY_CURRENT_CONFIG (reinterpret_cast<HKEY>(static_cast<ULONG_PTR>(static_cast<LONG>(0x80000005u))))
#define REG_SZ 1u

LONG RegOpenKeyExA(HKEY key, const char* subKey, DWORD options, DWORD access, HKEY* result);
LONG RegQueryValueExA(HKEY key, const char* valueName, DWORD* reserved, DWORD* type, BYTE* data, DWORD* dataSize);
LONG RegCreateKeyExA(
  HKEY key, const char* subKey, DWORD reserved, char* className, DWORD options, DWORD access, void* security,
  HKEY* result, DWORD* disposition
);
LONG RegSetValueExA(HKEY key, const char* valueName, DWORD reserved, DWORD type, const BYTE* data, DWORD dataSize);

// --- Files (continued) -------------------------------------------------------------------------

union ULARGE_INTEGER
{
  struct
  {
    DWORD LowPart;
    DWORD HighPart;
  };
  ULONGLONG QuadPart;
};

struct WIN32_FIND_DATAW
{
  DWORD dwFileAttributes;
  FILETIME ftCreationTime;
  FILETIME ftLastAccessTime;
  FILETIME ftLastWriteTime;
  DWORD nFileSizeHigh;
  DWORD nFileSizeLow;
  DWORD dwReserved0;
  DWORD dwReserved1;
  WCHAR cFileName[MAX_PATH];
  WCHAR cAlternateFileName[14];
};

/// FindFirstFileA's search, with wide names.
HANDLE FindFirstFileW(const wchar_t* pattern, WIN32_FIND_DATAW* data);
BOOL FindNextFileW(HANDLE search, WIN32_FIND_DATAW* data);

/// Copies the file; with `failIfExists`, ERROR_FILE_EXISTS when the target is there.
BOOL CopyFileW(const wchar_t* existingFileName, const wchar_t* newFileName, BOOL failIfExists);

#define FILE_MAP_WRITE 0x0002u

/// A copy of the variable, or null when it is not set, as the UDP connector's packet log asks for %TEMP%.
using errno_t = int;
inline errno_t _dupenv_s(char** const value, std::size_t* const length, const char* const name) noexcept
{
  const char* const found = std::getenv(name);
  *value = found != nullptr ? ::strdup(found) : nullptr;
  if (length != nullptr) {
    *length = *value != nullptr ? std::strlen(*value) + 1 : 0;
  }
  return found != nullptr && *value == nullptr ? ENOMEM : 0;
}

/// The engine's platform check: Android is not the Windows 9x line.
#define VER_PLATFORM_WIN32_WINDOWS 1u

// The message layer: windows, messages, input, the clipboard.
#include <WinUser.h>
