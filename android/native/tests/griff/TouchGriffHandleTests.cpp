// Griff's handle as the screen shows it (src/touch/GriffHandle.h): the program's, kept while a stroke is drawn, gone without a
// game's frames. The cases of the app's GriffHandleTest.java.

#include "touch/GriffHandle.h"

#include "Check.h"

WXT_TEST(GriffHandleTheHandleIsWhereTheProgramPlacedIt)
{
  touch::GriffHandle handle;
  WXT_CHECK(handle.Place(true, true, 100, 50));
  WXT_CHECK(handle.Shown());
  WXT_CHECK_EQ(handle.X(), 100.0f);
  WXT_CHECK_EQ(handle.Y(), 50.0f);
  WXT_CHECK(handle.Place(true, true, 120, 60));
  WXT_CHECK_EQ(handle.X(), 120.0f);
  WXT_CHECK(handle.Place(true, false, 0, 0));
  WXT_CHECK(!handle.Shown());
}

WXT_TEST(GriffHandleAStrokeKeepsTheHandleWhereItBegan)
{
  touch::GriffHandle handle;
  handle.Place(true, true, 100, 50);
  // A new stroke, then the same stroke.
  WXT_CHECK(handle.Stroke(true));
  WXT_CHECK(!handle.Stroke(true));
  WXT_CHECK(!handle.Place(true, true, 300, 80));
  WXT_CHECK(!handle.Place(true, false, 0, 0));
  WXT_CHECK(handle.Shown());
  WXT_CHECK_EQ(handle.X(), 100.0f);
  handle.Stroke(false);
  WXT_CHECK(handle.Place(true, true, 300, 80));
  WXT_CHECK_EQ(handle.X(), 300.0f);
}

WXT_TEST(GriffHandleWithoutTheFramesThereIsNoHandleThoughAStrokeIsDrawn)
{
  // The game ends, or a dialog takes the fingers, while a finger draws a stroke: nothing reads the frames from then on, so
  // nothing would put a kept handle away; over the score screen or the dialog there is none.
  touch::GriffHandle handle;
  handle.Place(true, true, 100, 50);
  handle.Stroke(true);
  WXT_CHECK(handle.Place(false, false, 0, 0));
  WXT_CHECK(!handle.Shown());
  // The dialog goes while the finger still draws: the stroke keeps what the screen shows, none, until it ends.
  WXT_CHECK(!handle.Place(true, true, 300, 80));
  WXT_CHECK(!handle.Shown());
  handle.Stroke(false);
  WXT_CHECK(handle.Place(true, true, 300, 80));
  WXT_CHECK(handle.Shown());
  // Placed without the frames, as a caller might say it: still none.
  WXT_CHECK(handle.Place(false, true, 300, 80));
  WXT_CHECK(!handle.Shown());
}

WXT_TEST(GriffHandleStrokesAreNumberedInSixteenBits)
{
  touch::GriffHandle handle;
  const int first = handle.StrokeNumber();
  bool inRange = true;
  for (int stroke = 1; stroke <= 0x10000; ++stroke) {
    handle.Stroke(true);
    handle.Stroke(false);
    inRange = inRange && handle.StrokeNumber() >= 0 && handle.StrokeNumber() <= 0xFFFF;
  }
  WXT_CHECK(inRange);
  WXT_CHECK_EQ(handle.StrokeNumber(), first);
}
