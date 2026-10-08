#pragma once

// The engine-side half of galplay on Android (M7a1): the part that sees gpg::gal types and is
// therefore compiled with the engine's flags and its Android shim (port/engine/shim), like the gal
// headers it includes. GalPlay.cpp (the app side: data, windows, pacing, files, callbacks) calls it
// through this header, which carries no engine, Diligent or shim types.
//
// RunEngineReplay creates the Diligent backend on Vulkan through DeviceFactory.h when the trace's
// DeviceCreate record asks for a device, replays every record with port/graphics/trace's player, and
// reports through the request's callbacks; it returns when the trace ended, failed or was stopped.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace galtrace
{
    class PayloadResolver;
} // namespace galtrace

namespace gpg::gal::diligent
{
    class DiligentHost;
} // namespace gpg::gal::diligent

namespace faf::android::galplay {

  /** One readback as the player saw it (valid during the callback). */
  struct EngineReadback
  {
    unsigned index = 0;
    unsigned frame = 0;
    unsigned width = 0;
    unsigned height = 0;
    const std::uint8_t* bgra = nullptr; ///< B, G, R, A, top row first, `rowBytes` per row
    std::uint32_t rowBytes = 0;
    std::uint32_t rows = 0;
    std::string hash;      ///< FNV-1a 64 of R,G,B (hex)
    std::string reference; ///< the trace's reference hash for the backend ("" when none)
    std::string verdict;   ///< "pass", "differs", "no-reference"
    int identicalToRecording = -1; ///< 1/0 when the trace holds the recorded bytes or their hash, else -1
  };

  struct EngineRequest
  {
    std::string tracePath;
    std::string outDir;                       ///< the player's own outputs ("" none)
    galtrace::PayloadResolver* resolver = nullptr;
    bool verifyReferencesFirst = true;
    std::string referenceBackend = "diligent:vk";
    std::vector<std::string> backendArguments; ///< the backend's options (/galreport <file>, ...)
    std::function<void*()> currentWindow;      ///< the ANativeWindow* to create the swap chain on (may be null)
    /** The device was created (host set) or is about to be destroyed (host null). Render thread. */
    std::function<void(gpg::gal::diligent::DiligentHost* host, std::uint64_t presents)> onDevice;
    /** The backend could not be created; `message` says why (the replay then stops). */
    std::function<void(const std::string& message)> onDeviceError;
    std::function<bool(unsigned presents)> afterPresent; ///< false stops
    std::function<bool()> shouldStop;
    std::function<void(const EngineReadback& readback)> onReadback;
    std::function<void(int level, const std::string& line)> log; ///< the backend's gpg::Logf/Warnf (0 debug, 1 info, 2 warning)
    /** Keep the device after the trace's DeviceDestroy (hold the last frame); RunEngineReplay's caller destroys it with ReleaseHeldDevice. */
    bool holdDevice = true;
  };

  struct EngineOutcome
  {
    bool completed = false;
    bool stopped = false;
    bool missingData = false;
    std::vector<std::string> dataProblems;
    std::string fatal;
    std::string deviceError;
    std::uint32_t formatVersion = 0;
    std::uint64_t records = 0;
    std::uint64_t calls = 0;
    unsigned presents = 0;
    unsigned draws = 0;
    unsigned references = 0;
    unsigned providedPayloads = 0;
    std::uint64_t galErrors = 0;
    double seconds = 0.0;
    double referenceCheckSeconds = 0.0;
    std::string playerJson; ///< the player's report (galplay.json's "player")
  };

  EngineOutcome RunEngineReplay(const EngineRequest& request);

  /** The device RunEngineReplay held (EngineRequest::holdDevice), or null. Render thread. */
  [[nodiscard]] gpg::gal::diligent::DiligentHost* HeldHost();

  /** Destroys the held device (its report is written then). Render thread. */
  void ReleaseHeldDevice();

} // namespace faf::android::galplay
