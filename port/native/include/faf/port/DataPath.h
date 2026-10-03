#pragma once

// Runs the data-path script (init_faf.lua, or SupComDataPath.lua) the way
// moho::DISK_SetupDataAndSearchPaths does (src/sdk/moho/misc/StartupHelpers.cpp,
// ~5698-5812): publish LaunchDir and InitFileDir, expose the "unsafe" bindings
// the script calls, run it against the globals, then read back the `path`,
// `hook` and `protocols` tables.
//
// The Lua core is stock LuaPlus 1081 (Lua 5.0.1, float lua_Number) with the
// lexer patches the FAF scripts need ('!=' and a UTF-8 BOM), not the engine's
// recovered VM - see port/native/README.md.
//
// Bindings: LOG/_ALERT/print (tab-joined tostring of the arguments, like the
// engine's LOG), SHGetFolderPath, io.dir, and wrapped io.open/io.lines/
// io.input/io.output/os.remove/os.rename/dofile/loadfile that resolve paths
// case-insensitively. os.execute and os.exit raise errors. Each run has its own
// lua_State and the bindings find their context through upvalues, so several
// runs (also concurrent ones) in one process do not interfere.

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "faf/port/Vfs.h"

namespace faf::port {

  /// Where SHGetFolderPath(<name>) points. Values are directories; the binding
  /// appends a trailing '/' because the scripts concatenate onto the result
  /// ("...LOCAL_APPDATA') .. 'Gas Powered Games/...").
  struct KnownFolders
  {
    std::string localAppData; ///< 'LOCAL_APPDATA' (also the fallback for the other CSIDL names the engine knows).
    std::string appData;      ///< 'APPDATA'; empty = localAppData.
    std::string personal;     ///< 'PERSONAL' / 'MYDOCUMENTS' (Documents); empty = localAppData.
  };

  struct DataPathOptions
  {
    std::string scriptPath; ///< Absolute path of the data-path script.
    std::string launchDir;  ///< Value of LaunchDir; empty = the script's directory.
    KnownFolders folders;
    /// Receives every LOG() line and the runner's own diagnostics. May be empty.
    std::function<void(std::string_view)> log;
    /// Progress breadcrumbs (file read, Lua state ready, script started, each
    /// dofile), so a run that dies in native code shows how far it got. Kept
    /// apart from `log`, which mirrors the game's own log. May be empty.
    std::function<void(std::string_view)> trace;
    /// false blocks os.remove and write-mode io.open (host dry runs against a
    /// real install must not touch it). The device runs with writes allowed:
    /// init_faf.lua clears its shader cache with os.remove.
    bool allowWrites = true;
  };

  struct DataPathResult
  {
    bool ok = false;
    std::string error;
    std::vector<MountSpec> mounts;      ///< `path`, in table order.
    std::vector<std::string> hooks;     ///< `hook`.
    std::vector<std::string> protocols; ///< `protocols`.
    double scriptMilliseconds = 0.0;
    std::string initFileDir;   ///< InitFileDir as published to the script.
    std::string launchDir;     ///< LaunchDir as published to the script.
    std::size_t logLines = 0;  ///< LOG()/print() calls made by the script.
  };

  [[nodiscard]] DataPathResult RunDataPathScript(const DataPathOptions& options);

  /// Parses `source` as a Lua chunk with the same Lua core, without running it.
  /// Used by the host checks to prove every FAF .lua file is readable.
  /// `chunkName` appears in error messages ("lua/x.lua:12: ...").
  bool CheckLuaSyntax(const void* source, std::size_t size, std::string_view chunkName, std::string* error = nullptr);

} // namespace faf::port
