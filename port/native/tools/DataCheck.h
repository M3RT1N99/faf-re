#pragma once

// Host-side verification of a game data layout with the same code the device
// runs: run the data-path script, mount its `path` table, look up the
// standard probe paths, optionally parse every .lua in lua.nx2 and read every
// archive entry. Shared by tools/faf_datacheck.cpp and the integration test.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "faf/port/DataPath.h"
#include "faf/port/Vfs.h"

namespace faf::port::datacheck {

  /// A VFS path and where it must come from when the data is complete.
  struct Probe
  {
    std::string_view vfsPath;
    /// Lower-case file name of the archive, or of the mounted directory, that
    /// must win the lookup; empty = the path must not resolve at all.
    std::string_view expectedSource;
    /// "required" / "recommended" (port/data/gamedata.json tiers).
    std::string_view tier;
  };

  /// Probes that show the mount order is the engine's: FAF .nx2 content
  /// shadows SCFA .scd content, SCFA-only assets come from the .scd, loose
  /// directories mount where init_faf.lua puts them.
  [[nodiscard]] std::span<const Probe> StandardProbes();

  /// The splash textures the bring-up shows, in order of preference.
  [[nodiscard]] std::span<const std::string_view> SplashCandidates();

  struct Options
  {
    std::string scriptPath;
    std::string localAppData;
    std::string documents;
    bool allowWrites = false;
    bool checkLua = false;       ///< Parse every .lua entry of lua.nx2.
    bool verifyArchives = false; ///< Read (inflate) every entry of every mounted archive.
    std::function<void(std::string_view)> log; ///< LOG() lines of the script.
  };

  struct Lookup
  {
    std::string vfsPath;
    std::string expectedSource;
    std::string tier;
    bool found = false;
    std::string source; ///< Lower-case file name of the winning archive / directory mount.
    FileLocation location;
    bool ok = false;     ///< Matches the expectation.
    std::string problem; ///< Why not, when !ok.
  };

  struct LuaCheck
  {
    bool ran = false;
    std::string archive;
    std::size_t parsed = 0;
    std::size_t failed = 0;
    double milliseconds = 0.0;
    std::vector<std::string> errors; ///< First failures, capped.
  };

  struct ArchiveCheck
  {
    bool ran = false;
    std::size_t entries = 0;
    std::size_t failed = 0;
    std::uint64_t bytes = 0;
    double milliseconds = 0.0;
    std::vector<std::string> errors; ///< First failures, capped.
  };

  struct Report
  {
    DataPathResult dataPath;
    std::size_t mounts = 0;
    std::size_t archives = 0;
    std::size_t directories = 0;
    std::size_t archiveEntries = 0;
    std::size_t skippedPathEntries = 0; ///< `path` entries that mounted nothing (missing on disk).
    std::vector<std::string> mountErrors;
    double mountMilliseconds = 0.0;
    std::vector<Lookup> lookups;
    LuaCheck lua;
    ArchiveCheck archiveCheck;
    std::vector<std::string> warnings; ///< Recommended data missing and similar.
    std::vector<std::string> problems; ///< Anything that makes the run fail.
    [[nodiscard]] bool Ok() const
    {
      return problems.empty();
    }
  };

  /// Runs the checks; `vfs` must be empty and receives the mounts so the
  /// caller can read from it afterwards.
  [[nodiscard]] Report Run(const Options& options, VirtualFileSystem& vfs);

  struct Extraction
  {
    bool ok = false;
    std::string error;
    std::string format;
    int width = 0;
    int height = 0;
    std::size_t ddsBytes = 0;
    double decodeMilliseconds = 0.0;
  };

  /// Reads a DDS through the VFS, decodes it and writes it as PNG.
  [[nodiscard]] Extraction
  ExtractToPng(const VirtualFileSystem& vfs, std::string_view vfsPath, const std::string& pngPath);

  /// Lower-cased last component of a '/'-separated path.
  [[nodiscard]] std::string LeafName(std::string_view path);

} // namespace faf::port::datacheck
