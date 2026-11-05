// The sharing out of a finger's drag between a list and the page (include/ListDrag.h).

#include <ListDrag.h>

#include "Check.h"

#include <algorithm>

namespace
{
  constexpr float kPixelsPerLine = 40;

  /// A list of `lines` lines that shows `shown`, as FAF's Grid answers.
  struct List
  {
    int lines;
    int shown;
    int top = 0;
    int calls = 0;

    [[nodiscard]] listdrag::Position Position() const
    {
      return {0, static_cast<float>(lines), static_cast<float>(top), static_cast<float>(top + shown)};
    }

    int ScrollBy(const int by)
    {
      ++calls;
      const int before = top;
      top = (std::max)(0, (std::min)(lines - shown, top + by));
      return top - before;
    }
  };

  float Drag(listdrag::ListDrag& drag, List& list, const float pixels)
  {
    return drag.Take(pixels, list.Position(), kPixelsPerLine, [&list](const int by) { return list.ScrollBy(by); });
  }
} // namespace

WXT_TEST(ListDragLessThanALineWaitsForTheNextMove)
{
  listdrag::ListDrag drag;
  List list{10, 4, 3};
  WXT_CHECK_EQ(Drag(drag, list, -30), 0.0f);
  WXT_CHECK_EQ(list.top, 3);
  WXT_CHECK_EQ(list.calls, 0);
  WXT_CHECK_EQ(Drag(drag, list, -30), 0.0f);
  WXT_CHECK_EQ(list.top, 4);
  // 20 are left over, which count for the next line.
  WXT_CHECK_EQ(Drag(drag, list, -20), 0.0f);
  WXT_CHECK_EQ(list.top, 5);
}

WXT_TEST(ListDragAFingerGoingDownBringsTheEarlierLines)
{
  listdrag::ListDrag drag;
  List list{10, 4, 5};
  WXT_CHECK_EQ(Drag(drag, list, 85), 0.0f);
  WXT_CHECK_EQ(list.top, 3);
}

WXT_TEST(ListDragWhatTheListCannotTakeGoesToThePage)
{
  listdrag::ListDrag drag;
  List list{10, 4, 5};
  // One line is left to go: 40 of the 100 pixels.
  WXT_CHECK_EQ(Drag(drag, list, -100), -60.0f);
  WXT_CHECK_EQ(list.top, 6);
}

WXT_TEST(ListDragAtItsEndAllOfItGoesToThePageAtOnce)
{
  listdrag::ListDrag drag;
  List list{10, 4, 6};
  WXT_CHECK_EQ(Drag(drag, list, -3), -3.0f);
  WXT_CHECK_EQ(Drag(drag, list, -3), -3.0f);
  WXT_CHECK_EQ(list.calls, 0);
  // Back the other way the list goes first.
  WXT_CHECK_EQ(Drag(drag, list, 45), 0.0f);
  WXT_CHECK_EQ(list.top, 5);
}

WXT_TEST(ListDragTheCarriedPartIsLostToNoDirection)
{
  listdrag::ListDrag drag;
  List list{10, 4, 3};
  WXT_CHECK_EQ(Drag(drag, list, -30), 0.0f);
  // 30 up and then 30 down come to nothing.
  WXT_CHECK_EQ(Drag(drag, list, 30), 0.0f);
  WXT_CHECK_EQ(list.top, 3);
  WXT_CHECK_EQ(Drag(drag, list, -39), 0.0f);
  WXT_CHECK_EQ(list.top, 3);
}

WXT_TEST(ListDragAListThatRefusesCountsAsAtItsEnd)
{
  // A list's own idea of its end can differ from its numbers: lobby.lua's chat.
  listdrag::ListDrag drag;
  const listdrag::Position position{0, 10, 3, 7};
  const float rest = drag.Take(-80, position, kPixelsPerLine, [](int) { return 0; });
  WXT_CHECK_EQ(rest, -80.0f);
}

WXT_TEST(ListDragAListThatTakesAPartLeavesTheRest)
{
  listdrag::ListDrag drag;
  const listdrag::Position position{0, 10, 3, 7};
  const float rest = drag.Take(-120, position, kPixelsPerLine, [](int) { return 1; });
  WXT_CHECK_EQ(rest, -80.0f);
}
