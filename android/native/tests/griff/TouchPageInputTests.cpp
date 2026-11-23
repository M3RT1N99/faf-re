// PageInput's gestures (src/touch/PageInput.h), as what reaches the game: the cases of the app's PageInputTest.java.

#include <string>
#include <vector>

#include "touch/PageInput.h"

#include "Check.h"

namespace
{
  using touch::Millis;
  using touch::PageInput;
  using touch::TouchInput;

  std::string I(const float value)
  {
    return std::to_string(touch::ToInt(value));
  }

  /// Writes down what the game gets, and runs the timer when a test says time has passed.
  struct Recorder final : PageInput::Mouse
  {
    std::vector<std::string> events;
    Millis timer = -1;
    bool leaving = false;

    void Move(const float x, const float y) override
    {
      events.push_back("move " + I(x) + " " + I(y));
    }

    void Press(const float x, const float y) override
    {
      events.push_back("press " + I(x) + " " + I(y));
    }

    void Release(const float x, const float y) override
    {
      events.push_back("release " + I(x) + " " + I(y));
    }

    void Tap(const float x, const float y) override
    {
      events.push_back("tap " + I(x) + " " + I(y));
    }

    void RightClick(const float x, const float y) override
    {
      events.push_back("right " + I(x) + " " + I(y));
    }

    void LookForList(const float x, const float y) override
    {
      events.push_back("list? " + I(x) + " " + I(y));
    }

    void Scroll(const float dy) override
    {
      events.push_back("scroll " + I(dy));
    }

    void Held() override
    {
      events.push_back("held");
    }

    void Leave() override
    {
      events.push_back("leave");
    }

    void LeaveSoon() override
    {
      leaving = true;
    }

    void Stay() override
    {
      leaving = false;
    }

    void StartTimer(const Millis milliseconds) override
    {
      timer = milliseconds;
    }

    void StopTimer() override
    {
      timer = -1;
    }
  };

  struct PageFixture
  {
    static constexpr float kSlop = 10;
    static constexpr int kFinger = 3;
    static constexpr int kOther = 4;

    Recorder mouse;
    PageInput input{mouse, kSlop};

    void MoveTo(const float x, const float y)
    {
      input.Move(kFinger, x, y);
      input.Moved(0);
    }
  };
} // namespace

#define PAGE_TEST(name) WXT_FIXTURE_TEST(PageFixture, name)
#define EXPECT_EVENTS(...) WXT_CHECK_EQ(mouse.events, (std::vector<std::string>{__VA_ARGS__}))

PAGE_TEST(PageAFingerThatLandsShowsWhatItIsOverAndAsksForTheList)
{
  input.Down(kFinger, 100, 200, 0, true);
  EXPECT_EVENTS("move 100 200", "list? 100 200");
  WXT_CHECK_EQ(mouse.timer, PageInput::kRestMilliseconds);
}

PAGE_TEST(PageAQuickTapClicksWhenTheCursorHasSettled)
{
  input.Down(kFinger, 100, 200, 0, true);
  mouse.events.clear();
  input.Up(kFinger, 50, true);
  EXPECT_EVENTS();
  WXT_CHECK_EQ(mouse.timer, TouchInput::kSettleMilliseconds - 50);
  input.Timer();
  EXPECT_EVENTS("tap 100 200");
  WXT_CHECK_EQ(mouse.timer, -1);
  WXT_CHECK(mouse.leaving);
}

PAGE_TEST(PageATapAfterARestIsAClickAtOnce)
{
  input.Down(kFinger, 100, 200, 0, true);
  input.Timer();
  mouse.events.clear();
  input.Up(kFinger, 400, true);
  EXPECT_EVENTS("tap 100 200");
  // The right click's time goes.
  WXT_CHECK_EQ(mouse.timer, -1);
}

PAGE_TEST(PageARestIsFeltAndALongerRestIsFeltAgain)
{
  input.Down(kFinger, 100, 200, 0, true);
  mouse.events.clear();
  input.Timer();
  EXPECT_EVENTS("held");
  WXT_CHECK_EQ(mouse.timer, PageInput::kRightClickMilliseconds - PageInput::kRestMilliseconds);
  input.Timer();
  EXPECT_EVENTS("held", "held");
}

PAGE_TEST(PageNothingMoreIsFeltAfterTheSecondTime)
{
  input.Down(kFinger, 100, 200, 0, true);
  input.Timer();
  input.Timer();
  mouse.events.clear();
  input.Timer();
  EXPECT_EVENTS();
}

PAGE_TEST(PageALongRestThatLiftsIsARightClickWhereItRests)
{
  input.Down(kFinger, 100, 200, 0, true);
  input.Timer();
  input.Timer();
  mouse.events.clear();
  input.Up(kFinger, 600, true);
  EXPECT_EVENTS("right 100 200");
  WXT_CHECK(mouse.leaving);
}

PAGE_TEST(PageWanderingWithinTheSlopIsStillARightClick)
{
  input.Down(kFinger, 100, 200, 0, true);
  input.Timer();
  MoveTo(106, 206);
  input.Timer();
  mouse.events.clear();
  input.Up(kFinger, 600, true);
  EXPECT_EVENTS("right 100 200");
}

PAGE_TEST(PageALongRestThatMovesStaysADrag)
{
  input.Down(kFinger, 100, 200, 0, true);
  input.Timer();
  input.Timer();
  mouse.events.clear();
  MoveTo(100, 240);
  input.Up(kFinger, 800, true);
  EXPECT_EVENTS("press 100 200", "move 100 240", "release 100 240");
}

PAGE_TEST(PageARestThatMovesBeforeItIsLongLeavesNoRightClickBehind)
{
  input.Down(kFinger, 100, 200, 0, true);
  input.Timer();
  MoveTo(100, 240);
  // The right click's time goes.
  WXT_CHECK_EQ(mouse.timer, -1);
  mouse.events.clear();
  input.Timer();
  input.Up(kFinger, 800, true);
  EXPECT_EVENTS("release 100 240");
}

PAGE_TEST(PageASecondFingerTakesTheRightClickBack)
{
  input.Down(kFinger, 100, 200, 0, true);
  input.Timer();
  input.Timer();
  input.Down(kOther, 300, 200, 600, false);
  mouse.events.clear();
  input.Up(kOther, 700, false);
  input.Up(kFinger, 800, true);
  EXPECT_EVENTS();
}

PAGE_TEST(PageWhenTheSystemTakesAFingerThatRestedNoRightClickComes)
{
  input.Down(kFinger, 100, 200, 0, true);
  input.Timer();
  input.Timer();
  mouse.events.clear();
  input.Cancel();
  input.Up(kFinger, 800, true);
  EXPECT_EVENTS();
}

PAGE_TEST(PageARightClickLeavesNothingForTheNextTapToPairWith)
{
  input.Down(kFinger, 100, 200, 0, true);
  input.Timer();
  input.Timer();
  input.Up(kFinger, 600, true);
  input.Down(kFinger, 110, 205, 650, true);
  mouse.events.clear();
  input.Up(kFinger, 800, true);
  EXPECT_EVENTS("tap 110 205");
}

PAGE_TEST(PageWanderingWithinTheSlopIsAStillATap)
{
  input.Down(kFinger, 100, 200, 0, true);
  MoveTo(106, 206);
  mouse.events.clear();
  input.Up(kFinger, 200, true);
  EXPECT_EVENTS("tap 100 200");
}

PAGE_TEST(PageAFingerThatGoesDownScrollsAndPressesNothing)
{
  input.Down(kFinger, 100, 200, 0, true);
  mouse.events.clear();
  MoveTo(102, 215);
  EXPECT_EVENTS("leave");
  // The rest's timer goes.
  WXT_CHECK_EQ(mouse.timer, -1);
  MoveTo(103, 240);
  MoveTo(103, 230);
  input.Up(kFinger, 300, true);
  EXPECT_EVENTS("leave", "scroll 25", "scroll -10");
  WXT_CHECK_EQ(mouse.timer, -1);
  WXT_CHECK(mouse.leaving);
}

PAGE_TEST(PageAScrollThatPausesGoesOnAsAScroll)
{
  input.Down(kFinger, 100, 200, 0, true);
  MoveTo(100, 220);
  input.Timer();
  mouse.events.clear();
  MoveTo(100, 230);
  EXPECT_EVENTS("scroll 10");
}

PAGE_TEST(PageAFingerThatGoesSidewaysDragsFromWhereItLanded)
{
  input.Down(kFinger, 100, 200, 0, true);
  mouse.events.clear();
  MoveTo(120, 203);
  MoveTo(150, 240);
  input.Up(kFinger, 300, true);
  EXPECT_EVENTS("press 100 200", "move 120 203", "move 150 240", "release 150 240");
  WXT_CHECK_EQ(mouse.timer, -1);
}

PAGE_TEST(PageAFingerThatRestedAndThenGoesDownDrags)
{
  input.Down(kFinger, 100, 200, 0, true);
  input.Timer();
  mouse.events.clear();
  MoveTo(100, 240);
  MoveTo(100, 260);
  input.Up(kFinger, 800, true);
  EXPECT_EVENTS("press 100 200", "move 100 240", "move 100 260", "release 100 260");
}

PAGE_TEST(PageTwoFingersDoNothing)
{
  input.Down(kFinger, 100, 200, 0, true);
  input.Down(kOther, 300, 200, 50, false);
  WXT_CHECK_EQ(mouse.timer, -1);
  mouse.events.clear();
  MoveTo(100, 300);
  input.Move(kOther, 300, 300);
  input.Moved(0);
  input.Timer();
  input.Up(kOther, 400, false);
  input.Up(kFinger, 500, true);
  EXPECT_EVENTS();
  WXT_CHECK(mouse.leaving);
}

PAGE_TEST(PageASecondFingerLeavesAScrollAsItIs)
{
  input.Down(kFinger, 100, 200, 0, true);
  MoveTo(100, 230);
  input.Down(kOther, 300, 200, 50, false);
  mouse.events.clear();
  MoveTo(100, 240);
  input.Move(kOther, 400, 200);
  input.Moved(0);
  EXPECT_EVENTS("scroll 10");
  input.Up(kFinger, 200, false);
  MoveTo(100, 300);
  input.Up(kOther, 300, true);
  EXPECT_EVENTS("scroll 10");
}

PAGE_TEST(PageASecondFingerLeavesADragAsItIs)
{
  input.Down(kFinger, 100, 200, 0, true);
  MoveTo(150, 200);
  input.Down(kOther, 300, 200, 50, false);
  mouse.events.clear();
  MoveTo(170, 200);
  input.Up(kOther, 100, false);
  MoveTo(190, 200);
  input.Up(kFinger, 200, true);
  EXPECT_EVENTS("move 170 200", "move 190 200", "release 190 200");
}

PAGE_TEST(PageAPalmIsAsIfItHadNotComeDown)
{
  input.Down(kFinger, 100, 200, 0, true);
  input.Down(kOther, 300, 200, 50, false);
  mouse.events.clear();
  input.Forget(kOther, 60);
  WXT_CHECK_EQ(mouse.timer, PageInput::kRestMilliseconds);
  mouse.events.clear();
  input.Up(kFinger, 300, true);
  EXPECT_EVENTS("tap 100 200");
}

PAGE_TEST(PageWhenTheSystemTakesTheFingersWhatIsHeldGoesUp)
{
  input.Down(kFinger, 100, 200, 0, true);
  MoveTo(150, 200);
  mouse.events.clear();
  input.Cancel();
  EXPECT_EVENTS("release 150 200");
  WXT_CHECK(mouse.leaving);
  mouse.events.clear();
  MoveTo(200, 200);
  EXPECT_EVENTS();
}

PAGE_TEST(PageWhenTheSystemTakesAFingerThatDidNothingNoClickComes)
{
  input.Down(kFinger, 100, 200, 0, true);
  mouse.events.clear();
  input.Cancel();
  input.Timer();
  EXPECT_EVENTS();
}

PAGE_TEST(PageANewFingerSendsTheWaitingClickFirst)
{
  input.Down(kFinger, 100, 200, 0, true);
  input.Up(kFinger, 20, true);
  mouse.events.clear();
  input.Down(kFinger, 300, 400, 40, true);
  EXPECT_EVENTS("tap 100 200", "move 300 400", "list? 300 400");
}

PAGE_TEST(PageTwoTapsNearEachOtherGoToTheSamePlaceForTheDoubleClick)
{
  input.Down(kFinger, 100, 200, 0, true);
  input.Up(kFinger, 150, true);
  input.Down(kFinger, 110, 205, 250, true);
  mouse.events.clear();
  input.Up(kFinger, 400, true);
  EXPECT_EVENTS("tap 100 200");
}

PAGE_TEST(PageTwoTapsFarApartStayWhereTheyLanded)
{
  input.Down(kFinger, 100, 200, 0, true);
  input.Up(kFinger, 150, true);
  input.Down(kFinger, 200, 205, 250, true);
  mouse.events.clear();
  input.Up(kFinger, 400, true);
  EXPECT_EVENTS("tap 200 205");
}

PAGE_TEST(PageAComingFingerTakesTheLeaveBack)
{
  input.Down(kFinger, 100, 200, 0, true);
  input.Up(kFinger, 150, true);
  WXT_CHECK(mouse.leaving);
  input.Down(kFinger, 100, 200, 250, true);
  WXT_CHECK(!mouse.leaving);
}

PAGE_TEST(PageAWheelScrollsTheListUnderThePointerAndAsksOnlyOnce)
{
  input.Wheel(100, 200, -60, 1000);
  input.Wheel(100, 200, -60, 1100);
  input.Wheel(100, 200, 60, 1200);
  EXPECT_EVENTS("list? 100 200", "scroll -60", "scroll -60", "scroll 60");
}

PAGE_TEST(PageAWheelAsksAgainAfterAPauseOrWhereThePointerHasGone)
{
  input.Wheel(100, 200, -60, 1000);
  mouse.events.clear();
  input.Wheel(100, 200, -60, 1000 + PageInput::kWheelPauseMilliseconds + 1);
  EXPECT_EVENTS("list? 100 200", "scroll -60");
  mouse.events.clear();
  input.Wheel(130, 200, -60, 1500);
  EXPECT_EVENTS("list? 130 200", "scroll -60");
}

PAGE_TEST(PageAFingerAsksForItsListSoAWheelAsksAgainAfterIt)
{
  input.Wheel(100, 200, -60, 1000);
  input.Down(kFinger, 300, 400, 1050, true);
  input.Up(kFinger, 1090, true);
  input.Timer();
  mouse.events.clear();
  input.Wheel(100, 200, -60, 1100);
  EXPECT_EVENTS("list? 100 200", "scroll -60");
}

PAGE_TEST(PageAWheelLeavesAFingerOnTheGlassItsListAndItsPage)
{
  input.Down(kFinger, 100, 200, 0, true);
  MoveTo(100, 230);
  mouse.events.clear();
  input.Wheel(300, 400, -60, 50);
  EXPECT_EVENTS();
  MoveTo(100, 240);
  EXPECT_EVENTS("scroll 10");
}

PAGE_TEST(PageAWheelGoesOnWhenTheSystemHasTakenTheFingers)
{
  input.Down(kFinger, 100, 200, 0, true);
  input.Cancel();
  mouse.events.clear();
  input.Wheel(300, 400, -60, 50);
  EXPECT_EVENTS("list? 300 400", "scroll -60");
}

PAGE_TEST(PageAMoveOfAFingerThatDoesNotCountIsNoMove)
{
  input.Down(kFinger, 100, 200, 0, true);
  mouse.events.clear();
  input.Move(kOther, 500, 500);
  input.Moved(0);
  EXPECT_EVENTS();
}
