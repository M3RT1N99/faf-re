#pragma once

// A finger's input for the engine (InputAndroid.cpp): what the touch layer gives as a mouse's, in the picture's pixels, as
// tasks on the program's thread in the order it was given (HostSdl.h). The earlier host's natives of the app's Game class
// (mouseMove, tap, pressNear and the rest), which the layer on SDL's fingers calls (FingersSdl.cpp). Any thread.

namespace fingerinput
{
  void MouseMove(int x, int y);
  /// `button`: host::kButtonLeft and the rest.
  void MouseButton(int button, bool down, int x, int y);
  /// A finger's button (host::TouchButton): where a building's ghost waits, its press does not place the building.
  void TouchButton(int button, bool down, int x, int y);
  /// `delta` in WHEEL_DELTA units of 120 per notch, positive away from the user; less than 32768 at a time.
  void MouseWheel(int delta, int x, int y);
  /// Whether a building's ghost waits to be placed: fingers then move it (TouchInput). On the program's thread.
  [[nodiscard]] bool PlacingBuilding();
  /// A finger tapped near a building's ghost: a tap on the UI is the UI's, a ghost under the UI is not there to be tapped.
  void TapNearGhost(int fingerX, int fingerY, int ghostX, int ghostY);
  /// A finger gave up the line of buildings it was drawing.
  void CancelLine();
  /// A finger's tap, the left button's click on the small control it meant; `reach` and `smallSide` in the picture's pixels.
  void Tap(int x, int y, int reach, int smallSide);
  /// A finger's press for a drag, the left button's, where a tap's click would go.
  void PressNear(int x, int y, int reach, int smallSide);
  /// A finger's click of the right button, on the small control it meant.
  void RightClickNear(int x, int y, int reach, int smallSide);
  /// No finger is on the glass: the pointer leaves what it is over, unless a finger came since.
  void PointerLeave();
  /// A finger came down, ahead of what it sends (PointerLeave).
  void PointerStay();
  /// Deletes the queued order of the selected units nearest to the cursor, which is at this place.
  void DeleteOrder(int x, int y);
  /// A key the layer holds (VK_SHIFT, VK_CONTROL, VK_MENU) went down or up, in its place among the fingers' input. A keyboard's
  /// keys take SDL's own way.
  void Key(unsigned virtualKey, bool down);
} // namespace fingerinput
