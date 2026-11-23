#pragma once

// The touch scheme Griff's undo as its button shows it (buildstage/griff-v3-spec.md T3.1, the mockup's armUndo and doUndo; FAF's
// UI draws the button, griffhud.lua): for 3 s after a gesture ordered where its undo saves work (the program says so only then:
// a replaced queue that held a building or two orders or more, or the draft's tick), a tap on the button takes the gesture's
// orders back; so it does for two records of Griff's own that order nothing, a draft of two plans or more the cross or two
// fingers' tap dropped and a control group a rest stored over other units, which it puts back. One level: a new record
// replaces the last, and any other order ends it, as do FAF's Distribute orders, a draft that opens in place of a dropped one's
// and a store that changes the group of a group's record (the program says so as a record 0). The program holds the record
// (GriffAndroid.cpp, include/GriffUndo.h) and says when one is made (Ordered) and when its undo is over (Undone), which takes
// as long as the sim takes to take the gesture and FAF's clicks that take its orders back run. Between the tap and then the
// button says it waits, also past its 3 s. An undo that clicked nothing leaves the record as it was: the program tells it
// anew, and the button offers it again for its 3 s.

#include <algorithm>

#include "touch/Numbers.h"

namespace touch
{
  class Undo
  {
  public:
    /// How long the button stays after an order.
    static constexpr Millis kMilliseconds = 3000;

    /// A gesture ordered at `time`: its record `record` is the button's from now on. The tap ladder's attack-move in place of
    /// a move tells the move's record again: its time goes on.
    void Ordered(const int record, const Millis time)
    {
      if (record != mRecord) {
        mRecord = record;
        mOrderedAt = time;
        mTapped = false;
      }
    }

    /// Whether the button shows at `time`: for its 3 s, and while a tap on it waits for the program.
    [[nodiscard]] bool Showing(const Millis time) const
    {
      return mRecord != 0 && (mTapped || time - mOrderedAt < kMilliseconds);
    }

    /// How much of the button's time is left at `time`, from 1 to 0: the bar at its bottom.
    [[nodiscard]] float Left(const Millis time) const
    {
      const float left = static_cast<float>(kMilliseconds - (time - mOrderedAt)) / kMilliseconds;
      return std::max(0.0f, std::min(1.0f, left));
    }

    /// Whether the button says it waits: tapped, and its record not taken back yet.
    [[nodiscard]] bool Waiting() const
    {
      return mTapped;
    }

    /// The button was tapped at `time`: the record to take back, or 0 where there is none (gone, or tapped already).
    int Tap(const Millis time)
    {
      if (!Showing(time) || mTapped) {
        return 0;
      }
      mTapped = true;
      return mRecord;
    }

    /// The undo of record `record` is over, whatever it did: the button goes. Whether it was the button's record: the program
    /// also tells of an undo over earlier, whose word changed afterwards (an upgrade FAF ordered for one of its assists only
    /// then).
    bool Undone(const int record)
    {
      if (record != mRecord) {
        return false;
      }
      mRecord = 0;
      mTapped = false;
      return true;
    }

    /// No game any more: no button.
    void Clear()
    {
      mRecord = 0;
      mTapped = false;
    }

  private:
    /// The record (0: none), when it was made, and whether the button was tapped for it.
    int mRecord = 0;
    Millis mOrderedAt = 0;
    bool mTapped = false;
  };
} // namespace touch
