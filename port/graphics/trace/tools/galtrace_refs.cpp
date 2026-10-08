// galtrace-refs: makes a galtrace that ships without the game's data, and checks it (GalTraceRefs.h).
// The game data is mounted exactly as the device mounts it: the data-path script (init_faf.lua) through
// port/native's RunDataPathScript, its `path` table into port/native's VirtualFileSystem.
//
//   galtrace-refs convert <recording> <out> --init <init_faf.lua> [data options] [--meta key=value]...
//                 [--keep-readbacks] [--allow-embedded-game-files] [--json <file>]
//   galtrace-refs check <trace> --init <init_faf.lua> [data options] [--against <recording>] [--json <file>]
//   galtrace-refs extract <trace> --init <init_faf.lua> [data options] --out <dir>
//   galtrace-refs setmeta <in> <out> [--meta key=value]... [--remove key]...
//   galtrace-refs refs <trace>          the references, as the app's data check sees them
//
//   data options: --localappdata <dir> --documents <dir> (SHGetFolderPath answers for the script; default
//                 a temporary folder). The script runs with writes blocked.
//
// Exit code 0 when the command succeeded (check: every rule held), 1 when not, 2 for usage errors.

#include "GalTraceRefs.h"

#include "../format/GalTraceResolver.h"
#include "../format/GalTraceVfsResolver.h"

#include "faf/port/DataPath.h"
#include "faf/port/FileSystem.h"
#include "faf/port/Vfs.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace
{
  using Clock = std::chrono::steady_clock;

  std::string JsonEscape(const std::string& text)
  {
    std::string out;
    for (const char raw : text) {
      const auto c = static_cast<unsigned char>(raw);
      if (c == '"' || c == '\\') {
        out += '\\';
        out += static_cast<char>(c);
      } else if (c < 0x20) {
        char escaped[8];
        std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
        out += escaped;
      } else {
        out += static_cast<char>(c);
      }
    }
    return out;
  }

  std::string JsonList(const std::vector<std::string>& items)
  {
    std::string out = "[";
    for (std::size_t index = 0; index < items.size(); ++index) {
      out += (index == 0 ? "\n    \"" : ",\n    \"") + JsonEscape(items[index]) + "\"";
    }
    return out + (items.empty() ? "]" : "\n  ]");
  }

  std::string Lower(std::string text)
  {
    for (char& c : text) {
      if (c >= 'A' && c <= 'Z') {
        c = static_cast<char>(c - 'A' + 'a');
      }
    }
    return text;
  }

  /** The mounted game data as galtrace::refs::GameFileIndex. */
  class VfsIndex final : public galtrace::refs::GameFileIndex
  {
  public:
    bool Mount(const std::string& script, const std::string& localAppData, const std::string& documents, std::string* const error)
    {
      const Clock::time_point start = Clock::now();
      faf::port::DataPathOptions options;
      options.scriptPath = faf::port::fs::NormalizePath(script);
      options.folders.localAppData = localAppData;
      options.folders.personal = documents;
      options.allowWrites = false;
      const faf::port::DataPathResult result = faf::port::RunDataPathScript(options);
      if (!result.ok) {
        *error = "data-path script failed: " + result.error;
        return false;
      }
      for (const faf::port::MountSpec& spec : result.mounts) {
        std::string mountError;
        (void)vfs_.Mount(spec, &mountError);
        if (!mountError.empty()) {
          std::fprintf(stderr, "galtrace-refs: mount: %s\n", mountError.c_str());
        }
      }
      std::size_t archives = 0;
      for (const faf::port::MountInfo& mount : vfs_.Mounts()) {
        archives += mount.isArchive ? 1u : 0u;
      }
      std::fprintf(stderr, "galtrace-refs: %s: %zu mounts (%zu archives) in %.0f ms\n", options.scriptPath.c_str(), vfs_.Mounts().size(), archives,
                   std::chrono::duration<double, std::milli>(Clock::now() - start).count());
      return !vfs_.Mounts().empty() || (*error = "the data-path script mounted nothing", false);
    }

    std::vector<std::string> CandidatesBySize(const std::uint64_t size) override
    {
      if (!built_) {
        Build();
      }
      // Archive entries first (mount order, then archive order), then files of directory mounts: a
      // reference to an archive the user imports beats one to a map or mod folder with the same bytes.
      std::vector<std::string> paths;
      for (const auto* index : {&archiveBySize_, &directoryBySize_}) {
        const auto range = index->equal_range(size);
        for (auto it = range.first; it != range.second; ++it) {
          paths.push_back(it->second);
        }
      }
      return paths;
    }

    bool Read(const std::string& vfsPath, std::vector<std::uint8_t>* const out, std::string* const error) override
    {
      return vfs_.Read(vfsPath, *out, error);
    }

    std::string ArchiveOf(const std::string& vfsPath) override
    {
      const faf::port::FileLocation location = vfs_.Find(vfsPath);
      if (!location.found) {
        return {};
      }
      std::string disk = location.diskPath;
      if (location.entryName.empty()) {
        // A directory mount: name the mounted directory.
        disk = vfs_.Mounts()[location.mountIndex].diskPath;
      }
      const std::size_t slash = disk.find_last_of("/\\");
      return Lower(slash == std::string::npos ? disk : disk.substr(slash + 1));
    }

    [[nodiscard]] const faf::port::VirtualFileSystem& Vfs() const { return vfs_; }

  private:
    /** Every file that wins its own lookup, by size: archive entries, and the files of directory mounts. */
    void Build()
    {
      built_ = true;
      const Clock::time_point start = Clock::now();
      std::size_t entries = 0;
      std::size_t files = 0;
      std::size_t skipped = 0;
      const std::vector<faf::port::MountInfo>& mounts = vfs_.Mounts();
      auto add = [&](const std::size_t mount, const std::string& relative) {
        std::string path = mounts[mount].mountpoint;
        for (const char c : relative) {
          path += c == '\\' ? '/' : c;
        }
        path = Lower(path);
        const faf::port::FileLocation location = vfs_.Find(path);
        if (location.found && location.mountIndex == mount) {
          (mounts[mount].isArchive ? archiveBySize_ : directoryBySize_).emplace(location.size, path);
          return true;
        }
        return false;
      };
      for (std::size_t mount = 0; mount < mounts.size(); ++mount) {
        if (mounts[mount].isArchive) {
          for (const std::string& name : vfs_.ArchiveEntries(mount)) {
            entries += add(mount, name) ? 1u : 0u;
          }
          continue;
        }
        // A directory mount (the install's fonts, maps, mods): its files, as the VFS sees them. Paths are
        // UTF-8 (a name the ANSI code page cannot hold must not throw).
        try {
          std::error_code code;
          const std::string& disk = mounts[mount].diskPath;
          const std::filesystem::path root(std::u8string(disk.begin(), disk.end()));
          for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, code), end;
               !code && it != end; it.increment(code)) {
            if (it->is_regular_file(code)) {
              const std::u8string relative = it->path().lexically_relative(root).generic_u8string();
              files += add(mount, std::string(relative.begin(), relative.end())) ? 1u : 0u;
            }
          }
        } catch (const std::exception& error) {
          ++skipped;
          std::fprintf(stderr, "galtrace-refs: %s: not indexed (%s)\n", mounts[mount].diskPath.c_str(), error.what());
        }
      }
      std::fprintf(stderr, "galtrace-refs: size index of %zu archive files and %zu files of directory mounts (%zu mounts skipped) in %.0f ms\n",
                   entries, files, skipped, std::chrono::duration<double, std::milli>(Clock::now() - start).count());
    }

    faf::port::VirtualFileSystem vfs_;
    std::multimap<std::uint64_t, std::string> archiveBySize_;   // equal sizes in insertion (mount) order
    std::multimap<std::uint64_t, std::string> directoryBySize_;
    bool built_ = false;
  };

  struct Args
  {
    std::string command;
    std::vector<std::string> positional;
    std::string init;
    std::string localAppData;
    std::string documents;
    std::string against;
    std::string out;
    std::string json;
    galtrace::Metadata meta;
    std::vector<std::string> remove;
    bool keepReadbacks = false;
    bool allowEmbedded = false;
  };

  int Usage(const char* const message)
  {
    if (message != nullptr) {
      std::fprintf(stderr, "galtrace-refs: %s\n", message);
    }
    std::fprintf(stderr,
                 "usage: galtrace-refs convert <recording> <out> --init <init_faf.lua> [--localappdata d] [--documents d]\n"
                 "                     [--meta key=value]... [--keep-readbacks] [--allow-embedded-game-files] [--json f]\n"
                 "       galtrace-refs check <trace> --init <init_faf.lua> [--against <recording>] [--json f]\n"
                 "       galtrace-refs extract <trace> --init <init_faf.lua> --out <dir>\n"
                 "       galtrace-refs setmeta <in> <out> [--meta key=value]... [--remove key]...\n"
                 "       galtrace-refs refs <trace>\n");
    return 2;
  }

  bool MountData(const Args& args, VfsIndex* const index)
  {
    if (args.init.empty()) {
      std::fprintf(stderr, "galtrace-refs: --init <init_faf.lua> is required\n");
      return false;
    }
    std::string localAppData = args.localAppData;
    std::string documents = args.documents;
    if (localAppData.empty()) {
      std::error_code code;
      const std::filesystem::path base = std::filesystem::temp_directory_path(code) / "galtrace_refs" / "localappdata";
      std::filesystem::create_directories(base, code);
      localAppData = faf::port::fs::NormalizePath(base.string());
    }
    if (documents.empty()) {
      documents = localAppData + "/../documents";
    }
    std::string error;
    if (!index->Mount(args.init, faf::port::fs::NormalizePath(localAppData), faf::port::fs::NormalizePath(documents), &error)) {
      std::fprintf(stderr, "galtrace-refs: %s\n", error.c_str());
      return false;
    }
    return true;
  }

  void WriteText(const std::string& path, const std::string& text)
  {
    if (path.empty()) {
      return;
    }
    if (std::FILE* const file = std::fopen(path.c_str(), "wb")) {
      std::fwrite(text.data(), 1, text.size(), file);
      std::fclose(file);
    }
  }

  int Convert(const Args& args)
  {
    if (args.positional.size() != 2) {
      return Usage("convert needs <recording> <out>");
    }
    VfsIndex index;
    if (!MountData(args, &index)) {
      return 1;
    }
    galtrace::refs::ConvertOptions options;
    options.setMetadata = args.meta;
    options.digestReadbacks = !args.keepReadbacks;
    options.allowEmbeddedGameFiles = args.allowEmbedded;
    const Clock::time_point start = Clock::now();
    options.log = [start](const std::string& line) {
      std::fprintf(stderr, "galtrace-refs: %7.1f s %s\n", std::chrono::duration<double>(Clock::now() - start).count(), line.c_str());
    };
    galtrace::refs::ConvertReport report;
    const bool ok = galtrace::refs::Convert(args.positional[0], args.positional[1], index, options, &report);
    const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
    for (const std::string& line : report.referenceLines) {
      std::printf("reference %s\n", line.c_str());
    }
    for (const std::string& line : report.compositionLines) {
      std::printf("composition %s\n", line.c_str());
    }
    for (const auto& [use, count] : report.byUse) {
      std::printf("payloads %-42s %5u  %12llu bytes\n", use.c_str(), count.count, static_cast<unsigned long long>(count.bytes));
    }
    for (const std::string& line : report.unresolved) {
      std::printf("UNRESOLVED %s\n", line.c_str());
    }
    for (const std::string& line : report.notes) {
      std::printf("note %s\n", line.c_str());
    }
    std::printf("records %llu, presents %u, readbacks %u\n", static_cast<unsigned long long>(report.records), report.presents, report.readbacks);
    std::printf("embedded %u (%llu bytes), references %u (%llu bytes), digests %u (%llu bytes), compositions %u (%llu bytes; %u copies, %llu literal bytes)\n",
                report.embedded.count, static_cast<unsigned long long>(report.embedded.bytes), report.references.count,
                static_cast<unsigned long long>(report.references.bytes), report.digests.count, static_cast<unsigned long long>(report.digests.bytes),
                report.compositions.count, static_cast<unsigned long long>(report.compositions.bytes), report.compositionCopies,
                static_cast<unsigned long long>(report.compositionLiteralBytes));
    std::printf("file %llu -> %llu bytes in %.1f s\n", static_cast<unsigned long long>(report.inputBytes),
                static_cast<unsigned long long>(report.outputBytes), seconds);
    std::printf("CONVERT: %s\n", ok ? "OK" : ("FAILED: " + report.error).c_str());

    std::string json = "{\n  \"ok\": " + std::string(ok ? "true" : "false") + ",\n  \"error\": \"" + JsonEscape(report.error) + "\",\n";
    json += "  \"input\": \"" + JsonEscape(args.positional[0]) + "\",\n  \"output\": \"" + JsonEscape(args.positional[1]) + "\",\n";
    json += "  \"input_bytes\": " + std::to_string(report.inputBytes) + ",\n  \"output_bytes\": " + std::to_string(report.outputBytes) + ",\n";
    json += "  \"records\": " + std::to_string(report.records) + ",\n  \"presents\": " + std::to_string(report.presents) + ",\n";
    json += "  \"readbacks\": " + std::to_string(report.readbacks) + ",\n";
    json += "  \"embedded\": {\"count\": " + std::to_string(report.embedded.count) + ", \"bytes\": " + std::to_string(report.embedded.bytes) + "},\n";
    json += "  \"references\": {\"count\": " + std::to_string(report.references.count) + ", \"bytes\": " + std::to_string(report.references.bytes) + "},\n";
    json += "  \"digests\": {\"count\": " + std::to_string(report.digests.count) + ", \"bytes\": " + std::to_string(report.digests.bytes) + "},\n";
    json += "  \"compositions\": {\"count\": " + std::to_string(report.compositions.count) + ", \"bytes\": " +
            std::to_string(report.compositions.bytes) + ", \"copies\": " + std::to_string(report.compositionCopies) +
            ", \"literal_bytes\": " + std::to_string(report.compositionLiteralBytes) + "},\n";
    json += "  \"by_use\": {";
    bool first = true;
    for (const auto& [use, count] : report.byUse) {
      json += std::string(first ? "\n    " : ",\n    ") + "\"" + JsonEscape(use) + "\": {\"count\": " + std::to_string(count.count) +
              ", \"bytes\": " + std::to_string(count.bytes) + "}";
      first = false;
    }
    json += "\n  },\n";
    json += "  \"reference_lines\": " + JsonList(report.referenceLines) + ",\n";
    json += "  \"composition_lines\": " + JsonList(report.compositionLines) + ",\n";
    json += "  \"unresolved\": " + JsonList(report.unresolved) + ",\n";
    json += "  \"notes\": " + JsonList(report.notes) + ",\n";
    char secondsText[32];
    std::snprintf(secondsText, sizeof(secondsText), "%.2f", seconds);
    json += "  \"seconds\": " + std::string(secondsText) + "\n}\n";
    WriteText(args.json, json);
    return ok ? 0 : 1;
  }

  int Check(const Args& args)
  {
    if (args.positional.size() != 1) {
      return Usage("check needs <trace>");
    }
    VfsIndex index;
    if (!MountData(args, &index)) {
      return 1;
    }
    galtrace::refs::CheckOptions options;
    options.againstPath = args.against;
    galtrace::refs::CheckReport report;
    const Clock::time_point start = Clock::now();
    const bool ok = galtrace::refs::Check(args.positional[0], index, options, &report);
    const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
    for (const std::string& line : report.lines) {
      std::printf("%s\n", line.c_str());
    }
    for (const std::string& line : report.referenceProblems) {
      std::printf("reference problem: %s\n", line.c_str());
    }
    for (const std::string& line : report.equivalenceProblems) {
      std::printf("equivalence problem: %s\n", line.c_str());
    }
    for (const std::string& line : report.gameContentFindings) {
      std::printf("GAME CONTENT: %s\n", line.c_str());
    }
    if (!report.error.empty()) {
      std::printf("error: %s\n", report.error.c_str());
    }
    std::printf("CHECK: %s (%.1f s)\n", ok ? "PASS" : "FAIL", seconds);
    std::string json = "{\n  \"ok\": " + std::string(ok ? "true" : "false") + ",\n  \"trace\": \"" + JsonEscape(args.positional[0]) + "\",\n";
    json += "  \"valid\": " + std::string(report.valid ? "true" : "false") + ",\n  \"validator_error\": \"" + JsonEscape(report.validatorError) + "\",\n";
    json += "  \"references\": " + std::to_string(report.references) + ",\n  \"references_resolved\": " + std::to_string(report.referencesResolved) + ",\n";
    json += "  \"embedded_payloads\": " + std::to_string(report.embeddedPayloads) + ",\n  \"embedded_bytes\": " + std::to_string(report.embeddedBytes) + ",\n";
    json += "  \"literal_bytes\": " + std::to_string(report.literalBytes) + ",\n";
    json += "  \"game_windows_indexed\": " + std::to_string(report.gameChunksIndexed) + ",\n";
    json += "  \"whole_file_matches\": " + std::to_string(report.wholeFileMatches) + ",\n";
    json += "  \"game_bytes_in_matching_windows\": " + std::to_string(report.chunkMatches) + ",\n";
    json += "  \"game_content_findings\": " + JsonList(report.gameContentFindings) + ",\n";
    json += "  \"against\": \"" + JsonEscape(args.against) + "\",\n  \"equivalent\": " +
            std::string(report.againstChecked ? (report.equivalent ? "true" : "false") : "null") + ",\n";
    json += "  \"records_compared\": " + std::to_string(report.recordsCompared) + ",\n  \"payloads_compared\": " +
            std::to_string(report.payloadsCompared) + ",\n  \"records_differing_only_in_paths\": " +
            std::to_string(report.recordsDifferingOnlyInPaths) + ",\n";
    json += "  \"equivalence_problems\": " + JsonList(report.equivalenceProblems) + ",\n";
    json += "  \"lines\": " + JsonList(report.lines) + "\n}\n";
    WriteText(args.json, json);
    return ok ? 0 : 1;
  }

  int Extract(const Args& args)
  {
    if (args.positional.size() != 1 || args.out.empty()) {
      return Usage("extract needs <trace> and --out <dir>");
    }
    VfsIndex index;
    if (!MountData(args, &index)) {
      return 1;
    }
    std::vector<std::string> written;
    std::string error;
    const bool ok = galtrace::refs::Extract(args.positional[0], index, args.out, &written, &error);
    for (const std::string& path : written) {
      std::printf("wrote %s\n", path.c_str());
    }
    std::printf("EXTRACT: %s (%zu files)\n", ok ? "OK" : ("FAILED: " + error).c_str(), written.size());
    return ok ? 0 : 1;
  }

  int SetMeta(const Args& args)
  {
    if (args.positional.size() != 2) {
      return Usage("setmeta needs <in> <out>");
    }
    std::string error;
    const bool ok = galtrace::refs::SetMetadata(args.positional[0], args.positional[1], args.meta, args.remove, &error);
    std::printf("SETMETA: %s\n", ok ? "OK" : ("FAILED: " + error).c_str());
    return ok ? 0 : 1;
  }

  int Refs(const Args& args)
  {
    if (args.positional.size() != 1) {
      return Usage("refs needs <trace>");
    }
    std::vector<galtrace::PayloadRefInfo> refs;
    std::string error;
    if (!galtrace::ScanPayloadRefs(args.positional[0], &refs, &error)) {
      std::printf("REFS: FAILED: %s\n", error.c_str());
      return 1;
    }
    for (const galtrace::PayloadRefInfo& ref : refs) {
      std::printf("#%u %s (%s) %u bytes %s/%s\n", ref.payload, ref.path.c_str(), ref.archive.c_str(), ref.key.size,
                  galtrace::Hex64(ref.key.a).c_str(), galtrace::Hex64(ref.key.b).c_str());
    }
    std::string archives;
    for (const std::string& archive : galtrace::RefArchives(refs)) {
      archives += (archives.empty() ? "" : ", ") + archive;
    }
    std::printf("REFS: %zu references from %s\n", refs.size(), archives.c_str());
    return 0;
  }
} // namespace

int main(int argc, char** argv)
{
  Args args;
  if (argc < 2) {
    return Usage(nullptr);
  }
  args.command = argv[1];
  for (int index = 2; index < argc; ++index) {
    const std::string arg = argv[index];
    auto value = [&](std::string* const out) {
      if (index + 1 >= argc) {
        return false;
      }
      *out = argv[++index];
      return true;
    };
    std::string text;
    if (arg == "--init") {
      if (!value(&args.init)) return Usage("--init needs a path");
    } else if (arg == "--localappdata") {
      if (!value(&args.localAppData)) return Usage("--localappdata needs a directory");
    } else if (arg == "--documents") {
      if (!value(&args.documents)) return Usage("--documents needs a directory");
    } else if (arg == "--against") {
      if (!value(&args.against)) return Usage("--against needs a trace");
    } else if (arg == "--out") {
      if (!value(&args.out)) return Usage("--out needs a directory");
    } else if (arg == "--json") {
      if (!value(&args.json)) return Usage("--json needs a file");
    } else if (arg == "--meta") {
      if (!value(&text) || text.find('=') == std::string::npos) return Usage("--meta needs key=value");
      args.meta.emplace_back(text.substr(0, text.find('=')), text.substr(text.find('=') + 1));
    } else if (arg == "--remove") {
      if (!value(&text)) return Usage("--remove needs a key");
      args.remove.push_back(text);
    } else if (arg == "--keep-readbacks") {
      args.keepReadbacks = true;
    } else if (arg == "--allow-embedded-game-files") {
      args.allowEmbedded = true;
    } else if (!arg.empty() && arg[0] == '-') {
      return Usage(("unknown option " + arg).c_str());
    } else {
      args.positional.push_back(arg);
    }
  }
  if (args.command == "convert") return Convert(args);
  if (args.command == "check") return Check(args);
  if (args.command == "extract") return Extract(args);
  if (args.command == "setmeta") return SetMeta(args);
  if (args.command == "refs") return Refs(args);
  return Usage(("unknown command " + args.command).c_str());
}
