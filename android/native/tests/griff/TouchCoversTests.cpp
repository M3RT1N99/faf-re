// Griff's covers and what the player opened of FAF's (src/touch/Covers.h, Opened.h): the layer's pieces that show, which the
// program hears when they changed, and what lies under a piece. The cases of the app's CoversTest.java and OpenedTest.java.

#include <array>
#include <vector>

#include "touch/Covers.h"
#include "touch/Opened.h"

#include "Check.h"

namespace
{
  using touch::Covers;
  using touch::Opened;

  struct CoversFixture
  {
    Covers covers;

    [[nodiscard]] std::vector<int> Given() const
    {
      return {covers.Boxes().begin(), covers.Boxes().begin() + 4 * covers.Count()};
    }
  };

  struct OpenedFixture
  {
    Opened opened;

    bool Give(const std::vector<float>& boxes)
    {
      std::array<float, Opened::kValues> values{};
      std::copy(boxes.begin(), boxes.end(), values.begin());
      return opened.Update(values.data(), static_cast<int>(boxes.size()));
    }
  };

  std::string Describe(const std::vector<int>& values)
  {
    std::string out;
    for (const int value : values) {
      out += (out.empty() ? "" : " ") + std::to_string(value);
    }
    return out;
  }
} // namespace

#define COVERS_TEST(name) WXT_FIXTURE_TEST(CoversFixture, name)
#define OPENED_TEST(name) WXT_FIXTURE_TEST(OpenedFixture, name)
/// The covers given, as their numbers.
#define EXPECT_GIVEN(...) WXT_CHECK_EQ(Describe(Given()), Describe(std::vector<int>{__VA_ARGS__}))

COVERS_TEST(CoversAPieceCoversWhileItShowsAndHasASize)
{
  covers.Begin();
  covers.Piece(true, 0, 10, 20, 40);
  covers.Piece(false, 50, 50, 60, 60);
  covers.Piece(true, 70, 70, 70, 90);
  covers.Piece(true, 80, 80, 90, 80);
  WXT_CHECK(covers.End());
  EXPECT_GIVEN(0, 10, 20, 40);
}

COVERS_TEST(CoversTheProgramHearsThemOnlyWhenTheyChanged)
{
  covers.Begin();
  covers.Piece(true, 0, 10, 20, 40);
  covers.Piece(false, 30, 300, 330, 360);
  // The first.
  WXT_CHECK(covers.End());
  covers.Begin();
  covers.Piece(true, 0, 10, 20, 40);
  covers.Piece(false, 30, 300, 330, 360);
  // The same.
  WXT_CHECK(!covers.End());
  // The pill shows, and goes again.
  covers.Begin();
  covers.Piece(true, 0, 10, 20, 40);
  covers.Piece(true, 30, 300, 330, 360);
  WXT_CHECK(covers.End());
  EXPECT_GIVEN(0, 10, 20, 40, 30, 300, 330, 360);
  covers.Begin();
  covers.Piece(true, 0, 10, 20, 40);
  covers.Piece(false, 30, 300, 330, 360);
  WXT_CHECK(covers.End());
  WXT_CHECK_EQ(covers.Count(), 1);
  // None at all is a change too: the program's go.
  covers.Begin();
  WXT_CHECK(covers.End());
  WXT_CHECK_EQ(covers.Count(), 0);
}

COVERS_TEST(CoversAPieceThatMovedIsAChange)
{
  covers.Begin();
  covers.Piece(true, 0, 10, 20, 40);
  covers.End();
  covers.Begin();
  covers.Piece(true, 0, 11, 20, 41);
  WXT_CHECK(covers.End());
}

COVERS_TEST(CoversTheUndosPillCoversItsPlaceAShortWhileAfterItWent)
{
  // The pill shows at two places, its time runs out at 3000, and a tap meant for it is on its way: the place where it last
  // showed stays covered until 500 ms after its last frame, then the program hears it go.
  Covers::Lingering pill(500);
  covers.Begin();
  covers.Piece(pill, true, 30, 300, 330, 360, 1000);
  WXT_CHECK(covers.End());
  covers.Begin();
  covers.Piece(pill, true, 30, 310, 330, 370, 2990);
  // Moved.
  WXT_CHECK(covers.End());
  covers.Begin();
  covers.Piece(pill, false, 0, 0, 0, 0, 3000);
  // Gone, and covering still.
  WXT_CHECK(!covers.End());
  EXPECT_GIVEN(30, 310, 330, 370);
  covers.Begin();
  covers.Piece(pill, false, 0, 0, 0, 0, 3489);
  WXT_CHECK(!covers.End());
  WXT_CHECK_EQ(covers.Count(), 1);
  covers.Begin();
  covers.Piece(pill, false, 0, 0, 0, 0, 3490);
  WXT_CHECK(covers.End());
  WXT_CHECK_EQ(covers.Count(), 0);
  // It shows again for a new order: it covers again, where it shows.
  covers.Begin();
  covers.Piece(pill, true, 30, 300, 330, 360, 5000);
  WXT_CHECK(covers.End());
  EXPECT_GIVEN(30, 300, 330, 360);
}

COVERS_TEST(CoversThePillsCoverGoesAtOnceWhereThePlayerOpenedSomethingOfFafs)
{
  // The pill went at 3000 and would cover its place until 3500; FAF's Menu opens over it at 3100: a tap there is the Menu's from
  // then on. Shown again, it covers again.
  Covers::Lingering pill(500);
  covers.Begin();
  covers.Piece(pill, true, 30, 300, 330, 360, 3000);
  WXT_CHECK(covers.End());
  covers.Begin();
  covers.Piece(pill, false, 0, 0, 0, 0, 3050);
  // Still covering.
  WXT_CHECK(!covers.End());
  pill.Forget();
  covers.Begin();
  covers.Piece(pill, false, 0, 0, 0, 0, 3100);
  // Gone at once.
  WXT_CHECK(covers.End());
  WXT_CHECK_EQ(covers.Count(), 0);
  WXT_CHECK(pill.CoversNow(true, 30, 300, 330, 360, 5000));
  WXT_CHECK(pill.CoversNow(false, 0, 0, 0, 0, 5100));
}

COVERS_TEST(CoversAPieceThatNeverShowedNeverCoversNorDoesOneWithoutASize)
{
  Covers::Lingering never(500);
  WXT_CHECK(!never.CoversNow(false, 30, 300, 330, 360, 0));
  // Without a size.
  WXT_CHECK(!never.CoversNow(true, 30, 300, 30, 360, 0));
  Covers::Lingering pill(500);
  Covers::Lingering sizeless(500);
  covers.Begin();
  covers.Piece(pill, false, 30, 300, 330, 360, 0);
  covers.Piece(sizeless, true, 30, 300, 30, 360, 0);
  covers.End();
  WXT_CHECK_EQ(covers.Count(), 0);
}

COVERS_TEST(CoversThereAreAtMostSoMany)
{
  covers.Begin();
  for (int piece = 0; piece <= Covers::kMost; ++piece) {
    covers.Piece(true, piece, 0, piece + 1, 1);
  }
  covers.End();
  WXT_CHECK_EQ(covers.Count(), Covers::kMost);
}

OPENED_TEST(OpenedWhatLiesUnderAPieceIsWhatSharesMoreThanAnEdgeWithIt)
{
  // The overlay filters' list as FAF 3835 opens it right of its multifunction panel at 1.6, and the rail's Attack and Box.
  Give({356.6f, 160.8f, 600, 690});
  // Attack, Box, and a pixel's corner.
  WXT_CHECK(opened.Under(393, 231, 512, 351));
  WXT_CHECK(!opened.Under(117, 231, 237, 351));
  WXT_CHECK(opened.Under(355, 159, 357, 161));
}

OPENED_TEST(OpenedAPieceJustBesideItCoversNothingOfIt)
{
  Give({100, 100, 200, 200});
  // Left, right, above and below it; a pixel into it.
  WXT_CHECK(!opened.Under(50, 120, 100, 180));
  WXT_CHECK(!opened.Under(200, 120, 250, 180));
  WXT_CHECK(!opened.Under(120, 50, 180, 100));
  WXT_CHECK(!opened.Under(120, 200, 180, 250));
  WXT_CHECK(opened.Under(50, 120, 101, 180));
}

OPENED_TEST(OpenedAPieceWithoutASizeCoversNothing)
{
  // As the pill's place before it ever showed (Covers::Lingering), here inside what a question box's shadow covers.
  Give({0, 0, 1647, 768});
  WXT_CHECK(!opened.Under(300, 300, 300, 360));
  WXT_CHECK(!opened.Under(300, 300, 360, 300));
  WXT_CHECK(opened.Under(300, 300, 301, 301));
}

OPENED_TEST(OpenedEachOfThemCounts)
{
  Give({0, 0, 10, 10, 699, 115, 948, 700});
  // The Menu's list, the second.
  WXT_CHECK(opened.Under(530, 231, 740, 351));
  WXT_CHECK(!opened.Under(300, 300, 400, 400));
}

OPENED_TEST(OpenedOnlyWholeBoxesCount)
{
  // A box given in part is none, also where a whole one stood before; and no more are read than there is room for.
  WXT_CHECK(Give({0, 0, 10, 10, 50, 50, 300, 300}));
  WXT_CHECK(Give({0, 0, 10, 10, 100, 100}));
  // The part of a box.
  WXT_CHECK(!opened.Under(150, 150, 200, 200));
  WXT_CHECK(opened.Under(5, 5, 20, 20));
  std::vector<float> many(Opened::kValues + 4);
  for (std::size_t at = 0; at < many.size(); at += 4) {
    many[at] = static_cast<float>(at);
    many[at + 1] = 0;
    many[at + 2] = static_cast<float>(at + 1);
    many[at + 3] = 1;
  }
  WXT_CHECK(opened.Update(many.data(), static_cast<int>(many.size())));
  // The last that fits, and one past them.
  WXT_CHECK(opened.Under(Opened::kValues - 4, 0, Opened::kValues - 3, 1));
  WXT_CHECK(!opened.Under(Opened::kValues, 0, Opened::kValues + 1, 1));
  // None.
  WXT_CHECK(opened.Update(many.data(), -4));
  WXT_CHECK(!opened.Under(0, 0, 1, 1));
}

OPENED_TEST(OpenedItChangesWhereABoxComesGoesOrMoves)
{
  // None, as before; one opens; the same; it grows; another; it is gone; all gone.
  WXT_CHECK(!Give({}));
  WXT_CHECK(Give({10, 10, 20, 20}));
  WXT_CHECK(!Give({10, 10, 20, 20}));
  WXT_CHECK(Give({10, 10, 20, 21}));
  WXT_CHECK(Give({10, 10, 20, 21, 30, 30, 40, 40}));
  WXT_CHECK(Give({30, 30, 40, 40}));
  WXT_CHECK(!opened.Under(10, 10, 20, 21));
  WXT_CHECK(Give({}));
  WXT_CHECK(!opened.Under(30, 30, 40, 40));
}

WXT_TEST(OpenedAPieceShowsWhereItIsWantedAndStepsAsideInItsPlace)
{
  WXT_CHECK(Opened::Visibility(true, false) == Opened::Shows::kSeen);
  // Unseen, in its place.
  WXT_CHECK(Opened::Visibility(true, true) == Opened::Shows::kAside);
  WXT_CHECK(Opened::Visibility(false, false) == Opened::Shows::kNot);
  WXT_CHECK(Opened::Visibility(false, true) == Opened::Shows::kNot);
}
