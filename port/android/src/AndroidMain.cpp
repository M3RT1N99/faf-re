// android_main: the native side of GameActivity (android.app.NativeActivity,
// android.app.lib_name = faf_android), driven by the NDK's
// android_native_app_glue. What it does for milestone M1:
//
//  1. read the data root (externalDataPath) and the "argv" extra, open
//     <root>/logs/faf_android_<vulkan|gles>.log and write status.json (stage args),
//  2. start GameRuntime: run init_faf.lua, mount the data, decode the
//     main-menu background (worker thread, status per stage),
//  3. create the renderer on the first window (Vulkan, else OpenGL ES) and
//     draw a loading screen until the background is ready, then the
//     background (state running, stage frame),
//  4. back key, or a tap once the background is shown, finishes the activity
//     (state exited). Any failure writes state=error with the reason and
//     finishes the activity too; the launcher shows the reason.
//
// Lifecycle rules of the glue that shape the loop below: the UI thread blocks
// in onNativeWindowDestroyed until APP_CMD_TERM_WINDOW is processed, and in
// onDestroy until android_main has returned, so commands are handled
// promptly, the window is released inside the TERM_WINDOW handler, and
// android_main only returns after APP_CMD_DESTROY. Returning earlier would
// leave a frozen activity behind.

#include <android/configuration.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/looper.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

#include <chrono>
#include <cmath>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "AndroidArgs.h"
#include "CrashHandler.h"
#include "faf/port/CommandLine.h"
#include "faf/port/FileSystem.h"
#include "GameRuntime.h"
#include "Log.h"
#include "Renderer.h"
#include "RunStatus.h"

namespace faf::android {

  namespace {

    constexpr std::string_view kVersionName = FAF_ANDROID_VERSION_NAME;

    /// Once the background is on screen nothing animates; a frame per second
    /// keeps it fresh after events Android does not report (Vulkan transform
    /// changes on a 180° turn are handled by Diligent on present).
    constexpr std::chrono::milliseconds kStaticRefreshInterval{1000};

    /// How long APP_CMD_DESTROY waits for a still running data-path script.
    /// The UI thread is blocked meanwhile (onDestroy), so keep it short.
    constexpr std::chrono::milliseconds kWorkerShutdownWait{1000};

    std::string ParentDirectory(const std::string& path)
    {
      const std::size_t slash = path.find_last_of('/');
      return slash == std::string::npos || slash == 0 ? std::string() : path.substr(0, slash);
    }

    const char* CommandName(const int32_t command)
    {
      switch (command) {
      case APP_CMD_INPUT_CHANGED:
        return "INPUT_CHANGED";
      case APP_CMD_INIT_WINDOW:
        return "INIT_WINDOW";
      case APP_CMD_TERM_WINDOW:
        return "TERM_WINDOW";
      case APP_CMD_WINDOW_RESIZED:
        return "WINDOW_RESIZED";
      case APP_CMD_WINDOW_REDRAW_NEEDED:
        return "WINDOW_REDRAW_NEEDED";
      case APP_CMD_CONTENT_RECT_CHANGED:
        return "CONTENT_RECT_CHANGED";
      case APP_CMD_GAINED_FOCUS:
        return "GAINED_FOCUS";
      case APP_CMD_LOST_FOCUS:
        return "LOST_FOCUS";
      case APP_CMD_CONFIG_CHANGED:
        return "CONFIG_CHANGED";
      case APP_CMD_LOW_MEMORY:
        return "LOW_MEMORY";
      case APP_CMD_START:
        return "START";
      case APP_CMD_RESUME:
        return "RESUME";
      case APP_CMD_SAVE_STATE:
        return "SAVE_STATE";
      case APP_CMD_PAUSE:
        return "PAUSE";
      case APP_CMD_STOP:
        return "STOP";
      case APP_CMD_DESTROY:
        return "DESTROY";
      default:
        return "?";
      }
    }

    class App
    {
    public:
      explicit App(android_app* app)
        : mApp(app)
      {}

      App(const App&) = delete;
      App& operator=(const App&) = delete;

      void Run();
      void OnCommand(int32_t command);
      int32_t OnInput(const AInputEvent* event);

    private:
      void Start();
      /// Waits for and dispatches every pending looper event. False when the
      /// looper itself failed (nothing sensible is left to do then).
      bool PumpEvents();
      [[nodiscard]] int PollTimeoutMs() const;
      [[nodiscard]] bool CanRender() const;
      void CheckLoading();
      void UploadSplash();
      void RenderFrame();
      void OnWindowCreated();
      void Fail(Stage stage, const std::string& message);
      void RequestExit(std::string_view reason);
      void FinishActivity();
      void Shutdown();
      [[nodiscard]] Stage CurrentStage() const;

      android_app* const mApp;
      std::string mRoot;
      LaunchOptions mOptions;
      std::shared_ptr<RunStatus> mStatus;
      std::unique_ptr<GameRuntime> mRuntime;
      Renderer mRenderer;
      std::optional<SplashImage> mSplash; ///< Decoded, waiting for a device to upload to.

      bool mStarted = false;        ///< Between APP_CMD_START and APP_CMD_STOP: visible.
      bool mFinishing = false;      ///< ANativeActivity_finish was called.
      bool mSurfaceChanged = false; ///< Resize/configuration change to apply before the next frame.
      bool mDirty = true;           ///< Something changed since the last frame.
      bool mSplashShown = false;    ///< The background has been presented.
      bool mTouchArmed = false;     ///< A touch went down after the background was shown.
      bool mFrameSkipped = false;   ///< The last frame could not be drawn.
      float mTargetProgress = 0.0f;
      float mShownProgress = 0.0f;
      std::chrono::steady_clock::time_point mStartTime;
      std::chrono::steady_clock::time_point mLastFrame;
    };

    void App::Run()
    {
      mStartTime = std::chrono::steady_clock::now();
      mLastFrame = mStartTime;
      try {
        Start();
      } catch (const std::exception& e) {
        Fail(Stage::Args, std::string("internal error: ") + e.what());
      }

      while (mApp->destroyRequested == 0) {
        if (!PumpEvents()) {
          break;
        }
        if (mApp->destroyRequested != 0) {
          break;
        }
        try {
          CheckLoading();
          if (CanRender()) {
            const bool staticFrameDue =
              std::chrono::steady_clock::now() - mLastFrame >= kStaticRefreshInterval;
            if (!mRenderer.HasImage() || mDirty || mSurfaceChanged || staticFrameDue) {
              RenderFrame();
            }
          }
        } catch (const std::exception& e) {
          Fail(CurrentStage(), std::string("internal error: ") + e.what());
        }
      }
      Shutdown();
    }

    void App::Start()
    {
      ANativeActivity* activity = mApp->activity;
      LogInfo("faf-re Android runtime {} starting (Android API {})", kVersionName, activity->sdkVersion);

      mRoot = DataRoot(activity);
      if (mRoot.empty()) {
        // Without the data root there is neither a log file nor a status.json
        // for the launcher; logcat is all that is left.
        LogError("no external files directory (shared storage unavailable); cannot start");
        FinishActivity();
        return;
      }
      for (const char* directory : {"logs", "launch", "localappdata", "documents"}) {
        const std::string path = mRoot + "/" + directory;
        if (!faf::port::fs::CreateDirectories(path)) {
          LogWarning("cannot create {}", path);
        }
      }
      std::string error;
      mStatus = std::make_shared<RunStatus>(mRoot + "/launch/status.json", std::string(kVersionName));
      mStatus->Update([](RunStatusData& data) {
        data.stage = Stage::Args;
      });
      LogInfo("data root {}", mRoot);

      // One log per graphics backend (faf_android_vulkan.log,
      // faf_android_gles.log), each keeping its previous run as .1.log, so
      // trying both backends in a row loses nothing. The backend is known only
      // once the arguments are read; lines logged until then are buffered.
      const auto openRuntimeLog = [this](const Backend backend) {
        const std::string path = mRoot + "/logs/faf_android_" + ToString(backend) + ".log";
        std::string openError;
        if (!Log::Get().OpenFile(path, &openError)) {
          LogWarning("{}", openError);
        }
        InstallCrashHandler(path);
      };

      const IntentArgs intent = ReadIntentArgs(activity);
      if (!intent.ok) {
        openRuntimeLog(Backend::Vulkan);
        Fail(Stage::Args, "Cannot read the launch arguments: " + intent.error);
        return;
      }
      if (!intent.present) {
        LogInfo("args: no \"argv\" extra; using the defaults");
      }
      LogInfo("args ({}): {}", intent.argv.size(), DescribeArgs(intent.argv));
      const faf::port::CommandLine commandLine(intent.argv);
      mOptions = ResolveLaunchOptions(commandLine, mRoot);
      openRuntimeLog(mOptions.backend);
      for (const std::string& warning : mOptions.warnings) {
        LogWarning("args: {}", warning);
      }
      if (!mOptions.unusedOptions.empty()) {
        LogInfo("args: kept for the engine, not used by this build: {}", DescribeArgs(mOptions.unusedOptions));
      }
      LogInfo(
        "args: init {}, renderer {}, game log {}",
        mOptions.initScript,
        ToString(mOptions.backend),
        mOptions.gameLog.empty() ? "(none)" : mOptions.gameLog
      );

      if (!mOptions.gameLog.empty()) {
        // CreateDirectories reuses a directory whose case differs; open the
        // file in the directory that is actually there.
        std::string gameLog = mOptions.gameLog;
        const std::string directory = ParentDirectory(gameLog);
        if (!directory.empty() && faf::port::fs::CreateDirectories(directory)) {
          if (const std::optional<std::string> existing = faf::port::fs::ResolveCaseInsensitive(directory)) {
            gameLog = *existing + gameLog.substr(directory.size());
          }
        }
        if (!Log::Get().OpenGameLog(gameLog, &error)) {
          LogWarning("game log: {}", error);
        }
      }

      GameRuntimeConfig config;
      config.root = mRoot;
      config.initScript = mOptions.initScript;
      config.folders.localAppData = mRoot + "/localappdata";
      config.folders.personal = mRoot + "/documents";
      ALooper* looper = mApp->looper;
      mRuntime = std::make_unique<GameRuntime>(std::move(config), mStatus, [looper] {
        ALooper_wake(looper);
      });
      mRuntime->Start();
    }

    bool App::PumpEvents()
    {
      int timeout = PollTimeoutMs();
      for (;;) {
        int events = 0;
        android_poll_source* source = nullptr;
        const int id = ALooper_pollOnce(timeout, nullptr, &events, reinterpret_cast<void**>(&source));
        if (id == ALOOPER_POLL_TIMEOUT) {
          return true;
        }
        if (id == ALOOPER_POLL_ERROR) {
          Fail(CurrentStage(), "internal error: ALooper_pollOnce failed");
          return false;
        }
        if (id >= 0 && source != nullptr) {
          source->process(mApp, source);
        }
        if (mApp->destroyRequested != 0) {
          return true;
        }
        // ALOOPER_POLL_WAKE (the worker), a callback or an event: drain the
        // rest without blocking, then do the frame's work.
        timeout = 0;
      }
    }

    int App::PollTimeoutMs() const
    {
      if (!CanRender()) {
        // No window, not visible or finishing: sleep until Android or the
        // worker (ALooper_wake) has something.
        return -1;
      }
      if (mFrameSkipped) {
        return 16;
      }
      if (!mRenderer.HasImage() || mDirty || mSurfaceChanged) {
        return 0; // loading animation or a pending redraw
      }
      const auto sinceLastFrame = std::chrono::steady_clock::now() - mLastFrame;
      const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(kStaticRefreshInterval - sinceLastFrame).count();
      return remaining > 0 ? static_cast<int>(remaining) : 0;
    }

    bool App::CanRender() const
    {
      return !mFinishing && mStarted && mRenderer.HasSurface();
    }

    Stage App::CurrentStage() const
    {
      return mStatus ? mStatus->Snapshot().stage : Stage::Args;
    }

    void App::CheckLoading()
    {
      if (!mRuntime || mFinishing) {
        return;
      }
      LoadProgress progress = mRuntime->Progress();
      if (progress.failed) {
        Fail(progress.stage, progress.error);
        return;
      }
      mTargetProgress = progress.fraction;
      if (progress.done && !mSplash && !mRenderer.HasImage()) {
        mSplash = mRuntime->TakeSplash();
      }
      UploadSplash();
    }

    void App::UploadSplash()
    {
      // Only with a surface: an OpenGL ES context without one is current
      // only where EGL supports surfaceless contexts. OnWindowCreated retries.
      if (!mSplash || mFinishing || !mRenderer.HasSurface() || mRenderer.HasImage()) {
        return;
      }
      mStatus->Update([](RunStatusData& data) {
        data.stage = Stage::Renderer;
      });
      std::string error;
      if (!mRenderer.SetImage(mSplash->image, error)) {
        Fail(Stage::Renderer, "Cannot show the main menu background: " + error);
        return;
      }
      LogInfo(
        "renderer: background {} uploaded ({}x{} {})",
        mSplash->vfsPath,
        mSplash->image.width,
        mSplash->image.height,
        mSplash->image.format
      );
      mSplash.reset(); // the GPU has it now
      mDirty = true;
    }

    void App::RenderFrame()
    {
      std::string error;
      if (mSurfaceChanged) {
        mSurfaceChanged = false;
        if (!mRenderer.UpdateSurface(error)) {
          Fail(Stage::Renderer, "Cannot resize the swap chain: " + error);
          return;
        }
        LogInfo("renderer: surface updated: {}", mRenderer.Description());
      }

      const auto now = std::chrono::steady_clock::now();
      const float dt = std::chrono::duration<float>(now - mLastFrame).count();
      // Ease towards the worker's progress so the bar moves smoothly between
      // its coarse updates.
      mShownProgress += (mTargetProgress - mShownProgress) * (1.0f - std::exp(-5.0f * dt));

      FrameParams params;
      params.seconds = std::chrono::duration<float>(now - mStartTime).count();
      params.progress = mRenderer.HasImage() ? -1.0f : mShownProgress;
      const FrameResult result = mRenderer.Render(params, error);
      if (result == FrameResult::Failed) {
        Fail(Stage::Frame, error);
        return;
      }
      mLastFrame = now;
      // A skipped frame (no image acquired while the window changes) is not
      // paced by the swap chain; wait a little before the next attempt.
      mFrameSkipped = result == FrameResult::Skipped;
      if (mFrameSkipped) {
        return;
      }
      mDirty = false;

      if (mRenderer.HasImage() && !mSplashShown) {
        mSplashShown = true;
        mStatus->Update([](RunStatusData& data) {
          data.state = RunState::Running;
          data.stage = Stage::Frame;
          data.message.clear();
        });
        LogInfo(
          "main menu background shown after {:.0f} ms ({}); tap or press back to return to the launcher",
          params.seconds * 1000.0f,
          mRenderer.Description()
        );
      }
    }

    void App::OnWindowCreated()
    {
      ANativeWindow* window = mApp->window;
      if (window == nullptr || mFinishing) {
        return;
      }
      LogInfo("window {}x{}", ANativeWindow_getWidth(window), ANativeWindow_getHeight(window));
      std::string error;
      if (!mRenderer.HasDevice()) {
        const bool allowFallback = mOptions.backend == Backend::Vulkan;
        LogInfo(
          "renderer: creating a {} device{}",
          ToString(mOptions.backend),
          allowFallback ? " (OpenGL ES fallback allowed)" : ""
        );
        if (!mRenderer.Create(window, mOptions.backend, allowFallback, error)) {
          Fail(Stage::Renderer, "No usable graphics backend. " + error);
          return;
        }
        LogInfo("renderer: {}", mRenderer.Description());
        const std::string name = ToString(mRenderer.ActiveBackend());
        if (mStatus) {
          mStatus->Update([&name](RunStatusData& data) {
            data.renderer = name;
          });
        }
      } else {
        if (!mRenderer.AttachWindow(window, error)) {
          Fail(Stage::Renderer, "Cannot use the new window: " + error);
          return;
        }
        LogInfo("renderer: window attached: {}", mRenderer.Description());
      }
      mSurfaceChanged = false;
      mDirty = true;
      UploadSplash();
    }

    void App::OnCommand(const int32_t command)
    {
      if (command != APP_CMD_INPUT_CHANGED && command != APP_CMD_WINDOW_REDRAW_NEEDED) {
        LogDebug("lifecycle: {}", CommandName(command));
      }
      try {
        switch (command) {
        case APP_CMD_INIT_WINDOW:
          OnWindowCreated();
          break;
        case APP_CMD_TERM_WINDOW:
          // The window is destroyed as soon as this handler returns.
          mRenderer.DetachWindow();
          break;
        case APP_CMD_WINDOW_RESIZED:
          mSurfaceChanged = true;
          mDirty = true;
          break;
        case APP_CMD_CONFIG_CHANGED:
          LogInfo("configuration changed (orientation {})", AConfiguration_getOrientation(mApp->config));
          mSurfaceChanged = true;
          mDirty = true;
          break;
        case APP_CMD_CONTENT_RECT_CHANGED:
        case APP_CMD_GAINED_FOCUS:
          mDirty = true;
          break;
        case APP_CMD_LOST_FOCUS:
          // Keep the swap chain: a notification shade or a dialog on top
          // still leaves us visible.
          break;
        case APP_CMD_WINDOW_REDRAW_NEEDED:
          // Draw before returning so the system shows a current frame.
          mDirty = true;
          if (CanRender()) {
            RenderFrame();
          }
          break;
        case APP_CMD_START:
          mStarted = true;
          mDirty = true;
          break;
        case APP_CMD_STOP:
          mStarted = false;
          break;
        case APP_CMD_LOW_MEMORY:
          LogWarning("Android reports low memory");
          break;
        case APP_CMD_DESTROY:
          if (mStatus && !mStatus->IsTerminal()) {
            mStatus->Finish(
              RunState::Exited,
              std::nullopt,
              mSplashShown ? std::string() : std::string("Closed before loading finished.")
            );
          }
          break;
        default:
          break;
        }
      } catch (const std::exception& e) {
        Fail(CurrentStage(), std::string("internal error: ") + e.what());
      }
    }

    int32_t App::OnInput(const AInputEvent* event)
    {
      const int32_t type = AInputEvent_getType(event);
      if (type == AINPUT_EVENT_TYPE_KEY) {
        const int32_t key = AKeyEvent_getKeyCode(event);
        if (key == AKEYCODE_BACK || key == AKEYCODE_ESCAPE) {
          // Consumed on down and up, otherwise NativeActivity finishes the
          // activity itself and status.json never says "exited".
          if (AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_UP) {
            RequestExit(key == AKEYCODE_BACK ? "back key" : "escape key");
          }
          return 1;
        }
        return 0;
      }
      if (type == AINPUT_EVENT_TYPE_MOTION) {
        // A tap ends the run once the background is shown; a touch that began
        // during loading does not count.
        switch (AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK) {
        case AMOTION_EVENT_ACTION_DOWN:
          mTouchArmed = mSplashShown;
          break;
        case AMOTION_EVENT_ACTION_UP:
          if (mTouchArmed) {
            mTouchArmed = false;
            RequestExit("tap");
          }
          break;
        case AMOTION_EVENT_ACTION_CANCEL:
          mTouchArmed = false;
          break;
        default:
          break;
        }
        return 1;
      }
      return 0;
    }

    void App::Fail(const Stage stage, const std::string& message)
    {
      // Finish is a no-op (false) when the worker already wrote and logged
      // this error, or when the run had ended before.
      const bool first = mStatus == nullptr || mStatus->Finish(RunState::Error, stage, message);
      if (first && !mFinishing) {
        LogError("stopping at stage {}: {}", ToString(stage), message);
      }
      FinishActivity();
    }

    void App::RequestExit(const std::string_view reason)
    {
      if (mFinishing) {
        return;
      }
      LogInfo("exit requested ({})", reason);
      if (mStatus) {
        mStatus->Finish(RunState::Exited, std::nullopt, std::string());
      }
      FinishActivity();
    }

    void App::FinishActivity()
    {
      if (mFinishing) {
        return;
      }
      mFinishing = true;
      // Asynchronous: Android pauses, stops and destroys the activity, and the
      // loop keeps serving those commands until APP_CMD_DESTROY.
      ANativeActivity_finish(mApp->activity);
    }

    void App::Shutdown()
    {
      LogInfo("shutting down");
      if (mRuntime) {
        mRuntime->Shutdown(kWorkerShutdownWait);
      }
      mRenderer.Destroy();
      if (mStatus && !mStatus->IsTerminal()) {
        mStatus->Finish(RunState::Exited, std::nullopt, std::string());
      }
      LogInfo("runtime stopped");
      Log::Get().Close();
    }

  } // namespace

} // namespace faf::android

// Declared (extern "C") by android_native_app_glue.h and called on the glue's
// thread. Hidden like everything else; only ANativeActivity_onCreate is exported.
void android_main(android_app* app)
{
  faf::android::App runtime(app);
  app->userData = &runtime;
  app->onAppCmd = [](android_app* glue, const int32_t command) {
    static_cast<faf::android::App*>(glue->userData)->OnCommand(command);
  };
  app->onInputEvent = [](android_app* glue, AInputEvent* event) -> int32_t {
    return static_cast<faf::android::App*>(glue->userData)->OnInput(event);
  };

  runtime.Run();

  app->onAppCmd = nullptr;
  app->onInputEvent = nullptr;
  app->userData = nullptr;
}
