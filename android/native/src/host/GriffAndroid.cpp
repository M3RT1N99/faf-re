// Griff, the app's touch scheme (buildstage/touch-concept-v2.md, TouchInput): what its gestures do in the game, and where
// the handle over the selection goes. FAF's Lua reports each frame where the selection is on the screen
// (assets/fafre_ui/lua/fafre/griff.lua); the handle goes beside it where the map is free, and the activity reads it to draw it
// and to know a finger on it (Game.griff). A gesture's clicks are a mouse's, and Plus is the keyboard's Shift: FAF decides
// what they order, as for a mouse, and each click picks what is under it at its own place (CUIWorldView::HandleEvent). The
// camera moves as the UI moves it (griff.lua: its glide, its flight to the group, the pinch), not the simulation. They come
// to the program's thread as tasks, in the order of the rest of the input (GameHost.cpp). The draft holds buildings planned
// at world places (include/GriffDraft.h) until the tick orders them by FAF's build mode and a mouse's click per plan. What a
// gesture orders is watched (griff.lua), for the pins at its places (include/GriffPins.h) and the pill that takes it back
// (include/GriffUndo.h), with FAF's own inputs again.

#include <jni.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <exception>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Windows.h>
#include <Win32AndroidHost.h>

#include <GriffDraft.h>
#include <GriffPins.h>
#include <GriffPlace.h>
#include <GriffTap.h>
#include <GriffUndo.h>

#include "HostClicks.h"
#include "MenuFingers.h"
#include "TaskPlaces.h"
#include "gpg/core/utils/Logging.h"
#include "moho/console/CConCommand.h"
#include "moho/lua/CScrLuaBinder.h"
#include "moho/ui/UiPrivate.h"

namespace moho
{
  // src/moho/ui/CUIWorldViewEvents.cpp's, by its patch.
  [[nodiscard]] bool UI_GamePlaying() noexcept;
  [[nodiscard]] bool UI_TapTarget(float x, float y, float reach, float smallSide, float* targetX, float* targetY);
} // namespace moho

namespace
{
  using griffplace::Box;
  using grifftap::kRailAttack;
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

  // TouchInput's TAP_: what a tap was, for its ladder.
  constexpr int kTapUi = 1;
  constexpr int kTapOwn = 2;
  constexpr int kTapGround = 3;
  constexpr int kTapEmpty = 4;
  constexpr int kTapOther = 5;

  // Stroke's END_ that the program decides; griff.lua's StrokeKind answers the others.
  constexpr int kEndPanel = 8;
  constexpr int kEndMinimap = 9;

  // Game's SAID_.
  constexpr int kSaidTap = 1;
  constexpr int kSaidRouteGivenUp = 2;
  constexpr int kSaidToast = 3;
  constexpr int kSaidCommitted = 4;
  constexpr int kSaidPan = 5;
  constexpr int kSaidOrdered = 6;
  constexpr int kSaidUndone = 7;

  /// Game.GRIFF_VALUES: what Game.griff gives of the handle.
  constexpr int kHandleValues = 11;

  // Game's TOAST_: what the draft says, the activity's words.
  constexpr int kToastNoFreeResource = 1;
  constexpr int kToastTaken = 2;
  constexpr int kToastFull = 3;
  constexpr int kToastNoPlan = 4;
  constexpr int kToastNoEngineer = 5;
  constexpr int kToastAllOnBuildings = 6;
  constexpr int kToastSomeOnBuildings = 7;
  constexpr int kToastFirstTickOrCross = 8;
  constexpr int kToastNotOrdered = 9;

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

  // griff.lua's yes and no for Ask, whose 0 is no answer.
  constexpr int kYes = 2;

  constexpr char kGriffLua[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').";
  constexpr char kTapKind[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').TapKind()";
  constexpr char kTroops[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').Troops()";
  constexpr char kStrokeKind[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').StrokeKind()";
  constexpr char kStartAttackMove[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').StartAttackMove()";
  constexpr char kOffersPatrol[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').OffersPatrol()";
  constexpr char kStartPatrol[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').StartPatrol()";
  constexpr char kEndCommandMode[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').EndCommandMode()";
  constexpr char kShiftLetGo[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').ShiftLetGo()";
  constexpr char kDeselect[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').Deselect()";
  constexpr char kStopCamera[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').StopCamera()";
  constexpr char kPinchEnd[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').PinchEnd()";
  constexpr char kTypeInView[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').SelectTypeInView()";
  constexpr char kDraftEnded[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').DraftEnded()";
  constexpr char kCommitBegin[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').CommitBegin()";
  constexpr char kCommitEnd[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').CommitEnd()";
  constexpr char kLookBack[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').LookBack()";
  constexpr char kUndoWatchEnd[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').UndoWatchEnd()";
  constexpr char kUndoEnd[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').UndoEnd()";
  /// FAF's key actions select_all_units_of_same_type and, with MOBILE for its ALLUNITS (the mockup's rough grab of the army,
  /// not the base), select_all_onscreen.
  constexpr char kAllOfType[] = "UI_ExpandCurrentSelection";
  constexpr char kAllMobile[] = "UI_SelectByCategory +inview MOBILE";

  /// Shift's key, as the keyboard's left one (KeysAndroid.cpp).
  constexpr UINT kShiftScanCode = 0x2A;

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

  /// The app's own pieces over the picture (Game.setGriffCovers): the rail's buttons, the draft's tick and cross, the undo's
  /// pill while it shows. A finger on one is the activity's: the handle keeps clear of them, and nothing of the game's lies
  /// under them for a gesture, as under FAF's panels.
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
  /// map or tints the whole screen at a depth below it, as the paused game's glow does. With it, the main map in `mainMap`.
  [[nodiscard]] griffplace::FreeMap FreeMapOver(const Box& view, moho::CUIWorldView** const mainMap = nullptr)
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
  /// Whether this game's free map has been worked out: without a selection it is worked out once, for a toast's place.
  bool gFreeKnown = false;

  // --- The draft: plans at world places --------------------------------------------------------

  /// The draft, on the program's thread: from a build icon of FAF's construction panel (griff.lua BuildIconClicked) to the
  /// tick, the cross or the two fingers' tap.
  griffdraft::Draft gDraft;
  /// The blueprints of the draft's buildings by their ids, for FAF's own check of a place.
  std::unordered_map<std::string, moho::RUnitBlueprint*> gDraftBlueprints;
  /// What a finger shows of what it would add: the line from the newest plan, each place with whether it is free; and the plan
  /// a finger that rests would place, of the picked building.
  std::vector<std::pair<griffdraft::Place, bool>> gDraftLine;
  bool gAimShown = false;
  griffdraft::Place gAim;
  /// The finger that rests is on the map, not on FAF's UI: the loupe is for the map.
  bool gAimOnMap = false;
  /// The world place under the finger where the draft's move began.
  griffdraft::Place gMoveFrom;
  /// Where FAF's construction panel's top and the left of the column at the right (its avatars) are, for the tick and the
  /// cross (griff.lua); below 0 where not known.
  float gConstructionTop = -1;
  float gColumnLeft = -1;

  // --- Clicks at world places: the tick's plans, the undo's orders given again ---------------------------------------

  /// A click at a world place: FAF's mode for it (the call of griff.lua's that arms it), the place, and its run. The clicks of
  /// a run go to the units selected for it (`select`, griff.lua's list of their ids, at the run's first click; empty: the
  /// selection stays), the first that FAF orders by replacing what the units do unless all are appended, the others with
  /// Shift held to the run's end. A building's click (FAF's build mode, `builds`) lands in the cell of `building` at the place.
  struct ClickStep
  {
    std::string arm;
    griffundo::Place place;
    int run = 0;
    std::string select;
    bool builds = false;
    griffdraft::Building building;
  };

  /// The clicks that run (ClickStepTask), one a task, so that each sees what the one before did, and what comes after the last
  /// of them (`finish`), also once the game is over. A place the free map does not show gets the camera first, as high and
  /// turned as it is, and its click after the next frame that drew it there; a building whose cell no pixel lands in gets it
  /// closer (`closer`, once a step). A place that no frame brought onto the free map, or that no pixel lands on then, is not
  /// clicked (`unreached`). The orders FAF's UI gave for a click are counted (FafreGriffOrdered), up to the next step, which
  /// comes after the click's input: a click that gave none was `refused`, as where the ground was taken meanwhile.
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
    bool counting = false;
    int ordered = 0;
    int unreached = 0;
    int refused = 0;
  };

  Clicks gClicks;
  /// The program's clicks run, the draft's tick's or the undo's: the fingers rest meanwhile (Game.griffClicking, ClicksRun),
  /// as a gesture's click between them would act with their selection, in their mode, or move the camera they aim by.
  std::atomic<bool> gClicking{false};
  /// A step may wait for the next frame, in which FAF lays its panels out for the units just selected and the camera moves:
  /// the frames' number (griff.lua counts each frame of the session's UI, FafreGriffFrame), the one the step waits after, and
  /// since when it waits (0: it does not), for the clicks' patience.
  bool gClicksWait = false;
  std::uint32_t gFrame = 0;
  std::uint32_t gClicksAfterFrame = 0;
  std::atomic<std::int64_t> gClicksWaitSince{0};

  // --- What a gesture ordered: its pins and its undo ----------------------------------------------------------------------

  /// The pins of the orders of Griff's gestures, at world places, and what the activity reads of them (Game.griffOrders), as
  /// the last frame drew them: the head (the undo pill's anchors, the pins' count), then each pin.
  griffpins::Pins gPins;
  std::mutex gOrdersShownMutex;
  constexpr std::size_t kOrdersHead = 4;
  constexpr std::size_t kOrdersItem = 4;
  std::vector<float> gOrdersShown{-1, -1, -1, 0};

  /// Where the undo's pill stands (griff.lua PillAnchors): FAF's unit view's top and its orders panel's left and right, in the
  /// picture's pixels; below 0 where not known.
  float gUnitViewTop = -1;
  float gOrdersLeft = -1;
  float gOrdersRight = -1;

  /// The last gesture that ordered, which the undo's pill takes back (griff.lua's record): its number (0: none), whether the
  /// pill asked for the undo, which comes when no clicks run, and whether the gesture ordered units it knew no queue of before
  /// it.
  struct Undoable
  {
    int number = 0;
    bool asked = false;
    bool unknown = false;
  };

  Undoable gUndoable;
  /// The undo whose clicks run: its record's number, the orders it could not give again, and whether some units were unknown.
  struct Undoing
  {
    int number = 0;
    int dropped = 0;
    bool unknown = false;
  };

  Undoing gUndoing;

  /// What griff.lua tells of the record the pill takes back (UndoPlan): its units with their queues before and now, and where
  /// the units their orders before targeted are now.
  std::vector<griffundo::Unit> gUndoUnits;
  griffundo::Targets gUndoTargets;

  /// The gesture whose orders griff.lua watches now (UndoWatch), by its number; 0: none.
  int gWatch = 0;
  int gWatches = 0;

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
  constexpr std::size_t kDraftHead = 8;
  constexpr std::size_t kDraftItem = 8;
  /// More of a line than the draft could ever hold is not shown.
  constexpr std::size_t kLineShown = griffdraft::Draft::kMaxPlans;

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

  /// The draft as this frame drew it, for the activity (Game.griffDraft): whether it is open, whether its building stands on
  /// a deposit, the tick's and the cross's anchors, how many plans, line places and aims follow, whether a resting finger is
  /// on the map, then each, on the picture.
  void PublishDraft(const griffplace::FreeMap& free, moho::CUIWorldView* const mainMap)
  {
    std::vector<float> shown{0};
    MapView map;
    if (gDraft.Open()) {
      shown = {1, gDraft.Picked().deposit != griffdraft::Deposit::kNone ? 1.0f : 0.0f, gConstructionTop, gColumnLeft, 0, 0, 0,
               gAimOnMap ? 1.0f : 0.0f};
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
      shown[4] = static_cast<float>((shown.size() - kDraftHead) / kDraftItem);
      const std::size_t lineFrom = shown.size();
      if (const griffdraft::Plan* const start = gDraft.LineStart(); start != nullptr) {
        const std::size_t count = (std::min)(gDraftLine.size(), kLineShown);
        for (std::size_t at = 0; at < count; ++at) {
          Show(shown, map, gDraft.BuildingOf(*start), gDraftLine[at].first, 0, gDraftLine[at].second ? 0 : kPlanTaken);
        }
      }
      shown[5] = static_cast<float>((shown.size() - lineFrom) / kDraftItem);
      if (gAimShown) {
        const std::size_t aimFrom = shown.size();
        Show(shown, map, gDraft.Picked(), gAim, 0, 0);
        shown[6] = shown.size() > aimFrom ? 1.0f : 0.0f;
      }
    }
    const std::lock_guard<std::mutex> lock(gDraftShownMutex);
    gDraftShown = std::move(shown);
  }

  void ClickStepTask(WPARAM);
  void TryUndo();
  [[nodiscard]] bool ClicksRun();

  /// What the activity reads of the orders (Game.griffOrders) as this frame drew them: the undo pill's anchors, then each pin
  /// whose tip is on the free map (`free`, where it was worked out: FAF's panels stand over the map's picture): its tip on the
  /// picture, its kind and how much of it shows.
  void PublishOrders(const griffplace::FreeMap* const free, moho::CUIWorldView* const mainMap)
  {
    std::vector<float> shown{gUnitViewTop, gOrdersLeft, gOrdersRight, 0};
    const std::int64_t now = Now();
    const std::vector<griffpins::Pin> live = gPins.Live(now);
    MapView map;
    if (!live.empty() && (MapOf(mainMap, map) || MainMap(map))) {
      for (const griffpins::Pin& pin : live) {
        float x = 0;
        float y = 0;
        if (Project(map, pin.x, pin.y, pin.z, x, y) && (free == nullptr || free->Free(x, y))) {
          shown.insert(shown.end(), {x, y, static_cast<float>(pin.kind), griffpins::Alpha(now - pin.born)});
        }
      }
      shown[kOrdersHead - 1] = static_cast<float>((shown.size() - kOrdersHead) / kOrdersItem);
    }
    const std::lock_guard<std::mutex> lock(gOrdersShownMutex);
    gOrdersShown = std::move(shown);
  }


  /// The handle for the reported selection, and the free map's cross, after the frame's drawing: FAF's Lua changes layouts
  /// in its frame, after which they are not known until the drawing works them out (unitview.lua places its panel anew each
  /// frame), and the free map reads only what is worked out (LaidOutBox). Without a selection the cross stays as it was: FAF's
  /// panels at the map's middle hardly change, and the walk costs a frame up to a ms; it runs while pins show, which keep to
  /// the free map. While a draft is open there is no handle (the mockup's: no stroke starts), and the draft is shown each
  /// frame, as the camera moves its plans, as are the orders' pins. An undo the pill asked for while clicks ran comes now.
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
      if (gUndoable.asked) {
        TryUndo();
      }
      const bool drafting = gDraft.Open();
      const bool pinned = !gPins.Live(Now()).empty();
      if (!selection.viewKnown || (!drafting && selection.count <= 0 && gFreeKnown && !pinned)) {
        if (selection.count <= 0 || drafting) {
          const std::lock_guard<std::mutex> lock(gHandleMutex);
          gHandle = Handle{};
        }
        PublishOrders(nullptr, nullptr);
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
      PublishOrders(&free, mainMap);
      gFreeKnown = true;
      const std::lock_guard<std::mutex> lock(gHandleMutex);
      gHandle = handle;
      gFree = cross;
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
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
  /// the clicks' steps wait for frames by their number, so they never wait on the report.
  int Frame(lua_State*)
  {
    ++gFrame;
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
    gFreeKnown = false;
    ForgetDraft();
    gConstructionTop = -1;
    gColumnLeft = -1;
    // The orders of this game: no watch, record, pin or pill any more (griff.lua's state goes with the game's UI).
    gWatch = 0;
    gUndoable = Undoable{};
    gPins.Clear();
    gUnitViewTop = -1;
    gOrdersLeft = -1;
    gOrdersRight = -1;
    {
      const std::lock_guard<std::mutex> lock(gOrdersShownMutex);
      gOrdersShown = {-1, -1, -1, 0};
    }
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
  constexpr char kDraftAnchorsHelp[] =
    "FafreGriffDraftAnchors(constructionTop, columnLeft) - where the draft's tick and cross stand, in the picture's pixels";

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

  /// What the draft and a building's click take of the blueprint `id`: its footprint, its skirt and the deposit it wants; the
  /// blueprint itself, or none where the session has no such blueprint.
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
    return blueprint;
  }

  /// A building of FAF's construction panel goes into the draft (griff.lua BuildIconClicked), not while the program's clicks
  /// run, whose draft it is.
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
    gDraftBlueprints[building.id] = blueprint;
    gDraft.Pick(building);
    // The next frame shows it; a finger that comes before needs to know at once whether the draft is open.
    const std::lock_guard<std::mutex> lock(gDraftShownMutex);
    if (gDraftShown.empty() || gDraftShown[0] == 0) {
      gDraftShown = {1, building.deposit != griffdraft::Deposit::kNone ? 1.0f : 0.0f, gConstructionTop, gColumnLeft, 0, 0, 0, 0};
    } else {
      gDraftShown[1] = building.deposit != griffdraft::Deposit::kNone ? 1.0f : 0.0f;
    }
    return 0;
  }

  int DraftPlans(lua_State* const lua)
  {
    lua_pushnumber(lua, gDraft.Open() ? static_cast<lua_Number>(gDraft.Plans().size()) : -1);
    return 1;
  }

  int DraftRefused(lua_State*)
  {
    Say(kSaidToast, kToastFirstTickOrCross, 0);
    return 0;
  }

  int DraftEnd(lua_State*)
  {
    if (!gClicking) {
      ForgetDraft();
    }
    return 0;
  }

  int DraftAnchors(lua_State* const lua)
  {
    gConstructionTop = static_cast<float>(lua_tonumber(lua, 1));
    gColumnLeft = static_cast<float>(lua_tonumber(lua, 2));
    return 0;
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
  moho::CScrLuaBinder
    gDraftAnchorsBinder(moho::SCR_UserLuaInitSet(), "FafreGriffDraftAnchors", &DraftAnchors, nullptr, "<global>", kDraftAnchorsHelp);

  // --- The orders' binders: what griff.lua tells of the orders the gestures gave, and of the one the pill takes back --------

  constexpr char kOrderedHelp[] =
    "FafreGriffOrdered(commandType, attackMove, watched, placed, x, y, z) - FAF's UI gave an order; a watched gesture's is pinned";
  constexpr char kRecordedHelp[] = "FafreGriffRecorded(record, unknown) - the gesture's record is whole: the pill can take it back";
  constexpr char kUndoUnitHelp[] = "FafreGriffUndoUnit(unitId, factory, replaced) - a unit of the record the pill takes back";
  constexpr char kUndoEntryHelp[] =
    "FafreGriffUndoEntry(now, id, type, x, y, z, target, targetsPlace, blueprint, task) - an order of its queue, before or now";
  constexpr char kUndoTargetHelp[] = "FafreGriffUndoTarget(unitId, x, y, z) - a unit an order before targets lives here";
  constexpr char kPillAnchorsHelp[] =
    "FafreGriffPillAnchors(unitViewTop, ordersLeft, ordersRight) - where the undo's pill stands, in the picture's pixels";

  [[nodiscard]] float Number(lua_State* const lua, const int index)
  {
    return static_cast<float>(lua_tonumber(lua, index));
  }

  [[nodiscard]] int Whole(lua_State* const lua, const int index)
  {
    return static_cast<int>(lua_tonumber(lua, index));
  }

  [[nodiscard]] std::string Text(lua_State* const lua, const int index)
  {
    const char* const text = lua_tostring(lua, index);
    return text != nullptr ? text : "";
  }

  /// Of every order FAF's UI gives: a pin at its place if a gesture watched gave it, and the count of the clicks that run.
  int Ordered(lua_State* const lua)
  {
    if (Whole(lua, 3) != 0 && Whole(lua, 4) != 0) {
      gPins.Add(Number(lua, 5), Number(lua, 6), Number(lua, 7), griffpins::KindOf(Text(lua, 1), Whole(lua, 2) != 0), Now());
    }
    if (gClicking) {
      ++gClicks.ordered;
    }
    return 0;
  }

  /// The record is whole, and the pill's from now on: the one before is gone (one level). The tap ladder's attack-move tells
  /// the move's record again, with what it added: the pill's tap stays asked for.
  int Recorded(lua_State* const lua)
  {
    const int number = Whole(lua, 1);
    Undoable record;
    record.number = number;
    record.unknown = Whole(lua, 2) != 0;
    record.asked = gUndoable.number == number && gUndoable.asked;
    gUndoable = record;
    Say(kSaidOrdered, number, 0);
    return 0;
  }

  int UndoUnit(lua_State* const lua)
  {
    griffundo::Unit unit;
    unit.id = Whole(lua, 1);
    unit.factory = Whole(lua, 2) != 0;
    unit.replaced = Whole(lua, 3) != 0;
    gUndoUnits.push_back(std::move(unit));
    return 0;
  }

  int UndoEntry(lua_State* const lua)
  {
    if (gUndoUnits.empty()) {
      return 0;
    }
    griffundo::Entry entry;
    entry.id = Whole(lua, 2);
    entry.type = Text(lua, 3);
    entry.place = griffundo::Place{Number(lua, 4), Number(lua, 5), Number(lua, 6)};
    entry.target = Whole(lua, 7);
    entry.targetsPlace = Whole(lua, 8) != 0;
    // It goes into a console command when it is built again (StartBuild).
    const std::string blueprint = Text(lua, 9);
    entry.blueprint = PlainId(blueprint.c_str()) ? blueprint : "";
    entry.task = Text(lua, 10);
    griffundo::Unit& unit = gUndoUnits.back();
    (Whole(lua, 1) != 0 ? unit.now : unit.before).push_back(std::move(entry));
    return 0;
  }

  int UndoTarget(lua_State* const lua)
  {
    gUndoTargets[Whole(lua, 1)] = griffundo::Place{Number(lua, 2), Number(lua, 3), Number(lua, 4)};
    return 0;
  }

  int PillAnchors(lua_State* const lua)
  {
    gUnitViewTop = Number(lua, 1);
    gOrdersLeft = Number(lua, 2);
    gOrdersRight = Number(lua, 3);
    return 0;
  }

  moho::CScrLuaBinder gOrderedBinder(moho::SCR_UserLuaInitSet(), "FafreGriffOrdered", &Ordered, nullptr, "<global>", kOrderedHelp);
  moho::CScrLuaBinder gRecordedBinder(moho::SCR_UserLuaInitSet(), "FafreGriffRecorded", &Recorded, nullptr, "<global>", kRecordedHelp);
  moho::CScrLuaBinder gUndoUnitBinder(moho::SCR_UserLuaInitSet(), "FafreGriffUndoUnit", &UndoUnit, nullptr, "<global>", kUndoUnitHelp);
  moho::CScrLuaBinder
    gUndoEntryBinder(moho::SCR_UserLuaInitSet(), "FafreGriffUndoEntry", &UndoEntry, nullptr, "<global>", kUndoEntryHelp);
  moho::CScrLuaBinder
    gUndoTargetBinder(moho::SCR_UserLuaInitSet(), "FafreGriffUndoTarget", &UndoTarget, nullptr, "<global>", kUndoTargetHelp);
  moho::CScrLuaBinder
    gPillAnchorsBinder(moho::SCR_UserLuaInitSet(), "FafreGriffPillAnchors", &PillAnchors, nullptr, "<global>", kPillAnchorsHelp);

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

  // --- The watch over a gesture's orders ------------------------------------------------------------------------------------

  /// The watched gesture is over: griff.lua makes its record of what it ordered, if it ordered anything.
  void EndWatch()
  {
    if (gWatch != 0) {
      gWatch = 0;
      moho::CON_Execute(kUndoWatchEnd);
    }
  }

  /// A gesture that may order begins: what FAF's UI orders from now on is its own, until the watch with its number ends
  /// (EndWatchTask), or the next gesture's begins, which comes after this one's input (a task's input comes before the next
  /// task). griff.lua keeps the queues of the selection's units as they are now; the tap ladder's attack-move in place of a
  /// move goes on with the move's (`join`). None while the tick's or the undo's clicks run: what they order is their own.
  [[nodiscard]] int WatchOrders(const bool join)
  {
    if (gClicking) {
      return 0;
    }
    EndWatch();
    CallGriff("UndoWatch(%d)", join ? 1 : 0, 0, 0);
    gWatch = ++gWatches;
    return gWatch;
  }

  void EndWatchTask(const WPARAM watch)
  {
    try {
      if (gWatch != 0 && static_cast<WPARAM>(gWatch) == watch && moho::UI_GamePlaying()) {
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

  /// A first finger came down here: on the map or the minimap the camera's glide or flight stops, and FAF's reach for a unit
  /// beside a tap is the mockup's for what is selected (with Box, which picks units, as without troops). Not while a command
  /// mode is armed, which picks as FAF sets it: its reclaim mode reaches further for wrecks. Nothing while the program's clicks
  /// run, which pick as they began; the down still comes, as the task that ends clicks that wait past their patience.
  void DownTask(const WPARAM places)
  {
    const int x = Coordinate(places, 0);
    const int y = Coordinate(places, 1);
    const bool box = Coordinate(places, 2) != 0;
    try {
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

  /// A tap where the finger was and the cursor has rested (TouchInput): FAF's click by what it lands on. The UI's controls take
  /// it, with the tap help; a command mode that FAF's orders panel armed takes it; Box adds one's own unit to the selection or
  /// takes it out, and on anything else does nothing; with Attack it is an attack-move; one's own unit is selected, Plus adds it
  /// (Shift's click); else the selection gets FAF's right click there, also on one's own unit with Plus or Attack and troops
  /// selected: the order there, as the mockup has it. Near misses are FAF's own pick, within the reach DownTask set.
  void TapByPlace(const Tap& tap)
  {
    const int x = tap.x;
    const int y = tap.y;
    const bool queue = (tap.rail & kRailQueue) != 0;
    const bool attack = (tap.rail & kRailAttack) != 0;
    // With Box counting, the UI's tap and an armed mode's are none of Box's: it stays lit, Plus and Attack go out.
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
    const bool selection = (kind & kSelection) != 0;
    if ((tap.rail & kRailBox) != 0) {
      if (own) {
        ClickWith(true, WIN32ANDROID_BUTTON_LEFT, x, y);
      }
      AnswerTap(tap, kTapOther, own ? grifftap::Outcome::kBoxToggled : grifftap::Outcome::kBoxFoundNothing);
      return;
    }
    if (attack && selection && ArmAttackMove()) {
      ClickWith(queue, WIN32ANDROID_BUTTON_LEFT, x, y);
      AnswerTap(tap, kTapOther);
      return;
    }
    if (own && !((queue || attack) && (kind & kTroopsSelected) != 0)) {
      ClickWith(queue, WIN32ANDROID_BUTTON_LEFT, x, y);
      AnswerTap(tap, queue ? kTapOther : kTapOwn);
      return;
    }
    ClickWith(queue, selection ? WIN32ANDROID_BUTTON_RIGHT : WIN32ANDROID_BUTTON_LEFT, x, y);
    int answer = kTapOther;
    if (!selection) {
      answer = (kind & kUnitUnder) != 0 ? kTapOther : kTapEmpty;
    } else if ((kind & kMoveOrder) != 0 && !queue && !attack) {
      answer = kTapGround;
    }
    AnswerTap(tap, answer);
  }

  /// A tap of the ladder above its first rung, by what its first tap was, whatever lies under this one: the UI's click again
  /// where the first went, which the message layer makes the game's double click; all of the selected unit's type in view (as
  /// the game's double click on the unit, which could not reach it where the panels its selection brought up cover it), then on
  /// the map; an attack-move at the first tap's place in place of its move (where FAF offers none, as for factories alone, a
  /// right click again: the rally point); all one's own mobile units in view.
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

  // --- Drags, the pan's glide and the pinch ---------------------------------------------------------

  /// What a finger's drag pressed, for its end.
  enum class Drag
  {
    kNone,
    kPan,
    kBox,
    kAddingBox,
    kPanel,
  };

  Drag gDrag = Drag::kNone;

  /// The camera glides on at this velocity, in the picture's pixels a second (griff.lua Glide), or stays where it is.
  void Glide(const int velocityX, const int velocityY)
  {
    if (velocityX != 0 || velocityY != 0) {
      CallGriff("Glide(%d, %d, %d)", velocityX, velocityY, static_cast<int>(gGlideStop.load() + 0.5f));
    }
  }

  void EndDrag(const int x, const int y, const int velocityX, const int velocityY)
  {
    switch (gDrag) {
      case Drag::kPan:
        Win32Android_TouchButton(WIN32ANDROID_BUTTON_MIDDLE, FALSE, x, y);
        Glide(velocityX, velocityY);
        break;
      case Drag::kBox:
      case Drag::kPanel:
        Win32Android_TouchButton(WIN32ANDROID_BUTTON_LEFT, FALSE, x, y);
        break;
      case Drag::kAddingBox:
        Win32Android_TouchButton(WIN32ANDROID_BUTTON_LEFT, FALSE, x, y);
        ReleaseShift();
        break;
      case Drag::kNone:
        break;
    }
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

  /// A finger's drag from where it came down, as the place wants it: on the map the middle button's, which pans the camera,
  /// pressed where the finger is now so that the map does not jump by the slop it went; after a rest or with Box a selection
  /// box from where it came down, which Plus makes add to the selection (Shift's); on a panel or the minimap the left button's
  /// from there, as a mouse drags there. The activity hears which drag is a pan, which a second finger makes two of.
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
    bool map = false;
    try {
      const bool playing = moho::UI_GamePlaying();
      // A box, a panel's drag or a pan would come between the undo's or the tick's clicks; and one of the app's pieces that
      // came over where the finger went down takes it.
      if (playing && (ClicksRun() || Covered(drag.x, drag.y))) {
        return;
      }
      map = playing && OnMap(static_cast<float>(drag.x), static_cast<float>(drag.y));
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
    if (!map) {
      gDrag = Drag::kPanel;
      Win32Android_TouchButton(WIN32ANDROID_BUTTON_LEFT, TRUE, drag.x, drag.y);
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

  /// A finger rested and lifted: on a panel FAF's right click, with the tap help, as the mouse scheme's hold gives it; on the map
  /// the rest was for a box that did not come, but with Box it was none (TouchInput) and is Box's tap. With no game behind the
  /// UI any more, the UI's right click wherever it is.
  void RestTapTask(const WPARAM places)
  {
    Tap tap;
    tap.x = Coordinate(places, 0);
    tap.y = Coordinate(places, 1);
    tap.rail = Coordinate(places, 2);
    try {
      const bool playing = moho::UI_GamePlaying();
      if (playing && (ClicksRun() || Covered(tap.x, tap.y))) {
        if ((tap.rail & kRailBox) != 0) {
          AnswerTap(tap, kTapOther, grifftap::Outcome::kBoxFoundNothing);
        }
        return;
      }
      if (playing && OnMap(static_cast<float>(tap.x), static_cast<float>(tap.y))) {
        if ((tap.rail & kRailBox) != 0) {
          TapByPlace(tap);
        }
        return;
      }
      // A right click on a panel may order, as on the minimap: watched.
      const int watch = playing ? WatchOrders(false) : 0;
      ClickUi((tap.rail & kRailQueue) != 0, static_cast<float>(tap.x), static_cast<float>(tap.y), WIN32ANDROID_BUTTON_RIGHT);
      EndWatchAfterInput(watch);
      if ((tap.rail & kRailBox) != 0) {
        // Box, which the activity left to the answer, was none of this: it stays lit, and Plus and Attack go out.
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
      const int end = view == nullptr ? kEndPanel : view->IsMiniMap() ? kEndMinimap : Ask(kStrokeKind);
      gStrokeAnswer.store(static_cast<std::int64_t>(stroke) << 32 | static_cast<std::uint32_t>(end));
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  /// The cursor hovers at the stroke's end, without a button: FAF shows its order cursor there and what is under it. The
  /// answer comes in the task after the move's events.
  void StrokeHoverTask(const WPARAM places)
  {
    Win32Android_MouseMove(Coordinate(places, 0), Coordinate(places, 1));
    Win32Android_RunOnWindowThread(&StrokeAnswerTask, places);
  }

  // --- A route ----------------------------------------------------------------------------------

  struct Route
  {
    std::vector<POINT> points;
    int rail = 0;
    /// A loop: FAF's patrol, where it offers one.
    bool patrol = false;
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

  /// What a route gives: a stroke that ends on a panel other than the minimap gives nothing, which the activity says; on its
  /// way, only what is on the map itself is a waypoint. A loop is a patrol where FAF's orders panel offers one, not for
  /// factories alone: their loop is their rally route, as any stroke. None while the undo's or the tick's clicks run, which
  /// its clicks would come between.
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
        Say(kSaidRouteGivenUp, 0, 0);
        return;
      }
      gRoute.rail = given.rail;
      gRoute.patrol = given.patrol && Ask(kOffersPatrol) == kYes;
      for (std::size_t at = 0; at + 1 < given.points.size(); ++at) {
        const POINT point = given.points[at];
        if (OnMap(static_cast<float>(point.x), static_cast<float>(point.y))) {
          gRoute.points.push_back(point);
        }
      }
      gRoute.points.push_back(end);
      // Its clicks are right clicks, or an attack-move's or a patrol's left clicks in the mode each arms, which a command mode
      // armed before would take for its own.
      if (CommandModeArmed()) {
        moho::CON_Execute(kEndCommandMode);
      }
      gRouteWatch = WatchOrders(false);
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
      gRoute = Route{};
    }
  }

  /// The next waypoint: FAF's right click there, the first one replacing what the units were doing unless Plus counts, the
  /// others appended (Shift, held to the route's end). With Attack, an attack-move's left click; a patrol's, a patrol's.
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
      } else if ((gRoute.rail & kRailAttack) != 0 && (CommandModeArmed() || ArmAttackMove())) {
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
        Say(kSaidToast, kToastNoFreeResource, 0);
        break;
      case griffdraft::Placed::kTaken:
        Say(kSaidToast, kToastTaken, 0);
        break;
      case griffdraft::Placed::kFull:
        Say(kSaidToast, kToastFull, 0);
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

  /// Where PlaceAt would put a plan for a finger there, without putting it: false where it would not.
  [[nodiscard]] bool WouldPlace(const MapView& map, const int x, const int y, griffdraft::Place& place)
  {
    const griffdraft::Building& building = gDraft.Picked();
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
    return !gDraft.OnPlan(building, place.x, place.z) && GroundFree(building, place.x, place.z);
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
  /// while the program's clicks run.
  [[nodiscard]] bool DraftPlaceUnder(const int x, const int y, griffdraft::Place& place)
  {
    MapView map;
    return gDraft.Open() && !ClicksRun() && MainMap(map) && Unproject(map, x, y, place);
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
        Say(kSaidToast, kToastFull, 0);
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
  /// coalesce); at the lift that plan, or on the UI the rest's right click there, as Griff's rest on a panel gives.
  void DraftAimTask(const WPARAM places)
  {
    try {
      MapView map;
      const int x = Coordinate(places, 0);
      const int y = Coordinate(places, 1);
      gAimOnMap = gDraft.Open() && !ClicksRun() && MapAt(x, y, map);
      gAimShown = gAimOnMap && WouldPlace(map, x, y, gAim);
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

  /// Box over deposits with an extractor picked: every free deposit of its kind whose middle is in the box on the picture gets
  /// a plan (FAF's own box is not pressed).
  void DraftBoxTask(const WPARAM places)
  {
    try {
      MapView map;
      if (!gDraft.Open() || ClicksRun() || gDraft.Picked().deposit == griffdraft::Deposit::kNone || !MainMap(map)) {
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
        Say(kSaidToast, kToastFull, 0);
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

  /// The clicks begin after the next frame, in which FAF lays its panels out for the units just selected for them.
  void StartClicks(Clicks clicks)
  {
    gClicks = std::move(clicks);
    gClicking = true;
    WaitForFrame();
  }

  /// The clicks are over, or the game is: Shift as it was, the camera where it was, then the tick's or the undo's own end.
  void FinishClicks()
  {
    gClicking = false;
    gClicksWait = false;
    gClicksWaitSince = 0;
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

  /// The step is not clicked: no frame brought its place onto the free map, or no pixel lands on it.
  void SkipStep()
  {
    ++gClicks.unreached;
    ++gClicks.next;
    gClicks.waited = 0;
    gClicks.closer = false;
    Win32Android_RunOnWindowThread(&ClickStepTask, 0);
  }

  /// The next click: at a run's first, its units selected and a frame drawn for them; then FAF's mode for the click, as its
  /// panels arm it, and a mouse's left click at its place on the picture, the run's first that FAF orders by replacing what
  /// the units do unless all are appended, the others with Shift. A place the free map does not show, under a panel or out of
  /// view, gets the camera first, as high and turned as it is, and its click after the frame that drew it there. A building's
  /// click goes to a pixel whose world point FAF builds it at, in its cell: where none is, the camera comes closer first.
  void ClickStepTask(const WPARAM)
  {
    if (!gClicking) {
      return;
    }
    try {
      // The last click's input came before this task: what FAF ordered for it is known. Until FAF orders by one, the next
      // click is the run's first, which replaces.
      if (gClicks.counting) {
        gClicks.counting = false;
        if (gClicks.ordered == 0) {
          ++gClicks.refused;
        } else {
          ++gClicks.orderedInRun;
        }
      }
      if (!moho::UI_GamePlaying() || gClicks.next >= gClicks.steps.size()) {
        FinishClicks();
        return;
      }
      const ClickStep& step = gClicks.steps[gClicks.next];
      if (step.run != gClicks.run) {
        if (gClicks.holdsShift) {
          ReleaseShift();
          gClicks.holdsShift = false;
        }
        gClicks.run = step.run;
        gClicks.orderedInRun = 0;
        if (!step.select.empty()) {
          CallGriffLine("UndoSelect(" + step.select + ")");
          WaitForFrame();
          return;
        }
      }
      const Selection selection = gSelection;
      moho::CUIWorldView* mainMap = nullptr;
      const griffplace::FreeMap free = selection.viewKnown ? FreeMapOver(selection.view, &mainMap) : griffplace::FreeMap{};
      MapView map;
      float x = 0;
      float y = 0;
      const griffundo::Place& place = step.place;
      if (MapOf(mainMap, map) && Project(map, place.x, place.y, place.z, x, y) && free.DiscFree(x, y, kClickRoom)) {
        int clickX = static_cast<int>(std::lround(x));
        int clickY = static_cast<int>(std::lround(y));
        if (step.builds && !BuildPixel(map, step, x, y, clickX, clickY)) {
          // FAF would build it in a cell beside its place: no pixel's world point is in that cell here, as where a pixel spans
          // more than one. Once the camera has come closer and still none is, it is not ordered.
          if (gClicks.closer) {
            SkipStep();
            return;
          }
          gClicks.closer = true;
          gClicks.looked = true;
          LookAt(place, Closer(map, place));
          WaitForFrame();
          return;
        }
        if ((gClicks.orderedInRun > 0 || gClicks.appendAll) && !gClicks.holdsShift) {
          HoldShift();
          gClicks.holdsShift = true;
        }
        CallGriffLine(step.arm);
        // A mouse's click: a finger's press would only take FAF's ghost along (Win32Android_TouchButton).
        Win32Android_MouseButton(WIN32ANDROID_BUTTON_LEFT, TRUE, clickX, clickY);
        Win32Android_MouseButton(WIN32ANDROID_BUTTON_LEFT, FALSE, clickX, clickY);
        ++gClicks.next;
        gClicks.waited = 0;
        gClicks.closer = false;
        gClicks.counting = true;
        gClicks.ordered = 0;
        Win32Android_RunOnWindowThread(&ClickStepTask, 0);
        return;
      }
      if (gClicks.waited == 0) {
        LookAt(place, 1);
        gClicks.looked = true;
      }
      if (++gClicks.waited > kClickFrames) {
        SkipStep();
        return;
      }
      WaitForFrame();
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
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
      Say(kSaidToast, kToastNotOrdered, notOrdered);
    }
    Say(kSaidCommitted, gClicks.appendAll ? kRailQueue : 0, 0);
    ForgetDraft();
  }

  /// The tick (argument 1: Plus counts): no plans, no engineers, or nothing that can be built says so; else the builders are
  /// selected (griff.lua CommitBegin: the selected engineers, or the draft's that live) and each plan FAF would build is a
  /// click: FAF's build mode for its building, as the construction panel arms it for a build icon (FAF's build templates
  /// cleared first), in its cell, the first replacing what the builders do unless Plus counted. None while the undo's clicks
  /// run.
  void DraftCommitTask(const WPARAM plus)
  {
    try {
      if (!moho::UI_GamePlaying() || !gDraft.Open() || ClicksRun()) {
        return;
      }
      if (gDraft.Plans().empty()) {
        Say(kSaidToast, kToastNoPlan, 0);
        return;
      }
      if (Ask(kCommitBegin) != kYes) {
        Say(kSaidToast, kToastNoEngineer, 0);
        return;
      }
      const std::vector<griffdraft::Plan> buildable = gDraft.Buildable(GroundFree);
      if (buildable.empty()) {
        Say(kSaidToast, kToastAllOnBuildings, 0);
        moho::CON_Execute(kCommitEnd);
        return;
      }
      if (buildable.size() < gDraft.Plans().size()) {
        Say(kSaidToast, kToastSomeOnBuildings, static_cast<int>(gDraft.Plans().size() - buildable.size()));
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
      // The builders CommitBegin selected are the tick's units: their queues as they are now, for its undo.
      gCommitWatch = WatchOrders(false);
      StartClicks(std::move(clicks));
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  // --- The undo: the pill takes the last gesture's orders back --------------------------------------------------------------

  /// Units for griff.lua, a Lua list of their ids: {12,34}.
  [[nodiscard]] std::string UnitList(const std::vector<int>& units)
  {
    std::string list = "{";
    for (std::size_t at = 0; at < units.size(); ++at) {
      list += (at == 0 ? "" : ",") + std::to_string(units[at]);
    }
    return list + "}";
  }

  /// The call of griff.lua's that arms FAF's mode for an order given again: the construction panel's build mode for its
  /// building, else the mode the orders panel arms for its way (griff.lua Arm, by the number of griffundo::Way).
  [[nodiscard]] std::string ArmCall(const griffundo::Click& click)
  {
    if (click.way == griffundo::Way::kBuild) {
      return "StartBuild('" + click.blueprint + "')";
    }
    return "Arm(" + std::to_string(static_cast<int>(click.way)) + ")";
  }

  /// The undo's clicks are over: FAF's mode they left ended, the selection the player had; the activity hears whether all of
  /// the gesture came back.
  void FinishUndo()
  {
    if (moho::UI_GamePlaying()) {
      moho::CON_Execute(kUndoEnd);
    }
    const bool inPart = gUndoing.unknown || gUndoing.dropped + gClicks.unreached + gClicks.refused > 0;
    Say(kSaidUndone, gUndoing.number, inPart ? 1 : 0);
    gUndoing = Undoing{};
  }

  /// The pill's undo, once no clicks run: griff.lua tells of the record (UndoPlan), the plan takes its orders back
  /// (include/GriffUndo.h), by FAF's own inputs: the orders panel's Stop, and the clicks that give queues again, a run for each
  /// group of units. All of it comes after the gesture's orders in what the sim is sent, so it needs no answer of the sim's.
  /// The record is used up.
  void TryUndo()
  {
    if (!gUndoable.asked || gUndoable.number == 0 || ClicksRun() || !moho::UI_GamePlaying()) {
      return;
    }
    const Undoable record = gUndoable;
    gUndoable = Undoable{};
    // A gesture's watch that is still open has had its input: its orders are its own, not the undo's.
    EndWatch();
    gUndoUnits.clear();
    gUndoTargets.clear();
    if (Ask((std::string(kGriffLua) + "UndoPlan(" + std::to_string(record.number) + ")").c_str()) != kYes) {
      Say(kSaidUndone, record.number, 1);
      return;
    }
    const griffundo::Plan plan = griffundo::PlanUndo(gUndoUnits, gUndoTargets);
    if (!plan.stops.empty()) {
      CallGriffLine("UndoStop(" + UnitList(plan.stops) + ")");
    }
    gUndoing = Undoing{record.number, plan.dropped, record.unknown};
    Clicks clicks;
    clicks.finish = &FinishUndo;
    for (std::size_t run = 0; run < plan.groups.size(); ++run) {
      const griffundo::Group& group = plan.groups[run];
      for (std::size_t at = 0; at < group.clicks.size(); ++at) {
        const griffundo::Click& click = group.clicks[at];
        ClickStep step;
        step.arm = ArmCall(click);
        step.place = click.place;
        step.run = static_cast<int>(run);
        step.select = at == 0 ? UnitList(group.units) : "";
        // A building given again at its place lands in its cell, as the tick's.
        step.builds = click.way == griffundo::Way::kBuild && BlueprintBuilding(click.blueprint.c_str(), step.building) != nullptr;
        clicks.steps.push_back(std::move(step));
      }
    }
    StartClicks(std::move(clicks));
  }

  /// The pill was tapped for record `number`: its undo, now or once the tick's or another undo's clicks are over. A record
  /// replaced meanwhile is not taken back (one level).
  void UndoTask(const WPARAM number)
  {
    try {
      if (gUndoable.number != 0 && static_cast<WPARAM>(gUndoable.number) == number) {
        gUndoable.asked = true;
        TryUndo();
      }
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  /// The cross, or two fingers' tap in the draft: it goes, with nothing ordered. Not while the program's clicks run, which end
  /// the tick's draft themselves.
  void DraftDropTask(const WPARAM)
  {
    try {
      if (ClicksRun() || !gDraft.Open()) {
        return;
      }
      ForgetDraft();
      if (moho::UI_GamePlaying()) {
        moho::CON_Execute(kDraftEnded);
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

/// A finger rested here and lifted (RestTapTask).
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_restTap(JNIEnv*, jclass, const jint x, const jint y, const jint rail)
{
  Win32Android_RunOnWindowThread(&RestTapTask, Places(x, y, rail));
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

/// The cross, or two fingers' tap in the draft (DraftDropTask).
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_draftDrop(JNIEnv*, jclass)
{
  Win32Android_RunOnWindowThread(&DraftDropTask, 0);
}

/// The orders as the last frame drew them (PublishOrders), as many of its numbers as fit; how many.
extern "C" JNIEXPORT jint JNICALL Java_io_github_m3rt1n99_fafre_Game_griffOrders(JNIEnv* const env, jclass, const jfloatArray values)
{
  std::vector<float> shown;
  {
    const std::lock_guard<std::mutex> lock(gOrdersShownMutex);
    shown = gOrdersShown;
  }
  const jsize count = (std::min)(env->GetArrayLength(values), static_cast<jsize>(shown.size()));
  env->SetFloatArrayRegion(values, 0, count, shown.data());
  return count;
}

/// The undo's pill was tapped for record `number` (UndoTask).
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
