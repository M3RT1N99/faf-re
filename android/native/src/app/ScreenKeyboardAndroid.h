#pragma once

// The phone's screen keyboard for the game's text fields (ScreenKeyboardAndroid.cpp).

namespace screenkeyboard
{
  /// Watches the window system's events for a finger's press on a text field, Enter, and the keyboard going. On the program's
  /// thread, before the engine reads its first event.
  void Install();

  /// The keyboard comes, or goes where it is up, as FAF's Menu tab's Keyboard asks. On the program's thread.
  void Toggle();
} // namespace screenkeyboard
