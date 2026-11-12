// The touch scheme Griff's undo (include/GriffUndo.h): which units stop, which queues are given again and how, and what
// cannot come back. Queues as FAF's UI reads them in a game: its own commands with small ids, those the sim issued itself
// all with one id (INT_MIN).

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include <GriffUndo.h>

#include "Check.h"

namespace
{
  using griffundo::Click;
  using griffundo::Entry;
  using griffundo::Place;
  using griffundo::Plan;
  using griffundo::PlanUndo;
  using griffundo::Targets;
  using griffundo::Unit;
  using griffundo::Way;
  using griffundo::WayOf;

  constexpr int kSimId = (std::numeric_limits<int>::min)();
  const float kNone = std::numeric_limits<float>::quiet_NaN();

  Entry Order(const int id, const std::string& type, const float x, const float z)
  {
    Entry entry;
    entry.id = id;
    entry.type = type;
    entry.place = Place{x, 18, z};
    return entry;
  }

  Entry On(Entry entry, const int target)
  {
    entry.target = target;
    return entry;
  }

  Entry Named(Entry entry, const std::string& blueprint)
  {
    entry.blueprint = blueprint;
    return entry;
  }

  Unit Of(const int id, const bool replaced, std::vector<Entry> before, std::vector<Entry> now, const bool factory = false)
  {
    Unit unit;
    unit.id = id;
    unit.factory = factory;
    unit.replaced = replaced;
    unit.before = std::move(before);
    unit.now = std::move(now);
    return unit;
  }

  /// The plan's only group's clicks, or none.
  std::vector<Click> Clicks(const Plan& plan)
  {
    return plan.groups.size() == 1 ? plan.groups[0].clicks : std::vector<Click>{};
  }

  const Targets kNoTargets;
} // namespace

WXT_TEST(GriffUndoAnAppendedOrderGoesWithTheQueueGivenAgain)
{
  // Its replacing first click takes the gesture's order with it.
  const Plan plan = PlanUndo({Of(4, false, {Order(3, "Move", 10, 11)}, {Order(3, "Move", 10, 11), Order(7, "Move", 20, 20)})},
                             kNoTargets);
  WXT_CHECK(plan.stops.empty());
  WXT_CHECK_EQ(plan.dropped, 0);
  const std::vector<Click> clicks = Clicks(plan);
  WXT_CHECK_EQ(clicks.size(), std::size_t{1});
  if (clicks.size() == 1) {
    WXT_CHECK_EQ(clicks[0].way, Way::kMove);
    WXT_CHECK(!clicks[0].appended);
    WXT_CHECK(clicks[0].place.x == 10 && clicks[0].place.z == 11);
  }
}

WXT_TEST(GriffUndoAnOrderAtAnotherPlaceIsAnotherThoughItsIdIsTheSame)
{
  // The queue before still showed the last move, which the sim had finished, and the gesture's command took its id: the
  // unit was idle.
  const Plan plan = PlanUndo({Of(4, false, {Order(0, "Move", 10, 10)}, {Order(0, "Move", 12, 11)})}, kNoTargets);
  WXT_CHECK(plan.stops == (std::vector<int>{4}));
  WXT_CHECK(plan.groups.empty());
}

WXT_TEST(GriffUndoWhatAUnitFinishedSinceIsNotGivenAgain)
{
  // It reached the first place of its route after the gesture appended to it: it goes on from the second.
  const Plan plan = PlanUndo(
    {Of(4, false, {Order(1, "Move", 10, 10), Order(2, "Move", 20, 20)}, {Order(2, "Move", 20, 20), Order(7, "Move", 50, 50)})},
    kNoTargets
  );
  WXT_CHECK(plan.stops.empty());
  const std::vector<Click> clicks = Clicks(plan);
  WXT_CHECK(clicks.size() == 1 && clicks[0].place.x == 20 && !clicks[0].appended);
  // It finished all of them: it stops, as one that was idle.
  const Plan done = PlanUndo({Of(4, false, {Order(1, "Move", 10, 10)}, {Order(7, "Move", 50, 50)})}, kNoTargets);
  WXT_CHECK(done.stops == (std::vector<int>{4}));
  WXT_CHECK(done.groups.empty());
  WXT_CHECK_EQ(done.dropped, 0);
}

WXT_TEST(GriffUndoAUnitThatWasIdleStops)
{
  for (const bool replaced : {true, false}) {
    const Plan plan = PlanUndo({Of(4, replaced, {}, {Order(2, "Move", 20, 20)})}, kNoTargets);
    WXT_CHECK(plan.stops == (std::vector<int>{4}));
    WXT_CHECK(plan.groups.empty());
    WXT_CHECK_EQ(plan.dropped, 0);
  }
  // A Stop the sim has not taken yet leaves the unit idle: nothing to give again.
  const Plan stopping = PlanUndo({Of(4, true, {Order(0, "Stop", kNone, kNone)}, {Order(2, "Move", 20, 20)})}, kNoTargets);
  WXT_CHECK(stopping.stops == (std::vector<int>{4}));
  WXT_CHECK_EQ(stopping.dropped, 0);
}

WXT_TEST(GriffUndoAFactorysRallyPointComesBackButAFactoryIsNotStopped)
{
  // The sim's own rally point before, which comes back as one of the UI's at its place.
  const Plan plan = PlanUndo({Of(1, true, {Order(kSimId, "Move", 30, 31)}, {Order(0, "Move", 20, 20)}, true)}, kNoTargets);
  const std::vector<Click> clicks = Clicks(plan);
  WXT_CHECK(clicks.size() == 1 && clicks[0].way == Way::kMove && clicks[0].place.x == 30);
  // None before: its Stop would take what it builds too, so the gesture's rally point stays.
  const Plan none = PlanUndo({Of(1, true, {}, {Order(0, "Move", 20, 20)}, true)}, kNoTargets);
  WXT_CHECK(none.stops.empty());
  WXT_CHECK(none.groups.empty());
  WXT_CHECK_EQ(none.dropped, 1);
}

WXT_TEST(GriffUndoAReplacedQueueIsGivenAgainTheFirstClickReplacing)
{
  const std::vector<Entry> before = {Order(1, "Move", 10, 11), Order(2, "Patrol", 20, 21), Order(kSimId, "AggressiveMove", 30, 31)};
  const Plan plan = PlanUndo({Of(4, true, before, {Order(3, "Move", 50, 50)})}, kNoTargets);
  WXT_CHECK(plan.stops.empty());
  WXT_CHECK_EQ(plan.dropped, 0);
  const std::vector<Click> clicks = Clicks(plan);
  WXT_CHECK_EQ(clicks.size(), std::size_t{3});
  if (clicks.size() != 3) {
    return;
  }
  WXT_CHECK_EQ(clicks[0].way, Way::kMove);
  WXT_CHECK(!clicks[0].appended);
  WXT_CHECK(clicks[0].place.x == 10 && clicks[0].place.z == 11);
  WXT_CHECK_EQ(clicks[1].way, Way::kPatrol);
  WXT_CHECK(clicks[1].appended);
  WXT_CHECK_EQ(clicks[2].way, Way::kAttackMove);
  WXT_CHECK(clicks[2].appended);
  WXT_CHECK(plan.groups[0].units == (std::vector<int>{4}));
}

WXT_TEST(GriffUndoUnitsWhoseQueuesComeBackAlikeShareAGroup)
{
  const std::vector<Entry> one = {Order(1, "Move", 10, 10)};
  const std::vector<Entry> other = {Order(2, "Move", 40, 40)};
  const std::vector<Entry> now = {Order(3, "Move", 50, 50)};
  const Plan plan = PlanUndo({Of(4, true, one, now), Of(5, true, other, now), Of(6, true, one, now)}, kNoTargets);
  WXT_CHECK_EQ(plan.groups.size(), std::size_t{2});
  if (plan.groups.size() == 2) {
    WXT_CHECK(plan.groups[0].units == (std::vector<int>{4, 6}));
    WXT_CHECK(plan.groups[1].units == (std::vector<int>{5}));
  }
}

WXT_TEST(GriffUndoWhatTheSimAddedToAnAppendedOrderGoesWithTheQueueGivenAgain)
{
  // FAF's attack-move with Plus: the sim gave an AggressiveMove of its own.
  const Entry building = Named(Order(1, "BuildMobile", 10, 10), "ueb0101");
  const Plan plan = PlanUndo({Of(4, false, {building}, {building, Order(kSimId, "AggressiveMove", 30, 30)})}, kNoTargets);
  WXT_CHECK(plan.stops.empty());
  WXT_CHECK_EQ(plan.dropped, 0);
  const std::vector<Click> clicks = Clicks(plan);
  WXT_CHECK(clicks.size() == 1 && clicks[0].way == Way::kBuild && clicks[0].blueprint == "ueb0101");
}

WXT_TEST(GriffUndoTheSimsOrdersAreToldApartByTheirPlaces)
{
  const Entry earlier = Order(kSimId, "AggressiveMove", 30, 30);
  // The same at the same place: the queue is as it was.
  const Plan kept = PlanUndo({Of(4, true, {earlier}, {earlier})}, kNoTargets);
  WXT_CHECK(kept.groups.empty());
  WXT_CHECK(kept.stops.empty());
  // Another of the sim's at another place, though its id is the same: the one before was lost, and comes back.
  const Plan lost = PlanUndo({Of(4, true, {earlier}, {Order(kSimId, "AggressiveMove", 40, 40)})}, kNoTargets);
  const std::vector<Click> clicks = Clicks(lost);
  WXT_CHECK(clicks.size() == 1 && clicks[0].place.x == 30);
}

WXT_TEST(GriffUndoAUnitWhoseQueueIsAsItWasIsLeftAlone)
{
  // The sim kept the commander at its enhancement, which FAF's UI reads without a place, and the gesture's move did not take.
  const std::vector<Entry> enhancing = {Order(0, "Script", kNone, kNone)};
  const Plan plan = PlanUndo({Of(0, true, enhancing, enhancing)}, kNoTargets);
  WXT_CHECK(plan.stops.empty());
  WXT_CHECK(plan.groups.empty());
  WXT_CHECK_EQ(plan.dropped, 0);
}

WXT_TEST(GriffUndoWhatTheSimKeptThroughAReplacingOrderIsNotGivenAgain)
{
  // The commander's enhancement under way stays through the move that replaced the queue; the move after it went.
  const Entry enhancing = Order(1, "Script", kNone, kNone);
  const Plan plan = PlanUndo({Of(0, true, {enhancing, Order(2, "Move", 10, 11)}, {enhancing, Order(3, "Move", 50, 50)})},
                             kNoTargets);
  WXT_CHECK_EQ(plan.dropped, 0);
  WXT_CHECK(plan.stops.empty());
  const std::vector<Click> clicks = Clicks(plan);
  WXT_CHECK(clicks.size() == 1 && clicks[0].way == Way::kMove && clicks[0].place.x == 10 && !clicks[0].appended);
  // Where the replacing order took nothing and the gesture's came after what the unit holds, which FAF cannot give again:
  // the unit keeps both, and the undo is in part, not its Stop, which would take the enhancement.
  const Plan held = PlanUndo({Of(0, true, {enhancing}, {enhancing, Order(3, "Move", 50, 50)})}, kNoTargets);
  WXT_CHECK(held.stops.empty());
  WXT_CHECK(held.groups.empty());
  WXT_CHECK_EQ(held.dropped, 1);
}

WXT_TEST(GriffUndoAUnitThatLostAllItsOrdersAndGetsNoneBackStops)
{
  const Plan plan = PlanUndo({Of(4, true, {Order(1, "Ferry", 5, 5)}, {Order(3, "Move", 50, 50)})}, kNoTargets);
  WXT_CHECK(plan.stops == (std::vector<int>{4}));
  WXT_CHECK_EQ(plan.dropped, 1);
}

WXT_TEST(GriffUndoAnOrderOnAUnitComesBackWhereItIsNowOrNotAtAll)
{
  const std::vector<Entry> before = {On(Order(1, "Guard", 10, 10), 77)};
  Targets targets;
  targets[77] = Place{15, 18, 16};
  const std::vector<Click> clicks = Clicks(PlanUndo({Of(4, true, before, {Order(3, "Move", 50, 50)})}, targets));
  WXT_CHECK_EQ(clicks.size(), std::size_t{1});
  if (clicks.size() == 1) {
    WXT_CHECK_EQ(clicks[0].way, Way::kGuard);
    WXT_CHECK(clicks[0].place.x == 15 && clicks[0].place.z == 16);
    WXT_CHECK_EQ(clicks[0].target, 77);
  }
  // The first army's commander is unit 0.
  Targets commander;
  commander[0] = Place{20, 18, 21};
  const std::vector<Click> guarding = Clicks(PlanUndo({Of(4, true, {On(Order(1, "Guard", 10, 10), 0)}, {Order(3, "Move", 50, 50)})},
                                                      commander));
  WXT_CHECK(guarding.size() == 1 && guarding[0].place.x == 20);
  // Gone, or never seen given: nothing of the queue comes back, and the unit stops.
  const Plan gone = PlanUndo({Of(4, true, before, {Order(3, "Move", 50, 50)})}, kNoTargets);
  const Plan unseen = PlanUndo({Of(4, true, {On(Order(1, "Guard", 10, 10), griffundo::kNoTarget)}, {Order(3, "Move", 50, 50)})},
                               targets);
  for (const Plan* const lost : {&gone, &unseen}) {
    WXT_CHECK_EQ(lost->dropped, 1);
    WXT_CHECK(lost->groups.empty());
    WXT_CHECK(lost->stops == (std::vector<int>{4}));
  }
}

WXT_TEST(GriffUndoAnAttackOnTheGroundAndTheReclaimOfAWreckKeepTheirPlace)
{
  for (const char* type : {"Attack", "Reclaim"}) {
    Entry entry = Order(1, type, 10, 11);
    entry.targetsPlace = true;
    const Plan plan = PlanUndo({Of(4, true, {entry}, {Order(3, "Move", 50, 50)})}, kNoTargets);
    WXT_CHECK_EQ(plan.dropped, 0);
    const std::vector<Click> clicks = Clicks(plan);
    WXT_CHECK(clicks.size() == 1 && clicks[0].place.x == 10);
  }
}

WXT_TEST(GriffUndoAnOrderWithoutAPlaceDoesNotComeBack)
{
  // FAF's UI reads no place for an order on an entity it does not find: an attack-move clicked on a unit, a wreck's reclaim.
  Entry attackMove = Order(1, "Script", kNone, kNone);
  attackMove.task = "AttackMove";
  Entry reclaim = Order(1, "Reclaim", kNone, kNone);
  reclaim.targetsPlace = true;
  for (const Entry& entry : {attackMove, reclaim, Order(1, "Move", kNone, kNone)}) {
    const Plan plan = PlanUndo({Of(4, true, {entry}, {Order(3, "Move", 50, 50)})}, kNoTargets);
    WXT_CHECK_EQ(plan.dropped, 1);
    WXT_CHECK(plan.groups.empty());
    WXT_CHECK(plan.stops == (std::vector<int>{4}));
  }
}

WXT_TEST(GriffUndoABuildingComesBackByItsNameOnly)
{
  const Plan plan = PlanUndo(
    {Of(4, true, {Named(Order(1, "BuildMobile", 10, 10), "ueb1101"), Order(2, "BuildMobile", 20, 20)}, {Order(3, "Move", 50, 50)})},
    kNoTargets
  );
  WXT_CHECK_EQ(plan.dropped, 1);
  const std::vector<Click> clicks = Clicks(plan);
  WXT_CHECK(clicks.size() == 1 && clicks[0].way == Way::kBuild && clicks[0].blueprint == "ueb1101");
}

WXT_TEST(GriffUndoTheFirstOrderThatComesBackReplacesThoughOnesBeforeItCannot)
{
  const Plan plan = PlanUndo({Of(4, true, {Order(1, "Ferry", 5, 5), Order(2, "Move", 10, 10)}, {Order(3, "Move", 50, 50)})},
                             kNoTargets);
  WXT_CHECK_EQ(plan.dropped, 1);
  const std::vector<Click> clicks = Clicks(plan);
  WXT_CHECK(clicks.size() == 1 && !clicks[0].appended);
}

WXT_TEST(GriffUndoTheWayOfEachOrder)
{
  const auto way = [](const char* type, const char* task = "") {
    Entry entry = Order(1, type, 0, 0);
    entry.task = task;
    return WayOf(entry);
  };
  WXT_CHECK_EQ(way("Move"), Way::kMove);
  WXT_CHECK_EQ(way("FormMove"), Way::kMove);
  WXT_CHECK_EQ(way("AggressiveMove"), Way::kAttackMove);
  WXT_CHECK_EQ(way("FormAggressiveMove"), Way::kAttackMove);
  WXT_CHECK_EQ(way("Script", "AttackMove"), Way::kAttackMove);
  WXT_CHECK_EQ(way("Script"), Way::kNone);
  WXT_CHECK_EQ(way("Patrol"), Way::kPatrol);
  WXT_CHECK_EQ(way("FormPatrol"), Way::kPatrol);
  WXT_CHECK_EQ(way("Attack"), Way::kAttack);
  WXT_CHECK_EQ(way("FormAttack"), Way::kAttack);
  WXT_CHECK_EQ(way("Guard"), Way::kGuard);
  WXT_CHECK_EQ(way("Repair"), Way::kRepair);
  WXT_CHECK_EQ(way("Reclaim"), Way::kReclaim);
  WXT_CHECK_EQ(way("Capture"), Way::kCapture);
  WXT_CHECK_EQ(way("BuildMobile"), Way::kBuild);
  WXT_CHECK_EQ(way("TransportLoadUnits"), Way::kNone);
  WXT_CHECK_EQ(way("Teleport"), Way::kNone);
  WXT_CHECK_EQ(way("Stop"), Way::kNone);
}
