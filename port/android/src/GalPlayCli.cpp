// galplay_cli (M7a1): the Android menu replay (GalPlay.h) as a command-line program, for the emulator and
// for devices over adb. It runs exactly what GameActivity's mode "menu-replay" runs - the data mounted by
// init_faf.lua, the trace's references checked and resolved through the VFS, the Diligent backend on
// Vulkan, the shader and pipeline caches - but without a window: every frame is drawn into the head
// target and the readback frames are read back offscreen. Not part of the APK.
//
//   adb push galplay_cli /data/local/tmp/
//   adb shell /data/local/tmp/galplay_cli <trace> <data root> --run-dir /data/local/tmp/galplay-run
//
// Writes, besides what galplay_run writes into --run-dir (galplay.json, galplay.log, galreport.json),
// frame_<N>.bmp per readback in the frame harness's BMP format (32-bit BI_RGB, bottom-up, B G R A), so
// `python scripts/port/gfx_capture.py parity <PC run dir> <pulled run dir>` compares them with the PC's
// frames. Exit code: galplay_run's (GalPlay.h GalPlayExitCode), 3 for bad arguments.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "GalPlay.h"

namespace {

  void PutLe16(std::vector<std::uint8_t>& out, const std::uint32_t value)
  {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
  }

  void PutLe32(std::vector<std::uint8_t>& out, const std::uint32_t value)
  {
    for (unsigned shift = 0; shift < 32U; shift += 8U) {
      out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
  }

  /** The frame harness's BMP (GalCapture.cpp WriteBmp) from R G B A rows, top row first. */
  bool WriteBmp(const std::string& path, const std::uint32_t width, const std::uint32_t height, const std::uint8_t* const rgba)
  {
    const std::uint32_t pixelBytes = width * height * 4U;
    std::vector<std::uint8_t> file;
    file.reserve(54U + pixelBytes);
    PutLe16(file, 0x4D42);
    PutLe32(file, 54U + pixelBytes);
    PutLe32(file, 0);
    PutLe32(file, 54U);
    PutLe32(file, 40U);
    PutLe32(file, width);
    PutLe32(file, height);
    PutLe16(file, 1);
    PutLe16(file, 32);
    PutLe32(file, 0); // BI_RGB
    PutLe32(file, pixelBytes);
    for (int i = 0; i < 4; ++i) {
      PutLe32(file, 0);
    }
    for (std::uint32_t row = height; row-- > 0;) {
      const std::uint8_t* pixel = rgba + static_cast<std::size_t>(row) * width * 4U;
      for (std::uint32_t x = 0; x < width; ++x, pixel += 4) {
        file.push_back(pixel[2]);
        file.push_back(pixel[1]);
        file.push_back(pixel[0]);
        file.push_back(pixel[3]);
      }
    }
    std::FILE* const out = std::fopen(path.c_str(), "wb");
    if (out == nullptr) {
      return false;
    }
    const bool ok = std::fwrite(file.data(), 1, file.size(), out) == file.size();
    return std::fclose(out) == 0 && ok;
  }

  struct Context
  {
    std::string runDir;
    bool quiet = false;
    unsigned lastPrinted = 0;
  };

  void OnProgress(void* const user, const GalPlayProgress* const progress)
  {
    auto* const context = static_cast<Context*>(user);
    const bool frame = std::strcmp(progress->stage, "replay") == 0 && (progress->message == nullptr || progress->message[0] == '\0');
    if (frame) {
      // One line per 100 frames (and the last).
      if (context->quiet || (progress->frame / 100U == context->lastPrinted / 100U && progress->frame != progress->frames)) {
        return;
      }
      context->lastPrinted = progress->frame;
      std::printf("frame %u/%u  last %.1f ms (work %.1f ms), avg %.2f ms, readbacks %u/%u pass %u\n", progress->frame, progress->frames,
                  progress->frameMs, progress->gpuFrameMs, progress->avgFrameMs, progress->readbacksDone, progress->readbacks,
                  progress->readbacksPassed);
    } else {
      std::printf("[%s] %s\n", progress->stage, progress->message != nullptr ? progress->message : "");
    }
    std::fflush(stdout);
  }

  void OnReadback(void* const user, const GalPlayReadback* const readback)
  {
    auto* const context = static_cast<Context*>(user);
    std::string file;
    if (!context->runDir.empty()) {
      char name[48];
      std::snprintf(name, sizeof(name), "frame_%06u.bmp", readback->frame);
      file = context->runDir + "/" + name;
      if (!WriteBmp(file, readback->width, readback->height, readback->rgba)) {
        std::fprintf(stderr, "cannot write %s\n", file.c_str());
        file.clear();
      }
    }
    std::printf("readback frame %u: %ux%u rgb %s reference %s -> %s%s%s\n", readback->frame, readback->width, readback->height, readback->hash,
                readback->reference[0] != '\0' ? readback->reference : "(none)", readback->verdict,
                readback->identicalToRecording == 1 ? ", identical to the recording" : readback->identicalToRecording == 0 ? ", differs from the recording" : "",
                file.empty() ? "" : (" (" + file + ")").c_str());
    std::fflush(stdout);
  }

  int Usage()
  {
    std::fprintf(stderr,
                 "usage: galplay_cli <trace> <data root> [--run-dir DIR] [--cache-dir DIR] [--local-appdata DIR] [--documents DIR]\n"
                 "                   [--init FILE] [--paced] [--force-cpu-bc] [--no-cache] [--validation] [--quiet]\n"
                 "  <data root>: the app's data root (faf/bin/init_faf.lua, scfa/, faf/gamedata/ below it)\n"
                 "  --paced: the trace's recorded frame rate instead of as fast as possible\n");
    return GALPLAY_EXIT_BAD_OPTIONS;
  }

} // namespace

int main(int argc, char** argv)
{
  if (argc < 3) {
    return Usage();
  }
  std::string trace = argv[1];
  std::string dataRoot = argv[2];
  std::string cacheDir;
  std::string localAppData;
  std::string documents;
  std::string init;
  std::string backend;
  Context context;
  GalPlayOptions options{};
  options.structSize = sizeof(GalPlayOptions);
  options.fast = 1;
  options.headless = 1;
  for (int index = 3; index < argc; ++index) {
    const std::string arg = argv[index];
    const auto value = [&](std::string* const out) {
      if (index + 1 >= argc) {
        return false;
      }
      *out = argv[++index];
      return true;
    };
    if (arg == "--run-dir") {
      if (!value(&context.runDir)) {
        return Usage();
      }
    } else if (arg == "--cache-dir") {
      if (!value(&cacheDir)) {
        return Usage();
      }
    } else if (arg == "--local-appdata") {
      if (!value(&localAppData)) {
        return Usage();
      }
    } else if (arg == "--documents") {
      if (!value(&documents)) {
        return Usage();
      }
    } else if (arg == "--init") {
      if (!value(&init)) {
        return Usage();
      }
    } else if (arg == "--backend") {
      std::string option;
      if (!value(&option)) {
        return Usage();
      }
      backend += (backend.empty() ? "" : " ") + option;
    } else if (arg == "--paced") {
      options.fast = 0;
    } else if (arg == "--force-cpu-bc") {
      options.forceCpuBcDecode = 1;
    } else if (arg == "--no-cache") {
      options.noShaderCache = 1;
    } else if (arg == "--validation") {
      options.validation = 1;
    } else if (arg == "--quiet") {
      context.quiet = true;
    } else {
      std::fprintf(stderr, "unknown argument %s\n", arg.c_str());
      return Usage();
    }
  }
  options.runDir = context.runDir.empty() ? nullptr : context.runDir.c_str();
  options.cacheDir = cacheDir.empty() ? nullptr : cacheDir.c_str();
  options.localAppData = localAppData.empty() ? nullptr : localAppData.c_str();
  options.documents = documents.empty() ? nullptr : documents.c_str();
  options.initScript = init.empty() ? nullptr : init.c_str();
  options.backendOptions = backend.empty() ? nullptr : backend.c_str();

  GalPlayCallbacks callbacks{};
  callbacks.user = &context;
  callbacks.onProgress = &OnProgress;
  callbacks.onReadback = &OnReadback;
  GalPlayResult result{};
  const int code = galplay_run(nullptr, trace.c_str(), dataRoot.c_str(), &options, &callbacks, &result);
  std::printf("galplay_cli: exit %d, %u frames, readbacks %u (pass %u, other %u), %.3f s: %s\n", code, result.frames, result.readbacks, result.passed,
              result.failed, result.replaySeconds, result.message);
  return code;
}
