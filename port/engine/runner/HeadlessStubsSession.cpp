// The user session (moho/sim/CWldSession.cpp) and the user-side unit (moho/unit/core/UserUnit.cpp)
// for the Android headless runner; see HeadlessStubs.h for the rules.
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
  // loading a save), which a replay run never makes. The stand-in still leaves an empty map.
  SSessionSaveData::SSessionSaveData()
  {
    FAF_RUNNER_STUB("SSessionSaveData::SSessionSaveData");
    mNodeMap.mAllocProxy = nullptr;
    mNodeMap.mHead = nullptr;
    mNodeMap.mSize = 0u;
  }

  SSessionSaveData::~SSessionSaveData()
  {
    FAF_RUNNER_STUB("SSessionSaveData::~SSessionSaveData");
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
  // UserUnit.cpp. Both take a UserUnit, and the runner has none; the callers are CFormation's
  // user-side drag preview (CFormation.cpp:180, :257, :294 also read WLD_GetActiveSession()->mState).

  // UserUnit.cpp: GetLastQueuedUserCommandAnchor (declared UserUnit.h:1198).
  const QueuedUserCommandRecord* GetLastQueuedUserCommandAnchor(const UserUnit* const unit) noexcept
  {
    (void)unit;
    FAF_RUNNER_STUB("GetLastQueuedUserCommandAnchor");
    return nullptr;
  }

  // UserUnit.cpp: GetIUnitBridge (declared UserUnit.h:1170).
  IUnit* GetIUnitBridge(UserUnit* const self) noexcept
  {
    (void)self;
    FAF_RUNNER_STUB("GetIUnitBridge");
    return nullptr;
  }

  // UserUnit.cpp: the user side of command issuing (UserCommandIssueHelper and the per-unit command
  // queues) and UserUnit queries. They act on UserUnits or on helpers made for them; the callers are
  // ISSUE_Command, ISSUE_SetCommandTarget and the factory-queue binders in Sim.cpp and the user
  // binders of CCommandLuaFunctionRegistrations.cpp, all reached only through a session's selection.
  UserUnit* GetUserUnitOptional(const LuaPlus::LuaObject& object, LuaPlus::LuaState* const state)
  {
    (void)object;
    (void)state;
    FAF_RUNNER_STUB("GetUserUnitOptional");
    return nullptr;
  }

  void CollectUpgradeCommandTargetBlueprints(UserUnit* const unit, msvc8::list<const RUnitBlueprint*>& out)
  {
    (void)unit;
    (void)out;
    FAF_RUNNER_STUB("CollectUpgradeCommandTargetBlueprints");
  }

  bool USERUNIT_IsDockTargetIdle(const UserUnit* const unit) noexcept
  {
    (void)unit;
    FAF_RUNNER_STUB("USERUNIT_IsDockTargetIdle");
    return false;
  }

  std::int32_t GetUserUnitManagerQueueSize(UserCommandQueue* const managerPtr) noexcept
  {
    (void)managerPtr;
    FAF_RUNNER_STUB("GetUserUnitManagerQueueSize");
    return 0;
  }

  void ResetUserUnitManagerState(UserCommandQueue* const manager, const std::int32_t commandType)
  {
    (void)manager;
    (void)commandType;
    FAF_RUNNER_STUB("ResetUserUnitManagerState");
  }

  void UserUnitManagerAdd(UserCommandQueue* const manager, UserCommandIssueHelper* const helper, const CmdId cmdId, const CmdId index)
  {
    (void)manager;
    (void)helper;
    (void)cmdId;
    (void)index;
    FAF_RUNNER_STUB("UserUnitManagerAdd");
  }

  Wm3::Vector3<float> ResolvePositionFromTarget(const UserTarget& target) noexcept
  {
    (void)target;
    FAF_RUNNER_STUB("ResolvePositionFromTarget");
    return Wm3::Vector3<float>{};
  }

  UserEntity* DecodeEntityFromCommandTargetIfEntity(const UserTarget* const target) noexcept
  {
    (void)target;
    FAF_RUNNER_STUB("DecodeEntityFromCommandTargetIfEntity");
    return nullptr;
  }

  EUnitCommandType ResolveCommandIssueHelperCommandType(const UserCommandIssueHelper& helper) noexcept
  {
    (void)helper;
    FAF_RUNNER_STUB("ResolveCommandIssueHelperCommandType");
    return EUnitCommandType{};
  }

  std::int32_t QueuedBuildCommandCount(const UserCommandIssueHelper& helper) noexcept
  {
    (void)helper;
    FAF_RUNNER_STUB("QueuedBuildCommandCount");
    return 0;
  }

  // UserUnit.cpp:1593, the same body (a value type).
  UserCommandIssueLocalEvent::UserCommandIssueLocalEvent(const CmdId cmdId, const ECommandIssueEvent type)
    : mCmdId(cmdId)
    , mType(type)
    , mUnits()
    , mCount(0)
    , mTarget()
    , mCells()
  {
    FAF_RUNNER_STUB("UserCommandIssueLocalEvent::UserCommandIssueLocalEvent");
  }

  // UserUnit.cpp:1609 without its log probe. The destructor below leaves out the real one's removal
  // of the helper from the session's command map (DiscardActiveSessionCommandIssueHelper), since
  // there is no session; the members tear themselves down as there.
  UserCommandIssueHelper::UserCommandIssueHelper(
    const SSTICommandConstantData& constantData, const std::uint8_t deleteWhenDue, const std::int32_t dueSeqNo
  )
    : WeakObject()
    , mConstantData(constantData)
    , mVariableData()
    , mReservedB0(0u)
    , mDeleteWhenDue(deleteWhenDue)
    , mVariableDataDirty(1u)
    , mReservedB3(0u)
    , mDueSeqNo(dueSeqNo)
    , mLocalQueue{}
    , mCursorEntitySet()
  {
    FAF_RUNNER_STUB("UserCommandIssueHelper::UserCommandIssueHelper");
  }

  UserCommandIssueHelper::~UserCommandIssueHelper() noexcept
  {
    FAF_RUNNER_STUB("UserCommandIssueHelper::~UserCommandIssueHelper");
  }

  void UserCommandIssueHelper::AdvanceLocalEventsToBeat(const std::int32_t beat) noexcept
  {
    (void)beat;
    FAF_RUNNER_STUB("UserCommandIssueHelper::AdvanceLocalEventsToBeat");
  }

  // ---------------------------------------------------------------------------------------------
  // UserUnit's virtual functions (UserUnit.h; bodies in UserUnit.cpp). CScriptEvent.cpp and Sim.cpp
  // look the class up as gpg::LookupRType(typeid(UserUnit)) (for Lua objects that wrap a UserUnit), and
  // under the Itanium ABI UserUnit's type_info and vtable are emitted only with its key function,
  // ~UserUnit. Defining all of its virtual functions here emits them. No UserUnit can exist in the
  // runner: its only constructor, UserUnit(CWldSession*, const SCreateUnitParams&), is not linked, so
  // a closure TU that ever constructed one would fail the link instead of reaching these. The
  // accessors that only read a member keep the real bodies; the rest log and return a neutral value.
  // (On Android UserUnit's RType is never registered, UserUnitTypeInfo.cpp being outside the closure,
  // so the lookups themselves could only fail; they are reached only for UserUnit Lua objects.)

  // UserUnit.cpp:2016.
  UserUnit::~UserUnit()
  {
    FAF_RUNNER_STUB("UserUnit::~UserUnit");
  }

  void UserUnit::Tick(const std::int32_t seqNo)
  {
    (void)seqNo;
    FAF_RUNNER_STUB("UserUnit::Tick");
  }

  // UserUnit.cpp:2555 and :2566, the same bodies.
  const UserUnit* UserUnit::IsUserUnit() const
  {
    FAF_RUNNER_STUB("UserUnit::IsUserUnit const");
    return this;
  }

  UserUnit* UserUnit::IsUserUnit()
  {
    FAF_RUNNER_STUB("UserUnit::IsUserUnit");
    return this;
  }

  float UserUnit::GetUniformScale() const
  {
    FAF_RUNNER_STUB("UserUnit::GetUniformScale");
    return 1.0f;
  }

  // UserUnit.cpp:2591, :2602 and the factory-queue pair after them, the same bodies.
  const UserCommandQueue* UserUnit::GetCommandQueue() const
  {
    FAF_RUNNER_STUB("UserUnit::GetCommandQueue const");
    return mManager;
  }

  UserCommandQueue* UserUnit::GetCommandQueue()
  {
    FAF_RUNNER_STUB("UserUnit::GetCommandQueue");
    return mManager;
  }

  const UserCommandQueue* UserUnit::GetFactoryCommandQueue() const
  {
    FAF_RUNNER_STUB("UserUnit::GetFactoryCommandQueue const");
    return mFactoryManager;
  }

  UserCommandQueue* UserUnit::GetFactoryCommandQueue()
  {
    FAF_RUNNER_STUB("UserUnit::GetFactoryCommandQueue");
    return mFactoryManager;
  }

  void UserUnit::UpdateEntityData(const SSTIEntityVariableData& variableData)
  {
    (void)variableData;
    FAF_RUNNER_STUB("UserUnit::UpdateEntityData");
  }

  void UserUnit::UpdateVisibility()
  {
    FAF_RUNNER_STUB("UserUnit::UpdateVisibility");
  }

  bool UserUnit::RequiresUIRefresh() const
  {
    FAF_RUNNER_STUB("UserUnit::RequiresUIRefresh");
    return false;
  }

  bool UserUnit::IsSelectable() const
  {
    FAF_RUNNER_STUB("UserUnit::IsSelectable");
    return false;
  }

  bool UserUnit::IsBeingBuilt() const
  {
    FAF_RUNNER_STUB("UserUnit::IsBeingBuilt");
    return false;
  }

  void UserUnit::NotifyFocusArmyUnitDamaged()
  {
    FAF_RUNNER_STUB("UserUnit::NotifyFocusArmyUnitDamaged");
  }

  void UserUnit::CreateMeshInstance(const bool forUnitPose)
  {
    (void)forUnitPose;
    FAF_RUNNER_STUB("UserUnit::CreateMeshInstance");
  }

  void UserUnit::DestroyMeshInstance()
  {
    FAF_RUNNER_STUB("UserUnit::DestroyMeshInstance");
  }

  EntId UserUnit::GetEntityId() const
  {
    FAF_RUNNER_STUB("UserUnit::GetEntityId");
    return EntId{};
  }

  // UserUnit.cpp:2240 and :2249, the same bodies.
  const Wm3::Vec3f& UserUnit::GetPosition() const
  {
    FAF_RUNNER_STUB("UserUnit::GetPosition");
    return mVariableData.mCurTransform.pos_;
  }

  const VTransform& UserUnit::GetTransform() const
  {
    FAF_RUNNER_STUB("UserUnit::GetTransform");
    return mVariableData.mCurTransform;
  }

  const RUnitBlueprint* UserUnit::GetBlueprint() const
  {
    FAF_RUNNER_STUB("UserUnit::GetBlueprint");
    return nullptr;
  }

  LuaPlus::LuaObject UserUnit::GetLuaObject()
  {
    FAF_RUNNER_STUB("UserUnit::GetLuaObject");
    return LuaPlus::LuaObject{};
  }

  float UserUnit::CalcTransportLoadFactor() const
  {
    FAF_RUNNER_STUB("UserUnit::CalcTransportLoadFactor");
    return 0.0f;
  }

  bool UserUnit::IsDead() const
  {
    FAF_RUNNER_STUB("UserUnit::IsDead");
    return false;
  }

  bool UserUnit::DestroyQueued() const
  {
    FAF_RUNNER_STUB("UserUnit::DestroyQueued");
    return false;
  }

  bool UserUnit::IsMobile() const
  {
    FAF_RUNNER_STUB("UserUnit::IsMobile");
    return false;
  }

  bool UserUnit::IsNavigatorIdle() const
  {
    FAF_RUNNER_STUB("UserUnit::IsNavigatorIdle");
    return false;
  }

  bool UserUnit::IsUnitState(const EUnitState state) const
  {
    (void)state;
    FAF_RUNNER_STUB("UserUnit::IsUnitState");
    return false;
  }

  // UserUnit.cpp:2348 and :2357, the same bodies.
  UnitAttributes& UserUnit::GetAttributes()
  {
    FAF_RUNNER_STUB("UserUnit::GetAttributes");
    return mUnitVarDat.mAttributes;
  }

  const UnitAttributes& UserUnit::GetAttributes() const
  {
    FAF_RUNNER_STUB("UserUnit::GetAttributes const");
    return mUnitVarDat.mAttributes;
  }

  StatItem* UserUnit::GetStat(const gpg::StrArg statPath, const std::string& defaultValue)
  {
    (void)statPath;
    (void)defaultValue;
    FAF_RUNNER_STUB("UserUnit::GetStat(string)");
    return nullptr;
  }

  StatItem* UserUnit::GetStat(const gpg::StrArg statPath, const float& defaultValue)
  {
    (void)statPath;
    (void)defaultValue;
    FAF_RUNNER_STUB("UserUnit::GetStat(float)");
    return nullptr;
  }

  StatItem* UserUnit::GetStat(const gpg::StrArg statPath, const int& defaultValue)
  {
    (void)statPath;
    (void)defaultValue;
    FAF_RUNNER_STUB("UserUnit::GetStat(int)");
    return nullptr;
  }

  StatItem* UserUnit::GetStat(const gpg::StrArg statPath)
  {
    (void)statPath;
    FAF_RUNNER_STUB("UserUnit::GetStat");
    return nullptr;
  }

  gpg::RType* UserUnit::GetClass() const
  {
    FAF_RUNNER_STUB("UserUnit::GetClass");
    return nullptr;
  }

  gpg::RRef UserUnit::GetDerivedObjectRef()
  {
    FAF_RUNNER_STUB("UserUnit::GetDerivedObjectRef");
    return gpg::RRef{};
  }

  bool UserUnit::FindWeaponBy(const std::int32_t rangeCategoryFilter, float* const outMinRange, float* const outMaxRange) const
  {
    (void)rangeCategoryFilter;
    (void)outMinRange;
    (void)outMaxRange;
    FAF_RUNNER_STUB("UserUnit::FindWeaponBy");
    return false;
  }

  bool UserUnit::GetIntelRanges(float* const outOmniRange, float* const outRadarRange, float* const outSonarRange) const
  {
    (void)outOmniRange;
    (void)outRadarRange;
    (void)outSonarRange;
    FAF_RUNNER_STUB("UserUnit::GetIntelRanges");
    return false;
  }

  bool UserUnit::GetMaxCounterIntel(float* const outMaxCounterIntelRange) const
  {
    (void)outMaxCounterIntelRange;
    FAF_RUNNER_STUB("UserUnit::GetMaxCounterIntel");
    return false;
  }

  bool UserUnit::GetAutoMode() const
  {
    FAF_RUNNER_STUB("UserUnit::GetAutoMode");
    return false;
  }

  bool UserUnit::IsAutoSurfaceMode() const
  {
    FAF_RUNNER_STUB("UserUnit::IsAutoSurfaceMode");
    return false;
  }

  bool UserUnit::Func1() const
  {
    FAF_RUNNER_STUB("UserUnit::Func1");
    return false;
  }

  bool UserUnit::IsOverchargePaused() const
  {
    FAF_RUNNER_STUB("UserUnit::IsOverchargePaused");
    return false;
  }

  char* UserUnit::GetCustomName()
  {
    FAF_RUNNER_STUB("UserUnit::GetCustomName");
    return nullptr;
  }

  float UserUnit::GetFuel() const
  {
    FAF_RUNNER_STUB("UserUnit::GetFuel");
    return 0.0f;
  }

  float UserUnit::GetShield() const
  {
    FAF_RUNNER_STUB("UserUnit::GetShield");
    return 0.0f;
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
