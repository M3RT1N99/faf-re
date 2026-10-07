#pragma once

// The shell functions the engine calls (shlobj.h, shlwapi.h, shellapi.h), for
// the non-Windows targets of the port. Users: moho/misc/StartupHelpers.cpp
// (SHGetFolderPathW for the user's documents and app-data directories and the
// SHGetFolderPath Lua binder that FAF's init_faf.lua calls for its shader
// cache; PathFileExistsW; SHFileOperationA/W FO_DELETE for the cache purge and
// DISK_Recycle).
//
// Known folders: Windows keeps them under the user profile
// (C:\Users\<name>\Documents, \AppData\Local, ...). Here they keep the same
// layout under one root directory: $FAF_KNOWN_FOLDERS when set, else $HOME
// when it names a directory other than "/", else $TMPDIR, else
// /data/local/tmp. Where the app's storage lies is the platform layer's
// choice (W2, docs/port/android-roadmap.md); it sets FAF_KNOWN_FOLDERS. The
// folder is created when missing, since on Windows these always exist.
// Paths come back with '/' separators; the engine appends '\' and the shim's
// file functions take both. Virtual folders (CSIDL_BITBUCKET, ...) fail with
// E_INVALIDARG, as they do on Windows.
//
// SHFileOperation supports FO_DELETE only (other operations return
// ERROR_NOT_SUPPORTED): every name in the double-null-terminated list is
// expanded (wildcards in its last component, as FindFirstFile does) and
// deleted, directories with their contents. There is no recycle bin, so
// FOF_ALLOWUNDO deletes as well.

#if defined(_WIN32) || defined(_MSC_VER)
#error "port/engine/shim is for non-Windows targets; it must not be on a Windows include path"
#endif

#include "faf_win_compat.h"

#include <ftw.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

extern "C++" {

#define CSIDL_DESKTOP 0x0000
#define CSIDL_PROGRAMS 0x0002
#define CSIDL_PERSONAL 0x0005
#define CSIDL_FAVORITES 0x0006
#define CSIDL_STARTUP 0x0007
#define CSIDL_RECENT 0x0008
#define CSIDL_SENDTO 0x0009
#define CSIDL_BITBUCKET 0x000a
#define CSIDL_STARTMENU 0x000b
#define CSIDL_MYDOCUMENTS CSIDL_PERSONAL
#define CSIDL_MYMUSIC 0x000d
#define CSIDL_MYVIDEO 0x000e
#define CSIDL_DESKTOPDIRECTORY 0x0010
#define CSIDL_FONTS 0x0014
#define CSIDL_TEMPLATES 0x0015
#define CSIDL_COMMON_STARTMENU 0x0016
#define CSIDL_COMMON_PROGRAMS 0x0017
#define CSIDL_COMMON_STARTUP 0x0018
#define CSIDL_COMMON_DESKTOPDIRECTORY 0x0019
#define CSIDL_APPDATA 0x001a
#define CSIDL_LOCAL_APPDATA 0x001c
#define CSIDL_COMMON_FAVORITES 0x001f
#define CSIDL_COMMON_APPDATA 0x0023
#define CSIDL_WINDOWS 0x0024
#define CSIDL_SYSTEM 0x0025
#define CSIDL_PROGRAM_FILES 0x0026
#define CSIDL_MYPICTURES 0x0027
#define CSIDL_PROFILE 0x0028
#define CSIDL_SYSTEMX86 0x0029
#define CSIDL_PROGRAM_FILESX86 0x002a
#define CSIDL_PROGRAM_FILES_COMMON 0x002b
#define CSIDL_PROGRAM_FILES_COMMONX86 0x002c
#define CSIDL_COMMON_TEMPLATES 0x002d
#define CSIDL_COMMON_DOCUMENTS 0x002e
#define CSIDL_COMMON_MUSIC 0x0035
#define CSIDL_COMMON_PICTURES 0x0036
#define CSIDL_COMMON_VIDEO 0x0037
#define CSIDL_FLAG_DONT_VERIFY 0x4000
#define CSIDL_FLAG_CREATE 0x8000
#define CSIDL_FLAG_MASK 0xFF00

#define SHGFP_TYPE_CURRENT 0
#define SHGFP_TYPE_DEFAULT 1

#define FO_MOVE 0x0001
#define FO_COPY 0x0002
#define FO_DELETE 0x0003
#define FO_RENAME 0x0004

#define FOF_MULTIDESTFILES 0x0001
#define FOF_CONFIRMMOUSE 0x0002
#define FOF_SILENT 0x0004
#define FOF_RENAMEONCOLLISION 0x0008
#define FOF_NOCONFIRMATION 0x0010
#define FOF_WANTMAPPINGHANDLE 0x0020
#define FOF_ALLOWUNDO 0x0040
#define FOF_FILESONLY 0x0080
#define FOF_SIMPLEPROGRESS 0x0100
#define FOF_NOCONFIRMMKDIR 0x0200
#define FOF_NOERRORUI 0x0400

typedef WORD FILEOP_FLAGS;

typedef struct _SHFILEOPSTRUCTA {
  HWND hwnd;
  UINT wFunc;
  LPCSTR pFrom;
  LPCSTR pTo;
  FILEOP_FLAGS fFlags;
  BOOL fAnyOperationsAborted;
  LPVOID hNameMappings;
  LPCSTR lpszProgressTitle;
} SHFILEOPSTRUCTA, *LPSHFILEOPSTRUCTA;

typedef struct _SHFILEOPSTRUCTW {
  HWND hwnd;
  UINT wFunc;
  LPCWSTR pFrom;
  LPCWSTR pTo;
  FILEOP_FLAGS fFlags;
  BOOL fAnyOperationsAborted;
  LPVOID hNameMappings;
  LPCWSTR lpszProgressTitle;
} SHFILEOPSTRUCTW, *LPSHFILEOPSTRUCTW;

namespace faf_compat
{
  // The profile-relative location of a known folder (Windows Vista and later
  // layout), or null for a virtual folder.
  inline const char* KnownFolderPath(const int csidl) noexcept
  {
    switch (csidl) {
      case CSIDL_PROFILE:
        return "";
      case CSIDL_DESKTOP:
      case CSIDL_DESKTOPDIRECTORY:
        return "Desktop";
      case CSIDL_PERSONAL:
      case 0x000c:  // CSIDL_MYDOCUMENTS of older SDKs
        return "Documents";
      case CSIDL_FAVORITES:
      case CSIDL_COMMON_FAVORITES:
        return "Favorites";
      case CSIDL_MYMUSIC:
        return "Music";
      case CSIDL_MYVIDEO:
        return "Videos";
      case CSIDL_MYPICTURES:
        return "Pictures";
      case CSIDL_APPDATA:
        return "AppData/Roaming";
      case CSIDL_LOCAL_APPDATA:
        return "AppData/Local";
      case CSIDL_STARTMENU:
        return "AppData/Roaming/Microsoft/Windows/Start Menu";
      case CSIDL_PROGRAMS:
        return "AppData/Roaming/Microsoft/Windows/Start Menu/Programs";
      case CSIDL_STARTUP:
        return "AppData/Roaming/Microsoft/Windows/Start Menu/Programs/Startup";
      case CSIDL_RECENT:
        return "AppData/Roaming/Microsoft/Windows/Recent";
      case CSIDL_SENDTO:
        return "AppData/Roaming/Microsoft/Windows/SendTo";
      case CSIDL_TEMPLATES:
        return "AppData/Roaming/Microsoft/Windows/Templates";
      case CSIDL_COMMON_APPDATA:
        return "ProgramData";
      case CSIDL_COMMON_STARTMENU:
        return "ProgramData/Microsoft/Windows/Start Menu";
      case CSIDL_COMMON_PROGRAMS:
        return "ProgramData/Microsoft/Windows/Start Menu/Programs";
      case CSIDL_COMMON_STARTUP:
        return "ProgramData/Microsoft/Windows/Start Menu/Programs/Startup";
      case CSIDL_COMMON_TEMPLATES:
        return "ProgramData/Microsoft/Windows/Templates";
      case CSIDL_COMMON_DESKTOPDIRECTORY:
        return "Public/Desktop";
      case CSIDL_COMMON_DOCUMENTS:
        return "Public/Documents";
      case CSIDL_COMMON_MUSIC:
        return "Public/Music";
      case CSIDL_COMMON_PICTURES:
        return "Public/Pictures";
      case CSIDL_COMMON_VIDEO:
        return "Public/Videos";
      case CSIDL_WINDOWS:
        return "Windows";
      case CSIDL_FONTS:
        return "Windows/Fonts";
      case CSIDL_SYSTEM:
        return "Windows/System32";
      case CSIDL_SYSTEMX86:
        return "Windows/SysWOW64";
      case CSIDL_PROGRAM_FILES:
        return "Program Files";
      case CSIDL_PROGRAM_FILESX86:
        return "Program Files (x86)";
      case CSIDL_PROGRAM_FILES_COMMON:
        return "Program Files/Common Files";
      case CSIDL_PROGRAM_FILES_COMMONX86:
        return "Program Files (x86)/Common Files";
      default:
        return nullptr;
    }
  }

  inline bool IsUsableRoot(const char* path) noexcept
  {
    struct stat status;
    return path != nullptr && path[0] != '\0' && strcmp(path, "/") != 0 && stat(path, &status) == 0 &&
      S_ISDIR(status.st_mode);
  }

  inline const char* KnownFoldersRoot() noexcept
  {
    const char* const candidates[] = {getenv("FAF_KNOWN_FOLDERS"), getenv("HOME"), getenv("TMPDIR")};
    for (const char* candidate : candidates) {
      if (IsUsableRoot(candidate)) {
        return candidate;
      }
    }
    return "/data/local/tmp";
  }

  // mkdir -p of a native path.
  inline bool CreateDirectoryChain(char* path) noexcept
  {
    for (char* p = path + 1; *p != '\0'; ++p) {
      if (*p != '/') {
        continue;
      }
      *p = '\0';
      const int result = mkdir(path, 0777);
      *p = '/';
      if (result != 0 && errno != EEXIST) {
        return false;
      }
    }
    return mkdir(path, 0777) == 0 || errno == EEXIST;
  }

  inline int RemoveTreeEntry(const char* path, const struct stat*, int type, struct FTW*) noexcept
  {
    return (type == FTW_DP ? rmdir(path) : unlink(path)) == 0 ? 0 : -1;
  }

  // Deletes what one FO_DELETE name matches; false when nothing matched or a
  // deletion failed.
  inline bool DeleteMatches(const char* spec, const bool filesOnly) noexcept
  {
    ListingStatus status = ListingStatus::Ok;
    DirectoryListing* const listing = OpenDirectoryListing(spec, &status);
    if (listing == nullptr) {
      return false;
    }
    bool ok = true;
    for (size_t i = 0; i < listing->count; ++i) {
      const char* const name = listing->names[i];
      if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        continue;
      }
      char path[kMaxNativePath];
      const int length = snprintf(path, sizeof(path), "%s/%s", listing->directory, name);
      if (length <= 0 || static_cast<size_t>(length) >= sizeof(path)) {
        ok = false;
        continue;
      }
      struct stat entry;
      if (lstat(path, &entry) != 0) {
        ok = false;
        continue;
      }
      if (S_ISDIR(entry.st_mode)) {
        if (!filesOnly && nftw(path, &RemoveTreeEntry, 16, FTW_DEPTH | FTW_PHYS) != 0) {
          ok = false;
        }
      } else if (unlink(path) != 0) {
        ok = false;
      }
    }
    CloseDirectoryListing(listing);
    return ok;
  }
} // namespace faf_compat

extern "C" {

// S_OK, or E_INVALIDARG for a folder that has no location here, E_FAIL when
// the folder cannot be created. `path` receives at most MAX_PATH characters.
inline HRESULT SHGetFolderPathA(HWND, int folder, HANDLE, DWORD, LPSTR path) noexcept
{
  if (path == nullptr) {
    return E_INVALIDARG;
  }
  path[0] = '\0';
  const char* const relative = faf_compat::KnownFolderPath(folder & ~CSIDL_FLAG_MASK);
  if (relative == nullptr) {
    return E_INVALIDARG;
  }
  char full[faf_compat::kMaxNativePath];
  const char* const root = faf_compat::KnownFoldersRoot();
  const int length = relative[0] != '\0' ? snprintf(full, sizeof(full), "%s/%s", root, relative)
                                         : snprintf(full, sizeof(full), "%s", root);
  if (length <= 0 || length >= MAX_PATH) {
    return E_FAIL;
  }
  if (!faf_compat::CreateDirectoryChain(full)) {
    return E_FAIL;
  }
  memcpy(path, full, static_cast<size_t>(length) + 1);
  return S_OK;
}

inline HRESULT SHGetFolderPathW(HWND window, int folder, HANDLE token, DWORD flags, LPWSTR path) noexcept
{
  if (path == nullptr) {
    return E_INVALIDARG;
  }
  path[0] = L'\0';
  char narrow[MAX_PATH];
  const HRESULT result = SHGetFolderPathA(window, folder, token, flags, narrow);
  if (result == S_OK) {
    (void)faf_compat::Utf8ToWide(narrow, path, MAX_PATH);
  }
  return result;
}

inline BOOL PathFileExistsA(LPCSTR path) noexcept
{
  return path != nullptr && GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES ? TRUE : FALSE;
}

inline BOOL PathFileExistsW(LPCWSTR path) noexcept
{
  return path != nullptr && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES ? TRUE : FALSE;
}

// 0 on success, else a nonzero error code (as SHFileOperation's own codes
// are, they are not GetLastError values).
inline int SHFileOperationA(LPSHFILEOPSTRUCTA operation) noexcept
{
  if (operation == nullptr || operation->pFrom == nullptr) {
    return ERROR_INVALID_PARAMETER;
  }
  operation->fAnyOperationsAborted = FALSE;
  if (operation->wFunc != FO_DELETE) {
    return ERROR_NOT_SUPPORTED;
  }
  int result = 0;
  for (const char* name = operation->pFrom; *name != '\0'; name += strlen(name) + 1) {
    if (!faf_compat::DeleteMatches(name, (operation->fFlags & FOF_FILESONLY) != 0)) {
      result = ERROR_FILE_NOT_FOUND;
    }
  }
  return result;
}

inline int SHFileOperationW(LPSHFILEOPSTRUCTW operation) noexcept
{
  if (operation == nullptr || operation->pFrom == nullptr) {
    return ERROR_INVALID_PARAMETER;
  }
  operation->fAnyOperationsAborted = FALSE;
  if (operation->wFunc != FO_DELETE) {
    return ERROR_NOT_SUPPORTED;
  }
  int result = 0;
  for (const wchar_t* name = operation->pFrom; *name != L'\0'; name += wcslen(name) + 1) {
    char narrow[faf_compat::kMaxNativePath];
    if (!faf_compat::WideToUtf8(name, narrow, sizeof(narrow)) ||
        !faf_compat::DeleteMatches(narrow, (operation->fFlags & FOF_FILESONLY) != 0)) {
      result = ERROR_FILE_NOT_FOUND;
    }
  }
  return result;
}

} // extern "C"

} // extern "C++"
