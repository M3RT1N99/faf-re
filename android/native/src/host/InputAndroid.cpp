// The activity's input on SDL's activity: the natives of the app's Game class that a finger's input comes by, as the earlier
// host had them (android-client's GameHost.cpp). All of it comes to the program's thread as tasks (HostSdl.h), as a tap has
// to (it looks at the controls there): so all of it stays in the order it was given, and the cursor moves on that thread
// alone. Input that went straight to the engine from the activity's thread could overtake a task, or fall between its press
// and release. That costs a drag no frame: the program takes its events until none is left before each frame, and the
// moves of a long frame still make one move.
//
// While Griff's own clicks run (the draft's tick's, the undo's), a finger's input does nothing: it would come between them
// in their mode, with their selection and Shift, or move the cursor they aim with. The activity holds a finger back once it
// knows of them (TouchInput), a moment after they began here; this is what it gave in that moment.

#include <jni.h>

#include <atomic>
#include <cstdint>

#include <GriffTap.h>
#include <Windows.h>

#include "HostClicks.h"
#include "HostSdl.h"
#include "MenuFingers.h"
#include "TaskPlaces.h"
#include "gpg/core/utils/Logging.h"
#include "moho/console/CConCommand.h"

namespace moho
{
  [[nodiscard]] bool UI_PlacingBuilding() noexcept;
  [[nodiscard]] bool UI_WorldViewAt(float x, float y);
  [[nodiscard]] bool UI_GhostWaitsAt(float x, float y);
  [[nodiscard]] bool UI_TapTarget(float x, float y, float reach, float smallSide, float* targetX, float* targetY);
  void UI_CancelBuildDrag();
} // namespace moho

namespace
{
  using hosttask::Argument;
  using hosttask::Coordinate;
  using hosttask::Places;

  void MoveMouse(const Argument place)
  {
    if (hostclicks::GriffClicking()) {
      return;
    }
    host::MouseMove(Coordinate(place, 0), Coordinate(place, 1));
  }

  void PressMouseButton(const Argument press)
  {
    if (hostclicks::GriffClicking()) {
      return;
    }
    host::MouseButton(Coordinate(press, 2), Coordinate(press, 3) != 0, Coordinate(press, 0), Coordinate(press, 1));
  }

  void PressTouchButton(const Argument press)
  {
    if (hostclicks::GriffClicking()) {
      return;
    }
    host::TouchButton(Coordinate(press, 2), Coordinate(press, 3) != 0, Coordinate(press, 0), Coordinate(press, 1));
  }

  void TurnWheel(const Argument turn)
  {
    if (hostclicks::GriffClicking()) {
      return;
    }
    host::MouseWheel(hosttask::SignedCoordinate(turn, 2), Coordinate(turn, 0), Coordinate(turn, 1));
  }

  /// On the program's thread, where the cursor has come to the place. PC players delete a queued order
  /// by pointing at it, within a few pixels; this takes the nearest within a part of the view, which a
  /// finger can hit. Nothing for a tap on the UI, as the cursor's place in the world then says nothing.
  void DeleteOrderAtCursor(const Argument place)
  {
    if (hostclicks::GriffClicking()
        || !moho::UI_WorldViewAt(static_cast<float>(Coordinate(place, 0)), static_cast<float>(Coordinate(place, 1)))) {
      return;
    }
    moho::CON_Execute(
      "UI_Lua local mouse = GetMouseWorldPos() "
      "local reach = GetCamera('WorldCamera'):GetZoom() * 0.1 "
      "local best, bestDistance = nil, reach * reach "
      "for _, unit in GetSelectedUnits() or {} do "
      "for _, order in unit:GetCommandQueue() or {} do "
      "local x, z = order.position[1] - mouse[1], order.position[3] - mouse[3] "
      "if x * x + z * z < bestDistance then best, bestDistance = order.ID, x * x + z * z end "
      "end end "
      "if best then DeleteCommand(best) end"
    );
  }

  /// On the program's thread, which knows what is under the finger and the ghost: a tap on the UI is the
  /// UI's, and a ghost under the UI is not there to be tapped. A mouse's click places the building, and a
  /// finger's only puts the ghost.
  void TapNearGhost(const Argument places)
  {
    if (hostclicks::GriffClicking()) {
      return;
    }
    const int fingerX = Coordinate(places, 0);
    const int fingerY = Coordinate(places, 1);
    const int ghostX = Coordinate(places, 2);
    const int ghostY = Coordinate(places, 3);
    if (moho::UI_WorldViewAt(static_cast<float>(fingerX), static_cast<float>(fingerY))
        && moho::UI_WorldViewAt(static_cast<float>(ghostX), static_cast<float>(ghostY))) {
      host::MouseButton(host::kButtonLeft, true, ghostX, ghostY);
      host::MouseButton(host::kButtonLeft, false, ghostX, ghostY);
    } else {
      host::TouchButton(host::kButtonLeft, true, fingerX, fingerY);
      host::TouchButton(host::kButtonLeft, false, fingerX, fingerY);
    }
  }

  void CancelLine(Argument)
  {
    moho::UI_CancelBuildDrag();
  }

  /// Press and release in one task, so that they stay together.
  void ClickNear(const Argument places, const int button)
  {
    if (hostclicks::GriffClicking()) {
      return;
    }
    hostclicks::ClickNear(
      static_cast<float>(Coordinate(places, 0)), static_cast<float>(Coordinate(places, 1)), static_cast<float>(Coordinate(places, 2)),
      static_cast<float>(Coordinate(places, 3)), button
    );
  }

  void Tap(const Argument places)
  {
    ClickNear(places, host::kButtonLeft);
  }

  void RightClickNear(const Argument places)
  {
    ClickNear(places, host::kButtonRight);
  }

  /// On the program's thread: a finger's press that starts a drag goes where a tap's click would. Where a tap would click nothing, a
  /// window's title, it stays where it landed: the title takes a drag. So it does on a scroll bar's thumb, which a tap's help could give
  /// away to the arrow beside it.
  void PressNear(const Argument places)
  {
    if (hostclicks::GriffClicking()) {
      return;
    }
    float targetX = 0;
    float targetY = 0;
    const float x = static_cast<float>(Coordinate(places, 0));
    const float y = static_cast<float>(Coordinate(places, 1));
    if (menufingers::OnScrollBar(x, y)
        || !moho::UI_TapTarget(x, y, static_cast<float>(Coordinate(places, 2)), static_cast<float>(Coordinate(places, 3)), &targetX, &targetY)) {
      targetX = x;
      targetY = y;
    }
    host::TouchButton(host::kButtonLeft, true, static_cast<int>(targetX), static_cast<int>(targetY));
  }

  /// Off every control, where the pointer goes when no finger is on the glass.
  constexpr int kPointerAway = -1000;

  /// How many fingers have come down: a leave that was given before the last of them has come too late.
  std::atomic<Argument> gFingersDown{0};

  /// Whether the pointer's leave comes, and the one that waits while Griff holds the cursor (on the program's thread).
  grifftap::Leave gLeave;

  /// The pointer leaves what it is over, as a mouse that leaves the window does, so that rollovers and tooltips go. A
  /// building's ghost follows the pointer over the world, and stays with it.
  void LeaveNow()
  {
    int x = 0;
    int y = 0;
    host::CursorPos(x, y);
    if (!moho::UI_GhostWaitsAt(static_cast<float>(x), static_cast<float>(y))) {
      host::MouseMove(kPointerAway, kPointerAway);
    }
  }

  /// On the program's thread, unless a finger has come since: the pointer leaves (LeaveNow). It stays with the cursor Griff's
  /// own clicks aim with while they run, which a finger lifted before they began would take away; where Griff holds the cursor
  /// for its Distribute orders' frame, the leave comes once it is freed (hostclicks::CursorFreed).
  void PointerLeave(const Argument fingersDownThen)
  {
    if (gLeave.Asked(fingersDownThen, gFingersDown.load(), hostclicks::GriffClicking(), hostclicks::GriffHoldsCursor())) {
      LeaveNow();
    }
  }

  /// The keys of Android's (AKEYCODE_*) that the activity's bar holds, as Windows names them; 0 for any other.
  [[nodiscard]] unsigned VirtualKeyOf(const int keyCode)
  {
    switch (keyCode) {
    case 59: // AKEYCODE_SHIFT_LEFT
    case 60: // AKEYCODE_SHIFT_RIGHT
      return VK_SHIFT;
    case 113: // AKEYCODE_CTRL_LEFT
    case 114: // AKEYCODE_CTRL_RIGHT
      return VK_CONTROL;
    case 57: // AKEYCODE_ALT_LEFT
    case 58: // AKEYCODE_ALT_RIGHT
      return VK_MENU;
    default:
      return 0;
    }
  }

  void PressKey(const Argument key)
  {
    host::Key(static_cast<unsigned>(Coordinate(key, 0)), Coordinate(key, 1) != 0);
  }
} // namespace

void hostclicks::CursorFreed()
{
  if (gLeave.Freed(gFingersDown.load(), hostclicks::GriffClicking())) {
    LeaveNow();
  }
}

/// On the program's thread, which knows the controls around the place.
void hostclicks::ClickNear(const float x, const float y, const float reach, const float smallSide, const int button)
{
  float targetX = 0;
  float targetY = 0;
  if (moho::UI_TapTarget(x, y, reach, smallSide, &targetX, &targetY)) {
    host::TouchButton(button, true, static_cast<int>(targetX), static_cast<int>(targetY));
    host::TouchButton(button, false, static_cast<int>(targetX), static_cast<int>(targetY));
    menufingers::NoteTapOnField(targetX, targetY);
  }
}

extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_mouseMove(JNIEnv*, jclass, const jint x, const jint y)
{
  host::RunNewestOnGameThread(&MoveMouse, Places(x, y));
}

/// `button`: Game.BUTTON_LEFT and the rest, which are the host's numbers (host::kButtonLeft).
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_mouseButton(
  JNIEnv*, jclass, const jint button, const jboolean down, const jint x, const jint y
)
{
  host::RunOnGameThread(&PressMouseButton, Places(x, y, button, down != JNI_FALSE ? 1 : 0));
}

/// A finger's button (host::TouchButton): where a building's ghost waits, its press does not place the building
/// (CUIWorldView::HandleEvent).
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_touchButton(
  JNIEnv*, jclass, const jint button, const jboolean down, const jint x, const jint y
)
{
  host::RunOnGameThread(&PressTouchButton, Places(x, y, button, down != JNI_FALSE ? 1 : 0));
}

/// `delta` in WHEEL_DELTA units of 120 per notch, positive away from the user; less than 32768 at a time.
extern "C" JNIEXPORT void JNICALL
Java_io_github_m3rt1n99_fafre_Game_mouseWheel(JNIEnv*, jclass, const jint delta, const jint x, const jint y)
{
  host::RunOnGameThread(&TurnWheel, Places(x, y, delta));
}

/// Whether a building's ghost waits to be placed: fingers then move it (TouchInput).
extern "C" JNIEXPORT jboolean JNICALL Java_io_github_m3rt1n99_fafre_Game_placingBuilding(JNIEnv*, jclass)
{
  return moho::UI_PlacingBuilding() ? JNI_TRUE : JNI_FALSE;
}

/// A finger tapped near a building's ghost (TapNearGhost).
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_tapNearGhost(
  JNIEnv*, jclass, const jint fingerX, const jint fingerY, const jint ghostX, const jint ghostY
)
{
  host::RunOnGameThread(&TapNearGhost, Places(fingerX, fingerY, ghostX, ghostY));
}

/// A finger gave up the line of buildings it was drawing, after the input it gave so far.
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_cancelLine(JNIEnv*, jclass)
{
  host::RunOnGameThread(&CancelLine, 0);
}

/// A finger's tap, the left button's click (Tap). `reach` and `smallSide` in the picture's pixels.
extern "C" JNIEXPORT void JNICALL
Java_io_github_m3rt1n99_fafre_Game_tap(JNIEnv*, jclass, const jint x, const jint y, const jint reach, const jint smallSide)
{
  host::RunOnGameThread(&Tap, Places(x, y, reach, smallSide));
}

/// A finger's press for a drag, the left button's (PressNear). `reach` and `smallSide` in the picture's pixels.
extern "C" JNIEXPORT void JNICALL
Java_io_github_m3rt1n99_fafre_Game_pressNear(JNIEnv*, jclass, const jint x, const jint y, const jint reach, const jint smallSide)
{
  host::RunOnGameThread(&PressNear, Places(x, y, reach, smallSide));
}

/// A finger's click of the right button (RightClickNear). `reach` and `smallSide` in the picture's pixels.
extern "C" JNIEXPORT void JNICALL
Java_io_github_m3rt1n99_fafre_Game_rightClickNear(JNIEnv*, jclass, const jint x, const jint y, const jint reach, const jint smallSide)
{
  host::RunOnGameThread(&RightClickNear, Places(x, y, reach, smallSide));
}

/// No finger is on the glass (PointerLeave).
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_pointerLeave(JNIEnv*, jclass)
{
  host::RunOnGameThread(&PointerLeave, gFingersDown.load());
}

/// A finger came down, ahead of what it sends (PointerLeave).
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_pointerStay(JNIEnv*, jclass)
{
  gFingersDown.fetch_add(1);
}

/// Deletes the queued order of the selected units nearest to the cursor, which is at this place.
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_deleteOrder(JNIEnv*, jclass, const jint x, const jint y)
{
  host::RunOnGameThread(&DeleteOrderAtCursor, Places(x, y));
}

/// A key the activity's bar holds (`keyCode`, AKEYCODE_*: Shift, Ctrl, Alt) went down or up, in its place among the
/// fingers' input. A keyboard's keys take SDL's own way.
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_key(JNIEnv*, jclass, const jint keyCode, const jboolean down)
{
  const unsigned virtualKey = VirtualKeyOf(keyCode);
  if (virtualKey == 0) {
    gpg::Warnf("Host: the bar gave key %d, which only a keyboard has", static_cast<int>(keyCode));
    return;
  }
  host::RunOnGameThread(&PressKey, Places(static_cast<int>(virtualKey), down != JNI_FALSE ? 1 : 0));
}
