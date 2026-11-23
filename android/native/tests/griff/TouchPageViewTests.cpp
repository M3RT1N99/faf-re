// How the picture lies on the screen (src/touch/PageView.h), with the phone's numbers: 1647x768 on 1544x720, and the lobby's
// panel. The cases of the app's PageViewTest.java.

#include <cmath>

#include "touch/Numbers.h"
#include "touch/PageView.h"

#include "Check.h"

/// JUnit's assertEquals with a delta: `actual` within `delta` of `expected`.
#define CHECK_NEAR(actual, expected, delta)                                                                              \
  WXT_CHECK(std::abs(static_cast<double>(actual) - static_cast<double>(expected)) <= (delta))

namespace
{
  constexpr float kNear = 0.51f;
  constexpr float kLobbyScale = 1544.0f / 1024;

  struct ViewFixture
  {
    touch::PageView view;

    ViewFixture()
    {
      view.SetScreen(1544, 720);
      view.SetPicture(1647, 768);
    }

    void Lobby()
    {
      view.SetPage(312, 0, 1336, 768);
    }

    /// A page as wide as the picture and higher than the screen, which is no zoom until the picture grows.
    void PageThatIsNoZoom()
    {
      view.SetPage(0, 0, 1647, 1400);
      WXT_CHECK(!view.Zoomed());
    }

    /// Where such a page stands once it is a zoom: ViewTop() says 0 for as long as it is not.
    float WhereItStandsOnceAZoom()
    {
      view.SetPicture(3000, 1400);
      WXT_CHECK(view.Zoomed());
      return view.ViewTop();
    }
  };
} // namespace

#define VIEW_TEST(name) WXT_FIXTURE_TEST(ViewFixture, name)

VIEW_TEST(PageViewWithoutAPageThePictureFillsTheScreen)
{
  WXT_CHECK(!view.Zoomed());
  WXT_CHECK_EQ(view.ViewWidth(), 1544);
  WXT_CHECK_EQ(view.ViewHeight(), 720);
  CHECK_NEAR(view.ViewLeft(), 0, 0);
  CHECK_NEAR(view.ViewTop(), 0, 0);
  CHECK_NEAR(view.Zoom(), 1, 0.001);
  WXT_CHECK_EQ(view.PictureX(772), 824);
  WXT_CHECK_EQ(view.PictureY(360), 384);
}

VIEW_TEST(PageViewAPageFillsTheScreensWidthAtItsLeftEdgeFromItsTop)
{
  Lobby();
  WXT_CHECK(view.Zoomed());
  CHECK_NEAR(view.Scale(), kLobbyScale, 0.001);
  WXT_CHECK_EQ(view.ViewWidth(), touch::Round(1647 * kLobbyScale));
  WXT_CHECK_EQ(view.ViewHeight(), touch::Round(768 * kLobbyScale));
  CHECK_NEAR(view.ViewLeft(), -touch::Round(312 * kLobbyScale), 0);
  CHECK_NEAR(view.ViewTop(), 0, 0);
  CHECK_NEAR(view.Zoom(), 1.608, 0.001);
}

VIEW_TEST(PageViewTheScreensEdgesAreThePagesEdges)
{
  Lobby();
  CHECK_NEAR(view.PictureX(0), 312, 1);
  CHECK_NEAR(view.PictureX(1544), 1336, 1);
  WXT_CHECK_EQ(view.PictureY(0), 0);
  WXT_CHECK_EQ(view.PictureY(720), touch::Round(720 / kLobbyScale));
}

VIEW_TEST(PageViewWhatThePictureHasHereIsWhereTheScreenShowsIt)
{
  CHECK_NEAR(view.ScreenX(824), 772, 1);
  CHECK_NEAR(view.ScreenY(384), 360, 1);
  Lobby();
  CHECK_NEAR(view.ScreenX(312), 0, 1);
  CHECK_NEAR(view.ScreenX(1336), 1544, 1);
  CHECK_NEAR(view.ScreenX(static_cast<float>(view.PictureX(500))), 500, 1);
  CHECK_NEAR(view.ScreenY(static_cast<float>(view.PictureY(700))), 700, 1);
}

VIEW_TEST(PageViewAFingerScrollsWithinThePagesHeight)
{
  Lobby();
  const float range = 768 * kLobbyScale - 720;
  view.ScrollBy(100);
  // Not above the page's top.
  CHECK_NEAR(view.ViewTop(), 0, 0);
  view.ScrollBy(-100);
  CHECK_NEAR(view.ViewTop(), -100, 0);
  WXT_CHECK_EQ(view.PictureY(0), touch::Round(100 / kLobbyScale));
  view.ScrollBy(-5000);
  CHECK_NEAR(view.ViewTop(), -range, kNear);
  WXT_CHECK_EQ(view.PictureY(720), 767);
  view.ScrollBy(50);
  CHECK_NEAR(view.ViewTop(), -range + 50, kNear);
}

VIEW_TEST(PageViewThePlaceIsAlwaysOnThePicture)
{
  Lobby();
  WXT_CHECK_EQ(view.PictureY(-50), 0);
  WXT_CHECK_EQ(view.PictureX(5000), 1646);
  WXT_CHECK_EQ(view.PictureX(-3000), 0);
}

VIEW_TEST(PageViewAPageWithinThePicturesHeightScrollsOnlyThroughItself)
{
  view.SetPage(336, 17, 1310, 751);
  const float scale = 1544.0f / 974;
  CHECK_NEAR(view.ViewTop(), -17 * scale, kNear);
  WXT_CHECK_EQ(view.PictureY(0), 17);
  view.ScrollBy(-5000);
  WXT_CHECK_EQ(view.PictureY(720), 751);
}

VIEW_TEST(PageViewAPageLowerThanTheScreenStandsInTheMiddleAndDoesNotScroll)
{
  view.SetPage(500, 300, 1100, 500);
  const float scale = 1544.0f / 600;
  const float middle = (720 - 200 * scale) / 2 - 300 * scale;
  CHECK_NEAR(view.ViewTop(), middle, kNear);
  view.ScrollBy(100);
  CHECK_NEAR(view.ViewTop(), middle, kNear);
  view.ScrollBy(-100);
  CHECK_NEAR(view.ViewTop(), middle, kNear);
}

VIEW_TEST(PageViewAPageAsWideAsThePictureIsNoZoom)
{
  view.SetPage(0, 0, 1647, 768);
  WXT_CHECK(!view.Zoomed());
  WXT_CHECK_EQ(view.ViewWidth(), 1544);
}

VIEW_TEST(PageViewAFingerMovesNoPageThatIsNoZoom)
{
  PageThatIsNoZoom();
  view.ScrollBy(-100);
  CHECK_NEAR(WhereItStandsOnceAZoom(), 0, 0);
}

VIEW_TEST(PageViewTheSamePageAgainChangesNothing)
{
  Lobby();
  view.ScrollBy(-100);
  WXT_CHECK(!view.SetPage(312, 0, 1336, 768));
  CHECK_NEAR(view.ViewTop(), -100, 0);
}

VIEW_TEST(PageViewClosingThePageTakesTheViewBack)
{
  Lobby();
  view.ScrollBy(-100);
  WXT_CHECK(view.ClearPage());
  WXT_CHECK(!view.ClearPage());
  WXT_CHECK(!view.Zoomed());
  CHECK_NEAR(view.ViewTop(), 0, 0);
  CHECK_NEAR(view.ViewLeft(), 0, 0);
  WXT_CHECK_EQ(view.ViewWidth(), 1544);
}

VIEW_TEST(PageViewAPageOpensAtItsTopEvenIfTheLastStoodLower)
{
  Lobby();
  view.ScrollBy(-100);
  view.ClearPage();
  Lobby();
  CHECK_NEAR(view.ViewTop(), 0, 0);
}

VIEW_TEST(PageViewAPageComesBackAsItStoodWhenOneOpenedOverIt)
{
  Lobby();
  view.ScrollBy(-100);
  view.SetPage(346, 38, 1302, 730);
  Lobby();
  CHECK_NEAR(view.ViewTop(), -100, 0);
}

VIEW_TEST(PageViewPagesNestAndComeBackAsTheyStood)
{
  Lobby();
  view.ScrollBy(-100);
  view.SetPage(346, 38, 1302, 730);
  view.ScrollBy(-50);
  const float second = view.ViewTop();
  view.SetPage(500, 0, 1147, 768);
  view.ScrollBy(-20);
  view.SetPage(346, 38, 1302, 730);
  CHECK_NEAR(view.ViewTop(), second, 0);
  Lobby();
  CHECK_NEAR(view.ViewTop(), -100, 0);
}

VIEW_TEST(PageViewAPageThatHasClosedOpensAgainAtItsTop)
{
  Lobby();
  view.SetPage(346, 38, 1302, 730);
  const float top = view.ViewTop();
  view.ScrollBy(-50);
  Lobby();
  WXT_CHECK(view.SetPage(346, 38, 1302, 730));
  CHECK_NEAR(view.ViewTop(), top, 0);
}

VIEW_TEST(PageViewAPageUnderTwoThatComesBackClosesThemBoth)
{
  Lobby();
  view.SetPage(346, 38, 1302, 730);
  const float top = view.ViewTop();
  view.ScrollBy(-50);
  view.SetPage(500, 0, 1147, 768);
  Lobby();
  view.SetPage(346, 38, 1302, 730);
  CHECK_NEAR(view.ViewTop(), top, 0);
}

VIEW_TEST(PageViewThePageWaitsForTheScreenAndThePicture)
{
  touch::PageView early;
  early.SetPage(312, 0, 1336, 768);
  WXT_CHECK(!early.Zoomed());
  early.SetScreen(1544, 720);
  WXT_CHECK(!early.Zoomed());
  early.SetPicture(1647, 768);
  WXT_CHECK(early.Zoomed());
  CHECK_NEAR(early.ViewTop(), 0, 0);
  CHECK_NEAR(early.Scale(), kLobbyScale, 0.001);
}

VIEW_TEST(PageViewWithTheKeyboardUpThePageGoesUpFurther)
{
  Lobby();
  view.SetKeyboard(360);
  view.ScrollBy(-5000);
  WXT_CHECK_EQ(view.PictureY(360), 767);
  view.SetKeyboard(0);
  WXT_CHECK_EQ(view.PictureY(720), 767);
}

VIEW_TEST(PageViewAFieldUnderTheKeyboardComesAboveIt)
{
  Lobby();
  view.SetKeyboard(360);
  view.Reveal(684, 706);
  const float bottom = view.ViewTop() + 706 * kLobbyScale;
  // Above the keyboard, and not far from it.
  WXT_CHECK(bottom <= 360);
  WXT_CHECK(bottom > 360 - 30);
}

VIEW_TEST(PageViewAFieldThatCanBeSeenStaysWhereItIs)
{
  Lobby();
  view.SetKeyboard(360);
  view.Reveal(100, 120);
  CHECK_NEAR(view.ViewTop(), 0, 0);
}

VIEW_TEST(PageViewAFieldAboveTheScreenComesDown)
{
  Lobby();
  view.ScrollBy(-400);
  view.Reveal(100, 120);
  WXT_CHECK(view.ViewTop() + 100 * kLobbyScale > 0);
}

VIEW_TEST(PageViewAFieldOnAPageThatIsNoZoomMovesNothing)
{
  PageThatIsNoZoom();
  view.SetKeyboard(360);
  view.Reveal(684, 706);
  view.SetKeyboard(0);
  CHECK_NEAR(WhereItStandsOnceAZoom(), 0, 0);
}
