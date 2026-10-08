#pragma once

// galplay's core: replays a galtrace file (port/graphics/trace/format) into a gpg::gal::Device.
// No engine, no Lua, no game logic: only the gal interfaces and the backend behind them. The caller
// (GalPlayMain.cpp in the graphics main.exe, the Android galplay) supplies the device, through hooks,
// because creating one needs the process's windows and the backend's own entry point.
//
// What a replay checks besides running every call:
//   - read-only texture locks (the frame harness's readbacks): the backend's bytes against the bytes
//     (version 1) or the hashes (version 2) recorded, and against the trace's reference frame hashes
//     for one backend (metadata "reference_frames.<backend>", PlayOptions::referenceBackend); each is
//     written as the BMP the frame harness would have written, and handed to PlayHooks::onReadback;
//   - answers the engine got back (created texture contexts, technique lists, pass counts,
//     GetTexture2D outputs, annotations, the device context after setup): differences are listed,
//     they do not stop the replay.
//
// Version 2 traces name game files instead of holding them (GalTraceFormat.h): PlayOptions::resolver
// reads them (the VFS on the phone, a DirectoryResolver on the PC). With verifyReferencesFirst every
// reference is read and checked before the first call, and a missing or different file stops the
// replay with PlayReport::missingData and a message that names the files and their archives.

#include <cstddef>
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

namespace galtrace
{
    class PayloadResolver;
} // namespace galtrace

namespace port::graphics::trace
{
    struct PlayOptions
    {
        std::string tracePath;
        std::string outDir;                 // BMPs, galplay.json, remapped cache and save paths ("" = no files)
        std::vector<unsigned> readbackNames; // frame numbers for the readbacks in order (the harness's /galframes);
                                             // empty: the trace's harness_frames metadata
        bool applyFpuState = true;          // set the recorded x87/SSE control words before each call
        bool writeBmps = true;
        std::string skipOp;                 // a mutation for the gates: records of this op are read but not played
        galtrace::PayloadResolver* resolver = nullptr; // game files of a version 2 trace (not owned)
        bool verifyReferencesFirst = true;  // read and check every reference before the first call
        std::string referenceBackend = "diligent:vk"; // compare readbacks with metadata reference_frames.<this>
    };

    struct ReadbackResult;

    struct PlayHooks
    {
        /** Creates and sets up the device for a DeviceCreate record; fills the heads' window handles. */
        std::function<gpg::gal::Device*(gpg::gal::DeviceContext& requested)> createDevice;
        /** Destroys the device createDevice returned. */
        std::function<void(gpg::gal::Device* device)> destroyDevice;
        /** Window handles for a context passed to Device::Reset (head index -> handle/window). */
        std::function<void(gpg::gal::DeviceContext& context)> fillWindows;
        /**
         * After every replayed Present, with the number of presents so far (1-based). Pacing waits and
         * progress reports belong here. Return false to stop the replay (PlayReport::stopped).
         */
        std::function<bool(unsigned presents)> afterPresent;
        /** Polled before every record (keep it cheap): true stops the replay (PlayReport::stopped). */
        std::function<bool()> shouldStop;
        /**
         * Every readback with the replayed pixels as the lock gave them: `rows` rows of `rowBytes`,
         * packed, top row first; for gal format 2/3 (A8R8G8B8/X8R8G8B8) the bytes are B, G, R, A.
         * Valid during the call only.
         */
        std::function<void(const ReadbackResult& result, const std::uint8_t* pixels, std::uint32_t rowBytes, std::uint32_t rows)> onReadback;
    };

    struct ReadbackResult
    {
        unsigned index = 0;      // 0-based, in trace order
        unsigned name = 0;       // the harness frame number when known, else index + 1
        unsigned presents = 0;   // Present calls replayed before it
        unsigned endScenes = 0;  // EndScene calls replayed before it
        unsigned width = 0;
        unsigned height = 0;
        unsigned format = 0;     // the gal texture format of the locked texture
        bool identical = false;  // replayed bytes == recorded bytes (by content hash for a version 2 digest)
        bool recordedBytesKnown = false; // the trace holds the recorded bytes (version 1); else only their hashes
        std::uint64_t differingBytes = 0; // when recordedBytesKnown
        std::string recordedRgb; // FNV-1a 64 over R,G,B as the harness hashes frames ("" when unknown)
        std::string replayedRgb;
        std::string referenceRgb; // reference_frames.<referenceBackend> for this frame ("" when the trace has none)
        std::string verdict;      // "pass" (replayedRgb == referenceRgb), "differs", "no-reference"
        std::string file;        // the BMP written
    };

    struct PlayReport
    {
        bool completed = false;  // reached the End record without a fatal error
        bool stopped = false;    // a hook asked to stop
        bool missingData = false; // a game file the trace refers to is missing or differs (fatal says which)
        std::vector<std::string> dataProblems; // one line per such file
        std::string fatal;       // why the replay stopped early
        std::uint32_t formatVersion = 0;
        std::uint64_t records = 0;
        std::uint64_t calls = 0;
        unsigned presents = 0;
        unsigned endScenes = 0;
        unsigned draws = 0;
        unsigned references = 0;  // version 2: game files read through the resolver
        unsigned providedPayloads = 0; // version 2: backend outputs the replay supplied (GetTexture2D)
        std::map<std::string, std::uint64_t> opCounts;
        std::vector<ReadbackResult> readbacks;
        std::map<std::string, std::uint64_t> mismatchCounts; // category -> count
        std::vector<std::string> mismatchExamples;           // the first ones, with record numbers
        std::uint64_t skipped = 0;                           // records not played (PlayOptions::skipOp)
        std::uint64_t galErrors = 0;                         // gal::Error thrown by the replay backend
        std::vector<std::string> errorExamples;
        double seconds = 0.0;
        double referenceCheckSeconds = 0.0;                  // verifyReferencesFirst
    };

    /** Replays the whole trace. False when it could not be read or stopped early (report->fatal). */
    bool PlayTrace(const PlayOptions& options, const PlayHooks& hooks, PlayReport* report);

    /** The report as JSON (galplay.json). */
    std::string PlayReportJson(const PlayOptions& options, const PlayReport& report, const std::string& extraJsonFields);
} // namespace port::graphics::trace
