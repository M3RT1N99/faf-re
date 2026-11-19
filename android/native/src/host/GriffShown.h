#pragma once

// What the touch scheme Griff shows that the activity and the program know of (android/TOUCH_UI.md "Griff", GriffShown.cpp):
// the numbers of each piece, which FAF's UI draws in FAF's look each frame (assets/fafre_ui/lua/fafre/griffmap.lua,
// FafreGriffShown).

#include <vector>

namespace griffshown
{
  /// Game.SHOWN_: the pieces. The activity says the handle as the screen keeps it, a resting finger's ring, a stroke, Box's
  /// box over deposits and the loupe's circle, in the picture's pixels (Game.setGriffShown); the program what a rest found,
  /// which the ring's look shows (Game.REST_).
  enum Piece : int
  {
    kHandle = 0,
    kRing = 1,
    kStroke = 2,
    kBox = 3,
    kLoupe = 4,
    kRest = 5,
    kPieces = 6,
  };

  /// A piece's numbers from now on; none: it does not show. Any thread.
  void Set(Piece piece, std::vector<float> values);
} // namespace griffshown
