#include "DataCheck.h"

#include <algorithm>
#include <array>
#include <chrono>

#include "faf/port/Image.h"

namespace faf::port::datacheck {

  namespace {
    using Clock = std::chrono::steady_clock;

    constexpr std::size_t kMaxRecordedErrors = 20;

    [[nodiscard]] double MillisecondsSince(const Clock::time_point start)
    {
      return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }

    [[nodiscard]] bool EndsWithNoCase(const std::string_view text, const std::string_view suffix)
    {
      if (text.size() < suffix.size()) {
        return false;
      }
      const std::string_view tail = text.substr(text.size() - suffix.size());
      return std::equal(tail.begin(), tail.end(), suffix.begin(), [](const char a, const char b) {
        const auto lower = [](const char c) {
          return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        };
        return lower(a) == lower(b);
      });
    }

    constexpr std::array<Probe, 10> kProbes = {{
      {"/lua/system/config.lua", "lua.nx2", "required"},
      {"/lua/ui/uiutil.lua", "lua.nx2", "required"},
      {"/loc/us/strings_db.lua", "loc.nx2", "required"},
      {"/etc/faf/blacklist.lua", "etc.nx2", "required"},
      {"/effects/terrain.fx", "effects.nx2", "required"},
      {"/textures/ui/common/slot/slot-player.dds", "textures.nx2", "required"},
      {"/textures/ui/common/menus02/background-paint01_bmp.dds", "textures.scd", "required"},
      {"/fonts/arial.ttf", "fonts", "required"},
      {"/units/uel0001/uel0001_unit.bp", "units.nx2", "recommended"},
      {"/does/not/exist.lua", "", "required"},
    }};

    constexpr std::array<std::string_view, 3> kSplashCandidates = {
      "/textures/ui/common/menus02/background-paint01_bmp.dds",
      "/textures/ui/common/load/background-portal_bmp.dds",
      "/textures/environment/DefaultBackground.dds",
    };

    void Record(std::vector<std::string>& errors, std::string message)
    {
      if (errors.size() < kMaxRecordedErrors) {
        errors.push_back(std::move(message));
      }
    }
  } // namespace

  std::span<const Probe> StandardProbes()
  {
    return kProbes;
  }

  std::span<const std::string_view> SplashCandidates()
  {
    return kSplashCandidates;
  }

  std::string LeafName(const std::string_view path)
  {
    std::string_view leaf = path;
    while (!leaf.empty() && (leaf.back() == '/' || leaf.back() == '\\')) {
      leaf.remove_suffix(1);
    }
    const std::size_t cut = leaf.find_last_of("/\\");
    if (cut != std::string_view::npos) {
      leaf.remove_prefix(cut + 1);
    }
    std::string out(leaf);
    for (char& c : out) {
      if (c >= 'A' && c <= 'Z') {
        c = static_cast<char>(c - 'A' + 'a');
      }
    }
    return out;
  }

  Report Run(const Options& options, VirtualFileSystem& vfs)
  {
    Report report;

    DataPathOptions dataPathOptions;
    dataPathOptions.scriptPath = options.scriptPath;
    dataPathOptions.folders.localAppData = options.localAppData;
    dataPathOptions.folders.personal = options.documents;
    dataPathOptions.allowWrites = options.allowWrites;
    dataPathOptions.log = options.log;
    report.dataPath = RunDataPathScript(dataPathOptions);
    if (!report.dataPath.ok) {
      report.problems.push_back("data-path script failed: " + report.dataPath.error);
      return report;
    }

    const Clock::time_point mountStart = Clock::now();
    for (const MountSpec& spec : report.dataPath.mounts) {
      std::string error;
      if (vfs.Mount(spec, &error) == 0 && error.empty()) {
        ++report.skippedPathEntries;
      }
      if (!error.empty()) {
        report.mountErrors.push_back(error);
      }
    }
    report.mountMilliseconds = MillisecondsSince(mountStart);

    for (const MountInfo& mount : vfs.Mounts()) {
      if (mount.isArchive) {
        ++report.archives;
        report.archiveEntries += mount.entryCount;
      } else {
        ++report.directories;
      }
    }
    report.mounts = vfs.Mounts().size();
    if (report.mounts == 0) {
      report.problems.push_back("the data-path script produced no usable mount");
    }
    for (const std::string& error : report.mountErrors) {
      report.warnings.push_back(error);
    }

    for (const Probe& probe : StandardProbes()) {
      Lookup lookup;
      lookup.vfsPath = probe.vfsPath;
      lookup.expectedSource = probe.expectedSource;
      lookup.tier = probe.tier;
      lookup.location = vfs.Find(probe.vfsPath);
      lookup.found = lookup.location.found;
      if (lookup.found) {
        lookup.source = LeafName(vfs.Mounts()[lookup.location.mountIndex].diskPath);
      }

      if (probe.expectedSource.empty()) {
        lookup.ok = !lookup.found;
        if (!lookup.ok) {
          lookup.problem = "must not resolve, but came from " + lookup.location.diskPath;
        }
      } else if (!lookup.found) {
        lookup.problem = "not found (" + std::string(probe.expectedSource) + " missing?)";
      } else if (lookup.source != probe.expectedSource) {
        lookup.problem = "came from " + lookup.source + ", expected " + std::string(probe.expectedSource);
      } else {
        lookup.ok = true;
      }

      if (!lookup.ok) {
        const std::string message = lookup.vfsPath + ": " + lookup.problem;
        if (lookup.found || probe.tier == "required") {
          report.problems.push_back(message);
        } else {
          report.warnings.push_back(message);
        }
      }
      report.lookups.push_back(std::move(lookup));
    }

    if (options.checkLua) {
      LuaCheck& lua = report.lua;
      lua.ran = true;
      const Clock::time_point start = Clock::now();
      const std::vector<MountInfo>& mounts = vfs.Mounts();
      const auto archive = std::find_if(mounts.begin(), mounts.end(), [](const MountInfo& mount) {
        return mount.isArchive && LeafName(mount.diskPath) == "lua.nx2";
      });
      if (archive == mounts.end()) {
        report.problems.push_back("--check-lua: lua.nx2 is not mounted");
      } else {
        lua.archive = archive->diskPath;
        const auto mountIndex = static_cast<std::size_t>(archive - mounts.begin());
        std::vector<std::uint8_t> bytes;
        for (const std::string& entry : vfs.ArchiveEntries(mountIndex)) {
          if (!EndsWithNoCase(entry, ".lua")) {
            continue;
          }
          std::string error;
          if (!vfs.ReadArchiveEntry(mountIndex, entry, bytes, &error) ||
              !CheckLuaSyntax(bytes.data(), bytes.size(), entry, &error)) {
            ++lua.failed;
            Record(lua.errors, error);
            continue;
          }
          ++lua.parsed;
        }
        if (lua.failed > 0) {
          report.problems.push_back(std::to_string(lua.failed) + " Lua file(s) in lua.nx2 failed to parse");
        }
        if (lua.parsed == 0 && lua.failed == 0) {
          report.problems.push_back("lua.nx2 holds no .lua files");
        }
      }
      lua.milliseconds = MillisecondsSince(start);
    }

    if (options.verifyArchives) {
      ArchiveCheck& check = report.archiveCheck;
      check.ran = true;
      const Clock::time_point start = Clock::now();
      std::vector<std::uint8_t> bytes;
      for (std::size_t i = 0; i < vfs.Mounts().size(); ++i) {
        for (const std::string& entry : vfs.ArchiveEntries(i)) {
          ++check.entries;
          std::string error;
          if (!vfs.ReadArchiveEntry(i, entry, bytes, &error)) {
            ++check.failed;
            Record(check.errors, error);
            continue;
          }
          check.bytes += bytes.size();
        }
      }
      check.milliseconds = MillisecondsSince(start);
      if (check.failed > 0) {
        report.problems.push_back(std::to_string(check.failed) + " archive entries could not be read");
      }
    }

    return report;
  }

  Extraction ExtractToPng(const VirtualFileSystem& vfs, const std::string_view vfsPath, const std::string& pngPath)
  {
    Extraction extraction;
    std::vector<std::uint8_t> dds;
    if (!vfs.Read(vfsPath, dds, &extraction.error)) {
      return extraction;
    }
    extraction.ddsBytes = dds.size();

    Image image;
    const Clock::time_point start = Clock::now();
    if (!DecodeDds(dds.data(), dds.size(), image, &extraction.error)) {
      extraction.error = std::string(vfsPath) + ": " + extraction.error;
      return extraction;
    }
    extraction.decodeMilliseconds = MillisecondsSince(start);
    extraction.format = image.format;
    extraction.width = image.width;
    extraction.height = image.height;
    if (!WritePng(pngPath, image)) {
      extraction.error = "cannot write " + pngPath;
      return extraction;
    }
    extraction.ok = true;
    return extraction;
  }

} // namespace faf::port::datacheck
