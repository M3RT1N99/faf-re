#include "touch/PageInput.h"

#include <cmath>

touch::PageInput::PageInput(Mouse& mouse, const float slop) : mMouse(mouse), mSlop(slop)
{}

void touch::PageInput::SetSlop(const float slop)
{
  mSlop = slop;
}

void touch::PageInput::Down(const int id, const float x, const float y, const Millis time, const bool first)
{
  // First: a leave on its way counts as taken back, whatever this finger goes on to send.
  mMouse.Stay();
  if (first) {
    if (mState != State::kNone && mState != State::kSpent) {
      EndGesture();
    }
    FinishClick();
    mSecondId = -1;
    StartPending(id, x, y, time);
  } else if (mState == State::kPending || mState == State::kRested || mState == State::kRestedLong) {
    mMouse.StopTimer();
    mSecondId = id;
    mState = State::kTwo;
  }
}

void touch::PageInput::StartPending(const int id, const float x, const float y, const Millis time)
{
  mFirstId = id;
  mX = x;
  mY = y;
  mStartX = x;
  mStartY = y;
  mStartTime = time;
  mWheelAsked = false;
  mMouse.Move(x, y);
  mMouse.LookForList(x, y);
  mMouse.StartTimer(kRestMilliseconds);
  mState = State::kPending;
}

void touch::PageInput::Move(const int id, const float x, const float y)
{
  if (id == mFirstId) {
    mX = x;
    mY = y;
  }
}

void touch::PageInput::Moved(const Millis /*time*/)
{
  switch (mState) {
    case State::kPending:
      if (BeyondSlop()) {
        mMouse.StopTimer();
        if (std::abs(mY - mStartY) >= std::abs(mX - mStartX)) {
          mMouse.Leave();
          mLastY = mY;
          mState = State::kScrolling;
        } else {
          StartDrag();
        }
      }
      break;
    case State::kRested:
    case State::kRestedLong:
      if (BeyondSlop()) {
        mMouse.StopTimer();
        StartDrag();
      }
      break;
    case State::kScrolling:
      if (mY != mLastY) {
        mMouse.Scroll(mY - mLastY);
        mLastY = mY;
      }
      break;
    case State::kDragging:
      mMouse.Move(mX, mY);
      break;
    default:
      break;
  }
}

bool touch::PageInput::BeyondSlop() const
{
  return Hypot(mX - mStartX, mY - mStartY) > mSlop;
}

void touch::PageInput::StartDrag()
{
  mMouse.Press(mStartX, mStartY);
  mMouse.Move(mX, mY);
  mState = State::kDragging;
}

void touch::PageInput::Up(const int id, const Millis time, const bool last)
{
  if (id == mFirstId) {
    switch (mState) {
      case State::kPending:
      case State::kRested:
        mMouse.StopTimer();
        Click(time);
        mState = State::kSpent;
        break;
      case State::kRestedLong:
        mMouse.RightClick(mStartX, mStartY);
        mState = State::kSpent;
        break;
      case State::kDragging:
        mMouse.Release(mX, mY);
        mState = State::kSpent;
        break;
      case State::kScrolling:
      case State::kTwo:
        mState = State::kSpent;
        break;
      default:
        break;
    }
  }
  if (last) {
    mState = State::kNone;
    mFirstId = -1;
    mSecondId = -1;
    mMouse.LeaveSoon();
  }
}

void touch::PageInput::Forget(const int id, const Millis time)
{
  if (mState == State::kTwo && id == mSecondId) {
    mSecondId = -1;
    StartPending(mFirstId, mX, mY, time);
  } else if (id == mFirstId) {
    Cancel();
  }
}

void touch::PageInput::Cancel()
{
  EndGesture();
  mMouse.LeaveSoon();
}

void touch::PageInput::EndGesture()
{
  mMouse.StopTimer();
  if (mState == State::kDragging) {
    mMouse.Release(mX, mY);
  }
  FinishClick();
  mState = State::kSpent;
}

void touch::PageInput::Wheel(const float x, const float y, const float dy, const Millis time)
{
  if (mState != State::kNone && mState != State::kSpent) {
    return;
  }
  if (!mWheelAsked || x != mWheelX || y != mWheelY || time - mWheelTime > kWheelPauseMilliseconds) {
    mMouse.LookForList(x, y);
    mWheelAsked = true;
    mWheelX = x;
    mWheelY = y;
  }
  mWheelTime = time;
  mMouse.Scroll(dy);
}

void touch::PageInput::Timer()
{
  if (mState == State::kPending) {
    mState = State::kRested;
    mMouse.Held();
    mMouse.StartTimer(kRightClickMilliseconds - kRestMilliseconds);
  } else if (mState == State::kRested) {
    mState = State::kRestedLong;
    mMouse.Held();
  }
  FinishClick();
}

void touch::PageInput::Click(const Millis time)
{
  mClick.ClickedAt(mStartX, mStartY, time, mSlop);
  mClickWaits = true;
  const Millis rested = time - mStartTime;
  if (rested >= TouchInput::kSettleMilliseconds) {
    FinishClick();
  } else {
    mMouse.StartTimer(TouchInput::kSettleMilliseconds - rested);
  }
}

void touch::PageInput::FinishClick()
{
  if (mClickWaits) {
    mMouse.StopTimer();
    mClickWaits = false;
    mMouse.Tap(mClick.X(), mClick.Y());
  }
}
