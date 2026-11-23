// Griff's rail, Box and Plus (src/touch/Rail.h): a tap lights a button for the next action, a hold counts while held; how they
// look. The cases of the app's RailTest.java.

#include "touch/Rail.h"
#include "touch/TouchInput.h"

#include "Check.h"

namespace
{
  using touch::Millis;
  using touch::Rail;

  struct RailFixture
  {
    static constexpr int kBox = touch::TouchInput::kRailBox;
    static constexpr int kPlus = touch::TouchInput::kRailQueue;

    Rail rail;

    void Tap(const int bit, const Millis time)
    {
      rail.Press(bit, time);
      rail.Release(bit, time + 100, true);
    }
  };
} // namespace

#define RAIL_TEST(name) WXT_FIXTURE_TEST(RailFixture, name)

RAIL_TEST(RailATapLightsItForTheNextActionWhichPutsItOut)
{
  Tap(kPlus, 0);
  WXT_CHECK_EQ(rail.Counting(5000), kPlus);
  WXT_CHECK_EQ(rail.Shown(), kPlus);
  rail.Used(kPlus);
  WXT_CHECK_EQ(rail.Counting(5000), 0);
  WXT_CHECK_EQ(rail.Shown(), 0);
}

RAIL_TEST(RailASecondTapPutsItOut)
{
  Tap(kBox, 0);
  Tap(kBox, 1000);
  WXT_CHECK_EQ(rail.Counting(5000), 0);
}

RAIL_TEST(RailHeldItCountsUntilItGoesUpAndAMomentAfter)
{
  rail.Press(kPlus, 0);
  WXT_CHECK(rail.Held(kPlus));
  WXT_CHECK_EQ(rail.Counting(2000), kPlus);
  rail.Release(kPlus, 2000, true);
  WXT_CHECK(!rail.Held(kPlus));
  WXT_CHECK_EQ(rail.Counting(2000 + Rail::kLiftMilliseconds - 1), kPlus);
  WXT_CHECK_EQ(rail.Counting(2000 + Rail::kLiftMilliseconds), 0);
  WXT_CHECK_EQ(rail.Shown(), 0);
}

RAIL_TEST(RailAHoldThatAnActionUsedLightsNothingAndItsMomentAfterStillCounts)
{
  rail.Press(kPlus, 0);
  rail.Used(kPlus);
  WXT_CHECK_EQ(rail.Counting(50), kPlus);
  rail.Release(kPlus, 100, true);
  WXT_CHECK_EQ(rail.Shown(), 0);
  // A finger that lifts with the thumb.
  WXT_CHECK_EQ(rail.Counting(101), kPlus);
  rail.Used(kPlus);
  // One action takes the moment.
  WXT_CHECK_EQ(rail.Counting(102), 0);
}

RAIL_TEST(RailAShortPressThatAnActionUsedLightsNothing)
{
  rail.Press(kPlus, 0);
  rail.Used(kPlus);
  rail.Release(kPlus, 100, true);
  WXT_CHECK_EQ(rail.Counting(5000), 0);
}

RAIL_TEST(RailALongPressLeavesALitButtonLit)
{
  Tap(kBox, 0);
  rail.Press(kBox, 1000);
  rail.Release(kBox, 1000 + Rail::kTapMilliseconds, true);
  WXT_CHECK_EQ(rail.Shown(), kBox);
}

RAIL_TEST(RailACancelLightsNothingAndLeavesALitButtonLit)
{
  rail.Press(kBox, 0);
  rail.Cancel(kBox);
  WXT_CHECK_EQ(rail.Counting(1), 0);
  Tap(kBox, 1000);
  rail.Press(kBox, 2000);
  rail.Cancel(kBox);
  WXT_CHECK_EQ(rail.Shown(), kBox);
}

RAIL_TEST(RailAFingerThatSlidOffIsNoTap)
{
  rail.Press(kBox, 0);
  rail.Release(kBox, 100, false);
  WXT_CHECK_EQ(rail.Shown(), 0);
}

RAIL_TEST(RailATapGivesNoMomentAfter)
{
  Tap(kPlus, 0);
  Tap(kPlus, 1000);
  WXT_CHECK_EQ(rail.Counting(1101), 0);
}

RAIL_TEST(RailTheButtonsCountEachForItself)
{
  Tap(kBox, 0);
  rail.Press(kPlus, 0);
  WXT_CHECK_EQ(rail.Counting(500), kBox | kPlus);
  rail.Used(kBox);
  WXT_CHECK_EQ(rail.Counting(500), kPlus);
}

RAIL_TEST(RailAButtonLooksIdleThenHeldThenLitUntilAnActionUsesIt)
{
  WXT_CHECK_EQ(rail.Look(kPlus), Rail::kIdle);
  rail.Press(kPlus, 0);
  WXT_CHECK_EQ(rail.Look(kPlus), Rail::kHeld);
  rail.Release(kPlus, 100, true);
  WXT_CHECK_EQ(rail.Look(kPlus), Rail::kLit);
  rail.Used(kPlus);
  WXT_CHECK_EQ(rail.Look(kPlus), Rail::kIdle);
}

RAIL_TEST(RailALitButtonLooksHeldWhileAFingerIsOnItAndLitAgainAfterALongPress)
{
  Tap(kBox, 0);
  rail.Press(kBox, 1000);
  WXT_CHECK_EQ(rail.Look(kBox), Rail::kHeld);
  rail.Release(kBox, 1000 + Rail::kTapMilliseconds, true);
  WXT_CHECK_EQ(rail.Look(kBox), Rail::kLit);
}

RAIL_TEST(RailTheMomentAfterALiftCountsButLooksIdle)
{
  rail.Press(kPlus, 0);
  rail.Release(kPlus, 2000, false);
  WXT_CHECK_EQ(rail.Counting(2001), kPlus);
  WXT_CHECK_EQ(rail.Look(kPlus), Rail::kIdle);
}

RAIL_TEST(RailEachButtonLooksAsItIs)
{
  Tap(kBox, 0);
  WXT_CHECK_EQ(rail.Look(kBox), Rail::kLit);
  WXT_CHECK_EQ(rail.Look(kPlus), Rail::kIdle);
  rail.Press(kPlus, 0);
  WXT_CHECK_EQ(rail.Look(kBox), Rail::kLit);
  WXT_CHECK_EQ(rail.Look(kPlus), Rail::kHeld);
}

RAIL_TEST(RailAllOutLeavesNothingCounting)
{
  Tap(kBox, 0);
  rail.Press(kPlus, 0);
  rail.Clear();
  WXT_CHECK_EQ(rail.Counting(1), 0);
  WXT_CHECK_EQ(rail.Shown(), 0);
  WXT_CHECK(!rail.Held(kPlus));
}
