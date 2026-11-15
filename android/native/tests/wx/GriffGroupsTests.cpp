// The touch scheme Griff's groups (include/GriffGroups.h): which group a unit is in, by the gesture that ordered it last or
// the run of the factory that made it, and where it goes back to when an undo takes that gesture back; which units a factory's
// run holds; from which zoom the game draws a unit's strategic icon, which a group's icon follows; and which icon a tap is on.

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <GriffGroups.h>

#include "Check.h"

namespace
{
  using griffgroups::Book;
  using griffgroups::DrawsIcon;
  using griffgroups::Earlier;
  using griffgroups::Icon;
  using griffgroups::IconAt;
  using griffgroups::IconReach;
  using griffgroups::IconTakesTap;
  using griffgroups::Origin;

  /// The units' kinds, as the program's blueprints tell them apart.
  struct Kind
  {
  };

  using Runs = griffgroups::Runs<Kind>;

  /// The groups of a book, as the icons go by them: the units of each origin that holds two or more, in the book's order.
  std::vector<std::vector<int>> Groups(const Book& book)
  {
    std::vector<std::vector<int>> groups;
    for (const auto& [origin, units] : book.UnitsByOrigin()) {
      if (units.size() >= griffgroups::kGroupUnits) {
        groups.push_back(units);
      }
    }
    return groups;
  }

  using Units = std::vector<std::vector<int>>;
} // namespace

WXT_TEST(GriffGroupsTheUnitsOfAGesturesOrdersAreOneGroup)
{
  Book book;
  // A route's waypoints are orders of their own, each to all the units the gesture ordered.
  book.Ordered(4, {10, 11, 12});
  book.Ordered(4, {10, 11, 12});
  book.Ordered(5, {20, 21});
  WXT_CHECK(Groups(book) == (Units{{10, 11, 12}, {20, 21}}));
}

WXT_TEST(GriffGroupsAUnitAnotherGestureOrdersLeavesItsGroup)
{
  Book book;
  book.Ordered(4, {10, 11, 12});
  book.Ordered(5, {12, 13});
  WXT_CHECK(Groups(book) == (Units{{10, 11}, {12, 13}}));
  // A group left with one unit is none: no icon, nothing to tap.
  book.Ordered(6, {11, 13});
  WXT_CHECK(Groups(book) == (Units{{11, 13}}));
  WXT_CHECK(book.Holds(10));
  WXT_CHECK(book.Holds(12));
}

WXT_TEST(GriffGroupsTheUnitsOfAFactorysRunAreOneGroupUntilAGestureOrdersThem)
{
  Book book;
  book.Made(30, 1);
  WXT_CHECK(Groups(book).empty());
  book.Made(31, 1);
  book.Made(40, 2);
  WXT_CHECK(Groups(book) == (Units{{30, 31}}));
  // A unit the order makes after a gesture took the others joins those the gesture left.
  book.Ordered(7, {30, 31});
  book.Made(32, 1);
  book.Made(41, 2);
  WXT_CHECK(Groups(book) == (Units{{30, 31}, {40, 41}}));
  book.Made(33, 1);
  WXT_CHECK(Groups(book) == (Units{{30, 31}, {32, 33}, {40, 41}}));
}

WXT_TEST(GriffGroupsAUnitIsMadeOnce)
{
  Book book;
  book.Ordered(4, {10, 11});
  book.Made(10, 1);
  book.Made(50, 1);
  WXT_CHECK(Groups(book) == (Units{{10, 11}}));
}

WXT_TEST(GriffGroupsAGoneUnitLeavesItsGroup)
{
  Book book;
  book.Ordered(4, {10, 11, 12});
  book.Gone(11);
  WXT_CHECK(Groups(book) == (Units{{10, 12}}));
  book.Gone(12);
  WXT_CHECK(Groups(book).empty());
  WXT_CHECK(!book.Holds(12));
  // One it never held, and one gone before.
  book.Gone(99);
  book.Gone(12);
  book.Gone(10);
  WXT_CHECK(book.UnitsByOrigin().empty());
}

WXT_TEST(GriffGroupsClearForgetsAll)
{
  Book book;
  book.Ordered(4, {10, 11});
  book.Made(30, 1);
  book.Clear();
  WXT_CHECK(book.UnitsByOrigin().empty());
  WXT_CHECK(!book.Holds(10));
  WXT_CHECK(!book.Holds(30));
}

WXT_TEST(GriffGroupsTheGesturesComeBeforeTheFactoryOrders)
{
  const Origin gesture1{false, 1};
  const Origin gesture2{false, 2};
  const Origin gesture9{false, 9};
  const Origin factory1{true, 1};
  const Origin factory3{true, 3};
  WXT_CHECK(gesture9 < factory1);
  WXT_CHECK(gesture1 < gesture2);
  WXT_CHECK(!(factory1 < gesture9));
  WXT_CHECK(factory3 == (Origin{true, 3}));
  WXT_CHECK(!(factory3 == (Origin{false, 3})));
}

WXT_TEST(GriffGroupsTheGameDrawsAnIconFromItsMeshsFadeInZoom)
{
  // CWldSession::RenderStrategicIcons: the mesh while min(IconFadeInZoom, 0.89 of the farthest zoom) > the zoom.
  WXT_CHECK(!DrawsIcon(129.9f, 130, 1000));
  WXT_CHECK(DrawsIcon(130, 130, 1000));
  WXT_CHECK(DrawsIcon(400, 130, 1000));
  // A large unit's fade-in zoom past the camera's reach: from 0.89 of the farthest zoom on.
  WXT_CHECK(!DrawsIcon(800, 5000, 1000));
  WXT_CHECK(DrawsIcon(890, 5000, 1000));
}

WXT_TEST(GriffGroupsATapIsOnTheNearestIconWithinItsReach)
{
  const std::vector<Icon> icons{{100, 100, 10}, {140, 100, 10}, {300, 300, 10}};
  WXT_CHECK_EQ(IconAt(icons, 100, 100, 26), 0);
  WXT_CHECK_EQ(IconAt(icons, 125, 100, 26), 1);
  // Within its radius and the reach of the middle: 36 pixels, not 37.
  WXT_CHECK_EQ(IconAt(icons, 336, 300, 26), 2);
  WXT_CHECK_EQ(IconAt(icons, 337, 300, 26), -1);
  WXT_CHECK_EQ(IconAt({}, 100, 100, 26), -1);
}

WXT_TEST(GriffGroupsOfIconsAsNearTheOneDrawnLastTakesTheTap)
{
  const std::vector<Icon> icons{{100, 100, 10}, {140, 100, 10}, {100, 100, 10}};
  WXT_CHECK_EQ(IconAt(icons, 120, 100, 26), 2);
  WXT_CHECK_EQ(IconAt(icons, 100, 100, 26), 2);
}

WXT_TEST(GriffGroupsAnIconWithoutAPlaceIsNeverTapped)
{
  const float none = std::numeric_limits<float>::quiet_NaN();
  const std::vector<Icon> icons{{none, 100, 10}, {100, none, 10}};
  WXT_CHECK_EQ(IconAt(icons, 100, 100, 26), -1);
}

WXT_TEST(GriffGroupsWithTroopsSelectedAnIconTakesATapOnlyWithinFafsPickForThem)
{
  // The phone's picture: the tap help 26 pixels, FAF's pick with troops selected 21 (2 mm), an icon's disc 10.
  const std::vector<Icon> icons{{100, 100, 10}};
  WXT_CHECK_EQ(IconReach(false, 26, 21), 26.0f);
  WXT_CHECK_EQ(IconReach(true, 26, 21), 21.0f);
  // Nothing selected that takes orders: the tap help's reach beside the disc.
  WXT_CHECK_EQ(IconAt(icons, 135, 100, IconReach(false, 26, 21)), 0);
  // Troops selected: a tap 35 pixels from the middle is a move beside the group, 31 still the icon's.
  WXT_CHECK_EQ(IconAt(icons, 135, 100, IconReach(true, 26, 21)), -1);
  WXT_CHECK_EQ(IconAt(icons, 131, 100, IconReach(true, 26, 21)), 0);
}

WXT_TEST(GriffGroupsAnEnemyUnderTheCursorKeepsTheOrderOfATapOnAnIcon)
{
  WXT_CHECK(IconTakesTap(false, false));
  WXT_CHECK(IconTakesTap(true, false));
  // Nothing selected that takes orders: the icon selects, whatever stands there.
  WXT_CHECK(IconTakesTap(false, true));
  // Troops selected and another's unit under FAF's cursor: the tap's order (an attack) goes on it.
  WXT_CHECK(!IconTakesTap(true, true));
}

WXT_TEST(GriffGroupsAGesturesOrderSaysWhereItsUnitsWereBefore)
{
  Book book;
  book.Made(30, 1);
  book.Made(31, 1);
  book.Ordered(4, {10, 11});
  const std::vector<Earlier> moved = book.Ordered(5, {11, 30, 50});
  WXT_CHECK_EQ(moved.size(), std::size_t{3});
  WXT_CHECK(moved[0].held && moved[0].unit == 11 && moved[0].origin == (Origin{false, 4}));
  WXT_CHECK(moved[1].held && moved[1].unit == 30 && moved[1].origin == (Origin{true, 1}));
  WXT_CHECK(!moved[2].held && moved[2].unit == 50);
  // The gesture's next order (a route's next waypoint) moves nobody.
  WXT_CHECK(book.Ordered(5, {11, 30, 50}).empty());
}

WXT_TEST(GriffGroupsAnUndoPutsTheGesturesUnitsBackInTheirGroups)
{
  Book book;
  book.Made(30, 1);
  book.Made(31, 1);
  book.Ordered(4, {10, 11, 12});
  const std::vector<Earlier> earlier = book.Ordered(5, {12, 30, 50});
  WXT_CHECK(Groups(book) == (Units{{10, 11}, {12, 30, 50}}));
  book.Undone({5}, earlier);
  WXT_CHECK(Groups(book) == (Units{{10, 11, 12}, {31, 30}}));
  // A unit that was in no group is in none again.
  WXT_CHECK(!book.Holds(50));
}

WXT_TEST(GriffGroupsAnUndoLeavesUnitsAnotherGestureOrderedSince)
{
  Book book;
  book.Ordered(4, {10, 11, 12});
  const std::vector<Earlier> earlier = book.Ordered(5, {11, 12});
  book.Ordered(6, {12, 13});
  book.Gone(11);
  book.Undone({5}, earlier);
  WXT_CHECK(Groups(book) == (Units{{12, 13}}));
  WXT_CHECK(!book.Holds(11));
  WXT_CHECK(book.Holds(10));
}

WXT_TEST(GriffGroupsTheLaddersAttackMoveIsUndoneWithItsMove)
{
  Book book;
  book.Ordered(4, {10, 11});
  // The move's watch, then the attack-move's, which joins the move's record.
  std::vector<Earlier> earlier = book.Ordered(5, {10, 11});
  const std::vector<Earlier> attack = book.Ordered(6, {10, 11});
  earlier.insert(earlier.end(), attack.begin(), attack.end());
  book.Undone({5, 6}, earlier);
  WXT_CHECK(Groups(book) == (Units{{10, 11}}));
  WXT_CHECK(book.UnitsByOrigin().count(Origin{false, 4}) == 1);
}

WXT_TEST(GriffGroupsAFactorysUnitsOfOneKindAreOneRun)
{
  const Kind tank;
  Runs runs;
  runs.Ordered(1);
  runs.Building(1, 30);
  // Not finished yet: no run.
  WXT_CHECK_EQ(runs.Built(), (std::vector<int>{30}));
  const std::uint32_t first = runs.Finished(30, &tank);
  WXT_CHECK(first != 0);
  runs.Building(1, 31);
  WXT_CHECK_EQ(runs.Finished(31, &tank), first);
  runs.Building(1, 32);
  WXT_CHECK_EQ(runs.Finished(32, &tank), first);
  WXT_CHECK(runs.Built().empty());
}

WXT_TEST(GriffGroupsAnotherKindBeginsAFactorysNextRun)
{
  const Kind tank;
  const Kind scout;
  Runs runs;
  runs.Ordered(1);
  runs.Building(1, 30);
  const std::uint32_t tanks = runs.Finished(30, &tank);
  runs.Building(1, 31);
  const std::uint32_t scouts = runs.Finished(31, &scout);
  runs.Building(1, 32);
  const std::uint32_t tanksAgain = runs.Finished(32, &tank);
  WXT_CHECK(tanks != scouts);
  WXT_CHECK(tanksAgain != tanks);
  WXT_CHECK(tanksAgain != scouts);
}

WXT_TEST(GriffGroupsARepeatingQueueIsOneRunPerKind)
{
  // FAF's repeat build puts each finished order back at the queue's end: the queue never runs empty.
  const Kind tank;
  const Kind scout;
  Runs runs;
  runs.Ordered(1);
  std::vector<std::uint32_t> made;
  int unit = 30;
  for (int round = 0; round < 3; ++round) {
    runs.Building(1, unit);
    made.push_back(runs.Finished(unit++, &tank));
  }
  WXT_CHECK(made[0] == made[1] && made[1] == made[2]);
  // A repeated queue of two kinds: each change of kind begins a run, as FAF's construction panel shows each entry.
  runs.Building(1, unit);
  const std::uint32_t scouts = runs.Finished(unit++, &scout);
  runs.Building(1, unit);
  const std::uint32_t tanks = runs.Finished(unit++, &tank);
  WXT_CHECK(scouts != made[0]);
  WXT_CHECK(tanks != scouts && tanks != made[0]);
}

WXT_TEST(GriffGroupsAnEmptyQueueEndsTheRunAfterItsLastUnit)
{
  const Kind tank;
  Runs runs;
  runs.Ordered(1);
  runs.Building(1, 30);
  const std::uint32_t first = runs.Finished(30, &tank);
  runs.Building(1, 31);
  // The sync shows the queue empty a frame before the last unit is finished: that unit is still the run's.
  runs.Emptied(1);
  WXT_CHECK_EQ(runs.Finished(31, &tank), first);
  // The next order of the same kind is a run of its own.
  runs.Building(1, 32);
  const std::uint32_t next = runs.Finished(32, &tank);
  WXT_CHECK(next != 0 && next != first);
}

WXT_TEST(GriffGroupsAQueueEmptyAsItsLastUnitIsFinishedEndsTheRun)
{
  const Kind tank;
  Runs runs;
  runs.Ordered(1);
  runs.Building(1, 30);
  const std::uint32_t first = runs.Finished(30, &tank);
  runs.Building(1, 31);
  WXT_CHECK_EQ(runs.Finished(31, &tank), first);
  runs.Emptied(1);
  runs.Emptied(1);
  runs.Building(1, 32);
  WXT_CHECK(runs.Finished(32, &tank) != first);
}

WXT_TEST(GriffGroupsAUnitThatWentCountsForNoRunAndChangesNone)
{
  const Kind tank;
  Runs runs;
  runs.Ordered(1);
  runs.Building(1, 30);
  const std::uint32_t first = runs.Finished(30, &tank);
  runs.Building(1, 31);
  WXT_CHECK_EQ(runs.Finished(31, nullptr), std::uint32_t{0});
  runs.Building(1, 32);
  WXT_CHECK_EQ(runs.Finished(32, &tank), first);
}

WXT_TEST(GriffGroupsOnlyTheFactoriesGivenBuildsHaveRuns)
{
  const Kind tank;
  Runs runs;
  runs.Building(1, 30);
  WXT_CHECK(runs.Built().empty());
  WXT_CHECK_EQ(runs.Finished(30, &tank), std::uint32_t{0});
  runs.Ordered(1);
  runs.Ordered(2);
  WXT_CHECK_EQ(runs.Factories().size(), std::size_t{2});
  runs.Building(1, 31);
  runs.Gone(1);
  // A factory that went leaves its unit to no run.
  WXT_CHECK_EQ(runs.Finished(31, &tank), std::uint32_t{0});
  WXT_CHECK_EQ(runs.Factories(), (std::vector<int>{2}));
  runs.Clear();
  WXT_CHECK(runs.Factories().empty());
}
