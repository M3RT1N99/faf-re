#pragma once

// Fingers in the menus (android/TOUCH_UI.md), where a screen is read and scrolled:
//
//   - a tap is a click, also after a rest, as a careful finger is slow;
//   - a finger that rests kRightClickMilliseconds, which it feels a second time, and lifts without moving is a click of the
//     right button;
//   - a finger that moves mainly up or down scrolls, the list under it first and then the page, and presses nothing;
//   - a finger that moves mainly sideways is a mouse's drag from where it came down: a slider;
//   - a finger that rested kRestMilliseconds and then moves is a mouse's drag in any direction: a scroll bar's thumb, a
//     window;
//   - two fingers do nothing;
//   - a mouse's wheel on a page scrolls as a finger does, the list under the pointer first and then the page: a mouse can
//     click only what the view shows.
//
// Positions are the screen's pixels, which Mouse turns into the game's. As in a game the pointer goes shortly after the last
// finger has lifted, and in a scroll at once, as what it was over moves away from under it.

#include "touch/Fingers.h"
#include "touch/TouchInput.h"

namespace touch
{
  class PageInput final : public Fingers
  {
  public:
    /// What the fingers do, on the thread that calls this class.
    class Mouse
    {
    public:
      virtual ~Mouse() = default;

      virtual void Move(float x, float y) = 0;
      /// The left button goes down, on what a finger means by this place: small controls are near misses.
      virtual void Press(float x, float y) = 0;
      virtual void Release(float x, float y) = 0;
      /// A tap, a click of the left button, which the game's UI may give to a small control beside it.
      virtual void Tap(float x, float y) = 0;
      /// A click of the right button, which the game's UI may give to a small control beside it as a tap's.
      virtual void RightClick(float x, float y) = 0;
      /// A finger came down here: if it goes on to scroll, it is the list here that it scrolls.
      virtual void LookForList(float x, float y) = 0;
      /// The finger that came down last went this far on the screen, positive downwards, as a scroll.
      virtual void Scroll(float dy) = 0;
      /// A finger rested: what it does now is different, which the user should feel.
      virtual void Held() = 0;
      /// The pointer leaves what it is over, now.
      virtual void Leave() = 0;
      /// In TouchInput::kLeaveMilliseconds the pointer leaves what it is over.
      virtual void LeaveSoon() = 0;
      /// A finger came: the pointer stays, whatever LeaveSoon said, also if the leave is on its way to the game.
      virtual void Stay() = 0;
      /// Calls Timer after this long, in place of the time set before.
      virtual void StartTimer(Millis milliseconds) = 0;
      virtual void StopTimer() = 0;
    };

    /// How long a finger stays before it moves for a drag.
    static constexpr Millis kRestMilliseconds = 300;
    /// How long a finger stays before it lifts for a click of the right button.
    static constexpr Millis kRightClickMilliseconds = 500;
    /// After this long a wheel's next notch starts anew, with what list is under the pointer by then.
    static constexpr Millis kWheelPauseMilliseconds = 400;

    /// `slop`: how far a finger may wander in the screen's pixels and still count as staying.
    PageInput(Mouse& mouse, float slop);
    PageInput(const PageInput&) = delete;
    PageInput& operator=(const PageInput&) = delete;

    void Down(int id, float x, float y, Millis time, bool first) override;
    void Move(int id, float x, float y) override;
    void Moved(Millis time) override;
    void Up(int id, Millis time, bool last) override;
    void Forget(int id, Millis time) override;
    /// What is held goes up, as a mouse's would, and no gesture comes of what is left of the fingers.
    void Cancel() override;

    /// A mouse's wheel turned, this far on the screen, positive as a finger going down. A gesture under way has the list and
    /// the page to itself.
    void Wheel(float x, float y, float dy, Millis time);
    /// The time from Mouse::StartTimer has passed.
    void Timer();

  private:
    enum class State
    {
      /// No finger.
      kNone,
      /// One finger, and nothing shown yet of what it does.
      kPending,
      /// One finger rested: a move drags now.
      kRested,
      /// One finger rested longer: a move still drags, and lifting it is a click of the right button.
      kRestedLong,
      kScrolling,
      /// One finger holds the left button.
      kDragging,
      /// Two fingers, or more, on a gesture that had not begun.
      kTwo,
      /// Fingers left of a gesture that is over.
      kSpent,
    };

    void StartPending(int id, float x, float y, Millis time);
    [[nodiscard]] bool BeyondSlop() const;
    void StartDrag();
    void EndGesture();
    /// A click now, or when the cursor has settled: the game looks for what is under it once a frame.
    void Click(Millis time);
    void FinishClick();

    Mouse& mMouse;
    const float mSlop;
    State mState = State::kNone;
    int mFirstId = -1;
    int mSecondId = -1;
    float mX = 0;
    float mY = 0;
    float mStartX = 0;
    float mStartY = 0;
    Millis mStartTime = 0;
    /// Where the scroll has come to.
    float mLastY = 0;
    bool mClickWaits = false;
    TouchInput::ClickPlace mClick;
    /// The wheel's last notch: whether the list it asked for is still the one asked for last, and its place and time.
    bool mWheelAsked = false;
    float mWheelX = 0;
    float mWheelY = 0;
    Millis mWheelTime = 0;
  };
} // namespace touch
