// Replay input for the Android headless runner executable (faf_headless_runner): reads a FAF
// .fafreplay as the vault, the browser download or a FAF client writes it, or a plain .scfareplay,
// and turns it into the .scfareplay the engine reads. Port code, linked into the executable only
// (scripts/port/build_runner.py, RUNNER_EXE_SOURCES); libfafengine.so and main.exe never see it.
//
// A .fafreplay is one JSON line, "\n", then the body (scripts/perf/convert_replay.py):
//  - legacy (FAF Java client recordings): base64( 4-byte big-endian decoded length + zlib stream );
//  - "compression":"zstd" (vault, Python and Rust clients): one zstd frame, often without a content
//    size, so it is decoded as a stream.
// The decoded body starts with "Supreme Commander v1.50.NNNN\0". The engine only loads 3764
// (SessionStartup.cpp VCR_SetupReplaySession), so the four digits are rewritten in place, exactly as
// `convert_replay.py --as-version 3764` does; nothing else moves. The output is byte-identical to that
// script's for every single-frame file it accepts (checked on 419 local recordings and the vault T1,
// see port/engine/runner/README.md). One intended difference: a zstd body of several concatenated
// frames is decoded completely here, while the script (python-zstandard's decompressobj) stops after
// the first frame. No real recording seen so far has more than one frame.
//
// zstd is the vendored decoder in port/third_party/zstd (1.5.7, BSD), zlib the system libz.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace faf_runner
{
  // What the body's header and message stream say: the same parse as the engine-side pre-scan
  // (HeadlessReplay.cpp ScanReplay), so `beats` and `has_end_game` agree with the run.
  struct ReplayScan
  {
    std::string error;    // empty: the header parsed
    std::string version;  // "Supreme Commander v1.50.3839" as recorded
    std::string mapPath;  // "/maps/SCMP_026/SCMP_026.scmap"
    int commandSources = 0;
    int armies = 0;
    uint32_t seed = 0;
    int beats = 0;
    int messages = 0;
    int verifyChecksums = 0;  // VerifyChecksum messages
    int checksumBeats = 0;    // distinct beats with a recorded checksum
    bool hasEndGame = false;
  };

  struct ReplayInput
  {
    std::string path;
    uint64_t fileSize = 0;
    std::string sha256;  // of the file as read (the key of the reference table)

    // "scfareplay" (already decoded), "fafreplay-legacy", "fafreplay-zstd"; empty when unreadable.
    std::string format;

    // The JSON line of a .fafreplay (absent fields stay unset).
    bool hasJson = false;
    std::string jsonError;
    bool hasId = false;
    long long id = 0;              // "uid" (a number out of range, or over 18 digits, is ignored)
    std::string mapName;           // "mapname" (the map folder, e.g. "scmp_026"); /replayinfo's "map"
                                   // falls back to the body's map folder when it is empty
    std::string featuredMod;       // "featured_mod"
    bool hasPlayers = false;
    int players = 0;               // "num_players" (0..1000000, else ignored)
    bool hasGameTime = false;
    double gameTimeSeconds = 0.0;  // "game_end" - "launched_at" (wall clock of the game)
    bool hasComplete = false;
    bool complete = false;         // "complete"
    std::string compression;       // "compression"

    // The decoded body (the .scfareplay bytes), with the version rewritten when asked.
    std::vector<uint8_t> data;
    std::string decodedSha256;    // of the body as decoded, before any rewrite
    std::string recordedVersion;  // "Supreme Commander v1.50.3831"
    bool versionRewritten = false;
    std::string convertedSha256;  // of `data` after the rewrite (what /convertreplay writes)
    size_t trailingBytes = 0;     // ignored input after the zlib stream / the last zstd frame
    bool truncated = false;       // a zstd frame ended early (decoded as far as it goes, as the script does)
    int zstdFrames = 0;

    ReplayScan scan;

    std::string error;  // non-empty: the file could not be decoded (or the version not rewritten)
  };

  // Reads and decodes `path`. With `asVersion` (four digits, e.g. "3764") the body's version is
  // rewritten like convert_replay.py --as-version; a body whose first string is not
  // "Supreme Commander v1.50." plus four characters is then an error. Returns !error.empty() == false.
  bool ReadReplay(const char* path, const char* asVersion, ReplayInput& out);

  // True when the file is a decoded replay the engine loads as it is: it starts with
  // "Supreme Commander v1.50.3764\0". Reads 29 bytes with open/read and allocates nothing (safe
  // before the arena hands the engine its first block). Anything else (a .fafreplay, or a
  // .scfareplay of another version) is for ReadReplay.
  bool IsEngineReadyReplay(const char* path);

  // One JSON object (no newline) describing `in`, for /replayinfo and /convertreplay. No player
  // names, titles or chat: ids, map, versions, counts, hashes.
  std::string ReplayInfoJson(const ReplayInput& in, const char* outputPath);

  // Writes `data` to `path` through `path`.tmp and rename (so a reader never sees half a file).
  bool WriteFileAtomic(const std::string& path, const std::vector<uint8_t>& data, std::string& error);

  std::string Sha256Hex(const uint8_t* data, size_t size);
  std::string JsonString(const std::string& text);
} // namespace faf_runner
