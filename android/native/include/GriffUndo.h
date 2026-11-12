#pragma once

// The undo of the touch scheme Griff (android/TOUCH_UI.md, "Griff"; buildstage/griff-v3-spec.md, T3.1): the last gesture's
// orders taken back with what a PC player's mouse and keyboard send. Each command of the gesture's is taken out of its
// units' queues by FAF's Shift+Ctrl right release on its waypoint (CUIWorldView::HandleEvent's right release, which strips
// the hovered command from every unit it went to, by the command's own id). A unit whose queue the gesture replaced (FAF's
// Clear) gets what it lost given again by FAF's command modes and a left click each, all with Shift, onto its emptied
// queue: a click FAF refuses loses only its own order. What FAF cannot give again, or take out, is counted. The plan reads
// the queues once the sim has taken the gesture (no edit of the UI's pending), each command by its own id, a factory's own
// queue and its rally route apart; the program reads them and carries the plan out (src/host/GriffAndroid.cpp), each click
// at the pixel that decides where it lands; here the rules alone, for their tests (tests/wx/GriffUndoTests.cpp).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace griffundo
{
  /// No entity targeted: entity ids start at 0, the first army's commander's.
  constexpr int kNoTarget = -1;

  /// A world place: FAF's x, its height and z.
  struct Place
  {
    float x = 0;
    float y = 0;
    float z = 0;
  };

  /// An order in a unit's queue as FAF's UI holds it: its command's own id (the engine's, whole: the game's Lua numbers hold
  /// those of the second army on to an even number only), its type (FAF's name: Move, AggressiveMove, ...), its place (where
  /// it goes, or where the entity it targets is), the entity it targets (kNoTarget: the ground, or none), the building it
  /// builds, and its script's task (FAF's attack-move button gives a script, AttackMove, beside the sim's AggressiveMove).
  struct Entry
  {
    int id = 0;
    std::string type;
    Place place;
    int target = kNoTarget;
    std::string blueprint;
    std::string task;
  };

  /// A queue of a unit's that an order of the gesture went into, its own or a factory's rally route (a factory is two Units
  /// with its id): whether one replaced the queue (FAF's Clear), whether the sim refills it once it is empty (a rally route:
  /// the sim rallies an emptied one to the factory's own place again), the queue as the gesture began, as the sim held it
  /// when the gesture's replacement landed (`taken`: the sim runs the old queue until then, a network delay after the input,
  /// and what the unit finished meanwhile is not lost; the program gives `before` where it read no such view) and now, and
  /// the building the unit was building as the gesture began, where that is unfinished still.
  struct Unit
  {
    int id = 0;
    bool replaced = false;
    bool refilled = false;
    std::vector<Entry> before;
    std::vector<Entry> taken;
    std::vector<Entry> now;
    int site = kNoTarget;
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

  /// An order given again: its way, where the click goes (for one on an entity: where that is now, read again at the click),
  /// the entity, the building, and whether the click carries Shift: every one does but a refilled queue's first, which
  /// replaces the gesture's orders there and what the sim refilled.
  struct Click
  {
    Way way = Way::kNone;
    Place place;
    int target = kNoTarget;
    std::string blueprint;
    bool shift = true;

    [[nodiscard]] bool operator==(const Click& other) const
    {
      return way == other.way && place.x == other.place.x && place.y == other.place.y && place.z == other.place.z
          && target == other.target && blueprint == other.blueprint && shift == other.shift;
    }
  };

  /// Units whose lost orders come back alike: selected together, and the clicks given them.
  struct Group
  {
    std::vector<int> units;
    std::vector<Click> clicks;
  };

  /// A command of the gesture's to take out of its units' queues: its id and its units.
  struct Removal
  {
    int command = 0;
    std::vector<int> units;
  };

  struct Plan
  {
    std::vector<Group> groups;
    std::vector<Removal> removals;
    /// Orders that cannot come back, and orders of the gesture's that cannot go.
    int dropped = 0;
  };

  /// Where the entities that orders target are now, those that live; the unfinished buildings units had begun too.
  using Targets = std::unordered_map<int, Place>;

  /// Whether an order FAF's UI gave is a gesture's to take back: not FAF's by-product (None), nor the construction panel's own
  /// queue (a factory's units, an upgrade, an enhancement: a script other than the attack-move's), which the panel itself
  /// takes back. Any other order ends the pill's record: only the last order counts.
  [[nodiscard]] inline bool Takeable(const std::string& type, const bool attackMove)
  {
    return type != "None" && type != "BuildFactory" && type != "Upgrade" && (type != "Script" || attackMove);
  }

  /// The way FAF's UI gives an order of this kind again; kNone where its orders panel has no mode for it that a click gives
  /// (a transport's, a missile's, an enhancement's).
  [[nodiscard]] inline Way WayOf(const Entry& entry)
  {
    const std::string& type = entry.type;
    if (type == "Move" || type == "FormMove") {
      return Way::kMove;
    }
    if (type == "AggressiveMove" || type == "FormAggressiveMove") {
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

  /// Whether an order leaves nothing to take out or give again: a Stop the sim had not taken as the gesture began, and the
  /// script FAF's attack-move button gives beside the sim's AggressiveMove, which is done the moment it heads the queue.
  /// Until then, behind an order the unit kept, the script stays its queue's last command, and FAF anchors a patrol given
  /// with Shift at its place (ResolveGroupMoveAnchorOrDetectPatrol takes the last queued command of any kind): PlanUndo
  /// counts it there, as FAF draws no waypoint for it to take out.
  [[nodiscard]] inline bool Inert(const Entry& entry)
  {
    return entry.type == "Stop" || (entry.type == "Script" && entry.task == "AttackMove");
  }

  /// Whether FAF draws a waypoint for an order, which its Shift+Ctrl right release can take out: not for the kinds its command
  /// graph leaves out (UICommandGraph's AddCommandQueueToCommandGraph, mask 0x2D80000EA).
  [[nodiscard]] inline bool Removable(const Entry& entry)
  {
    static const char* const kUndrawn[] = {"Stop",    "Dive",   "BuildSiloTactical", "BuildSiloNuke", "BuildFactory",
                                           "Upgrade", "Script", "KillSelf",          "DestroySelf",   "Pause"};
    return std::none_of(std::begin(kUndrawn), std::end(kUndrawn), [&](const char* type) { return entry.type == type; });
  }

  /// Whether a unit's own order, which FAF's cursor hovers where one of the gesture's waypoints lies, may be taken out with
  /// the gesture's and given back after them: an order on a place (a move, an attack-move, a patrol point), whose click with
  /// Shift gives it again exactly, that comes last in the unit's queue but for the gesture's (`removed`), so that given back
  /// after them it stands where it stood. FAF anchors a patrol given with Shift at the queue's last place: there two
  /// waypoints lie at one place, and FAF's cursor may hover either.
  [[nodiscard]] inline bool ComesBackAfter(const std::vector<Entry>& queue, const int id, const std::vector<int>& removed)
  {
    const auto at = std::find_if(queue.begin(), queue.end(), [id](const Entry& entry) { return entry.id == id; });
    if (at == queue.end() || at->target != kNoTarget) {
      return false;
    }
    const Way way = WayOf(*at);
    if (way != Way::kMove && way != Way::kAttackMove && way != Way::kPatrol) {
      return false;
    }
    return std::all_of(std::next(at), queue.end(), [&](const Entry& entry) {
      return std::find(removed.begin(), removed.end(), entry.id) != removed.end();
    });
  }

  /// What decides where an aimed click lands, the undo's, which goes a frame after FAF's cursor went to its place (the aim),
  /// once FAF's cursor is found right there (the program's CursorAt): FAF's hover at the aim, which CursorAt checked (a
  /// removal: the command its release takes out; a click on a unit: that unit, which may walk on, as may a waypoint on it);
  /// the cell of a building under the press, which this frame's pixel was checked to land in (griffdraft::ClickPixel); or the
  /// ground under the press, where CursorAt checked at the aim what FAF's cursor picks (for a move, no waypoint whose move
  /// FAF's move mode would restart as a patrol).
  enum class Landing
  {
    kHover,
    kBuilding,
    kGround,
  };

  [[nodiscard]] inline Landing LandingOf(const bool removal, const bool onUnit, const bool builds)
  {
    if (removal || onUnit) {
      return Landing::kHover;
    }
    return builds ? Landing::kBuilding : Landing::kGround;
  }

  /// Where an aimed click goes: at the aim, where FAF's cursor is; at this frame's pixel of its place; or nowhere yet, as the
  /// cursor goes to its place again, which the camera has moved off the aim (within the step's frames).
  enum class Aim
  {
    kAtAim,
    kHere,
    kAgain,
  };

  /// The pixel of an aimed click (Aim), by what decides where it lands (Landing), from the aim and this frame's pixel of its
  /// place: a hover at the aim, which FAF's press picks there, also where what it picked walked on since; a building at this
  /// frame's pixel, whose landing the press takes there; the ground at the aim only while this frame's pixel is the aim, as
  /// what CursorAt found lies there. A camera that moved since the aim, as a glide does, would put a building's or a move's
  /// world point off its place by what the picture moved.
  [[nodiscard]] inline Aim AimedClick(const Landing landing, const int aimX, const int aimY, const int x, const int y)
  {
    switch (landing) {
      case Landing::kHover:
        return Aim::kAtAim;
      case Landing::kBuilding:
        return Aim::kHere;
      case Landing::kGround:
        break;
    }
    return x == aimX && y == aimY ? Aim::kAtAim : Aim::kAgain;
  }

  namespace detail
  {
    [[nodiscard]] inline bool Holds(const std::vector<Entry>& queue, const int id)
    {
      return std::any_of(queue.begin(), queue.end(), [id](const Entry& entry) { return entry.id == id; });
    }

    /// Whether a begun building stands at an order's place: FAF's build mode refuses a new one where it stands.
    [[nodiscard]] inline bool At(const Place& site, const Place& place)
    {
      return std::abs(site.x - place.x) <= 1 && std::abs(site.z - place.z) <= 1;
    }

    /// The clicks that give a unit's lost orders again, as far as FAF can; the dropped ones counted. A begun building comes
    /// back by its repair. A patrol's first point is FAF's own anchor: FAF anchors a patrol given with Shift at the queue's
    /// last place, so the point comes back as a move to it (none where the click before ends there), which keeps FAF from
    /// adding a point of its own.
    [[nodiscard]] inline std::vector<Click> Again(const Unit& unit, const std::vector<Entry>& lost, const Targets& targets, int& dropped)
    {
      std::vector<Click> clicks;
      const auto site = unit.site != kNoTarget ? targets.find(unit.site) : targets.end();
      for (std::size_t at = 0; at < lost.size(); ++at) {
        const Entry& entry = lost[at];
        if (Inert(entry)) {
          continue;
        }
        Click click;
        click.way = WayOf(entry);
        click.target = entry.target;
        click.blueprint = entry.blueprint;
        click.place = entry.place;
        if (click.way == Way::kBuild && site != targets.end() && At(site->second, entry.place)) {
          click.way = Way::kRepair;
          click.target = unit.site;
          click.blueprint.clear();
        }
        bool reached = click.way != Way::kNone;
        if (reached && click.target != kNoTarget) {
          const auto found = targets.find(click.target);
          reached = found != targets.end();
          if (reached) {
            click.place = found->second;
          }
        } else if (reached) {
          reached = !std::isnan(click.place.x) && !std::isnan(click.place.z) && (click.way != Way::kBuild || !click.blueprint.empty());
        }
        if (!reached) {
          ++dropped;
          continue;
        }
        if (click.way == Way::kPatrol && (at == 0 || WayOf(lost[at - 1]) != Way::kPatrol)) {
          if (!clicks.empty() && clicks.back().place.x == click.place.x && clicks.back().place.z == click.place.z) {
            continue;
          }
          click.way = Way::kMove;
        }
        clicks.push_back(std::move(click));
      }
      return clicks;
    }
  } // namespace detail

  /// The plan that takes the gesture's orders back from these units' queues, those that live. Each command of the gesture's
  /// (in a queue now, not in it before, also what the sim gave for it, as the attack-move's AggressiveMove) is taken out of
  /// all its units at once; one FAF draws no waypoint for cannot go and counts as dropped, as does the attack-move's script
  /// left behind an order the unit kept. A queue the gesture replaced gets what it lost given again, every click with Shift:
  /// what it held as the gesture began and still when the replacement landed (`taken`), not now; what it holds still the
  /// sim kept, as a commander's enhancement under way. One the gesture appended to keeps its queue before as it is (a begun
  /// building goes on), and what the unit finished since stays done. A refilled queue's lost orders come back by a first
  /// click that replaces the gesture's there instead, as an emptied rally route would come back as the sim's own.
  [[nodiscard]] inline Plan PlanUndo(const std::vector<Unit>& units, const Targets& targets)
  {
    Plan plan;
    for (const Unit& unit : units) {
      std::vector<Click> clicks;
      if (unit.replaced) {
        std::vector<Entry> lost;
        std::copy_if(unit.before.begin(), unit.before.end(), std::back_inserter(lost), [&](const Entry& entry) {
          return detail::Holds(unit.taken, entry.id) && !detail::Holds(unit.now, entry.id);
        });
        clicks = detail::Again(unit, lost, targets, plan.dropped);
      }
      if (unit.refilled && !clicks.empty()) {
        clicks.front().shift = false;
      } else {
        bool kept = false;
        for (const Entry& entry : unit.now) {
          if (detail::Holds(unit.before, entry.id)) {
            kept = true;
            continue;
          }
          if (Inert(entry)) {
            if (kept && entry.type == "Script") {
              ++plan.dropped;
            }
            continue;
          }
          if (!Removable(entry)) {
            ++plan.dropped;
            continue;
          }
          const auto removal = std::find_if(plan.removals.begin(), plan.removals.end(),
                                            [&](const Removal& other) { return other.command == entry.id; });
          if (removal != plan.removals.end()) {
            removal->units.push_back(unit.id);
          } else {
            plan.removals.push_back(Removal{entry.id, {unit.id}});
          }
        }
      }
      if (clicks.empty()) {
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
