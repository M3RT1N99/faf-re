#pragma once

// Test fixtures: scratch directories and zip archives built in memory, so the
// unit tests need no files from the repository or the game.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <zlib.h>

#include "NativeFile.h"
#include "TestHarness.h"

namespace faf::test {

  /// A fresh directory, removed again when the test ends. Lives under
  /// $FAF_PORT_TEST_TMP (CTest points it into the build tree) or the system
  /// temp directory.
  class TempDir
  {
  public:
    TempDir()
    {
      static std::atomic<unsigned> counter{0};
      std::filesystem::path base;
      if (const char* const configured = std::getenv("FAF_PORT_TEST_TMP"); configured != nullptr && *configured) {
        base = port::detail::PathFromUtf8(configured);
      } else {
        base = std::filesystem::temp_directory_path() / "faf_port_tests";
      }
      const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
      mPath = base / ("t" + std::to_string(stamp) + "_" + std::to_string(counter++));
      std::error_code ec;
      std::filesystem::create_directories(mPath, ec);
      FAF_REQUIRE(!ec);
    }

    ~TempDir()
    {
      std::error_code ec;
      std::filesystem::remove_all(mPath, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    /// UTF-8, '/' separators.
    [[nodiscard]] std::string Path() const
    {
      std::string text = port::detail::Utf8FromPath(mPath);
      for (char& c : text) {
        if (c == '\\') {
          c = '/';
        }
      }
      return text;
    }

    [[nodiscard]] std::string operator/(const std::string_view relative) const
    {
      return Path() + "/" + std::string(relative);
    }

  private:
    std::filesystem::path mPath;
  };

  inline void WriteFile(const std::string& utf8Path, const std::string_view bytes)
  {
    const std::filesystem::path path = port::detail::PathFromUtf8(utf8Path);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    FAF_REQUIRE(out.good());
  }

  inline void WriteFile(const std::string& utf8Path, const std::vector<std::uint8_t>& bytes)
  {
    WriteFile(utf8Path, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
  }

  inline void MakeDirectory(const std::string& utf8Path)
  {
    std::error_code ec;
    std::filesystem::create_directories(port::detail::PathFromUtf8(utf8Path), ec);
    FAF_REQUIRE(!ec);
  }

  [[nodiscard]] inline bool Exists(const std::string& utf8Path)
  {
    std::error_code ec;
    return std::filesystem::exists(port::detail::PathFromUtf8(utf8Path), ec);
  }

  enum class ZipMethod
  {
    Stored,
    Deflate,
    /// Method 8 with no data at all (csize 0, usize 0): how FAF's lua.nx2
    /// stores some empty files.
    EmptyDeflateNoStream,
  };

  struct ZipEntrySpec
  {
    std::string name;
    std::string data;
    ZipMethod method = ZipMethod::Deflate;
    bool dataDescriptor = false; ///< General purpose flag 0x08 plus a trailing descriptor.
  };

  [[nodiscard]] inline std::vector<std::uint8_t> RawDeflate(const std::string_view data)
  {
    z_stream stream{};
    FAF_REQUIRE(deflateInit2(&stream, 9, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) == Z_OK);
    std::vector<std::uint8_t> out(deflateBound(&stream, static_cast<uLong>(data.size())) + 16);
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    stream.avail_in = static_cast<uInt>(data.size());
    stream.next_out = out.data();
    stream.avail_out = static_cast<uInt>(out.size());
    const int status = deflate(&stream, Z_FINISH);
    out.resize(out.size() - stream.avail_out);
    deflateEnd(&stream);
    FAF_REQUIRE(status == Z_STREAM_END);
    return out;
  }

  /// Builds a zip archive in memory. `zip64` writes every size and offset
  /// through zip64 extra fields and a zip64 end record.
  [[nodiscard]] inline std::vector<std::uint8_t> BuildZip(
    const std::vector<ZipEntrySpec>& entries, const bool zip64 = false, const std::string_view comment = {}
  )
  {
    std::vector<std::uint8_t> out;
    const auto u16 = [](std::vector<std::uint8_t>& v, const std::uint64_t x) {
      v.push_back(static_cast<std::uint8_t>(x));
      v.push_back(static_cast<std::uint8_t>(x >> 8));
    };
    const auto u32 = [&](std::vector<std::uint8_t>& v, const std::uint64_t x) {
      u16(v, x & 0xFFFF);
      u16(v, (x >> 16) & 0xFFFF);
    };
    const auto u64 = [&](std::vector<std::uint8_t>& v, const std::uint64_t x) {
      u32(v, x & 0xFFFFFFFFu);
      u32(v, x >> 32);
    };
    const auto bytes = [](std::vector<std::uint8_t>& v, const std::string_view s) {
      v.insert(v.end(), s.begin(), s.end());
    };

    std::vector<std::uint8_t> central;
    for (const ZipEntrySpec& entry : entries) {
      std::vector<std::uint8_t> payload;
      std::uint16_t method = 8;
      switch (entry.method) {
      case ZipMethod::Stored:
        method = 0;
        payload.assign(entry.data.begin(), entry.data.end());
        break;
      case ZipMethod::Deflate:
        payload = RawDeflate(entry.data);
        break;
      case ZipMethod::EmptyDeflateNoStream:
        break;
      }
      const std::uint32_t crc = static_cast<std::uint32_t>(
        crc32(0L, reinterpret_cast<const Bytef*>(entry.data.data()), static_cast<uInt>(entry.data.size()))
      );
      const std::uint16_t flags = entry.dataDescriptor ? 0x0008 : 0x0000;
      const std::uint64_t localOffset = out.size();

      std::vector<std::uint8_t> localExtra;
      std::vector<std::uint8_t> centralExtra;
      if (zip64) {
        u16(localExtra, 0x0001);
        u16(localExtra, 16);
        u64(localExtra, entry.data.size());
        u64(localExtra, payload.size());
        u16(centralExtra, 0x0001);
        u16(centralExtra, 24);
        u64(centralExtra, entry.data.size());
        u64(centralExtra, payload.size());
        u64(centralExtra, localOffset);
      }

      u32(out, 0x04034b50);
      u16(out, zip64 ? 45 : 20);
      u16(out, flags);
      u16(out, method);
      u32(out, 0); // DOS time + date
      u32(out, entry.dataDescriptor ? 0 : crc);
      u32(out, entry.dataDescriptor ? 0 : (zip64 ? 0xFFFFFFFFu : payload.size()));
      u32(out, entry.dataDescriptor ? 0 : (zip64 ? 0xFFFFFFFFu : entry.data.size()));
      u16(out, entry.name.size());
      u16(out, localExtra.size());
      bytes(out, entry.name);
      out.insert(out.end(), localExtra.begin(), localExtra.end());
      out.insert(out.end(), payload.begin(), payload.end());
      if (entry.dataDescriptor) {
        u32(out, 0x08074b50);
        u32(out, crc);
        u32(out, payload.size());
        u32(out, entry.data.size());
      }

      u32(central, 0x02014b50);
      u16(central, zip64 ? 45 : 20);
      u16(central, zip64 ? 45 : 20);
      u16(central, flags);
      u16(central, method);
      u32(central, 0);
      u32(central, crc);
      u32(central, zip64 ? 0xFFFFFFFFu : payload.size());
      u32(central, zip64 ? 0xFFFFFFFFu : entry.data.size());
      u16(central, entry.name.size());
      u16(central, centralExtra.size());
      u16(central, 0); // comment
      u16(central, 0); // disk
      u16(central, 0); // internal attributes
      u32(central, 0); // external attributes
      u32(central, zip64 ? 0xFFFFFFFFu : localOffset);
      bytes(central, entry.name);
      central.insert(central.end(), centralExtra.begin(), centralExtra.end());
    }

    const std::uint64_t centralOffset = out.size();
    out.insert(out.end(), central.begin(), central.end());
    if (zip64) {
      const std::uint64_t recordOffset = out.size();
      u32(out, 0x06064b50);
      u64(out, 44);
      u16(out, 45);
      u16(out, 45);
      u32(out, 0);
      u32(out, 0);
      u64(out, entries.size());
      u64(out, entries.size());
      u64(out, central.size());
      u64(out, centralOffset);
      u32(out, 0x07064b50);
      u32(out, 0);
      u64(out, recordOffset);
      u32(out, 1);
    }
    u32(out, 0x06054b50);
    u16(out, 0);
    u16(out, 0);
    u16(out, zip64 ? 0xFFFF : entries.size());
    u16(out, zip64 ? 0xFFFF : entries.size());
    u32(out, zip64 ? 0xFFFFFFFFu : central.size());
    u32(out, zip64 ? 0xFFFFFFFFu : centralOffset);
    u16(out, comment.size());
    bytes(out, comment);
    return out;
  }

} // namespace faf::test
