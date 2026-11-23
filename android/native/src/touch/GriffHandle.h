#pragma once

// Griff's handle as the screen shows it: where the program placed it in the last frame while a game's frames are read, kept
// where a stroke from it began while the stroke is drawn (the mockup's), and gone once the frames are not read any more, at the
// game's end or under a dialog that takes the fingers, whatever a finger still draws: nothing would put it away later. In the
// screen's pixels.

namespace touch
{
  class GriffHandle
  {
  public:
    /// A stroke is drawn (`drawn`), or gone. Whether a new one began: it has a new number.
    bool Stroke(const bool drawn)
    {
      const bool began = drawn && !mStroking;
      if (began) {
        mStroke = (mStroke + 1) & 0xFFFF;
      }
      mStroking = drawn;
      return began;
    }

    /// The number of the stroke drawn now, or of the last.
    [[nodiscard]] int StrokeNumber() const
    {
      return mStroke;
    }

    /// The handle of this frame of the screen's: `reading` whether a game's frames are read at all, `placed` whether the
    /// program shows a handle, at `x`, `y`. False while a stroke keeps it where it began: the screen keeps what it shows.
    /// Without the frames there is none, also while a stroke is drawn.
    bool Place(const bool reading, const bool placed, const float x, const float y)
    {
      if (mStroking && reading) {
        return false;
      }
      mShown = reading && placed;
      if (mShown) {
        mX = x;
        mY = y;
      }
      return true;
    }

    [[nodiscard]] bool Shown() const
    {
      return mShown;
    }

    [[nodiscard]] float X() const
    {
      return mX;
    }

    [[nodiscard]] float Y() const
    {
      return mY;
    }

  private:
    bool mShown = false;
    float mX = 0;
    float mY = 0;
    /// A stroke is drawn, and its number, which the program's answers for it carry, in the 16 bits of its task.
    bool mStroking = false;
    int mStroke = 0;
  };
} // namespace touch
