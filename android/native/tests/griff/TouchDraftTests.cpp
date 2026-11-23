// Griff's draft as the screen shows it (src/touch/Draft.h): what the program's numbers say, and which plan a finger is on. The
// cases of the app's DraftTest.java.

#include <array>
#include <cmath>
#include <optional>
#include <vector>

#include "touch/Draft.h"

#include "Check.h"

#define CHECK_NEAR(actual, expected, delta)                                                                              \
  WXT_CHECK(std::abs(static_cast<double>(actual) - static_cast<double>(expected)) <= (delta))

namespace
{
  using touch::Draft;

  /// A picture shown at half its size, 10 pixels in from the left and 20 from the top.
  struct Half final : Draft::Screen
  {
    float X(const float pictureX) const override
    {
      return pictureX / 2 + 10;
    }

    float Y(const float pictureY) const override
    {
      return pictureY / 2 + 20;
    }
  };

  /// The picture as the screen.
  struct Same final : Draft::Screen
  {
    float X(const float pictureX) const override
    {
      return pictureX;
    }

    float Y(const float pictureY) const override
    {
      return pictureY;
    }
  };

  const Half kHalf;
  const Same kSame;
  constexpr int kFree = Draft::kFree;

  using Place = std::array<float, 8>;
  using Values = std::array<float, Draft::kValues>;

  /// The program's numbers: open, its building on a deposit or not, the counts, the resting finger on the map, then the
  /// places of plans, line and aim.
  Values Shown(
    const bool picksDeposit,
    const std::vector<Place>& plans,
    const std::vector<Place>& line,
    const std::optional<Place>& aim
  )
  {
    Values values{};
    values[0] = 1;
    values[1] = picksDeposit ? 1 : 0;
    values[2] = static_cast<float>(plans.size());
    values[3] = static_cast<float>(line.size());
    values[4] = aim.has_value() ? 1 : 0;
    values[5] = aim.has_value() ? 1 : 0;
    std::size_t at = 6;
    for (const Place& place : plans) {
      std::copy(place.begin(), place.end(), values.begin() + static_cast<long>(at));
      at += 8;
    }
    for (const Place& place : line) {
      std::copy(place.begin(), place.end(), values.begin() + static_cast<long>(at));
      at += 8;
    }
    if (aim.has_value()) {
      std::copy(aim->begin(), aim->end(), values.begin() + static_cast<long>(at));
    }
    return values;
  }

  /// A place: its number, its middle, the box its ground takes, its flags.
  Place At(const int id, const float x, const float y, const float half, const int flags)
  {
    return {static_cast<float>(id), x, y, x - half, y - half, x + half, y + half, static_cast<float>(flags)};
  }

  int Count(const Values& values)
  {
    return 6 + 8 * (static_cast<int>(values[2]) + static_cast<int>(values[3]) + static_cast<int>(values[4]));
  }

  Draft Made(const Values& values)
  {
    Draft draft;
    draft.Update(values.data(), Count(values), kSame);
    return draft;
  }
} // namespace

WXT_TEST(DraftADraftIsClosedWithoutNumbersOrWithAZero)
{
  Draft draft;
  const Values none{};
  draft.Update(none.data(), 0, kSame);
  WXT_CHECK(!draft.Open());
  const float zero[] = {0};
  draft.Update(zero, 1, kSame);
  WXT_CHECK(!draft.Open());
  WXT_CHECK_EQ(draft.Plans(), 0);
  WXT_CHECK(!draft.AimShown());
  WXT_CHECK(!draft.RestsOnMap());
}

WXT_TEST(DraftTheProgramsPlacesComeOnTheScreen)
{
  const Values values = Shown(
    true,
    {At(4, 100, 200, 10, kFree), At(9, 300, 200, 10, Draft::kNewest | kFree)},
    {At(0, 320, 200, 10, Draft::kTaken)},
    At(0, 600, 100, 6, 0)
  );
  Draft draft;
  WXT_CHECK(draft.Update(values.data(), Count(values), kHalf));
  WXT_CHECK(draft.Open());
  WXT_CHECK(draft.PicksDeposit());
  WXT_CHECK_EQ(draft.Plans(), 2);
  WXT_CHECK_EQ(draft.LineCount(), 1);
  WXT_CHECK(draft.AimShown());
  WXT_CHECK(draft.RestsOnMap());
  WXT_CHECK_EQ(draft.Id(0), 4);
  CHECK_NEAR(draft.X(0), 60, 0);
  CHECK_NEAR(draft.Y(0), 120, 0);
  CHECK_NEAR(draft.Left(0), 55, 0);
  CHECK_NEAR(draft.Top(0), 115, 0);
  CHECK_NEAR(draft.Right(0), 65, 0);
  CHECK_NEAR(draft.Bottom(0), 125, 0);
  WXT_CHECK(draft.Has(1, Draft::kNewest));
  WXT_CHECK(!draft.Has(0, Draft::kNewest));
  WXT_CHECK_EQ(draft.LineItem(0), 2);
  WXT_CHECK(draft.Has(draft.LineItem(0), Draft::kTaken));
  WXT_CHECK_EQ(draft.AimItem(), 3);
  CHECK_NEAR(draft.X(draft.AimItem()), 310, 0);
}

WXT_TEST(DraftAnEmptyDraftPicksNoDepositAndNoFingerRests)
{
  Draft draft;
  const Values values = Shown(false, {}, {}, std::nullopt);
  draft.Update(values.data(), Count(values), kHalf);
  WXT_CHECK(draft.Open());
  WXT_CHECK_EQ(draft.Plans(), 0);
  WXT_CHECK(!draft.PicksDeposit());
  // No finger rests.
  WXT_CHECK(!draft.RestsOnMap());
}

WXT_TEST(DraftOnlyWholePlacesAreRead)
{
  // The program says three plans, but the numbers hold two.
  Values values = Shown(false, {At(1, 100, 100, 10, kFree), At(2, 200, 100, 10, kFree)}, {}, std::nullopt);
  values[2] = 3;
  Draft draft;
  draft.Update(values.data(), 6 + 2 * 8 + 5, kSame);
  WXT_CHECK_EQ(draft.Plans(), 2);
  WXT_CHECK_EQ(draft.LineCount(), 0);
}

WXT_TEST(DraftAnUpdateSaysWhetherAnythingChanged)
{
  Values values = Shown(false, {At(1, 100, 100, 10, kFree)}, {}, std::nullopt);
  Draft draft;
  WXT_CHECK(draft.Update(values.data(), Count(values), kSame));
  const Values copy = values;
  WXT_CHECK(!draft.Update(copy.data(), Count(values), kSame));
  // The first plan's x: after the head's 6 numbers, its id.
  values[7] = 101;
  WXT_CHECK(draft.Update(values.data(), Count(values), kSame));
  CHECK_NEAR(draft.X(0), 101, 0);
}

WXT_TEST(DraftAFingerIsOnAPlanWithinItsBoxAlone)
{
  const Draft draft = Made(Shown(false, {At(5, 100, 100, 10, kFree)}, {}, std::nullopt));
  WXT_CHECK_EQ(draft.PlanAt(100, 100, 0), 5);
  WXT_CHECK_EQ(draft.PlanAt(110, 100, 0), 5);
  WXT_CHECK_EQ(draft.PlanAt(90, 90, 0), 5);
  // Just past its ground, where a building goes against it, the finger is on no plan.
  WXT_CHECK_EQ(draft.PlanAt(111, 100, 0), 0);
  WXT_CHECK_EQ(draft.PlanAt(100, 89, 0), 0);
}

WXT_TEST(DraftTheNewestPlanIsFoundFirst)
{
  const Draft draft =
    Made(Shown(false, {At(1, 100, 100, 10, kFree), At(2, 110, 100, 10, Draft::kNewest | kFree)}, {}, std::nullopt));
  WXT_CHECK_EQ(draft.PlanAt(104, 100, 0), 2);
  WXT_CHECK_EQ(draft.PlanAt(95, 100, 0), 1);
}

WXT_TEST(DraftASmallPlanIsAtLeastItsLeastAboutItsMiddle)
{
  // Far away its ground is a pixel on the screen: a finger finds it within the least half side.
  const Draft draft = Made(Shown(false, {At(3, 100, 100, 0.5f, kFree)}, {}, std::nullopt));
  WXT_CHECK_EQ(draft.PlanAt(112, 100, 12), 3);
  WXT_CHECK_EQ(draft.PlanAt(113, 100, 12), 0);
  WXT_CHECK_EQ(draft.PlanAt(101, 100, 0), 0);
}

WXT_TEST(DraftAPlanUnderAPanelIsNotUnderTheFinger)
{
  // A finger there is on the panel, which takes it.
  const Draft draft =
    Made(Shown(false, {At(1, 100, 100, 10, kFree), At(2, 100, 100, 10, Draft::kNewest)}, {}, std::nullopt));
  WXT_CHECK_EQ(draft.PlanAt(100, 100, 0), 1);
}

WXT_TEST(DraftAPlansNumberTellsWhetherItIsTheNewestOrOnADeposit)
{
  const Draft draft = Made(Shown(
    true, {At(1, 100, 100, 10, Draft::kOnDeposit | kFree), At(2, 200, 100, 10, Draft::kNewest | kFree)}, {}, std::nullopt
  ));
  WXT_CHECK(draft.OnDeposit(1));
  WXT_CHECK(!draft.OnDeposit(2));
  WXT_CHECK(draft.Newest(2));
  WXT_CHECK(!draft.Newest(1));
  // A number no plan has.
  WXT_CHECK(!draft.Newest(7));
}

WXT_TEST(DraftTheLoupesCopyIsTheSquareAboutTheFingerKeptOnThePicture)
{
  // The phone's 768-line picture: 6 mm are 68 pixels. About the finger in the middle of it.
  WXT_CHECK_EQ(Draft::LoupeFrom(400, 68, 768), 366);
  // A finger 10 pixels from the top: the square at the top, its middle 24 pixels below the finger, where the loupe shows it.
  const int top = Draft::LoupeFrom(10, 68, 768);
  WXT_CHECK_EQ(top, 0);
  WXT_CHECK_EQ(top + 68 / 2 - 10, 24);
  // At the bottom and at the right of a 1647 by 768 picture.
  WXT_CHECK_EQ(Draft::LoupeFrom(760, 68, 768), 700);
  WXT_CHECK_EQ(Draft::LoupeFrom(1640, 68, 1647), 1579);
  // A picture no larger than the square.
  WXT_CHECK_EQ(Draft::LoupeFrom(30, 68, 68), 0);
}
