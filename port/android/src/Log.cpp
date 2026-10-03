#include "Log.h"

#include <android/log.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <ctime>

namespace faf::android {

  namespace {

    constexpr char kLogcatTag[] = "faf_android";

    /// logcat truncates a message at about 4 KB; longer texts go out in pieces.
    constexpr std::size_t kLogcatChunk = 4000;

    /// Lines kept while no log file is open; argument parsing logs a few dozen.
    constexpr std::size_t kMaxEarlyLines = 2000;

    int LogcatPriority(const LogLevel level)
    {
      switch (level) {
      case LogLevel::Debug:
        return ANDROID_LOG_DEBUG;
      case LogLevel::Info:
        return ANDROID_LOG_INFO;
      case LogLevel::Warning:
        return ANDROID_LOG_WARN;
      case LogLevel::Error:
        return ANDROID_LOG_ERROR;
      }
      return ANDROID_LOG_INFO;
    }

    char LevelLetter(const LogLevel level)
    {
      switch (level) {
      case LogLevel::Debug:
        return 'D';
      case LogLevel::Info:
        return 'I';
      case LogLevel::Warning:
        return 'W';
      case LogLevel::Error:
        return 'E';
      }
      return 'I';
    }

    void WriteLogcat(const LogLevel level, const std::string_view text)
    {
      const int priority = LogcatPriority(level);
      std::size_t offset = 0;
      do {
        std::size_t length = std::min(kLogcatChunk, text.size() - offset);
        // Do not cut a UTF-8 sequence in two.
        while (offset + length < text.size() && length > 0 &&
               (static_cast<unsigned char>(text[offset + length]) & 0xC0) == 0x80) {
          --length;
        }
        if (length == 0) {
          length = std::min(kLogcatChunk, text.size() - offset);
        }
        const std::string chunk(text.substr(offset, length));
        __android_log_write(priority, kLogcatTag, chunk.c_str());
        offset += length;
      } while (offset < text.size());
    }

    /// Local wall-clock time "HH:MM:SS.mmm", so lines line up with launcher.log.
    std::string TimeStamp()
    {
      timespec now{};
      clock_gettime(CLOCK_REALTIME, &now);
      tm local{};
      localtime_r(&now.tv_sec, &local);
      char buffer[32];
      std::snprintf(
        buffer,
        sizeof(buffer),
        "%02d:%02d:%02d.%03ld",
        local.tm_hour,
        local.tm_min,
        local.tm_sec,
        static_cast<long>(now.tv_nsec / 1000000)
      );
      return buffer;
    }

    /// Empties the file, then keeps it open for appending. The crash handler
    /// appends through its own descriptor while other threads may still log
    /// for a moment; a stream with its own write position would overwrite the
    /// crash report with those last lines.
    std::FILE* OpenTruncated(const std::string& path, std::string* error)
    {
      // 'e' = O_CLOEXEC (bionic); nothing this process spawns should inherit it.
      std::FILE* file = std::fopen(path.c_str(), "we");
      if (file != nullptr) {
        std::fclose(file);
        file = std::fopen(path.c_str(), "ae");
      }
      if (file == nullptr && error != nullptr) {
        *error = "cannot open " + path + ": " + std::strerror(errno);
      }
      return file;
    }

  } // namespace

  Log& Log::Get()
  {
    // Never destroyed: see the header.
    static Log* const instance = new Log();
    return *instance;
  }

  bool Log::OpenFile(const std::string& path, std::string* error)
  {
    // Keep the previous run next to it ("x.log" -> "x.1.log"), so a crash
    // report survives one more start. A failed rename only costs that copy.
    const std::size_t dot = path.rfind(".log");
    if (dot != std::string::npos && dot + 4 == path.size()) {
      const std::string previous = path.substr(0, dot) + ".1.log";
      std::remove(previous.c_str());
      std::rename(path.c_str(), previous.c_str());
    }
    std::FILE* file = OpenTruncated(path, error);
    if (file == nullptr) {
      return false;
    }
    const std::lock_guard lock(mMutex);
    if (mFile != nullptr) {
      std::fclose(mFile);
    }
    mFile = file;
    for (const std::string& line : mEarlyLines) {
      std::fputs(line.c_str(), mFile);
    }
    mEarlyLines.clear();
    mEarlyLines.shrink_to_fit();
    std::fflush(mFile);
    return true;
  }

  bool Log::OpenGameLog(const std::string& path, std::string* error)
  {
    std::FILE* file = OpenTruncated(path, error);
    if (file == nullptr) {
      return false;
    }
    const std::lock_guard lock(mMutex);
    if (mGameLog != nullptr) {
      std::fclose(mGameLog);
    }
    mGameLog = file;
    return true;
  }

  void Log::WriteFileLineLocked(const LogLevel level, const std::string_view stamp, const std::string_view text)
  {
    // Continuation lines of a multi-line message are indented so every line
    // that starts at column 0 is a new entry.
    std::string line;
    line.reserve(stamp.size() + text.size() + 8);
    line.append(stamp);
    line.push_back(' ');
    line.push_back(LevelLetter(level));
    line.push_back(' ');
    for (const char c : text) {
      // An embedded NUL would end the line early in every C-string consumer.
      line.push_back(c == '\0' ? '?' : c);
      if (c == '\n') {
        line.append("    ");
      }
    }
    line.push_back('\n');

    if (mFile != nullptr) {
      std::fwrite(line.data(), 1, line.size(), mFile);
      // A native crash must not lose the lines that lead up to it.
      std::fflush(mFile);
    } else if (mEarlyLines.size() < kMaxEarlyLines) {
      mEarlyLines.push_back(std::move(line));
    }
  }

  void Log::Write(const LogLevel level, const std::string_view text)
  {
    WriteLogcat(level, text);
    const std::string stamp = TimeStamp();
    const std::lock_guard lock(mMutex);
    WriteFileLineLocked(level, stamp, text);
  }

  void Log::ScriptLine(const std::string_view text)
  {
    std::string tagged = "script: ";
    tagged.append(text);
    WriteLogcat(LogLevel::Info, tagged);
    const std::string stamp = TimeStamp();
    const std::lock_guard lock(mMutex);
    WriteFileLineLocked(LogLevel::Info, stamp, tagged);
    if (mGameLog != nullptr) {
      std::fputs("info: ", mGameLog);
      std::fwrite(text.data(), 1, text.size(), mGameLog);
      std::fputc('\n', mGameLog);
      std::fflush(mGameLog);
    }
  }

  void Log::Close()
  {
    const std::lock_guard lock(mMutex);
    if (mFile != nullptr) {
      std::fclose(mFile);
      mFile = nullptr;
    }
    if (mGameLog != nullptr) {
      std::fclose(mGameLog);
      mGameLog = nullptr;
    }
  }

} // namespace faf::android
