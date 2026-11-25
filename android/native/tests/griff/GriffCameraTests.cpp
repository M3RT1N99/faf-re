// Which camera the touch scheme Griff's gestures move (include/GriffCamera.h): the view whose camera a finger moves, by the
// camera's name, and the view of a pan and of a pinch, which their glides keep, in FAF's split screen as in its one view.

#include <GriffCamera.h>

#include "Check.h"

namespace
{
  using griffcamera::Fingers;
  using griffcamera::View;
  using griffcamera::ViewOfCamera;
} // namespace

WXT_TEST(GriffCameraTheSplitScreensRightViewHasACameraOfItsOwn)
{
  WXT_CHECK_EQ(ViewOfCamera("WorldCamera2"), View::kRight);
  WXT_CHECK_EQ(ViewOfCamera("WorldCamera"), View::kMain);
  // The minimap's, and none where the fingers are on no map: the main camera, as in FAF's one view.
  WXT_CHECK_EQ(ViewOfCamera("MiniMap"), View::kMain);
  WXT_CHECK_EQ(ViewOfCamera(""), View::kMain);
  // griff.lua's numbers.
  WXT_CHECK_EQ(static_cast<int>(View::kMain), 1);
  WXT_CHECK_EQ(static_cast<int>(View::kRight), 2);
}

WXT_TEST(GriffCameraAPansGlideMovesTheViewItsPressCameTo)
{
  Fingers fingers;
  WXT_CHECK_EQ(fingers.Panned(), View::kMain);
  fingers.Pan(View::kRight);
  WXT_CHECK_EQ(fingers.Panned(), View::kRight);
  // A pinch takes the view under it, and the pan keeps its own.
  WXT_CHECK_EQ(fingers.Pinch([] { return View::kMain; }), View::kMain);
  WXT_CHECK_EQ(fingers.Panned(), View::kRight);
  fingers.Pan(View::kMain);
  WXT_CHECK_EQ(fingers.Panned(), View::kMain);
}

WXT_TEST(GriffCameraAPinchKeepsItsViewWhileTheFingerLeftOfItGoesOnAndTheNextTakesItsOwn)
{
  Fingers fingers;
  int asked = 0;
  const auto under = [&asked](const View view) {
    return [&asked, view] {
      ++asked;
      return view;
    };
  };
  WXT_CHECK_EQ(fingers.Pinch(under(View::kRight)), View::kRight);
  // The fingers move; then one lifts, and the other goes on from the main view: the right view's camera still, and the
  // view under the fingers is asked only as the pinch began.
  WXT_CHECK_EQ(fingers.Pinch(under(View::kRight)), View::kRight);
  WXT_CHECK_EQ(fingers.Pinch(under(View::kMain)), View::kRight);
  WXT_CHECK_EQ(asked, 1);
  // The lift: the right view's camera glides on. A finger again beside the one left pinches anew, on the view under it.
  WXT_CHECK(fingers.Lift() == View::kRight);
  WXT_CHECK_EQ(fingers.Pinch(under(View::kMain)), View::kMain);
  WXT_CHECK_EQ(asked, 2);
  WXT_CHECK(fingers.Lift() == View::kMain);
}

WXT_TEST(GriffCameraALiftWithNoPinchGoingOnGlidesNoCamera)
{
  Fingers fingers;
  WXT_CHECK(!fingers.Lift().has_value());
  (void)fingers.Pinch([] { return View::kRight; });
  WXT_CHECK(fingers.Lift() == View::kRight);
  WXT_CHECK(!fingers.Lift().has_value());
}
