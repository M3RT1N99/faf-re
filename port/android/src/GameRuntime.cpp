#include "GameRuntime.h"

#include <pthread.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <mutex>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "faf/port/FileSystem.h"
#include "faf/port/Vfs.h"
#include "Log.h"

namespace faf::android {

  namespace {

    namespace port = faf::port;

    /// Main-menu backgrounds in order of preference (the launch contract):
    /// FAF's menu background, the loading-screen portal, SCFA's default
    /// background. The first two are in textures.scd (FAF's textures.nx2 may
    /// override them), the last one in env.scd.
    constexpr std::array<std::string_view, 3> kSplashCandidates = {
      "/textures/ui/common/menus02/background-paint01_bmp.dds",
      "/textures/ui/common/load/background-portal_bmp.dds",
      "/textures/environment/DefaultBackground.dds",
    };

    // Rough share of the loading time per stage, for the progress bar. On a
    // PC the script and the mounts take about the same time; decoding the
    // splash is quick.
    constexpr float kDataPathStart = 0.05f;
    constexpr float kMountStart = 0.40f;
    constexpr float kSplashStart = 0.92f;

    double MillisecondsSince(const std::chrono::steady_clock::time_point start)
    {
      return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }

    std::string_view LeafName(const std::string_view path)
    {
      const std::size_t slash = path.find_last_of('/');
      return slash == std::string_view::npos ? path : path.substr(slash + 1);
    }

    /// Thrown inside the worker to end the bring-up; the message is for the user.
    struct StageFailure
    {
      Stage stage;
      std::string message;
    };

  } // namespace

  struct GameRuntime::Shared
  {
    GameRuntimeConfig config;
    std::shared_ptr<RunStatus> status;

    mutable std::mutex mutex;
    std::condition_variable finishedChanged;
    std::function<void()> wake;        ///< Guarded by `mutex`; cleared by Shutdown.
    LoadProgress progress;             ///< Guarded by `mutex`.
    std::optional<SplashImage> splash; ///< Guarded by `mutex`.
    bool finished = false;             ///< Guarded by `mutex`: the worker returned.
    std::atomic<bool> cancel{false};

    /// Must hold `mutex`. Calling under the lock keeps Shutdown from clearing
    /// the callback (and its looper going away) while it runs.
    void WakeLocked() const
    {
      if (wake) {
        wake();
      }
    }

    void EnterStage(const Stage stage, const float fraction)
    {
      status->Update([stage](RunStatusData& data) {
        data.stage = stage;
      });
      const std::lock_guard lock(mutex);
      progress.stage = stage;
      progress.fraction = fraction;
      WakeLocked();
    }

    void SetFraction(const float fraction)
    {
      const std::lock_guard lock(mutex);
      progress.fraction = fraction;
    }

    void Fail(const Stage stage, std::string message)
    {
      LogError("{} failed: {}", ToString(stage), message);
      status->Finish(RunState::Error, stage, message);
      const std::lock_guard lock(mutex);
      progress.stage = stage;
      progress.failed = true;
      progress.error = std::move(message);
      WakeLocked();
    }

    void Succeed(SplashImage image)
    {
      const std::lock_guard lock(mutex);
      progress.fraction = 1.0f;
      progress.done = true;
      splash = std::move(image);
      WakeLocked();
    }

    [[nodiscard]] Stage CurrentStage() const
    {
      const std::lock_guard lock(mutex);
      return progress.stage;
    }
  };

  namespace {

    port::DataPathResult RunScript(GameRuntime::Shared& shared)
    {
      const std::string& script = shared.config.initScript;
      shared.EnterStage(Stage::DataPath, kDataPathStart);

      const std::optional<std::string> resolved = port::fs::ResolveCaseInsensitive(script);
      if (!resolved || !port::fs::IsRegularFile(*resolved)) {
        throw StageFailure{
          Stage::DataPath,
          "The data-path script " + script +
            " does not exist. Get the FAF files in the launcher (Download FAF files) or deploy them from the PC.",
        };
      }
      LogInfo("datapath: running {}", *resolved);

      port::DataPathOptions options;
      options.scriptPath = *resolved;
      options.folders = shared.config.folders;
      options.allowWrites = shared.config.allowWrites;
      options.log = [](const std::string_view line) {
        Log::Get().ScriptLine(line);
      };
      options.trace = [](const std::string_view line) {
        Log::Get().Write(LogLevel::Debug, line);
      };
      port::DataPathResult result = port::RunDataPathScript(options);
      if (!result.ok) {
        throw StageFailure{Stage::DataPath, std::string(LeafName(*resolved)) + " failed: " + result.error};
      }
      LogInfo(
        "datapath: {} path entries, {} hooks, {} protocols, {} LOG lines in {:.1f} ms (LaunchDir {}, InitFileDir {})",
        result.mounts.size(),
        result.hooks.size(),
        result.protocols.size(),
        result.logLines,
        result.scriptMilliseconds,
        result.launchDir,
        result.initFileDir
      );
      const double scriptMs = result.scriptMilliseconds;
      shared.status->Update([scriptMs](RunStatusData& data) {
        data.scriptMs = scriptMs;
      });
      if (result.mounts.empty()) {
        throw StageFailure{
          Stage::DataPath,
          std::string(LeafName(*resolved)) + " ran but defined no search paths (its `path` table is empty).",
        };
      }
      return result;
    }

    void MountAll(GameRuntime::Shared& shared, const port::DataPathResult& script, port::VirtualFileSystem& vfs)
    {
      shared.EnterStage(Stage::Mount, kMountStart);
      const auto start = std::chrono::steady_clock::now();
      std::size_t skipped = 0;
      std::size_t failed = 0;
      const std::size_t total = script.mounts.size();
      for (std::size_t i = 0; i < total; ++i) {
        if (shared.cancel.load(std::memory_order_relaxed)) {
          LogInfo("mount: cancelled after {} of {} path entries", i, total);
          return;
        }
        const port::MountSpec& spec = script.mounts[i];
        std::string error;
        const std::size_t added = vfs.Mount(spec, &error);
        if (!error.empty()) {
          // The engine skips a search path it cannot open as well; a damaged
          // archive (an interrupted copy) is reported but does not stop the run.
          ++failed;
          LogWarning("mount: {} -> {}: {}", spec.dir, spec.mountpoint, error);
        } else if (added == 0) {
          ++skipped;
        }
        shared.SetFraction(
          kMountStart + (kSplashStart - kMountStart) * static_cast<float>(i + 1) / static_cast<float>(total)
        );
      }
      const double mountMs = MillisecondsSince(start);

      std::uint64_t archives = 0;
      std::uint64_t entries = 0;
      for (const port::MountInfo& mount : vfs.Mounts()) {
        if (mount.isArchive) {
          ++archives;
          entries += mount.entryCount;
          LogInfo("mount: {} -> {} ({} entries)", mount.diskPath, mount.mountpoint, mount.entryCount);
        }
      }
      const std::uint64_t mounts = vfs.Mounts().size();
      LogInfo(
        "mount: {} mounts ({} archives, {} directories, {} archive entries) from {} path entries in {:.1f} ms; "
        "{} path entries missing on disk, {} failed",
        mounts,
        archives,
        mounts - archives,
        entries,
        total,
        mountMs,
        skipped,
        failed
      );
      shared.status->Update([&](RunStatusData& data) {
        data.mounts = mounts;
        data.archives = archives;
        data.archiveEntries = entries;
        data.mountMs = mountMs;
      });
      if (mounts == 0) {
        throw StageFailure{
          Stage::Mount,
          "Nothing could be mounted: none of the " + std::to_string(total) + " search paths of " +
            std::string(LeafName(shared.config.initScript)) + " exists on the device. Is the game data in " +
            shared.config.root + "/scfa and " + shared.config.root + "/faf/gamedata?",
        };
      }
    }

    SplashImage LoadSplash(GameRuntime::Shared& shared, const port::VirtualFileSystem& vfs)
    {
      shared.EnterStage(Stage::Splash, kSplashStart);
      std::string tried;
      for (const std::string_view candidate : kSplashCandidates) {
        if (shared.cancel.load(std::memory_order_relaxed)) {
          throw StageFailure{Stage::Splash, "cancelled"};
        }
        if (!tried.empty()) {
          tried.append("; ");
        }
        tried.append(candidate);

        const port::FileLocation location = vfs.Find(candidate);
        if (!location.found) {
          tried.append(" (not found)");
          continue;
        }
        const std::string_view source = LeafName(vfs.Mounts()[location.mountIndex].diskPath);
        const auto start = std::chrono::steady_clock::now();
        std::vector<std::uint8_t> bytes;
        std::string error;
        if (!vfs.Read(candidate, bytes, &error)) {
          LogWarning("splash: cannot read {} from {}: {}", candidate, source, error);
          tried.append(" (unreadable: " + error + ")");
          continue;
        }
        SplashImage splash;
        splash.vfsPath = std::string(candidate);
        if (!port::DecodeDds(bytes.data(), bytes.size(), splash.image, &error)) {
          LogWarning("splash: cannot decode {} from {}: {}", candidate, source, error);
          tried.append(" (not a usable DDS: " + error + ")");
          continue;
        }
        const port::Image& image = splash.image;
        if (image.width <= 0 || image.height <= 0 ||
            image.rgba.size() != static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4) {
          tried.append(" (decoded to an empty image)");
          continue;
        }
        LogInfo(
          "splash: {} from {}: {} {}x{}, {} bytes, read+decode {:.1f} ms",
          candidate,
          source,
          image.format,
          image.width,
          image.height,
          bytes.size(),
          MillisecondsSince(start)
        );
        shared.status->Update([&](RunStatusData& data) {
          data.splash = splash.vfsPath;
          data.splashFormat = image.format;
          data.width = image.width;
          data.height = image.height;
        });
        return splash;
      }
      throw StageFailure{
        Stage::Splash,
        "No main menu background could be read (" + tried +
          "). They come from SCFA's gamedata/textures.scd and FAF's textures.nx2.",
      };
    }

    void RunWorker(const std::shared_ptr<GameRuntime::Shared>& shared)
    {
      pthread_setname_np(pthread_self(), "faf-data");
      const auto start = std::chrono::steady_clock::now();
      try {
        const port::DataPathResult script = RunScript(*shared);
        port::VirtualFileSystem vfs;
        MountAll(*shared, script, vfs);
        if (!shared->cancel.load(std::memory_order_relaxed)) {
          SplashImage splash = LoadSplash(*shared, vfs);
          LogInfo("data bring-up finished in {:.1f} ms", MillisecondsSince(start));
          shared->Succeed(std::move(splash));
        }
        // The VFS (and its open archives) is not needed past the splash in
        // this milestone; it closes here.
      } catch (const StageFailure& failure) {
        shared->Fail(failure.stage, failure.message);
      } catch (const std::exception& e) {
        shared->Fail(shared->CurrentStage(), std::string("internal error: ") + e.what());
      } catch (...) {
        shared->Fail(shared->CurrentStage(), "internal error (unknown exception)");
      }
      {
        const std::lock_guard lock(shared->mutex);
        shared->finished = true;
      }
      shared->finishedChanged.notify_all();
    }

  } // namespace

  GameRuntime::GameRuntime(GameRuntimeConfig config, std::shared_ptr<RunStatus> status, std::function<void()> wake)
    : mShared(std::make_shared<Shared>())
  {
    mShared->config = std::move(config);
    mShared->status = std::move(status);
    mShared->wake = std::move(wake);
  }

  GameRuntime::~GameRuntime()
  {
    Shutdown(std::chrono::milliseconds(0));
  }

  void GameRuntime::Start()
  {
    if (mThread.joinable()) {
      return;
    }
    try {
      mThread = std::thread([shared = mShared] {
        RunWorker(shared);
      });
    } catch (const std::system_error& e) {
      mShared->Fail(Stage::DataPath, std::string("cannot start the loader thread: ") + e.what());
    }
  }

  LoadProgress GameRuntime::Progress() const
  {
    const std::lock_guard lock(mShared->mutex);
    return mShared->progress;
  }

  std::optional<SplashImage> GameRuntime::TakeSplash()
  {
    const std::lock_guard lock(mShared->mutex);
    std::optional<SplashImage> splash = std::move(mShared->splash);
    mShared->splash.reset();
    return splash;
  }

  void GameRuntime::Shutdown(const std::chrono::milliseconds timeout)
  {
    mShared->cancel.store(true, std::memory_order_relaxed);
    std::unique_lock lock(mShared->mutex);
    mShared->wake = nullptr;
    if (!mThread.joinable()) {
      return;
    }
    const bool finished = mShared->finishedChanged.wait_for(lock, timeout, [this] {
      return mShared->finished;
    });
    lock.unlock();
    if (finished) {
      mThread.join();
    } else {
      LogWarning("data bring-up still running at shutdown; it finishes in the background");
      mThread.detach();
    }
  }

} // namespace faf::android
