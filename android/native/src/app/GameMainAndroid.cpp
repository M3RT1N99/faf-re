// The app's entry into libfafgame.so (exports.map). The game's activity (io.github.m3rt1n99.fafre.
// GameActivity, an SDLActivity) names it as its main function, and SDL's activity calls it on a thread
// of its own with the activity's arguments: the program's path, as FAF's client starts the game, then
// its command line. It gives the engine's entry, SDL_main (src/platform/posix/ClientMainPosix.cpp with
// patches/src/platform/posix/ClientMainPosix.cpp.patch), what it cannot know on Android, and runs it:
//
// - SDL's hints for a game on a phone: the program goes on in the back, as an online game's simulation
//   keeps the others' pace (the surface goes, the loop does not stop), and outlives an activity Android
//   destroys, which a new one shows (GameActivity.java); landscape only; no screen keyboard that comes
//   by itself, as the engine asks for typed text in every window: it comes for a text field
//   (ScreenKeyboardAndroid.cpp).
// - argv[0] is the program's path, from which the engine takes its install's folder (DISK_GetLaunchDir):
//   SDL's own is "app_process".
// - `/vulkan`, the one renderer the engine has here: its Direct3D ones are Windows' (gpg/gal/Device.cpp), and
//   the Graphics API option's `auto` would try Direct3D 12 first on each start.
// - The display mode of `/fullscreen <width> <height>`, which the activity gives with its surface's size,
//   also for a screen smaller than the 1024 x 768 the engine holds as the least a mode may be (CScApp's
//   SetUpHeads clamps the mode to wnd_DefaultCreateWidth and wnd_DefaultCreateHeight).
// - The engine's main thread has the Windows program's stack (/STACK:4000000, src/CMakeLists.txt): SDL's
//   thread is a Java thread with the runtime's default, 1 MB.
// - The process ends with the program, as a Windows program's does: the engine is one program per
//   process, and SDL's activity would outlive it.
// - Android's performance hints for the render thread and the sim's (PerformanceAndroid.cpp), which time the engine's
//   frames and beats through its hooks, and the watch of the phone's thermal headroom.
//
// The report (FAF_GAME_REPORT, which the activity sets: GameReport.java) gets the lines the app reads:
// when the first frame went to the screen, and the program's exit code; and of the engine's log what a
// tester's report needs from this phone: the GPU the renderer took, each swap chain, whether BC textures
// are the GPU's or decoded on the CPU, and the output the sound plays through, or why it has none. The
// activity asks whether the first frame came (GamePhase.firstFrame): the game loads until then.

#include <android/log.h>
#include <jni.h>
#include <pthread.h>
#include <strings.h>
#include <time.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_system.h>

#include "GameAndroid.h"
#include "PerformanceAndroid.h"
#include "ScreenKeyboardAndroid.h"
#include "../host/HostSdl.h"
#include "gpg/core/utils/Logging.h"
#include "moho/misc/StartupHelpers.h"

// The engine's entry, as SDL_main.h declares it on Android (ClientMainPosix.cpp includes that header).
extern "C" int SDL_main(int argc, char* argv[]);

namespace
{
  using platform::android::Report;

  constexpr char kLogTag[] = "faf";
  /// The x64 Windows program's stack (/STACK:4000000, src/CMakeLists.txt).
  constexpr std::size_t kMainThreadStackSize = 4000000;
  /// The engine's one renderer here. A switch comes before FAF's Graphics API option (RendererAttemptOrder in
  /// CScApp.cpp), whose `auto` tries Direct3D 12 first, which fails with a GAL exception and a fallback's warning,
  /// and whose Direct3D choices would leave the game with no renderer at all.
  char gVulkanSwitch[] = "/vulkan";

  /// When the program began (FafGameMain), the report's clock; -1 before it, when the activity's lines (Game.report) have
  /// no time on it, as the start's preparation's have none (GameReport.append). Set on SDL's thread, read on any.
  std::atomic<std::int64_t> gStartMilliseconds{-1};
  std::atomic<bool> gFirstFrameReported{false};

  std::int64_t NowMilliseconds()
  {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::int64_t>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
  }

  /// The engine's log lines that go into the report, by how they begin (src/gpg/gal/backends/vulkan/DeviceVulkan.cpp and
  /// its patch): the device, each swap chain, the BC textures' way.
  constexpr std::string_view kRendererLines[] = {
    "Vulkan: device created on ", "Vulkan: swap chain ", "Vulkan: the device has no BC texture compression",
    "Vulkan: BC1 to BC3 textures are decoded on the CPU", "Vulkan: BC textures are the GPU's"};
  /// The sound engine is up (moho/audio/AudioEngine.cpp, after XACT's Initialize and 3D audio's): FACT's mastering voice,
  /// FAudio's, plays through SDL's audio output by then.
  constexpr std::string_view kSoundUp = "MEM: 0 bytes SND";
  /// Or it is not: XACT's Initialize or 3D audio's failed, and the game plays without sound (the same file).
  constexpr std::string_view kSoundFailed = "SND: Error initializing ";

  /// Copies those lines into the report as the engine logs them, on whichever thread logs. It logs nothing itself: the
  /// engine's log holds its lock while it calls its targets.
  class ReportLines final : public gpg::LogTarget
  {
  public:
    ReportLines()
      : gpg::LogTarget(true)
    {}

    void OnMessage(gpg::LogSeverity, const fa::string& message, const fa::vector<fa::string>&, int) override
    {
      const std::string_view text(message.c_str(), message.size());
      for (const std::string_view start : kRendererLines) {
        if (text.substr(0, start.size()) == start) {
          Report("renderer: " + std::string(text));
          return;
        }
      }
      if (text == kSoundUp && !mSoundReported.exchange(true)) {
        const char* const driver = SDL_GetCurrentAudioDriver();
        Report(std::string("sound: XACT on FAudio, through SDL's ") + (driver != nullptr ? driver : "(no)") + " output");
        platform::android::SoundStarted();
      } else if (text.substr(0, kSoundFailed.size()) == kSoundFailed) {
        // On one line: the engine's message has XACT's word on a second.
        std::string line(text);
        std::replace(line.begin(), line.end(), '\n', ' ');
        Report("sound: " + line);
      }
    }

  private:
    std::atomic<bool> mSoundReported{false};
  };

  /// `/fullscreen <width> <height>`'s size makes the engine's least display mode no larger than it.
  void FitLeastMode(const int argc, char** const argv)
  {
    for (int index = 1; index + 2 < argc; ++index) {
      if (strcasecmp(argv[index], "/fullscreen") != 0) {
        continue;
      }
      const int width = std::atoi(argv[index + 1]);
      const int height = std::atoi(argv[index + 2]);
      if (width > 0 && height > 0) {
        moho::wnd_DefaultCreateWidth = (std::min)(moho::wnd_DefaultCreateWidth, static_cast<std::int32_t>(width));
        moho::wnd_DefaultCreateHeight = (std::min)(moho::wnd_DefaultCreateHeight, static_cast<std::int32_t>(height));
      }
      return;
    }
  }

  /// The environment as the activity set it (GameActivity.prepare, Os.setenv), in the program's own: what differs is taken.
  /// On a phone the activity and the program share one C library, and nothing differs. Under an ARM translation (an x86_64
  /// emulator running the arm64 app) the program's code has a C library of its own, whose environment was copied when the
  /// process began, before the activity set it: the engine found no HOME and could not set up its search paths.
  void TakeActivityEnvironment()
  {
    JNIEnv* const env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (env == nullptr) {
      return;
    }
    const jclass os = env->FindClass("android/system/Os");
    const jmethodID current = os != nullptr ? env->GetStaticMethodID(os, "environ", "()[Ljava/lang/String;") : nullptr;
    const auto entries = current != nullptr ? static_cast<jobjectArray>(env->CallStaticObjectMethod(os, current)) : nullptr;
    if (env->ExceptionCheck() || entries == nullptr) {
      env->ExceptionClear();
      return;
    }
    const jsize count = env->GetArrayLength(entries);
    for (jsize at = 0; at < count; ++at) {
      const auto entry = static_cast<jstring>(env->GetObjectArrayElement(entries, at));
      if (entry == nullptr) {
        continue;
      }
      if (const char* const chars = env->GetStringUTFChars(entry, nullptr)) {
        const std::string_view text(chars);
        const std::size_t equals = text.find('=');
        if (equals != std::string_view::npos && equals > 0) {
          const std::string name(text.substr(0, equals));
          const std::string value(text.substr(equals + 1));
          const char* const own = std::getenv(name.c_str());
          if (own == nullptr || value != own) {
            (void)::setenv(name.c_str(), value.c_str(), 1);
          }
        }
        env->ReleaseStringUTFChars(entry, chars);
      }
      env->DeleteLocalRef(entry);
    }
    env->DeleteLocalRef(entries);
    env->DeleteLocalRef(os);
  }

  struct Program
  {
    int argc = 0;
    char** argv = nullptr;
    int exitCode = 1;
  };

  void* RunProgram(void* const data)
  {
    Program& program = *static_cast<Program*>(data);
    // On the program's thread: the first of SDL's calls makes it SDL's main thread.
    (void)SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "0");
    // SDL's activity ends the process when it is made anew, unless this says that it may be.
    (void)SDL_SetHint(SDL_HINT_ANDROID_ALLOW_RECREATE_ACTIVITY, "1");
    (void)SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    (void)SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "0");
    // A mouse plays as a mouse, as on a desktop: SDL makes no finger of it, which the touch layer would take
    // (src/host/FingersSdl.cpp).
    (void)SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    screenkeyboard::Install();
    host::Install();
    performance::Install();
    // Logged to until the process ends, which runs no destructors.
    static ReportLines reportLines;
    program.exitCode = SDL_main(program.argc, program.argv);
    return nullptr;
  }
} // namespace

namespace platform::android
{
  /// A line of the report, as the app reads it: "<ms since the start> ms: <text>", or "<text>" alone for one of the
  /// activity's from before the program's start. Also to the system log.
  void Report(const std::string& line)
  {
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "game: %s", line.c_str());
    const char* const path = std::getenv("FAF_GAME_REPORT");
    if (path == nullptr || path[0] == '\0') {
      return;
    }
    if (std::FILE* const file = std::fopen(path, "a")) {
      const std::int64_t start = gStartMilliseconds.load();
      if (start < 0) {
        std::fprintf(file, "%s\n", line.c_str());
      } else {
        std::fprintf(file, "%lld ms: %s\n", static_cast<long long>(NowMilliseconds() - start), line.c_str());
      }
      std::fclose(file);
    }
  }

  // A frame with draws went to the screen (src/gal/VulkanSurfaceAndroid.cpp, at each such present): the first is the
  // report's line (GameReport.FIRST_FRAME) and the end of the game's loading for Android (GamePhase), and each ends the
  // render thread's work for its performance hint.
  void FirstFramePresented()
  {
    performance::FramePresented();
    if (!gFirstFrameReported.exchange(true)) {
      Report("first frame");
    }
  }
} // namespace platform::android

/// Whether the program's first frame went to the screen (the app's GamePhase.firstFrame, on the activity's thread).
extern "C" JNIEXPORT jboolean JNICALL Java_io_github_m3rt1n99_fafre_GamePhase_firstFrame(JNIEnv*, jclass)
{
  return gFirstFrameReported.load() ? JNI_TRUE : JNI_FALSE;
}

extern "C" __attribute__((visibility("default"))) int FafGameMain(const int argc, char* argv[])
{
  gStartMilliseconds = NowMilliseconds();
  TakeActivityEnvironment();
  // argv[0] is SDL's; the activity's arguments follow, the program's path first. The renderer's switch goes right after
  // the path, where no switch of the activity's can take it for its value.
  if (argc < 2) {
    Report("fatal: the activity gave no program to start");
    return 1;
  }
  std::vector<char*> arguments{argv[1], gVulkanSwitch};
  arguments.insert(arguments.end(), argv + 2, argv + argc);
  arguments.push_back(nullptr);
  Program program{static_cast<int>(arguments.size()) - 1, arguments.data(), 1};
  FitLeastMode(program.argc, program.argv);

  pthread_attr_t attributes;
  pthread_attr_init(&attributes);
  pthread_attr_setstacksize(&attributes, kMainThreadStackSize);
  pthread_t thread{};
  const int created = pthread_create(&thread, &attributes, &RunProgram, &program);
  pthread_attr_destroy(&attributes);
  if (created != 0) {
    Report("fatal: the program's thread could not start (error " + std::to_string(created) + ")");
    return 1;
  }
  pthread_join(thread, nullptr);
  Report("exit " + std::to_string(program.exitCode));
  std::fflush(nullptr);
  std::_Exit(program.exitCode);
}
