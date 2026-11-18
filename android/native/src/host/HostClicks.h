#pragma once

// A finger's clicks on the program's thread, for the host's tasks (GameHost.cpp), and whether Griff's own clicks run.

namespace hostclicks
{
  /// A click of `button` (WIN32ANDROID_BUTTON_*), press and release, where a finger's tap at this place goes: on the small
  /// control it meant within `reach` (`smallSide`: what counts as small), else where it landed; none where only a window's
  /// title or edge is (moho::UI_TapTarget). In the picture's pixels.
  void ClickNear(float x, float y, float reach, float smallSide, int button);

  /// Whether Griff's own clicks run, the draft's tick's or the undo's (GriffAndroid.cpp): a finger's input reaches the game no
  /// more meanwhile.
  [[nodiscard]] bool GriffClicking() noexcept;

  /// Whether Griff holds the cursor where it put it (GriffAndroid.cpp): its Distribute orders waits for a frame drawn with the
  /// cursor at the group's middle, within the clicks' patience. The pointer's leave waits for it (grifftap::Leave). On the
  /// program's thread.
  [[nodiscard]] bool GriffHoldsCursor() noexcept;

  /// Griff holds the cursor no more (GriffAndroid.cpp): the pointer's leave that waited for it comes now, unless a finger came
  /// since it was asked (GameHost.cpp). On the program's thread.
  void CursorFreed();
} // namespace hostclicks
