#pragma once

// What the player opened of FAF's, which nothing of the touch scheme Griff's may cover (android/TOUCH_UI.md "Griff"): FAF's
// windows, its popups, the dropouts of its multifunction panel and the content of its tabs, as griff.lua reports those that show
// (fafre/opened.lua), in the picture's pixels. Box, Plus, the undo's button and the draft's tick and cross, which FAF's UI draws
// (griffhud.lua), step aside there as FAF's UI says, and the fingers' targets over them with them.

#include <algorithm>
#include <array>

namespace touch
{
  class Opened
  {
  public:
    /// More of FAF's controls open at once than a player opens, and their numbers.
    static constexpr int kMost = 16;
    static constexpr int kValues = 4 * kMost;

    /// How a piece shows (Visibility).
    enum class Shows
    {
      /// Not at all.
      kNot,
      /// Kept in its place unseen, which takes no finger and covers nothing.
      kAside,
      kSeen,
    };

    /// What the program gives now: `count` numbers of `values`, four a box, as many whole boxes as there are. Whether it
    /// changed.
    bool Update(const float* const values, const int count)
    {
      const int numbers = std::max(0, std::min(count, kValues)) / 4 * 4;
      const bool same = numbers == mCount && std::equal(values, values + numbers, mBoxes.begin());
      if (same) {
        return false;
      }
      std::copy(values, values + numbers, mBoxes.begin());
      mCount = numbers;
      return true;
    }

    /// Whether something the player opened lies under a piece with this box, in the picture's pixels: they share more than an
    /// edge, as a piece just beside it covers nothing of it, and one without a size covers nothing at all.
    [[nodiscard]] bool Under(const float left, const float top, const float right, const float bottom) const
    {
      if (right <= left || bottom <= top) {
        return false;
      }
      for (int at = 0; at < mCount; at += 4) {
        if (left < mBoxes[at + 2] && mBoxes[at] < right && top < mBoxes[at + 3] && mBoxes[at + 1] < bottom) {
          return true;
        }
      }
      return false;
    }

    /// How a piece shows: not at all where it is not wanted; where something the player opened lies under it, kept in its
    /// place unseen, which takes no finger and covers nothing while the pieces beside it stay where they are; else seen.
    [[nodiscard]] static Shows Visibility(const bool wanted, const bool aside)
    {
      return !wanted ? Shows::kNot : aside ? Shows::kAside : Shows::kSeen;
    }

  private:
    std::array<float, kValues> mBoxes{};
    int mCount = 0;
  };
} // namespace touch
