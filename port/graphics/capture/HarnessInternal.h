#pragma once

// Shared state of the frame harness (GalCapture.h). Not for engine code.

#include <cstdint>
#include <string>
#include <vector>

namespace port::graphics::capture::detail
{
  // Process exit codes of a harness run. scripts/port/gfx_capture.py maps them back to names.
  enum ExitCode : int
  {
    kExitComplete = 0,      // every requested frame captured (the summary says so too)
    kExitRefused = 10,      // bad options, a FAF_* variable set, or a pin could not be applied
    kExitVisibility = 11,   // a window of this process became visible or foreground
    kExitModalUi = 12,      // the engine tried to open a message box or a modal dialog
    kExitCaptureFailed = 13, // a capture frame was not rendered, or the readback failed
    kExitScriptFailed = 14   // the `/galscript` navigation script did not load, or one of its steps raised an error
  };

  struct HarnessConfig
  {
    bool active = false;
    std::wstring outDir;           // `/galharness <dir>`, without a trailing separator
    std::vector<unsigned> frames;  // `/galframes 60,300,900` (`none`: no captures)
    unsigned exitFrame = 0;        // `/galexitframe <n>`, default: the last capture frame
    double paceFps = 30.0;         // `/galpace <fps>`, 0 = as fast as the engine runs
    float fixedFrameSeconds = 0;   // 1 / `/framerate <n>`; the harness refuses to run without it
    std::uint32_t seed = 0;        // `/galseed <n>`: the global random stream's seed
    bool noPins = false;           // `/galnopins`: wall clock and time seed, to show what the pins fix
    std::wstring scriptPath;       // `/galscript <file.lua>`: UI actions by frame number (GalCapture.cpp)
    std::string commandLine;       // UTF-8, for the summary
  };

  [[nodiscard]] const HarnessConfig& Config() noexcept;

  // harness.log in the output directory: one line per call, flushed, safe from any thread.
  void Log(const char* format, ...);

  // Writes the summary with `status` failed and `reason`, then ends the process with `code`
  // (TerminateProcess: no teardown, so nothing else can open a window on the way out).
  [[noreturn]] void Abort(int code, const char* format, ...);

  // HarnessSandbox.cpp: the interception report (JSON object text) and the in-process monitor.
  [[nodiscard]] std::string SandboxReportJson();
  [[nodiscard]] bool WindowsHiddenAndNotForeground(std::string* violation);

  // HarnessPins.cpp: the virtual clock the Lua time functions read, and the pin report.
  void AdvanceClock(double seconds) noexcept;
  [[nodiscard]] double ClockSeconds() noexcept;
  [[nodiscard]] bool InstallPins(std::string* error);   // static-init time
  [[nodiscard]] bool VerifyPinsOnFirstFrame(std::string* error);
  void ReseedRandomStream() noexcept;
  [[nodiscard]] std::string PinsReportJson();

  // GalCapture.cpp: the summary writer, shared with Abort, and the progress the stall reporter
  // watches (CScApp::Main calls plus harness paints; it stops moving when the UI thread hangs).
  void WriteSummary(const char* status, int exitCode, const std::string& reason);
  [[nodiscard]] unsigned Progress() noexcept;

  // Small helpers.
  [[nodiscard]] std::string JsonString(const std::string& text);
  [[nodiscard]] std::string Utf8(const std::wstring& text);
} // namespace port::graphics::capture::detail
