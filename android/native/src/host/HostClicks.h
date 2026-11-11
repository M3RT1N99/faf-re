#pragma once

// A finger's clicks on the program's thread, for the host's tasks (GameHost.cpp).

namespace hostclicks
{
  /// A click of `button` (WIN32ANDROID_BUTTON_*), press and release, where a finger's tap at this place goes: on the small
  /// control it meant within `reach` (`smallSide`: what counts as small), else where it landed; none where only a window's
  /// title or edge is (moho::UI_TapTarget). In the picture's pixels.
  void ClickNear(float x, float y, float reach, float smallSide, int button);
} // namespace hostclicks
