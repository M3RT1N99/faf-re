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

// <malloc.h>. Declared only: the engine replaces the CRT's _msize with its own
// (gpg/core/utils/Global.cpp, `_msize`, 0x00957AE0), and crtdbg.h's Release
// `_msize_dbg` expands to it.
size_t _msize(void* memblock);

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

// _findfirst64 / _findnext64 / _findclose over opendir/readdir, as
// moho/sim/CVFSImpl.cpp:119 and lua/LuaObject.cpp:6224 enumerate directories.
// What carries over from the CRT, which wraps FindFirstFile:
// - '/' and '\' both separate directories; the wildcards apply to the last
//   component only. `*` matches any run of characters, `?` one character, and
//   names compare ASCII case-insensitively, as on NTFS. `*.*` matches every
//   name, and a trailing `.*` also matches a name without a dot.
// - "." and ".." come back like any other entry (callers skip them).
// - attrib has _A_SUBDIR for directories and _A_RDONLY when the owner cannot
//   write; the archive, hidden and system bits have no POSIX counterpart and
//   stay clear. time_create is -1, the CRT's value where the file system keeps
//   no creation time (FAT), since stat has none either.
// What does not: the directory part is resolved as written, so on a
// case-sensitive file system a directory spelled in another case is not found.
// Case-insensitive path resolution belongs to the platform layer (W2).
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
  struct FindState {
    DIR* directory;
    char* pattern;
  };

  inline int AsciiLower(const unsigned char character) noexcept
  {
    return (character >= 'A' && character <= 'Z') ? character + ('a' - 'A') : character;
  }

  // FindFirstFile's matching of one name against the last component of a
  // pattern (see above).
  inline bool MatchFindPattern(const char* pattern, const char* name) noexcept
  {
    for (;;) {
      if (pattern[0] == '.' && pattern[1] == '*' && pattern[2] == '\0' && name[0] == '\0') {
        return true;
      }
      if (*pattern == '\0') {
        return *name == '\0';
      }
      if (*pattern == '*') {
        while (*pattern == '*') {
          ++pattern;
        }
        if (*pattern == '\0') {
          return true;
        }
        for (; *name != '\0'; ++name) {
          if (MatchFindPattern(pattern, name)) {
            return true;
          }
        }
        return MatchFindPattern(pattern, name);
      }
      if (*name == '\0') {
        return false;
      }
      if (*pattern != '?' &&
          AsciiLower(static_cast<unsigned char>(*pattern)) != AsciiLower(static_cast<unsigned char>(*name))) {
        return false;
      }
      ++pattern;
      ++name;
    }
  }

  // Advances to the next entry that matches and fills `fileInfo`; false at the
  // end of the directory.
  inline bool NextFindEntry(FindState& state, __finddata64_t* fileInfo) noexcept
  {
    while (const dirent* entry = readdir(state.directory)) {
      if (!MatchFindPattern(state.pattern, entry->d_name)) {
        continue;
      }
      struct stat status;
      const bool haveStatus = fstatat(dirfd(state.directory), entry->d_name, &status, 0) == 0;
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
      const size_t length = strnlen(entry->d_name, sizeof(fileInfo->name) - 1u);
      memcpy(fileInfo->name, entry->d_name, length);
      fileInfo->name[length] = '\0';
      return true;
    }
    return false;
  }
} // namespace faf_compat

extern "C" {

inline intptr_t _findfirst64(const char* fileSpec, struct __finddata64_t* fileInfo) noexcept
{
  if (fileSpec == nullptr || fileInfo == nullptr) {
    errno = EINVAL;
    return -1;
  }
  const char* lastSeparator = nullptr;
  for (const char* p = fileSpec; *p != '\0'; ++p) {
    if (*p == '/' || *p == '\\') {
      lastSeparator = p;
    }
  }
  const char* const pattern = lastSeparator != nullptr ? lastSeparator + 1 : fileSpec;
  if (*pattern == '\0') {
    errno = ENOENT;
    return -1;
  }
  char* directoryPath = nullptr;
  if (lastSeparator == nullptr) {
    directoryPath = strdup(".");
  } else if (lastSeparator == fileSpec) {
    directoryPath = strdup("/");
  } else {
    directoryPath = strndup(fileSpec, static_cast<size_t>(lastSeparator - fileSpec));
  }
  if (directoryPath == nullptr) {
    errno = ENOMEM;
    return -1;
  }
  for (char* p = directoryPath; *p != '\0'; ++p) {
    if (*p == '\\') {
      *p = '/';
    }
  }
  DIR* const directory = opendir(directoryPath);
  free(directoryPath);
  if (directory == nullptr) {
    errno = ENOENT;
    return -1;
  }
  faf_compat::FindState* const state = static_cast<faf_compat::FindState*>(malloc(sizeof(faf_compat::FindState)));
  char* const patternCopy = strdup(strcmp(pattern, "*.*") == 0 ? "*" : pattern);
  if (state == nullptr || patternCopy == nullptr) {
    free(state);
    free(patternCopy);
    closedir(directory);
    errno = ENOMEM;
    return -1;
  }
  state->directory = directory;
  state->pattern = patternCopy;
  if (!faf_compat::NextFindEntry(*state, fileInfo)) {
    closedir(directory);
    free(patternCopy);
    free(state);
    errno = ENOENT;
    return -1;
  }
  return reinterpret_cast<intptr_t>(state);
}

inline int _findnext64(intptr_t handle, struct __finddata64_t* fileInfo) noexcept
{
  if (handle == -1 || handle == 0 || fileInfo == nullptr) {
    errno = EINVAL;
    return -1;
  }
  if (!faf_compat::NextFindEntry(*reinterpret_cast<faf_compat::FindState*>(handle), fileInfo)) {
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
  faf_compat::FindState* const state = reinterpret_cast<faf_compat::FindState*>(handle);
  closedir(state->directory);
  free(state->pattern);
  free(state);
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
