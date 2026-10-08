// The frame harness's engine side (GalCapture.h): it drives one paint per CScApp::Main into a
// window nobody sees, captures chosen frames through gal right after EndScene, and writes the
// summary. Port-only (FAF_PORT_GRAPHICS); every entry point returns at once without
// `/galharness`.
//
// Frame N is the paint that follows the N-th CScApp::Main. A normal run gets that paint from
// WM_PAINT: Main ends with CD3DDevice::Refresh (CScApp.cpp:1098-1105), wx invalidates the
// viewport (CD3DDevice.cpp:816-822), WIN_AppExecute's MsgWaitEx wakes on the message, the next
// loop pass runs the BeforeEvents stage and dispatches WM_PAINT to WD3DViewport::OnPaint, which
// calls CD3DDevice::Paint (WinApp.cpp:2777-2808, WxRuntimeTypes.cpp:238-251). A hidden window
// gets no WM_PAINT, so HarnessFrame posts a message of its own to a message-only window instead.
// It is dispatched where the WM_PAINT would be - by the loop's wx Dispatch, or on frame 1 by the
// nested pump the front-end start runs (see HarnessFrame) - so the stages run in the same order as
// in a visible run, and its handler calls the same CD3DDevice::Paint: Present of frame N-1, then
// WRenViewport::Render of frame N.

#include "port/graphics/capture/GalCapture.h"
#include "port/graphics/capture/HarnessInternal.h"

#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "boost/shared_ptr.h"
#include "gpg/gal/Device.hpp"
#include "gpg/gal/DeviceContext.hpp"
#include "gpg/gal/Error.hpp"
#include "gpg/gal/Head.hpp"
#include "gpg/gal/OutputContext.hpp"
#include "gpg/gal/RenderTarget.hpp"
#include "gpg/gal/RenderTargetContext.hpp"
#include "gpg/gal/Texture.hpp"
#include "gpg/gal/TextureContext.hpp"
#include "lua/LuaObject.h"
#include "moho/app/WinApp.h"
#include "moho/render/d3d/CD3DDevice.h"
#include "moho/ui/UiRuntimeTypes.h"

namespace port::graphics::capture
{
  namespace
  {
    using detail::Config;
    using detail::Log;

    constexpr UINT kPaintMessage = WM_APP + 0x6A; // wParam: the frame number
    constexpr wchar_t kPaintWindowClass[] = L"FafGalHarnessPaint";

    // gal texture description of the readback target. source 2 = empty texture of the given
    // size and format; usage 3 = D3DPOOL_SYSTEMMEM on D3D9 (DeviceD3D9::CreateTexture,
    // D3D9Interfaces.cpp:2569-2577); format 2 = A8R8G8B8, the D3D9 back-buffer format
    // (GetHeadParameters, D3D9Interfaces.cpp:1918; format pairs at :686-746).
    constexpr std::uint32_t kTextureSourceEmpty = 2;
    constexpr std::uint32_t kTextureUsageSystemMemory = 3;
    constexpr std::uint32_t kGalFormatA8R8G8B8 = 2;
    constexpr int kLockReadOnly = 2; // gal lock flag bit 1 = D3DLOCK_READONLY (Texture.hpp)

    struct CaptureRecord
    {
      unsigned frame = 0;
      std::string file;
      unsigned width = 0;
      unsigned height = 0;
      std::string rgbHash;   // FNV-1a 64 over R,G,B per pixel, rows top to bottom
      std::string alphaHash; // FNV-1a 64 over A
      double clock = 0.0;
    };

    HWND gPaintWindow = nullptr;
    unsigned gAppFrames = 0;          // CScApp::Main calls seen
    unsigned gPaints = 0;             // harness paints run
    unsigned gPaintsWithoutRender = 0; // paints in which Render did not reach EndScene (startup)
    unsigned gPaintFrame = 0;         // frame of the paint in progress
    unsigned gRendersInPaint = 0;
    unsigned gExtraRenders = 0;       // Render calls beyond one per paint
    bool gInPaint = false;
    bool gFinished = false;
    std::vector<CaptureRecord> gCaptures;
    LARGE_INTEGER gFrequency{};
    LARGE_INTEGER gLastPaint{};
    std::string gDeviceDescription; // JSON object, filled on the first frame
    bool gMainMenuLoaded = false;

    // The navigation script (`/galscript`, M6c): UI actions injected from the Lua side, by frame
    // number, never as real input. The file runs once in the user Lua state on the first frame and
    // must define GalHarnessStep(frame); the harness calls it once per CScApp::Main, before that
    // frame's paint, and a string it returns is logged as the action taken. An error ends the run
    // (exit 14). The frame numbers make it as deterministic as the rest of the run.
    struct ScriptAction
    {
      unsigned frame = 0;
      std::string note;
    };
    bool gScriptLoaded = false;
    unsigned gScriptCalls = 0;
    std::string gScriptFnv;
    std::vector<ScriptAction> gScriptActions;

    std::string Hex64(const std::uint64_t value)
    {
      char text[24];
      (void)std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
      return text;
    }

    bool IsCaptureFrame(const unsigned frame)
    {
      return std::binary_search(Config().frames.begin(), Config().frames.end(), frame);
    }

    bool IsCaptured(const unsigned frame)
    {
      return std::any_of(gCaptures.begin(), gCaptures.end(), [frame](const CaptureRecord& record) {
        return record.frame == frame;
      });
    }

    // 32-bit uncompressed BMP, bottom-up, the bytes exactly as the A8R8G8B8 surface holds them
    // (B, G, R, A): viewers show RGB, the alpha byte stays in the file for the comparison script.
    bool WriteBmp(const std::wstring& path, const unsigned width, const unsigned height, const std::vector<std::uint8_t>& topDownBgra)
    {
      std::FILE* file = _wfopen(path.c_str(), L"wb");
      if (file == nullptr) {
        return false;
      }
      const std::uint32_t pixelBytes = width * height * 4u;
      BITMAPFILEHEADER fileHeader{};
      fileHeader.bfType = 0x4D42; // "BM"
      fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
      fileHeader.bfSize = fileHeader.bfOffBits + pixelBytes;
      BITMAPINFOHEADER infoHeader{};
      infoHeader.biSize = sizeof(BITMAPINFOHEADER);
      infoHeader.biWidth = static_cast<LONG>(width);
      infoHeader.biHeight = static_cast<LONG>(height);
      infoHeader.biPlanes = 1;
      infoHeader.biBitCount = 32;
      infoHeader.biCompression = BI_RGB;
      infoHeader.biSizeImage = pixelBytes;
      bool ok = std::fwrite(&fileHeader, sizeof(fileHeader), 1, file) == 1 &&
                std::fwrite(&infoHeader, sizeof(infoHeader), 1, file) == 1;
      for (unsigned row = height; ok && row-- > 0;) {
        ok = std::fwrite(topDownBgra.data() + static_cast<std::size_t>(row) * width * 4u, width * 4u, 1, file) == 1;
      }
      ok = (std::fclose(file) == 0) && ok;
      return ok;
    }

    // GetHeadOutputContext -> GetRenderTargetData into a system-memory texture -> Lock. Only gal
    // calls, so the same code reads back any backend that implements the three.
    void CaptureHead(const int head, const unsigned frame)
    {
      gpg::gal::Device* const device = gpg::gal::Device::GetInstance();
      unsigned width = 0;
      unsigned height = 0;
      std::vector<std::uint8_t> pixels;
      try {
        gpg::gal::OutputContext* const output = device->GetHeadOutputContext(static_cast<unsigned int>(head));
        const boost::shared_ptr<gpg::gal::RenderTarget> surface = output->surface;
        if (surface.get() == nullptr) {
          detail::Abort(detail::kExitCaptureFailed, "frame %u: head %d has no colour surface", frame, head);
        }
        const gpg::gal::RenderTargetContext* const surfaceContext = surface->GetContext();
        width = surfaceContext->width_;
        height = surfaceContext->height_;

        gpg::gal::TextureContext textureContext{};
        textureContext.source_ = kTextureSourceEmpty;
        textureContext.usage_ = kTextureUsageSystemMemory;
        textureContext.format_ = kGalFormatA8R8G8B8;
        textureContext.mipmapLevels_ = 1;
        textureContext.width_ = width;
        textureContext.height_ = height;
        const boost::shared_ptr<gpg::gal::Texture> readback = device->CreateTexture(&textureContext);

        device->GetRenderTargetData(surface, readback);

        const RECT wholeLevel{}; // an empty rect locks the whole level (TextureD3D9::Lock)
        const gpg::gal::TextureLockRect lock = readback->Lock(0, wholeLevel, kLockReadOnly);
        pixels.resize(static_cast<std::size_t>(width) * height * 4u);
        for (unsigned row = 0; row < height; ++row) {
          std::memcpy(
            pixels.data() + static_cast<std::size_t>(row) * width * 4u,
            static_cast<const std::uint8_t*>(lock.bits) + static_cast<std::size_t>(row) * static_cast<std::size_t>(lock.pitch),
            width * 4u
          );
        }
        (void)readback->Unlock(lock);
      } catch (const gpg::gal::Error& error) {
        detail::Abort(
          detail::kExitCaptureFailed, "frame %u: gal readback failed: %s (%s:%d)", frame, error.what(),
          error.GetRuntimeMessage(), error.GetRuntimeLine()
        );
      } catch (const std::exception& error) {
        detail::Abort(detail::kExitCaptureFailed, "frame %u: readback failed: %s", frame, error.what());
      }

      std::uint64_t rgb = 0xcbf29ce484222325ull;
      std::uint64_t alpha = 0xcbf29ce484222325ull;
      for (std::size_t index = 0; index < pixels.size(); index += 4) {
        for (const std::size_t channel : {std::size_t{2}, std::size_t{1}, std::size_t{0}}) { // R, G, B
          rgb = (rgb ^ pixels[index + channel]) * 0x100000001b3ull;
        }
        alpha = (alpha ^ pixels[index + 3]) * 0x100000001b3ull;
      }

      wchar_t name[32];
      (void)std::swprintf(name, 32, L"frame_%06u.bmp", frame);
      if (!WriteBmp(Config().outDir + L"\\" + name, width, height, pixels)) {
        detail::Abort(detail::kExitCaptureFailed, "frame %u: could not write %s", frame, detail::Utf8(name).c_str());
      }

      CaptureRecord record{};
      record.frame = frame;
      record.file = detail::Utf8(name);
      record.width = width;
      record.height = height;
      record.rgbHash = Hex64(rgb);
      record.alphaHash = Hex64(alpha);
      record.clock = detail::ClockSeconds();
      gCaptures.push_back(record);
      Log("frame %u captured: %ux%u rgb %s alpha %s clock %.6f", frame, width, height, record.rgbHash.c_str(),
          record.alphaHash.c_str(), record.clock);
    }

    std::string DescribeDevice()
    {
      if (!gpg::gal::Device::IsReady()) {
        return "{}";
      }
      gpg::gal::DeviceContext* const context = gpg::gal::Device::GetInstance()->GetDeviceContext();
      const char* api = "unknown";
      switch (static_cast<int>(context->mDeviceType)) {
        case 1: api = "Direct3D9"; break;
        case 2: api = "Direct3D10"; break;
        case 3: api = "Diligent"; break; // port/graphics/diligent/GalDiligent.h DeviceApiDiligent
        default: break;
      }
      std::string json = "{\"api\": " + detail::JsonString(api) + ", \"api_code\": " +
                         std::to_string(static_cast<int>(context->mDeviceType)) + ", \"adapter\": " + std::to_string(context->mAdapter);
      if (context->GetHeadCount() > 0) {
        const gpg::gal::Head& head = context->GetHead(0);
        json += ", \"head0\": [" + std::to_string(head.mWidth) + ", " + std::to_string(head.mHeight) + "]";
        json += ", \"head0_fullscreen_flag\": " + std::string(head.mWindowed ? "true" : "false");
      }
      // Which d3d9.dll answered: a d3d9.dll beside main.exe (dgVoodoo in FAF's bin folder ships
      // one, disabled as D3D9.dll.off) would be loaded instead of the system one.
      wchar_t modulePath[MAX_PATH]{};
      if (const HMODULE d3d9 = ::GetModuleHandleW(L"d3d9.dll"); d3d9 != nullptr) {
        (void)::GetModuleFileNameW(d3d9, modulePath, MAX_PATH);
      }
      json += ", \"d3d9_dll\": " + detail::JsonString(detail::Utf8(modulePath)) + "}";
      return json;
    }

    // FAF's import keeps every loaded module in the global __modules table (lua.nx2
    // lua/system/import.lua:17); the main menu is the module lua/ui/menus/main.lua.
    bool MainMenuModuleLoaded()
    {
      LuaPlus::LuaState* const state = moho::USER_GetLuaState();
      if (state == nullptr) {
        return false;
      }
      const LuaPlus::LuaObject modules = state->GetGlobals().GetByName("__modules");
      return modules.IsTable() && modules.GetByName("/lua/ui/menus/main.lua").IsTable();
    }

    std::string LuaErrorText(lua_State* const state)
    {
      const char* const text = lua_isstring(state, -1) ? lua_tostring(state, -1) : nullptr;
      return text != nullptr ? text : "(error object is not a string)";
    }

    void LoadScript()
    {
      const std::wstring& path = Config().scriptPath;
      if (path.empty()) {
        return;
      }
      std::string text;
      if (std::FILE* const file = _wfopen(path.c_str(), L"rb"); file != nullptr) {
        char buffer[4096];
        for (std::size_t read = 0; (read = std::fread(buffer, 1, sizeof(buffer), file)) != 0;) {
          text.append(buffer, read);
        }
        (void)std::fclose(file);
      }
      if (text.empty()) {
        detail::Abort(detail::kExitScriptFailed, "/galscript %s is empty or unreadable", detail::Utf8(path).c_str());
      }
      std::uint64_t hash = 0xcbf29ce484222325ull;
      for (const char c : text) {
        hash = (hash ^ static_cast<std::uint8_t>(c)) * 0x100000001b3ull;
      }
      gScriptFnv = Hex64(hash);

      LuaPlus::LuaState* const user = moho::USER_GetLuaState();
      if (user == nullptr || user->m_state == nullptr) {
        detail::Abort(detail::kExitScriptFailed, "/galscript: no user Lua state");
      }
      lua_State* const state = user->m_state;
      const int top = lua_gettop(state);
      const std::string chunkName = "@" + detail::Utf8(path);
      int status = luaL_loadbuffer(state, text.data(), text.size(), chunkName.c_str());
      if (status == 0) {
        status = LuaCallProtected(state, 0, 0);
      }
      if (status != 0) {
        const std::string message = LuaErrorText(state);
        lua_settop(state, top);
        detail::Abort(detail::kExitScriptFailed, "/galscript did not load: %s", message.c_str());
      }
      lua_pushstring(state, "GalHarnessStep");
      lua_gettable(state, LUA_GLOBALSINDEX);
      const bool defined = lua_isfunction(state, -1);
      lua_settop(state, top);
      if (!defined) {
        detail::Abort(detail::kExitScriptFailed, "/galscript %s defines no function GalHarnessStep(frame)", detail::Utf8(path).c_str());
      }
      gScriptLoaded = true;
      Log("navigation script loaded: %s (%u bytes, fnv1a64 %s)", detail::Utf8(path).c_str(), static_cast<unsigned>(text.size()),
          gScriptFnv.c_str());
    }

    void RunScriptStep(const unsigned frame)
    {
      if (!gScriptLoaded) {
        return;
      }
      lua_State* const state = moho::USER_GetLuaState()->m_state;
      const int top = lua_gettop(state);
      lua_pushstring(state, "GalHarnessStep");
      lua_gettable(state, LUA_GLOBALSINDEX);
      lua_pushnumber(state, static_cast<float>(frame));
      const int status = LuaCallProtected(state, 1, 1);
      ++gScriptCalls;
      if (status != 0) {
        const std::string message = LuaErrorText(state);
        lua_settop(state, top);
        detail::Abort(detail::kExitScriptFailed, "/galscript step at frame %u failed: %s", frame, message.c_str());
      }
      if (lua_isstring(state, -1)) {
        ScriptAction action;
        action.frame = frame;
        action.note = lua_tostring(state, -1);
        Log("script frame %u: %s", frame, action.note.c_str());
        gScriptActions.push_back(std::move(action));
      }
      lua_settop(state, top);
    }

    void Pace()
    {
      const double fps = Config().paceFps;
      LARGE_INTEGER now{};
      (void)::QueryPerformanceCounter(&now);
      if (fps > 0.0 && gLastPaint.QuadPart != 0) {
        const double period = 1.0 / fps;
        const double elapsed = static_cast<double>(now.QuadPart - gLastPaint.QuadPart) / static_cast<double>(gFrequency.QuadPart);
        if (elapsed < period) {
          ::Sleep(static_cast<DWORD>((period - elapsed) * 1000.0));
          (void)::QueryPerformanceCounter(&now);
        }
      }
      gLastPaint = now;
    }

    // A teardown that hangs must not keep a hidden process around: the summary is written.
    DWORD WINAPI TeardownWatchdog(LPVOID)
    {
      ::Sleep(30000);
      Log("teardown did not finish within 30 s; terminating (the captures and summary are complete)");
      ::TerminateProcess(::GetCurrentProcess(), detail::kExitComplete);
      return 0;
    }

    void Finish()
    {
      gFinished = true;
      gMainMenuLoaded = MainMenuModuleLoaded();
      gDeviceDescription = DescribeDevice();
      detail::WriteSummary("complete", detail::kExitComplete, "");
      Log("all frames done; asking the engine to exit");

      const HANDLE watchdog = ::CreateThread(nullptr, 0, &TeardownWatchdog, nullptr, 0, nullptr);
      if (watchdog != nullptr) {
        (void)::CloseHandle(watchdog);
      }
      moho::WIN_AppRequestExit();
    }

    void RunPaint(const unsigned frame)
    {
      if (gFinished) {
        return;
      }
      Pace();

      gPaintFrame = frame;
      gRendersInPaint = 0;
      gInPaint = true;
      if (frame <= 3) {
        Log("frame %u: paint", frame);
      }
      if (frame == 1) {
        // Where the first paint is dispatched from (see HarnessFrame): logged once, as evidence.
        void* callers[24]{};
        const USHORT count = ::RtlCaptureStackBackTrace(0, 24, callers, nullptr);
        for (USHORT index = 0; index < count; ++index) {
          moho::SPlatSymbolInfo symbol{};
          if (moho::PLAT_GetSymbolInfo(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(callers[index])), &symbol)) {
            Log("  paint 1 caller #%02u %s", index, symbol.FormatResolvedLine().c_str());
          }
        }
      }
      // Paint handles gal errors itself (CD3DDevice.cpp:1923-1926); nothing else may unwind
      // through the window procedure and wx's Dispatch.
      try {
        if (moho::CD3DDevice* const device = moho::D3D_GetDevice(); device != nullptr) {
          device->Paint();
        }
      } catch (const std::exception& error) {
        detail::Abort(detail::kExitCaptureFailed, "frame %u: exception during paint: %s", frame, error.what());
      } catch (...) {
        detail::Abort(detail::kExitCaptureFailed, "frame %u: unknown exception during paint", frame);
      }
      gInPaint = false;
      ++gPaints;
      if (frame <= 3) {
        Log("frame %u: painted, %u render(s)", frame, gRendersInPaint);
      }
      if (gRendersInPaint == 0) {
        ++gPaintsWithoutRender;
      }

      if (IsCaptureFrame(frame) && !IsCaptured(frame)) {
        detail::Abort(
          detail::kExitCaptureFailed, "frame %u was not rendered (CD3DDevice::Paint did not reach EndScene for head 0)", frame
        );
      }

      std::string violation;
      if (!detail::WindowsHiddenAndNotForeground(&violation)) {
        detail::Abort(detail::kExitVisibility, "frame %u: %s", frame, violation.c_str());
      }

      if (frame % 100 == 0) {
        Log("frame %u painted (paints without render so far: %u)", frame, gPaintsWithoutRender);
      }
      if (frame >= Config().exitFrame) {
        Finish();
      }
    }

    LRESULT CALLBACK PaintWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
      if (message == kPaintMessage) {
        RunPaint(static_cast<unsigned>(wParam));
        return 0;
      }
      return ::DefWindowProcW(window, message, wParam, lParam);
    }

    void OnFirstFrame()
    {
      (void)::QueryPerformanceFrequency(&gFrequency);

      WNDCLASSW windowClass{};
      windowClass.lpfnWndProc = &PaintWindowProc;
      windowClass.hInstance = ::GetModuleHandleW(nullptr);
      windowClass.lpszClassName = kPaintWindowClass;
      (void)::RegisterClassW(&windowClass);
      gPaintWindow = ::CreateWindowExW(
        0, kPaintWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, ::GetModuleHandleW(nullptr), nullptr
      );
      if (gPaintWindow == nullptr) {
        detail::Abort(detail::kExitRefused, "could not create the harness's message window (error %lu)", ::GetLastError());
      }

      std::string error;
      if (!detail::VerifyPinsOnFirstFrame(&error)) {
        detail::Abort(detail::kExitRefused, "%s", error.c_str());
      }
      if (!Config().noPins) {
        detail::ReseedRandomStream();
      }
      LoadScript();
      gDeviceDescription = DescribeDevice();
      Log("first frame: device %s", gDeviceDescription.c_str());
      if (gpg::gal::Device::IsReady() && gpg::gal::Device::GetInstance()->GetDeviceContext()->GetHeadCount() > 0) {
        const gpg::gal::Head& head = gpg::gal::Device::GetInstance()->GetDeviceContext()->GetHead(0);
        RECT frameClient{};
        RECT viewClient{};
        RECT frameWindow{};
        (void)::GetClientRect(static_cast<HWND>(head.mHandle), &frameClient);
        (void)::GetClientRect(static_cast<HWND>(head.mWindow), &viewClient);
        (void)::GetWindowRect(static_cast<HWND>(head.mHandle), &frameWindow);
        Log("first frame: frame client %ldx%ld, viewport client %ldx%ld, frame window at %ld,%ld size %ldx%ld",
            frameClient.right, frameClient.bottom, viewClient.right, viewClient.bottom, frameWindow.left, frameWindow.top,
            frameWindow.right - frameWindow.left, frameWindow.bottom - frameWindow.top);
      }

      std::string violation;
      if (!detail::WindowsHiddenAndNotForeground(&violation)) {
        detail::Abort(detail::kExitVisibility, "before the first frame: %s", violation.c_str());
      }
    }
  } // namespace

  void HarnessFrame(const float frameSeconds)
  {
    if (!HarnessActive() || gFinished) {
      return;
    }
    ++gAppFrames;
    if (gAppFrames == 1) {
      OnFirstFrame();
    }
    detail::AdvanceClock(frameSeconds);
    RunScriptStep(gAppFrames);
    (void)::PostMessageW(gPaintWindow, kPaintMessage, static_cast<WPARAM>(gAppFrames), 0);
    // The loop must not sleep after this Main: on the first frame the paint message is already
    // dispatched inside Main - InitializeSessionFromCommandLine (CScApp.cpp:1108-1111) starts the
    // front end, whose Lua calls FlushEvents, a nested message pump (UiRuntimeTypes.cpp:17633-17651,
    // 17664-17694; harness.log logs the stack of paint 1) - and a hidden window has no pending WM_PAINT
    // to wake MsgWaitEx afterwards (WinApp.cpp:2805-2807). A zero wake-up time returns that wait
    // at once. (A visible run's pending WM_PAINT would be taken by that pump as well: PeekMessage
    // returns WM_PAINT once nothing else is queued.)
    moho::WIN_SetWakeupTimer(0.0f);
  }

  void HarnessSceneEnded(const int head)
  {
    if (!HarnessActive()) {
      return;
    }
    if (!gInPaint) {
      ++gExtraRenders; // a Render outside a harness paint: not expected on the menu
      return;
    }
    if (++gRendersInPaint > 1) {
      ++gExtraRenders;
    }
    if (head == 0 && IsCaptureFrame(gPaintFrame) && !IsCaptured(gPaintFrame)) {
      CaptureHead(head, gPaintFrame);
    }
  }
} // namespace port::graphics::capture

namespace port::graphics::capture::detail
{
  unsigned Progress() noexcept
  {
    return gAppFrames + gPaints;
  }

  void WriteSummary(const char* const status, const int exitCode, const std::string& reason)
  {
    const HarnessConfig& config = Config();
    if (config.outDir.empty()) {
      return;
    }
    std::string json = "{\n";
    json += "  \"status\": " + JsonString(status) + ",\n";
    json += "  \"exit_code\": " + std::to_string(exitCode) + ",\n";
    json += "  \"reason\": " + JsonString(reason) + ",\n";
    json += "  \"command_line\": " + JsonString(config.commandLine) + ",\n";
    json += "  \"config\": {\"frames\": [";
    for (std::size_t index = 0; index < config.frames.size(); ++index) {
      json += (index != 0 ? ", " : "") + std::to_string(config.frames[index]);
    }
    char fixedDelta[32];
    (void)std::snprintf(fixedDelta, sizeof(fixedDelta), "%.9g", static_cast<double>(config.fixedFrameSeconds));
    json += "], \"exit_frame\": " + std::to_string(config.exitFrame) + ", \"pace_fps\": " + std::to_string(config.paceFps) +
            ", \"fixed_frame_seconds\": " + fixedDelta + "},\n";
    json += "  \"device\": " + (gDeviceDescription.empty() ? std::string("{}") : gDeviceDescription) + ",\n";
    json += "  \"app_frames\": " + std::to_string(gAppFrames) + ",\n";
    json += "  \"paints\": " + std::to_string(gPaints) + ",\n";
    json += "  \"paints_without_render\": " + std::to_string(gPaintsWithoutRender) + ",\n";
    json += "  \"extra_renders\": " + std::to_string(gExtraRenders) + ",\n";
    json += "  \"main_menu_module_loaded\": " + std::string(gMainMenuLoaded ? "true" : "false") + ",\n";
    json += "  \"captures\": [";
    for (std::size_t index = 0; index < gCaptures.size(); ++index) {
      const CaptureRecord& record = gCaptures[index];
      char clock[32];
      (void)std::snprintf(clock, sizeof(clock), "%.9g", record.clock);
      json += std::string(index != 0 ? "," : "") + "\n    {\"frame\": " + std::to_string(record.frame) +
              ", \"file\": " + JsonString(record.file) + ", \"width\": " + std::to_string(record.width) +
              ", \"height\": " + std::to_string(record.height) + ", \"rgb_fnv1a64\": " + JsonString(record.rgbHash) +
              ", \"alpha_fnv1a64\": " + JsonString(record.alphaHash) + ", \"clock_seconds\": " + clock + "}";
    }
    json += std::string(gCaptures.empty() ? "" : "\n  ") + "],\n";
    json += "  \"script\": {\"path\": " + JsonString(Utf8(config.scriptPath)) + ", \"fnv1a64\": " + JsonString(gScriptFnv) +
            ", \"loaded\": " + (gScriptLoaded ? "true" : "false") + ", \"calls\": " + std::to_string(gScriptCalls) + ", \"actions\": [";
    for (std::size_t index = 0; index < gScriptActions.size(); ++index) {
      json += std::string(index != 0 ? ", " : "") + "{\"frame\": " + std::to_string(gScriptActions[index].frame) +
              ", \"note\": " + JsonString(gScriptActions[index].note) + "}";
    }
    json += "]},\n";
    json += "  \"pins\": " + PinsReportJson() + ",\n";
    json += "  \"sandbox\": " + SandboxReportJson() + "\n";
    json += "}\n";

    const std::wstring path = config.outDir + L"\\harness.json";
    if (std::FILE* const file = _wfopen(path.c_str(), L"wb"); file != nullptr) {
      (void)std::fwrite(json.data(), 1, json.size(), file);
      (void)std::fclose(file);
    }
  }

  void Abort(const int code, const char* const format, ...)
  {
    char reason[2048];
    va_list args;
    va_start(args, format);
    (void)std::vsnprintf(reason, sizeof(reason), format, args);
    va_end(args);
    Log("ABORT (exit %d): %s", code, reason);
    WriteSummary(code == kExitRefused ? "refused" : "failed", code, reason);
    ::TerminateProcess(::GetCurrentProcess(), static_cast<UINT>(code));
    for (;;) {
      ::Sleep(INFINITE); // TerminateProcess does not return for the current process
    }
  }
} // namespace port::graphics::capture::detail
