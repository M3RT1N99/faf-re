// Griff, the app's touch scheme (buildstage/touch-concept-v2.md, TouchInput): what its gestures do in the game, and where
// the handle over the selection goes. FAF's Lua reports each frame where the selection is on the screen
// (assets/fafre_ui/lua/fafre/griff.lua); the handle goes beside it where the map is free, and the activity reads it to draw it
// and to know a finger on it (Game.griff). A gesture's clicks are a mouse's, and Plus is the keyboard's Shift: FAF decides
// what they order, as for a mouse, and each click picks what is under it at its own place (CUIWorldView::HandleEvent). The
// camera moves as the UI moves it (griff.lua: its glide, its flight to the group, the pinch), not the simulation. They come
// to the program's thread as tasks, in the order of the rest of the input (GameHost.cpp). The draft holds buildings planned
// at world places (include/GriffDraft.h) until the tick orders them by FAF's build mode and a mouse's click per plan. What a
// gesture orders is watched (griff.lua), for the undo that takes it back where that saves work (include/GriffUndo.h), with
// FAF's own inputs again; FAF's own feedback blip marks each order's place. All Griff shows lives in FAF's UI, in FAF's look:
// the rail's buttons, the undo's and the draft's tick and cross as FAF's order buttons, the hints where FAF prints its
// messages, the waypoint a resting finger marked (fafre/griffhud.lua); the handle, a resting finger's ring, a stroke, the
// draft's plans as FAF's build preview ghosts, Box's box and the loupe's frame (fafre/griffmap.lua, GriffShown.cpp); the
// groups' icons (fafre/griffgroups.lua). While the draft is open the map's view draws the ground each building takes as FAF's
// build mode does (include/GriffSkirts.h, DraftSkirts). The activity keeps the fingers and says how the buttons look
// (FafreGriffHud).

#include <jni.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <exception>
#include <iterator>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Windows.h>
#include <Win32AndroidHost.h>

#include <CameraClear.h>
#include <GriffDraft.h>
#include <GriffGroups.h>
#include <GriffPlace.h>
#include <GriffSkirts.h>
#include <GriffTap.h>
#include <GriffUndo.h>

#include "GriffShown.h"
#include "HostClicks.h"
#include "MenuFingers.h"
#include "TaskPlaces.h"
#include "gpg/core/utils/Logging.h"
#include "moho/command/CommandIssueHelper.h"
#include "moho/command/UserCommandQueue.h"
#include "moho/console/CConCommand.h"
#include "moho/lua/CScrLuaBinder.h"
#include "moho/render/camera/CameraImpl.h"
#include "moho/resource/blueprints/RMeshBlueprint.h"
#include "moho/resource/blueprints/RUnitBlueprint.h"
#include "moho/sim/CFormation.h"
#include "moho/sim/CWldSession.h"
#include "moho/sim/UserArmy.h"
#include "moho/ui/UiPrivate.h"
#include "moho/unit/core/UserUnit.h"

namespace moho
{
  // src/moho/ui/CUIWorldViewEvents.cpp's, by its patch.
  [[nodiscard]] bool UI_GamePlaying() noexcept;
  [[nodiscard]] bool UI_TapTarget(float x, float y, float reach, float smallSide, float* targetX, float* targetY);
  void UI_CancelBuildDrag();
  // src/moho/sim/CWldSessionRender.cpp's: the size of the strategic icons (FAF's option strat_icon_scale).
  extern float ui_StrategicIconScale;
} // namespace moho

namespace
{
  using griffplace::Box;
  using grifftap::kRailBox;
  using grifftap::kRailQueue;
  using hosttask::Coordinate;
  using hosttask::Places;
  using hosttask::SignedCoordinate;

  // TouchInput's STEP_: a tap's step on its ladder.
  constexpr int kStepTap = 0;
  constexpr int kStepAgain = 1;
  constexpr int kStepAllOfType = 2;
  constexpr int kStepAttackMove = 3;
  constexpr int kStepAllMobile = 4;
  constexpr int kStepTypeInView = 5;
  constexpr int kStepGroupAgain = 6;

  // TouchInput's TAP_: what a tap was, for its ladder.
  constexpr int kTapUi = 1;
  constexpr int kTapOwn = 2;
  constexpr int kTapGround = 3;
  constexpr int kTapEmpty = 4;
  constexpr int kTapOther = 5;
  constexpr int kTapGroup = 6;

  // Stroke's END_ that the program decides, and its flag for FAF's attack-move armed; griff.lua's StrokeKind answers the others.
  constexpr int kEndPanel = 8;
  constexpr int kEndMinimap = 9;
  constexpr int kEndAttackMove = 64;

  // Game's SAID_.
  constexpr int kSaidTap = 1;
  constexpr int kSaidGroupStored = 3;
  constexpr int kSaidCommitted = 4;
  constexpr int kSaidPan = 5;
  constexpr int kSaidOrdered = 6;
  constexpr int kSaidUndone = 7;
  constexpr int kSaidFormation = 8;
  constexpr int kSaidDraftRest = 9;
  constexpr int kSaidDistributes = 10;

  /// Game.GRIFF_VALUES: what Game.griff gives of the handle.
  constexpr int kHandleValues = 11;

  // Game's HINT_: griff.lua's Hint words them where FAF prints its own messages. The control groups' (10 to 12) come from
  // their hook, the stroke's Cancelled from the activity.
  constexpr int kHintNoFreeResource = 1;
  constexpr int kHintTaken = 2;
  constexpr int kHintFull = 3;
  constexpr int kHintNoPlan = 4;
  constexpr int kHintNoEngineer = 5;
  constexpr int kHintAllOnBuildings = 6;
  constexpr int kHintSomeOnBuildings = 7;
  constexpr int kHintFirstTickOrCross = 8;
  constexpr int kHintNotOrdered = 9;
  constexpr int kHintCancelled = 13;
  constexpr int kHintUndoneInPart = 14;
  constexpr int kHintNotUndone = 15;
  constexpr int kHintNotDeleted = 16;

  // Game's DRAFT_: a draft gesture's phase.
  constexpr int kDraftShow = 0;
  constexpr int kDraftEnd = 1;
  constexpr int kDraftAbort = 2;

  // The answer of TapKind in griff.lua.
  constexpr int kOwnUnit = 1;
  constexpr int kSelection = 2;
  constexpr int kUnitUnder = 4;
  constexpr int kTroopsSelected = 8;
  constexpr int kMoveOrder = 16;
  constexpr int kMobileSelected = 32;

  /// What a rest and a drag on the ground give with mobile units selected: FAF's formation, the right button's drag (true,
  /// Griff v4), or a selection box, as with nothing selected (false). The user has not decided which yet (2026-11-15): this
  /// is the one place that chooses. A selection without mobile units (factories alone) boxes either way, and the rest's lift
  /// is FAF's right click there either way.
  constexpr bool kRestDragGivesFormation = true;

  /// The ring of the handle's rest whose lift gives FAF's Distribute orders (Game.REST_, grifftap::RestLook): its drag is the
  /// handle's route, a move, so the formation's.
  constexpr grifftap::RestLook kRestDistribute = grifftap::RestLook::kFormation;

  // griff.lua's yes and no for Ask, whose 0 is no answer.
  constexpr int kYes = 2;

  constexpr char kGriffLua[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').";
  constexpr char kTapKind[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').TapKind()";
  constexpr char kTroops[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').Troops()";
  constexpr char kStrokeKind[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').StrokeKind()";
  constexpr char kStartAttackMove[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').StartAttackMove()";
  constexpr char kAttackMoveArmed[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').AttackMoveArmed()";
  constexpr char kOffersPatrol[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').OffersPatrol()";
  constexpr char kStartPatrol[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').StartPatrol()";
  constexpr char kEndCommandMode[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').EndCommandMode()";
  constexpr char kShiftLetGo[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').ShiftLetGo()";
  constexpr char kDeselect[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').Deselect()";
  constexpr char kStopCamera[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').StopCamera()";
  constexpr char kPinchEnd[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').PinchEnd()";
  constexpr char kTypeInView[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').SelectTypeInView()";
  constexpr char kDraftEnded[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').DraftEnded()";
  constexpr char kDraftBack[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').DraftBack()";
  constexpr char kCommitBegin[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').CommitBegin()";
  constexpr char kCommitEnd[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').CommitEnd()";
  constexpr char kLookBack[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').LookBack()";
  constexpr char kUndoBegin[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').UndoBegin()";
  constexpr char kUndoEnd[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').UndoEnd()";
  /// FAF's key actions select_all_units_of_same_type and, with MOBILE for its ALLUNITS (the mockup's rough grab of the army,
  /// not the base), select_all_onscreen.
  constexpr char kAllOfType[] = "UI_ExpandCurrentSelection";
  constexpr char kAllMobile[] = "UI_SelectByCategory +inview MOBILE";
  /// FAF's key action spreadattack, G (its shift_spreadattack, Shift-G, has the same): Distribute orders (keyactions.lua).
  constexpr char kDistributeOrders[] = "UI_Lua import(\"/lua/ui/game/hotkeys/distribute-queue.lua\").DistributeOrders(true)";

  /// Shift's and Ctrl's keys, as the keyboard's left ones (KeysAndroid.cpp).
  constexpr UINT kShiftScanCode = 0x2A;
  constexpr UINT kCtrlScanCode = 0x1D;

  // --- What the activity has set, in the picture's pixels ---------------------------------------

  std::atomic<bool> gOn{false};
  /// The handle's radius, how far from its middle a finger is on it, its room from the group's units, and how far from its
  /// middle the tip of its arrow at a group elsewhere is.
  std::atomic<float> gRadius{0};
  std::atomic<float> gTouchRadius{0};
  std::atomic<float> gGap{0};
  std::atomic<float> gArrowReach{0};
  /// The tap help's reach and what counts as a small control (Game.tap).
  std::atomic<float> gTapReach{0};
  std::atomic<float> gTapSmallSide{0};
  /// How far beside a unit a tap picks it: without troops selected, and with them.
  std::atomic<float> gNearReach{0};
  std::atomic<float> gNearReachWithTroops{0};
  /// The speed a second under which the camera's glide stops.
  std::atomic<float> gGlideStop{0};
  /// How far from a finger the draft looks for a free deposit for an extractor (the mockup's snap).
  std::atomic<float> gDepositReach{0};

  /// The app's own pieces over the picture (Game.setGriffCovers): the draft's tick and cross, and the place of the undo's
  /// button a moment after it went (FAF's UI holds the button itself, and the rail's). A finger on one is the activity's: the
  /// handle keeps clear of them, and nothing of the game's lies under them for a gesture, as under FAF's panels.
  std::mutex gCoversMutex;
  std::vector<Box> gCovers;

  [[nodiscard]] bool Covered(const float x, const float y)
  {
    const std::lock_guard<std::mutex> lock(gCoversMutex);
    return std::any_of(gCovers.begin(), gCovers.end(), [x, y](const Box& cover) { return cover.Contains(x, y); });
  }

  [[nodiscard]] bool Covered(const int x, const int y)
  {
    return Covered(static_cast<float>(x), static_cast<float>(y));
  }

  /// What the player opened of FAF's that shows, as griff.lua reports it where it changed (fafre/opened.lua): its windows, its
  /// popups, the multifunction panel's dropouts and the tabs' content, left, top, right and bottom of each in the picture's
  /// pixels. The activity's pieces step aside from them (Game.griffOpened).
  std::mutex gOpenedMutex;
  std::vector<float> gOpened;

  // --- What the program says to the activity -----------------------------------------------------

  std::mutex gSaidMutex;
  std::deque<std::array<int, 3>> gSaid;

  /// The activity hears it when it can (Game.griffSaid).
  void Say(const int what, const int first, const int second)
  {
    {
      const std::lock_guard<std::mutex> lock(gSaidMutex);
      gSaid.push_back({what, first, second});
    }
    menufingers::NotifyActivity();
  }

  // --- The map ----------------------------------------------------------------------------------

  [[nodiscard]] moho::CMauiControl* UiRoot()
  {
    return moho::g_UIManager != nullptr && !moho::g_UIManager->mFrames.Empty() ? moho::g_UIManager->mFrames[0].get() : nullptr;
  }

  [[nodiscard]] moho::CMauiControl* InputRoot()
  {
    moho::CMauiControl* const capture = ResolveTopInputCaptureControl();
    return capture != nullptr ? capture : UiRoot();
  }

  /// The world view a press here comes to, the minimap's too, or none where the UI over it takes it: a press goes on to the
  /// parents, past what does not take it, such as a marker on the map. None under the app's own pieces.
  [[nodiscard]] moho::CUIWorldView* WorldViewAt(const float x, const float y)
  {
    moho::CMauiControl* const root = InputRoot();
    if (root == nullptr || Covered(x, y)) {
      return nullptr;
    }
    for (moho::CMauiControl* control = moho::CMauiControl::GetTopmostControl(root, x, y); control != nullptr;
         control = control->GetParent()) {
      if (auto* const view = dynamic_cast<moho::CUIWorldView*>(control)) {
        return view;
      }
    }
    return nullptr;
  }

  /// Whether a press here comes to the map itself: not to a panel over it, not to the minimap.
  [[nodiscard]] bool OnMap(const float x, const float y)
  {
    moho::CUIWorldView* const view = WorldViewAt(x, y);
    return view != nullptr && !view->IsMiniMap();
  }

  /// Whether a press here comes to a Checkbox of FAF's, which acts on the press (grifftap::DragPressesNothing): the control
  /// under it or a parent whose Lua object has Checkbox's ToggleCheck (maui/checkbox.lua), its class's as FAF's Lua finds it.
  [[nodiscard]] bool PressComesToCheckbox(const float x, const float y)
  {
    moho::CMauiControl* const root = InputRoot();
    return root != nullptr
        && grifftap::DragPressesNothing(
             moho::CMauiControl::GetTopmostControl(root, x, y), [](moho::CMauiControl* const control) { return control->GetParent(); },
             [](moho::CMauiControl* const control) {
               LuaPlus::LuaObject toggle;
               control->FindScript(&toggle, "ToggleCheck");
               return toggle.IsFunction();
             }
           );
  }

  /// Whether FAF's Lua has armed a command mode, which the next click on the map gives. Only with the session's UI up.
  [[nodiscard]] bool CommandModeArmed()
  {
    moho::UICommandModeData mode{};
    moho::UI_GetCommandMode(mode);
    return !mode.mMode.empty();
  }

  /// FAF's attack-move mode for the next click, as its orders panel's button arms it, where that button is offered for the
  /// selection (griff.lua StartAttackMove): whether it is armed. With none armed before.
  [[nodiscard]] bool ArmAttackMove()
  {
    moho::CON_Execute(kStartAttackMove);
    return CommandModeArmed();
  }

  /// FAF's patrol mode for the next click, as its orders panel's button arms it (griff.lua StartPatrol): whether it is armed.
  [[nodiscard]] bool ArmPatrol()
  {
    moho::CON_Execute(kStartPatrol);
    return CommandModeArmed();
  }

  /// Runs a function of griff.lua, `format` with up to three numbers, as its console command.
  void CallGriff(const char* const format, const int first, const int second, const int third)
  {
    char call[64];
    std::snprintf(call, sizeof call, format, first, second, third);
    char command[160];
    std::snprintf(command, sizeof command, "%s%s", kGriffLua, call);
    moho::CON_Execute(command);
  }

  /// A hint of Griff's (Game's HINT_, with its number where it counts), where FAF prints its own messages (griff.lua Hint), while
  /// the session's UI is up. On the program's thread.
  void Hint(const int kind, const int number = 0)
  {
    if (moho::UI_GamePlaying()) {
      CallGriff("Hint(%d, %d)", kind, number, 0);
    }
  }

  // --- The handle -------------------------------------------------------------------------------

  /// A control's box as its layout last worked it out; false where it has not since it last changed. Only FAF's own use
  /// works a layout out: from here it could come before FAF has laid the control out, and the failure would leave 0 in the
  /// layout's cache, which nothing clears (CScriptLazyVar_float::GetValue): a control laid out at 0 for good.
  [[nodiscard]] bool LaidOutBox(moho::CMauiControl* const control, Box& box)
  {
    const auto cached = [](const moho::CScriptLazyVar_float& lazy, float& value) {
      if (lazy.m_state == nullptr) {
        return false;
      }
      const LuaPlus::LuaObject number = lazy.GetByIndex(1);
      if (!number.IsNumber()) {
        return false;
      }
      value = static_cast<float>(number.GetNumber());
      return true;
    };
    return cached(control->mLeftLV, box.left) && cached(control->mTopLV, box.top) && cached(control->mRightLV, box.right)
        && cached(control->mBottomLV, box.bottom);
  }

  /// The free map for the handle: the main map of this view, less what lies over it where the handle would hide it and a
  /// finger on the handle would be another's: FAF's panels, also those that let a click through to the map beneath (the unit
  /// view, the multifunction panel), the minimap, and the app's own pieces. One walk over the UI for all the places a frame
  /// tries.
  /// The HUD is drawn after the map's picture (WRenViewport::Render), so what covers the map is told apart by depth: the
  /// controls at the map's depth or above, such as the unit view, a sibling of the map at its depth, wherever the walk meets
  /// them (FAF makes the map anew when it splits and joins the view, which puts it after the unit view); not what holds the
  /// map or tints the whole screen at a depth below it, as the paused game's glow does. With it, the main map in `mainMap`,
  /// and what of FAF's UI covers it in `fafs`, without the app's pieces.
  [[nodiscard]] griffplace::FreeMap
  FreeMapOver(const Box& view, moho::CUIWorldView** const mainMap = nullptr, std::vector<Box>* const fafs = nullptr)
  {
    if (mainMap != nullptr) {
      *mainMap = nullptr;
    }
    moho::CMauiControl* const root = UiRoot();
    if (root == nullptr) {
      return {};
    }
    struct Seen
    {
      Box box;
      float depth;
    };
    std::vector<Seen> seen;
    // What is drawn on a map, as a label over a unit, or is a part of the minimap: the walk comes to a parent first.
    std::unordered_set<const moho::CMauiControl*> onAMap;
    const float middleX = (view.left + view.right) / 2;
    const float middleY = (view.top + view.bottom) / 2;
    bool found = false;
    float mapDepth = 0;
    for (moho::CMauiControl* control = root; control != nullptr; control = control->DepthFirstSuccessor(root)) {
      const moho::CMauiControl* const parent = control->GetParent();
      if (parent != nullptr && (onAMap.count(parent) != 0 || dynamic_cast<const moho::CUIWorldView*>(parent) != nullptr)) {
        onAMap.insert(control);
        continue;
      }
      // What cannot be seen covers nothing, such as a panel that stands faded out; nor does what is not laid out yet.
      if (control->IsHidden() || control->GetAlpha() <= 0) {
        continue;
      }
      Box box;
      if (auto* const map = dynamic_cast<moho::CUIWorldView*>(control); map != nullptr && !map->IsMiniMap()) {
        if (!found && LaidOutBox(map, box) && box.Contains(middleX, middleY)) {
          found = true;
          mapDepth = map->mDepth;
          if (mainMap != nullptr) {
            *mainMap = map;
          }
        }
        continue;
      }
      const bool drawsNothing = control->IsHitTestDisabled()
                             && (dynamic_cast<moho::CMauiGroup*>(control) != nullptr || dynamic_cast<moho::CMauiFrame*>(control) != nullptr);
      if (drawsNothing || !LaidOutBox(control, box)) {
        continue;
      }
      seen.push_back(Seen{box, control->mDepth});
    }
    if (!found) {
      return {};
    }
    std::vector<Box> covers;
    for (const Seen& over : seen) {
      if (over.depth >= mapDepth) {
        covers.push_back(over.box);
      }
    }
    if (fafs != nullptr) {
      *fafs = covers;
    }
    {
      const std::lock_guard<std::mutex> lock(gCoversMutex);
      covers.insert(covers.end(), gCovers.begin(), gCovers.end());
    }
    return griffplace::FreeMap(view, std::move(covers));
  }

  struct Handle
  {
    bool shown = false;
    /// Not beside the group, which is out of view or under a panel: the handle points at it.
    bool off = false;
    float x = 0;
    float y = 0;
    float groupX = 0;
    float groupY = 0;
    int count = 0;
  };

  std::mutex gHandleMutex;
  Handle gHandle;
  griffplace::Cross gFree;

  /// The handle for the group whose units take `units` on the screen, about their middle (griffplace::Place).
  [[nodiscard]] Handle Place(
    const griffplace::FreeMap& free, const int count, const float middleX, const float middleY, const Box& units, const Box& view
  )
  {
    const griffplace::Sizes sizes{gTouchRadius.load(), gRadius.load() + gGap.load(), gArrowReach.load()};
    const griffplace::Spot spot = griffplace::Place(free, sizes, middleX, middleY, units, view);
    Handle handle;
    handle.shown = spot.shown;
    handle.off = spot.off;
    handle.x = spot.x;
    handle.y = spot.y;
    handle.groupX = middleX;
    handle.groupY = middleY;
    handle.count = count;
    return handle;
  }

  constexpr char kWantedHelp[] = "FafreGriffWanted() - whether the app's touch scheme Griff wants FafreGriffBox";
  constexpr char kBoxHelp[] =
    "FafreGriffBox(count, middleX, middleY, left, top, right, bottom, viewLeft, viewTop, viewRight, viewBottom) - "
    "the selection's units on the screen, for Griff's handle, and the map's view; count 0: nothing is selected";
  constexpr char kFrameHelp[] = "FafreGriffFrame() - a frame of the session's UI, for Griff's work after its paint";
  constexpr char kAnswerHelp[] = "FafreGriffAnswer(number) - griff.lua's answer to the app's question";
  constexpr char kEndedHelp[] = "FafreGriffEnded() - Griff's game is over: what it set of FAF's goes back";

  int Wanted(lua_State* const lua)
  {
    lua_pushboolean(lua, gOn.load() ? 1 : 0);
    return 1;
  }

  /// The selection as griff.lua reported it in this frame, for the handle once the frame is drawn. On the program's thread.
  struct Selection
  {
    int count = 0;
    float middleX = 0;
    float middleY = 0;
    Box units;
    /// The map's view; none when the report had none.
    bool viewKnown = false;
    Box view;
  };

  Selection gSelection;

  // --- The draft: plans at world places --------------------------------------------------------

  /// The draft, on the program's thread: from a build icon of FAF's construction panel (griff.lua BuildIconClicked) to the
  /// tick, the cross or the two fingers' tap.
  griffdraft::Draft gDraft;
  /// The blueprints of the draft's buildings by their ids, for FAF's own check of a place.
  std::unordered_map<std::string, moho::RUnitBlueprint*> gDraftBlueprints;
  /// What a finger shows of what it would add: the line from the newest plan, each place with whether it is free; and the plan
  /// a finger that rests would place, of the picked building, and whether its ground is taken there, where it shows as FAF's
  /// ghost does over ground its check refuses.
  std::vector<std::pair<griffdraft::Place, bool>> gDraftLine;
  bool gAimShown = false;
  griffdraft::Place gAim;
  bool gAimTaken = false;
  /// The finger that rests is on the map, not on FAF's UI: the loupe is for the map.
  bool gAimOnMap = false;
  /// The world place under the finger where the draft's move began.
  griffdraft::Place gMoveFrom;

  // --- Clicks at world places: the tick's plans, the undo's orders given again ---------------------------------------

  /// A step of the program's clicks, in a run: the clicks of a run go to the units selected for it (`select`, at the run's
  /// first step; empty: the selection stays). A click at a world place in FAF's mode for it (the call of griff.lua's that
  /// arms it), the run's first that FAF orders by replacing what the units do unless all are appended, the others with Shift
  /// held to the run's end; a building's (FAF's build mode, `builds`) lands in the cell of `building` at the place. Or the
  /// undo's: an `aimed` step comes a frame after its cursor went to its place and its mode was armed, once FAF's cursor knows
  /// what lies there (CursorAt): a click of the mode for its `way` with Shift or without (`shift`), on an entity (`target`, a
  /// unit: `onUnit`) only while FAF's cursor picks it, at the place it is then; or a removal, FAF's Shift+Ctrl right release
  /// on the waypoint of `command`, which takes it from its `units`, only while FAF's cursor hovers that very command.
  struct ClickStep
  {
    bool removal = false;
    std::string arm;
    griffundo::Place place;
    int run = 0;
    std::vector<int> select;
    bool builds = false;
    griffdraft::Building building;
    bool aimed = false;
    griffundo::Way way = griffundo::Way::kNone;
    bool shift = true;
    int target = griffundo::kNoTarget;
    bool onUnit = false;
    int command = 0;
    std::vector<int> units;
  };

  /// The clicks that run (ClickStepTask), one a task, so that each sees what the one before did, and what comes after the last
  /// of them (`finish`), also once the game is over. A place the free map does not show gets the camera first, as high and
  /// turned as it is, and its click after the next frame that drew it there; a building whose cell no pixel lands in gets it
  /// closer (`closer`, once a step). A place that no frame brought onto the free map, or that no pixel lands on then, is not
  /// clicked (`unreached`), nor is a step the clicks did not come to, as they ended without a frame, a game or after a throw.
  /// An aimed step waits for FAF's cursor at most kClickFrames frames (`aims`): a target it never picked is `missed`, a
  /// command it never hovered `unremoved`. The orders FAF's UI gave for a click are counted (FafreGriffOrdered), up to the
  /// next step, which comes after the click's input: a click that gave none was `refused`, as where the ground was taken
  /// meanwhile, one that gave an order on another entity than its target `missed`. A removal is checked in its units'
  /// queues after its input. Units of a run that FAF selected as another unit (a transport carrying them) are `leftOut`.
  /// How many releases and clicks were made (`acted`), and how many orders FAF's UI gave while the clicks ran (`orders`).
  struct Clicks
  {
    std::vector<ClickStep> steps;
    bool appendAll = false;
    void (*finish)() = nullptr;
    std::size_t next = 0;
    int run = -1;
    int orderedInRun = 0;
    bool holdsShift = false;
    bool looked = false;
    int waited = 0;
    bool closer = false;
    int aims = 0;
    int aimX = 0;
    int aimY = 0;
    bool counting = false;
    bool removing = false;
    int removedCommand = 0;
    std::vector<int> removedUnits;
    std::vector<int> released;
    std::int64_t releasedAt = 0;
    bool redrawn = false;
    bool lifted = false;
    int ordered = 0;
    int orderedTarget = griffundo::kNoTarget;
    int unreached = 0;
    int refused = 0;
    int missed = 0;
    int unremoved = 0;
    int leftOut = 0;
    int acted = 0;
    int orders = 0;
  };

  Clicks gClicks;
  /// The program's clicks run, the draft's tick's or the undo's: the fingers rest meanwhile (Game.griffClicking, ClicksRun),
  /// as a gesture's click between them would act with their selection, in their mode, or move the camera they aim by.
  std::atomic<bool> gClicking{false};
  /// How many runs of the program's clicks have begun (Game.griffClickRuns): a gesture one came into gives nothing more, also
  /// once they are over (TouchInput), as its input would act on what they did.
  std::atomic<int> gClickRuns{0};
  /// A step may wait for the next frame, in which FAF lays its panels out for the units just selected and the camera moves:
  /// the frames' number (griff.lua counts each frame of the session's UI, FafreGriffFrame), the one the step waits after, and
  /// since when it waits (0: it does not), for the clicks' patience.
  bool gClicksWait = false;
  std::uint32_t gFrame = 0;
  std::uint32_t gClicksAfterFrame = 0;
  std::atomic<std::int64_t> gClicksWaitSince{0};

  // --- What a gesture ordered: its undo ---------------------------------------------------------------------------------

  /// One of a unit's command queues as a gesture began, and what the gesture's orders did to it: its orders then (as FAF's UI
  /// held them, each command by its own id) and those of them the sim had not taken yet (`unsynced`), its newest edit as last
  /// seen (griffundo::TookOrder tells the queue an order went into by it), whether an order of the gesture's went into it,
  /// the commands they put there (`given`), and whether one replaced it (FAF's Clear). From the first that replaced it the sim
  /// runs the queue as it was until the sync reaches that order's beat (`command`, `beat`), a network delay after the input:
  /// what the sim holds of it is read each frame until then (`taken`, with the game's tick as of that read), and kept from
  /// then on (`landed`), also where the replacement left the queue empty (a Stop) or the sim gave orders of its own after it
  /// (the attack-move's AggressiveMove); whether the sim may have ticked between that read and the replacement's beat
  /// (`stale`, griffundo::Stale).
  struct Queue
  {
    std::vector<griffundo::Entry> before;
    std::vector<griffundo::Unsynced> unsynced;
    griffundo::Edit edit;
    bool ordered = false;
    std::vector<griffundo::Entry> given;
    bool replaced = false;
    bool snapshot = false;
    bool landed = false;
    int command = 0;
    moho::CmdId beat = 0;
    std::vector<griffundo::Entry> taken;
    int readTick = 0;
    bool stale = false;
  };

  /// A unit as a gesture began: its own queue, a factory's rally route apart (UserUnit::GetFactoryCommandQueue: the route the
  /// units it builds take on; its own holds what it builds), and the building it was building (its focus).
  struct Stored
  {
    Queue own;
    Queue route;
    int focus = griffundo::kNoTarget;
  };

  /// The gesture whose orders are watched (WatchOrders to EndWatch), by its number (0: none): the selection's units as it
  /// began (none for the tap ladder's attack-move, which `joins` the move's record and notes its orders there), whether it is
  /// the draft's tick, whose orders always offer their undo, whether its orders went to units it knew no queue of (not
  /// selected as it began), the units its Guards went to, the upgrades FAF ordered them for those Guards (AssistsUpgrade),
  /// whether it ordered at all, and the groups its orders took units from (GroupOrdered).
  struct Watch
  {
    int number = 0;
    bool joins = false;
    bool tick = false;
    std::unordered_map<int, Stored> stored;
    bool unknown = false;
    std::unordered_set<int> guarded;
    int upgrades = 0;
    bool any = false;
    std::vector<griffgroups::Earlier> earlier;
  };

  Watch gWatch;
  int gWatches = 0;

  /// What a record's undo puts back: a gesture's orders, or what a gesture of Griff's own threw away (Offer).
  using griffundo::Kept;

  /// The last gesture that ordered where its undo saves work (SavesWork), which the undo's button takes back: its number (0:
  /// none), whether the button asked for its
  /// undo, which comes once the sim took the gesture and no clicks run, its units as it began with what its orders did to
  /// their queues, whether it ordered units it knew no queue of, the units its Guards went to and the upgrades FAF ordered
  /// for them; the watches it holds (the tap ladder's attack-move joins the move's) and the groups their orders took units
  /// from, where an undo that takes all of it back puts them again (griffgroups::Book::Undone). One level: the next
  /// gesture's replaces it, and any other order ends it. A record of Griff's own holds what its gesture threw away instead
  /// (`kept`): the draft as it was, with its buildings' blueprints; a control group's number and the units it held.
  struct Record
  {
    int number = 0;
    Kept kept = Kept::kOrders;
    bool asked = false;
    std::unordered_map<int, Stored> stored;
    bool unknown = false;
    std::unordered_set<int> guarded;
    int upgrades = 0;
    std::vector<std::uint32_t> gestures;
    std::vector<griffgroups::Earlier> earlier;
    griffdraft::Draft draft;
    std::unordered_map<std::string, moho::RUnitBlueprint*> blueprints;
    int group = 0;
    std::vector<int> held;
  };

  Record gRecord;
  int gRecords = 0;

  /// The undo whose clicks run: its record, the orders it could not give again or take out, those it gave back or left as the
  /// units' own without knowing it (griffundo::Plan::unsure), and the targets of the Guards it gives back, for which FAF's
  /// assist-to-upgrade may order an upgrade as it did for the units' own Guards (AssistsUpgrade).
  struct Undoing
  {
    Record record;
    int dropped = 0;
    int unsure = 0;
    std::unordered_set<int> again;
  };

  // Game's UNDONE_WHOLE, UNDONE_IN_PART and NOT_UNDONE: what SAID_UNDONE says an undo did.
  constexpr int kUndoneWhole = 0;
  constexpr int kUndoneInPart = 1;
  constexpr int kNotUndone = 2;

  Undoing gUndoing;

  /// The undos whose clicks are over in the game's last few ticks, for FAF's assist-to-upgrade, which may order the upgrade
  /// of a target of one of their Guards only after they said what they did (griffundo::Undones).
  griffundo::Undones gUndone;

  /// The commands of the Guards FAF's UI gave while Ctrl was down, in this game: FAF copies the queue of an engineer such a
  /// Guard targets into the guarding units' (commandmode.lua OnGuardCopy, by its option assist_to_copy_command_queue), which
  /// no finger and no click of the program's holds, a keyboard's may (griffundo::Entry::answered).
  std::unordered_set<int> gGuardsWithCtrl;

  [[nodiscard]] std::int64_t Now()
  {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  }


  /// What the activity reads of the draft (Game.griffDraft), as the last frame drew it.
  std::mutex gDraftShownMutex;
  std::vector<float> gDraftShown{0};

  // Game.DRAFT_ values: a plan's flags, and how many numbers come before the plans and for each.
  constexpr float kPlanNewest = 1;
  constexpr float kPlanOnDeposit = 2;
  constexpr float kPlanTaken = 4;
  constexpr float kPlanFree = 8;
  constexpr std::size_t kDraftHead = 6;
  constexpr std::size_t kDraftItem = 8;

  /// A map's view and its place on the picture: plans are projected through its camera, a finger's place unprojected.
  struct MapView
  {
    moho::CUIWorldView* view = nullptr;
    Box box;
  };

  [[nodiscard]] bool MapOf(moho::CUIWorldView* const view, MapView& map)
  {
    if (view == nullptr || view->IsMiniMap() || view->GetCamera() == nullptr || !LaidOutBox(view, map.box)) {
      return false;
    }
    map.view = view;
    return true;
  }

  /// The map a finger at this place is on: none where FAF's UI or the minimap takes it.
  [[nodiscard]] bool MapAt(const int x, const int y, MapView& map)
  {
    return MapOf(WorldViewAt(static_cast<float>(x), static_cast<float>(y)), map);
  }

  /// The main map of the reported view, whatever lies over a finger's place: a line or a move goes on under a panel.
  [[nodiscard]] bool MainMap(MapView& map)
  {
    const Selection selection = gSelection;
    moho::CMauiControl* const root = UiRoot();
    if (!selection.viewKnown || root == nullptr) {
      return false;
    }
    const float middleX = (selection.view.left + selection.view.right) / 2;
    const float middleY = (selection.view.top + selection.view.bottom) / 2;
    for (moho::CMauiControl* control = root; control != nullptr; control = control->DepthFirstSuccessor(root)) {
      Box box;
      if (auto* const view = dynamic_cast<moho::CUIWorldView*>(control);
          view != nullptr && !view->IsMiniMap() && LaidOutBox(view, box) && box.Contains(middleX, middleY)) {
        return MapOf(view, map);
      }
    }
    return false;
  }

  /// A world place on the picture through the map's camera as it is (CUIWorldView:Project, which counts from the map's corner).
  [[nodiscard]] bool Project(const MapView& map, const float x, const float y, const float z, float& screenX, float& screenY)
  {
    const Wm3::Vector2f at = map.view->GetCamera()->CameraGetView().Project(
      Wm3::Vector3f(x, y, z), 0, map.box.right - map.box.left, map.box.bottom - map.box.top, 0
    );
    screenX = map.box.left + at.x;
    screenY = map.box.top + at.y;
    return std::isfinite(screenX) && std::isfinite(screenY);
  }

  /// The world place a click on the picture there lands on (UnProject, as FAF's own drags use it); false where none.
  [[nodiscard]] bool Unproject(const MapView& map, const int screenX, const int screenY, griffdraft::Place& place)
  {
    const Wm3::Vector3f at =
      map.view->GetCamera()->CameraScreenToSurface(Wm3::Vector2f(static_cast<float>(screenX), static_cast<float>(screenY)));
    if (!std::isfinite(at.x) || !std::isfinite(at.z)) {
      return false;
    }
    place = griffdraft::Place{at.x, at.z};
    return true;
  }

  /// The height of what a click there lands on: the ground, or the water over it.
  [[nodiscard]] float SurfaceAt(const float x, const float z)
  {
    moho::CWldSession* const session = moho::WLD_GetActiveSession();
    if (session == nullptr || session->mWldMap == nullptr || session->mWldMap->mTerrainRes == nullptr) {
      return 0;
    }
    moho::IWldTerrainRes* const terrain = session->mWldMap->mTerrainRes;
    const float ground = terrain->GetHeightField()->GetElevation(x, z);
    return terrain->IsWaterEnabled() ? (std::max)(ground, terrain->GetWaterElevation()) : ground;
  }

  /// FAF's own check of a building's place, the one its ghost turns red by: the structures and queued buildings it sees there
  /// and the ground (USERUNIT_CanBeBuiltAt). Where it says no, FAF's click there orders nothing.
  [[nodiscard]] bool GroundFree(const griffdraft::Building& building, const float x, const float z)
  {
    moho::CWldSession* const session = moho::WLD_GetActiveSession();
    const auto blueprint = gDraftBlueprints.find(building.id);
    if (session == nullptr || blueprint == gDraftBlueprints.end() || blueprint->second == nullptr) {
      return false;
    }
    moho::SOccupationResult occupation{};
    return moho::USERUNIT_CanBeBuiltAt(*session, blueprint->second, moho::SCoordsVec2{x, z}, false, &occupation, nullptr);
  }

  /// The deposits a building of this kind stands on, as FAF's build check knows them, in FAF's order: their middles, not yet
  /// on the picture; none for a building that needs none.
  /// The walk reads the sim's list without CSimResources' lock, as the engine's own UI does (FindClosestDeposit for an
  /// extractor's ghost at each move of the mouse, GetDepositsAroundPoint): the sim's thread adds a deposit under that lock
  /// (AddDeposit), and the list that grows meanwhile may move, under this walk as under theirs. FAF's own Lua adds them all
  /// once, as the session begins (ScenarioUtilities CreateResources, from the map's markers); later only a map's or a mod's
  /// script can, by CreateResourceDeposit (FAF's MarkerUtilities hooks it for them), as the adaptive maps' Crazyrush option
  /// does with each extractor built (loc adaptive_crazyrush_help: new mass points beside it). Nothing removes one. So it can
  /// bite only on such a map, while a finger aims, taps or boxes for an extractor, or the tick clicks one, as a deposit comes.
  [[nodiscard]] std::vector<griffdraft::Candidate> DepositsOf(const griffdraft::Deposit kind)
  {
    std::vector<griffdraft::Candidate> candidates;
    moho::CWldSession* const session = moho::WLD_GetActiveSession();
    if (session == nullptr || session->mSimResources.px == nullptr || kind == griffdraft::Deposit::kNone) {
      return candidates;
    }
    const moho::EDepositType type = kind == griffdraft::Deposit::kMass ? moho::kMass : moho::kHydrocarbon;
    // As USERUNIT_CanBeBuiltAt reaches them.
    const auto* const resources = reinterpret_cast<const moho::ISimResources*>(session->mSimResources.px);
    for (const moho::ResourceDeposit& deposit : resources->GetDeposits()) {
      if (deposit.depositType != type) {
        continue;
      }
      griffdraft::Candidate candidate;
      candidate.place = griffdraft::Place{static_cast<float>(deposit.footprintRect.x0 + deposit.footprintRect.x1) * 0.5f,
                                          static_cast<float>(deposit.footprintRect.z0 + deposit.footprintRect.z1) * 0.5f};
      candidates.push_back(candidate);
    }
    return candidates;
  }

  /// The deposits a building of this kind stands on, with their middles on the picture.
  [[nodiscard]] std::vector<griffdraft::Candidate> Deposits(const MapView& map, const griffdraft::Deposit kind)
  {
    std::vector<griffdraft::Candidate> candidates;
    for (griffdraft::Candidate& candidate : DepositsOf(kind)) {
      const float x = candidate.place.x;
      const float z = candidate.place.z;
      if (Project(map, x, SurfaceAt(x, z), z, candidate.screenX, candidate.screenY)) {
        candidates.push_back(candidate);
      }
    }
    return candidates;
  }

  /// A building's place for the activity: its middle on the picture and the box its skirt's corners take there, with its
  /// number and flags (kDraftItem numbers).
  void Show(std::vector<float>& shown, const MapView& map, const griffdraft::Building& building, const griffdraft::Place& at,
            const float id, const float flags)
  {
    const float y = SurfaceAt(at.x, at.z);
    float x = 0;
    float screenY = 0;
    if (!Project(map, at.x, y, at.z, x, screenY)) {
      return;
    }
    float left = x;
    float top = screenY;
    float right = x;
    float bottom = screenY;
    const griffdraft::Rect skirt = griffdraft::SkirtRect(building, at.x, at.z);
    const std::pair<float, float> corners[] = {{skirt.x0, skirt.z0}, {skirt.x1, skirt.z0}, {skirt.x0, skirt.z1}, {skirt.x1, skirt.z1}};
    for (const auto& [cornerX, cornerZ] : corners) {
      float cornerScreenX = 0;
      float cornerScreenY = 0;
      if (Project(map, cornerX, y, cornerZ, cornerScreenX, cornerScreenY)) {
        left = (std::min)(left, cornerScreenX);
        right = (std::max)(right, cornerScreenX);
        top = (std::min)(top, cornerScreenY);
        bottom = (std::max)(bottom, cornerScreenY);
      }
    }
    shown.insert(shown.end(), {id, x, screenY, left, top, right, bottom, flags});
  }

  /// The draft as this frame drew it, for the activity (Game.griffDraft), which knows a finger on a plan by it: whether it is
  /// open, whether its building stands on a deposit, how many plans, line places and aims follow, whether a resting finger is
  /// on the map, then each, on the picture. FAF's UI draws them (fafre/griffmap.lua, FafreGriffPlans).
  void PublishDraft(const griffplace::FreeMap& free, moho::CUIWorldView* const mainMap)
  {
    std::vector<float> shown{0};
    MapView map;
    if (gDraft.Open()) {
      shown = {1, gDraft.Picked().deposit != griffdraft::Deposit::kNone ? 1.0f : 0.0f, 0, 0, 0, gAimOnMap ? 1.0f : 0.0f};
    }
    if (gDraft.Open() && MapOf(mainMap, map)) {
      const std::vector<griffdraft::Plan>& plans = gDraft.Plans();
      for (std::size_t at = 0; at < plans.size(); ++at) {
        const griffdraft::Plan& plan = plans[at];
        const griffdraft::Building& building = gDraft.BuildingOf(plan);
        float flags = at + 1 == plans.size() ? kPlanNewest : 0;
        flags += building.deposit != griffdraft::Deposit::kNone ? kPlanOnDeposit : 0;
        flags += gDraft.Taken(plan, GroundFree) ? kPlanTaken : 0;
        const std::size_t before = shown.size();
        Show(shown, map, building, griffdraft::Place{plan.x, plan.z}, static_cast<float>(plan.id), flags);
        // Its middle on the free map: a finger there is on the plan, not on a panel over it.
        if (shown.size() > before && free.Free(shown[before + 1], shown[before + 2])) {
          shown.back() += kPlanFree;
        }
      }
      shown[2] = static_cast<float>((shown.size() - kDraftHead) / kDraftItem);
      const std::size_t lineFrom = shown.size();
      if (const griffdraft::Plan* const start = gDraft.LineStart(); start != nullptr) {
        for (const auto& [place, groundFree] : gDraftLine) {
          Show(shown, map, gDraft.BuildingOf(*start), place, 0, groundFree ? 0 : kPlanTaken);
        }
      }
      shown[3] = static_cast<float>((shown.size() - lineFrom) / kDraftItem);
      if (gAimShown) {
        const std::size_t aimFrom = shown.size();
        Show(shown, map, gDraft.Picked(), gAim, 0, gAimTaken ? kPlanTaken : 0);
        shown[4] = shown.size() > aimFrom ? 1.0f : 0.0f;
      }
    }
    const std::lock_guard<std::mutex> lock(gDraftShownMutex);
    gDraftShown = std::move(shown);
  }

  // Game.PLAN_ flags of a place FafreGriffPlans gives griffmap.lua, which draws it as the ghost of FAF's build preview.
  constexpr int kGhostNewest = 1;
  constexpr int kGhostTaken = 2;
  constexpr int kGhostLine = 4;
  constexpr int kGhostAim = 8;

  /// What the draft shows, while it is open: each plan, the line a finger draws from the newest and the plan a resting finger
  /// would place, each `visit`ed with its building, its world place and its flags: the newest, on ground that is taken (another
  /// plan's, or FAF's check), a line's, the aim.
  template <typename Visit>
  void EachShown(Visit&& visit)
  {
    const std::vector<griffdraft::Plan>& plans = gDraft.Plans();
    for (std::size_t at = 0; at < plans.size(); ++at) {
      const griffdraft::Plan& plan = plans[at];
      const int flags = (at + 1 == plans.size() ? kGhostNewest : 0) + (gDraft.Taken(plan, GroundFree) ? kGhostTaken : 0);
      visit(gDraft.BuildingOf(plan), griffdraft::Place{plan.x, plan.z}, flags);
    }
    if (const griffdraft::Plan* const start = gDraft.LineStart(); start != nullptr) {
      for (const auto& [place, groundFree] : gDraftLine) {
        visit(gDraft.BuildingOf(*start), place, kGhostLine + (groundFree ? 0 : kGhostTaken));
      }
    }
    if (gAimShown) {
      visit(gDraft.Picked(), gAim, kGhostAim + (gAimTaken ? kGhostTaken : 0));
    }
  }

  constexpr char kPlansHelp[] = "FafreGriffPlans(places, ids) - Griff's draft for its ghosts: x, y, z and flags of each plan, line "
                                "place and aim into places, their buildings' ids into ids; how many";
  constexpr char kGhostHelp[] = "FafreGriffGhost(blueprintId) - the mesh of a building of Griff's draft and its scale, for its ghost";

  /// The draft as griffmap.lua draws it, each frame of the session's UI: what it shows (EachShown), at its place in the world
  /// (on the ground or the water over it), its building's id apart, and its flags. Nothing without a draft.
  int Plans(lua_State* const lua)
  {
    int count = 0;
    if (gDraft.Open() && lua_istable(lua, 1) && lua_istable(lua, 2)) {
      EachShown([lua, &count](const griffdraft::Building& building, const griffdraft::Place& at, const int flags) {
        ++count;
        const float values[] = {at.x, SurfaceAt(at.x, at.z), at.z, static_cast<float>(flags)};
        for (int value = 0; value < 4; ++value) {
          lua_pushnumber(lua, values[value]);
          lua_rawseti(lua, 1, 4 * (count - 1) + value + 1);
        }
        lua_pushstring(lua, building.id.c_str());
        lua_rawseti(lua, 2, count);
      });
    }
    lua_pushnumber(lua, static_cast<lua_Number>(count));
    return 1;
  }

  /// A blueprint of the draft's by its building's id; none for a building the draft does not hold.
  [[nodiscard]] moho::RUnitBlueprint* DraftBlueprint(const std::string& id)
  {
    const auto blueprint = gDraftBlueprints.find(id);
    return blueprint != gDraftBlueprints.end() ? blueprint->second : nullptr;
  }

  /// A building's mesh, as FAF's build preview takes it (CBuildDragPreview): none where it has no level of detail.
  [[nodiscard]] moho::RMeshBlueprint* MeshOf(const moho::RUnitBlueprint* const blueprint)
  {
    moho::CWldSession* const session = moho::WLD_GetActiveSession();
    if (session == nullptr || session->mRules == nullptr || blueprint == nullptr) {
      return nullptr;
    }
    moho::RMeshBlueprint* const mesh = session->mRules->GetMeshBlueprint(blueprint->Display.MeshBlueprint);
    return mesh != nullptr && mesh->mLods.begin() != mesh->mLods.end() ? mesh : nullptr;
  }

  /// A building of the draft's for its ghost: the mesh of its top level of detail and the building's scale, as FAF's build
  /// preview takes them; nothing for a building the draft does not hold.
  int Ghost(lua_State* const lua)
  {
    const char* const id = lua_tostring(lua, 1);
    moho::RUnitBlueprint* const blueprint = id != nullptr ? DraftBlueprint(id) : nullptr;
    moho::RMeshBlueprint* const mesh = MeshOf(blueprint);
    if (mesh == nullptr) {
      return 0;
    }
    lua_pushstring(lua, mesh->mLods.begin()->mMeshName.c_str());
    lua_pushnumber(lua, blueprint->Display.UniformScale);
    return 2;
  }

  /// The draft for the build mode's skirts as the map's view draws (griffskirts::gDraft): the picked building's mesh and what
  /// the draft shows (EachShown), each in FAF's build preview's colour for the ground it takes or for ground that is taken.
  bool DraftSkirts(griffskirts::Draft& skirts)
  {
    if (!gDraft.Open()) {
      return false;
    }
    skirts.mesh = MeshOf(DraftBlueprint(gDraft.Picked().id));
    EachShown([&skirts](const griffdraft::Building& building, const griffdraft::Place& at, const int flags) {
      if (const moho::RUnitBlueprint* const blueprint = DraftBlueprint(building.id); blueprint != nullptr) {
        const std::uint32_t color = (flags & kGhostTaken) != 0 ? griffskirts::kRefused : griffskirts::kFree;
        skirts.buildings.push_back(griffskirts::Building{blueprint, at.x, SurfaceAt(at.x, at.z), at.z, color});
      }
    });
    return true;
  }

  /// The map's view asks for the draft's skirts from the program's start; without a draft it gets none.
  [[maybe_unused]] const bool gSkirtsAsked = (griffskirts::gDraft = &DraftSkirts, true);

  constexpr char kWaypointHelp[] = "FafreGriffWaypoint(x, y, z, units) - as FAF's command graph draws an order of so many "
                                   "units at this world place on the main map: its waypoint's half side and its order line's "
                                   "width, in world units";

  /// What FAF's command graph draws at a world place for an order of `units` selected units, one waypoint after another,
  /// so that Griff's stroke shows its route as FAF will once it is ordered (fafre/griffmap.lua): the waypoint's half side
  /// (UICommandGraph's waypoint draw: the root of its busier lane's units, each order line counting for no more than
  /// ui_CommandGraphMaxNodeUnits, FAF's 1, over the camera's perspective width there, its viewport's row 2, within
  /// ui_MinWaypointSize and ui_MaxWaypointSize of those widths, at a selected waypoint's scale of 1) and the order line's width
  /// (DrawCommandOrderline: the root of the count up to 10, commandwaypoint.lua's CalculateWaypointLineWidth, and two of those
  /// widths, by ui_WaypointLineScale). Nothing without the main map.
  int Waypoint(lua_State* const lua)
  {
    MapView map;
    if (!MainMap(map)) {
      return 0;
    }
    const Wm3::Vector3f place{static_cast<float>(lua_tonumber(lua, 1)), static_cast<float>(lua_tonumber(lua, 2)),
                              static_cast<float>(lua_tonumber(lua, 3))};
    const float units = (std::max)(1.0f, static_cast<float>(lua_tonumber(lua, 4)));
    const float laneUnits = (std::min)(units, static_cast<float>(moho::ui_CommandGraphMaxNodeUnits));
    const float width = map.view->GetCamera()->CameraGetView().viewport.ProjectViewportWidthRow2(place);
    if (!(width > 0)) {
      return 0;
    }
    lua_pushnumber(lua, std::clamp(std::sqrt(laneUnits) / width, moho::ui_MinWaypointSize, moho::ui_MaxWaypointSize) * width);
    lua_pushnumber(lua, ((std::min)(std::sqrt(units), 10.0f) + 2 * width) * moho::ui_WaypointLineScale);
    return 2;
  }

  moho::CScrLuaBinder gPlansBinder(moho::SCR_UserLuaInitSet(), "FafreGriffPlans", &Plans, nullptr, "<global>", kPlansHelp);
  moho::CScrLuaBinder gGhostBinder(moho::SCR_UserLuaInitSet(), "FafreGriffGhost", &Ghost, nullptr, "<global>", kGhostHelp);
  moho::CScrLuaBinder
    gWaypointBinder(moho::SCR_UserLuaInitSet(), "FafreGriffWaypoint", &Waypoint, nullptr, "<global>", kWaypointHelp);

  void ClickStepTask(WPARAM);
  void TryUndo();
  void DraftCommitTask(WPARAM plus);
  void ForgetRest();
  void ForgetGroups();
  [[nodiscard]] bool ClicksRun();
  void ReadTaken(std::unordered_map<int, Stored>& stored);
  void DropRecord();
  void EndRecord();
  [[nodiscard]] bool Outwaited(std::int64_t since, std::int64_t now);

  /// A tick that waits until no clicks run and no finger's drag holds FAF's dragger (DraftCommitTask): whether Plus counted
  /// for it, plus one; 0: none.
  WPARAM gCommitAsked = 0;

  /// FAF's Distribute orders that waits for a frame drawn with the cursor at the group's middle (DistributeTask): where the
  /// cursor is, the frame it waits after, and since when (0: none waits). Meanwhile Griff holds the cursor, and the pointer's
  /// leave that the handle's lift asked for waits (hostclicks::GriffHoldsCursor).
  struct Distributing
  {
    POINT cursor{};
    std::uint32_t afterFrame = 0;
    std::int64_t since = 0;
  };

  Distributing gDistributing;
  void DistributeAfterFrame();

  /// No Distribute orders waits for its frame any more: the pointer's leave that waited for it comes now.
  void StopDistributing()
  {
    gDistributing = Distributing{};
    hostclicks::CursorFreed();
  }

  /// How FAF's panels along the view's edges are told (cameraclear::Rules), the pixels as parts of the view's height: the
  /// pieces of a panel overlap by more than 0.4 % (3 of the phone's 768 lines, where FAF's unit view lies 2 of them on its
  /// orders panel); a panel stands at an edge within 1 %, its brackets reach 2 % past it, and a panel covers at most half of
  /// the view.
  constexpr float kPanelOverlap = 0.004f;
  constexpr float kPanelTouch = 0.01f;
  constexpr float kPanelPast = 0.02f;
  constexpr float kPanelMost = 0.5f;
  /// Frames between the measures of the panels' margins, each a walk over the UI of its own: they change only as panels come.
  constexpr std::uint32_t kMarginFrames = 30;
  std::uint32_t gMarginsFrame = 0;

  /// What the game's panels along the main map's view's edges cover of it, for the world camera's clamp, which lets each edge
  /// of the map come out from under them (include/CameraClear.h, the camera's patch): the panels that FAF's controls over the
  /// map (FreeMapOver's) make up and that stand at an edge of the view, not what the player opened (fafre/opened.lua: windows,
  /// dialogs, the multifunction panel's dropouts, the tabs' content), and of all that showed in this game the larger margin
  /// at each edge. The panels come and go with the selection, and a margin that shrank would pull the camera back from an
  /// edge it went to.
  void MeasureMargins(const Box& view, const std::vector<Box>& fafs)
  {
    std::vector<float> opened;
    {
      const std::lock_guard<std::mutex> lock(gOpenedMutex);
      opened = gOpened;
    }
    const auto inOpened = [&opened](const Box& box) {
      for (std::size_t at = 0; at + 3 < opened.size(); at += 4) {
        if (box.left >= opened[at] && box.top >= opened[at + 1] && box.right <= opened[at + 2] && box.bottom <= opened[at + 3]) {
          return true;
        }
      }
      return false;
    };
    std::vector<cameraclear::Rect> pieces;
    for (const Box& box : fafs) {
      if (!inOpened(box)) {
        pieces.push_back(cameraclear::Rect{box.left, box.top, box.right, box.bottom});
      }
    }
    const float height = view.bottom - view.top;
    const cameraclear::Rules rules{kPanelOverlap * height, kPanelTouch * height, kPanelPast * height, kPanelMost};
    const cameraclear::Margins now =
      cameraclear::Measure(pieces, cameraclear::Rect{view.left, view.top, view.right, view.bottom}, rules);
    cameraclear::gWorldCamera = cameraclear::Larger(cameraclear::gWorldCamera, now);
    gMarginsFrame = gFrame;
  }

  // --- Griff's Esc: the cross -------------------------------------------------------------------------

  /// Whether units are selected, as FAF's GetSelectedUnits has them.
  [[nodiscard]] bool Selected()
  {
    moho::CWldSession* const session = moho::WLD_GetActiveSession();
    if (session == nullptr) {
      return false;
    }
    msvc8::vector<moho::UserUnit*> units;
    session->GetSelectionUnits(units);
    return !units.empty();
  }

  /// What Griff's Esc, the cross, puts away now (grifftap::EscapeFor): the draft, an armed command mode, the selection.
  [[nodiscard]] grifftap::Escape EscapeNow()
  {
    return grifftap::EscapeFor(gDraft.Open(), CommandModeArmed(), Selected());
  }

  /// Whether the cross shows, as each frame's work after its paint finds Esc (EscapeNow): griffhud.lua draws it by this
  /// (FafreGriffEscapes) and the activity lays the view that takes its finger (Game.griffEscapes).
  std::atomic<bool> gEscapes{false};

  /// The handle for the reported selection, and the free map's cross, after the frame's drawing: FAF's Lua changes layouts
  /// in its frame, after which they are not known until the drawing works them out (unitview.lua places its panel anew each
  /// frame), and the free map reads only what is worked out (LaidOutBox). Without a selection there is no handle and no walk,
  /// which costs a frame up to a ms, but each kMarginFrames frames one for the panels' margins (MeasureMargins). While a draft
  /// is open there is no handle (the mockup's: no stroke starts), and the draft is shown each frame, as the camera moves its
  /// plans; without the map's view its plans have no place on the picture, and none shows rather than where an earlier frame
  /// drew them. What the sim holds of the queues a gesture's orders replaced is read until their replacement landed. An undo
  /// the button asked for and a tick that wait for the sim, clicks or a finger's drag come now, the undo first, which was asked
  /// for the gesture before the tick.
  void PlaceTask(const WPARAM)
  {
    // The clicks' next step once a frame after the one it waits after is drawn: this task comes after each frame's paint, and
    // one of the frame the step began in may still wait.
    if (gClicking && gClicksWait && gFrame != gClicksAfterFrame) {
      gClicksWait = false;
      gClicksWaitSince = 0;
      Win32Android_RunOnWindowThread(&ClickStepTask, 0);
    }
    const Selection selection = gSelection;
    try {
      if (gDistributing.since != 0 && gFrame != gDistributing.afterFrame) {
        DistributeAfterFrame();
      }
      ReadTaken(gWatch.stored);
      ReadTaken(gRecord.stored);
      if (gRecord.asked) {
        TryUndo();
      }
      if (gCommitAsked != 0) {
        DraftCommitTask(gCommitAsked - 1);
      }
      gEscapes = moho::UI_GamePlaying() && EscapeNow() != grifftap::Escape::kNothing;
      const bool drafting = gDraft.Open();
      if (selection.viewKnown && gFrame - gMarginsFrame >= kMarginFrames) {
        std::vector<Box> panels;
        (void)FreeMapOver(selection.view, nullptr, &panels);
        MeasureMargins(selection.view, panels);
      }
      if (!selection.viewKnown || (!drafting && selection.count <= 0)) {
        if (selection.count <= 0 || drafting) {
          const std::lock_guard<std::mutex> lock(gHandleMutex);
          gHandle = Handle{};
        }
        if (drafting) {
          PublishDraft(griffplace::FreeMap{}, nullptr);
        }
        return;
      }
      moho::CUIWorldView* mainMap = nullptr;
      const griffplace::FreeMap free = FreeMapOver(selection.view, &mainMap);
      const griffplace::Cross cross = griffplace::CrossAt(free, selection.view);
      const Handle handle = selection.count > 0 && !drafting
                            ? Place(free, selection.count, selection.middleX, selection.middleY, selection.units, selection.view)
                            : Handle{};
      if (drafting) {
        PublishDraft(free, mainMap);
      }
      const std::lock_guard<std::mutex> lock(gHandleMutex);
      gHandle = handle;
      gFree = cross;
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
      gEscapes = false;
      const std::lock_guard<std::mutex> lock(gHandleMutex);
      gHandle = Handle{};
    }
  }

  int ReportBox(lua_State* const lua)
  {
    const auto number = [lua](const int index) { return static_cast<float>(lua_tonumber(lua, index)); };
    Selection selection;
    if (lua_gettop(lua) >= 11) {
      selection.count = static_cast<int>(lua_tonumber(lua, 1));
      selection.middleX = number(2);
      selection.middleY = number(3);
      selection.units = Box{number(4), number(5), number(6), number(7)};
      selection.viewKnown = true;
      selection.view = Box{number(8), number(9), number(10), number(11)};
    }
    gSelection = selection;
    return 0;
  }

  /// A frame of the session's UI (griff.lua, first each frame and apart from its report, which an error stops for the game):
  /// the clicks' steps wait for frames by their number, so they never wait on the report. The frame a step waits for has
  /// come once its number is past the one the step waits after: the clicks' patience is for time without a frame, and stops
  /// here, also where the frame's paint takes long; the step goes on after the paint (PlaceTask).
  int Frame(lua_State*)
  {
    ++gFrame;
    if (gClicksWait && gFrame != gClicksAfterFrame) {
      gClicksWaitSince = 0;
    }
    // The frame's Lua runs before its paint, in which the program draws it (CScApp::Main asks for the paint).
    Win32Android_RunWhenIdle(&PlaceTask, 0);
    return 0;
  }

  /// What griff.lua answered last: the program's thread asks it and waits (Ask).
  int gAnswer = 0;

  int Answer(lua_State* const lua)
  {
    gAnswer = static_cast<int>(lua_tonumber(lua, 1));
    return 0;
  }

  // --- FAF's reach for a unit beside a tap ------------------------------------------------------

  /// What Griff set FAF's ui_SelectTolerance to last, the half side of the square about the cursor in which the world view
  /// picks a unit, and what it was before: the player's, as FAF's options set it, which goes back when Griff's game ends.
  bool gToleranceSet = false;
  float gTolerance = 0;
  float gPlayersTolerance = 0;

  /// The console's command for the reach, in digits that it reads back as this very float: so the program knows its own value
  /// at the next down, and the player's goes back as it was.
  void ExecuteTolerance(const float value)
  {
    moho::CON_Execute(("ui_SelectTolerance " + grifftap::FloatText(value)).c_str());
  }

  /// FAF's own console variable, as a PC player sets it, and only when it changes.
  void SetTolerance(const float wanted)
  {
    const float now = moho::ui_SelectTolerance;
    if (now == wanted) {
      return;
    }
    // Set by someone else since Griff's last: the player's, or FAF's own as it leaves its reclaim mode.
    if (!gToleranceSet || now != gTolerance) {
      gPlayersTolerance = now;
    }
    gToleranceSet = true;
    gTolerance = wanted;
    ExecuteTolerance(wanted);
  }

  /// No draft any more on the program's side: its plans, its buildings, what a finger showed of it; the activity hears it with
  /// the next read. The tick's clicks, which hold what they click, end by themselves.
  void ForgetDraft()
  {
    gDraft.Close();
    gDraftBlueprints.clear();
    gDraftLine.clear();
    gAimShown = false;
    gAimOnMap = false;
    const std::lock_guard<std::mutex> lock(gDraftShownMutex);
    gDraftShown = {0};
  }

  int Ended(lua_State*)
  {
    if (gToleranceSet && moho::ui_SelectTolerance == gTolerance) {
      ExecuteTolerance(gPlayersTolerance);
    }
    gToleranceSet = false;
    ForgetDraft();
    gEscapes = false;
    {
      const std::lock_guard<std::mutex> lock(gOpenedMutex);
      gOpened.clear();
    }
    // A rest's mark and its Shift go with the game, as does a Distribute orders that waits for a frame.
    ForgetRest();
    StopDistributing();
    // The orders of this game: no watch, record or undo button any more; an undo the button asked for that waits is not done.
    gWatch = Watch{};
    DropRecord();
    gUndone.Clear();
    gGuardsWithCtrl.clear();
    // Its groups, and the camera's room past the map's edges, which the next game measures anew.
    ForgetGroups();
    cameraclear::gWorldCamera = cameraclear::Margins{};
    // Clicks that wait for a frame get none any more: their next step ends them, Shift too.
    if (gClicking) {
      gClicksWait = false;
      gClicksWaitSince = 0;
      Win32Android_RunOnWindowThread(&ClickStepTask, 0);
    }
    return 0;
  }

  // --- The draft's binders: what griff.lua tells it and asks of it ---------------------------------

  constexpr char kDraftHelp[] = "FafreGriffDraft(blueprintId) - Griff's draft opens with this building, or takes it for its next plans";
  constexpr char kDraftPlansHelp[] = "FafreGriffDraftPlans() - how many plans Griff's draft holds; -1 without a draft";
  constexpr char kDraftRefusedHelp[] = "FafreGriffDraftRefused() - an order mode was refused while the draft has plans: the app says so";
  constexpr char kDraftEndHelp[] = "FafreGriffDraftEnd() - Griff's empty draft ends, as an order mode is armed";

  /// What goes into a console command when the draft is ticked (StartBuild): FAF's ids are letters, digits and underscores.
  [[nodiscard]] bool PlainId(const char* const id)
  {
    if (id == nullptr || *id == 0) {
      return false;
    }
    for (const char* at = id; *at != 0; ++at) {
      const char letter = *at;
      if (!((letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9') || letter == '_')) {
        return false;
      }
    }
    return true;
  }

  /// What the draft and a building's click take of the blueprint `id`: its footprint, its skirt, the deposit it wants and
  /// whether FAF's click drags it (the rules' DRAGBUILD category, as UIBuildDragger::ReleaseDrag asks it); the blueprint
  /// itself, or none where the session has no such blueprint.
  [[nodiscard]] moho::RUnitBlueprint* BlueprintBuilding(const char* const id, griffdraft::Building& building)
  {
    moho::CWldSession* const session = moho::WLD_GetActiveSession();
    if (!PlainId(id) || session == nullptr || session->mRules == nullptr) {
      return nullptr;
    }
    moho::RResId resource{};
    (void)gpg::STR_SetFilename(&resource.name, gpg::StrArg(id));
    moho::RUnitBlueprint* const blueprint = session->mRules->GetUnitBlueprint(resource);
    if (blueprint == nullptr) {
      return nullptr;
    }
    building.id = id;
    building.footprintX = (std::max)(1, static_cast<int>(blueprint->mFootprint.mSizeX));
    building.footprintZ = (std::max)(1, static_cast<int>(blueprint->mFootprint.mSizeZ));
    building.skirtOffsetX = blueprint->Physics.SkirtOffsetX;
    building.skirtOffsetZ = blueprint->Physics.SkirtOffsetZ;
    building.skirtSizeX = blueprint->Physics.SkirtSizeX;
    building.skirtSizeZ = blueprint->Physics.SkirtSizeZ;
    switch (blueprint->Physics.BuildRestriction) {
      case moho::RULEUBR_OnMassDeposit:
        building.deposit = griffdraft::Deposit::kMass;
        break;
      case moho::RULEUBR_OnHydrocarbonDeposit:
        building.deposit = griffdraft::Deposit::kHydrocarbon;
        break;
      default:
        building.deposit = griffdraft::Deposit::kNone;
        break;
    }
    const moho::EntityCategorySet* const dragged = session->mRules->GetEntityCategory("DRAGBUILD");
    building.drags = dragged != nullptr && dragged->ContainsBit(blueprint->mCategoryBitIndex);
    return blueprint;
  }

  /// The draft is open with this building picked: the next frame shows it, but a finger that comes before needs to know at once.
  void ShowDraftOpen(const griffdraft::Building& picked)
  {
    const float deposit = picked.deposit != griffdraft::Deposit::kNone ? 1.0f : 0.0f;
    const std::lock_guard<std::mutex> lock(gDraftShownMutex);
    if (gDraftShown.empty() || gDraftShown[0] == 0) {
      gDraftShown = {1, deposit, 0, 0, 0, 0};
    } else {
      gDraftShown[1] = deposit;
    }
  }

  /// A building of FAF's construction panel goes into the draft (griff.lua BuildIconClicked), not while the program's clicks
  /// run, whose draft it is. A draft that opens ends the record of one dropped, whose undo would take its place
  /// (griffundo::OpeningEndsRecord).
  int PickForDraft(lua_State* const lua)
  {
    const char* const id = lua_tostring(lua, 1);
    moho::CWldSession* const session = moho::WLD_GetActiveSession();
    if (gClicking || !PlainId(id) || session == nullptr || session->mRules == nullptr) {
      return 0;
    }
    griffdraft::Building building;
    moho::RUnitBlueprint* const blueprint = BlueprintBuilding(id, building);
    if (blueprint == nullptr) {
      gpg::Warnf("Griff: no blueprint %s for the draft", id);
      return 0;
    }
    if (griffundo::OpeningEndsRecord(gDraft.Open(), gRecord.kept)) {
      EndRecord();
    }
    gDraftBlueprints[building.id] = blueprint;
    gDraft.Pick(building);
    ShowDraftOpen(building);
    return 0;
  }

  int DraftPlans(lua_State* const lua)
  {
    lua_pushnumber(lua, gDraft.Open() ? static_cast<lua_Number>(gDraft.Plans().size()) : -1);
    return 1;
  }

  int DraftRefused(lua_State*)
  {
    Hint(kHintFirstTickOrCross);
    return 0;
  }

  int DraftEnd(lua_State*)
  {
    if (!gClicking) {
      ForgetDraft();
    }
    return 0;
  }

  constexpr char kEscapesHelp[] = "FafreGriffEscapes() - whether Griff's Esc, the cross, shows: the draft, an armed command mode "
                                  "or the selection to put away";

  int Escapes(lua_State* const lua)
  {
    lua_pushboolean(lua, gEscapes.load() ? 1 : 0);
    return 1;
  }

  moho::CScrLuaBinder gWantedBinder(moho::SCR_UserLuaInitSet(), "FafreGriffWanted", &Wanted, nullptr, "<global>", kWantedHelp);
  moho::CScrLuaBinder gBoxBinder(moho::SCR_UserLuaInitSet(), "FafreGriffBox", &ReportBox, nullptr, "<global>", kBoxHelp);
  moho::CScrLuaBinder gFrameBinder(moho::SCR_UserLuaInitSet(), "FafreGriffFrame", &Frame, nullptr, "<global>", kFrameHelp);
  moho::CScrLuaBinder gAnswerBinder(moho::SCR_UserLuaInitSet(), "FafreGriffAnswer", &Answer, nullptr, "<global>", kAnswerHelp);
  moho::CScrLuaBinder gEndedBinder(moho::SCR_UserLuaInitSet(), "FafreGriffEnded", &Ended, nullptr, "<global>", kEndedHelp);
  moho::CScrLuaBinder gDraftBinder(moho::SCR_UserLuaInitSet(), "FafreGriffDraft", &PickForDraft, nullptr, "<global>", kDraftHelp);
  moho::CScrLuaBinder
    gDraftPlansBinder(moho::SCR_UserLuaInitSet(), "FafreGriffDraftPlans", &DraftPlans, nullptr, "<global>", kDraftPlansHelp);
  moho::CScrLuaBinder
    gDraftRefusedBinder(moho::SCR_UserLuaInitSet(), "FafreGriffDraftRefused", &DraftRefused, nullptr, "<global>", kDraftRefusedHelp);
  moho::CScrLuaBinder gDraftEndBinder(moho::SCR_UserLuaInitSet(), "FafreGriffDraftEnd", &DraftEnd, nullptr, "<global>", kDraftEndHelp);
  moho::CScrLuaBinder gEscapesBinder(moho::SCR_UserLuaInitSet(), "FafreGriffEscapes", &Escapes, nullptr, "<global>", kEscapesHelp);

  // --- Griff's pieces in FAF's UI (fafre/griffhud.lua) ----------------------------------------------------------------------

  constexpr char kHudHelp[] =
    "FafreGriffHud() - how the activity has Griff's rail, undo button and draft's tick and cross: looks, undo, undoLeft";
  constexpr char kHudPlacesHelp[] = "FafreGriffHudPlaces(left, top, right, bottom, aside, ...) - where griffhud.lua drew Box, "
                                   "Plus, the undo's button and the draft's tick and cross";

  /// How the activity has the rail's Box and Plus (Rail.look, two bits each, Box's first: idle, held, lit), a finger on the
  /// draft's tick, its cross and the undo's button (Game.HUD_HELD_), and the undo's button (Game's HUD_UNDO_: none, offered
  /// with the part of its time left, waiting for its undo), for griffhud.lua, which draws them (Game.setGriffHud).
  std::atomic<int> gHudLooks{0};
  std::atomic<int> gUndoShown{0};
  std::atomic<float> gUndoLeft{0};

  /// Where griffhud.lua drew what a finger takes on the activity's side, Box, Plus, the undo's button, the draft's tick and its
  /// cross: left, top, right and bottom in the picture's pixels and 1 where something the player opened of FAF's lies over it,
  /// or FAF's UI is hidden, for each (Game.griffHudPlaces); all 0 until it said.
  constexpr std::size_t kHudPlaces = 25;
  std::mutex gHudPlacesMutex;
  std::array<float, kHudPlaces> gHudPlaces{};

  int Hud(lua_State* const lua)
  {
    lua_pushnumber(lua, static_cast<lua_Number>(gHudLooks.load()));
    lua_pushnumber(lua, static_cast<lua_Number>(gUndoShown.load()));
    lua_pushnumber(lua, static_cast<lua_Number>(gUndoLeft.load()));
    return 3;
  }

  int HudPlaces(lua_State* const lua)
  {
    std::array<float, kHudPlaces> places{};
    for (std::size_t at = 0; at < kHudPlaces && static_cast<int>(at) < lua_gettop(lua); ++at) {
      places[at] = static_cast<float>(lua_tonumber(lua, static_cast<int>(at) + 1));
    }
    const std::lock_guard<std::mutex> lock(gHudPlacesMutex);
    gHudPlaces = places;
    return 0;
  }

  moho::CScrLuaBinder gHudBinder(moho::SCR_UserLuaInitSet(), "FafreGriffHud", &Hud, nullptr, "<global>", kHudHelp);
  moho::CScrLuaBinder gHudPlacesBinder(moho::SCR_UserLuaInitSet(), "FafreGriffHudPlaces", &HudPlaces, nullptr, "<global>", kHudPlacesHelp);

  // --- What the player opened of FAF's (fafre/opened.lua) ------------------------------------------------------------------

  constexpr char kOpenedHelp[] =
    "FafreGriffOpened(left, top, right, bottom, ...) - what the player opened of FAF's that shows, which Griff's pieces step aside from";

  /// What the player opened of FAF's that shows now, four numbers a box; none: nothing.
  int ReportOpened(lua_State* const lua)
  {
    const int count = lua_gettop(lua) / 4 * 4;
    std::vector<float> opened;
    opened.reserve(static_cast<std::size_t>(count));
    for (int at = 1; at <= count; ++at) {
      opened.push_back(static_cast<float>(lua_tonumber(lua, at)));
    }
    const std::lock_guard<std::mutex> lock(gOpenedMutex);
    gOpened = std::move(opened);
    return 0;
  }

  moho::CScrLuaBinder gOpenedBinder(moho::SCR_UserLuaInitSet(), "FafreGriffOpened", &ReportOpened, nullptr, "<global>", kOpenedHelp);

  // --- The queues: what FAF's UI holds of the units' orders --------------------------------------------------------------

  /// A unit of this game by its entity id, while it lives.
  [[nodiscard]] moho::UserUnit* UnitOf(const int id)
  {
    moho::CWldSession* const session = moho::WLD_GetActiveSession();
    moho::UserEntity* const entity = session != nullptr ? session->LookupEntityId(static_cast<moho::EntId>(id)) : nullptr;
    return entity != nullptr ? entity->IsUserUnit() : nullptr;
  }

  [[nodiscard]] int IdOf(const moho::UserEntity& entity)
  {
    return static_cast<int>(entity.mParams.mEntityId);
  }

  /// One of a unit's command queues: its own, or a factory's rally route (none for other units).
  [[nodiscard]] moho::UserCommandQueue* CommandsOf(moho::UserUnit& unit, const bool route)
  {
    return route ? unit.GetFactoryCommandQueue() : unit.GetCommandQueue();
  }

  /// A command of a queue as the undo reads it: by its own id, the engine's whole one; where FAF draws its waypoint, which is
  /// where the entity it targets is for one on an entity; the building it builds, which goes into a console command when it
  /// is built again (StartBuild); FAF's attack-move button's script by its task in the order's Lua data, which the command
  /// keeps as the table's byte stream (SCR_ToString), NULs and all.
  [[nodiscard]] griffundo::Entry EntryOf(moho::UserCommandIssueHelper& helper)
  {
    griffundo::Entry entry;
    entry.id = static_cast<int>(helper.mConstantData.cmd);
    moho::EUnitCommandType type = moho::ResolveCommandIssueHelperCommandType(helper);
    gpg::RRef typeRef{};
    typeRef = gpg::MakeRRef<moho::EUnitCommandType>(&type);
    entry.type = typeRef.GetLexical().c_str();
    const Wm3::Vector3f at = moho::ResolveCommandGraphAnchorWorldPosition(helper);
    entry.place = griffundo::Place{at.x, at.y, at.z};
    const moho::SSTITarget& target = helper.mVariableData.mTarget1;
    if (target.mType == moho::EAiTargetType::AITARGET_Entity) {
      entry.target = static_cast<int>(target.mEntityId);
    }
    if (type == moho::EUnitCommandType::UNITCOMMAND_BuildMobile && helper.mConstantData.blueprint != nullptr) {
      const char* const id = helper.mConstantData.blueprint->mBlueprintId.c_str();
      entry.blueprint = PlainId(id) ? id : "";
    }
    const std::string_view data = helper.mConstantData.unk2.view();
    if (type == moho::EUnitCommandType::UNITCOMMAND_Script && data.find("AttackMove") != std::string_view::npos) {
      entry.task = "AttackMove";
    }
    return entry;
  }

  /// A queue's orders as FAF's UI holds them now: what the sim took, with the UI's edits it has not taken yet over it.
  [[nodiscard]] std::vector<griffundo::Entry> QueueOf(moho::UserCommandQueue* const commands)
  {
    std::vector<griffundo::Entry> queue;
    if (commands == nullptr) {
      return queue;
    }
    for (const moho::WeakPtr<moho::UserCommandIssueHelper>& link : *moho::GetUserUnitManagerQueueLinks(commands)) {
      if (moho::UserCommandIssueHelper* const helper = link.GetObjectPtr(); helper != nullptr) {
        queue.push_back(EntryOf(*helper));
      }
    }
    return queue;
  }

  /// What the sim holds of a queue as of the last sync, without the UI's edits: its primary links, which the sync sets from
  /// the unit's commands (ResyncUserCommandQueueLinks).
  [[nodiscard]] std::vector<griffundo::Entry> HeldOf(moho::UserCommandQueue& commands)
  {
    std::vector<griffundo::Entry> queue;
    for (const moho::WeakPtr<moho::UserCommandIssueHelper>& link : commands.primaryLinks) {
      if (moho::UserCommandIssueHelper* const helper = link.GetObjectPtr(); helper != nullptr) {
        queue.push_back(EntryOf(*helper));
      }
    }
    return queue;
  }

  /// An edit of a queue's as FAF's UI made it (griffundo::Edit).
  [[nodiscard]] griffundo::Edit EditFrom(const moho::UserManagerHelperEntry& edit)
  {
    griffundo::Edit made;
    switch (edit.mEdit) {
      case moho::EUserQueueEdit::Add:
        made.kind = griffundo::EditKind::kAdd;
        break;
      case moho::EUserQueueEdit::Reset:
        made.kind = griffundo::EditKind::kReset;
        break;
      case moho::EUserQueueEdit::Remove:
        made.kind = griffundo::EditKind::kRemove;
        break;
    }
    made.command = edit.mHelper != nullptr ? static_cast<int>(edit.mHelper->mConstantData.cmd) : 0;
    made.beat = static_cast<int>(edit.mCommandId);
    return made;
  }

  /// A queue's newest edit as FAF's UI made it, as long as the sim has not taken it (griffundo::Edit).
  [[nodiscard]] griffundo::Edit EditOf(const moho::UserCommandQueue& commands)
  {
    return commands.issueQueue.empty() ? griffundo::Edit{} : EditFrom(commands.issueQueue.back());
  }

  /// Whether the sync has reached a beat for a queue: no edit of that beat waits in the queue (UserUnit::Tick drops them as
  /// the sync reaches their beat; a later replacing order of the gesture's clears them too), nor in the helper of the command
  /// issued for it, which keeps the units it went to as events of that beat until the sync reaches it
  /// (UserCommandIssueHelper::AdvanceLocalEventsToBeat) and is gone from then on where the sim did not take the command. Both
  /// go in the same DoBeat that sets the queue's primary links from what the sim did at that beat. Beats as the engine
  /// compares them.
  [[nodiscard]] bool SyncReached(const moho::UserCommandQueue& commands, const int command, const moho::CmdId beat)
  {
    const auto waits = [beat](const moho::CmdId due) { return due - beat <= 0; };
    for (std::size_t at = 0; at < commands.issueQueue.size(); ++at) {
      if (waits(commands.issueQueue[at].mCommandId)) {
        return false;
      }
    }
    const moho::UserCommandIssueHelper* const helper =
      moho::FindCommandIssueHelperInSession(moho::WLD_GetActiveSession(), static_cast<moho::CmdId>(command));
    if (helper != nullptr) {
      for (std::size_t at = 0; at < helper->mLocalQueue.size(); ++at) {
        if (waits(helper->mLocalQueue[at].mCmdId)) {
          return false;
        }
      }
    }
    return true;
  }

  /// The game's tick as of the syncs applied so far (CWldSession::mGameTick, which a paused game holds); 0 without a game.
  [[nodiscard]] int GameTick()
  {
    const moho::CWldSession* const session = moho::WLD_GetActiveSession();
    return session != nullptr ? session->mGameTick : 0;
  }

  /// A read of what the sim holds of a replaced queue (`taken`), before its replacement landed, at the game's tick as of the
  /// syncs applied so far: it shows what became of each order the sim had not taken as the gesture began whose beat the sync
  /// has reached by now (griffundo::Pending::kRead). That order's own edit went with the replacement's Reset, but the helper
  /// of its command keeps the units it went to as events of that beat until the sync reaches it.
  void Read(moho::UserCommandQueue& commands, Queue& queue)
  {
    queue.taken = HeldOf(commands);
    queue.readTick = GameTick();
    for (griffundo::Unsynced& order : queue.unsynced) {
      order.read = order.read || SyncReached(commands, order.command, static_cast<moho::CmdId>(order.beat));
    }
  }

  /// What the sim holds of the queues the gesture's orders replaced, read each frame until the replacement landed: from then
  /// on it is what the units still held when it did. Each read comes after the frame's syncs (DoBeat), so the sync that
  /// brings the replacement ends the reading before its view is read; a frame that brought the beats before the
  /// replacement's with it read none of them, and where the sim ticked in those, the unit may have done what the read
  /// before shows (griffundo::Stale).
  void ReadTaken(std::unordered_map<int, Stored>& stored)
  {
    for (auto& [id, kept] : stored) {
      const bool reading = (kept.own.snapshot && !kept.own.landed) || (kept.route.snapshot && !kept.route.landed);
      moho::UserUnit* const unit = reading ? UnitOf(id) : nullptr;
      for (const bool route : {false, true}) {
        Queue& queue = route ? kept.route : kept.own;
        if (!queue.snapshot || queue.landed) {
          continue;
        }
        moho::UserCommandQueue* const commands = unit != nullptr ? CommandsOf(*unit, route) : nullptr;
        if (commands == nullptr || SyncReached(*commands, queue.command, queue.beat)) {
          const moho::CWldSession* const session = moho::WLD_GetActiveSession();
          queue.landed = true;
          queue.stale = session != nullptr && griffundo::Stale(queue.readTick, session->mGameTick, session->mLastBeatWasTick != 0);
        } else {
          Read(*commands, queue);
        }
      }
    }
  }

  /// Whether the sim has taken what the UI ordered the units: none of their queues' edits is pending, so their queues are the
  /// sim's (UserCommandQueue::issueQueue), a factory's own and its rally route alike. A unit that is gone has none.
  [[nodiscard]] bool SimTook(const std::unordered_map<int, Stored>& stored)
  {
    return std::all_of(stored.begin(), stored.end(), [](const auto& item) {
      moho::UserUnit* const unit = item.second.own.ordered || item.second.route.ordered ? UnitOf(item.first) : nullptr;
      if (unit == nullptr) {
        return true;
      }
      for (const bool route : {false, true}) {
        const moho::UserCommandQueue* const commands = CommandsOf(*unit, route);
        if (commands != nullptr && !commands->issueQueue.empty()) {
          return false;
        }
      }
      return true;
    });
  }

  /// The selection's units as a gesture begins, for its undo: each queue's orders, those of them the sim has not taken yet
  /// (griffundo::UnsyncedOf, from the queue's pending edits that name a command and its primary links, which the sync sets)
  /// with the beat each goes to the sim at, and its newest edit.
  [[nodiscard]] std::unordered_map<int, Stored> StoreSelection()
  {
    std::unordered_map<int, Stored> stored;
    moho::CWldSession* const session = moho::WLD_GetActiveSession();
    if (session == nullptr) {
      return stored;
    }
    msvc8::vector<moho::UserUnit*> units;
    session->GetSelectionUnits(units);
    for (moho::UserUnit* const unit : units) {
      Stored& kept = stored[IdOf(*unit)];
      for (const bool route : {false, true}) {
        moho::UserCommandQueue* const commands = CommandsOf(*unit, route);
        Queue& queue = route ? kept.route : kept.own;
        queue.before = QueueOf(commands);
        if (commands == nullptr) {
          continue;
        }
        queue.edit = EditOf(*commands);
        std::vector<griffundo::Edit> edits;
        for (const moho::UserManagerHelperEntry& edit : commands->issueQueue) {
          if (edit.mHelper != nullptr) {
            edits.push_back(EditFrom(edit));
          }
        }
        queue.unsynced = griffundo::UnsyncedOf(edits, queue.before, HeldOf(*commands));
      }
      kept.focus = static_cast<int>(unit->mUnitVarDat.mFocusUnit);
    }
    return stored;
  }

  /// An order of the watched gesture's went to a unit selected as it began: into the queue whose newest edit it is now
  /// (griffundo::TookOrder), which it replaced if `clear`. The first that replaced the queue starts the reading of what the
  /// sim holds of it, until the sync reaches that order's beat: the beat of the Reset it put at the queue's front, as the
  /// edits of the orders before it in the queue went with it (ResetUserUnitManagerState). The sim's view now is the first
  /// read.
  void NoteOrder(const int id, Stored& kept, const bool clear)
  {
    moho::UserUnit* const unit = UnitOf(id);
    if (unit == nullptr) {
      return;
    }
    for (const bool route : {false, true}) {
      moho::UserCommandQueue* const commands = CommandsOf(*unit, route);
      Queue& queue = route ? kept.route : kept.own;
      const griffundo::Edit edit = commands != nullptr ? EditOf(*commands) : griffundo::Edit{};
      // An Add is the queue's newest edit, so the queue is there and its last edit names the order's command.
      if (!griffundo::TookOrder(queue.edit, edit) || commands->issueQueue.back().mHelper == nullptr) {
        continue;
      }
      queue.edit = edit;
      queue.ordered = true;
      queue.given.push_back(EntryOf(*commands->issueQueue.back().mHelper));
      if (!clear || queue.replaced) {
        continue;
      }
      queue.replaced = true;
      const moho::UserManagerHelperEntry& reset = commands->issueQueue.front();
      if (reset.mEdit == moho::EUserQueueEdit::Reset) {
        queue.snapshot = true;
        queue.beat = reset.mCommandId;
        queue.command = edit.command;
        Read(*commands, queue);
      }
    }
  }

  /// The selection's units now.
  [[nodiscard]] std::vector<int> SelectedIds()
  {
    std::vector<int> ids;
    moho::CWldSession* const session = moho::WLD_GetActiveSession();
    if (session != nullptr) {
      msvc8::vector<moho::UserUnit*> units;
      session->GetSelectionUnits(units);
      for (moho::UserUnit* const unit : units) {
        ids.push_back(IdOf(*unit));
      }
    }
    return ids;
  }

  /// Units for griff.lua, a Lua list of their ids: {12,34}.
  [[nodiscard]] std::string UnitList(const std::vector<int>& units)
  {
    std::string list = "{";
    for (std::size_t at = 0; at < units.size(); ++at) {
      list += (at == 0 ? "" : ",") + std::to_string(units[at]);
    }
    return list + "}";
  }

  // --- The groups: Supreme Commander 2's, of the units a gesture ordered or a factory order made ----------------------------

  /// Each of one's own mobile units' group (include/GriffGroups.h): the gesture that ordered it last, or the run of the factory
  /// that made it where no gesture has ordered it since.
  griffgroups::Book gGroups;

  /// The factories one's own orders gave builds to (FAF's BuildFactory), with their runs: the units one makes one after another
  /// of one kind while its queue does not run empty, one group each (griffgroups::Runs).
  griffgroups::Runs<moho::RUnitBlueprint> gRuns;

  /// The group icons the last frame placed (FafreGriffGroups), each with its group's origin: a tap goes by them.
  std::vector<griffgroups::Icon> gIcons;
  std::vector<griffgroups::Origin> gIconOrigins;

  /// The group the ladder's first tap selected by its icon, by that tap's number: its next rung selects it again and brings the
  /// camera, wherever the icon has gone meanwhile.
  int gGroupTap = 0;
  griffgroups::Origin gGroupTapped;

  /// The game is over: its groups go.
  void ForgetGroups()
  {
    gGroups.Clear();
    gRuns.Clear();
    gIcons.clear();
    gIconOrigins.clear();
    gGroupTap = 0;
  }

  /// The side of a group's icon, in the strategic icons' pixels, which FAF's option strat_icon_scale scales: FAF's largest unit
  /// icon's, a selected land unit's (its 16 at rest).
  constexpr float kIconSide = 20;

  /// One of the focus army's mobile units that lives: what a group holds.
  [[nodiscard]] bool Groupable(const moho::UserUnit& unit)
  {
    const moho::CWldSession* const session = moho::WLD_GetActiveSession();
    const moho::RUnitBlueprint* const blueprint = unit.GetBlueprint();
    return session != nullptr && unit.mArmy != nullptr && unit.mArmy == session->GetFocusArmy() && !unit.IsDead()
        && blueprint != nullptr && blueprint->IsMobile();
  }

  /// A watched gesture's order went to these units: those of them that a group holds are the gesture's group, and where they
  /// were before goes with the watch, for its undo (Book::Undone). The draft's tick makes no group: it is no gesture's.
  void GroupOrdered(const std::vector<int>& units)
  {
    if (gWatch.tick) {
      return;
    }
    std::vector<int> grouped;
    for (const int id : units) {
      if (const moho::UserUnit* const unit = UnitOf(id); unit != nullptr && Groupable(*unit)) {
        grouped.push_back(id);
      }
    }
    const std::vector<griffgroups::Earlier> moved = gGroups.Ordered(static_cast<std::uint32_t>(gWatch.number), grouped);
    gWatch.earlier.insert(gWatch.earlier.end(), moved.begin(), moved.end());
  }

  /// Each frame: the units the factories finished go to their runs' groups, before a factory whose queue is empty ends its run
  /// (griffgroups::Runs ends it once what the factory builds is finished); what the factories build now is noted; and the
  /// units that died or are no longer one's own leave their groups.
  void KeepGroups()
  {
    for (const int id : gRuns.Built()) {
      moho::UserUnit* const unit = UnitOf(id);
      if (unit != nullptr && !unit->IsDead() && unit->IsBeingBuilt()) {
        continue;
      }
      const bool made = unit != nullptr && Groupable(*unit);
      if (const std::uint32_t run = gRuns.Finished(id, made ? unit->GetBlueprint() : nullptr); run != 0) {
        gGroups.Made(id, run);
      }
    }
    for (const int id : gRuns.Factories()) {
      moho::UserUnit* const factory = UnitOf(id);
      if (factory == nullptr || factory->IsDead()) {
        gRuns.Gone(id);
        continue;
      }
      if (factory->mUnitVarDat.mCommands.empty()) {
        gRuns.Emptied(id);
      }
      if (moho::UserUnit* const built = UnitOf(static_cast<int>(factory->mUnitVarDat.mFocusUnit));
          built != nullptr && built->IsBeingBuilt()) {
        gRuns.Building(id, IdOf(*built));
      }
    }
    std::vector<int> gone;
    for (const auto& [origin, units] : gGroups.UnitsByOrigin()) {
      for (const int id : units) {
        if (const moho::UserUnit* const unit = UnitOf(id); unit == nullptr || !Groupable(*unit)) {
          gone.push_back(id);
        }
      }
    }
    for (const int id : gone) {
      gGroups.Gone(id);
    }
  }

  /// The colour the game draws one's own strategic icons in (CWldSession::RenderStrategicIcons): the army's, or FAF's team
  /// colour where those are on; as a colour of FAF's UI, its hex digits.
  [[nodiscard]] std::string IconColour(const moho::CWldSession& session, const moho::UserArmy& army)
  {
    const std::size_t index = static_cast<std::size_t>(army.mArmyIndex);
    const bool team = session.mTeamColorMode && army.mArmyIndex >= 0 && index < moho::kTeamColorPaletteSize;
    const std::uint32_t colour = team ? moho::teamcolors[index] : army.mVarDat.mPlayerColorBgra;
    char text[16];
    std::snprintf(text, sizeof text, "%08x", static_cast<unsigned>(colour));
    return text;
  }

  constexpr char kGroupsHelp[] =
    "FafreGriffGroups(places) - each frame: Griff's groups kept, and with `places` their icons, four numbers each into it (x, y, "
    "count, all selected); answers how many, their side in the picture's pixels and the colour of one's own strategic icons";

  /// Each frame of the session's UI (fafre/griffgroups.lua): the groups kept; then, with a table for them, the icons of the
  /// groups the game draws strategic icons for, at their units' middle on the main map's view. The game draws them where it
  /// draws any (CUIWorldView::Render: not while the camera turns or is free, nor with FAF's icons off) and for a unit from its
  /// mesh's IconFadeInZoom on (griffgroups::DrawsIcon): a group's icon shows once one of its units shows as an icon, and goes
  /// as the camera comes close. Without the table, as while Griff's Groups button hides them, there are none, for a tap either.
  int Groups(lua_State* const lua)
  {
    KeepGroups();
    gIcons.clear();
    gIconOrigins.clear();
    const auto answer = [lua](const std::size_t count, const float side, const std::string& colour) {
      lua_pushnumber(lua, static_cast<lua_Number>(count));
      lua_pushnumber(lua, side);
      lua_pushstring(lua, colour.c_str());
      return 3;
    };
    moho::CWldSession* const session = moho::WLD_GetActiveSession();
    moho::UserArmy* const army = session != nullptr ? session->GetFocusArmy() : nullptr;
    MapView map;
    if (!lua_istable(lua, 1) || army == nullptr || !MainMap(map) || !map.view->mIconsVisible || !moho::ui_RenderIcons
        || !moho::ui_NisRenderIcons) {
      return answer(0, 0, "");
    }
    moho::CameraImpl* const camera = map.view->GetCamera();
    const moho::VMatrix4& viewport = camera->CameraGetView().viewport;
    const Wm3::Vector3f focus = camera->GetTargetPosition();
    // The renderer's zoom: the view's width at the focus, with the camera's LOD scale (its viewport's row 1).
    const float zoom = viewport.r[1].x * focus.x + viewport.r[1].y * focus.y + viewport.r[1].z * focus.z + viewport.r[1].w;
    const float maxZoom = camera->GetMaxZoom();
    const float side = kIconSide * moho::ui_StrategicIconScale;
    int at = 1;
    for (const auto& [origin, units] : gGroups.UnitsByOrigin()) {
      if (units.size() < griffgroups::kGroupUnits) {
        continue;
      }
      bool drawn = moho::ui_AlwaysRenderStrategicIcons;
      bool selected = true;
      float sumX = 0;
      float sumY = 0;
      float sumZ = 0;
      // KeepGroups left the book only units that live.
      for (const int id : units) {
        moho::UserUnit* const unit = UnitOf(id);
        const moho::RMeshBlueprint* const mesh = unit->mVariableData.mMeshBlueprint;
        drawn = drawn || mesh == nullptr || griffgroups::DrawsIcon(zoom, mesh->mIconFadeInZoom, maxZoom);
        selected = selected && session->mSelection.Find(unit) != session->mSelection.end();
        const Wm3::Vec3f place = unit->GetInterpolatedPosition(session->mTimeSinceLastTick);
        sumX += place.x;
        sumY += place.y;
        sumZ += place.z;
      }
      const float count = static_cast<float>(units.size());
      float x = 0;
      float y = 0;
      if (!drawn || !Project(map, sumX / count, sumY / count, sumZ / count, x, y) || !map.box.Contains(x, y)) {
        continue;
      }
      for (const float value : {x, y, count, selected ? 1.0f : 0.0f}) {
        lua_pushnumber(lua, value);
        lua_rawseti(lua, 1, at++);
      }
      gIcons.push_back(griffgroups::Icon{x, y, side / 2});
      gIconOrigins.push_back(origin);
    }
    return answer(gIcons.size(), side, IconColour(*session, *army));
  }

  moho::CScrLuaBinder gGroupsBinder(moho::SCR_UserLuaInitSet(), "FafreGriffGroups", &Groups, nullptr, "<global>", kGroupsHelp);

  // --- The orders' binders: what griff.lua tells of the orders FAF's UI gives ---------------------------------------------

  constexpr char kOrderedHelp[] = "FafreGriffOrdered(commandType, attackMove, clear, unitIds, targetId) - "
                                  "FAF's UI gave an order: a watched gesture's is its record's, any other ends that";

  [[nodiscard]] int Whole(lua_State* const lua, const int index)
  {
    return static_cast<int>(lua_tonumber(lua, index));
  }

  [[nodiscard]] std::string Text(lua_State* const lua, const int index)
  {
    const char* const text = lua_tostring(lua, index);
    return text != nullptr ? text : "";
  }

  /// An entity's id as FAF's UI Lua gives it, a string of its digits (the game's Lua numbers would round the larger ones);
  /// kNoTarget for none.
  [[nodiscard]] int EntityIdOf(const std::string& text)
  {
    char* end = nullptr;
    const unsigned long long id = std::strtoull(text.c_str(), &end, 10);
    return end != text.c_str() && *end == 0 ? static_cast<int>(static_cast<std::uint32_t>(id)) : griffundo::kNoTarget;
  }

  /// A list of entity ids as griff.lua gives it: their strings, with commas between.
  [[nodiscard]] std::vector<int> EntityIdsOf(const std::string& text)
  {
    std::vector<int> ids;
    std::size_t from = 0;
    while (from < text.size()) {
      const std::size_t comma = (std::min)(text.find(',', from), text.size());
      if (const int id = EntityIdOf(text.substr(from, comma - from)); id != griffundo::kNoTarget) {
        ids.push_back(id);
      }
      from = comma + 1;
    }
    return ids;
  }

  void FinishUndo();

  /// The undo of record `number` is over and did what `said` says (UNDONE_): the activity hears it (SAID_UNDONE), and an undo
  /// that did not take all of the gesture back says so where FAF prints its messages.
  void SayUndone(const int number, const int said)
  {
    Say(kSaidUndone, number, said);
    if (said == kUndoneInPart) {
      Hint(kHintUndoneInPart);
    } else if (said == kNotUndone) {
      Hint(kHintNotUndone);
    }
  }

  /// The record goes: an undo the button asked for it that waits is not done, and the activity hears so, never that it just
  /// went.
  void DropRecord()
  {
    if (gRecord.number != 0 && gRecord.asked) {
      SayUndone(gRecord.number, kNotUndone);
    }
    gRecord = Record{};
  }

  /// The undo's record is gone: an order came that no gesture's watch counted, or one whose undo would save nothing, and only
  /// the last order counts; or what a record of Griff's own keeps would undo what came after its gesture.
  void EndRecord()
  {
    if (gRecord.number != 0) {
      DropRecord();
      Say(kSaidOrdered, 0, 0);
    }
  }

  /// A record of Griff's own (Kept), which ordered nothing: the undo's button takes it back from now on, one level, as a
  /// gesture's orders (EndWatch), and any order ends it as ever; an undo the button asked for the record it replaces is not
  /// done.
  void Offer(Record record)
  {
    DropRecord();
    gRecord = std::move(record);
    gRecord.number = ++gRecords;
    Say(kSaidOrdered, gRecord.number, 0);
  }

  /// Whether the record is a gesture's orders, which the tap ladder's attack-move in place of a move joins.
  [[nodiscard]] bool OrdersRecorded()
  {
    return gRecord.number != 0 && gRecord.kept == Kept::kOrders;
  }

  /// Whether FAF's capping may ring this unit for a Guard of engineers on it (capping.lua AssistToCap, 3835): a structure that
  /// extracts mass, a radar, an omni sensor or an artillery structure, by its options, the Shift held and its memory of the
  /// player's taps. Where the game's categories are not known, it may.
  [[nodiscard]] bool CappingRings(const moho::UserEntity& unit)
  {
    const moho::CWldSession* const session = moho::WLD_GetActiveSession();
    const moho::REntityBlueprint* const blueprint = unit.mParams.mBlueprint;
    if (session == nullptr || session->mRules == nullptr || blueprint == nullptr) {
      return true;
    }
    const auto in = [&](const char* const category) {
      const moho::EntityCategorySet* const set = session->mRules->GetEntityCategory(category);
      return set == nullptr || set->ContainsBit(blueprint->mCategoryBitIndex);
    };
    return in("STRUCTURE") && (in("MASSEXTRACTION") || in("RADAR") || in("OMNI") || in("ARTILLERY"));
  }

  /// FAF's assist-to-upgrade (its options assist_to_upgrade and assist_to_upgrade_radar): its UI orders an extractor's or a
  /// radar's upgrade a frame or more after a Guard on it (commandmode.lua OnGuardIssued forks UpgradeUnit, which may wait for
  /// an unpause first), with Clear, which no upgrade of the player's has (griffundo::AssistUpgrade). Such an Upgrade of the
  /// target of a Guard that the watch, the record or the undo that runs holds is that Guard's by-product: it neither ends nor
  /// joins the record, which notes it, as the undo takes the Guard out and leaves the upgrade going; so is one for a Guard
  /// the undo that runs gives back, which is the unit's own order's again. One for an undo that finished in the game's last
  /// few ticks (griffundo::Undones), whose word comes before it, neither: where that undo said it took all of the gesture
  /// back, it makes it one in part after all, and the activity hears so then (SAID_UNDONE once more, its toast).
  [[nodiscard]] bool AssistsUpgrade(const std::string& type, const bool clear, const std::vector<int>& units)
  {
    if (!griffundo::AssistUpgrade(type, clear, units.size())) {
      return false;
    }
    const int target = units.front();
    if (gWatch.number != 0 && gWatch.guarded.count(target) != 0) {
      ++gWatch.upgrades;
      return true;
    }
    for (Record* const record : {&gRecord, &gUndoing.record}) {
      if (record->number != 0 && record->guarded.count(target) != 0) {
        ++record->upgrades;
        return true;
      }
    }
    if (gUndoing.record.number != 0 && gUndoing.again.count(target) != 0) {
      return true;
    }
    std::vector<int> turned;
    const bool undone = gUndone.Upgraded(target, GameTick(), turned);
    for (const int number : turned) {
      SayUndone(number, kUndoneInPart);
    }
    return undone;
  }

  /// Of every order FAF's UI gives: a Guard given with Ctrl noted (gGuardsWithCtrl), its count for the clicks that run, with
  /// the entity it targets. A watched gesture's that is one to take back (griffundo::Takeable) is noted in the queues of its
  /// units that it went into; any other but the undo's own and FAF's upgrade for an assist ends the undo's record: FAF's
  /// ghost from a build key or a template, the Keys keyboard's hotkeys, the construction panel's own queue.
  int Ordered(lua_State* const lua)
  {
    const std::string type = Text(lua, 1);
    const bool attackMove = Whole(lua, 2) != 0;
    const bool clear = Whole(lua, 3) != 0;
    const std::vector<int> units = EntityIdsOf(Text(lua, 4));
    const int target = EntityIdOf(Text(lua, 5));
    // FAF's copy of an engineer's queue goes with a Guard given with Ctrl (OnGuardCopy, which reads the key as this does,
    // after it): the Guard's command, each unit's newest edit now, is noted for the undo's plan.
    if (type == "Guard" && target != griffundo::kNoTarget && moho::MAUI_KeyIsDown(moho::MKEY_CONTROL)) {
      for (const int id : units) {
        moho::UserUnit* const unit = UnitOf(id);
        moho::UserCommandQueue* const commands = unit != nullptr ? unit->GetCommandQueue() : nullptr;
        const griffundo::Edit edit = commands != nullptr ? EditOf(*commands) : griffundo::Edit{};
        if (edit.kind == griffundo::EditKind::kAdd) {
          gGuardsWithCtrl.insert(edit.command);
        }
      }
    }
    if (AssistsUpgrade(type, clear, units)) {
      return 0;
    }
    if (gClicking) {
      ++gClicks.ordered;
      ++gClicks.orders;
      gClicks.orderedTarget = target;
    }
    // The factories given builds, whose units are their runs' groups (KeepGroups).
    if (type == "BuildFactory") {
      for (const int factory : units) {
        gRuns.Ordered(factory);
      }
    }
    if (gWatch.number != 0 && griffundo::Takeable(type, attackMove)) {
      GroupOrdered(units);
      std::unordered_map<int, Stored>& stored = gWatch.joins ? gRecord.stored : gWatch.stored;
      for (const int unit : units) {
        if (const auto kept = stored.find(unit); kept != stored.end()) {
          NoteOrder(unit, kept->second, clear);
        } else {
          gWatch.unknown = true;
        }
      }
      if (type == "Guard" && target != griffundo::kNoTarget) {
        gWatch.guarded.insert(target);
      }
      gWatch.any = true;
    } else if (!(gClicking && gClicks.finish == &FinishUndo)) {
      EndRecord();
    }
    return 0;
  }

  moho::CScrLuaBinder gOrderedBinder(moho::SCR_UserLuaInitSet(), "FafreGriffOrdered", &Ordered, nullptr, "<global>", kOrderedHelp);

  constexpr char kGroupStoredHelp[] = "FafreGriffGroupStored(group, appended, before, after) - a group of Griff's control groups "
                                      "was stored, by Plus's append (or while FAF's option to steal from other control groups "
                                      "is On) or not: the ids of the units it held before and of those now";

  /// A group stored by a rest on its slot (the hook of FAF's controlgroups.lua, after its hint): the activity buzzes once.
  /// Where the store took out units it held that lived, it is a record of Griff's own, whose undo gives the group those units
  /// back (GroupBack); any other store that changed the group of such a record ends it, as its units would undo that store,
  /// and one that changed nothing keeps it (griffundo::OutcomeOfStore).
  int GroupStored(lua_State* const lua)
  {
    Say(kSaidGroupStored, 0, 0);
    const int group = Whole(lua, 1);
    const bool appended = Whole(lua, 2) != 0;
    std::vector<int> before = EntityIdsOf(Text(lua, 3));
    switch (griffundo::OutcomeOfStore(gRecord.kept, gRecord.group, group, appended, before, EntityIdsOf(Text(lua, 4)))) {
      case griffundo::StoreOutcome::kOffer: {
        Record stored;
        stored.kept = Kept::kGroup;
        stored.group = group;
        stored.held = std::move(before);
        Offer(std::move(stored));
        break;
      }
      case griffundo::StoreOutcome::kEnd:
        EndRecord();
        break;
      case griffundo::StoreOutcome::kKeep:
        break;
    }
    return 0;
  }

  moho::CScrLuaBinder
    gGroupStoredBinder(moho::SCR_UserLuaInitSet(), "FafreGriffGroupStored", &GroupStored, nullptr, "<global>", kGroupStoredHelp);

  /// Runs griff.lua's function, which answers by FafreGriffAnswer; 0 when it did not.
  [[nodiscard]] int Ask(const char* const command)
  {
    gAnswer = 0;
    moho::CON_Execute(command);
    return gAnswer;
  }

  /// Runs a call of griff.lua's of any length, such as one with a list of units.
  void CallGriffLine(const std::string& call)
  {
    moho::CON_Execute((std::string(kGriffLua) + call).c_str());
  }

  /// Whether the command mode armed is FAF's attack-move, as its orders panel's button arms it (griff.lua AttackMoveArmed).
  [[nodiscard]] bool AttackMoveArmed()
  {
    return CommandModeArmed() && Ask(kAttackMoveArmed) == kYes;
  }

  // --- The watch over a gesture's orders ------------------------------------------------------------------------------------

  /// Whether a gesture's undo saves the player work (griffundo::SavesWork): the draft's tick does, and an order that replaced a
  /// queue of one of its units that held a building or two orders or more, as the gesture began.
  [[nodiscard]] bool SavesWork(const bool tick, const std::unordered_map<int, Stored>& stored)
  {
    return tick || std::any_of(stored.begin(), stored.end(), [](const auto& item) {
             const Stored& kept = item.second;
             return (kept.own.replaced && griffundo::SavesWork(kept.own.before))
                 || (kept.route.replaced && griffundo::SavesWork(kept.route.before));
           });
  }

  /// The watched gesture is over: if it ordered, and its undo saves work, it is the record the undo's button takes back from
  /// now on, one level, and the activity hears of it (SAID_ORDERED); an undo the button asked for the record it replaces is
  /// not done. An order whose undo would save nothing ends the record before it, as any order does, and offers none. The tap
  /// ladder's attack-move adds to the move's record, whose time goes on, and noted its orders there; where that record ended
  /// meanwhile or one of Griff's own took its place, or the move made none, it is an order like any other, whose queues as it
  /// began are not known (its own were the move's). A unit the gesture ordered whose queue was not known as it began (not
  /// selected then) is not taken back.
  void EndWatch()
  {
    if (gWatch.number == 0) {
      return;
    }
    Watch watch = std::move(gWatch);
    gWatch = Watch{};
    if (!watch.any) {
      return;
    }
    const bool joined = watch.joins && OrdersRecorded();
    if (!joined && !SavesWork(watch.tick, watch.stored)) {
      EndRecord();
      return;
    }
    if (!joined) {
      DropRecord();
      gRecord.number = ++gRecords;
      gRecord.stored = std::move(watch.stored);
    }
    gRecord.gestures.push_back(static_cast<std::uint32_t>(watch.number));
    gRecord.earlier.insert(gRecord.earlier.end(), watch.earlier.begin(), watch.earlier.end());
    gRecord.unknown = gRecord.unknown || watch.unknown;
    gRecord.guarded.insert(watch.guarded.begin(), watch.guarded.end());
    gRecord.upgrades += watch.upgrades;
    Say(kSaidOrdered, gRecord.number, 0);
  }

  /// A gesture that may order begins: what FAF's UI orders from now on is its own, until the watch with its number ends
  /// (EndWatchTask), or the next gesture's begins, which comes after this one's input (a task's input comes before the next
  /// task). The selection's units are kept as they are now, their queues before the gesture's clicks; the tap ladder's
  /// attack-move in place of a move goes on with the move's record (`join`), whose queues are from before the move. None
  /// while the tick's or the undo's clicks run: what they order is their own.
  [[nodiscard]] int WatchOrders(const bool join)
  {
    if (gClicking) {
      return 0;
    }
    EndWatch();
    gWatch.number = ++gWatches;
    gWatch.joins = join && OrdersRecorded();
    if (!gWatch.joins) {
      gWatch.stored = StoreSelection();
    }
    return gWatch.number;
  }

  void EndWatchTask(const WPARAM watch)
  {
    try {
      if (gWatch.number != 0 && static_cast<WPARAM>(gWatch.number) == watch && moho::UI_GamePlaying()) {
        EndWatch();
      }
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  /// The watch ends after the input that the task running now gives: its clicks.
  void EndWatchAfterInput(const int watch)
  {
    if (watch != 0) {
      Win32Android_RunOnWindowThread(&EndWatchTask, static_cast<WPARAM>(watch));
    }
  }

  /// A first finger came down here: what a rest of the gesture before it found is over (RestTask); on the map or the minimap
  /// the camera's glide or flight stops, and FAF's reach for a unit beside a tap is the mockup's for what is selected (with
  /// Box, which picks units, as without troops). Not while a command mode is armed, which picks as FAF sets it: its reclaim
  /// mode reaches further for wrecks. Nothing while the program's clicks run, which pick as they began; the down still comes,
  /// as the task that ends clicks that wait past their patience.
  void DownTask(const WPARAM places)
  {
    const int x = Coordinate(places, 0);
    const int y = Coordinate(places, 1);
    const bool box = Coordinate(places, 2) != 0;
    try {
      ForgetRest();
      if (!moho::UI_GamePlaying() || ClicksRun()) {
        return;
      }
      if (WorldViewAt(static_cast<float>(x), static_cast<float>(y)) != nullptr) {
        moho::CON_Execute(kStopCamera);
      }
      if (!CommandModeArmed()) {
        const bool troops = !box && Ask(kTroops) == kYes;
        SetTolerance(troops ? gNearReachWithTroops.load() : gNearReach.load());
      }
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  // --- The clicks, on the program's thread ------------------------------------------------------

  /// Shift's holders: a held Plus, and an action that Plus counts for, around its clicks. The key goes down with the first and
  /// up with the last, so a held Plus stays held through the action.
  int gShiftHolders = 0;

  void HoldShift()
  {
    if (gShiftHolders++ == 0) {
      Win32Android_Key(VK_SHIFT, kShiftScanCode, TRUE, FALSE, 0);
    }
  }

  /// After Shift went up, in the task after its key: a command mode that orders with Shift used ends, as FAF ends it at its
  /// next beat once Shift is up (griff.lua ShiftLetGo), which a paused game does not have. So a lit Plus appends one order in
  /// a mode and the mode ends with it, and a held Plus keeps it until it goes up.
  void ShiftLetGoTask(const WPARAM)
  {
    try {
      if (moho::UI_GamePlaying()) {
        moho::CON_Execute(kShiftLetGo);
      }
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  void ReleaseShift()
  {
    if (gShiftHolders > 0 && --gShiftHolders == 0) {
      Win32Android_Key(VK_SHIFT, kShiftScanCode, FALSE, FALSE, 0);
      // From a task its input and tasks come next, in order (Win32Android_RunOnWindowThread): FAF has taken the clicks and
      // the key by then.
      Win32Android_RunOnWindowThread(&ShiftLetGoTask, 0);
    }
  }

  /// A mouse's click there, press and release.
  void Click(const int button, const int x, const int y)
  {
    Win32Android_TouchButton(button, TRUE, x, y);
    Win32Android_TouchButton(button, FALSE, x, y);
  }

  /// A click as a mouse's with Shift held, if `shift`, for the next action of a lit or held Plus.
  void ClickWith(const bool shift, const int button, const int x, const int y)
  {
    if (shift) {
      HoldShift();
    }
    Click(button, x, y);
    if (shift) {
      ReleaseShift();
    }
  }

  /// The UI's click where a tap or a rest goes, with the tap help (hostclicks::ClickNear), with Shift's if `shift`.
  void ClickUi(const bool shift, const float x, const float y, const int button)
  {
    if (shift) {
      HoldShift();
    }
    hostclicks::ClickNear(x, y, gTapReach.load(), gTapSmallSide.load(), button);
    if (shift) {
      ReleaseShift();
    }
  }

  /// Whether a tap here on the map meant a small control beside it, which the tap help clicks instead (UI_TapTarget): as with
  /// the mouse scheme, a tap 2 mm beside a tech tab is the tab's. Never with a command mode armed, which wants the very place.
  [[nodiscard]] bool MeantSmallControl(const float x, const float y)
  {
    float targetX = x;
    float targetY = y;
    return moho::UI_TapTarget(x, y, gTapReach.load(), gTapSmallSide.load(), &targetX, &targetY) && (targetX != x || targetY != y);
  }

  /// Whether a tap here is the UI's: a panel's, the minimap's, or a small control's beside it.
  [[nodiscard]] bool UiTap(const int x, const int y)
  {
    return !OnMap(static_cast<float>(x), static_cast<float>(y)) || MeantSmallControl(static_cast<float>(x), static_cast<float>(y));
  }

  /// A tap of the activity's (Game.smartTap): where its finger was, the rail's bits, its step on the ladder, the ladder's first
  /// tap, and its number for the answer.
  struct Tap
  {
    int x = 0;
    int y = 0;
    int rail = 0;
    int step = kStepTap;
    int baseX = 0;
    int baseY = 0;
    int number = 0;
  };

  std::mutex gTapsMutex;
  std::deque<Tap> gTaps;

  /// What a STEP_TAP was, for the ladder (TouchInput.TapLadder), and with Box counting, which the activity then leaves to this
  /// answer, the rail's bits it used by what it turned out to be (`outcome`: Rail's rule, grifftap::Used).
  void AnswerTap(const Tap& tap, const int kind, const grifftap::Outcome outcome = grifftap::Outcome::kOther)
  {
    const int used = (tap.rail & kRailBox) != 0 ? grifftap::Used(tap.rail, outcome) : 0;
    Say(kSaidTap, tap.number, kind | used << 8);
  }

  /// A group's units as the book holds them now, by its origin; none where it is no group any more.
  [[nodiscard]] std::vector<int> GroupUnits(const griffgroups::Origin& origin)
  {
    const auto& groups = gGroups.UnitsByOrigin();
    const auto group = groups.find(origin);
    return group != groups.end() && group->second.size() >= griffgroups::kGroupUnits ? group->second : std::vector<int>{};
  }

  /// How a group's icon takes a tap (griff.lua SelectGroup): its units are the selection, or join it (Plus), or with Box join
  /// it or, where all of them are selected, leave it, as Box's tap does with one's own unit.
  enum class GroupTap
  {
    kSelect = 0,
    kAdd = 1,
    kToggle = 2,
  };

  /// A group by its icon (griff.lua SelectGroup), as `how` says, and with `camera` the camera goes to it as to a control group
  /// selected twice. Nothing where it is no group any more.
  void SelectGroup(const griffgroups::Origin& origin, const GroupTap how, const bool camera)
  {
    const std::vector<int> units = GroupUnits(origin);
    if (!units.empty()) {
      CallGriffLine(
        "SelectGroup(" + UnitList(units) + ", " + std::to_string(static_cast<int>(how)) + ", " + (camera ? "true" : "false") + ")"
      );
    }
  }

  /// A tap where the finger was and the cursor has rested (TouchInput): FAF's click by what it lands on. The UI's controls take
  /// it, with the tap help; a command mode that FAF's orders panel armed takes it (its attack-move too); a group's icon selects
  /// the group, which joins the selection with Plus, and Box adds or takes out as for one's own unit; Box adds one's own unit
  /// to the selection or takes it out, and on anything else does nothing; one's own unit is selected, Plus adds it (Shift's
  /// click); else the selection gets FAF's right click there, also on one's own unit with Plus and troops selected: the order
  /// there, as the mockup has it. Near misses are FAF's own pick, within the reach DownTask set. A group's icon takes a tap
  /// within the tap help's reach of its disc, but with troops selected only within FAF's pick for them, and not where FAF's
  /// cursor is over a unit that is not one's own, which the tap orders on (griffgroups::IconReach, IconTakesTap).
  void TapByPlace(const Tap& tap)
  {
    const int x = tap.x;
    const int y = tap.y;
    const bool queue = (tap.rail & kRailQueue) != 0;
    const bool box = (tap.rail & kRailBox) != 0;
    // With Box counting, the UI's tap and an armed mode's are none of Box's: it stays lit, Plus goes out.
    if (UiTap(x, y)) {
      ClickUi(queue, static_cast<float>(x), static_cast<float>(y), WIN32ANDROID_BUTTON_LEFT);
      AnswerTap(tap, kTapUi);
      return;
    }
    if (CommandModeArmed()) {
      ClickWith(queue, WIN32ANDROID_BUTTON_LEFT, x, y);
      AnswerTap(tap, kTapOther);
      return;
    }
    // What the cursor is over, where it rested: the finger's place.
    const int kind = Ask(kTapKind);
    const bool own = (kind & kOwnUnit) != 0;
    const bool troops = (kind & kTroopsSelected) != 0;
    if (griffgroups::IconTakesTap(troops, (kind & kUnitUnder) != 0 && !own)) {
      const float reach = griffgroups::IconReach(troops, gTapReach.load(), gNearReachWithTroops.load());
      if (const int icon = griffgroups::IconAt(gIcons, static_cast<float>(x), static_cast<float>(y), reach); icon >= 0) {
        gGroupTap = tap.number;
        gGroupTapped = gIconOrigins[static_cast<std::size_t>(icon)];
        SelectGroup(gGroupTapped, box ? GroupTap::kToggle : queue ? GroupTap::kAdd : GroupTap::kSelect, false);
        AnswerTap(tap, kTapGroup, box ? grifftap::Outcome::kBoxToggled : grifftap::Outcome::kOther);
        return;
      }
    }
    const bool selection = (kind & kSelection) != 0;
    if (box) {
      if (own) {
        ClickWith(true, WIN32ANDROID_BUTTON_LEFT, x, y);
      }
      AnswerTap(tap, kTapOther, own ? grifftap::Outcome::kBoxToggled : grifftap::Outcome::kBoxFoundNothing);
      return;
    }
    if (own && !(queue && troops)) {
      ClickWith(queue, WIN32ANDROID_BUTTON_LEFT, x, y);
      AnswerTap(tap, queue ? kTapOther : kTapOwn);
      return;
    }
    ClickWith(queue, selection ? WIN32ANDROID_BUTTON_RIGHT : WIN32ANDROID_BUTTON_LEFT, x, y);
    int answer = kTapOther;
    if (!selection) {
      answer = (kind & kUnitUnder) != 0 ? kTapOther : kTapEmpty;
    } else if ((kind & kMoveOrder) != 0 && !queue) {
      answer = kTapGround;
    }
    AnswerTap(tap, answer);
  }

  /// A tap of the ladder above its first rung, by what its first tap was, whatever lies under this one: the UI's click again
  /// where the first went, which the message layer makes the game's double click; all of the selected unit's type in view (as
  /// the game's double click on the unit, which could not reach it where the panels its selection brought up cover it), then on
  /// the map; an attack-move at the first tap's place in place of its move (where FAF offers none, as for factories alone, a
  /// right click again: the rally point); all one's own mobile units in view; the group the first tap's icon selected, again,
  /// with the camera, as a control group's double click.
  void TapByStep(const Tap& tap)
  {
    switch (tap.step) {
      case kStepTypeInView:
        moho::CON_Execute(kTypeInView);
        break;
      case kStepAgain:
        if (UiTap(tap.baseX, tap.baseY)) {
          ClickUi(false, static_cast<float>(tap.baseX), static_cast<float>(tap.baseY), WIN32ANDROID_BUTTON_LEFT);
        } else {
          Click(WIN32ANDROID_BUTTON_LEFT, tap.baseX, tap.baseY);
        }
        break;
      case kStepAllOfType:
        moho::CON_Execute(kAllOfType);
        break;
      case kStepAttackMove:
        // FAF cannot change an order's kind: a new attack-move replaces the move, which replaced the queue too.
        if (CommandModeArmed()) {
          moho::CON_Execute(kEndCommandMode);
        }
        Click(ArmAttackMove() ? WIN32ANDROID_BUTTON_LEFT : WIN32ANDROID_BUTTON_RIGHT, tap.baseX, tap.baseY);
        break;
      case kStepAllMobile:
        moho::CON_Execute(kAllMobile);
        break;
      case kStepGroupAgain:
        if (tap.number == gGroupTap) {
          SelectGroup(gGroupTapped, GroupTap::kSelect, true);
        }
        break;
      default:
        break;
    }
  }

  void SmartTapTask(const WPARAM)
  {
    Tap tap;
    {
      const std::lock_guard<std::mutex> lock(gTapsMutex);
      if (gTaps.empty()) {
        return;
      }
      tap = gTaps.front();
      gTaps.pop_front();
    }
    try {
      if (moho::UI_GamePlaying()) {
        const bool again = tap.step == kStepAgain;
        if (ClicksRun() || ((tap.step == kStepTap || again) && Covered(again ? tap.baseX : tap.x, again ? tap.baseY : tap.y))) {
          // The undo's or the tick's clicks run, which a click of the finger's would come between; or one of the app's pieces
          // came over the place the finger went down at: the piece's, never the game's.
          if (tap.step == kStepTap) {
            AnswerTap(tap, kTapOther, grifftap::Outcome::kBoxFoundNothing);
          }
          return;
        }
        // A tap's own click, the ladder's attack-move in place of the move, and FAF's double click, which a UI's button may
        // order by, are watched; the ladder's other rungs only select.
        const bool orders = tap.step == kStepTap || again || tap.step == kStepAttackMove;
        const int watch = orders ? WatchOrders(tap.step == kStepAttackMove) : 0;
        if (tap.step == kStepTap) {
          TapByPlace(tap);
        } else {
          TapByStep(tap);
        }
        EndWatchAfterInput(watch);
      } else if (tap.step == kStepTap || tap.step == kStepAgain) {
        // No game behind the UI any more, such as at its end under FAF's last dialogs: a tap is the UI's, as the mouse's.
        const bool again = tap.step == kStepAgain;
        ClickUi(false, static_cast<float>(again ? tap.baseX : tap.x), static_cast<float>(again ? tap.baseY : tap.y),
                WIN32ANDROID_BUTTON_LEFT);
        if (!again) {
          AnswerTap(tap, kTapUi);
        }
      }
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  /// Two fingers' tap: an armed command mode goes, as a right click puts it away; with none, the selection goes. Not between
  /// the program's clicks, whose mode and selection it is.
  void DeselectTask(const WPARAM)
  {
    try {
      if (moho::UI_GamePlaying() && !ClicksRun()) {
        moho::CON_Execute(CommandModeArmed() ? kEndCommandMode : kDeselect);
      }
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  /// A hint of the activity's (Game.griffHint): its kind and number.
  void HintTask(const WPARAM places)
  {
    try {
      Hint(Coordinate(places, 0), Coordinate(places, 1));
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  /// The camera to the selection; argument 1: a flight, else at once. Not while the program's clicks run, which aim by it.
  void ToGroupTask(const WPARAM fly)
  {
    try {
      if (moho::UI_GamePlaying() && !ClicksRun()) {
        CallGriff("ToGroup(%d)", fly != 0 ? 1 : 0, 0, 0);
      }
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  // --- The handle's rest: FAF's Distribute orders -----------------------------------------------------------------------

  /// The group's middle on the picture as griff.lua reported it in the last frame, the handle's group's (Report), where it is on
  /// the main map: a cursor there is the map's, whose world place under it FAF's Distribute orders reads (GetMouseWorldPos).
  [[nodiscard]] bool GroupMiddleOnMap(int& x, int& y)
  {
    const Selection selection = gSelection;
    x = static_cast<int>(std::lround(selection.middleX));
    y = static_cast<int>(std::lround(selection.middleY));
    return selection.count > 0 && OnMap(static_cast<float>(x), static_cast<float>(y));
  }

  /// A finger rests on the handle (Game.handleRest, number `rest`): whether its lift gives FAF's Distribute orders, with the
  /// cursor at the group's middle, as griff.lua's CanDistribute finds FAF's own pick there: two of the selection's non-factory
  /// units or more, and the queue of the one nearest the cursor's world place two orders or more. Then the ring shows the move
  /// ping's look, as a drag from the rest is the handle's route. The activity hears it (SAID_DISTRIBUTES). No, where the
  /// group's middle is not on the map (its units out of view or under a panel), where the cursor could not point at them, and
  /// between the program's clicks.
  void HandleRestTask(const WPARAM rest)
  {
    bool distributes = false;
    try {
      int x = 0;
      int y = 0;
      if (moho::UI_GamePlaying() && !ClicksRun() && GroupMiddleOnMap(x, y)) {
        char call[64];
        std::snprintf(call, sizeof call, "CanDistribute(%d, %d)", x, y);
        distributes = Ask((std::string(kGriffLua) + call).c_str()) == kYes;
      }
      if (distributes) {
        griffshown::Set(griffshown::kRest, {static_cast<float>(kRestDistribute)});
      }
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
      distributes = false;
    }
    Say(kSaidDistributes, static_cast<int>(rest), distributes ? 1 : 0);
  }

  /// The lift of the handle's rest that distributes (Game.distribute): the cursor goes to the group's middle, where it stays for
  /// a drawn frame, as FAF reads the world place under the cursor with the frame's (DistributeAfterFrame, from PlaceTask). The
  /// pointer's leave that the lift asked for waits for it: on a slow frame it came first and took the cursor off the group.
  /// Where the group's middle left the map since the rest, nothing, which says Cancelled. Not between the program's clicks.
  void DistributeTask(const WPARAM)
  {
    StopDistributing();
    try {
      int x = 0;
      int y = 0;
      if (!moho::UI_GamePlaying() || ClicksRun()) {
        return;
      }
      if (!GroupMiddleOnMap(x, y)) {
        Hint(kHintCancelled);
        return;
      }
      Win32Android_MouseMove(x, y);
      Distributing waits;
      // Where the cursor went, within the game's clip of it: a later move would take it elsewhere.
      (void)::GetCursorPos(&waits.cursor);
      waits.afterFrame = gFrame;
      waits.since = Now();
      gDistributing = waits;
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  /// After a frame drew the cursor at the group's middle: FAF's own key action G, Distribute orders, which takes the selection's
  /// non-factory unit nearest the cursor's world place, clears every unit's queue and deals that unit's orders out over them,
  /// and says what it did where FAF prints its messages ('Distributing orders'). The undo's record ends, as G gives every queue
  /// anew, which no record takes back. Where the cursor moved meanwhile (a finger) or no frame came in the clicks' patience,
  /// nothing, which says Cancelled; nothing between the program's clicks. Then the pointer's leave comes, which waited for this.
  void DistributeAfterFrame()
  {
    const Distributing waited = gDistributing;
    gDistributing = Distributing{};
    if (moho::UI_GamePlaying() && !ClicksRun()) {
      POINT cursor{};
      (void)::GetCursorPos(&cursor);
      if (cursor.x != waited.cursor.x || cursor.y != waited.cursor.y || Outwaited(waited.since, Now())) {
        Hint(kHintCancelled);
      } else {
        moho::CON_Execute(kDistributeOrders);
        EndRecord();
      }
    }
    hostclicks::CursorFreed();
  }

  // --- Drags, the pan's glide and the pinch ---------------------------------------------------------

  /// What a finger's drag pressed, for its end.
  enum class Drag
  {
    kNone,
    kPan,
    kBox,
    kAddingBox,
    kPanel,
    kFormation,
    kQueuedFormation,
  };

  Drag gDrag = Drag::kNone;
  /// The watch over a formation's order, which its lift gives.
  int gFormationWatch = 0;

  /// The camera glides on at this velocity, in the picture's pixels a second (griff.lua Glide), or stays where it is.
  void Glide(const int velocityX, const int velocityY)
  {
    if (velocityX != 0 || velocityY != 0) {
      CallGriff("Glide(%d, %d, %d)", velocityX, velocityY, static_cast<int>(gGlideStop.load() + 0.5f));
    }
  }

  /// What the finger's drag pressed goes up here; a pan's camera glides on at the velocity, not while the program's clicks
  /// run, which aim by the camera.
  void EndDrag(const int x, const int y, const int velocityX, const int velocityY)
  {
    switch (gDrag) {
      case Drag::kPan:
        Win32Android_TouchButton(WIN32ANDROID_BUTTON_MIDDLE, FALSE, x, y);
        if ((velocityX != 0 || velocityY != 0) && !ClicksRun()) {
          Glide(velocityX, velocityY);
        }
        break;
      case Drag::kBox:
      case Drag::kPanel:
        Win32Android_TouchButton(WIN32ANDROID_BUTTON_LEFT, FALSE, x, y);
        break;
      case Drag::kAddingBox:
        Win32Android_TouchButton(WIN32ANDROID_BUTTON_LEFT, FALSE, x, y);
        ReleaseShift();
        break;
      case Drag::kFormation:
      case Drag::kQueuedFormation:
        // FAF orders the formation as the right button goes up, facing the way the drag went.
        Win32Android_TouchButton(WIN32ANDROID_BUTTON_RIGHT, FALSE, x, y);
        if (gDrag == Drag::kQueuedFormation) {
          ReleaseShift();
        }
        EndWatchAfterInput(gFormationWatch);
        gFormationWatch = 0;
        break;
      case Drag::kNone:
        break;
    }
    gDrag = Drag::kNone;
  }

  /// GiveUpDrag's formation, at its place and 1 where it was queued: the world views forget the order a release would give
  /// (their command data, which only the one that took the right button's press holds: by now a panel or one of the app's
  /// pieces may lie over where it took it) and the formation FAF shows, then the button goes up.
  void GiveUpFormationTask(const WPARAM places)
  {
    try {
      if (moho::CMauiControl* const root = UiRoot(); root != nullptr) {
        for (moho::CMauiControl* control = root; control != nullptr; control = control->DepthFirstSuccessor(root)) {
          if (auto* const view = dynamic_cast<moho::CUIWorldView*>(control)) {
            view->mCommandData.mMode = moho::COMMOD_None;
          }
        }
      }
      if (moho::CWldSession* const session = moho::WLD_GetActiveSession();
          session != nullptr && session->mCurFormation != nullptr) {
        session->mCurFormation->mReady = false;
        session->mCurFormation->Reset();
      }
      Win32Android_TouchButton(WIN32ANDROID_BUTTON_RIGHT, FALSE, Coordinate(places, 0), Coordinate(places, 1));
      if (Coordinate(places, 2) != 0) {
        ReleaseShift();
      }
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  /// The finger's drag goes without its lift (Game.griffDragGiveUp: a second finger on FAF's formation, the system taking the
  /// fingers, a new first finger): FAF's formation is given up, not ordered, as a PC player's right button let go over a
  /// panel orders nothing (GiveUpFormationTask). That runs as a task of its own: this one may have been queued before
  /// DragTask ran, and so come ahead of the right button's press DragTask queued, which would give the view its command data
  /// back; a task queued from here comes after that press (Win32Android_RunOnWindowThread). Any other drag ends here as at a
  /// lift, without a pan's glide.
  void GiveUpDrag(const int x, const int y)
  {
    if (gDrag != Drag::kFormation && gDrag != Drag::kQueuedFormation) {
      EndDrag(x, y, 0, 0);
      return;
    }
    Win32Android_RunOnWindowThread(&GiveUpFormationTask, Places(x, y, gDrag == Drag::kQueuedFormation ? 1 : 0));
    // Nothing was ordered: the watch ends with no record.
    EndWatchAfterInput(gFormationWatch);
    gFormationWatch = 0;
    gDrag = Drag::kNone;
  }

  /// A finger's drag (Game.griffDrag): where it came down, where it is now, after a rest, the rail's bits, its number.
  struct DragStart
  {
    int x = 0;
    int y = 0;
    int nowX = 0;
    int nowY = 0;
    bool rested = false;
    int rail = 0;
    int number = 0;
  };

  std::mutex gDragsMutex;
  std::deque<DragStart> gDrags;

  // --- A finger's rest on the map: a waypoint to delete, a formation's right button --------------------------------------

  /// What a finger that rests on the map found (RestTask), until its gesture goes on: a waypoint of the selection's orders,
  /// marked for deletion, with Shift held so that FAF draws its command graph and FAF's cursor on the waypoint (`aimX`, `aimY`);
  /// or the ground, where FAF's right click is a plain move for the selection, where the rest's right button gives a formation
  /// (at the drag that follows, or as a click at the lift); or nothing of either.
  enum class Rest
  {
    kNone,
    kWaypoint,
    kGround,
  };

  struct Resting
  {
    Rest kind = Rest::kNone;
    /// On the ground, whether the drag that follows is FAF's formation (kRestDragGivesFormation, with mobile units selected).
    bool formation = false;
    int command = 0;
    int aimX = 0;
    int aimY = 0;
  };

  Resting gRest;

  /// The rest's gesture went on, or went: a marked waypoint's Shift goes up, and its mark with it; its ring goes.
  void ForgetRest()
  {
    if (gRest.kind == Rest::kWaypoint) {
      ReleaseShift();
    }
    gRest = Resting{};
    griffshown::Set(griffshown::kRest, {});
  }

  /// The waypoint of the selection's orders nearest to a finger at this place on the map, within the tap help's reach: one FAF
  /// draws in its command graph (griffundo::Removable), at the place it draws it. Marked for deletion: Shift held, so that FAF
  /// draws the graph and its cursor hovers waypoints, and FAF's cursor on it. False where none is in reach.
  [[nodiscard]] bool MarkWaypoint(const int x, const int y)
  {
    moho::CWldSession* const session = moho::WLD_GetActiveSession();
    MapView map;
    if (session == nullptr || !MapAt(x, y, map)) {
      return false;
    }
    const float reach = gTapReach.load();
    float nearest = reach * reach;
    Resting found;
    msvc8::vector<moho::UserUnit*> units;
    session->GetSelectionUnits(units);
    for (moho::UserUnit* const unit : units) {
      for (const bool route : {false, true}) {
        moho::UserCommandQueue* const commands = CommandsOf(*unit, route);
        if (commands == nullptr) {
          continue;
        }
        for (const moho::WeakPtr<moho::UserCommandIssueHelper>& link : *moho::GetUserUnitManagerQueueLinks(commands)) {
          moho::UserCommandIssueHelper* const helper = link.GetObjectPtr();
          if (helper == nullptr) {
            continue;
          }
          const griffundo::Entry entry = EntryOf(*helper);
          float screenX = 0;
          float screenY = 0;
          if (!griffundo::Removable(entry) || !Project(map, entry.place.x, entry.place.y, entry.place.z, screenX, screenY)) {
            continue;
          }
          const float distance = (screenX - x) * (screenX - x) + (screenY - y) * (screenY - y);
          if (distance <= nearest) {
            nearest = distance;
            found = Resting{.kind = Rest::kWaypoint,
                            .command = entry.id,
                            .aimX = static_cast<int>(std::lround(screenX)),
                            .aimY = static_cast<int>(std::lround(screenY))};
          }
        }
      }
    }
    if (found.kind != Rest::kWaypoint) {
      return false;
    }
    gRest = found;
    HoldShift();
    Win32Android_MouseMove(gRest.aimX, gRest.aimY);
    return true;
  }

  /// How a resting finger's ring looks (Game.REST_, griffmap.lua): what its drag gives, by what the rest found, or nothing on a
  /// Checkbox of FAF's (`checkbox`), from which a drag presses nothing (DragTask; grifftap::RestLookOf).
  void ShowRest(const bool checkbox)
  {
    const grifftap::RestLook look = grifftap::RestLookOf(checkbox, gRest.kind == Rest::kWaypoint, gRest.formation);
    griffshown::Set(griffshown::kRest, {static_cast<float>(look)});
  }

  /// A finger rested here on the map (Game.griffRest), without Box: on a waypoint of the selection's orders it is marked for
  /// deletion; on the ground, where FAF's right click is a plain move for the selection, the rest is the right button's, and
  /// with mobile units selected the drag after it FAF's formation (kRestDragGivesFormation); else nothing, and a drag after it
  /// is FAF's box as before. Not with a command mode armed, whose drag is its own (FAF's area attack), nor on FAF's UI or the
  /// minimap, nor between the program's clicks. Whatever it found, the ring about the finger shows what its drag gives
  /// (ShowRest, Game.REST_), also where it looked for nothing: there the left button's drag, the armed mode's, the minimap's or
  /// the panel's, but from a Checkbox of FAF's nothing.
  void RestTask(const WPARAM places)
  {
    const int x = Coordinate(places, 0);
    const int y = Coordinate(places, 1);
    try {
      ForgetRest();
      // The map as DragTask finds it for the drag that may follow.
      const bool map = moho::UI_GamePlaying() && OnMap(static_cast<float>(x), static_cast<float>(y));
      if (map && !ClicksRun() && !Covered(x, y) && !CommandModeArmed() && !MarkWaypoint(x, y)) {
        // What the cursor is over, where it rests: the finger's place since its down.
        const int kind = Ask(kTapKind);
        if ((kind & kSelection) != 0 && (kind & kMoveOrder) != 0) {
          gRest.kind = Rest::kGround;
          gRest.formation = kRestDragGivesFormation && (kind & kMobileSelected) != 0;
        }
      }
      ShowRest(!map && PressComesToCheckbox(static_cast<float>(x), static_cast<float>(y)));
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  /// The rest's gesture went without its lift or a drag: a second finger came, or the system took the fingers.
  void RestGoneTask(const WPARAM)
  {
    ForgetRest();
  }

  /// FAF's Shift+Ctrl right release on the marked waypoint, where FAF's cursor hovers its very command: FAF takes it out of
  /// every unit it went to (CUIWorldView::HandleEvent's right release), as the undo does; whether it went. Not where the
  /// press would pair with the press before it as the message layer's double click, which FAF's world view takes otherwise.
  [[nodiscard]] bool DeleteMarked()
  {
    moho::CWldSession* const session = moho::WLD_GetActiveSession();
    if (session == nullptr || session->CursorInfo().mIsDragger != gRest.command
        || Win32Android_DoubleClickWait(WIN32ANDROID_BUTTON_RIGHT, gRest.aimX, gRest.aimY) != 0) {
      return false;
    }
    Win32Android_Key(VK_CONTROL, kCtrlScanCode, TRUE, FALSE, 0);
    Win32Android_MouseButton(WIN32ANDROID_BUTTON_RIGHT, TRUE, gRest.aimX, gRest.aimY);
    Win32Android_MouseButton(WIN32ANDROID_BUTTON_RIGHT, FALSE, gRest.aimX, gRest.aimY);
    Win32Android_Key(VK_CONTROL, kCtrlScanCode, FALSE, FALSE, 0);
    return true;
  }

  constexpr char kMarkedHelp[] =
    "FafreGriffMarked() - the waypoint a resting finger marked for deletion: x, y, z where FAF draws it, and whether FAF's cursor hovers it";

  /// The marked waypoint (RestTask) for griffhud.lua, which marks it with FAF's Cancel icon while lifting would take it out:
  /// where FAF draws it now (it goes with what its command targets), and whether FAF's cursor hovers its very command. Nothing
  /// where none is marked, or FAF holds its command no more.
  int Marked(lua_State* const lua)
  {
    moho::CWldSession* const session = moho::WLD_GetActiveSession();
    if (gRest.kind != Rest::kWaypoint || session == nullptr) {
      return 0;
    }
    moho::UserCommandIssueHelper* const helper = moho::FindCommandIssueHelperInSession(session, gRest.command);
    if (helper == nullptr) {
      return 0;
    }
    const Wm3::Vector3f at = moho::ResolveCommandGraphAnchorWorldPosition(*helper);
    if (!std::isfinite(at.x) || !std::isfinite(at.y) || !std::isfinite(at.z)) {
      return 0;
    }
    lua_pushnumber(lua, at.x);
    lua_pushnumber(lua, at.y);
    lua_pushnumber(lua, at.z);
    lua_pushboolean(lua, session->CursorInfo().mIsDragger == gRest.command ? 1 : 0);
    return 4;
  }

  moho::CScrLuaBinder gMarkedBinder(moho::SCR_UserLuaInitSet(), "FafreGriffMarked", &Marked, nullptr, "<global>", kMarkedHelp);

  /// A finger's drag from where it came down, as the place wants it: on the map the middle button's, which pans the camera,
  /// pressed where the finger is now so that the map does not jump by the slop it went; with Box, or after a rest that found
  /// neither a waypoint nor the ground for a formation, a selection box from where it came down, which Plus makes add to the
  /// selection (Shift's); after a rest on the ground the right button's from there, FAF's formation, which turns to face the
  /// way the finger goes and is ordered at the lift (with Shift for Plus); after a rest on a waypoint nothing, as the finger
  /// slid off the waypoint it marked; on a panel or the minimap the left button's from there, as a mouse drags there, but
  /// from a Checkbox of FAF's nothing, also after a rest on it, as its press would act (PressComesToCheckbox). The activity
  /// hears which drag is a pan, which a second finger makes two of.
  void DragTask(const WPARAM)
  {
    DragStart drag;
    {
      const std::lock_guard<std::mutex> lock(gDragsMutex);
      if (gDrags.empty()) {
        return;
      }
      drag = gDrags.front();
      gDrags.pop_front();
    }
    EndDrag(drag.x, drag.y, 0, 0);
    // What the rest before the drag found is this drag's, once: the rest's gesture goes on as the drag.
    const Rest rest = drag.rested ? gRest.kind : Rest::kNone;
    const bool formation = drag.rested && gRest.formation;
    ForgetRest();
    bool map = false;
    bool checkbox = false;
    try {
      const bool playing = moho::UI_GamePlaying();
      // A box, a panel's drag or a pan would come between the undo's or the tick's clicks; and one of the app's pieces that
      // came over where the finger went down takes it.
      if (playing && (ClicksRun() || Covered(drag.x, drag.y))) {
        return;
      }
      map = playing && OnMap(static_cast<float>(drag.x), static_cast<float>(drag.y));
      checkbox = !map && PressComesToCheckbox(static_cast<float>(drag.x), static_cast<float>(drag.y));
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
    if (checkbox) {
      // A slide from the button presses nothing, as a mouse's: the pointer only goes with the finger.
    } else if (!map) {
      gDrag = Drag::kPanel;
      Win32Android_TouchButton(WIN32ANDROID_BUTTON_LEFT, TRUE, drag.x, drag.y);
    } else if (rest == Rest::kWaypoint) {
      // The finger slid off the waypoint it marked: the mark went with Shift, and nothing is deleted.
    } else if (rest == Rest::kGround && formation && (drag.rail & kRailBox) == 0) {
      const bool queued = (drag.rail & kRailQueue) != 0;
      if (queued) {
        HoldShift();
      }
      gFormationWatch = WatchOrders(false);
      gDrag = queued ? Drag::kQueuedFormation : Drag::kFormation;
      Win32Android_TouchButton(WIN32ANDROID_BUTTON_RIGHT, TRUE, drag.x, drag.y);
      // The activity gives a formation up where its fingers go without a lift (Game.griffDragGiveUp).
      Say(kSaidFormation, drag.number, 0);
    } else if (drag.rested || (drag.rail & kRailBox) != 0) {
      const bool adding = (drag.rail & kRailQueue) != 0;
      if (adding) {
        HoldShift();
      }
      gDrag = adding ? Drag::kAddingBox : Drag::kBox;
      Win32Android_TouchButton(WIN32ANDROID_BUTTON_LEFT, TRUE, drag.x, drag.y);
    } else {
      gDrag = Drag::kPan;
      Win32Android_TouchButton(WIN32ANDROID_BUTTON_MIDDLE, TRUE, drag.nowX, drag.nowY);
      Say(kSaidPan, drag.number, 0);
    }
  }

  /// The drag ends at a place, with the velocity a pan glides on at (SignedCoordinate).
  void DragEndTask(const WPARAM places)
  {
    try {
      EndDrag(Coordinate(places, 0), Coordinate(places, 1), SignedCoordinate(places, 2), SignedCoordinate(places, 3));
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  /// The drag goes without its lift, at a place (GiveUpDrag).
  void DragGiveUpTask(const WPARAM places)
  {
    try {
      GiveUpDrag(Coordinate(places, 0), Coordinate(places, 1));
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  /// The camera keeps what lies at a place under the fingers (griff.lua): from here on, at the pinch's start or when one of two
  /// fingers lifted; and as they move. Two kinds of task: the moves coalesce, a start must not be one of them. Not while the
  /// program's clicks run, which aim by the camera.
  void Pinch(const char* const format, const WPARAM places)
  {
    try {
      if (moho::UI_GamePlaying() && !ClicksRun()) {
        CallGriff(format, Coordinate(places, 0), Coordinate(places, 1), Coordinate(places, 2));
      }
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  void PinchStartTask(const WPARAM places)
  {
    Pinch("PinchStart(%d, %d, %d)", places);
  }

  void PinchTask(const WPARAM places)
  {
    Pinch("Pinch(%d, %d, %d)", places);
  }

  void PinchEndTask(const WPARAM places)
  {
    try {
      if (moho::UI_GamePlaying()) {
        moho::CON_Execute(kPinchEnd);
        if (!ClicksRun()) {
          Glide(SignedCoordinate(places, 0), SignedCoordinate(places, 1));
        }
      }
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  /// A finger rested and lifted: on a waypoint it marked (RestTask), FAF's removal of that order, as a PC player's Shift+Ctrl
  /// right click on it, and a hint where FAF's cursor did not hover it then; on the ground where the rest was the right
  /// button's, FAF's right click there, which a mouse's hold gives (with Shift for Plus), watched as a tap's; elsewhere on the
  /// map the rest was for a box that did not come, but with Box it was none (TouchInput) and is Box's tap; on a panel FAF's
  /// right click, with the tap help, as the mouse scheme's hold gives it. With no game behind the UI any more, the UI's right
  /// click wherever it is.
  void RestTapTask(const WPARAM places)
  {
    Tap tap;
    tap.x = Coordinate(places, 0);
    tap.y = Coordinate(places, 1);
    tap.rail = Coordinate(places, 2);
    try {
      const bool playing = moho::UI_GamePlaying();
      if (playing && (ClicksRun() || Covered(tap.x, tap.y))) {
        ForgetRest();
        if ((tap.rail & kRailBox) != 0) {
          AnswerTap(tap, kTapOther, grifftap::Outcome::kBoxFoundNothing);
        }
        return;
      }
      // FAF's release needs Shift down still, which the mark holds until it is forgotten.
      const Rest rest = gRest.kind;
      if (playing && rest == Rest::kWaypoint && !DeleteMarked()) {
        Hint(kHintNotDeleted);
      }
      ForgetRest();
      if (rest == Rest::kWaypoint) {
        return;
      }
      if (playing && OnMap(static_cast<float>(tap.x), static_cast<float>(tap.y))) {
        if (rest == Rest::kGround && (tap.rail & kRailBox) == 0) {
          const int watch = WatchOrders(false);
          ClickWith((tap.rail & kRailQueue) != 0, WIN32ANDROID_BUTTON_RIGHT, tap.x, tap.y);
          EndWatchAfterInput(watch);
        } else if ((tap.rail & kRailBox) != 0) {
          TapByPlace(tap);
        }
        return;
      }
      // A right click on a panel may order, as on the minimap: watched.
      const int watch = playing ? WatchOrders(false) : 0;
      ClickUi((tap.rail & kRailQueue) != 0, static_cast<float>(tap.x), static_cast<float>(tap.y), WIN32ANDROID_BUTTON_RIGHT);
      EndWatchAfterInput(watch);
      if ((tap.rail & kRailBox) != 0) {
        // Box, which the activity left to the answer, was none of this: it stays lit, and Plus goes out.
        AnswerTap(tap, kTapUi);
      }
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  void ShiftTask(const WPARAM down)
  {
    if (down != 0) {
      HoldShift();
    } else {
      ReleaseShift();
    }
  }

  // --- A stroke's end ---------------------------------------------------------------------------

  /// The stroke the program last answered for, and what a click at its end would order (Stroke.END_): the activity reads
  /// both at once.
  std::atomic<std::int64_t> gStrokeAnswer{0};

  /// What FAF would order by a click at the stroke's end, once the hover's move has come to the world view, which picks what
  /// is under the cursor at each event: a panel, the minimap, or what griff.lua says of FAF's right click there.
  void StrokeAnswerTask(const WPARAM places)
  {
    const int x = Coordinate(places, 0);
    const int y = Coordinate(places, 1);
    const int stroke = Coordinate(places, 2);
    try {
      if (!moho::UI_GamePlaying()) {
        return;
      }
      moho::CUIWorldView* const view = WorldViewAt(static_cast<float>(x), static_cast<float>(y));
      const int end = view == nullptr       ? kEndPanel
                      : view->IsMiniMap() ? kEndMinimap | (AttackMoveArmed() ? kEndAttackMove : 0)
                                          : Ask(kStrokeKind);
      gStrokeAnswer.store(static_cast<std::int64_t>(stroke) << 32 | static_cast<std::uint32_t>(end));
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  /// The cursor hovers at the stroke's end, without a button: FAF shows its order cursor there and what is under it. The
  /// answer comes in the task after the move's events. Not while the program's clicks run, whose cursor it is.
  void StrokeHoverTask(const WPARAM places)
  {
    try {
      if (ClicksRun()) {
        return;
      }
      Win32Android_MouseMove(Coordinate(places, 0), Coordinate(places, 1));
      Win32Android_RunOnWindowThread(&StrokeAnswerTask, places);
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  // --- A route ----------------------------------------------------------------------------------

  struct Route
  {
    std::vector<POINT> points;
    int rail = 0;
    /// A loop: FAF's patrol, where it offers one.
    bool patrol = false;
    /// FAF's attack-move was armed as it began (its orders panel's button): each waypoint is an attack-move.
    bool attackMove = false;
  };

  /// What the activity has given and the program's thread has not begun.
  std::mutex gRoutesMutex;
  std::deque<Route> gRoutes;

  /// On the program's thread: the route whose steps run, a click each. Each step is a task of its own, so that it sees what the
  /// click before it has done: an attack-move's or a patrol's mode, which FAF ends after an order that replaces, comes back
  /// for the next.
  Route gRoute;
  std::size_t gNextPoint = 0;
  bool gRouteHoldsShift = false;
  /// The watch over the route's orders, one undo for all of its clicks.
  int gRouteWatch = 0;

  /// What a route gives: a stroke that ends on a panel other than the minimap gives nothing, which a hint says; on its way,
  /// only what is on the map itself is a waypoint. A loop is a patrol where FAF's orders panel offers one, not for factories
  /// alone: their loop is their rally route, as any stroke. With FAF's attack-move armed (its orders panel's button) each
  /// waypoint is an attack-move, as a PC player's clicks with Shift in that mode give; any other mode armed goes first. None
  /// while the undo's or the tick's clicks run, which its clicks would come between.
  void BeginRouteTask(const WPARAM)
  {
    gRoute = Route{};
    gNextPoint = 0;
    gRouteWatch = 0;
    Route given;
    {
      const std::lock_guard<std::mutex> lock(gRoutesMutex);
      if (gRoutes.empty()) {
        return;
      }
      given = std::move(gRoutes.front());
      gRoutes.pop_front();
    }
    try {
      if (!moho::UI_GamePlaying() || given.points.empty() || ClicksRun()) {
        return;
      }
      const POINT end = given.points.back();
      if (WorldViewAt(static_cast<float>(end.x), static_cast<float>(end.y)) == nullptr) {
        Hint(kHintCancelled);
        return;
      }
      gRoute.rail = given.rail;
      gRoute.patrol = given.patrol && Ask(kOffersPatrol) == kYes;
      gRoute.attackMove = !gRoute.patrol && AttackMoveArmed();
      for (std::size_t at = 0; at + 1 < given.points.size(); ++at) {
        const POINT point = given.points[at];
        if (OnMap(static_cast<float>(point.x), static_cast<float>(point.y))) {
          gRoute.points.push_back(point);
        }
      }
      gRoute.points.push_back(end);
      // Its clicks are right clicks, or an attack-move's or a patrol's left clicks in the mode each arms, which a command mode
      // armed before would take for its own: but FAF's attack-move, which the route's clicks are then.
      if (CommandModeArmed() && !gRoute.attackMove) {
        moho::CON_Execute(kEndCommandMode);
      }
      gRouteWatch = WatchOrders(false);
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
      gRoute = Route{};
    }
  }

  /// The next waypoint: FAF's right click there, the first one replacing what the units were doing unless Plus counts, the
  /// others appended (Shift, held to the route's end). With FAF's attack-move armed, an attack-move's left click; a patrol's,
  /// a patrol's.
  void RouteStepTask(const WPARAM)
  {
    if (gNextPoint >= gRoute.points.size()) {
      return;
    }
    const POINT point = gRoute.points[gNextPoint];
    const bool appended = gNextPoint > 0 || (gRoute.rail & kRailQueue) != 0;
    ++gNextPoint;
    try {
      if (appended && !gRouteHoldsShift) {
        HoldShift();
        gRouteHoldsShift = true;
      }
      // A mode that FAF ended after a replacing order is armed again; where FAF offers no attack-move, a right click.
      if (gRoute.patrol) {
        Click(CommandModeArmed() || ArmPatrol() ? WIN32ANDROID_BUTTON_LEFT : WIN32ANDROID_BUTTON_RIGHT, point.x, point.y);
      } else if (gRoute.attackMove && (CommandModeArmed() || ArmAttackMove())) {
        Click(WIN32ANDROID_BUTTON_LEFT, point.x, point.y);
      } else {
        Click(WIN32ANDROID_BUTTON_RIGHT, point.x, point.y);
      }
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
      gNextPoint = gRoute.points.size();
    }
  }

  /// After the last waypoint's input: Shift as it was, and the route's orders are one undo.
  void EndRouteTask(const WPARAM)
  {
    if (gRouteHoldsShift) {
      ReleaseShift();
      gRouteHoldsShift = false;
    }
    gRoute = Route{};
    gNextPoint = 0;
    const int watch = gRouteWatch;
    gRouteWatch = 0;
    EndWatchTask(static_cast<WPARAM>(watch));
  }

  // --- The draft's gestures ------------------------------------------------------------------------

  /// What the draft says when a plan did not land, as the mockup words it.
  void SayPlaced(const griffdraft::Placed placed)
  {
    switch (placed) {
      case griffdraft::Placed::kNoFreeDeposit:
        Hint(kHintNoFreeResource);
        break;
      case griffdraft::Placed::kTaken:
        Hint(kHintTaken);
        break;
      case griffdraft::Placed::kFull:
        Hint(kHintFull);
        break;
      case griffdraft::Placed::kPlaced:
        break;
    }
  }

  /// A plan of the picked building for a finger at this place on the map: a deposit's building on the free deposit nearest to
  /// it within reach, any other where the finger is, on FAF's grid.
  void PlaceAt(const MapView& map, const int x, const int y)
  {
    const griffdraft::Deposit deposit = gDraft.Picked().deposit;
    if (deposit != griffdraft::Deposit::kNone) {
      SayPlaced(gDraft.PlaceOnDeposit(Deposits(map, deposit), static_cast<float>(x), static_cast<float>(y), gDepositReach.load(), GroundFree));
      return;
    }
    griffdraft::Place at;
    if (Unproject(map, x, y, at)) {
      SayPlaced(gDraft.PlaceAt(at.x, at.z, GroundFree));
    }
  }

  /// Where PlaceAt would put a plan for a finger there, without putting it: false where it would put none. A building that
  /// stands on no deposit has its place under the finger also where its ground is taken (`taken`), as FAF's ghost shows
  /// itself over ground its check refuses, in the colour of a refused place: so a finger that slides along the draft's plans
  /// and the buildings sees where the ground of one ends.
  [[nodiscard]] bool WouldPlace(const MapView& map, const int x, const int y, griffdraft::Place& place, bool& taken)
  {
    const griffdraft::Building& building = gDraft.Picked();
    taken = false;
    if (gDraft.Full()) {
      return false;
    }
    if (building.deposit != griffdraft::Deposit::kNone) {
      const std::vector<griffdraft::Candidate> deposits = Deposits(map, building.deposit);
      const int nearest = gDraft.NearestFree(deposits, static_cast<float>(x), static_cast<float>(y), gDepositReach.load(), GroundFree);
      if (nearest < 0) {
        return false;
      }
      place = deposits[static_cast<std::size_t>(nearest)].place;
      return true;
    }
    griffdraft::Place at;
    if (!Unproject(map, x, y, at)) {
      return false;
    }
    place = gDraft.Snapped(at.x, at.z);
    taken = gDraft.OnPlan(building, place.x, place.z) || !GroundFree(building, place.x, place.z);
    return true;
  }

  /// A tap in the draft (TouchInput's DRAFT_PENDING): on the UI the UI's click, with the tap help; a lit Plus stays for the tick
  /// (a held Plus is Shift as ever). On the map it removes the plan the finger was on (its number in the argument's last two
  /// words; 0: none) or places one, also where that plan is gone by now, as the mockup's tap then finds none. Nothing while the
  /// program's clicks run, between which its click would come.
  void DraftTapTask(const WPARAM places)
  {
    const int x = Coordinate(places, 0);
    const int y = Coordinate(places, 1);
    const int plan = Coordinate(places, 2) | Coordinate(places, 3) << 16;
    try {
      const bool playing = moho::UI_GamePlaying();
      if (playing && (ClicksRun() || Covered(x, y))) {
        return;
      }
      if (!playing || UiTap(x, y)) {
        // The UI's click, which may order, as the orders panel's Stop does: watched.
        const int watch = playing ? WatchOrders(false) : 0;
        ClickUi(false, static_cast<float>(x), static_cast<float>(y), WIN32ANDROID_BUTTON_LEFT);
        EndWatchAfterInput(watch);
        return;
      }
      MapView map;
      if (!gDraft.Open() || !MapAt(x, y, map)) {
        return;
      }
      if (plan != 0 && gDraft.Remove(plan)) {
        return;
      }
      PlaceAt(map, x, y);
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  /// The world place a finger's place stands for in a line or a move, through the main map, whatever lies over it there; none
  /// while the program's clicks run. Those first: clicks it ends past their patience may be the tick's, which closes the draft.
  [[nodiscard]] bool DraftPlaceUnder(const int x, const int y, griffdraft::Place& place)
  {
    MapView map;
    return !ClicksRun() && gDraft.Open() && MainMap(map) && Unproject(map, x, y, place);
  }

  /// A drag from the newest plan: the line to the finger shown (the moves coalesce), added where it lifted, or given up.
  void DraftLineTask(const WPARAM places)
  {
    try {
      gDraftLine.clear();
      griffdraft::Place at;
      if (DraftPlaceUnder(Coordinate(places, 0), Coordinate(places, 1), at)) {
        gDraftLine = gDraft.LineTo(at.x, at.z, GroundFree);
      }
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  void DraftLineEndTask(const WPARAM places)
  {
    try {
      gDraftLine.clear();
      griffdraft::Place at;
      if (DraftPlaceUnder(Coordinate(places, 0), Coordinate(places, 1), at) && !gDraft.AddLine(at.x, at.z, GroundFree)) {
        Hint(kHintFull);
      }
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  void DraftLineAbortTask(const WPARAM)
  {
    gDraftLine.clear();
  }

  /// A drag from another plan: the whole draft follows the finger from where it came down (the first two words), by whole world
  /// units; the moves coalesce, the end keeps it, a second finger puts it back.
  void DraftMoveTask(const WPARAM places)
  {
    try {
      if (!gDraft.Moving()) {
        if (!DraftPlaceUnder(Coordinate(places, 0), Coordinate(places, 1), gMoveFrom)) {
          return;
        }
        gDraft.MoveBegin();
      }
      griffdraft::Place at;
      if (DraftPlaceUnder(Coordinate(places, 2), Coordinate(places, 3), at)) {
        gDraft.MoveBy(at.x - gMoveFrom.x, at.z - gMoveFrom.z);
      }
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  void DraftMoveEndTask(const WPARAM places)
  {
    DraftMoveTask(places);
    gDraft.MoveEnd();
  }

  void DraftMoveAbortTask(const WPARAM)
  {
    gDraft.MoveAbort();
  }

  /// A finger that rests in the draft on no plan: the plan its lift would place shown under it, as it moves (the moves
  /// coalesce), also where its ground is taken (WouldPlace); at the lift that plan, or on the UI the rest's right click there,
  /// as Griff's rest on a panel gives.
  void DraftAimTask(const WPARAM places)
  {
    try {
      MapView map;
      const int x = Coordinate(places, 0);
      const int y = Coordinate(places, 1);
      gAimOnMap = !ClicksRun() && gDraft.Open() && MapAt(x, y, map);
      bool taken = false;
      gAimShown = gAimOnMap && WouldPlace(map, x, y, gAim, taken);
      gAimTaken = taken;
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
      gAimShown = false;
    }
  }

  void DraftAimEndTask(const WPARAM places)
  {
    gAimShown = false;
    gAimOnMap = false;
    const int x = Coordinate(places, 0);
    const int y = Coordinate(places, 1);
    try {
      const bool playing = moho::UI_GamePlaying();
      // Nothing between the program's clicks, the UI's right click as little as a plan.
      if (playing && (ClicksRun() || Covered(x, y))) {
        return;
      }
      if (!playing || !OnMap(static_cast<float>(x), static_cast<float>(y))) {
        const int watch = playing ? WatchOrders(false) : 0;
        ClickUi(false, static_cast<float>(x), static_cast<float>(y), WIN32ANDROID_BUTTON_RIGHT);
        EndWatchAfterInput(watch);
        return;
      }
      MapView map;
      if (gDraft.Open() && MapAt(x, y, map)) {
        PlaceAt(map, x, y);
      }
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  void DraftAimGoneTask(const WPARAM)
  {
    gAimShown = false;
    gAimOnMap = false;
  }

  /// A finger rests in the draft, Griff's rest (Game.draftRest: its place and number): whether it is on FAF's UI, where the
  /// rest is the panel's as outside the draft, its lift FAF's right click there (RestTapTask) as the aim's on FAF's UI gave it;
  /// the activity hears it (SAID_DRAFT_REST). With no game behind the UI any more all of it is the UI's.
  void DraftRestTask(const WPARAM places)
  {
    const float x = static_cast<float>(Coordinate(places, 0));
    const float y = static_cast<float>(Coordinate(places, 1));
    bool onUi = false;
    try {
      onUi = !moho::UI_GamePlaying() || !OnMap(x, y);
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
    Say(kSaidDraftRest, Coordinate(places, 2), onUi ? 1 : 0);
  }

  /// Box over deposits with an extractor picked: every free deposit of its kind whose middle is in the box on the picture gets
  /// a plan (FAF's own box is not pressed).
  void DraftBoxTask(const WPARAM places)
  {
    try {
      MapView map;
      if (ClicksRun() || !gDraft.Open() || gDraft.Picked().deposit == griffdraft::Deposit::kNone || !MainMap(map)) {
        return;
      }
      const int left = (std::min)(Coordinate(places, 0), Coordinate(places, 2));
      const int right = (std::max)(Coordinate(places, 0), Coordinate(places, 2));
      const int top = (std::min)(Coordinate(places, 1), Coordinate(places, 3));
      const int bottom = (std::max)(Coordinate(places, 1), Coordinate(places, 3));
      std::vector<griffdraft::Candidate> inside;
      for (const griffdraft::Candidate& candidate : Deposits(map, gDraft.Picked().deposit)) {
        if (candidate.screenX >= left && candidate.screenX <= right && candidate.screenY >= top && candidate.screenY <= bottom) {
          inside.push_back(candidate);
        }
      }
      bool full = false;
      gDraft.PlaceOnDeposits(inside, GroundFree, full);
      if (full) {
        Hint(kHintFull);
      }
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  // --- Clicks at world places: the tick's plans, the undo's orders given again ------------------------------------------

  /// How many frames a click waits for the camera that went to its place.
  constexpr int kClickFrames = 10;
  /// How far inside the map and from any panel a click goes: FAF scrolls the view with a cursor on its edge pixel.
  constexpr float kClickRoom = 2;
  /// How long the clicks wait for a frame at most, in ms. Past it the frames do not come (griff.lua's stopped, the activity in
  /// the back), and the clicks would hold the fingers, Shift, the selection and the camera for good.
  constexpr std::int64_t kClicksPatience = 2000;
  /// How long a removal waits at most, in ms, for the sim to take a release whose waypoint FAF's cursor picks until then: a
  /// network game takes it its lag later (FAF's 500 ms and a beat). The gesture's orders the undo waits for without an end
  /// (TryUndo), as its fingers are free meanwhile.
  constexpr std::int64_t kSimPatience = 2000;
  /// How many pixels a world unit takes at least where the camera comes closer for a building whose cell no pixel lands in.
  constexpr float kCellPixels = 4;

  /// The camera to a world place, as high and turned as it is, or closer by `closer` of its zoom (griff.lua LookAt).
  void LookAt(const griffundo::Place& place, const float closer)
  {
    char call[128];
    std::snprintf(call, sizeof call, "LookAt(%.3f, %.3f, %.3f, %.3f)", static_cast<double>(place.x), static_cast<double>(place.y),
                  static_cast<double>(place.z), static_cast<double>(closer));
    CallGriffLine(call);
  }

  /// The part of its zoom the camera keeps to come so close to a place that a world unit there takes kCellPixels on both
  /// axes, from what it takes now; 1 where it does already or where the map shows no unit there.
  [[nodiscard]] float Closer(const MapView& map, const griffundo::Place& place)
  {
    float x = 0;
    float y = 0;
    float acrossX = 0;
    float acrossY = 0;
    float downX = 0;
    float downY = 0;
    if (!Project(map, place.x, place.y, place.z, x, y) || !Project(map, place.x + 1, place.y, place.z, acrossX, acrossY)
        || !Project(map, place.x, place.y, place.z + 1, downX, downY)) {
      return 1;
    }
    const float pixels = (std::min)(std::hypot(acrossX - x, acrossY - y), std::hypot(downX - x, downY - y));
    return pixels > 0 && pixels < kCellPixels ? pixels / kCellPixels : 1;
  }

  /// The pixel a building's click goes to, whose place the map shows at `x`, `y`: one whose world point FAF builds it at, by
  /// the camera's own unprojection, which FAF's click goes through (griffdraft::ClickPixel); false where none is.
  [[nodiscard]] bool BuildPixel(const MapView& map, const ClickStep& step, const float x, const float y, int& clickX, int& clickY)
  {
    const griffdraft::Place place{step.place.x, step.place.z};
    const std::vector<griffdraft::Candidate> deposits = DepositsOf(step.building.deposit);
    const auto lands = [&](const int pixelX, const int pixelY) {
      griffdraft::Place under;
      return Unproject(map, pixelX, pixelY, under) && griffdraft::LandsAt(step.building, deposits, place, under.x, under.z);
    };
    return griffdraft::ClickPixel(x, y, lands, clickX, clickY);
  }

  /// Whether a wait for a frame since `since` (0: none) has outlasted the clicks' patience.
  [[nodiscard]] bool Outwaited(const std::int64_t since, const std::int64_t now)
  {
    return since != 0 && now - since > kClicksPatience;
  }

  /// The next step comes after the next frame (PlaceTask).
  void WaitForFrame()
  {
    gClicksWait = true;
    gClicksAfterFrame = gFrame;
    gClicksWaitSince = Now();
  }

  /// Whether a press of the program's (`button`) at this pixel waits: it would make a double click with the press before it,
  /// whoever gave that (a finger, the program), as the message layer's rule says (Win32Android_DoubleClickWait), and FAF's
  /// world view takes a double click as its own (its left one gives the first press's order again: CUIWorldView::HandleEvent's
  /// double-click arm). A PC player who wants two single clicks there waits until the double-click time has passed; so does
  /// the step, which comes again after the next frame. None of those frames counts against its frames (kClickFrames).
  [[nodiscard]] bool PressWaits(const int button, const int x, const int y)
  {
    if (Win32Android_DoubleClickWait(button, x, y) == 0) {
      return false;
    }
    WaitForFrame();
    return true;
  }

  /// The clicks begin after the next frame, in which FAF lays its panels out for the units just selected for them. The camera
  /// holds still for them, which they aim by: a glide or a flight stops (griff.lua StopCamera), and none starts while they
  /// run. A line of buildings a finger draws on FAF's ghost goes, as its own cancel gives it up: FAF's build drag would take
  /// the cursor's moves they aim with, and the line's release would come between them; the activity gives up the rest of
  /// any gesture they came into (TouchInput). A finger's pan, box or panel drag, FAF's other draggers, they wait for.
  void StartClicks(Clicks clicks)
  {
    gClicks = std::move(clicks);
    gClicking = true;
    ++gClickRuns;
    moho::UI_CancelBuildDrag();
    moho::CON_Execute(kStopCamera);
    WaitForFrame();
  }

  /// The clicks are over, or the game is: Shift as it was (the key down again first where a step lifted it for a frame,
  /// whatever else holds it), the camera where it was, then the tick's or the undo's own end.
  void FinishClicks()
  {
    gClicking = false;
    gClicksWait = false;
    gClicksWaitSince = 0;
    if (gClicks.lifted) {
      Win32Android_Key(VK_SHIFT, kShiftScanCode, TRUE, FALSE, 0);
    }
    if (gClicks.holdsShift) {
      ReleaseShift();
    }
    if (gClicks.looked && moho::UI_GamePlaying()) {
      moho::CON_Execute(kLookBack);
    }
    if (gClicks.finish != nullptr) {
      gClicks.finish();
    }
    gClicks = Clicks{};
  }

  /// Whether the program's clicks run, for a task that would act between them, which then does nothing. Clicks that have
  /// waited for a frame past their patience are over first, as their own end has them, their clicks not made not ordered: so
  /// nothing holds the fingers, Shift, the selection or the camera longer, whatever kept the frames away.
  bool ClicksRun()
  {
    if (gClicking && gClicksWait && Outwaited(gClicksWaitSince.load(), Now())) {
      const int left = static_cast<int>(gClicks.steps.size() - gClicks.next);
      gpg::Warnf("Griff: no frame came for the clicks in %lld ms, %d of %d not made", static_cast<long long>(kClicksPatience), left,
                 static_cast<int>(gClicks.steps.size()));
      gClicks.unreached += left;
      FinishClicks();
    }
    return gClicking;
  }

  /// To the next step: what the one before waited for, aimed at or came closer for goes with it.
  void NextStep()
  {
    ++gClicks.next;
    gClicks.waited = 0;
    gClicks.closer = false;
    gClicks.aims = 0;
    gClicks.redrawn = false;
  }

  /// The step is not made, and counted where it says why (`count`): no frame brought its place onto the free map or no pixel
  /// lands on it (unreached), FAF's cursor never picked its target (missed) or hovered its command (unremoved).
  void SkipStep(int& count)
  {
    ++count;
    NextStep();
    Win32Android_RunOnWindowThread(&ClickStepTask, 0);
  }

  /// A run's units selected at its first step (griff.lua UndoSelect). FAF selects a unit a transport carries as that
  /// transport (SelectUnits): such units are left out, and the selection made again without the transports. A run that gives
  /// orders back counts the units left out; a removal's needs no selection but for the waypoints FAF's cursor prefers. False
  /// where FAF selected none of a run's units as themselves.
  [[nodiscard]] bool SelectRun(const ClickStep& step)
  {
    CallGriffLine("UndoSelect(" + UnitList(step.select) + ")");
    const std::vector<int> selected = SelectedIds();
    std::vector<int> kept;
    for (const int id : step.select) {
      if (std::find(selected.begin(), selected.end(), id) != selected.end()) {
        kept.push_back(id);
      } else if (!step.removal && UnitOf(id) != nullptr) {
        ++gClicks.leftOut;
      }
    }
    if (kept.size() < selected.size()) {
      CallGriffLine("UndoSelect(" + UnitList(kept) + ")");
    }
    return step.removal || !kept.empty();
  }

  /// Whether a unit holds a command in one of its queues (as FAF's UI holds them, with the UI's edits over the sim's).
  [[nodiscard]] bool Holds(moho::UserUnit& unit, const moho::UserCommandIssueHelper* const helper)
  {
    return moho::UserUnitManagerContainsCommandIssueHelper(unit.GetCommandQueue(), helper)
        || moho::UserUnitManagerContainsCommandIssueHelper(unit.GetFactoryCommandQueue(), helper);
  }

  /// Whether FAF's release took a command out of the units it went to: none holds it any more, or FAF has sent the sim its
  /// removal (a pending Remove edit of the unit's command queue, which a factory's rally route shows only once the sim took it).
  [[nodiscard]] bool TakenOut(const int command, const std::vector<int>& units)
  {
    moho::CWldSession* const session = moho::WLD_GetActiveSession();
    const moho::UserCommandIssueHelper* const helper = moho::FindCommandIssueHelperInSession(session, command);
    if (helper == nullptr) {
      return true;
    }
    for (const int id : units) {
      moho::UserUnit* const unit = UnitOf(id);
      if (unit == nullptr || !Holds(*unit, helper)) {
        continue;
      }
      const moho::UserCommandQueue* const commands = unit->GetCommandQueue();
      bool sent = false;
      for (std::size_t at = 0; commands != nullptr && at < commands->issueQueue.size() && !sent; ++at) {
        const moho::UserManagerHelperEntry& edit = commands->issueQueue[at];
        sent = edit.mEdit == moho::EUserQueueEdit::Remove && edit.mHelper == helper;
      }
      if (!sent) {
        return false;
      }
    }
    return true;
  }

  /// Whether FAF's cursor, which went to an aimed step's place a frame ago, is where the step wants it: on the command a
  /// removal takes out, on the unit a click targets, and for a move's click on no waypoint whose move FAF's move mode would
  /// restart as a patrol instead of ordering (SCommandModeData::HandleEvent's move arm asks CanRestartMoveCommandAsPatrol of
  /// the command its press is on: a move that every selected unit holds, not their last, in queues of moves alone). FAF's
  /// move cursor would keep it from hovering waypoints at all (its ignore mode), but the console variable it sets,
  /// ui_CommandClickScale, is not the one this engine's hover reads.
  [[nodiscard]] bool CursorAt(const ClickStep& step)
  {
    moho::CWldSession* const session = moho::WLD_GetActiveSession();
    if (session == nullptr) {
      return false;
    }
    const moho::MouseInfo& cursor = session->CursorInfo();
    if (step.removal) {
      return cursor.mIsDragger == step.command;
    }
    if (step.onUnit) {
      const moho::UserEntity* const hovered = session->GetHoveredUserEntity();
      return hovered != nullptr && IdOf(*hovered) == step.target;
    }
    if (step.way == griffundo::Way::kMove) {
      moho::UserCommandIssueHelper* const hovered = moho::FindCommandIssueHelperInSession(session, cursor.mIsDragger);
      return !moho::CanRestartSelectionMoveCommandAsPatrol(session->mSelection, hovered);
    }
    return true;
  }

  [[nodiscard]] std::string ArmCall(const griffundo::Click& click);

  /// FAF's Shift+Ctrl right release where its cursor is, on the waypoint of `command`, which it takes from `units` (Shift is
  /// held for the removals' run); the next task checks it.
  void Release(const int command, std::vector<int> units)
  {
    Win32Android_Key(VK_CONTROL, kCtrlScanCode, TRUE, FALSE, 0);
    Win32Android_MouseButton(WIN32ANDROID_BUTTON_RIGHT, TRUE, gClicks.aimX, gClicks.aimY);
    Win32Android_MouseButton(WIN32ANDROID_BUTTON_RIGHT, FALSE, gClicks.aimX, gClicks.aimY);
    Win32Android_Key(VK_CONTROL, kCtrlScanCode, FALSE, FALSE, 0);
    gClicks.removing = true;
    gClicks.removedCommand = command;
    gClicks.removedUnits = std::move(units);
    gClicks.released.push_back(command);
    gClicks.releasedAt = Now();
    ++gClicks.acted;
  }

  /// A removal's place where FAF's cursor hovers another command than the step's: one waypoint within the reach of FAF's
  /// cursor of another, of the selection's units both, either may be hovered (UICommandGraph::ResolveCursorHighlightCommandId
  /// takes the last of them). It is taken out instead, and the step aims again, within its frames: another of the gesture's,
  /// whose own step then finds it gone; or an own order on a place that comes last in its units' queues but for the
  /// gesture's, as FAF anchors a patrol given with Shift at the queue's last place, which comes back after the gesture's are
  /// out by its mode's click with Shift, appended to the steps. FAF's release takes such an order from every unit it went
  /// to, the gesture's or not, so it is given back to all of them; where one of them holds a later order of its own, it
  /// stays, and so does the gesture's waypoint, counted. False for any other, which stays, and for one released already,
  /// whose waypoint FAF draws until it draws its graph anew (and a factory's rally route shows until the sim took it).
  [[nodiscard]] bool TakeInstead(const int hovered)
  {
    moho::CWldSession* const session = moho::WLD_GetActiveSession();
    moho::UserCommandIssueHelper* const helper = moho::FindCommandIssueHelperInSession(session, hovered);
    if (helper == nullptr || std::find(gClicks.released.begin(), gClicks.released.end(), hovered) != gClicks.released.end()) {
      return false;
    }
    for (std::size_t at = gClicks.next + 1; at < gClicks.steps.size(); ++at) {
      if (gClicks.steps[at].removal && gClicks.steps[at].command == hovered) {
        if (!PressWaits(WIN32ANDROID_BUTTON_RIGHT, gClicks.aimX, gClicks.aimY)) {
          Release(hovered, gClicks.steps[at].units);
          Win32Android_RunOnWindowThread(&ClickStepTask, 0);
        }
        return true;
      }
    }
    std::vector<int> removed;
    for (const ClickStep& step : gClicks.steps) {
      if (step.removal) {
        removed.push_back(step.command);
      }
    }
    std::vector<int> units;
    for (moho::UserUnit* const unit : *moho::ResolveCommandIssueCursorEntities(*helper)) {
      moho::UserCommandQueue* const own = unit->GetCommandQueue();
      const std::vector<griffundo::Entry> queue =
        QueueOf(moho::UserUnitManagerContainsCommandIssueHelper(own, helper) ? own : unit->GetFactoryCommandQueue());
      if (std::none_of(queue.begin(), queue.end(), [hovered](const griffundo::Entry& entry) { return entry.id == hovered; })) {
        continue;
      }
      if (!griffundo::ComesBackAfter(queue, hovered, removed)) {
        return false;
      }
      units.push_back(IdOf(*unit));
    }
    if (units.empty()) {
      return false;
    }
    if (PressWaits(WIN32ANDROID_BUTTON_RIGHT, gClicks.aimX, gClicks.aimY)) {
      return true;
    }
    const griffundo::Entry kept = EntryOf(*helper);
    const griffundo::Way way = griffundo::WayOf(kept);
    ClickStep back;
    back.arm = ArmCall(griffundo::Click{way, kept.place, griffundo::kNoTarget, "", true});
    back.place = kept.place;
    back.run = gClicks.steps.back().run + 1;
    back.select = units;
    back.aimed = true;
    back.way = way;
    Release(hovered, std::move(units));
    gClicks.steps.push_back(std::move(back));
    Win32Android_RunOnWindowThread(&ClickStepTask, 0);
    return true;
  }

  /// An aimed step's click at the pixel griffundo::AimedClick chose, or its release, which goes where FAF's cursor hovers the
  /// command, at the aim, once the press would make no double click with the one before it (PressWaits). A click with Shift
  /// or without it, whatever holds Shift (a held Plus): a click that replaces lifts it for itself.
  void Act(const ClickStep& step, const int x, const int y)
  {
    if (PressWaits(step.removal ? WIN32ANDROID_BUTTON_RIGHT : WIN32ANDROID_BUTTON_LEFT, x, y)) {
      return;
    }
    if (step.removal) {
      Release(step.command, step.units);
    } else {
      if (step.shift && !gClicks.holdsShift) {
        HoldShift();
        gClicks.holdsShift = true;
      }
      const bool lift = !step.shift && gShiftHolders > 0;
      if (lift) {
        Win32Android_Key(VK_SHIFT, kShiftScanCode, FALSE, FALSE, 0);
      }
      Win32Android_MouseButton(WIN32ANDROID_BUTTON_LEFT, TRUE, x, y);
      Win32Android_MouseButton(WIN32ANDROID_BUTTON_LEFT, FALSE, x, y);
      if (lift) {
        Win32Android_Key(VK_SHIFT, kShiftScanCode, TRUE, FALSE, 0);
      }
      gClicks.counting = true;
      gClicks.ordered = 0;
      gClicks.orderedTarget = griffundo::kNoTarget;
      ++gClicks.acted;
    }
    NextStep();
    Win32Android_RunOnWindowThread(&ClickStepTask, 0);
  }

  /// The next step. First what the last one's input did: what FAF ordered for a click (until FAF orders by one, the tick's next
  /// click is the run's first, which replaces), whether a release took its command out. At a run's first step its units are
  /// selected and a frame is drawn for them. A place the free map does not show, under a panel or out of view, gets the
  /// camera first, as high and turned as it is, and its step after the frame that drew it there. A building's click goes to a
  /// pixel whose world point FAF builds it at, in its cell: where none is, the camera comes closer first. A step that is not
  /// aimed (the tick's) arms FAF's mode and clicks at once; an aimed one (the undo's) arms its mode, moves FAF's cursor to its
  /// place and comes after the next frame, in which FAF's cursor takes the mode and picks what lies there, and again until the
  /// cursor is where the step wants it (CursorAt): its command or target where it is now, read again each time. With no game
  /// behind the UI any more, or after a throw, the steps not come to are not made.
  void ClickStepTask(const WPARAM)
  {
    if (!gClicking) {
      return;
    }
    try {
      if (gClicks.counting) {
        gClicks.counting = false;
        const ClickStep& clicked = gClicks.steps[gClicks.next - 1];
        if (gClicks.ordered == 0) {
          ++gClicks.refused;
        } else {
          ++gClicks.orderedInRun;
          if (clicked.target != griffundo::kNoTarget && gClicks.orderedTarget != clicked.target) {
            ++gClicks.missed;
          }
        }
      }
      if (gClicks.removing) {
        gClicks.removing = false;
        if (!TakenOut(gClicks.removedCommand, gClicks.removedUnits)) {
          ++gClicks.unremoved;
        }
      }
      if (!moho::UI_GamePlaying() || gClicks.next >= gClicks.steps.size()) {
        gClicks.unreached += static_cast<int>(gClicks.steps.size() - gClicks.next);
        FinishClicks();
        return;
      }
      // Shift down again after the frame without it that drew FAF's command graph anew (below), a frame before the cursor
      // counts on it.
      if (gClicks.lifted) {
        gClicks.lifted = false;
        Win32Android_Key(VK_SHIFT, kShiftScanCode, TRUE, FALSE, 0);
        WaitForFrame();
        return;
      }
      const ClickStep& step = gClicks.steps[gClicks.next];
      if (step.run != gClicks.run) {
        if (gClicks.holdsShift && !gClicks.appendAll) {
          ReleaseShift();
          gClicks.holdsShift = false;
        }
        gClicks.run = step.run;
        gClicks.orderedInRun = 0;
        if (!step.select.empty()) {
          if (!SelectRun(step)) {
            while (gClicks.next < gClicks.steps.size() && gClicks.steps[gClicks.next].run == step.run) {
              NextStep();
            }
            Win32Android_RunOnWindowThread(&ClickStepTask, 0);
            return;
          }
          // The undo's runs hold Shift through all of their clicks, from a frame before the first: FAF draws the command
          // graph whose waypoints its release picks only while Shift is down (CUIWorldView::RenderCommandGraph).
          if (gClicks.appendAll && !gClicks.holdsShift) {
            HoldShift();
            gClicks.holdsShift = true;
          }
          WaitForFrame();
          return;
        }
      }
      // Where the step goes now: the waypoint of a removal's command, read again each pass, as it goes with what the command
      // targets (a unit walking on); a click's target where it is now.
      griffundo::Place place = step.place;
      moho::CWldSession* const session = moho::WLD_GetActiveSession();
      if (step.removal) {
        moho::UserCommandIssueHelper* const helper = moho::FindCommandIssueHelperInSession(session, step.command);
        const bool released = std::find(gClicks.released.begin(), gClicks.released.end(), step.command) != gClicks.released.end();
        const auto holds = [helper](const int id) {
          moho::UserUnit* const unit = UnitOf(id);
          return unit != nullptr && Holds(*unit, helper);
        };
        const bool held = helper != nullptr && !released && std::any_of(step.units.begin(), step.units.end(), holds);
        if (!held) {
          // Done by its units meanwhile, or released already in another's place: there is nothing to take out.
          NextStep();
          Win32Android_RunOnWindowThread(&ClickStepTask, 0);
          return;
        }
        const Wm3::Vector3f at = moho::ResolveCommandGraphAnchorWorldPosition(*helper);
        if (!std::isfinite(at.x) || !std::isfinite(at.z)) {
          // On an entity FAF's UI does not find: FAF draws its waypoint nowhere.
          SkipStep(gClicks.unremoved);
          return;
        }
        place = griffundo::Place{at.x, at.y, at.z};
      } else if (step.target != griffundo::kNoTarget) {
        moho::UserEntity* const target =
          session != nullptr ? session->LookupEntityId(static_cast<moho::EntId>(step.target)) : nullptr;
        if (target == nullptr) {
          SkipStep(gClicks.missed);
          return;
        }
        const Wm3::Vector3f& at = target->mVariableData.mCurTransform.pos_;
        place = griffundo::Place{at.x, at.y, at.z};
      }
      const Selection selection = gSelection;
      moho::CUIWorldView* mainMap = nullptr;
      const griffplace::FreeMap free = selection.viewKnown ? FreeMapOver(selection.view, &mainMap) : griffplace::FreeMap{};
      MapView map;
      float x = 0;
      float y = 0;
      if (MapOf(mainMap, map) && Project(map, place.x, place.y, place.z, x, y) && free.DiscFree(x, y, kClickRoom)) {
        int clickX = static_cast<int>(std::lround(x));
        int clickY = static_cast<int>(std::lround(y));
        if (step.builds && !BuildPixel(map, step, x, y, clickX, clickY)) {
          // FAF would build it in a cell beside its place: no pixel's world point is in that cell here, as where a pixel spans
          // more than one. Once the camera has come closer and still none is, it is not ordered.
          if (gClicks.closer) {
            SkipStep(gClicks.unreached);
            return;
          }
          gClicks.closer = true;
          gClicks.looked = true;
          LookAt(place, Closer(map, place));
          WaitForFrame();
          return;
        }
        if (step.aimed) {
          // Where FAF's cursor is found right, the click goes by what decides where it lands: FAF's hover at the aim, a
          // building's cell under this frame's pixel, the ground at the aim while the place is shown there still; else the
          // cursor goes to the place again, as below.
          if (gClicks.aims > 0 && CursorAt(step)) {
            switch (griffundo::AimedClick(griffundo::LandingOf(step.removal, step.onUnit, step.builds), gClicks.aimX, gClicks.aimY,
                                          clickX, clickY)) {
              case griffundo::Aim::kAtAim:
                Act(step, gClicks.aimX, gClicks.aimY);
                return;
              case griffundo::Aim::kHere:
                Act(step, clickX, clickY);
                return;
              case griffundo::Aim::kAgain:
                break;
            }
          }
          const int hovered = session != nullptr ? session->CursorInfo().mIsDragger : -1;
          if (gClicks.aims > 0 && step.removal && TakeInstead(hovered)) {
            return;
          }
          // A command released already keeps its waypoint where FAF's cursor picks it, before others of the selection's,
          // until the sim took its removal and FAF let its command go: the step waits for that, within the sim's patience,
          // and counts none of those frames. FAF's graph keeps the waypoint of a command it let go, too, until it draws the
          // graph anew, which it does after a frame without Shift (CUIWorldView::RenderCommandGraph lets it go): the key goes
          // up for a frame, once a step, as Act's lift, whatever else holds Shift (a held Plus, gShiftHolders).
          const bool released = std::find(gClicks.released.begin(), gClicks.released.end(), hovered) != gClicks.released.end();
          const bool alive = released && moho::FindCommandIssueHelperInSession(session, hovered) != nullptr;
          const bool waits = step.removal && alive && Now() - gClicks.releasedAt < kSimPatience;
          if (step.removal && released && !alive && !gClicks.redrawn && gClicks.holdsShift) {
            Win32Android_Key(VK_SHIFT, kShiftScanCode, FALSE, FALSE, 0);
            gClicks.lifted = true;
            gClicks.redrawn = true;
            WaitForFrame();
            return;
          }
          if (gClicks.aims > kClickFrames && !waits) {
            SkipStep(step.removal ? gClicks.unremoved : gClicks.missed);
            return;
          }
          if (gClicks.aims == 0 && !step.removal) {
            CallGriffLine(step.arm);
          }
          Win32Android_MouseMove(clickX, clickY);
          gClicks.aimX = clickX;
          gClicks.aimY = clickY;
          if (!waits || gClicks.aims == 0) {
            ++gClicks.aims;
          }
          WaitForFrame();
          return;
        }
        if ((gClicks.orderedInRun > 0 || gClicks.appendAll) && !gClicks.holdsShift) {
          HoldShift();
          gClicks.holdsShift = true;
        }
        if (PressWaits(WIN32ANDROID_BUTTON_LEFT, clickX, clickY)) {
          return;
        }
        CallGriffLine(step.arm);
        // A mouse's click: a finger's press would only take FAF's ghost along (Win32Android_TouchButton).
        Win32Android_MouseButton(WIN32ANDROID_BUTTON_LEFT, TRUE, clickX, clickY);
        Win32Android_MouseButton(WIN32ANDROID_BUTTON_LEFT, FALSE, clickX, clickY);
        gClicks.counting = true;
        gClicks.ordered = 0;
        gClicks.orderedTarget = griffundo::kNoTarget;
        ++gClicks.acted;
        NextStep();
        Win32Android_RunOnWindowThread(&ClickStepTask, 0);
        return;
      }
      if (gClicks.waited == 0) {
        LookAt(place, 1);
        gClicks.looked = true;
      }
      if (++gClicks.waited > kClickFrames) {
        SkipStep(step.removal ? gClicks.unremoved : gClicks.unreached);
        return;
      }
      WaitForFrame();
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
      gClicks.unreached += static_cast<int>(gClicks.steps.size() - gClicks.next);
      FinishClicks();
    }
  }

  // --- The tick: FAF's build mode and a mouse's click per plan ------------------------------------------------------------

  /// The watch over the tick's orders: one undo for all of its clicks.
  int gCommitWatch = 0;

  /// The tick is over: its orders are one undo, FAF's build mode it left ended, the selection as the player had it, the draft
  /// closed; the activity hears of plans no click ordered and of the rail's buttons the tick used. With no game, only that.
  void FinishCommit()
  {
    if (moho::UI_GamePlaying()) {
      EndWatchTask(static_cast<WPARAM>(gCommitWatch));
      moho::CON_Execute(kCommitEnd);
      moho::CON_Execute(kDraftEnded);
    }
    gCommitWatch = 0;
    const int notOrdered = gClicks.unreached + gClicks.refused;
    if (notOrdered > 0) {
      Hint(kHintNotOrdered, notOrdered);
    }
    Say(kSaidCommitted, gClicks.appendAll ? kRailQueue : 0, 0);
    ForgetDraft();
  }

  /// The tick (argument 1: Plus counts): no plans, no map's view to click on, no engineers, or nothing that can be built says
  /// so; else the builders are selected (griff.lua CommitBegin: the selected engineers, or the draft's that live) and each
  /// plan FAF would build is a click: FAF's build mode for its building, as the construction panel arms it for a build icon
  /// (FAF's build templates cleared first), in its cell, the first replacing what the builders do unless Plus counted. It
  /// waits while the undo's clicks run, or a finger's drag holds FAF's dragger (a pan's camera drag, a panel's), which would
  /// take the clicks' cursor and come between them: it comes after them, from the next frame on (PlaceTask), as the undo
  /// does; and while an undo the button asked for waits for the sim, as its record is a gesture's from before the tick, whose
  /// orders would replace it. Clicks that ran past their patience are over first, as their own end has them: the tick's own
  /// closes its draft, and its 'not ordered' stays the last word.
  void DraftCommitTask(const WPARAM plus)
  {
    gCommitAsked = 0;
    try {
      if (!moho::UI_GamePlaying()) {
        return;
      }
      const bool clicking = ClicksRun();
      if (!gDraft.Open()) {
        return;
      }
      if (clicking || gDrag != Drag::kNone || gRecord.asked) {
        gCommitAsked = plus + 1;
        return;
      }
      if (gDraft.Plans().empty()) {
        Hint(kHintNoPlan);
        return;
      }
      // Without the map's view each click would only wait for frames that never show its place (griff.lua's report of the
      // view failed): no plan is ordered, and the draft stays.
      if (!gSelection.viewKnown) {
        Hint(kHintNotOrdered, static_cast<int>(gDraft.Plans().size()));
        return;
      }
      if (Ask(kCommitBegin) != kYes) {
        Hint(kHintNoEngineer);
        return;
      }
      const std::vector<griffdraft::Plan> buildable = gDraft.Buildable(GroundFree);
      if (buildable.empty()) {
        Hint(kHintAllOnBuildings);
        moho::CON_Execute(kCommitEnd);
        return;
      }
      if (buildable.size() < gDraft.Plans().size()) {
        Hint(kHintSomeOnBuildings, static_cast<int>(gDraft.Plans().size() - buildable.size()));
      }
      Clicks clicks;
      for (const griffdraft::Plan& plan : buildable) {
        ClickStep step;
        step.arm = "StartBuild('" + gDraft.BuildingOf(plan).id + "')";
        step.place = griffundo::Place{plan.x, SurfaceAt(plan.x, plan.z), plan.z};
        step.builds = true;
        step.building = gDraft.BuildingOf(plan);
        clicks.steps.push_back(std::move(step));
      }
      clicks.appendAll = plus != 0;
      clicks.finish = &FinishCommit;
      // The builders CommitBegin selected are the tick's units: their queues as they are now, for its undo, which the tick
      // always offers.
      gCommitWatch = WatchOrders(false);
      gWatch.tick = gCommitWatch != 0;
      StartClicks(std::move(clicks));
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  // --- The undo: its button takes the last gesture's orders back -----------------------------------------------------------

  /// The call of griff.lua's that arms FAF's mode for an order given again: the construction panel's build mode for its
  /// building, else the mode the orders panel arms for its way (griff.lua Arm, by the number of griffundo::Way).
  [[nodiscard]] std::string ArmCall(const griffundo::Click& click)
  {
    if (click.way == griffundo::Way::kBuild) {
      return "StartBuild('" + click.blueprint + "')";
    }
    return "Arm(" + std::to_string(static_cast<int>(click.way)) + ")";
  }

  /// The undo's record, whose undo was not done and nothing of it clicked, holds as it was: the button offers it again for its
  /// time, as the activity is told the record anew (Undo.ordered after Undo.undone).
  void OfferAgain()
  {
    gRecord.asked = false;
    Say(kSaidOrdered, gRecord.number, 0);
  }

  /// The undo's clicks are over: FAF's mode they left ended, the selection the player had; the activity hears whether all of
  /// the gesture went and came back (an upgrade FAF ordered for one of its assists goes on; what the plan was unsure of counts
  /// as not), part of it (also where nothing was left to click, as for a factory's builds a Stop took), or nothing, where it
  /// had steps and none of them made a release or a click: as where its clicks ended before their first step (no frame came,
  /// the game ended, a throw). Nothing was clicked then, so the record holds as it was and the button offers it again, while
  /// the game goes on and no other order came meanwhile, which would have ended it. The Guards of the record and those the
  /// undo gave back wait for an upgrade FAF's assist-to-upgrade orders only after this word, for the game's next few ticks
  /// (gUndone), while a game is playing: none outlives it.
  void FinishUndo()
  {
    const bool playing = moho::UI_GamePlaying();
    if (playing) {
      moho::CON_Execute(kUndoEnd);
    }
    const int notDone = gUndoing.dropped + gUndoing.unsure + gUndoing.record.upgrades + gClicks.unreached + gClicks.refused
                      + gClicks.missed + gClicks.unremoved + gClicks.leftOut;
    const bool whole = notDone == 0 && !gUndoing.record.unknown;
    const bool none = gClicks.acted == 0 && !gClicks.steps.empty();
    const int said = whole ? kUndoneWhole : none ? kNotUndone : kUndoneInPart;
    SayUndone(gUndoing.record.number, said);
    // The gesture taken back whole: its units are in the groups they were in before it, where no other gesture took them.
    if (whole) {
      gGroups.Undone(gUndoing.record.gestures, gUndoing.record.earlier);
    }
    if (playing) {
      const std::unordered_set<int>& guarded = gUndoing.record.guarded;
      gUndone.Finished(gUndoing.record.number, std::vector<int>(guarded.begin(), guarded.end()),
                       std::vector<int>(gUndoing.again.begin(), gUndoing.again.end()), said == kUndoneWhole, GameTick());
    }
    if (said == kNotUndone && gClicks.orders == 0 && playing) {
      gRecord = std::move(gUndoing.record);
      OfferAgain();
    }
    gUndoing = Undoing{};
  }

  /// The undo's steps, once the sim took the gesture: of the record's units that live, each queue an order of the gesture's
  /// went into (a factory's own and its rally route apart), as it began, with what the program read of the orders the sim had
  /// not taken then, as the sim held it when the replacement landed, and now, the commands the gesture put there, the building
  /// each unit was building then if unfinished still, and where what their orders before target is now (include/GriffUndo.h);
  /// then each command of the gesture's taken out, in a run of their units, then each order lost given again, in a run for
  /// each group of units whose orders come back alike; the orders that cannot come back or go counted (`dropped`), those the
  /// plan gives back or leaves without knowing whose they are or that the units held them still (`unsure`), and the targets
  /// of the Guards it gives back (`again`). A unit the gesture ordered whose queue was not known as it began is not taken
  /// back.
  [[nodiscard]] std::vector<ClickStep> PlanUndoSteps(const Record& record, int& dropped, int& unsure, std::unordered_set<int>& again)
  {
    std::vector<ClickStep> steps;
    moho::CWldSession* const session = moho::WLD_GetActiveSession();
    if (session == nullptr) {
      return steps;
    }
    const auto living = [session](const int id) {
      return session->LookupEntityId(static_cast<moho::EntId>(id));
    };
    std::vector<griffundo::Unit> units;
    griffundo::Targets targets;
    std::vector<int> ids;
    for (const auto& kept : record.stored) {
      ids.push_back(kept.first);
    }
    std::sort(ids.begin(), ids.end());
    for (const int id : ids) {
      const Stored& kept = record.stored.at(id);
      moho::UserUnit* const unit = UnitOf(id);
      if (unit == nullptr) {
        continue;
      }
      moho::UserUnit* const building = UnitOf(kept.focus);
      const int site = building != nullptr && building->IsBeingBuilt() ? kept.focus : griffundo::kNoTarget;
      for (const bool route : {false, true}) {
        const Queue& queue = route ? kept.route : kept.own;
        moho::UserCommandQueue* const commands = CommandsOf(*unit, route);
        if (!queue.ordered || commands == nullptr) {
          continue;
        }
        griffundo::Unit planned;
        planned.id = id;
        planned.replaced = queue.replaced;
        planned.refilled = route;
        planned.before = queue.before;
        for (griffundo::Entry& entry : planned.before) {
          entry.pending = griffundo::PendingOf(queue.unsynced, entry.id, queue.snapshot, static_cast<int>(queue.beat));
          // A Guard FAF's UI may have answered with commands of the sim's own: on a unit its capping may ring, or given with
          // Ctrl; one on a unit gone may have been either.
          if (entry.type == "Guard" && entry.target != griffundo::kNoTarget) {
            const moho::UserEntity* const guarded = living(entry.target);
            entry.answered = guarded == nullptr || CappingRings(*guarded) || gGuardsWithCtrl.count(entry.id) != 0;
          }
        }
        // Where no view of the sim's was read (the replacing order put no Reset at the queue's front, which ISSUE_Command
        // and ISSUE_FactoryCommand always do), what the queue held as the gesture began stands for what the sim held.
        planned.taken = queue.snapshot ? queue.taken : queue.before;
        planned.stale = queue.snapshot && queue.stale;
        planned.now = QueueOf(commands);
        planned.given = queue.given;
        planned.site = route ? griffundo::kNoTarget : site;
        std::vector<int> aimedAt{planned.site};
        for (const std::vector<griffundo::Entry>* const orders : {&planned.before, &planned.taken}) {
          for (const griffundo::Entry& entry : *orders) {
            aimedAt.push_back(entry.target);
          }
        }
        for (const int target : aimedAt) {
          if (moho::UserEntity* const entity = target != griffundo::kNoTarget ? living(target) : nullptr; entity != nullptr) {
            const Wm3::Vector3f& at = entity->mVariableData.mCurTransform.pos_;
            targets[target] = griffundo::Place{at.x, at.y, at.z};
          }
        }
        units.push_back(std::move(planned));
      }
    }
    const griffundo::Plan plan = griffundo::PlanUndo(units, targets);
    dropped = plan.dropped;
    unsure = plan.unsure;
    std::vector<int> removing;
    for (const griffundo::Removal& removal : plan.removals) {
      for (const int unit : removal.units) {
        if (std::find(removing.begin(), removing.end(), unit) == removing.end()) {
          removing.push_back(unit);
        }
      }
    }
    for (const griffundo::Removal& removal : plan.removals) {
      ClickStep step;
      step.removal = true;
      step.aimed = true;
      step.command = removal.command;
      step.units = removal.units;
      step.select = steps.empty() ? removing : std::vector<int>{};
      steps.push_back(std::move(step));
    }
    for (std::size_t group = 0; group < plan.groups.size(); ++group) {
      const std::vector<griffundo::Click>& clicks = plan.groups[group].clicks;
      for (std::size_t at = 0; at < clicks.size(); ++at) {
        const griffundo::Click& click = clicks[at];
        ClickStep step;
        step.arm = ArmCall(click);
        step.place = click.place;
        step.run = static_cast<int>(group) + 1;
        step.select = at == 0 ? plan.groups[group].units : std::vector<int>{};
        step.aimed = true;
        step.way = click.way;
        step.shift = click.shift;
        step.target = click.target;
        step.onUnit = click.target != griffundo::kNoTarget && UnitOf(click.target) != nullptr;
        // A building given again at its place lands in its cell, as the tick's.
        step.builds = click.way == griffundo::Way::kBuild && BlueprintBuilding(click.blueprint.c_str(), step.building) != nullptr;
        if (click.way == griffundo::Way::kGuard && click.target != griffundo::kNoTarget) {
          again.insert(click.target);
        }
        steps.push_back(std::move(step));
      }
    }
    return steps;
  }

  /// The undo the button asked for, once the sim took what the gesture ordered its units, so that its plan reads the queues the sim holds
  /// (a paused game's after it resumed, a lagging one's its lag later), no clicks run and no finger's drag holds FAF's
  /// dragger, which would take the clicks' cursor and come between them (a pan's camera drag, a box's, a panel's): until then
  /// the fingers are free and the button says it waits, and the next order ends the record as ever. Then the record is taken
  /// (one level), griff.lua keeps the selection, ends a command mode and holds FAF's capping off (UndoBegin), and the clicks
  /// run. Without the map's view, which their places are found on, or where the plan threw, nothing is clicked and the button
  /// offers the record again. All of the undo's inputs go to the sim after the gesture's, which it takes in order. Clicks
  /// that ran past their patience are over first: the tick's orders they end are a record of their own, which an undo asked
  /// for the one before does not take back.
  void TryUndo()
  {
    if (ClicksRun() || !gRecord.asked || gRecord.number == 0 || !moho::UI_GamePlaying() || gDrag != Drag::kNone) {
      return;
    }
    ReadTaken(gRecord.stored);
    if (!SimTook(gRecord.stored)) {
      return;
    }
    if (!gSelection.viewKnown) {
      SayUndone(gRecord.number, kNotUndone);
      OfferAgain();
      return;
    }
    Clicks clicks;
    int dropped = 0;
    int unsure = 0;
    std::unordered_set<int> again;
    try {
      clicks.steps = PlanUndoSteps(gRecord, dropped, unsure, again);
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
      SayUndone(gRecord.number, kNotUndone);
      OfferAgain();
      return;
    }
    gUndoing = Undoing{};
    gUndoing.record = std::move(gRecord);
    gUndoing.dropped = dropped;
    gUndoing.unsure = unsure;
    gUndoing.again = std::move(again);
    gRecord = Record{};
    // FAF's capping answers a Guard of engineers on a structure with rings of its own, by its memory of the player's taps
    // and the Shift held: griff.lua holds it off while the undo's clicks run, and says so. Where it could not, a Guard given
    // back on a structure it may ring may bring rings nobody ordered, and counts.
    if (Ask(kUndoBegin) != kYes) {
      gUndoing.unsure += static_cast<int>(std::count_if(gUndoing.again.begin(), gUndoing.again.end(), [](const int target) {
        const moho::UserUnit* const unit = UnitOf(target);
        return unit != nullptr && CappingRings(*unit);
      }));
    }
    clicks.finish = &FinishUndo;
    clicks.appendAll = true;
    StartClicks(std::move(clicks));
  }

  /// The undo of a draft the cross or two fingers' tap dropped (Kept::kDraft): it is open again as it was, its builders with
  /// it (griff.lua DraftBack), at once, as nothing is clicked and nothing goes to the sim. No other draft is open by then, as
  /// one that opens ends the record (PickForDraft), and no clicks run, which only an open draft's tick or an order record's
  /// undo starts.
  void DraftBack()
  {
    Record record = std::move(gRecord);
    gRecord = Record{};
    if (!moho::UI_GamePlaying()) {
      SayUndone(record.number, kNotUndone);
      return;
    }
    gDraft = std::move(record.draft);
    gDraftBlueprints = std::move(record.blueprints);
    ShowDraftOpen(gDraft.Picked());
    moho::CON_Execute(kDraftBack);
    SayUndone(record.number, kUndoneWhole);
  }

  /// The undo of a control group a rest stored over (Kept::kGroup): the units it held that live are the group again, as FAF
  /// stores a selection (griff.lua GroupBack), at once, as nothing is clicked or ordered; with none of them alive nothing is
  /// given back, and the group stays as the store left it.
  void GroupBack()
  {
    Record record = std::move(gRecord);
    gRecord = Record{};
    const bool back =
      moho::UI_GamePlaying()
      && Ask((std::string(kGriffLua) + "GroupBack(" + std::to_string(record.group) + ", " + UnitList(record.held) + ")").c_str()) == kYes;
    SayUndone(record.number, back ? kUndoneWhole : kNotUndone);
  }

  /// The undo's button was tapped for record `number`: a gesture's orders taken back, now or once the tick's or another undo's
  /// clicks are over; what a gesture of Griff's own threw away put back at once. A record replaced or ended meanwhile is not
  /// taken back (one level).
  void UndoTask(const WPARAM number)
  {
    try {
      if (gRecord.number == 0 || static_cast<WPARAM>(gRecord.number) != number) {
        return;
      }
      switch (gRecord.kept) {
        case Kept::kOrders:
          gRecord.asked = true;
          TryUndo();
          break;
        case Kept::kDraft:
          DraftBack();
          break;
        case Kept::kGroup:
          GroupBack();
          break;
      }
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  /// The cross, or two fingers' tap in the draft: it goes, with nothing ordered, and one of plans enough is a record of Griff's
  /// own (griffundo::DropSavesWork), which the undo's button brings back as it was, at rest where its plans stood. Not while
  /// the program's clicks run, which end the tick's draft themselves; a tick that waits goes all the same.
  void DraftDropTask(const WPARAM)
  {
    gCommitAsked = 0;
    try {
      if (ClicksRun() || !gDraft.Open()) {
        return;
      }
      const bool playing = moho::UI_GamePlaying();
      Record dropped;
      if (playing && griffundo::DropSavesWork(gDraft.Plans().size())) {
        dropped.kept = Kept::kDraft;
        dropped.draft = gDraft;
        dropped.draft.MoveEnd();
        dropped.blueprints = gDraftBlueprints;
      }
      ForgetDraft();
      if (playing) {
        moho::CON_Execute(kDraftEnded);
      }
      if (dropped.kept == Kept::kDraft) {
        Offer(std::move(dropped));
      }
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  /// A tap on the cross, Griff's Esc (EscapeNow): the draft goes as the cross dropped it, else FAF's armed command mode ends,
  /// else the selection goes, as FAF's HandleEsc puts them away. The mode and the selection not between the program's clicks,
  /// whose they are, as for two fingers' tap.
  void EscapeTask(const WPARAM)
  {
    try {
      const grifftap::Escape escape = EscapeNow();
      if (escape == grifftap::Escape::kDropDraft) {
        DraftDropTask(0);
      } else if (escape != grifftap::Escape::kNothing && moho::UI_GamePlaying() && !ClicksRun()) {
        moho::CON_Execute(escape == grifftap::Escape::kEndCommandMode ? kEndCommandMode : kDeselect);
      }
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  /// A draft gesture's task by its phase (Game.DRAFT_): shown as the finger moves, which coalesces, its end, or given up.
  void RunDraft(const int phase, void (*show)(WPARAM), void (*end)(WPARAM), void (*abort)(WPARAM), const WPARAM places)
  {
    if (phase == kDraftShow) {
      Win32Android_RunNewestOnWindowThread(show, places);
    } else if (phase == kDraftEnd) {
      Win32Android_RunOnWindowThread(end, places);
    } else if (phase == kDraftAbort) {
      Win32Android_RunOnWindowThread(abort, places);
    }
  }

  /// A velocity for a task's argument (SignedCoordinate).
  [[nodiscard]] int Velocity(const float pixelsASecond)
  {
    return static_cast<int>(std::clamp(pixelsASecond, -32767.0f, 32767.0f));
  }
} // namespace

/// The scheme is on or off; the handle's radius, how far from its middle a finger is on it, its room from the group's units and
/// how far from its middle its arrow reaches; the tap help's reach and what counts as a small control; how far beside a unit a
/// tap picks it, without troops and with them; the speed a second under which the camera's glide stops; how far from a finger
/// the draft finds a deposit: in the picture's pixels.
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_setGriff(
  JNIEnv*, jclass, const jboolean on, const jfloat radius, const jfloat touchRadius, const jfloat gap, const jfloat arrowReach,
  const jfloat tapReach, const jfloat tapSmallSide, const jfloat nearReach, const jfloat nearReachWithTroops, const jfloat glideStop,
  const jfloat depositReach
)
{
  gRadius.store(radius);
  gTouchRadius.store(touchRadius);
  gGap.store(gap);
  gArrowReach.store(arrowReach);
  gTapReach.store(tapReach);
  gTapSmallSide.store(tapSmallSide);
  gNearReach.store(nearReach);
  gNearReachWithTroops.store(nearReachWithTroops);
  gGlideStop.store(glideStop);
  gDepositReach.store(depositReach);
  gOn.store(on != JNI_FALSE);
}

/// Where the app's own pieces lie over the picture, `count` boxes of left, top, right and bottom in its pixels: the handle keeps
/// clear of them, and a gesture finds none of the game's under them.
extern "C" JNIEXPORT void JNICALL
Java_io_github_m3rt1n99_fafre_Game_setGriffCovers(JNIEnv* const env, jclass, const jintArray boxes, const jint count)
{
  const jint values = (std::max)(0, (std::min)(4 * count, env->GetArrayLength(boxes)));
  std::vector<jint> numbers(static_cast<std::size_t>(values));
  env->GetIntArrayRegion(boxes, 0, values, numbers.data());
  std::vector<Box> covers;
  for (jint at = 0; at + 3 < values; at += 4) {
    covers.push_back(Box{static_cast<float>(numbers[at]), static_cast<float>(numbers[at + 1]), static_cast<float>(numbers[at + 2]),
                         static_cast<float>(numbers[at + 3])});
  }
  const std::lock_guard<std::mutex> lock(gCoversMutex);
  gCovers = std::move(covers);
}

/// The handle as the last frame placed it, into x, y, the group's middle x and y, the selection's count and 1 if it points at
/// the group from elsewhere, else 0; the free map's left, top and right at its middle; the stroke last answered for and what
/// its end is on. False when there is no handle, or no room for it.
extern "C" JNIEXPORT jboolean JNICALL Java_io_github_m3rt1n99_fafre_Game_griff(JNIEnv* const env, jclass, const jfloatArray handle)
{
  if (env->GetArrayLength(handle) < kHandleValues) {
    return JNI_FALSE;
  }
  Handle now;
  griffplace::Cross free;
  {
    const std::lock_guard<std::mutex> lock(gHandleMutex);
    now = gHandle;
    free = gFree;
  }
  const std::int64_t answer = gStrokeAnswer.load();
  const jfloat values[kHandleValues] = {
    now.x,     now.y,    now.groupX, now.groupY, static_cast<jfloat>(now.count), now.off ? 1.0f : 0.0f,
    free.left, free.top, free.right, static_cast<jfloat>(answer >> 32), static_cast<jfloat>(answer & 0xFFFFFFFF),
  };
  env->SetFloatArrayRegion(handle, 0, kHandleValues, values);
  return now.shown && gOn.load() ? JNI_TRUE : JNI_FALSE;
}

/// What the program said since the last call, three numbers each, as many as fit into `said`; how many numbers.
extern "C" JNIEXPORT jint JNICALL Java_io_github_m3rt1n99_fafre_Game_griffSaid(JNIEnv* const env, jclass, const jintArray said)
{
  const jsize room = env->GetArrayLength(said) / 3;
  std::vector<jint> values;
  {
    const std::lock_guard<std::mutex> lock(gSaidMutex);
    while (!gSaid.empty() && static_cast<jsize>(values.size() / 3) < room) {
      values.insert(values.end(), gSaid.front().begin(), gSaid.front().end());
      gSaid.pop_front();
    }
  }
  env->SetIntArrayRegion(said, 0, static_cast<jsize>(values.size()), values.data());
  return static_cast<jint>(values.size());
}

/// A first finger came down here (DownTask); `box`: the rail's Box counts.
extern "C" JNIEXPORT void JNICALL
Java_io_github_m3rt1n99_fafre_Game_griffDown(JNIEnv*, jclass, const jint x, const jint y, const jboolean box)
{
  Win32Android_RunOnWindowThread(&DownTask, Places(x, y, box != JNI_FALSE ? 1 : 0));
}

/// A tap's click by what it lands on, or a step of its ladder (SmartTapTask).
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_smartTap(
  JNIEnv*, jclass, const jint x, const jint y, const jint rail, const jint step, const jint baseX, const jint baseY, const jint tap
)
{
  {
    const std::lock_guard<std::mutex> lock(gTapsMutex);
    gTaps.push_back(Tap{x, y, rail, step, baseX, baseY, tap});
  }
  Win32Android_RunOnWindowThread(&SmartTapTask, 0);
}

extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_deselect(JNIEnv*, jclass)
{
  Win32Android_RunOnWindowThread(&DeselectTask, 0);
}

/// The camera goes to the selection, by a flight or at once.
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_toGroup(JNIEnv*, jclass, const jboolean fly)
{
  Win32Android_RunOnWindowThread(&ToGroupTask, fly != JNI_FALSE ? 1 : 0);
}

/// A finger rests on the handle, rest number `rest` (HandleRestTask).
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_handleRest(JNIEnv*, jclass, const jint rest)
{
  Win32Android_RunOnWindowThread(&HandleRestTask, static_cast<WPARAM>(rest & 0xFFFF));
}

/// The lift of the handle's rest that distributes (DistributeTask).
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_distribute(JNIEnv*, jclass)
{
  Win32Android_RunOnWindowThread(&DistributeTask, 0);
}

/// A route's waypoints, `count` places as x, y pairs in the picture's pixels; `rail`: the RAIL_ bits that count for it; `patrol`:
/// a loop.
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_route(
  JNIEnv* const env, jclass, const jintArray places, const jint count, const jint rail, const jboolean patrol
)
{
  if (count <= 0 || env->GetArrayLength(places) < 2 * count) {
    return;
  }
  std::vector<jint> values(static_cast<std::size_t>(2 * count));
  env->GetIntArrayRegion(places, 0, 2 * count, values.data());
  Route route;
  route.rail = rail;
  route.patrol = patrol != JNI_FALSE;
  for (jint at = 0; at < count; ++at) {
    route.points.push_back(POINT{values[2 * at], values[2 * at + 1]});
  }
  {
    const std::lock_guard<std::mutex> lock(gRoutesMutex);
    gRoutes.push_back(std::move(route));
  }
  Win32Android_RunOnWindowThread(&BeginRouteTask, 0);
  for (jint at = 0; at < count; ++at) {
    Win32Android_RunOnWindowThread(&RouteStepTask, 0);
  }
  Win32Android_RunOnWindowThread(&EndRouteTask, 0);
}

/// The cursor hovers at a stroke's end (StrokeHoverTask), for stroke number `stroke`.
extern "C" JNIEXPORT void JNICALL
Java_io_github_m3rt1n99_fafre_Game_strokeHover(JNIEnv*, jclass, const jint x, const jint y, const jint stroke)
{
  Win32Android_RunOnWindowThread(&StrokeHoverTask, Places(x, y, stroke & 0xFFFF));
}

/// A finger's drag from here, the finger now at `nowX`, `nowY` (DragTask); `rail`: the RAIL_ bits that count for it; `drag`: its
/// number, which the program says when it made a pan of it.
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_griffDrag(
  JNIEnv*, jclass, const jint x, const jint y, const jint nowX, const jint nowY, const jboolean rested, const jint rail,
  const jint drag
)
{
  {
    const std::lock_guard<std::mutex> lock(gDragsMutex);
    gDrags.push_back(DragStart{x, y, nowX, nowY, rested != JNI_FALSE, rail, drag});
  }
  Win32Android_RunOnWindowThread(&DragTask, 0);
}

/// The drag ends here; a pan's camera glides on at this velocity in the picture's pixels a second.
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_griffDragEnd(
  JNIEnv*, jclass, const jint x, const jint y, const jfloat velocityX, const jfloat velocityY
)
{
  Win32Android_RunOnWindowThread(&DragEndTask, Places(x, y, Velocity(velocityX), Velocity(velocityY)));
}

/// The drag goes without its lift, here: FAF's formation given up, any other drag ended without a glide (DragGiveUpTask).
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_griffDragGiveUp(JNIEnv*, jclass, const jint x, const jint y)
{
  Win32Android_RunOnWindowThread(&DragGiveUpTask, Places(x, y));
}

/// A finger rested here and lifted (RestTapTask).
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_restTap(JNIEnv*, jclass, const jint x, const jint y, const jint rail)
{
  Win32Android_RunOnWindowThread(&RestTapTask, Places(x, y, rail));
}

/// A finger rests here on the map, without Box (RestTask): a waypoint marked, or the ground for a formation's right button.
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_griffRest(JNIEnv*, jclass, const jint x, const jint y)
{
  Win32Android_RunOnWindowThread(&RestTask, Places(x, y));
}

/// The resting finger's gesture went without its lift or a drag (RestGoneTask).
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_griffRestGone(JNIEnv*, jclass)
{
  Win32Android_RunOnWindowThread(&RestGoneTask, 0);
}

/// The camera follows fingers: `start` keeps what lies at x, y under them, then fingers `distance` apart zoom it (0: one).
extern "C" JNIEXPORT void JNICALL
Java_io_github_m3rt1n99_fafre_Game_pinch(JNIEnv*, jclass, const jboolean start, const jint x, const jint y, const jint distance)
{
  if (start != JNI_FALSE) {
    Win32Android_RunOnWindowThread(&PinchStartTask, Places(x, y, distance));
  } else {
    Win32Android_RunNewestOnWindowThread(&PinchTask, Places(x, y, distance));
  }
}

/// The fingers left the camera, which glides on at this velocity in the picture's pixels a second.
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_pinchEnd(JNIEnv*, jclass, const jfloat velocityX, const jfloat velocityY)
{
  Win32Android_RunOnWindowThread(&PinchEndTask, Places(Velocity(velocityX), Velocity(velocityY)));
}

/// Plus held, or let go: Shift's key, in the order of the clicks.
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_holdShift(JNIEnv*, jclass, const jboolean down)
{
  Win32Android_RunOnWindowThread(&ShiftTask, down != JNI_FALSE ? 1 : 0);
}

/// The draft as the last frame drew it into `values` (Game.DRAFT_ layout), as much as fits; how many numbers.
extern "C" JNIEXPORT jint JNICALL Java_io_github_m3rt1n99_fafre_Game_griffDraft(JNIEnv* const env, jclass, const jfloatArray values)
{
  std::vector<float> shown;
  {
    const std::lock_guard<std::mutex> lock(gDraftShownMutex);
    shown = gDraftShown;
  }
  const jsize count = (std::min)(env->GetArrayLength(values), static_cast<jsize>(shown.size()));
  env->SetFloatArrayRegion(values, 0, count, shown.data());
  return count;
}

/// A tap in the draft, on the plan numbered `plan` (0: none) as the activity saw it (DraftTapTask).
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_draftTap(JNIEnv*, jclass, const jint x, const jint y, const jint plan)
{
  Win32Android_RunOnWindowThread(&DraftTapTask, Places(x, y, plan & 0xFFFF, (plan >> 16) & 0xFFFF));
}

/// The line from the newest plan to a finger here, by its phase (Game.DRAFT_).
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_draftLine(JNIEnv*, jclass, const jint phase, const jint x, const jint y)
{
  RunDraft(phase, &DraftLineTask, &DraftLineEndTask, &DraftLineAbortTask, Places(x, y));
}

/// The whole draft follows a finger from where it came down to where it is, by its phase.
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_draftMove(
  JNIEnv*, jclass, const jint phase, const jint fromX, const jint fromY, const jint x, const jint y
)
{
  RunDraft(phase, &DraftMoveTask, &DraftMoveEndTask, &DraftMoveAbortTask, Places(fromX, fromY, x, y));
}

/// A finger that rests here in the draft shows the plan its lift places, by its phase (the abort: none shown).
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_draftAim(JNIEnv*, jclass, const jint phase, const jint x, const jint y)
{
  RunDraft(phase, &DraftAimTask, &DraftAimEndTask, &DraftAimGoneTask, Places(x, y));
}

/// A finger rests here in the draft, Griff's rest number `rest` (DraftRestTask).
extern "C" JNIEXPORT void JNICALL
Java_io_github_m3rt1n99_fafre_Game_draftRest(JNIEnv*, jclass, const jint x, const jint y, const jint rest)
{
  Win32Android_RunOnWindowThread(&DraftRestTask, Places(x, y, rest));
}

/// Box from one corner to the other over deposits, with an extractor picked (DraftBoxTask).
extern "C" JNIEXPORT void JNICALL
Java_io_github_m3rt1n99_fafre_Game_draftBox(JNIEnv*, jclass, const jint x0, const jint y0, const jint x1, const jint y1)
{
  Win32Android_RunOnWindowThread(&DraftBoxTask, Places(x0, y0, x1, y1));
}

/// The tick, with Plus counting or not (DraftCommitTask).
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_draftCommit(JNIEnv*, jclass, const jboolean plus)
{
  Win32Android_RunOnWindowThread(&DraftCommitTask, plus != JNI_FALSE ? 1 : 0);
}

/// Two fingers' tap in the draft (DraftDropTask).
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_draftDrop(JNIEnv*, jclass)
{
  Win32Android_RunOnWindowThread(&DraftDropTask, 0);
}

/// A tap on the cross, Griff's Esc (EscapeTask).
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_griffEscape(JNIEnv*, jclass)
{
  Win32Android_RunOnWindowThread(&EscapeTask, 0);
}

/// Whether the cross shows, as the last frame found Esc.
extern "C" JNIEXPORT jboolean JNICALL Java_io_github_m3rt1n99_fafre_Game_griffEscapes(JNIEnv*, jclass)
{
  return gEscapes.load() ? JNI_TRUE : JNI_FALSE;
}

/// What the player opened of FAF's that shows (FafreGriffOpened), into `values` as many whole boxes as fit, left, top, right and
/// bottom of each in the picture's pixels; how many numbers.
extern "C" JNIEXPORT jint JNICALL Java_io_github_m3rt1n99_fafre_Game_griffOpened(JNIEnv* const env, jclass, const jfloatArray values)
{
  std::vector<float> opened;
  {
    const std::lock_guard<std::mutex> lock(gOpenedMutex);
    opened = gOpened;
  }
  const jsize count = (std::min)(env->GetArrayLength(values) / 4 * 4, static_cast<jsize>(opened.size()));
  env->SetFloatArrayRegion(values, 0, count, opened.data());
  return count;
}

/// A hint of the activity's (Game.HINT_, as a stroke given up), where FAF prints its own messages (griff.lua Hint).
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_griffHint(JNIEnv*, jclass, const jint kind, const jint number)
{
  Win32Android_RunOnWindowThread(&HintTask, Places(kind, number));
}

/// How the rail's Box and Plus look (Rail.look, two bits each, Box's first) with a finger on the draft's tick, its cross and the
/// undo's button (Game.HUD_HELD_), and the undo's button (Game.HUD_UNDO_, the part of its time left), which griffhud.lua draws in
/// FAF's UI (FafreGriffHud).
extern "C" JNIEXPORT void JNICALL
Java_io_github_m3rt1n99_fafre_Game_setGriffHud(JNIEnv*, jclass, const jint looks, const jint undo, const jfloat undoLeft)
{
  gHudLooks.store(looks);
  gUndoShown.store(undo);
  gUndoLeft.store(undoLeft);
}

/// Where griffhud.lua drew Box, Plus, the undo's button, the draft's tick and its cross (FafreGriffHudPlaces), into `values`:
/// left, top, right, bottom in the picture's pixels and 1 where something the player opened of FAF's lies over it or FAF's UI
/// is hidden, for each; how many numbers.
extern "C" JNIEXPORT jint JNICALL Java_io_github_m3rt1n99_fafre_Game_griffHudPlaces(JNIEnv* const env, jclass, const jfloatArray values)
{
  std::array<float, kHudPlaces> places{};
  {
    const std::lock_guard<std::mutex> lock(gHudPlacesMutex);
    places = gHudPlaces;
  }
  const jsize count = (std::min)(env->GetArrayLength(values), static_cast<jsize>(kHudPlaces));
  env->SetFloatArrayRegion(values, 0, count, places.data());
  return count;
}

/// The undo's button was tapped for record `number` (UndoTask).
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_griffUndo(JNIEnv*, jclass, const jint number)
{
  Win32Android_RunOnWindowThread(&UndoTask, static_cast<WPARAM>(number));
}

/// Whether the program's clicks run, the draft's tick's or the undo's: the fingers rest meanwhile. Clicks that have waited for a
/// frame past their patience count as over, which the next task makes them (ClicksRun; a finger's down is one).
extern "C" JNIEXPORT jboolean JNICALL Java_io_github_m3rt1n99_fafre_Game_griffClicking(JNIEnv*, jclass)
{
  return gClicking.load() && !Outwaited(gClicksWaitSince.load(), Now()) ? JNI_TRUE : JNI_FALSE;
}

/// How many runs of the program's clicks have begun: a finger's gesture that one began in gives nothing more (TouchInput).
extern "C" JNIEXPORT jint JNICALL Java_io_github_m3rt1n99_fafre_Game_griffClickRuns(JNIEnv*, jclass)
{
  return gClickRuns.load();
}

bool hostclicks::GriffClicking() noexcept
{
  return gClicking.load();
}

bool hostclicks::GriffHoldsCursor() noexcept
{
  // As the clicks' wait, only within their patience: without frames (the activity in the back) the leave is not held for good.
  return gDistributing.since != 0 && !Outwaited(gDistributing.since, Now());
}
