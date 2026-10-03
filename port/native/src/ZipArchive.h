#pragma once

// Internal: read-only zip archives (.scd stored, .nx2 deflate), the portable
// counterpart of moho::CZipFile (src/sdk/moho/misc/CZipFile.cpp).
//
// Every field read from the file is bounds-checked against the file size and
// the central directory before it is used, so a truncated or corrupt archive
// fails to open or fails one read with an error message - it never reads
// outside its buffers or allocates more than the file can justify. Engine
// behaviour kept on purpose: entries with a data descriptor (flag 0x08) are
// dropped, duplicate names resolve to the first entry, and CRCs are not
// checked (the engine does not either, so a CRC check could reject data the
// game accepts). Zip64 is read although the engine cannot.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "NativeFile.h"

namespace faf::port::detail {

  struct ZipEntry
  {
    std::string name; ///< As stored in the central directory.
    std::uint64_t compressedSize = 0;
    std::uint64_t uncompressedSize = 0;
    std::uint64_t localHeaderOffset = 0;
    std::uint16_t method = 0; ///< 0 stored, 8 deflate.
    std::uint16_t flags = 0;
    bool isDirectory = false; ///< Name ends in '/' (or '\\').
  };

  class ZipArchive
  {
  public:
    struct IndexItem
    {
      std::string canonical; ///< CanonicalName(entry.name).
      std::uint32_t entry = 0;
    };

    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    [[nodiscard]] static std::unique_ptr<ZipArchive> Open(const std::string& utf8Path, std::string* error);

    /// Lower-cased, '\\' -> '/', leading '/' removed: the key lookups use.
    [[nodiscard]] static std::string CanonicalName(std::string_view name);

    [[nodiscard]] const std::string& Path() const
    {
      return mPath;
    }

    /// Usable entries in archive order (data-descriptor entries dropped,
    /// directory entries kept - they count like the engine's mEntries).
    [[nodiscard]] const std::vector<ZipEntry>& Entries() const
    {
      return mEntries;
    }

    /// Entries dropped because they use a data descriptor.
    [[nodiscard]] std::size_t SkippedEntries() const
    {
      return mSkipped;
    }

    /// Entry index for a canonical file name, or npos. Directory entries are not indexed.
    [[nodiscard]] std::size_t Find(std::string_view canonicalName) const;

    /// File entries whose canonical name starts with `canonicalPrefix`, in canonical order.
    [[nodiscard]] std::span<const IndexItem> WithPrefix(std::string_view canonicalPrefix) const;

    /// Reads and, for deflate, inflates entry `index`. Thread-safe.
    bool Read(std::size_t index, std::vector<std::uint8_t>& out, std::string* error) const;

  private:
    ZipArchive() = default;

    std::string mPath;
    std::unique_ptr<ReadOnlyFile> mFile;
    std::vector<ZipEntry> mEntries;
    std::vector<IndexItem> mIndex; ///< Sorted by canonical name, one item per name (first entry wins).
    std::size_t mSkipped = 0;
  };

} // namespace faf::port::detail
