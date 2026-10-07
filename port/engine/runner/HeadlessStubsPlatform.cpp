// The Windows app layer (moho/app/WinApp.cpp, CWaitHandleSet.cpp), the Lua debugger window
// (moho/misc/ScrDebugHooks.cpp), the directory watcher (moho/misc/CDiskWatch.cpp) and the save-game
// writer (moho/misc/CSaveGameRequestImpl.cpp) for the Android headless runner; see HeadlessStubs.h.
//
// The runner state these follow (moho/app/HeadlessReplay.cpp, Run; WinMain.cpp:682 branches to it
// before anything else): WIN_AppExecute never runs, so neither does PLAT_Init (WinApp.cpp:2760, the
// dbghelp symbol handler) nor the frame loop with its task stages, wait-handle set and
// DISK_UpdateWatcher pump (CScApp.cpp:1051); no Lua debug window is opened; no save is requested.

#include "HeadlessStubs.h"

#include <cstdio>
#include <string>
#include <utility>

#include <windows.h>

#include "gpg/core/containers/String.h"
#include "gpg/core/utils/Logging.h"
#include "moho/app/CWaitHandleSet.h"
#include "moho/app/WinApp.h"
#include "moho/misc/CDiskWatch.h"
#include "moho/misc/CSaveGameRequestImpl.h"
#include "moho/misc/ScrDebugHooks.h"

namespace moho
{
  // ---------------------------------------------------------------------------------------------
  // WinApp.cpp

  // WinApp.cpp:3003: returns 0 while `sSymbolHandlerInitialized` is false, and only PLAT_Init sets
  // it, which the runner never calls. Reached whenever an exception type that records its call
  // stack is built (XException.cpp, CVarAccess.cpp).
  std::uint32_t PLAT_GetCallStack(void* const contextRecord, const std::uint32_t maxFrames, std::uint32_t* const outFrames)
  {
    (void)contextRecord;
    (void)maxFrames;
    (void)outFrames;
    FAF_RUNNER_STUB("PLAT_GetCallStack");
    return 0;
  }

  // WinApp.cpp:3192: with no symbol handler PLAT_GetSymbolInfo (WinApp.cpp:3137) fails for every
  // frame, so each one is printed as an unknown address; this is that branch.
  msvc8::string PLAT_FormatCallstack(std::int32_t firstFrame, const std::int32_t endFrame, const std::uint32_t* const frames)
  {
    FAF_RUNNER_STUB("PLAT_FormatCallstack");
    msvc8::string formatted;
    formatted.assign_owned("");
    if (frames == nullptr || firstFrame >= endFrame) {
      return formatted;
    }
    if (firstFrame < 0) {
      firstFrame = 0;
    }

    std::string assembled;
    for (std::int32_t frameIndex = firstFrame; frameIndex < endFrame; ++frameIndex) {
      const msvc8::string line = gpg::STR_Printf("\tUnknown symbol (address 0x%08x)\r\n", frames[frameIndex]);
      assembled.append(line.c_str());
    }

    formatted.assign_owned(assembled);
    return formatted;
  }

  // WinApp.cpp:3440, the same body; the shim's FormatMessageW knows the error codes the shim sets
  // (port/engine/shim/faf_win_file.h). Only error messages use the text.
  msvc8::string WIN_GetLastError()
  {
    FAF_RUNNER_STUB("WIN_GetLastError");
    const DWORD errorCode = ::GetLastError();

    LPWSTR messageBuffer = nullptr;
    const DWORD formatResult = ::FormatMessageW(
      FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER,
      nullptr,
      errorCode,
      0x400u,
      reinterpret_cast<LPWSTR>(&messageBuffer),
      0,
      nullptr
    );

    if (formatResult == 0 || messageBuffer == nullptr) {
      return gpg::STR_Printf("Unknown error 0x%08x", errorCode);
    }

    const msvc8::string message = gpg::STR_WideToUtf8(messageBuffer);
    (void)::LocalFree(messageBuffer);
    return message;
  }

  // WinApp.cpp:2608 and :2621, the same bodies: process-wide stages the frame loop runs. In the
  // runner nothing runs them; their only closure caller is CDiscoveryService.cpp (LAN game
  // discovery, created only by the lobby's Lua), which the runner never starts.
  CTaskStage& WIN_GetBeforeEventsStage()
  {
    FAF_RUNNER_STUB("WIN_GetBeforeEventsStage");
    static CTaskStage sBeforeEventsStage;
    return sBeforeEventsStage;
  }

  CTaskStage& WIN_GetBeforeWaitStage()
  {
    FAF_RUNNER_STUB("WIN_GetBeforeWaitStage");
    static CTaskStage sBeforeWaitStage;
    return sBeforeWaitStage;
  }

  // WinApp.cpp:2634: the frame loop's wait-handle set. Its only closure caller is
  // CDiscoveryService.cpp (see above); CWaitHandleSet.cpp cannot be linked.
  CWaitHandleSet* WIN_GetWaitHandleSet()
  {
    FAF_RUNNER_STUB("WIN_GetWaitHandleSet");
    return nullptr;
  }

  // WinApp.cpp:2660: shortens the frame loop's idle wait; the runner has no frame loop.
  void WIN_SetWakeupTimer(const float milliseconds)
  {
    (void)milliseconds;
    FAF_RUNNER_STUB("WIN_SetWakeupTimer");
  }

  // WinApp.cpp:3408 and :2538: the Windows clipboard, for the console's copy/paste commands
  // (CConCommand.cpp); the runner executes no console command.
  msvc8::string WIN_GetClipboardText()
  {
    FAF_RUNNER_STUB("WIN_GetClipboardText");
    return {};
  }

  bool WIN_CopyToClipboard(const wchar_t* const text)
  {
    (void)text;
    FAF_RUNNER_STUB("WIN_CopyToClipboard");
    return false;
  }

  // WinApp.cpp:3562: a modal message box. Callers are the /perftest result (StatItem.cpp) and
  // StartCommandLineSession (StartupHelpers.cpp), neither of which the runner reaches; the text goes
  // to the log instead, since nobody could click a box here.
  void WIN_OkBox(const gpg::StrArg caption, const gpg::StrArg text)
  {
    FAF_RUNNER_STUB("WIN_OkBox");
    gpg::Warnf("%s: %s", caption != nullptr ? caption : "", text != nullptr ? text : "");
  }

  // WinApp.cpp:2908 and :2921: record crash-report attachments, read only by the crash reporter's
  // dialog. USER_LoadPreferences (StartupHelpers.cpp) calls them when the runner gets /prefs.
  void PLAT_InitErrorReportOutputDir(const wchar_t* const outputDir)
  {
    (void)outputDir;
    FAF_RUNNER_STUB("PLAT_InitErrorReportOutputDir");
  }

  void PLAT_RegisterFileForErrorReport(const wchar_t* const file)
  {
    (void)file;
    FAF_RUNNER_STUB("PLAT_RegisterFileForErrorReport");
  }

  // CWaitHandleSet.cpp: members of the set WIN_GetWaitHandleSet() returns; with no set there is
  // nothing to call them on (caller: CDiscoveryService.cpp, see above).
  void CWaitHandleSet::AddHandle(HANDLE handle)
  {
    (void)handle;
    FAF_RUNNER_STUB("CWaitHandleSet::AddHandle");
  }

  void CWaitHandleSet::RemoveHandle(HANDLE handle)
  {
    (void)handle;
    FAF_RUNNER_STUB("CWaitHandleSet::RemoveHandle");
  }

  // ---------------------------------------------------------------------------------------------
  // ScrDebugHooks.cpp, with no debug window: `debugWindow` is set only by SCR_CreateDebugWindow.

  // ScrDebugHooks.cpp:859.
  bool SCR_IsDebugWindowActive()
  {
    FAF_RUNNER_STUB("SCR_IsDebugWindowActive");
    return false;
  }

  // ScrDebugHooks.cpp:735: returns at once while no debug window is active (ScrDebugHooks.cpp:748);
  // it is only installed as a hook when one is.
  void DebugLuaHook(lua_State* const state, lua_Debug* const debugFrame)
  {
    (void)state;
    (void)debugFrame;
    FAF_RUNNER_STUB("DebugLuaHook");
  }

  // ScrDebugHooks.cpp:340: adds the state to the debugger's list of hookable states and, only while
  // a debug window is active, sets the line hook. The list is read only by the debug window, so with
  // none the state runs exactly as without the call. Reached in every run (RRuleGameRules.cpp).
  void SCR_HookState(LuaPlus::LuaState* const state)
  {
    (void)state;
    FAF_RUNNER_STUB("SCR_HookState");
  }

  // ScrDebugHooks.cpp:807: opens the wx Lua debugger; only a console command calls it.
  void SCR_CreateDebugWindow()
  {
    FAF_RUNNER_STUB("SCR_CreateDebugWindow");
  }

  // ---------------------------------------------------------------------------------------------
  // CDiskWatch.cpp. Directory change events reach listeners only through DISK_UpdateWatcher
  // (CDiskWatch.cpp:777), and only the frame loop calls it (CScApp.cpp:1051). So in the runner no
  // listener ever receives an event: AnyChangesPending stays false and the copies stay empty, whether
  // or not a watch object exists. The listener members keep the real bodies; the watch itself (a
  // Win32 ReadDirectoryChangesW thread per directory) is the part that is left out.

  // CDiskWatch.cpp:455. The real constructor also links the listener into the watch through
  // DISK_AddWatchListener (below), which changes no observable state in the runner.
  CDiskWatchListener::CDiskWatchListener(const gpg::StrArg patterns)
    : mWatch(nullptr)
    , mEvents()
    , mPatterns()
  {
    FAF_RUNNER_STUB("CDiskWatchListener::CDiskWatchListener");
    if (patterns && patterns[0] != '\0') {
      mPatterns.push_back(msvc8::string(patterns));
      DISK_AddWatchListener(this);
    }
  }

  // CDiskWatch.cpp:469; `mWatch` stays null here (DISK_AddWatchListener below never sets it).
  CDiskWatchListener::~CDiskWatchListener()
  {
    FAF_RUNNER_STUB("CDiskWatchListener::~CDiskWatchListener");
    mPatterns.clear();
    mEvents.clear();
  }

  // CDiskWatch.cpp:481, :491, :509: the event path; with no watch nothing delivers an event.
  void CDiskWatchListener::OnEvent(const SDiskWatchEvent& event)
  {
    FAF_RUNNER_STUB("CDiskWatchListener::OnEvent");
    if (FilterEvent(event)) {
      OnDiskWatchEvent(event);
    }
  }

  bool CDiskWatchListener::FilterEvent(const SDiskWatchEvent& event)
  {
    (void)event;
    FAF_RUNNER_STUB("CDiskWatchListener::FilterEvent");
    return false;
  }

  void CDiskWatchListener::OnDiskWatchEvent(const SDiskWatchEvent& event)
  {
    FAF_RUNNER_STUB("CDiskWatchListener::OnDiskWatchEvent");
    mEvents.push_back(event);
  }

  // CDiskWatch.cpp:517 and :533, the `mWatch == nullptr` branches (the lock only guards the same
  // vector on Windows).
  bool CDiskWatchListener::AnyChangesPending()
  {
    FAF_RUNNER_STUB("CDiskWatchListener::AnyChangesPending");
    return !mEvents.empty();
  }

  void CDiskWatchListener::CopyAndClearPendingChanges(msvc8::vector<SDiskWatchEvent>& outEvents)
  {
    FAF_RUNNER_STUB("CDiskWatchListener::CopyAndClearPendingChanges");
    outEvents = msvc8::vector<SDiskWatchEvent>{};
    std::swap(outEvents, mEvents);
  }

  // CDiskWatch.cpp:751: links the listener into the process watch, whose events are never pumped in
  // the runner (see above). Reached in every run (RRuleGameRules, ResourceManager and the user Lua
  // state's ScrDiskWatcherTask construct listeners).
  void DISK_AddWatchListener(CDiskWatchListener* const listener)
  {
    (void)listener;
    FAF_RUNNER_STUB("DISK_AddWatchListener");
  }

  // CDiskWatch.cpp:725: starts watching a mounted directory. CVFSImpl.cpp:643 calls it only with
  // /EnableDiskWatch and ignores the result; the watch would deliver nothing in the runner anyway.
  bool DISK_AddWatchDirectory(const gpg::StrArg directoryPath)
  {
    (void)directoryPath;
    FAF_RUNNER_STUB("DISK_AddWatchDirectory");
    return false;
  }

  // ---------------------------------------------------------------------------------------------
  // CSaveGameRequestImpl.cpp: the save-game writer. A request exists only after the user Lua's save
  // binder (SessionStartup.cpp) asked for a save; a replay run never does, so SimDriver.cpp never
  // has a request to call these on.

  // CSaveGameRequestImpl.cpp:290, the same body (the deleter of the request's FILE).
  void SFileStarCloser::operator()(std::FILE* const file) const noexcept
  {
    FAF_RUNNER_STUB("SFileStarCloser::operator()");
    if (file) {
      std::fclose(file);
    }
  }

  // CSaveGameRequestImpl.cpp:34, the same (defaulted) base constructor.
  ISaveRequest::ISaveRequest() = default;

  // CSaveGameRequestImpl.cpp:306 (which also opens the temporary save file).
  CSaveGameRequestImpl::CSaveGameRequestImpl(
    const gpg::StrArg savePath, const gpg::StrArg sessionName, const LuaPlus::LuaObject& completionCallback
  )
    : mSavePath(savePath != nullptr ? savePath : "")
    , mSessionName(sessionName != nullptr ? sessionName : "")
    , mCompletionCallback(completionCallback)
    , mFile()
    , mArchive(nullptr)
  {
    FAF_RUNNER_STUB("CSaveGameRequestImpl::CSaveGameRequestImpl");
  }

  // CSaveGameRequestImpl.cpp:360, the same body.
  gpg::WriteArchive* CSaveGameRequestImpl::GetArchive()
  {
    FAF_RUNNER_STUB("CSaveGameRequestImpl::GetArchive");
    return mArchive;
  }

  // CSaveGameRequestImpl.cpp:372 (writes and renames the save file).
  void CSaveGameRequestImpl::Save(const SSaveGameDispatchData& data)
  {
    (void)data;
    FAF_RUNNER_STUB("CSaveGameRequestImpl::Save");
  }
} // namespace moho
