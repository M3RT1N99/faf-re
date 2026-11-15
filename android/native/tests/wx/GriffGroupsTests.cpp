// The touch scheme Griff's groups (include/GriffGroups.h): which group a unit is in, by the gesture that ordered it last or
// the run of the factory that made it; from which zoom the game draws a unit's strategic icon, which a group's icon follows;
// and which icon a tap is on.

#include <cmath>
#include <limits>
#include <vector>

#include <GriffGroups.h>

#include "Check.h"

namespace
{
  using griffgroups::Book;
  using griffgroups::DrawsIcon;
  using griffgroups::Icon;
  using griffgroups::IconAt;
  using griffgroups::Origin;

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
