// Griff, the app's touch scheme (buildstage/touch-concept-v2.md, TouchInput): what its gestures do in the game, and where
// the handle over the selection goes. FAF's Lua reports each frame where the selection is on the screen
// (assets/fafre_ui/lua/fafre/griff.lua); the handle goes beside it where the map is free, and the activity reads it to draw it
// and to know a finger on it (Game.griff). A gesture's clicks are a mouse's, and Plus is the keyboard's Shift: FAF decides
// what they order, as for a mouse, and each click picks what is under it at its own place (CUIWorldView::HandleEvent). The
// camera moves as the UI moves it (griff.lua: its glide, its flight to the group, the pinch), not the simulation. They come
// to the program's thread as tasks, in the order of the rest of the input (GameHost.cpp).

#include <jni.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <exception>
#include <mutex>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Windows.h>
#include <Win32AndroidHost.h>

#include <GriffPlace.h>

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
  using hosttask::Coordinate;
  using hosttask::Places;
  using hosttask::SignedCoordinate;

  // TouchInput's RAIL_ bits.
  constexpr int kRailBox = 1;
  constexpr int kRailQueue = 2;
  constexpr int kRailAttack = 4;

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

  /// The app's bar at the screen's edge, whose buttons lie over the picture: none until the activity has laid it out.
  std::mutex gBarMutex;
  Box gBar;

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
  /// parents, past what does not take it, such as a marker on the map.
  [[nodiscard]] moho::CUIWorldView* WorldViewAt(const float x, const float y)
  {
    moho::CMauiControl* const root = InputRoot();
    if (root == nullptr) {
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
  /// view, the multifunction panel), the minimap, and the app's bar. One walk over the UI for all the places a frame tries.
  /// The HUD is drawn after the map's picture (WRenViewport::Render), so what covers the map is told apart by depth: the
  /// controls at the map's depth or above, such as the unit view, a sibling of the map at its depth, wherever the walk meets
  /// them (FAF makes the map anew when it splits and joins the view, which puts it after the unit view); not what holds the
  /// map or tints the whole screen at a depth below it, as the paused game's glow does.
  [[nodiscard]] griffplace::FreeMap FreeMapOver(const Box& view)
  {
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
      const std::lock_guard<std::mutex> lock(gBarMutex);
      covers.push_back(gBar);
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

  /// The handle for the reported selection, and the free map's cross, after the frame's drawing: FAF's Lua changes layouts
  /// in its frame, after which they are not known until the drawing works them out (unitview.lua places its panel anew each
  /// frame), and the free map reads only what is worked out (LaidOutBox). Without a selection the cross stays as it was: FAF's
  /// panels at the map's middle hardly change, and the walk costs a frame up to a ms.
  void PlaceTask(const WPARAM)
  {
    const Selection selection = gSelection;
    try {
      if (!selection.viewKnown || (selection.count <= 0 && gFreeKnown)) {
        if (selection.count <= 0) {
          const std::lock_guard<std::mutex> lock(gHandleMutex);
          gHandle = Handle{};
        }
        return;
      }
      const griffplace::FreeMap free = FreeMapOver(selection.view);
      const griffplace::Cross cross = griffplace::CrossAt(free, selection.view);
      const Handle handle = selection.count > 0
                            ? Place(free, selection.count, selection.middleX, selection.middleY, selection.units, selection.view)
                            : Handle{};
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
    char command[64];
    std::snprintf(command, sizeof command, "ui_SelectTolerance %g", static_cast<double>(wanted));
    moho::CON_Execute(command);
  }

  int Ended(lua_State*)
  {
    if (gToleranceSet && moho::ui_SelectTolerance == gTolerance) {
      char command[64];
      std::snprintf(command, sizeof command, "ui_SelectTolerance %g", static_cast<double>(gPlayersTolerance));
      moho::CON_Execute(command);
    }
    gToleranceSet = false;
    gFreeKnown = false;
    return 0;
  }

  moho::CScrLuaBinder gWantedBinder(moho::SCR_UserLuaInitSet(), "FafreGriffWanted", &Wanted, nullptr, "<global>", kWantedHelp);
  moho::CScrLuaBinder gBoxBinder(moho::SCR_UserLuaInitSet(), "FafreGriffBox", &ReportBox, nullptr, "<global>", kBoxHelp);
  moho::CScrLuaBinder gAnswerBinder(moho::SCR_UserLuaInitSet(), "FafreGriffAnswer", &Answer, nullptr, "<global>", kAnswerHelp);
  moho::CScrLuaBinder gEndedBinder(moho::SCR_UserLuaInitSet(), "FafreGriffEnded", &Ended, nullptr, "<global>", kEndedHelp);

  /// Runs griff.lua's function, which answers by FafreGriffAnswer; 0 when it did not.
  [[nodiscard]] int Ask(const char* const command)
  {
    gAnswer = 0;
    moho::CON_Execute(command);
    return gAnswer;
  }

  /// A first finger came down here: on the map or the minimap the camera's glide or flight stops, and FAF's reach for a unit
  /// beside a tap is the mockup's for what is selected (with Box, which picks units, as without troops). Not while a command
  /// mode is armed, which picks as FAF sets it: its reclaim mode reaches further for wrecks.
  void DownTask(const WPARAM places)
  {
    const int x = Coordinate(places, 0);
    const int y = Coordinate(places, 1);
    const bool box = Coordinate(places, 2) != 0;
    try {
      if (!moho::UI_GamePlaying()) {
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
  /// answer, the rail's bits it used.
  void AnswerTap(const Tap& tap, const int kind, const int used)
  {
    Say(kSaidTap, tap.number, kind | ((tap.rail & kRailBox) != 0 ? used : 0) << 8);
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
    if (UiTap(x, y)) {
      ClickUi(queue, static_cast<float>(x), static_cast<float>(y), WIN32ANDROID_BUTTON_LEFT);
      AnswerTap(tap, kTapUi, queue ? kRailQueue : 0);
      return;
    }
    if (CommandModeArmed()) {
      ClickWith(queue, WIN32ANDROID_BUTTON_LEFT, x, y);
      AnswerTap(tap, kTapOther, queue ? kRailQueue : 0);
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
      AnswerTap(tap, kTapOther, own ? kRailBox : 0);
      return;
    }
    if (attack && selection && ArmAttackMove()) {
      ClickWith(queue, WIN32ANDROID_BUTTON_LEFT, x, y);
      AnswerTap(tap, kTapOther, 0);
      return;
    }
    if (own && !((queue || attack) && (kind & kTroopsSelected) != 0)) {
      ClickWith(queue, WIN32ANDROID_BUTTON_LEFT, x, y);
      AnswerTap(tap, queue ? kTapOther : kTapOwn, 0);
      return;
    }
    ClickWith(queue, selection ? WIN32ANDROID_BUTTON_RIGHT : WIN32ANDROID_BUTTON_LEFT, x, y);
    int answer = kTapOther;
    if (!selection) {
      answer = (kind & kUnitUnder) != 0 ? kTapOther : kTapEmpty;
    } else if ((kind & kMoveOrder) != 0 && !queue && !attack) {
      answer = kTapGround;
    }
    AnswerTap(tap, answer, 0);
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
        if (tap.step == kStepTap) {
          TapByPlace(tap);
        } else {
          TapByStep(tap);
        }
      } else if (tap.step == kStepTap || tap.step == kStepAgain) {
        // No game behind the UI any more, such as at its end under FAF's last dialogs: a tap is the UI's, as the mouse's.
        const bool again = tap.step == kStepAgain;
        ClickUi(false, static_cast<float>(again ? tap.baseX : tap.x), static_cast<float>(again ? tap.baseY : tap.y),
                WIN32ANDROID_BUTTON_LEFT);
        if (!again) {
          AnswerTap(tap, kTapUi, 0);
        }
      }
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  /// Two fingers' tap: an armed command mode goes, as a right click puts it away; with none, the selection goes.
  void DeselectTask(const WPARAM)
  {
    try {
      if (moho::UI_GamePlaying()) {
        moho::CON_Execute(CommandModeArmed() ? kEndCommandMode : kDeselect);
      }
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

  /// The camera to the selection; argument 1: a flight, else at once.
  void ToGroupTask(const WPARAM fly)
  {
    try {
      if (moho::UI_GamePlaying()) {
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

  /// A finger's drag (Game.griffDrag): where it came down, where it is now, after a rest, the rail's bits.
  struct DragStart
  {
    int x = 0;
    int y = 0;
    int nowX = 0;
    int nowY = 0;
    bool rested = false;
    int rail = 0;
  };

  std::mutex gDragsMutex;
  std::deque<DragStart> gDrags;

  /// A finger's drag from where it came down, as the place wants it: on the map the middle button's, which pans the camera,
  /// pressed where the finger is now so that the map does not jump by the slop it went; after a rest or with Box a selection
  /// box from where it came down, which Plus makes add to the selection (Shift's); on a panel or the minimap the left button's
  /// from there, as a mouse drags there.
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
      map = moho::UI_GamePlaying() && OnMap(static_cast<float>(drag.x), static_cast<float>(drag.y));
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
  /// fingers lifted; and as they move. Two kinds of task: the moves coalesce, a start must not be one of them.
  void Pinch(const char* const format, const WPARAM places)
  {
    try {
      if (moho::UI_GamePlaying()) {
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
        Glide(SignedCoordinate(places, 0), SignedCoordinate(places, 1));
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
      if (moho::UI_GamePlaying() && OnMap(static_cast<float>(tap.x), static_cast<float>(tap.y))) {
        if ((tap.rail & kRailBox) != 0) {
          TapByPlace(tap);
        }
        return;
      }
      ClickUi((tap.rail & kRailQueue) != 0, static_cast<float>(tap.x), static_cast<float>(tap.y), WIN32ANDROID_BUTTON_RIGHT);
      if ((tap.rail & kRailBox) != 0) {
        // Box, which the activity left to the answer, was none of this; Plus counted for the right click.
        AnswerTap(tap, kTapUi, tap.rail & kRailQueue);
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

  /// What a route gives: a stroke that ends on a panel other than the minimap gives nothing, which the activity says; on its
  /// way, only what is on the map itself is a waypoint. A loop is a patrol where FAF's orders panel offers one, not for
  /// factories alone: their loop is their rally route, as any stroke.
  void BeginRouteTask(const WPARAM)
  {
    gRoute = Route{};
    gNextPoint = 0;
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
      if (!moho::UI_GamePlaying() || given.points.empty()) {
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

  void EndRouteTask(const WPARAM)
  {
    if (gRouteHoldsShift) {
      ReleaseShift();
      gRouteHoldsShift = false;
    }
    gRoute = Route{};
    gNextPoint = 0;
  }

  /// A velocity for a task's argument (SignedCoordinate).
  [[nodiscard]] int Velocity(const float pixelsASecond)
  {
    return static_cast<int>(std::clamp(pixelsASecond, -32767.0f, 32767.0f));
  }
} // namespace

/// The scheme is on or off; the handle's radius, how far from its middle a finger is on it, its room from the group's units and
/// how far from its middle its arrow reaches; the tap help's reach and what counts as a small control; how far beside a unit a
/// tap picks it, without troops and with them; the speed a second under which the camera's glide stops: in the picture's pixels.
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_setGriff(
  JNIEnv*, jclass, const jboolean on, const jfloat radius, const jfloat touchRadius, const jfloat gap, const jfloat arrowReach,
  const jfloat tapReach, const jfloat tapSmallSide, const jfloat nearReach, const jfloat nearReachWithTroops, const jfloat glideStop
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
  gOn.store(on != JNI_FALSE);
}

/// Where the app's bar lies over the picture, in its pixels, which the handle keeps clear of.
extern "C" JNIEXPORT void JNICALL
Java_io_github_m3rt1n99_fafre_Game_setGriffBar(JNIEnv*, jclass, const jint left, const jint top, const jint right, const jint bottom)
{
  const std::lock_guard<std::mutex> lock(gBarMutex);
  gBar = Box{static_cast<float>(left), static_cast<float>(top), static_cast<float>(right), static_cast<float>(bottom)};
}

/// The handle as the last frame placed it, into x, y, the group's middle x and y, the selection's count and 1 if it points at
/// the group from elsewhere, else 0; the free map's left, top and right at its middle; the stroke last answered for and what
/// its end is on. False when there is no handle.
extern "C" JNIEXPORT jboolean JNICALL Java_io_github_m3rt1n99_fafre_Game_griff(JNIEnv* const env, jclass, const jfloatArray handle)
{
  Handle now;
  griffplace::Cross free;
  {
    const std::lock_guard<std::mutex> lock(gHandleMutex);
    now = gHandle;
    free = gFree;
  }
  const std::int64_t answer = gStrokeAnswer.load();
  const jfloat values[11] = {
    now.x,     now.y,    now.groupX, now.groupY, static_cast<jfloat>(now.count), now.off ? 1.0f : 0.0f,
    free.left, free.top, free.right, static_cast<jfloat>(answer >> 32), static_cast<jfloat>(answer & 0xFFFFFFFF),
  };
  env->SetFloatArrayRegion(handle, 0, 11, values);
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

/// A finger's drag from here, the finger now at `nowX`, `nowY` (DragTask); `rail`: the RAIL_ bits that count for it.
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_griffDrag(
  JNIEnv*, jclass, const jint x, const jint y, const jint nowX, const jint nowY, const jboolean rested, const jint rail
)
{
  {
    const std::lock_guard<std::mutex> lock(gDragsMutex);
    gDrags.push_back(DragStart{x, y, nowX, nowY, rested != JNI_FALSE, rail});
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
