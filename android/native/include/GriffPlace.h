#pragma once

// Where the touch scheme Griff's handle goes (android/TOUCH_UI.md, "Griff"): beside the selection so that it covers none of
// its units, or, where the group is out of view or under a panel, on the free map towards the view's middle with room for
// its arrow at the group. The free map is what the caller makes of the UI (src/host/GriffAndroid.cpp); the choice alone,
// for its tests (tests/wx/GriffPlaceTests.cpp). In the picture's pixels.

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace griffplace
{
  struct Box
  {
    float left = 0;
    float top = 0;
    float right = 0;
    float bottom = 0;

    [[nodiscard]] bool Contains(const float x, const float y) const
    {
      return x >= left && x < right && y >= top && y < bottom;
    }
  };

  /// The map's view less what covers it: FAF's panels, the minimap, the app's bar. Nothing is free in one made empty.
  class FreeMap
  {
  public:
    FreeMap() = default;

    FreeMap(const Box& view, std::vector<Box> covers)
      : mView(view)
      , mCovers(std::move(covers))
    {
    }

    [[nodiscard]] bool Free(const float x, const float y) const
    {
      if (!mView.Contains(x, y)) {
        return false;
      }
      return std::none_of(mCovers.begin(), mCovers.end(), [x, y](const Box& cover) { return cover.Contains(x, y); });
    }

    /// Whether the disc of `radius` about this middle lies within the view and touches none of its covers, however small they
    /// are: a finger anywhere on a handle there is on the free map.
    [[nodiscard]] bool DiscFree(const float x, const float y, const float radius) const
    {
      // As comparisons that hold, so that a middle that is no number has no room.
      if (!(x - radius >= mView.left && x + radius <= mView.right && y - radius >= mView.top && y + radius <= mView.bottom)) {
        return false;
      }
      return std::none_of(mCovers.begin(), mCovers.end(), [x, y, radius](const Box& cover) {
        // A box without area covers nothing; and the clamp below wants its edges in order.
        if (!(cover.left < cover.right && cover.top < cover.bottom)) {
          return false;
        }
        const float dx = x - std::clamp(x, cover.left, cover.right);
        const float dy = y - std::clamp(y, cover.top, cover.bottom);
        return dx * dx + dy * dy <= radius * radius;
      });
    }

  private:
    Box mView;
    std::vector<Box> mCovers;
  };

  struct Sizes
  {
    /// How far from its middle a finger is on the handle.
    float reach = 0;
    /// From its middle to the nearest edge of the group's units: its radius and a gap.
    float apart = 0;
    /// From its middle to the tip of its arrow at a group elsewhere.
    float arrowReach = 0;
  };

  struct Spot
  {
    bool shown = false;
    /// Not beside the group: the handle points at it.
    bool off = false;
    float x = 0;
    float y = 0;
  };

  /// For a group in view, above its units (`units`, their middle at `middleX`, `middleY`), and where the map has no room
  /// there below, right, left. For one out of view, or where none of these is free, as for a group under a panel: from the
  /// group's place on the screen towards the view's middle, a finger's reach at a time, as far from the view's edges as the
  /// arrow reaches, which then stays on the screen whichever way it points. None where the map has no room at all.
  [[nodiscard]] inline Spot Place(
    const FreeMap& free, const Sizes& sizes, const float middleX, const float middleY, const Box& units, const Box& view
  )
  {
    Spot spot;
    const float reach = sizes.reach;
    const float leftmost = view.left + reach;
    const float rightmost = view.right - reach;
    const float topmost = view.top + reach;
    const float bottommost = view.bottom - reach;
    if (reach <= 0 || leftmost > rightmost || topmost > bottommost) {
      return spot;
    }
    // Beside a group out of view the handle would stand at the view's edge as if the group were there.
    const bool inView = units.right > view.left && units.left < view.right && units.bottom > view.top && units.top < view.bottom;
    if (inView) {
      // Near the view's edge the handle shifts along it, to stay on the screen.
      const float besideX = std::clamp(middleX, leftmost, rightmost);
      const float besideY = std::clamp(middleY, topmost, bottommost);
      const std::pair<float, float> beside[] = {
        {besideX, units.top - sizes.apart},
        {besideX, units.bottom + sizes.apart},
        {units.right + sizes.apart, besideY},
        {units.left - sizes.apart, besideY},
      };
      for (const auto& [x, y] : beside) {
        if (x >= leftmost && x <= rightmost && y >= topmost && y <= bottommost && free.DiscFree(x, y, reach)) {
          return Spot{true, false, x, y};
        }
      }
    }
    const float margin = (std::max)(reach, sizes.arrowReach);
    if (view.left + margin > view.right - margin || view.top + margin > view.bottom - margin) {
      return spot;
    }
    const float startX = std::clamp(middleX, view.left + margin, view.right - margin);
    const float startY = std::clamp(middleY, view.top + margin, view.bottom - margin);
    const float centreX = (view.left + view.right) / 2;
    const float centreY = (view.top + view.bottom) / 2;
    const int steps = static_cast<int>(std::hypot(centreX - startX, centreY - startY) / reach) + 1;
    for (int step = 0; step <= steps; ++step) {
      const float along = static_cast<float>(step) / static_cast<float>(steps);
      const float x = startX + (centreX - startX) * along;
      const float y = startY + (centreY - startY) * along;
      if (free.DiscFree(x, y, reach)) {
        return Spot{true, true, x, y};
      }
    }
    return spot;
  }
} // namespace griffplace
