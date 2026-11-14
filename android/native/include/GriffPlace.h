#pragma once

// Where the touch scheme Griff's handle goes (android/TOUCH_UI.md, "Griff"): beside the selection so that it covers none of
// its units, or, where the group is out of view or under a panel, on the free map towards the view's middle with room for
// its arrow at the group, and where that way is covered up to the middle, at the free place nearest the middle. The free map
// is what the caller makes of the UI (src/host/GriffAndroid.cpp); the choice alone, for its tests (tests/wx/GriffPlaceTests.cpp).
// In the picture's pixels.

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

    /// The middle nearest (`x`, `y`) of a disc of `radius` that DiscFree takes, with its middle within `bounds`, in `foundX` and
    /// `foundY`; false where there is none, or where (`x`, `y`) lies outside the bounds. Row by row outward from `y`, and on each
    /// row exactly where a middle keeps the disc off every cover, a pixel clear of what it would touch: a place is found however
    /// little room the map has for it across. Of places as near, the one below comes first, and on a row the one to the left.
    [[nodiscard]] bool NearestDiscFree(
      const float x, const float y, const float radius, const Box& bounds, float& foundX, float& foundY
    ) const
    {
      // Pixels from row to row, as the cross's steps.
      constexpr float kRow = 2;
      const float left = (std::max)(bounds.left, mView.left + radius);
      const float right = (std::min)(bounds.right, mView.right - radius);
      const float top = (std::max)(bounds.top, mView.top + radius);
      const float bottom = (std::min)(bounds.bottom, mView.bottom - radius);
      // As comparisons that hold, so that a start that is no number has no place either; nor have bounds without end, whose
      // rows would never run out.
      if (!(x >= left && x <= right && y >= top && y <= bottom && std::isfinite(bottom - top))) {
        return false;
      }
      // Where on a row a middle is too near a cover: a stretch of it a pixel longer at each end than where the disc touches the
      // cover, open, so that its ends are free of that cover.
      std::vector<std::pair<float, float>> tooNear;
      bool found = false;
      float nearest = 0;
      const auto onRow = [&](const float rowY) {
        tooNear.clear();
        for (const Box& cover : mCovers) {
          if (!(cover.left < cover.right && cover.top < cover.bottom)) {
            continue;
          }
          const float dy = rowY - std::clamp(rowY, cover.top, cover.bottom);
          if (dy * dy <= radius * radius) {
            const float half = std::sqrt(radius * radius - dy * dy) + 1;
            tooNear.emplace_back(cover.left - half, cover.right + half);
          }
        }
        // From x each way past every stretch that holds the place: in the order of their starts going right, and of the starts
        // backwards going left, a stretch passed never holds the place again.
        std::sort(tooNear.begin(), tooNear.end());
        float rightX = x;
        for (const auto& [from, to] : tooNear) {
          if (from < rightX && rightX < to) {
            rightX = to;
          }
        }
        float leftX = x;
        for (auto stretch = tooNear.rbegin(); stretch != tooNear.rend(); ++stretch) {
          if (stretch->first < leftX && leftX < stretch->second) {
            leftX = stretch->first;
          }
        }
        for (const float rowX : {leftX, rightX}) {
          const float distance = (rowX - x) * (rowX - x) + (rowY - y) * (rowY - y);
          if (rowX >= left && rowX <= right && (!found || distance < nearest)) {
            found = true;
            nearest = distance;
            foundX = rowX;
            foundY = rowY;
          }
        }
      };
      for (int step = 0;; ++step) {
        const float dy = kRow * static_cast<float>(step);
        // No row further out has a place nearer than the one found.
        if (found && dy * dy >= nearest) {
          return true;
        }
        if (y + dy > bottom && y - dy < top) {
          return found;
        }
        if (y + dy <= bottom) {
          onRow(y + dy);
        }
        if (step > 0 && y - dy >= top) {
          onRow(y - dy);
        }
      }
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

  /// How far the free map reaches from the view's middle, left, up and right, to what covers it or the view's edge: where the
  /// app keeps a word for the user, under FAF's top bar and clear of the panels at its sides. The view's where its middle is
  /// covered, and nothing where the view is none.
  struct Cross
  {
    float left = 0;
    float top = 0;
    float right = 0;
  };

  [[nodiscard]] inline Cross CrossAt(const FreeMap& free, const Box& view)
  {
    // Pixels a step: the walk to the edges is a few hundred steps of the covers.
    constexpr float kStep = 2;
    const float middleX = (view.left + view.right) / 2;
    const float middleY = (view.top + view.bottom) / 2;
    Cross cross{view.left, view.top, view.right};
    if (!free.Free(middleX, middleY)) {
      return cross;
    }
    for (cross.top = middleY; cross.top - kStep >= view.top && free.Free(middleX, cross.top - kStep);) {
      cross.top -= kStep;
    }
    for (cross.left = middleX; cross.left - kStep >= view.left && free.Free(cross.left - kStep, middleY);) {
      cross.left -= kStep;
    }
    for (cross.right = middleX; cross.right + kStep < view.right && free.Free(cross.right + kStep, middleY);) {
      cross.right += kStep;
    }
    return cross;
  }

  /// For a group in view, above its units (`units`, their middle at `middleX`, `middleY`), and where the map has no room
  /// there below, right, left. For one out of view, or where none of these is free, as for a group under a panel: from the
  /// group's place on the screen towards the view's middle, a finger's reach at a time, as far from the view's edges as the
  /// arrow reaches, which then stays on the screen whichever way it points; where all of that way is covered, the free place
  /// nearest the middle, as far from the edges. None where the map has no room at all.
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
    // Where the handle's middle may stand away from its group.
    const Box middles{view.left + margin, view.top + margin, view.right - margin, view.bottom - margin};
    if (middles.left > middles.right || middles.top > middles.bottom) {
      return spot;
    }
    const float startX = std::clamp(middleX, middles.left, middles.right);
    const float startY = std::clamp(middleY, middles.top, middles.bottom);
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
    // The line is covered up to the view's middle, as by a window over it: the free place nearest the middle, so that no cover
    // there takes the handle away while the map has room for it.
    float x = 0;
    float y = 0;
    if (free.NearestDiscFree(centreX, centreY, reach, middles, x, y)) {
      return Spot{true, true, x, y};
    }
    return spot;
  }
} // namespace griffplace
