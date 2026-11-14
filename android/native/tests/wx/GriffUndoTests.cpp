// The touch scheme Griff's undo (include/GriffUndo.h): which commands are taken out, which lost orders are given again and
// with which keys, what cannot, and the pixel of each aimed click. Queues as the program reads them in a game: each command
// by its own id, the UI's from 0, the sim's own with its source byte 0x80; a factory's own queue and its rally route as two
// units with its id.

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
  using griffundo::Pending;
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
  /// What FAF's sim callbacks make of an engineer's order: the BuildMobile of a wreck's Rebuild, the storages of an
  /// extractor's rings.
  constexpr int kRebuild = static_cast<int>(0x80000041u);
  constexpr int kRing = static_cast<int>(0x80000031u);
  constexpr int kRingOther = static_cast<int>(0x80000032u);
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

  /// FAF's attack-move button's script at a place.
  Entry AttackMoveScript(const int id, const float x, const float z)
  {
    return Task(Order(id, "Script", x, z), "AttackMove");
  }

  /// An order of the queue as the gesture began that the sim had not taken then, and what the program read of it after.
  Entry NotTaken(Entry entry, const Pending pending)
  {
    entry.pending = pending;
    return entry;
  }

  /// A unit's queue as the program gives it where it read no view of the sim's as the replacement landed: `taken` is
  /// `before`.
  Unit Of(const int id, const bool replaced, std::vector<Entry> before, std::vector<Entry> now)
  {
    Unit unit;
    unit.id = id;
    unit.replaced = replaced;
    unit.before = std::move(before);
    unit.taken = unit.before;
    unit.now = std::move(now);
    return unit;
  }

  /// What the sim held of the queue when the gesture's replacement landed.
  Unit Taken(Unit unit, std::vector<Entry> taken)
  {
    unit.taken = std::move(taken);
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

  /// The commands the gesture's orders put into the queue.
  Unit Given(Unit unit, std::vector<Entry> given)
  {
    unit.given = std::move(given);
    return unit;
  }

  /// `taken` is a read the sim may have ticked after before the replacement's beat.
  Unit ReadTicksAgo(Unit unit)
  {
    unit.stale = true;
    return unit;
  }

  /// Whether one of the plan's clicks gives this way at this place.
  bool ClickAt(const Plan& plan, const Way way, const float x, const float z)
  {
    for (const griffundo::Group& group : plan.groups) {
      for (const Click& click : group.clicks) {
        if (click.way == way && click.place.x == x && click.place.z == z) {
          return true;
        }
      }
    }
    return false;
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
  // The attack-move with Plus: the sim's AggressiveMove goes; FAF's script beside it stays behind the building, which FAF
  // draws no waypoint of to take out, and counts.
  const Plan sims = PlanUndo(
    {Of(4, false, {building}, {building, Task(Order(6, "Script", 30, 30), "AttackMove"), Order(kSimId, "AggressiveMove", 30, 30)})},
    kNoTargets
  );
  WXT_CHECK(Removed(sims) == (std::vector<int>{kSimId}));
  WXT_CHECK(sims.groups.empty());
  WXT_CHECK_EQ(sims.dropped, 1);
}

// The verifier's (finding 5). An attack-move appended behind the engineer's building: the sim's AggressiveMove goes, FAF's
// script stays behind the building, the queue's last command until the building is done, and FAF anchors a patrol given with
// Shift at its place (CWldSessionCommands.cpp ResolveGroupMoveAnchorOrDetectPatrol takes the last queued helper of any type).
// FAF draws no waypoint for it, so it cannot go: it counts, and the toast says in part.
WXT_TEST(GriffUndoTheAttackMovesScriptLeftBehindAUnitsOwnOrdersCounts)
{
  const Entry building = Named(Order(3, "BuildMobile", 10, 10), "ueb1101");
  const Entry script = Task(Order(6, "Script", 30, 30), "AttackMove");
  const Entry aggressive = Order(kSimId, "AggressiveMove", 30, 30);
  const Plan busy = PlanUndo({Of(4, false, {building}, {building, script, aggressive})}, kNoTargets);
  WXT_CHECK(Removed(busy) == (std::vector<int>{kSimId}));
  WXT_CHECK(busy.groups.empty());
  WXT_CHECK_EQ(busy.dropped, 1);
  // On an idle unit the script heads the queue and FAF's task is done with it at the next tick: nothing stays, nothing counts.
  const Plan idle = PlanUndo({Of(5, false, {}, {script, aggressive})}, kNoTargets);
  WXT_CHECK(Removed(idle) == (std::vector<int>{kSimId}));
  WXT_CHECK_EQ(idle.dropped, 0);
  // A replacing attack-move's script heads the emptied queue as well.
  const Plan replaced = PlanUndo({Of(6, true, {Order(1, "Move", 5, 5)}, {script, aggressive})}, kNoTargets);
  WXT_CHECK(Removed(replaced) == (std::vector<int>{kSimId}));
  WXT_CHECK_EQ(replaced.dropped, 0);
  WXT_CHECK(Clicks(replaced).size() == 1);
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

WXT_TEST(GriffUndoWhatAReplacedUnitFinishedBeforeTheReplacementLandedIsNotGivenAgain)
{
  // A network game: the sim ran the old queue a lag on, and the tank reached A before the replacing move landed.
  const Entry a = Order(1, "Move", 10, 10);
  const Entry b = Order(2, "Move", 20, 20);
  const Plan plan = PlanUndo({Taken(Of(4, true, {a, b}, {Order(9, "Move", 50, 50)}), {b})}, kNoTargets);
  WXT_CHECK(Removed(plan) == (std::vector<int>{9}));
  WXT_CHECK_EQ(plan.dropped, 0);
  const std::vector<Click> clicks = Clicks(plan);
  WXT_CHECK(clicks.size() == 1 && clicks[0].way == Way::kMove && clicks[0].place.x == 20 && clicks[0].shift);
  // What the sim held then that the gesture began without (its own, as a rally point it refilled) is none of the unit's to
  // give back; only the queue as it began comes back, in its order.
  const Entry sims = Order(kSimId, "Move", 30, 30);
  const std::vector<Click> again = Clicks(PlanUndo({Taken(Of(4, true, {a, b}, {Order(9, "Move", 50, 50)}), {b, sims, a})}, kNoTargets));
  WXT_CHECK(again.size() == 2 && again[0].place.x == 10 && again[1].place.x == 20);
  // A unit that finished all of it: nothing comes back, nothing counts.
  const Plan done = PlanUndo({Taken(Of(4, true, {a, b}, {Order(9, "Move", 50, 50)}), {})}, kNoTargets);
  WXT_CHECK(done.groups.empty());
  WXT_CHECK_EQ(done.dropped, 0);
  // Appended to, the unit gives nothing back whatever the sim held.
  const Plan appended = PlanUndo({Taken(Of(4, false, {a, b}, {b, Order(9, "Move", 50, 50)}), {b})}, kNoTargets);
  WXT_CHECK(Removed(appended) == (std::vector<int>{9}));
  WXT_CHECK(appended.groups.empty());
}

// A characterization of PlanUndo with a factory's two queues as two Units of one id: it pins nothing of which queue an order
// went into, which the program tells by the queues' newest edits (griffundo::TookOrder, pinned below), and passes on the
// rules before c907f4753 as well.
WXT_TEST(GriffUndoCharacterizationAFactorysTwoQueuesPlanAsTwoUnitsOfOneId)
{
  // FAF's orders panel's Stop on a land factory with five tanks queued and a rally point: ISSUE_Command clears the factory's
  // own queue (its build queue), not the route. What it lost cannot come back by a click: it counts, the route stays. A
  // factory's build as the program reads it: no place, no building's name (EntryOf names a BuildMobile's only).
  const Entry route = Order(kSimId, "Move", 30, 31);
  std::vector<Entry> queued;
  for (int at = 0; at < 5; ++at) {
    queued.push_back(Order(10 + at, "BuildFactory", kNone, kNone));
  }
  const Plan stopped = PlanUndo({Of(1, true, queued, {}), Refilled(Of(1, false, {route}, {route}))}, kNoTargets);
  WXT_CHECK(stopped.removals.empty());
  WXT_CHECK(stopped.groups.empty());
  WXT_CHECK_EQ(stopped.dropped, 5);
  // An attack-move to tanks and the factory: its script replaced the factory's own queue too, which heads it.
  const Entry script = Task(Order(6, "Script", 40, 40), "AttackMove");
  const Plan attack = PlanUndo({Of(1, true, queued, {script}), Refilled(Of(1, false, {route}, {route}))}, kNoTargets);
  WXT_CHECK(attack.removals.empty());
  WXT_CHECK_EQ(attack.dropped, 5);
  // Its rally point replaced while its own queue got an assist: each queue comes back by its own way, in a run each.
  const Entry assist = On(Order(7, "Guard", 60, 60), 88);
  const Plan both = PlanUndo({Of(1, false, queued, {queued[0], queued[1], queued[2], queued[3], queued[4], assist}),
                              Refilled(Of(1, true, {route}, {Order(8, "Move", 20, 20)}))},
                             kNoTargets);
  WXT_CHECK(Removed(both) == (std::vector<int>{7}));
  WXT_CHECK_EQ(both.dropped, 0);
  WXT_CHECK_EQ(both.groups.size(), std::size_t{1});
  if (both.groups.size() == 1) {
    WXT_CHECK(both.groups[0].units == (std::vector<int>{1}));
    WXT_CHECK(both.groups[0].clicks.size() == 1 && both.groups[0].clicks[0].place.x == 30 && !both.groups[0].clicks[0].shift);
  }
}

// Review finding pending-attack-move-taken-out, appended. The tank's own attack-move P (FAF's script, then its AttackMove
// callback's AggressiveMove at the script's place) was not taken by the sim yet as the next gesture began, which appended a
// move G with Plus: the sim made P's AggressiveMove after the program read `before`. It is the tank's own and stays; G goes.
WXT_TEST(GriffUndoAPendingAttackMovesAggressiveMoveStaysWhereTheGestureAppended)
{
  const Entry scriptP = NotTaken(AttackMoveScript(5, 30, 30), Pending::kUnread);
  const Entry aggressiveP = Order(kSimId, "AggressiveMove", 30, 30);
  const Entry g = Order(9, "Move", 50, 50);
  // An idle tank: P's script ran off at the head once the sim ticked.
  const Plan idle = PlanUndo({Of(4, false, {scriptP}, {aggressiveP, g})}, kNoTargets);
  WXT_CHECK(Removed(idle) == (std::vector<int>{9}));
  WXT_CHECK_EQ(idle.dropped, 0);
  WXT_CHECK_EQ(idle.unsure, 0);
  // A tank busy with X: P's script stays behind X, its AggressiveMove behind the script.
  const Entry x = Order(1, "Move", 10, 10);
  const Plan busy = PlanUndo({Of(4, false, {x, scriptP}, {x, scriptP, aggressiveP, g})}, kNoTargets);
  WXT_CHECK(Removed(busy) == (std::vector<int>{9}));
  WXT_CHECK_EQ(busy.dropped, 0);
  WXT_CHECK_EQ(busy.unsure, 0);
  // The gesture's own attack-move at another place (P's x, another z): its AggressiveMove goes, P's stays, all told apart; also
  // where P's is done since.
  const Entry scriptElsewhere = AttackMoveScript(9, 30, 40);
  const Entry aggressiveG = Order(kSimOther, "AggressiveMove", 30, 40);
  const Plan elsewhere =
    PlanUndo({Given(Of(4, false, {scriptP}, {aggressiveP, scriptElsewhere, aggressiveG}), {scriptElsewhere})}, kNoTargets);
  WXT_CHECK(Removed(elsewhere) == (std::vector<int>{kSimOther}));
  WXT_CHECK_EQ(elsewhere.unsure, 0);
  const Plan done = PlanUndo({Given(Of(4, false, {scriptP}, {aggressiveG}), {scriptElsewhere})}, kNoTargets);
  WXT_CHECK(Removed(done) == (std::vector<int>{kSimOther}));
  WXT_CHECK_EQ(done.unsure, 0);
  // An AggressiveMove the tank held at P's place already, of an earlier attack-move, is not the one the sim made of P.
  const Entry earlier = Order(kSimOther, "AggressiveMove", 30, 30);
  const Plan twice = PlanUndo({Of(4, false, {earlier, scriptP}, {earlier, scriptP, aggressiveP, g})}, kNoTargets);
  WXT_CHECK(Removed(twice) == (std::vector<int>{9}));
  // Two pending attack-moves at one place: each has its own AggressiveMove.
  const Entry scriptQ = NotTaken(AttackMoveScript(6, 30, 30), Pending::kUnread);
  const Entry aggressiveQ = Order(static_cast<int>(0x80000013u), "AggressiveMove", 30, 30);
  const Plan two = PlanUndo({Of(4, false, {scriptP, scriptQ}, {aggressiveP, scriptQ, aggressiveQ, g})}, kNoTargets);
  WXT_CHECK(Removed(two) == (std::vector<int>{9}));
  // The sim held the tank's attack-move as the gesture began, its AggressiveMove with it: the gesture's at the same place is
  // the gesture's, whose script stays behind the tank's (counted, as ever).
  const Entry held = AttackMoveScript(5, 30, 30);
  const Entry scriptG = AttackMoveScript(9, 30, 30);
  const Entry aggressiveHere = Order(static_cast<int>(0x80000014u), "AggressiveMove", 30, 30);
  const Plan gestures =
    PlanUndo({Given(Of(4, false, {x, held, aggressiveP}, {x, held, aggressiveP, scriptG, aggressiveHere}), {scriptG})}, kNoTargets);
  WXT_CHECK(Removed(gestures) == (std::vector<int>{static_cast<int>(0x80000014u)}));
  WXT_CHECK_EQ(gestures.unsure, 0);
}

// The gesture's own attack-move went to the very place of a pending one of the tank's: once P's AggressiveMove is done and the
// gesture's script too (its first command gone), the one left there may be either's. It stays, as the tank's own may never be
// taken out, and the report says in part.
WXT_TEST(GriffUndoAnAggressiveMoveWhereBothThePendingAttackMoveAndTheGesturesWentCannotBeTold)
{
  const Entry scriptP = NotTaken(AttackMoveScript(5, 30, 30), Pending::kUnread);
  const Entry scriptG = AttackMoveScript(9, 30, 30);
  const Entry left = Order(kSimOther, "AggressiveMove", 30, 30);
  const Plan plan = PlanUndo({Given(Of(4, false, {scriptP}, {left}), {scriptG})}, kNoTargets);
  WXT_CHECK(Removed(plan).empty());
  WXT_CHECK_EQ(plan.dropped, 0);
  WXT_CHECK_EQ(plan.unsure, 1);
}

// Review finding appended-keeps-gestures-own (b). While the gesture's script still stands, the queue's order tells all: what
// stands behind it is the gesture's, its AggressiveMove at the very place of the tank's pending attack-move too, and what stands
// ahead of it the tank's. Alone, it goes, whole. With P's AggressiveMove still ahead of the script, that one is P's: it stays,
// the gesture's goes, and the gesture's script left behind P's counts as ever (the report says in part by it alone).
WXT_TEST(GriffUndoWhileTheGesturesScriptStandsAllBehindItIsTheGesturesAtAPendingAttackMovesPlaceToo)
{
  const Entry scriptP = NotTaken(AttackMoveScript(5, 30, 30), Pending::kUnread);
  const Entry scriptG = AttackMoveScript(9, 30, 30);
  const Entry aggressiveG = Order(kSimOther, "AggressiveMove", 30, 30);
  const Plan alone = PlanUndo({Given(Of(4, false, {scriptP}, {scriptG, aggressiveG}), {scriptG})}, kNoTargets);
  WXT_CHECK(Removed(alone) == (std::vector<int>{kSimOther}));
  WXT_CHECK_EQ(alone.dropped, 0);
  WXT_CHECK_EQ(alone.unsure, 0);
  const Entry aggressiveP = Order(kSimId, "AggressiveMove", 30, 30);
  const Plan both = PlanUndo({Given(Of(4, false, {scriptP}, {aggressiveP, scriptG, aggressiveG}), {scriptG})}, kNoTargets);
  WXT_CHECK(Removed(both) == (std::vector<int>{kSimOther}));
  WXT_CHECK_EQ(both.dropped, 1);
  WXT_CHECK_EQ(both.unsure, 0);
}

// Review finding pending-attack-move-taken-out, replaced: the tank's pending attack-move P under a replacing gesture G.
// Whatever the program read of the sim (P's script and AggressiveMove; the AggressiveMove alone, once the script ran off or
// Clear's callback replaced it) or did not read (P went to the sim in the replacement's own beat), P comes back as one
// attack-move at its place.
WXT_TEST(GriffUndoAPendingAttackMoveTheGestureReplacedComesBackAsOneAttackMove)
{
  const Entry aggressiveP = Order(kSimId, "AggressiveMove", 30, 30);
  const Entry g = Order(9, "Move", 50, 50);
  const Entry read = NotTaken(AttackMoveScript(5, 30, 30), Pending::kRead);
  for (const std::vector<Entry>& taken : {std::vector<Entry>{read, aggressiveP}, std::vector<Entry>{aggressiveP}}) {
    const Plan plan = PlanUndo({Taken(Of(4, true, {read}, {g}), taken)}, kNoTargets);
    WXT_CHECK(Removed(plan) == (std::vector<int>{9}));
    WXT_CHECK(Clicks(plan).size() == 1 && ClickAt(plan, Way::kAttackMove, 30, 30));
    WXT_CHECK_EQ(plan.dropped, 0);
    WXT_CHECK_EQ(plan.unsure, 0);
  }
  // Read gone before the replacement landed: the tank did it.
  const Plan done = PlanUndo({Taken(Of(4, true, {read}, {g}), {})}, kNoTargets);
  WXT_CHECK(done.groups.empty());
  WXT_CHECK_EQ(done.unsure, 0);
  // Taken by the sim in the replacement's own beat, just before it: never read, in the queue the replacement emptied.
  const Entry withReplacement = NotTaken(AttackMoveScript(5, 30, 30), Pending::kWithReplacement);
  const Plan inBeat = PlanUndo({Taken(Of(4, true, {withReplacement}, {g}), {})}, kNoTargets);
  WXT_CHECK(Clicks(inBeat).size() == 1 && ClickAt(inBeat, Way::kAttackMove, 30, 30));
  WXT_CHECK_EQ(inBeat.unsure, 0);
  // Taken in a beat before the replacement's that no read came after: it comes back, and the report says in part.
  const Entry unread = NotTaken(AttackMoveScript(5, 30, 30), Pending::kUnread);
  const Plan unseen = PlanUndo({Taken(Of(4, true, {unread}, {g}), {})}, kNoTargets);
  WXT_CHECK(Clicks(unseen).size() == 1 && ClickAt(unseen, Way::kAttackMove, 30, 30));
  WXT_CHECK_EQ(unseen.unsure, 1);
  // The gesture's replacing attack-move at the same place: its AggressiveMove goes, P's attack-move comes back.
  const Entry aggressiveG = Order(kSimOther, "AggressiveMove", 30, 30);
  const Plan same =
    PlanUndo({Given(Taken(Of(4, true, {withReplacement}, {aggressiveG}), {}), {AttackMoveScript(9, 30, 30)})}, kNoTargets);
  WXT_CHECK(Removed(same) == (std::vector<int>{kSimOther}));
  WXT_CHECK(ClickAt(same, Way::kAttackMove, 30, 30));
  WXT_CHECK_EQ(same.unsure, 0);
}

// Review finding pending-order-missing-from-taken. Tank T holds [A]; a Plus tap appends P, a tap without Plus replaces with G
// before the sim took P. In a paused game both go to the sim in one beat, P first (IssueCommand marks no activity): no read
// of the sim's queue shows P, and G's Reset wiped P's edit, so `taken` is [A]. T held [A, P] as the gesture began.
WXT_TEST(GriffUndoAnOrderPendingAsTheGestureBeganComesBackByTheBeatTheSimTookItIn)
{
  const Entry a = Order(1, "Move", 10, 10);
  const Entry p = Order(2, "Move", 20, 20);
  const Entry g = Order(9, "Move", 50, 50);
  const Plan plan = PlanUndo({Taken(Of(4, true, {a, NotTaken(p, Pending::kWithReplacement)}, {g}), {a})}, kNoTargets);
  WXT_CHECK(Removed(plan) == (std::vector<int>{9}));
  const std::vector<Click> clicks = Clicks(plan);
  WXT_CHECK(clicks.size() == 2 && clicks[0].place.x == 10 && clicks[1].place.x == 20 && AllWithShift(clicks));
  WXT_CHECK_EQ(plan.dropped, 0);
  WXT_CHECK_EQ(plan.unsure, 0);
  // P replacing A (its Reset pending too): T held [P] as the gesture began; A, which the sim held until then, is not T's.
  const Plan replacing = PlanUndo({Taken(Of(4, true, {NotTaken(p, Pending::kWithReplacement)}, {g}), {a})}, kNoTargets);
  const std::vector<Click> back = Clicks(replacing);
  WXT_CHECK(back.size() == 1 && back[0].place.x == 20);
  WXT_CHECK_EQ(replacing.unsure, 0);
  // P went to the sim in a beat before G's, whose syncs came in one frame: P comes back, and the report says in part.
  const Plan unread = PlanUndo({Taken(Of(4, true, {a, NotTaken(p, Pending::kUnread)}, {g}), {a})}, kNoTargets);
  WXT_CHECK_EQ(Clicks(unread).size(), std::size_t{2});
  WXT_CHECK_EQ(unread.unsure, 1);
  // A read came after the sim took P: what the sim held when G landed tells, as for A.
  const Plan read = PlanUndo({Taken(Of(4, true, {a, NotTaken(p, Pending::kRead)}, {g}), {a, p})}, kNoTargets);
  WXT_CHECK_EQ(Clicks(read).size(), std::size_t{2});
  WXT_CHECK_EQ(read.unsure, 0);
  const std::vector<Click> gone = Clicks(PlanUndo({Taken(Of(4, true, {a, NotTaken(p, Pending::kRead)}, {g}), {a})}, kNoTargets));
  WXT_CHECK(gone.size() == 1 && gone[0].place.x == 10);
  // A Stop pending in a beat no read came after leaves nothing to give back, nor to be unsure of.
  const Plan stop = PlanUndo({Taken(Of(4, true, {NotTaken(Order(3, "Stop", kNone, kNone), Pending::kUnread)}, {g}), {})}, kNoTargets);
  WXT_CHECK(stop.groups.empty());
  WXT_CHECK_EQ(stop.unsure, 0);
}

// Review finding pending-byproducts-taken-out, appended. FAF's sim makes commands of an order in the beat it takes it: an
// engineer's Repair on a wreck (88) brings FAF's Rebuild callback, which clears the Repair and gives the BuildMobile that
// rebuilds the wreck's structure (commandmode.lua OnRepairIssued, wreckage.lua Rebuild); a Guard on an extractor (77) brings
// its rings of storages (capping.lua AssistToCap, RingWithStorages, which appends them). The engineer's own order was pending
// as a Plus tap appended move 9: the sim took it, and made those, before 9. They are the engineer's and stay; 9 goes, whole.
WXT_TEST(GriffUndoWhatFAFMadeOfAPendingOrderStaysAheadOfTheGesturesCommandsWhereItAppended)
{
  const Entry g = Order(9, "Move", 50, 50);
  const Entry repair = NotTaken(On(Order(5, "Repair", 40, 40), 88), Pending::kUnread);
  const Entry rebuild = Named(Order(kRebuild, "BuildMobile", 40, 40), "ueb1301");
  const Plan rebuilt = PlanUndo({Given(Of(4, false, {repair}, {rebuild, g}), {g})}, kNoTargets);
  WXT_CHECK(Removed(rebuilt) == (std::vector<int>{9}));
  WXT_CHECK_EQ(rebuilt.dropped, 0);
  WXT_CHECK_EQ(rebuilt.unsure, 0);
  // The rings behind the Guard, and without it where the sim's Guard of an idle extractor is done at once.
  const Entry guard = NotTaken(On(Order(5, "Guard", 30, 30), 77), Pending::kUnread);
  const Entry ring = Named(Order(kRing, "BuildMobile", 32, 30), "ueb1106");
  const Entry ringOther = Named(Order(kRingOther, "BuildMobile", 30, 32), "ueb1106");
  for (const std::vector<Entry>& now : {std::vector<Entry>{guard, ring, ringOther, g}, std::vector<Entry>{ring, ringOther, g}}) {
    const Plan ringed = PlanUndo({Given(Of(4, false, {guard}, now), {g})}, kNoTargets);
    WXT_CHECK(Removed(ringed) == (std::vector<int>{9}));
    WXT_CHECK_EQ(ringed.unsure, 0);
  }
  // The gesture's own attack-move behind the rebuild: its AggressiveMove goes, the rebuild stays, and the script left behind
  // the rebuild counts, as ever.
  const Entry script = AttackMoveScript(9, 60, 60);
  const Entry aggressive = Order(kSimOther, "AggressiveMove", 60, 60);
  const Plan attack = PlanUndo({Given(Of(4, false, {repair}, {rebuild, script, aggressive}), {script})}, kNoTargets);
  WXT_CHECK(Removed(attack) == (std::vector<int>{kSimOther}));
  WXT_CHECK_EQ(attack.dropped, 1);
  WXT_CHECK_EQ(attack.unsure, 0);
}

// Review finding exact-anchor-unpinned. Nothing of the tank's was pending as the gesture appended move 9: the AggressiveMove
// FAF's callback made of the attack-move script the sim held came a beat after the program read the queue, ahead of 9, which
// still stands. The queue's order alone tells it is the tank's (the third round's open item 2): it stays, only 9 goes, whole. So
// does a ring of an extractor a held Guard targets that came a beat late.
WXT_TEST(GriffUndoTheGesturesFirstCommandStandingKeepsWhatStandsAheadOfItWithNothingPending)
{
  const Entry x = Order(1, "Move", 10, 10);
  const Entry script = AttackMoveScript(5, 30, 30);
  const Entry late = Order(kSimId, "AggressiveMove", 30, 30);
  const Entry g = Order(9, "Move", 50, 50);
  const Plan plan = PlanUndo({Given(Of(4, false, {x, script}, {x, script, late, g}), {g})}, kNoTargets);
  WXT_CHECK(Removed(plan) == (std::vector<int>{9}));
  WXT_CHECK_EQ(plan.dropped, 0);
  WXT_CHECK_EQ(plan.unsure, 0);
  const Entry guard = On(Order(2, "Guard", 30, 30), 77);
  const Entry ring = Named(Order(kRing, "BuildMobile", 32, 30), "ueb1106");
  const Plan ringed = PlanUndo({Given(Of(4, false, {guard}, {guard, ring, g}), {g})}, kNoTargets);
  WXT_CHECK(Removed(ringed) == (std::vector<int>{9}));
  WXT_CHECK_EQ(ringed.unsure, 0);
}

// Review finding appended-keeps-gestures-own (a). Where the gesture's first command is gone, only a pending order FAF's UI
// answers with commands of the sim's own may have made what stands ahead of its next (AnswerOf). FAF makes nothing of a move
// (commandmode.lua has no answer to it): the gesture's AggressiveMove, whose script ran off at the head, goes as it does where
// the move was held, whole. A pending Guard on a unit, a Repair on one or an Attack on the ground may have made it: it stays,
// counted. A Guard the program knows FAF answered with nothing (its unit's kind, no Ctrl) is none.
WXT_TEST(GriffUndoOnlyAPendingOrderFafAnswersMayHaveMadeWhatStandsAheadOfTheGesturesNext)
{
  const Entry script = AttackMoveScript(9, 60, 60);
  const Entry aggressive = Order(kSimOther, "AggressiveMove", 60, 60);
  const Entry move = NotTaken(Order(1, "Move", 10, 10), Pending::kUnread);
  const Plan moved = PlanUndo({Given(Of(4, false, {move}, {aggressive}), {script})}, kNoTargets);
  WXT_CHECK(Removed(moved) == (std::vector<int>{kSimOther}));
  WXT_CHECK_EQ(moved.unsure, 0);
  for (const Entry& maker : {On(Order(1, "Guard", 30, 30), 77), On(Order(1, "Repair", 40, 40), 88), Order(1, "Attack", 50, 50)}) {
    const Plan plan = PlanUndo({Given(Of(4, false, {NotTaken(maker, Pending::kUnread)}, {aggressive}), {script})}, kNoTargets);
    WXT_CHECK(Removed(plan).empty());
    WXT_CHECK_EQ(plan.unsure, 1);
  }
  Entry quiet = On(Order(1, "Guard", 30, 30), 66);
  quiet.answered = false;
  const Plan unanswered = PlanUndo({Given(Of(4, false, {NotTaken(quiet, Pending::kUnread)}, {aggressive}), {script})}, kNoTargets);
  WXT_CHECK(Removed(unanswered) == (std::vector<int>{kSimOther}));
  WXT_CHECK_EQ(unanswered.unsure, 0);
  // A Guard the sim held as the gesture began: what FAF made of it the queue as the gesture began holds already.
  const Entry held = On(Order(1, "Guard", 30, 30), 77);
  const Plan guarded = PlanUndo({Given(Of(4, false, {held}, {held, aggressive}), {script})}, kNoTargets);
  WXT_CHECK(Removed(guarded) == (std::vector<int>{kSimOther}));
  WXT_CHECK_EQ(guarded.unsure, 0);
}

// Where the gesture's first command is gone (its attack-move's script, done at the queue's head; an attack whose target
// died), what stands ahead of its next may be what that one made: it goes as the gesture's where nothing of the unit's was
// pending, and where one of the unit's pending orders may have made it as well, whose it is cannot be told: it stays,
// counted. The next command of the gesture's goes.
WXT_TEST(GriffUndoWhereTheGesturesFirstCommandIsGoneWhatStandsAheadOfItsNextCounts)
{
  const Entry script = AttackMoveScript(8, 60, 60);
  const Entry aggressive = Order(kSimOther, "AggressiveMove", 60, 60);
  const Entry g = Order(9, "Move", 50, 50);
  const Plan gestures = PlanUndo({Given(Of(4, false, {}, {aggressive, g}), {script, g})}, kNoTargets);
  WXT_CHECK(Removed(gestures) == (std::vector<int>{kSimOther, 9}));
  WXT_CHECK_EQ(gestures.unsure, 0);
  const Entry repair = NotTaken(On(Order(5, "Repair", 40, 40), 88), Pending::kUnread);
  const Entry rebuild = Named(Order(kRebuild, "BuildMobile", 40, 40), "ueb1301");
  const Plan either = PlanUndo({Given(Of(4, false, {repair}, {rebuild, g}), {script, g})}, kNoTargets);
  WXT_CHECK(Removed(either) == (std::vector<int>{9}));
  WXT_CHECK_EQ(either.unsure, 1);
  // None of the gesture's commands left: what the queue holds that it did not hold as the gesture began cannot be told.
  const Entry attack = On(Order(9, "Attack", 50, 50), 66);
  const Plan none = PlanUndo({Given(Of(4, false, {repair}, {rebuild}), {attack})}, kNoTargets);
  WXT_CHECK(Removed(none).empty());
  WXT_CHECK_EQ(none.unsure, 1);
}

// Review finding pending-byproducts-taken-out, replaced. The engineer's Repair on wreck 88 with Clear was pending as a
// replacing move 9 began, and a read came after the sim took it (kRead): the sim held the rebuild alone, as FAF's Rebuild
// cleared the Repair, when the replacement took it. It comes back by FAF's build mode at its place, by its name, whole; in
// its place in the queue, after an order of the unit's that stood ahead of it, and also behind one the sim kept.
WXT_TEST(GriffUndoWhatFAFMadeOfAPendingOrderComesBackByItsOwnWayWhereTheGestureReplaced)
{
  Targets wreck;
  wreck[88] = Place{40, 18, 40};
  const Entry repair = NotTaken(On(Order(5, "Repair", 40, 40), 88), Pending::kRead);
  const Entry rebuild = Named(Order(kRebuild, "BuildMobile", 40, 40), "ueb1301");
  const Entry g = Order(9, "Move", 50, 50);
  const Plan plan = PlanUndo({Given(Taken(Of(4, true, {repair}, {g}), {rebuild}), {g})}, wreck);
  WXT_CHECK(Removed(plan) == (std::vector<int>{9}));
  const std::vector<Click> clicks = Clicks(plan);
  WXT_CHECK(clicks.size() == 1 && clicks[0].way == Way::kBuild && clicks[0].blueprint == "ueb1301" && clicks[0].shift);
  WXT_CHECK(clicks.size() == 1 && clicks[0].place.x == 40 && clicks[0].place.z == 40);
  WXT_CHECK_EQ(plan.dropped, 0);
  WXT_CHECK_EQ(plan.unsure, 0);
  const Entry a = Order(1, "Move", 10, 10);
  const Plan behindPlan = PlanUndo({Given(Taken(Of(4, true, {a, repair}, {g}), {a, rebuild}), {g})}, wreck);
  const std::vector<Click> behind = Clicks(behindPlan);
  WXT_CHECK(behind.size() == 2 && behind[0].way == Way::kMove && behind[1].way == Way::kBuild);
  WXT_CHECK_EQ(behindPlan.unsure, 0);
  const Entry enhancing = Order(2, "Script", kNone, kNone);
  const Plan kept = PlanUndo({Given(Taken(Of(4, true, {enhancing, repair}, {enhancing, g}), {enhancing, rebuild}), {g})}, wreck);
  WXT_CHECK(Clicks(kept).size() == 1 && ClickAt(kept, Way::kBuild, 40, 40));
  // A command of the player's that the queue as the gesture began did not hold is none of what the sim made.
  const Plan players = PlanUndo({Given(Taken(Of(4, true, {repair}, {g}), {Order(3, "Move", 70, 70), rebuild}), {g})}, wreck);
  WXT_CHECK(Clicks(players).size() == 1 && ClickAt(players, Way::kBuild, 40, 40));
  // What the sim holds still through the replacement it kept: nothing comes back.
  const Plan still = PlanUndo({Given(Taken(Of(4, true, {repair}, {rebuild, g}), {rebuild}), {g})}, wreck);
  WXT_CHECK(still.groups.empty());
  // A pending attack-move's AggressiveMove stands for its script, which comes back as one attack-move, before the rebuild
  // behind it.
  const Entry script = NotTaken(AttackMoveScript(6, 30, 31), Pending::kRead);
  const Plan bothPlan =
    PlanUndo({Given(Taken(Of(4, true, {script, repair}, {g}), {Order(kSimId, "AggressiveMove", 30, 31), rebuild}), {g})}, wreck);
  const std::vector<Click> both = Clicks(bothPlan);
  WXT_CHECK(both.size() == 2 && both[0].way == Way::kAttackMove && both[1].way == Way::kBuild);
  WXT_CHECK_EQ(bothPlan.unsure, 0);
}

// Only a read after the sim took one of the unit's pending orders shows what the sim made of it; before that, a command of
// the sim's own in `taken` is its own doing (a rally point it refills): the Repair the sim took in the replacement's own beat
// comes back by its repair, which FAF's Rebuild answers anew, and nothing else does. From the gesture's first command in
// `taken` on (an order of its that landed before its replacement), what the sim made is the gesture's.
WXT_TEST(GriffUndoOnlyAReadAfterTheSimTookAPendingOrderShowsWhatItMadeOfIt)
{
  Targets wreck;
  wreck[88] = Place{40, 18, 40};
  const Entry rebuild = Named(Order(kRebuild, "BuildMobile", 40, 40), "ueb1301");
  const Entry g = Order(9, "Move", 50, 50);
  const Entry sims = Order(kSimOther, "Move", 30, 30);
  const Entry inBeat = NotTaken(On(Order(5, "Repair", 40, 40), 88), Pending::kWithReplacement);
  const Plan plan = PlanUndo({Given(Taken(Of(4, true, {inBeat}, {g}), {sims}), {g})}, wreck);
  const std::vector<Click> clicks = Clicks(plan);
  WXT_CHECK(clicks.size() == 1 && clicks[0].way == Way::kRepair && clicks[0].target == 88);
  WXT_CHECK_EQ(plan.unsure, 0);
  // A read attack-move's AggressiveMove stands for its script, and a refill beside it is the sim's.
  const Entry script = NotTaken(AttackMoveScript(6, 30, 31), Pending::kRead);
  const Plan attack = PlanUndo({Given(Taken(Of(4, true, {script}, {g}), {Order(kSimId, "AggressiveMove", 30, 31), sims}), {g})}, kNoTargets);
  WXT_CHECK(Clicks(attack).size() == 1 && ClickAt(attack, Way::kAttackMove, 30, 31));
  // The gesture's Guard on extractor 77 with Plus landed first and FAF ringed it; its move 10 replaced the queue after.
  const Entry repair = NotTaken(On(Order(5, "Repair", 40, 40), 88), Pending::kRead);
  const Entry guard = On(Order(9, "Guard", 30, 30), 77);
  const Entry ring = Named(Order(kRing, "BuildMobile", 32, 30), "ueb1106");
  const Plan gestures =
    PlanUndo({Given(Taken(Of(4, true, {repair}, {Order(10, "Move", 50, 50)}), {rebuild, guard, ring}), {guard, Order(10, "Move", 50, 50)})},
             wreck);
  WXT_CHECK(Clicks(gestures).size() == 1 && ClickAt(gestures, Way::kBuild, 40, 40));
  // A read pending move, which FAF's UI answers with nothing: a command of the sim's own beside it is the sim's doing too.
  const Entry p = NotTaken(Order(3, "Move", 20, 20), Pending::kRead);
  const Plan move = PlanUndo({Given(Taken(Of(4, true, {p}, {g}), {sims, p}), {g})}, kNoTargets);
  WXT_CHECK(Clicks(move).size() == 1 && ClickAt(move, Way::kMove, 20, 20));
}

// Review finding give-back-guard-is-a-capping-tap. What the sim made of one of the unit's pending orders that stands right
// behind it in `taken`, where that order comes back too, comes back by its own way, never lost. FAF's capping rings nothing
// for the Guard given back (griff.lua holds it off while the undo's clicks run, as a PC player who wants only the Guard turns
// FAF's capping options off): the extractor's rings come back once, whole. FAF's Rebuild makes the rebuild again of the Repair
// given back: a second order of it, which finds the building there, and it counts. Where the order is done or gone, what it
// made comes back exact.
WXT_TEST(GriffUndoWhatFAFMadeOfAPendingOrderThatComesBackCountsWhereFafMakesItAgain)
{
  Targets targets;
  targets[77] = Place{30, 18, 30};
  targets[88] = Place{40, 18, 40};
  const Entry guard = NotTaken(On(Order(5, "Guard", 30, 30), 77), Pending::kRead);
  const Entry ring = Named(Order(kRing, "BuildMobile", 32, 30), "ueb1106");
  const Entry ringOther = Named(Order(kRingOther, "BuildMobile", 30, 32), "ueb1106");
  const Entry g = Order(9, "Move", 50, 50);
  const Plan plan = PlanUndo({Given(Taken(Of(4, true, {guard}, {g}), {guard, ring, ringOther}), {g})}, targets);
  const std::vector<Click> clicks = Clicks(plan);
  WXT_CHECK(clicks.size() == 3 && clicks[0].way == Way::kGuard && clicks[0].target == 77 && AllWithShift(clicks));
  WXT_CHECK(ClickAt(plan, Way::kBuild, 32, 30) && ClickAt(plan, Way::kBuild, 30, 32));
  WXT_CHECK_EQ(plan.unsure, 0);
  const Plan done = PlanUndo({Given(Taken(Of(4, true, {guard}, {g}), {ring, ringOther}), {g})}, targets);
  WXT_CHECK(Clicks(done).size() == 2 && ClickAt(done, Way::kBuild, 32, 30) && ClickAt(done, Way::kBuild, 30, 32));
  WXT_CHECK_EQ(done.unsure, 0);
  const Entry repair = NotTaken(On(Order(6, "Repair", 40, 40), 88), Pending::kRead);
  const Entry rebuild = Named(Order(kRebuild, "BuildMobile", 40, 40), "ueb1301");
  const Plan rebuilt = PlanUndo({Given(Taken(Of(4, true, {repair}, {g}), {repair, rebuild}), {g})}, targets);
  WXT_CHECK(Clicks(rebuilt).size() == 2 && ClickAt(rebuilt, Way::kRepair, 40, 40) && ClickAt(rebuilt, Way::kBuild, 40, 40));
  WXT_CHECK_EQ(rebuilt.unsure, 1);
}

// Review finding with-replacement-byproducts-lost. Engineer 4's Guard on extractor 77 was pending as a replacing move 9 began,
// and the sim took it in the replacement's own beat (a paused game holds both to one beat): FAF's capping rang the extractor
// for it then (RingWithStorages beside the Guard), and 9 emptied the queue of the Guard and its rings before any read. The
// Guard comes back, but its rings no read showed, and no click of the undo's makes them again (griff.lua holds FAF's capping
// off meanwhile): it counts, and the report says in part. So does an Attack on the ground, whose press may have dragged an
// area FAF attacks about (ExtendAttackOrder), where the undo's click does not. Not a Guard FAF answered with nothing (the
// program knows its unit's kind: a factory, a tank), nor a Repair on a wreck, whose rebuild FAF's Rebuild makes again of the
// repair given back.
WXT_TEST(GriffUndoAPendingOrderTheSimTookInTheReplacementsBeatCountsWhereNoClickMakesWhatFafAnsweredItWith)
{
  Targets targets;
  targets[77] = Place{30, 18, 30};
  targets[88] = Place{40, 18, 40};
  const Entry g = Order(9, "Move", 50, 50);
  const Entry guard = NotTaken(On(Order(5, "Guard", 30, 30), 77), Pending::kWithReplacement);
  const Plan plan = PlanUndo({Given(Taken(Of(4, true, {guard}, {g}), {}), {g})}, targets);
  const std::vector<Click> clicks = Clicks(plan);
  WXT_CHECK(clicks.size() == 1 && clicks[0].way == Way::kGuard && clicks[0].target == 77 && clicks[0].shift);
  WXT_CHECK(Removed(plan) == (std::vector<int>{9}));
  WXT_CHECK_EQ(plan.dropped, 0);
  WXT_CHECK_EQ(plan.unsure, 1);
  const Entry attack = NotTaken(Order(5, "Attack", 60, 60), Pending::kWithReplacement);
  const Plan area = PlanUndo({Given(Taken(Of(4, true, {attack}, {g}), {}), {g})}, targets);
  WXT_CHECK(Clicks(area).size() == 1 && ClickAt(area, Way::kAttack, 60, 60));
  WXT_CHECK_EQ(area.unsure, 1);
  Entry quiet = guard;
  quiet.answered = false;
  const Plan factory = PlanUndo({Given(Taken(Of(4, true, {quiet}, {g}), {}), {g})}, targets);
  WXT_CHECK(Clicks(factory).size() == 1 && ClickAt(factory, Way::kGuard, 30, 30));
  WXT_CHECK_EQ(factory.unsure, 0);
  const Entry repair = NotTaken(On(Order(5, "Repair", 40, 40), 88), Pending::kWithReplacement);
  const Plan rebuilt = PlanUndo({Given(Taken(Of(4, true, {repair}, {g}), {}), {g})}, targets);
  WXT_CHECK(Clicks(rebuilt).size() == 1 && ClickAt(rebuilt, Way::kRepair, 40, 40));
  WXT_CHECK_EQ(rebuilt.unsure, 0);
  // Counted once, also where the read before the landing may be ticks old.
  const Plan old = PlanUndo({ReadTicksAgo(Given(Taken(Of(4, true, {guard}, {g}), {}), {g}))}, targets);
  WXT_CHECK_EQ(old.unsure, 1);
}

// What FAF's UI answers each order with, as commands of the sim's own in its units' queues (commandmode.lua's answers to
// OnCommandIssued, 3835): a Guard on a unit FAF's capping may ring or whose queue it may copy, once; a Repair on an entity, the
// Rebuild of the wreck, again for the repair given back; an Attack on the ground, the area its press may drag, once; nothing for
// the rest, nor for a Guard the program knows FAF answered with nothing.
WXT_TEST(GriffUndoWhatFafAnswersEachOrderWith)
{
  using griffundo::Answer;
  using griffundo::AnswerOf;
  WXT_CHECK(AnswerOf(On(Order(1, "Guard", 30, 30), 77)) == Answer::kOnce);
  Entry quiet = On(Order(1, "Guard", 30, 30), 66);
  quiet.answered = false;
  WXT_CHECK(AnswerOf(quiet) == Answer::kNone);
  WXT_CHECK(AnswerOf(On(Order(1, "Repair", 40, 40), 88)) == Answer::kAgain);
  WXT_CHECK(AnswerOf(Order(1, "Attack", 50, 50)) == Answer::kOnce);
  WXT_CHECK(AnswerOf(On(Order(1, "Attack", 50, 50), 66)) == Answer::kNone);
  WXT_CHECK(AnswerOf(Order(1, "Guard", 30, 30)) == Answer::kNone);
  WXT_CHECK(AnswerOf(Order(1, "Repair", 40, 40)) == Answer::kNone);
  for (const char* const type : {"Move", "Patrol", "AggressiveMove", "FormAttack", "BuildMobile", "Stop", "Reclaim", "Capture"}) {
    WXT_CHECK(AnswerOf(Order(1, type, 10, 10)) == Answer::kNone);
    WXT_CHECK(AnswerOf(On(Order(1, type, 10, 10), 66)) == Answer::kNone);
  }
  WXT_CHECK(AnswerOf(AttackMoveScript(1, 10, 10)) == Answer::kNone);
}

// Review finding stale-taken-trusted. Tank 4 held [A, B], both the sim's, as a replacing move 9 began. The read before the
// landing showed [A, B]; where the frame that brought the replacement's beat brought the beat before it too, the tank may have
// reached A in it: A and B come back, and each counts. A pending order a read showed counts so as well, and what the sim made
// of one; one the sim took in the replacement's own beat is exact whatever was read.
WXT_TEST(GriffUndoWhatAReadTheSimMayHaveTickedAfterShowsComesBackCounted)
{
  const Entry a = Order(1, "Move", 10, 10);
  const Entry b = Order(2, "Move", 20, 20);
  const Entry g = Order(9, "Move", 50, 50);
  const Unit unit = Given(Taken(Of(4, true, {a, b}, {g}), {a, b}), {g});
  const Plan fresh = PlanUndo({unit}, kNoTargets);
  WXT_CHECK_EQ(Clicks(fresh).size(), std::size_t{2});
  WXT_CHECK_EQ(fresh.unsure, 0);
  const Plan old = PlanUndo({ReadTicksAgo(unit)}, kNoTargets);
  WXT_CHECK_EQ(Clicks(old).size(), std::size_t{2});
  WXT_CHECK_EQ(old.unsure, 2);
  const Entry p = Order(3, "Move", 30, 30);
  const Plan read = PlanUndo({ReadTicksAgo(Given(Taken(Of(4, true, {a, NotTaken(p, Pending::kRead)}, {g}), {a, p}), {g}))}, kNoTargets);
  WXT_CHECK_EQ(read.unsure, 2);
  const Plan inBeat =
    PlanUndo({ReadTicksAgo(Given(Taken(Of(4, true, {a, NotTaken(p, Pending::kWithReplacement)}, {g}), {a}), {g}))}, kNoTargets);
  WXT_CHECK_EQ(Clicks(inBeat).size(), std::size_t{2});
  WXT_CHECK_EQ(inBeat.unsure, 1);
  Targets wreck;
  wreck[88] = Place{40, 18, 40};
  const Entry repair = NotTaken(On(Order(5, "Repair", 40, 40), 88), Pending::kRead);
  const Entry rebuild = Named(Order(kRebuild, "BuildMobile", 40, 40), "ueb1301");
  const Plan made = PlanUndo({ReadTicksAgo(Given(Taken(Of(4, true, {repair}, {g}), {rebuild}), {g}))}, wreck);
  WXT_CHECK(Clicks(made).size() == 1 && ClickAt(made, Way::kBuild, 40, 40));
  WXT_CHECK_EQ(made.unsure, 1);
}

// Whether the sim may have ticked between the last read of a replaced queue and the replacement's beat: from the game's tick
// at that read and at the read that found the replacement landed, and whether the newest sync's beat ticked.
WXT_TEST(GriffUndoAReadIsOldWhereTheSimMayHaveTickedBeforeTheReplacementsBeat)
{
  using griffundo::Stale;
  // A frame a beat: the read at tick 100 showed the beat before the replacement's, whose own beat made tick 101.
  WXT_CHECK(!Stale(100, 101, true));
  // The frame that brought the replacement's beat brought the one before it too, which made tick 101.
  WXT_CHECK(Stale(100, 102, true));
  // A paused game's beats make no tick, however many a frame brings.
  WXT_CHECK(!Stale(100, 100, false));
  // The newest beat made none (the game paused at it), one before it did.
  WXT_CHECK(Stale(100, 101, false));
}

// Review finding pendingof-untested: the orders the sim had not taken as the gesture began, from the queue's pending edits as
// FAF's UI made them: each that an Add adds, which the UI's view shows and the sim's primary links lack.
WXT_TEST(GriffUndoAnOrderTheSimHadNotTakenIsOneWhoseAddWaitsAndThePrimaryLinksLack)
{
  using griffundo::Edit;
  using griffundo::EditKind;
  using griffundo::Unsynced;
  using griffundo::UnsyncedOf;
  const Entry a = Order(1, "Move", 10, 10);
  const Entry p = Order(2, "Move", 20, 20);
  const std::vector<Unsynced> unsynced = UnsyncedOf({Edit{EditKind::kAdd, 2, 700}}, {a, p}, {a});
  WXT_CHECK_EQ(unsynced.size(), std::size_t{1});
  if (unsynced.size() == 1) {
    WXT_CHECK(unsynced[0].command == 2 && unsynced[0].beat == 700 && !unsynced[0].read);
  }
  // The primary links hold its command already: the sim has it.
  WXT_CHECK(UnsyncedOf({Edit{EditKind::kAdd, 2, 700}}, {a, p}, {a, p}).empty());
  // An Add of an order the UI's view does not show (a Remove after it took it out).
  WXT_CHECK(UnsyncedOf({Edit{EditKind::kAdd, 3, 700}, Edit{EditKind::kRemove, 3, 701}}, {a, p}, {a}).empty());
  // A removal of one the UI shows adds none, nor does a Reset.
  WXT_CHECK(UnsyncedOf({Edit{EditKind::kReset, 0, 700}, Edit{EditKind::kRemove, 2, 701}}, {a, p}, {a}).empty());
}

// Review finding pendingof-untested: what the program read of an order the sim had not taken as the gesture began, by the
// order's beat and the replacement's (its Reset at beat 705), and whether a read came after the sync reached the order's beat.
WXT_TEST(GriffUndoWhatWasReadOfAnOrderTheSimHadNotTakenGoesByItsBeat)
{
  using griffundo::PendingOf;
  const std::vector<griffundo::Unsynced> unsynced = {{2, 705, false}, {3, 704, false}, {4, 703, true}};
  WXT_CHECK_EQ(PendingOf(unsynced, 1, true, 705), Pending::kNo);
  WXT_CHECK_EQ(PendingOf(unsynced, 2, true, 705), Pending::kWithReplacement);
  WXT_CHECK_EQ(PendingOf(unsynced, 3, true, 705), Pending::kUnread);
  WXT_CHECK_EQ(PendingOf(unsynced, 4, true, 705), Pending::kRead);
  // No view of the sim's read (a queue the gesture appended to): unread, whatever its beat.
  WXT_CHECK_EQ(PendingOf(unsynced, 2, false, 705), Pending::kUnread);
  WXT_CHECK_EQ(PendingOf(unsynced, 4, false, 705), Pending::kUnread);
  WXT_CHECK_EQ(PendingOf(unsynced, 1, false, 705), Pending::kNo);
}

// Review finding factory-test-pins-nothing: which of a unit's queues an order went into, by each queue's newest edit before the
// order and after it, as FAF's UI made the edits (the program's EditOf).
WXT_TEST(GriffUndoAnOrderWentIntoTheQueueWhoseNewestEditIsItsAdd)
{
  using griffundo::Edit;
  using griffundo::EditKind;
  using griffundo::TookOrder;
  const Edit none;
  // A land factory with a build queued (its own queue's edit not taken yet) gets a rally point: ISSUE_FactoryCommand adds it
  // to the route, and the own queue's newest edit is the build's still.
  const Edit build{EditKind::kAdd, 12, 700};
  const Edit rally{EditKind::kAdd, 13, 701};
  WXT_CHECK(TookOrder(none, rally));
  WXT_CHECK(!TookOrder(build, build));
  // FAF's orders panel's Stop: ISSUE_Command empties the own queue, its Reset first, and adds the Stop last; the route's
  // newest edit is the rally point's still.
  const Edit stop{EditKind::kAdd, 14, 702};
  WXT_CHECK(TookOrder(build, stop));
  WXT_CHECK(!TookOrder(rally, rally));
  // The sync took the edits meanwhile: none waits, no order.
  WXT_CHECK(!TookOrder(build, none));
  // A removal, the player's or the undo's own release, is no order; nor is a Reset alone.
  WXT_CHECK(!TookOrder(none, Edit{EditKind::kRemove, 12, 703}));
  WXT_CHECK(!TookOrder(none, Edit{EditKind::kReset, 0, 703}));
  // The gesture's next order into the same queue: its own Add.
  WXT_CHECK(TookOrder(stop, Edit{EditKind::kAdd, 15, 702}));
}

// Residual of the review: FAF's assist-to-upgrade upgrades the one unit a Guard went to with Clear; the player's own upgrades go
// without, so the player's upgrade of a guarded unit ends the record as any other order.
WXT_TEST(GriffUndoFafsAssistToUpgradeIsTheUpgradeOfOneUnitWithClear)
{
  WXT_CHECK(griffundo::AssistUpgrade("Upgrade", true, 1));
  WXT_CHECK(!griffundo::AssistUpgrade("Upgrade", false, 1));
  WXT_CHECK(!griffundo::AssistUpgrade("Upgrade", true, 2));
  WXT_CHECK(!griffundo::AssistUpgrade("Guard", true, 1));
}

// Review finding gundone-one-slot-no-end. FAF's assist-to-upgrade orders paused extractor 77's upgrade only after the undo of
// the Guard on it said its word (UpgradeUnit unpauses it and waits five game ticks, which a paused game holds); a second
// undo came meanwhile, in the paused game. The upgrade reaches the first undo's word, once; one of another unit is none of
// theirs. An undo that said in part already turns nothing, and an upgrade for a Guard an undo gave back is the unit's own
// order's again.
WXT_TEST(GriffUndoFafsLateUpgradeReachesTheWordOfTheUndoWhoseGuardItWas)
{
  griffundo::Undones undones;
  undones.Finished(1, {77}, {}, true, 100);
  undones.Finished(2, {}, {}, true, 100);
  std::vector<int> turned;
  WXT_CHECK(undones.Upgraded(77, 105, turned));
  WXT_CHECK(turned == (std::vector<int>{1}));
  turned.clear();
  WXT_CHECK(undones.Upgraded(77, 105, turned));
  WXT_CHECK(turned.empty());
  WXT_CHECK(!undones.Upgraded(78, 105, turned));
  undones.Finished(3, {79}, {}, false, 105);
  WXT_CHECK(undones.Upgraded(79, 105, turned));
  undones.Finished(4, {}, {80}, true, 105);
  WXT_CHECK(undones.Upgraded(80, 105, turned));
  WXT_CHECK(turned.empty());
}

// An undo's Guards wait six of the game's ticks after it finished, which a paused game holds, and none outlives its game: not
// one whose tick lies ahead of the game's, another game's whose ticks began anew, nor any once the game ended.
WXT_TEST(GriffUndoAnUndosGuardsWaitSixOfTheGamesTicksForFafsUpgrade)
{
  std::vector<int> turned;
  griffundo::Undones waiting;
  waiting.Finished(1, {77}, {}, true, 100);
  WXT_CHECK(waiting.Upgraded(77, 106, turned));
  WXT_CHECK(turned == (std::vector<int>{1}));
  turned.clear();
  griffundo::Undones late;
  late.Finished(1, {77}, {}, true, 100);
  WXT_CHECK(!late.Upgraded(77, 107, turned));
  griffundo::Undones another;
  another.Finished(1, {77}, {}, true, 100);
  WXT_CHECK(!another.Upgraded(77, 3, turned));
  griffundo::Undones ended;
  ended.Finished(1, {77}, {}, true, 100);
  ended.Clear();
  WXT_CHECK(!ended.Upgraded(77, 100, turned));
  WXT_CHECK(turned.empty());
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

// What decides where an aimed click lands: FAF's hover at the aim for a removal and a click on a unit, the building's cell
// under the press, else the ground under it.
WXT_TEST(GriffUndoAnAimedClickLandsByWhatFafsCursorOrItsPressPicks)
{
  using griffundo::Landing;
  using griffundo::LandingOf;
  WXT_CHECK_EQ(LandingOf(true, false, false), Landing::kHover);
  WXT_CHECK_EQ(LandingOf(false, true, false), Landing::kHover);
  WXT_CHECK_EQ(LandingOf(false, false, true), Landing::kBuilding);
  WXT_CHECK_EQ(LandingOf(false, false, false), Landing::kGround);
}

// The camera glided on 16 pixels since a step aimed at 640 300. A building clicks where this frame shows its cell, a move
// only where it is shown at the aim still, else the cursor goes there again; a removal releases at the aim, where FAF's
// cursor hovers its command, also when the waypoint rides a unit that walked on, which an equal pixel would never meet.
WXT_TEST(GriffUndoAnAimedClickGoesWhereItsLandingWasChecked)
{
  using griffundo::Aim;
  using griffundo::AimedClick;
  using griffundo::Landing;
  WXT_CHECK_EQ(AimedClick(Landing::kBuilding, 640, 300, 656, 300), Aim::kHere);
  WXT_CHECK_EQ(AimedClick(Landing::kBuilding, 640, 300, 640, 300), Aim::kHere);
  WXT_CHECK_EQ(AimedClick(Landing::kGround, 640, 300, 656, 300), Aim::kAgain);
  WXT_CHECK_EQ(AimedClick(Landing::kGround, 640, 300, 640, 301), Aim::kAgain);
  WXT_CHECK_EQ(AimedClick(Landing::kGround, 640, 300, 640, 300), Aim::kAtAim);
  WXT_CHECK_EQ(AimedClick(Landing::kHover, 640, 300, 652, 303), Aim::kAtAim);
  WXT_CHECK_EQ(AimedClick(Landing::kHover, 640, 300, 640, 300), Aim::kAtAim);
}
