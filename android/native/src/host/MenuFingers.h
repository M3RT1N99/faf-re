#pragma once

// The menus' fingers on the program's thread (android/TOUCH_UI.md, MenuFingers.cpp): what FAF's Lua declares for them, which
// the touch layer reads (FingersSdl.cpp), and a list's share of a finger's drag.

namespace menufingers
{
  /// Who hears that something is new for the fingers (a page, whose the fingers are, a list's answer, what no list took, lines
  /// to show): called on the program's thread, in a task of its own. Set once, before the program's input.
  void SetListener(void (*heard)());

  /// A screen's page, in the picture's pixels; none: the view is the whole picture.
  struct Page
  {
    bool shown = false;
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
  };

  /// The page FAF's Lua has declared. Any thread.
  [[nodiscard]] Page PageNow();
  /// Whether fingers act as in the menus, not as in a game: the front end's are the menus' from the start, a session says
  /// otherwise. Any thread.
  [[nodiscard]] bool MenuFingers();
  /// The session's UI factor; 0: nothing has said yet. Any thread.
  [[nodiscard]] float HudFactor();

  /// What list a finger that came down here would scroll: ListAnswer says, for the last finger that asked.
  void LookForList(int x, int y);
  constexpr int kNotKnownYet = 0;
  constexpr int kNoList = 1;
  constexpr int kList = 2;
  [[nodiscard]] int ListAnswer();
  /// This much of a finger's drag, in the picture's pixels positive downwards, for the last finger's list; what it cannot take
  /// goes to the page (TakePageDrag).
  void ScrollList(float drag);
  /// What no list took since the last call, in the picture's pixels.
  [[nodiscard]] float TakePageDrag();

  /// The keyboard is coming: TakeToShow gives the text field with the focus.
  void AskForField();
  /// The lines to bring into view, and why: none, a finger tapped a text field, AskForField's field, FAF's Lua says so.
  constexpr int kNoPlace = 0;
  constexpr int kFieldTapped = 1;
  constexpr int kFieldAskedFor = 2;
  constexpr int kLinesToShow = 3;

  struct ToShow
  {
    int why = kNoPlace;
    int top = 0;
    int bottom = 0;
  };

  /// What is to be brought into view since the last call.
  [[nodiscard]] ToShow TakeToShow();

  /// A finger's click goes down here: if it is on a text field, the keyboard comes for it. On the program's thread.
  void NoteTapOnField(float x, float y);

  /// Whether a press here lands on a scroll bar itself, not on one of its arrows. On the program's thread.
  [[nodiscard]] bool OnScrollBar(float x, float y);
} // namespace menufingers
