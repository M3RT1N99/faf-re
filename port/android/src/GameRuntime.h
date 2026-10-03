#pragma once

// The data bring-up on a worker thread, so the main thread keeps answering
// Android (lifecycle commands, input) and drawing the loading screen:
//
//   datapath  run the data-path script (init_faf.lua) the way the engine does,
//   mount     mount every `path` entry in order into a VirtualFileSystem,
//   splash    read the first main-menu background that exists and decode it
//             to RGBA8.
//
// Each stage is written to status.json as it starts, the counters as they are
// known. A failure is written as state=error by the worker itself, so the
// launcher sees it even if the process dies before the main thread reacts.
//
// The state the worker touches is shared (shared_ptr), not owned by this
// object: if Android destroys the activity while the script is still running,
// Shutdown() stops waiting after a while and leaves the worker to finish on
// its own instead of blocking the UI thread into an ANR.

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "faf/port/DataPath.h"
#include "faf/port/Image.h"
#include "RunStatus.h"

namespace faf::android {

  struct GameRuntimeConfig
  {
    std::string root;       ///< Data root (logs, launch, localappdata, documents live here).
    std::string initScript; ///< Absolute path of the data-path script.
    faf::port::KnownFolders folders;
    /// The device runs with writes allowed (init_faf.lua clears its shader
    /// cache with os.remove); host checks against a real install turn it off.
    bool allowWrites = true;
  };

  /// What the main thread needs to know each frame.
  struct LoadProgress
  {
    Stage stage = Stage::Args;
    float fraction = 0.0f; ///< 0..1, for the progress bar.
    bool done = false;     ///< The splash is decoded and can be taken.
    bool failed = false;   ///< status.json holds state=error; `error` says why.
    std::string error;
  };

  /// The decoded main-menu background and where it came from.
  struct SplashImage
  {
    std::string vfsPath;
    faf::port::Image image;
  };

  class GameRuntime
  {
  public:
    /// `wake` is called (from the worker) when the stage changes, on success
    /// and on failure, so a main loop that blocks while invisible still reacts.
    /// It must be cheap and must not call back into this object.
    GameRuntime(GameRuntimeConfig config, std::shared_ptr<RunStatus> status, std::function<void()> wake);
    ~GameRuntime();

    GameRuntime(const GameRuntime&) = delete;
    GameRuntime& operator=(const GameRuntime&) = delete;

    void Start();

    [[nodiscard]] LoadProgress Progress() const;

    /// The splash once `done`; the first call takes it, later calls return nullopt.
    [[nodiscard]] std::optional<SplashImage> TakeSplash();

    /// Asks the worker to stop at the next mount, disables `wake` and waits up
    /// to `timeout` for it to finish; detaches it after that.
    void Shutdown(std::chrono::milliseconds timeout);

    /// State shared with the worker thread (defined in GameRuntime.cpp).
    struct Shared;

  private:
    std::shared_ptr<Shared> mShared;
    std::thread mThread;
  };

} // namespace faf::android
