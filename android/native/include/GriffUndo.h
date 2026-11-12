#pragma once

// The undo of the touch scheme Griff (android/TOUCH_UI.md, "Griff"; buildstage/griff-v3-spec.md, T3.1): the last gesture's
// orders taken back with what FAF's UI itself sends, as a PC player's mouse and keyboard would. Of each unit the gesture
// ordered, its queue before the gesture and now (UserUnit:GetCommandQueue) give the plan: the orders panel's Stop for a unit
// that was idle, and for one that had orders, those given again by FAF's command modes and a left click at each order's
// place, the first replacing what the unit does, which takes the gesture's orders with it. What FAF cannot give again (an
// order it has no mode for, a target that is gone, a building it never named) is dropped and counted. The program carries it
// out (src/host/GriffAndroid.cpp); here the rules alone, for their tests (tests/wx/GriffUndoTests.cpp).
//
// FAF's UI has DeleteCommand for a single command, by its id, but an id FAF's UI reads is no command's own: it gives a
// finished command's id to the next, and the ids of the second army's commands on are 2^24 and more, which the game's Lua
// numbers (floats) hold to an even number only. The phone as the second player of a game against the PC deleted none of
// its appended orders so, where it did as the first in a skirmish. So an order the gesture appended goes as one it
// replaced, with the queue given again.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace griffundo
{
  /// No unit targeted, or none known: unit ids start at 0, the first army's commander's.
  constexpr int kNoTarget = -1;

  /// A world place: FAF's x, its height and z.
  struct Place
  {
    float x = 0;
    float y = 0;
    float z = 0;
  };

  /// An order in a unit's queue, as FAF's UI reads it: its command's id, its type (FAF's name: Move, AggressiveMove, ...) and
  /// its place, which for an order on an entity is where the entity was. With what Griff saw of the order when it was given,
  /// where it did: the unit it targets (`target`, kNoTarget where not a unit or not seen), whether it targets a place that
  /// stays where it is (`targetsPlace`: the ground, or a feature such as a wreck), the building it builds and the script it
  /// runs (FAF's attack-move is a script order until the sim gives its AggressiveMove).
  struct Entry
  {
    int id = 0;
    std::string type;
    Place place;
    int target = kNoTarget;
    bool targetsPlace = false;
    std::string blueprint;
    std::string task;
  };

  /// A unit that an order of the gesture went to: its queue before the gesture and now, whether an order replaced what it did
  /// (FAF's Clear), and whether its queue is a factory's rally route, whose Stop would also take what it builds.
  struct Unit
  {
    int id = 0;
    bool factory = false;
    bool replaced = false;
    std::vector<Entry> before;
    std::vector<Entry> now;
  };

  /// How FAF's UI gives an order: the mode its orders panel arms for it (or its construction panel, for a building), then the
  /// left click at its place.
  enum class Way
  {
    kNone,
    kMove,
    kAttackMove,
    kPatrol,
    kAttack,
    kGuard,
    kRepair,
    kReclaim,
    kCapture,
    kBuild,
  };

  /// An order to give again: its way, where the click goes (a unit it targets: where that is now), the unit, the building, and
  /// whether it is appended to the ones before it (Shift): the first of a queue replaces what the units do.
  struct Click
  {
    Way way = Way::kNone;
    Place place;
    int target = kNoTarget;
    std::string blueprint;
    bool appended = false;

    [[nodiscard]] bool operator==(const Click& other) const
    {
      return way == other.way && place.x == other.place.x && place.y == other.place.y && place.z == other.place.z
          && target == other.target && blueprint == other.blueprint && appended == other.appended;
    }
  };

  /// Units whose queues come back alike: selected together, and the clicks given them.
  struct Group
  {
    std::vector<int> units;
    std::vector<Click> clicks;
  };

  struct Plan
  {
    /// Units to stop.
    std::vector<int> stops;
    std::vector<Group> groups;
    /// Orders that could not come back, and gestures' orders that could not go.
    int dropped = 0;
  };

  /// Where the units that orders target are now, those that live.
  using Targets = std::unordered_map<int, Place>;

  /// The way FAF's UI gives an order of this kind again; kNone where its orders panel has no mode for it that a click gives
  /// (a transport's, a missile's, an enhancement's), or it is the queue's Stop.
  [[nodiscard]] inline Way WayOf(const Entry& entry)
  {
    const std::string& type = entry.type;
    if (type == "Move" || type == "FormMove") {
      return Way::kMove;
    }
    if (type == "AggressiveMove" || type == "FormAggressiveMove" || (type == "Script" && entry.task == "AttackMove")) {
      return Way::kAttackMove;
    }
    if (type == "Patrol" || type == "FormPatrol") {
      return Way::kPatrol;
    }
    if (type == "Attack" || type == "FormAttack") {
      return Way::kAttack;
    }
    if (type == "Guard") {
      return Way::kGuard;
    }
    if (type == "Repair") {
      return Way::kRepair;
    }
    if (type == "Reclaim") {
      return Way::kReclaim;
    }
    if (type == "Capture") {
      return Way::kCapture;
    }
    if (type == "BuildMobile") {
      return Way::kBuild;
    }
    return Way::kNone;
  }

  /// Whether two entries are the very same order: the same id, type and place (where FAF's UI reads none, as for a Stop or an
  /// enhancement, alike). FAF's UI gives a finished command's id to the next, two of the second army's commands on can show
  /// one id, and all the sim's own orders have one, so an order at another place is another.
  [[nodiscard]] inline bool Exactly(const Entry& one, const Entry& other)
  {
    const auto same = [](const float a, const float b) { return a == b || (std::isnan(a) && std::isnan(b)); };
    return one.id == other.id && one.type == other.type && same(one.place.x, other.place.x) && same(one.place.z, other.place.z);
  }

  /// Whether a queue is as it was before the gesture, order by order: then the gesture's orders did not take there, as where
  /// the sim keeps a commander at its enhancement, and there is nothing to take back.
  [[nodiscard]] inline bool Unchanged(const std::vector<Entry>& before, const std::vector<Entry>& now)
  {
    return before.size() == now.size() && std::equal(before.begin(), before.end(), now.begin(), Exactly);
  }

  /// A queue's orders: a Stop that the sim has not taken yet stands in it, and leaves nothing.
  [[nodiscard]] inline std::vector<Entry> Orders(const std::vector<Entry>& queue)
  {
    std::vector<Entry> orders;
    for (const Entry& entry : queue) {
      if (entry.type != "Stop") {
        orders.push_back(entry);
      }
    }
    return orders;
  }

  /// Of a queue before, the orders the unit holds still (`holds`) or not any more: what the sim kept through a replacing
  /// order, as a commander's enhancement under way, or what the order took.
  [[nodiscard]] inline std::vector<Entry> Held(const std::vector<Entry>& before, const std::vector<Entry>& now, const bool holds)
  {
    std::vector<Entry> held;
    for (const Entry& entry : before) {
      if (std::any_of(now.begin(), now.end(), [&](const Entry& kept) { return Exactly(entry, kept); }) == holds) {
        held.push_back(entry);
      }
    }
    return held;
  }

  namespace detail
  {
    /// Where the click for an order goes: its place, or where the unit it targets is now. False where it cannot be given again.
    [[nodiscard]] inline bool ClickPlace(const Entry& entry, const Way way, const Targets& targets, Place& place)
    {
      const auto living = [&](Place& at) {
        const auto found = entry.target != kNoTarget ? targets.find(entry.target) : targets.end();
        if (found == targets.end()) {
          return false;
        }
        at = found->second;
        return true;
      };
      // FAF's UI reads no place (NaN) for an order on an entity it does not find, which no click reaches.
      const auto own = [&](Place& at) {
        if (std::isnan(entry.place.x) || std::isnan(entry.place.z)) {
          return false;
        }
        at = entry.place;
        return true;
      };
      switch (way) {
        case Way::kMove:
        case Way::kAttackMove:
        case Way::kPatrol:
          return own(place);
        case Way::kBuild:
          return !entry.blueprint.empty() && own(place);
        case Way::kAttack:
        case Way::kReclaim:
          return entry.targetsPlace ? own(place) : living(place);
        case Way::kGuard:
        case Way::kRepair:
        case Way::kCapture:
          return living(place);
        case Way::kNone:
          break;
      }
      return false;
    }

    /// The clicks that give a queue again, as far as FAF can; the dropped orders counted.
    [[nodiscard]] inline std::vector<Click> Again(const std::vector<Entry>& queue, const Targets& targets, int& dropped)
    {
      std::vector<Click> clicks;
      for (const Entry& entry : queue) {
        Click click;
        click.way = WayOf(entry);
        if (!ClickPlace(entry, click.way, targets, click.place)) {
          ++dropped;
          continue;
        }
        click.target = entry.target;
        click.blueprint = entry.blueprint;
        click.appended = !clicks.empty();
        clicks.push_back(std::move(click));
      }
      return clicks;
    }
  } // namespace detail

  /// The plan that takes the gesture's orders back from these units, those that live. A unit whose queue a replacing order
  /// took orders from gets them given again, the first click replacing; what it holds still the sim kept. A unit the gesture
  /// appended to, or whose replacing order took nothing, gets what it holds still of its queue before given again, whose
  /// replacing first click takes the gesture's orders with it; what it finished since stays done, as it would have without
  /// the gesture. A unit that holds none of its orders before and gets none back is stopped (the orders panel's Stop, after
  /// the gesture's orders): one that was idle, finished them since, or lost them to the gesture. One that holds orders FAF
  /// cannot give again keeps them, and the gesture's, which counts as dropped. A factory with nothing to give again keeps the
  /// gesture's rally point: its Stop would take what it builds too. A unit whose queue is as it was is left alone.
  [[nodiscard]] inline Plan PlanUndo(const std::vector<Unit>& units, const Targets& targets)
  {
    Plan plan;
    for (const Unit& unit : units) {
      if (Unchanged(unit.before, unit.now)) {
        continue;
      }
      const std::vector<Entry> before = Orders(unit.before);
      const std::vector<Entry> now = Orders(unit.now);
      const std::vector<Entry> lost = Held(before, now, false);
      std::vector<Click> clicks =
        detail::Again(unit.replaced && !lost.empty() ? lost : Held(before, now, true), targets, plan.dropped);
      if (clicks.empty()) {
        // Nothing comes back by a click. A unit that holds none of its orders before is stopped, which takes the gesture's
        // with it; one that holds some keeps them, and the gesture's. A factory's Stop would take what it builds too.
        if (unit.factory) {
          ++plan.dropped;
        } else if (lost.size() == before.size()) {
          plan.stops.push_back(unit.id);
        }
        continue;
      }
      const auto group = std::find_if(plan.groups.begin(), plan.groups.end(), [&](const Group& other) { return other.clicks == clicks; });
      if (group != plan.groups.end()) {
        group->units.push_back(unit.id);
      } else {
        plan.groups.push_back(Group{{unit.id}, std::move(clicks)});
      }
    }
    return plan;
  }
} // namespace griffundo
