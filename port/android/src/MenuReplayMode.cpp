#include "MenuReplayMode.h"

#include <jni.h>

#include <chrono>
#include <cstdio>
#include <optional>
#include <string_view>
#include <utility>

#include "GalPlay.h"
#include "Log.h"

namespace faf::android {

  namespace {

    /// How long APP_CMD_TERM_WINDOW waits for the replay thread to drop the swap chain. The replay
    /// applies window changes between frames; the longest gap is the first frame's shader compilation.
    constexpr int kReleaseWindowTimeoutMs = 3000;

    std::string JsonEscape(const std::string_view text)
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

    std::string Number(const double value)
    {
      char text[48];
      std::snprintf(text, sizeof(text), "%.2f", value);
      return text;
    }

    /// UTF-8 to UTF-16 for JNI's NewString (NewStringUTF takes modified UTF-8, which differs for
    /// characters outside the BMP). Invalid sequences become U+FFFD.
    std::u16string ToUtf16(const std::string_view text)
    {
      std::u16string out;
      out.reserve(text.size());
      for (std::size_t i = 0; i < text.size();) {
        const auto byte = static_cast<unsigned char>(text[i]);
        char32_t cp = 0xFFFD;
        std::size_t length = 1;
        if (byte < 0x80) {
          cp = byte;
        } else if ((byte & 0xE0) == 0xC0) {
          length = 2;
        } else if ((byte & 0xF0) == 0xE0) {
          length = 3;
        } else if ((byte & 0xF8) == 0xF0) {
          length = 4;
        }
        if (length > 1) {
          if (i + length > text.size()) {
            length = 1;
          } else {
            cp = byte & (0x7F >> length);
            for (std::size_t k = 1; k < length; ++k) {
              const auto next = static_cast<unsigned char>(text[i + k]);
              if ((next & 0xC0) != 0x80) {
                cp = 0xFFFD;
                length = k;
                break;
              }
              cp = (cp << 6) | (next & 0x3F);
            }
          }
        }
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
          cp = 0xFFFD;
        }
        if (cp >= 0x10000) {
          cp -= 0x10000;
          out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
          out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
          out.push_back(static_cast<char16_t>(cp));
        }
        i += length;
      }
      return out;
    }

    /**
     * GameActivity's onMenuReplay* methods, called from the replay thread (attached to the VM for its
     * lifetime). A method the activity does not have is skipped (logged once); a Java exception is
     * cleared and logged, never left pending.
     */
    class JavaBridge
    {
    public:
      explicit JavaBridge(ANativeActivity* const activity)
        : mVm(activity->vm),
          mActivity(activity->clazz) // the GameActivity instance; valid until android_main returns
      {
        JavaVMAttachArgs args{JNI_VERSION_1_6, "faf_menu_replay", nullptr};
        if (mVm->AttachCurrentThread(&mEnv, &args) != JNI_OK) {
          mEnv = nullptr;
          LogError("menu replay: cannot attach the replay thread to the Java VM; the launcher gets no progress");
          return;
        }
        if (mEnv->PushLocalFrame(16) != JNI_OK) {
          mEnv->ExceptionClear();
        }
        jclass type = mEnv->GetObjectClass(mActivity);
        mProgress = Method(type, "onMenuReplayProgress", "(Ljava/lang/String;)V");
        mReadback = Method(type, "onMenuReplayReadback", "(IIILjava/nio/ByteBuffer;Ljava/lang/String;)V");
        mFinished = Method(type, "onMenuReplayFinished", "(Ljava/lang/String;)V");
        mEnv->PopLocalFrame(nullptr);
      }

      ~JavaBridge()
      {
        if (mEnv != nullptr) {
          mVm->DetachCurrentThread();
        }
      }

      JavaBridge(const JavaBridge&) = delete;
      JavaBridge& operator=(const JavaBridge&) = delete;

      void Progress(const std::string& json)
      {
        CallWithString(mProgress, json, "onMenuReplayProgress");
      }

      void Finished(const std::string& json)
      {
        CallWithString(mFinished, json, "onMenuReplayFinished");
      }

      void Readback(const int frame, const int width, const int height, const std::uint8_t* const rgba, const std::string& json)
      {
        if (mEnv == nullptr || mReadback == nullptr) {
          return;
        }
        if (mEnv->PushLocalFrame(4) != JNI_OK) {
          Clear("onMenuReplayReadback");
          return;
        }
        // A direct buffer over the replay's own memory, valid during the call only (GalPlay.h).
        jobject buffer = mEnv->NewDirectByteBuffer(const_cast<std::uint8_t*>(rgba),
                                                   static_cast<jlong>(width) * static_cast<jlong>(height) * 4);
        jstring text = NewString(json);
        if (buffer != nullptr && text != nullptr) {
          mEnv->CallVoidMethod(mActivity, mReadback, static_cast<jint>(frame), static_cast<jint>(width), static_cast<jint>(height), buffer, text);
        }
        Clear("onMenuReplayReadback");
        mEnv->PopLocalFrame(nullptr);
      }

    private:
      jmethodID Method(jclass type, const char* const name, const char* const signature)
      {
        jmethodID method = type != nullptr ? mEnv->GetMethodID(type, name, signature) : nullptr;
        if (mEnv->ExceptionCheck()) {
          mEnv->ExceptionClear();
          method = nullptr;
        }
        if (method == nullptr) {
          LogWarning("menu replay: GameActivity has no {}{}; skipped", name, signature);
        }
        return method;
      }

      jstring NewString(const std::string& text)
      {
        const std::u16string utf16 = ToUtf16(text);
        return mEnv->NewString(reinterpret_cast<const jchar*>(utf16.data()), static_cast<jsize>(utf16.size()));
      }

      void CallWithString(jmethodID method, const std::string& json, const char* const name)
      {
        if (mEnv == nullptr || method == nullptr) {
          return;
        }
        if (mEnv->PushLocalFrame(2) != JNI_OK) {
          Clear(name);
          return;
        }
        if (jstring text = NewString(json)) {
          mEnv->CallVoidMethod(mActivity, method, text);
        }
        Clear(name);
        mEnv->PopLocalFrame(nullptr);
      }

      void Clear(const char* const name)
      {
        if (mEnv->ExceptionCheck()) {
          mEnv->ExceptionDescribe();
          mEnv->ExceptionClear();
          LogWarning("menu replay: {} threw a Java exception (cleared)", name);
        }
      }

      JavaVM* const mVm;
      jobject const mActivity;
      JNIEnv* mEnv = nullptr;
      jmethodID mProgress = nullptr;
      jmethodID mReadback = nullptr;
      jmethodID mFinished = nullptr;
    };

    /// What the C callbacks need (GalPlayCallbacks::user).
    struct CallbackContext
    {
      JavaBridge* bridge = nullptr;
      RunStatus* status = nullptr;
      const std::atomic<bool>* stop = nullptr;
      std::atomic<bool>* resultReported = nullptr;
      std::atomic<bool>* failed = nullptr;
      std::function<void()> wake;
      std::string lastStage;
    };

    /// status.json's stage for a galplay stage; stages only move forward ("data" comes again for the
    /// reference check after "trace").
    std::optional<Stage> StatusStage(const std::string& stage)
    {
      if (stage == "data") {
        return Stage::DataPath;
      }
      if (stage == "trace") {
        return Stage::Mount;
      }
      if (stage == "device" || stage == "shaders") {
        return Stage::Renderer;
      }
      if (stage == "replay") {
        return Stage::Frame;
      }
      return std::nullopt;
    }

    void OnProgress(void* const user, const GalPlayProgress* const progress)
    {
      auto* const context = static_cast<CallbackContext*>(user);
      const std::string stage = progress->stage != nullptr ? progress->stage : "";
      const std::string message = progress->message != nullptr ? progress->message : "";
      if (stage != context->lastStage) {
        context->lastStage = stage;
        LogInfo("menu replay: [{}] {}", stage, message);
        if (context->status != nullptr) {
          if (const std::optional<Stage> next = StatusStage(stage)) {
            context->status->Update([next](RunStatusData& data) {
              if (static_cast<int>(*next) > static_cast<int>(data.stage)) {
                data.stage = *next;
              }
              if (*next == Stage::Renderer || *next == Stage::Frame) {
                data.renderer = "vulkan";
              }
              if (*next == Stage::Frame) {
                data.state = RunState::Running;
              }
            });
          } else if (stage == "done" || stage == "error") {
            // The result, before the replay holds its last frame: an error that kept it from running is
            // final at once; a result (or a stop the activity asked for) is the message until it closes.
            const bool stopped = context->stop != nullptr && context->stop->load();
            const std::string text = "Menu replay: " + message;
            if (stage == "error" && !stopped) {
              context->status->Finish(RunState::Error, context->status->Snapshot().stage, text);
              if (context->failed != nullptr) {
                context->failed->store(true);
              }
            } else {
              context->status->Update([&text](RunStatusData& data) {
                data.message = text;
              });
            }
          }
        }
      }
      if (context->bridge == nullptr) {
        return;
      }
      const std::string json = "{\"stage\": " + Quote(stage) + ", \"message\": " + Quote(message) + ", \"frame\": " + std::to_string(progress->frame) +
                               ", \"frames\": " + std::to_string(progress->frames) + ", \"frameMs\": " + Number(progress->frameMs) +
                               ", \"avgFrameMs\": " + Number(progress->avgFrameMs) + ", \"gpuFrameMs\": " + Number(progress->gpuFrameMs) +
                               ", \"readbacks\": " + std::to_string(progress->readbacks) + ", \"passed\": " + std::to_string(progress->readbacksPassed) +
                               ", \"failed\": " + std::to_string(progress->readbacksFailed) + ", \"done\": " + std::to_string(progress->readbacksDone) +
                               ", \"fast\": " + (progress->fast != 0 ? "true" : "false") + "}";
      context->bridge->Progress(json);
    }

    void OnReadback(void* const user, const GalPlayReadback* const readback)
    {
      auto* const context = static_cast<CallbackContext*>(user);
      if (context->bridge == nullptr) {
        return;
      }
      const std::string json = "{\"index\": " + std::to_string(readback->index) + ", \"frame\": " + std::to_string(readback->frame) +
                               ", \"width\": " + std::to_string(readback->width) + ", \"height\": " + std::to_string(readback->height) +
                               ", \"hash\": " + Quote(readback->hash) + ", \"reference\": " + Quote(readback->reference) + ", \"verdict\": " +
                               Quote(readback->verdict) + ", \"identicalToRecording\": " +
                               (readback->identicalToRecording < 0 ? "null" : readback->identicalToRecording != 0 ? "true" : "false") + "}";
      context->bridge->Readback(static_cast<int>(readback->frame), static_cast<int>(readback->width), static_cast<int>(readback->height),
                                readback->rgba, json);
    }

    void OnFinished(void* const user, const char* const json)
    {
      auto* const context = static_cast<CallbackContext*>(user);
      if (context->bridge != nullptr) {
        context->bridge->Finished(json != nullptr ? json : "{}");
      }
      if (context->resultReported != nullptr) {
        context->resultReported->store(true);
      }
      if (context->wake) {
        context->wake();
      }
    }

    int ShouldStop(void* const user)
    {
      const auto* const context = static_cast<const CallbackContext*>(user);
      return context->stop != nullptr && context->stop->load() ? 1 : 0;
    }

  } // namespace

  MenuReplayMode::MenuReplayMode(ANativeActivity* const activity, MenuReplayConfig config, std::shared_ptr<RunStatus> status,
                                 std::function<void()> wake)
    : mActivity(activity),
      mConfig(std::move(config)),
      mStatus(std::move(status)),
      mWake(std::move(wake))
  {}

  MenuReplayMode::~MenuReplayMode()
  {
    Shutdown();
  }

  void MenuReplayMode::Start(ANativeWindow* const window)
  {
    if (mThread.joinable()) {
      return;
    }
    if (window != nullptr) {
      galplay_set_window(window, 0); // kept for the replay that starts now
    }
    LogInfo(
      "menu replay: trace {}, run directory {}, {}{}{}",
      mConfig.extras.trace.empty() ? std::string("(none)") : mConfig.extras.trace,
      mConfig.extras.runDir.empty() ? std::string("(none)") : mConfig.extras.runDir,
      mConfig.extras.fast ? "as fast as possible" : "recorded pace",
      mConfig.extras.forceCpuDecode ? ", BC decoded on the CPU (forced)" : "",
      mConfig.extras.noShaderCache ? ", no shader cache" : ""
    );
    mThread = std::thread([this] {
      ThreadMain();
    });
  }

  void MenuReplayMode::ThreadMain()
  {
    JavaBridge bridge(mActivity);
    CallbackContext context;
    context.bridge = &bridge;
    context.status = mStatus.get();
    context.stop = &mStop;
    context.resultReported = &mResultReported;
    context.failed = &mFailed;
    context.wake = mWake;

    GalPlayOptions options{};
    options.structSize = sizeof(GalPlayOptions);
    options.fast = mConfig.extras.fast ? 1 : 0;
    options.forceCpuBcDecode = mConfig.extras.forceCpuDecode ? 1 : 0;
    options.noShaderCache = mConfig.extras.noShaderCache ? 1 : 0;
    options.validation = 0;
    const std::string localAppData = mConfig.root + "/localappdata";
    const std::string documents = mConfig.root + "/documents";
    options.initScript = mConfig.initScript.empty() ? nullptr : mConfig.initScript.c_str();
    options.localAppData = localAppData.c_str();
    options.documents = documents.c_str();
    options.cacheDir = mConfig.cacheDir.empty() ? nullptr : mConfig.cacheDir.c_str();
    options.runDir = mConfig.extras.runDir.empty() ? nullptr : mConfig.extras.runDir.c_str();
    options.headless = 0;

    GalPlayCallbacks callbacks{};
    callbacks.user = &context;
    callbacks.onProgress = &OnProgress;
    callbacks.onReadback = &OnReadback;
    callbacks.onFinished = &OnFinished;
    callbacks.shouldStop = &ShouldStop;

    GalPlayResult result{};
    const auto started = std::chrono::steady_clock::now();
    const int code = galplay_run(nullptr, mConfig.extras.trace.c_str(), mConfig.root.c_str(), &options, &callbacks, &result);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    LogInfo("menu replay: ended with exit code {} after {:.1f} s: {}", code, seconds, result.message);

    // status.json: an error that kept the replay from running is final at once; otherwise the activity
    // stays "running" with the result as its message until it closes (exited).
    const bool error = code != GALPLAY_EXIT_PASS && code != GALPLAY_EXIT_DIFFERS && code != GALPLAY_EXIT_STOPPED;
    if (mStatus) {
      if (error) {
        const Stage stage = mStatus->Snapshot().stage;
        mStatus->Finish(RunState::Error, stage, std::string("Menu replay: ") + result.message);
      } else {
        const std::string message = std::string("Menu replay: ") + result.message;
        mStatus->Update([&message](RunStatusData& data) {
          data.message = message;
        });
      }
    }
    mFailed.store(error);
    mResultReported.store(true);
    mFinished.store(true);
    if (mWake) {
      mWake();
    }
  }

  void MenuReplayMode::SetWindow(ANativeWindow* const window)
  {
    if (window != nullptr) {
      galplay_set_window(window, 0);
    }
  }

  void MenuReplayMode::ReleaseWindow()
  {
    if (galplay_set_window(nullptr, kReleaseWindowTimeoutMs) == 0) {
      LogWarning("menu replay: the replay did not release the window within {} ms", kReleaseWindowTimeoutMs);
    }
  }

  void MenuReplayMode::RequestStop()
  {
    mStop.store(true);
    galplay_request_stop();
  }

  void MenuReplayMode::Shutdown()
  {
    RequestStop();
    if (mThread.joinable()) {
      mThread.join();
    }
  }

} // namespace faf::android
