// A stroke's waypoints, what lifting it gives and what the screen says of it (touch scheme Griff, src/touch/Stroke.h): the
// cases of the app's StrokeTest.java.

#include <cmath>
#include <vector>

#include "touch/Stroke.h"

#include "Check.h"

#define CHECK_NEAR(actual, expected, delta)                                                                              \
  WXT_CHECK(std::abs(static_cast<double>(actual) - static_cast<double>(expected)) <= (delta))

namespace
{
  namespace stroke = touch::stroke;

  struct StrokeFixture
  {
    float outX[8] = {};
    float outY[8] = {};

    int Waypoints(const std::vector<float>& points, const float step)
    {
      std::vector<float> xs;
      std::vector<float> ys;
      for (std::size_t at = 0; at + 1 < points.size(); at += 2) {
        xs.push_back(points[at]);
        ys.push_back(points[at + 1]);
      }
      return stroke::Waypoints(xs.data(), ys.data(), static_cast<int>(xs.size()), step, 5, outX, outY, 8);
    }
  };

  /// A stroke of these points, a mm 10 pixels, classified.
  int Classify(const std::vector<float>& points)
  {
    std::vector<float> xs;
    std::vector<float> ys;
    for (std::size_t at = 0; at + 1 < points.size(); at += 2) {
      xs.push_back(points[at]);
      ys.push_back(points[at + 1]);
    }
    return stroke::Classify(xs.data(), ys.data(), static_cast<int>(xs.size()), 10);
  }
} // namespace

#define STROKE_TEST(name) WXT_FIXTURE_TEST(StrokeFixture, name)

STROKE_TEST(StrokeAboutOneEveryStepEndingOnTheEndWithoutTheStart)
{
  WXT_CHECK_EQ(Waypoints({0, 0, 50, 0, 100, 0}, 30), 3);
  CHECK_NEAR(outX[0], 33.333f, 0.01f);
  CHECK_NEAR(outX[1], 66.667f, 0.01f);
  CHECK_NEAR(outX[2], 100, 0.01f);
}

STROKE_TEST(StrokeAShortStrokeIsOnlyItsEnd)
{
  WXT_CHECK_EQ(Waypoints({0, 0, 10, 0}, 30), 1);
  CHECK_NEAR(outX[0], 10, 0);
}

STROKE_TEST(StrokeACornerStaysAWaypointHoweverLongTheStep)
{
  WXT_CHECK_EQ(Waypoints({0, 0, 40, 0, 40, 40}, 300), 2);
  CHECK_NEAR(outX[0], 40, 0);
  CHECK_NEAR(outY[0], 0, 0);
  CHECK_NEAR(outY[1], 40, 0);
}

STROKE_TEST(StrokeAWobbleWithinTheToleranceIsNoCorner)
{
  WXT_CHECK_EQ(Waypoints({0, 0, 50, 3, 100, 0}, 300), 1);
  CHECK_NEAR(outX[0], 100, 0);
}

STROKE_TEST(StrokeTooLittleRoomStillEndsOnTheEnd)
{
  WXT_CHECK_EQ(Waypoints({0, 0, 1000, 0}, 10), 8);
  CHECK_NEAR(outX[7], 1000, 0);
}

STROKE_TEST(StrokeNoStrokeNoWaypoints)
{
  WXT_CHECK_EQ(Waypoints({5, 5}, 30), 0);
}

WXT_TEST(StrokeAStrokeThatEndsEightMillimetresAwayOrMoreIsARoute)
{
  WXT_CHECK_EQ(Classify({0, 0, 80, 0}), stroke::kRoute);
  WXT_CHECK_EQ(Classify({0, 0, 300, 0, 300, 300, 0, 80}), stroke::kRoute);
}

WXT_TEST(StrokeALoopLongerThanFortyMillimetresIsAPatrol)
{
  // 40.2 mm out and back, ending 7.9 mm from its start; 39.2 mm is a stroke taken back.
  WXT_CHECK_EQ(Classify({0, 0, 240, 0, 240, 1, 79, 0}), stroke::kPatrol);
  WXT_CHECK_EQ(Classify({0, 0, 235, 0, 235, 1, 79, 0}), stroke::kCancel);
}

WXT_TEST(StrokeAStrokeOfOnePointGivesNothing)
{
  WXT_CHECK_EQ(Classify({5, 5}), stroke::kCancel);
}

WXT_TEST(StrokeTheLabelSaysWhatTheEndIsOnInTheMockupsOrder)
{
  const int engineers = stroke::kEndEngineers;
  const int attackMove = stroke::kEndAttackMove;
  // A panel or the minimap first, whatever the stroke is.
  WXT_CHECK_EQ(stroke::Meaning(stroke::kEndPanel, stroke::kPatrol), stroke::kSaysCancel);
  WXT_CHECK_EQ(stroke::Meaning(stroke::kEndMinimap | attackMove, stroke::kCancel), stroke::kSaysMinimap);
  // Then a loop: taken back, or a patrol where FAF offers one; not yet known, a patrol too.
  WXT_CHECK_EQ(stroke::Meaning(stroke::kEndAttack, stroke::kCancel), stroke::kSaysCancel);
  WXT_CHECK_EQ(stroke::Meaning(stroke::kEndMove | stroke::kEndPatrolOffered, stroke::kPatrol), stroke::kSaysPatrol);
  WXT_CHECK_EQ(stroke::Meaning(0, stroke::kPatrol), stroke::kSaysPatrol);
  // For factories alone a loop is their rally route.
  WXT_CHECK_EQ(stroke::Meaning(stroke::kEndMove, stroke::kPatrol), stroke::kSaysRoute);
  // Then FAF's order there: an attack before the attack-move FAF's button armed, and that before an assist.
  WXT_CHECK_EQ(stroke::Meaning(stroke::kEndAttack | attackMove, stroke::kRoute), stroke::kSaysAttack);
  WXT_CHECK_EQ(stroke::Meaning(stroke::kEndGuard | engineers | attackMove, stroke::kRoute), stroke::kSaysAttackMove);
  WXT_CHECK_EQ(stroke::Meaning(stroke::kEndMove | attackMove, stroke::kRoute), stroke::kSaysAttackMove);
  WXT_CHECK_EQ(stroke::Meaning(stroke::kEndGuard | engineers, stroke::kRoute), stroke::kSaysAssist);
  WXT_CHECK_EQ(stroke::Meaning(stroke::kEndRepair | engineers, stroke::kRoute), stroke::kSaysAssist);
  WXT_CHECK_EQ(stroke::Meaning(stroke::kEndGuard, stroke::kRoute), stroke::kSaysGuard);
  WXT_CHECK_EQ(stroke::Meaning(stroke::kEndReclaim | engineers, stroke::kRoute), stroke::kSaysReclaim);
  WXT_CHECK_EQ(stroke::Meaning(stroke::kEndCapture | engineers, stroke::kRoute), stroke::kSaysCapture);
  WXT_CHECK_EQ(stroke::Meaning(stroke::kEndOther, stroke::kRoute), stroke::kSaysOrder);
  WXT_CHECK_EQ(stroke::Meaning(stroke::kEndMove | engineers, stroke::kRoute), stroke::kSaysRoute);
  WXT_CHECK_EQ(stroke::Meaning(0, stroke::kRoute), stroke::kSaysRoute);
}

WXT_TEST(StrokeTheHandlesRestSaysFafsDistributeOrdersWhateverItsEndIsOn)
{
  // The finger is on the handle, over the map beside the group: what FAF's right click there would order counts for nothing.
  WXT_CHECK_EQ(stroke::Meaning(0, stroke::kDistribute), stroke::kSaysDistribute);
  WXT_CHECK_EQ(stroke::Meaning(stroke::kEndMove | stroke::kEndAttackMove, stroke::kDistribute), stroke::kSaysDistribute);
  WXT_CHECK_EQ(stroke::Meaning(stroke::kEndPanel, stroke::kDistribute), stroke::kSaysDistribute);
  // No stroke's shape gives it.
  WXT_CHECK_EQ(Classify({0, 0, 5, 0}), stroke::kCancel);
}

WXT_TEST(StrokeTheLengthIsAlongThePoints)
{
  const float xs[] = {0, 30, 30};
  const float ys[] = {0, 0, 40};
  CHECK_NEAR(stroke::Length(xs, ys, 3), 70, 1e-9);
  CHECK_NEAR(stroke::Length(xs, ys, 2), 30, 1e-9);
}
