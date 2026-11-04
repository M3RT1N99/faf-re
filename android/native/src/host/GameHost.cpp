// The Android host of the game (WX_LITE.md, "The Android host"): the native half of the game's
// activity (io.github.m3rt1n99.fafre.GameActivity, in a process of its own). It runs the program
// as WinMain does (moho/app/WinMain.cpp, which this build leaves out) on a thread of its own, the
// engine's main thread, and turns the activity's surface, input and lifecycle into what Windows
// gives a program: the screen, the mouse's and the keyboard's messages, and the application's
// activation (include/win32/Win32AndroidHost.h).
//
// The activity gives the command line. The engine reads it from the CRT's __argc and __argv,
// which the host sets before the thread starts; argv[0] is the program's path in its install, as
// FAF's client starts it, because the engine takes its install's folder from it
// (DISK_GetLaunchDir). HOME and TMPDIR give the engine its own folders (SHGetFolderPath). The
// host's lines go to a report file, which the activity shows as it grows and the app reads when
// the game's process has ended; the engine writes its log to the /log file and its crash report
// to HOME (gpg/core/diagnostics).
//
// The screen is the activity's surface, and it has one mode: the program waits for the first
// surface and asks for its size with /fullscreen, unless the command line chooses a size. Otherwise
// the options' default adapter (1024 x 768) would draw into the corner of the back buffer, which
// has the surface's size, where Windows would switch the display to the mode.
//
// When the program returns, the process ends with its exit code as the headless programs end
// (src/headless/HeadlessMain.cpp): the output streams flushed, without the static destructors.
// The activity's process holds the runtime's own threads, and nothing after the end is observed
// but the report, which the activity gets a moment to show.

#include <android/log.h>
#include <android/native_window_jni.h>
#include <fcntl.h>
#include <jni.h>
#include <pthread.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <Windows.h>
#include <Win32AndroidHost.h>

#include "KeysAndroid.h"
#include "gpg/core/diagnostics/CrashHooks.h"
#include "gpg/core/diagnostics/CrashReport.h"
#include "gpg/core/time/Timer.h"
#include "gpg/core/utils/Global.h"
#include "gpg/core/utils/Logging.h"
#include "gpg/gal/Error.hpp"
#include "moho/app/CScApp.h"
#include "moho/app/WinApp.h"
#include "moho/misc/StartupHelpers.h"

// The CRT's copies of the command line, which the engine reads (src/platform/posix/Win32Shim.cpp).
extern int __argc;
extern char** __argv;

namespace
{
  /// The x64 Windows program's stack (/STACK:4000000, src/CMakeLists.txt), for the engine's main
  /// thread here.
  constexpr std::size_t kMainThreadStackSize = 4000000;

  /// How long the process stays when the program returned, for the activity to show the report's
  /// end: it reads the report twice a second (GameActivity).
  constexpr DWORD kShowEndMilliseconds = 1500;

  /// What gpg::HandleAssertFailure's messages begin with.
  constexpr char kFailedAssertion[] = "Failed assertion:";

  /// How many failed assertions the report takes: one that repeats each frame would grow it without
  /// end, and the activity reads all of it twice a second. Windows reports the first alone.
  constexpr int kReportedAssertions = 16;

  std::atomic<bool> gStarted{false};
  std::atomic<int> gFailedAssertions{0};
  std::chrono::steady_clock::time_point gStartTime;
  std::vector<std::string> gArguments;
  std::vector<char*> gArgv;

  std::mutex gReportMutex;
  int gReportFile = -1;

  long long MillisecondsSinceStart()
  {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - gStartTime).count();
  }

  /// One line of the host's report, which also goes to the system log.
  void Report(const std::string& line)
  {
    __android_log_print(ANDROID_LOG_INFO, "faf", "host: %s", line.c_str());
    std::lock_guard<std::mutex> lock(gReportMutex);
    if (gReportFile >= 0) {
      const std::string text = std::to_string(MillisecondsSinceStart()) + " ms: " + line + "\n";
      (void)::write(gReportFile, text.data(), text.size());
    }
  }

  /// The process's end, with the report's end shown first; the streams flushed.
  [[noreturn]] void EndProcess(const int exitCode)
  {
    ::Sleep(kShowEndMilliseconds);
    std::fflush(nullptr);
    std::_Exit(exitCode);
  }

  /// WinMain's die handler, which gpg::Die and a failed assertion both call. A failed assertion goes
  /// on, as on Windows after the dialog, with a line of the report. gpg::Die leaves the crash report
  /// (gpg/core/diagnostics) and a line of the report, the app's verdict, and ends here. Past the
  /// handler it has clang's __debugbreak: brk on arm64, where debuggerd's handler would end the
  /// process, and int3 on x86_64, where returning from the handler runs on into gpg::Die's spin.
  void FatalErrorDieHandler(const char* const message)
  {
    const char* const reason = (message != nullptr) ? message : "engine Die()";
    if (std::strncmp(reason, kFailedAssertion, sizeof(kFailedAssertion) - 1) == 0) {
      if (gFailedAssertions.fetch_add(1) < kReportedAssertions) {
        Report(reason);
      }
      return;
    }
    Report(std::string("fatal: ") + reason);
    gpg::diag::CrashReportContext reportContext{};
    reportContext.reason = reason;
    gpg::diag::HandleFatalPath(reportContext);
    EndProcess(1);
  }

  /// The renderer's first frame: the activity takes the report off the screen (GameActivity).
  void ReportFirstFrame()
  {
    Report("first frame");
  }

  /**
   * WinMain's program: the diagnostic hooks, the die handler and the startup switches, then
   * CScApp under WIN_AppExecute, with WinMain's error handling. Left out, as Windows' own: the
   * crash tests (/crashtest), the probes and watches on the debug registers (/w, /p), AQtime
   * (/aqtime), the CRT's allocation log (/alloclog), the accessibility keys and Media Center.
   */
  int RunProgram()
  {
    gpg::SetThreadName(0xFFFFFFFFu, "Main");

    gpg::time::Timer runTimer{};

    gpg::diag::CrtDiagOptions diagOptions{};
    diagOptions.enableHeapCheck = moho::CFG_GetArgOption("/heapcheck", 0, nullptr);
    gpg::diag::InstallCrtDiagnosticHooks(diagOptions);

    // Windows shows a message box to close once the debugger is attached; here the program waits
    // for the debugger itself.
    if (moho::CFG_GetArgOption("/waitfordebugger", 0, nullptr)) {
      Report("waiting for a debugger (/waitfordebugger)");
      while (::IsDebuggerPresent() == FALSE) {
        ::Sleep(100);
      }
    }

    gpg::SetDieHandler(&FatalErrorDieHandler);

    if (moho::CFG_GetArgOption("/singleproc", 0, nullptr)) {
      DWORD_PTR processAffinityMask = 0;
      DWORD_PTR systemAffinityMask = 0;
      (void)::GetProcessAffinityMask(::GetCurrentProcess(), &processAffinityMask, &systemAffinityMask);

      DWORD_PTR selectedMask = 1;
      if (processAffinityMask != 0) {
        unsigned long bitIndex = 0;
        const unsigned long maxBits = static_cast<unsigned long>(sizeof(DWORD_PTR) * 8U);
        while (bitIndex + 1 < maxBits && ((processAffinityMask >> bitIndex) & 1U) == 0U) {
          ++bitIndex;
        }
        selectedMask = static_cast<DWORD_PTR>(1ULL << bitIndex);
      }
      (void)::SetProcessAffinityMask(::GetCurrentProcess(), selectedMask);
    }

    if (moho::CFG_GetArgOption("/purgecache", 0, nullptr)) {
      moho::USER_PurgeAppCacheDir();
    }

    int exitCode = 0;
    try {
      {
        CScApp app;
        moho::WIN_AppExecute(&app);
        exitCode = app.GetExitValue();
        app.framerates.Reset();
      }

      const int totalSeconds = static_cast<int>(runTimer.ElapsedSeconds());
      gpg::Logf("Run time: %dh%02dm%02ds", totalSeconds / 3600, (totalSeconds % 3600) / 60, totalSeconds % 60);
    } catch (const gpg::gal::Error& galError) {
      std::ostringstream formatted;
      formatted << "file : " << galError.GetFile() << "(" << galError.GetLine() << ")\n";
      formatted << "error: " << galError.what();
      gpg::Die("GAL Exception: %s", formatted.str().c_str());
    } catch (const std::exception& ex) {
      gpg::Die("Unhandled exception:\n\n%s", ex.what());
    }
    return exitCode;
  }

  /// The CRT's copies of the command line, from gArguments.
  void PublishArguments()
  {
    gArgv.clear();
    for (std::string& argument : gArguments) {
      gArgv.push_back(argument.data());
    }
    gArgv.push_back(nullptr);
    __argc = static_cast<int>(gArguments.size());
    __argv = gArgv.data();
  }

  /// Waits for the activity's first surface and makes its size the program's display mode, also
  /// for a screen smaller than the 1024 x 768 the engine holds as the least a mode may be.
  void TakeScreenMode()
  {
    Win32Android_WaitForScreenSurface();
    const int width = ::GetSystemMetrics(SM_CXSCREEN);
    const int height = ::GetSystemMetrics(SM_CYSCREEN);
    if (moho::CFG_GetArgOptionAliases(moho::CFG_GetFullscreenOptionAliases(), 0, nullptr) ||
        moho::CFG_GetArgOptionAliases(moho::CFG_GetWindowedOptionAliases(), 0, nullptr) || moho::CFG_HasMaximizeOption()) {
      return;
    }
    gArguments.insert(gArguments.end(), {"/fullscreen", std::to_string(width), std::to_string(height)});
    PublishArguments();
    moho::wnd_DefaultCreateWidth = (std::min)(moho::wnd_DefaultCreateWidth, width);
    moho::wnd_DefaultCreateHeight = (std::min)(moho::wnd_DefaultCreateHeight, height);
    Report("display mode " + std::to_string(width) + "x" + std::to_string(height));
  }

  void* EngineThread(void*)
  {
    TakeScreenMode();
    const int exitCode = RunProgram();
    Report("exit " + std::to_string(exitCode));
    EndProcess(exitCode);
  }

  /// The folders a Windows user profile always has, which SHGetFolderPath names: the POSIX layer
  /// puts them under HOME, where a new HOME has none of them yet.
  void CreateProfileFolders()
  {
    for (const int folder : {CSIDL_LOCAL_APPDATA, CSIDL_APPDATA, CSIDL_PERSONAL, CSIDL_DESKTOPDIRECTORY}) {
      char path[MAX_PATH];
      if (::SHGetFolderPathA(nullptr, folder, nullptr, SHGFP_TYPE_CURRENT, path) == S_OK) {
        const int result = ::SHCreateDirectoryExA(nullptr, path, nullptr);
        if (result != ERROR_SUCCESS && result != ERROR_ALREADY_EXISTS) {
          Report(std::string("cannot create ") + path + " (error " + std::to_string(result) + ")");
        }
      }
    }
  }

  std::string Utf8(JNIEnv* const env, const jstring text)
  {
    if (text == nullptr) {
      return std::string();
    }
    const char* const chars = env->GetStringUTFChars(text, nullptr);
    std::string out = chars != nullptr ? chars : "";
    env->ReleaseStringUTFChars(text, chars);
    return out;
  }

  /// A Java string's code points: wchar_t holds a whole one on Android.
  std::wstring Wide(JNIEnv* const env, const jstring text)
  {
    std::wstring out;
    if (text == nullptr) {
      return out;
    }
    const jsize length = env->GetStringLength(text);
    const jchar* const units = env->GetStringChars(text, nullptr);
    if (units == nullptr) {
      return out;
    }
    for (jsize at = 0; at < length; ++at) {
      const std::uint32_t unit = units[at];
      if (unit >= 0xD800 && unit < 0xDC00 && at + 1 < length && units[at + 1] >= 0xDC00 && units[at + 1] < 0xE000) {
        out += static_cast<wchar_t>(0x10000 + ((unit - 0xD800) << 10) + (units[at + 1] - 0xDC00));
        ++at;
      } else {
        out += static_cast<wchar_t>(unit);
      }
    }
    env->ReleaseStringChars(text, units);
    return out;
  }
} // namespace

/// Starts the program on the engine's thread: `arguments` is its command line, the program's
/// path first; `home` and `temp` its HOME and TMPDIR; the host's lines go to the end of
/// `reportPath`.
/// False when it was started already (one program per process, as on Windows) or cannot start.
extern "C" JNIEXPORT jboolean JNICALL Java_io_github_m3rt1n99_fafre_Game_start(
  JNIEnv* const env, jclass, const jobjectArray arguments, const jstring home, const jstring temp, const jstring reportPath
)
{
  if (arguments == nullptr || env->GetArrayLength(arguments) == 0 || gStarted.exchange(true)) {
    return JNI_FALSE;
  }
  gStartTime = std::chrono::steady_clock::now();
  // After the lines the activity wrote first.
  gReportFile = ::open(Utf8(env, reportPath).c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);

  const jsize count = env->GetArrayLength(arguments);
  for (jsize at = 0; at < count; ++at) {
    const auto argument = static_cast<jstring>(env->GetObjectArrayElement(arguments, at));
    gArguments.push_back(Utf8(env, argument));
    env->DeleteLocalRef(argument);
  }
  std::string commandLine;
  for (const std::string& argument : gArguments) {
    commandLine += (commandLine.empty() ? "" : " ") + argument;
  }
  PublishArguments();
  (void)::setenv("HOME", Utf8(env, home).c_str(), 1);
  (void)::setenv("TMPDIR", Utf8(env, temp).c_str(), 1);
  Report("program: " + commandLine);
  CreateProfileFolders();
  Win32Android_SetFirstFrameHandler(&ReportFirstFrame);

  pthread_attr_t attributes;
  (void)::pthread_attr_init(&attributes);
  (void)::pthread_attr_setstacksize(&attributes, kMainThreadStackSize);
  (void)::pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
  pthread_t thread;
  const int error = ::pthread_create(&thread, &attributes, &EngineThread, nullptr);
  (void)::pthread_attr_destroy(&attributes);
  if (error != 0) {
    Report(std::string("no engine thread: ") + std::strerror(error));
    // No program runs (Game_running), and a next start begins anew.
    (void)::close(gReportFile);
    gReportFile = -1;
    gArguments.clear();
    gStarted.store(false);
    return JNI_FALSE;
  }
  return JNI_TRUE;
}

/// Whether the program runs in this process, which it does until the process ends: Android can make
/// the activity anew around it.
extern "C" JNIEXPORT jboolean JNICALL Java_io_github_m3rt1n99_fafre_Game_running(JNIEnv*, jclass)
{
  return gStarted.load() ? JNI_TRUE : JNI_FALSE;
}

/// The activity's surface and its size in pixels, or null when it is gone.
extern "C" JNIEXPORT void JNICALL
Java_io_github_m3rt1n99_fafre_Game_surface(JNIEnv* const env, jclass, const jobject surface, const jint width, const jint height)
{
  ANativeWindow* const window = surface != nullptr ? ANativeWindow_fromSurface(env, surface) : nullptr;
  if (window != nullptr) {
    // Before the surface: with it the program goes on, and reads the size (TakeScreenMode).
    Win32Android_SetScreenSize(width, height);
    Report("surface " + std::to_string(width) + "x" + std::to_string(height));
  } else {
    Report("no surface");
  }
  if (Win32Android_SetScreenSurface(window) == FALSE) {
    Report("the program is busy: it lets go of the old surface when it can");
  }
  if (window != nullptr) {
    // The layer holds a reference of its own.
    ANativeWindow_release(window);
  }
}

/// The activity came to the front or went to the back.
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_foreground(JNIEnv*, jclass, const jboolean front)
{
  Report(front != JNI_FALSE ? "to the front" : "to the back");
  Win32Android_SetForeground(front != JNI_FALSE ? TRUE : FALSE);
}

extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_mouseMove(JNIEnv*, jclass, const jint x, const jint y)
{
  Win32Android_MouseMove(x, y);
}

/// `button`: Game.BUTTON_LEFT and the rest, which are the layer's WIN32ANDROID_BUTTON_ numbers.
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_mouseButton(
  JNIEnv*, jclass, const jint button, const jboolean down, const jint x, const jint y
)
{
  Win32Android_MouseButton(button, down != JNI_FALSE ? TRUE : FALSE, x, y);
}

/// `delta` in WHEEL_DELTA units of 120 per notch, positive away from the user.
extern "C" JNIEXPORT void JNICALL
Java_io_github_m3rt1n99_fafre_Game_mouseWheel(JNIEnv*, jclass, const jint delta, const jint x, const jint y)
{
  Win32Android_MouseWheel(delta, x, y);
}

/// A key of Android's (`keyCode`, AKEYCODE_*) went down or up; `scanCode` is the event's Linux
/// scan code and `character` what the press types (0: none). A key a PC keyboard has not types its
/// character all the same.
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_key(
  JNIEnv*, jclass, const jint keyCode, const jint scanCode, const jboolean down, const jint character
)
{
  WindowsKey key;
  if (KeyFromAndroid(keyCode, scanCode, &key)) {
    Win32Android_Key(key.virtualKey, key.scanCode, down != JNI_FALSE ? TRUE : FALSE, key.extended ? TRUE : FALSE, KeyCharacter(key, character));
  } else if (down != JNI_FALSE && character != 0) {
    const wchar_t text[2] = {static_cast<wchar_t>(character), L'\0'};
    Win32Android_Text(text);
  }
}

/// Text without a key: an input method's commit.
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_text(JNIEnv* const env, jclass, const jstring text)
{
  const std::wstring wide = Wide(env, text);
  if (!wide.empty()) {
    Win32Android_Text(wide.c_str());
  }
}
