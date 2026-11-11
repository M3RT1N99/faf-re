// Where the touch scheme Griff's handle goes (include/GriffPlace.h), on a view of the phone's picture: 1647x768, a handle a
// finger reaches 80 pixels from its middle, 120 from it to the group's units, its arrow's tip 88 from its middle.

#include <cmath>
#include <vector>

#include <GriffPlace.h>

#include "Check.h"

namespace
{
  using griffplace::Box;
  using griffplace::FreeMap;
  using griffplace::Place;
  using griffplace::Sizes;
  using griffplace::Spot;

  constexpr Box kView{0, 0, 1647, 768};
  constexpr Sizes kSizes{80, 120, 88};
  /// FAF's panels on the phone at 1.6, about: the economy and the score along the top, the construction panel and the
  /// orders along the bottom, the app's bar at the left edge.
  const std::vector<Box> kPanels{{0, 0, 520, 110}, {1180, 0, 1647, 110}, {0, 560, 1647, 768}, {0, 230, 140, 400}};

  /// A single unit at this place on the screen, as griff.lua reports it: its box is its size about its middle.
  Box Unit(const float x, const float y, const float half = 20)
  {
    return Box{x - half, y - half, x + half, y + half};
  }

  /// Whether a finger anywhere on the handle there would be on the free map: every point of a 2-pixel grid over its disc is.
  bool Clear(const FreeMap& free, const Spot& spot)
  {
    if (!spot.shown) {
      return false;
    }
    const float reach = kSizes.reach;
    for (float dy = -reach; dy <= reach; dy += 2) {
      for (float dx = -reach; dx <= reach; dx += 2) {
        if (dx * dx + dy * dy < reach * reach && !free.Free(spot.x + dx, spot.y + dy)) {
          return false;
        }
      }
    }
    return true;
  }
} // namespace

WXT_TEST(GriffPlaceAboveTheGroupWhereThereIsRoom)
{
  const FreeMap free(kView, kPanels);
  const Spot spot = Place(free, kSizes, 800, 400, Unit(800, 400), kView);
  WXT_CHECK(spot.shown);
  WXT_CHECK(!spot.off);
  WXT_CHECK_EQ(spot.x, 800.0f);
  WXT_CHECK_EQ(spot.y, 400.0f - 20 - 120);
  WXT_CHECK(Clear(free, spot));
}

WXT_TEST(GriffPlaceBelowAGroupUnderThePanelsAtTheTop)
{
  // Above the unit at 180 the top panels leave no room: below it.
  const FreeMap free(kView, kPanels);
  const Spot spot = Place(free, kSizes, 400, 180, Unit(400, 180), kView);
  WXT_CHECK(spot.shown);
  WXT_CHECK(!spot.off);
  WXT_CHECK_EQ(spot.x, 400.0f);
  WXT_CHECK_EQ(spot.y, 180.0f + 20 + 120);
  WXT_CHECK(Clear(free, spot));
}

WXT_TEST(GriffPlaceBesideAGroupAsTallAsTheFreeMap)
{
  // A column of units from the top panels to the bottom ones: neither above nor below, so right of it.
  const FreeMap free(kView, kPanels);
  const Box column{780, 120, 820, 550};
  const Spot spot = Place(free, kSizes, 800, 335, column, kView);
  WXT_CHECK(spot.shown);
  WXT_CHECK(!spot.off);
  WXT_CHECK_EQ(spot.x, 820.0f + 120);
  WXT_CHECK_EQ(spot.y, 335.0f);
}

WXT_TEST(GriffPlaceLeftOfAGroupAtTheRightEdge)
{
  const FreeMap free(kView, kPanels);
  const Box column{1560, 120, 1600, 550};
  const Spot spot = Place(free, kSizes, 1580, 335, column, kView);
  WXT_CHECK(spot.shown);
  WXT_CHECK(!spot.off);
  WXT_CHECK_EQ(spot.x, 1560.0f - 120);
}

WXT_TEST(GriffPlaceShiftsAlongTheViewsEdgeToStayOnIt)
{
  const FreeMap free(kView, {});
  const Spot spot = Place(free, kSizes, 40, 400, Unit(40, 400), kView);
  WXT_CHECK(spot.shown);
  WXT_CHECK(!spot.off);
  WXT_CHECK_EQ(spot.x, kSizes.reach);
  WXT_CHECK_EQ(spot.y, 400.0f - 20 - 120);
}

WXT_TEST(GriffPlaceNeverUnderTheAppsBar)
{
  // A group out of view to the left at the bar's height: the handle comes in from the edge past the bar, pointing at it.
  const FreeMap free(kView, kPanels);
  const Spot spot = Place(free, kSizes, -300, 315, Unit(-300, 315), kView);
  WXT_CHECK(spot.shown);
  WXT_CHECK(spot.off);
  WXT_CHECK(spot.x - kSizes.reach >= 140);
  WXT_CHECK(Clear(free, spot));
}

WXT_TEST(GriffPlaceAGroupOutOfViewLeavesRoomForTheArrow)
{
  // Nothing covers the view: the handle stays as far from the edge as its arrow at the group reaches.
  const FreeMap free(kView, {});
  const Spot spot = Place(free, kSizes, 800, -500, Unit(800, -500), kView);
  WXT_CHECK(spot.shown);
  WXT_CHECK(spot.off);
  WXT_CHECK_EQ(spot.x, 800.0f);
  WXT_CHECK_EQ(spot.y, kSizes.arrowReach);
}

WXT_TEST(GriffPlaceAGroupUnderAPanelGetsTheNearestFreePlace)
{
  // Units under the construction panel: the handle on the free map above it, pointing down at them, not beside them.
  const FreeMap free(kView, kPanels);
  const Spot spot = Place(free, kSizes, 800, 700, Unit(800, 700), kView);
  WXT_CHECK(spot.shown);
  WXT_CHECK(spot.off);
  WXT_CHECK(spot.y + kSizes.reach < 560);
  WXT_CHECK(std::abs(spot.x - 800) < 150);
  WXT_CHECK(Clear(free, spot));
}

WXT_TEST(GriffPlaceAGroupHalfUnderAPanelGetsItsHandleClearOfItsUnits)
{
  // Units from 500 down under the construction panel at 560, their middle under it: the handle above them, not on them.
  const FreeMap free(kView, kPanels);
  const Box units{760, 500, 840, 640};
  const Spot spot = Place(free, kSizes, 800, 570, units, kView);
  WXT_CHECK(spot.shown);
  WXT_CHECK(!spot.off);
  WXT_CHECK_EQ(spot.x, 800.0f);
  WXT_CHECK_EQ(spot.y, 500.0f - 120);
}

WXT_TEST(GriffPlaceKeepsClearOfASmallControlWithinItsDisc)
{
  // A control 24 pixels across, under a third of the reach, inside the disc the handle would have above the unit and clear of
  // its middle and its rim: the handle goes below instead.
  const Box small{800 + 24, 260 + 24, 800 + 48, 260 + 48};
  const FreeMap free(kView, {small});
  WXT_CHECK(free.Free(800, 260));
  WXT_CHECK(!free.DiscFree(800, 260, kSizes.reach));
  const Spot spot = Place(free, kSizes, 800, 400, Unit(800, 400), kView);
  WXT_CHECK(spot.shown);
  WXT_CHECK(!spot.off);
  WXT_CHECK_EQ(spot.x, 800.0f);
  WXT_CHECK_EQ(spot.y, 400.0f + 20 + 120);
  WXT_CHECK(Clear(free, spot));
}

WXT_TEST(GriffPlaceAnEmptyCoverCoversNothing)
{
  // The app's bar before its first layout, a control laid out with no size and one whose layout has its edges the wrong way
  // round, the last two within the handle's reach.
  const FreeMap free(kView, {Box{}, Box{820, 300, 820, 300}, Box{850, 300, 810, 250}});
  WXT_CHECK(free.DiscFree(800, 260, kSizes.reach));
}

WXT_TEST(GriffPlaceNoneWithoutRoom)
{
  WXT_CHECK(!Place(FreeMap(), kSizes, 800, 400, Unit(800, 400), kView).shown);
  WXT_CHECK(!Place(FreeMap(kView, {kView}), kSizes, 800, 400, Unit(800, 400), kView).shown);
  // A view smaller than the handle has no place for it.
  const Box tiny{0, 0, 100, 100};
  WXT_CHECK(!Place(FreeMap(tiny, {}), kSizes, 50, 50, Unit(50, 50), tiny).shown);
  // Nor has a middle that is no number.
  WXT_CHECK(!FreeMap(kView, {}).DiscFree(std::nanf(""), 400, kSizes.reach));
}

WXT_TEST(GriffPlaceTheFreeMapIsTheViewLessItsCovers)
{
  const FreeMap free(kView, kPanels);
  WXT_CHECK(free.Free(800, 400));
  WXT_CHECK(!free.Free(800, 600));
  WXT_CHECK(!free.Free(100, 300));
  WXT_CHECK(!free.Free(-1, 400));
  WXT_CHECK(!free.Free(1647, 400));
  WXT_CHECK(!FreeMap().Free(800, 400));
}
