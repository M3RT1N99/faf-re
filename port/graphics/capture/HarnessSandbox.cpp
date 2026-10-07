// The frame harness's sandbox: everything that keeps a harness run of main.exe invisible and
// side-effect free for whoever is using the desktop while it runs (M6a hard rule: the user may
// be playing a game on this machine). Port-only, FAF_PORT_GRAPHICS builds; inert unless
// `/galharness` is on the command line.
//
// It is installed from a static initialiser, before WinMain, because the first offenders run
// early: WinMain changes the sticky/toggle/filter-key settings (WinMain.cpp:709-712) and
// WIN_AppExecute installs a global low-level keyboard hook (WinApp.cpp:2735) before CScApp::Init.
// Three layers:
//   1. main.exe's import table. The engine, wx 2.4.2 (static) and LuaPlus all call user32 and
//      kernel32 through it, so redirecting an entry covers every call site at once. What each
//      redirect does is in kImportHooks below; each call is counted for the summary.
//   2. Thread hooks on the UI thread (thread-local, no effect on other processes): WH_CBT stops
//      every activation of our windows and creates top-level windows hidden and off-screen;
//      WH_GETMESSAGE turns any keyboard, mouse or touch message into WM_NULL.
//   3. A monitor, run after every harness frame: no window of this process may be visible or
//      the foreground window. A violation hides the window and ends the run (exit code 11),
//      because the rule is to stop and report rather than show anything.

#include "port/graphics/capture/GalCapture.h"
#include "port/graphics/capture/HarnessInternal.h"

#include <windows.h>
#include <shellapi.h>

#include <crtdbg.h>

#include "moho/app/WinApp.h"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace port::graphics::capture::detail
{
  namespace
  {
    HarnessConfig gConfig{};

    // ---- log -------------------------------------------------------------------------------

    CRITICAL_SECTION gLogLock;
    bool gLogLockReady = false;
    std::FILE* gLogFile = nullptr;
    LARGE_INTEGER gStartCounter{};
    LARGE_INTEGER gCounterFrequency{};

    // ---- interception report ---------------------------------------------------------------

    struct InterceptRecord
    {
      unsigned count = 0;
      std::vector<std::string> samples; // the first few, verbatim, for the summary
    };

    std::mutex gReportLock;
    std::map<std::string, InterceptRecord> gIntercepts;
    std::vector<std::string> gCreatedWindows;
    std::vector<std::string> gViolations;
    std::atomic<unsigned> gDroppedInput{0};
    std::atomic<unsigned> gBlockedActivations{0};
    std::vector<std::string> gImportHookResults;

    constexpr std::size_t kMaxSamples = 6;

    void Record(const char* api, const std::string& detail)
    {
      unsigned count = 0;
      {
        std::lock_guard<std::mutex> lock(gReportLock);
        InterceptRecord& record = gIntercepts[api];
        count = ++record.count;
        if (record.samples.size() < kMaxSamples) {
          record.samples.push_back(detail);
        }
      }
      if (count <= kMaxSamples || count % 100 == 0) {
        Log("intercepted %s (#%u): %s", api, count, detail.c_str());
      }
    }

    std::string Narrow(const wchar_t* text)
    {
      return text != nullptr ? Utf8(text) : std::string("(null)");
    }

    std::string Format(const char* format, ...)
    {
      char buffer[1024];
      va_list args;
      va_start(args, format);
      (void)std::vsnprintf(buffer, sizeof(buffer), format, args);
      va_end(args);
      return buffer;
    }

    std::string DescribeWindow(HWND window)
    {
      if (window == nullptr) {
        return "hwnd=null";
      }
      wchar_t className[128]{};
      wchar_t title[128]{};
      (void)::GetClassNameW(window, className, 127);
      (void)::GetWindowTextW(window, title, 127);
      return Format("hwnd=%p class='%s' title='%s'", static_cast<void*>(window), Utf8(className).c_str(), Utf8(title).c_str());
    }

    bool IsTopLevel(HWND window)
    {
      return window != nullptr && (::GetWindowLongW(window, GWL_STYLE) & WS_CHILD) == 0;
    }

    // Top-level windows are placed here: left of and above the whole virtual screen, so that
    // even code that polls the global cursor position (CUIManager::GetControlAtCursor via
    // wxGetMousePosition, CUIManager.cpp:545) can never find the cursor inside them.
    POINT OffscreenOrigin()
    {
      return POINT{::GetSystemMetrics(SM_XVIRTUALSCREEN) - 20000, ::GetSystemMetrics(SM_YVIRTUALSCREEN) - 20000};
    }

    // ---- original entry points (the import table points at the hooks once installed) -------

    using ShowWindowFn = BOOL(WINAPI*)(HWND, int);
    using SetForegroundWindowFn = BOOL(WINAPI*)(HWND);
    using BringWindowToTopFn = BOOL(WINAPI*)(HWND);
    using SetFocusFn = HWND(WINAPI*)(HWND);
    using SetWindowPosFn = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
    using MoveWindowFn = BOOL(WINAPI*)(HWND, int, int, int, int, BOOL);
    using ClipCursorFn = BOOL(WINAPI*)(const RECT*);
    using SetCursorPosFn = BOOL(WINAPI*)(int, int);
    using SetCaptureFn = HWND(WINAPI*)(HWND);
    using SetWindowsHookExWFn = HHOOK(WINAPI*)(int, HOOKPROC, HINSTANCE, DWORD);
    using SystemParametersInfoWFn = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT);
    using MessageBoxWFn = int(WINAPI*)(HWND, LPCWSTR, LPCWSTR, UINT);
    using MessageBoxAFn = int(WINAPI*)(HWND, LPCSTR, LPCSTR, UINT);
    using DialogBoxParamWFn = INT_PTR(WINAPI*)(HINSTANCE, LPCWSTR, HWND, DLGPROC, LPARAM);
    using KeybdEventFn = VOID(WINAPI*)(BYTE, BYTE, DWORD, ULONG_PTR);
    using ExitWindowsExFn = BOOL(WINAPI*)(UINT, DWORD);
    using CreateProcessWFn = BOOL(WINAPI*)(
      LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW,
      LPPROCESS_INFORMATION
    );
    using CreateProcessAFn = BOOL(WINAPI*)(
      LPCSTR, LPSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCSTR, LPSTARTUPINFOA,
      LPPROCESS_INFORMATION
    );
    using ShellExecuteWFn = HINSTANCE(WINAPI*)(HWND, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, INT);
    using ShellExecuteExWFn = BOOL(WINAPI*)(SHELLEXECUTEINFOW*);
    using MessageBeepFn = BOOL(WINAPI*)(UINT);
    using SetThreadPriorityFn = BOOL(WINAPI*)(HANDLE, int);
    using GetProcAddressFn = FARPROC(WINAPI*)(HMODULE, LPCSTR);
    using SetPriorityClassFn = BOOL(WINAPI*)(HANDLE, DWORD);

    ShowWindowFn gShowWindow = &::ShowWindow;
    SetForegroundWindowFn gSetForegroundWindow = &::SetForegroundWindow;
    BringWindowToTopFn gBringWindowToTop = &::BringWindowToTop;
    SetFocusFn gSetFocus = &::SetFocus;
    SetWindowPosFn gSetWindowPos = &::SetWindowPos;
    MoveWindowFn gMoveWindow = &::MoveWindow;
    ClipCursorFn gClipCursor = &::ClipCursor;
    SetCursorPosFn gSetCursorPos = &::SetCursorPos;
    SetCaptureFn gSetCapture = &::SetCapture;
    SetWindowsHookExWFn gSetWindowsHookExW = &::SetWindowsHookExW;
    SystemParametersInfoWFn gSystemParametersInfoW = &::SystemParametersInfoW;
    MessageBoxWFn gMessageBoxW = &::MessageBoxW;
    MessageBoxAFn gMessageBoxA = &::MessageBoxA;
    DialogBoxParamWFn gDialogBoxParamW = &::DialogBoxParamW;
    KeybdEventFn gKeybdEvent = &::keybd_event;
    ExitWindowsExFn gExitWindowsEx = &::ExitWindowsEx;
    CreateProcessWFn gCreateProcessW = &::CreateProcessW;
    CreateProcessAFn gCreateProcessA = &::CreateProcessA;
    ShellExecuteWFn gShellExecuteW = &::ShellExecuteW;
    ShellExecuteExWFn gShellExecuteExW = &::ShellExecuteExW;
    MessageBeepFn gMessageBeep = &::MessageBeep;
    SetThreadPriorityFn gSetThreadPriority = &::SetThreadPriority;
    GetProcAddressFn gGetProcAddress = &::GetProcAddress;

    // ---- the redirects ---------------------------------------------------------------------

    // wxWindowMSW::Show(true) is ShowWindow(SW_SHOW) followed by BringWindowToTop for
    // top-level windows, which is how a normal start takes the foreground (CScApp.cpp:1427,
    // 1480). Top-level windows stay hidden; children are shown as asked (their parent is not).
    BOOL WINAPI HookShowWindow(HWND window, int command)
    {
      if (command != SW_HIDE && IsTopLevel(window)) {
        Record("ShowWindow", Format("kept hidden (cmd %d) %s", command, DescribeWindow(window).c_str()));
        return ::IsWindowVisible(window);
      }
      return gShowWindow(window, command);
    }

    BOOL WINAPI HookSetForegroundWindow(HWND window)
    {
      Record("SetForegroundWindow", DescribeWindow(window));
      return FALSE;
    }

    BOOL WINAPI HookBringWindowToTop(HWND window)
    {
      Record("BringWindowToTop", DescribeWindow(window));
      return TRUE;
    }

    // SetFocus on a window whose top-level window is not active activates it.
    HWND WINAPI HookSetFocus(HWND window)
    {
      Record("SetFocus", DescribeWindow(window));
      return ::GetFocus();
    }

    BOOL WINAPI HookSetWindowPos(HWND window, HWND insertAfter, int x, int y, int cx, int cy, UINT flags)
    {
      UINT applied = flags | SWP_NOACTIVATE;
      if (IsTopLevel(window)) {
        if ((applied & SWP_SHOWWINDOW) != 0) {
          Record("SetWindowPos", Format("SWP_SHOWWINDOW dropped %s", DescribeWindow(window).c_str()));
          applied &= ~SWP_SHOWWINDOW;
        }
        if ((applied & SWP_NOMOVE) == 0) {
          const POINT origin = OffscreenOrigin();
          x = origin.x;
          y = origin.y;
        }
        if (insertAfter == HWND_TOPMOST || insertAfter == HWND_TOP) {
          applied |= SWP_NOZORDER;
        }
      }
      return gSetWindowPos(window, insertAfter, x, y, cx, cy, applied);
    }

    BOOL WINAPI HookMoveWindow(HWND window, int x, int y, int width, int height, BOOL repaint)
    {
      if (IsTopLevel(window)) {
        const POINT origin = OffscreenOrigin();
        x = origin.x;
        y = origin.y;
      }
      return gMoveWindow(window, x, y, width, height, repaint);
    }

    // The cursor clip is one per desktop: `SC_ToggleCursorClip 0`, which OPTIONS_Apply runs at
    // every start (FAF options.lua lock_fullscreen_cursor_to_window), and CScApp::Destroy
    // (CScApp.cpp:1166) both call ClipCursor(nullptr), which would free a game's clipped cursor.
    BOOL WINAPI HookClipCursor(const RECT* rect)
    {
      Record(
        "ClipCursor",
        rect != nullptr ? Format("rect %ld,%ld,%ld,%ld", rect->left, rect->top, rect->right, rect->bottom)
                        : std::string("release (nullptr)")
      );
      return TRUE;
    }

    BOOL WINAPI HookSetCursorPos(int x, int y)
    {
      Record("SetCursorPos", Format("%d,%d", x, y));
      return TRUE;
    }

    HWND WINAPI HookSetCapture(HWND window)
    {
      Record("SetCapture", DescribeWindow(window));
      return nullptr;
    }

    // Global hooks (dwThreadId 0, or the low-level kinds, which are always global) run in this
    // process for every key the user presses anywhere; WIN_AppExecute's WH_KEYBOARD_LL
    // (WinApp.cpp:2735) is one. Thread-local hooks are left alone.
    HHOOK WINAPI HookSetWindowsHookExW(int kind, HOOKPROC procedure, HINSTANCE module, DWORD threadId)
    {
      if (threadId == 0 || kind == WH_KEYBOARD_LL || kind == WH_MOUSE_LL) {
        Record("SetWindowsHookExW", Format("global hook kind %d not installed", kind));
        ::SetLastError(ERROR_ACCESS_DENIED);
        return nullptr;
      }
      return gSetWindowsHookExW(kind, procedure, module, threadId);
    }

    // SPI_SET* changes system-wide settings. The engine sets the screensaver flag
    // (CScApp.cpp:979, 1143) and the sticky/toggle/filter-key hotkeys (WinMain.cpp:264-285).
    bool IsSystemSettingWrite(const UINT action, const UINT winIni)
    {
      static constexpr UINT kClassicSets[] = {
        0x0002, 0x0004, 0x0006, 0x000B, 0x000D, 0x000F, 0x0011, 0x0013, 0x0014, 0x0015, 0x0017, 0x0018,
        0x001A, 0x001C, 0x001D, 0x001E, 0x0020, 0x0021, 0x0022, 0x0024, 0x0025, 0x002A, 0x002C, 0x002E,
        0x002F, 0x0031, 0x0033, 0x0035, 0x0037, 0x0039, 0x003B, 0x003D, 0x003F, 0x0041, 0x0043, 0x0045,
        0x0047, 0x0049, 0x004B, 0x004C, 0x004D, 0x004E, 0x0051, 0x0052, 0x0055, 0x0056, 0x0057, 0x0058,
        0x005A, 0x005B, 0x005D, 0x0061, 0x0063, 0x0065, 0x0067, 0x0069, 0x006B, 0x006F, 0x0071,
      };
      if (winIni != 0) {
        return true;
      }
      if (action >= 0x1000) {
        return (action & 1u) != 0; // the 0x1000+ ranges pair GET n with SET n+1
      }
      return std::find(std::begin(kClassicSets), std::end(kClassicSets), action) != std::end(kClassicSets);
    }

    BOOL WINAPI HookSystemParametersInfoW(UINT action, UINT parameter, PVOID data, UINT winIni)
    {
      if (IsSystemSettingWrite(action, winIni)) {
        Record("SystemParametersInfoW", Format("set action 0x%04X (param %u, winIni %u) not applied", action, parameter, winIni));
        return TRUE;
      }
      return gSystemParametersInfoW(action, parameter, data, winIni);
    }

    // A message box or modal dialog would be a window the user sees (WIN_OkBox passes
    // MB_TOPMOST, WinApp.cpp:3570) and would block the run. gpg::Die ends up here too, through
    // WIN_ShowCrashDialog (WinApp.cpp:3542). The text goes to the summary and the run stops.
    int WINAPI HookMessageBoxW(HWND, LPCWSTR text, LPCWSTR caption, UINT)
    {
      Record("MessageBoxW", Format("'%s': %s", Narrow(caption).c_str(), Narrow(text).c_str()));
      Abort(kExitModalUi, "message box '%s': %s", Narrow(caption).c_str(), Narrow(text).c_str());
    }

    int WINAPI HookMessageBoxA(HWND, LPCSTR text, LPCSTR caption, UINT)
    {
      Record("MessageBoxA", Format("'%s': %s", caption != nullptr ? caption : "", text != nullptr ? text : ""));
      Abort(kExitModalUi, "message box '%s': %s", caption != nullptr ? caption : "", text != nullptr ? text : "");
    }

    INT_PTR WINAPI HookDialogBoxParamW(HINSTANCE, LPCWSTR templateName, HWND, DLGPROC, LPARAM)
    {
      const std::string name = IS_INTRESOURCE(templateName)
        ? Format("#%u", static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(templateName)))
        : Narrow(templateName);
      Record("DialogBoxParamW", Format("template %s", name.c_str()));
      Abort(kExitModalUi, "modal dialog %s (WIN_ShowCrashDialog: the CRASH lines of the engine log say why)", name.c_str());
    }

    VOID WINAPI HookKeybdEvent(BYTE key, BYTE scan, DWORD flags, ULONG_PTR)
    {
      Record("keybd_event", Format("vk %u scan %u flags 0x%lX not injected", key, scan, flags));
    }

    BOOL WINAPI HookExitWindowsEx(UINT flags, DWORD reason)
    {
      Record("ExitWindowsEx", Format("flags 0x%X reason 0x%lX", flags, reason));
      ::SetLastError(ERROR_ACCESS_DENIED);
      return FALSE;
    }

    BOOL WINAPI HookCreateProcessW(
      LPCWSTR application, LPWSTR commandLine, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID,
      LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION
    )
    {
      Record("CreateProcessW", Format("%s %s", Narrow(application).c_str(), Narrow(commandLine).c_str()));
      ::SetLastError(ERROR_ACCESS_DENIED);
      return FALSE;
    }

    BOOL WINAPI HookCreateProcessA(
      LPCSTR application, LPSTR commandLine, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID,
      LPCSTR, LPSTARTUPINFOA, LPPROCESS_INFORMATION
    )
    {
      Record("CreateProcessA", Format("%s %s", application != nullptr ? application : "(null)", commandLine != nullptr ? commandLine : ""));
      ::SetLastError(ERROR_ACCESS_DENIED);
      return FALSE;
    }

    HINSTANCE WINAPI HookShellExecuteW(HWND, LPCWSTR operation, LPCWSTR file, LPCWSTR parameters, LPCWSTR, INT)
    {
      Record("ShellExecuteW", Format("%s %s %s", Narrow(operation).c_str(), Narrow(file).c_str(), Narrow(parameters).c_str()));
      return reinterpret_cast<HINSTANCE>(static_cast<std::uintptr_t>(SE_ERR_ACCESSDENIED));
    }

    BOOL WINAPI HookShellExecuteExW(SHELLEXECUTEINFOW* info)
    {
      Record("ShellExecuteExW", info != nullptr ? Narrow(info->lpFile) : std::string("(null)"));
      ::SetLastError(ERROR_ACCESS_DENIED);
      return FALSE;
    }

    BOOL WINAPI HookMessageBeep(UINT type)
    {
      Record("MessageBeep", Format("type 0x%X", type));
      return TRUE;
    }

    // THREAD_PRIORITY_TIME_CRITICAL means base priority 15 in every non-realtime class, below
    // normal included; above-normal thread priorities are capped so no harness thread can
    // outrank the game's threads on its core.
    BOOL WINAPI HookSetThreadPriority(HANDLE thread, int priority)
    {
      if (priority > THREAD_PRIORITY_NORMAL && priority != THREAD_PRIORITY_ERROR_RETURN) {
        Record("SetThreadPriority", Format("priority %d capped to THREAD_PRIORITY_NORMAL", priority));
        return gSetThreadPriority(thread, THREAD_PRIORITY_NORMAL);
      }
      return gSetThreadPriority(thread, priority);
    }

    // init_faf.lua:31-41 raises the process to HIGH_PRIORITY_CLASS through the
    // SetProcessPriority binder, which looks SetPriorityClass up with GetProcAddress
    // (StartupHelpers.cpp:470-475). The class stays what the launcher set: below normal.
    BOOL WINAPI StubSetPriorityClass(HANDLE process, DWORD priorityClass)
    {
      Record("SetPriorityClass", Format("class 0x%lX for %p not applied", priorityClass, static_cast<void*>(process)));
      return TRUE;
    }

    FARPROC WINAPI HookGetProcAddress(HMODULE module, LPCSTR name)
    {
      if ((reinterpret_cast<std::uintptr_t>(name) >> 16) != 0 && std::strcmp(name, "SetPriorityClass") == 0) {
        return reinterpret_cast<FARPROC>(&StubSetPriorityClass);
      }
      return gGetProcAddress(module, name);
    }

    struct ImportHook
    {
      const char* dll;
      const char* name;
      void* replacement;
      void** original;
    };

    const ImportHook kImportHooks[] = {
      {"user32.dll", "ShowWindow", reinterpret_cast<void*>(&HookShowWindow), reinterpret_cast<void**>(&gShowWindow)},
      {"user32.dll", "SetForegroundWindow", reinterpret_cast<void*>(&HookSetForegroundWindow), reinterpret_cast<void**>(&gSetForegroundWindow)},
      {"user32.dll", "BringWindowToTop", reinterpret_cast<void*>(&HookBringWindowToTop), reinterpret_cast<void**>(&gBringWindowToTop)},
      {"user32.dll", "SetFocus", reinterpret_cast<void*>(&HookSetFocus), reinterpret_cast<void**>(&gSetFocus)},
      {"user32.dll", "SetWindowPos", reinterpret_cast<void*>(&HookSetWindowPos), reinterpret_cast<void**>(&gSetWindowPos)},
      {"user32.dll", "MoveWindow", reinterpret_cast<void*>(&HookMoveWindow), reinterpret_cast<void**>(&gMoveWindow)},
      {"user32.dll", "ClipCursor", reinterpret_cast<void*>(&HookClipCursor), reinterpret_cast<void**>(&gClipCursor)},
      {"user32.dll", "SetCursorPos", reinterpret_cast<void*>(&HookSetCursorPos), reinterpret_cast<void**>(&gSetCursorPos)},
      {"user32.dll", "SetCapture", reinterpret_cast<void*>(&HookSetCapture), reinterpret_cast<void**>(&gSetCapture)},
      {"user32.dll", "SetWindowsHookExW", reinterpret_cast<void*>(&HookSetWindowsHookExW), reinterpret_cast<void**>(&gSetWindowsHookExW)},
      {"user32.dll", "SystemParametersInfoW", reinterpret_cast<void*>(&HookSystemParametersInfoW), reinterpret_cast<void**>(&gSystemParametersInfoW)},
      {"user32.dll", "MessageBoxW", reinterpret_cast<void*>(&HookMessageBoxW), reinterpret_cast<void**>(&gMessageBoxW)},
      {"user32.dll", "MessageBoxA", reinterpret_cast<void*>(&HookMessageBoxA), reinterpret_cast<void**>(&gMessageBoxA)},
      {"user32.dll", "DialogBoxParamW", reinterpret_cast<void*>(&HookDialogBoxParamW), reinterpret_cast<void**>(&gDialogBoxParamW)},
      {"user32.dll", "keybd_event", reinterpret_cast<void*>(&HookKeybdEvent), reinterpret_cast<void**>(&gKeybdEvent)},
      {"user32.dll", "ExitWindowsEx", reinterpret_cast<void*>(&HookExitWindowsEx), reinterpret_cast<void**>(&gExitWindowsEx)},
      {"user32.dll", "MessageBeep", reinterpret_cast<void*>(&HookMessageBeep), reinterpret_cast<void**>(&gMessageBeep)},
      {"kernel32.dll", "CreateProcessW", reinterpret_cast<void*>(&HookCreateProcessW), reinterpret_cast<void**>(&gCreateProcessW)},
      {"kernel32.dll", "CreateProcessA", reinterpret_cast<void*>(&HookCreateProcessA), reinterpret_cast<void**>(&gCreateProcessA)},
      {"kernel32.dll", "SetThreadPriority", reinterpret_cast<void*>(&HookSetThreadPriority), reinterpret_cast<void**>(&gSetThreadPriority)},
      {"kernel32.dll", "GetProcAddress", reinterpret_cast<void*>(&HookGetProcAddress), reinterpret_cast<void**>(&gGetProcAddress)},
      {"shell32.dll", "ShellExecuteW", reinterpret_cast<void*>(&HookShellExecuteW), reinterpret_cast<void**>(&gShellExecuteW)},
      {"shell32.dll", "ShellExecuteExW", reinterpret_cast<void*>(&HookShellExecuteExW), reinterpret_cast<void**>(&gShellExecuteExW)},
    };

    // Rewrites the import address table entries of `module` that import `name` from `dll`
    // (by name; ordinal imports are left alone). Returns how many entries were changed.
    int PatchImport(HMODULE module, const ImportHook& hook)
    {
      auto* const base = reinterpret_cast<std::uint8_t*>(module);
      const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
      const auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
      const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
      if (directory.VirtualAddress == 0) {
        return 0;
      }

      int patched = 0;
      for (auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
           descriptor->Name != 0; ++descriptor) {
        const char* const dllName = reinterpret_cast<const char*>(base + descriptor->Name);
        if (_stricmp(dllName, hook.dll) != 0 || descriptor->OriginalFirstThunk == 0) {
          continue;
        }
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData != 0; ++names, ++slots) {
          if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) {
            continue;
          }
          const auto* const byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
          if (std::strcmp(reinterpret_cast<const char*>(byName->Name), hook.name) != 0) {
            continue;
          }
          DWORD oldProtection = 0;
          if (::VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), PAGE_READWRITE, &oldProtection) == FALSE) {
            continue;
          }
          *hook.original = reinterpret_cast<void*>(slots->u1.Function);
          slots->u1.Function = reinterpret_cast<ULONG_PTR>(hook.replacement);
          (void)::VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), oldProtection, &oldProtection);
          ++patched;
        }
      }
      return patched;
    }

    // ---- thread hooks on the UI thread -----------------------------------------------------

    HHOOK gCbtHook = nullptr;
    HHOOK gGetMessageHook = nullptr;

    LRESULT CALLBACK CbtHook(int code, WPARAM wParam, LPARAM lParam)
    {
      if (code == HCBT_ACTIVATE) {
        // Prevents the activation outright (CBTProc: nonzero blocks HCBT_ACTIVATE).
        const unsigned blocked = ++gBlockedActivations;
        if (blocked <= 4) {
          Log("activation blocked: %s", DescribeWindow(reinterpret_cast<HWND>(wParam)).c_str());
        }
        return 1;
      }
      if (code == HCBT_CREATEWND) {
        auto* const create = reinterpret_cast<CBT_CREATEWNDW*>(lParam);
        CREATESTRUCTW* const params = create != nullptr ? create->lpcs : nullptr;
        if (params != nullptr && (params->style & WS_CHILD) == 0 && params->hwndParent != HWND_MESSAGE) {
          params->style &= ~static_cast<LONG>(WS_VISIBLE);
          params->dwExStyle &= ~static_cast<DWORD>(WS_EX_TOPMOST);
          const POINT origin = OffscreenOrigin();
          params->x = origin.x;
          params->y = origin.y;
          const std::string className = IS_INTRESOURCE(params->lpszClass)
            ? Format("#%u", static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(params->lpszClass)))
            : Narrow(params->lpszClass);
          const std::string entry = Format(
            "class='%s' title='%s' %dx%d", className.c_str(), Narrow(params->lpszName).c_str(), params->cx, params->cy
          );
          {
            std::lock_guard<std::mutex> lock(gReportLock);
            gCreatedWindows.push_back(entry);
          }
          Log("top-level window created hidden and off-screen: %s", entry.c_str());
        }
      }
      return ::CallNextHookEx(gCbtHook, code, wParam, lParam);
    }

    bool IsInputMessage(const UINT message)
    {
      return (message >= WM_KEYFIRST && message <= WM_KEYLAST) || (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) ||
             (message >= WM_NCMOUSEMOVE && message <= WM_NCXBUTTONDBLCLK) || message == WM_INPUT ||
             message == WM_MOUSEHOVER || message == WM_MOUSELEAVE || message == WM_NCMOUSEHOVER ||
             message == WM_NCMOUSELEAVE || message == WM_TOUCH || (message >= 0x0241 && message <= 0x0257) || // WM_POINTER*
             message == WM_HOTKEY || message == WM_APPCOMMAND;
    }

    LRESULT CALLBACK GetMessageHook(int code, WPARAM wParam, LPARAM lParam)
    {
      if (code == HC_ACTION && wParam == PM_REMOVE) {
        auto* const message = reinterpret_cast<MSG*>(lParam);
        if (message != nullptr && IsInputMessage(message->message)) {
          const unsigned dropped = ++gDroppedInput;
          if (dropped <= 8) {
            Log("input message 0x%04X dropped (%s)", message->message, DescribeWindow(message->hwnd).c_str());
          }
          message->message = WM_NULL;
        }
      }
      return ::CallNextHookEx(gGetMessageHook, code, wParam, lParam);
    }

    // ---- monitor ---------------------------------------------------------------------------

    struct MonitorScan
    {
      DWORD processId = 0;
      std::vector<HWND> visible;
    };

    BOOL CALLBACK CollectVisibleWindows(HWND window, LPARAM context)
    {
      auto* const scan = reinterpret_cast<MonitorScan*>(context);
      DWORD owner = 0;
      (void)::GetWindowThreadProcessId(window, &owner);
      if (owner == scan->processId && ::IsWindowVisible(window)) {
        scan->visible.push_back(window);
      }
      return TRUE;
    }

    // ---- stall reporter --------------------------------------------------------------------

    // If the UI thread stops making frames, a run would only end at the launcher's timeout with
    // nothing to show. Every 15 s without progress this logs the UI thread's call stack: the
    // thread is suspended only while its frame-pointer chain is copied (Debug builds keep EBP
    // frames, /Oy-), and resumed before anything that could take a lock it holds (the log, the
    // heap, dbghelp through PLAT_GetSymbolInfo).
    HANDLE gUiThread = nullptr;

    unsigned CaptureUiStack(std::uint32_t* frames, const unsigned capacity)
    {
      if (::SuspendThread(gUiThread) == static_cast<DWORD>(-1)) {
        return 0;
      }
      CONTEXT context{};
      context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
      unsigned count = 0;
      if (::GetThreadContext(gUiThread, &context) != FALSE) {
        frames[count++] = context.Eip;
        std::uint32_t framePointer = context.Ebp;
        while (count < capacity && framePointer != 0) {
          std::uint32_t pair[2]{}; // saved EBP, return address
          SIZE_T read = 0;
          if (::ReadProcessMemory(::GetCurrentProcess(), reinterpret_cast<LPCVOID>(static_cast<std::uintptr_t>(framePointer)),
                                  pair, sizeof(pair), &read) == FALSE || read != sizeof(pair) || pair[0] <= framePointer) {
            break;
          }
          frames[count++] = pair[1];
          framePointer = pair[0];
        }
      }
      (void)::ResumeThread(gUiThread);
      return count;
    }

    DWORD WINAPI StallReporter(LPVOID)
    {
      unsigned lastProgress = Progress();
      DWORD stalledMs = 0;
      for (;;) {
        ::Sleep(1000);
        const unsigned progress = Progress();
        if (progress != lastProgress) {
          lastProgress = progress;
          stalledMs = 0;
          continue;
        }
        stalledMs += 1000;
        if (stalledMs % 15000 != 0) {
          continue;
        }
        std::uint32_t frames[32]{};
        const unsigned count = CaptureUiStack(frames, 32);
        Log("no progress for %lu s (progress counter %u); UI thread stack:", stalledMs / 1000, progress);
        for (unsigned index = 0; index < count; ++index) {
          moho::SPlatSymbolInfo symbol{};
          if (moho::PLAT_GetSymbolInfo(frames[index], &symbol)) {
            Log("  #%02u %08X %s", index, frames[index], symbol.FormatResolvedLine().c_str());
          } else {
            Log("  #%02u %08X", index, frames[index]);
          }
        }
      }
    }

    // ---- options ---------------------------------------------------------------------------

    bool OptionIs(const wchar_t* argument, const wchar_t* name)
    {
      return argument != nullptr && (argument[0] == L'/' || argument[0] == L'-') && _wcsicmp(argument + 1, name) == 0;
    }

    bool ParseFrames(const std::wstring& text, std::vector<unsigned>* frames)
    {
      frames->clear();
      if (_wcsicmp(text.c_str(), L"none") == 0) {
        return true;
      }
      std::size_t start = 0;
      while (start <= text.size()) {
        const std::size_t comma = text.find(L',', start);
        const std::wstring item = text.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
        wchar_t* end = nullptr;
        const unsigned long value = std::wcstoul(item.c_str(), &end, 10);
        if (item.empty() || end == nullptr || *end != L'\0' || value == 0 || value > 1000000) {
          return false;
        }
        frames->push_back(static_cast<unsigned>(value));
        if (comma == std::wstring::npos) {
          break;
        }
        start = comma + 1;
      }
      std::sort(frames->begin(), frames->end());
      frames->erase(std::unique(frames->begin(), frames->end()), frames->end());
      return true;
    }

    std::string FafEnvironmentVariables()
    {
      std::string found;
      wchar_t* const block = ::GetEnvironmentStringsW();
      if (block == nullptr) {
        return found;
      }
      for (const wchar_t* entry = block; *entry != L'\0'; entry += std::wcslen(entry) + 1) {
        if (_wcsnicmp(entry, L"FAF_", 4) == 0) {
          const wchar_t* const equals = std::wcschr(entry, L'=');
          const std::wstring name = equals != nullptr ? std::wstring(entry, equals) : std::wstring(entry);
          found += (found.empty() ? "" : ", ") + Utf8(name);
        }
      }
      (void)::FreeEnvironmentStringsW(block);
      return found;
    }

    void OpenLog()
    {
      ::InitializeCriticalSection(&gLogLock);
      gLogLockReady = true;
      (void)::QueryPerformanceFrequency(&gCounterFrequency);
      (void)::QueryPerformanceCounter(&gStartCounter);
      (void)::CreateDirectoryW(gConfig.outDir.c_str(), nullptr);
      const std::wstring path = gConfig.outDir + L"\\harness.log";
      gLogFile = _wfopen(path.c_str(), L"w");
    }

    // Reads the options; returns an empty string when they are usable, otherwise why not.
    std::string ReadOptions(int argc, wchar_t** argv)
    {
      std::string problem;
      bool exitGiven = false;
      gConfig.frames = {60, 300, 900};
      gConfig.seed = 0x6D366100u; // "m6a\0"
      for (int index = 1; index < argc; ++index) {
        const wchar_t* const option = argv[index];
        const wchar_t* const value = index + 1 < argc ? argv[index + 1] : nullptr;
        if (OptionIs(option, L"galframes") && value != nullptr) {
          if (!ParseFrames(value, &gConfig.frames)) {
            problem = "bad /galframes list: " + Utf8(value);
          }
        } else if (OptionIs(option, L"galexitframe") && value != nullptr) {
          exitGiven = true;
          gConfig.exitFrame = static_cast<unsigned>(std::wcstoul(value, nullptr, 10));
        } else if (OptionIs(option, L"galpace") && value != nullptr) {
          gConfig.paceFps = std::wcstod(value, nullptr);
        } else if (OptionIs(option, L"galnopins")) {
          gConfig.noPins = true;
        } else if (OptionIs(option, L"galseed") && value != nullptr) {
          gConfig.seed = static_cast<std::uint32_t>(std::wcstoul(value, nullptr, 0));
        } else if (OptionIs(option, L"framerate") && value != nullptr) {
          // CScApp.cpp:297-315 (TryReadFixedFrameDeltaSeconds) computes the same value.
          const float rate = std::wcstof(value, nullptr);
          gConfig.fixedFrameSeconds = rate > 0.0f ? 1.0f / rate : 0.0f;
        }
      }
      if (!exitGiven) {
        gConfig.exitFrame = gConfig.frames.empty() ? 0u : gConfig.frames.back();
      }
      if (problem.empty() && gConfig.exitFrame == 0) {
        problem = "no exit frame: give /galframes or /galexitframe";
      }
      if (problem.empty() && !gConfig.frames.empty() && gConfig.frames.back() > gConfig.exitFrame) {
        problem = "a capture frame lies after /galexitframe";
      }
      if (problem.empty() && !(gConfig.fixedFrameSeconds > 0.0f)) {
        problem = "the harness needs a fixed frame delta: add /framerate <fps> (CScApp.cpp:297-315)";
      }
      if (problem.empty() && gConfig.paceFps < 0.0) {
        problem = "/galpace must be >= 0";
      }
      return problem;
    }

    void Bootstrap()
    {
      int argc = 0;
      wchar_t** const argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
      if (argv == nullptr) {
        return;
      }
      for (int index = 1; index + 1 < argc; ++index) {
        if (OptionIs(argv[index], L"galharness")) {
          gConfig.active = true;
          gConfig.outDir = argv[index + 1];
          while (!gConfig.outDir.empty() && (gConfig.outDir.back() == L'\\' || gConfig.outDir.back() == L'/')) {
            gConfig.outDir.pop_back();
          }
          break;
        }
      }
      if (!gConfig.active) {
        ::LocalFree(argv);
        return;
      }

      gConfig.commandLine = Utf8(::GetCommandLineW());
      OpenLog();
      Log("frame harness: %s", gConfig.commandLine.c_str());

      // No error UI from this process: no WER dialog, no critical-error boxes, and debug-CRT
      // reports go to the debugger and stderr instead of an Abort/Retry/Ignore window.
      (void)::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
      for (const int kind : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        (void)_CrtSetReportMode(kind, _CRTDBG_MODE_DEBUG | _CRTDBG_MODE_FILE);
        (void)_CrtSetReportFile(kind, _CRTDBG_FILE_STDERR);
      }
      (void)_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

      // The committed render-path probes arm themselves from FAF_* variables (FAF_TOGGLE_DIR,
      // FAF_DUMP_FRAMES, ...); a reference run must not have any of them.
      const std::string fafVariables = FafEnvironmentVariables();
      std::string problem = ReadOptions(argc, argv);
      ::LocalFree(argv);
      if (problem.empty() && !fafVariables.empty()) {
        problem = "FAF_* environment variables are set (" + fafVariables + "); the harness refuses to run with them";
      }
      if (!problem.empty()) {
        Abort(kExitRefused, "%s", problem.c_str());
      }

      // Below-normal CPU priority (the launcher asks for it too) and, where the kernel thunk
      // exists, below-normal GPU scheduling priority, so a game in the foreground wins both.
      (void)::SetPriorityClass(::GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
      if (const HMODULE gdi = ::GetModuleHandleW(L"gdi32.dll"); gdi != nullptr) {
        using SetGpuPriorityFn = LONG(WINAPI*)(HANDLE, int);
        const auto setGpuPriority =
          reinterpret_cast<SetGpuPriorityFn>(::GetProcAddress(gdi, "D3DKMTSetProcessSchedulingPriorityClass"));
        const LONG status = setGpuPriority != nullptr ? setGpuPriority(::GetCurrentProcess(), 1 /* BELOW_NORMAL */) : -1;
        Log("GPU scheduling priority below normal: status 0x%08lX", static_cast<unsigned long>(status));
      }

      std::string pinError;
      if (gConfig.noPins) {
        Log("/galnopins: the Lua time functions read the wall clock and the random stream keeps its time seed");
      } else if (!InstallPins(&pinError)) {
        Abort(kExitRefused, "%s", pinError.c_str());
      }

      // Thread hooks first: they are thread-local and installed through the real
      // SetWindowsHookExW, before its import entry is redirected.
      gCbtHook = ::SetWindowsHookExW(WH_CBT, &CbtHook, nullptr, ::GetCurrentThreadId());
      gGetMessageHook = ::SetWindowsHookExW(WH_GETMESSAGE, &GetMessageHook, nullptr, ::GetCurrentThreadId());
      if (gCbtHook == nullptr || gGetMessageHook == nullptr) {
        Abort(kExitRefused, "could not install the thread hooks (error %lu)", ::GetLastError());
      }

      (void)::DuplicateHandle(::GetCurrentProcess(), ::GetCurrentThread(), ::GetCurrentProcess(), &gUiThread,
                              THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
      if (const HANDLE reporter = ::CreateThread(nullptr, 0, &StallReporter, nullptr, 0, nullptr); reporter != nullptr) {
        (void)::CloseHandle(reporter);
      }

      const HMODULE self = ::GetModuleHandleW(nullptr);
      for (const ImportHook& hook : kImportHooks) {
        const int patched = PatchImport(self, hook);
        gImportHookResults.push_back(Format("%s!%s:%d", hook.dll, hook.name, patched));
      }
      std::string patchedList;
      for (const std::string& result : gImportHookResults) {
        patchedList += result + " ";
      }
      Log("import redirects (entries patched): %s", patchedList.c_str());
      Log(
        "frames: %u captures, exit after frame %u, pace %.1f fps, fixed delta %.6f s, seed 0x%08X",
        static_cast<unsigned>(gConfig.frames.size()), gConfig.exitFrame, gConfig.paceFps,
        static_cast<double>(gConfig.fixedFrameSeconds), gConfig.seed
      );
    }

    // Runs during static initialisation. port_graphics.props appends port\graphics sources after
    // the engine's ClCompile items, so this initialiser runs after every engine one: the Lua
    // binders InstallPins rewrites and the random stream it reseeds exist by now.
    struct HarnessBootstrap
    {
      HarnessBootstrap()
      {
        Bootstrap();
      }
    };
    HarnessBootstrap gHarnessBootstrap;
  } // namespace

  const HarnessConfig& Config() noexcept
  {
    return gConfig;
  }

  void Log(const char* format, ...)
  {
    if (!gLogLockReady) {
      return;
    }
    char message[2048];
    va_list args;
    va_start(args, format);
    (void)std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    LARGE_INTEGER now{};
    (void)::QueryPerformanceCounter(&now);
    const double seconds = gCounterFrequency.QuadPart != 0
      ? static_cast<double>(now.QuadPart - gStartCounter.QuadPart) / static_cast<double>(gCounterFrequency.QuadPart)
      : 0.0;

    ::EnterCriticalSection(&gLogLock);
    if (gLogFile != nullptr) {
      std::fprintf(gLogFile, "[%9.3f] %s\n", seconds, message);
      std::fflush(gLogFile);
    }
    ::LeaveCriticalSection(&gLogLock);
  }

  std::string SandboxReportJson()
  {
    std::lock_guard<std::mutex> lock(gReportLock);
    std::string json = "{\"intercepted\": {";
    bool first = true;
    for (const auto& [api, record] : gIntercepts) {
      json += first ? "" : ", ";
      first = false;
      json += JsonString(api) + ": {\"count\": " + std::to_string(record.count) + ", \"samples\": [";
      for (std::size_t index = 0; index < record.samples.size(); ++index) {
        json += (index != 0 ? ", " : "") + JsonString(record.samples[index]);
      }
      json += "]}";
    }
    json += "}, \"activations_blocked\": " + std::to_string(gBlockedActivations.load());
    json += ", \"input_messages_dropped\": " + std::to_string(gDroppedInput.load());
    json += ", \"top_level_windows_created\": [";
    for (std::size_t index = 0; index < gCreatedWindows.size(); ++index) {
      json += (index != 0 ? ", " : "") + JsonString(gCreatedWindows[index]);
    }
    json += "], \"violations\": [";
    for (std::size_t index = 0; index < gViolations.size(); ++index) {
      json += (index != 0 ? ", " : "") + JsonString(gViolations[index]);
    }
    json += "], \"import_redirects\": [";
    for (std::size_t index = 0; index < gImportHookResults.size(); ++index) {
      json += (index != 0 ? ", " : "") + JsonString(gImportHookResults[index]);
    }
    json += "]}";
    return json;
  }

  bool WindowsHiddenAndNotForeground(std::string* violation)
  {
    MonitorScan scan{};
    scan.processId = ::GetCurrentProcessId();
    (void)::EnumWindows(&CollectVisibleWindows, reinterpret_cast<LPARAM>(&scan));

    std::string found;
    for (const HWND window : scan.visible) {
      found += "visible: " + DescribeWindow(window) + "; ";
      (void)gShowWindow(window, SW_HIDE);
    }
    const HWND foreground = ::GetForegroundWindow();
    DWORD foregroundOwner = 0;
    if (foreground != nullptr) {
      (void)::GetWindowThreadProcessId(foreground, &foregroundOwner);
    }
    if (foregroundOwner == scan.processId) {
      found += "foreground: " + DescribeWindow(foreground) + "; ";
    }
    if (found.empty()) {
      return true;
    }
    {
      std::lock_guard<std::mutex> lock(gReportLock);
      gViolations.push_back(found);
    }
    if (violation != nullptr) {
      *violation = found;
    }
    return false;
  }

  std::string JsonString(const std::string& text)
  {
    std::string out = "\"";
    for (const char raw : text) {
      const auto c = static_cast<unsigned char>(raw);
      switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
          if (c < 0x20) {
            char escaped[8];
            (void)std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
            out += escaped;
          } else {
            out += static_cast<char>(c);
          }
      }
    }
    out += "\"";
    return out;
  }

  std::string Utf8(const std::wstring& text)
  {
    if (text.empty()) {
      return {};
    }
    const int size = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size > 0 ? size : 0), '\0');
    if (size > 0) {
      (void)::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    }
    return out;
  }
} // namespace port::graphics::capture::detail

namespace port::graphics::capture
{
  bool HarnessActive() noexcept
  {
    return detail::Config().active;
  }
} // namespace port::graphics::capture
