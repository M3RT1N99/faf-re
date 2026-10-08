#pragma once

// galtrace-refs' core: turns a recording (version 1, every payload embedded) into a version 2 trace that
// holds no game data (GalTraceFormat.h), and checks such a trace. Portable C++17 over the format library;
// the game data comes in through GameFileIndex (galtrace_refs.cpp implements it over port/native's VFS).
//
// What each recorded payload becomes, by how the calls use it:
//   game file     the file image of DevCreateTexture, an effect source of DevCreateEffect, the input of
//                 DevGetTexture2D: a PayloadRef to the VFS file with the same bytes (the record's own
//                 path is tried first, then every file of the same size); the record's path string
//                 (a disk path of the recording machine) becomes that VFS path;
//   backend output  GetTexture2D's output: a PayloadDigest (the replay's backend computes it again);
//   readback      what a read-only lock read (the harness's frames), SaveTexture/SaveToBuffer output:
//                 a PayloadDigest (compared only);
//   texture write what the engine wrote into a locked texture: a PayloadCompose of the GetTexture2D
//                 outputs found in it (block rows at their place) plus literal runs for the rest (the
//                 glyphs the engine rasterised), when at least one output is found; else embedded;
//   the rest      (vertex and index data the engine generated) stays embedded.
// The converter checks that every reference resolves to the same bytes and that every composition
// builds the recorded bytes before it writes them.

#include "../format/GalTraceIO.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace galtrace::refs
{
  /** The game data, as the engine's VFS sees it. */
  class GameFileIndex
  {
  public:
    virtual ~GameFileIndex() = default;
    /**
     * VFS paths of the files of exactly `size` bytes that win their lookup (first mount): archive entries
     * first (mount order), then files of directory mounts. The converter takes the first with equal bytes.
     */
    virtual std::vector<std::string> CandidatesBySize(std::uint64_t size) = 0;
    /** The file at `vfsPath` (first mount wins). */
    virtual bool Read(const std::string& vfsPath, std::vector<std::uint8_t>* out, std::string* error) = 0;
    /** The file name of the archive (or the directory mount) `vfsPath` comes from, lower-case. */
    virtual std::string ArchiveOf(const std::string& vfsPath) = 0;
  };

  struct ConvertOptions
  {
    Metadata setMetadata;            // added or replaced in the output's header
    bool sanitizeCommandLine = true; // "command_line": paths of the recording machine replaced by "<path>"
    bool digestReadbacks = true;     // readbacks as hashes only
    bool composeTextureWrites = true;
    bool allowEmbeddedGameFiles = false; // a game-file payload that resolves to no VFS file: embed it (else fail)
    std::function<void(const std::string&)> log; // progress lines (phases, compositions); may be empty
  };

  struct PayloadCount
  {
    std::uint32_t count = 0;
    std::uint64_t bytes = 0;
  };

  struct ConvertReport
  {
    bool ok = false;
    std::string error;
    std::uint64_t records = 0;
    std::uint32_t presents = 0;
    std::uint32_t readbacks = 0;
    std::map<std::string, PayloadCount> byUse;    // "vertex/index data", "texture write", ... -> embedded/defined
    PayloadCount embedded;
    PayloadCount references;
    PayloadCount digests;
    PayloadCount compositions;      // bytes: the payloads they build
    std::uint64_t compositionLiteralBytes = 0;
    std::uint32_t compositionCopies = 0;
    std::vector<std::string> referenceLines;      // "#22 /textures/ui/...dds (textures.scd) 100192 bytes, by its record's path"
    std::vector<std::string> compositionLines;    // per composition: placements and literal bytes
    std::vector<std::string> unresolved;          // game-file payloads no VFS file matched
    std::vector<std::string> notes;
    std::uint64_t inputBytes = 0;
    std::uint64_t outputBytes = 0;
  };

  /** Reads `inPath` (version 1 or 2), writes `outPath` (version 2). */
  bool Convert(const std::string& inPath, const std::string& outPath, GameFileIndex& index, const ConvertOptions& options, ConvertReport* report);

  struct CheckOptions
  {
    std::string againstPath;  // the recording it was made from: payload-by-payload and record-by-record equivalence
    bool scanGameContent = true; // embedded bytes against the game files (whole files, and 64-byte chunks of
                                 // the referenced files and of GetTexture2D outputs)
  };

  struct CheckReport
  {
    bool ok = false;
    std::string error;
    bool valid = false;              // the Validator, with every reference resolved
    std::string validatorError;
    std::uint32_t references = 0;
    std::uint32_t referencesResolved = 0;
    std::vector<std::string> referenceProblems;
    // Game content in embedded bytes.
    std::uint64_t embeddedPayloads = 0;
    std::uint64_t embeddedBytes = 0;   // Blob payloads plus composition literals
    std::uint64_t literalBytes = 0;
    std::uint64_t wholeFileMatches = 0; // embedded payloads equal to a game file
    std::uint64_t chunkMatches = 0;     // 64-byte chunks of game-derived bytes found in embedded bytes
    std::uint64_t gameChunksIndexed = 0;
    std::vector<std::string> gameContentFindings;
    // Equivalence with the recording (CheckOptions::againstPath).
    bool againstChecked = false;
    bool equivalent = false;
    std::uint64_t recordsCompared = 0;
    std::uint64_t recordsDifferingOnlyInPaths = 0; // record strings replaced by VFS paths
    std::uint64_t payloadsCompared = 0;
    std::vector<std::string> equivalenceProblems;
    std::vector<std::string> lines; // a readable summary
  };

  bool Check(const std::string& tracePath, GameFileIndex& index, const CheckOptions& options, CheckReport* report);

  /** Copies `inPath` to `outPath` with metadata keys added or replaced (`remove` keys dropped). */
  bool SetMetadata(
    const std::string& inPath, const std::string& outPath, const Metadata& set, const std::vector<std::string>& remove, std::string* error
  );

  /** Writes every referenced file of the trace to <outDir>/<vfs path, lower-case> (DirectoryResolver's tree). */
  bool Extract(const std::string& tracePath, GameFileIndex& index, const std::string& outDir, std::vector<std::string>* written, std::string* error);

  /** "c:\program files (x86)\...\textures.scd\textures\ui\x.dds" -> "/textures/ui/x.dds" ("" when no archive is in the path). */
  std::string VfsPathFromDiskHint(const std::string& diskPath);
} // namespace galtrace::refs
