#pragma once

// The menus' fingers on the program's thread (android/TOUCH_UI.md, MenuFingers.cpp).

namespace menufingers
{
  /// A left press goes down here: if it is on a text field, the app shows the keyboard for it. On the program's thread.
  void NoteTapOnField(float x, float y);
} // namespace menufingers
