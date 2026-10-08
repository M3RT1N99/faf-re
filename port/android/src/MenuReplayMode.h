#pragma once

// GameActivity's mode "menu-replay" (release 0.5.0): the native side of the launcher's "Menu replay"
// (GalPlay.h has the Java contract). AndroidMain's App hands this mode the activity, the window
// changes and the input it acts on; MenuReplayMode runs galplay_run on a thread of its own (the
// replay's render thread), calls GameActivity's onMenuReplay* methods from it and keeps
// <root>/launch/status.json current.

#include <android/native_activity.h>
#include <android/native_window.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "AndroidArgs.h"
#include "RunStatus.h"

namespace faf::android {

  struct MenuReplayConfig
  {
    std::string root;        ///< the data root (externalDataPath)
    std::string initScript;  ///< init_faf.lua (LaunchOptions::initScript)
    std::string cacheDir;    ///< SPIR-V and pipeline caches (the app's internal storage)
    MenuReplayExtras extras; ///< the Intent's menuReplay.* extras
  };

  class MenuReplayMode
  {
  public:
    /// `wake` is called (on the replay thread) when the replay has finished, so the activity's loop
    /// can react; `status` is the run's status.json.
    MenuReplayMode(ANativeActivity* activity, MenuReplayConfig config, std::shared_ptr<RunStatus> status, std::function<void()> wake);
    ~MenuReplayMode();

    MenuReplayMode(const MenuReplayMode&) = delete;
    MenuReplayMode& operator=(const MenuReplayMode&) = delete;

    /// Starts the replay thread (once).
    void Start(ANativeWindow* window);

    /// APP_CMD_INIT_WINDOW, a resize or a configuration change: the (same or new) window.
    void SetWindow(ANativeWindow* window);
    /// APP_CMD_TERM_WINDOW: returns once the replay no longer uses the window (or after a timeout).
    void ReleaseWindow();

    /// Back, or the activity going away: stops a running replay (at its next record).
    void RequestStop();
    /// True once the replay reported its result (onMenuReplayFinished): a completed replay then holds
    /// its last frame on screen, a failed one has stopped.
    [[nodiscard]] bool ResultReported() const { return mResultReported.load(); }
    /// True once galplay_run has returned (the replay ended, failed, or was stopped).
    [[nodiscard]] bool Finished() const { return mFinished.load(); }
    /// True when the replay ended with an error that status.json already holds.
    [[nodiscard]] bool Failed() const { return mFailed.load(); }

    /// Stops the replay and joins its thread. Called before android_main returns.
    void Shutdown();

  private:
    void ThreadMain();

    ANativeActivity* const mActivity;
    const MenuReplayConfig mConfig;
    std::shared_ptr<RunStatus> mStatus;
    std::function<void()> mWake;
    std::thread mThread;
    std::atomic<bool> mStop{false};
    std::atomic<bool> mFinished{false};
    std::atomic<bool> mFailed{false};
    std::atomic<bool> mResultReported{false};
  };

} // namespace faf::android
