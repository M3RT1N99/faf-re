#include "NativeFile.h"

#include <algorithm>
#include <cerrno>
#include <limits>
#include <string>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace faf::port::detail {

  namespace {
    /// Upper bound for one read call: Win32 takes a DWORD count, and POSIX
    /// pread may return short for very large requests anyway.
    constexpr std::size_t kMaxChunk = std::size_t{1} << 30;

#if !defined(_WIN32)
    // 32-bit Android without _FILE_OFFSET_BITS=64 has a 32-bit off_t; use the
    // explicit 64-bit entry point there so archives past 2 GiB still work.
#if defined(__ANDROID__) && !defined(__LP64__)
    ssize_t PositionalRead(int fd, void* buffer, std::size_t size, std::uint64_t offset)
    {
      return ::pread64(fd, buffer, size, static_cast<off64_t>(offset));
    }
#else
    static_assert(sizeof(off_t) >= 8, "Build with _FILE_OFFSET_BITS=64");
    ssize_t PositionalRead(int fd, void* buffer, std::size_t size, std::uint64_t offset)
    {
      return ::pread(fd, buffer, size, static_cast<off_t>(offset));
    }
#endif
#endif
  } // namespace

  std::filesystem::path PathFromUtf8(const std::string_view utf8)
  {
    std::u8string text;
    text.reserve(utf8.size());
    for (const char c : utf8) {
      text.push_back(static_cast<char8_t>(c));
    }
    return std::filesystem::path(text);
  }

  std::string Utf8FromPath(const std::filesystem::path& path)
  {
    const std::u8string text = path.u8string();
    std::string out;
    out.reserve(text.size());
    for (const char8_t c : text) {
      out.push_back(static_cast<char>(c));
    }
    return out;
  }

  std::FILE* OpenStdioFile(const std::string& utf8Path, const char* const mode)
  {
#if defined(_WIN32)
    std::wstring wideMode;
    for (const char* c = mode; *c != '\0'; ++c) {
      wideMode.push_back(static_cast<wchar_t>(*c));
    }
    return ::_wfopen(PathFromUtf8(utf8Path).c_str(), wideMode.c_str());
#else
    return std::fopen(utf8Path.c_str(), mode);
#endif
  }

  std::unique_ptr<ReadOnlyFile> ReadOnlyFile::Open(const std::string& utf8Path, std::string* const error)
  {
    std::unique_ptr<ReadOnlyFile> file(new ReadOnlyFile());
#if defined(_WIN32)
    const HANDLE handle = ::CreateFileW(
      PathFromUtf8(utf8Path).c_str(),
      GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,
      nullptr
    );
    if (handle == INVALID_HANDLE_VALUE) {
      const DWORD code = ::GetLastError();
      if (error != nullptr) {
        *error = utf8Path + ": " + std::system_category().message(static_cast<int>(code));
      }
      return nullptr;
    }
    file->mHandle = handle;

    LARGE_INTEGER size{};
    if (::GetFileSizeEx(handle, &size) == 0 || size.QuadPart < 0) {
      if (error != nullptr) {
        *error = utf8Path + ": cannot query the file size";
      }
      return nullptr;
    }
    file->mSize = static_cast<std::uint64_t>(size.QuadPart);
#else
    int fd = -1;
    do {
      fd = ::open(utf8Path.c_str(), O_RDONLY | O_CLOEXEC);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
      const int code = errno;
      if (error != nullptr) {
        *error = utf8Path + ": " + std::generic_category().message(code);
      }
      return nullptr;
    }
    file->mFd = fd;

    struct stat info{};
    if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0) {
      if (error != nullptr) {
        *error = utf8Path + ": not a regular file";
      }
      return nullptr;
    }
    file->mSize = static_cast<std::uint64_t>(info.st_size);
#endif
    return file;
  }

  ReadOnlyFile::~ReadOnlyFile()
  {
#if defined(_WIN32)
    if (mHandle != nullptr) {
      ::CloseHandle(static_cast<HANDLE>(mHandle));
    }
#else
    if (mFd >= 0) {
      ::close(mFd);
    }
#endif
  }

  bool ReadOnlyFile::ReadAt(std::uint64_t offset, void* const buffer, std::size_t size) const
  {
    if (offset > mSize || size > mSize - offset) {
      return false;
    }

    auto* out = static_cast<unsigned char*>(buffer);
    while (size > 0) {
      const std::size_t request = std::min(size, kMaxChunk);
#if defined(_WIN32)
      OVERLAPPED position{};
      position.Offset = static_cast<DWORD>(offset & 0xFFFFFFFFu);
      position.OffsetHigh = static_cast<DWORD>(offset >> 32);
      DWORD got = 0;
      if (::ReadFile(static_cast<HANDLE>(mHandle), out, static_cast<DWORD>(request), &got, &position) == 0 ||
          got == 0) {
        return false;
      }
      const std::size_t received = got;
#else
      const ssize_t got = PositionalRead(mFd, out, request, offset);
      if (got < 0) {
        if (errno == EINTR) {
          continue;
        }
        return false;
      }
      if (got == 0) {
        return false;
      }
      const auto received = static_cast<std::size_t>(got);
#endif
      out += received;
      offset += received;
      size -= received;
    }
    return true;
  }

} // namespace faf::port::detail
