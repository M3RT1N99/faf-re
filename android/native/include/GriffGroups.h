#pragma once

// The touch scheme Griff's groups (android/TOUCH_UI.md, "The groups"), Supreme Commander 2's on a console: the units one
// gesture ordered together, and the units one factory order made, are a group, which one round icon shows at their middle in
// the world while the game draws strategic icons; a tap on it selects the group. The program keeps the groups and places
// their icons each frame (src/host/GriffAndroid.cpp), FAF's UI draws them (fafre/griffgroups.lua); here the rules alone, for
// their tests (tests/wx/GriffGroupsTests.cpp).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace griffgroups
{
  /// Where a group comes from: the gesture whose orders went to its units (the number of the program's watch over it), or
  /// the run of the factory that made them (Runs, which numbers its runs itself).
  struct Origin
  {
    bool factory = false;
    std::uint32_t number = 0;

    friend bool operator==(const Origin& one, const Origin& other)
    {
      return one.factory == other.factory && one.number == other.number;
    }

    friend bool operator<(const Origin& one, const Origin& other)
    {
      return one.factory != other.factory ? !one.factory : one.number < other.number;
    }
  };

  /// Where a unit was before a gesture's order took it into the gesture's group: in the group of `origin`, or (`held` false)
  /// in none.
  struct Earlier
  {
    int unit = 0;
    bool held = false;
    Origin origin;
  };

  /// Each unit's group: that of the gesture that ordered it last, or of the factory run that made it where no gesture has
  /// ordered it since, until it goes. A group of fewer than two units is none, and shows no icon.
  class Book
  {
  public:
    /// A gesture ordered these units: they leave their groups for the gesture's, with the units its other orders went to.
    /// Where each of those that moved was before, for the gesture's undo (Undone).
    std::vector<Earlier> Ordered(const std::uint32_t gesture, const std::vector<int>& units)
    {
      std::vector<Earlier> moved;
      const Origin origin{false, gesture};
      for (const int unit : units) {
        const auto known = mOriginOf.find(unit);
        if (known != mOriginOf.end() && known->second == origin) {
          continue;
        }
        moved.push_back(known != mOriginOf.end() ? Earlier{unit, true, known->second} : Earlier{unit, false, Origin{}});
        Join(unit, origin);
      }
      return moved;
    }

    /// An undo took the orders of these gestures back whole: each unit still in one of their groups goes back to where it was
    /// before the first of them took it (`earlier`, as Ordered said it, the oldest first), or out of the book where it was in
    /// no group. A unit another gesture ordered since stays in that one's group, and one that went stays gone.
    void Undone(const std::vector<std::uint32_t>& gestures, const std::vector<Earlier>& earlier)
    {
      std::unordered_set<int> seen;
      for (const Earlier& was : earlier) {
        if (!seen.insert(was.unit).second) {
          continue;
        }
        const auto known = mOriginOf.find(was.unit);
        if (known == mOriginOf.end() || known->second.factory
            || std::find(gestures.begin(), gestures.end(), known->second.number) == gestures.end()) {
          continue;
        }
        if (was.held) {
          Join(was.unit, was.origin);
        } else {
          Gone(was.unit);
        }
      }
    }

    /// A factory's run made this unit (Runs): it is in the run's group, with the units the run made before that no gesture
    /// has ordered since, as long as no gesture orders it. A unit is made once: one the book holds stays where it is.
    void Made(const int unit, const std::uint32_t run)
    {
      if (!Holds(unit)) {
        Join(unit, Origin{true, run});
      }
    }

    /// The unit is gone: dead, or no longer one's own.
    void Gone(const int unit)
    {
      const auto origin = mOriginOf.find(unit);
      if (origin == mOriginOf.end()) {
        return;
      }
      Leave(unit, origin->second);
      mOriginOf.erase(origin);
    }

    /// Whether the unit is in the book, a group's or one waiting for its order's next.
    [[nodiscard]] bool Holds(const int unit) const
    {
      return mOriginOf.count(unit) != 0;
    }

    /// Each origin with its units, in the order of the origins: the gestures', oldest first, then the factory orders'. Those
    /// of one unit too, which are no group.
    [[nodiscard]] const std::map<Origin, std::vector<int>>& UnitsByOrigin() const
    {
      return mUnitsOf;
    }

    void Clear()
    {
      mOriginOf.clear();
      mUnitsOf.clear();
    }

  private:
    void Join(const int unit, const Origin origin)
    {
      const auto known = mOriginOf.find(unit);
      if (known != mOriginOf.end()) {
        if (known->second == origin) {
          return;
        }
        Leave(unit, known->second);
        known->second = origin;
      } else {
        mOriginOf.emplace(unit, origin);
      }
      mUnitsOf[origin].push_back(unit);
    }

    void Leave(const int unit, const Origin origin)
    {
      const auto group = mUnitsOf.find(origin);
      if (group == mUnitsOf.end()) {
        return;
      }
      std::vector<int>& units = group->second;
      units.erase(std::remove(units.begin(), units.end(), unit), units.end());
      if (units.empty()) {
        mUnitsOf.erase(group);
      }
    }

    std::unordered_map<int, Origin> mOriginOf;
    std::map<Origin, std::vector<int>> mUnitsOf;
  };

  /// The runs of the factories one's own orders gave builds to (FAF's BuildFactory): the units a factory makes one after
  /// another of one kind while its queue does not run empty are one run, as FAF's construction panel shows them as one entry
  /// with its count (also where the player added to it, or its queue repeats), and one group. A unit counts for its run when
  /// it is finished. A queue seen empty ends the run once what the factory builds is finished: the sync may show the queue
  /// empty a frame before its last unit is finished, which is still the run's. `Kind` tells the units' kinds apart (the
  /// program's blueprints); a unit that went, or is none of the groups', counts for no run and changes none.
  template <class Kind>
  class Runs
  {
  public:
    /// FAF's UI gave the factory builds: its runs are kept from now on.
    void Ordered(const int factory)
    {
      mFactories.try_emplace(factory);
    }

    /// The factories kept, by their ids.
    [[nodiscard]] std::vector<int> Factories() const
    {
      std::vector<int> factories;
      factories.reserve(mFactories.size());
      for (const auto& factory : mFactories) {
        factories.push_back(factory.first);
      }
      return factories;
    }

    /// The units the kept factories build now, by their ids, as Building said them.
    [[nodiscard]] std::vector<int> Built() const
    {
      std::vector<int> units;
      units.reserve(mBuilding.size());
      for (const auto& unit : mBuilding) {
        units.push_back(unit.first);
      }
      return units;
    }

    /// The factory builds this unit now.
    void Building(const int factory, const int unit)
    {
      if (mFactories.count(factory) != 0) {
        mBuilding.try_emplace(unit, factory);
      }
    }

    /// The factory's queue is empty now: its run ends once what it builds is finished.
    void Emptied(const int factory)
    {
      const auto kept = mFactories.find(factory);
      if (kept == mFactories.end()) {
        return;
      }
      kept->second.ending = true;
      EndIfDone(factory, kept->second);
    }

    /// A unit a kept factory built is finished, of this kind, or went (`kind` none): the run it is in, 0 for none.
    [[nodiscard]] std::uint32_t Finished(const int unit, const Kind* const kind)
    {
      const auto building = mBuilding.find(unit);
      if (building == mBuilding.end()) {
        return 0;
      }
      const int factory = building->second;
      mBuilding.erase(building);
      const auto kept = mFactories.find(factory);
      if (kept == mFactories.end()) {
        return 0;
      }
      Factory& made = kept->second;
      std::uint32_t run = 0;
      if (kind != nullptr) {
        if (made.kind != kind) {
          made.kind = kind;
          made.run = ++mRuns;
        }
        run = made.run;
      }
      EndIfDone(factory, made);
      return run;
    }

    /// The factory went: dead, or no longer one's own.
    void Gone(const int factory)
    {
      mFactories.erase(factory);
    }

    void Clear()
    {
      mFactories.clear();
      mBuilding.clear();
    }

  private:
    struct Factory
    {
      std::uint32_t run = 0;
      const Kind* kind = nullptr;
      bool ending = false;
    };

    /// An ending run ends where the factory builds nothing any more: its next unit begins a run of its own.
    void EndIfDone(const int factory, Factory& made)
    {
      if (!made.ending) {
        return;
      }
      const bool builds =
        std::any_of(mBuilding.begin(), mBuilding.end(), [factory](const auto& unit) { return unit.second == factory; });
      if (!builds) {
        made.kind = nullptr;
        made.ending = false;
      }
    }

    std::unordered_map<int, Factory> mFactories;
    std::unordered_map<int, int> mBuilding;
    std::uint32_t mRuns = 0;
  };

  /// The fewest units that make a group.
  constexpr std::size_t kGroupUnits = 2;

  /// Whether the game draws a unit's strategic icon in place of its mesh at this zoom (CWldSession::RenderStrategicIcons):
  /// from its mesh's IconFadeInZoom on, but from 0.89 of the camera's farthest zoom on at the latest. `zoom` is the
  /// renderer's: the view's width at the camera's focus (its viewport's LOD row, with the camera's LOD scale).
  [[nodiscard]] inline bool DrawsIcon(const float zoom, const float iconFadeInZoom, const float maxZoom)
  {
    return !((std::min)(iconFadeInZoom, maxZoom * 0.89f) > zoom);
  }

  /// A group's icon on the picture: its middle and its radius.
  struct Icon
  {
    float x = 0;
    float y = 0;
    float radius = 0;
  };

  /// How far beside its disc a group's icon takes a tap: as far as the tap help reaches (`tapReach`), but while a selection
  /// that takes orders is there (`troops`: mobile units, or factories alone) only as far as FAF's pick then takes one's own
  /// unit beside a tap (`troopsReach`), so that a tap for an order beside one's own group orders.
  [[nodiscard]] inline float IconReach(const bool troops, const float tapReach, const float troopsReach)
  {
    return troops ? troopsReach : tapReach;
  }

  /// Whether a tap on a group's icon is the icon's, once FAF's cursor is known: not where a selection that takes orders is
  /// there and FAF's cursor is over a unit that is not one's own (`otherUnder`: an enemy's, an ally's), which the tap orders on.
  [[nodiscard]] inline bool IconTakesTap(const bool troops, const bool otherUnder)
  {
    return !(troops && otherUnder);
  }

  /// The icon a tap at (`x`, `y`) is on: of those whose middle is within its radius and `reach` of the tap, the nearest, and
  /// of icons as near the one drawn last, which lies over the others; -1 where there is none.
  [[nodiscard]] inline int IconAt(const std::vector<Icon>& icons, const float x, const float y, const float reach)
  {
    int found = -1;
    float nearest = 0;
    for (std::size_t at = 0; at < icons.size(); ++at) {
      const Icon& icon = icons[at];
      const float dx = icon.x - x;
      const float dy = icon.y - y;
      const float distance = dx * dx + dy * dy;
      const float within = icon.radius + reach;
      // As comparisons that hold, so that an icon whose place is no number is never found.
      if (distance <= within * within && (found < 0 || distance <= nearest)) {
        found = static_cast<int>(at);
        nearest = distance;
      }
    }
    return found;
  }
} // namespace griffgroups
