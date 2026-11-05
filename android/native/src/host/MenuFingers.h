#pragma once

// The menus' fingers on the program's thread (android/TOUCH_UI.md, MenuFingers.cpp).

namespace menufingers
{
  /// A finger's click goes down here: if it is on a text field, the app shows the keyboard for it. On the program's thread.
  void NoteTapOnField(float x, float y);

  /// Whether a press here lands on a scroll bar itself, not on one of its arrows. On the program's thread.
  [[nodiscard]] bool OnScrollBar(float x, float y);
} // namespace menufingers
