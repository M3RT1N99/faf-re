#pragma once

// The touch scheme Griff's covers (buildstage/griff-v3-spec.md T3.7): the layer's own pieces over the game's picture, the undo's
// button while it shows and a short while after it went (Lingering). A finger on one is the piece's, never the game's; the
// program keeps the handle clear of them and finds nothing of the game's under them for a gesture, as under FAF's panels. Each
// frame the layer gives every piece, shown or not, in the picture's pixels; the program hears the covering ones when they
// changed.

#include <algorithm>
#include <array>

#include "touch/Numbers.h"

namespace touch
{
  class Covers
  {
  public:
    /// A piece whose cover stays a short while after it went, where it last showed: the undo's button, which goes when its
    /// time runs out, maybe under a finger already on its way to it. A tap meant for it that comes late then lands on nothing,
    /// never on the map beneath; but not where the player has opened something of FAF's (Forget). The program finds a tap's
    /// place covered when it clicks, a settle after the finger's down (TouchInput::kSettleMilliseconds), so the time counts
    /// from the piece's last frame to that click.
    class Lingering
    {
    public:
      /// A piece that covers for `after` milliseconds past its last frame shown.
      explicit Lingering(const Millis after) : mAfter(after)
      {}

      /// The piece's frame at `time`: whether it covers, at the box of its last frame shown (Box()).
      bool CoversNow(const bool shown, const int left, const int top, const int right, const int bottom, const Millis time)
      {
        if (shown && right > left && bottom > top) {
          mShownOnce = true;
          mShownAt = time;
          mBox = {left, top, right, bottom};
        }
        return mShownOnce && time - mShownAt < mAfter;
      }

      /// Where it last showed: left, top, right and bottom.
      [[nodiscard]] const std::array<int, 4>& Box() const
      {
        return mBox;
      }

      /// It covers no more until it shows again: something the player opened of FAF's lies where it last showed (Opened),
      /// and a tap there is for that.
      void Forget()
      {
        mShownOnce = false;
      }

    private:
      Millis mAfter;
      bool mShownOnce = false;
      Millis mShownAt = 0;
      std::array<int, 4> mBox{};
    };

    /// More pieces than the layer has.
    static constexpr int kMost = 16;

    /// The pieces of a frame begin.
    void Begin()
    {
      mCount = 0;
    }

    /// A piece, in the picture's pixels: it covers while it is shown and has a size.
    void Piece(const bool shown, const int left, const int top, const int right, const int bottom)
    {
      if (!shown || right <= left || bottom <= top || mCount == kMost) {
        return;
      }
      const int at = 4 * mCount++;
      mBoxes[at] = left;
      mBoxes[at + 1] = top;
      mBoxes[at + 2] = right;
      mBoxes[at + 3] = bottom;
    }

    /// A piece that lingers, at `time`: it covers while it is shown and a while after, where it last showed.
    void Piece(
      Lingering& lingering,
      const bool shown,
      const int left,
      const int top,
      const int right,
      const int bottom,
      const Millis time
    )
    {
      const bool covers = lingering.CoversNow(shown, left, top, right, bottom, time);
      const std::array<int, 4>& box = lingering.Box();
      Piece(covers, box[0], box[1], box[2], box[3]);
    }

    /// The frame's pieces are given: whether they are not what the program heard last, which it is to hear now.
    bool End()
    {
      const bool same = mCount == mHeardCount && std::equal(mBoxes.begin(), mBoxes.begin() + 4 * mCount, mHeard.begin());
      if (same) {
        return false;
      }
      mHeard = mBoxes;
      mHeardCount = mCount;
      return true;
    }

    /// The covers, Count() boxes of left, top, right and bottom.
    [[nodiscard]] const std::array<int, 4 * kMost>& Boxes() const
    {
      return mBoxes;
    }

    [[nodiscard]] int Count() const
    {
      return mCount;
    }

  private:
    std::array<int, 4 * kMost> mBoxes{};
    int mCount = 0;
    std::array<int, 4 * kMost> mHeard{};
    int mHeardCount = -1;
  };
} // namespace touch
