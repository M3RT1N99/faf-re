#pragma once

// The runtime's log: every line goes to logcat (tag "faf_android") and, once
// the launch arguments are read, to <root>/logs/faf_android_<vulkan|gles>.log
// (one per graphics backend; the previous run moves to .1.log), which the
// launcher shows and `adb pull` collects. Lines logged before the file is open
// (argument parsing) are kept and written first.
//
// The game's own log (the file named by /log, game.sclog) receives the LOG()
// output of the scripts in the "info: ..." form the desktop game writes, so
// tools that read FA logs can read this one.
//
// Thread-safe; the worker thread and Diligent's message callback log too. The
// instance is never destroyed, so a detached worker that is still finishing
// after android_main returned can keep logging.

#include <cstdio>
#include <format>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace faf::android {

  enum class LogLevel
  {
    Debug,
    Info,
    Warning,
    Error,
  };

  class Log
  {
  public:
    static Log& Get();

    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;

    /// Opens (truncates) the runtime log file and writes the lines logged so far.
    bool OpenFile(const std::string& path, std::string* error = nullptr);

    /// Opens (truncates) the game log named by /log.
    bool OpenGameLog(const std::string& path, std::string* error = nullptr);

    void Write(LogLevel level, std::string_view text);

    /// A LOG()/print() line of a game script: runtime log and game log.
    void ScriptLine(std::string_view text);

    /// Flushes and closes both files. Later lines go to logcat and are kept
    /// for the next OpenFile (a second run in the same process).
    void Close();

  private:
    Log() = default;

    void WriteFileLineLocked(LogLevel level, std::string_view stamp, std::string_view text);

    std::mutex mMutex;
    std::FILE* mFile = nullptr;
    std::FILE* mGameLog = nullptr;
    /// Formatted lines logged while no file is open, written when one opens.
    std::vector<std::string> mEarlyLines;
  };

  template <class... Args>
  void LogDebug(std::format_string<Args...> format, Args&&... args)
  {
    Log::Get().Write(LogLevel::Debug, std::format(format, std::forward<Args>(args)...));
  }

  template <class... Args>
  void LogInfo(std::format_string<Args...> format, Args&&... args)
  {
    Log::Get().Write(LogLevel::Info, std::format(format, std::forward<Args>(args)...));
  }

  template <class... Args>
  void LogWarning(std::format_string<Args...> format, Args&&... args)
  {
    Log::Get().Write(LogLevel::Warning, std::format(format, std::forward<Args>(args)...));
  }

  template <class... Args>
  void LogError(std::format_string<Args...> format, Args&&... args)
  {
    Log::Get().Write(LogLevel::Error, std::format(format, std::forward<Args>(args)...));
  }

} // namespace faf::android
