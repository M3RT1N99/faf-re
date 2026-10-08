#pragma once

// galplay on Android (milestone M7a1, release 0.5.0): replays a galtrace file
// (port/graphics/trace) of FAF's main menu through the port's own Diligent gal
// backend (port/graphics/diligent) over Vulkan, on the GameActivity window, in
// the :game process. No engine, no Lua state of the game, no simulation: the
// player makes the recorded gal calls, the backend draws them, and at the
// recorded readback frames the head target is read back and its hash compared
// with the PC's Diligent-Vulkan hash stored in the trace's metadata.
//
// Two layers are described here:
//
//   1. The C API below (galplay_run and its two companions). The native side of
//      GameActivity (AndroidMain.cpp, mode "menu-replay") calls it on a thread of
//      its own and passes the window changes on.
//
//   2. The Java contract of that mode (for the launcher and GameActivity.java):
//      the Intent extras that select it, the callbacks the native side makes into
//      the GameActivity instance, and the files it writes into the run directory.
//
// ---------------------------------------------------------------------------------
// Java contract (GameActivity, mode "menu-replay")
// ---------------------------------------------------------------------------------
//
// Start: the explicit Intent to .GameActivity the launcher uses for Start, with
//
//   "argv"                     String[]  as for Start (the native side takes /init and /log from it;
//                                        /init's init_faf.lua mounts the game data the trace refers to)
//   "mode"                     String    "menu-replay" (absent or anything else: the M1 runtime)
//   "menuReplay.trace"         String    absolute path of the .galtrace file (readable by the app)
//   "menuReplay.runDir"        String    absolute path of the run directory; the launcher creates it
//   "menuReplay.fast"          boolean   true: as fast as possible; false (default): the recorded
//                                        pace (the trace's frame rate, 30 fps)
//   "menuReplay.forceCpuDecode" boolean  true: decode BC textures on the CPU even when the GPU can
//                                        sample them (the fallback path for GPUs without BC, for tests)
//   "menuReplay.noShaderCache" boolean   true: neither read nor write the shader and pipeline caches
//                                        (a cold first launch, for timing)
//
// Callbacks into the GameActivity instance. Every one is optional: the native side looks each method
// up once and skips (and logs) a missing one. They are called on a native thread (attached to the VM
// for the call), never on the UI thread, so post to the UI thread before touching views. Keep them
// short: the replay waits for them.
//
//   void onMenuReplayProgress(String json)
//       At every stage change and after every replayed frame (at the recorded pace 30 per second; in
//       fast mode at most 30 per second). JSON object:
//         {"stage": "data|trace|device|shaders|replay|done|error",
//          "message": "...",                  // what happens now, or the error
//          "frame": 123, "frames": 900,       // presents replayed / in the trace
//          "frameMs": 33.3, "avgFrameMs": 33.4, // wall time of the last frame / of all so far
//          "gpuFrameMs": 4.1,                 // replay work of the last frame (without the pacing wait)
//          "readbacks": 9, "passed": 3, "failed": 0, "done": 3,  // readback frames: total / so far
//          "fast": false}
//
//   void onMenuReplayReadback(int frame, int width, int height, java.nio.ByteBuffer rgba, String json)
//       Once per readback frame (10, 15, 20, 25, 30, 45, 60, 300, 900 for the menu trace), right after
//       it was read back. `rgba` is a direct buffer of width * height * 4 bytes, R G B A, top row
//       first; it is valid only during the call (compress it into the run directory's PNG, or copy
//       it, before returning). JSON object:
//         {"index": 2, "frame": 20, "width": 1280, "height": 720,
//          "hash": "2ea237a728985926",          // FNV-1a 64 over R,G,B, as the PC harness hashes frames
//          "reference": "2ea237a728985926",     // the PC Diligent-Vulkan hash from the trace ("" if none)
//          "verdict": "pass|differs|no-reference",
//          "identicalToRecording": true}         // the bytes equal the recording's (when it holds them)
//       "pass" means byte-identical RGB to the PC frame. "differs" is not yet a failure of the parity
//       rule (max |delta| <= 1 on <= 0.1 % of the pixels): that needs the PC frame, so it is decided
//       on the PC from the zip's PNG (scripts/port/gfx_capture.py parity).
//
//   void onMenuReplayFinished(String json)
//       Once, at the end (also after an error or a stop). The same object galplay.json holds (below).
//
// Files the native side writes into the run directory:
//   galplay.json      the result: {"schema": 1, "result": "PASS|DIFFERS|INCOMPLETE|MISSING_DATA|ERROR",
//                     "exitCode": n, "message": ..., "fast": bool,
//                     "frames": n (presents replayed), "framesInTrace": n,
//                     "readbacks": {"expected", "done", "passed", "differs"},
//                     "frameHashes": [{"frame", "hash", "reference", "verdict", "identicalToRecording"}...],
//                     "trace": {"path", "version", "bytes", "frameRate", "references", "archives"},
//                     "data": {"root", "mounts", "scriptMs", "mountMs"},
//                     "device": {"api", "adapter", "surface", "swapChainFormat", "bcFeature", "bcSampled",
//                     "bc": "gpu|cpu", "bcForced", "d24s8Attachment", "d32s8Attachment",
//                     "depthStencil": "D24S8|D32S8", "subPixelPrecisionBits", "subTexelPrecisionBits"},
//                     "timings": {"dataMs", "deviceMs", "firstFrameMs", "replaySeconds", "avgFrameMs",
//                     "pausedMs", "shadersCompiled", "shaderCompileMs", "shaderCacheHits", "shaderCacheMs",
//                     "shaderCacheStores", "shaderCompileFailures"},
//                     "caches": {"dir", "enabled", "pipelineCache", "pipelineCacheLoadedBytes",
//                     "pipelineCacheSavedBytes", "spirvLoaded", "spirvStored"},
//                     "replay": {"completed", "stopped", "missingData", "fatal", "records", ...},
//                     "player": {galplay's report}}
//                     A replay that could not start (no trace, another replay running) has only
//                     schema, result "ERROR", exitCode, message, frames 0 and an empty frameHashes.
//   galplay.log       the replay's log lines (the runtime log <root>/logs/faf_android_vulkan.log has
//                     them too)
//   galreport.json    the Diligent backend's report (draws, uploads, render passes, effects, ...)
// and <root>/launch/status.json as for Start (stages args, datapath, mount, renderer, frame; state
// loading, then running while it replays and holds its last frame, exited when the activity closes, or
// error at once when the replay cannot run), so the launcher can tell that a menu replay is loading or
// running.
//
// The activity stays open after the replay with its last frame on screen, and after an error with the
// overlay's message; back, or a tap, closes it (status "exited", unless "error" was written). Back during
// the replay stops it (galplay.json "INCOMPLETE") and closes the activity.
//
// ---------------------------------------------------------------------------------
// C API
// ---------------------------------------------------------------------------------

#include <android/native_window.h>

#include <cstddef>
#include <cstdint>

extern "C" {

/** galplay_run's results, also galplay.json's "exitCode" (the Windows galplay's codes plus 5-7). */
enum GalPlayExitCode
{
  GALPLAY_EXIT_PASS = 0,         ///< complete, every readback has the reference hash
  GALPLAY_EXIT_STOPPED = 2,      ///< stopped early (galplay_request_stop, or a fatal replay error)
  GALPLAY_EXIT_BAD_OPTIONS = 3,  ///< no trace, unreadable trace, bad arguments
  GALPLAY_EXIT_DIFFERS = 4,      ///< complete, some readback differs from its reference (or has none)
  GALPLAY_EXIT_MISSING_DATA = 5, ///< the game data the trace refers to is missing or differs
  GALPLAY_EXIT_NO_DEVICE = 6,    ///< no usable Vulkan device or swap chain
  GALPLAY_EXIT_INTERNAL = 7,     ///< an exception or another internal error
};

struct GalPlayOptions
{
  std::uint32_t structSize;      ///< sizeof(GalPlayOptions)
  int fast;                      ///< 1: as fast as possible; 0: the recorded pace
  int forceCpuBcDecode;          ///< 1: BC textures decoded on the CPU (the fallback for GPUs without BC)
  int noShaderCache;             ///< 1: no SPIR-V or pipeline cache (read or write)
  int validation;                ///< 1: Diligent's validation (and the Khronos layer if the device has one)
  const char* initScript;        ///< init_faf.lua; null: <dataRoot>/faf/bin/init_faf.lua
  const char* localAppData;      ///< LOCAL_APPDATA for init_faf.lua; null: <dataRoot>/localappdata
  const char* documents;         ///< Documents for init_faf.lua; null: <dataRoot>/documents
  const char* cacheDir;          ///< shader and pipeline caches; null: <dataRoot>/localappdata/galplay-cache
  const char* runDir;            ///< galplay.json, galplay.log, galreport.json; null: no files
  // ---- appended after the first version (structSize tells them apart) ----
  int headless;                  ///< 1: never a window (galplay_cli over adb): frames are drawn and read back
                                 ///< offscreen, the replay never waits for a window and does not hold its
                                 ///< last frame; 0: the activity's window
  const char* backendOptions;    ///< more backend options, space separated ("/galhalfpixel viewport"; tests); null: none
};

/** A progress report; `stage` and `message` are valid during the callback only. */
struct GalPlayProgress
{
  const char* stage;             ///< "data", "trace", "device", "shaders", "replay", "done", "error"
  const char* message;
  std::uint32_t frame;           ///< presents replayed so far
  std::uint32_t frames;          ///< presents in the trace
  double frameMs;                ///< wall time of the last frame
  double avgFrameMs;             ///< average wall time per frame so far
  double gpuFrameMs;             ///< replay work of the last frame (without the pacing wait)
  std::uint32_t readbacks;       ///< readback frames in the trace
  std::uint32_t readbacksDone;
  std::uint32_t readbacksPassed;
  std::uint32_t readbacksFailed;
  int fast;
};

/** One read-back frame; every pointer is valid during the callback only. */
struct GalPlayReadback
{
  std::uint32_t index;           ///< 0-based, in trace order
  std::uint32_t frame;           ///< the harness frame number
  std::uint32_t width;
  std::uint32_t height;
  const std::uint8_t* rgba;      ///< R G B A, top row first, width * 4 bytes per row
  const char* hash;              ///< FNV-1a 64 of R,G,B in hex (the harness's frame hash)
  const char* reference;         ///< the PC Diligent-Vulkan hash, or "" when the trace has none
  const char* verdict;           ///< "pass", "differs" or "no-reference"
  int identicalToRecording;      ///< 1 when the bytes equal the trace's recorded readback, -1 unknown
};

struct GalPlayCallbacks
{
  void* user;
  /** Stage changes and every replayed frame (rate-limited to 30/s). Replay thread. */
  void (*onProgress)(void* user, const GalPlayProgress* progress);
  /** Every readback frame. Replay thread. */
  void (*onReadback)(void* user, const GalPlayReadback* readback);
  /** The final result as JSON (galplay.json's content). Replay thread. */
  void (*onFinished)(void* user, const char* json);
  /** Nonzero to stop; polled between records. Any thread-safe check. */
  int (*shouldStop)(void* user);
};

struct GalPlayResult
{
  int exitCode;                  ///< GalPlayExitCode
  std::uint32_t frames;          ///< presents replayed
  std::uint32_t readbacks;
  std::uint32_t passed;
  std::uint32_t failed;
  double replaySeconds;
  char message[512];             ///< for the user: what failed, or the summary line
};

/**
 * Replays `tracePath` on `window` (the GameActivity's ANativeWindow; null to start without one: the
 * replay waits for galplay_set_window), resolving the trace's game-data references through the
 * VFS that init_faf.lua under `dataRoot` mounts. Blocks until the replay ended, failed or was
 * stopped; call it on a thread of its own (it becomes the render thread). One replay per process
 * at a time. `options` and `callbacks` may be null (defaults: recorded pace, caches on, no files,
 * no callbacks). Returns the exit code, also in `result` when it is given. onFinished is called
 * exactly once on every path, also when the replay cannot start.
 */
int galplay_run(
  ANativeWindow* window,
  const char* tracePath,
  const char* dataRoot,
  const GalPlayOptions* options,
  const GalPlayCallbacks* callbacks,
  GalPlayResult* result
);

/**
 * The window changed (APP_CMD_INIT_WINDOW: the new one; APP_CMD_TERM_WINDOW: null). For null this
 * returns only after the replay thread has released the swap chain and surface of the old window
 * (or after `timeoutMs`; then 0 is returned), so the caller may let Android destroy it. Without a
 * window the replay pauses at its next present. Also for a resize or a rotation (the same window
 * again: the swap chain is rebuilt). Returns 1 on success. Safe from any thread. Before a replay
 * runs, the window is kept for the next galplay_run (whose own non-null window argument wins), so a
 * window that arrives while the replay thread is starting is not lost.
 */
int galplay_set_window(ANativeWindow* window, int timeoutMs);

/** Asks a running replay to stop at its next record; returns at once. */
void galplay_request_stop(void);

} // extern "C"
