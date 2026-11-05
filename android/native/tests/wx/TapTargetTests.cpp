// The choice of the small control a finger's tap meant (include/TapTarget.h).

#include <TapTarget.h>

#include "Check.h"

namespace
{
  constexpr float kReach = 26;
  constexpr float kSmall = 52;

  using Nearest = taptarget::Nearest<const char>;
  const char kFirst[] = "first";
  const char kSecond[] = "second";
  const char kRow[] = "row";

  /// A control's handler as UI_TapTarget reads it from Lua.
  struct Handler
  {
    int id;

    [[nodiscard]] bool IsNil() const
    {
      return id == 0;
    }

    [[nodiscard]] bool operator==(const Handler& other) const
    {
      return id == other.id;
    }
  };

  constexpr Handler kNoHandler{0};
  constexpr Handler kPlain{1};
  constexpr Handler kOwn{2};
  constexpr Handler kWrapper{3};

  /// As UI_TapTarget offers them; `withinBest`: this one is a part of the best so far.
  void Offer(Nearest& nearest, const char* const control, const taptarget::Box& box, const bool withinBest = false)
  {
    if (nearest.Wants(box, withinBest)) {
      nearest.Take(control, box);
    }
  }
} // namespace

WXT_TEST(TapTargetTheNearestOfTwoSmallControlsWins)
{
  Nearest nearest(100, 100, kReach, kSmall);
  Offer(nearest, kFirst, {120, 90, 140, 110});
  Offer(nearest, kSecond, {80, 90, 95, 110});
  WXT_CHECK(nearest.Best() == kSecond);
  WXT_CHECK_EQ(nearest.Target().x, 94.0f);
  WXT_CHECK_EQ(nearest.Target().y, 100.0f);
}

WXT_TEST(TapTargetTheDistanceIsToTheRectangleNotItsCentre)
{
  // A long and narrow control near its end is nearer than a small one with a nearer centre.
  Nearest nearest(100, 100, kReach, kSmall);
  Offer(nearest, kFirst, {115, 95, 125, 105});
  Offer(nearest, kSecond, {105, 0, 120, 300});
  WXT_CHECK(nearest.Best() == kSecond);
}

WXT_TEST(TapTargetNothingBeyondReach)
{
  Nearest nearest(100, 100, kReach, kSmall);
  Offer(nearest, kFirst, {127, 90, 140, 110});
  Offer(nearest, kFirst, {90, 127, 110, 140});
  Offer(nearest, kFirst, {120, 120, 130, 130}); // 20 and 20 apart: the corner is beyond it
  WXT_CHECK(nearest.Best() == nullptr);
}

WXT_TEST(TapTargetJustWithinReachCounts)
{
  Nearest nearest(100, 100, kReach, kSmall);
  Offer(nearest, kFirst, {126, 90, 140, 110});
  WXT_CHECK(nearest.Best() == kFirst);
}

WXT_TEST(TapTargetOnlySmallControls)
{
  Nearest nearest(100, 100, kReach, kSmall);
  Offer(nearest, kFirst, {110, 100, 162, 300}); // its short side is the limit
  Offer(nearest, kFirst, {110, 100, 111, 110}); // a line of the UI
  WXT_CHECK(nearest.Best() == nullptr);
  Offer(nearest, kSecond, {110, 100, 161, 300});
  WXT_CHECK(nearest.Best() == kSecond);
}

WXT_TEST(TapTargetATapOnASmallControlStaysWhereItLanded)
{
  Nearest nearest(100, 100, kReach, kSmall);
  Offer(nearest, kFirst, {110, 90, 130, 110});
  Offer(nearest, kSecond, {90, 90, 110, 110});
  WXT_CHECK(nearest.Best() == kSecond);
  WXT_CHECK_EQ(nearest.Target().x, 100.0f);
  WXT_CHECK_EQ(nearest.Target().y, 100.0f);
  // Nothing goes ahead of it now, not even another it lands on.
  Offer(nearest, kFirst, {95, 95, 105, 105});
  WXT_CHECK(nearest.Best() == kSecond);
}

WXT_TEST(TapTargetASmallRowGivesWayToTheChoiceInIt)
{
  // Options' row, 40 high and 715 wide: it takes only the wheel. Its choice is 26 high, and the tap 5 above that, on the row.
  Nearest nearest(500, 100, kReach, kSmall);
  Offer(nearest, kRow, {100, 90, 815, 130});
  WXT_CHECK(nearest.Best() == kRow);
  Offer(nearest, kFirst, {400, 105, 700, 131}, true);
  WXT_CHECK(nearest.Best() == kFirst);
  WXT_CHECK_EQ(nearest.Target().x, 500.0f);
  WXT_CHECK_EQ(nearest.Target().y, 105.0f);
}

WXT_TEST(TapTargetARowKeepsATapWithNoChoiceInReach)
{
  Nearest nearest(150, 100, kReach, kSmall);
  Offer(nearest, kRow, {100, 90, 815, 130});
  Offer(nearest, kFirst, {400, 105, 700, 131}, true);
  WXT_CHECK(nearest.Best() == kRow);
  WXT_CHECK_EQ(nearest.Target().x, 150.0f);
  WXT_CHECK_EQ(nearest.Target().y, 100.0f);
}

WXT_TEST(TapTargetWhatTookOverFromARowGoesOnBeingCompared)
{
  // The choice 15 away goes ahead of its row, and the next row's choice 10 away goes ahead of it, as any nearer one does.
  Nearest nearest(500, 100, kReach, kSmall);
  Offer(nearest, kRow, {100, 90, 815, 130});
  Offer(nearest, kFirst, {400, 115, 700, 131}, true);
  WXT_CHECK(nearest.Best() == kFirst);
  Offer(nearest, kSecond, {400, 80, 700, 90});
  WXT_CHECK(nearest.Best() == kSecond);
}

WXT_TEST(TapTargetOnlyAPartGoesAheadOfWhatTheTapIsOn)
{
  Nearest nearest(500, 100, kReach, kSmall);
  Offer(nearest, kRow, {100, 90, 815, 130});
  Offer(nearest, kFirst, {400, 80, 700, 90});
  WXT_CHECK(nearest.Best() == kRow);
}

WXT_TEST(TapTargetANearMissClicksTheNearestPixelNotTheCentre)
{
  // A slider's bar under a label: the press sets the value where it lands, so it stays under the finger.
  const taptarget::Box bar{200, 100, 500, 120};
  taptarget::Place place = bar.NearestPixel(260, 90);
  WXT_CHECK_EQ(place.x, 260.0f);
  WXT_CHECK_EQ(place.y, 100.0f);
  place = bar.NearestPixel(260, 130);
  WXT_CHECK_EQ(place.x, 260.0f);
  WXT_CHECK_EQ(place.y, 119.0f);
  place = bar.NearestPixel(190, 110);
  WXT_CHECK_EQ(place.x, 200.0f);
  WXT_CHECK_EQ(place.y, 110.0f);
  place = bar.NearestPixel(510, 90);
  WXT_CHECK_EQ(place.x, 499.0f);
  WXT_CHECK_EQ(place.y, 100.0f);
}

WXT_TEST(TapTargetTheNearestPixelIsOneAPressHits)
{
  // A hit test takes left and top, and leaves out right and bottom: every pixel it gives lies in the box, also at fractions.
  for (const taptarget::Box box : {taptarget::Box{10, 10, 20, 20}, taptarget::Box{10.4f, 10.6f, 20.6f, 20.4f}, taptarget::Box{10.5f, 10.5f, 12.5f, 12.5f}}) {
    for (float y = 0; y <= 30; y += 5) {
      for (float x = 0; x <= 30; x += 5) {
        const taptarget::Place place = box.NearestPixel(x, y);
        WXT_CHECK(place.x == static_cast<float>(static_cast<int>(place.x)));
        WXT_CHECK(place.y == static_cast<float>(static_cast<int>(place.y)));
        WXT_CHECK_EQ(box.SquaredDistanceTo(place.x, place.y), 0.0f);
      }
    }
  }
}

WXT_TEST(TapTargetAPlaceOnTheBoxIsItsOwnNearestPixel)
{
  const taptarget::Box box{10.4f, 10.4f, 20.6f, 20.6f};
  for (float y = 11; y <= 20; ++y) {
    for (float x = 11; x <= 20; ++x) {
      const taptarget::Place place = box.NearestPixel(x, y);
      WXT_CHECK_EQ(place.x, x);
      WXT_CHECK_EQ(place.y, y);
    }
  }
}

WXT_TEST(TapTargetAPlainControlDoesNotActOnAPress)
{
  WXT_CHECK(!taptarget::ActsOnAPress(kPlain, kNoHandler, kPlain));
  WXT_CHECK(!taptarget::ActsOnAPress(kNoHandler, kNoHandler, kPlain));
}

WXT_TEST(TapTargetAHandlerOfItsOwnActsOnAPress)
{
  WXT_CHECK(taptarget::ActsOnAPress(kOwn, kNoHandler, kPlain));
}

WXT_TEST(TapTargetATooltipOnALabelDoesNotMakeItActOnAPress)
{
  // tooltip.lua's wrapper around the plain handler: the game's time, the economy's numbers.
  WXT_CHECK(!taptarget::ActsOnAPress(kWrapper, kPlain, kPlain));
}

WXT_TEST(TapTargetATooltipOnAButtonLeavesItAButton)
{
  WXT_CHECK(taptarget::ActsOnAPress(kWrapper, kOwn, kPlain));
}

WXT_TEST(TapTargetTheDistanceToABoxIsZeroFromEdgeToEdge)
{
  const taptarget::Box box{10, 10, 20, 20};
  WXT_CHECK_EQ(box.SquaredDistanceTo(10, 10), 0.0f);
  WXT_CHECK_EQ(box.SquaredDistanceTo(19, 10), 0.0f);
  WXT_CHECK_EQ(box.SquaredDistanceTo(20, 10), 0.0f);
  WXT_CHECK_EQ(box.SquaredDistanceTo(23, 14), 9.0f);
  WXT_CHECK_EQ(box.SquaredDistanceTo(7, 6), 25.0f);
}
