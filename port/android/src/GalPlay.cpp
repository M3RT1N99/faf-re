// galplay_run and its companions (GalPlay.h): the app side of the Android galplay (M7a1). The game data
// is mounted the way M1's GameRuntime mounts it (init_faf.lua, port/native's VFS), the trace's game-file
// references resolve through that VFS (port/graphics/trace's VfsResolver), and the engine-side half
// (GalPlayEngine.cpp, the gal types) replays the trace into the Diligent backend on Vulkan. This file
// owns what is the activity's business: the window and its changes, the recorded pace, the progress and
// readback callbacks, the shader and pipeline caches, and the files in the run directory.
//
// Threads: galplay_run's caller thread becomes the render thread (the Vulkan device and every swap
// chain live there). galplay_set_window and galplay_request_stop come from the activity's thread and
// only post to the session; the render thread applies window changes between frames.

#include "GalPlay.h"

#include <android/native_window.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "GalPlayEngine.h"
#include "Log.h"
#include "faf/port/DataPath.h"
#include "faf/port/FileSystem.h"
#include "faf/port/Vfs.h"
#include "port/graphics/diligent/DiligentHost.h"
#include "port/graphics/diligent/ShaderCompileDiligent.h"
#include "port/graphics/trace/format/GalTraceFormat.h"
#include "port/graphics/trace/format/GalTraceIO.h"
#include "port/graphics/trace/format/GalTraceResolver.h"
#include "port/graphics/trace/format/GalTraceVfsResolver.h"

namespace {

  namespace dil = gpg::gal::diligent;
  namespace port = faf::port;
  using Clock = std::chrono::steady_clock;
  using faf::android::LogError;
  using faf::android::LogInfo;
  using faf::android::LogWarning;

  double MillisecondsBetween(const Clock::time_point from, const Clock::time_point to)
  {
    return std::chrono::duration<double, std::milli>(to - from).count();
  }

  std::string JsonEscape(const std::string_view text)
  {
    std::string out;
    out.reserve(text.size() + 2);
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
          out += c;
        }
      }
    }
    return out;
  }

  std::string Quote(const std::string_view text)
  {
    return "\"" + JsonEscape(text) + "\"";
  }

  std::string Number(const double value, const int decimals = 1)
  {
    char text[48];
    std::snprintf(text, sizeof(text), "%.*f", decimals, value);
    return text;
  }

  bool WriteFileAtomically(const std::string& path, const std::string& content)
  {
    const std::string temporary = path + ".tmp";
    {
      std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
      file << content;
      if (!file) {
        return false;
      }
    }
    return std::rename(temporary.c_str(), path.c_str()) == 0;
  }

  // --- the shader cache: one file per compiled shader --------------------------------------------------

  /**
   * ShaderCompileDiligent.h's cache as files: <dir>/<key>.spv. A file is written next to its final name
   * and renamed, so a process killed mid-write leaves no half file; an unreadable or truncated one is a
   * miss (the shader is compiled again and the file replaced).
   */
  class FileShaderCache final : public dil::ShaderBytecodeCache
  {
  public:
    explicit FileShaderCache(std::string directory)
      : mDirectory(std::move(directory))
    {}

    bool Load(const std::string& key, std::vector<std::uint8_t>* const bytecode) override
    {
      std::ifstream file(Path(key), std::ios::binary);
      if (!file) {
        return false;
      }
      bytecode->assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
      if (!bytecode->empty()) {
        ++mLoaded;
      }
      return !bytecode->empty();
    }

    void Store(const std::string& key, const void* const bytecode, const std::size_t size) override
    {
      const std::string path = Path(key);
      const std::string temporary = path + ".tmp";
      {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(static_cast<const char*>(bytecode), static_cast<std::streamsize>(size));
        if (!file) {
          return;
        }
      }
      if (std::rename(temporary.c_str(), path.c_str()) == 0) {
        ++mStored;
        mStoredBytes += size;
      }
    }

    [[nodiscard]] std::uint64_t Loaded() const
    {
      return mLoaded;
    }
    [[nodiscard]] std::uint64_t Stored() const
    {
      return mStored;
    }
    [[nodiscard]] std::uint64_t StoredBytes() const
    {
      return mStoredBytes;
    }

  private:
    [[nodiscard]] std::string Path(const std::string& key) const
    {
      return mDirectory + "/" + key + ".spv";
    }

    std::string mDirectory;
    std::uint64_t mLoaded = 0;
    std::uint64_t mStored = 0;
    std::uint64_t mStoredBytes = 0;
  };

  // --- the session: what the activity's thread posts to the render thread -------------------------------

  struct Session
  {
    std::mutex mutex;
    std::condition_variable changed;
    bool running = false;
    std::atomic<bool> stop{false};
    // Window handoff. `window` is the one the render thread should use (referenced while held here).
    ANativeWindow* window = nullptr;
    bool windowPending = false; ///< `window` changed and the render thread has not applied it yet
    std::uint64_t windowTicket = 0;
    std::uint64_t windowApplied = 0;
    bool hasDevice = false;     ///< a swap chain may exist: a null window must wait for the render thread
  };

  Session& TheSession()
  {
    static Session session;
    return session;
  }

  /** The replay's log: logcat and the runtime log (Log.h), galplay.log in the run directory. */
  class ReplayLog
  {
  public:
    void Open(const std::string& path)
    {
      const std::lock_guard lock(mMutex);
      mFile.reset(std::fopen(path.c_str(), "wb"));
    }

    void Line(const int level, const std::string& text)
    {
      switch (level) {
      case 0:
        faf::android::Log::Get().Write(faf::android::LogLevel::Debug, "galplay: " + text);
        break;
      case 2:
        faf::android::Log::Get().Write(faf::android::LogLevel::Warning, "galplay: " + text);
        break;
      case 3:
        faf::android::Log::Get().Write(faf::android::LogLevel::Error, "galplay: " + text);
        break;
      default:
        faf::android::Log::Get().Write(faf::android::LogLevel::Info, "galplay: " + text);
        break;
      }
      const std::lock_guard lock(mMutex);
      if (mFile) {
        static constexpr const char* kTags[] = {"debug", "info", "warning", "error"};
        std::fprintf(mFile.get(), "%s: %s\n", kTags[std::clamp(level, 0, 3)], text.c_str());
        std::fflush(mFile.get());
      }
    }

    void Close()
    {
      const std::lock_guard lock(mMutex);
      mFile.reset();
    }

  private:
    struct FileCloser
    {
      void operator()(std::FILE* file) const
      {
        std::fclose(file);
      }
    };
    std::mutex mMutex;
    std::unique_ptr<std::FILE, FileCloser> mFile;
  };

  struct FrameRecord
  {
    unsigned frame = 0;
    std::string hash;
    std::string reference;
    std::string verdict;
    int identicalToRecording = -1;
  };

  /** Everything one galplay_run keeps on its render thread. */
  class Replay
  {
  public:
    Replay(std::string tracePath, std::string dataRoot, const GalPlayOptions& options, const GalPlayCallbacks* callbacks)
      : mTracePath(std::move(tracePath)),
        mDataRoot(std::move(dataRoot)),
        mOptions(options)
    {
      if (callbacks != nullptr) {
        mCallbacks = *callbacks;
      }
    }

    int Run(GalPlayResult* result);

  private:
    // ---- stages
    bool MountData();
    bool ReadTrace();
    void ConfigureCaches();
    void Replaying();
    void Hold();
    int Finish(GalPlayResult* result);

    // ---- render thread helpers
    void ApplyPendingWindow();
    bool AfterPresent(unsigned presents);
    void OnReadback(const faf::android::galplay::EngineReadback& readback);
    void Progress(const char* stage, const std::string& message, bool force);
    [[nodiscard]] bool StopRequested() const
    {
      if (TheSession().stop.load()) {
        return true;
      }
      return mCallbacks.shouldStop != nullptr && mCallbacks.shouldStop(mCallbacks.user) != 0;
    }
    void Log(const int level, const std::string& text)
    {
      mLog.Line(level, text);
    }
    void Fail(const int code, const std::string& message)
    {
      if (mExitCode == GALPLAY_EXIT_PASS) {
        mExitCode = code;
        mMessage = message;
      }
      Log(3, message);
    }
    [[nodiscard]] std::string BuildJson() const;

    std::string mTracePath;
    std::string mDataRoot;
    GalPlayOptions mOptions;
    GalPlayCallbacks mCallbacks{};
    ReplayLog mLog;
    std::string mRunDir;
    std::string mCacheDir;

    // data
    port::VirtualFileSystem mVfs;
    std::size_t mMounts = 0;
    double mScriptMs = 0.0;
    double mMountMs = 0.0;

    // trace
    galtrace::Metadata mMetadata;
    std::uint32_t mTraceVersion = 0;
    std::uint64_t mTraceBytes = 0;
    unsigned mFrames = 0;
    unsigned mReadbacksExpected = 0;
    double mFrameRate = 30.0;
    std::vector<galtrace::PayloadRefInfo> mRefs;
    std::vector<std::string> mRefArchives;

    // caches
    std::unique_ptr<FileShaderCache> mShaderCache;
    std::string mPipelineCachePath;
    std::uint64_t mPipelineCacheSaved = 0;

    // device
    dil::DiligentHost* mHost = nullptr;
    dil::AndroidDeviceSupport mSupport;
    std::string mAdapter;
    std::string mSurface;
    std::string mSwapChainFormat;

    // replay
    faf::android::galplay::EngineOutcome mOutcome;
    std::vector<FrameRecord> mFrameRecords;
    unsigned mPassed = 0;
    unsigned mFailed = 0;
    unsigned mPresents = 0;
    Clock::time_point mStart;
    Clock::time_point mDataDone;
    Clock::time_point mDeviceReady;
    Clock::time_point mFirstPresent;
    Clock::time_point mLastPresent;
    Clock::time_point mWorkStart;
    Clock::time_point mNextDue;
    Clock::time_point mLastProgress;
    double mLastFrameMs = 0.0;
    double mLastWorkMs = 0.0;
    double mPausedMs = 0.0;
    std::vector<std::uint8_t> mRgba;

    int mExitCode = GALPLAY_EXIT_PASS;
    std::string mMessage;
    std::string mResultWord = "ERROR";
  };

  // ---------------------------------------------------------------------------------------------------

  bool Replay::MountData()
  {
    Progress("data", "Mounting the game data", true);
    const std::string script = mOptions.initScript != nullptr && mOptions.initScript[0] != '\0'
      ? std::string(mOptions.initScript)
      : mDataRoot + "/faf/bin/init_faf.lua";
    const std::optional<std::string> resolved = port::fs::ResolveCaseInsensitive(script);
    if (!resolved || !port::fs::IsRegularFile(*resolved)) {
      Fail(GALPLAY_EXIT_MISSING_DATA,
           "The data-path script " + script +
             " does not exist. Get the FAF files in the launcher (Download FAF files) or deploy them from the PC.");
      return false;
    }
    port::DataPathOptions options;
    options.scriptPath = *resolved;
    options.folders.localAppData =
      mOptions.localAppData != nullptr && mOptions.localAppData[0] != '\0' ? mOptions.localAppData : mDataRoot + "/localappdata";
    options.folders.personal = mOptions.documents != nullptr && mOptions.documents[0] != '\0' ? mOptions.documents : mDataRoot + "/documents";
    options.allowWrites = true;
    options.log = [this](const std::string_view line) {
      Log(0, "init_faf.lua: " + std::string(line));
    };
    const port::DataPathResult result = port::RunDataPathScript(options);
    if (!result.ok) {
      Fail(GALPLAY_EXIT_MISSING_DATA, "init_faf.lua failed: " + result.error);
      return false;
    }
    mScriptMs = result.scriptMilliseconds;
    const Clock::time_point mountStart = Clock::now();
    for (const port::MountSpec& spec : result.mounts) {
      if (StopRequested()) {
        return false;
      }
      std::string error;
      mVfs.Mount(spec, &error);
      if (!error.empty()) {
        Log(2, "mount " + spec.dir + " -> " + spec.mountpoint + ": " + error);
      }
    }
    mMountMs = MillisecondsBetween(mountStart, Clock::now());
    mMounts = mVfs.Mounts().size();
    Log(1, "data: init_faf.lua " + Number(mScriptMs) + " ms, " + std::to_string(mMounts) + " mounts in " + Number(mMountMs) + " ms");
    if (mMounts == 0) {
      Fail(GALPLAY_EXIT_MISSING_DATA,
           "Nothing could be mounted: none of the search paths of init_faf.lua exists. Is the game data in " + mDataRoot + "/scfa and " +
             mDataRoot + "/faf/gamedata?");
      return false;
    }
    return true;
  }

  bool Replay::ReadTrace()
  {
    Progress("trace", "Reading the trace", true);
    galtrace::Reader reader;
    std::string error;
    if (!reader.Open(mTracePath, &error)) {
      Fail(GALPLAY_EXIT_BAD_OPTIONS, "Cannot read the menu trace " + mTracePath + ": " + error);
      return false;
    }
    mTraceVersion = reader.Version();
    mMetadata = reader.GetMetadata();
    struct stat info{};
    if (::stat(mTracePath.c_str(), &info) == 0) {
      mTraceBytes = static_cast<std::uint64_t>(info.st_size);
    }
    const auto meta = [this](const char* const key) {
      for (const auto& [name, value] : mMetadata) {
        if (name == key) {
          return value;
        }
      }
      return std::string();
    };
    mFrames = static_cast<unsigned>(std::strtoul(meta(galtrace::kMetaPresents).c_str(), nullptr, 10));
    mReadbacksExpected = static_cast<unsigned>(std::strtoul(meta(galtrace::kMetaReadbacks).c_str(), nullptr, 10));
    if (mReadbacksExpected == 0) {
      const std::string frames = meta(galtrace::kMetaHarnessFrames);
      mReadbacksExpected = frames.empty() ? 0U : static_cast<unsigned>(std::count(frames.begin(), frames.end(), ',') + 1);
    }
    const double frameRate = std::strtod(meta(galtrace::kMetaFrameRate).c_str(), nullptr);
    if (frameRate > 0.0 && frameRate <= 240.0) {
      mFrameRate = frameRate;
    }
    // The game files the trace refers to, up front: what is missing is named before anything renders.
    if (mTraceVersion >= galtrace::kFormatVersion2) {
      if (!galtrace::ScanPayloadRefs(mTracePath, &mRefs, &error)) {
        Fail(GALPLAY_EXIT_BAD_OPTIONS, "Cannot read the menu trace's references: " + error);
        return false;
      }
      mRefArchives = galtrace::RefArchives(mRefs);
      galtrace::VfsResolver resolver(mVfs);
      std::vector<galtrace::PayloadRefProblem> problems;
      Progress("data", "Checking " + std::to_string(mRefs.size()) + " game files the trace uses", true);
      if (!galtrace::VerifyPayloadRefs(mRefs, resolver, &problems)) {
        Fail(GALPLAY_EXIT_MISSING_DATA, "The game data on this device does not have what the menu trace needs:\n" +
                                          galtrace::DescribeRefProblems(problems));
        return false;
      }
    }
    std::string archives;
    for (const std::string& archive : mRefArchives) {
      archives += (archives.empty() ? "" : ", ") + archive;
    }
    Log(1, "trace " + mTracePath + ": version " + std::to_string(mTraceVersion) + ", " + std::to_string(mTraceBytes) + " bytes, " +
             std::to_string(mFrames) + " frames at " + Number(mFrameRate) + " fps, " + std::to_string(mReadbacksExpected) + " readbacks, " +
             std::to_string(mRefs.size()) + " game files from " + (archives.empty() ? "-" : archives));
    return true;
  }

  void Replay::ConfigureCaches()
  {
    mCacheDir = mOptions.cacheDir != nullptr && mOptions.cacheDir[0] != '\0' ? mOptions.cacheDir : mDataRoot + "/localappdata/galplay-cache";
    dil::AndroidHostOptions host;
    host.forceCpuBcDecode = mOptions.forceCpuBcDecode != 0;
    if (mOptions.noShaderCache == 0 && port::fs::CreateDirectories(mCacheDir + "/spirv")) {
      mShaderCache = std::make_unique<FileShaderCache>(mCacheDir + "/spirv");
      mPipelineCachePath = mCacheDir + "/vk-pipeline-cache.bin";
      host.pipelineCachePath = mPipelineCachePath;
    }
    dil::SetAndroidHostOptions(host);
    dil::SetShaderBytecodeCache(mShaderCache.get());
  }

  void Replay::Progress(const char* const stage, const std::string& message, const bool force)
  {
    const Clock::time_point now = Clock::now();
    if (!force && MillisecondsBetween(mLastProgress, now) < 33.0) {
      return;
    }
    mLastProgress = now;
    if (mCallbacks.onProgress == nullptr) {
      return;
    }
    GalPlayProgress progress{};
    progress.stage = stage;
    progress.message = message.c_str();
    progress.frame = mPresents;
    progress.frames = mFrames;
    progress.frameMs = mLastFrameMs;
    progress.avgFrameMs =
      mPresents > 1 ? (MillisecondsBetween(mFirstPresent, mLastPresent) - mPausedMs) / static_cast<double>(mPresents - 1) : mLastFrameMs;
    progress.gpuFrameMs = mLastWorkMs;
    progress.readbacks = mReadbacksExpected;
    progress.readbacksDone = mPassed + mFailed;
    progress.readbacksPassed = mPassed;
    progress.readbacksFailed = mFailed;
    progress.fast = mOptions.fast;
    mCallbacks.onProgress(mCallbacks.user, &progress);
  }

  // The render thread takes a window the activity posted (a new one, or null before Android destroys
  // the old one), and tells the poster when the old swap chain is gone.
  void Replay::ApplyPendingWindow()
  {
    Session& session = TheSession();
    std::unique_lock lock(session.mutex);
    if (!session.windowPending) {
      return;
    }
    ANativeWindow* const window = session.window;
    if (window != nullptr) {
      ANativeWindow_acquire(window);
    }
    session.windowPending = false;
    const std::uint64_t ticket = session.windowTicket;
    lock.unlock();
    if (mHost != nullptr) {
      std::string error;
      if (!mHost->SetWindow(window, &error)) {
        Log(2, "window: " + error);
      } else if (window != nullptr) {
        mSurface = mHost->GetSurfaceDescription();
        mSwapChainFormat = mHost->GetSwapChainFormat();
        Log(1, "window attached: " + mSurface);
      } else {
        Log(1, "window released");
      }
    }
    if (window != nullptr) {
      ANativeWindow_release(window);
    }
    lock.lock();
    session.windowApplied = std::max(session.windowApplied, ticket);
    session.changed.notify_all();
  }

  bool Replay::AfterPresent(const unsigned presents)
  {
    const Clock::time_point now = Clock::now();
    mPresents = presents;
    if (presents == 1) {
      mFirstPresent = now;
      mNextDue = now;
      Log(1, "first frame " + Number(MillisecondsBetween(mStart, now)) + " ms after the start (device " +
               Number(MillisecondsBetween(mStart, mDeviceReady)) + " ms)");
    }
    mLastWorkMs = MillisecondsBetween(mWorkStart, now);
    mLastFrameMs = presents == 1 ? mLastWorkMs : MillisecondsBetween(mLastPresent, now);
    mLastPresent = now;
    ApplyPendingWindow();
    if (StopRequested()) {
      return false;
    }

    // Without a window (the activity is stopped) the replay waits for one; a headless replay never has one.
    if (mOptions.headless == 0 && mHost != nullptr && !mHost->HasSurface()) {
      const Clock::time_point pauseStart = Clock::now();
      Progress("replay", "Paused: no window", true);
      Session& session = TheSession();
      std::unique_lock lock(session.mutex);
      while (!session.stop.load() && !(session.windowPending && session.window != nullptr)) {
        session.changed.wait_for(lock, std::chrono::milliseconds(250));
        if (mCallbacks.shouldStop != nullptr && mCallbacks.shouldStop(mCallbacks.user) != 0) {
          break;
        }
      }
      lock.unlock();
      ApplyPendingWindow();
      mPausedMs += MillisecondsBetween(pauseStart, Clock::now());
      mNextDue = Clock::now();
      if (StopRequested()) {
        return false;
      }
    }

    // The recorded pace: one present per 1/frame_rate s; a late frame moves the schedule, it is not
    // made up for.
    if (mOptions.fast == 0) {
      const auto interval = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / mFrameRate));
      mNextDue += interval;
      const Clock::time_point current = Clock::now();
      if (current > mNextDue + interval) {
        mNextDue = current;
      } else if (current < mNextDue) {
        Session& session = TheSession();
        std::unique_lock lock(session.mutex);
        session.changed.wait_until(lock, mNextDue, [&session] {
          return session.stop.load() || session.windowPending;
        });
      }
    }
    Progress("replay", std::string(), presents == mFrames);
    mWorkStart = Clock::now();
    return !StopRequested();
  }

  void Replay::OnReadback(const faf::android::galplay::EngineReadback& readback)
  {
    FrameRecord record;
    record.frame = readback.frame;
    record.hash = readback.hash;
    record.reference = readback.reference;
    record.verdict = readback.verdict;
    record.identicalToRecording = readback.identicalToRecording;
    if (readback.verdict == "pass") {
      ++mPassed;
    } else {
      ++mFailed;
    }
    mFrameRecords.push_back(record);
    Log(1, "frame " + std::to_string(readback.frame) + ": " + readback.hash + " (reference " +
             (readback.reference.empty() ? std::string("none") : readback.reference) + ") " + readback.verdict);
    if (mCallbacks.onReadback != nullptr && readback.bgra != nullptr && readback.width != 0U && readback.rowBytes >= readback.width * 4U) {
      // B, G, R, A rows to R, G, B, A rows (what Android's Bitmap takes).
      mRgba.resize(static_cast<std::size_t>(readback.width) * readback.rows * 4U);
      for (std::uint32_t row = 0; row < readback.rows; ++row) {
        const std::uint8_t* from = readback.bgra + static_cast<std::size_t>(row) * readback.rowBytes;
        std::uint8_t* to = mRgba.data() + static_cast<std::size_t>(row) * readback.width * 4U;
        for (std::uint32_t x = 0; x < readback.width; ++x, from += 4, to += 4) {
          to[0] = from[2];
          to[1] = from[1];
          to[2] = from[0];
          to[3] = from[3];
        }
      }
      GalPlayReadback out{};
      out.index = readback.index;
      out.frame = readback.frame;
      out.width = readback.width;
      out.height = readback.rows;
      out.rgba = mRgba.data();
      out.hash = record.hash.c_str();
      out.reference = record.reference.c_str();
      out.verdict = record.verdict.c_str();
      out.identicalToRecording = record.identicalToRecording;
      mCallbacks.onReadback(mCallbacks.user, &out);
    }
    Progress("replay", std::string(), true);
  }

  void Replay::Replaying()
  {
    namespace gp = faf::android::galplay;
    galtrace::VfsResolver resolver(mVfs);
    gp::EngineRequest request;
    request.tracePath = mTracePath;
    request.resolver = &resolver;
    request.verifyReferencesFirst = false; // ReadTrace checked every reference already
    request.referenceBackend = "diligent:vk";
    if (!mRunDir.empty()) {
      request.backendArguments = {"/galreport", mRunDir + "/galreport.json"};
    }
    if (mOptions.validation == 0) {
      request.backendArguments.emplace_back("/galnovalidation");
    }
    if (mOptions.backendOptions != nullptr) {
      std::istringstream words(mOptions.backendOptions);
      for (std::string word; words >> word;) {
        request.backendArguments.push_back(word);
      }
    }
    if (request.backendArguments.size() > (mRunDir.empty() ? 0U : 2U) + (mOptions.validation == 0 ? 1U : 0U)) {
      std::string all;
      for (const std::string& argument : request.backendArguments) {
        all += (all.empty() ? "" : " ") + argument;
      }
      Log(1, "backend options: " + all);
    }
    const bool headless = mOptions.headless != 0;
    request.currentWindow = [headless] {
      Session& session = TheSession();
      const std::lock_guard lock(session.mutex);
      session.windowPending = false; // the device starts with this window
      session.windowApplied = session.windowTicket;
      session.hasDevice = true;
      session.changed.notify_all();
      return headless ? nullptr : static_cast<void*>(session.window);
    };
    request.onDevice = [this](dil::DiligentHost* const host, std::uint64_t) {
      mHost = host;
      if (host != nullptr) {
        mDeviceReady = Clock::now();
        mSupport = host->GetDeviceSupport();
        mAdapter = host->GetAdapterDescription();
        mSurface = host->GetSurfaceDescription();
        mSwapChainFormat = host->GetSwapChainFormat();
        Log(1, "device: " + mAdapter + ", Vulkan; BC " + (mSupport.cpuBcDecode ? "decoded on the CPU" : "sampled by the GPU") +
                 (mOptions.forceCpuBcDecode != 0 ? " (forced)" : "") + ", depth-stencil " + (mSupport.d32s8ForD24s8 ? "D32S8 (no D24S8)" : "D24S8") +
                 ", pipeline cache " + std::to_string(mSupport.pipelineCacheBytesLoaded) + " bytes loaded; sub-pixel/sub-texel bits " +
                 std::to_string(mSupport.subPixelPrecisionBits) + "/" + std::to_string(mSupport.subTexelPrecisionBits) + "; surface " +
                 (mSurface.empty() ? std::string("none yet") : mSurface));
        Progress("shaders", "Compiling shaders", true);
      }
    };
    request.onDeviceError = [this](const std::string& message) {
      Fail(GALPLAY_EXIT_NO_DEVICE, "No usable Vulkan device: " + message);
    };
    request.afterPresent = [this](const unsigned presents) {
      return AfterPresent(presents);
    };
    request.shouldStop = [this] {
      return StopRequested();
    };
    request.onReadback = [this](const gp::EngineReadback& readback) {
      OnReadback(readback);
    };
    request.log = [this](const int level, const std::string& line) {
      Log(level, line);
    };
    request.holdDevice = true;

    mWorkStart = Clock::now();
    Progress("device", "Creating the Vulkan device", true);
    mOutcome = gp::RunEngineReplay(request);
    mHost = gp::HeldHost();
    if (mHost != nullptr) {
      // Persist the pipeline cache now: a killed process later loses nothing.
      std::string error;
      mPipelineCacheSaved = mHost->SavePipelineCache(&error);
      if (!error.empty()) {
        Log(2, "pipeline cache: " + error);
      }
    }
  }

  // After the replay: the last frame stays on screen (presented again on a new window) until stop.
  void Replay::Hold()
  {
    Session& session = TheSession();
    while (!StopRequested()) {
      ApplyPendingWindow();
      if (mHost != nullptr && mHost->HasSurface()) {
        mHost->Present(1);
      }
      std::unique_lock lock(session.mutex);
      session.changed.wait_for(lock, std::chrono::milliseconds(500), [&session] {
        return session.stop.load() || session.windowPending;
      });
    }
  }

  std::string Replay::BuildJson() const
  {
    const dil::ShaderCompileCounts compile = dil::GetShaderCompileCounts();
    std::string frames;
    for (const FrameRecord& record : mFrameRecords) {
      frames += std::string(frames.empty() ? "" : ",\n    ") + "{\"frame\": " + std::to_string(record.frame) + ", \"hash\": " + Quote(record.hash) +
                ", \"reference\": " + Quote(record.reference) + ", \"verdict\": " + Quote(record.verdict) +
                ", \"identicalToRecording\": " + (record.identicalToRecording < 0 ? std::string("null") : record.identicalToRecording ? "true" : "false") +
                "}";
    }
    std::string archives;
    for (const std::string& archive : mRefArchives) {
      archives += std::string(archives.empty() ? "" : ", ") + Quote(archive);
    }
    const double replaySeconds = mOutcome.seconds;
    const double avgFrameMs =
      mPresents > 1 ? (MillisecondsBetween(mFirstPresent, mLastPresent) - mPausedMs) / static_cast<double>(mPresents - 1) : 0.0;
    std::string json = "{\n";
    json += "  \"schema\": 1,\n";
    json += "  \"result\": " + Quote(mResultWord) + ",\n";
    json += "  \"exitCode\": " + std::to_string(mExitCode) + ",\n";
    json += "  \"message\": " + Quote(mMessage) + ",\n";
    json += "  \"fast\": " + std::string(mOptions.fast != 0 ? "true" : "false") + ",\n";
    json += "  \"frames\": " + std::to_string(mPresents) + ",\n";
    json += "  \"framesInTrace\": " + std::to_string(mFrames) + ",\n";
    json += "  \"readbacks\": {\"expected\": " + std::to_string(mReadbacksExpected) + ", \"done\": " + std::to_string(mFrameRecords.size()) +
            ", \"passed\": " + std::to_string(mPassed) + ", \"differs\": " + std::to_string(mFailed) + "},\n";
    json += "  \"frameHashes\": [\n    " + frames + "\n  ],\n";
    json += "  \"trace\": {\"path\": " + Quote(mTracePath) + ", \"version\": " + std::to_string(mTraceVersion) + ", \"bytes\": " +
            std::to_string(mTraceBytes) + ", \"frameRate\": " + Number(mFrameRate) + ", \"references\": " + std::to_string(mRefs.size()) +
            ", \"archives\": [" + archives + "]},\n";
    json += "  \"data\": {\"root\": " + Quote(mDataRoot) + ", \"mounts\": " + std::to_string(mMounts) + ", \"scriptMs\": " + Number(mScriptMs) +
            ", \"mountMs\": " + Number(mMountMs) + "},\n";
    json += "  \"device\": {\"api\": \"vulkan\", \"adapter\": " + Quote(mAdapter) + ", \"surface\": " + Quote(mSurface) +
            ", \"swapChainFormat\": " + Quote(mSwapChainFormat) + ", \"bcFeature\": " + (mSupport.bcFeature ? "true" : "false") +
            ", \"bcSampled\": " + (mSupport.bcSampled ? "true" : "false") + ", \"bc\": " + Quote(mSupport.cpuBcDecode ? "cpu" : "gpu") +
            ", \"bcForced\": " + (mOptions.forceCpuBcDecode != 0 ? "true" : "false") + ", \"d24s8Attachment\": " +
            (mSupport.d24s8Attachment ? "true" : "false") + ", \"d32s8Attachment\": " + (mSupport.d32s8Attachment ? "true" : "false") +
            ", \"depthStencil\": " + Quote(mSupport.d32s8ForD24s8 ? "D32S8" : "D24S8") + ", \"subPixelPrecisionBits\": " +
            std::to_string(mSupport.subPixelPrecisionBits) + ", \"subTexelPrecisionBits\": " + std::to_string(mSupport.subTexelPrecisionBits) +
            ", \"mipmapPrecisionBits\": " + std::to_string(mSupport.mipmapPrecisionBits) + "},\n";
    json += "  \"timings\": {\"dataMs\": " + Number(MillisecondsBetween(mStart, mDataDone)) + ", \"deviceMs\": " +
            Number(mDeviceReady > mStart ? MillisecondsBetween(mStart, mDeviceReady) : 0.0) + ", \"firstFrameMs\": " +
            Number(mPresents > 0 ? MillisecondsBetween(mStart, mFirstPresent) : 0.0) + ", \"replaySeconds\": " + Number(replaySeconds, 3) +
            ", \"avgFrameMs\": " + Number(avgFrameMs, 2) + ", \"pausedMs\": " + Number(mPausedMs) + ", \"shadersCompiled\": " +
            std::to_string(compile.compiled) + ", \"shaderCompileMs\": " + Number(compile.compileMilliseconds) + ", \"shaderCacheHits\": " +
            std::to_string(compile.cacheHits) + ", \"shaderCacheMs\": " + Number(compile.cacheMilliseconds) + ", \"shaderCacheStores\": " +
            std::to_string(compile.cacheStores) + ", \"shaderCompileFailures\": " + std::to_string(compile.failed) + "},\n";
    json += "  \"caches\": {\"dir\": " + Quote(mCacheDir) + ", \"enabled\": " + (mShaderCache ? "true" : "false") + ", \"pipelineCache\": " +
            Quote(mPipelineCachePath) + ", \"pipelineCacheLoadedBytes\": " + std::to_string(mSupport.pipelineCacheBytesLoaded) +
            ", \"pipelineCacheSavedBytes\": " + std::to_string(mPipelineCacheSaved) + ", \"spirvLoaded\": " +
            std::to_string(mShaderCache ? mShaderCache->Loaded() : 0U) + ", \"spirvStored\": " + std::to_string(mShaderCache ? mShaderCache->Stored() : 0U) +
            "},\n";
    json += "  \"replay\": {\"completed\": " + std::string(mOutcome.completed ? "true" : "false") + ", \"stopped\": " +
            (mOutcome.stopped ? "true" : "false") + ", \"missingData\": " + (mOutcome.missingData ? "true" : "false") + ", \"fatal\": " +
            Quote(mOutcome.fatal) + ", \"records\": " + std::to_string(mOutcome.records) + ", \"calls\": " + std::to_string(mOutcome.calls) +
            ", \"draws\": " + std::to_string(mOutcome.draws) + ", \"galErrors\": " + std::to_string(mOutcome.galErrors) + ", \"references\": " +
            std::to_string(mOutcome.references) + ", \"providedPayloads\": " + std::to_string(mOutcome.providedPayloads) + "},\n";
    json += "  \"player\": " + (mOutcome.playerJson.empty() ? std::string("null") : mOutcome.playerJson) + "\n";
    json += "}\n";
    return json;
  }

  int Replay::Finish(GalPlayResult* const result)
  {
    if (mExitCode == GALPLAY_EXIT_PASS) {
      if (mOutcome.missingData) {
        mExitCode = GALPLAY_EXIT_MISSING_DATA;
        mMessage = mOutcome.fatal;
      } else if (!mOutcome.completed) {
        mExitCode = mOutcome.stopped || TheSession().stop.load() ? GALPLAY_EXIT_STOPPED : GALPLAY_EXIT_INTERNAL;
        mMessage = mOutcome.stopped ? "Stopped after " + std::to_string(mPresents) + " of " + std::to_string(mFrames) + " frames."
                                    : "The replay stopped: " + mOutcome.fatal;
      } else if (mFailed != 0 || mFrameRecords.size() < mReadbacksExpected) {
        mExitCode = GALPLAY_EXIT_DIFFERS;
        mMessage = std::to_string(mPassed) + " of " + std::to_string(mFrameRecords.size()) +
                   " frames equal the PC's Vulkan frames; the others differ (the PNGs in the zip show how).";
      } else {
        mMessage = "All " + std::to_string(mPassed) + " frames equal the PC's Vulkan frames.";
      }
    }
    switch (mExitCode) {
    case GALPLAY_EXIT_PASS:
      mResultWord = "PASS";
      break;
    case GALPLAY_EXIT_DIFFERS:
      mResultWord = "DIFFERS";
      break;
    case GALPLAY_EXIT_STOPPED:
      mResultWord = "INCOMPLETE";
      break;
    case GALPLAY_EXIT_MISSING_DATA:
      mResultWord = "MISSING_DATA";
      break;
    default:
      mResultWord = "ERROR";
      break;
    }
    const std::string json = BuildJson();
    if (!mRunDir.empty() && !WriteFileAtomically(mRunDir + "/galplay.json", json)) {
      Log(2, "cannot write " + mRunDir + "/galplay.json");
    }
    Log(1, "result " + mResultWord + ": " + mMessage);
    Progress(mExitCode == GALPLAY_EXIT_PASS || mExitCode == GALPLAY_EXIT_DIFFERS ? "done" : "error", mMessage, true);
    if (mCallbacks.onFinished != nullptr) {
      mCallbacks.onFinished(mCallbacks.user, json.c_str());
    }
    if (result != nullptr) {
      result->exitCode = mExitCode;
      result->frames = mPresents;
      result->readbacks = static_cast<std::uint32_t>(mFrameRecords.size());
      result->passed = mPassed;
      result->failed = mFailed;
      result->replaySeconds = mOutcome.seconds;
      std::snprintf(result->message, sizeof(result->message), "%s", mMessage.c_str());
    }
    return mExitCode;
  }

  int Replay::Run(GalPlayResult* const result)
  {
    mStart = Clock::now();
    mLastProgress = mStart - std::chrono::seconds(1);
    mWorkStart = mStart;
    mRunDir = mOptions.runDir != nullptr ? mOptions.runDir : "";
    if (!mRunDir.empty()) {
      port::fs::CreateDirectories(mRunDir);
      mLog.Open(mRunDir + "/galplay.log");
    }
    Log(1, "menu replay of " + mTracePath + " (" + (mOptions.fast != 0 ? "as fast as possible" : "recorded pace") + ")");
    try {
      if (MountData() && !StopRequested() && ReadTrace() && !StopRequested()) {
        mDataDone = Clock::now();
        ConfigureCaches();
        Replaying();
      } else if (StopRequested() && mExitCode == GALPLAY_EXIT_PASS) {
        mExitCode = GALPLAY_EXIT_STOPPED;
        mMessage = "Stopped before the replay started.";
      }
    } catch (const std::exception& error) {
      Fail(GALPLAY_EXIT_INTERNAL, std::string("internal error: ") + error.what());
    }
    if (mDataDone == Clock::time_point{}) {
      mDataDone = Clock::now();
    }
    const int code = Finish(result);
    // Keep the last frame on screen until the activity ends the session.
    if (mOutcome.completed && mOptions.headless == 0) {
      Hold();
    }
    faf::android::galplay::ReleaseHeldDevice();
    mHost = nullptr;
    dil::SetShaderBytecodeCache(nullptr);
    mLog.Close();
    return code;
  }

} // namespace

namespace {

  /** galplay.json for a replay that could not start (bad arguments, another replay running). */
  std::string EarlyResultJson(const int code, const std::string& message)
  {
    return "{\n  \"schema\": 1,\n  \"result\": \"ERROR\",\n  \"exitCode\": " + std::to_string(code) + ",\n  \"message\": " + Quote(message) +
           ",\n  \"frames\": 0,\n  \"frameHashes\": []\n}\n";
  }

} // namespace

extern "C" int galplay_run(
  ANativeWindow* const window,
  const char* const tracePath,
  const char* const dataRoot,
  const GalPlayOptions* const options,
  const GalPlayCallbacks* const callbacks,
  GalPlayResult* const result
)
{
  // Every way out reports through onFinished, so the activity always learns how the replay ended.
  const auto refuse = [&](const int code, const std::string& message) {
    if (result != nullptr) {
      *result = GalPlayResult{};
      result->exitCode = code;
      std::snprintf(result->message, sizeof(result->message), "%s", message.c_str());
    }
    if (callbacks != nullptr && callbacks->onFinished != nullptr) {
      callbacks->onFinished(callbacks->user, EarlyResultJson(code, message).c_str());
    }
    return code;
  };
  Session& session = TheSession();
  {
    const std::lock_guard lock(session.mutex);
    if (session.running) {
      return refuse(GALPLAY_EXIT_BAD_OPTIONS, "A menu replay is already running in this process.");
    }
    session.running = true;
    session.stop.store(false);
    session.hasDevice = false;
    session.windowPending = false;
    // The window: this call's, or the one galplay_set_window handed over before the replay started.
    if (window != nullptr && window != session.window) {
      ANativeWindow_acquire(window);
      if (session.window != nullptr) {
        ANativeWindow_release(session.window);
      }
      session.window = window;
    }
  }
  GalPlayOptions effective{};
  effective.structSize = sizeof(GalPlayOptions);
  if (options != nullptr) {
    std::memcpy(&effective, options, std::min<std::size_t>(sizeof(GalPlayOptions), options->structSize != 0U ? options->structSize : sizeof(GalPlayOptions)));
  }
  int code = GALPLAY_EXIT_BAD_OPTIONS;
  if (tracePath == nullptr || tracePath[0] == '\0' || dataRoot == nullptr || dataRoot[0] == '\0') {
    code = refuse(GALPLAY_EXIT_BAD_OPTIONS, tracePath == nullptr || tracePath[0] == '\0' ? "No menu trace was given (the launcher extracts it from the APK)."
                                                                                          : "No data root was given.");
  } else {
    Replay replay(tracePath, dataRoot, effective, callbacks);
    code = replay.Run(result);
  }
  const std::lock_guard lock(session.mutex);
  session.running = false;
  session.hasDevice = false;
  if (session.window != nullptr) {
    ANativeWindow_release(session.window);
    session.window = nullptr;
  }
  session.windowPending = false;
  session.windowApplied = session.windowTicket;
  session.changed.notify_all();
  return code;
}

extern "C" int galplay_set_window(ANativeWindow* const window, const int timeoutMs)
{
  Session& session = TheSession();
  std::unique_lock lock(session.mutex);
  if (window != nullptr) {
    ANativeWindow_acquire(window);
  }
  if (session.window != nullptr) {
    ANativeWindow_release(session.window);
  }
  session.window = window;
  if (!session.running) {
    // Kept for a replay that is about to start (galplay_run takes it); released when it ends.
    return 1;
  }
  if (!session.hasDevice) {
    // No device yet: it will start with whatever window is current then.
    return 1;
  }
  session.windowPending = true;
  const std::uint64_t ticket = ++session.windowTicket;
  session.changed.notify_all();
  if (window != nullptr) {
    return 1; // a new window is taken at the next frame
  }
  // The old window goes away when this returns: wait until the render thread dropped its swap chain.
  const bool applied = session.changed.wait_for(lock, std::chrono::milliseconds(std::max(timeoutMs, 0)), [&session, ticket] {
    return session.windowApplied >= ticket || !session.running;
  });
  return applied ? 1 : 0;
}

extern "C" void galplay_request_stop(void)
{
  Session& session = TheSession();
  session.stop.store(true);
  const std::lock_guard lock(session.mutex);
  session.changed.notify_all();
}
