// Griff, the app's touch scheme (buildstage/touch-concept-v2.md, TouchInput): what its gestures do in the game, and where
// the handle over the selection goes. FAF's Lua reports each frame where the selection is on the screen
// (assets/fafre_ui/lua/fafre/griff.lua); the handle goes beside it where the map is free, and the activity reads it to draw it
// and to know a finger on it (Game.griff). A gesture's clicks are a mouse's, and Plus is the keyboard's Shift: FAF decides
// what they order, as for a mouse, and each click picks what is under it at its own place (CUIWorldView::HandleEvent). They
// come to the program's thread as tasks, in the order of the rest of the input (GameHost.cpp).

#include <jni.h>

#include <atomic>
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

  // TouchInput's RAIL_ bits.
  constexpr int kRailBox = 1;
  constexpr int kRailQueue = 2;
  constexpr int kRailAttack = 4;

  // The answer of TapKind in griff.lua.
  constexpr int kOwnUnit = 1;
  constexpr int kSelection = 2;

  constexpr char kTapKind[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').TapKind()";
  constexpr char kToGroup[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').ToGroup()";
  constexpr char kStartAttackMove[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').StartAttackMove()";
  constexpr char kEndCommandMode[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').EndCommandMode()";
  constexpr char kShiftLetGo[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').ShiftLetGo()";
  constexpr char kDeselect[] = "UI_Lua import('/fafre_ui/lua/fafre/griff.lua').Deselect()";

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

  /// The app's bar at the screen's edge, whose buttons lie over the picture: none until the activity has laid it out.
  std::mutex gBarMutex;
  Box gBar;

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

  void Publish(const Handle& handle)
  {
    const std::lock_guard<std::mutex> lock(gHandleMutex);
    gHandle = handle;
  }

  /// The handle for the group whose units take `units` on the screen, about their middle (griffplace::Place).
  [[nodiscard]] Handle Place(const int count, const float middleX, const float middleY, const Box& units, const Box& view)
  {
    const griffplace::Sizes sizes{gTouchRadius.load(), gRadius.load() + gGap.load(), gArrowReach.load()};
    const griffplace::Spot spot = griffplace::Place(FreeMapOver(view), sizes, middleX, middleY, units, view);
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
    "FafreGriffBox(count, [middleX, middleY, left, top, right, bottom, viewLeft, viewTop, viewRight, viewBottom]) - "
    "the selection's units on the screen, for Griff's handle; 0: nothing is selected";
  constexpr char kAnswerHelp[] = "FafreGriffAnswer(number) - griff.lua's answer to the app's question";

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
    Box view;
  };

  Selection gSelection;

  /// The handle for the reported selection, after the frame's drawing: FAF's Lua changes layouts in its frame, after which
  /// they are not known until the drawing works them out (unitview.lua places its panel anew each frame), and the free map
  /// reads only what is worked out (LaidOutBox).
  void PlaceTask(const WPARAM)
  {
    if (gSelection.count <= 0) {
      Publish(Handle{});
      return;
    }
    try {
      Publish(Place(gSelection.count, gSelection.middleX, gSelection.middleY, gSelection.units, gSelection.view));
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
      Publish(Handle{});
    }
  }

  int ReportBox(lua_State* const lua)
  {
    const int count = static_cast<int>(lua_tonumber(lua, 1));
    const auto number = [lua](const int index) { return static_cast<float>(lua_tonumber(lua, index)); };
    gSelection = count > 0 && lua_gettop(lua) >= 11
                 ? Selection{count, number(2), number(3), Box{number(4), number(5), number(6), number(7)},
                             Box{number(8), number(9), number(10), number(11)}}
                 : Selection{};
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

  moho::CScrLuaBinder gWantedBinder(moho::SCR_UserLuaInitSet(), "FafreGriffWanted", &Wanted, nullptr, "<global>", kWantedHelp);
  moho::CScrLuaBinder gBoxBinder(moho::SCR_UserLuaInitSet(), "FafreGriffBox", &ReportBox, nullptr, "<global>", kBoxHelp);
  moho::CScrLuaBinder gAnswerBinder(moho::SCR_UserLuaInitSet(), "FafreGriffAnswer", &Answer, nullptr, "<global>", kAnswerHelp);

  /// Runs griff.lua's function, which answers by FafreGriffAnswer; 0 when it did not.
  [[nodiscard]] int Ask(const char* const command)
  {
    gAnswer = 0;
    moho::CON_Execute(command);
    return gAnswer;
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

  /// What a tap clicked: the UI, one's own unit, or else.
  enum class Clicked
  {
    kElse,
    kUi,
    kOwnUnit,
  };

  /// The last tap's click, and where it went.
  Clicked gTapClicked = Clicked::kElse;
  POINT gTapPlace{};

  /// Where the click goes of a tap that what lies at the finger's place (x, y) makes click `clicked`: there, or where the last
  /// tap's click went if this one comes soon after it and near it (`again`, TouchInput's double tap) and clicks the same, the
  /// UI or one's own unit, which the message layer then makes the game's double click. Kept for the next tap.
  [[nodiscard]] POINT TapPlace(const Clicked clicked, const int x, const int y, const bool again)
  {
    const bool doubled = again && clicked != Clicked::kElse && clicked == gTapClicked;
    const POINT place = doubled ? gTapPlace : POINT{x, y};
    gTapClicked = clicked;
    gTapPlace = place;
    return place;
  }

  /// A tap where the finger was and the cursor has rested (TouchInput): FAF's click by what it lands on. The UI's controls take
  /// it, with the tap help; a command mode that FAF's orders panel armed takes it; with Attack it is an attack-move; one's own
  /// unit is selected, with Box or Plus added to the selection or taken out (Shift's click); else the selection gets FAF's
  /// right click there.
  void SmartTap(const int x, const int y, const int rail, const bool again)
  {
    const bool queue = (rail & kRailQueue) != 0;
    if (!OnMap(static_cast<float>(x), static_cast<float>(y)) || MeantSmallControl(static_cast<float>(x), static_cast<float>(y))) {
      const POINT at = TapPlace(Clicked::kUi, x, y, again);
      ClickUi(queue, static_cast<float>(at.x), static_cast<float>(at.y), WIN32ANDROID_BUTTON_LEFT);
      return;
    }
    if (CommandModeArmed()) {
      const POINT at = TapPlace(Clicked::kElse, x, y, again);
      ClickWith(queue, WIN32ANDROID_BUTTON_LEFT, at.x, at.y);
      return;
    }
    // What the cursor is over, where it rested: the finger's place.
    const int kind = Ask(kTapKind);
    const bool selection = (kind & kSelection) != 0;
    if ((rail & kRailAttack) != 0 && selection && ArmAttackMove()) {
      const POINT at = TapPlace(Clicked::kElse, x, y, again);
      ClickWith(queue, WIN32ANDROID_BUTTON_LEFT, at.x, at.y);
    } else if ((kind & kOwnUnit) != 0) {
      const POINT at = TapPlace(Clicked::kOwnUnit, x, y, again);
      ClickWith(queue || (rail & kRailBox) != 0, WIN32ANDROID_BUTTON_LEFT, at.x, at.y);
    } else {
      const POINT at = TapPlace(Clicked::kElse, x, y, again);
      ClickWith(queue, selection ? WIN32ANDROID_BUTTON_RIGHT : WIN32ANDROID_BUTTON_LEFT, at.x, at.y);
    }
  }

  void SmartTapTask(const WPARAM places)
  {
    const int x = Coordinate(places, 0);
    const int y = Coordinate(places, 1);
    const int rail = Coordinate(places, 2);
    const bool again = Coordinate(places, 3) != 0;
    try {
      if (moho::UI_GamePlaying()) {
        SmartTap(x, y, rail, again);
      } else {
        // No game behind the UI any more, such as at its end under FAF's last dialogs: a tap is the UI's, as the mouse's.
        const POINT at = TapPlace(Clicked::kUi, x, y, again);
        ClickUi(false, static_cast<float>(at.x), static_cast<float>(at.y), WIN32ANDROID_BUTTON_LEFT);
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

  void ToGroupTask(const WPARAM)
  {
    try {
      if (moho::UI_GamePlaying()) {
        moho::CON_Execute(kToGroup);
      }
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
  }

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

  void EndDrag(const int x, const int y)
  {
    switch (gDrag) {
      case Drag::kPan:
        Win32Android_TouchButton(WIN32ANDROID_BUTTON_MIDDLE, FALSE, x, y);
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

  /// A finger's drag from here, as the place wants it: on the map the middle button's, which pans the camera, or after a rest
  /// or with Box a selection box, which Plus makes add to the selection (Shift's); on a panel or the minimap the left button's,
  /// as a mouse drags there.
  void DragTask(const WPARAM places)
  {
    const int x = Coordinate(places, 0);
    const int y = Coordinate(places, 1);
    const bool rested = Coordinate(places, 2) != 0;
    const int rail = Coordinate(places, 3);
    EndDrag(x, y);
    bool map = false;
    try {
      map = moho::UI_GamePlaying() && OnMap(static_cast<float>(x), static_cast<float>(y));
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
    }
    if (!map) {
      gDrag = Drag::kPanel;
      Win32Android_TouchButton(WIN32ANDROID_BUTTON_LEFT, TRUE, x, y);
    } else if (rested || (rail & kRailBox) != 0) {
      const bool adding = (rail & kRailQueue) != 0;
      if (adding) {
        HoldShift();
      }
      gDrag = adding ? Drag::kAddingBox : Drag::kBox;
      Win32Android_TouchButton(WIN32ANDROID_BUTTON_LEFT, TRUE, x, y);
    } else {
      gDrag = Drag::kPan;
      Win32Android_TouchButton(WIN32ANDROID_BUTTON_MIDDLE, TRUE, x, y);
    }
  }

  void DragEndTask(const WPARAM place)
  {
    EndDrag(Coordinate(place, 0), Coordinate(place, 1));
  }

  /// A finger rested and lifted: on a panel FAF's right click, with the tap help, as the mouse scheme's hold gives it; on the map
  /// the rest was for a box that did not come. With no game behind the UI any more, the UI's right click wherever it is.
  void RestTapTask(const WPARAM places)
  {
    const int x = Coordinate(places, 0);
    const int y = Coordinate(places, 1);
    const bool queue = (Coordinate(places, 2) & kRailQueue) != 0;
    try {
      if (moho::UI_GamePlaying() && OnMap(static_cast<float>(x), static_cast<float>(y))) {
        return;
      }
      ClickUi(queue, static_cast<float>(x), static_cast<float>(y), WIN32ANDROID_BUTTON_RIGHT);
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

  // --- A route ----------------------------------------------------------------------------------

  struct Route
  {
    std::vector<POINT> points;
    int rail = 0;
  };

  /// What the activity has given and the program's thread has not begun.
  std::mutex gRoutesMutex;
  std::deque<Route> gRoutes;

  /// On the program's thread: the route whose steps run, a click each. Each step is a task of its own, so that it sees what the
  /// click before it has done: an attack-move's mode, which FAF ends after an order that replaces, comes back for the next.
  Route gRoute;
  std::size_t gNextPoint = 0;
  bool gRouteHoldsShift = false;

  /// What a route gives: a stroke that ends on a panel other than the minimap gives nothing; on its way, only what is on the
  /// map itself is a waypoint.
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
        return;
      }
      gRoute.rail = given.rail;
      for (std::size_t at = 0; at + 1 < given.points.size(); ++at) {
        const POINT point = given.points[at];
        if (OnMap(static_cast<float>(point.x), static_cast<float>(point.y))) {
          gRoute.points.push_back(point);
        }
      }
      gRoute.points.push_back(end);
      // Its clicks are right clicks, or an attack-move's left clicks in the mode each arms, which a command mode armed before
      // would take for its own.
      if (CommandModeArmed()) {
        moho::CON_Execute(kEndCommandMode);
      }
    } catch (const std::exception& exception) {
      gpg::Warnf("Griff: %s", exception.what());
      gRoute = Route{};
    }
  }

  /// The next waypoint: FAF's right click there, the first one replacing what the units were doing unless Plus counts, the
  /// others appended (Shift, held to the route's end). With Attack, an attack-move's left click.
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
      // An attack-move that FAF ended after a replacing order is armed again; where FAF offers none, a right click.
      if ((gRoute.rail & kRailAttack) != 0 && (CommandModeArmed() || ArmAttackMove())) {
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
} // namespace

/// The scheme is on or off; the handle's radius, how far from its middle a finger is on it, its room from the group's units and
/// how far from its middle its arrow reaches; the tap help's reach and what counts as a small control: in the picture's pixels.
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_setGriff(
  JNIEnv*, jclass, const jboolean on, const jfloat radius, const jfloat touchRadius, const jfloat gap, const jfloat arrowReach,
  const jfloat tapReach, const jfloat tapSmallSide
)
{
  gRadius.store(radius);
  gTouchRadius.store(touchRadius);
  gGap.store(gap);
  gArrowReach.store(arrowReach);
  gTapReach.store(tapReach);
  gTapSmallSide.store(tapSmallSide);
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
/// the group from elsewhere, else 0; false when there is none.
extern "C" JNIEXPORT jboolean JNICALL Java_io_github_m3rt1n99_fafre_Game_griff(JNIEnv* const env, jclass, const jfloatArray handle)
{
  Handle now;
  {
    const std::lock_guard<std::mutex> lock(gHandleMutex);
    now = gHandle;
  }
  const jfloat values[6] = {now.x, now.y, now.groupX, now.groupY, static_cast<jfloat>(now.count), now.off ? 1.0f : 0.0f};
  env->SetFloatArrayRegion(handle, 0, 6, values);
  return now.shown && gOn.load() ? JNI_TRUE : JNI_FALSE;
}

/// A tap's click by what it lands on (SmartTap); `again`: soon after the last tap and near it, the game's double click where
/// both click the same; `rail`: TouchInput's RAIL_ bits that count for it.
extern "C" JNIEXPORT void JNICALL
Java_io_github_m3rt1n99_fafre_Game_smartTap(JNIEnv*, jclass, const jint x, const jint y, const jboolean again, const jint rail)
{
  Win32Android_RunOnWindowThread(&SmartTapTask, Places(x, y, rail, again != JNI_FALSE ? 1 : 0));
}

extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_deselect(JNIEnv*, jclass)
{
  Win32Android_RunOnWindowThread(&DeselectTask, 0);
}

/// The camera goes to the selection.
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_toGroup(JNIEnv*, jclass)
{
  Win32Android_RunOnWindowThread(&ToGroupTask, 0);
}

/// A route's waypoints, `count` places as x, y pairs in the picture's pixels; `rail`: the RAIL_ bits that count for it.
extern "C" JNIEXPORT void JNICALL
Java_io_github_m3rt1n99_fafre_Game_route(JNIEnv* const env, jclass, const jintArray places, const jint count, const jint rail)
{
  if (count <= 0 || env->GetArrayLength(places) < 2 * count) {
    return;
  }
  std::vector<jint> values(static_cast<std::size_t>(2 * count));
  env->GetIntArrayRegion(places, 0, 2 * count, values.data());
  Route route;
  route.rail = rail;
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

/// A finger's drag from here (DragTask); `rail`: the RAIL_ bits that count for it.
extern "C" JNIEXPORT void JNICALL
Java_io_github_m3rt1n99_fafre_Game_griffDrag(JNIEnv*, jclass, const jint x, const jint y, const jboolean rested, const jint rail)
{
  Win32Android_RunOnWindowThread(&DragTask, Places(x, y, rested != JNI_FALSE ? 1 : 0, rail));
}

extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_griffDragEnd(JNIEnv*, jclass, const jint x, const jint y)
{
  Win32Android_RunOnWindowThread(&DragEndTask, Places(x, y));
}

/// A finger rested here and lifted (RestTapTask).
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_restTap(JNIEnv*, jclass, const jint x, const jint y, const jint rail)
{
  Win32Android_RunOnWindowThread(&RestTapTask, Places(x, y, rail));
}

/// Plus held, or let go: Shift's key, in the order of the clicks.
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_holdShift(JNIEnv*, jclass, const jboolean down)
{
  Win32Android_RunOnWindowThread(&ShiftTask, down != JNI_FALSE ? 1 : 0);
}
