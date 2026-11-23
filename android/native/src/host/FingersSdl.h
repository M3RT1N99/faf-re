#pragma once

// The fingers on SDL's events (android/TOUCH_UI.md): the touch layer (src/touch) takes SDL's finger events on the program's
// thread, before the engine translates them, and gives the game what a mouse would (FingerInput.h), the touch scheme Griff's
// gestures (GriffInput.h) and the menus' pages (MenuFingers.h); while it takes them, SDL's mouse made of a finger stays out of
// the engine, so a tap is not also a click. A mouse, a pen and the keyboards keep SDL's own way. It is the finger half of
// the app's earlier activity (TouchLayer), without its views: FAF's UI draws all Griff shows (griffhud.lua, griffmap.lua).

union SDL_Event;

namespace fingers
{
  /// Whether fingers are the layer's at all, and how a game takes them: as the mouse the game was made for, or Griff.
  enum class Scheme
  {
    kOff,
    kMouse,
    kGriff,
  };

  /// The scheme from now on: a gesture under way ends, and with kOff fingers are SDL's mouse as on a desktop. On the program's
  /// thread. The process starts with the environment's FAF_TOUCH (griff, mouse; anything else or none: off).
  void SetScheme(Scheme scheme);
  [[nodiscard]] Scheme SchemeNow();

  /// The window system's hook (HostSdl.cpp): an SDL event before the engine translates it. True where it was the layer's and
  /// is taken: a finger's, the layer's timer, SDL's mouse made of a finger while the layer takes fingers, and a mouse on a page
  /// the layer shows larger, which it gives the engine at the UI's places. On the program's thread.
  [[nodiscard]] bool OnSdlEvent(const SDL_Event& event);

  /// How much of the screen's height the phone's keyboard covers (0: none), as the activity's window insets say: a page keeps
  /// the field typed into above it. From any thread.
  void KeyboardCovers(float share);
} // namespace fingers
