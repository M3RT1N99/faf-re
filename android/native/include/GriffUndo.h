#pragma once

// The undo of the touch scheme Griff (android/TOUCH_UI.md, "Griff"; buildstage/griff-v3-spec.md, T3.1): the last gesture's
// orders taken back with what a PC player's mouse and keyboard send. Each command of the gesture's is taken out of its
// units' queues by FAF's Shift+Ctrl right release on its waypoint (CUIWorldView::HandleEvent's right release, which strips
// the hovered command from every unit it went to, by the command's own id). A unit whose queue the gesture replaced (FAF's
// Clear) gets what it lost given again by FAF's command modes and a left click each, all with Shift, onto its emptied
// queue: a click FAF refuses loses only its own order. What FAF cannot give again, or take out, is counted. The plan reads
// the queues once the sim has taken the gesture (no edit of the UI's pending), each command by its own id, a factory's own
// queue and its rally route apart, and knows which orders of the queue as the gesture began the sim had not taken yet then,
// what the program read of them and what the sim made of them; the program reads them and carries the plan out
// (src/host/GriffAndroid.cpp), each click at the pixel that decides where it lands; here the rules alone, for their tests
// (tests/wx/GriffUndoTests.cpp).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
  /// in the queue as the gesture began, whether the sim held it then and, for a Guard on a unit, whether FAF's UI may have
  /// answered it with commands of the sim's own (`answered`, AnswerOf: the program tells it from the unit's kind and the
  /// Ctrl held as FAF's UI gave the Guard, and leaves it where it cannot tell).
  struct Entry
  {
    int id = 0;
    std::string type;
    Place place;
    int target = kNoTarget;
    std::string blueprint;
    std::string task;
    Pending pending = Pending::kNo;
    bool answered = true;
  };

  /// A queue of a unit's that an order of the gesture went into, its own or a factory's rally route (a factory is two Units
  /// with its id): whether one replaced the queue (FAF's Clear), whether the sim refills it once it is empty (a rally route:
  /// the sim rallies an emptied one to the factory's own place again), the queue as the gesture began, as the sim held it
  /// when the gesture's replacement landed (`taken`: the sim runs the old queue until then, a network delay after the input,
  /// and what the unit finished meanwhile is not lost; the program gives `before` where it read no such view) and whether
  /// the sim may have ticked between the read `taken` is and the replacement's beat (`stale`, Stale: a frame that brought
  /// several beats; the unit may have done what `taken` shows), now, the commands the gesture's orders put into it as FAF's
  /// UI gave them (`given`, in their order), and the building the unit was building as the gesture began, where that is
  /// unfinished still.
  struct Unit
  {
    int id = 0;
    bool replaced = false;
    bool refilled = false;
    std::vector<Entry> before;
    std::vector<Entry> taken;
    bool stale = false;
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
    /// after, one a read that may be ticks old showed, one the sim took in the replacement's own beat that FAF answered with
    /// what no read showed and no click of the undo's makes again, an AggressiveMove at a place where both the unit's own
    /// pending attack-move and one of the gesture's went where the gesture's first command is gone, what stands ahead of the
    /// gesture's commands there that one of the unit's pending orders may have made, and what the sim made of a pending
    /// order that comes back with it, which FAF makes again of it.
    int unsure = 0;
  };

  /// Where the entities that orders target are now, those that live; the unfinished buildings units had begun too.
  using Targets = std::unordered_map<int, Place>;

  /// Whether an order FAF's UI gave is a gesture's to take back: not FAF's by-product (None), nor the construction panel's own
  /// queue (a factory's units, an upgrade, an enhancement: a script other than the attack-move's), which the panel itself
  /// takes back. Any other order ends the undo's record: only the last order counts.
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

  /// How many of the game's ticks after an undo finished FAF's upgrade for a Guard of its may still come: UpgradeUnit waits
  /// five after the Guard, which came before the undo, and one more.
  constexpr int kUpgradeTicks = 6;

  /// The undos that finished in the game's last kUpgradeTicks ticks, for FAF's assist-to-upgrade, which may order the
  /// upgrade of a Guard's target only after the undo of that Guard said what it did: UpgradeUnit unpauses a paused extractor
  /// first and waits five of the game's ticks, which a paused game holds back while the sim takes the undo. Each with its
  /// record's number, the targets of the record's Guards, which the undo took out (an upgrade for one of them goes on: a
  /// word that the undo took all of the gesture back turns in part), those of the Guards it gave back (`again`: an upgrade
  /// for one of them is the unit's own order's again), whether it said it took all back, and the game's tick as it finished
  /// (CWldSession::mGameTick, which a paused game holds).
  class Undones
  {
  public:
    void Finished(const int number, std::vector<int> guarded, std::vector<int> again, const bool whole, const int tick)
    {
      Drop(tick);
      mUndone.push_back(Undone{number, std::move(guarded), std::move(again), whole, tick});
    }

    /// FAF ordered the upgrade of `target` at `tick`: whether it is one for a Guard of an undo that finished meanwhile, and
    /// the records whose undo said it took all back and now turns in part by it (`turned`), each once.
    [[nodiscard]] bool Upgraded(const int target, const int tick, std::vector<int>& turned)
    {
      Drop(tick);
      bool upgraded = false;
      for (Undone& undone : mUndone) {
        const bool guarded = std::find(undone.guarded.begin(), undone.guarded.end(), target) != undone.guarded.end();
        upgraded = upgraded || guarded || std::find(undone.again.begin(), undone.again.end(), target) != undone.again.end();
        if (guarded && undone.whole) {
          undone.whole = false;
          turned.push_back(undone.number);
        }
      }
      return upgraded;
    }

    /// The game is over: no upgrade of its comes any more.
    void Clear()
    {
      mUndone.clear();
    }

  private:
    struct Undone
    {
      int number = 0;
      std::vector<int> guarded;
      std::vector<int> again;
      bool whole = false;
      int tick = 0;
    };

    /// Undos that finished more than kUpgradeTicks of the game's ticks before `tick` wait no more, nor any whose tick lies
    /// ahead of it, another game's, whose ticks began anew.
    void Drop(const int tick)
    {
      mUndone.erase(std::remove_if(mUndone.begin(), mUndone.end(),
                                   [tick](const Undone& undone) { return tick < undone.tick || tick - undone.tick > kUpgradeTicks; }),
                    mUndone.end());
    }

    std::vector<Undone> mUndone;
  };

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

  /// Whether taking a gesture back saves more than the player's own next order (Griff v4, the user's rule of 2026-11-14): the
  /// gesture replaced a queue (`before`, as it began) that held a building to give back, or two orders or more that come back
  /// (a route, a patrol, a line of reclaims). For idle units, or units with one plain move or one assist, a Stop or the next
  /// order does as much, and no undo is offered. A factory's own builds and what the orders panel has no mode for cannot come
  /// back by a click and count for nothing; an attack-move's script and its AggressiveMove are one order.
  [[nodiscard]] inline bool SavesWork(const std::vector<Entry>& before)
  {
    std::size_t back = 0;
    for (const Entry& entry : before) {
      const Way way = WayOf(entry);
      if (way == Way::kBuild) {
        return true;
      }
      back += way != Way::kNone ? 1 : 0;
    }
    return back >= 2;
  }

  /// Whether the undo's button offers to bring back a draft that the cross or two fingers' tap dropped, a record of Griff's own
  /// that orders nothing (the user's choice of 2026-11-18): one of two plans or more, as one plan comes back by a tap.
  [[nodiscard]] constexpr bool DropSavesWork(const std::size_t plans)
  {
    return plans >= 2;
  }

  /// Whether the undo's button offers to give a control group back the units a rest stored over, a record of Griff's own that
  /// orders nothing (the user's choice of 2026-11-18): where the slot held units, those of them that live `before`, and the
  /// store put another set there (`after`, the group's units now); not on an empty slot, not where the group holds the very
  /// units it held, and never for Plus's append (`appended`), which takes none of them out.
  [[nodiscard]] inline bool StoreSavesWork(const bool appended, std::vector<int> before, std::vector<int> after)
  {
    if (appended || before.empty()) {
      return false;
    }
    std::sort(before.begin(), before.end());
    std::sort(after.begin(), after.end());
    return before != after;
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

  /// What FAF's UI sends the sim beside an order that makes commands of the sim's own in the queues of its units, in the beat
  /// the sim takes the order (commandmode.lua's answers to OnCommandIssued, 3835): nothing; what FAF makes again of the
  /// order where a click of the undo's gives it again (kAgain: the Rebuild of the wreck a Repair targets); or what only the
  /// player's input made (kOnce), which no click of the undo's brings again: the rings of FAF's capping about a structure a
  /// Guard of engineers targets (capping.lua AssistToCap, by its options, the Shift held and its memory of the player's
  /// taps), which griff.lua holds off while the undo's clicks run; the copy of an engineer's queue for a Guard on it with
  /// Ctrl (OnGuardCopy), which the undo's clicks never hold; and the area attack about the place of an Attack on the ground
  /// whose press dragged in FAF's attack mode (area-attack-order.lua, ExtendAttackOrder at the lift), as Griff's rested or
  /// Box drag on the map does with that mode armed, where the undo's press lifts where it went down. A Guard the program
  /// knows FAF's UI answered with nothing (Entry::answered) is none. An attack-move's script makes its AggressiveMove, which
  /// MadeOf pairs by its place: none here.
  enum class Answer
  {
    kNone,
    kAgain,
    kOnce,
  };

  [[nodiscard]] inline Answer AnswerOf(const Entry& entry)
  {
    if (entry.type == "Repair" && entry.target != kNoTarget) {
      return Answer::kAgain;
    }
    const bool guard = entry.type == "Guard" && entry.target != kNoTarget && entry.answered;
    return guard || (entry.type == "Attack" && entry.target == kNoTarget) ? Answer::kOnce : Answer::kNone;
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

    /// Whether a command is the sim's own, one its Lua issued (FAF's callbacks for an order: an attack-move's AggressiveMove,
    /// a wreck's rebuild, an extractor's rings), whose id carries the sim's source byte, 0x80; a player's carry their army's
    /// (CCommandDB::AddIssueData).
    [[nodiscard]] inline bool SimsOwn(const int id)
    {
      return (static_cast<std::uint32_t>(id) & 0xFF000000u) == 0x80000000u;
    }

    /// Whether one of the unit's orders the sim had not taken as the gesture began may have made commands of the sim's since:
    /// one FAF's UI answers with commands of the sim's own (AnswerOf). FAF makes none of a Move, a Patrol, a Reclaim, a
    /// Capture, a building or a Stop; an attack-move's AggressiveMove is MadeOf's.
    [[nodiscard]] inline bool PendingMakers(const std::vector<Entry>& before)
    {
      return std::any_of(before.begin(), before.end(),
                         [](const Entry& entry) { return entry.pending != Pending::kNo && AnswerOf(entry) != Answer::kNone; });
    }

    /// Where the gesture's commands begin in a queue it appended to: the sim takes the unit's own orders, those it had not
    /// taken as the gesture began too, and what FAF makes of them in the same beat before the gesture's later orders, and
    /// appends each, so whatever stands ahead of the gesture's first command is the unit's own, and all from it on the
    /// gesture's. The index of the first of the gesture's commands still in the queue (its size where none is), and whether
    /// that is the gesture's very first (`exact`): where that one is gone (done at the queue's head, or its target gone),
    /// what stands ahead of the next may be what it made.
    struct Anchor
    {
      std::size_t at = 0;
      bool exact = false;
    };

    [[nodiscard]] inline Anchor GesturesFirst(const std::vector<Entry>& queue, const std::vector<Entry>& given)
    {
      for (std::size_t at = 0; at < queue.size(); ++at) {
        if (Holds(given, queue[at].id)) {
          return Anchor{at, queue[at].id == given.front().id};
        }
      }
      return Anchor{queue.size(), false};
    }

    /// A command of the sim's that `taken` shows it made of one of the unit's orders it had not taken as the gesture began,
    /// and the order of `before` that stands last ahead of it there (its index; -1: none): its maker where that is one of
    /// those orders, which may stay in the queue as an extractor's Guard, else gone, as a Repair on a wreck that FAF's
    /// Rebuild with Clear clears.
    struct Product
    {
      const Entry* entry = nullptr;
      int after = -1;
    };

    /// What the sim made of the unit's orders it had not taken as the gesture began, in the beat it took each (FAF's
    /// callbacks: a wreck's rebuild of a Repair on it, an extractor's rings of a Guard on it), as a read of what the sim held
    /// later shows it (`queue`): the sim's own commands that `before` does not hold and that are no pending attack-move's
    /// AggressiveMove (`made`, which stands for its script), ahead of the gesture's first command there (from it on, what
    /// the gesture's orders that landed earlier made). None unless that read came after the sim took one of those orders
    /// that FAF's UI answers with commands of the sim's own (Pending::kRead, AnswerOf): else the sim's own commands in it are
    /// its own doing, as a rally point it refills.
    [[nodiscard]] inline std::vector<Product> ByProducts(const std::vector<Entry>& before, const std::vector<Entry>& queue,
                                                         const std::vector<const Entry*>& made, const std::vector<Entry>& given)
    {
      std::vector<Product> products;
      if (std::none_of(before.begin(), before.end(),
                       [](const Entry& entry) { return entry.pending == Pending::kRead && AnswerOf(entry) != Answer::kNone; })) {
        return products;
      }
      int after = -1;
      for (const Entry& entry : queue) {
        if (Holds(given, entry.id)) {
          break;
        }
        const auto own = std::find_if(before.begin(), before.end(), [&](const Entry& order) { return order.id == entry.id; });
        const auto pair = std::find(made.begin(), made.end(), &entry);
        if (own != before.end()) {
          after = static_cast<int>(own - before.begin());
        } else if (pair != made.end()) {
          after = static_cast<int>(pair - made.begin());
        } else if (SimsOwn(entry.id)) {
          products.push_back(Product{&entry, after});
        }
      }
      return products;
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

  /// An order of a queue as a gesture began that the sim had not taken then, the UI's Add of it pending: its command, the beat
  /// that Add goes to the sim at, and whether a read of what the sim holds came after the sync reached that beat and before
  /// the gesture's replacement of the queue landed (the replacement's Reset took the order's own edit, but the helper of its
  /// command keeps the units it went to as events of that beat until the sync reaches it: the program's SyncReached).
  struct Unsynced
  {
    int command = 0;
    int beat = 0;
    bool read = false;
  };

  /// The orders of a queue as a gesture began (`before`: FAF's UI's view, the sim's primary links with the UI's pending edits
  /// over them) that the sim had not taken then: each that a pending Add of the queue's edits adds (`edits`, as FAF's UI
  /// made them: UserCommandQueue::issueQueue) and the primary links (`held`) do not hold yet, with the beat that Add goes to
  /// the sim at.
  [[nodiscard]] inline std::vector<Unsynced> UnsyncedOf(const std::vector<Edit>& edits, const std::vector<Entry>& before,
                                                        const std::vector<Entry>& held)
  {
    std::vector<Unsynced> unsynced;
    for (const Edit& edit : edits) {
      if (edit.kind == EditKind::kAdd && detail::Holds(before, edit.command) && !detail::Holds(held, edit.command)) {
        unsynced.push_back(Unsynced{edit.command, edit.beat, false});
      }
    }
    return unsynced;
  }

  /// What the program read of an order of the queue as the gesture began, for the plan (Pending): kNo where the sim held it
  /// then (it is none of `unsynced`). Else, for a queue an order of the gesture's replaced, whose view of the sim's the
  /// program read from that order on (`snapshot`) until the sync reached the beat of its Reset (`beat`): kRead where a read
  /// came after the sync reached the order's own beat, kWithReplacement where that beat is the replacement's, else kUnread
  /// (an earlier beat no read came after); for any other queue kUnread, as nothing of it was read.
  [[nodiscard]] inline Pending PendingOf(const std::vector<Unsynced>& unsynced, const int command, const bool snapshot, const int beat)
  {
    const auto order = std::find_if(unsynced.begin(), unsynced.end(), [command](const Unsynced& one) { return one.command == command; });
    if (order == unsynced.end()) {
      return Pending::kNo;
    }
    if (!snapshot) {
      return Pending::kUnread;
    }
    return order->read ? Pending::kRead : order->beat == beat ? Pending::kWithReplacement : Pending::kUnread;
  }

  /// Whether the sim may have ticked between the last read of what it held of a replaced queue and the beat its replacement
  /// landed in (Unit::stale). The program reads after each frame's syncs, and a frame that took long brings several: from
  /// the game's tick at that read (`readTick`) and at the read that found the replacement landed (`tick`, the newest
  /// sync's), and whether the newest sync's beat ticked (`ticked`). The queue the replacement emptied is the sim's after the
  /// beat before the replacement's, whose tick is at most `tick` less the newest beat's own: exact where the newest sync is
  /// the replacement's; where the frame brought beats after it too, it may call a read old that was not (the honest way).
  /// A paused game's beats make no tick.
  [[nodiscard]] inline bool Stale(const int readTick, const int tick, const bool ticked)
  {
    return readTick < tick - (ticked ? 1 : 0);
  }

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
  /// what the sim made of it after the gesture began, in the beat it took it (FAF's answers to it, AnswerOf: a wreck's
  /// rebuild of a Repair on it, an extractor's rings of a Guard on it; an attack-move script's AggressiveMove at its place,
  /// MadeOf). In a queue the gesture appended to, the sim took all of that before the gesture's orders and appended each:
  /// what stands ahead of the gesture's first command stays, and from that command on all is the gesture's (GesturesFirst).
  /// Where that command is gone, what stands ahead of the next may be what it made: where one of the unit's pending orders
  /// may have made it as well (PendingMakers), whose it is cannot be told, and it stays, counted (`unsure`); so does the
  /// AggressiveMove at a place where both the unit's pending attack-move and the gesture's went. In a queue the gesture
  /// replaced such an order was lost with it as far as the program read what the sim held (`taken`, where the script's
  /// AggressiveMove stands for the script), else by the beat it went to the sim at: in the replacement's own beat it was
  /// there, but what FAF answered it with then no read showed, and where no click of the undo's makes that again
  /// (Answer::kOnce) it counts; in an earlier one no read came after, it was unless the unit did it in between, which
  /// counts. Such an attack-move comes back as one, at its place. What `taken` shows the sim made of such orders
  /// (ByProducts) comes back by its own way, in its place (a building by FAF's build mode at its own), never lost; where its
  /// maker is an order that comes back too and FAF makes it again of that one (Answer::kAgain: a second rebuild of the
  /// wreck, which finds the building there), it counts. Where the read `taken` is may be ticks old (`stale`), what comes
  /// back of it counts.
  [[nodiscard]] inline Plan PlanUndo(const std::vector<Unit>& units, const Targets& targets)
  {
    Plan plan;
    for (const Unit& unit : units) {
      std::vector<Click> clicks;
      if (unit.replaced) {
        const std::vector<const Entry*> made = detail::MadeOf(unit.before, unit.taken);
        const std::vector<detail::Product> products = detail::ByProducts(unit.before, unit.taken, made, unit.given);
        std::vector<Entry> lost;
        // What the sim made that stands after the order `after` of `before` in `taken`, lost with the replacement: it comes
        // back, counted where its maker is that order, which comes back too and which FAF answers alike again (`makerBack`).
        const auto lostProducts = [&](const int after, const bool makerBack) {
          for (const detail::Product& product : products) {
            if (product.after != after || detail::Holds(unit.now, product.entry->id)) {
              continue;
            }
            if (makerBack || unit.stale) {
              ++plan.unsure;
            }
            lost.push_back(*product.entry);
          }
        };
        lostProducts(-1, false);
        for (std::size_t at = 0; at < unit.before.size(); ++at) {
          const Entry& entry = unit.before[at];
          const bool attackMove = entry.pending != Pending::kNo && AttackMoveScript(entry);
          const bool held = entry.pending == Pending::kWithReplacement || entry.pending == Pending::kUnread
                         || detail::Holds(unit.taken, entry.id) || made[at] != nullptr;
          if (!held || detail::Holds(unit.now, entry.id)) {
            lostProducts(static_cast<int>(at), false);
            continue;
          }
          const Entry back = attackMove ? detail::AsAttackMove(entry) : entry;
          // Exact where `taken` is the sim's queue just before the replacement's beat, or where the sim took it in that very
          // beat, just before the replacement emptied the queue with it, unless FAF answered it then with what no read showed
          // and no click of the undo's makes again; else the unit may have done it in between.
          const bool exact = entry.pending == Pending::kWithReplacement ? AnswerOf(entry) != Answer::kOnce
                                                                        : entry.pending != Pending::kUnread && !unit.stale;
          if (!exact && !Inert(back)) {
            ++plan.unsure;
          }
          lost.push_back(back);
          lostProducts(static_cast<int>(at), entry.pending != Pending::kNo && AnswerOf(entry) == Answer::kAgain);
        }
        clicks = detail::Again(unit, lost, targets, plan.dropped);
      }
      if (unit.refilled && !clicks.empty()) {
        clicks.front().shift = false;
      } else {
        // Where the gesture appended, the queue's order tells whose a command is while the gesture's first stands: ahead of it
        // all is the unit's, from it on all the gesture's, an AggressiveMove of its at the very place of a pending attack-move
        // of the unit's too. Where that command is gone, what the sim made of the unit's own pending attack-moves is the
        // unit's (MadeOf). A queue the gesture replaced lost it with the replacement, and an AggressiveMove there now is the
        // gesture's.
        const detail::Anchor first = unit.replaced ? detail::Anchor{} : detail::GesturesFirst(unit.now, unit.given);
        const std::vector<const Entry*> own =
          unit.replaced || first.exact ? std::vector<const Entry*>{} : detail::MadeOf(unit.before, unit.now);
        const bool makers = detail::PendingMakers(unit.before);
        bool kept = false;
        for (std::size_t at = 0; at < unit.now.size(); ++at) {
          const Entry& entry = unit.now[at];
          const bool made = std::find(own.begin(), own.end(), &entry) != own.end();
          if (detail::Holds(unit.before, entry.id) || made) {
            if (made && detail::GaveAttackMoveAt(unit, entry.place)) {
              ++plan.unsure;
            }
            kept = true;
            continue;
          }
          if (at < first.at && (first.exact || makers)) {
            if (!first.exact) {
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
