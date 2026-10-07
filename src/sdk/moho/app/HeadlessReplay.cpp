#include "moho/app/HeadlessReplay.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <typeinfo>
#include <vector>
#if !defined(_MSC_VER)
#include <cxxabi.h>
#endif

#include "gpg/core/algorithms/MD5.h"
#include "gpg/core/reflection/Reflection.h"
#include "gpg/core/utils/Global.h"
#include "gpg/core/utils/Logging.h"
#include "lua/LuaObject.h"
#include "moho/console/CConCommand.h"
#include "moho/lua/CScrLuaInitForm.h"
#include "moho/math/MathReflection.h"
#include "moho/misc/LaunchInfoBase.h"
#include "moho/misc/SessionStartup.h"
#include "moho/misc/StartupHelpers.h"
#include "moho/net/CClientManagerImpl.h"
#include "moho/net/IClientMgrUIInterface.h"
#include "moho/resource/ResourceManager.h"
#include "moho/serialization/PrefetchHandleBase.h"
#include "moho/sim/CWldMap.h"
#include "moho/sim/CWldSessionLoaderImpl.h"
#include "moho/sim/ISTIDriver.h"
#include "moho/sim/RRuleGameRules.h"
#include "moho/sim/SDesyncInfo.h"
#include "moho/sim/STIMap.h"
#include "moho/sim/Sim.h"
#include "moho/sim/SimDriver.h"
#include "moho/sim/WldSessionInfo.h"
#include "platform/X87Precision.h"

// On Android this is port/engine/shim's <windows.h>: the sync loop's event wait (faf_win_kernel.h).
#include <windows.h>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

// Port addition (no binary counterpart): the M3a headless replay runner. See HeadlessReplay.h for
// the command line and the exit codes.
//
// The GUI plays a replay through CScApp: `/replay` -> VCR_SetupReplaySession -> WLD_BeginSession,
// then the WLD_Frame machine (CWldSession.cpp) walks Preload -> Loading -> Initialize ->
// PostInitialize -> Playing, one step per app frame. This runner repeats, in the same order, only
// the steps of that machine that feed the sim, and replaces the user side (CWldSession, the game
// UI, the renderer and sound) with a loop that drains the driver's sync packets. Every step the GUI
// takes and this runner deliberately does not is listed where it would have happened, with the
// reason it cannot change what the sim computes.

namespace moho
{
  namespace
  {
    using Clock = std::chrono::steady_clock;

    // ---------------------------------------------------------------------------------------------
    // Options

    struct HeadlessOptions
    {
      std::string mReplayPath;
      std::string mSummaryPath;
      std::string mRegistryPath;
      int mProgressEvery = 500;
      int mTimeoutSeconds = 120;
      int mGameSpeed = 50;
      bool mInterlocked = false;
    };

    [[nodiscard]] bool ReadArg(const char* const option, std::string& out)
    {
      msvc8::vector<msvc8::string> args;
      if (!CFG_GetArgOption(option, 1u, &args) || args.empty()) {
        return false;
      }
      out = args[0].c_str();
      return true;
    }

    [[nodiscard]] bool ReadIntArg(const char* const option, int& out)
    {
      std::string text;
      if (!ReadArg(option, text)) {
        return false;
      }
      out = std::atoi(text.c_str());
      return true;
    }

    // ---------------------------------------------------------------------------------------------
    // Replay pre-scan
    //
    // The runner reads the replay once on its own, before the engine does, for two things the engine
    // does not hand out: the number of beats the file advances (the end of the replay) and every
    // `VerifyChecksum` the players' clients recorded, by beat and command source. The engine still
    // compares those checksums itself (`Sim::VerifyChecksum` via the decoder) and reports a mismatch
    // in `SSyncData::mDesyncs`; the scan lets the runner count the checks and keep the digests.
    //
    // Layout: the header that VCR_CreateReplay writes and VCR_SetupReplaySession reads, then
    // `CMessage`s - u8 type, u16 size including these 3 bytes, payload.

    constexpr std::uint8_t kOpAdvance = 0;          // ECmdStreamOp::CMDST_Advance: i32 beats
    constexpr std::uint8_t kOpSetCommandSource = 1; // u8 source
    constexpr std::uint8_t kOpVerifyChecksum = 3;   // 16-byte MD5, i32 beat
    constexpr std::uint8_t kOpEndGame = 23;

    /// Beats the runner waits past the end of the stream for the sim's game-over flag.
    constexpr int kEndGraceBeats = 10;

    struct RecordedChecksum
    {
      int mSource = -1;
      std::uint8_t mDigest[16]{};
    };

    struct ReplayScan
    {
      std::string mError;
      std::string mVersion;
      std::string mMap;
      std::vector<std::string> mSourceNames;
      int mArmies = 0;
      std::uint32_t mSeed = 0;
      int mTotalBeats = 0;
      int mMessages = 0;
      int mVerifyMessages = 0;
      bool mHasEndGame = false;
      std::map<int, std::vector<RecordedChecksum>> mChecksums;
    };

    class ByteReader
    {
    public:
      explicit ByteReader(const std::vector<std::uint8_t>& bytes)
        : mBytes(bytes)
      {}

      [[nodiscard]] bool CString(std::string& out)
      {
        const std::size_t start = mPos;
        while (mPos < mBytes.size() && mBytes[mPos] != 0) {
          ++mPos;
        }
        if (mPos >= mBytes.size()) {
          return false;
        }
        out.assign(reinterpret_cast<const char*>(mBytes.data() + start), mPos - start);
        ++mPos;
        return true;
      }

      [[nodiscard]] bool Raw(const std::size_t count, const std::uint8_t*& out)
      {
        if (mBytes.size() - mPos < count) {
          return false;
        }
        out = mBytes.data() + mPos;
        mPos += count;
        return true;
      }

      [[nodiscard]] bool U8(std::uint8_t& out)
      {
        const std::uint8_t* raw = nullptr;
        if (!Raw(1, raw)) {
          return false;
        }
        out = raw[0];
        return true;
      }

      [[nodiscard]] bool U32(std::uint32_t& out)
      {
        const std::uint8_t* raw = nullptr;
        if (!Raw(4, raw)) {
          return false;
        }
        out = static_cast<std::uint32_t>(raw[0]) | (static_cast<std::uint32_t>(raw[1]) << 8) |
              (static_cast<std::uint32_t>(raw[2]) << 16) | (static_cast<std::uint32_t>(raw[3]) << 24);
        return true;
      }

      [[nodiscard]] std::size_t Pos() const { return mPos; }
      [[nodiscard]] std::size_t Size() const { return mBytes.size(); }
      [[nodiscard]] const std::uint8_t* At(const std::size_t pos) const { return mBytes.data() + pos; }
      void Skip(const std::size_t count) { mPos += count; }

    private:
      const std::vector<std::uint8_t>& mBytes;
      std::size_t mPos = 0;
    };

    [[nodiscard]] std::int32_t ReadLe32(const std::uint8_t* const p)
    {
      return static_cast<std::int32_t>(
        static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
        (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24)
      );
    }

    [[nodiscard]] ReplayScan ScanReplay(const std::string& path)
    {
      ReplayScan scan;
      std::FILE* const file = std::fopen(path.c_str(), "rb");
      if (file == nullptr) {
        scan.mError = "cannot open replay file";
        return scan;
      }
      std::vector<std::uint8_t> bytes;
      std::uint8_t chunk[65536];
      for (;;) {
        const std::size_t got = std::fread(chunk, 1, sizeof(chunk), file);
        if (got == 0) {
          break;
        }
        bytes.insert(bytes.end(), chunk, chunk + got);
      }
      std::fclose(file);

      ByteReader reader(bytes);
      std::string ignored;
      const std::uint8_t* raw = nullptr;
      std::uint32_t length = 0;
      std::uint8_t count = 0;
      if (!reader.CString(scan.mVersion) || !reader.CString(ignored) || !reader.Raw(13, raw) ||
          std::memcmp(raw, "Replay v1.9\r\n", 13) != 0 || !reader.CString(scan.mMap) || !reader.CString(ignored) ||
          !reader.U32(length) || !reader.Raw(length, raw) || !reader.U32(length) || !reader.Raw(length, raw) ||
          !reader.U8(count)) {
        scan.mError = "truncated or unrecognised replay header";
        return scan;
      }
      for (std::uint8_t i = 0; i < count; ++i) {
        std::string name;
        std::uint32_t timeouts = 0;
        if (!reader.CString(name) || !reader.U32(timeouts)) {
          scan.mError = "truncated command-source list";
          return scan;
        }
        scan.mSourceNames.push_back(name);
      }
      std::uint8_t cheats = 0;
      std::uint8_t armies = 0;
      if (!reader.U8(cheats) || !reader.U8(armies)) {
        scan.mError = "truncated army list";
        return scan;
      }
      scan.mArmies = armies;
      for (std::uint8_t i = 0; i < armies; ++i) {
        if (!reader.U32(length) || !reader.Raw(length, raw)) {
          scan.mError = "truncated army list";
          return scan;
        }
        std::uint8_t source = 0;
        do {
          if (!reader.U8(source)) {
            scan.mError = "truncated army list";
            return scan;
          }
        } while (source != 0xFFu);
      }
      if (!reader.U32(scan.mSeed)) {
        scan.mError = "truncated header";
        return scan;
      }

      int currentSource = -1;
      int beatsSoFar = 0;
      while (reader.Size() - reader.Pos() >= 3) {
        const std::uint8_t* const head = reader.At(reader.Pos());
        const std::uint8_t type = head[0];
        const std::size_t size = static_cast<std::size_t>(head[1]) | (static_cast<std::size_t>(head[2]) << 8);
        if (size < 3 || reader.Size() - reader.Pos() < size) {
          break; // a truncated tail; the engine stops reading there too
        }
        const std::uint8_t* const payload = head + 3;
        const std::size_t payloadSize = size - 3;
        ++scan.mMessages;
        if (type == kOpAdvance && payloadSize >= 4) {
          beatsSoFar += ReadLe32(payload);
        } else if (type == kOpSetCommandSource && payloadSize >= 1) {
          currentSource = payload[0];
        } else if (type == kOpVerifyChecksum && payloadSize >= 20) {
          RecordedChecksum recorded;
          recorded.mSource = currentSource;
          std::memcpy(recorded.mDigest, payload, 16);
          scan.mChecksums[ReadLe32(payload + 16)].push_back(recorded);
          ++scan.mVerifyMessages;
        } else if (type == kOpEndGame) {
          scan.mHasEndGame = true;
        }
        reader.Skip(size);
      }
      scan.mTotalBeats = beatsSoFar;
      return scan;
    }

    [[nodiscard]] std::string DigestHex(const std::uint8_t* const bytes)
    {
      static const char kHex[] = "0123456789abcdef";
      std::string text(32, '0');
      for (int i = 0; i < 16; ++i) {
        text[static_cast<std::size_t>(i) * 2] = kHex[bytes[i] >> 4];
        text[static_cast<std::size_t>(i) * 2 + 1] = kHex[bytes[i] & 0xF];
      }
      return text;
    }

    [[nodiscard]] std::string JsonEscape(const std::string& text)
    {
      std::string out;
      out.reserve(text.size() + 8);
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
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned char>(c));
            out += escaped;
          } else {
            out += c;
          }
          break;
        }
      }
      return out;
    }

    // ---------------------------------------------------------------------------------------------
    // Run state, shared with the log target and the die handler

    struct CheckpointResult
    {
      int mBeat = 0;
      bool mHaveSim = false;
      std::uint8_t mSim[16]{};
      std::vector<RecordedChecksum> mRecorded;
    };

    struct DesyncReport
    {
      int mBeat = 0;
      int mArmy = 0;
      int mSyncBeat = 0;
    };

    enum class EPhase : int
    {
      Startup = 0,
      Load = 1,
      Sim = 2,
      Teardown = 3,
    };

    /**
     * What the engine's static initialisers registered, counted once the host services are up.
     * Every entry of these registries comes from a static initialiser in some linked TU, so the
     * Android runner (M3b), which links a computed subset of the engine
     * (port/engine/runner/closure.txt), is compared against these numbers: a TU missing from an ELF
     * link drops its entries without any other symptom. The hashes are FNV-1a over the sorted
     * names; `/headlessregistry <file>` writes the names themselves, so two runs can be diffed
     * entry by entry.
     *
     * M3c added the registries the first version could not see (link_closure.py's REG_KINDS):
     * prefetch kinds, resource factories, and the serializer/construct helpers. A helper binds its
     * callbacks onto its RType only when the first archive is created
     * (gpg::SerHelperBase::InitNewHelpers); a replay creates none, so at this point the helpers are
     * still queued and are counted by class, and the RType counts cover only callbacks a TypeInfo
     * set itself. Reading them changes nothing.
     */
    struct RegistrySnapshot
    {
      bool mTaken = false;
      int mRTypes = 0;              // gpg::GetRTypeMap() after REF_RegisterAllTypes
      int mRTypesPreregistered = 0; // gpg::GetRTypePreregisteredMap()
      int mConsoleCommands = 0;     // CON_GetCommandList: console commands and variables
      std::map<std::string, std::pair<int, int>> mLuaSets; // init-set name -> {sets, binders}
      std::uint64_t mRTypeHash = 0;
      std::uint64_t mLuaHash = 0;
      std::uint64_t mConsoleHash = 0;
      std::vector<std::pair<std::string, std::string>> mPrefetchKinds; // kind -> RType name ("" = none)
      int mResourceFactories = 0;    // RTypes with an active factory in the resource manager
      int mSerializerHelpers = 0;    // gpg::SerHelperBase::sNewHelpers, still queued
      int mRTypesWithSerializer = 0; // serLoadFunc_ or serSaveFunc_ already set
      int mRTypesWithConstruct = 0;  // serConstructFunc_ or serSaveConstructArgsFunc_ already set
      std::uint64_t mFactoryHash = 0;
      std::uint64_t mSerializerHash = 0;
      std::uint64_t mCallbackHash = 0;
    };

    struct RunState
    {
      std::mutex mLock; // guards the message lists and the log file
      HeadlessOptions mOptions;
      ReplayScan mScan;
      std::FILE* mLogFile = nullptr;

      std::atomic<int> mPhase{static_cast<int>(EPhase::Startup)};
      std::atomic<bool> mSimFailed{false};
      std::atomic<int> mWarnings{0};
      std::atomic<int> mLuaErrorsLoad{0};
      std::atomic<int> mLuaErrorsSim{0};
      std::atomic<int> mAssertions{0};
      std::atomic<int> mEngineMismatchLines{0};
      std::vector<std::string> mFirstLuaErrors;
      std::vector<std::string> mFirstAssertions;
      std::string mSimFailure;

      Clock::time_point mStart;
      double mLoadSeconds = 0.0;
      double mSimSeconds = 0.0;
      std::string mEndReason = "not started";
      int mExitCode = 1;

      int mSyncPackets = 0;
      int mFirstBeat = -1;
      int mLastBeat = -1;
      bool mGameOver = false;
      int mGameOverBeat = -1;
      int mChecks = 0;
      int mMismatches = 0;
      int mFirstMismatchBeat = -1;
      int mCheckpointsMissing = 0;
      std::vector<CheckpointResult> mCheckpoints;
      int mEngineDesyncs = 0;
      int mFirstEngineDesyncBeat = -1;
      std::vector<DesyncReport> mDesyncs;
      std::uint64_t mCheckpointChain = 1469598103934665603ull; // FNV-1a over the sim digests
      bool mTeardownDone = false;
      bool mBeforeTeardown = false; // the interim summary written before the teardown

      // What the sim was given besides the replay itself, so runs on different hosts (x86 GUI,
      // x86 runner, arm64 runner) can be checked for identical inputs before their digests are.
      std::string mLanguage;
      std::string mPrefsFile;
      std::string mSimWorkers = "default";
      bool mSse2 = false;

      RegistrySnapshot mRegistry;
    };

    RunState* sRun = nullptr;

    void Out(const char* const format, ...)
    {
      va_list args;
      va_start(args, format);
      std::vfprintf(stdout, format, args);
      va_end(args);
      std::fflush(stdout);
    }

    [[nodiscard]] double SecondsSince(const Clock::time_point start)
    {
      return std::chrono::duration<double>(Clock::now() - start).count();
    }

    [[nodiscard]] bool StartsWith(const char* const text, const char* const prefix)
    {
      return std::strncmp(text, prefix, std::strlen(prefix)) == 0;
    }

    [[nodiscard]] bool IsLuaErrorMessage(const char* const text)
    {
      // The engine's own Lua error reports (CLuaTask, CScriptObject, CScrLuaObjectFactory,
      // SCR_Traceback) all start with "Error " or carry a traceback.
      return StartsWith(text, "Error ") || std::strstr(text, "stack traceback:") != nullptr ||
             std::strstr(text, "attempt to ") != nullptr;
    }

    void RememberMessage(std::vector<std::string>& list, const char* const text)
    {
      constexpr std::size_t kMaxKept = 20;
      constexpr std::size_t kMaxLength = 400;
      if (list.size() >= kMaxKept) {
        return;
      }
      std::string trimmed(text);
      if (trimmed.size() > kMaxLength) {
        trimmed.resize(kMaxLength);
        trimmed += "...";
      }
      list.push_back(trimmed);
    }

    /**
     * Every engine log line goes to the `/log` file (when given) and is classified on the way:
     * Lua errors by phase, the engine's own checksum-mismatch warnings, and the two messages with
     * which `CSimDriver` reports that the sim thread gave up (`Sim::Create() crashed`,
     * `Sim crashed hard in DoSimBeat()`), which leave the driver Failed with no other signal.
     */
    class HeadlessLogTarget final : public gpg::LogTarget
    {
    public:
      HeadlessLogTarget()
        : gpg::LogTarget(true)
      {}

      void OnMessage(
        const gpg::LogSeverity level,
        const msvc8::string& message,
        const msvc8::vector<msvc8::string>& context,
        int previousDepth
      ) override
      {
        (void)context;
        (void)previousDepth;
        RunState* const run = sRun;
        if (run == nullptr) {
          return;
        }

        const char* const text = message.c_str();
        if (level == gpg::LogSeverity::Warn) {
          ++run->mWarnings;
          if (IsLuaErrorMessage(text)) {
            const bool simPhase = run->mPhase.load() >= static_cast<int>(EPhase::Sim);
            ++(simPhase ? run->mLuaErrorsSim : run->mLuaErrorsLoad);
            std::lock_guard<std::mutex> lock(run->mLock);
            RememberMessage(run->mFirstLuaErrors, (std::string(simPhase ? "[sim] " : "[load] ") + text).c_str());
          }
          if (std::strstr(text, "Checksum for beat ") != nullptr && std::strstr(text, " mismatched") != nullptr) {
            ++run->mEngineMismatchLines;
          }
        }
        if (StartsWith(text, "Sim::Create() crashed") || StartsWith(text, "Sim crashed hard")) {
          std::lock_guard<std::mutex> lock(run->mLock);
          run->mSimFailure = text;
          run->mSimFailed = true;
        }

        if (run->mLogFile != nullptr) {
          static const char* const kLevel[] = {"DEBUG", "INFO", "WARNING"};
          const int index = static_cast<int>(level);
          std::lock_guard<std::mutex> lock(run->mLock);
          std::fprintf(run->mLogFile, "%s: %s\n", (index >= 0 && index < 3) ? kLevel[index] : "?", text);
          if (level == gpg::LogSeverity::Warn) {
            std::fflush(run->mLogFile);
          }
        }
      }
    };

    // ---------------------------------------------------------------------------------------------
    // Summary

    void WriteSummary()
    {
      RunState* const run = sRun;
      if (run == nullptr) {
        return;
      }

      std::string json;
      char buffer[512];
      const auto add = [&json, &buffer](const char* const format, ...) {
        va_list args;
        va_start(args, format);
        std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        json += buffer;
      };
      // Strings go in unformatted: a path or an error text can be longer than `buffer`.
      const auto addString = [&json](const char* const key, const std::string& value) {
        json += "  \"";
        json += key;
        json += "\": \"";
        json += JsonEscape(value);
        json += "\",\n";
      };

      const double wall = SecondsSince(run->mStart);
      const int simBeats = (run->mFirstBeat >= 0) ? (run->mLastBeat - run->mFirstBeat) : 0;
      add("{\n");
      addString("replay", run->mOptions.mReplayPath);
      addString("header_version", run->mScan.mVersion);
      addString("map", run->mScan.mMap);
      add("  \"mode\": \"%s\",\n", run->mOptions.mInterlocked ? "interlocked" : "threaded");
      add("  \"game_speed_requested\": %d,\n", run->mOptions.mGameSpeed);
      json += "  \"inputs\": {\"language\": \"" + JsonEscape(run->mLanguage) + "\", \"prefs\": \"" +
              JsonEscape(run->mPrefsFile) + "\", \"sim_workers\": \"" + JsonEscape(run->mSimWorkers) + "\", ";
      add(
        "\"sse2\": %s, \"seed\": %u, \"armies\": %d, \"command_sources\": %d},\n", run->mSse2 ? "true" : "false",
        run->mScan.mSeed, run->mScan.mArmies, static_cast<int>(run->mScan.mSourceNames.size())
      );
      if (const RegistrySnapshot& registry = run->mRegistry; registry.mTaken) {
        add(
          "  \"registry\": {\"rtypes\": %d, \"rtypes_preregistered\": %d, \"console_commands\": %d, "
          "\"lua_sets\": {",
          registry.mRTypes, registry.mRTypesPreregistered, registry.mConsoleCommands
        );
        bool first = true;
        for (const auto& [setName, counts] : registry.mLuaSets) {
          json += first ? "\"" : ", \"";
          json += JsonEscape(setName);
          add("\": {\"sets\": %d, \"binders\": %d}", counts.first, counts.second);
          first = false;
        }
        json += "}, \"prefetch_kinds\": {";
        first = true;
        for (const auto& [kind, typeName] : registry.mPrefetchKinds) {
          json += first ? "\"" : ", \"";
          json += JsonEscape(kind);
          json += typeName.empty() ? "\": null" : "\": \"" + JsonEscape(typeName) + "\"";
          first = false;
        }
        add(
          "}, \"resource_factories\": %d, \"serializer_helpers_pending\": %d, \"rtypes_with_serializer\": %d, "
          "\"rtypes_with_construct\": %d",
          registry.mResourceFactories, registry.mSerializerHelpers, registry.mRTypesWithSerializer,
          registry.mRTypesWithConstruct
        );
        add(
          ", \"fnv1a\": {\"rtypes\": \"%016llx\", \"lua\": \"%016llx\", \"console\": \"%016llx\", ",
          static_cast<unsigned long long>(registry.mRTypeHash), static_cast<unsigned long long>(registry.mLuaHash),
          static_cast<unsigned long long>(registry.mConsoleHash)
        );
        add(
          "\"factories\": \"%016llx\", \"serializer_helpers\": \"%016llx\", \"rtype_callbacks\": \"%016llx\"}},\n",
          static_cast<unsigned long long>(registry.mFactoryHash),
          static_cast<unsigned long long>(registry.mSerializerHash),
          static_cast<unsigned long long>(registry.mCallbackHash)
        );
      } else {
        json += "  \"registry\": null,\n";
      }
      addString("end_reason", run->mEndReason);
      add("  \"exit_code\": %d,\n", run->mExitCode);
      add("  \"reached_end\": %s,\n", run->mExitCode == 0 ? "true" : "false");
      add("  \"beats_in_replay\": %d,\n", run->mScan.mTotalBeats);
      add("  \"last_beat\": %d,\n", run->mLastBeat);
      add("  \"sync_packets\": %d,\n", run->mSyncPackets);
      add("  \"replay_has_end_game\": %s,\n", run->mScan.mHasEndGame ? "true" : "false");
      add("  \"game_over\": %s,\n", run->mGameOver ? "true" : "false");
      add("  \"game_over_beat\": %d,\n", run->mGameOverBeat);
      add("  \"wall_seconds\": %.3f,\n", wall);
      add("  \"load_seconds\": %.3f,\n", run->mLoadSeconds);
      add("  \"sim_seconds\": %.3f,\n", run->mSimSeconds);
      add("  \"beats_per_second\": %.2f,\n", run->mSimSeconds > 0.0 ? simBeats / run->mSimSeconds : 0.0);
      add("  \"checkpoints_recorded\": %d,\n", static_cast<int>(run->mScan.mChecksums.size()));
      add("  \"checkpoints_reached\": %d,\n", static_cast<int>(run->mCheckpoints.size()));
      add("  \"checkpoints_without_sim_digest\": %d,\n", run->mCheckpointsMissing);
      add("  \"checksum_checks\": %d,\n", run->mChecks);
      add("  \"checksum_mismatches\": %d,\n", run->mMismatches);
      add("  \"first_mismatch_beat\": %d,\n", run->mFirstMismatchBeat);
      add("  \"engine_desync_reports\": %d,\n", run->mEngineDesyncs);
      add("  \"engine_first_desync_beat\": %d,\n", run->mFirstEngineDesyncBeat);
      add("  \"engine_mismatch_warnings\": %d,\n", run->mEngineMismatchLines.load());
      add("  \"checkpoint_chain_fnv1a\": \"%016llx\",\n", static_cast<unsigned long long>(run->mCheckpointChain));
      add("  \"warnings\": %d,\n", run->mWarnings.load());
      add("  \"lua_errors_load\": %d,\n", run->mLuaErrorsLoad.load());
      add("  \"lua_errors_sim\": %d,\n", run->mLuaErrorsSim.load());
      add("  \"lua_errors\": %d,\n", run->mLuaErrorsSim.load());
      add("  \"assertions\": %d,\n", run->mAssertions.load());
      add("  \"teardown_done\": %s,\n", run->mTeardownDone ? "true" : "false");
      {
        std::lock_guard<std::mutex> lock(run->mLock);
        addString("sim_failure", run->mSimFailure);
        json += "  \"first_lua_errors\": [";
        for (std::size_t i = 0; i < run->mFirstLuaErrors.size(); ++i) {
          json += (i == 0) ? "\n    \"" : ",\n    \"";
          json += JsonEscape(run->mFirstLuaErrors[i]);
          json += "\"";
        }
        json += run->mFirstLuaErrors.empty() ? "],\n" : "\n  ],\n";
        json += "  \"first_assertions\": [";
        for (std::size_t i = 0; i < run->mFirstAssertions.size(); ++i) {
          json += (i == 0) ? "\n    \"" : ",\n    \"";
          json += JsonEscape(run->mFirstAssertions[i]);
          json += "\"";
        }
        json += run->mFirstAssertions.empty() ? "],\n" : "\n  ],\n";
      }
      json += "  \"engine_desyncs\": [";
      for (std::size_t i = 0; i < run->mDesyncs.size(); ++i) {
        add(
          "%s{\"beat\": %d, \"army\": %d, \"reported_in_sync\": %d}", (i == 0) ? "\n    " : ",\n    ",
          run->mDesyncs[i].mBeat, run->mDesyncs[i].mArmy, run->mDesyncs[i].mSyncBeat
        );
      }
      json += run->mDesyncs.empty() ? "],\n" : "\n  ],\n";
      json += "  \"checkpoints\": [";
      for (std::size_t i = 0; i < run->mCheckpoints.size(); ++i) {
        const CheckpointResult& checkpoint = run->mCheckpoints[i];
        add(
          "%s{\"beat\": %d, \"sim\": \"%s\", \"recorded\": [", (i == 0) ? "\n    " : ",\n    ", checkpoint.mBeat,
          checkpoint.mHaveSim ? DigestHex(checkpoint.mSim).c_str() : ""
        );
        for (std::size_t r = 0; r < checkpoint.mRecorded.size(); ++r) {
          const RecordedChecksum& recorded = checkpoint.mRecorded[r];
          const bool match = checkpoint.mHaveSim && std::memcmp(recorded.mDigest, checkpoint.mSim, 16) == 0;
          add(
            "%s{\"source\": %d, \"md5\": \"%s\", \"match\": %s}", (r == 0) ? "" : ", ", recorded.mSource,
            DigestHex(recorded.mDigest).c_str(), match ? "true" : "false"
          );
        }
        json += "]}";
      }
      json += run->mCheckpoints.empty() ? "]\n" : "\n  ]\n";
      json += "}\n";

      if (!run->mOptions.mSummaryPath.empty()) {
        if (std::FILE* const file = std::fopen(run->mOptions.mSummaryPath.c_str(), "wb"); file != nullptr) {
          std::fwrite(json.data(), 1, json.size(), file);
          std::fclose(file);
        } else {
          Out("[headless] cannot write summary %s\n", run->mOptions.mSummaryPath.c_str());
        }
      }

      Out(
        "[headless] %s end=%s exit=%d beats=%d/%d wall=%.1fs sim=%.1fs checks=%d mismatches=%d "
        "first_mismatch=%d engine_desyncs=%d lua_errors_sim=%d lua_errors_load=%d chain=%016llx\n",
        run->mBeforeTeardown ? "RESULT (before teardown)" : "RESULT",
        run->mEndReason.c_str(), run->mExitCode, run->mLastBeat, run->mScan.mTotalBeats, wall, run->mSimSeconds,
        run->mChecks, run->mMismatches, run->mFirstMismatchBeat, run->mEngineDesyncs, run->mLuaErrorsSim.load(),
        run->mLuaErrorsLoad.load(), static_cast<unsigned long long>(run->mCheckpointChain)
      );
    }

    /**
     * `gpg::Die` and `gpg::HandleAssertFailure` both end here. WinMain's GUI handler opens a crash
     * dialog, which would hang an unattended run. An assertion only reports (the engine continues
     * after it), so it is counted; anything else is fatal - `gpg::Die` would break into the
     * debugger and spin after this returns - so the summary is written and the process ends.
     */
    void HeadlessDieHandler(const char* const message)
    {
      const char* const text = message != nullptr ? message : "";
      RunState* const run = sRun;
      if (StartsWith(text, "Failed assertion")) {
        if (run != nullptr) {
          ++run->mAssertions;
          std::lock_guard<std::mutex> lock(run->mLock);
          RememberMessage(run->mFirstAssertions, text);
        }
        Out("[headless] ASSERTION %s\n", text);
        return;
      }

      Out("[headless] FATAL %s\n", text);
      if (run != nullptr) {
        run->mEndReason = std::string("die: ") + text;
        run->mExitCode = 3;
        WriteSummary();
        if (run->mLogFile != nullptr) {
          std::fflush(run->mLogFile);
        }
      }
      std::fflush(stdout);
      std::_Exit(3);
    }

#if defined(_WIN32)
    /**
     * An access violation on the sim thread has no catch in the engine (the GUI installs its crash
     * reporter for that, PLAT_CatchStructuredExceptions). Without a filter Windows Error Reporting
     * would show a dialog and keep the process alive; this one records the fault in the summary and
     * lets the process end. WinMain's first-chance diagnostic handler has already logged the stack
     * to faf_diag.log beside the executable.
     */
    LONG WINAPI HeadlessUnhandledExceptionFilter(EXCEPTION_POINTERS* const info)
    {
      char text[160] = "unhandled exception";
      if (info != nullptr && info->ExceptionRecord != nullptr) {
        std::snprintf(
          text, sizeof(text), "unhandled exception 0x%08lX at %p", info->ExceptionRecord->ExceptionCode,
          info->ExceptionRecord->ExceptionAddress
        );
      }
      Out("[headless] CRASH %s\n", text);
      if (RunState* const run = sRun; run != nullptr) {
        run->mEndReason = std::string("crash: ") + text;
        run->mExitCode = 3;
        WriteSummary();
        if (run->mLogFile != nullptr) {
          std::fflush(run->mLogFile);
        }
      }
      return EXCEPTION_EXECUTE_HANDLER;
    }

    /**
     * Host glue: main.exe is a GUI-subsystem program, so stdout only exists when the caller
     * redirected it; otherwise borrow the parent console. And no modal dialogs - CRT asserts and
     * reports go to stderr, OS fault boxes are off.
     */
    void PrepareWindowsHost()
    {
      const HANDLE stdoutHandle = ::GetStdHandle(STD_OUTPUT_HANDLE);
      if ((stdoutHandle == nullptr || stdoutHandle == INVALID_HANDLE_VALUE) &&
          ::AttachConsole(ATTACH_PARENT_PROCESS) != FALSE) {
        std::FILE* reopened = nullptr;
        (void)::freopen_s(&reopened, "CONOUT$", "w", stdout);
        (void)::freopen_s(&reopened, "CONOUT$", "w", stderr);
      }

      (void)::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
      (void)::SetUnhandledExceptionFilter(&HeadlessUnhandledExceptionFilter);
#if defined(_MSC_VER)
      for (const int reportType : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        (void)_CrtSetReportMode(reportType, _CRTDBG_MODE_FILE);
        (void)_CrtSetReportFile(reportType, _CRTDBG_FILE_STDERR);
      }
      (void)_set_abort_behavior(0u, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    }
#endif

    /**
     * What `func_DoPreload` installs on the session's client manager is `CWldUiInterface`, which
     * posts disconnects, chat and speed changes to the game UI. With no UI the base interface's
     * no-op slots are the whole behaviour; none of them feeds the sim.
     */
    class HeadlessClientMgrUIInterface final : public IClientMgrUIInterface
    {};

    // ---------------------------------------------------------------------------------------------
    // Sync handling

    void HandleSync(RunState& run, SSyncData* const sync)
    {
      const int beat = sync->mCurBeat;
      ++run.mSyncPackets;
      if (run.mFirstBeat < 0) {
        run.mFirstBeat = beat;
      }
      run.mLastBeat = beat;

      for (const SDesyncInfo& desync : sync->mDesyncs) {
        ++run.mEngineDesyncs;
        if (run.mFirstEngineDesyncBeat < 0 || desync.beat < run.mFirstEngineDesyncBeat) {
          run.mFirstEngineDesyncBeat = desync.beat;
        }
        if (run.mDesyncs.size() < 200) {
          run.mDesyncs.push_back(DesyncReport{desync.beat, desync.army, beat});
        }
      }

      if (sync->mGameOver && !run.mGameOver) {
        run.mGameOver = true;
        run.mGameOverBeat = beat;
        Out("[headless] game over at beat %d\n", beat);
      }

      const auto recorded = run.mScan.mChecksums.find(beat);
      if (recorded != run.mScan.mChecksums.end()) {
        CheckpointResult checkpoint;
        checkpoint.mBeat = beat;
        checkpoint.mRecorded = recorded->second;

        // The sync packet for beat N is queued after `Sim::Sync` stored beat N's digest in the
        // 128-beat ring and advanced `mCurBeat`, so the digest is there now. In threaded mode the
        // sim may already be further on, but the issue thread never lets it run more than 98 beats
        // past the last packet taken here, so slot N cannot have been reused yet.
        gpg::MD5Digest digest{};
        const Sim* const sim = Sim::sInstance;
        checkpoint.mHaveSim = sim != nullptr && sim->GetBeatChecksum(&digest, beat);
        if (checkpoint.mHaveSim) {
          std::memcpy(checkpoint.mSim, digest.vals, 16);
          for (int i = 0; i < 16; ++i) {
            run.mCheckpointChain = (run.mCheckpointChain ^ checkpoint.mSim[i]) * 1099511628211ull;
          }
        } else {
          ++run.mCheckpointsMissing;
        }

        int matches = 0;
        int mismatches = 0;
        for (const RecordedChecksum& entry : checkpoint.mRecorded) {
          ++run.mChecks;
          if (checkpoint.mHaveSim && std::memcmp(entry.mDigest, checkpoint.mSim, 16) == 0) {
            ++matches;
          } else {
            ++mismatches;
          }
        }
        run.mMismatches += mismatches;
        if (mismatches > 0 && run.mFirstMismatchBeat < 0) {
          run.mFirstMismatchBeat = beat;
        }
        Out(
          "[headless] checksum beat %d sim %s recorded %s %s (%d/%d)\n", beat,
          checkpoint.mHaveSim ? DigestHex(checkpoint.mSim).c_str() : "<none>",
          checkpoint.mRecorded.empty() ? "<none>" : DigestHex(checkpoint.mRecorded.front().mDigest).c_str(),
          mismatches == 0 ? "match" : "MISMATCH", matches, matches + mismatches
        );
        run.mCheckpoints.push_back(checkpoint);
      }

      if (run.mOptions.mProgressEvery > 0 && beat > 0 && beat % run.mOptions.mProgressEvery == 0) {
        const double simSeconds = SecondsSince(run.mStart) - run.mLoadSeconds;
        Out(
          "[headless] beat %d/%d  %.1fs  %.1f beats/s  desyncs %d\n", beat, run.mScan.mTotalBeats,
          simSeconds, simSeconds > 0.0 ? (beat - std::max(run.mFirstBeat, 0)) / simSeconds : 0.0,
          run.mEngineDesyncs
        );
      }
    }

    /**
     * The replay client feeds the stream's beats and, at the end of the file, an EndGame plus one
     * more beat, and then ejects itself (CReplayClient::Start). With the replay client gone nothing
     * holds the beat pipeline back any more and the sim would run empty beats forever - in the GUI
     * the player is looking at the score screen by then. So the replay has ended once that extra
     * beat ran and the sim reported the game over (Sim::Sync publishes `mGameOver` the beat after
     * EndGame); a few beats of grace cover a replay whose end does not produce one.
     */
    [[nodiscard]] bool ReachedEnd(const RunState& run)
    {
      const int finalBeat = run.mScan.mTotalBeats;
      return (run.mLastBeat >= finalBeat + 1 && run.mGameOver) || run.mLastBeat >= finalBeat + kEndGraceBeats;
    }

    /**
     * Takes every packet the driver has queued, up to the end of the replay. `GetSyncData` only
     * blocks (through `PerformNextEvent`) when the queue is empty, and `HasSyncData` rules that out.
     * Stopping at the end keeps `last_beat` independent of how many empty beats past it the sim
     * thread happened to queue by then; the teardown frees the rest.
     */
    [[nodiscard]] int DrainSyncs(RunState& run, ISTIDriver& driver)
    {
      int taken = 0;
      while (!ReachedEnd(run) && driver.HasSyncData()) {
        SSyncData* sync = nullptr;
        driver.GetSyncData(sync);
        if (sync == nullptr) {
          break;
        }
        HandleSync(run, sync);
        delete sync;
        ++taken;
      }
      return taken;
    }

    void SleepMs(const int milliseconds)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }

    // ---------------------------------------------------------------------------------------------
    // Registry snapshot

    [[nodiscard]] std::uint64_t HashSortedNames(std::vector<std::string>& names)
    {
      std::sort(names.begin(), names.end());
      std::uint64_t hash = 1469598103934665603ull;
      for (const std::string& name : names) {
        for (const char c : name) {
          hash = (hash ^ static_cast<unsigned char>(c)) * 1099511628211ull;
        }
        hash = (hash ^ static_cast<unsigned char>('\n')) * 1099511628211ull;
      }
      return hash;
    }

    /**
     * A class name both compilers spell the same: MSVC's type_info::name() ("class moho::Foo",
     * "struct gpg::Bar<class moho::Foo>", "class `anonymous namespace'::Baz") and the demangled
     * Itanium name ("moho::Foo", "gpg::Bar<moho::Foo>", "(anonymous namespace)::Baz") both become
     * "moho::Foo", "gpg::Bar<moho::Foo>", "{anon}::Baz".
     */
    [[nodiscard]] std::string PortableTypeName(const std::type_info& type)
    {
#if defined(_MSC_VER)
      std::string name = type.name();
#else
      int status = 0;
      char* const demangled = abi::__cxa_demangle(type.name(), nullptr, nullptr, &status);
      std::string name = (status == 0 && demangled != nullptr) ? demangled : type.name();
      std::free(demangled);
#endif
      for (const char* const anonymous : {"`anonymous namespace'", "(anonymous namespace)"}) {
        for (std::size_t at = name.find(anonymous); at != std::string::npos; at = name.find(anonymous)) {
          name.replace(at, std::strlen(anonymous), "{anon}");
        }
      }
      std::string out;
      for (std::size_t i = 0; i < name.size();) {
        const bool wordStart = (i == 0) || name[i - 1] == ' ' || name[i - 1] == '<' || name[i - 1] == ',' ||
                               name[i - 1] == '(';
        bool skipped = false;
        if (wordStart) {
          for (const char* const keyword : {"class ", "struct ", "enum ", "union "}) {
            if (name.compare(i, std::strlen(keyword), keyword) == 0) {
              i += std::strlen(keyword);
              skipped = true;
              break;
            }
          }
        }
        if (skipped) {
          continue;
        }
        if (name[i] != ' ') {
          out += name[i];
        }
        ++i;
      }
      return out;
    }

    /**
     * Reads the registries the engine fills from static initialisers: reflected types, Lua binders
     * by init set ("Core", "Sim", "User"), console commands/variables, prefetch kinds, resource
     * factories and serializer helpers. Read-only; runs after REF_RegisterAllTypes, when every
     * pre-registered type is registered, and after RES_ActivatePendingFactories.
     */
    void TakeRegistrySnapshot(RunState& run)
    {
      RegistrySnapshot& registry = run.mRegistry;

      std::vector<std::string> rtypes;
      for (const auto& entry : gpg::GetRTypeMap()) {
        rtypes.emplace_back(entry.first != nullptr ? entry.first : "");
      }
      registry.mRTypes = static_cast<int>(rtypes.size());
      registry.mRTypesPreregistered = static_cast<int>(gpg::GetRTypePreregisteredMap().size());

      std::vector<std::string> binders;
      for (CScrLuaInitFormSet* set = CScrLuaInitFormSet::GetFirst(); set != nullptr; set = set->GetNext()) {
        const std::string setName = (set->mSetName != nullptr) ? set->mSetName : "";
        std::pair<int, int>& counts = registry.mLuaSets[setName];
        ++counts.first;
        for (const CScrLuaInitForm* form = set->mForms; form != nullptr; form = form->mNextInSet) {
          ++counts.second;
          binders.push_back(
            setName + " " + ((form->mGroupName != nullptr) ? form->mGroupName : "") + " " +
            ((form->mName != nullptr) ? form->mName : "")
          );
        }
      }

      msvc8::string commandList;
      CON_GetCommandList(commandList, false);
      std::vector<std::string> commands;
      for (const char* cursor = commandList.c_str(); *cursor != '\0';) {
        const char* const end = std::strchr(cursor, '\n');
        const std::size_t length = (end != nullptr) ? static_cast<std::size_t>(end - cursor) : std::strlen(cursor);
        if (length > 0) {
          commands.emplace_back(cursor, length);
        }
        cursor += length + ((end != nullptr) ? 1 : 0);
      }
      registry.mConsoleCommands = static_cast<int>(commands.size());

      // Prefetch kinds. Their map is private to PrefetchHandleBase.cpp, so the kinds are looked up by
      // name: these four are every name a static initialiser registers (RES_RegisterPrefetchType in
      // RScaResource.cpp, RScmResource.cpp, SBatchTextureDataFactory.cpp and
      // CD3DTextureResourceFactory.cpp/CD3DDeviceResources.cpp), the kinds CPrefetchSet:Update takes.
      for (const char* const kind : {"anims", "batch_textures", "d3d_textures", "models"}) {
        const gpg::RType* const type = RES_FindPrefetchType(kind);
        const char* const typeName = (type != nullptr) ? type->GetName() : nullptr;
        registry.mPrefetchKinds.emplace_back(kind, typeName != nullptr ? typeName : "");
      }

      // Resource factories: the manager keys its active factories by their resource RType
      // (ResourceManager::ActivatePendingFactories). FindFactoryByRegistrationKey answers with the
      // first factory at or above a key, so an exact match is one the next key does not also find.
      std::vector<std::string> factories;
      std::vector<std::string> callbacks;
      ResourceManager* const resourceManager = RES_GetResourceManager();
      for (const auto& [typeName, type] : gpg::GetRTypeMap()) {
        if (type == nullptr) {
          continue;
        }
        const std::string name = (typeName != nullptr) ? typeName : "";
        if (resourceManager != nullptr) {
          const unsigned int key = static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(type));
          ResourceFactoryBase* const factory = resourceManager->FindFactoryByRegistrationKey(key);
          if (factory != nullptr && factory != resourceManager->FindFactoryByRegistrationKey(key + 1u)) {
            factories.push_back(name);
          }
        }
        const bool serializer = type->serLoadFunc_ != nullptr || type->serSaveFunc_ != nullptr;
        const bool construct = type->serConstructFunc_ != nullptr || type->serSaveConstructArgsFunc_ != nullptr;
        registry.mRTypesWithSerializer += serializer ? 1 : 0;
        registry.mRTypesWithConstruct += construct ? 1 : 0;
        if (serializer || construct) {
          callbacks.push_back(
            name + " " + (type->serLoadFunc_ != nullptr ? "L" : "-") + (type->serSaveFunc_ != nullptr ? "S" : "-") +
            (type->serConstructFunc_ != nullptr ? "C" : "-") + (type->serSaveConstructArgsFunc_ != nullptr ? "A" : "-")
          );
        }
      }
      registry.mResourceFactories = static_cast<int>(factories.size());

      // Serializer and construct helpers still waiting for SerHelperBase::InitNewHelpers, by class.
      std::vector<std::string> helpers;
      if (gpg::SerHelperBase::sNewHelpers != nullptr) {
        for (gpg::SerHelperBase* const helper : *gpg::SerHelperBase::sNewHelpers) {
          helpers.push_back(PortableTypeName(typeid(*helper)));
        }
      }
      registry.mSerializerHelpers = static_cast<int>(helpers.size());

      registry.mRTypeHash = HashSortedNames(rtypes);
      registry.mLuaHash = HashSortedNames(binders);
      registry.mConsoleHash = HashSortedNames(commands);
      registry.mFactoryHash = HashSortedNames(factories);
      registry.mSerializerHash = HashSortedNames(helpers);
      registry.mCallbackHash = HashSortedNames(callbacks);
      registry.mTaken = true;

      if (!run.mOptions.mRegistryPath.empty()) {
        std::FILE* const file = std::fopen(run.mOptions.mRegistryPath.c_str(), "wb");
        if (file == nullptr) {
          Out("[headless] cannot write registry dump %s\n", run.mOptions.mRegistryPath.c_str());
          return;
        }
        for (const std::string& name : rtypes) {
          std::fprintf(file, "rtype %s\n", name.c_str());
        }
        for (const std::string& name : binders) {
          std::fprintf(file, "lua %s\n", name.c_str());
        }
        for (const std::string& name : commands) {
          std::fprintf(file, "console %s\n", name.c_str());
        }
        for (const auto& [kind, typeName] : registry.mPrefetchKinds) {
          std::fprintf(file, "prefetch %s %s\n", kind.c_str(), typeName.empty() ? "-" : typeName.c_str());
        }
        for (const std::string& name : factories) {
          std::fprintf(file, "factory %s\n", name.c_str());
        }
        for (const std::string& name : helpers) {
          std::fprintf(file, "serhelper %s\n", name.c_str());
        }
        for (const std::string& entry : callbacks) {
          std::fprintf(file, "rtypecb %s\n", entry.c_str());
        }
        std::fclose(file);
      }
    }

    // ---------------------------------------------------------------------------------------------
    // The run

    [[nodiscard]] int Run(RunState& run)
    {
      HeadlessOptions& options = run.mOptions;

      // -- Host services. IWinApp::AppInitCommonServices, CScApp::CScApp/Init and WIN_AppExecute
      // up to the first frame, minus everything with a window, a device, sound or UI in it.
      APP_InitializeIdentity();

      // CScApp::Init's first statement. The SSE2 lane of the engine's CRT math (acos, ceil, ...)
      // is off unless `/sse2` is given; the sim calls those, so this must match the GUI.
      run.mSse2 = CFG_GetArgOption("/sse2", 0, nullptr);
      (void)RuntimeSetSse2Mode(run.mSse2 ? 1 : 0);
      (void)ReadArg("/simworkers", run.mSimWorkers);

      // WIN_AppExecute puts the main thread at 24-bit x87 precision before its frame loop. The sim
      // and loader threads set it for themselves; the main thread needs it for `ShutDown` (which
      // runs `Sim::Shutdown` here) and, with /headlessinterlocked, for every beat.
      platform::SetX87PrecisionControl(_PC_24);

      gpg::EnableLogHistory(100);
      gpg::REF_RegisterAllTypes();
      RES_EnsureResourceManager();
      RES_ActivatePendingFactories();
      TakeRegistrySnapshot(run);
      Out(
        "[headless] registry: %d rtypes (%d pre-registered), %d console commands, %d Lua init sets\n",
        run.mRegistry.mRTypes, run.mRegistry.mRTypesPreregistered, run.mRegistry.mConsoleCommands,
        static_cast<int>(run.mRegistry.mLuaSets.size())
      );
      {
        int prefetchKinds = 0;
        for (const auto& kind : run.mRegistry.mPrefetchKinds) {
          prefetchKinds += kind.second.empty() ? 0 : 1;
        }
        Out(
          "[headless] registry: %d/%d prefetch kinds, %d resource factories, %d serializer helpers queued, "
          "%d/%d rtypes with serializer/construct callbacks\n",
          prefetchKinds, static_cast<int>(run.mRegistry.mPrefetchKinds.size()), run.mRegistry.mResourceFactories,
          run.mRegistry.mSerializerHelpers, run.mRegistry.mRTypesWithSerializer, run.mRegistry.mRTypesWithConstruct
        );
      }
      if (!DISK_SetupDataAndSearchPaths(msvc8::string("SupComDataPath.lua"), DISK_GetLaunchDir())) {
        run.mEndReason = "failed to set up the data search path (check /init)";
        return 1;
      }

      // Deliberately skipped from CScApp::Init:
      // - USER_LoadPreferences. The GUI loads Game.prefs from the user's profile; the only value
      //   from it that reaches the sim is `__language` (userInit.lua reads it on the loader's Lua
      //   state, WLD_DoLoading copies it into LaunchInfo::mLanguage). The runner stays independent
      //   of the host profile by default and loads a file only for an explicit `/prefs <name>` (a
      //   file name in the profile directory, as for the GUI). Without prefs the preference is ""
      //   and Localization.lua settles on "us", the language every install has; a GUI whose profile
      //   picked another language hands the sim that one instead.
      // - The audio engine, and with it the rest of SessionInit.lua on the loader state: the
      //   language switch ends in AudioSetLanguage, which fails without XACT ("SND: Error
      //   retrieving XACT COM interface") and stops the script after `__language` is set. That is
      //   one user-side Lua error per run (lua_errors_load); the session Lua state is not used
      //   after loading.
      // - userInit.lua on the front-end state, REN_Init (fonts), UI_Init, CreateDevice,
      //   OPTIONS_Apply (graphics, sound and UI options), the screensaver and cursor handling.
      // - The sound engine. Without it `sSoundConfiguration` stays null and `Sim::Setup` creates no
      //   CSimSoundManager, exactly as with `/nosound`; it only forwards sound requests to the
      //   sync packet.
      {
        std::string prefsFile;
        if (ReadArg("/prefs", prefsFile)) {
          USER_LoadPreferences(msvc8::string(prefsFile.c_str()));
          run.mPrefsFile = prefsFile;
        }
      }

      // -- Session info: CScApp's first frame (`InitializeSessionFromCommandLine`, `/replay`).
      // SetFrontEndReplayFilename only writes the front-end Lua state; skipped. The front-end state
      // never ran userInit.lua here, so VCR_SetupReplaySession's Loc() of the local client's name
      // logs "Error localizing" and keeps the raw key - a nickname, which the sim never sees.
      run.mPhase = static_cast<int>(EPhase::Load);
      msvc8::auto_ptr<SWldSessionInfo> sessionInfo = VCR_SetupReplaySession(options.mReplayPath.c_str());
      if (sessionInfo.get() == nullptr || sessionInfo->mClientManager == nullptr) {
        run.mEndReason = "VCR_SetupReplaySession rejected the replay (missing file, or a header other than "
                         "\"Supreme Commander v1.50.3764\" - convert with scripts/perf/convert_replay.py "
                         "--as-version 3764)";
        return 1;
      }
      LaunchInfoBase* const launchInfo = sessionInfo->mLaunchInfo.get();
      Out(
        "[headless] session: map %s, %d armies, %d command sources, replay %s\n", sessionInfo->mMapName.c_str(),
        static_cast<int>(launchInfo->mArmyLaunchInfo.size()),
        static_cast<int>(launchInfo->mCommandSources.mSrcs.size()), sessionInfo->mIsReplay ? "yes" : "no"
      );

      // -- WLD_DoPreload. Kept: the prefetch request on the world-session loader and the client
      // manager's UI interface. Skipped: WLD_Teardown (nothing to tear down), USER_SavePreferences
      // (never write the user's profile), UI_StartGameUI and the loading dialog.
      HeadlessClientMgrUIInterface clientUi;
      CWldSessionLoaderImpl* const loader = GetWldSessionLoader();
      (void)loader->GetScenarioInfo(sessionInfo->mMapName.raw_data_unsafe(), &launchInfo->mGameMods, true);
      sessionInfo->mClientManager->SetUIInterface(&clientUi);

      // -- WLD_DoLoading, first half. WLD_Frame runs `loader->Update()` every frame and the Loading
      // step beats the client manager until the "Map loader" thread (WorldSessionUserLoad: rules,
      // the session Lua state with the Core and User binders, SessionInit.lua, CWldMap::MapLoad)
      // is done. Same calls here, paced by a short sleep instead of the frame clock.
      const Clock::time_point loadStart = Clock::now();
      for (;;) {
        loader->Update();
        sessionInfo->mClientManager->DoBeat();
        if (loader->IsLoaded()) {
          break;
        }
        if (SecondsSince(loadStart) > std::max(options.mTimeoutSeconds, 600)) {
          run.mEndReason = "scenario load timed out";
          return 4;
        }
        SleepMs(5);
      }

      SWldGameData gameData{};
      (void)loader->LoadGameData(&gameData);
      if (gameData.mGameRules == nullptr || gameData.mWldMap == nullptr || gameData.mWldMap->mTerrainRes == nullptr ||
          gameData.mWldMap->mTerrainRes->mMap == nullptr) {
        gpg::Warnf("map %s failed.  aborting session.", sessionInfo->mMapName.c_str());
        ReleaseWldGameDataHandles(&gameData);
        run.mEndReason = "scenario load failed (no rules or no map)";
        return 2;
      }
      std::unique_ptr<LuaPlus::LuaState> sessionState(gameData.mState);
      std::unique_ptr<RRuleGameRules> rules(gameData.mGameRules);
      std::unique_ptr<CWldMap> wldMap(gameData.mWldMap);
      gameData = SWldGameData{};

      // -- WLD_DoLoading, second half. Skipped: VCR_CreateReplay (playback records nothing) and
      // WLD_CreateSession. The session takes ownership of the loaded state, rules and map and builds
      // the user-side world (entity mirrors, selection, vision quadtree, the session task stage);
      // nothing of it is read by the sim, which gets the four launch-info fields below - the same
      // values, from the same objects, as `WLD_DoLoading` copies.
      launchInfo->mGameRules = rules.get();
      STIMap* const launchMap = new STIMap(wldMap->mTerrainRes->mMap);
      if (launchInfo->mMap != nullptr && launchInfo->mMap != launchMap) {
        delete launchInfo->mMap;
      }
      launchInfo->mMap = launchMap;
      launchInfo->mLanguage.assign_owned(sessionState->GetGlobal("__language").ToString());
      run.mLanguage = launchInfo->mLanguage.c_str();
      if (LaunchInfoNew* const newLaunchInfo = launchInfo->GetNew(); newLaunchInfo != nullptr) {
        newLaunchInfo->mProps = wldMap->mProps;
      }
      Out(
        "[headless] scenario loaded in %.1fs (language \"%s\")\n", SecondsSince(loadStart),
        launchInfo->mLanguage.c_str()
      );

      // The driver takes the client manager; the session info gives it up, as in WLD_DoLoading.
      run.mPhase = static_cast<int>(EPhase::Sim);
      auto* const clientManager = static_cast<CClientManagerImpl*>(sessionInfo->mClientManager);
      sessionInfo->mClientManager = nullptr;
      std::unique_ptr<ISTIDriver> driver(
        SIM_CreateDriver(clientManager, nullptr, sessionInfo->mLaunchInfo, sessionInfo->mSourceId)
      );

      // -- WLD_DoInitializing: dispatch until the sim publishes its opening sync (Sim::Create runs
      // on the driver's bootstrap thread meanwhile). Skipped: UI_StartGameUI, CWldSession::DoBeat
      // on that packet, the loading dialog, CreateGameInterface and the teardown callbacks - all
      // user side. Kept: dropping the session info and `Cleanup`, which declares this client ready.
      const Clock::time_point createStart = Clock::now();
      while (!driver->HasSyncData()) {
        driver->Dispatch();
        if (run.mSimFailed) {
          run.mEndReason = "Sim::Create failed: " + run.mSimFailure;
          run.mLoadSeconds = SecondsSince(run.mStart);
          return 3;
        }
        if (SecondsSince(createStart) > std::max(options.mTimeoutSeconds, 600)) {
          run.mEndReason = "no opening sync from Sim::Create";
          run.mLoadSeconds = SecondsSince(run.mStart);
          return 4;
        }
        SleepMs(5);
      }
      run.mLoadSeconds = SecondsSince(run.mStart);
      Out("[headless] sim created, %.1fs since start\n", run.mLoadSeconds);
      (void)DrainSyncs(run, *driver);

      if (options.mInterlocked) {
        // ISTIDriver::ProcessEvents puts the driver in interlocked mode: from here `Dispatch`
        // executes the beats on this thread and the bootstrap thread only waits. Nothing has been
        // issued yet (the issue thread waits for the outstanding request released below), so the
        // switch cannot land in the middle of a beat.
        (void)driver->ProcessEvents();
      }

      sessionInfo.reset();
      clientManager->Cleanup();

      // -- WLD_DoPostInitializing / WLD_DoWaiting: once everyone is ready, release the driver's
      // outstanding request so the issue thread starts handing out beats. Skipped: the waiting
      // dialog and IWldUIProvider::OnStart.
      const Clock::time_point readyStart = Clock::now();
      for (;;) {
        driver->Dispatch();
        if (clientManager->IsEveryoneReady()) {
          break;
        }
        if (SecondsSince(readyStart) > options.mTimeoutSeconds) {
          run.mEndReason = "clients never became ready";
          return 4;
        }
        SleepMs(5);
      }
      driver->DecrementOutstandingRequestsAndSignal();

      // The GUI's replay speed keys (WLD_IncreaseSimRate / WLD_SetGameSpeed) end in this call. 50
      // is the ceiling WLD_IncreaseSimRate allows; the issue thread then runs as far ahead as its
      // own caps let it, and the sim's measured rate (CLIMSG_IntParam) bounds it from below.
      clientManager->SetSimRate(options.mGameSpeed);

      // -- WLD_DoPlayingAction, every frame: Dispatch, SetGeomCams, CWldSession::SessionFrame
      // (which drains the sync queue into CWldSession::DoBeat), the driver's NoOp slot. The camera
      // list only filters what the sync packet carries for rendering, and DoBeat is the user-side
      // consumer; the sim reads neither. So: Dispatch, then drain.
      const Clock::time_point simStart = Clock::now();
      Clock::time_point lastProgress = simStart;
      bool dumpedStall = false;
      for (;;) {
        driver->Dispatch();
        const int taken = DrainSyncs(run, *driver);
        if (taken > 0) {
          lastProgress = Clock::now();
          dumpedStall = false;
        }

        if (ReachedEnd(run)) {
          run.mEndReason = run.mGameOver ? "replay_end" : "replay_end_without_game_over";
          break;
        }
        if (run.mSimFailed) {
          run.mEndReason = "sim failed: " + run.mSimFailure;
          run.mSimSeconds = SecondsSince(simStart);
          return 3;
        }

        const double idle = std::chrono::duration<double>(Clock::now() - lastProgress).count();
        if (idle > 5.0 && !dumpedStall) {
          // A stall is almost always the beat pipeline (who has queued, acked, dispatched what);
          // `wld_ClientDebugDump`'s dump goes to the log.
          dumpedStall = true;
          Out(
            "[headless] no new beat for 5s at beat %d (sim rate %d, requested %d); client dump in the log\n",
            run.mLastBeat, clientManager->GetSimRate(), clientManager->GetSimRateRequested()
          );
          clientManager->Debug();
        }
        if (idle > options.mTimeoutSeconds) {
          run.mEndReason = run.mGameOver ? "stalled after game over" : "stalled";
          run.mSimSeconds = SecondsSince(simStart);
          return 4;
        }

        if (taken == 0) {
          if (options.mInterlocked) {
            // Nothing issued yet; the issue thread is ahead again within a millisecond or two.
            SleepMs(1);
          } else {
            // The driver's manual-reset "sync data available" event, as CScApp::Main waits on it
            // (on Android through port/engine/shim/faf_win_kernel.h, with the same semantics).
            (void)::WaitForSingleObject(driver->GetSyncDataAvailableEvent(), 100u);
          }
        }
      }
      run.mSimSeconds = SecondsSince(simStart);
      run.mExitCode = 0;
      run.mBeforeTeardown = true;
      WriteSummary(); // before the teardown, so a crash there cannot lose the result
      run.mBeforeTeardown = false;

      // -- Teardown in WLD_Teardown's order: ShutDown (stops the threads, runs Sim::Shutdown and the
      // last sync pass, deletes the sim), drain what it queued, delete what the session would have
      // owned (map, rules, Lua state), and only then the driver. RES_Exit as at the end of
      // WIN_AppExecute.
      run.mPhase = static_cast<int>(EPhase::Teardown);
      Out("[headless] shutting down\n");
      if (options.mInterlocked) {
        driver->ReleaseInterlockRef();
      }
      driver->ShutDown();
      // ShutDown queues one last packet (and the issue thread may have run a few empty beats past
      // the end); WLD_Teardown hands them to the session, here they are only freed.
      while (driver->HasSyncData()) {
        SSyncData* sync = nullptr;
        driver->GetSyncData(sync);
        delete sync;
      }
      wldMap.reset();
      rules.reset();
      sessionState.reset();
      driver.reset();
      RES_Exit();
      run.mTeardownDone = true;
      return 0;
    }
  } // namespace

  int HEADLESS_RunReplay()
  {
#if defined(_WIN32)
    PrepareWindowsHost();
#endif

    static RunState run; // static: the die handler and the log target may outlive this frame
    run.mStart = Clock::now();
    sRun = &run;

    if (!ReadArg("/headlessreplay", run.mOptions.mReplayPath)) {
      Out("[headless] usage: /headlessreplay <file.scfareplay> /init <init_faf.lua> [/headlesssummary <json>]\n");
      return 1;
    }
    (void)ReadArg("/headlesssummary", run.mOptions.mSummaryPath);
    (void)ReadArg("/headlessregistry", run.mOptions.mRegistryPath);
    (void)ReadIntArg("/headlessprogress", run.mOptions.mProgressEvery);
    (void)ReadIntArg("/headlesstimeout", run.mOptions.mTimeoutSeconds);
    (void)ReadIntArg("/headlessspeed", run.mOptions.mGameSpeed);
    run.mOptions.mInterlocked = CFG_GetArgOption("/headlessinterlocked", 0, nullptr);
    run.mOptions.mGameSpeed = std::clamp(run.mOptions.mGameSpeed, -10, 50);
    if (run.mOptions.mTimeoutSeconds <= 0) {
      run.mOptions.mTimeoutSeconds = 120;
    }

    std::string logPath;
    if (ReadArg("/log", logPath)) {
      run.mLogFile = std::fopen(logPath.c_str(), "w");
    }

    gpg::SetDieHandler(&HeadlessDieHandler);
    static HeadlessLogTarget logTarget;

    run.mScan = ScanReplay(run.mOptions.mReplayPath);
    if (!run.mScan.mError.empty()) {
      run.mEndReason = "replay scan: " + run.mScan.mError;
      run.mExitCode = 1;
      WriteSummary();
      return run.mExitCode;
    }
    Out(
      "[headless] replay %s: \"%s\", map %s, %d beats, %d recorded checksums at %d beats, %s\n",
      run.mOptions.mReplayPath.c_str(), run.mScan.mVersion.c_str(), run.mScan.mMap.c_str(), run.mScan.mTotalBeats,
      run.mScan.mVerifyMessages, static_cast<int>(run.mScan.mChecksums.size()),
      run.mScan.mHasEndGame ? "has EndGame" : "no EndGame"
    );
    Out("[headless] mode %s, game speed %d\n", run.mOptions.mInterlocked ? "interlocked" : "threaded",
        run.mOptions.mGameSpeed);

    int exitCode = 1;
    try {
      exitCode = Run(run);
    } catch (const std::exception& error) {
      run.mEndReason = std::string("exception: ") + error.what();
      exitCode = 3;
    } catch (...) {
      run.mEndReason = "exception: unknown";
      exitCode = 3;
    }
    run.mExitCode = exitCode;
    WriteSummary();
    if (run.mLogFile != nullptr) {
      std::fflush(run.mLogFile);
    }
    return exitCode;
  }
} // namespace moho
