#pragma once

// Where a version 2 trace's game-file payloads come from (GalTraceFormat.h, PayloadRef). A trace that
// ships without the game's data names each game file by its VFS path and content hashes; the reader
// asks a PayloadResolver for the bytes and checks size and hashes itself.
//
//   PayloadResolver    the interface the reader (and so galplay) calls;
//   DirectoryResolver  a directory tree that mirrors the VFS (<root>/effects/ui.fx): the Windows
//                      galplay and the tests; `galtrace-refs extract` writes the tree from the data;
//   CallbackResolver   a std::function, for a caller with its own file access;
//   VfsResolver        GalTraceVfsResolver.h: port/native's VirtualFileSystem, mounted by
//                      init_faf.lua as the engine mounts it (Android, and galtrace-refs on the PC).
//
//   ScanPayloadRefs    every reference of a trace, without replaying it (the app's data check);
//   VerifyPayloadRefs  reads each through a resolver and lists what is missing or different, in words a
//                      tester can act on (DescribeRefProblems).
//
// Portable C++17, no engine types; part of the format library.

#include "GalTraceFormat.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace galtrace
{
  class PayloadResolver
  {
  public:
    virtual ~PayloadResolver() = default;

    /**
     * The bytes of the game file at `vfsPath`: absolute, '/' separators, compared case-insensitively,
     * the first mount that holds it wins (the engine's VFS rules). False, with *error set to a sentence
     * that names the path, when the file is missing or unreadable. The reader checks size and hashes.
     * May be called from the replay thread only (one call at a time per resolver).
     */
    virtual bool ReadGameFile(const std::string& vfsPath, std::vector<std::uint8_t>* out, std::string* error) = 0;
  };

  /** <root>/<vfs path, lower-cased>: the tree `galtrace-refs extract` writes. */
  class DirectoryResolver final : public PayloadResolver
  {
  public:
    explicit DirectoryResolver(std::string root);
    bool ReadGameFile(const std::string& vfsPath, std::vector<std::uint8_t>* out, std::string* error) override;
    /** The file a VFS path maps to under `root` ("/Effects/UI.fx" -> "<root>/effects/ui.fx"). Empty for a path with "..". */
    [[nodiscard]] static std::string MapPath(const std::string& root, const std::string& vfsPath);

  private:
    std::string root_;
  };

  class CallbackResolver final : public PayloadResolver
  {
  public:
    using Function = std::function<bool(const std::string& vfsPath, std::vector<std::uint8_t>* out, std::string* error)>;
    explicit CallbackResolver(Function function) : function_(std::move(function)) {}
    bool ReadGameFile(const std::string& vfsPath, std::vector<std::uint8_t>* out, std::string* error) override
    {
      return function_ ? function_(vfsPath, out, error) : false;
    }

  private:
    Function function_;
  };

  /** One PayloadRef of a trace. */
  struct PayloadRefInfo
  {
    std::uint32_t payload = 0;
    std::string path;    // VFS path
    std::string archive; // where the recording machine found it ("textures.scd"), for messages
    BlobKey key{};       // hashes and size of the bytes
  };

  /**
   * Every game file the trace's PayloadRef records name, in file order, without replaying it (reads the
   * record headers, skips embedded bytes): one entry per file, so a payload made of several files (an
   * effect source: d3d9states.compat and the .fx) gives one entry per part, each with its own size and
   * hashes. A file (path and content) used by several payloads is listed once, at its first use. True
   * when the whole file was read; a version 1 trace has none.
   */
  bool ScanPayloadRefs(const std::string& tracePath, std::vector<PayloadRefInfo>* refs, std::string* error);

  struct PayloadRefProblem
  {
    PayloadRefInfo ref;
    bool missing = false;     // not found (or unreadable) in the game data
    std::uint64_t foundSize = 0; // when present but different
    std::string detail;       // the resolver's message, or "size X, expected Y" / "content differs"
  };

  /**
   * Reads every reference through `resolver` and checks its size and hashes. True when all are present
   * and equal; else `problems` lists the others (missing ones first is not implied: file order).
   */
  bool VerifyPayloadRefs(const std::vector<PayloadRefInfo>& refs, PayloadResolver& resolver, std::vector<PayloadRefProblem>* problems);

  /**
   * The problems as text for a tester: which archives are missing files and which files differ from
   * the ones the trace was recorded with, at most `maxLines` lines plus a count of the rest.
   */
  [[nodiscard]] std::string DescribeRefProblems(const std::vector<PayloadRefProblem>& problems, std::size_t maxLines = 12);

  /** The distinct archives the references name, in first-use order ("effects.nx2", "textures.scd"). */
  [[nodiscard]] std::vector<std::string> RefArchives(const std::vector<PayloadRefInfo>& refs);
} // namespace galtrace
