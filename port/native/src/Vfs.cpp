#include "faf/port/Vfs.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <set>
#include <system_error>
#include <utility>

#include "faf/port/FileSystem.h"

#include "FileSystemDetail.h"
#include "NativeFile.h"
#include "ZipArchive.h"

namespace faf::port {

  namespace {
    using fs::detail::Kind;

    /// A VFS path split into the lower-cased form used for matching and the
    /// original spelling used to look the file up on disk. Both are normalised
    /// the same way ('/' separators, leading '/', no empty or "." components),
    /// and ASCII lower-casing keeps their lengths equal, so one offset cuts both.
    struct VfsPath
    {
      std::string canonical;
      std::string spelled;
      bool hasParentReference = false; ///< A ".." component (never resolved in directory mounts).
    };

    [[nodiscard]] VfsPath MakeVfsPath(const std::string_view path, const bool directory)
    {
      VfsPath out;
      out.spelled.push_back('/');
      std::size_t pos = 0;
      while (pos <= path.size()) {
        std::size_t end = path.find_first_of("/\\", pos);
        if (end == std::string_view::npos) {
          end = path.size();
        }
        const std::string_view component = path.substr(pos, end - pos);
        pos = end + 1;
        if (component.empty() || component == ".") {
          continue;
        }
        if (component == "..") {
          out.hasParentReference = true;
        }
        if (out.spelled.size() > 1) {
          out.spelled.push_back('/');
        }
        out.spelled.append(component);
      }
      if (directory && out.spelled.size() > 1) {
        out.spelled.push_back('/');
      }
      out.canonical = fs::detail::LowerAscii(out.spelled);
      return out;
    }

    /// Mount point form: lower-cased, '/' separators, leading and trailing '/'.
    [[nodiscard]] std::string CanonicalMountpoint(const std::string_view mountpoint)
    {
      return MakeVfsPath(mountpoint, true).canonical;
    }

    [[nodiscard]] bool ContainsWildcard(const std::string_view text)
    {
      return text.find_first_of("*?") != std::string_view::npos;
    }
  } // namespace

  struct VirtualFileSystem::Impl
  {
    struct Mount
    {
      std::unique_ptr<detail::ZipArchive> archive; ///< Null for directory mounts.
    };

    struct Hit
    {
      std::size_t mountIndex = 0;
      std::size_t entryIndex = detail::ZipArchive::npos; ///< Archive mounts.
      std::string diskFile;                              ///< Directory mounts.
      std::uint64_t size = 0;
    };

    std::vector<MountInfo> infos;
    std::vector<Mount> mounts;

    void Add(MountInfo info, std::unique_ptr<detail::ZipArchive> archive)
    {
      infos.push_back(std::move(info));
      mounts.push_back({std::move(archive)});
    }

    [[nodiscard]] std::optional<Hit> FindHit(const std::string_view vfsPath) const
    {
      const VfsPath path = MakeVfsPath(vfsPath, false);
      if (path.canonical.size() <= 1) {
        return std::nullopt;
      }
      for (std::size_t i = 0; i < mounts.size(); ++i) {
        const std::string& mountpoint = infos[i].mountpoint;
        if (!path.canonical.starts_with(mountpoint) || path.canonical.size() == mountpoint.size()) {
          continue;
        }

        if (const detail::ZipArchive* const archive = mounts[i].archive.get()) {
          const std::size_t entry = archive->Find(std::string_view(path.canonical).substr(mountpoint.size()));
          if (entry != detail::ZipArchive::npos) {
            return Hit{i, entry, {}, archive->Entries()[entry].uncompressedSize};
          }
          continue;
        }

        if (path.hasParentReference) {
          continue; // Never let "/maps/x/../../.." walk out of a directory mount.
        }
        const std::string diskPath =
          fs::detail::JoinPath(infos[i].diskPath, std::string_view(path.spelled).substr(mountpoint.size()));
        Kind kind = Kind::Missing;
        std::optional<std::string> resolved = fs::detail::Resolve(diskPath, true, &kind);
        if (!resolved || kind != Kind::File) {
          continue;
        }
        std::error_code ec;
        const std::uintmax_t size = std::filesystem::file_size(detail::PathFromUtf8(*resolved), ec);
        return Hit{i, detail::ZipArchive::npos, std::move(*resolved), ec ? 0 : static_cast<std::uint64_t>(size)};
      }
      return std::nullopt;
    }
  };

  VirtualFileSystem::VirtualFileSystem()
    : mImpl(std::make_unique<Impl>())
  {}

  VirtualFileSystem::~VirtualFileSystem() = default;

  std::size_t VirtualFileSystem::Mount(const MountSpec& spec, std::string* const error)
  {
    std::string dir = spec.dir;
    std::replace(dir.begin(), dir.end(), '\\', '/');
    if (dir.empty()) {
      return 0;
    }
    const std::string mountpoint =
      CanonicalMountpoint(spec.mountpoint.empty() ? std::string_view("/") : spec.mountpoint);

    // CVFSImpl::AddMountPoint expands a wildcard in the last component and
    // mounts every match; anything else names exactly one candidate.
    std::vector<std::string> candidates;
    const std::size_t cut = dir.find_last_of('/');
    const std::string_view leaf =
      cut == std::string::npos ? std::string_view(dir) : std::string_view(dir).substr(cut + 1);
    if (ContainsWildcard(leaf)) {
      const std::string parent = cut == std::string::npos ? std::string("./") : dir.substr(0, cut + 1);
      for (const std::string& name : fs::FindFiles(dir)) {
        if (name != "." && name != "..") {
          candidates.push_back(fs::NormalizePath(parent + name));
        }
      }
    } else {
      candidates.push_back(fs::NormalizePath(dir));
    }

    std::size_t added = 0;
    std::string errors;
    for (const std::string& candidate : candidates) {
      Kind kind = Kind::Missing;
      const std::optional<std::string> resolved = fs::detail::Resolve(candidate, true, &kind);
      if (!resolved) {
        continue; // Missing search paths are skipped, as in the engine.
      }

      if (kind == Kind::Directory) {
        mImpl->Add(MountInfo{*resolved, mountpoint, false, 0}, nullptr);
        ++added;
      } else if (kind == Kind::File) {
        std::string openError;
        std::unique_ptr<detail::ZipArchive> archive = detail::ZipArchive::Open(*resolved, &openError);
        if (archive == nullptr) {
          if (!errors.empty()) {
            errors += '\n';
          }
          errors += "not a valid zip file: " + openError;
          continue;
        }
        const std::size_t entries = archive->Entries().size();
        mImpl->Add(MountInfo{*resolved, mountpoint, true, entries}, std::move(archive));
        ++added;
      }
    }

    if (error != nullptr) {
      *error = std::move(errors);
    }
    return added;
  }

  const std::vector<MountInfo>& VirtualFileSystem::Mounts() const
  {
    return mImpl->infos;
  }

  FileLocation VirtualFileSystem::Find(const std::string_view vfsPath) const
  {
    FileLocation location;
    const std::optional<Impl::Hit> hit = mImpl->FindHit(vfsPath);
    if (!hit) {
      return location;
    }
    location.found = true;
    location.mountIndex = hit->mountIndex;
    location.size = hit->size;
    if (const detail::ZipArchive* const archive = mImpl->mounts[hit->mountIndex].archive.get()) {
      const detail::ZipEntry& entry = archive->Entries()[hit->entryIndex];
      location.diskPath = archive->Path();
      location.entryName = entry.name;
      location.compressed = entry.method != 0;
    } else {
      location.diskPath = hit->diskFile;
    }
    return location;
  }

  bool VirtualFileSystem::Read(const std::string_view vfsPath, std::vector<std::uint8_t>& out, std::string* const error)
    const
  {
    out.clear();
    const std::optional<Impl::Hit> hit = mImpl->FindHit(vfsPath);
    if (!hit) {
      if (error != nullptr) {
        *error = std::string(vfsPath) + ": not found in the virtual file system";
      }
      return false;
    }
    if (const detail::ZipArchive* const archive = mImpl->mounts[hit->mountIndex].archive.get()) {
      return archive->Read(hit->entryIndex, out, error);
    }
    return fs::ReadWholeFile(hit->diskFile, out, error);
  }

  std::vector<std::string> VirtualFileSystem::List(const std::string_view vfsDir, const std::string_view pattern) const
  {
    const VfsPath dir = MakeVfsPath(vfsDir, true);
    std::vector<std::string> names;
    std::set<std::string> seen; // Lower-cased names already listed.
    const auto offer = [&](std::string_view name) {
      if (fs::WildcardMatch(pattern, name) && seen.insert(fs::detail::LowerAscii(name)).second) {
        names.emplace_back(name);
      }
    };

    for (std::size_t i = 0; i < mImpl->mounts.size(); ++i) {
      const std::string& mountpoint = mImpl->infos[i].mountpoint;
      // Only mounts at or above the directory contribute files directly in it.
      if (!dir.canonical.starts_with(mountpoint)) {
        continue;
      }
      const std::string_view relative = std::string_view(dir.canonical).substr(mountpoint.size());

      if (const detail::ZipArchive* const archive = mImpl->mounts[i].archive.get()) {
        for (const detail::ZipArchive::IndexItem& item : archive->WithPrefix(relative)) {
          const std::string_view rest = std::string_view(item.canonical).substr(relative.size());
          if (rest.empty() || rest.find('/') != std::string_view::npos) {
            continue;
          }
          // Report the stored spelling of the leaf, not the lower-cased key.
          const std::string& stored = archive->Entries()[item.entry].name;
          offer(std::string_view(stored).substr(stored.size() - rest.size()));
        }
        continue;
      }

      if (dir.hasParentReference) {
        continue;
      }
      const std::string diskDir =
        fs::detail::JoinPath(mImpl->infos[i].diskPath, std::string_view(dir.spelled).substr(mountpoint.size()));
      Kind kind = Kind::Missing;
      const std::optional<std::string> resolved = fs::detail::Resolve(diskDir, true, &kind);
      if (!resolved || kind != Kind::Directory) {
        continue;
      }
      for (const std::string& name : fs::FindFiles(fs::detail::JoinPath(*resolved, "*"))) {
        if (name != "." && name != ".." && fs::detail::StatKind(fs::detail::JoinPath(*resolved, name)) == Kind::File) {
          offer(name);
        }
      }
    }
    return names;
  }

  std::vector<std::string> VirtualFileSystem::ArchiveEntries(const std::size_t mountIndex) const
  {
    std::vector<std::string> names;
    if (mountIndex >= mImpl->mounts.size() || mImpl->mounts[mountIndex].archive == nullptr) {
      return names;
    }
    for (const detail::ZipEntry& entry : mImpl->mounts[mountIndex].archive->Entries()) {
      if (!entry.isDirectory && !entry.name.empty()) {
        names.push_back(entry.name);
      }
    }
    return names;
  }

  bool VirtualFileSystem::ReadArchiveEntry(
    const std::size_t mountIndex,
    const std::string_view entryName,
    std::vector<std::uint8_t>& out,
    std::string* const error
  ) const
  {
    out.clear();
    if (mountIndex >= mImpl->mounts.size() || mImpl->mounts[mountIndex].archive == nullptr) {
      if (error != nullptr) {
        *error = "mount " + std::to_string(mountIndex) + " is not an archive";
      }
      return false;
    }
    const detail::ZipArchive& archive = *mImpl->mounts[mountIndex].archive;
    const std::size_t entry = archive.Find(detail::ZipArchive::CanonicalName(entryName));
    if (entry == detail::ZipArchive::npos) {
      if (error != nullptr) {
        *error = archive.Path() + ": no entry " + std::string(entryName);
      }
      return false;
    }
    return archive.Read(entry, out, error);
  }

} // namespace faf::port
