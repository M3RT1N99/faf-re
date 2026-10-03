// faf_datacheck: runs a data-path script and mounts the data exactly as the
// Android runtime does, on the host, and reports what the engine would see.
//
//   faf_datacheck --init <script> [--root <dir>] [--localappdata <dir>] [--documents <dir>]
//                 [--no-writes] [--extract <vfspath> <out.png>]... [--check-lua]
//                 [--verify-archives] [--list-mounts] [--json] [--verbose]
//
// --root <dir> points at an Android-style data root (as staged by
// scripts/port/deploy_android.ps1 -StageDir): the script defaults to
// <root>/faf/bin/init_faf.lua and the known folders to <root>/localappdata and
// <root>/documents. Without --root and --localappdata the folders default to a
// sandbox under the system temp directory, never the real profile.
//
// Exit code: 0 all checks passed, 1 a check failed, 2 usage error.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "faf/port/FileSystem.h"

#include "DataCheck.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

  using namespace faf::port;

  struct Extract
  {
    std::string vfsPath;
    std::string pngPath;
    datacheck::Extraction result;
  };

  struct CommandLineOptions
  {
    std::string init;
    std::string root;
    std::string localAppData;
    std::string documents;
    bool noWrites = false;
    bool checkLua = false;
    bool verifyArchives = false;
    bool listMounts = false;
    bool json = false;
    bool verbose = false;
    std::vector<Extract> extracts;
  };

  constexpr const char* kUsage =
    "usage: faf_datacheck --init <script> [--root <dir>] [--localappdata <dir>] [--documents <dir>]\n"
    "                     [--no-writes] [--extract <vfspath> <out.png>]... [--check-lua]\n"
    "                     [--verify-archives] [--list-mounts] [--json] [--verbose]\n"
    "  --init <script>        data-path script (default with --root: <root>/faf/bin/init_faf.lua)\n"
    "  --root <dir>           Android-style data root; defaults the script and the known folders\n"
    "  --localappdata <dir>   SHGetFolderPath('LOCAL_APPDATA')\n"
    "  --documents <dir>      SHGetFolderPath('PERSONAL')\n"
    "  --no-writes            block os.remove and write-mode io.open (use against a real install)\n"
    "  --extract <vfs> <png>  decode a DDS from the mounted data and write it as PNG\n"
    "  --check-lua            parse every .lua in lua.nx2 with the port's Lua core\n"
    "  --verify-archives      read and inflate every entry of every mounted archive\n"
    "  --list-mounts          print the mount table\n"
    "  --json                 machine-readable report on stdout\n"
    "  --verbose              echo the script's LOG() lines to stderr\n";

  int UsageError(const std::string& message)
  {
    std::fprintf(stderr, "faf_datacheck: %s\n%s", message.c_str(), kUsage);
    return 2;
  }

  /// Parses argv; returns an exit code to stop with, or -1 to continue.
  int Parse(const int argc, char** const argv, CommandLineOptions& options)
  {
    for (int i = 1; i < argc; ++i) {
      const std::string_view arg = argv[i];
      const auto value = [&](std::string& out) {
        if (i + 1 >= argc) {
          return false;
        }
        out = argv[++i];
        return true;
      };
      if (arg == "--help" || arg == "-h") {
        std::fputs(kUsage, stdout);
        return 0;
      }
      if (arg == "--init") {
        if (!value(options.init)) {
          return UsageError("--init needs a path");
        }
      } else if (arg == "--root") {
        if (!value(options.root)) {
          return UsageError("--root needs a directory");
        }
      } else if (arg == "--localappdata") {
        if (!value(options.localAppData)) {
          return UsageError("--localappdata needs a directory");
        }
      } else if (arg == "--documents") {
        if (!value(options.documents)) {
          return UsageError("--documents needs a directory");
        }
      } else if (arg == "--extract") {
        Extract extract;
        if (!value(extract.vfsPath) || !value(extract.pngPath)) {
          return UsageError("--extract needs a VFS path and an output file");
        }
        options.extracts.push_back(std::move(extract));
      } else if (arg == "--no-writes") {
        options.noWrites = true;
      } else if (arg == "--check-lua") {
        options.checkLua = true;
      } else if (arg == "--verify-archives") {
        options.verifyArchives = true;
      } else if (arg == "--list-mounts") {
        options.listMounts = true;
      } else if (arg == "--json") {
        options.json = true;
      } else if (arg == "--verbose") {
        options.verbose = true;
      } else {
        return UsageError("unknown argument '" + std::string(arg) + "'");
      }
    }

    if (!options.root.empty()) {
      const std::string root = fs::NormalizePath(options.root);
      if (options.init.empty()) {
        options.init = root + "/faf/bin/init_faf.lua";
      }
      if (options.localAppData.empty()) {
        options.localAppData = root + "/localappdata";
      }
      if (options.documents.empty()) {
        options.documents = root + "/documents";
      }
    }
    if (options.init.empty()) {
      return UsageError("--init or --root is required");
    }
    if (options.localAppData.empty()) {
      std::error_code ec;
      const std::filesystem::path temp = std::filesystem::temp_directory_path(ec);
      const std::u8string base = (ec ? std::filesystem::path(".") : temp).u8string();
      options.localAppData = fs::NormalizePath(std::string(base.begin(), base.end()) + "/faf_datacheck/localappdata");
    }
    if (options.documents.empty()) {
      options.documents = options.localAppData + "/../documents";
    }
    options.init = fs::NormalizePath(options.init);
    options.localAppData = fs::NormalizePath(options.localAppData);
    options.documents = fs::NormalizePath(options.documents);
    return -1;
  }

  // ------------------------------------------------------------- JSON output

  std::string Json(const std::string_view text)
  {
    std::string out = "\"";
    for (const char c : text) {
      switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char escaped[8];
          std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
          out += escaped;
        } else {
          out.push_back(c);
        }
      }
    }
    out.push_back('"');
    return out;
  }

  std::string Json(const bool value)
  {
    return value ? "true" : "false";
  }

  std::string Json(const double value)
  {
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f", value);
    return text;
  }

  template <typename Integer>
  std::string JsonInt(const Integer value)
  {
    return std::to_string(value);
  }

  std::string Json(const std::vector<std::string>& values)
  {
    std::string out = "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
      out += (i > 0 ? "," : "") + Json(values[i]);
    }
    return out + "]";
  }

  void PrintJson(
    const CommandLineOptions& options,
    const datacheck::Report& report,
    const VirtualFileSystem& vfs,
    bool ok
  )
  {
    const DataPathResult& dataPath = report.dataPath;
    std::string out = "{";
    out += "\"ok\":" + Json(ok);
    out += ",\"script\":" + Json(options.init);
    out += ",\"initFileDir\":" + Json(dataPath.initFileDir);
    out += ",\"localAppData\":" + Json(options.localAppData);
    out += ",\"documents\":" + Json(options.documents);
    out += ",\"allowWrites\":" + Json(!options.noWrites);
    out += ",\"dataPath\":{\"ok\":" + Json(dataPath.ok) + ",\"error\":" + Json(dataPath.error) +
           ",\"pathEntries\":" + JsonInt(dataPath.mounts.size()) + ",\"hooks\":" + Json(dataPath.hooks) +
           ",\"protocols\":" + Json(dataPath.protocols) + ",\"logLines\":" + JsonInt(dataPath.logLines) +
           ",\"scriptMs\":" + Json(dataPath.scriptMilliseconds) + "}";
    out += ",\"mounts\":" + JsonInt(report.mounts);
    out += ",\"archives\":" + JsonInt(report.archives);
    out += ",\"directories\":" + JsonInt(report.directories);
    out += ",\"archiveEntries\":" + JsonInt(report.archiveEntries);
    out += ",\"skippedPathEntries\":" + JsonInt(report.skippedPathEntries);
    out += ",\"mountMs\":" + Json(report.mountMilliseconds);
    if (options.listMounts) {
      out += ",\"mountList\":[";
      for (std::size_t i = 0; i < vfs.Mounts().size(); ++i) {
        const MountInfo& mount = vfs.Mounts()[i];
        out += (i > 0 ? "," : "") + std::string("{\"disk\":") + Json(mount.diskPath) +
               ",\"mountpoint\":" + Json(mount.mountpoint) + ",\"archive\":" + Json(mount.isArchive) +
               ",\"entries\":" + JsonInt(mount.entryCount) + "}";
      }
      out += "]";
    }
    out += ",\"lookups\":[";
    for (std::size_t i = 0; i < report.lookups.size(); ++i) {
      const datacheck::Lookup& lookup = report.lookups[i];
      out += (i > 0 ? "," : "") + std::string("{\"path\":") + Json(lookup.vfsPath) +
             ",\"expected\":" + Json(lookup.expectedSource) + ",\"tier\":" + Json(lookup.tier) +
             ",\"found\":" + Json(lookup.found) + ",\"source\":" + Json(lookup.source) +
             ",\"disk\":" + Json(lookup.location.diskPath) + ",\"entry\":" + Json(lookup.location.entryName) +
             ",\"size\":" + JsonInt(lookup.location.size) + ",\"compressed\":" + Json(lookup.location.compressed) +
             ",\"ok\":" + Json(lookup.ok) + ",\"problem\":" + Json(lookup.problem) + "}";
    }
    out += "]";
    if (report.lua.ran) {
      out += ",\"lua\":{\"archive\":" + Json(report.lua.archive) + ",\"parsed\":" + JsonInt(report.lua.parsed) +
             ",\"failed\":" + JsonInt(report.lua.failed) + ",\"ms\":" + Json(report.lua.milliseconds) +
             ",\"errors\":" + Json(report.lua.errors) + "}";
    }
    if (report.archiveCheck.ran) {
      const datacheck::ArchiveCheck& check = report.archiveCheck;
      out += ",\"archiveCheck\":{\"entries\":" + JsonInt(check.entries) + ",\"failed\":" + JsonInt(check.failed) +
             ",\"bytes\":" + JsonInt(check.bytes) + ",\"ms\":" + Json(check.milliseconds) +
             ",\"errors\":" + Json(check.errors) + "}";
    }
    out += ",\"extract\":[";
    for (std::size_t i = 0; i < options.extracts.size(); ++i) {
      const Extract& extract = options.extracts[i];
      out += (i > 0 ? "," : "") + std::string("{\"path\":") + Json(extract.vfsPath) +
             ",\"png\":" + Json(extract.pngPath) + ",\"ok\":" + Json(extract.result.ok) +
             ",\"format\":" + Json(extract.result.format) + ",\"width\":" + JsonInt(extract.result.width) +
             ",\"height\":" + JsonInt(extract.result.height) + ",\"ddsBytes\":" + JsonInt(extract.result.ddsBytes) +
             ",\"decodeMs\":" + Json(extract.result.decodeMilliseconds) + ",\"error\":" + Json(extract.result.error) +
             "}";
    }
    out += "]";
    out += ",\"warnings\":" + Json(report.warnings);
    out += ",\"problems\":" + Json(report.problems);
    out += "}\n";
    std::fputs(out.c_str(), stdout);
  }

  // ------------------------------------------------------------- text output

  void PrintText(
    const CommandLineOptions& options,
    const datacheck::Report& report,
    const VirtualFileSystem& vfs,
    bool ok
  )
  {
    const DataPathResult& dataPath = report.dataPath;
    std::printf("faf_datacheck  (LuaPlus 1081 / Lua 5.0.1, lua_Number = float)\n");
    std::printf("script         %s\n", options.init.c_str());
    if (!dataPath.initFileDir.empty()) {
      std::printf("InitFileDir    %s\n", dataPath.initFileDir.c_str());
    }
    std::printf("LOCAL_APPDATA  %s\n", options.localAppData.c_str());
    std::printf("PERSONAL       %s\n", options.documents.c_str());
    std::printf("writes         %s\n\n", options.noWrites ? "disabled (--no-writes)" : "allowed");

    if (!dataPath.ok) {
      std::printf("data-path      FAILED: %s\n", dataPath.error.c_str());
    } else {
      std::printf(
        "data-path      ok in %.1f ms: %zu path entries, %zu hook(s), %zu protocol(s), %zu LOG line(s)\n",
        dataPath.scriptMilliseconds,
        dataPath.mounts.size(),
        dataPath.hooks.size(),
        dataPath.protocols.size(),
        dataPath.logLines
      );
      std::printf(
        "mounts         %zu (%zu archives, %zu directories), %zu archive entries, %zu path entr%s skipped (missing), "
        "%.1f ms\n",
        report.mounts,
        report.archives,
        report.directories,
        report.archiveEntries,
        report.skippedPathEntries,
        report.skippedPathEntries == 1 ? "y" : "ies",
        report.mountMilliseconds
      );
    }

    if (options.listMounts) {
      std::printf("mount table\n");
      for (std::size_t i = 0; i < vfs.Mounts().size(); ++i) {
        const MountInfo& mount = vfs.Mounts()[i];
        std::printf(
          "  %4zu  %-40s <- %s%s\n",
          i + 1,
          mount.mountpoint.c_str(),
          mount.diskPath.c_str(),
          mount.isArchive ? (" [" + std::to_string(mount.entryCount) + " entries]").c_str() : ""
        );
      }
    }

    if (!report.lookups.empty()) {
      std::printf("lookups (first mount wins)\n");
      for (const datacheck::Lookup& lookup : report.lookups) {
        std::string where = "(not found)";
        if (lookup.found) {
          where = lookup.location.diskPath;
          if (!lookup.location.entryName.empty()) {
            where += lookup.location.compressed ? "  [deflate, " : "  [stored, ";
            where += std::to_string(lookup.location.size) + " bytes]";
          }
        }
        std::printf(
          "  %-8s %-56s %s\n",
          lookup.ok ? "ok" : (lookup.tier == "required" || lookup.found ? "FAIL" : "missing"),
          lookup.vfsPath.c_str(),
          where.c_str()
        );
        if (!lookup.ok) {
          std::printf("           -> %s\n", lookup.problem.c_str());
        }
      }
    }

    if (report.lua.ran) {
      std::printf(
        "lua            %zu parsed, %zu failed in %s (%.1f ms)\n",
        report.lua.parsed,
        report.lua.failed,
        report.lua.archive.empty() ? "(lua.nx2 not mounted)" : report.lua.archive.c_str(),
        report.lua.milliseconds
      );
      for (const std::string& error : report.lua.errors) {
        std::printf("  %s\n", error.c_str());
      }
    }
    if (report.archiveCheck.ran) {
      const datacheck::ArchiveCheck& check = report.archiveCheck;
      std::printf(
        "archives       %zu entries read, %zu failed, %.1f MiB in %.1f ms\n",
        check.entries,
        check.failed,
        static_cast<double>(check.bytes) / (1024.0 * 1024.0),
        check.milliseconds
      );
      for (const std::string& error : check.errors) {
        std::printf("  %s\n", error.c_str());
      }
    }
    for (const Extract& extract : options.extracts) {
      if (extract.result.ok) {
        std::printf(
          "extract        %s: %s %dx%d (%zu bytes, decode %.1f ms) -> %s\n",
          extract.vfsPath.c_str(),
          extract.result.format.c_str(),
          extract.result.width,
          extract.result.height,
          extract.result.ddsBytes,
          extract.result.decodeMilliseconds,
          extract.pngPath.c_str()
        );
      } else {
        std::printf("extract        %s: FAILED: %s\n", extract.vfsPath.c_str(), extract.result.error.c_str());
      }
    }
    for (const std::string& warning : report.warnings) {
      std::printf("warning        %s\n", warning.c_str());
    }
    for (const std::string& problem : report.problems) {
      std::printf("problem        %s\n", problem.c_str());
    }
    std::printf("result         %s\n", ok ? "OK" : "FAILED");
  }

} // namespace

int main(int argc, char** argv)
{
#if defined(_WIN32)
  ::SetConsoleOutputCP(CP_UTF8);
#endif
  CommandLineOptions options;
  if (const int stop = Parse(argc, argv, options); stop >= 0) {
    return stop;
  }

  datacheck::Options checkOptions;
  checkOptions.scriptPath = options.init;
  checkOptions.localAppData = options.localAppData;
  checkOptions.documents = options.documents;
  checkOptions.allowWrites = !options.noWrites;
  checkOptions.checkLua = options.checkLua;
  checkOptions.verifyArchives = options.verifyArchives;
  if (options.verbose) {
    checkOptions.log = [](const std::string_view line) {
      std::fprintf(stderr, "[LOG] %.*s\n", static_cast<int>(line.size()), line.data());
    };
  }

  VirtualFileSystem vfs;
  datacheck::Report report = datacheck::Run(checkOptions, vfs);
  bool ok = report.Ok();
  if (report.dataPath.ok) {
    for (Extract& extract : options.extracts) {
      extract.result = datacheck::ExtractToPng(vfs, extract.vfsPath, extract.pngPath);
      if (!extract.result.ok) {
        report.problems.push_back("extract " + extract.vfsPath + ": " + extract.result.error);
        ok = false;
      }
    }
  }

  if (options.json) {
    PrintJson(options, report, vfs, ok);
  } else {
    PrintText(options, report, vfs, ok);
  }
  return ok ? 0 : 1;
}
