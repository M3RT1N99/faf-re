#ifndef FAF_PORT_ENGINE_SHIM_FAF_MSVC_CRT_H
#define FAF_PORT_ENGINE_SHIM_FAF_MSVC_CRT_H

// The MSVC CRT names that the engine (src/sdk) uses beyond ISO C and POSIX
// (_stricmp, strcpy_s, memcpy_s, _snprintf, _towlower_l, _time64, _getcwd,
// ...), for the non-Windows targets of the port (docs/port/android-roadmap.md,
// W1.2).
//
// MSVC declares these in its ordinary C headers, so engine files use them after
// including only <cstring> or <cwchar> (gpg/core/containers/String.cpp:455,
// legacy/containers/String.cpp:241, moho/misc/CrtRuntimeExportedHelpers.cpp:57).
// The shim gives the same reach: its <string.h>, <wchar.h>, <ctype.h>,
// <wctype.h> and <stdlib.h> include the C library's header and then this one,
// and so do its stand-ins for the MSVC-only headers <direct.h>, <io.h>,
// <sys/timeb.h> and <crtdbg.h>, and faf_win_compat.h (<windows.h>).
//
// <stdio.h> and <time.h> are not interposed, although MSVC declares part of
// this surface there: bionic's <wchar.h> includes both, and <stdlib.h> includes
// <stdio.h>, before their own declarations, so this header would be entered
// while the functions it calls are not declared yet. Everything below depends
// only on headers that no header it includes reaches half-way.
//
// Semantics are MSVC's where they differ from the POSIX function underneath
// (the _snprintf truncation contract, the _s error returns); each place says
// so. Functions have C linkage, as MSVC declares them, so an engine
// redeclaration such as `extern "C" int __cdecl _stricmp(...)` matches;
// MSVC's C++ template overloads (sprintf_s(char (&)[N], ...)) keep C++ linkage.

#if defined(_WIN32) || defined(_MSC_VER)
#error "port/engine/shim is for non-Windows targets; it must not be on a Windows include path"
#endif

#if !defined(__cplusplus)
#error "faf_msvc_crt.h is C++ only (the engine compiles no C translation units)"
#endif

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <locale.h>
#include <malloc.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#include "faf_win_path.h"

// Third-party C headers may include <string.h> inside `extern "C" { }`; the
// templates below need C++ linkage wherever this header is entered.
extern "C++" {

// ---------------------------------------------------------------------------
// Types and constants (corecrt.h, stdlib.h)
// ---------------------------------------------------------------------------
typedef int errno_t;
typedef size_t rsize_t;
#define _TRUNCATE ((size_t)-1)
#define STRUNCATE 80
#define _MAX_PATH 260
#define _MAX_DRIVE 3
#define _MAX_DIR 256
#define _MAX_FNAME 256
#define _MAX_EXT 256

namespace faf_compat
{
  template <class T, size_t N>
  char (&CountOfHelper(T (&array)[N]))[N];
} // namespace faf_compat

// The C++ form of MSVC's _countof: it rejects pointers.
#define _countof(array) (sizeof(faf_compat::CountOfHelper(array)))

extern "C" {

// ---------------------------------------------------------------------------
// <string.h>
// ---------------------------------------------------------------------------
inline int _stricmp(const char* lhs, const char* rhs) noexcept
{
  return strcasecmp(lhs, rhs);
}

inline int _strnicmp(const char* lhs, const char* rhs, size_t count) noexcept
{
  return strncasecmp(lhs, rhs, count);
}

// The pre-underscore names MSVC still declares.
inline int stricmp(const char* lhs, const char* rhs) noexcept
{
  return strcasecmp(lhs, rhs);
}

inline int strnicmp(const char* lhs, const char* rhs, size_t count) noexcept
{
  return strncasecmp(lhs, rhs, count);
}

inline int _memicmp(const void* lhs, const void* rhs, size_t count) noexcept
{
  const unsigned char* a = static_cast<const unsigned char*>(lhs);
  const unsigned char* b = static_cast<const unsigned char*>(rhs);
  for (size_t i = 0; i < count; ++i) {
    const int difference = tolower(a[i]) - tolower(b[i]);
    if (difference != 0) {
      return difference;
    }
  }
  return 0;
}

inline char* _strdup(const char* text) noexcept
{
  return strdup(text);
}

// In place, as MSVC's; the C locale's case mapping.
inline char* _strlwr(char* text) noexcept
{
  for (char* p = text; *p != '\0'; ++p) {
    *p = static_cast<char>(tolower(static_cast<unsigned char>(*p)));
  }
  return text;
}

inline char* _strupr(char* text) noexcept
{
  for (char* p = text; *p != '\0'; ++p) {
    *p = static_cast<char>(toupper(static_cast<unsigned char>(*p)));
  }
  return text;
}

// ---------------------------------------------------------------------------
// <wchar.h>
// ---------------------------------------------------------------------------
inline int _wcsicmp(const wchar_t* lhs, const wchar_t* rhs) noexcept
{
  return wcscasecmp(lhs, rhs);
}

inline int _wcsnicmp(const wchar_t* lhs, const wchar_t* rhs, size_t count) noexcept
{
  return wcsncasecmp(lhs, rhs, count);
}

inline wchar_t* _wcsdup(const wchar_t* text) noexcept
{
  return wcsdup(text);
}

// ---------------------------------------------------------------------------
// <stdio.h> printf family
// ---------------------------------------------------------------------------
// _snprintf and _vsnprintf keep MSVC's legacy contract, which differs from C99:
// when the output does not fit, `count` characters are stored without a
// terminator and the result is -1; when it fits exactly, there is no terminator
// either and the result is `count`.
inline int _vsnprintf(char* buffer, size_t count, const char* format, va_list args) noexcept
{
  va_list measureArgs;
  va_copy(measureArgs, args);
  const int length = vsnprintf(nullptr, 0, format, measureArgs);
  va_end(measureArgs);
  if (length < 0) {
    return -1;
  }
  if (static_cast<size_t>(length) < count) {
    return vsnprintf(buffer, count, format, args);
  }
  if (count != 0) {
    char* full = static_cast<char*>(malloc(static_cast<size_t>(length) + 1u));
    if (full == nullptr) {
      return -1;
    }
    vsnprintf(full, static_cast<size_t>(length) + 1u, format, args);
    memcpy(buffer, full, count);
    free(full);
  }
  return static_cast<size_t>(length) == count ? length : -1;
}

inline int _snprintf(char* buffer, size_t count, const char* format, ...) noexcept
{
  va_list args;
  va_start(args, format);
  const int result = _vsnprintf(buffer, count, format, args);
  va_end(args);
  return result;
}

inline int _vsnwprintf(wchar_t* buffer, size_t count, const wchar_t* format, va_list args) noexcept
{
  // vswprintf reports truncation as -1 and always terminates; the cases MSVC
  // leaves unterminated are not reproduced for wide strings.
  return vswprintf(buffer, count, format, args);
}

inline int _snwprintf(wchar_t* buffer, size_t count, const wchar_t* format, ...) noexcept
{
  va_list args;
  va_start(args, format);
  const int result = _vsnwprintf(buffer, count, format, args);
  va_end(args);
  return result;
}

inline int _vscprintf(const char* format, va_list args) noexcept
{
  va_list measureArgs;
  va_copy(measureArgs, args);
  const int length = vsnprintf(nullptr, 0, format, measureArgs);
  va_end(measureArgs);
  return length;
}

inline int _scprintf(const char* format, ...) noexcept
{
  va_list args;
  va_start(args, format);
  const int result = vsnprintf(nullptr, 0, format, args);
  va_end(args);
  return result;
}

// ---------------------------------------------------------------------------
// The secure (_s) functions. Where MSVC calls the invalid-parameter handler
// (which terminates by default), these leave an empty string and return the
// error, which is what MSVC does when a handler returns.
// ---------------------------------------------------------------------------
inline int vsprintf_s(char* buffer, size_t size, const char* format, va_list args) noexcept
{
  if (buffer == nullptr || size == 0 || format == nullptr) {
    errno = EINVAL;
    return -1;
  }
  const int length = vsnprintf(buffer, size, format, args);
  if (length < 0 || static_cast<size_t>(length) >= size) {
    buffer[0] = '\0';
    errno = ERANGE;
    return -1;
  }
  return length;
}

inline int sprintf_s(char* buffer, size_t size, const char* format, ...) noexcept
{
  va_list args;
  va_start(args, format);
  const int result = vsprintf_s(buffer, size, format, args);
  va_end(args);
  return result;
}

inline int vswprintf_s(wchar_t* buffer, size_t size, const wchar_t* format, va_list args) noexcept
{
  if (buffer == nullptr || size == 0 || format == nullptr) {
    errno = EINVAL;
    return -1;
  }
  const int length = vswprintf(buffer, size, format, args);
  if (length < 0) {
    buffer[0] = L'\0';
    errno = ERANGE;
    return -1;
  }
  return length;
}

inline int swprintf_s(wchar_t* buffer, size_t size, const wchar_t* format, ...) noexcept
{
  va_list args;
  va_start(args, format);
  const int result = vswprintf_s(buffer, size, format, args);
  va_end(args);
  return result;
}

} // extern "C"

template <size_t N>
inline int sprintf_s(char (&buffer)[N], const char* format, ...) noexcept
{
  va_list args;
  va_start(args, format);
  const int result = vsprintf_s(buffer, N, format, args);
  va_end(args);
  return result;
}

template <size_t N>
inline int swprintf_s(wchar_t (&buffer)[N], const wchar_t* format, ...) noexcept
{
  va_list args;
  va_start(args, format);
  const int result = vswprintf_s(buffer, N, format, args);
  va_end(args);
  return result;
}

namespace faf_compat
{
  // strcpy_s / strncpy_s / strcat_s for both character types.
  template <class Char>
  inline errno_t CopyString(Char* destination, rsize_t size, const Char* source, rsize_t count) noexcept
  {
    if (destination == nullptr || size == 0) {
      return EINVAL;
    }
    if (source == nullptr) {
      destination[0] = Char(0);
      return EINVAL;
    }
    rsize_t length = 0;
    while (length < count && source[length] != Char(0)) {
      ++length;
    }
    if (length >= size) {
      if (count == _TRUNCATE) {
        memcpy(destination, source, (size - 1) * sizeof(Char));
        destination[size - 1] = Char(0);
        return STRUNCATE;
      }
      destination[0] = Char(0);
      return ERANGE;
    }
    memcpy(destination, source, length * sizeof(Char));
    destination[length] = Char(0);
    return 0;
  }

  template <class Char>
  inline errno_t AppendString(Char* destination, rsize_t size, const Char* source) noexcept
  {
    if (destination == nullptr || size == 0) {
      return EINVAL;
    }
    rsize_t used = 0;
    while (used < size && destination[used] != Char(0)) {
      ++used;
    }
    if (used == size) {
      destination[0] = Char(0);
      return EINVAL;
    }
    const errno_t result = CopyString(destination + used, size - used, source, static_cast<rsize_t>(-2));
    if (result != 0) {
      destination[0] = Char(0);
    }
    return result;
  }
} // namespace faf_compat

extern "C" {

inline errno_t strcpy_s(char* destination, rsize_t size, const char* source) noexcept
{
  return faf_compat::CopyString(destination, size, source, static_cast<rsize_t>(-2));
}

inline errno_t strncpy_s(char* destination, rsize_t size, const char* source, rsize_t count) noexcept
{
  return faf_compat::CopyString(destination, size, source, count);
}

inline errno_t strcat_s(char* destination, rsize_t size, const char* source) noexcept
{
  return faf_compat::AppendString(destination, size, source);
}

inline errno_t wcscpy_s(wchar_t* destination, rsize_t size, const wchar_t* source) noexcept
{
  return faf_compat::CopyString(destination, size, source, static_cast<rsize_t>(-2));
}

inline errno_t wcsncpy_s(wchar_t* destination, rsize_t size, const wchar_t* source, rsize_t count) noexcept
{
  return faf_compat::CopyString(destination, size, source, count);
}

inline errno_t wcscat_s(wchar_t* destination, rsize_t size, const wchar_t* source) noexcept
{
  return faf_compat::AppendString(destination, size, source);
}

// memcpy_s / memmove_s: on a size error MSVC zero-fills the destination.
inline errno_t memcpy_s(void* destination, rsize_t size, const void* source, rsize_t count) noexcept
{
  if (count == 0) {
    return 0;
  }
  if (destination == nullptr) {
    return EINVAL;
  }
  if (source == nullptr) {
    memset(destination, 0, size);
    return EINVAL;
  }
  if (size < count) {
    memset(destination, 0, size);
    return ERANGE;
  }
  memcpy(destination, source, count);
  return 0;
}

inline errno_t memmove_s(void* destination, rsize_t size, const void* source, rsize_t count) noexcept
{
  if (count == 0) {
    return 0;
  }
  if (destination == nullptr || source == nullptr) {
    return EINVAL;
  }
  if (size < count) {
    return ERANGE;
  }
  memmove(destination, source, count);
  return 0;
}

} // extern "C"

template <size_t N>
inline errno_t strcpy_s(char (&destination)[N], const char* source) noexcept
{
  return faf_compat::CopyString(destination, N, source, static_cast<rsize_t>(-2));
}

template <size_t N>
inline errno_t strncpy_s(char (&destination)[N], const char* source, rsize_t count) noexcept
{
  return faf_compat::CopyString(destination, N, source, count);
}

template <size_t N>
inline errno_t strcat_s(char (&destination)[N], const char* source) noexcept
{
  return faf_compat::AppendString(destination, N, source);
}

template <size_t N>
inline errno_t wcscpy_s(wchar_t (&destination)[N], const wchar_t* source) noexcept
{
  return faf_compat::CopyString(destination, N, source, static_cast<rsize_t>(-2));
}

// ---------------------------------------------------------------------------
// <ctype.h> / <wctype.h>: the _l (explicit locale) forms. MSVC treats a null
// locale as the current one.
// ---------------------------------------------------------------------------
typedef locale_t _locale_t;

extern "C" {

inline int _tolower_l(int character, _locale_t locale) noexcept
{
  return locale != nullptr ? tolower_l(character, locale) : tolower(character);
}

inline int _toupper_l(int character, _locale_t locale) noexcept
{
  return locale != nullptr ? toupper_l(character, locale) : toupper(character);
}

inline wint_t _towlower_l(wint_t character, _locale_t locale) noexcept
{
  return locale != nullptr ? towlower_l(character, locale) : towlower(character);
}

inline wint_t _towupper_l(wint_t character, _locale_t locale) noexcept
{
  return locale != nullptr ? towupper_l(character, locale) : towupper(character);
}

// ---------------------------------------------------------------------------
// <stdlib.h>, <errno.h>, <stdio.h> odds and ends
// ---------------------------------------------------------------------------
// MSVC's errno accessor (what its `errno` macro expands to).
inline int* _errno() noexcept
{
  return &errno;
}

// <malloc.h>. On Windows the engine replaces the CRT's _msize with its own
// (gpg/core/utils/Global.cpp, `_msize`, 0x00957AE0); off Windows Global.cpp
// leaves the C library's allocator alone (no malloc/free/_msize of its own),
// so this is the C library's block size, which crtdbg.h's Release
// `_msize_dbg` expands to as well. It can exceed the size that was requested,
// as HeapSize does not (faf_win_memory.h).
inline size_t _msize(void* memblock) noexcept
{
  return memblock != nullptr ? malloc_usable_size(memblock) : 0u;
}

// MSVC's sscanf_s takes a buffer size after each %s, %c and %[ argument;
// plain sscanf would read that size as the next pointer. Only formats without
// those conversions are accepted here (moho/sim/Sim.cpp:10164 reads "%f"), and
// they mean the same in both; any other format fails with EINVAL and -1 (EOF).
inline int sscanf_s(const char* buffer, const char* format, ...) noexcept
{
  for (const char* p = format; *p != '\0'; ++p) {
    if (*p != '%') {
      continue;
    }
    ++p;
    if (*p == '%') {
      continue;
    }
    while (*p != '\0' && strchr("scCS[", *p) == nullptr && strchr("diouxXaAeEfFgGpn", *p) == nullptr) {
      ++p;
    }
    if (*p == '\0' || strchr("scCS[", *p) != nullptr) {
      errno = EINVAL;
      return EOF;
    }
  }
  va_list args;
  va_start(args, format);
  const int result = vsscanf(buffer, format, args);
  va_end(args);
  return result;
}

inline FILE* _popen(const char* command, const char* mode) noexcept
{
  return popen(command, mode);
}

inline int _pclose(FILE* stream) noexcept
{
  return pclose(stream);
}

inline errno_t fopen_s(FILE** file, const char* path, const char* mode) noexcept
{
  if (file == nullptr) {
    return EINVAL;
  }
  *file = fopen(path, mode);
  return *file != nullptr ? 0 : errno;
}

inline int _fileno(FILE* stream) noexcept
{
  return fileno(stream);
}

} // extern "C"

// ---------------------------------------------------------------------------
// <time.h>: the 64-bit names. time_t is 64-bit on every target the port builds
// for, so they are the plain functions.
// ---------------------------------------------------------------------------
_Static_assert(sizeof(time_t) == 8, "the _time64 family maps onto a 64-bit time_t");
typedef time_t __time64_t;

extern "C" {

inline __time64_t _time64(__time64_t* result) noexcept
{
  return time(result);
}

inline struct tm* _localtime64(const __time64_t* when) noexcept
{
  return localtime(when);
}

inline struct tm* _gmtime64(const __time64_t* when) noexcept
{
  return gmtime(when);
}

inline __time64_t _mktime64(struct tm* when) noexcept
{
  return mktime(when);
}

// MSVC's ctime and POSIX's share the format ("Wed Jan 02 02:03:55 1980\n") and the static result
// buffer (moho/misc/StatItem.cpp:1014).
inline char* _ctime64(const __time64_t* when) noexcept
{
  return ctime(when);
}

inline errno_t localtime_s(struct tm* result, const time_t* when) noexcept
{
  return localtime_r(when, result) != nullptr ? 0 : EINVAL;
}

inline errno_t gmtime_s(struct tm* result, const time_t* when) noexcept
{
  return gmtime_r(when, result) != nullptr ? 0 : EINVAL;
}

} // extern "C"

// ---------------------------------------------------------------------------
// <sys/timeb.h>. `timezone` is minutes west of UTC for standard time and
// `dstflag` says whether daylight saving time is in effect, as in the MSVC CRT
// (which also assumes a 60-minute daylight shift).
// ---------------------------------------------------------------------------
struct __timeb64 {
  __time64_t time;
  unsigned short millitm;
  short timezone;
  short dstflag;
};
#define _timeb __timeb64

extern "C" {

inline void _ftime64(struct __timeb64* result) noexcept
{
  timespec now;
  clock_gettime(CLOCK_REALTIME, &now);
  struct tm local;
  const time_t seconds = now.tv_sec;
  localtime_r(&seconds, &local);
  result->time = now.tv_sec;
  result->millitm = static_cast<unsigned short>(now.tv_nsec / 1000000L);
  result->dstflag = static_cast<short>(local.tm_isdst > 0 ? 1 : 0);
  result->timezone = static_cast<short>(-(local.tm_gmtoff / 60) + (local.tm_isdst > 0 ? 60 : 0));
}

inline void _ftime(struct __timeb64* result) noexcept
{
  _ftime64(result);
}

// ---------------------------------------------------------------------------
// <direct.h>
// ---------------------------------------------------------------------------
inline char* _getcwd(char* buffer, int size) noexcept
{
  return getcwd(buffer, static_cast<size_t>(size));
}

inline int _chdir(const char* path) noexcept
{
  return chdir(path);
}

inline int _mkdir(const char* path) noexcept
{
  return mkdir(path, 0777);
}

inline int _rmdir(const char* path) noexcept
{
  return rmdir(path);
}

// _getdcwd / _getdrive (moho/misc/StartupHelpers.cpp FILE_Dir): a POSIX process has one working
// directory and no drives (faf_win_path.h: "Drive letters mean nothing here"). _getdcwd answers
// every drive with that directory; _getdrive reports 0, "no drive", which MSVC never returns, so a
// caller that formats it as a letter (FILE_Dir: 'a' - 1 + drive) produces a path that names
// nothing rather than a plausible one.
inline char* _getdcwd(int /*drive*/, char* buffer, int maxlen) noexcept
{
  return getcwd(buffer, static_cast<size_t>(maxlen));
}

inline int _getdrive(void) noexcept
{
  return 0;
}

// _fullpath (moho/misc/StartupHelpers.cpp DISK_GetLaunchDir): MSVC's contract over POSIX paths.
// The result is absolute (relPath joined to the working directory unless it starts with a
// separator), '/' and '\' both separate components (as in faf_win_path.h), "." and empty
// components drop, ".." removes the previous one and stops at the root. Nothing is resolved
// against the file system: the path need not exist and symbolic links stay, as on Windows. A null
// or empty relPath gives the working directory. A null absPath returns a malloc'd buffer of at
// least _MAX_PATH bytes; otherwise maxLength bounds the result. Too long: null with errno ERANGE.
inline char* _fullpath(char* absPath, const char* relPath, size_t maxLength) noexcept
{
  char joined[faf_compat::kMaxNativePath];
  size_t length = 0;
  const bool absolute = relPath != nullptr && (relPath[0] == '/' || relPath[0] == '\\');
  if (!absolute) {
    if (getcwd(joined, sizeof(joined)) == nullptr) {
      return nullptr;
    }
    length = strlen(joined);
  }
  if (relPath != nullptr) {
    const size_t relLength = strlen(relPath);
    if (length + 1 + relLength + 1 > sizeof(joined)) {
      errno = ERANGE;
      return nullptr;
    }
    joined[length++] = '/';
    memcpy(joined + length, relPath, relLength + 1);
    length += relLength;
  }

  // Collapse into `normalized`; every component it keeps is written after a '/'.
  char normalized[faf_compat::kMaxNativePath];
  size_t out = 0;
  size_t in = 0;
  while (in < length) {
    while (in < length && (joined[in] == '/' || joined[in] == '\\')) {
      ++in;
    }
    size_t end = in;
    while (end < length && joined[end] != '/' && joined[end] != '\\') {
      ++end;
    }
    const size_t componentLength = end - in;
    if (componentLength == 0 || (componentLength == 1 && joined[in] == '.')) {
      // nothing
    } else if (componentLength == 2 && joined[in] == '.' && joined[in + 1] == '.') {
      while (out > 0 && normalized[out - 1] != '/') {
        --out;
      }
      if (out > 0) {
        --out;
      }
    } else {
      normalized[out++] = '/';
      memcpy(normalized + out, joined + in, componentLength);
      out += componentLength;
    }
    in = end;
  }
  if (out == 0) {
    normalized[out++] = '/';
  }
  normalized[out] = '\0';

  if (absPath == nullptr) {
    const size_t size = (out + 1 > _MAX_PATH) ? out + 1 : _MAX_PATH;
    absPath = static_cast<char*>(malloc(size));
    if (absPath == nullptr) {
      errno = ENOMEM;
      return nullptr;
    }
  } else if (out + 1 > maxLength) {
    errno = ERANGE;
    return nullptr;
  }
  memcpy(absPath, normalized, out + 1);
  return absPath;
}

// ---------------------------------------------------------------------------
// <io.h>
// ---------------------------------------------------------------------------
inline int _access(const char* path, int mode) noexcept
{
  return access(path, mode);
}

inline int _unlink(const char* path) noexcept
{
  return unlink(path);
}

} // extern "C"

// _findfirst64 / _findnext64 / _findclose, as moho/sim/CVFSImpl.cpp:119 and
// lua/LuaObject.cpp:6224 enumerate directories. The CRT wraps FindFirstFile,
// and so does this, through the same listing (faf_win_path.h):
// - '/' and '\' both separate directories, and the directory part is resolved
//   ignoring case as NTFS does; the wildcards apply to the last component
//   only. `*` matches any run of characters, `?` one character, and names
//   compare ASCII case-insensitively. `*.*` matches every name, and a trailing
//   `.*` also matches a name without a dot.
// - Entries come back in NTFS order: "." and ".." first (callers skip them),
//   then by upper-cased name. FAF's init_faf.lua mounts archives in this order.
// - attrib has _A_SUBDIR for directories and _A_RDONLY when the owner cannot
//   write; the archive, hidden and system bits have no POSIX counterpart and
//   stay clear. time_create is -1, the CRT's value where the file system keeps
//   no creation time (FAT), since stat has none either.
#define _A_NORMAL 0x00
#define _A_RDONLY 0x01
#define _A_HIDDEN 0x02
#define _A_SYSTEM 0x04
#define _A_SUBDIR 0x10
#define _A_ARCH 0x20

struct __finddata64_t {
  unsigned attrib;
  __time64_t time_create;
  __time64_t time_access;
  __time64_t time_write;
  __int64 size;
  char name[260];
};

namespace faf_compat
{
  // Fills `fileInfo` from the next listed name; false at the end.
  inline bool NextFindEntry(DirectoryListing& listing, __finddata64_t* fileInfo) noexcept
  {
    if (listing.next >= listing.count) {
      return false;
    }
    const char* const name = listing.names[listing.next++];
    struct stat status;
    const bool haveStatus = StatListedName(listing, name, &status);
    unsigned attributes = _A_NORMAL;
    if (haveStatus && S_ISDIR(status.st_mode)) {
      attributes |= _A_SUBDIR;
    }
    if (haveStatus && (status.st_mode & S_IWUSR) == 0) {
      attributes |= _A_RDONLY;
    }
    fileInfo->attrib = attributes;
    fileInfo->time_create = -1;
    fileInfo->time_access = haveStatus ? static_cast<__time64_t>(status.st_atime) : -1;
    fileInfo->time_write = haveStatus ? static_cast<__time64_t>(status.st_mtime) : -1;
    fileInfo->size = haveStatus && !S_ISDIR(status.st_mode) ? static_cast<__int64>(status.st_size) : 0;
    const size_t length = strnlen(name, sizeof(fileInfo->name) - 1u);
    memcpy(fileInfo->name, name, length);
    fileInfo->name[length] = '\0';
    return true;
  }
} // namespace faf_compat

extern "C" {

inline intptr_t _findfirst64(const char* fileSpec, struct __finddata64_t* fileInfo) noexcept
{
  if (fileSpec == nullptr || fileInfo == nullptr) {
    errno = EINVAL;
    return -1;
  }
  faf_compat::ListingStatus status = faf_compat::ListingStatus::Ok;
  faf_compat::DirectoryListing* const listing = faf_compat::OpenDirectoryListing(fileSpec, &status);
  if (listing == nullptr) {
    errno = status == faf_compat::ListingStatus::OutOfMemory ? ENOMEM : ENOENT;
    return -1;
  }
  if (!faf_compat::NextFindEntry(*listing, fileInfo)) {
    faf_compat::CloseDirectoryListing(listing);
    errno = ENOENT;
    return -1;
  }
  return reinterpret_cast<intptr_t>(listing);
}

inline int _findnext64(intptr_t handle, struct __finddata64_t* fileInfo) noexcept
{
  if (handle == -1 || handle == 0 || fileInfo == nullptr) {
    errno = EINVAL;
    return -1;
  }
  if (!faf_compat::NextFindEntry(*reinterpret_cast<faf_compat::DirectoryListing*>(handle), fileInfo)) {
    errno = ENOENT;
    return -1;
  }
  return 0;
}

inline int _findclose(intptr_t handle) noexcept
{
  if (handle == -1 || handle == 0) {
    errno = EINVAL;
    return -1;
  }
  faf_compat::CloseDirectoryListing(reinterpret_cast<faf_compat::DirectoryListing*>(handle));
  return 0;
}

} // extern "C"

// ---------------------------------------------------------------------------
// Wide-character file names and the environment
// ---------------------------------------------------------------------------
// The engine's wide paths are UTF-16 on Windows. wchar_t holds whole code
// points here (4 bytes), and the file system takes UTF-8, so the wide forms
// encode their arguments as UTF-8 and call the narrow function.
namespace faf_compat
{
  // False if `text` holds a value that is not a Unicode scalar value, or does
  // not fit into `size` bytes with its terminator.
  inline bool WideToUtf8(const wchar_t* text, char* out, const size_t size) noexcept
  {
    size_t used = 0;
    for (; *text != L'\0'; ++text) {
      const uint32_t codePoint = static_cast<uint32_t>(*text);
      unsigned char bytes[4];
      size_t count = 0;
      if (codePoint < 0x80u) {
        bytes[count++] = static_cast<unsigned char>(codePoint);
      } else if (codePoint < 0x800u) {
        bytes[count++] = static_cast<unsigned char>(0xC0u | (codePoint >> 6));
        bytes[count++] = static_cast<unsigned char>(0x80u | (codePoint & 0x3Fu));
      } else if (codePoint < 0x10000u) {
        if (codePoint >= 0xD800u && codePoint <= 0xDFFFu) {
          return false;
        }
        bytes[count++] = static_cast<unsigned char>(0xE0u | (codePoint >> 12));
        bytes[count++] = static_cast<unsigned char>(0x80u | ((codePoint >> 6) & 0x3Fu));
        bytes[count++] = static_cast<unsigned char>(0x80u | (codePoint & 0x3Fu));
      } else if (codePoint < 0x110000u) {
        bytes[count++] = static_cast<unsigned char>(0xF0u | (codePoint >> 18));
        bytes[count++] = static_cast<unsigned char>(0x80u | ((codePoint >> 12) & 0x3Fu));
        bytes[count++] = static_cast<unsigned char>(0x80u | ((codePoint >> 6) & 0x3Fu));
        bytes[count++] = static_cast<unsigned char>(0x80u | (codePoint & 0x3Fu));
      } else {
        return false;
      }
      if (used + count >= size) {
        return false;
      }
      memcpy(out + used, bytes, count);
      used += count;
    }
    if (used >= size) {
      return false;
    }
    out[used] = '\0';
    return true;
  }
} // namespace faf_compat

extern "C" {

inline FILE* _wfopen(const wchar_t* path, const wchar_t* mode) noexcept
{
  char narrowPath[4096];
  char narrowMode[32];
  if (path == nullptr || mode == nullptr || !faf_compat::WideToUtf8(path, narrowPath, sizeof(narrowPath)) ||
      !faf_compat::WideToUtf8(mode, narrowMode, sizeof(narrowMode))) {
    errno = EINVAL;
    return nullptr;
  }
  return fopen(narrowPath, narrowMode);
}

inline errno_t _wfopen_s(FILE** file, const wchar_t* path, const wchar_t* mode) noexcept
{
  if (file == nullptr) {
    return EINVAL;
  }
  *file = _wfopen(path, mode);
  return *file != nullptr ? 0 : errno;
}

// A copy of the variable on the C heap, released with free(); a missing
// variable is not an error (null and 0).
inline errno_t _dupenv_s(char** buffer, size_t* numberOfElements, const char* name) noexcept
{
  if (buffer == nullptr || name == nullptr) {
    return EINVAL;
  }
  *buffer = nullptr;
  if (numberOfElements != nullptr) {
    *numberOfElements = 0;
  }
  const char* const value = getenv(name);
  if (value == nullptr) {
    return 0;
  }
  const size_t length = strlen(value) + 1u;
  char* const copy = static_cast<char*>(malloc(length));
  if (copy == nullptr) {
    return ENOMEM;
  }
  memcpy(copy, value, length);
  *buffer = copy;
  if (numberOfElements != nullptr) {
    *numberOfElements = length;
  }
  return 0;
}

} // extern "C"

// ---------------------------------------------------------------------------
// <float.h>: the x87 precision-control constants. platform/X87Precision.h
// passes them to _controlfp only on _M_IX86; there is no x87 unit here (W5).
// ---------------------------------------------------------------------------
#define _MCW_PC 0x00030000
#define _PC_64 0x00000000
#define _PC_53 0x00010000
#define _PC_24 0x00020000

} // extern "C++"

#endif // FAF_PORT_ENGINE_SHIM_FAF_MSVC_CRT_H
