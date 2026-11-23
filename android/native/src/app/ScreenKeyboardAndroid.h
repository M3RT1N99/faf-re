#pragma once

// The phone's screen keyboard for the game's text fields (ScreenKeyboardAndroid.cpp).

namespace screenkeyboard
{
  /// Watches the window system's events for a finger's press on a text field, Enter, and the keyboard going. On the program's
  /// thread, before the engine reads its first event.
  void Install();

  /// The keyboard comes, or goes where it is up, as FAF's Menu tab's Keyboard asks. On the program's thread.
  void Toggle();
  /// The keyboard goes, where it is up: a new page has no field it was for. On the program's thread.
  void Dismiss();
  /// Whether SDL's keyboard is up.
  [[nodiscard]] bool Shown();
  /// A finger's press the host gave the engine, in its pixels (src/host/HostSdl.cpp, which SDL does not see): the
  /// keyboard comes where it lands on a text field. On the program's thread.
  void FingerPressed(float x, float y);
} // namespace screenkeyboard
