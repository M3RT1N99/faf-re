// Where the touch scheme Griff's handle goes (include/GriffPlace.h), on a view of the phone's picture: 1647x768, a handle a
// finger reaches 80 pixels from its middle, 120 from it to the group's units, its arrow's tip 88 from its middle; and on the
// app's own numbers at the HUD's factors 1.6 and 1.67 (kUltra, kSmall).

#include <cmath>
#include <limits>
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

  /// The app's numbers on the S22 Ultra (73 mm across) at the HUD's factor 1.6, for which the windows' homes were measured: the
  /// handle's sizes as GameActivity.setGriff gives them (a finger on it 7 mm from its middle, 7.5 mm from it to the units and to
  /// its arrow's tip), and what covers the map in a skirmish, about: FAF's panels as the T3 probe found them and the rail's row
  /// as GameActivity.placeRail lays it out.
  constexpr Sizes kUltra{73.64f, 78.9f, 78.9f};
  const std::vector<Box> kUltraCovers{
    {0, 4, 537, 116},       // the economy
    {0, 124, 312, 236},     // the multifunction panel
    {679, 0, 967, 108},     // the tabs
    {1101, 0, 1647, 149},   // the score
    {1525, 150, 1647, 296}, // the avatars
    {1547, 285, 1647, 471}, // the control groups' slots
    {0, 390, 555, 578},     // the unit view
    {0, 576, 651, 768},     // the orders
    {641, 541, 1647, 768},  // the construction panel
    {26, 231, 142, 347},    // Box
    {158, 231, 274, 347},   // Plus
    {290, 231, 405, 347},   // Attack
  };

  /// The same on the emulator's screen (65.3 mm) at the factor of a screen under 70 mm across, 1.67: FAF's panels grown from
  /// the corners they hold to, the rail's row with the undo's pill (GameActivity.placePill), and the chat window open at its
  /// home (chat.lua: FAF's 472 72 662 186 at 1.67).
  constexpr Sizes kSmall{82.33f, 88.21f, 88.21f};
  const std::vector<Box> kSmallCovers{
    {0, 4.2f, 560.5f, 121.1f},       // the economy
    {0, 129.4f, 325.7f, 246.3f},     // the multifunction panel
    {672.7f, 0, 974.3f, 112.7f},     // the tabs
    {1077.1f, 0, 1647, 155.5f},      // the score
    {1519.7f, 156.6f, 1647, 309},    // the avatars
    {1542.6f, 297.5f, 1647, 491.6f}, // the control groups' slots
    {0, 373.5f, 579.3f, 569.7f},     // the unit view
    {0, 567.6f, 679.5f, 768},        // the orders
    {669, 531.1f, 1647, 768},        // the construction panel
    {117, 241, 223, 347},            // Box
    {241, 241, 347, 347},            // Plus
    {365, 241, 470, 347},            // Attack
    {489, 241, 740, 347},            // the undo's pill
  };
  constexpr Box kSmallChat{788, 120, 1105, 310};

  /// These covers and one more.
  std::vector<Box> With(std::vector<Box> covers, const Box& more)
  {
    covers.push_back(more);
    return covers;
  }

  /// A single unit at this place on the screen, as griff.lua reports it: its box is its size about its middle.
  Box Unit(const float x, const float y, const float half = 20)
  {
    return Box{x - half, y - half, x + half, y + half};
  }

  /// Whether a finger anywhere on the handle there would be on the free map: every point of a 2-pixel grid over its disc is.
  bool Clear(const FreeMap& free, const Spot& spot, const float reach = kSizes.reach)
  {
    if (!spot.shown) {
      return false;
    }
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

WXT_TEST(GriffPlaceACoveredMiddleAtTheMostFactorSendsTheHandleBelowTheChat)
{
  // At 1.67 the chat window open at its home reaches 8 pixels into the handle's reach about the view's middle (823.5, 384). A
  // group under the window, or one above the pill while it shows, has no place beside it and its line to the middle is covered
  // all the way: the handle goes to the free place nearest the middle, its disc just under the window, within the two pixels
  // from one row to the next.
  const FreeMap free(kView, With(kSmallCovers, kSmallChat));
  WXT_CHECK(!free.DiscFree(823.5f, 384, kSmall.reach));
  const Spot underTheChat = Place(free, kSmall, 900, 200, Unit(900, 200), kView);
  const Spot aboveThePill = Place(free, kSmall, 650, 205, Unit(650, 205), kView);
  for (const Spot& spot : {underTheChat, aboveThePill}) {
    WXT_CHECK(spot.shown);
    WXT_CHECK(spot.off);
    WXT_CHECK(Clear(free, spot, kSmall.reach));
    WXT_CHECK_EQ(spot.x, 823.5f);
    WXT_CHECK(spot.y - kSmall.reach > kSmallChat.bottom && spot.y - kSmall.reach <= kSmallChat.bottom + 2);
  }
}

WXT_TEST(GriffPlaceAWindowDraggedOverTheMiddleSendsTheHandleAboveIt)
{
  // At 1.6, where the windows' homes leave the middle free, the player drags the chat window over the view's middle. A group
  // under it, and one out of view whose line runs under the rail's row and the unit view, get the handle above the window, the
  // free place nearest the middle: below it the construction panel leaves no room.
  const Box chat{823.5f - 152, 384 - 91, 823.5f + 152, 384 + 91};
  const FreeMap free(kView, With(kUltraCovers, chat));
  const Spot under = Place(free, kUltra, 823.5f, 384, Unit(823.5f, 384), kView);
  const Spot outOfView = Place(free, kUltra, -300, 315, Unit(-300, 315), kView);
  for (const Spot& spot : {under, outOfView}) {
    WXT_CHECK(spot.shown);
    WXT_CHECK(spot.off);
    WXT_CHECK(Clear(free, spot, kUltra.reach));
    WXT_CHECK_EQ(spot.x, 823.5f);
    WXT_CHECK(spot.y + kUltra.reach < chat.top && spot.y + kUltra.reach >= chat.top - 2);
  }
}

WXT_TEST(GriffPlaceTheNearestFreePlaceIsOneTheFreeMapTakes)
{
  // A window whose bottom just touches the handle's reach about the middle covers it, as DiscFree has it: the nearest free place
  // is a row of pixels further down. The boxes without area there cover nothing, as for DiscFree.
  const FreeMap free(kView, {Box{600, 150, 1050, 304}, Box{}, Box{820, 420, 820, 420}, Box{850, 450, 810, 400}});
  const Spot spot = Place(free, kSizes, 825, 230, Unit(825, 230), kView);
  WXT_CHECK(spot.shown);
  WXT_CHECK(spot.off);
  WXT_CHECK_EQ(spot.x, 823.5f);
  WXT_CHECK_EQ(spot.y, 386.0f);
  WXT_CHECK(free.DiscFree(spot.x, spot.y, kSizes.reach));
}

WXT_TEST(GriffPlaceAFreePlaceNoWiderThanTheHandleIsFound)
{
  // The map is covered but for a gap far from the middle where the handle's disc fits with a pixel to spare on each side: the
  // handle goes there, however narrow it is, rather than nowhere; right of the middle and left of it.
  const Box above{0, 0, 1647, 150};
  const Box below{0, 316, 1647, 768};
  const FreeMap right(kView, {above, below, Box{0, 150, 1300, 316}, Box{1462, 150, 1647, 316}});
  const Spot inRight = Place(right, kSizes, 800, 500, Unit(800, 500), kView);
  WXT_CHECK(inRight.shown);
  WXT_CHECK(inRight.off);
  WXT_CHECK_EQ(inRight.x, 1300 + kSizes.reach + 1);
  WXT_CHECK_EQ(inRight.y, 234.0f);
  WXT_CHECK(Clear(right, inRight));
  const FreeMap left(kView, {above, below, Box{0, 150, 185, 316}, Box{347, 150, 1647, 316}});
  const Spot inLeft = Place(left, kSizes, 800, 500, Unit(800, 500), kView);
  WXT_CHECK(inLeft.shown);
  WXT_CHECK_EQ(inLeft.x, 347 - kSizes.reach - 1);
  WXT_CHECK_EQ(inLeft.y, 234.0f);
  WXT_CHECK(Clear(left, inLeft));
}

WXT_TEST(GriffPlaceTheHandleGoesPastWindowsSideBySideInAnyOrder)
{
  // Windows side by side over the middle, as tall as the view, in whatever order the walk over the UI met them: the handle goes
  // past all of them on the side where the view has room for it, a pixel clear of its reach.
  const FreeMap right(kView, {Box{910, 0, 1000, 768}, Box{700, 0, 850, 768}, Box{0, 0, 700, 768}});
  const Spot pastRight = Place(right, kSizes, 823.5f, 384, Unit(823.5f, 384), kView);
  WXT_CHECK(pastRight.shown);
  WXT_CHECK_EQ(pastRight.x, 1000 + kSizes.reach + 1);
  WXT_CHECK_EQ(pastRight.y, 384.0f);
  const FreeMap left(kView, {Box{797, 0, 947, 768}, Box{647, 0, 737, 768}, Box{947, 0, 1647, 768}});
  const Spot pastLeft = Place(left, kSizes, 823.5f, 384, Unit(823.5f, 384), kView);
  WXT_CHECK(pastLeft.shown);
  WXT_CHECK_EQ(pastLeft.x, 647 - kSizes.reach - 1);
  WXT_CHECK_EQ(pastLeft.y, 384.0f);
}

WXT_TEST(GriffPlaceOfPlacesAsNearTheOneBelowThenTheLeftOne)
{
  // A window centred on the middle, as FAF centres its dialogs on the screen, leaves room above and below it as far from the
  // middle: the handle goes below. One as tall as the view leaves room left and right of it: the handle goes left.
  const FreeMap wide(kView, {Box{623.5f, 304, 1023.5f, 464}});
  const Spot below = Place(wide, kSizes, 823.5f, 384, Unit(823.5f, 384), kView);
  WXT_CHECK(below.shown);
  WXT_CHECK_EQ(below.x, 823.5f);
  WXT_CHECK_EQ(below.y, 546.0f);
  const FreeMap tall(kView, {Box{743.5f, 0, 903.5f, 768}});
  const Spot left = Place(tall, kSizes, 823.5f, 384, Unit(823.5f, 384), kView);
  WXT_CHECK(left.shown);
  WXT_CHECK_EQ(left.x, 743.5f - kSizes.reach - 1);
  WXT_CHECK_EQ(left.y, 384.0f);
}

WXT_TEST(GriffPlaceNoneWhereOnlyItsArrowWouldLeaveTheView)
{
  // The map is free only along one edge of the view, where the handle's disc fits but its arrow at a group elsewhere would
  // not stay on the screen: no handle.
  for (const Box& cover : {Box{168, 0, 1647, 768}, Box{0, 0, 1479, 768}, Box{0, 168, 1647, 768}, Box{0, 0, 1647, 600}}) {
    WXT_CHECK(!Place(FreeMap(kView, {cover}), kSizes, 800, 400, Unit(800, 400), kView).shown);
  }
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

WXT_TEST(GriffPlaceTheNearestFreeDiscKeepsToItsBoundsAndTheView)
{
  // The handle's bounds on the phone's picture: as far from the view's edges as its arrow reaches.
  const Box bounds{88, 88, 1559, 680};
  const float reach = kSizes.reach;
  float x = 0;
  float y = 0;
  // From near the top of the bounds the place is far below, past a cover over the top of the view.
  WXT_CHECK(FreeMap(kView, {Box{0, 0, 1647, 520}}).NearestDiscFree(823.5f, 100, reach, bounds, x, y));
  WXT_CHECK_EQ(x, 823.5f);
  WXT_CHECK_EQ(y, 602.0f);
  // Room only past the top or the bottom of the bounds, or past an edge of the free map's own view: none.
  WXT_CHECK(!FreeMap(kView, {Box{0, 160, 1647, 768}}).NearestDiscFree(823.5f, 100, reach, bounds, x, y));
  WXT_CHECK(!FreeMap(kView, {Box{0, 0, 1647, 608}}).NearestDiscFree(823.5f, 668, reach, bounds, x, y));
  const struct
  {
    Box view;
    Box cover;
  } narrower[] = {
    {{647, 0, 1647, 768}, {747, 0, 1647, 768}},
    {{0, 0, 1000, 768}, {0, 0, 900, 768}},
    {{0, 200, 1647, 768}, {0, 300, 1647, 768}},
    {{0, 0, 1647, 568}, {0, 0, 1647, 468}},
  };
  for (const auto& [view, cover] : narrower) {
    WXT_CHECK(!FreeMap(view, {cover}).NearestDiscFree(823.5f, 384, reach, bounds, x, y));
  }
  // Nor from a start outside the bounds, under a cover that a place within them is clear of, or from one that is no number.
  const struct
  {
    float x;
    float y;
    Box cover;
  } outside[] = {
    {10, 384, {0, 0, 100, 768}},
    {1637, 384, {1547, 0, 1647, 768}},
    {823.5f, 10, {0, 0, 1647, 100}},
    {823.5f, 758, {0, 668, 1647, 768}},
  };
  for (const auto& [startX, startY, cover] : outside) {
    WXT_CHECK(!FreeMap(kView, {cover}).NearestDiscFree(startX, startY, reach, bounds, x, y));
  }
  WXT_CHECK(!FreeMap(kView, {}).NearestDiscFree(823.5f, std::nanf(""), reach, bounds, x, y));
  // Nor in a view without end, covered all over.
  const float endless = std::numeric_limits<float>::infinity();
  const Box all{0, 0, 1647, endless};
  WXT_CHECK(!FreeMap(all, {all}).NearestDiscFree(823.5f, 384, reach, Box{88, 88, 1559, endless}, x, y));
}

WXT_TEST(GriffPlaceTheCrossReachesFromTheMiddleToThePanels)
{
  // At the view's middle (823.5, 384): the score's and the economy's panels are left and right of it, not above, so up to the
  // view's top; the app's bar at the left edge is beside the middle's height.
  const griffplace::Cross open = griffplace::CrossAt(FreeMap(kView, kPanels), kView);
  WXT_CHECK(open.top <= 1.0f);
  WXT_CHECK(open.left >= 140.0f && open.left <= 142.0f);
  WXT_CHECK(open.right >= 1645.0f && open.right < 1647.0f);
  // FAF's tabs over the middle at the top: the cross ends under them.
  std::vector<Box> panels = kPanels;
  panels.push_back(Box{700, 0, 950, 60});
  panels.push_back(Box{1500, 300, 1647, 500});
  const griffplace::Cross under = griffplace::CrossAt(FreeMap(kView, panels), kView);
  WXT_CHECK(under.top >= 60.0f && under.top <= 62.0f);
  WXT_CHECK(under.right >= 1498.0f && under.right < 1500.0f);
}

WXT_TEST(GriffPlaceTheCrossOfACoveredMiddleIsTheView)
{
  const griffplace::Cross covered = griffplace::CrossAt(FreeMap(kView, {Box{700, 300, 950, 500}}), kView);
  WXT_CHECK_EQ(covered.left, kView.left);
  WXT_CHECK_EQ(covered.top, kView.top);
  WXT_CHECK_EQ(covered.right, kView.right);
  // Nor is anything free in a free map that is none.
  const griffplace::Cross none = griffplace::CrossAt(FreeMap(), kView);
  WXT_CHECK_EQ(none.top, kView.top);
}
