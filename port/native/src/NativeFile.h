#pragma once

// Internal: the few OS calls the core needs, behind one interface for Windows
// (host tools and tests) and POSIX (Android).
//
// - Paths are UTF-8. std::filesystem::path(std::string) decodes with the ANSI
//   code page on Windows, so every conversion goes through char8_t instead.
// - ReadOnlyFile::ReadAt is a positional read (pread / ReadFile with an
//   OVERLAPPED offset). It never moves a shared file pointer, so one open
//   archive serves any number of concurrent readers without a lock, and
//   offsets are 64-bit everywhere (archives past 2/4 GiB are fine).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace faf::port::detail {

  [[nodiscard]] std::filesystem::path PathFromUtf8(std::string_view utf8);
  [[nodiscard]] std::string Utf8FromPath(const std::filesystem::path& path);

  /// fopen with a UTF-8 path (_wfopen on Windows). `mode` is ASCII.
  [[nodiscard]] std::FILE* OpenStdioFile(const std::string& utf8Path, const char* mode);

  class ReadOnlyFile
  {
  public:
    /// Opens a regular file for reading. Other processes may keep reading,
    /// writing or deleting it (the Windows share mode is fully permissive).
    [[nodiscard]] static std::unique_ptr<ReadOnlyFile> Open(const std::string& utf8Path, std::string* error);

    ~ReadOnlyFile();
    ReadOnlyFile(const ReadOnlyFile&) = delete;
    ReadOnlyFile& operator=(const ReadOnlyFile&) = delete;

    /// Size at open time; ReadAt never reads past it.
    [[nodiscard]] std::uint64_t Size() const
    {
      return mSize;
    }

    /// Reads exactly `size` bytes at `offset`. False on an I/O error, a short
    /// read, or a range outside [0, Size()).
    bool ReadAt(std::uint64_t offset, void* buffer, std::size_t size) const;

  private:
    ReadOnlyFile() = default;

#if defined(_WIN32)
    void* mHandle = nullptr; ///< HANDLE; void* keeps <windows.h> out of this header.
#else
    int mFd = -1;
#endif
    std::uint64_t mSize = 0;
  };

} // namespace faf::port::detail
