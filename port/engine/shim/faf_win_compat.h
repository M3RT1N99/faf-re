#pragma once

// The Win32 surface that the engine (src/sdk) uses, for the non-Windows targets
// of the port: Android arm64 first (docs/port/android-roadmap.md, W1.2), and
// x86_64 clang for host-side checks.
//
// On Windows, platform/Platform.h includes <winsock2.h>, <ws2tcpip.h> and
// <windows.h> (WIN32_LEAN_AND_MEAN, NOMINMAX), and most of the tree takes its
// Win32 names from there without including anything itself. Off Windows,
// Platform.h includes <windows.h> when the include path has one, and the
// Android engine build puts this directory first on it (port/engine/README.md).
// The forwarding headers next to this file (windows.h, winsock2.h, ws2tcpip.h,
// intrin.h, mmsystem.h) all land here, so the engine's own #includes of those
// names resolve here too. The MSVC CRT names (_stricmp, sprintf_s, ...) are in
// faf_msvc_crt.h, which the shim's C headers include as MSVC's do; this header
// includes it as well, since <windows.h> brings the CRT headers with it.
//
// What it holds, and what it does not:
// - Types keep their Win32 widths, not the LP64 ones. Windows is LLP64: DWORD,
//   LONG, ULONG, BOOL and HRESULT are 32-bit, LONGLONG is `__int64`. Here DWORD is
//   uint32_t and LONG int32_t, so a structure built from them has the Win32
//   layout. `long` itself is 8 bytes on LP64 and is not touched; see the
//   Interlocked section for the one place where that shows.
// - Functions are implemented only where the meaning is exact and small: time
//   (QueryPerformanceCounter, GetTickCount, Sleep), thread ids, the interlocked
//   family, critical sections, debug output, the environment, Winsock
//   spellings of BSD socket calls. File, thread, event, window, GDI, Direct3D
//   and DirectSound APIs are not here. They are the platform layer (W2) and the
//   renderer (W3); a TU that needs them still fails to compile, and the sweep
//   lists it under the missing name.
// - API functions have C linkage, as windows.h declares them. Overloads that
//   Windows does not have (the `volatile long*` Interlocked forms, Winsock's
//   `int*` lengths beside POSIX's `socklen_t*`) are C++.
// - WCHAR is wchar_t, which is 4 bytes (UTF-32) on Android and 2 (UTF-16) on
//   Windows. L"" literals and std::wstring keep working; anything that writes
//   wchar_t to a file or the wire does not (W1.4).
//
// This file must never be reachable from a Windows build: it would shadow the
// real SDK headers. main.vcxproj does not reference port/engine/shim.

#if defined(_WIN32) || defined(_MSC_VER)
#error "port/engine/shim is for non-Windows targets; it must not be on a Windows include path"
#endif

#if !defined(__cplusplus)
#error "faf_win_compat.h is C++ only (the engine compiles no C translation units)"
#endif

// clang declares the MSVC intrinsics (_InterlockedIncrement, _ReturnAddress,
// __debugbreak, _BitScanForward, _alloca, ...) as builtins under -fms-extensions
// on every target, with MSVC's operand widths. Everything below relies on that.
#if !defined(__clang__) || !__has_builtin(_InterlockedExchangeAdd)
#error "faf_win_compat.h needs clang with -fms-extensions (port/engine/compile_flags.txt)"
#endif

#include "faf_msvc_crt.h"

#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#if defined(__ANDROID__)
#include <android/log.h>
#endif
#if defined(__i386__) || defined(__x86_64__)
#include <immintrin.h>
#endif

// A check that survives platform/X64LayoutAsserts.h, which turns every
// `static_assert` into a no-op on non-x86 targets.
#define FAF_COMPAT_CHECK(condition, message) _Static_assert((condition), message)

extern "C++" {

// ---------------------------------------------------------------------------
// Declaration and calling-convention macros (minwindef.h, winnt.h)
// ---------------------------------------------------------------------------
// __stdcall, __cdecl and __fastcall are clang keywords under -fms-extensions;
// non-x86 targets ignore them with -Wignored-attributes, as MSVC does on x64.
#define WINAPI
#define WINAPIV
#define APIENTRY
#define CALLBACK
#define NTAPI
#define STDMETHODCALLTYPE
#define CONST const
#define VOID void

// ---------------------------------------------------------------------------
// Scalar types, with Win32 (LLP64) widths
// ---------------------------------------------------------------------------
typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef int32_t BOOL;
typedef uint8_t BOOLEAN;
typedef char CHAR;
typedef unsigned char UCHAR;
typedef int16_t SHORT;
typedef uint16_t USHORT;
typedef int INT;
typedef unsigned int UINT;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef float FLOAT;
typedef int32_t INT32;
typedef uint32_t UINT32;
typedef uint32_t DWORD32;
// `__int64` is `long long` under -fms-extensions, as on MSVC. int64_t is `long`
// on LP64, so LONGLONG* and int64_t* are different types here, as they are
// for unsigned long* and uint32_t* on Windows.
typedef __int64 LONGLONG;
typedef unsigned __int64 ULONGLONG;
typedef __int64 INT64;
typedef unsigned __int64 UINT64;
typedef __int64 LONG64;
typedef unsigned __int64 ULONG64;
typedef unsigned __int64 DWORD64;
typedef unsigned __int64 DWORDLONG;
typedef intptr_t INT_PTR;
typedef uintptr_t UINT_PTR;
typedef intptr_t LONG_PTR;
typedef uintptr_t ULONG_PTR;
typedef ULONG_PTR DWORD_PTR;
typedef ULONG_PTR SIZE_T;
typedef LONG_PTR SSIZE_T;
typedef LONG HRESULT;
typedef WORD ATOM;
typedef DWORD COLORREF;
typedef DWORD LCID;
typedef WORD LANGID;
typedef UINT_PTR WPARAM;
typedef LONG_PTR LPARAM;
typedef LONG_PTR LRESULT;

typedef wchar_t WCHAR;
#if defined(UNICODE)
typedef WCHAR TCHAR;
#define TEXT(text) L##text
#else
typedef char TCHAR;
#define TEXT(text) text
#endif

typedef void* PVOID;
typedef void* LPVOID;
typedef const void* LPCVOID;
typedef BYTE* PBYTE;
typedef BYTE* LPBYTE;
typedef WORD* PWORD;
typedef WORD* LPWORD;
typedef DWORD* PDWORD;
typedef DWORD* LPDWORD;
typedef LONG* PLONG;
typedef LONG* LPLONG;
typedef ULONG* PULONG;
typedef BOOL* PBOOL;
typedef BOOL* LPBOOL;
typedef INT* PINT;
typedef INT* LPINT;
typedef UINT* PUINT;
typedef CHAR* PCHAR;
typedef CHAR* PSTR;
typedef CHAR* LPSTR;
typedef const CHAR* PCSTR;
typedef const CHAR* LPCSTR;
typedef WCHAR* PWCHAR;
typedef WCHAR* PWSTR;
typedef WCHAR* LPWSTR;
typedef const WCHAR* PCWSTR;
typedef const WCHAR* LPCWSTR;
typedef TCHAR* LPTSTR;
typedef const TCHAR* LPCTSTR;

FAF_COMPAT_CHECK(sizeof(DWORD) == 4 && sizeof(LONG) == 4 && sizeof(BOOL) == 4, "Win32 32-bit types");
FAF_COMPAT_CHECK(sizeof(LONGLONG) == 8 && sizeof(DWORD_PTR) == sizeof(void*), "Win32 64-bit and pointer types");

// ---------------------------------------------------------------------------
// Handles (winnt.h, STRICT): distinct pointer types, so overloads on them stay
// distinct as on Windows. Values are opaque here; nothing in this header
// creates one.
// ---------------------------------------------------------------------------
#define DECLARE_HANDLE(name) \
  struct name##__ {          \
    int unused;              \
  };                         \
  typedef struct name##__* name

typedef void* HANDLE;
typedef HANDLE* PHANDLE;
typedef HANDLE* LPHANDLE;
typedef HANDLE HGLOBAL;
typedef HANDLE HLOCAL;
typedef void* HGDIOBJ;
DECLARE_HANDLE(HINSTANCE);
typedef HINSTANCE HMODULE;
DECLARE_HANDLE(HWND);
DECLARE_HANDLE(HDC);
DECLARE_HANDLE(HGLRC);
DECLARE_HANDLE(HFONT);
DECLARE_HANDLE(HBITMAP);
DECLARE_HANDLE(HBRUSH);
DECLARE_HANDLE(HPEN);
DECLARE_HANDLE(HRGN);
DECLARE_HANDLE(HPALETTE);
DECLARE_HANDLE(HICON);
typedef HICON HCURSOR;
DECLARE_HANDLE(HMENU);
DECLARE_HANDLE(HKEY);
DECLARE_HANDLE(HMONITOR);
typedef int (*FARPROC)();

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#define MAX_PATH 260
#define INFINITE 0xFFFFFFFF
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)

#define WAIT_OBJECT_0 0x00000000L
#define WAIT_ABANDONED 0x00000080L
#define WAIT_TIMEOUT 0x00000102L
#define WAIT_FAILED ((DWORD)0xFFFFFFFF)

// Standard access rights (winnt.h); moho/core/Thread.h:20 spells THREAD_ALL_ACCESS
// with them.
#define SYNCHRONIZE 0x00100000L
#define STANDARD_RIGHTS_REQUIRED 0x000F0000L

#define ERROR_SUCCESS 0L
#define NO_ERROR 0L
#define ERROR_ENVVAR_NOT_FOUND 203L

#define S_OK ((HRESULT)0L)
#define S_FALSE ((HRESULT)1L)
#define E_NOTIMPL ((HRESULT)0x80004001L)
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#define E_POINTER ((HRESULT)0x80004003L)
#define E_ABORT ((HRESULT)0x80004004L)
#define E_FAIL ((HRESULT)0x80004005L)
#define E_UNEXPECTED ((HRESULT)0x8000FFFFL)
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#define E_INVALIDARG ((HRESULT)0x80070057L)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)

// ---------------------------------------------------------------------------
// Byte and word macros (minwindef.h), with the SDK's exact definitions. The
// casts through DWORD_PTR truncate the same way on 32- and 64-bit targets.
// ---------------------------------------------------------------------------
#define MAKEWORD(a, b) ((WORD)(((BYTE)(((DWORD_PTR)(a)) & 0xff)) | ((WORD)((BYTE)(((DWORD_PTR)(b)) & 0xff))) << 8))
#define MAKELONG(a, b) ((LONG)(((WORD)(((DWORD_PTR)(a)) & 0xffff)) | ((DWORD)((WORD)(((DWORD_PTR)(b)) & 0xffff))) << 16))
#define LOWORD(l) ((WORD)(((DWORD_PTR)(l)) & 0xffff))
#define HIWORD(l) ((WORD)((((DWORD_PTR)(l)) >> 16) & 0xffff))
#define LOBYTE(w) ((BYTE)(((DWORD_PTR)(w)) & 0xff))
#define HIBYTE(w) ((BYTE)((((DWORD_PTR)(w)) >> 8) & 0xff))
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16)))

// ---------------------------------------------------------------------------
// Structures (winnt.h, minwinbase.h, windef.h), Win32 layout
// ---------------------------------------------------------------------------
typedef union _LARGE_INTEGER {
  struct {
    DWORD LowPart;
    LONG HighPart;
  };
  struct {
    DWORD LowPart;
    LONG HighPart;
  } u;
  LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef union _ULARGE_INTEGER {
  struct {
    DWORD LowPart;
    DWORD HighPart;
  };
  struct {
    DWORD LowPart;
    DWORD HighPart;
  } u;
  ULONGLONG QuadPart;
} ULARGE_INTEGER, *PULARGE_INTEGER;

typedef struct _FILETIME {
  DWORD dwLowDateTime;
  DWORD dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;

typedef struct _SYSTEMTIME {
  WORD wYear;
  WORD wMonth;
  WORD wDayOfWeek;
  WORD wDay;
  WORD wHour;
  WORD wMinute;
  WORD wSecond;
  WORD wMilliseconds;
} SYSTEMTIME, *PSYSTEMTIME, *LPSYSTEMTIME;

typedef struct tagRECT {
  LONG left;
  LONG top;
  LONG right;
  LONG bottom;
} RECT, *PRECT, *LPRECT;
typedef const RECT* LPCRECT;

typedef struct tagPOINT {
  LONG x;
  LONG y;
} POINT, *PPOINT, *LPPOINT;

typedef struct tagSIZE {
  LONG cx;
  LONG cy;
} SIZE, *PSIZE, *LPSIZE;

FAF_COMPAT_CHECK(sizeof(LARGE_INTEGER) == 8 && sizeof(FILETIME) == 8, "Win32 LARGE_INTEGER/FILETIME layout");
FAF_COMPAT_CHECK(sizeof(SYSTEMTIME) == 16 && sizeof(RECT) == 16 && sizeof(POINT) == 8, "Win32 SYSTEMTIME/RECT/POINT layout");

// ---------------------------------------------------------------------------
// Time, threads, debug output (kernel32, winmm)
// ---------------------------------------------------------------------------
namespace faf_compat
{
  inline ULONGLONG MonotonicNanoseconds() noexcept
  {
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<ULONGLONG>(now.tv_sec) * 1000000000ull + static_cast<ULONGLONG>(now.tv_nsec);
  }

  // Win32 last-error value: per thread, separate from errno.
  inline DWORD& LastErrorSlot() noexcept
  {
    static thread_local DWORD lastError = 0;
    return lastError;
  }
} // namespace faf_compat

extern "C" {

// The performance counter runs at 10 MHz, the frequency Windows 10 and later
// report on almost every machine, so tick arithmetic in the engine (ticks *
// 1000 / frequency, ...) sees the magnitudes it sees on Windows.
inline BOOL QueryPerformanceFrequency(LARGE_INTEGER* frequency) noexcept
{
  frequency->QuadPart = 10000000;
  return TRUE;
}

inline BOOL QueryPerformanceCounter(LARGE_INTEGER* counter) noexcept
{
  counter->QuadPart = static_cast<LONGLONG>(faf_compat::MonotonicNanoseconds() / 100u);
  return TRUE;
}

// Milliseconds since an arbitrary start, wrapping at 2^32 like GetTickCount.
inline DWORD GetTickCount() noexcept
{
  return static_cast<DWORD>(faf_compat::MonotonicNanoseconds() / 1000000u);
}

inline ULONGLONG GetTickCount64() noexcept
{
  return faf_compat::MonotonicNanoseconds() / 1000000u;
}

// winmm's timeGetTime counts the same milliseconds.
inline DWORD timeGetTime() noexcept
{
  return GetTickCount();
}

// Sleep(0) gives up the rest of the time slice; any other value sleeps at least
// that long (INFINITE sleeps for 49.7 days, which is as good as forever here).
inline void Sleep(DWORD milliseconds) noexcept
{
  if (milliseconds == 0) {
    sched_yield();
    return;
  }
  timespec remaining;
  remaining.tv_sec = static_cast<time_t>(milliseconds / 1000u);
  remaining.tv_nsec = static_cast<long>(milliseconds % 1000u) * 1000000L;
  while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {
  }
}

inline BOOL SwitchToThread() noexcept
{
  return sched_yield() == 0 ? TRUE : FALSE;
}

inline DWORD GetCurrentThreadId() noexcept
{
  return static_cast<DWORD>(gettid());
}

inline DWORD GetCurrentProcessId() noexcept
{
  return static_cast<DWORD>(getpid());
}

// Pseudo-handles, the constants kernel32 returns. What they are passed to
// (SetThreadPriority, ...) belongs to the platform layer (W2).
inline HANDLE GetCurrentProcess() noexcept
{
  return reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(-1));
}

inline HANDLE GetCurrentThread() noexcept
{
  return reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(-2));
}

inline void DebugBreak() noexcept
{
  __debugbreak();
}

inline DWORD GetLastError() noexcept
{
  return faf_compat::LastErrorSlot();
}

inline void SetLastError(DWORD errorCode) noexcept
{
  faf_compat::LastErrorSlot() = errorCode;
}

// The process environment (kernel32). GetEnvironmentVariableA returns the
// length copied without the terminator, or the size needed with it when the
// buffer is too small, or 0 with ERROR_ENVVAR_NOT_FOUND.
inline DWORD GetEnvironmentVariableA(LPCSTR name, LPSTR buffer, DWORD size) noexcept
{
  const char* const value = (name != nullptr) ? getenv(name) : nullptr;
  if (value == nullptr) {
    SetLastError(ERROR_ENVVAR_NOT_FOUND);
    return 0;
  }
  const size_t length = strlen(value);
  if (buffer == nullptr || length >= size) {
    return static_cast<DWORD>(length + 1u);
  }
  memcpy(buffer, value, length + 1u);
  return static_cast<DWORD>(length);
}

// A null value removes the variable, as on Windows.
inline BOOL SetEnvironmentVariableA(LPCSTR name, LPCSTR value) noexcept
{
  if (name == nullptr) {
    return FALSE;
  }
  const int result = (value != nullptr) ? setenv(name, value, 1) : unsetenv(name);
  return result == 0 ? TRUE : FALSE;
}

// Debug output goes to logcat (tag "faf") on Android and to stderr elsewhere.
inline void OutputDebugStringA(LPCSTR text) noexcept
{
  if (text == nullptr) {
    return;
  }
#if defined(__ANDROID__)
  __android_log_write(ANDROID_LOG_DEBUG, "faf", text);
#else
  fputs(text, stderr);
#endif
}

inline void OutputDebugStringW(LPCWSTR text) noexcept
{
  if (text == nullptr) {
    return;
  }
  char narrow[1024];
  size_t length = 0;
  for (; text[length] != L'\0' && length + 1 < sizeof(narrow); ++length) {
    const wchar_t ch = text[length];
    narrow[length] = (ch >= 0 && ch < 0x80) ? static_cast<char>(ch) : '?';
  }
  narrow[length] = '\0';
  OutputDebugStringA(narrow);
}

} // extern "C"

#if defined(UNICODE)
#define OutputDebugString OutputDebugStringW
#else
#define OutputDebugString OutputDebugStringA
#endif

// ---------------------------------------------------------------------------
// FILETIME and SYSTEMTIME (kernel32): 100-ns intervals since 1601-01-01 UTC,
// converted with the proleptic Gregorian calendar as Windows does.
// ---------------------------------------------------------------------------
namespace faf_compat
{
  // 1601-01-01 to 1970-01-01 in 100-ns intervals.
  constexpr ULONGLONG kFileTimeUnixEpoch = 116444736000000000ull;
  constexpr ULONGLONG kFileTimeTicksPerSecond = 10000000ull;
  constexpr LONGLONG kSecondsPerDay = 86400;

  inline ULONGLONG FileTimeToTicks(const FILETIME& fileTime) noexcept
  {
    return (static_cast<ULONGLONG>(fileTime.dwHighDateTime) << 32) | fileTime.dwLowDateTime;
  }

  inline FILETIME TicksToFileTime(const ULONGLONG ticks) noexcept
  {
    FILETIME fileTime;
    fileTime.dwLowDateTime = static_cast<DWORD>(ticks & 0xFFFFFFFFull);
    fileTime.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
    return fileTime;
  }

  // Days since 1970-01-01 of a civil date, and back (H. Hinnant's algorithms;
  // exact for every date a FILETIME can hold).
  inline LONGLONG DaysFromCivil(LONGLONG year, const unsigned month, const unsigned day) noexcept
  {
    year -= month <= 2 ? 1 : 0;
    const LONGLONG era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
    const unsigned dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return era * 146097 + static_cast<LONGLONG>(dayOfEra) - 719468;
  }

  inline void CivilFromDays(LONGLONG days, LONGLONG& year, unsigned& month, unsigned& day) noexcept
  {
    days += 719468;
    const LONGLONG era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned dayOfEra = static_cast<unsigned>(days - era * 146097);
    const unsigned yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    const unsigned dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const unsigned monthIndex = (5 * dayOfYear + 2) / 153;
    day = dayOfYear - (153 * monthIndex + 2) / 5 + 1;
    month = monthIndex < 10 ? monthIndex + 3 : monthIndex - 9;
    year = static_cast<LONGLONG>(yearOfEra) + era * 400 + (month <= 2 ? 1 : 0);
  }

  inline bool IsLeapYear(const unsigned year) noexcept
  {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  }

  // The current offset of local time from UTC, in seconds, daylight saving
  // included. FileTimeToLocalFileTime uses the current offset whatever the date,
  // and so does this.
  inline LONGLONG CurrentLocalOffsetSeconds() noexcept
  {
    const time_t now = time(nullptr);
    struct tm local;
    localtime_r(&now, &local);
    return static_cast<LONGLONG>(local.tm_gmtoff);
  }

  inline ULONGLONG RealtimeFileTimeTicks() noexcept
  {
    timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    return kFileTimeUnixEpoch + static_cast<ULONGLONG>(now.tv_sec) * kFileTimeTicksPerSecond +
      static_cast<ULONGLONG>(now.tv_nsec) / 100u;
  }
} // namespace faf_compat

extern "C" {

inline void GetSystemTimeAsFileTime(LPFILETIME fileTime) noexcept
{
  *fileTime = faf_compat::TicksToFileTime(faf_compat::RealtimeFileTimeTicks());
}

inline BOOL FileTimeToSystemTime(const FILETIME* fileTime, LPSYSTEMTIME systemTime) noexcept
{
  const ULONGLONG ticks = faf_compat::FileTimeToTicks(*fileTime);
  if (ticks > 0x7FFFFFFFFFFFFFFFull) {
    return FALSE;
  }
  const LONGLONG seconds = static_cast<LONGLONG>(ticks / faf_compat::kFileTimeTicksPerSecond);
  const LONGLONG unixSeconds = seconds - static_cast<LONGLONG>(faf_compat::kFileTimeUnixEpoch / faf_compat::kFileTimeTicksPerSecond);
  LONGLONG days = unixSeconds / faf_compat::kSecondsPerDay;
  LONGLONG secondOfDay = unixSeconds % faf_compat::kSecondsPerDay;
  if (secondOfDay < 0) {
    secondOfDay += faf_compat::kSecondsPerDay;
    --days;
  }
  LONGLONG year = 0;
  unsigned month = 0;
  unsigned day = 0;
  faf_compat::CivilFromDays(days, year, month, day);
  systemTime->wYear = static_cast<WORD>(year);
  systemTime->wMonth = static_cast<WORD>(month);
  systemTime->wDay = static_cast<WORD>(day);
  // 1970-01-01 was a Thursday (4); Sunday is 0.
  systemTime->wDayOfWeek = static_cast<WORD>(((days % 7) + 11) % 7);
  systemTime->wHour = static_cast<WORD>(secondOfDay / 3600);
  systemTime->wMinute = static_cast<WORD>((secondOfDay / 60) % 60);
  systemTime->wSecond = static_cast<WORD>(secondOfDay % 60);
  systemTime->wMilliseconds = static_cast<WORD>((ticks % faf_compat::kFileTimeTicksPerSecond) / 10000u);
  return TRUE;
}

inline BOOL SystemTimeToFileTime(const SYSTEMTIME* systemTime, LPFILETIME fileTime) noexcept
{
  static const unsigned char kDaysInMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const unsigned year = systemTime->wYear;
  const unsigned month = systemTime->wMonth;
  if (year < 1601 || year > 30827 || month < 1 || month > 12 || systemTime->wDay < 1 ||
      systemTime->wDay > kDaysInMonth[month - 1] + ((month == 2 && faf_compat::IsLeapYear(year)) ? 1 : 0) ||
      systemTime->wHour > 23 || systemTime->wMinute > 59 || systemTime->wSecond > 59 ||
      systemTime->wMilliseconds > 999) {
    return FALSE;
  }
  const LONGLONG days = faf_compat::DaysFromCivil(year, month, systemTime->wDay);
  const LONGLONG unixSeconds = days * faf_compat::kSecondsPerDay + systemTime->wHour * 3600 +
    systemTime->wMinute * 60 + systemTime->wSecond;
  const LONGLONG ticks = (unixSeconds * static_cast<LONGLONG>(faf_compat::kFileTimeTicksPerSecond)) +
    static_cast<LONGLONG>(faf_compat::kFileTimeUnixEpoch) + systemTime->wMilliseconds * 10000LL;
  *fileTime = faf_compat::TicksToFileTime(static_cast<ULONGLONG>(ticks));
  return TRUE;
}

inline BOOL FileTimeToLocalFileTime(const FILETIME* fileTime, LPFILETIME localFileTime) noexcept
{
  const LONGLONG offset = faf_compat::CurrentLocalOffsetSeconds() * static_cast<LONGLONG>(faf_compat::kFileTimeTicksPerSecond);
  *localFileTime = faf_compat::TicksToFileTime(faf_compat::FileTimeToTicks(*fileTime) + static_cast<ULONGLONG>(offset));
  return TRUE;
}

inline BOOL LocalFileTimeToFileTime(const FILETIME* localFileTime, LPFILETIME fileTime) noexcept
{
  const LONGLONG offset = faf_compat::CurrentLocalOffsetSeconds() * static_cast<LONGLONG>(faf_compat::kFileTimeTicksPerSecond);
  *fileTime = faf_compat::TicksToFileTime(faf_compat::FileTimeToTicks(*localFileTime) - static_cast<ULONGLONG>(offset));
  return TRUE;
}

inline LONG CompareFileTime(const FILETIME* first, const FILETIME* second) noexcept
{
  const ULONGLONG a = faf_compat::FileTimeToTicks(*first);
  const ULONGLONG b = faf_compat::FileTimeToTicks(*second);
  return a < b ? -1 : (a > b ? 1 : 0);
}

inline void GetSystemTime(LPSYSTEMTIME systemTime) noexcept
{
  const FILETIME now = faf_compat::TicksToFileTime(faf_compat::RealtimeFileTimeTicks());
  (void)FileTimeToSystemTime(&now, systemTime);
}

inline void GetLocalTime(LPSYSTEMTIME systemTime) noexcept
{
  const FILETIME now = faf_compat::TicksToFileTime(faf_compat::RealtimeFileTimeTicks());
  FILETIME local;
  (void)FileTimeToLocalFileTime(&now, &local);
  (void)FileTimeToSystemTime(&local, systemTime);
}

// MS-DOS date and time, as zip directories store them: no time zone.
inline BOOL DosDateTimeToFileTime(WORD dosDate, WORD dosTime, LPFILETIME fileTime) noexcept
{
  SYSTEMTIME systemTime;
  systemTime.wYear = static_cast<WORD>(1980 + (dosDate >> 9));
  systemTime.wMonth = static_cast<WORD>((dosDate >> 5) & 0x0F);
  systemTime.wDay = static_cast<WORD>(dosDate & 0x1F);
  systemTime.wDayOfWeek = 0;
  systemTime.wHour = static_cast<WORD>(dosTime >> 11);
  systemTime.wMinute = static_cast<WORD>((dosTime >> 5) & 0x3F);
  systemTime.wSecond = static_cast<WORD>((dosTime & 0x1F) * 2);
  systemTime.wMilliseconds = 0;
  return SystemTimeToFileTime(&systemTime, fileTime);
}

inline BOOL FileTimeToDosDateTime(const FILETIME* fileTime, LPWORD dosDate, LPWORD dosTime) noexcept
{
  SYSTEMTIME systemTime;
  if (!FileTimeToSystemTime(fileTime, &systemTime) || systemTime.wYear < 1980 || systemTime.wYear > 2107) {
    return FALSE;
  }
  *dosDate = static_cast<WORD>(((systemTime.wYear - 1980) << 9) | (systemTime.wMonth << 5) | systemTime.wDay);
  *dosTime = static_cast<WORD>((systemTime.wHour << 11) | (systemTime.wMinute << 5) | (systemTime.wSecond / 2));
  return TRUE;
}

} // extern "C"

// ---------------------------------------------------------------------------
// CRITICAL_SECTION: a recursive mutex owned by one thread at a time, which is
// what a recursive pthread mutex is. The structure is not the Win32 one; the
// engine never reads its fields.
// ---------------------------------------------------------------------------
typedef struct _RTL_CRITICAL_SECTION {
  pthread_mutex_t mutex;
} CRITICAL_SECTION, *PCRITICAL_SECTION, *LPCRITICAL_SECTION;

extern "C" {

inline void InitializeCriticalSection(LPCRITICAL_SECTION criticalSection) noexcept
{
  pthread_mutexattr_t attributes;
  pthread_mutexattr_init(&attributes);
  pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&criticalSection->mutex, &attributes);
  pthread_mutexattr_destroy(&attributes);
}

inline BOOL InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION criticalSection, DWORD) noexcept
{
  InitializeCriticalSection(criticalSection);
  return TRUE;
}

inline void EnterCriticalSection(LPCRITICAL_SECTION criticalSection) noexcept
{
  pthread_mutex_lock(&criticalSection->mutex);
}

inline BOOL TryEnterCriticalSection(LPCRITICAL_SECTION criticalSection) noexcept
{
  return pthread_mutex_trylock(&criticalSection->mutex) == 0 ? TRUE : FALSE;
}

inline void LeaveCriticalSection(LPCRITICAL_SECTION criticalSection) noexcept
{
  pthread_mutex_unlock(&criticalSection->mutex);
}

inline void DeleteCriticalSection(LPCRITICAL_SECTION criticalSection) noexcept
{
  pthread_mutex_destroy(&criticalSection->mutex);
}

} // extern "C"

// ---------------------------------------------------------------------------
// Interlocked family (winnt.h maps the API names onto the intrinsics)
// ---------------------------------------------------------------------------
// clang's builtins take `int` where MSVC's take `long` (32-bit on LLP64), and
// `long long` for the 64-bit forms: the Win32 widths. The recovered code also
// passes `volatile long*`, mostly casts of 32-bit fields
// (reinterpret_cast<volatile long*>(&mRefCount)), because `long` is 32-bit on
// Windows. These overloads keep that meaning: they operate on the 4 bytes at the
// address, exactly what the Win32 and x64 builds do. The 32-bit atomic helper in
// src/sdk/platform replaces the casts (W1.2); an object that really is an
// 8-byte `long` must not be passed here.
#if __SIZEOF_LONG__ == 8
namespace faf_compat
{
  inline volatile int* AsLong32(volatile long* target) noexcept
  {
    return reinterpret_cast<volatile int*>(target);
  }
} // namespace faf_compat

inline long _InterlockedIncrement(volatile long* addend) noexcept
{
  return _InterlockedIncrement(faf_compat::AsLong32(addend));
}

inline long _InterlockedDecrement(volatile long* addend) noexcept
{
  return _InterlockedDecrement(faf_compat::AsLong32(addend));
}

inline long _InterlockedExchange(volatile long* target, long value) noexcept
{
  return _InterlockedExchange(faf_compat::AsLong32(target), static_cast<int>(value));
}

inline long _InterlockedExchangeAdd(volatile long* addend, long value) noexcept
{
  return _InterlockedExchangeAdd(faf_compat::AsLong32(addend), static_cast<int>(value));
}

inline long _InterlockedCompareExchange(volatile long* destination, long exchange, long comparand) noexcept
{
  return _InterlockedCompareExchange(
    faf_compat::AsLong32(destination), static_cast<int>(exchange), static_cast<int>(comparand)
  );
}

inline long _InterlockedAnd(volatile long* target, long value) noexcept
{
  return _InterlockedAnd(faf_compat::AsLong32(target), static_cast<int>(value));
}

inline long _InterlockedOr(volatile long* target, long value) noexcept
{
  return _InterlockedOr(faf_compat::AsLong32(target), static_cast<int>(value));
}

inline long _InterlockedXor(volatile long* target, long value) noexcept
{
  return _InterlockedXor(faf_compat::AsLong32(target), static_cast<int>(value));
}
#endif

#define InterlockedIncrement _InterlockedIncrement
#define InterlockedDecrement _InterlockedDecrement
#define InterlockedExchange _InterlockedExchange
#define InterlockedExchangeAdd _InterlockedExchangeAdd
#define InterlockedCompareExchange _InterlockedCompareExchange
#define InterlockedAnd _InterlockedAnd
#define InterlockedOr _InterlockedOr
#define InterlockedXor _InterlockedXor
#define InterlockedCompareExchange64 _InterlockedCompareExchange64
#define InterlockedExchangePointer _InterlockedExchangePointer
#define InterlockedCompareExchangePointer _InterlockedCompareExchangePointer

// ---------------------------------------------------------------------------
// <intrin.h>: MSVC intrinsics that are not clang builtins on this target
// ---------------------------------------------------------------------------
#if defined(__aarch64__) || defined(__arm__)
// PAUSE in a spin loop; YIELD is the ARM hint for the same thing. (On x86 it
// comes from <immintrin.h>, included above.)
extern "C" inline void _mm_pause() noexcept
{
  __asm__ __volatile__("yield" ::: "memory");
}
#endif

#if __SIZEOF_LONG__ == 8
// The bit-scan builtins take `unsigned int` (MSVC: unsigned long, 32-bit).
inline unsigned char _BitScanForward(unsigned long* index, unsigned long mask) noexcept
{
  unsigned int position = 0;
  const unsigned char found = _BitScanForward(&position, static_cast<unsigned int>(mask));
  *index = position;
  return found;
}

inline unsigned char _BitScanReverse(unsigned long* index, unsigned long mask) noexcept
{
  unsigned int position = 0;
  const unsigned char found = _BitScanReverse(&position, static_cast<unsigned int>(mask));
  *index = position;
  return found;
}
#endif

// ---------------------------------------------------------------------------
// Winsock names over BSD sockets
// ---------------------------------------------------------------------------
// SOCKET is UINT_PTR on Windows; POSIX descriptors are int, and INVALID_SOCKET
// compares the same way against them.
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define SD_RECEIVE SHUT_RD
#define SD_SEND SHUT_WR
#define SD_BOTH SHUT_RDWR

typedef struct sockaddr SOCKADDR, *PSOCKADDR, *LPSOCKADDR;
typedef struct sockaddr_in SOCKADDR_IN, *PSOCKADDR_IN, *LPSOCKADDR_IN;
typedef struct sockaddr_storage SOCKADDR_STORAGE;
typedef struct addrinfo ADDRINFOA, *PADDRINFOA;
typedef struct hostent HOSTENT;

extern "C" inline int closesocket(SOCKET socketHandle) noexcept
{
  return close(socketHandle);
}

// Winsock passes address lengths as int*, POSIX as socklen_t*. These overloads
// carry the length in and out; the POSIX functions stay visible beside them.
inline int getsockname(SOCKET socketHandle, sockaddr* name, int* nameLength) noexcept
{
  socklen_t length = static_cast<socklen_t>(*nameLength);
  const int result = getsockname(socketHandle, name, &length);
  *nameLength = static_cast<int>(length);
  return result;
}

inline int getpeername(SOCKET socketHandle, sockaddr* name, int* nameLength) noexcept
{
  socklen_t length = static_cast<socklen_t>(*nameLength);
  const int result = getpeername(socketHandle, name, &length);
  *nameLength = static_cast<int>(length);
  return result;
}

inline SOCKET accept(SOCKET socketHandle, sockaddr* address, int* addressLength) noexcept
{
  socklen_t length = static_cast<socklen_t>(*addressLength);
  const SOCKET result = accept(socketHandle, address, &length);
  *addressLength = static_cast<int>(length);
  return result;
}

inline int recvfrom(SOCKET socketHandle, char* buffer, int size, int flags, sockaddr* from, int* fromLength) noexcept
{
  socklen_t length = static_cast<socklen_t>(*fromLength);
  const int result =
    static_cast<int>(recvfrom(socketHandle, buffer, static_cast<size_t>(size), flags, from, &length));
  *fromLength = static_cast<int>(length);
  return result;
}

inline int getsockopt(SOCKET socketHandle, int level, int name, char* value, int* valueLength) noexcept
{
  socklen_t length = static_cast<socklen_t>(*valueLength);
  const int result = getsockopt(socketHandle, level, name, value, &length);
  *valueLength = static_cast<int>(length);
  return result;
}

// The argument is u_long on Windows (32-bit); ioctl reads and writes an int.
inline int ioctlsocket(SOCKET socketHandle, long command, u_long* argument) noexcept
{
  int value = static_cast<int>(*argument);
  const int result = ioctl(socketHandle, static_cast<int>(command), &value);
  *argument = static_cast<u_long>(static_cast<unsigned int>(value));
  return result;
}

extern "C" {

inline int WSAGetLastError() noexcept
{
  return errno;
}

inline void WSASetLastError(int errorCode) noexcept
{
  errno = errorCode;
}

} // extern "C"

// The WSAE* codes are errno values here, so WSAGetLastError() compares as on
// Windows. Note that EWOULDBLOCK == EAGAIN on Linux.
#define WSAEINTR EINTR
#define WSAEBADF EBADF
#define WSAEACCES EACCES
#define WSAEFAULT EFAULT
#define WSAEINVAL EINVAL
#define WSAEMFILE EMFILE
#define WSAEWOULDBLOCK EWOULDBLOCK
#define WSAEINPROGRESS EINPROGRESS
#define WSAEALREADY EALREADY
#define WSAENOTSOCK ENOTSOCK
#define WSAEDESTADDRREQ EDESTADDRREQ
#define WSAEMSGSIZE EMSGSIZE
#define WSAEPROTOTYPE EPROTOTYPE
#define WSAENOPROTOOPT ENOPROTOOPT
#define WSAEPROTONOSUPPORT EPROTONOSUPPORT
#define WSAEOPNOTSUPP EOPNOTSUPP
#define WSAEAFNOSUPPORT EAFNOSUPPORT
#define WSAEADDRINUSE EADDRINUSE
#define WSAEADDRNOTAVAIL EADDRNOTAVAIL
#define WSAENETDOWN ENETDOWN
#define WSAENETUNREACH ENETUNREACH
#define WSAENETRESET ENETRESET
#define WSAECONNABORTED ECONNABORTED
#define WSAECONNRESET ECONNRESET
#define WSAENOBUFS ENOBUFS
#define WSAEISCONN EISCONN
#define WSAENOTCONN ENOTCONN
#define WSAESHUTDOWN ESHUTDOWN
#define WSAETIMEDOUT ETIMEDOUT
#define WSAECONNREFUSED ECONNREFUSED
#define WSAEHOSTDOWN EHOSTDOWN
#define WSAEHOSTUNREACH EHOSTUNREACH

// Winsock needs no start-up on POSIX.
typedef struct WSAData {
  WORD wVersion;
  WORD wHighVersion;
  char szDescription[257];
  char szSystemStatus[129];
  unsigned short iMaxSockets;
  unsigned short iMaxUdpDg;
  char* lpVendorInfo;
} WSADATA, *LPWSADATA;

extern "C" {

inline int WSAStartup(WORD versionRequested, LPWSADATA data) noexcept
{
  if (data != nullptr) {
    memset(data, 0, sizeof(*data));
    data->wVersion = versionRequested;
    data->wHighVersion = versionRequested;
  }
  return 0;
}

inline int WSACleanup() noexcept
{
  return 0;
}

} // extern "C"

// ---------------------------------------------------------------------------
// Direct3D 9 plain data that the backend-neutral GAL interface takes
// ---------------------------------------------------------------------------
// gpg/gal/Device.hpp:443/:450 (SetViewport/GetViewport, vtable slots 34/35)
// pass the D3D9 viewport structure to every backend; the D3D10 backend copies
// its six fields into a D3D10_VIEWPORT. The layout is d3d9types.h's. Nothing
// else of Direct3D is here: the renderer is W3.
typedef struct _D3DVIEWPORT9 {
  DWORD X;
  DWORD Y;
  DWORD Width;
  DWORD Height;
  float MinZ;
  float MaxZ;
} D3DVIEWPORT9;

FAF_COMPAT_CHECK(sizeof(D3DVIEWPORT9) == 24, "d3d9types.h D3DVIEWPORT9 layout");

} // extern "C++"

#undef FAF_COMPAT_CHECK
