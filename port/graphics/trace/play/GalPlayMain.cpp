// galplay in the graphics main.exe: `main.exe /galplay <trace> /galplayout <dir> [/gal diligent:<api>]`
// replays a galtrace file into the D3D9 backend (the default) or the Diligent backend, and exits
// before WinMain: no CScApp, no Lua state, no simulation, no wx frame - only gpg::gal, the backend
// and two hidden windows of its own (a top-level frame and the head's child window, as the engine
// gives the backend). It runs from the CRT's initialiser table after every static initialiser of
// the image (section .CRT$XCY sorts after the compiler's .CRT$XCU), so the backends' and the
// engine's statics exist, and __argv is set.
//
// Options:
//   /galplay <trace>         the trace to replay (without it main.exe starts as usual)
//   /galplayout <dir>        output directory: frame_<N>.bmp per readback, galplay.json, galplay.log
//   /galplayframes <list>    the harness frame numbers of the readbacks, in order (names the BMPs;
//                            default: the trace's own harness_frames metadata)
//   /galplaynofpu            do not apply the recorded x87/SSE control words
//   /galplayskip <Op>        read but do not play the records of one op (a mutation for the gates,
//                            e.g. VarSetFloat or DevDrawIndexedPrimitive; op names as galtrace-dump prints them)
//   /galplaydata <dir>       the game files a version 2 trace refers to, as a tree that mirrors the VFS
//                            (<dir>/effects/ui.fx; `galtrace-refs extract` writes it from the game data)
//   /galplayref <backend>    compare the readbacks with the trace's reference_frames.<backend> hashes
//                            (default: the backend replayed into, e.g. diligent:vk)
//   /gal diligent:<api>      replay into the Diligent backend (d3d11, vk, gl), as the engine selects it
// Every other option the backends read (/galreport, /galnovalidation, /galtex2d, ...) works as in
// an engine run.
//
// Exit codes: 0 the whole trace replayed and every readback equals the recorded bytes; 4 the whole
// trace replayed, some readback differs (scripts/port/galtrace.py applies the parity rule); 2 the
// replay stopped early or the device could not be created; 3 bad options; 5 a game file a version 2
// trace refers to is missing from /galplaydata or differs (galplay.json "data_problems").
//
// Only the graphics build compiles this (port/graphics/port_graphics.props). Without /galplay on the
// command line the initialiser returns at once and main.exe behaves as before.

#include "port/graphics/trace/play/GalPlayer.h"

#include "port/graphics/diligent/GalDiligent.h"
#include "port/graphics/trace/format/GalTraceIO.h"
#include "port/graphics/trace/format/GalTraceResolver.h"

#include "gpg/gal/Device.hpp"
#include "gpg/gal/DeviceContext.hpp"
#include "gpg/gal/Error.hpp"
#include "gpg/gal/Head.hpp"

#include <Windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace port::graphics::trace
{
    namespace
    {
        namespace gal = gpg::gal;

        constexpr wchar_t kWindowClass[] = L"FafGalPlayWindow";

        std::FILE* gLog = nullptr;

        void Log(const char* const format, ...)
        {
            if (gLog == nullptr) {
                return;
            }
            va_list args;
            va_start(args, format);
            std::vfprintf(gLog, format, args);
            va_end(args);
            std::fputc('\n', gLog);
            std::fflush(gLog);
        }

        bool ArgValue(const char* const option, std::string* const value)
        {
            for (int index = 1; index + 1 < __argc; ++index) {
                if (__argv[index] != nullptr && _stricmp(__argv[index], option) == 0 && __argv[index + 1] != nullptr) {
                    *value = __argv[index + 1];
                    return true;
                }
            }
            return false;
        }

        bool HasArg(const char* const option)
        {
            for (int index = 1; index < __argc; ++index) {
                if (__argv[index] != nullptr && _stricmp(__argv[index], option) == 0) {
                    return true;
                }
            }
            return false;
        }

        std::vector<unsigned> ParseFrames(const std::string& text)
        {
            std::vector<unsigned> frames;
            const char* cursor = text.c_str();
            while (*cursor != '\0') {
                char* end = nullptr;
                const unsigned long value = std::strtoul(cursor, &end, 10);
                if (end == cursor) {
                    break;
                }
                frames.push_back(static_cast<unsigned>(value));
                cursor = (*end == ',') ? end + 1 : end;
            }
            return frames;
        }

        LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            if (message == WM_MOUSEACTIVATE) {
                return MA_NOACTIVATE;
            }
            return ::DefWindowProcW(window, message, wParam, lParam);
        }

        /** Hidden windows for the heads: a top-level frame (Head::mHandle) and a child (Head::mWindow). */
        struct Windows
        {
            std::vector<HWND> frames;
            std::vector<HWND> children;

            bool Create(const std::size_t heads, const gal::DeviceContext& context)
            {
                WNDCLASSEXW windowClass{};
                windowClass.cbSize = sizeof(windowClass);
                windowClass.lpfnWndProc = &WindowProc;
                windowClass.hInstance = ::GetModuleHandleW(nullptr);
                windowClass.lpszClassName = kWindowClass;
                (void)::RegisterClassExW(&windowClass);
                const int left = ::GetSystemMetrics(SM_XVIRTUALSCREEN) + ::GetSystemMetrics(SM_CXVIRTUALSCREEN) + 64;
                const int top = ::GetSystemMetrics(SM_YVIRTUALSCREEN) + ::GetSystemMetrics(SM_CYVIRTUALSCREEN) + 64;
                while (frames.size() < heads) {
                    const gal::Head& head = context.GetHead(static_cast<std::uint32_t>(frames.size()));
                    const int width = head.mWidth != 0 ? static_cast<int>(head.mWidth) : 1280;
                    const int height = head.mHeight != 0 ? static_cast<int>(head.mHeight) : 720;
                    // Never WS_VISIBLE, never shown, beyond the virtual screen, no activation.
                    const HWND frame = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kWindowClass, L"galplay", WS_POPUP, left, top,
                                                         width, height, nullptr, nullptr, windowClass.hInstance, nullptr);
                    if (frame == nullptr) {
                        return false;
                    }
                    const HWND child = ::CreateWindowExW(0, kWindowClass, L"galplay head", WS_CHILD | WS_VISIBLE, 0, 0, width, height, frame,
                                                         nullptr, windowClass.hInstance, nullptr);
                    if (child == nullptr) {
                        return false;
                    }
                    frames.push_back(frame);
                    children.push_back(child);
                }
                return true;
            }

            void Fill(gal::DeviceContext& context) const
            {
                for (int index = 0; index < context.GetHeadCount() && static_cast<std::size_t>(index) < frames.size(); ++index) {
                    gal::Head& head = context.GetHead(static_cast<std::uint32_t>(index));
                    head.mHandle = head.mHandle != nullptr ? static_cast<void*>(frames[static_cast<std::size_t>(index)]) : nullptr;
                    head.mWindow = head.mWindow != nullptr ? static_cast<void*>(children[static_cast<std::size_t>(index)]) : nullptr;
                }
            }

            void Destroy()
            {
                for (const HWND child : children) {
                    ::DestroyWindow(child);
                }
                for (const HWND frame : frames) {
                    ::DestroyWindow(frame);
                }
                children.clear();
                frames.clear();
            }
        };

        /** Any window of this process visible or in the foreground: the replay must stay invisible. */
        std::string VisibilityViolation()
        {
            struct Scan
            {
                DWORD process;
                std::string found;
            } scan{::GetCurrentProcessId(), {}};
            ::EnumWindows(
                [](HWND window, LPARAM parameter) -> BOOL {
                    auto* const state = reinterpret_cast<Scan*>(parameter);
                    DWORD owner = 0;
                    ::GetWindowThreadProcessId(window, &owner);
                    if (owner == state->process && ::IsWindowVisible(window)) {
                        state->found += "visible window; ";
                        ::ShowWindow(window, SW_HIDE);
                    }
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&scan)
            );
            DWORD foregroundOwner = 0;
            if (const HWND foreground = ::GetForegroundWindow()) {
                ::GetWindowThreadProcessId(foreground, &foregroundOwner);
            }
            if (foregroundOwner == scan.process) {
                scan.found += "foreground window; ";
            }
            return scan.found;
        }

        std::string JsonString(const std::string& text)
        {
            std::string out = "\"";
            for (const char c : text) {
                if (c == '"' || c == '\\') {
                    out += '\\';
                    out += c;
                } else if (static_cast<unsigned char>(c) < 0x20) {
                    char escaped[8];
                    std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += escaped;
                } else {
                    out += c;
                }
            }
            return out + "\"";
        }

        int RunGalPlay(const std::string& tracePath)
        {
            ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
            std::string outDir;
            if (!ArgValue("/galplayout", &outDir) || outDir.empty()) {
                return 3;
            }
            ::CreateDirectoryA(outDir.c_str(), nullptr);
            ::CreateDirectoryA((outDir + "\\cache").c_str(), nullptr);
            ::CreateDirectoryA((outDir + "\\saved").c_str(), nullptr);
            gLog = std::fopen((outDir + "\\galplay.log").c_str(), "wb");

            std::string api = "d3d9";
            (void)ArgValue("/gal", &api);
            const bool diligent = std::strncmp(api.c_str(), "diligent", 8) == 0;
            Log("galplay: trace %s, backend %s, output %s", tracePath.c_str(), api.c_str(), outDir.c_str());

            PlayOptions options;
            options.tracePath = tracePath;
            options.outDir = outDir;
            options.applyFpuState = !HasArg("/galplaynofpu");
            (void)ArgValue("/galplayskip", &options.skipOp);
            options.referenceBackend = api;
            (void)ArgValue("/galplayref", &options.referenceBackend);
            std::string dataDir;
            (void)ArgValue("/galplaydata", &dataDir);
            galtrace::DirectoryResolver resolver(dataDir);
            if (!dataDir.empty()) {
                options.resolver = &resolver;
                Log("galplay: game files from %s", dataDir.c_str());
            }
            std::string frames;
            if (!ArgValue("/galplayframes", &frames)) {
                galtrace::Reader reader;
                std::string error;
                if (reader.Open(tracePath, &error)) {
                    frames = reader.Meta("harness_frames");
                }
            }
            options.readbackNames = ParseFrames(frames);

            Windows windows;
            std::string violations;
            PlayHooks hooks;
            hooks.createDevice = [&](gal::DeviceContext& requested) -> gal::Device* {
                requested.mDeviceType = diligent ? gal::DeviceApiDiligent : gal::DeviceApi::Direct3D9;
                if (!windows.Create(static_cast<std::size_t>(requested.GetHeadCount()), requested)) {
                    Log("galplay: could not create the head windows (error %lu)", ::GetLastError());
                    return nullptr;
                }
                windows.Fill(requested);
                try {
                    gal::Device* const device = gal::Device::Create(&requested);
                    Log("galplay: device created (api %d, %d head(s))", static_cast<int>(requested.mDeviceType), requested.GetHeadCount());
                    return device;
                } catch (const gal::Error& error) {
                    Log("galplay: device creation failed: %s (%s)", error.what(), error.GetRuntimeMessage());
                } catch (const std::exception& error) {
                    Log("galplay: device creation failed: %s", error.what());
                }
                return nullptr;
            };
            hooks.destroyDevice = [&](gal::Device*) {
                violations += VisibilityViolation();
                gal::Device::DestroyInstance();
            };
            hooks.fillWindows = [&](gal::DeviceContext& context) {
                windows.Fill(context);
            };

            PlayReport report;
            const bool completed = PlayTrace(options, hooks, &report);
            violations += VisibilityViolation();
            windows.Destroy();

            unsigned identical = 0;
            for (const ReadbackResult& readback : report.readbacks) {
                identical += readback.identical ? 1u : 0u;
                Log("galplay: readback %u (frame %u, after %u presents): %s, replay rgb %s recorded %s, reference_frames.%s %s: %s", readback.index,
                    readback.name, readback.presents, readback.identical ? "identical" : "DIFFERENT", readback.replayedRgb.c_str(),
                    readback.recordedRgb.c_str(), options.referenceBackend.c_str(), readback.referenceRgb.empty() ? "-" : readback.referenceRgb.c_str(),
                    readback.verdict.c_str());
            }
            for (const std::string& example : report.mismatchExamples) {
                Log("galplay: mismatch: %s", example.c_str());
            }
            for (const std::string& example : report.errorExamples) {
                Log("galplay: error: %s", example.c_str());
            }
            Log("galplay: %s in %.1f s: %llu records, %llu calls, %u presents, %u draws, %u readbacks (%u identical), %llu gal errors%s%s",
                completed ? "completed" : "STOPPED", report.seconds, static_cast<unsigned long long>(report.records),
                static_cast<unsigned long long>(report.calls), report.presents, report.draws, static_cast<unsigned>(report.readbacks.size()),
                identical, static_cast<unsigned long long>(report.galErrors), report.fatal.empty() ? "" : ": ", report.fatal.c_str());

            const std::string extra = "  \"backend\": " + JsonString(api) + ",\n  \"apply_fpu_state\": " +
                                      std::string(options.applyFpuState ? "true" : "false") + ",\n  \"window_violations\": " +
                                      JsonString(violations) + ",\n  \"pointer_bits\": " + std::to_string(sizeof(void*) * 8) +
                                      ",\n  \"data_dir\": " + JsonString(dataDir) + ",\n";
            const std::string json = PlayReportJson(options, report, extra);
            if (std::FILE* const file = std::fopen((outDir + "\\galplay.json").c_str(), "wb")) {
                std::fwrite(json.data(), 1, json.size(), file);
                std::fclose(file);
            }
            if (!completed) {
                return report.missingData ? 5 : 2;
            }
            return (identical == report.readbacks.size() && report.galErrors == 0 && violations.empty()) ? 0 : 4;
        }

        void GalPlayEntry()
        {
            std::string tracePath;
            if (!ArgValue("/galplay", &tracePath) || tracePath.empty()) {
                return; // a normal main.exe run
            }
            int code = 3;
            try {
                code = RunGalPlay(tracePath);
            } catch (...) {
                Log("galplay: unexpected exception");
                code = 2;
            }
            if (gLog != nullptr) {
                std::fclose(gLog);
                gLog = nullptr;
            }
            // Never reach WinMain: the engine does not start. ExitProcess skips the engine's static
            // destructors, which assume a WinMain ran.
            ::ExitProcess(static_cast<UINT>(code));
        }

        void __cdecl GalPlayInitialiser()
        {
            GalPlayEntry();
        }
    } // namespace
} // namespace port::graphics::trace

// After every C++ dynamic initialiser of the image (.CRT$XCU), before WinMain.
#pragma section(".CRT$XCY", long, read)
extern "C" __declspec(allocate(".CRT$XCY")) void(__cdecl* const gFafGalPlayInitialiser)() =
    &port::graphics::trace::GalPlayInitialiser;
// The C name as the linker sees it: decorated with a leading underscore on x86 only.
#if defined(_M_IX86)
#pragma comment(linker, "/include:_gFafGalPlayInitialiser")
#else
#pragma comment(linker, "/include:gFafGalPlayInitialiser")
#endif
