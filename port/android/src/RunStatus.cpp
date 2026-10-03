#include "RunStatus.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <format>
#include <utility>

#include "Log.h"

namespace faf::android {

  namespace {

    void AppendCodePointUtf8(std::string& out, const char32_t cp)
    {
      if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
      } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      }
    }

    /// Length of the well-formed UTF-8 sequence at `text[i]`, or 0 when it is
    /// not one (stray continuation byte, overlong form, surrogate, > U+10FFFF,
    /// truncated). Lua error messages and file names can carry any bytes.
    std::size_t Utf8SequenceLength(const std::string_view text, const std::size_t i)
    {
      const auto byte = [&](const std::size_t k) {
        return static_cast<unsigned char>(text[k]);
      };
      const unsigned char lead = byte(i);
      std::size_t length = 0;
      char32_t cp = 0;
      char32_t minimum = 0;
      if (lead < 0x80) {
        return 1;
      }
      if ((lead & 0xE0) == 0xC0) {
        length = 2;
        cp = lead & 0x1Fu;
        minimum = 0x80;
      } else if ((lead & 0xF0) == 0xE0) {
        length = 3;
        cp = lead & 0x0Fu;
        minimum = 0x800;
      } else if ((lead & 0xF8) == 0xF0) {
        length = 4;
        cp = lead & 0x07u;
        minimum = 0x10000;
      } else {
        return 0;
      }
      if (i + length > text.size()) {
        return 0;
      }
      for (std::size_t k = 1; k < length; ++k) {
        if ((byte(i + k) & 0xC0) != 0x80) {
          return 0;
        }
        cp = (cp << 6) | (byte(i + k) & 0x3Fu);
      }
      if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return 0;
      }
      return length;
    }

    void AppendJsonString(std::string& out, const std::string_view text)
    {
      out.push_back('"');
      for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        switch (c) {
        case '"':
          out.append("\\\"");
          ++i;
          continue;
        case '\\':
          out.append("\\\\");
          ++i;
          continue;
        case '\n':
          out.append("\\n");
          ++i;
          continue;
        case '\r':
          out.append("\\r");
          ++i;
          continue;
        case '\t':
          out.append("\\t");
          ++i;
          continue;
        default:
          break;
        }
        if (c < 0x20) {
          out.append(std::format("\\u{:04x}", static_cast<unsigned>(c)));
          ++i;
          continue;
        }
        const std::size_t length = Utf8SequenceLength(text, i);
        if (length == 0) {
          AppendCodePointUtf8(out, 0xFFFD);
          ++i;
          continue;
        }
        out.append(text.substr(i, length));
        i += length;
      }
      out.push_back('"');
    }

    std::string JsonNumber(const double value)
    {
      return std::isfinite(value) ? std::format("{:.1f}", value) : std::string("0.0");
    }

    std::string UtcTimestamp()
    {
      const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
      std::tm utc{};
      gmtime_r(&now, &utc);
      char buffer[32];
      std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
      return buffer;
    }

    bool WriteAll(const int fd, const std::string& text)
    {
      std::size_t written = 0;
      while (written < text.size()) {
        const ssize_t n = ::write(fd, text.data() + written, text.size() - written);
        if (n < 0) {
          if (errno == EINTR) {
            continue;
          }
          return false;
        }
        written += static_cast<std::size_t>(n);
      }
      return true;
    }

    /// Writes `text` to `path` atomically: temporary file, fsync, rename.
    bool ReplaceFile(const std::string& path, const std::string& text, std::string& error)
    {
      const std::string temporary = path + ".tmp";
      const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0660);
      if (fd < 0) {
        error = "cannot create " + temporary + ": " + std::strerror(errno);
        return false;
      }
      const bool ok = WriteAll(fd, text) && ::fsync(fd) == 0;
      const int writeErrno = errno;
      ::close(fd);
      if (!ok) {
        error = "cannot write " + temporary + ": " + std::strerror(writeErrno);
        ::unlink(temporary.c_str());
        return false;
      }
      if (::rename(temporary.c_str(), path.c_str()) != 0) {
        error = "cannot replace " + path + ": " + std::strerror(errno);
        ::unlink(temporary.c_str());
        return false;
      }
      return true;
    }

  } // namespace

  std::string_view ToString(const RunState state)
  {
    switch (state) {
    case RunState::Loading:
      return "loading";
    case RunState::Running:
      return "running";
    case RunState::Error:
      return "error";
    case RunState::Exited:
      return "exited";
    }
    return "error";
  }

  std::string_view ToString(const Stage stage)
  {
    switch (stage) {
    case Stage::Args:
      return "args";
    case Stage::DataPath:
      return "datapath";
    case Stage::Mount:
      return "mount";
    case Stage::Splash:
      return "splash";
    case Stage::Renderer:
      return "renderer";
    case Stage::Frame:
      return "frame";
    }
    return "args";
  }

  std::string ToJson(const RunStatusData& data, const std::string_view timestamp, const std::string_view versionName)
  {
    std::string out;
    out.reserve(512);
    out.append("{\"schema\":1,\"state\":");
    AppendJsonString(out, ToString(data.state));
    out.append(",\"stage\":");
    AppendJsonString(out, ToString(data.stage));
    out.append(",\"message\":");
    AppendJsonString(out, data.message);
    out.append(",\"renderer\":");
    AppendJsonString(out, data.renderer);
    out.append(std::format(
      ",\"mounts\":{},\"archives\":{},\"archiveEntries\":{}",
      data.mounts,
      data.archives,
      data.archiveEntries
    ));
    out.append(",\"scriptMs\":").append(JsonNumber(data.scriptMs));
    out.append(",\"mountMs\":").append(JsonNumber(data.mountMs));
    out.append(",\"splash\":");
    AppendJsonString(out, data.splash);
    out.append(",\"splashFormat\":");
    AppendJsonString(out, data.splashFormat);
    out.append(std::format(",\"width\":{},\"height\":{}", data.width, data.height));
    out.append(",\"timestamp\":");
    AppendJsonString(out, timestamp);
    out.append(",\"versionName\":");
    AppendJsonString(out, versionName);
    out.append("}\n");
    return out;
  }

  RunStatus::RunStatus(std::string path, std::string versionName)
    : mPath(std::move(path))
    , mVersionName(std::move(versionName))
  {}

  bool RunStatus::Finish(const RunState state, const std::optional<Stage> stage, std::string message)
  {
    const std::lock_guard lock(mMutex);
    if (mTerminal) {
      return false;
    }
    mData.state = state;
    if (stage) {
      mData.stage = *stage;
    }
    mData.message = std::move(message);
    mTerminal = state == RunState::Error || state == RunState::Exited;
    WriteLocked();
    return true;
  }

  bool RunStatus::IsTerminal() const
  {
    const std::lock_guard lock(mMutex);
    return mTerminal;
  }

  RunStatusData RunStatus::Snapshot() const
  {
    const std::lock_guard lock(mMutex);
    return mData;
  }

  void RunStatus::WriteLocked()
  {
    std::string error;
    if (ReplaceFile(mPath, ToJson(mData, UtcTimestamp(), mVersionName), error)) {
      return;
    }
    // Once is enough: the next writes fail for the same reason.
    if (!mReportedWriteError) {
      mReportedWriteError = true;
      LogError("status.json: {}", error);
    }
  }

} // namespace faf::android
