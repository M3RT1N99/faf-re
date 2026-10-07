#ifndef FAF_PORT_ENGINE_SHIM_FAF_WIN_PATH_H
#define FAF_PORT_ENGINE_SHIM_FAF_WIN_PATH_H

// Win32 path semantics over a POSIX file system, for the shim's file functions
// (faf_win_file.h: CreateFile, GetFileAttributes, FindFirstFile, ...) and the
// CRT's _findfirst64 (faf_msvc_crt.h).
//
// The engine was written against NTFS, and its paths say so:
// gpg::STR_CanonizeFilename (gpg/core/containers/String.cpp:1074) lower-cases
// every path and turns '/' into '\', and the VFS (moho/sim/CVFSImpl.cpp,
// moho/misc/FileWaitHandleSet.cpp) opens files under those canonical names.
// Android's storage is case-sensitive, so a path is resolved here the way
// NTFS resolves it:
// - '\' and '/' both separate components;
// - a component that does not exist as spelled is looked up in its directory
//   ignoring ASCII case (the engine's STR_ToLower folds ASCII only). When
//   several names differ only in case, the first in NTFS order wins, so the
//   choice is deterministic. A component that exists as spelled is taken as
//   is, so a case-sensitive directory with "Units" and "units" still opens
//   the one that was asked for;
// - directory listings come back in NTFS order: "." and ".." first, then the
//   names ordered by their upper-cased bytes (raw bytes break ties). The order
//   matters beyond cosmetics: FAF's init_faf.lua mounts the *.nx2/*.scd
//   archives in the order io.dir lists them (lua/LuaObject.cpp io_dir ->
//   _findfirst64), and the first mount of a file wins.
// Drive letters mean nothing here: "C:\x" is a relative path whose first
// component is "C:", which does not exist. Mapping the engine's known folders
// and data paths onto app storage belongs to the platform layer (W2).
//
// port/native/src/FileSystem.cpp implements the same rules for the M1 data
// bring-up (std::filesystem based); this is the allocation-light C version the
// shim needs, since it runs inside the engine's own file and allocator paths.

#if defined(_WIN32) || defined(_MSC_VER)
#error "port/engine/shim is for non-Windows targets; it must not be on a Windows include path"
#endif

#if !defined(__cplusplus)
#error "faf_win_path.h is C++ only (the engine compiles no C translation units)"
#endif

#include <dirent.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <wchar.h>

extern "C++" {

namespace faf_compat
{
  // Longest native path the shim builds (PATH_MAX on Linux).
  constexpr size_t kMaxNativePath = 4096;

  inline int AsciiLower(const unsigned char character) noexcept
  {
    return (character >= 'A' && character <= 'Z') ? character + ('a' - 'A') : character;
  }

  inline unsigned char AsciiUpperByte(const unsigned char character) noexcept
  {
    return (character >= 'a' && character <= 'z') ? static_cast<unsigned char>(character - ('a' - 'A')) : character;
  }

  // NTFS directory order: upper-cased bytes (UTF-8 then sorts by code point),
  // shorter first on a common prefix, raw bytes as the tie-break.
  inline int NtfsNameCompare(const char* lhs, const char* rhs) noexcept
  {
    const unsigned char* a = reinterpret_cast<const unsigned char*>(lhs);
    const unsigned char* b = reinterpret_cast<const unsigned char*>(rhs);
    for (; *a != '\0' && *b != '\0'; ++a, ++b) {
      const unsigned char ua = AsciiUpperByte(*a);
      const unsigned char ub = AsciiUpperByte(*b);
      if (ua != ub) {
        return ua < ub ? -1 : 1;
      }
    }
    if (*a != *b) {
      return *a == '\0' ? -1 : 1;
    }
    return strcmp(lhs, rhs);
  }

  inline bool EqualsNoCase(const char* name, const char* text, const size_t length) noexcept
  {
    for (size_t i = 0; i < length; ++i) {
      if (name[i] == '\0' ||
          AsciiLower(static_cast<unsigned char>(name[i])) != AsciiLower(static_cast<unsigned char>(text[i]))) {
        return false;
      }
    }
    return name[length] == '\0';
  }

  // FindFirstFile's matching of one name against the last component of a
  // pattern: `*` matches any run of characters, `?` one character, names
  // compare ASCII case-insensitively, and a trailing `.*` also matches a name
  // without a dot. (Callers turn "*.*" into "*".)
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

  // Copies `path` with '\' turned into '/'. False when it does not fit.
  inline bool CopyWithSlashes(const char* path, char* out, const size_t size) noexcept
  {
    size_t i = 0;
    for (; path[i] != '\0'; ++i) {
      if (i + 1 >= size) {
        return false;
      }
      out[i] = path[i] == '\\' ? '/' : path[i];
    }
    out[i] = '\0';
    return true;
  }

  // The entry of directory `directory` ("" = the current directory) whose name
  // equals [name, name + length) ignoring ASCII case; the first in NTFS order
  // when there are several. False when there is none or it does not fit.
  inline bool FindNameNoCase(
    const char* directory, const char* name, const size_t length, char* out, const size_t size
  ) noexcept
  {
    DIR* const stream = opendir(directory[0] != '\0' ? directory : ".");
    if (stream == nullptr) {
      return false;
    }
    bool found = false;
    while (const dirent* const entry = readdir(stream)) {
      if (!EqualsNoCase(entry->d_name, name, length)) {
        continue;
      }
      if (found && NtfsNameCompare(entry->d_name, out) >= 0) {
        continue;
      }
      const size_t entryLength = strlen(entry->d_name);
      if (entryLength + 1 > size) {
        continue;
      }
      memcpy(out, entry->d_name, entryLength + 1);
      found = true;
    }
    closedir(stream);
    return found;
  }

  // A Win32 path as the POSIX path that names the same file (see the top of
  // this file). Components after the first one that matches nothing are kept
  // as written, so the path of a file about to be created still resolves the
  // directories that exist. False, with errno = ENAMETOOLONG, only when the
  // result does not fit into `size`.
  inline bool ResolveWin32Path(const char* path, char* out, const size_t size) noexcept
  {
    char text[kMaxNativePath];
    if (!CopyWithSlashes(path, text, sizeof(text)) || strlen(text) + 1 > size) {
      errno = ENAMETOOLONG;
      return false;
    }
    struct stat status;
    if (text[0] == '\0' || lstat(text, &status) == 0) {
      memcpy(out, text, strlen(text) + 1);
      return true;
    }

    size_t used = 0;
    const char* cursor = text;
    if (*cursor == '/') {
      out[used++] = '/';
      while (*cursor == '/') {
        ++cursor;
      }
    }
    out[used] = '\0';

    bool matching = true;
    while (*cursor != '\0') {
      const char* const slash = strchr(cursor, '/');
      const size_t length = slash != nullptr ? static_cast<size_t>(slash - cursor) : strlen(cursor);
      const size_t separator = (used > 0 && out[used - 1] != '/') ? 1u : 0u;
      if (used + separator + length + 1 > size) {
        errno = ENAMETOOLONG;
        return false;
      }
      const size_t directoryEnd = used;
      if (separator != 0) {
        out[used++] = '/';
      }
      memcpy(out + used, cursor, length);
      out[used + length] = '\0';

      const bool dotComponent = (length == 1 && cursor[0] == '.') || (length == 2 && cursor[0] == '.' && cursor[1] == '.');
      if (matching && !dotComponent && lstat(out, &status) != 0) {
        char directory[kMaxNativePath];
        memcpy(directory, out, directoryEnd);
        directory[directoryEnd] = '\0';
        char match[256];
        if (FindNameNoCase(directory, cursor, length, match, sizeof(match))) {
          const size_t matchLength = strlen(match);
          if (used + matchLength + 1 > size) {
            errno = ENAMETOOLONG;
            return false;
          }
          memcpy(out + used, match, matchLength + 1);
          used += matchLength;
        } else {
          used += length;
          matching = false;
        }
      } else {
        used += length;
      }

      cursor += length;
      if (*cursor == '/') {
        while (*cursor == '/') {
          ++cursor;
        }
        if (*cursor == '\0' && used + 2 <= size) {
          out[used++] = '/';
          out[used] = '\0';
        }
      }
    }
    return true;
  }

  // Splits a Win32 search spec ("dir\*.scd") into its directory, resolved as
  // above, and its pattern (the last component, "*.*" turned into "*").
  // False with errno ENOENT for an empty pattern, ENAMETOOLONG when a part
  // does not fit.
  inline bool SplitFindSpec(
    const char* spec, char* directory, const size_t directorySize, char* pattern, const size_t patternSize
  ) noexcept
  {
    const char* lastSeparator = nullptr;
    for (const char* p = spec; *p != '\0'; ++p) {
      if (*p == '/' || *p == '\\') {
        lastSeparator = p;
      }
    }
    const char* const name = lastSeparator != nullptr ? lastSeparator + 1 : spec;
    if (*name == '\0') {
      errno = ENOENT;
      return false;
    }
    const char* const effective = strcmp(name, "*.*") == 0 ? "*" : name;
    if (strlen(effective) + 1 > patternSize) {
      errno = ENAMETOOLONG;
      return false;
    }
    memcpy(pattern, effective, strlen(effective) + 1);

    char written[kMaxNativePath];
    if (lastSeparator == nullptr) {
      memcpy(written, ".", 2);
    } else if (lastSeparator == spec) {
      memcpy(written, "/", 2);
    } else {
      const size_t length = static_cast<size_t>(lastSeparator - spec);
      if (length + 1 > sizeof(written)) {
        errno = ENAMETOOLONG;
        return false;
      }
      memcpy(written, spec, length);
      written[length] = '\0';
    }
    return ResolveWin32Path(written, directory, directorySize);
  }

  // One FindFirstFile / _findfirst64 enumeration: the matching names of one
  // directory, sorted, read up front (as NTFS hands back an index, not a live
  // readdir stream).
  struct DirectoryListing {
    char* directory; // resolved, '/'-separated
    char** names;
    size_t count;
    size_t next;
  };

  enum class ListingStatus {
    Ok,
    NoMatch,      // the directory exists, nothing matches (ERROR_FILE_NOT_FOUND)
    NoDirectory,  // the directory does not exist (ERROR_PATH_NOT_FOUND)
    OutOfMemory,
    NameTooLong,
  };

  inline void CloseDirectoryListing(DirectoryListing* listing) noexcept
  {
    if (listing == nullptr) {
      return;
    }
    for (size_t i = 0; i < listing->count; ++i) {
      free(listing->names[i]);
    }
    free(listing->names);
    free(listing->directory);
    free(listing);
  }

  inline int CompareListedNames(const void* lhs, const void* rhs) noexcept
  {
    const char* const a = *static_cast<char* const*>(lhs);
    const char* const b = *static_cast<char* const*>(rhs);
    const int rankA = strcmp(a, ".") == 0 ? 0 : (strcmp(a, "..") == 0 ? 1 : 2);
    const int rankB = strcmp(b, ".") == 0 ? 0 : (strcmp(b, "..") == 0 ? 1 : 2);
    if (rankA != rankB) {
      return rankA < rankB ? -1 : 1;
    }
    return NtfsNameCompare(a, b);
  }

  inline DirectoryListing* OpenDirectoryListing(const char* spec, ListingStatus* status) noexcept
  {
    char directory[kMaxNativePath];
    char pattern[1024];
    if (!SplitFindSpec(spec, directory, sizeof(directory), pattern, sizeof(pattern))) {
      *status = errno == ENAMETOOLONG ? ListingStatus::NameTooLong : ListingStatus::NoMatch;
      return nullptr;
    }
    DIR* const stream = opendir(directory);
    if (stream == nullptr) {
      *status = ListingStatus::NoDirectory;
      return nullptr;
    }
    DirectoryListing* const listing = static_cast<DirectoryListing*>(calloc(1, sizeof(DirectoryListing)));
    if (listing == nullptr || (listing->directory = strdup(directory)) == nullptr) {
      free(listing);
      closedir(stream);
      *status = ListingStatus::OutOfMemory;
      return nullptr;
    }
    size_t capacity = 0;
    while (const dirent* const entry = readdir(stream)) {
      if (!MatchFindPattern(pattern, entry->d_name)) {
        continue;
      }
      if (listing->count == capacity) {
        const size_t grown = capacity != 0 ? capacity * 2 : 64;
        char** const names = static_cast<char**>(realloc(listing->names, grown * sizeof(char*)));
        if (names == nullptr) {
          break;
        }
        listing->names = names;
        capacity = grown;
      }
      char* const name = strdup(entry->d_name);
      if (name == nullptr) {
        break;
      }
      listing->names[listing->count++] = name;
    }
    closedir(stream);
    if (listing->count == 0) {
      CloseDirectoryListing(listing);
      *status = ListingStatus::NoMatch;
      return nullptr;
    }
    qsort(listing->names, listing->count, sizeof(char*), &CompareListedNames);
    *status = ListingStatus::Ok;
    return listing;
  }

  // stat of one listed name (following symlinks, as FindFirstFile reports the
  // target's size and times for a link to a file).
  inline bool StatListedName(const DirectoryListing& listing, const char* name, struct stat* status) noexcept
  {
    char path[kMaxNativePath];
    const size_t directoryLength = strlen(listing.directory);
    const size_t nameLength = strlen(name);
    if (directoryLength + 1 + nameLength + 1 > sizeof(path)) {
      return false;
    }
    memcpy(path, listing.directory, directoryLength);
    size_t used = directoryLength;
    if (used > 0 && path[used - 1] != '/') {
      path[used++] = '/';
    }
    memcpy(path + used, name, nameLength + 1);
    return stat(path, status) == 0;
  }

  // UTF-8 to wchar_t code points (UTF-32 here); a malformed sequence becomes
  // U+FFFD. Truncates to `count` - 1 characters; always terminates.
  inline size_t Utf8ToWide(const char* text, wchar_t* out, const size_t count) noexcept
  {
    if (count == 0) {
      return 0;
    }
    const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
    size_t used = 0;
    while (*p != '\0' && used + 1 < count) {
      uint32_t codePoint = 0xFFFDu;
      size_t extra = 0;
      if (*p < 0x80u) {
        codePoint = *p;
      } else if ((*p & 0xE0u) == 0xC0u) {
        codePoint = *p & 0x1Fu;
        extra = 1;
      } else if ((*p & 0xF0u) == 0xE0u) {
        codePoint = *p & 0x0Fu;
        extra = 2;
      } else if ((*p & 0xF8u) == 0xF0u) {
        codePoint = *p & 0x07u;
        extra = 3;
      }
      ++p;
      for (size_t i = 0; i < extra; ++i, ++p) {
        if ((*p & 0xC0u) != 0x80u) {
          codePoint = 0xFFFDu;
          break;
        }
        codePoint = (codePoint << 6) | (*p & 0x3Fu);
      }
      out[used++] = static_cast<wchar_t>(codePoint);
    }
    out[used] = L'\0';
    return used;
  }
} // namespace faf_compat

} // extern "C++"

#endif // FAF_PORT_ENGINE_SHIM_FAF_WIN_PATH_H
