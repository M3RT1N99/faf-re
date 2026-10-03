#pragma once

// The engine's virtual file system (CVFSImpl) re-expressed portably for the
// Android bring-up. Behaviour reference: src/sdk/moho/sim/CVFSImpl.cpp
// (AddSearchPath ~601-660, FindFile ~667-699) and CZipFile.cpp.
//
// - Mounts are searched in the order they were added; the first mount that
//   holds a file wins. init_faf.lua relies on it: FAF's .nx2 archives are
//   mounted before SCFA's .scd archives and override them.
// - Mount points are lower-cased and end in '/'. VFS paths compare
//   case-insensitively; disk paths keep their real case (the engine lower-cases
//   them, which only works on NTFS).
// - A mount's directory is either a directory or a zip archive (.scd stored,
//   .nx2 deflate). Entries with a data descriptor (flag 0x08) are skipped like
//   the engine does; zero-length deflate entries read as empty. Zip64 archives
//   are read too (the engine cannot, so no game data uses them).
// - Lookups in a directory mount never leave it: a VFS path with a ".."
//   component is not found there.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace faf::port {

  /// One `path` entry produced by the data-path script: { dir = ..., mountpoint = ... }.
  struct MountSpec
  {
    std::string dir;        ///< As written by the script; may use '\\' and wildcards.
    std::string mountpoint; ///< VFS mount point, e.g. "/" or "/maps/SCMP_009".
  };

  struct MountInfo
  {
    std::string diskPath;       ///< Resolved on-disk path, '/' separators, real case.
    std::string mountpoint;     ///< Lower-cased, trailing '/'.
    bool isArchive = false;
    std::size_t entryCount = 0; ///< Archive entries; 0 for directory mounts.
  };

  struct FileLocation
  {
    bool found = false;
    std::size_t mountIndex = 0;
    std::string diskPath;   ///< The archive, or the file for directory mounts.
    std::string entryName;  ///< Entry name inside the archive; empty for directory mounts.
    std::uint64_t size = 0; ///< Uncompressed size.
    bool compressed = false;
  };

  class VirtualFileSystem
  {
  public:
    VirtualFileSystem();
    ~VirtualFileSystem();
    VirtualFileSystem(const VirtualFileSystem&) = delete;
    VirtualFileSystem& operator=(const VirtualFileSystem&) = delete;

    /// Adds `spec` after the existing mounts. A wildcard in the last component
    /// expands with fs::FindFiles order. Missing paths are skipped, not errors
    /// (the engine skips them too); a file that is not a readable zip is an
    /// error. Returns the number of mounts added. Not safe to call while other
    /// threads read.
    std::size_t Mount(const MountSpec& spec, std::string* error = nullptr);

    [[nodiscard]] const std::vector<MountInfo>& Mounts() const;

    /// First-mount-wins lookup of an absolute VFS path such as "/lua/system/config.lua".
    [[nodiscard]] FileLocation Find(std::string_view vfsPath) const;

    /// Reads the file `Find` resolves. Thread-safe for concurrent readers.
    bool Read(std::string_view vfsPath, std::vector<std::uint8_t>& out, std::string* error = nullptr) const;

    /// Names (not paths) of the files directly inside VFS directory `vfsDir`
    /// that match `pattern`, merged across mounts, deduplicated case-insensitively,
    /// in first-mount-wins order of first appearance.
    [[nodiscard]] std::vector<std::string> List(std::string_view vfsDir, std::string_view pattern = "*") const;

    /// Entry names of archive mount `mountIndex` as stored in the archive, in
    /// archive order, without directory entries. Empty for directory mounts.
    /// Used by the host checks to walk a whole archive (e.g. every .lua in lua.nx2).
    [[nodiscard]] std::vector<std::string> ArchiveEntries(std::size_t mountIndex) const;

    /// Reads entry `entryName` (case-insensitive) of archive mount `mountIndex`
    /// directly, bypassing first-mount-wins. Thread-safe for concurrent readers.
    bool ReadArchiveEntry(
      std::size_t mountIndex,
      std::string_view entryName,
      std::vector<std::uint8_t>& out,
      std::string* error = nullptr
    ) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
  };

} // namespace faf::port
