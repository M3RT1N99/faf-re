// The UI (moho/ui/UiRuntimeTypes.cpp, IUIManager.cpp, CUIManager.cpp) and the wx app layer
// (moho/app/WxRuntimeTypes.cpp, WxUrl.cpp) for the Android headless runner; see HeadlessStubs.h.
//
// The runner state these follow (moho/app/HeadlessReplay.cpp, Run): UI_Init and CreateDevice never
// run, so `g_UIManager` (IUIManager.cpp:18) stays null and no UI Lua state, frame, control or
// viewport exists; `sUIState` keeps UIS_none; no console command is executed.

#include "HeadlessStubs.h"

#include <new>

class wxWindow; // IUIManager.h names it in a signature; the type itself is never needed here.

#include "lua/LuaObject.h"
#include "moho/app/FrameDumpCommands.h"
#include "moho/app/WxUrl.h"
#include "moho/lua/CScrLuaInitForm.h"
#include "moho/misc/ScrDebugHooks.h"
#include "moho/sim/BuildQueueCommandDecrement.h"
#include "moho/task/CTaskThread.h"
#include "moho/task/ScrDiskWatcherTask.h"
#include "moho/ui/CUIManager.h"
#include "moho/ui/IUIManager.h"
#include "moho/ui/UiRuntimeTypes.h"

namespace moho
{
  namespace
  {
    // UiRuntimeTypes.cpp:643 (its own `gUserLuaState`); only USER_GetLuaState below writes it here.
    LuaPlus::LuaState* gRunnerUserLuaState = nullptr;

    // UiRuntimeTypes.cpp:645, same body.
    void AttachTaskToStage(CTask* const task, CTaskStage* const stage, const bool owning)
    {
      if (task == nullptr || stage == nullptr || task->mOwnerThread != nullptr) {
        return;
      }

      CTaskThread* const thread = new CTaskThread(stage);
      if (thread == nullptr) {
        return;
      }

      task->mAutoDelete = owning;
      task->mOwnerThread = thread;
      task->mSubtask = thread->mTaskTop;
      thread->mTaskTop = task;
    }

    // UiRuntimeTypes.cpp:666, same body.
    void RunLuaInitFormSetIfPresent(const char* const setName, LuaPlus::LuaState* const state)
    {
      CScrLuaInitFormSet* const initSet = SCR_FindLuaInitFormSet(setName);
      if (initSet == nullptr) {
        return;
      }

      initSet->mRegistered = 1;
      for (CScrLuaInitForm* form = initSet->mForms; form != nullptr; form = form->mNextInSet) {
        form->Run(state);
      }
    }
  } // namespace

  // ---------------------------------------------------------------------------------------------
  // The user Lua state. Not a null stub: the Windows runner reaches it (VCR_SetupReplaySession's
  // Loc() of the local client name, SessionStartup.cpp; the M3a T1 log shows its "Error localizing
  // "<LOC Engine0031>Local"" line), and there it creates the state. This is UiRuntimeTypes.cpp:25886
  // with the same body, so the runner gets the same state: base library, the Core and User init
  // sets (of the User set, Android has the binders of the linked TUs; the closure report lists the
  // user-side registration TUs it leaves out), a ScrDiskWatcherTask on the user stage, no script.
  // SCR_IsDebugWindowActive() is false in the runner (HeadlessStubsPlatform.cpp), so no hook.
  LuaPlus::LuaState* USER_GetLuaState()
  {
    FAF_RUNNER_STUB("USER_GetLuaState");
    LuaPlus::LuaState* state = gRunnerUserLuaState;
    if (state != nullptr) {
      return state;
    }

    static LuaPlus::LuaState sLuaState(LuaPlus::LuaState::LIB_BASE);
    static CTaskStage sStage;

    sUserStage = &sStage;
    gRunnerUserLuaState = &sLuaState;

    if (SCR_IsDebugWindowActive()) {
      lua_sethook(gRunnerUserLuaState->m_state, &DebugLuaHook, 4, 0);
    }

    gRunnerUserLuaState->m_luaTask = reinterpret_cast<CLuaTask*>(sUserStage);

    ScrDiskWatcherTask* const diskWatcherTask = new (std::nothrow) ScrDiskWatcherTask(gRunnerUserLuaState);
    AttachTaskToStage(diskWatcherTask, sUserStage, true);

    RunLuaInitFormSetIfPresent("Core", gRunnerUserLuaState);
    RunLuaInitFormSetIfPresent("User", gRunnerUserLuaState);
    return gRunnerUserLuaState;
  }

  // ---------------------------------------------------------------------------------------------
  // UiRuntimeTypes.cpp functions. All of them serve the game UI (a UI Lua state, frames, key
  // bindings, the build queue panel); the runner has none of it and runs no console command.

  // UiRuntimeTypes.cpp:26019: calls /lua/ui/uimain.lua on the UI manager's state. Its only caller is
  // UI_DriverNoteGameSpeedChanged (IClientMgrUIInterface.cpp), which needs WLD_GetDriver() (null)
  // and is bound only by CWldUiInterface, which the runner never installs.
  void UI_NoteGameSpeedChanged(const std::int32_t slotZeroBased, const std::int32_t gameSpeed)
  {
    (void)slotZeroBased;
    (void)gameSpeed;
    FAF_RUNNER_STUB("UI_NoteGameSpeedChanged");
  }

  // UiRuntimeTypes.cpp:27061: reads the build queue panel (`sCurrentBuildQueue`), which is filled
  // only by the UI; for an empty queue it yields two null pointers, as here. Callers: user binders in
  // Sim.cpp.
  void CurrentBuildQueueItemCommands(
    const int oneBasedQueueIndex, const CmdId** const outBegin, const CmdId** const outEnd
  ) noexcept
  {
    (void)oneBasedQueueIndex;
    FAF_RUNNER_STUB("CurrentBuildQueueItemCommands");
    *outBegin = nullptr;
    *outEnd = nullptr;
  }

  // UiRuntimeTypes.cpp:9168: stores the camera scrub direction for the UI's mouse handling, which the
  // runner never runs. Caller: a user binder in Sim.cpp.
  void UI_SetInvertMidMouseScrub(const bool invert) noexcept
  {
    (void)invert;
    FAF_RUNNER_STUB("UI_SetInvertMidMouseScrub");
  }

  // UiRuntimeTypes.cpp:28151 and its neighbours: the IN_* key-binding console commands
  // (CConCommand.cpp registers them). Console commands only run when typed or scripted; the runner
  // executes none.
  void IN_BindKey(const msvc8::vector<msvc8::string>& args)
  {
    (void)args;
    FAF_RUNNER_STUB("IN_BindKey");
  }

  void IN_DumpKeyBindings(const msvc8::vector<msvc8::string>& args)
  {
    (void)args;
    FAF_RUNNER_STUB("IN_DumpKeyBindings");
  }

  void IN_SetKeyName(const msvc8::vector<msvc8::string>& args)
  {
    (void)args;
    FAF_RUNNER_STUB("IN_SetKeyName");
  }

  void IN_DumpKeyNames(const msvc8::vector<msvc8::string>& args)
  {
    (void)args;
    FAF_RUNNER_STUB("IN_DumpKeyNames");
  }

  // UiRuntimeTypes.cpp:22567: walks a UI control tree; no CMauiControl exists in the runner (the
  // caller is CConCommand.cpp's UI_DumpControlsUnderCursor console command).
  CMauiControl* CMauiControl::DepthFirstSuccessor(CMauiControl* const subtreeRoot)
  {
    (void)subtreeRoot;
    FAF_RUNNER_STUB("CMauiControl::DepthFirstSuccessor");
    return nullptr;
  }

  // UiRuntimeTypes.cpp:3369-3391: the UI's console variables and state, with their initial values.
  // Only the UI reads or changes them (UiRuntimeTypes.cpp:18637-18643 is the options dialog).
  EUIState sUIState = UIS_none;
  bool cam_Free = false;
  float ren_BgLowerBound = 125.0f;
  bool ui_DisableCursorFixing = false;
  float ui_SelectTolerance = 4.0f;
  float ui_ExtractSnapTolerance = 20.0f;
  float ui_MinExtractSnapPixels = 20.0f;
  float ui_MaxExtractSnapPixels = 90.0f;
  float ui_FootprintMinThickness = 2.0f;
  float cam_DefaultMiniLOD = 1.8f;
  bool ui_WindowedAlwaysShowsCursor = false;
  bool ui_DragSelect2D = true;
  bool ui_AttackGroundIgnoresFireState = false;
  float ui_KeyboardPanSpeed = 90.0f;
  float ui_KeyboardPanAccelerateMultiplier = 4.0f;
  float ui_KeyboardRotateSpeed = 10.0f;
  float ui_KeyboardRotateAccelerateMultiplier = 2.0f;
  bool ui_ScreenEdgeScrollView = true;
  bool ui_ArrowKeysScrollView = true;

  // ---------------------------------------------------------------------------------------------
  // IUIManager.cpp, with `g_UIManager` null.

  // IUIManager.cpp:18.
  CUIManager* g_UIManager = nullptr;

  // IUIManager.cpp:179.
  IUIManager* UI_GetManager()
  {
    FAF_RUNNER_STUB("UI_GetManager");
    return nullptr;
  }

  // IUIManager.cpp:242 and :256 via StartUIMainEntry (IUIManager.cpp:117): USER_GetLuaState(), then
  // `false` because UI_GetManager() is null, before sUIState or any script is touched.
  bool UI_StartFrontEnd()
  {
    FAF_RUNNER_STUB("UI_StartFrontEnd");
    (void)USER_GetLuaState();
    return false;
  }

  bool UI_StartSplashScreens()
  {
    FAF_RUNNER_STUB("UI_StartSplashScreens");
    (void)USER_GetLuaState();
    return false;
  }

  // IUIManager.cpp:428: `true` when the UI manager has no Lua state.
  bool ShowEscapeDialog(const bool showDialog)
  {
    (void)showDialog;
    FAF_RUNNER_STUB("ShowEscapeDialog");
    return true;
  }

  // IUIManager.cpp:213/233: defaulted in the real TU too.
  UICommandModeData::UICommandModeData() = default;
  UICommandModeData::~UICommandModeData() = default;

  // IUIManager.cpp:493: clears the output, then returns because the UI manager has no Lua state.
  void UI_GetCommandMode(UICommandModeData& outCommandModeData)
  {
    FAF_RUNNER_STUB("UI_GetCommandMode");
    outCommandModeData.mMode.clear();
    outCommandModeData.mPayload = LuaPlus::LuaObject{};
  }

  // IUIManager.cpp:537 and :567: return at once without a UI manager Lua state.
  void UI_StartCommandMode(const UICommandModeData& commandModeData)
  {
    (void)commandModeData;
    FAF_RUNNER_STUB("UI_StartCommandMode");
  }

  void UI_EndCommandMode()
  {
    FAF_RUNNER_STUB("UI_EndCommandMode");
  }

  // IUIManager.cpp:326: returns at once without a UI manager Lua state; bound only by CWldUiInterface
  // (CWldUiInterface::ReceiveChat, IClientMgrUIInterface.cpp), which the runner never installs.
  void func_ReceiveChat(const msvc8::string& senderName, const gpg::MemBuffer<const char>& data)
  {
    (void)senderName;
    (void)data;
    FAF_RUNNER_STUB("func_ReceiveChat");
  }

  // CUIManager.cpp: both need the CUIManager instance, which does not exist in the runner (their
  // callers in Sim.cpp reach them through g_UIManager).
  bool CUIManager::HasFrames() const
  {
    FAF_RUNNER_STUB("CUIManager::HasFrames");
    return false;
  }

  void CUIManager::ClearChildren(const int frameIdx)
  {
    (void)frameIdx;
    FAF_RUNNER_STUB("CUIManager::ClearChildren");
  }

  // ---------------------------------------------------------------------------------------------
  // UiRuntimeTypes.cpp's factory build-queue display (the UI mirror of the selected factory's queue).
  // Its users are the User-set binders of UserUnit.cpp (linked since M3c), which rebuild it for the
  // construction panel from a selected factory UserUnit; the runner has no session, so no UserUnit
  // and no selection, and its User Lua state never calls them.

  // UiRuntimeTypes.cpp:26498-26499, the same definitions (empty at start; only those binders write).
  FactoryQueueDisplaySnapshot sCurrentBuildQueue{};
  WeakPtr<UserUnit> sCurrentBuildFactory{};

  // UiRuntimeTypes.cpp:26527, :26548, :26565, :26586, the same bodies: the value type of a queue row.
  FactoryQueueDisplayItem::FactoryQueueDisplayItem(const msvc8::string& sourceBlueprintId, const std::int32_t sourceCount)
    : blueprintId(sourceBlueprintId)
    , count(sourceCount)
    , commands()
  {
    FAF_RUNNER_STUB("FactoryQueueDisplayItem::FactoryQueueDisplayItem");
  }

  FactoryQueueDisplayItem::FactoryQueueDisplayItem(const FactoryQueueDisplayItem& other)
    : blueprintId(other.blueprintId)
    , count(other.count)
    , commands(other.commands)
  {
    FAF_RUNNER_STUB("FactoryQueueDisplayItem::FactoryQueueDisplayItem(copy)");
  }

  FactoryQueueDisplayItem& FactoryQueueDisplayItem::operator=(const FactoryQueueDisplayItem& other)
  {
    FAF_RUNNER_STUB("FactoryQueueDisplayItem::operator=");
    blueprintId.assign(other.blueprintId, 0u, msvc8::string::npos);
    count = other.count;
    commands = other.commands;
    return *this;
  }

  FactoryQueueDisplayItem::~FactoryQueueDisplayItem() noexcept = default;

  // UiRuntimeTypes.cpp:26972: compacts sCurrentBuildQueue in place. Its caller is the queue-display
  // binder in UserUnit.cpp (UserUnit.cpp:5903), which the runner never runs (see above); the real body
  // works on the queue's raw storage lanes, so this is a trap rather than a copy.
  FactoryQueueDisplayItem** RebaseFactoryQueueRangeAndTrimTail(
    FactoryQueueDisplayItem** const outBegin, FactoryQueueDisplayItem* const destinationBegin,
    FactoryQueueDisplayItem* const sourceBegin
  )
  {
    (void)outBegin;
    (void)destinationBegin;
    (void)sourceBegin;
    FAF_RUNNER_TRAP("RebaseFactoryQueueRangeAndTrimTail");
  }

  // ---------------------------------------------------------------------------------------------
  // WxRuntimeTypes.cpp and WxUrl.cpp: wx windows and the browser hand-off.

  // WxRuntimeTypes.cpp:2344, a frame-dump console variable; only the renderer's frame dump reads it.
  int dump_outputFrameNumber = 0;

  // WxUrl.cpp: opens a URL in the desktop browser through wx; called by the user-side OpenURL binder
  // (CCommandLuaFunctionRegistrations.cpp), which nothing in the runner runs.
  void OpenAllowedUrlByWx(const std::wstring& url)
  {
    (void)url;
    FAF_RUNNER_STUB("OpenAllowedUrlByWx");
  }
} // namespace moho
