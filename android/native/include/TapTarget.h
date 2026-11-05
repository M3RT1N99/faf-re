#pragma once

// A finger is wider than FAF's small controls, so a tap beside one should still hit it (android/TOUCH_UI.md): the
// choice among the controls around a tap, which the engine's control tree offers one by one (UI_TapTarget, in the
// patch of src/moho/ui/CUIWorldViewEvents.cpp).

#include <algorithm>
#include <cmath>

namespace taptarget
{
  struct Place
  {
    float x;
    float y;
  };

  struct Box
  {
    float left;
    float top;
    float right;
    float bottom;

    /// Too small for a finger: a thing of a pixel or two is a line of the UI, not a thing to hit.
    [[nodiscard]] bool IsSmall(const float smallSide) const
    {
      const float side = (std::min)(right - left, bottom - top);
      return side >= 2 && side < smallSide;
    }

    /// How far a place is from the box: 0 inside. The square of it, as only the order counts.
    [[nodiscard]] float SquaredDistanceTo(const float x, const float y) const
    {
      const float dx = x < left ? left - x : (x >= right ? x - right : 0);
      const float dy = y < top ? top - y : (y >= bottom ? y - bottom : 0);
      return dx * dx + dy * dy;
    }

    /// The pixel of the box nearest to a place, which a press there hits (a control's hit test takes its left and top
    /// edges and leaves out its right and bottom ones). Sliders and scroll bars act on the place of a press, so a near
    /// miss has to mean what the finger was next to, not the middle of the bar. A place on the box stays as it is.
    [[nodiscard]] Place NearestPixel(const float x, const float y) const
    {
      return Place{
        (std::min)((std::max)(x, std::ceil(left)), std::ceil(right) - 1),
        (std::min)((std::max)(y, std::ceil(top)), std::ceil(bottom) - 1)
      };
    }
  };

  /// Whether a control's handlers can act on a press: it has one of its own, not `plain`, which a plain Text, Bitmap or
  /// Group has. A tooltip's wrapper is a handler too, and keeps what it wraps in `wrapped`: `plain` there is a label, with
  /// nothing for a press, and a real button's own there is still a button.
  template <class Handler>
  [[nodiscard]] bool ActsOnAPress(const Handler& handler, const Handler& wrapped, const Handler& plain)
  {
    return !handler.IsNil() && !(handler == plain) && (wrapped.IsNil() || !(wrapped == plain));
  }

  /// The nearest small control to a tap, within reach. The first of equally near ones stays.
  template <class Control>
  class Nearest
  {
  public:
    Nearest(const float x, const float y, const float reach, const float smallSide)
      : mX(x), mY(y), mSmallSide(smallSide), mReachSquared(reach * reach)
    {
    }

    /// Whether the control with this box would be the best so far, if it is one a finger can act on. `withinBest`: it is a
    /// part of the best so far, which it then goes ahead of whatever the distance. A small control around others is a row
    /// around its choice, say, and what is in it is what the finger meant: a walk from the root comes to the row first.
    [[nodiscard]] bool Wants(const Box& box, const bool withinBest) const
    {
      const float distance = box.SquaredDistanceTo(mX, mY);
      return box.IsSmall(mSmallSide) && distance <= mReachSquared && (mBest == nullptr || withinBest || distance < mBestDistance);
    }

    void Take(Control* const control, const Box& box)
    {
      mBest = control;
      mBox = box;
      mBestDistance = box.SquaredDistanceTo(mX, mY);
    }

    [[nodiscard]] Control* Best() const
    {
      return mBest;
    }

    /// Where the tap goes to hit the best: the tap's place on it, or what is nearest to that.
    [[nodiscard]] Place Target() const
    {
      return mBox.NearestPixel(mX, mY);
    }

  private:
    float mX;
    float mY;
    float mSmallSide;
    float mReachSquared;
    float mBestDistance = 0;
    Control* mBest = nullptr;
    Box mBox{};
  };
} // namespace taptarget
