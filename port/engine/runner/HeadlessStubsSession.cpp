// The user session (moho/sim/CWldSession.cpp) for the Android headless runner; see HeadlessStubs.h
// for the rules. (The user-side unit, moho/unit/core/UserUnit.cpp, is linked for real since M3c.)
//
// The runner state these follow (moho/app/HeadlessReplay.cpp, Run): WLD_DoLoading and
// WLD_CreateSession never run, so no CWldSession exists; `gActiveWldSession` (CWldSession.cpp:2910)
// stays null, `sSimDriver` (CWldSession.cpp:2925) stays empty because the runner keeps the driver
// SIM_CreateDriver returns itself (only WLD_DoLoading stores it, CWldSession.cpp:15753), and
// `gWldFrameAction` stays Inactive because nothing calls WLD_BeginSession. No UserEntity or UserUnit
// is ever constructed (they mirror sim entities for a session).

#include "HeadlessStubs.h"

#include "moho/command/CommandIssueHelper.h"
#include "moho/sim/BuildQueueCommandDecrement.h"
#include "moho/sim/CWldSession.h"
#include "moho/unit/core/UserUnit.h"

namespace moho
{
  namespace
  {
    // CWldSession.cpp:2934 `gWldFrameAction`; written only by WLD_BeginSession / WLD_SetFrameAction /
    // CON_WLD_RequestEndSession, all defined below with the real bodies.
    EWldFrameAction gRunnerWldFrameAction = EWldFrameAction::Inactive;

    // CWldSession.cpp:2933 `gPendingWldSessionInfo`: WLD_BeginSession's target, read only by the
    // WLD_Frame machine, which the runner does not run.
    msvc8::auto_ptr<SWldSessionInfo> gRunnerPendingWldSessionInfo;
  } // namespace

  // ---------------------------------------------------------------------------------------------
  // Session accessors: null in the runner, exactly as on Windows.

  // CWldSession.cpp:16567. `gActiveWldSession` is set only by the CWldSession constructor
  // (CWldSession.cpp:9528) and WLD_CreateSession; the runner builds no session.
  CWldSession* WLD_GetActiveSession()
  {
    FAF_RUNNER_STUB("WLD_GetActiveSession");
    return nullptr;
  }

  // CWldSession.cpp:16392: the same global as WLD_GetActiveSession, so null as well.
  CWldSession* WLD_GetSession()
  {
    FAF_RUNNER_STUB("WLD_GetSession");
    return nullptr;
  }

  // CWldSession.cpp:16558: `sSimDriver.get()`. Only WLD_DoLoading stores the driver there
  // (CWldSession.cpp:15753); the runner owns the driver SIM_CreateDriver returns, so this is null on
  // Windows too and its callers (the user-side Unit binders at Unit.cpp:3939/4132) skip their work.
  ISTIDriver* WLD_GetDriver()
  {
    FAF_RUNNER_STUB("WLD_GetDriver");
    return nullptr;
  }

  // CWldSession.cpp:16444: false without an active session, which the runner never has.
  bool WLD_CanAdjustSimRate()
  {
    FAF_RUNNER_STUB("WLD_CanAdjustSimRate");
    return false;
  }

  // CWldSession.cpp:16470/16491/16511: each returns at once while `sSimDriver` is empty.
  void WLD_IncreaseSimRate()
  {
    FAF_RUNNER_STUB("WLD_IncreaseSimRate");
  }

  void WLD_ResetSimRate()
  {
    FAF_RUNNER_STUB("WLD_ResetSimRate");
  }

  void WLD_DecreaseSimRate()
  {
    FAF_RUNNER_STUB("WLD_DecreaseSimRate");
  }

  // CWldSession.cpp:16077: dumps the client manager of `sSimDriver`, which is empty in the runner.
  void CON_WLD_ClientDebugDump(const msvc8::vector<msvc8::string>& args)
  {
    (void)args;
    FAF_RUNNER_STUB("CON_WLD_ClientDebugDump");
  }

  // ---------------------------------------------------------------------------------------------
  // The frame-action state: same bodies as CWldSession.cpp, over this file's copy of the globals.

  // CWldSession.cpp:16022.
  EWldFrameAction WLD_GetFrameAction()
  {
    FAF_RUNNER_STUB("WLD_GetFrameAction");
    return gRunnerWldFrameAction;
  }

  // CWldSession.cpp:16027.
  void WLD_SetFrameAction(const EWldFrameAction action)
  {
    FAF_RUNNER_STUB("WLD_SetFrameAction");
    gRunnerWldFrameAction = action;
  }

  // CWldSession.cpp:16403. The pending info is read only by WLD_Frame, so on both platforms it just
  // waits there until the process ends.
  void WLD_BeginSession(msvc8::auto_ptr<SWldSessionInfo> sessionInfo)
  {
    FAF_RUNNER_STUB("WLD_BeginSession");
    gRunnerPendingWldSessionInfo = sessionInfo;
    gRunnerWldFrameAction = EWldFrameAction::Preload;
  }

  // CWldSession.cpp:16093.
  void CON_WLD_RequestEndSession(const msvc8::vector<msvc8::string>& args)
  {
    (void)args;
    FAF_RUNNER_STUB("CON_WLD_RequestEndSession");
    if (gRunnerWldFrameAction != EWldFrameAction::Inactive) {
      gRunnerWldFrameAction = EWldFrameAction::Exit;
    }
  }

  // CWldSession.cpp:16241 (with WLD_GetOnTeardownCallbacks, CWldSession.cpp:16202): the same body.
  // Reached in every run: the static initialisers of PauseListener.cpp:113, SelectionListener.cpp:192
  // and IdleUnitSelector.cpp:194 register their listeners. Only WLD_CreateSession and WLD_Teardown
  // read the vector, and the runner calls neither.
  WldTeardownCallbackVector* WLD_AddOnTeardownCallback(IWldTeardownCallback* const callback)
  {
    FAF_RUNNER_STUB("WLD_AddOnTeardownCallback");
    static WldTeardownCallbackVector sCallbacks;
    sCallbacks.push_back(callback);
    return &sCallbacks;
  }

  // ---------------------------------------------------------------------------------------------
  // CWldSession members. Each needs a CWldSession, and the runner never constructs one, so nothing
  // reaches them (on Windows a call through the null session would fault).

  // CWldSession.cpp:12950.
  UserEntity* CWldSession::LookupEntityId(const EntId entityId)
  {
    (void)entityId;
    FAF_RUNNER_STUB("CWldSession::LookupEntityId");
    return nullptr;
  }

  // CWldSession.cpp:9618.
  UserArmy* CWldSession::GetFocusArmy() const
  {
    FAF_RUNNER_STUB("CWldSession::GetFocusArmy");
    return nullptr;
  }

  // CWldSession.cpp:10019.
  UserArmy* CWldSession::GetFocusUserArmy()
  {
    FAF_RUNNER_STUB("CWldSession::GetFocusUserArmy");
    return nullptr;
  }

  // CWldSession.cpp:10130.
  SpatialDB<UserEntity>* CWldSession::GetEntitySpatialDbStorage()
  {
    FAF_RUNNER_STUB("CWldSession::GetEntitySpatialDbStorage");
    return nullptr;
  }

  // CWldSession.cpp:10318.
  void CWldSession::AddToVizUpdate(UserEntity* const entity)
  {
    (void)entity;
    FAF_RUNNER_STUB("CWldSession::AddToVizUpdate");
  }

  // CWldSession.cpp:10335.
  void CWldSession::RemoveFromVizUpdate(UserEntity* const entity)
  {
    (void)entity;
    FAF_RUNNER_STUB("CWldSession::RemoveFromVizUpdate");
  }

  // CWldSession.cpp:10106.
  EntityCategoryLookupResolver* CWldSession::GetCategoryLookupResolver()
  {
    FAF_RUNNER_STUB("CWldSession::GetCategoryLookupResolver");
    return nullptr;
  }

  // CWldSession.cpp:9599.
  bool CWldSession::TryGetPlayableMapRect(VisibilityRect& outRect) const
  {
    (void)outRect;
    FAF_RUNNER_STUB("CWldSession::TryGetPlayableMapRect");
    return false;
  }

  // CWldSession.cpp:11254 (the WeakSet overload). Its one closure caller, ANI_DumpSkeleton
  // (CAniSkel.cpp:996), calls it on WLD_GetActiveSession() without a null check, which faults on
  // Windows in the runner state.
  void CWldSession::GetSelectionUnits(WeakSet<UserUnit>& outUnits) const
  {
    (void)outUnits;
    FAF_RUNNER_STUB("CWldSession::GetSelectionUnits(WeakSet)");
  }

  // CWldSession.cpp:9086: resolves a record handed out by GetLastQueuedUserCommandAnchor (below),
  // which needs a UserUnit; its callers are CFormation's drag preview (CFormation.cpp), user side.
  Wm3::Vector3f ResolveLastQueuedCommandAnchorPosition(const QueuedUserCommandRecord* const record)
  {
    (void)record;
    FAF_RUNNER_STUB("ResolveLastQueuedCommandAnchorPosition");
    return Wm3::Vector3f{};
  }

  // CWldSession.cpp:9238/9247. SSessionSaveDataTypeInfo.cpp registers them as the reflection
  // constructor/destructor; an SSessionSaveData exists only for a save game (CWldSession::GetSaveData,
  // loading a save), which a replay run never makes. The real constructor builds the session's node
  // map (a sentinel head node), which a stand-in cannot reproduce without the session code, so both
  // are traps: the destructor only runs on an object the constructor made.
  SSessionSaveData::SSessionSaveData()
  {
    FAF_RUNNER_TRAP("SSessionSaveData::SSessionSaveData");
  }

  SSessionSaveData::~SSessionSaveData()
  {
    FAF_RUNNER_TRAP("SSessionSaveData::~SSessionSaveData");
  }

  // CWldSession.cpp:13263, :9682, :12966, :10448, :10554, :13021, :11230, :10150, :10170, :10202,
  // :13235, :11270, :13006, :10350, :10372, :13293, :10039, :10463, :10536, :10651. The callers are
  // user-side Lua binders and console commands in Sim.cpp, CConCommand.cpp,
  // CCommandLuaFunctionRegistrations.cpp and SessionStartup.cpp (the save-game binder), and each gets
  // the session from WLD_GetActiveSession() (null) or a session it was handed.
  boost::shared_ptr<SSessionSaveData> CWldSession::GetSaveData() const
  {
    FAF_RUNNER_STUB("CWldSession::GetSaveData");
    return {};
  }

  const MouseInfo& CWldSession::GetCursorInfo() const
  {
    FAF_RUNNER_STUB("CWldSession::GetCursorInfo");
    static const MouseInfo sNoCursor{};
    return sNoCursor;
  }

  void CWldSession::SetSelection(const WeakSet<UserEntity>& selection)
  {
    (void)selection;
    FAF_RUNNER_STUB("CWldSession::SetSelection");
  }

  gpg::fastvector_n<SBuildTemplateInfo, 16>* CWldSession::GetActiveBuildTemplate(
    float* const outTemplateSpanZ, float* const outTemplateSpanX, gpg::fastvector_n<SBuildTemplateInfo, 16>* const result
  ) const
  {
    (void)outTemplateSpanZ;
    (void)outTemplateSpanX;
    FAF_RUNNER_STUB("CWldSession::GetActiveBuildTemplate");
    return result;
  }

  void CWldSession::SetActiveBuildTemplate(
    const gpg::fastvector_n<SBuildTemplateInfo, 16>& templates, const float templateSpanX, const float templateSpanZ
  )
  {
    (void)templates;
    (void)templateSpanX;
    (void)templateSpanZ;
    FAF_RUNNER_STUB("CWldSession::SetActiveBuildTemplate");
  }

  bool CWldSession::CanSelectUnit(UserUnit* const unit) const
  {
    (void)unit;
    FAF_RUNNER_STUB("CWldSession::CanSelectUnit");
    return false;
  }

  void CWldSession::GetSelectionUnits(msvc8::vector<UserUnit*>& outUnits) const
  {
    (void)outUnits;
    FAF_RUNNER_STUB("CWldSession::GetSelectionUnits(vector)");
  }

  void CWldSession::AddToExtraSelectList(UserEntity* const entity)
  {
    (void)entity;
    FAF_RUNNER_STUB("CWldSession::AddToExtraSelectList");
  }

  void CWldSession::RemoveFromExtraSelectList(UserEntity* const entity)
  {
    (void)entity;
    FAF_RUNNER_STUB("CWldSession::RemoveFromExtraSelectList");
  }

  void CWldSession::ClearExtraSelectList()
  {
    FAF_RUNNER_STUB("CWldSession::ClearExtraSelectList");
  }

  void CWldSession::SyncPlayableRect(const gpg::Rect2i& playableRect)
  {
    (void)playableRect;
    FAF_RUNNER_STUB("CWldSession::SyncPlayableRect");
  }

  void CWldSession::GetValidAttackingUnits(msvc8::vector<UserUnit*>& outUnits) const
  {
    (void)outUnits;
    FAF_RUNNER_STUB("CWldSession::GetValidAttackingUnits");
  }

  void CWldSession::SetSelectionUnits(const msvc8::vector<UserUnit*>& units)
  {
    (void)units;
    FAF_RUNNER_STUB("CWldSession::SetSelectionUnits");
  }

  void CWldSession::RequestPause()
  {
    FAF_RUNNER_STUB("CWldSession::RequestPause");
  }

  void CWldSession::Resume()
  {
    FAF_RUNNER_STUB("CWldSession::Resume");
  }

  LuaPlus::LuaObject CWldSession::GetScenarioInfo() const
  {
    FAF_RUNNER_STUB("CWldSession::GetScenarioInfo");
    return LuaPlus::LuaObject{};
  }

  void CWldSession::RequestFocusArmy(const int index)
  {
    (void)index;
    FAF_RUNNER_STUB("CWldSession::RequestFocusArmy");
  }

  void CWldSession::GenerateBuildTemplates()
  {
    FAF_RUNNER_STUB("CWldSession::GenerateBuildTemplates");
  }

  void CWldSession::ClearBuildTemplates()
  {
    FAF_RUNNER_STUB("CWldSession::ClearBuildTemplates");
  }

  void CWldSession::DirtyCommandGraph()
  {
    FAF_RUNNER_STUB("CWldSession::DirtyCommandGraph");
  }

  // CWldSession.cpp:9230: collects the user units of a session, which the runner never has (caller:
  // a user binder in CCommandLuaFunctionRegistrations.cpp).
  void GetSessionUserUnits(CWldSession* const session, msvc8::vector<UserUnit*>& outUnits)
  {
    (void)session;
    (void)outUnits;
    FAF_RUNNER_STUB("GetSessionUserUnits");
  }

  // CWldSession.cpp:11324: re-issues a user command for UserUnits (caller: the factory-queue binders
  // in Sim.cpp, user side).
  void ISSUE_IncreaseCommandCount(UserCommandIssueHelper* const helper, const int count)
  {
    (void)helper;
    (void)count;
    FAF_RUNNER_STUB("ISSUE_IncreaseCommandCount");
  }

  // CWldSession.cpp:12921: the selection overload; its callers (console commands in CConCommand.cpp
  // and user binders in CCommandLuaFunctionRegistrations.cpp) pass `session->mSelection` of
  // WLD_GetActiveSession(), which the runner never has.
  void ISSUE_Command(const WeakSet<UserEntity>& entities, const SSTICommandIssueData& commandIssueData, const bool clearQueue)
  {
    (void)entities;
    (void)commandIssueData;
    (void)clearQueue;
    FAF_RUNNER_STUB("ISSUE_Command(WeakSet)");
  }

  // CWldSession.cpp:12846: tells the UI scripts about an issued user command (caller: ISSUE_Command
  // in Sim.cpp, which only the user side calls).
  void UI_OnCommandIssued(
    const gpg::fastvector<UserUnit*>& units, const SSTICommandIssueData& commandIssueData, const bool doClear
  )
  {
    (void)units;
    (void)commandIssueData;
    (void)doClear;
    FAF_RUNNER_STUB("UI_OnCommandIssued");
  }

  // CWldSession.cpp:683, :737, :755 and :2852, the same bodies: the cursor record GetCursorInfo above
  // hands out, and the build-template copy CCommandLuaFunctionRegistrations.cpp makes.
  MouseInfo::MouseInfo()
    : mHitValid(0u)
    , pad_01{0u, 0u, 0u}
    , mMouseWorldPos(0.0f, 0.0f, 0.0f)
    , mUnitHover()
    , mIsDragger(-1)
    , mMouseScreenPos(0.0f, 0.0f)
  {}

  MouseInfo::~MouseInfo() = default;

  UserEntity* MouseInfo::HoveredEntity() const noexcept
  {
    FAF_RUNNER_STUB("MouseInfo::HoveredEntity");
    return mUnitHover.GetObjectPtr();
  }

  SBuildTemplateInfo::SBuildTemplateInfo(const SBuildTemplateInfo& other)
    : mPos(other.mPos)
    , mBuildOrder(other.mBuildOrder)
    , mBlueprintId(other.mBlueprintId)
  {
    FAF_RUNNER_STUB("SBuildTemplateInfo::SBuildTemplateInfo(copy)");
  }

  // ---------------------------------------------------------------------------------------------
  // UserUnit.cpp is linked for real since M3c (the closure takes its arm64 compile status from the
  // post-M3b sweep, where it compiles), so its User-set binders register as on Windows and its
  // vtable and type_info come from its own key function. It still cannot construct a UserUnit: only
  // CWldSession creates them, and the runner has no session.

  // CWldSession.cpp:9708: the world position of a queued command's anchor. Its callers in UserUnit.cpp
  // (the build-drag lookup at UserUnit.cpp:1793) and CWldSession.cpp work on the user command queues
  // of UserUnits, which need a session; the Windows runner never reaches it, and the real body reads
  // the session's command graph, so this is a trap.
  Wm3::Vector3f ResolveCommandGraphAnchorWorldPosition(UserCommandIssueHelper& helper) noexcept
  {
    (void)helper;
    FAF_RUNNER_TRAP("ResolveCommandGraphAnchorWorldPosition");
  }

  // CWldSession.cpp:16298: loads a scenario's info table into a Lua state. Its closure caller is
  // CLobby::LaunchGame (CLobby.cpp:3193), started only from the lobby's Lua, which the runner never
  // runs (the runner loads the scenario through CWldSessionLoaderImpl). A trap.
  LuaPlus::LuaObject WLD_LoadScenarioInfo(const msvc8::string& scenarioFile, LuaPlus::LuaState* const state)
  {
    (void)scenarioFile;
    (void)state;
    FAF_RUNNER_TRAP("WLD_LoadScenarioInfo");
  }
  // ---------------------------------------------------------------------------------------------
  // CWldSession.cpp's user-side console variables (CWldSession.cpp:1100-1206), with the values they
  // are defined with there. CConCommand.cpp registers them as console variables; only the session's
  // drawing code and the UI scripts read or set them, so in the runner they keep these values.

  bool ui_DebugAltClick = false;
  bool UI_SelectAnything = false;
  std::int32_t ui_CurveSegments = 20;
  float UI_StrategicProjectileLOD = 128.0f;
  bool UI_RenResources = true;
  bool UI_RenProjectileIcons = true;
  bool UI_RenProjectileGlow = true;
  bool UI_forceWeaponsToYellow = true;
  float UI_RenProjectileGlowMin = 0.01f;
  float UI_RenProjectileGlowMax = 0.15f;
  float UI_RenProjectileGlowPeriod = 2.0f;
  float UI_ResourceLODCutoff = 75.0f;
  bool ui_RenderUnitBars = true;
  bool ui_RenderIcons = true;
  float ui_lifebarHeight = 0.125f;
  float ui_LifebarWidth = 1.5f;
  float ui_LifebarLOD = 200.0f;
  float ui_LifebarOffset = 0.1f;
  bool ui_NisRenderIcons = true;
  bool ui_RenderCustomNames = true;
  bool ui_RenderSelectionSetNames = true;
  std::uint32_t ui_CustomNameColor = 0xFF00AA00u;
  std::int32_t ui_CustomNameFontSize = 12;
  std::uint32_t ui_SelectionSetNamesColor = 0xFF00AA00u;
  float ui_StrategicIconBlinkRate = 0.6f;
  float ui_FuelEmptyBlinkRate = 0.1f;
  float ui_StrategicIconBlinkDuration = 0.5f;
  std::uint32_t ui_LifeBarGoodColor = 0xFF00FF00u;
  std::uint32_t ui_LifeBarMedColor = 0xFFFFFF00u;
  std::uint32_t ui_LifeBarBadColor = 0xFFFF0000u;
  float ui_LifeBarGoodCutoff = 0.75f;
  float ui_LifeBarBadCutoff = 0.25f;
  std::uint32_t ui_FuelBarColor = 0xFFF4EC4Du;
  std::uint32_t ui_FuelWarningColor = 0xFFFF0000u;
  std::uint32_t ui_ShieldBarColor = 0xFF00C3F7u;
  std::uint32_t ui_ProgressBarColor = 0xFFFF9900u;
  msvc8::string ui_CustomNameFont{};
  bool ui_ForceLifbarsOnEnemy = false;
  bool ui_AlwaysRenderStrategicIcons = false;
  bool ui_DrawPathPreview = false;
  bool ui_PathPreview = false;
  float ui_CurveSmoothness = 0.0f;
  float ui_PathSmoothness = 0.0f;
  float ui_MaxTextLOD = 0.0f;
  std::int32_t ui_CommandGraphMaxNodeUnits = 0;
  float ui_MinWaypointSize = 0.0f;
  float ui_MaxWaypointSize = 0.0f;
  float ui_WaypointLineScale = 0.0f;
} // namespace moho
