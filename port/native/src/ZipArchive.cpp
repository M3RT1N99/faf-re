#include "ZipArchive.h"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <utility>

#include <zlib.h>

#include "FileSystemDetail.h"

namespace faf::port::detail {

  namespace {
    constexpr std::uint32_t kLocalHeaderSignature = 0x04034b50u;
    constexpr std::uint32_t kCentralHeaderSignature = 0x02014b50u;
    constexpr std::uint32_t kEndOfCentralDirectorySignature = 0x06054b50u;
    constexpr std::uint32_t kZip64EndSignature = 0x06064b50u;
    constexpr std::uint32_t kZip64LocatorSignature = 0x07064b50u;

    constexpr std::size_t kLocalHeaderSize = 30;
    constexpr std::size_t kCentralHeaderSize = 46;
    constexpr std::size_t kEndOfCentralDirectorySize = 22;
    constexpr std::size_t kZip64LocatorSize = 20;
    constexpr std::size_t kZip64EndSize = 56;
    constexpr std::size_t kMaxCommentSize = 0xFFFF;

    constexpr std::uint16_t kFlagEncrypted = 0x0001;
    constexpr std::uint16_t kFlagDataDescriptor = 0x0008;
    constexpr std::uint16_t kZip64ExtraId = 0x0001;
    constexpr std::uint16_t kMethodStored = 0;
    constexpr std::uint16_t kMethodDeflate = 8;
    constexpr std::uint32_t kSaturated32 = 0xFFFFFFFFu;
    constexpr std::uint16_t kSaturated16 = 0xFFFFu;

    /// SCFA's largest central directory is about 1 MiB; the cap bounds what a
    /// corrupt end record can make us allocate.
    constexpr std::uint64_t kMaxCentralDirectorySize = std::uint64_t{1} << 30;
    /// Deflate cannot expand data by more than about 1032:1, so a larger
    /// declared size is corrupt. Checking it before allocating keeps a lying
    /// header from asking for gigabytes.
    constexpr std::uint64_t kMaxDeflateRatio = 1032;
    constexpr std::size_t kInflateInputChunk = 64 * 1024;
    /// zlib counts in uInt; feed the output window in pieces below that.
    constexpr std::size_t kInflateOutputChunk = std::size_t{1} << 30;

    [[nodiscard]] std::uint16_t Le16(const std::uint8_t* const p)
    {
      return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
    }

    [[nodiscard]] std::uint32_t Le32(const std::uint8_t* const p)
    {
      return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) | (std::uint32_t{p[2]} << 16) |
             (std::uint32_t{p[3]} << 24);
    }

    [[nodiscard]] std::uint64_t Le64(const std::uint8_t* const p)
    {
      return std::uint64_t{Le32(p)} | (std::uint64_t{Le32(p + 4)} << 32);
    }

    void SetError(std::string* const error, std::string message)
    {
      if (error != nullptr) {
        *error = std::move(message);
      }
    }

    /// Fills the 64-bit values an entry marks as saturated from its zip64
    /// extra field. False if the field is missing or too short.
    [[nodiscard]] bool ApplyZip64Extra(const std::uint8_t* const extra, const std::size_t size, ZipEntry& entry)
    {
      const bool needUncompressed = entry.uncompressedSize == kSaturated32;
      const bool needCompressed = entry.compressedSize == kSaturated32;
      const bool needOffset = entry.localHeaderOffset == kSaturated32;
      if (!needUncompressed && !needCompressed && !needOffset) {
        return true;
      }

      std::size_t pos = 0;
      while (size - pos >= 4) {
        const std::uint16_t id = Le16(extra + pos);
        const std::size_t length = Le16(extra + pos + 2);
        pos += 4;
        if (length > size - pos) {
          return false;
        }
        if (id == kZip64ExtraId) {
          // The values appear in this fixed order, each only when saturated.
          const std::uint8_t* field = extra + pos;
          std::size_t left = length;
          const auto take = [&](std::uint64_t& value) {
            if (left < 8) {
              return false;
            }
            value = Le64(field);
            field += 8;
            left -= 8;
            return true;
          };
          return (!needUncompressed || take(entry.uncompressedSize)) &&
                 (!needCompressed || take(entry.compressedSize)) && (!needOffset || take(entry.localHeaderOffset));
        }
        pos += length;
      }
      return false;
    }

    struct InflateEndGuard
    {
      z_stream* stream;
      ~InflateEndGuard()
      {
        inflateEnd(stream);
      }
    };
  } // namespace

  std::string ZipArchive::CanonicalName(const std::string_view name)
  {
    std::string out;
    out.reserve(name.size());
    for (const char c : name) {
      out.push_back(c == '\\' ? '/' : fs::detail::LowerAscii(c));
    }
    const std::size_t first = out.find_first_not_of('/');
    out.erase(0, first == std::string::npos ? out.size() : first);
    return out;
  }

  std::unique_ptr<ZipArchive> ZipArchive::Open(const std::string& utf8Path, std::string* const error)
  {
    std::unique_ptr<ZipArchive> archive(new ZipArchive());
    archive->mPath = utf8Path;
    archive->mFile = ReadOnlyFile::Open(utf8Path, error);
    if (archive->mFile == nullptr) {
      return nullptr;
    }
    const ReadOnlyFile& file = *archive->mFile;
    const std::uint64_t fileSize = file.Size();
    const auto fail = [&](const std::string& what) -> std::unique_ptr<ZipArchive> {
      SetError(error, utf8Path + ": " + what);
      return nullptr;
    };

    if (fileSize < kEndOfCentralDirectorySize) {
      return fail("not a zip archive (too small)");
    }

    // The end record is the last one carrying its signature; it sits at most
    // a maximum-length comment before the end of the file.
    const auto tailSize =
      static_cast<std::size_t>(std::min<std::uint64_t>(fileSize, kEndOfCentralDirectorySize + kMaxCommentSize));
    const std::uint64_t tailStart = fileSize - tailSize;
    std::vector<std::uint8_t> tail(tailSize);
    if (!file.ReadAt(tailStart, tail.data(), tail.size())) {
      return fail("read error");
    }
    // Prefer the record whose comment length accounts for exactly the bytes
    // after it, so a comment that happens to contain "PK\5\6" is not taken
    // for the record; failing that, take the last signature like CZipFile.
    std::size_t endInTail = npos;
    std::size_t lastSignature = npos;
    for (std::size_t pos = tailSize - kEndOfCentralDirectorySize + 1; pos-- > 0;) {
      if (Le32(&tail[pos]) != kEndOfCentralDirectorySignature) {
        continue;
      }
      if (lastSignature == npos) {
        lastSignature = pos;
      }
      if (pos + kEndOfCentralDirectorySize + Le16(&tail[pos + 20]) == tailSize) {
        endInTail = pos;
        break;
      }
    }
    if (endInTail == npos) {
      endInTail = lastSignature;
    }
    if (endInTail == npos) {
      return fail("not a zip archive (no end of central directory)");
    }
    const std::uint8_t* const end = &tail[endInTail];
    const std::uint64_t endOffset = tailStart + endInTail;

    std::uint64_t diskNumber = Le16(end + 4);
    std::uint64_t directoryDisk = Le16(end + 6);
    std::uint64_t entriesOnDisk = Le16(end + 8);
    std::uint64_t totalEntries = Le16(end + 10);
    std::uint64_t directorySize = Le32(end + 12);
    std::uint64_t directoryOffset = Le32(end + 16);
    std::uint64_t directoryLimit = endOffset; // The directory must end before its end record.

    if (entriesOnDisk == kSaturated16 || totalEntries == kSaturated16 || directorySize == kSaturated32 ||
        directoryOffset == kSaturated32) {
      if (endOffset < kZip64LocatorSize) {
        return fail("zip64 locator missing");
      }
      const std::uint64_t locatorOffset = endOffset - kZip64LocatorSize;
      std::array<std::uint8_t, kZip64LocatorSize> locator{};
      if (!file.ReadAt(locatorOffset, locator.data(), locator.size())) {
        return fail("read error");
      }
      if (Le32(locator.data()) != kZip64LocatorSignature) {
        return fail("zip64 locator missing");
      }
      const std::uint64_t recordOffset = Le64(&locator[8]);
      if (recordOffset > locatorOffset || kZip64EndSize > locatorOffset - recordOffset) {
        return fail("zip64 end record out of range");
      }
      std::array<std::uint8_t, kZip64EndSize> record{};
      if (!file.ReadAt(recordOffset, record.data(), record.size())) {
        return fail("read error");
      }
      if (Le32(record.data()) != kZip64EndSignature) {
        return fail("corrupt zip64 end record");
      }
      diskNumber = Le32(&record[16]);
      directoryDisk = Le32(&record[20]);
      entriesOnDisk = Le64(&record[24]);
      totalEntries = Le64(&record[32]);
      directorySize = Le64(&record[40]);
      directoryOffset = Le64(&record[48]);
      directoryLimit = recordOffset;
    }

    // The engine's checks (CZipFile::CZipFile), then the ones that keep the
    // allocations below honest.
    if (diskNumber != 0 || directoryDisk != 0) {
      return fail("multi-disk archives are not supported");
    }
    if (entriesOnDisk != totalEntries) {
      return fail("inconsistent entry count");
    }
    if (directoryOffset > directoryLimit || directorySize > directoryLimit - directoryOffset) {
      return fail("central directory out of range");
    }
    if (directorySize > kMaxCentralDirectorySize) {
      return fail("central directory too large");
    }
    if (totalEntries > directorySize / kCentralHeaderSize) {
      return fail("entry count does not fit the central directory");
    }

    std::vector<std::uint8_t> directory;
    try {
      directory.resize(static_cast<std::size_t>(directorySize));
      archive->mEntries.reserve(static_cast<std::size_t>(totalEntries));
    } catch (const std::bad_alloc&) {
      return fail("out of memory");
    }
    if (!file.ReadAt(directoryOffset, directory.data(), directory.size())) {
      return fail("read error");
    }

    std::size_t pos = 0;
    for (std::uint64_t ordinal = 0; ordinal < totalEntries; ++ordinal) {
      if (directory.size() - pos < kCentralHeaderSize) {
        return fail("truncated central directory");
      }
      const std::uint8_t* const header = directory.data() + pos;
      if (Le32(header) != kCentralHeaderSignature) {
        return fail("corrupt central directory");
      }

      ZipEntry entry;
      entry.flags = Le16(header + 8);
      entry.method = Le16(header + 10);
      entry.compressedSize = Le32(header + 20);
      entry.uncompressedSize = Le32(header + 24);
      const std::size_t nameLength = Le16(header + 28);
      const std::size_t extraLength = Le16(header + 30);
      const std::size_t commentLength = Le16(header + 32);
      entry.localHeaderOffset = Le32(header + 42);

      const std::size_t recordSize = kCentralHeaderSize + nameLength + extraLength + commentLength;
      if (directory.size() - pos < recordSize) {
        return fail("truncated central directory");
      }
      entry.name.assign(reinterpret_cast<const char*>(header + kCentralHeaderSize), nameLength);
      if (!ApplyZip64Extra(header + kCentralHeaderSize + nameLength, extraLength, entry)) {
        return fail("corrupt zip64 extra field for " + entry.name);
      }
      pos += recordSize;

      if ((entry.flags & kFlagDataDescriptor) != 0) {
        // CZipFile logs "uses a data descriptor -- feature unsupported" and
        // drops the entry; doing the same keeps lookups identical.
        ++archive->mSkipped;
        continue;
      }
      entry.isDirectory = !entry.name.empty() && (entry.name.back() == '/' || entry.name.back() == '\\');
      archive->mEntries.push_back(std::move(entry));
    }

    std::vector<IndexItem>& index = archive->mIndex;
    index.reserve(archive->mEntries.size());
    for (std::size_t i = 0; i < archive->mEntries.size(); ++i) {
      const ZipEntry& entry = archive->mEntries[i];
      if (!entry.isDirectory && !entry.name.empty()) {
        index.push_back({CanonicalName(entry.name), static_cast<std::uint32_t>(i)});
      }
    }
    // Stable, so among duplicate names the earliest entry stays first and
    // survives the unique pass - CZipFile keeps the first insert as well.
    std::stable_sort(index.begin(), index.end(), [](const IndexItem& a, const IndexItem& b) {
      return a.canonical < b.canonical;
    });
    index.erase(
      std::unique(
        index.begin(),
        index.end(),
        [](const IndexItem& a, const IndexItem& b) {
          return a.canonical == b.canonical;
        }
      ),
      index.end()
    );
    return archive;
  }

  std::size_t ZipArchive::Find(const std::string_view canonicalName) const
  {
    const auto it =
      std::lower_bound(mIndex.begin(), mIndex.end(), canonicalName, [](const IndexItem& item, std::string_view key) {
        return item.canonical < key;
      });
    if (it == mIndex.end() || it->canonical != canonicalName) {
      return npos;
    }
    return it->entry;
  }

  std::span<const ZipArchive::IndexItem> ZipArchive::WithPrefix(const std::string_view canonicalPrefix) const
  {
    const auto first =
      std::lower_bound(mIndex.begin(), mIndex.end(), canonicalPrefix, [](const IndexItem& item, std::string_view key) {
        return item.canonical < key;
      });
    const auto last = std::partition_point(first, mIndex.end(), [&](const IndexItem& item) {
      return std::string_view(item.canonical).starts_with(canonicalPrefix);
    });
    return {first, last};
  }

  bool ZipArchive::Read(const std::size_t index, std::vector<std::uint8_t>& out, std::string* const error) const
  {
    out.clear();
    if (index >= mEntries.size()) {
      SetError(error, mPath + ": entry index out of range");
      return false;
    }
    const ZipEntry& entry = mEntries[index];
    const auto fail = [&](const std::string& what) {
      out.clear();
      SetError(error, mPath + " (" + entry.name + "): " + what);
      return false;
    };

    if (entry.isDirectory) {
      return fail("is a directory");
    }
    if ((entry.flags & kFlagEncrypted) != 0) {
      return fail("encrypted entries are not supported");
    }

    const std::uint64_t fileSize = mFile->Size();
    if (entry.localHeaderOffset > fileSize || kLocalHeaderSize > fileSize - entry.localHeaderOffset) {
      return fail("local header out of range");
    }
    std::array<std::uint8_t, kLocalHeaderSize> local{};
    if (!mFile->ReadAt(entry.localHeaderOffset, local.data(), local.size())) {
      return fail("read error");
    }
    if (Le32(local.data()) != kLocalHeaderSignature) {
      return fail("bad local header");
    }
    // The local name/extra lengths may differ from the central directory's;
    // the data starts after the local ones.
    const std::uint64_t dataOffset =
      entry.localHeaderOffset + kLocalHeaderSize + Le16(&local[26]) + std::uint64_t{Le16(&local[28])};
    if (dataOffset > fileSize || entry.compressedSize > fileSize - dataOffset) {
      return fail("data out of range");
    }
    if (entry.uncompressedSize >= std::min<std::uint64_t>(out.max_size(), std::numeric_limits<std::size_t>::max())) {
      return fail("too large to read into memory");
    }
    const auto size = static_cast<std::size_t>(entry.uncompressedSize);

    if (entry.method == kMethodStored) {
      if (entry.compressedSize != entry.uncompressedSize) {
        return fail("stored entry with different compressed and uncompressed sizes");
      }
      try {
        out.resize(size);
      } catch (const std::bad_alloc&) {
        return fail("out of memory");
      }
      if (size != 0 && !mFile->ReadAt(dataOffset, out.data(), size)) {
        return fail("read error");
      }
      return true;
    }

    if (entry.method != kMethodDeflate) {
      return fail("unsupported compression method " + std::to_string(entry.method));
    }
    if (size == 0) {
      // FAF's lua.nx2 has empty files stored as deflate (an empty stream or
      // none at all); there is nothing to inflate.
      return true;
    }
    if (entry.uncompressedSize / kMaxDeflateRatio > entry.compressedSize) {
      return fail("declared size is implausible for its compressed size");
    }

    try {
      // One spare byte: a stream that inflates past the declared size fills
      // it and is rejected instead of being silently cut.
      out.resize(size + 1);
    } catch (const std::bad_alloc&) {
      return fail("out of memory");
    }
    std::vector<std::uint8_t> input;
    try {
      input.resize(static_cast<std::size_t>(
        std::max<std::uint64_t>(1, std::min<std::uint64_t>(kInflateInputChunk, entry.compressedSize))
      ));
    } catch (const std::bad_alloc&) {
      return fail("out of memory");
    }

    z_stream stream{};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
      return fail("inflateInit2 failed");
    }
    const InflateEndGuard guard{&stream};

    std::uint8_t* const outBegin = out.data();
    stream.next_out = outBegin;
    std::uint64_t remaining = entry.compressedSize;
    std::uint64_t inputOffset = dataOffset;
    for (;;) {
      if (stream.avail_in == 0 && remaining > 0) {
        const auto chunk = static_cast<std::size_t>(std::min<std::uint64_t>(input.size(), remaining));
        if (!mFile->ReadAt(inputOffset, input.data(), chunk)) {
          return fail("read error");
        }
        inputOffset += chunk;
        remaining -= chunk;
        stream.next_in = input.data();
        stream.avail_in = static_cast<uInt>(chunk);
      }

      const auto produced = static_cast<std::size_t>(stream.next_out - outBegin);
      const std::size_t room = out.size() - produced;
      if (room == 0) {
        return fail("inflates to more than its declared size");
      }
      stream.avail_out = static_cast<uInt>(std::min(room, kInflateOutputChunk));

      const int status = inflate(&stream, Z_NO_FLUSH);
      if (status == Z_STREAM_END) {
        break;
      }
      if (status == Z_BUF_ERROR) {
        // No progress possible: either out of input for good, or the output
        // window is full and the next pass gives it more room.
        if (stream.avail_in == 0 && remaining == 0) {
          return fail("truncated deflate stream");
        }
        continue;
      }
      if (status != Z_OK) {
        return fail(
          std::string("corrupt deflate data") + (stream.msg != nullptr ? std::string(": ") + stream.msg : "")
        );
      }
    }

    const auto produced = static_cast<std::size_t>(stream.next_out - outBegin);
    if (produced != size) {
      return fail("inflated size does not match the declared size");
    }
    out.resize(size);
    return true;
  }

} // namespace faf::port::detail
