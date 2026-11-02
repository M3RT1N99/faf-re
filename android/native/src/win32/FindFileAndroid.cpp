// FindFirstFile, FindNextFile (A and W) and FindClose (include/win32/Windows.h) on POSIX
// directories, with Win32's matching: `*` and `?` in the pattern's last part, letters in any case, "." and ".."
// listed like any entry. The render info counts the shader cache's files with them
// (moho/render/RenderInfo.cpp), the startup code finds the game's files with the W pair. The
// wide names are UTF-8 on the file system.

#include <Windows.h>

#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace
{
  struct Search
  {
    DIR* directory = nullptr;
    std::string folder;
    std::string pattern;
  };

  /// Seconds since 1970 as a FILETIME (100 ns since 1601).
  FILETIME FileTime(const timespec& time)
  {
    constexpr std::uint64_t kEpochDifference = 11644473600ULL;
    const std::uint64_t ticks =
      (static_cast<std::uint64_t>(time.tv_sec) + kEpochDifference) * 10000000ULL + static_cast<std::uint64_t>(time.tv_nsec) / 100U;
    return FILETIME{static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32)};
  }

  /// The next entry of `search` that matches its pattern, into `data`.
  bool Next(Search& search, WIN32_FIND_DATAA* const data)
  {
    while (const dirent* const entry = ::readdir(search.directory)) {
      if (::fnmatch(search.pattern.c_str(), entry->d_name, FNM_CASEFOLD) != 0) {
        continue;
      }
      const std::string path = search.folder + "/" + entry->d_name;
      struct stat info{};
      if (::stat(path.c_str(), &info) != 0) {
        continue;
      }
      *data = WIN32_FIND_DATAA{};
      data->dwFileAttributes = S_ISDIR(info.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_ARCHIVE;
      data->ftCreationTime = FileTime(info.st_ctim);
      data->ftLastAccessTime = FileTime(info.st_atim);
      data->ftLastWriteTime = FileTime(info.st_mtim);
      const std::uint64_t size = S_ISDIR(info.st_mode) ? 0U : static_cast<std::uint64_t>(info.st_size);
      data->nFileSizeHigh = static_cast<DWORD>(size >> 32);
      data->nFileSizeLow = static_cast<DWORD>(size);
      std::strncpy(data->cFileName, entry->d_name, MAX_PATH - 1);
      return true;
    }
    return false;
  }
} // namespace

HANDLE FindFirstFileA(const char* const pattern, WIN32_FIND_DATAA* const data)
{
  if (pattern == nullptr || data == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return INVALID_HANDLE_VALUE;
  }
  std::string path = pattern;
  std::replace(path.begin(), path.end(), '\\', '/');
  const std::size_t slash = path.find_last_of('/');
  auto* const search = new Search;
  search->folder = (slash == std::string::npos) ? std::string(".") : path.substr(0, slash);
  search->pattern = (slash == std::string::npos) ? path : path.substr(slash + 1);
  search->directory = ::opendir(search->folder.c_str());
  if (search->directory == nullptr) {
    delete search;
    ::SetLastError(ERROR_PATH_NOT_FOUND);
    return INVALID_HANDLE_VALUE;
  }
  if (!Next(*search, data)) {
    ::closedir(search->directory);
    delete search;
    ::SetLastError(ERROR_FILE_NOT_FOUND);
    return INVALID_HANDLE_VALUE;
  }
  return search;
}

BOOL FindNextFileA(const HANDLE handle, WIN32_FIND_DATAA* const data)
{
  if (handle == nullptr || handle == INVALID_HANDLE_VALUE || data == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (!Next(*static_cast<Search*>(handle), data)) {
    ::SetLastError(ERROR_NO_MORE_FILES);
    return FALSE;
  }
  return TRUE;
}

BOOL FindClose(const HANDLE handle)
{
  if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  auto* const search = static_cast<Search*>(handle);
  ::closedir(search->directory);
  delete search;
  return TRUE;
}

namespace
{
  void Widen(const WIN32_FIND_DATAA& narrow, WIN32_FIND_DATAW* const wide)
  {
    *wide = WIN32_FIND_DATAW{};
    wide->dwFileAttributes = narrow.dwFileAttributes;
    wide->ftCreationTime = narrow.ftCreationTime;
    wide->ftLastAccessTime = narrow.ftLastAccessTime;
    wide->ftLastWriteTime = narrow.ftLastWriteTime;
    wide->nFileSizeHigh = narrow.nFileSizeHigh;
    wide->nFileSizeLow = narrow.nFileSizeLow;
    (void)::MultiByteToWideChar(CP_UTF8, 0, narrow.cFileName, -1, wide->cFileName, MAX_PATH);
    wide->cFileName[MAX_PATH - 1] = 0;
  }
} // namespace

HANDLE FindFirstFileW(const wchar_t* const pattern, WIN32_FIND_DATAW* const data)
{
  if (pattern == nullptr || data == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return INVALID_HANDLE_VALUE;
  }
  const int length = ::WideCharToMultiByte(CP_UTF8, 0, pattern, -1, nullptr, 0, nullptr, nullptr);
  std::string narrow(static_cast<std::size_t>(std::max(length, 1)), '\0');
  (void)::WideCharToMultiByte(CP_UTF8, 0, pattern, -1, narrow.data(), length, nullptr, nullptr);
  WIN32_FIND_DATAA found{};
  const HANDLE search = ::FindFirstFileA(narrow.c_str(), &found);
  if (search != INVALID_HANDLE_VALUE) {
    Widen(found, data);
  }
  return search;
}

BOOL FindNextFileW(const HANDLE handle, WIN32_FIND_DATAW* const data)
{
  if (data == nullptr) {
    ::SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  WIN32_FIND_DATAA found{};
  if (::FindNextFileA(handle, &found) == FALSE) {
    return FALSE;
  }
  Widen(found, data);
  return TRUE;
}
