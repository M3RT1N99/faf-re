// libfafdeviceprobe.so, the device probe for the graphics plan (release 0.4.1); see README.md.
// Shared pieces: a small JSON writer, a clock, the section interface and the PNG writer.
#pragma once

#include <stdint.h>
#include <time.h>

#include <functional>
#include <string>
#include <vector>

namespace faf_probe
{
  // ------------------------------------------------------------------------------------------------
  // JSON
  // ------------------------------------------------------------------------------------------------

  // `text` as a JSON string literal (quotes, backslashes and control characters escaped; bytes
  // >= 0x80 are passed through, so UTF-8 stays UTF-8; anything else a driver returns is replaced).
  std::string JsonString(const std::string& text);

  // An object or array built member by member; Raw() values are inserted as they are.
  class Json
  {
  public:
    explicit Json(char open = '{') : mOpen(open) {}

    Json& Raw(const std::string& key, const std::string& rawValue);
    Json& Str(const std::string& key, const std::string& value) { return Raw(key, JsonString(value)); }
    Json& Num(const std::string& key, long long value) { return Raw(key, std::to_string(value)); }
    Json& Unsigned(const std::string& key, unsigned long long value) { return Raw(key, std::to_string(value)); }
    Json& Real(const std::string& key, double value, int decimals = 3);
    Json& Bool(const std::string& key, bool value) { return Raw(key, value ? "true" : "false"); }
    Json& Null(const std::string& key) { return Raw(key, "null"); }
    // Array elements (for a Json('[')).
    Json& Add(const std::string& rawValue);
    Json& AddStr(const std::string& value) { return Add(JsonString(value)); }

    std::string Text() const;
    bool Empty() const { return mBody.empty(); }

  private:
    char mOpen;
    std::string mBody;
  };

  std::string RealText(double value, int decimals = 3);

  // ------------------------------------------------------------------------------------------------
  // Time
  // ------------------------------------------------------------------------------------------------

  inline double NowMs()
  {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) * 1000.0 + static_cast<double>(ts.tv_nsec) / 1.0e6;
  }

  // ------------------------------------------------------------------------------------------------
  // Sections
  // ------------------------------------------------------------------------------------------------

  struct Options
  {
    std::string outDir = ".";  // where deviceprobe.json and the PNGs go (the app's run directory)
    int glslangRepeats = 5;    // compiles per shader: the first (cold) and the rest (warm)
  };

  // A section's result: its JSON object (with "status") and one line for the console.
  struct SectionResult
  {
    std::string json;
    std::string summary;
  };

  // Each runs in its own child process (DeviceProbe.cpp), so a driver that crashes or hangs costs
  // only its own section.
  SectionResult RunVulkanReport(const Options& options);
  SectionResult RunVulkanRender(const Options& options);
  SectionResult RunGlesReport(const Options& options);
  SectionResult RunGlesRender(const Options& options);
  SectionResult RunGlslang(const Options& options);

  // Driver teardown (vkDestroyDevice, vkDestroyInstance, eglTerminate, ...). The sections' objects
  // hand their cleanup to Teardown() from their destructors. It runs at once, unless DeferTeardown(true)
  // was called: a forked section's child does that, so the cleanup waits in a queue until the child has
  // sent its result to the parent, and a driver that crashes or hangs while tearing down can no longer
  // take the section's report with it (DeviceProbe.cpp, RunInChild). RunDeferredTeardown() then runs
  // the queue in the order it was filled (the destructors' order: device before instance) and returns
  // how many entries it ran.
  void Teardown(std::function<void()> action);
  void DeferTeardown(bool defer);
  int RunDeferredTeardown();

  // ------------------------------------------------------------------------------------------------
  // The test pattern both render sections draw (and check), and the PNG writer
  // ------------------------------------------------------------------------------------------------

  constexpr int kPatternSize = 256;

  // The RGBA8 value the pattern shaders write at window pixel (x, y): red = x & 255, green = y & 255,
  // blue = a 32-pixel checkerboard (0 or 255), alpha 255. Window coordinates as the API defines them
  // (Vulkan: y down from the top; OpenGL ES: y up from the bottom), so a readback in the API's own
  // row order matches row for row.
  inline void PatternPixel(int x, int y, uint8_t out[4])
  {
    out[0] = static_cast<uint8_t>(x & 255);
    out[1] = static_cast<uint8_t>(y & 255);
    out[2] = (((x >> 5) ^ (y >> 5)) & 1) != 0 ? 255 : 0;
    out[3] = 255;
  }

  struct PatternCheck
  {
    int mismatched = 0;   // pixels with any channel off by more than 1
    int maxError = 0;     // largest channel difference
    int firstX = -1;      // first mismatching pixel
    int firstY = -1;
    std::string verdict;  // "exact", "within 1", "wrong"
  };

  // `rows` is kPatternSize rows of kPatternSize RGBA8 pixels, row r at window y = r.
  PatternCheck CheckPattern(const uint8_t* rows, size_t rowPitch);

  // Writes an 8-bit RGBA PNG (rows top to bottom); false with `error` set on failure.
  bool WritePng(const std::string& path, int width, int height, const uint8_t* rows, size_t rowPitch,
                bool bottomUp, std::string& error);
} // namespace faf_probe
