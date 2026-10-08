#pragma once

// galplay's core: replays a galtrace file (port/graphics/trace/format) into a gpg::gal::Device.
// No engine, no Lua, no game logic: only the gal interfaces and the backend behind them. The caller
// (GalPlayMain.cpp in the graphics main.exe) supplies the device, through hooks, because creating
// one needs the process's windows and the backend's own entry point.
//
// What a replay checks besides running every call:
//   - read-only texture locks (the frame harness's readbacks): the backend's bytes against the bytes
//     recorded, and each is written as a BMP the frame harness would have written;
//   - answers the engine got back (created texture contexts, technique lists, pass counts,
//     GetTexture2D outputs, annotations, the device context after setup): differences are listed,
//     they do not stop the replay.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace gpg::gal
{
    class Device;
    class DeviceContext;
} // namespace gpg::gal

namespace port::graphics::trace
{
    struct PlayOptions
    {
        std::string tracePath;
        std::string outDir;                 // BMPs, galplay.json, remapped cache and save paths
        std::vector<unsigned> readbackNames; // frame numbers for the readbacks in order (the harness's /galframes)
        bool applyFpuState = true;          // set the recorded x87/SSE control words before each call
        bool writeBmps = true;
        std::string skipOp;                 // a mutation for the gates: records of this op are read but not played
    };

    struct PlayHooks
    {
        /** Creates and sets up the device for a DeviceCreate record; fills the heads' window handles. */
        std::function<gpg::gal::Device*(gpg::gal::DeviceContext& requested)> createDevice;
        /** Destroys the device createDevice returned. */
        std::function<void(gpg::gal::Device* device)> destroyDevice;
        /** Window handles for a context passed to Device::Reset (head index -> handle/window). */
        std::function<void(gpg::gal::DeviceContext& context)> fillWindows;
    };

    struct ReadbackResult
    {
        unsigned index = 0;      // 0-based, in trace order
        unsigned name = 0;       // the harness frame number when known, else index + 1
        unsigned presents = 0;   // Present calls replayed before it
        unsigned endScenes = 0;  // EndScene calls replayed before it
        unsigned width = 0;
        unsigned height = 0;
        bool identical = false;  // replayed bytes == recorded bytes
        std::uint64_t differingBytes = 0;
        std::string recordedRgb; // FNV-1a 64 over R,G,B as the harness hashes frames
        std::string replayedRgb;
        std::string file;        // the BMP written
    };

    struct PlayReport
    {
        bool completed = false;  // reached the End record without a fatal error
        std::string fatal;       // why the replay stopped early
        std::uint64_t records = 0;
        std::uint64_t calls = 0;
        unsigned presents = 0;
        unsigned endScenes = 0;
        unsigned draws = 0;
        std::map<std::string, std::uint64_t> opCounts;
        std::vector<ReadbackResult> readbacks;
        std::map<std::string, std::uint64_t> mismatchCounts; // category -> count
        std::vector<std::string> mismatchExamples;           // the first ones, with record numbers
        std::uint64_t skipped = 0;                           // records not played (PlayOptions::skipOp)
        std::uint64_t galErrors = 0;                         // gal::Error thrown by the replay backend
        std::vector<std::string> errorExamples;
        double seconds = 0.0;
    };

    /** Replays the whole trace. False when it could not be read or stopped early (report->fatal). */
    bool PlayTrace(const PlayOptions& options, const PlayHooks& hooks, PlayReport* report);

    /** The report as JSON (galplay.json). */
    std::string PlayReportJson(const PlayOptions& options, const PlayReport& report, const std::string& extraJsonFields);
} // namespace port::graphics::trace
