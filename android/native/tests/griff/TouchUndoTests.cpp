// Griff's undo as its button shows it (src/touch/Undo.h): 3 s after an order, one level, a tap once, the waiting mark. The
// cases of the app's UndoTest.java.

#include <cmath>

#include "touch/Undo.h"

#include "Check.h"

#define CHECK_NEAR(actual, expected, delta)                                                                              \
  WXT_CHECK(std::abs(static_cast<double>(actual) - static_cast<double>(expected)) <= (delta))

namespace
{
  struct UndoFixture
  {
    touch::Undo undo;
  };
} // namespace

#define UNDO_TEST(name) WXT_FIXTURE_TEST(UndoFixture, name)

UNDO_TEST(UndoThePillShowsForThreeSecondsAfterAnOrderWithItsTimeLeft)
{
  // No order yet.
  WXT_CHECK(!undo.Showing(0));
  undo.Ordered(1, 1000);
  WXT_CHECK(undo.Showing(1000));
  WXT_CHECK(undo.Showing(3999));
  WXT_CHECK(!undo.Showing(4000));
  CHECK_NEAR(undo.Left(1000), 1, 0);
  CHECK_NEAR(undo.Left(2500), 0.5f, 1e-6);
  CHECK_NEAR(undo.Left(4000), 0, 0);
  WXT_CHECK_EQ(undo.Tap(4000), 0);
}

UNDO_TEST(UndoANewOrdersRecordReplacesTheLastWithItsOwnTime)
{
  undo.Ordered(1, 1000);
  undo.Ordered(2, 2500);
  WXT_CHECK(undo.Showing(5000));
  WXT_CHECK_EQ(undo.Tap(5000), 2);
}

UNDO_TEST(UndoTheLaddersAttackMoveKeepsTheMovesRecordAndTime)
{
  undo.Ordered(1, 1000);
  undo.Ordered(1, 1300);
  WXT_CHECK(undo.Showing(3999));
  WXT_CHECK(!undo.Showing(4000));
}

UNDO_TEST(UndoATapTakesTheRecordBackOnceAndWaitsTillItIs)
{
  undo.Ordered(3, 0);
  // Not tapped.
  WXT_CHECK(!undo.Waiting());
  WXT_CHECK_EQ(undo.Tap(100), 3);
  WXT_CHECK(undo.Waiting());
  // Tapped already.
  WXT_CHECK_EQ(undo.Tap(200), 0);
  // Another record's.
  WXT_CHECK(!undo.Undone(2));
  WXT_CHECK(undo.Showing(300));
  WXT_CHECK(undo.Waiting());
  // The pill's.
  WXT_CHECK(undo.Undone(3));
  WXT_CHECK(!undo.Showing(300));
  WXT_CHECK(!undo.Waiting());
  WXT_CHECK_EQ(undo.Tap(300), 0);
}

UNDO_TEST(UndoAWordThatChangedAfterAnUndoOverEarlierIsNoneOfThePillsRecord)
{
  // The undo of record 9 said all of it went; FAF's assist-to-upgrade ordered an upgrade for one of its Guards only after
  // that, and the program tells the undo's word once more, now in part, while the pill shows the next record.
  undo.Ordered(9, 0);
  undo.Tap(100);
  WXT_CHECK(undo.Undone(9));
  undo.Ordered(10, 500);
  WXT_CHECK(!undo.Undone(9));
  // The next record's pill stays.
  WXT_CHECK(undo.Showing(600));
  WXT_CHECK_EQ(undo.Tap(600), 10);
  // Nor before any record.
  WXT_CHECK(!touch::Undo().Undone(9));
}

UNDO_TEST(UndoATappedPillStaysWhileItWaitsPastItsThreeSeconds)
{
  // The undo waits for the sim to take the gesture first, a network game's lag.
  undo.Ordered(4, 0);
  WXT_CHECK_EQ(undo.Tap(2900), 4);
  WXT_CHECK(undo.Showing(3500));
  WXT_CHECK(undo.Waiting());
  // Tapped already.
  WXT_CHECK_EQ(undo.Tap(3500), 0);
  undo.Undone(4);
  WXT_CHECK(!undo.Showing(3600));
}

UNDO_TEST(UndoAnOrderThatEndsTheRecordTakesThePillAway)
{
  // FAF's ghost, a key's order or the construction panel's: only the last order counts, and it is no gesture's.
  undo.Ordered(5, 0);
  undo.Ordered(0, 500);
  WXT_CHECK(!undo.Showing(600));
  WXT_CHECK_EQ(undo.Tap(600), 0);
  undo.Ordered(6, 700);
  WXT_CHECK(undo.Showing(800));
}

UNDO_TEST(UndoANewOrderAfterATapIsTheNewRecordsNotTapped)
{
  undo.Ordered(1, 0);
  WXT_CHECK_EQ(undo.Tap(100), 1);
  undo.Ordered(2, 200);
  WXT_CHECK(!undo.Waiting());
  WXT_CHECK_EQ(undo.Tap(300), 2);
}

// The next two characterize how the pill takes what the program tells after an undo that clicked nothing. They cannot pin the
// rule that such a record holds and is offered again: the program decides it (GriffAndroid.cpp FinishUndo and OfferAgain,
// which tell its undone and then its ordered anew), and Undo has always taken a record told after its undo as a new one, so
// they pass on Undo as it was before that rule too. They keep the pill to what the program relies on.
UNDO_TEST(UndoCharacterizationARecordNotUndoneThatTheProgramTellsAgainIsOfferedForItsTimeOnceMore)
{
  // Nothing of it was clicked (its clicks ended before their first step): the record holds, and the program tells it anew
  // after the undo's word.
  undo.Ordered(7, 0);
  WXT_CHECK_EQ(undo.Tap(1000), 7);
  undo.Undone(7);
  WXT_CHECK(!undo.Showing(1100));
  undo.Ordered(7, 5000);
  WXT_CHECK(undo.Showing(5000));
  // Not tapped.
  WXT_CHECK(!undo.Waiting());
  CHECK_NEAR(undo.Left(5000), 1, 0);
  WXT_CHECK(undo.Showing(7999));
  WXT_CHECK(!undo.Showing(8000));
  // Tapped again.
  WXT_CHECK_EQ(undo.Tap(6000), 7);
  WXT_CHECK(undo.Waiting());
}

UNDO_TEST(UndoCharacterizationARecordNotUndoneThatTheProgramDroppedIsGone)
{
  // A new order ended it while its undo waited for the sim, or the game did: the pill goes with the word.
  undo.Ordered(8, 0);
  WXT_CHECK_EQ(undo.Tap(100), 8);
  undo.Undone(8);
  WXT_CHECK(!undo.Showing(200));
  WXT_CHECK(!undo.Waiting());
  WXT_CHECK_EQ(undo.Tap(200), 0);
}

UNDO_TEST(UndoWithoutTheGameThereIsNoPill)
{
  undo.Ordered(1, 0);
  undo.Tap(5);
  undo.Clear();
  WXT_CHECK(!undo.Showing(10));
  WXT_CHECK(!undo.Waiting());
  WXT_CHECK_EQ(undo.Tap(10), 0);
}
