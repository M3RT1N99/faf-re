// JSON, the pattern check and the PNG writer of libfafdeviceprobe.so.

#include "Probe.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <zlib.h>

namespace faf_probe
{
  namespace
  {
    bool gDeferTeardown = false;
    std::vector<std::function<void()>> gTeardown;
  } // namespace

  void Teardown(std::function<void()> action)
  {
    if (gDeferTeardown) {
      gTeardown.push_back(std::move(action));
    } else {
      action();
    }
  }

  void DeferTeardown(const bool defer)
  {
    gDeferTeardown = defer;
  }

  int RunDeferredTeardown()
  {
    std::vector<std::function<void()>> queue;
    queue.swap(gTeardown);
    for (const std::function<void()>& action : queue) {
      action();
    }
    return static_cast<int>(queue.size());
  }

  std::string JsonString(const std::string& text)
  {
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (const char ch : text) {
      const unsigned char c = static_cast<unsigned char>(ch);
      switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
          if (c < 0x20 || c == 0x7f) {
            char escaped[8];
            snprintf(escaped, sizeof(escaped), "\\u%04x", c);
            out += escaped;
          } else {
            out.push_back(static_cast<char>(c));
          }
      }
    }
    out.push_back('"');
    return out;
  }

  std::string RealText(const double value, const int decimals)
  {
    if (value != value) {
      return "null";
    }
    char text[64];
    snprintf(text, sizeof(text), "%.*f", decimals, value);
    return text;
  }

  Json& Json::Raw(const std::string& key, const std::string& rawValue)
  {
    if (!mBody.empty()) {
      mBody.push_back(',');
    }
    mBody += JsonString(key);
    mBody.push_back(':');
    mBody += rawValue;
    return *this;
  }

  Json& Json::Real(const std::string& key, const double value, const int decimals)
  {
    return Raw(key, RealText(value, decimals));
  }

  Json& Json::Add(const std::string& rawValue)
  {
    if (!mBody.empty()) {
      mBody.push_back(',');
    }
    mBody += rawValue;
    return *this;
  }

  std::string Json::Text() const
  {
    std::string out;
    out.reserve(mBody.size() + 2);
    out.push_back(mOpen);
    out += mBody;
    out.push_back(mOpen == '{' ? '}' : ']');
    return out;
  }

  PatternCheck CheckPattern(const uint8_t* rows, const size_t rowPitch)
  {
    PatternCheck check;
    for (int y = 0; y < kPatternSize; ++y) {
      const uint8_t* row = rows + static_cast<size_t>(y) * rowPitch;
      for (int x = 0; x < kPatternSize; ++x) {
        uint8_t expected[4];
        PatternPixel(x, y, expected);
        int worst = 0;
        for (int c = 0; c < 4; ++c) {
          const int d = abs(static_cast<int>(row[x * 4 + c]) - static_cast<int>(expected[c]));
          worst = d > worst ? d : worst;
        }
        check.maxError = worst > check.maxError ? worst : check.maxError;
        if (worst > 1) {
          if (check.mismatched == 0) {
            check.firstX = x;
            check.firstY = y;
          }
          ++check.mismatched;
        }
      }
    }
    check.verdict = check.maxError == 0 ? "exact" : (check.mismatched == 0 ? "within 1" : "wrong");
    return check;
  }

  namespace
  {
    void Be32(std::string& out, const uint32_t v)
    {
      out.push_back(static_cast<char>(v >> 24));
      out.push_back(static_cast<char>(v >> 16));
      out.push_back(static_cast<char>(v >> 8));
      out.push_back(static_cast<char>(v));
    }

    void Chunk(std::string& out, const char type[4], const std::string& data)
    {
      Be32(out, static_cast<uint32_t>(data.size()));
      std::string body(type, 4);
      body += data;
      out += body;
      Be32(out, static_cast<uint32_t>(crc32(0, reinterpret_cast<const Bytef*>(body.data()), static_cast<uInt>(body.size()))));
    }
  } // namespace

  bool WritePng(const std::string& path, const int width, const int height, const uint8_t* rows, const size_t rowPitch,
                const bool bottomUp, std::string& error)
  {
    std::string raw;
    raw.reserve(static_cast<size_t>(height) * (static_cast<size_t>(width) * 4 + 1));
    for (int y = 0; y < height; ++y) {
      const int source = bottomUp ? height - 1 - y : y;
      raw.push_back('\0');  // filter: none
      raw.append(reinterpret_cast<const char*>(rows + static_cast<size_t>(source) * rowPitch), static_cast<size_t>(width) * 4);
    }
    uLongf packedSize = compressBound(static_cast<uLong>(raw.size()));
    std::string packed(packedSize, '\0');
    if (compress2(reinterpret_cast<Bytef*>(&packed[0]), &packedSize, reinterpret_cast<const Bytef*>(raw.data()),
                  static_cast<uLong>(raw.size()), 6) != Z_OK) {
      error = "zlib compress2 failed";
      return false;
    }
    packed.resize(packedSize);

    std::string png("\x89PNG\r\n\x1a\n", 8);
    std::string header;
    Be32(header, static_cast<uint32_t>(width));
    Be32(header, static_cast<uint32_t>(height));
    header.push_back(8);  // bit depth
    header.push_back(6);  // colour type RGBA
    header.push_back(0);  // deflate
    header.push_back(0);  // adaptive filtering
    header.push_back(0);  // no interlace
    Chunk(png, "IHDR", header);
    Chunk(png, "IDAT", packed);
    Chunk(png, "IEND", std::string());

    const std::string temp = path + ".tmp";
    const int fd = open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
      error = "cannot create " + temp + ": " + strerror(errno);
      return false;
    }
    size_t done = 0;
    while (done < png.size()) {
      const ssize_t n = write(fd, png.data() + done, png.size() - done);
      if (n < 0 && errno == EINTR) {
        continue;
      }
      if (n <= 0) {
        error = "cannot write " + temp + ": " + strerror(errno);
        close(fd);
        unlink(temp.c_str());
        return false;
      }
      done += static_cast<size_t>(n);
    }
    close(fd);
    if (rename(temp.c_str(), path.c_str()) != 0) {
      error = "cannot rename " + temp + ": " + strerror(errno);
      unlink(temp.c_str());
      return false;
    }
    return true;
  }
} // namespace faf_probe
