// The touch scheme Griff's undo (include/GriffUndo.h): which commands are taken out, which lost orders are given again and
// with which keys, and what cannot. Queues as the program reads them in a game: each command by its own id, the UI's from 0,
// the sim's own with its source byte 0x80.

#include <cmath>
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
  using griffundo::Removal;
  using griffundo::Targets;
  using griffundo::Unit;
  using griffundo::Way;
  using griffundo::WayOf;

  /// The sim's own commands, as the attack-move's AggressiveMove and a factory's first rally point.
  constexpr int kSimId = static_cast<int>(0x80000012u);
  constexpr int kSimOther = static_cast<int>(0x80000061u);
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

  Entry Task(Entry entry, const std::string& task)
  {
    entry.task = task;
    return entry;
  }

  Unit Of(const int id, const bool replaced, std::vector<Entry> before, std::vector<Entry> now)
  {
    Unit unit;
    unit.id = id;
    unit.replaced = replaced;
    unit.before = std::move(before);
    unit.now = std::move(now);
    return unit;
  }

  Unit Refilled(Unit unit)
  {
    unit.refilled = true;
    return unit;
  }

  Unit Building(Unit unit, const int site)
  {
    unit.site = site;
    return unit;
  }

  /// The plan's only group's clicks, or none.
  std::vector<Click> Clicks(const Plan& plan)
  {
    return plan.groups.size() == 1 ? plan.groups[0].clicks : std::vector<Click>{};
  }

  /// The commands the plan takes out, in its order.
  std::vector<int> Removed(const Plan& plan)
  {
    std::vector<int> commands;
    for (const Removal& removal : plan.removals) {
      commands.push_back(removal.command);
    }
    return commands;
  }

  bool AllWithShift(const std::vector<Click>& clicks)
  {
    for (const Click& click : clicks) {
      if (!click.shift) {
        return false;
      }
    }
    return true;
  }

  const Targets kNoTargets;
} // namespace

WXT_TEST(GriffUndoAnAppendedOrderIsTakenOutAndTheQueueBeforeStays)
{
  const Plan plan = PlanUndo({Of(4, false, {Order(3, "Move", 10, 11)}, {Order(3, "Move", 10, 11), Order(7, "Move", 20, 20)})},
                             kNoTargets);
  WXT_CHECK(Removed(plan) == (std::vector<int>{7}));
  WXT_CHECK(plan.groups.empty());
  WXT_CHECK_EQ(plan.dropped, 0);
  if (plan.removals.size() == 1) {
    WXT_CHECK(plan.removals[0].units == (std::vector<int>{4}));
    WXT_CHECK(plan.removals[0].place.x == 20 && plan.removals[0].place.z == 20);
  }
}

WXT_TEST(GriffUndoABegunBuildingGoesOnWhereAMoveWasAppended)
{
  // The engineer at 40 % of its building (site 55, its focus), Plus and a tap on the ground: the move goes, the building is
  // neither clicked again nor left.
  Targets targets;
  targets[55] = Place{10, 18, 10};
  const Entry building = Named(Order(7, "BuildMobile", 10, 10), "ueb1101");
  const Plan plan = PlanUndo({Building(Of(4, false, {building}, {building, Order(8, "Move", 50, 50)}), 55)}, targets);
  WXT_CHECK(Removed(plan) == (std::vector<int>{8}));
  WXT_CHECK(plan.groups.empty());
  WXT_CHECK_EQ(plan.dropped, 0);
}

WXT_TEST(GriffUndoAHeldBuildingNotBegunIsNeverClickedAgain)
{
  // FAF's build check refuses a building where its own queued command stands: what the unit held stays as it is.
  const Entry building = Named(Order(3, "BuildMobile", 10, 10), "ueb1101");
  const Entry move = Order(4, "Move", 20, 20);
  const Entry reclaim = Order(5, "Reclaim", 30, 30);
  const Plan first = PlanUndo({Of(4, false, {building, move}, {building, move, reclaim})}, kNoTargets);
  const Plan second = PlanUndo({Of(4, false, {move, building}, {move, building, reclaim})}, kNoTargets);
  for (const Plan* const plan : {&first, &second}) {
    WXT_CHECK(Removed(*plan) == (std::vector<int>{5}));
    WXT_CHECK(plan->groups.empty());
    WXT_CHECK_EQ(plan->dropped, 0);
  }
  // The attack-move with Plus: the sim's AggressiveMove goes, FAF's script beside it is done by itself.
  const Plan sims = PlanUndo(
    {Of(4, false, {building}, {building, Task(Order(6, "Script", 30, 30), "AttackMove"), Order(kSimId, "AggressiveMove", 30, 30)})},
    kNoTargets
  );
  WXT_CHECK(Removed(sims) == (std::vector<int>{kSimId}));
  WXT_CHECK(sims.groups.empty());
  WXT_CHECK_EQ(sims.dropped, 0);
}

WXT_TEST(GriffUndoAReplacedQueueIsGivenAgainEveryClickWithShift)
{
  const std::vector<Entry> before = {Order(1, "Move", 10, 11), Order(2, "Move", 20, 21), Order(kSimId, "AggressiveMove", 30, 31)};
  const Plan plan = PlanUndo({Of(4, true, before, {Order(9, "Move", 50, 50)})}, kNoTargets);
  WXT_CHECK(Removed(plan) == (std::vector<int>{9}));
  WXT_CHECK_EQ(plan.dropped, 0);
  const std::vector<Click> clicks = Clicks(plan);
  WXT_CHECK_EQ(clicks.size(), std::size_t{3});
  WXT_CHECK(AllWithShift(clicks));
  if (clicks.size() == 3) {
    WXT_CHECK_EQ(clicks[0].way, Way::kMove);
    WXT_CHECK(clicks[0].place.x == 10 && clicks[0].place.z == 11);
    WXT_CHECK_EQ(clicks[1].way, Way::kMove);
    WXT_CHECK(clicks[1].place.x == 20);
    WXT_CHECK_EQ(clicks[2].way, Way::kAttackMove);
    WXT_CHECK(clicks[2].place.x == 30);
  }
  WXT_CHECK(plan.groups.size() == 1 && plan.groups[0].units == (std::vector<int>{4}));
}

WXT_TEST(GriffUndoABegunBuildingComesBackByItsRepair)
{
  // Replaced: the site (55, the engineer's focus as the gesture began, unfinished still) at the order's place.
  Targets targets;
  targets[55] = Place{10.4f, 18, 9.6f};
  const Entry building = Named(Order(7, "BuildMobile", 10, 10), "ueb1101");
  const Plan begun = PlanUndo({Building(Of(4, true, {building}, {Order(8, "Move", 50, 50)}), 55)}, targets);
  const std::vector<Click> clicks = Clicks(begun);
  WXT_CHECK(clicks.size() == 1 && clicks[0].way == Way::kRepair && clicks[0].target == 55 && clicks[0].shift);
  WXT_CHECK(clicks.size() == 1 && clicks[0].place.x == 10.4f && clicks[0].blueprint.empty());
  WXT_CHECK(Removed(begun) == (std::vector<int>{8}));
  // Not begun, or the site elsewhere or gone: FAF's build mode at its place, by its name.
  Targets elsewhere;
  elsewhere[55] = Place{12, 18, 10};
  const Targets& another = elsewhere;
  for (const Targets* const sites : {&kNoTargets, &another}) {
    const std::vector<Click> again = Clicks(PlanUndo({Building(Of(4, true, {building}, {Order(8, "Move", 50, 50)}), 55)}, *sites));
    WXT_CHECK(again.size() == 1 && again[0].way == Way::kBuild && again[0].blueprint == "ueb1101" && again[0].place.x == 10);
  }
}

WXT_TEST(GriffUndoAPatrolComesBackWithoutAPointOfItsOwn)
{
  // FAF's pair [anchor, point]: a move to the anchor, then the point, which FAF anchors at the move's end.
  const Plan plan =
    PlanUndo({Of(4, true, {Order(1, "Patrol", 10, 10), Order(2, "Patrol", 20, 20)}, {Order(3, "Move", 50, 50)})}, kNoTargets);
  std::vector<Click> clicks = Clicks(plan);
  WXT_CHECK_EQ(clicks.size(), std::size_t{2});
  if (clicks.size() == 2) {
    WXT_CHECK(clicks[0].way == Way::kMove && clicks[0].place.x == 10);
    WXT_CHECK(clicks[1].way == Way::kPatrol && clicks[1].place.x == 20);
  }
  // A patrol anchored at the move before it: that move ends at the anchor already.
  const Plan after = PlanUndo(
    {Of(4, true, {Order(1, "Move", 10, 10), Order(2, "Patrol", 10, 10), Order(3, "Patrol", 20, 20), Order(4, "Patrol", 30, 30)},
        {Order(5, "Move", 50, 50)})},
    kNoTargets
  );
  clicks = Clicks(after);
  WXT_CHECK_EQ(clicks.size(), std::size_t{3});
  if (clicks.size() == 3) {
    WXT_CHECK(clicks[0].way == Way::kMove && clicks[0].place.x == 10);
    WXT_CHECK(clicks[1].way == Way::kPatrol && clicks[1].place.x == 20);
    WXT_CHECK(clicks[2].way == Way::kPatrol && clicks[2].place.x == 30);
  }
}

WXT_TEST(GriffUndoAnEnhancementIsNeverTakenWithTheGesturesOrders)
{
  // The sim kept the commander at its enhancement through the replacing move: the move goes, the enhancement goes on.
  const Entry enhancing = Order(1, "Script", kNone, kNone);
  const Plan kept = PlanUndo({Of(0, true, {enhancing}, {enhancing, Order(3, "Move", 50, 50)})}, kNoTargets);
  WXT_CHECK(Removed(kept) == (std::vector<int>{3}));
  WXT_CHECK(kept.groups.empty());
  WXT_CHECK_EQ(kept.dropped, 0);
  // Gone with the move (the orders panel's Stop took it, say): it cannot come back, and nothing else stops the unit.
  const Plan lost = PlanUndo({Of(0, true, {enhancing}, {Order(3, "Move", 50, 50)})}, kNoTargets);
  WXT_CHECK(Removed(lost) == (std::vector<int>{3}));
  WXT_CHECK(lost.groups.empty());
  WXT_CHECK_EQ(lost.dropped, 1);
}

WXT_TEST(GriffUndoAnOrderOnAMovingUnitIsHeldStillWhereTheGestureAppended)
{
  // FAF's UI reads an order on a unit at that unit's place, which moves between the watch and the plan: the command's own id
  // tells it is the same, so the assist stays and only the gesture's move goes.
  const Entry guard = On(Order(7, "Guard", 10, 10), 77);
  Entry walked = guard;
  walked.place = Place{14, 18, 12};
  const Plan alone = PlanUndo({Of(4, false, {guard}, {walked, Order(8, "Move", 50, 50)})}, kNoTargets);
  WXT_CHECK(Removed(alone) == (std::vector<int>{8}));
  WXT_CHECK(alone.groups.empty());
  const Plan routed =
    PlanUndo({Of(4, false, {guard, Order(9, "Move", 30, 30)}, {walked, Order(9, "Move", 30, 30), Order(10, "Move", 50, 50)})}, kNoTargets);
  WXT_CHECK(Removed(routed) == (std::vector<int>{10}));
  WXT_CHECK(routed.groups.empty());
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
  // The first army's commander is entity 0.
  Targets commander;
  commander[0] = Place{20, 18, 21};
  const std::vector<Click> guarding =
    Clicks(PlanUndo({Of(4, true, {On(Order(1, "Guard", 10, 10), 0)}, {Order(3, "Move", 50, 50)})}, commander));
  WXT_CHECK(guarding.size() == 1 && guarding[0].place.x == 20 && guarding[0].target == 0);
  // Gone: it does not come back, and the gesture's move goes still.
  const Plan gone = PlanUndo({Of(4, true, before, {Order(3, "Move", 50, 50)})}, kNoTargets);
  WXT_CHECK_EQ(gone.dropped, 1);
  WXT_CHECK(gone.groups.empty());
  WXT_CHECK(Removed(gone) == (std::vector<int>{3}));
}

WXT_TEST(GriffUndoAnIdleUnitsOrderIsTakenOutAndNothingElse)
{
  for (const bool replaced : {true, false}) {
    const Plan plan = PlanUndo({Of(4, replaced, {}, {Order(2, "Move", 20, 20)})}, kNoTargets);
    WXT_CHECK(Removed(plan) == (std::vector<int>{2}));
    WXT_CHECK(plan.groups.empty());
    WXT_CHECK_EQ(plan.dropped, 0);
  }
  // A Stop the sim has not taken yet leaves the unit idle: nothing to give again, and nothing of it to take out.
  const Plan stopping = PlanUndo({Of(4, true, {Order(0, "Stop", kNone, kNone)}, {Order(2, "Move", 20, 20)})}, kNoTargets);
  WXT_CHECK(Removed(stopping) == (std::vector<int>{2}));
  WXT_CHECK(stopping.groups.empty());
  WXT_CHECK_EQ(stopping.dropped, 0);
  const Plan stopped = PlanUndo({Of(4, true, {Order(1, "Move", 20, 20)}, {Order(2, "Stop", kNone, kNone)})}, kNoTargets);
  WXT_CHECK(stopped.removals.empty());
  WXT_CHECK(Clicks(stopped).size() == 1);
}

WXT_TEST(GriffUndoWhatAUnitFinishedSinceIsNotGivenAgain)
{
  // It reached the first place of its route after the gesture appended to it: only the gesture's order goes.
  const Plan plan = PlanUndo(
    {Of(4, false, {Order(1, "Move", 10, 10), Order(2, "Move", 20, 20)}, {Order(2, "Move", 20, 20), Order(7, "Move", 50, 50)})},
    kNoTargets
  );
  WXT_CHECK(Removed(plan) == (std::vector<int>{7}));
  WXT_CHECK(plan.groups.empty());
  WXT_CHECK_EQ(plan.dropped, 0);
}

WXT_TEST(GriffUndoACommandOfManyUnitsIsTakenOutOnce)
{
  // One release takes it from every unit it went to.
  const std::vector<Entry> now = {Order(9, "Move", 50, 50)};
  const Plan plan = PlanUndo({Of(4, false, {}, now), Of(5, false, {Order(1, "Move", 5, 5)}, {Order(1, "Move", 5, 5), now[0]}),
                              Of(6, true, {}, now)},
                             kNoTargets);
  WXT_CHECK(Removed(plan) == (std::vector<int>{9}));
  if (plan.removals.size() == 1) {
    WXT_CHECK(plan.removals[0].units == (std::vector<int>{4, 5, 6}));
  }
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
  WXT_CHECK(Removed(plan) == (std::vector<int>{3}));
}

WXT_TEST(GriffUndoAFactorysRallyRouteComesBackByAClickThatReplacesIt)
{
  // The sim rallies an emptied route to the factory's own place again: the route before comes back by a first click that
  // replaces the gesture's rally point, which is not taken out.
  const std::vector<Entry> route = {Order(kSimId, "Move", 30, 31), Order(1, "Move", 35, 36)};
  const Plan plan = PlanUndo({Refilled(Of(1, true, route, {Order(4, "Move", 20, 20)}))}, kNoTargets);
  WXT_CHECK(plan.removals.empty());
  const std::vector<Click> clicks = Clicks(plan);
  WXT_CHECK_EQ(clicks.size(), std::size_t{2});
  if (clicks.size() == 2) {
    WXT_CHECK(clicks[0].way == Way::kMove && clicks[0].place.x == 30 && !clicks[0].shift);
    WXT_CHECK(clicks[1].place.x == 35 && clicks[1].shift);
  }
  // Appended to: the gesture's rally point goes, the route stays.
  const Plan appended = PlanUndo({Refilled(Of(1, false, route, {route[0], route[1], Order(4, "Move", 20, 20)}))}, kNoTargets);
  WXT_CHECK(Removed(appended) == (std::vector<int>{4}));
  WXT_CHECK(appended.groups.empty());
  // Nothing to give again: the gesture's rally point goes, and the sim rallies the factory as it does.
  const Plan none = PlanUndo({Refilled(Of(1, true, {Order(2, "Ferry", 5, 5)}, {Order(4, "Move", 20, 20)}))}, kNoTargets);
  WXT_CHECK(Removed(none) == (std::vector<int>{4}));
  WXT_CHECK_EQ(none.dropped, 1);
  // A factory and a unit that lost the same route come back apart: the factory's first click replaces, the unit's does not.
  const Plan both = PlanUndo({Refilled(Of(1, true, route, {Order(4, "Move", 20, 20)})), Of(5, true, route, {Order(4, "Move", 20, 20)})},
                             kNoTargets);
  WXT_CHECK_EQ(both.groups.size(), std::size_t{2});
  WXT_CHECK(Removed(both) == (std::vector<int>{4}));
}

WXT_TEST(GriffUndoAUnitsOwnOrderAtTheGesturesWaypointComesBackAfterItsLastOnly)
{
  // FAF anchors a patrol given with Shift at the queue's last place: the kept move and the patrol's first point lie at one
  // place. The move may go with the gesture's and come back after them, as nothing of the unit's own comes after it.
  const std::vector<Entry> queue = {Order(1, "Move", 5, 5), Order(2, "Move", 10, 10), Order(3, "Patrol", 10, 10),
                                    Order(4, "Patrol", 20, 20)};
  const std::vector<int> gestures = {3, 4};
  WXT_CHECK(griffundo::ComesBackAfter(queue, 2, gestures));
  WXT_CHECK(!griffundo::ComesBackAfter(queue, 1, gestures));
  WXT_CHECK(!griffundo::ComesBackAfter(queue, 9, gestures));
  // Only an order on a place, whose click gives it again exactly: not one on a unit (an attack-move clicked on one too), not
  // a building.
  for (const Entry& last : {On(Order(2, "Guard", 10, 10), 77), On(Order(2, "AggressiveMove", 10, 10), 77),
                            Named(Order(2, "BuildMobile", 10, 10), "ueb1101")}) {
    WXT_CHECK(!griffundo::ComesBackAfter({Order(1, "Move", 5, 5), last, Order(3, "Patrol", 10, 10)}, 2, {3}));
  }
  WXT_CHECK(griffundo::ComesBackAfter({Order(kSimId, "AggressiveMove", 10, 10), Order(3, "Patrol", 10, 10)}, kSimId, {3}));
  WXT_CHECK(griffundo::ComesBackAfter({Order(2, "Patrol", 10, 10), Order(3, "Move", 10, 10)}, 2, {3}));
}

WXT_TEST(GriffUndoTheAttackMovesAggressiveMoveGoesAndItComesBackAsOne)
{
  // Replaced: the sim's AggressiveMove is the gesture's; the move before comes back.
  const Plan plan = PlanUndo({Of(4, true, {Order(1, "Move", 10, 10)}, {Order(kSimOther, "AggressiveMove", 40, 40)})}, kNoTargets);
  WXT_CHECK(Removed(plan) == (std::vector<int>{kSimOther}));
  const std::vector<Click> clicks = Clicks(plan);
  WXT_CHECK(clicks.size() == 1 && clicks[0].way == Way::kMove && clicks[0].place.x == 10);
  // Lost with Plus's script beside it: one attack-move comes back, the script being FAF's by-product.
  const std::vector<Entry> before = {Task(Order(2, "Script", 30, 30), "AttackMove"), Order(kSimId, "AggressiveMove", 30, 30)};
  const Plan lost = PlanUndo({Of(4, true, before, {Order(5, "Move", 50, 50)})}, kNoTargets);
  const std::vector<Click> again = Clicks(lost);
  WXT_CHECK(again.size() == 1 && again[0].way == Way::kAttackMove && again[0].place.x == 30);
  WXT_CHECK_EQ(lost.dropped, 0);
}

WXT_TEST(GriffUndoAnOrderFAFDrawsNoWaypointForCannotGo)
{
  // A script other than the attack-move's (an enhancement) and a dive have no waypoint for FAF's release to strip.
  const Plan plan =
    PlanUndo({Of(4, false, {}, {Order(2, "Script", kNone, kNone), Order(3, "Dive", 1, 1), Order(4, "Move", 9, 9)})}, kNoTargets);
  WXT_CHECK(Removed(plan) == (std::vector<int>{4}));
  WXT_CHECK_EQ(plan.dropped, 2);
}

WXT_TEST(GriffUndoAnOrderWithoutAWayDoesNotComeBack)
{
  const Plan plan = PlanUndo({Of(4, true, {Order(1, "Ferry", 5, 5), Order(2, "Move", 10, 10)}, {Order(3, "Move", 50, 50)})},
                             kNoTargets);
  WXT_CHECK_EQ(plan.dropped, 1);
  const std::vector<Click> clicks = Clicks(plan);
  WXT_CHECK(clicks.size() == 1 && clicks[0].way == Way::kMove && clicks[0].shift);
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

WXT_TEST(GriffUndoAnAttackOnTheGroundKeepsItsPlaceAndAWrecksReclaimFindsTheWreck)
{
  const Plan ground = PlanUndo({Of(4, true, {Order(1, "Attack", 10, 11)}, {Order(3, "Move", 50, 50)})}, kNoTargets);
  WXT_CHECK_EQ(ground.dropped, 0);
  const std::vector<Click> clicks = Clicks(ground);
  WXT_CHECK(clicks.size() == 1 && clicks[0].way == Way::kAttack && clicks[0].place.x == 10 && clicks[0].target == griffundo::kNoTarget);
  // A wreck is an entity (a prop's id), which stays where it is as long as it lies there.
  constexpr int kWreck = 0x20000005;
  Targets wreck;
  wreck[kWreck] = Place{12, 18, 13};
  const std::vector<Click> reclaim =
    Clicks(PlanUndo({Of(4, true, {On(Order(1, "Reclaim", 12, 13), kWreck)}, {Order(3, "Move", 50, 50)})}, wreck));
  WXT_CHECK(reclaim.size() == 1 && reclaim[0].way == Way::kReclaim && reclaim[0].target == kWreck && reclaim[0].place.x == 12);
}

WXT_TEST(GriffUndoAnOrderWithoutAPlaceDoesNotComeBack)
{
  const Plan plan = PlanUndo({Of(4, true, {Order(1, "Move", kNone, kNone)}, {Order(3, "Move", 50, 50)})}, kNoTargets);
  WXT_CHECK_EQ(plan.dropped, 1);
  WXT_CHECK(plan.groups.empty());
  WXT_CHECK(std::isnan(Order(1, "Move", kNone, kNone).place.x));
}

WXT_TEST(GriffUndoAQueueAsItWasIsLeftAlone)
{
  // The sim kept the commander at its enhancement and dropped the gesture's move.
  const std::vector<Entry> enhancing = {Order(0, "Script", kNone, kNone)};
  const Plan plan = PlanUndo({Of(0, true, enhancing, enhancing)}, kNoTargets);
  WXT_CHECK(plan.removals.empty());
  WXT_CHECK(plan.groups.empty());
  WXT_CHECK_EQ(plan.dropped, 0);
}

WXT_TEST(GriffUndoCommandsAreToldApartByTheirOwnIds)
{
  const Plan plan =
    PlanUndo({Of(4, true, {Order(kSimId, "AggressiveMove", 30, 30)}, {Order(kSimOther, "AggressiveMove", 40, 40)})}, kNoTargets);
  WXT_CHECK(Removed(plan) == (std::vector<int>{kSimOther}));
  const std::vector<Click> clicks = Clicks(plan);
  WXT_CHECK(clicks.size() == 1 && clicks[0].place.x == 30);
  // The second army's commands from 2^24 on, which the game's Lua numbers (floats) hold to an even number only.
  constexpr int kSecondArmy = 0x01000000;
  const Plan second =
    PlanUndo({Of(4, true, {Order(kSecondArmy, "Move", 10, 10)}, {Order(kSecondArmy + 1, "Move", 20, 20)})}, kNoTargets);
  WXT_CHECK(Removed(second) == (std::vector<int>{kSecondArmy + 1}));
  const std::vector<Click> back = Clicks(second);
  WXT_CHECK(back.size() == 1 && back[0].place.x == 10);
}

WXT_TEST(GriffUndoWhichOrdersAreAGesturesToTakeBack)
{
  WXT_CHECK(griffundo::Takeable("Move", false));
  WXT_CHECK(griffundo::Takeable("BuildMobile", false));
  WXT_CHECK(griffundo::Takeable("Script", true));
  WXT_CHECK(!griffundo::Takeable("Script", false));
  WXT_CHECK(!griffundo::Takeable("None", false));
  WXT_CHECK(!griffundo::Takeable("BuildFactory", false));
  WXT_CHECK(!griffundo::Takeable("Upgrade", false));
}

WXT_TEST(GriffUndoTheWayOfEachOrder)
{
  const auto way = [](const char* type) { return WayOf(Order(1, type, 0, 0)); };
  WXT_CHECK_EQ(way("Move"), Way::kMove);
  WXT_CHECK_EQ(way("FormMove"), Way::kMove);
  WXT_CHECK_EQ(way("AggressiveMove"), Way::kAttackMove);
  WXT_CHECK_EQ(way("FormAggressiveMove"), Way::kAttackMove);
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

WXT_TEST(GriffUndoWhichOrdersFAFDrawsAWaypointFor)
{
  for (const char* type : {"Move", "AggressiveMove", "Patrol", "Attack", "Guard", "Repair", "Reclaim", "Capture", "BuildMobile",
                           "TransportLoadUnits", "Ferry", "Teleport"}) {
    WXT_CHECK(griffundo::Removable(Order(1, type, 0, 0)));
  }
  for (const char* type : {"Stop", "Dive", "BuildSiloTactical", "BuildSiloNuke", "BuildFactory", "Upgrade", "Script", "KillSelf",
                           "DestroySelf", "Pause"}) {
    WXT_CHECK(!griffundo::Removable(Order(1, type, 0, 0)));
  }
}
