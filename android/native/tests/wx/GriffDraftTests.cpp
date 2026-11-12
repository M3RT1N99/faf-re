// The touch scheme Griff's draft (include/GriffDraft.h): where a plan lands, the ground plans take, the line from the newest
// plan, the move of the whole draft and its limit, and the pixel a building's click goes to. Buildings as FAF's UEF ones: the
// power generator (a cell, its skirt 2 by 2 about it), the mass extractor (the same, on a mass deposit), the land factory (5
// cells, its skirt 8 by 8), the wall (a cell, its skirt the cell).

#include <cmath>
#include <cstddef>
#include <vector>

#include <GriffDraft.h>

#include "Check.h"

namespace
{
  using griffdraft::Building;
  using griffdraft::Candidate;
  using griffdraft::Deposit;
  using griffdraft::Draft;
  using griffdraft::Place;
  using griffdraft::Placed;
  using griffdraft::Plan;

  Building Generator()
  {
    return Building{"ueb1101", 1, 1, -0.5f, -0.5f, 2, 2, Deposit::kNone};
  }

  Building Extractor()
  {
    return Building{"ueb1103", 1, 1, -0.5f, -0.5f, 2, 2, Deposit::kMass};
  }

  Building Factory()
  {
    return Building{"ueb0101", 5, 5, -1.5f, -1.5f, 8, 8, Deposit::kNone};
  }

  Building Wall()
  {
    return Building{"ueb5101", 1, 1, 0, 0, 1, 1, Deposit::kNone};
  }

  /// A camera that shows the world evenly: `unitsPerPixel` world units a pixel across and down, a place at `shownX`, `shownY`
  /// on the picture, and down the picture `skew` world units of x a pixel of y as well (a camera turned and pitched).
  struct Camera
  {
    float unitsPerPixel = 1;
    Place place;
    float shownX = 0;
    float shownY = 0;
    float skew = 0;

    [[nodiscard]] Place Under(const int pixelX, const int pixelY) const
    {
      const float across = static_cast<float>(pixelX) - shownX;
      const float down = static_cast<float>(pixelY) - shownY;
      return Place{place.x + unitsPerPixel * across, place.z + unitsPerPixel * down + skew * across};
    }
  };

  /// The pixel ClickPixel picks for a building at `camera.place`, by where FAF's click at each pixel builds it under this
  /// camera; false where none builds it there.
  bool ClickFor(const Building& building, const std::vector<Candidate>& deposits, const Camera& camera, int& clickX, int& clickY)
  {
    const auto lands = [&](const int pixelX, const int pixelY) {
      const Place under = camera.Under(pixelX, pixelY);
      return griffdraft::LandsAt(building, deposits, camera.place, under.x, under.z);
    };
    return griffdraft::ClickPixel(camera.shownX, camera.shownY, lands, clickX, clickY);
  }

  /// FAF's own check, as the program asks it: everything free, or nothing.
  bool Anywhere(const Building&, float, float)
  {
    return true;
  }

  bool Nowhere(const Building&, float, float)
  {
    return false;
  }

  void CheckPlaces(const std::vector<Place>& places, const std::vector<Place>& expected)
  {
    WXT_CHECK_EQ(places.size(), expected.size());
    for (std::size_t at = 0; at < places.size() && at < expected.size(); ++at) {
      WXT_CHECK_EQ(places[at].x, expected[at].x);
      WXT_CHECK_EQ(places[at].z, expected[at].z);
    }
  }

  /// A draft of generators with plans at these places.
  Draft Generators(const std::vector<Place>& places)
  {
    Draft draft;
    draft.Pick(Generator());
    for (const Place& at : places) {
      WXT_CHECK(draft.PlaceAt(at.x, at.z, Anywhere) == Placed::kPlaced);
    }
    return draft;
  }
} // namespace

WXT_TEST(GriffDraftSnapsAsFafPlacesAFootprint)
{
  // A footprint of an odd size stands in the middle of a cell, an even one on a cell's corner.
  WXT_CHECK_EQ(griffdraft::Snap(10.3f, 1), 10.5f);
  WXT_CHECK_EQ(griffdraft::Snap(10.9f, 1), 10.5f);
  WXT_CHECK_EQ(griffdraft::Snap(10.3f, 2), 10.0f);
  WXT_CHECK_EQ(griffdraft::Snap(10.7f, 2), 11.0f);
  WXT_CHECK_EQ(griffdraft::Snap(13.2f, 5), 13.5f);
  // FAF rounds the footprint's first cell to the nearest, ties to the even one (GetFootprintRect).
  WXT_CHECK_EQ(griffdraft::Snap(11.5f, 2), 11.0f);
  WXT_CHECK_EQ(griffdraft::Snap(12.5f, 2), 13.0f);
}

WXT_TEST(GriffDraftAPlanTakesItsSkirt)
{
  const griffdraft::Rect generator = griffdraft::SkirtRect(Generator(), 10.5f, 20.5f);
  WXT_CHECK_EQ(generator.x0, 9.5f);
  WXT_CHECK_EQ(generator.x1, 11.5f);
  WXT_CHECK_EQ(generator.z0, 19.5f);
  WXT_CHECK_EQ(generator.z1, 21.5f);
  const griffdraft::Rect factory = griffdraft::SkirtRect(Factory(), 102.5f, 52.5f);
  WXT_CHECK_EQ(factory.x0, 98.5f);
  WXT_CHECK_EQ(factory.x1, 106.5f);
  // Without a skirt of its own a building takes its footprint.
  const griffdraft::Rect bare = griffdraft::SkirtRect(Building{"bare", 3, 1, 0, 0, 0, 0, Deposit::kNone}, 10.5f, 10.5f);
  WXT_CHECK_EQ(bare.x0, 9.0f);
  WXT_CHECK_EQ(bare.x1, 12.0f);
  WXT_CHECK_EQ(bare.z0, 10.0f);
  WXT_CHECK_EQ(bare.z1, 11.0f);
}

WXT_TEST(GriffDraftPlansWhoseSkirtsTouchTakeNoGroundOfEachOther)
{
  const Draft draft = Generators({{10.5f, 10.5f}});
  WXT_CHECK(!draft.OnPlan(Generator(), 12.5f, 10.5f));
  WXT_CHECK(!draft.OnPlan(Generator(), 12.5f, 12.5f));
  WXT_CHECK(draft.OnPlan(Generator(), 11.5f, 10.5f));
  WXT_CHECK(draft.OnPlan(Generator(), 10.5f, 11.5f));
  // A plan's own ground is not taken from it.
  WXT_CHECK(!draft.OnPlan(Generator(), 10.5f, 10.5f, draft.Plans()[0].id));
}

WXT_TEST(GriffDraftATapPlacesAPlanOnFafsGrid)
{
  Draft draft;
  WXT_CHECK(!draft.Open());
  draft.Pick(Generator());
  WXT_CHECK(draft.Open());
  WXT_CHECK(draft.PlaceAt(10.2f, 20.9f, Anywhere) == Placed::kPlaced);
  WXT_CHECK_EQ(draft.Plans().size(), std::size_t{1});
  WXT_CHECK_EQ(draft.Plans()[0].x, 10.5f);
  WXT_CHECK_EQ(draft.Plans()[0].z, 20.5f);
}

WXT_TEST(GriffDraftRefusesGroundThatIsTaken)
{
  Draft draft = Generators({{10.5f, 10.5f}});
  // Another plan's ground.
  WXT_CHECK(draft.PlaceAt(11.2f, 10.6f, Anywhere) == Placed::kTaken);
  // FAF's own check, asked at the snapped place.
  float askedX = 0;
  float askedZ = 0;
  const auto refused = [&](const Building&, const float x, const float z) {
    askedX = x;
    askedZ = z;
    return false;
  };
  WXT_CHECK(draft.PlaceAt(30.2f, 30.9f, refused) == Placed::kTaken);
  WXT_CHECK_EQ(askedX, 30.5f);
  WXT_CHECK_EQ(askedZ, 30.5f);
  WXT_CHECK_EQ(draft.Plans().size(), std::size_t{1});
}

WXT_TEST(GriffDraftHoldsThirtyPlans)
{
  Draft draft;
  draft.Pick(Generator());
  for (int at = 0; at < 30; ++at) {
    WXT_CHECK(draft.PlaceAt(10.5f + 2 * at, 10.5f, Anywhere) == Placed::kPlaced);
  }
  WXT_CHECK(draft.Full());
  WXT_CHECK(draft.PlaceAt(10.5f, 30.5f, Anywhere) == Placed::kFull);
  // Taken ground says so before the draft's limit.
  WXT_CHECK(draft.PlaceAt(10.5f, 10.5f, Anywhere) == Placed::kTaken);
  WXT_CHECK_EQ(draft.Plans().size(), std::size_t{30});
}

WXT_TEST(GriffDraftAnExtractorGoesOnTheNearestFreeDepositWithinReach)
{
  Draft draft;
  draft.Pick(Extractor());
  // Deposits on the screen at 100, 160 and 400 pixels across; the finger at 120, reach 50.
  const std::vector<Candidate> deposits{{100, 300, {50.5f, 50.5f}}, {160, 300, {80.5f, 50.5f}}, {400, 300, {200.5f, 50.5f}}};
  WXT_CHECK_EQ(draft.NearestFree(deposits, 120, 300, 50, Anywhere), 0);
  WXT_CHECK(draft.PlaceOnDeposit(deposits, 120, 300, 50, Anywhere) == Placed::kPlaced);
  WXT_CHECK_EQ(draft.Plans().back().x, 50.5f);
  // That deposit has a plan now: the next nearest within reach.
  WXT_CHECK_EQ(draft.NearestFree(deposits, 120, 300, 50, Anywhere), 1);
  // One FAF refuses (an extractor on it) is not free either.
  const auto onlyFar = [](const Building&, const float x, float) { return x > 100; };
  WXT_CHECK_EQ(draft.NearestFree(deposits, 120, 300, 50, onlyFar), -1);
  WXT_CHECK(draft.PlaceOnDeposit(deposits, 120, 300, 50, onlyFar) == Placed::kNoFreeDeposit);
  // Beyond reach.
  WXT_CHECK(draft.PlaceOnDeposit(deposits, 300, 300, 50, Anywhere) == Placed::kNoFreeDeposit);
}

WXT_TEST(GriffDraftABoxGivesEveryFreeDepositInItAPlan)
{
  Draft draft;
  draft.Pick(Extractor());
  WXT_CHECK(draft.PlaceOnDeposit({{100, 300, {50.5f, 50.5f}}}, 100, 300, 50, Anywhere) == Placed::kPlaced);
  const std::vector<Candidate> inBox{{100, 300, {50.5f, 50.5f}}, {160, 300, {80.5f, 50.5f}}, {220, 300, {110.5f, 50.5f}}};
  bool full = true;
  const auto notTheLast = [](const Building&, const float x, float) { return x < 100; };
  WXT_CHECK_EQ(draft.PlaceOnDeposits(inBox, notTheLast, full), std::size_t{1});
  WXT_CHECK(!full);
  WXT_CHECK_EQ(draft.Plans().size(), std::size_t{2});
  WXT_CHECK_EQ(draft.Plans().back().x, 80.5f);
}

WXT_TEST(GriffDraftABoxStopsWhenTheDraftIsFull)
{
  Draft draft;
  draft.Pick(Generator());
  for (int at = 0; at < 29; ++at) {
    WXT_CHECK(draft.PlaceAt(10.5f + 2 * at, 10.5f, Anywhere) == Placed::kPlaced);
  }
  draft.Pick(Extractor());
  bool full = false;
  WXT_CHECK_EQ(draft.PlaceOnDeposits({{0, 0, {300.5f, 300.5f}}, {0, 0, {310.5f, 300.5f}}}, Anywhere, full), std::size_t{1});
  WXT_CHECK(full);
}

WXT_TEST(GriffDraftATapOnAPlanRemovesItAndNumbersAreNotReused)
{
  Draft draft = Generators({{10.5f, 10.5f}, {12.5f, 10.5f}});
  const int first = draft.Plans()[0].id;
  const int second = draft.Plans()[1].id;
  WXT_CHECK(first != second);
  WXT_CHECK(draft.Remove(first));
  WXT_CHECK(!draft.Remove(first));
  WXT_CHECK_EQ(draft.Plans().size(), std::size_t{1});
  WXT_CHECK_EQ(draft.Plans()[0].id, second);
  WXT_CHECK(draft.PlaceAt(10.5f, 10.5f, Anywhere) == Placed::kPlaced);
  WXT_CHECK(draft.Plans()[1].id != first);
  WXT_CHECK(draft.Plans()[1].id != second);
}

WXT_TEST(GriffDraftALineStepsByTheSkirtInEightDirections)
{
  const Place from{10.5f, 10.5f};
  // Across, the other axis well under tan 22.5 degrees of it: 3.45 steps of 2 make 3 generators side by side.
  CheckPlaces(griffdraft::Line(Generator(), from, {17.4f, 10.9f}), {{12.5f, 10.5f}, {14.5f, 10.5f}, {16.5f, 10.5f}});
  CheckPlaces(griffdraft::Line(Generator(), from, {4.5f, 10.5f}), {{8.5f, 10.5f}, {6.5f, 10.5f}, {4.5f, 10.5f}});
  // Diagonal: both axes count.
  CheckPlaces(griffdraft::Line(Generator(), from, {16.5f, 16.3f}), {{12.5f, 12.5f}, {14.5f, 14.5f}, {16.5f, 16.5f}});
  CheckPlaces(griffdraft::Line(Generator(), from, {6.5f, 14.5f}), {{8.5f, 12.5f}, {6.5f, 14.5f}});
  // Steep, the other axis under tan 22.5 degrees: straight down; just over it: diagonal.
  CheckPlaces(griffdraft::Line(Generator(), from, {11.3f, 18.5f}), {{10.5f, 12.5f}, {10.5f, 14.5f}, {10.5f, 16.5f}, {10.5f, 18.5f}});
  CheckPlaces(griffdraft::Line(Generator(), from, {13.9f, 18.5f}), {{12.5f, 12.5f}, {14.5f, 14.5f}, {16.5f, 16.5f}, {18.5f, 18.5f}});
  // Nothing before half a step.
  CheckPlaces(griffdraft::Line(Generator(), from, {11.4f, 10.5f}), {});
  CheckPlaces(griffdraft::Line(Generator(), from, {11.6f, 10.5f}), {{12.5f, 10.5f}});
  // A factory steps by its skirt of 8.
  CheckPlaces(griffdraft::Line(Factory(), {102.5f, 52.5f}, {121, 53}), {{110.5f, 52.5f}, {118.5f, 52.5f}});
}

WXT_TEST(GriffDraftALineOfAnOblongBuildingCountsInItsOwnSteps)
{
  // A skirt 8 across and 4 deep: one step each way is its diagonal.
  const Building oblong{"oblong", 6, 2, -1, -1, 8, 4, Deposit::kNone};
  CheckPlaces(griffdraft::Line(oblong, {100, 100}, {108, 104}), {{108, 104}});
  CheckPlaces(griffdraft::Line(oblong, {100, 100}, {100, 112}), {{100, 104}, {100, 108}, {100, 112}});
}

WXT_TEST(GriffDraftALineGoesFromTheNewestPlanUnlessItIsOnADeposit)
{
  Draft draft = Generators({{10.5f, 10.5f}, {10.5f, 30.5f}});
  WXT_CHECK(draft.LineStart() != nullptr && draft.LineStart()->z == 30.5f);
  // Its places, each with whether it is free: one on an older plan is not.
  const auto line = draft.LineTo(10.5f, 10.5f, Anywhere);
  WXT_CHECK_EQ(line.size(), std::size_t{10});
  WXT_CHECK(line.front().second);
  WXT_CHECK(!line.back().second);
  draft.Pick(Extractor());
  WXT_CHECK(draft.PlaceOnDeposit({{0, 0, {50.5f, 50.5f}}}, 0, 0, 10, Anywhere) == Placed::kPlaced);
  WXT_CHECK(draft.LineStart() == nullptr);
  WXT_CHECK(draft.LineTo(70.5f, 50.5f, Anywhere).empty());
}

// LineTo asks FAF's own check (USERUNIT_CanBeBuiltAt: OCCUPY_Check, a spatial query, a walk over every queued command) once
// per place, on each coalesced move of the finger, while the activity is shown at most kMaxPlans of them and the lift adds at
// most kMaxPlans less the plans. A wall dragged 400 world units from the newest plan, as zoomed out: no more than a draft
// holds.
WXT_TEST(GriffDraftALineAsksNoMorePlacesThanTheDraftCouldHold)
{
  Draft draft;
  draft.Pick(Wall());
  WXT_CHECK(draft.PlaceAt(10.5f, 10.5f, Anywhere) == Placed::kPlaced);
  std::size_t asked = 0;
  const auto counting = [&asked](const Building&, float, float) {
    ++asked;
    return true;
  };
  const auto line = draft.LineTo(410.5f, 10.5f, counting);
  WXT_CHECK(line.size() <= Draft::kMaxPlans);
  WXT_CHECK(asked <= Draft::kMaxPlans);
}

// The activity is shown the line's first kMaxPlans places; the lift must add none beyond them, also where some of the shown
// ones are taken. Generators from 10.5 towards 90.5: 40 places 2 apart, FAF refuses the 11 from 14.5 to 34.5; the preview
// ends at 70.5.
WXT_TEST(GriffDraftALineAddsNoPlaceItsPreviewDidNotShow)
{
  Draft draft;
  draft.Pick(Generator());
  WXT_CHECK(draft.PlaceAt(10.5f, 10.5f, Anywhere) == Placed::kPlaced);
  const auto refused = [](const Building&, const float x, float) { return !(x >= 14 && x <= 35); };
  const auto preview = draft.LineTo(90.5f, 10.5f, refused);
  const std::size_t shown = preview.size() < Draft::kMaxPlans ? preview.size() : Draft::kMaxPlans;
  WXT_CHECK(shown > 0);
  const float lastShown = shown > 0 ? preview[shown - 1].first.x : 0;
  (void)draft.AddLine(90.5f, 10.5f, refused);
  for (const Plan& plan : draft.Plans()) {
    WXT_CHECK(plan.x <= lastShown);
  }
}

// FAF places a building by the world point under the click's whole pixel (IssueBuildDragOrders: its first cell by Snap).
// Zoomed far out on a 20 km map (1024 world units on a 768-line picture) a pixel spans about 1.3 world units: the pixel
// nearest a generator's middle at 100.5 (its cell 100), which the camera shows at 640.45, has its world point at 99.915, in
// cell 99, and the one across at 101.215, in cell 101. No pixel builds it there: none is clicked, and the camera comes
// closer for it first. Where the middle is shown nearer a pixel, that pixel builds it there.
WXT_TEST(GriffDraftATicksClickLandsInItsPlansCellWhenAPixelSpansMoreThanAUnit)
{
  int clickX = -1;
  int clickY = -1;
  WXT_CHECK(!ClickFor(Generator(), {}, Camera{1.3f, {100.5f, 50.5f}, 640.45f, 300.45f}, clickX, clickY));
  WXT_CHECK(ClickFor(Generator(), {}, Camera{1.3f, {100.5f, 50.5f}, 640.2f, 300.3f}, clickX, clickY));
  WXT_CHECK_EQ(clickX, 640);
  WXT_CHECK_EQ(clickY, 300);
  // The camera at half the zoom: the nearest pixel.
  WXT_CHECK(ClickFor(Generator(), {}, Camera{0.65f, {100.5f, 50.5f}, 640.45f, 300.45f}, clickX, clickY));
  WXT_CHECK_EQ(clickX, 640);
  WXT_CHECK_EQ(clickY, 300);
  // A factory's middle is no more forgiving: its cells are as wide.
  WXT_CHECK(!ClickFor(Factory(), {}, Camera{1.3f, {102.5f, 52.5f}, 640.45f, 300.45f}, clickX, clickY));
}

// A pitched or turned camera: down the picture the world's z moves by a pixel's x too. The pixel nearest the middle lands a
// cell off on z; the one across the middle on y lands in the plan's cell, and is clicked.
WXT_TEST(GriffDraftAClickGoesAcrossTheMiddleWhereTheNearestPixelLandsOff)
{
  int clickX = -1;
  int clickY = -1;
  const Camera turned{0.9f, {100.5f, 50.5f}, 640.4f, 300.45f, 0.3f};
  const Place nearest = turned.Under(640, 300);
  WXT_CHECK_EQ(griffdraft::Snap(nearest.z, 1), 49.5f);
  WXT_CHECK(ClickFor(Generator(), {}, turned, clickX, clickY));
  WXT_CHECK_EQ(clickX, 640);
  WXT_CHECK_EQ(clickY, 301);
}

// An extractor's click lands on the deposit FAF's ghost snaps to: from its first cell under the cursor, floored, the deposit
// whose middle cell is nearest, the first of the nearest (FindClosestDeposit). Two mass deposits 2 apart, as adjacent ones,
// the neighbour first in FAF's list; zoomed out to 1.6 world units a pixel. The pixel nearest the plan's deposit is as near
// the neighbour, which wins; the one across the middle snaps to the plan's.
WXT_TEST(GriffDraftAnExtractorsClickLandsOnItsPlansDepositNotItsNeighbours)
{
  const std::vector<Candidate> deposits{{0, 0, {99.5f, 51.5f}}, {0, 0, {101.5f, 51.5f}}};
  WXT_CHECK_EQ(griffdraft::SnappedDeposit(deposits, Extractor(), 101.02f, 51.5f), 0);
  WXT_CHECK_EQ(griffdraft::SnappedDeposit(deposits, Extractor(), 102.62f, 51.5f), 1);
  WXT_CHECK_EQ(griffdraft::SnappedDeposit({}, Extractor(), 102.62f, 51.5f), -1);
  int clickX = -1;
  int clickY = -1;
  WXT_CHECK(ClickFor(Extractor(), deposits, Camera{1.6f, {101.5f, 51.5f}, 640.3f, 300.0f}, clickX, clickY));
  WXT_CHECK_EQ(clickX, 641);
  WXT_CHECK_EQ(clickY, 300);
  // The neighbour's plan, from the same place on the picture: its own deposit.
  WXT_CHECK(ClickFor(Extractor(), deposits, Camera{1.6f, {99.5f, 51.5f}, 640.3f, 300.0f}, clickX, clickY));
  WXT_CHECK_EQ(clickX, 640);
}

WXT_TEST(GriffDraftALineAddsItsFreePlacesInOrderUntilTheDraftIsFull)
{
  Draft draft = Generators({{10.5f, 10.5f}});
  const auto notAtFourteen = [](const Building&, const float x, float) { return x != 14.5f; };
  WXT_CHECK(draft.AddLine(18.5f, 10.5f, notAtFourteen));
  WXT_CHECK_EQ(draft.Plans().size(), std::size_t{4});
  WXT_CHECK_EQ(draft.Plans()[1].x, 12.5f);
  WXT_CHECK_EQ(draft.Plans()[2].x, 16.5f);
  WXT_CHECK_EQ(draft.Plans()[3].x, 18.5f);
  // From the newest, 26 room left: a line 40 steps long, of 30 places, fills the draft and says so.
  WXT_CHECK(!draft.AddLine(18.5f + 80, 10.5f, Anywhere));
  WXT_CHECK_EQ(draft.Plans().size(), Draft::kMaxPlans);
}

WXT_TEST(GriffDraftAMoveTakesEveryPlanButThoseOnDepositsInWholeUnits)
{
  Draft draft = Generators({{10.5f, 10.5f}, {12.5f, 10.5f}});
  draft.Pick(Extractor());
  WXT_CHECK(draft.PlaceOnDeposit({{0, 0, {50.5f, 50.5f}}}, 0, 0, 10, Anywhere) == Placed::kPlaced);
  draft.MoveBegin();
  draft.MoveBy(3.4f, -1.6f);
  WXT_CHECK_EQ(draft.Plans()[0].x, 13.5f);
  WXT_CHECK_EQ(draft.Plans()[0].z, 8.5f);
  WXT_CHECK_EQ(draft.Plans()[1].x, 15.5f);
  WXT_CHECK_EQ(draft.Plans()[2].x, 50.5f);
  WXT_CHECK_EQ(draft.Plans()[2].z, 50.5f);
  // Each move counts from where the move began.
  draft.MoveBy(1, 0);
  WXT_CHECK_EQ(draft.Plans()[0].x, 11.5f);
  WXT_CHECK_EQ(draft.Plans()[0].z, 10.5f);
  draft.MoveEnd();
  WXT_CHECK(!draft.Moving());
  draft.MoveBy(5, 5);
  WXT_CHECK_EQ(draft.Plans()[0].x, 11.5f);
}

WXT_TEST(GriffDraftAMoveGivenUpPutsThePlansBack)
{
  Draft draft = Generators({{10.5f, 10.5f}, {12.5f, 10.5f}});
  draft.MoveBegin();
  draft.MoveBy(7, 7);
  draft.MoveAbort();
  WXT_CHECK_EQ(draft.Plans()[0].x, 10.5f);
  WXT_CHECK_EQ(draft.Plans()[1].z, 10.5f);
  WXT_CHECK(!draft.Moving());
}

WXT_TEST(GriffDraftAPlanOnAnotherOrOnRefusedGroundIsTaken)
{
  Draft draft = Generators({{10.5f, 10.5f}, {12.5f, 10.5f}});
  WXT_CHECK(!draft.Taken(draft.Plans()[0], Anywhere));
  WXT_CHECK(draft.Taken(draft.Plans()[0], Nowhere));
  // A move of the generators onto an extractor's plan, which stays: both are taken.
  draft.Pick(Extractor());
  WXT_CHECK(draft.PlaceOnDeposit({{0, 0, {16.5f, 10.5f}}}, 0, 0, 10, Anywhere) == Placed::kPlaced);
  draft.MoveBegin();
  draft.MoveBy(4, 0);
  WXT_CHECK(draft.Taken(draft.Plans()[1], Anywhere));
  WXT_CHECK(draft.Taken(draft.Plans()[2], Anywhere));
  WXT_CHECK(!draft.Taken(draft.Plans()[0], Anywhere));
}

WXT_TEST(GriffDraftOnlyWhatCanBeBuiltIsOrderedInItsOrder)
{
  Draft draft = Generators({{10.5f, 10.5f}, {12.5f, 10.5f}, {14.5f, 10.5f}});
  draft.Pick(Extractor());
  WXT_CHECK(draft.PlaceOnDeposit({{0, 0, {20.5f, 10.5f}}}, 0, 0, 10, Anywhere) == Placed::kPlaced);
  draft.MoveBegin();
  draft.MoveBy(6, 0);
  draft.MoveEnd();
  // FAF refuses the middle one; the last generator now takes the extractor's ground, which comes after it.
  const auto notTheMiddle = [](const Building&, const float x, float) { return x != 18.5f; };
  const std::vector<Plan> buildable = draft.Buildable(notTheMiddle);
  WXT_CHECK_EQ(buildable.size(), std::size_t{2});
  WXT_CHECK_EQ(buildable[0].x, 16.5f);
  WXT_CHECK_EQ(buildable[1].x, 20.5f);
  WXT_CHECK_EQ(buildable[1].id, draft.Plans()[2].id);
}

WXT_TEST(GriffDraftAnotherBuildingKeepsThePlansAndCloseEndsAll)
{
  Draft draft = Generators({{10.5f, 10.5f}});
  draft.Pick(Factory());
  WXT_CHECK(draft.PlaceAt(102.2f, 52.7f, Anywhere) == Placed::kPlaced);
  WXT_CHECK(draft.BuildingOf(draft.Plans()[0]).id == "ueb1101");
  WXT_CHECK(draft.BuildingOf(draft.Plans()[1]).id == "ueb0101");
  WXT_CHECK_EQ(draft.Plans()[1].x, 102.5f);
  // Picked again, a known building is the next plans' again.
  draft.Pick(Generator());
  WXT_CHECK(draft.Picked().id == "ueb1101");
  WXT_CHECK(draft.PlaceAt(14.5f, 10.5f, Anywhere) == Placed::kPlaced);
  WXT_CHECK_EQ(draft.Plans()[2].building, draft.Plans()[0].building);
  draft.Close();
  WXT_CHECK(!draft.Open());
  WXT_CHECK(draft.Plans().empty());
}
