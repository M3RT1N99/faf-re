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
#include <vector>

namespace griffgroups
{
  /// Where a group comes from: the gesture whose orders went to its units (the number of the program's watch over it), or
  /// the factory order that made them (the order's command id).
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

  /// Each unit's group: that of the gesture that ordered it last, or of the factory order that made it where no gesture has
  /// ordered it since, until it goes. A group of fewer than two units is none, and shows no icon.
  class Book
  {
  public:
    /// A gesture ordered these units: they leave their groups for the gesture's, with the units its other orders went to.
    void Ordered(const std::uint32_t gesture, const std::vector<int>& units)
    {
      for (const int unit : units) {
        Join(unit, Origin{false, gesture});
      }
    }

    /// A factory order made this unit: it is in the order's group, with the units the order made before that no gesture has
    /// ordered since, as long as no gesture orders it. A unit is made once: one the book holds stays where it is.
    void Made(const int unit, const std::uint32_t command)
    {
      if (!Holds(unit)) {
        Join(unit, Origin{true, command});
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
