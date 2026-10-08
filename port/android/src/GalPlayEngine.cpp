// The engine-side half of galplay on Android (GalPlayEngine.h). Compiled with the engine's flags and
// shim (port/android/CMakeLists.txt, faf_galplay_engine), because it includes the gal headers.

#include "GalPlayEngine.h"

#include <cstdio>
#include <exception>
#include <memory>
#include <mutex>

#include "gpg/gal/Device.hpp"
#include "gpg/gal/DeviceContext.hpp"
#include "gpg/gal/Error.hpp"
#include "gpg/gal/Head.hpp"
#include "port/graphics/diligent/DeviceDiligent.h"
#include "port/graphics/diligent/DeviceFactory.h"
#include "port/graphics/diligent/GalDiligent.h"
#include "port/graphics/diligent/android/GalDevicePortable.h"
#include "port/graphics/trace/play/GalPlayer.h"

namespace faf::android::galplay {

  namespace {

    namespace gal = gpg::gal;
    namespace dil = gpg::gal::diligent;
    namespace play = port::graphics::trace;

    // gpg::Logf/Warnf (GalDevicePortable.cpp) are plain function pointers; the request's log function
    // is kept here for the duration of a replay.
    std::mutex gLogLock;
    std::function<void(int, const std::string&)> gLog;

    void LogSink(const int level, const char* const line)
    {
      std::function<void(int, const std::string&)> log;
      {
        const std::lock_guard<std::mutex> lock(gLogLock);
        log = gLog;
      }
      if (log) {
        log(level, line != nullptr ? line : "");
      }
    }

    /** The device kept past the trace's DeviceDestroy (EngineRequest::holdDevice). Render thread. */
    std::unique_ptr<gal::Device> gHeldDevice;
    dil::DiligentHost* gHeldHost = nullptr;

    dil::DeviceDiligent* BackendOf(gal::Device* const device)
    {
      return device != nullptr ? static_cast<dil::DeviceDiligent*>(dil::GetBackendDevice(device)) : nullptr;
    }

  } // namespace

  EngineOutcome RunEngineReplay(const EngineRequest& request)
  {
    EngineOutcome outcome;
    ReleaseHeldDevice();
    {
      const std::lock_guard<std::mutex> lock(gLogLock);
      gLog = request.log;
    }
    dil::SetPortableLogSink(&LogSink);
    dil::SetPortableCommandLine(request.backendArguments);

    play::PlayOptions options;
    options.tracePath = request.tracePath;
    options.outDir = request.outDir;
    options.writeBmps = false; // the app writes PNGs from onReadback
    options.applyFpuState = false; // no x87 on Android's ABIs
    options.resolver = request.resolver;
    options.verifyReferencesFirst = request.verifyReferencesFirst;
    options.referenceBackend = request.referenceBackend;

    play::PlayHooks hooks;
    hooks.createDevice = [&request, &outcome](gal::DeviceContext& requested) -> gal::Device* {
      // The heads' window: the activity's ANativeWindow, or none yet (the swap chain then comes with
      // the first window; DiligentHost::SetWindow).
      void* const window = request.currentWindow ? request.currentWindow() : nullptr;
      for (int index = 0; index < requested.GetHeadCount(); ++index) {
        gal::Head& head = requested.GetHead(static_cast<std::uint32_t>(index));
        head.mHandle = window;
        head.mWindow = window;
      }
      gal::Device* const device = dil::CreateDeviceForApi("vk");
      if (device == nullptr) {
        outcome.deviceError = "the backend has no Vulkan device type";
        return nullptr;
      }
      try {
        dil::SetupDevice(device, &requested);
      } catch (const gal::Error& error) {
        outcome.deviceError = std::string(error.what()) + " (" + error.GetRuntimeMessage() + ")";
        delete device;
        if (request.onDeviceError) {
          request.onDeviceError(outcome.deviceError);
        }
        return nullptr;
      } catch (const std::exception& error) {
        outcome.deviceError = error.what();
        delete device;
        if (request.onDeviceError) {
          request.onDeviceError(outcome.deviceError);
        }
        return nullptr;
      }
      dil::SetPortableInstance(device);
      if (request.onDevice) {
        dil::DeviceDiligent* const backend = BackendOf(device);
        request.onDevice(backend != nullptr ? backend->GetHost() : nullptr, 0U);
      }
      return device;
    };
    hooks.destroyDevice = [&request](gal::Device* const device) {
      dil::DeviceDiligent* const backend = BackendOf(device);
      if (request.holdDevice) {
        // Keep it: the activity keeps showing the last frame (and presents it again on a new window).
        gHeldDevice.reset(device);
        gHeldHost = backend != nullptr ? backend->GetHost() : nullptr;
        return;
      }
      if (request.onDevice) {
        request.onDevice(nullptr, backend != nullptr ? backend->GetPresentCount() : 0U);
      }
      dil::SetPortableInstance(nullptr);
      delete device;
    };
    hooks.afterPresent = request.afterPresent;
    hooks.shouldStop = request.shouldStop;
    hooks.onReadback = [&request](const play::ReadbackResult& result, const std::uint8_t* const pixels, const std::uint32_t rowBytes,
                                  const std::uint32_t rows) {
      if (!request.onReadback) {
        return;
      }
      EngineReadback readback;
      readback.index = result.index;
      readback.frame = result.name;
      readback.width = result.width;
      readback.height = result.height;
      readback.bgra = pixels;
      readback.rowBytes = rowBytes;
      readback.rows = rows;
      readback.hash = result.replayedRgb;
      readback.reference = result.referenceRgb;
      readback.verdict = result.verdict;
      // Every readback of a trace has its recording: the bytes (version 1) or their digest (version 2);
      // the player stops at a readback without one. So `identical` is always known here.
      readback.identicalToRecording = result.identical ? 1 : 0;
      request.onReadback(readback);
    };

    play::PlayReport report;
    try {
      play::PlayTrace(options, hooks, &report);
    } catch (const std::exception& error) {
      report.fatal = std::string("internal error: ") + error.what();
    }
    outcome.completed = report.completed;
    outcome.stopped = report.stopped;
    outcome.missingData = report.missingData;
    outcome.dataProblems = report.dataProblems;
    outcome.fatal = report.fatal;
    outcome.formatVersion = report.formatVersion;
    outcome.records = report.records;
    outcome.calls = report.calls;
    outcome.presents = report.presents;
    outcome.draws = report.draws;
    outcome.references = report.references;
    outcome.providedPayloads = report.providedPayloads;
    outcome.galErrors = report.galErrors;
    outcome.seconds = report.seconds;
    outcome.referenceCheckSeconds = report.referenceCheckSeconds;
    outcome.playerJson = play::PlayReportJson(options, report, std::string());
    return outcome;
  }

  gpg::gal::diligent::DiligentHost* HeldHost()
  {
    return gHeldHost;
  }

  void ReleaseHeldDevice()
  {
    gHeldHost = nullptr;
    if (gHeldDevice) {
      gHeldDevice.reset(); // the backend writes its /galreport now
      dil::SetPortableInstance(nullptr);
    }
    dil::SetPortableLogSink(nullptr);
    const std::lock_guard<std::mutex> lock(gLogLock);
    gLog = nullptr;
  }

} // namespace faf::android::galplay
