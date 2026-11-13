#pragma once

// The undo of the touch scheme Griff (android/TOUCH_UI.md, "Griff"; buildstage/griff-v3-spec.md, T3.1): the last gesture's
// orders taken back with what a PC player's mouse and keyboard send. Each command of the gesture's is taken out of its
// units' queues by FAF's Shift+Ctrl right release on its waypoint (CUIWorldView::HandleEvent's right release, which strips
// the hovered command from every unit it went to, by the command's own id). A unit whose queue the gesture replaced (FAF's
// Clear) gets what it lost given again by FAF's command modes and a left click each, all with Shift, onto its emptied
// queue: a click FAF refuses loses only its own order. What FAF cannot give again, or take out, is counted. The plan reads
// the queues once the sim has taken the gesture (no edit of the UI's pending), each command by its own id, a factory's own
// queue and its rally route apart, and knows which orders of the queue as the gesture began the sim had not taken yet then
// and what the sim made of them; the program reads them and carries the plan out (src/host/GriffAndroid.cpp), each click
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

  /// Whether the sim held an order of the queue as the gesture began, which FAF's UI showed (its primary links with its own
  /// edits over them, UserCommandQueue::issueQueue, which go to the sim at the beat its driver gave them), and if not, what
  /// the program read once the sim took it: the sim held it (kNo); or the UI's edit was pending, and the program read what
  /// the sim held after the sim took it, before the gesture's replacement landed (kRead: `Unit::taken` tells what became of
  /// it); the sim took it in the very beat that the replacement landed in, just before the replacement, which emptied the
  /// queue with it (kWithReplacement); or the sim took it in a beat before the replacement's that no read came after, as
  /// where both beats' syncs came in one frame (kUnread: it was in the queue still unless the unit did it in between, which
  /// the program cannot tell; and any pending order of a queue the gesture did not replace, which the program does not read).
  enum class Pending
  {
    kNo,
    kRead,
    kWithReplacement,
    kUnread,
  };

  /// An order in a unit's queue as FAF's UI holds it: its command's own id (the engine's, whole: the game's Lua numbers hold
  /// those of the second army on to an even number only), its type (FAF's name: Move, AggressiveMove, ...), its place (where
  /// it goes, or where the entity it targets is), the entity it targets (kNoTarget: the ground, or none), the building it
  /// builds, its script's task (FAF's attack-move button gives a script, AttackMove, beside the sim's AggressiveMove), and,
  /// in the queue as the gesture began, whether the sim held it then.
  struct Entry
  {
    int id = 0;
    std::string type;
    Place place;
    int target = kNoTarget;
    std::string blueprint;
    std::string task;
    Pending pending = Pending::kNo;
  };

  /// A queue of a unit's that an order of the gesture went into, its own or a factory's rally route (a factory is two Units
  /// with its id): whether one replaced the queue (FAF's Clear), whether the sim refills it once it is empty (a rally route:
  /// the sim rallies an emptied one to the factory's own place again), the queue as the gesture began, as the sim held it
  /// when the gesture's replacement landed (`taken`: the sim runs the old queue until then, a network delay after the input,
  /// and what the unit finished meanwhile is not lost; the program gives `before` where it read no such view) and now, the
  /// commands the gesture's orders put into it as FAF's UI gave them (`given`), and the building the unit was building as
  /// the gesture began, where that is unfinished still.
  struct Unit
  {
    int id = 0;
    bool replaced = false;
    bool refilled = false;
    std::vector<Entry> before;
    std::vector<Entry> taken;
    std::vector<Entry> now;
    std::vector<Entry> given;
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
    /// Orders the plan gives back, or leaves, as the unit's own without knowing it: one the sim took in a beat no read came
    /// after, and an AggressiveMove at a place where both the unit's own pending attack-move and one of the gesture's went.
    int unsure = 0;
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

  /// Whether an order is the one FAF's assist-to-upgrade gives (its options assist_to_upgrade and assist_to_upgrade_radar:
  /// commandmode.lua's UpgradeUnit, which OnGuardIssued forks for a Guard on an extractor or a radar): the Upgrade of that one
  /// unit with Clear. No upgrade of the player's has Clear (the construction panel's, the upgrade key's, hotbuild's and the
  /// build mode keys' are given without), so the player's own upgrade of a unit a Guard went to is an order like any other.
  [[nodiscard]] inline bool AssistUpgrade(const std::string& type, const bool clear, const std::size_t units)
  {
    return type == "Upgrade" && clear && units == 1;
  }

  /// What a queue's newest edit does (UserCommandQueue's EUserQueueEdit): adds a command, empties the queue, takes one out;
  /// kNone while no edit waits.
  enum class EditKind
  {
    kNone,
    kAdd,
    kReset,
    kRemove,
  };

  /// A queue's newest edit as FAF's UI made it (UserCommandQueue::issueQueue's back), as long as the sim has not taken it:
  /// what it does, the command it adds or takes out by its own id (none for a Reset), and the beat it goes to the sim at.
  struct Edit
  {
    EditKind kind = EditKind::kNone;
    int command = 0;
    int beat = 0;

    [[nodiscard]] bool operator==(const Edit& other) const
    {
      return kind == other.kind && command == other.command && beat == other.beat;
    }
  };

  /// Whether an order FAF's UI gave went into a queue, from the queue's newest edit before the order (`before`, as last seen)
  /// and after it: the queue whose newest edit is now an Add that was not its newest before took it. ISSUE_Command (a unit's
  /// own queue) and ISSUE_FactoryCommand (a factory's rally route, where FAF's move, patrol and transport arms send a
  /// selection's factories their orders) push the order's edit last and then tell FAF's UI of it (UI_OnCommandIssued), so
  /// a factory's two queues are told apart by it; the sync drops edits from the front only, and a removal is no order.
  [[nodiscard]] inline bool TookOrder(const Edit& before, const Edit& after)
  {
    return after.kind == EditKind::kAdd && !(after == before);
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

  /// Whether an order is the script FAF's attack-move button gives (commandmode.lua's OnScriptIssued then asks the sim's
  /// AttackMove callback for an AggressiveMove at its place).
  [[nodiscard]] inline bool AttackMoveScript(const Entry& entry)
  {
    return entry.type == "Script" && entry.task == "AttackMove";
  }

  /// Whether an order leaves nothing to take out or give again: a Stop the sim had not taken as the gesture began, and the
  /// script FAF's attack-move button gives beside the sim's AggressiveMove, which is done the moment it heads the queue.
  /// Until then, behind an order the unit kept, the script stays its queue's last command, and FAF anchors a patrol given
  /// with Shift at its place (ResolveGroupMoveAnchorOrDetectPatrol takes the last queued command of any kind): PlanUndo
  /// counts it there, as FAF draws no waypoint for it to take out.
  [[nodiscard]] inline bool Inert(const Entry& entry)
  {
    return entry.type == "Stop" || AttackMoveScript(entry);
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

    /// Whether an AggressiveMove goes to an attack-move script's place: FAF's AttackMove callback copies the script's place
    /// from the unit's queue into its AggressiveMove.
    [[nodiscard]] inline bool SamePlace(const Place& one, const Place& other)
    {
      return one.x == other.x && one.z == other.z;
    }

    /// For each order of `before`, at its index: where it is an attack-move script the sim had not taken as the gesture began,
    /// the AggressiveMove of `queue` the sim made of it; else none. The sim makes it once it takes the script (FAF's
    /// AttackMove callback, which the UI sends with the script, adds an AggressiveMove at the script's place after the
    /// script; Clear's replaces the script), so `before` never holds it: it is the first AggressiveMove at the script's place
    /// that `before` does not hold and no script before it has, as each script's comes before the next order's.
    [[nodiscard]] inline std::vector<const Entry*> MadeOf(const std::vector<Entry>& before, const std::vector<Entry>& queue)
    {
      std::vector<const Entry*> made(before.size(), nullptr);
      for (std::size_t at = 0; at < before.size(); ++at) {
        const Entry& script = before[at];
        if (script.pending == Pending::kNo || !AttackMoveScript(script)) {
          continue;
        }
        const auto found = std::find_if(queue.begin(), queue.end(), [&](const Entry& entry) {
          return entry.type == "AggressiveMove" && SamePlace(entry.place, script.place) && !Holds(before, entry.id)
              && std::find(made.begin(), made.end(), &entry) == made.end();
        });
        if (found != queue.end()) {
          made[at] = &*found;
        }
      }
      return made;
    }

    /// Whether the gesture gave an attack-move at a place (its script, as FAF's UI gave it).
    [[nodiscard]] inline bool GaveAttackMoveAt(const Unit& unit, const Place& place)
    {
      return std::any_of(unit.given.begin(), unit.given.end(),
                         [&](const Entry& entry) { return AttackMoveScript(entry) && SamePlace(entry.place, place); });
    }

    /// The attack-move the sim makes of a script, which FAF's attack-move mode gives again: an AggressiveMove at its place.
    [[nodiscard]] inline Entry AsAttackMove(Entry script)
    {
      script.type = "AggressiveMove";
      script.task.clear();
      return script;
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
  ///
  /// An order of the queue as the gesture began that the sim had not taken then (Pending) is the unit's as well, and so is
  /// what the sim made of it after the gesture began: an attack-move script's AggressiveMove at its place (MadeOf). In a
  /// queue the gesture appended to, that AggressiveMove stays; where the gesture gave an attack-move at that place too, whose
  /// it is cannot be told: it stays, counted (`unsure`). In a queue the gesture replaced such an order was lost with it as
  /// far as the program read what the sim held (`taken`, where the script's AggressiveMove stands for the script), else by
  /// the beat it went to the sim at: in the replacement's own beat it was there; in an earlier one no read came after, it was
  /// unless the unit did it in between, which counts. Such an attack-move comes back as one, at its place.
  [[nodiscard]] inline Plan PlanUndo(const std::vector<Unit>& units, const Targets& targets)
  {
    Plan plan;
    for (const Unit& unit : units) {
      std::vector<Click> clicks;
      if (unit.replaced) {
        const std::vector<const Entry*> made = detail::MadeOf(unit.before, unit.taken);
        std::vector<Entry> lost;
        for (std::size_t at = 0; at < unit.before.size(); ++at) {
          const Entry& entry = unit.before[at];
          const bool attackMove = entry.pending != Pending::kNo && AttackMoveScript(entry);
          const bool held = entry.pending == Pending::kWithReplacement || entry.pending == Pending::kUnread
                         || detail::Holds(unit.taken, entry.id) || made[at] != nullptr;
          if (!held || detail::Holds(unit.now, entry.id)) {
            continue;
          }
          const Entry back = attackMove ? detail::AsAttackMove(entry) : entry;
          if (entry.pending == Pending::kUnread && !Inert(back)) {
            ++plan.unsure;
          }
          lost.push_back(back);
        }
        clicks = detail::Again(unit, lost, targets, plan.dropped);
      }
      if (unit.refilled && !clicks.empty()) {
        clicks.front().shift = false;
      } else {
        // What the sim made of the unit's own pending attack-moves is the unit's in a queue the gesture appended to; one the
        // gesture replaced lost it with the replacement, and an AggressiveMove there now is the gesture's.
        const std::vector<const Entry*> own = unit.replaced ? std::vector<const Entry*>{} : detail::MadeOf(unit.before, unit.now);
        bool kept = false;
        for (const Entry& entry : unit.now) {
          const bool made = std::find(own.begin(), own.end(), &entry) != own.end();
          if (detail::Holds(unit.before, entry.id) || made) {
            if (made && detail::GaveAttackMoveAt(unit, entry.place)) {
              ++plan.unsure;
            }
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
