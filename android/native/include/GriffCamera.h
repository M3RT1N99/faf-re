#pragma once

// Which camera the touch scheme Griff's gestures move (android/TOUCH_UI.md, "Griff", the split screen). FAF splits the screen
// into two map views, each with a camera of its own (the map's options or the minimap's), and a finger moves the camera of the
// view it is on, as a PC's middle button and wheel move the one under the mouse. The program decides which
// (src/host/GriffAndroid.cpp) and griff.lua moves it; here the rules alone, for their tests (tests/griff/GriffCameraTests.cpp).

#include <optional>
#include <string_view>

namespace griffcamera
{
  /// FAF's map views by griff.lua's numbers (worldview.lua CreateMainWorldView): the main one, whose camera is WorldCamera,
  /// the left one where FAF splits the screen; and the right one there, whose camera is WorldCamera2.
  enum class View : int
  {
    kMain = 1,
    kRight = 2,
  };

  /// The view whose camera has this name: the main one for any other, as the minimap's, on which the fingers move the main
  /// camera, and for none, where they are on no map (a panel over it).
  [[nodiscard]] constexpr View ViewOfCamera(const std::string_view camera) noexcept
  {
    return camera == "WorldCamera2" ? View::kRight : View::kMain;
  }

  /// Which view's camera the fingers move. A pan's, the view its press came to, as the engine's camera drag moves that one
  /// (CUIWorldView::HandleEvent), and so does the glide after it. A pinch's, the view under it where it began, also while the
  /// finger left of its two goes on, wherever that finger is now, as a pan's drag stays with its view; and so does the glide
  /// after it.
  class Fingers
  {
  public:
    /// A pan's press came to `at`.
    void Pan(const View at) noexcept
    {
      mPan = at;
    }

    /// The view of the last pan, whose camera glides on after its lift.
    [[nodiscard]] View Panned() const noexcept
    {
      return mPan;
    }

    /// Two fingers, or the one left of them, pinch at a place: the view whose camera they move. A pinch that begins takes
    /// `viewAt()`, the view under the place, which is asked only then.
    template <class ViewAt>
    [[nodiscard]] View Pinch(const ViewAt& viewAt)
    {
      if (!mPinch) {
        mPinch = viewAt();
      }
      return *mPinch;
    }

    /// Their lift: the view of the pinch that went, whose camera glides on; none where no pinch went on. The next pinch
    /// takes the view under it anew.
    [[nodiscard]] std::optional<View> Lift() noexcept
    {
      const std::optional<View> lifted = mPinch;
      mPinch.reset();
      return lifted;
    }

  private:
    View mPan = View::kMain;
    std::optional<View> mPinch;
  };
} // namespace griffcamera
