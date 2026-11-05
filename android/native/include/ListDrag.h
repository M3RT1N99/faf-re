#pragma once

// A finger that scrolls in a menu moves the list under it first, and what the list cannot take moves the page
// (android/TOUCH_UI.md): the sharing out of a drag, one to one on the glass, by whole lines with the rest carried. The
// engine's lists come by way of src/host/MenuFingers.cpp.

namespace listdrag
{
  /// What FAF's GetScrollValues says of a list: all of it in lines, and the lines it shows.
  struct Position
  {
    float rangeMin;
    float rangeMax;
    float visibleMin;
    float visibleMax;

    /// Lines to go towards the list's start.
    [[nodiscard]] float RoomUp() const
    {
      return visibleMin - rangeMin;
    }

    /// Lines to go towards the list's end.
    [[nodiscard]] float RoomDown() const
    {
      return rangeMax - visibleMax;
    }
  };

  /// One finger's drag on one list.
  class ListDrag
  {
  public:
    /**
     * `drag`: pixels the finger went, positive downwards, which brings the list's earlier lines. `scrollBy` moves the
     * list by a number of lines, positive towards its end, and says how many it really went. What comes back is what the
     * list could not take: at its end all of it, and otherwise nothing, as what is less than a line waits for the
     * next move.
     */
    template <typename ScrollBy>
    [[nodiscard]] float Take(const float drag, const Position& list, const float pixelsPerLine, ScrollBy&& scrollBy)
    {
      mCarried += drag;
      const bool towardsEnd = mCarried < 0;
      const float room = towardsEnd ? list.RoomDown() : list.RoomUp();
      const int wanted = static_cast<int>(-mCarried / pixelsPerLine);
      int lines = wanted;
      if (lines > 0 && static_cast<float>(lines) > room) {
        lines = room > 0 ? static_cast<int>(room) : 0;
      } else if (lines < 0 && static_cast<float>(-lines) > room) {
        lines = room > 0 ? -static_cast<int>(room) : 0;
      }
      const int moved = lines != 0 ? scrollBy(lines) : 0;
      mCarried += static_cast<float>(moved) * pixelsPerLine;
      if (moved != wanted || room < 1) {
        const float rest = mCarried;
        mCarried = 0;
        return rest;
      }
      return 0;
    }

  private:
    float mCarried = 0;
  };
} // namespace listdrag
