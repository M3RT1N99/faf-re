#include "touch/TouchInput.h"

#include <algorithm>
#include <cmath>

#include "touch/Stroke.h"

// --- ClickPlace ------------------------------------------------------------------------------------------------------

bool touch::TouchInput::ClickPlace::ClickedAt(const float x, const float y, const Millis time, const float slop)
{
  const bool again = Recent(x, y, time, slop);
  mTime = time;
  if (!again) {
    mX = x;
    mY = y;
  }
  return again;
}

bool touch::TouchInput::ClickPlace::Recent(const float x, const float y, const Millis time, const float slop) const
{
  return time - mTime <= kDoubleTapMilliseconds && Hypot(x - mX, y - mY) <= 2 * slop;
}

// --- TapLadder -------------------------------------------------------------------------------------------------------

int touch::TouchInput::TapLadder::Next(
  const float x,
  const float y,
  const Millis lift,
  const int rail,
  const float reach,
  const float uiReach
)
{
  int step = kStepTap;
  if (mLast && !OffLadder(rail)) {
    const bool climbs = lift - mLift <= kLadderMilliseconds && Hypot(x - mX, y - mY) <= reach;
    switch (mKind) {
      case kTapUi:
        if (lift - mLift <= kDoubleTapMilliseconds && Hypot(x - mBaseX, y - mBaseY) <= uiReach) {
          step = kStepAgain;
        }
        break;
      case kTapOwn:
        if (climbs) {
          step = mRung >= 2 ? kStepAllOfType : kStepTypeInView;
        }
        break;
      case kTapGround:
        step = climbs ? kStepAttackMove : kStepTap;
        break;
      case kTapEmpty:
        step = climbs ? kStepAllMobile : kStepTap;
        break;
      case kTapGroup:
        step = climbs ? kStepGroupAgain : kStepTap;
        break;
      default:
        // Unknown yet, or nothing to climb from.
        break;
    }
  }
  if (step == kStepTap) {
    mBaseX = x;
    mBaseY = y;
    mKind = 0;
    mRung = 1;
    ++mTap;
  } else {
    mRung = std::min(mRung + 1, 3);
  }
  // An attack-move, the grab of the army and a group's camera are the top of their ladders.
  mLast = step != kStepAttackMove && step != kStepAllMobile && step != kStepGroupAgain;
  mLift = lift;
  mX = x;
  mY = y;
  return step;
}

void touch::TouchInput::TapLadder::Answered(const int tap, const int kind)
{
  if (tap == mTap && mRung == 1) {
    mKind = kind;
  }
}

bool touch::TouchInput::TapLadder::Climbing(
  const float x,
  const float y,
  const Millis time,
  const int rail,
  const float reach
) const
{
  return mLast && !OffLadder(rail) && mRung < 3 && mKind != kTapUi && mKind != kTapOther &&
    time - mLift <= kLadderMilliseconds && Hypot(x - mX, y - mY) <= reach;
}

bool touch::TouchInput::TapLadder::OffLadder(const int rail)
{
  return (rail & (kRailBox | kRailQueue)) != 0;
}

// --- TouchInput ------------------------------------------------------------------------------------------------------

touch::TouchInput::TouchInput(Mouse& mouse, const float slop) : mMouse(mouse), mSlop(slop)
{}

void touch::TouchInput::SetGriff(const bool griff)
{
  mGriff = griff;
}

void touch::TouchInput::SetMillimetre(const float pixels)
{
  mMillimetre = pixels;
  mGriffSlop = kGriffSlopMm * pixels;
}

void touch::TouchInput::SetSlop(const float slop)
{
  mSlop = slop;
}

void touch::TouchInput::SetSwapped(const bool swapped)
{
  mSwapped = swapped;
}

void touch::TouchInput::SetDeleting(const bool deleting)
{
  mDeleting = deleting;
}

void touch::TouchInput::SetScreen(const float width, const float height)
{
  mScreenWidth = width;
  mScreenHeight = height;
}

void touch::TouchInput::TapAnswered(const int tap, const int kind)
{
  mLadder.Answered(tap, kind);
}

void touch::TouchInput::ClearLadder()
{
  mLadder.Clear();
}

void touch::TouchInput::Panned(const int drag, const Millis time)
{
  if (drag != mDrag) {
    return;
  }
  mPanned = true;
  if (TakeWaiting()) {
    TwoOfPan(mWaiting.id, mWaiting.x, mWaiting.y, time);
    mWaiting.id = -1;
  }
}

void touch::TouchInput::Formation(const int drag)
{
  if (drag != mDrag) {
    return;
  }
  mFormation = true;
  if (TakeWaiting()) {
    mWaiting.id = -1;
    GiveUpFormation();
  }
}

bool touch::TouchInput::TakeWaiting()
{
  if (mWaiting.id < 0 || mState != State::kPressed || !mPressedGriff) {
    return false;
  }
  if (ClicksCame()) {
    mWaiting.id = -1;
    GiveUpForClicks();
    return false;
  }
  return true;
}

bool touch::TouchInput::StoreBuzzes()
{
  const bool buzzed = mRestRang;
  mRestRang = false;
  return !buzzed;
}

int touch::TouchInput::TapButton() const
{
  return mSwapped ? kButtonRight : kButtonLeft;
}

int touch::TouchInput::HoldButton() const
{
  return mSwapped ? kButtonLeft : kButtonRight;
}

float touch::TouchInput::Slop() const
{
  return mGriff ? mGriffSlop : mSlop;
}

void touch::TouchInput::Down(const int id, const float x, const float y, const Millis time, const bool first)
{
  // First: a leave on its way counts as taken back, whatever this finger goes on to send.
  mMouse.Stay();
  if (first) {
    if (Live() && ClicksCame()) {
      GiveUpForClicks();
    } else if (Live()) {
      EndGesture();
    }
    FinishClick();
    BringCursorBackToGhost();
    mSecond.id = -1;
    mWaiting.id = -1;
    StartOneFinger(id, x, y, time);
  } else if (Live() && ClicksCame()) {
    // The finger joins a gesture the program's clicks came into: both rest until they lift.
    GiveUpForClicks();
  } else if (mState == State::kPlaceLine) {
    GiveUpLine();
    mState = State::kSpent;
  } else if (mState == State::kStroke && mStrokeFar <= mGriffSlop) {
    // A second finger while the first has not left the handle: two fingers, whose quick tap lets the selection go as
    // anywhere else, and which pinch where they move. Nothing was given up: no word of it; a handle's rest goes.
    HideStroke();
    HideRing();
    mMouse.StopTimer();
    StartTwo(id, x, y, time);
  } else if (mState == State::kStroke) {
    // A second finger gives the route up.
    GiveUpStroke();
    mState = State::kSpent;
  } else if (mState == State::kDraftLine || mState == State::kDraftMove || mState == State::kDraftBox) {
    // The mockup's: a second finger gives the line, the move or the box up, and the two pinch at once, no tap of two, which
    // would drop the whole draft.
    GiveUpDraftGesture();
    StartTwo(id, x, y, time);
    StartPinch();
  } else if (mState == State::kDraftPending || mState == State::kDraftAim) {
    GiveUpDraftGesture();
    mMouse.StopTimer();
    StartTwo(id, x, y, time);
  } else if (
    mState == State::kPending || mState == State::kRested || mState == State::kPlacePending ||
    mState == State::kPlaceLineReady
  ) {
    mGhostToRestore = mState != State::kPending && mState != State::kRested && mGhostPlaced;
    mMouse.StopTimer();
    HideRing();
    RestGone();
    StartTwo(id, x, y, time);
  } else if (mState == State::kPressed && mPressedGriff && mPanned) {
    TwoOfPan(id, x, y, time);
  } else if (mState == State::kPressed && mPressedGriff && mFormation) {
    GiveUpFormation();
  } else if (mState == State::kPressed && mPressedGriff && mWaiting.id < 0) {
    // Griff: the program has not said yet what the drag is (one program frame after it began): the finger waits for its word,
    // which takes it as above.
    mWaiting.id = id;
    mWaiting.Place(x, y);
  } else if (mState == State::kPinchPan) {
    // Griff: a finger again beside the one left of a pinch pinches anew, from where the two are; the camera moved, and their
    // lift is no tap of two.
    EndPinch(false, time);
    if (mFollowed != &mFirst) {
      mFirst.id = mFollowed->id;
      mFirst.Place(mFollowed->x, mFollowed->y);
    }
    StartTwo(id, x, y, time);
    mNoTapOfTwo = true;
  }
}

void touch::TouchInput::StartTwo(const int id, const float x, const float y, const Millis time)
{
  mSecond.id = id;
  mSecond.Place(x, y);
  mSecondX = x;
  mSecondY = y;
  mFirstAtTwoX = mFirst.x;
  mFirstAtTwoY = mFirst.y;
  mStartX = MiddleX();
  mStartY = MiddleY();
  mStartDistance = Distance();
  mStartTime = time;
  mNoTapOfTwo = false;
  mLadder.Clear();
  mMouse.Move(x, y);
  mState = State::kTwoPending;
}

void touch::TouchInput::TwoOfPan(const int id, const float x, const float y, const Millis time)
{
  mMouse.GriffDragEnd(mFirst.x, mFirst.y, 0, 0);
  mPressedGriff = false;
  StartTwo(id, x, y, time);
  mNoTapOfTwo = true;
}

void touch::TouchInput::GiveUpFormation()
{
  mMouse.GriffDragGiveUp(mFirst.x, mFirst.y);
  mPressedGriff = false;
  mMouse.StrokeGivenUp();
  mState = State::kSpent;
}

void touch::TouchInput::StartOneFinger(const int id, const float x, const float y, const Millis time)
{
  mFirstDown = time;
  mOnHandle = false;
  mRestInDraft = false;
  const int rail = mGriff ? mMouse.Rail() : 0;
  mFingerBox = rail & kRailBox;
  if (mGriff) {
    // The runs of the program's clicks so far: one that begins while the gesture goes on gives it up (ClicksCame).
    mClickRuns = mMouse.ClickRuns();
    // A finger on the map stops the camera's glide, and FAF's reach for a unit beside a tap follows the selection, or Box,
    // which picks units: the finger's tap and rest go by this Box (FingerRail).
    mMouse.GriffDown(x, y, mFingerBox != 0);
    if (mMouse.Clicking()) {
      // The program's clicks run: the fingers rest until they are over, also on FAF's ghost of the tick's build mode and on
      // the handle. Only the down went, which then does nothing but end clicks that wait past their patience.
      mState = State::kSpent;
      return;
    }
  }
  if (!mDeleting && mMouse.Placing()) {
    StartPlacing(id, x, y, time);
  } else if (mGriff && mMouse.Drafting()) {
    // The draft: fingers make plans, and there is no handle.
    mGhostPlaced = false;
    StartDraft(id, x, y, time);
  } else if (mGriff && mMouse.OnGriff(x, y)) {
    mGhostPlaced = false;
    if (mLadder.Climbing(x, y, time, rail, kLadderMm * mMillimetre)) {
      // The ladder first: a second tap on a unit is its double click, even where the first one has raised the handle. Where
      // its lift does not climb after all, it is the handle's tap (FinishClick).
      mOnHandle = true;
      StartPending(id, x, y, time);
    } else {
      // After the third rung, or with Plus counting, the handle takes a drag at once.
      StartStroke(id, x, y, time);
    }
  } else {
    mGhostPlaced = false;
    StartPending(id, x, y, time);
  }
}

void touch::TouchInput::StartPending(const int id, const float x, const float y, const Millis time)
{
  mFirst.id = id;
  mFirst.Place(x, y);
  mStartX = x;
  mStartY = y;
  mStartTime = time;
  StartTrail(x, y, time);
  mMouse.Move(x, y);
  mMouse.StartTimer(mGriff ? kRestMilliseconds : kHoldMilliseconds);
  mState = State::kPending;
}

void touch::TouchInput::StartDraft(const int id, const float x, const float y, const Millis time)
{
  mFirst.id = id;
  mFirst.Place(x, y);
  mStartX = x;
  mStartY = y;
  mStartTime = time;
  StartTrail(x, y, time);
  mLadder.Clear();
  mDraftPlan = mMouse.DraftPlan(x, y);
  mMouse.Move(x, y);
  // Griff's rest first, which on FAF's UI is the panel's (DraftRestOnUi); on the map the aim comes after
  // kDraftRestMilliseconds.
  mDraftRestAsked = false;
  mMouse.StartTimer(kRestMilliseconds);
  mState = State::kDraftPending;
}

void touch::TouchInput::StartStroke(const int id, const float x, const float y, const Millis time)
{
  mFirst.id = id;
  mFirst.Place(x, y);
  mStartX = x;
  mStartY = y;
  mStartTime = time;
  mStrokeCount = 0;
  mStrokeFar = 0;
  AddStrokePoint(x, y);
  StartTrail(x, y, time);
  mLadder.Clear();
  mDistributes = false;
  ShowStroke();
  mHoverAt = time;
  mHoverWaits = false;
  mMouse.StrokeHover(x, y);
  // The handle's rest, unless the finger leaves the slop first (LeaveHandleRest).
  mHandleRestWaits = true;
  mMouse.StartTimer(kRestMilliseconds);
  mState = State::kStroke;
}

void touch::TouchInput::RestOnHandle()
{
  mHandleRestWaits = false;
  mMouse.StopTimer();
  mHandleRests = (mHandleRests + 1) & 0xFFFF;
  mMouse.HandleRest(mHandleRests);
}

bool touch::TouchInput::LeaveHandleRest()
{
  if (mHandleRestWaits) {
    mHandleRestWaits = false;
    mMouse.StopTimer();
  }
  if (!mDistributes) {
    return false;
  }
  mDistributes = false;
  HideRing();
  return true;
}

void touch::TouchInput::Distributes(const int rest)
{
  if (mState != State::kStroke || rest != mHandleRests || mHandleRestWaits || mStrokeFar > mGriffSlop || mDistributes) {
    return;
  }
  if (ClicksCame()) {
    GiveUpForClicks();
    return;
  }
  mDistributes = true;
  mRing = true;
  mMouse.Ring(true, mFirst.x, mFirst.y);
  mMouse.Held();
  ShowStroke();
}

void touch::TouchInput::TakeForHandle()
{
  const float x = mFirst.x;
  const float y = mFirst.y;
  StartStroke(mFirst.id, mStartX, mStartY, mStartTime);
  mFirst.Place(x, y);
}

void touch::TouchInput::StartPlacing(const int id, const float x, const float y, const Millis time)
{
  mFirst.id = id;
  mFirst.Place(x, y);
  mStartX = x;
  mStartY = y;
  mStartTime = time;
  mMouse.StartTimer(kHoldMilliseconds);
  mState = State::kPlacePending;
}

void touch::TouchInput::Move(const int id, const float x, const float y)
{
  if (id == mFirst.id) {
    mFirst.Place(x, y);
  } else if (id == mSecond.id) {
    mSecond.Place(x, y);
  } else if (id == mWaiting.id) {
    mWaiting.Place(x, y);
  }
}

void touch::TouchInput::Moved(const Millis time)
{
  if (Live() && ClicksCame()) {
    GiveUpForClicks();
    return;
  }
  switch (mState) {
    case State::kPending:
      AddTrail(time);
      if (!mDeleting && Hypot(mFirst.x - mStartX, mFirst.y - mStartY) > Slop()) {
        mMouse.StopTimer();
        if (!mGriff) {
          Press(TapButton(), mStartX, mStartY);
          mMouse.Move(mFirst.x, mFirst.y);
        } else if (mOnHandle) {
          // The handle's finger, which the ladder had only for a quick lift: its stroke from where it came down, with this
          // move, which the settled lift may count.
          TakeForHandle();
          AddTrail(time);
          StrokeTo(mFirst.x, mFirst.y, time);
        } else {
          StartGriffDrag(false, time);
        }
      }
      break;
    case State::kRested:
      AddTrail(time);
      if (Hypot(mFirst.x - mStartX, mFirst.y - mStartY) > mGriffSlop) {
        HideRing();
        // The draft's rest on FAF's UI slides as the panel's drag, with none of the rail: Plus stays for the tick.
        StartGriffDrag(true, time, mRestInDraft ? 0 : mMouse.Rail());
      } else if (mRing) {
        mMouse.Ring(true, mFirst.x, mFirst.y);
      }
      break;
    case State::kStroke:
      AddTrail(time);
      StrokeTo(mFirst.x, mFirst.y, time);
      if (mRing) {
        mMouse.Ring(true, mFirst.x, mFirst.y);
      }
      break;
    case State::kDraftPending:
      AddTrail(time);
      if (Hypot(mFirst.x - mStartX, mFirst.y - mStartY) > mGriffSlop) {
        mMouse.StopTimer();
        StartDraftDrag(time);
      }
      break;
    case State::kDraftLine:
      AddTrail(time);
      mMouse.DraftLine(kDraftShow, mFirst.x, mFirst.y);
      break;
    case State::kDraftMove:
      AddTrail(time);
      mMouse.DraftMove(kDraftShow, mStartX, mStartY, mFirst.x, mFirst.y);
      break;
    case State::kDraftAim:
      AddTrail(time);
      mMouse.DraftAim(kDraftShow, mFirst.x, mFirst.y);
      break;
    case State::kDraftBox:
      AddTrail(time);
      mMouse.DraftBox(kDraftShow, mStartX, mStartY, mFirst.x, mFirst.y);
      break;
    case State::kPressed:
      mMouse.Move(mFirst.x, mFirst.y);
      if (mPressedGriff) {
        Follow(time);
      }
      break;
    case State::kPlacePending:
      if (Hypot(mFirst.x - mStartX, mFirst.y - mStartY) > mSlop) {
        mMouse.StopTimer();
        StartTakingGhost();
      }
      break;
    case State::kPlaceLineReady:
      if (Hypot(mFirst.x - mStartX, mFirst.y - mStartY) > mSlop) {
        StartLine();
      }
      break;
    case State::kPlaceDragging:
    case State::kPlaceLine:
      MoveGhost();
      break;
    case State::kTwoPending:
      if (mGriff) {
        // The mockup's: two fingers that hardly move may still be a tap, and past that they pinch at once. Each finger counts
        // from where it was as the two began, so a twist about a still middle pinches too, moving nothing.
        const float still = kTwoFingersStillMm * mMillimetre;
        if (
          std::abs(Distance() - mStartDistance) >= still || Hypot(MiddleX() - mStartX, MiddleY() - mStartY) >= still ||
          Hypot(mFirst.x - mFirstAtTwoX, mFirst.y - mFirstAtTwoY) >= still ||
          Hypot(mSecond.x - mSecondX, mSecond.y - mSecondY) >= still
        ) {
          StartPinch();
        }
      } else if (std::abs(Distance() - mStartDistance) > 2 * mSlop) {
        StartZooming();
      } else if (Hypot(MiddleX() - mStartX, MiddleY() - mStartY) > mSlop) {
        StartPanning();
      }
      break;
    case State::kPanning:
      mMouse.Move(MiddleX(), MiddleY());
      if (std::abs(Distance() - mStartDistance) > 6 * mSlop) {
        mMouse.Button(kButtonMiddle, false, MiddleX(), MiddleY());
        StartZooming();
      }
      break;
    case State::kZooming:
      Zoom();
      break;
    case State::kPinching:
      mMouse.Pinch(false, MiddleX(), MiddleY(), Distance());
      break;
    case State::kPinchPan:
      mMouse.Pinch(false, mFollowed->x, mFollowed->y, 0);
      Follow(time);
      break;
    default:
      break;
  }
}

void touch::TouchInput::Up(const int id, const Millis time, const bool last)
{
  if (id == mWaiting.id) {
    // It waited for the program's word, which did not come: it was no gesture's.
    mWaiting.id = -1;
  }
  if ((id == mFirst.id || id == mSecond.id) && Live() && ClicksCame()) {
    GiveUpForClicks();
  }
  if (id == mFirst.id || id == mSecond.id) {
    switch (mState) {
      case State::kPending:
        mMouse.StopTimer();
        if (mGriff) {
          mClickRail = FingerRail();
          mClickOnHandle = mOnHandle;
          Click(kGriffTap, mStartX, mStartY, time - mStartTime, time);
        } else {
          Click(mDeleting ? kDeleteOrder : TapButton(), mStartX, mStartY, time - mStartTime, time);
        }
        mState = State::kSpent;
        break;
      case State::kRested:
        RestTap();
        mState = State::kSpent;
        break;
      case State::kStroke:
        FinishStroke(time);
        mState = State::kSpent;
        break;
      case State::kDraftPending:
        mMouse.StopTimer();
        mClickPlan = mDraftPlan;
        Click(kDraftTap, mStartX, mStartY, time - mStartTime, time);
        mState = State::kSpent;
        break;
      case State::kDraftLine:
      case State::kDraftMove:
      case State::kDraftAim:
      case State::kDraftBox:
        FinishDraftGesture(time);
        mState = State::kSpent;
        break;
      case State::kPressed:
        ReleasePressed(true, time);
        mState = State::kSpent;
        break;
      case State::kPlacePending:
        mMouse.StopTimer();
        TapWhilePlacing();
        mState = State::kSpent;
        break;
      case State::kPlaceDragging:
      case State::kPlaceLineReady:
        mState = State::kSpent;
        break;
      case State::kPlaceLine:
        mMouse.Button(kButtonLeft, false, mGhostX, mGhostY);
        mGhostUsed = true;
        mState = State::kSpent;
        break;
      case State::kTwoPending:
        // Griff counts both fingers, from the first one down: a finger that rested and a quick second are no tap.
        if (!mNoTapOfTwo && time - (mGriff ? mFirstDown : mStartTime) <= kTwoFingerTapMilliseconds) {
          if (mGriff && !mMouse.Placing() && mMouse.Drafting()) {
            mMouse.DraftDrop();
          } else if (mGriff && !mMouse.Placing()) {
            mMouse.Deselect();
          } else {
            const int button = mMouse.Placing() ? kButtonRight : HoldButton();
            Click(button, mSecondX, mSecondY, time - mStartTime, time);
          }
        }
        mState = State::kSpent;
        break;
      case State::kPanning:
        mMouse.Button(kButtonMiddle, false, MiddleX(), MiddleY());
        mState = State::kSpent;
        break;
      case State::kZooming:
        mState = State::kSpent;
        break;
      case State::kPinching:
        // The finger left moves the camera on from where it is, as a pan.
        StartPinchPan(id == mFirst.id ? mSecond : mFirst, time);
        break;
      case State::kPinchPan:
        if (id == mFollowed->id) {
          EndPinch(true, time);
          mState = State::kSpent;
        }
        break;
      default:
        break;
    }
  }
  if (last) {
    BringCursorBackToGhost();
    mState = State::kNone;
    mFirst.id = -1;
    mSecond.id = -1;
    mWaiting.id = -1;
    mMouse.LeaveSoon();
  }
}

void touch::TouchInput::Forget(const int id, const Millis time)
{
  if (id == mWaiting.id) {
    // A palm that waited for the program's word.
    mWaiting.id = -1;
  } else if ((id == mFirst.id || id == mSecond.id) && Live() && ClicksCame()) {
    // The finger left rests, as the gesture it was part of is given up.
    GiveUpForClicks();
  } else if (mState == State::kTwoPending && (id == mFirst.id || id == mSecond.id)) {
    const Finger left = id == mFirst.id ? mSecond : mFirst;
    mSecond.id = -1;
    StartOneFinger(left.id, left.x, left.y, time);
  } else if (id == mFirst.id || id == mSecond.id) {
    Cancel();
  }
}

void touch::TouchInput::Cancel()
{
  if (Live() && ClicksCame()) {
    GiveUpForClicks();
  } else {
    EndGesture();
  }
  mMouse.LeaveSoon();
}

bool touch::TouchInput::Live() const
{
  return mState != State::kNone && mState != State::kSpent;
}

bool touch::TouchInput::ClicksCame()
{
  return mGriff && (mMouse.Clicking() || mMouse.ClickRuns() != mClickRuns);
}

void touch::TouchInput::GiveUpForClicks()
{
  mMouse.StopTimer();
  switch (mState) {
    case State::kStroke:
      HideStroke();
      mMouse.StrokeGivenUp();
      break;
    case State::kDraftLine:
    case State::kDraftMove:
    case State::kDraftAim:
    case State::kDraftBox:
      GiveUpDraftGesture();
      break;
    case State::kPinching:
    case State::kPinchPan:
      EndPinch(false, 0);
      break;
    default:
      break;
  }
  HideRing();
  RestGone();
  mPressedGriff = false;
  mGhostToRestore = false;
  mClickButton = kNoClick;
  mLadder.Clear();
  if (mState != State::kNone) {
    mState = State::kSpent;
  }
}

void touch::TouchInput::EndGesture()
{
  mMouse.StopTimer();
  if (mState == State::kPressed && mPressedGriff) {
    // Griff's drag goes without a lift: FAF's formation is given up, as a route is; any other ends as at a lift.
    mMouse.GriffDragGiveUp(mFirst.x, mFirst.y);
    mPressedGriff = false;
  } else if (mState == State::kPressed) {
    ReleasePressed(false, 0);
  } else if (mState == State::kPanning) {
    mMouse.Button(kButtonMiddle, false, MiddleX(), MiddleY());
  } else if (mState == State::kPlaceLine) {
    GiveUpLine();
  } else if (mState == State::kStroke) {
    // A route goes only where a finger lifted: the system's taking the fingers gives it up.
    HideStroke();
  } else if (
    mState == State::kDraftLine || mState == State::kDraftMove || mState == State::kDraftAim || mState == State::kDraftBox
  ) {
    GiveUpDraftGesture();
  } else if (mState == State::kPinching || mState == State::kPinchPan) {
    EndPinch(false, 0);
  }
  HideRing();
  RestGone();
  FinishClick();
  BringCursorBackToGhost();
  mState = State::kSpent;
}

void touch::TouchInput::Timer()
{
  if ((Live() || mClickButton != kNoClick) && ClicksCame()) {
    GiveUpForClicks();
    return;
  }
  if (mState == State::kPending && !mDeleting) {
    if (mGriff && mOnHandle) {
      // The handle's finger, which the ladder had only for a quick lift, rested: the handle's rest, never the map's, which
      // would be the right button at the handle's place, a move or a formation there.
      TakeForHandle();
      RestOnHandle();
    } else if (mGriff) {
      StartRest();
    } else {
      Press(HoldButton(), mStartX, mStartY);
      mMouse.Held();
    }
  } else if (mState == State::kPlacePending) {
    if (StartedOnGhost()) {
      mState = State::kPlaceLineReady;
    } else {
      StartTakingGhost();
    }
    mMouse.Held();
  } else if (mState == State::kStroke && mHandleRestWaits) {
    RestOnHandle();
  } else if (mState == State::kStroke && mHoverWaits) {
    mHoverWaits = false;
    mHoverAt += kHoverMilliseconds;
    mMouse.StrokeHover(mFirst.x, mFirst.y);
  } else if (mState == State::kDraftPending && !mDraftRestAsked) {
    // Griff's rest: the program says whether the finger is on FAF's UI, whose rest it is then; meanwhile the aim's time goes
    // on, which a slow answer leaves as it was, the aim with the UI's right click at its lift.
    mDraftRestAsked = true;
    mDraftRests = (mDraftRests + 1) & 0xFFFF;
    mMouse.DraftRest(mStartX, mStartY, mDraftRests);
    mMouse.StartTimer(kDraftRestMilliseconds - kRestMilliseconds);
  } else if (mState == State::kDraftPending && mDraftPlan == 0 && (mMouse.Rail() & kRailBox) == 0) {
    // A rest on no plan, without Box (the mockup's): the plan its lift places shows under it, with the loupe.
    mState = State::kDraftAim;
    mMouse.DraftAim(kDraftShow, mFirst.x, mFirst.y);
    mMouse.Held();
  }
  FinishClick();
}

void touch::TouchInput::StartRest()
{
  mState = State::kRested;
  mLadder.Clear();
  if (mFingerBox == 0) {
    mRing = true;
    mMouse.Ring(true, mFirst.x, mFirst.y);
    mMouse.Held();
    mRestSaid = true;
    mMouse.GriffRest(mStartX, mStartY);
  }
}

void touch::TouchInput::DraftRestOnUi(const int rest)
{
  if (mState != State::kDraftPending || !mDraftRestAsked || rest != mDraftRests) {
    return;
  }
  if (ClicksCame()) {
    GiveUpForClicks();
    return;
  }
  mMouse.StopTimer();
  mRestInDraft = true;
  StartRest();
}

void touch::TouchInput::BringCursorBackToGhost()
{
  if (mGhostToRestore) {
    mGhostToRestore = false;
    mMouse.Move(mGhostX, mGhostY);
  }
}

void touch::TouchInput::StartTakingGhost()
{
  if (!mGhostPlaced) {
    // Its first place: above the finger, where the finger does not cover it.
    mGhostPlaced = true;
    mGhostX = mStartX;
    mGhostY = mStartY - kGhostAboveSlops * mSlop;
  }
  mGhostFingerX = mStartX;
  mGhostFingerY = mStartY;
  mGhostUsed = false;
  mState = State::kPlaceDragging;
  MoveGhost();
}

void touch::TouchInput::MoveGhost()
{
  mGhostX = std::max(0.0f, std::min(mScreenWidth, mGhostX + mFirst.x - mGhostFingerX));
  mGhostY = std::max(0.0f, std::min(mScreenHeight, mGhostY + mFirst.y - mGhostFingerY));
  mGhostFingerX = mFirst.x;
  mGhostFingerY = mFirst.y;
  mMouse.Move(mGhostX, mGhostY);
}

bool touch::TouchInput::StartedOnGhost() const
{
  return mGhostPlaced && !mGhostUsed && Hypot(mStartX - mGhostX, mStartY - mGhostY) <= kGhostReachSlops * mSlop;
}

void touch::TouchInput::StartLine()
{
  mLineStartX = mGhostX;
  mLineStartY = mGhostY;
  mMouse.StartLine(mGhostX, mGhostY);
  mGhostFingerX = mStartX;
  mGhostFingerY = mStartY;
  mState = State::kPlaceLine;
  MoveGhost();
}

void touch::TouchInput::GiveUpLine()
{
  mMouse.CancelLine();
  mGhostX = mLineStartX;
  mGhostY = mLineStartY;
  mMouse.Move(mGhostX, mGhostY);
}

void touch::TouchInput::TapWhilePlacing()
{
  const bool onGhost = StartedOnGhost();
  mGhostUsed = onGhost;
  if (onGhost) {
    mMouse.TapNearGhost(mStartX, mStartY, mGhostX, mGhostY);
    return;
  }
  mGhostPlaced = true;
  mGhostX = mStartX;
  mGhostY = mStartY;
  mMouse.Button(kButtonLeft, true, mGhostX, mGhostY);
  mMouse.Button(kButtonLeft, false, mGhostX, mGhostY);
}

void touch::TouchInput::Press(const int button, const float x, const float y)
{
  mPressed = button;
  mPressedGriff = false;
  mMouse.Button(button, true, x, y);
  mState = State::kPressed;
}

void touch::TouchInput::StartGriffDrag(const bool rested, const Millis time)
{
  StartGriffDrag(rested, time, mMouse.Rail());
}

void touch::TouchInput::StartGriffDrag(const bool rested, const Millis time, const int rail)
{
  // The program takes what the rest found with the drag.
  mRestSaid = false;
  mPressedGriff = true;
  // A pan glides on (ReleasePressed); only the program can tell it from a panel's or the minimap's drag (Panned), and a
  // rested drag on the ground from a box (Formation).
  mMayPan = !rested && (rail & kRailBox) == 0;
  mPanned = false;
  mFormation = false;
  ++mDrag;
  mLadder.Clear();
  mMouse.GriffDrag(mStartX, mStartY, mFirst.x, mFirst.y, rested, rail, mDrag);
  mMouse.Move(mFirst.x, mFirst.y);
  StartFollowing(mFirst, time);
  mState = State::kPressed;
  // A box is an action that Box and Plus count for, a pan is none.
  const int used = !mMayPan ? rail & (kRailBox | kRailQueue) : 0;
  if (used != 0) {
    mMouse.RailUsed(used);
  }
}

void touch::TouchInput::RestTap()
{
  HideRing();
  // The program takes what the rest found with its lift.
  mRestSaid = false;
  // It rang and buzzed unless Box counted at the finger's down (Timer), which on a panel is a rest all the same.
  mRestRang = mFingerBox == 0;
  // The draft's rest on FAF's UI takes no Plus, as the draft's tap there: a lit one stays for the tick.
  const int rail = mRestInDraft ? FingerRail() & ~kRailQueue : FingerRail();
  mMouse.RestTap(mStartX, mStartY, rail);
  if (rail != 0 && (rail & kRailBox) == 0) {
    mMouse.RailUsed(rail);
  }
}

int touch::TouchInput::FingerRail()
{
  return (mMouse.Rail() & ~kRailBox) | mFingerBox;
}

void touch::TouchInput::ReleasePressed(const bool glide, const Millis time)
{
  if (mPressedGriff) {
    const bool moving = glide && mMayPan && time - mFollowTime < kGlideMilliseconds;
    mMouse.GriffDragEnd(
      mFirst.x, mFirst.y, moving ? kGlideSpeed * mVelocityX : 0, moving ? kGlideSpeed * mVelocityY : 0
    );
    mPressedGriff = false;
  } else {
    mMouse.Button(mPressed, false, mFirst.x, mFirst.y);
  }
}

void touch::TouchInput::HideRing()
{
  if (mRing) {
    mRing = false;
    mMouse.Ring(false, 0, 0);
  }
}

void touch::TouchInput::RestGone()
{
  if (mRestSaid) {
    mRestSaid = false;
    mMouse.GriffRestGone();
  }
}

// --- Griff's draft ---------------------------------------------------------------------------------------------------

void touch::TouchInput::StartDraftDrag(const Millis time)
{
  const bool movable = mDraftPlan != 0 && !mMouse.DraftOnDeposit(mDraftPlan);
  mLadder.Clear();
  if ((mMouse.Rail() & kRailBox) != 0 && mMouse.DraftPicksDeposit()) {
    mState = State::kDraftBox;
    mMouse.DraftBox(kDraftShow, mStartX, mStartY, mFirst.x, mFirst.y);
  } else if (movable && mMouse.DraftNewest(mDraftPlan)) {
    mState = State::kDraftLine;
    mMouse.DraftLine(kDraftShow, mFirst.x, mFirst.y);
  } else if (movable) {
    mState = State::kDraftMove;
    mMouse.DraftMove(kDraftShow, mStartX, mStartY, mFirst.x, mFirst.y);
  } else {
    StartGriffDrag(false, time, 0);
  }
}

void touch::TouchInput::FinishDraftGesture(const Millis time)
{
  const int settled = Settled(time);
  const float x = mTrailX[settled];
  const float y = mTrailY[settled];
  switch (mState) {
    case State::kDraftLine:
      mMouse.DraftLine(kDraftEnd, x, y);
      break;
    case State::kDraftMove:
      mMouse.DraftMove(kDraftEnd, mStartX, mStartY, x, y);
      break;
    case State::kDraftAim:
      mMouse.DraftAim(kDraftEnd, x, y);
      break;
    case State::kDraftBox:
      mMouse.DraftBox(kDraftEnd, mStartX, mStartY, x, y);
      mMouse.RailUsed(kRailBox);
      break;
    default:
      break;
  }
}

void touch::TouchInput::GiveUpDraftGesture()
{
  switch (mState) {
    case State::kDraftLine:
      mMouse.DraftLine(kDraftAbort, mFirst.x, mFirst.y);
      break;
    case State::kDraftMove:
      mMouse.DraftMove(kDraftAbort, mStartX, mStartY, mFirst.x, mFirst.y);
      break;
    case State::kDraftAim:
      mMouse.DraftAim(kDraftAbort, mFirst.x, mFirst.y);
      break;
    case State::kDraftBox:
      mMouse.DraftBox(kDraftAbort, mStartX, mStartY, mFirst.x, mFirst.y);
      break;
    default:
      break;
  }
}

// --- Griff's trail, velocity and settled lift ------------------------------------------------------------------------

void touch::TouchInput::StartTrail(const float x, const float y, const Millis time)
{
  mTrailCount = 0;
  mTrailNext = 0;
  Trail(x, y, time);
}

void touch::TouchInput::AddTrail(const Millis time)
{
  Trail(mFirst.x, mFirst.y, time);
}

void touch::TouchInput::Trail(const float x, const float y, const Millis time)
{
  mTrailX[mTrailNext] = x;
  mTrailY[mTrailNext] = y;
  mTrailTime[mTrailNext] = time;
  mTrailNext = (mTrailNext + 1) % kTrail;
  mTrailCount = std::min(mTrailCount + 1, kTrail);
}

int touch::TouchInput::Settled(const Millis time) const
{
  int at = (mTrailNext - 1 + kTrail) % kTrail;
  for (int k = 1; k < mTrailCount && mTrailTime[at] > time - kSettleLiftMilliseconds; ++k) {
    at = (at - 1 + kTrail) % kTrail;
  }
  return at;
}

void touch::TouchInput::StartFollowing(Finger& finger, const Millis time)
{
  mFollowed = &finger;
  mFollowX = finger.x;
  mFollowY = finger.y;
  mFollowTime = time;
  mVelocityX = 0;
  mVelocityY = 0;
}

void touch::TouchInput::Follow(const Millis time)
{
  const float seconds = static_cast<float>(std::max<Millis>(1, time - mFollowTime)) / 1000.0f;
  mVelocityX = kVelocityKept * mVelocityX + (1 - kVelocityKept) * (mFollowed->x - mFollowX) / seconds;
  mVelocityY = kVelocityKept * mVelocityY + (1 - kVelocityKept) * (mFollowed->y - mFollowY) / seconds;
  mFollowX = mFollowed->x;
  mFollowY = mFollowed->y;
  mFollowTime = time;
}

// --- Griff's pinch ---------------------------------------------------------------------------------------------------

void touch::TouchInput::StartPinch()
{
  mGhostUsed = false;
  mMouse.Pinch(true, MiddleX(), MiddleY(), Distance());
  mState = State::kPinching;
}

void touch::TouchInput::StartPinchPan(Finger& left, const Millis time)
{
  mMouse.Pinch(true, left.x, left.y, 0);
  StartFollowing(left, time);
  mState = State::kPinchPan;
}

void touch::TouchInput::EndPinch(const bool glide, const Millis time)
{
  const bool moving = glide && mState == State::kPinchPan && time - mFollowTime < kGlideMilliseconds;
  mMouse.PinchEnd(moving ? kGlideSpeed * mVelocityX : 0, moving ? kGlideSpeed * mVelocityY : 0);
}

// --- Griff's stroke --------------------------------------------------------------------------------------------------

void touch::TouchInput::AddStrokePoint(const float x, const float y)
{
  // Past the room for points the last one follows the finger: the route keeps its end.
  const int at = std::min(mStrokeCount, kStrokePoints - 1);
  mStrokeX[at] = x;
  mStrokeY[at] = y;
  mStrokeCount = at + 1;
}

void touch::TouchInput::StrokeTo(const float x, const float y, const Millis time)
{
  mStrokeFar = std::max(mStrokeFar, static_cast<float>(Hypot(x - mStartX, y - mStartY)));
  if (mStrokeFar > mGriffSlop && LeaveHandleRest()) {
    // Past the slop the handle's rest is over: the route's meaning, which a short loop back gives up.
    ShowStroke();
  }
  Hover(x, y, time);
  const int last = mStrokeCount - 1;
  if (
    mStrokeFar <= kStrokePointMm * mMillimetre ||
    Hypot(x - mStrokeX[last], y - mStrokeY[last]) < kStrokePointMm * mMillimetre
  ) {
    return;
  }
  AddStrokePoint(x, y);
  ShowStroke();
}

void touch::TouchInput::Hover(const float x, const float y, const Millis time)
{
  if (time - mHoverAt >= kHoverMilliseconds) {
    mHoverAt = time;
    if (mHoverWaits) {
      mHoverWaits = false;
      mMouse.StopTimer();
    }
    mMouse.StrokeHover(x, y);
  } else if (!mHoverWaits && !mHandleRestWaits) {
    // The handle's rest has the timer while its time runs: a hover within the slop comes with a later move then.
    mHoverWaits = true;
    mMouse.StartTimer(mHoverAt + kHoverMilliseconds - time);
  }
}

int touch::TouchInput::StrokeWaypoints()
{
  return stroke::Waypoints(
    mStrokeX.data(),
    mStrokeY.data(),
    mStrokeCount,
    kWaypointMm * mMillimetre,
    kCornerMm * mMillimetre,
    mWaypointX.data(),
    mWaypointY.data(),
    kMaxWaypoints
  );
}

void touch::TouchInput::ShowStroke()
{
  if (mDistributes) {
    // The handle's rest: the label alone, no route.
    mMouse.StrokeShown(
      mStrokeX.data(), mStrokeY.data(), mStrokeCount, stroke::kDistribute, mWaypointX.data(), mWaypointY.data(), 0
    );
    return;
  }
  const int mode = stroke::Classify(mStrokeX.data(), mStrokeY.data(), mStrokeCount, mMillimetre);
  const int waypoints = StrokeWaypoints();
  mMouse.StrokeShown(mStrokeX.data(), mStrokeY.data(), mStrokeCount, mode, mWaypointX.data(), mWaypointY.data(), waypoints);
}

void touch::TouchInput::HideStroke()
{
  mHoverWaits = false;
  mMouse.StrokeShown(mStrokeX.data(), mStrokeY.data(), 0, stroke::kCancel, mWaypointX.data(), mWaypointY.data(), 0);
}

void touch::TouchInput::GiveUpStroke()
{
  mMouse.StopTimer();
  HideStroke();
  mMouse.StrokeGivenUp();
}

void touch::TouchInput::TrimStrokeTo(const float x, const float y)
{
  int best = mStrokeCount - 1;
  double bestDistance = std::numeric_limits<double>::max();
  for (int k = mStrokeCount - 1; k >= std::max(1, mStrokeCount - kTrimPoints); --k) {
    const double distance = Hypot(mStrokeX[k] - x, mStrokeY[k] - y);
    if (distance < bestDistance) {
      bestDistance = distance;
      best = k;
    }
  }
  mStrokeCount = best + 1;
  mStrokeX[best] = x;
  mStrokeY[best] = y;
}

void touch::TouchInput::FinishStroke(const Millis time)
{
  mMouse.StopTimer();
  HideStroke();
  HideRing();
  if (mStrokeFar <= mGriffSlop) {
    if (mDistributes) {
      // As the PC's G with the cursor at the group's middle; as the handle's tap, it uses none of the rail.
      mMouse.Distribute();
    } else {
      mMouse.GriffTap();
    }
    return;
  }
  const int settled = Settled(time);
  AddStrokePoint(mFirst.x, mFirst.y);
  TrimStrokeTo(mTrailX[settled], mTrailY[settled]);
  const int mode = stroke::Classify(mStrokeX.data(), mStrokeY.data(), mStrokeCount, mMillimetre);
  if (mode == stroke::kCancel) {
    mMouse.StrokeGivenUp();
    return;
  }
  const int rail = mMouse.Rail();
  const int waypoints = StrokeWaypoints();
  mMouse.Route(mWaypointX.data(), mWaypointY.data(), waypoints, rail, mode == stroke::kPatrol);
  if ((rail & kRailQueue) != 0) {
    mMouse.RailUsed(kRailQueue);
  }
}

void touch::TouchInput::Click(const int button, const float x, const float y, const Millis rested, const Millis time)
{
  mClickButton = button;
  if (button == kGriffTap || button == kDraftTap) {
    // Griff's tap is decided by what the cursor rests on, at the finger's place, so it clicks there; its ladder decides where
    // a double click goes.
    mClickLift = time;
    mClickX = x;
    mClickY = y;
  } else {
    mLastClick.ClickedAt(x, y, time, mSlop);
    mClickX = mLastClick.X();
    mClickY = mLastClick.Y();
  }
  if (rested >= kSettleMilliseconds) {
    FinishClick();
  } else {
    mMouse.StartTimer(kSettleMilliseconds - rested);
  }
}

void touch::TouchInput::FinishClick()
{
  if (mClickButton == kNoClick) {
    return;
  }
  mMouse.StopTimer();
  const int button = mClickButton;
  mClickButton = kNoClick;
  if (ClicksCame()) {
    // The program's clicks began since the tap's finger came down, as while its click waited for the cursor's rest: they took
    // what it would act on.
    return;
  }
  if (button == kDeleteOrder) {
    mMouse.DeleteOrder(mClickX, mClickY);
    return;
  }
  if (button == kDraftTap) {
    // A plan is no action of the rail's: Plus stays for the tick.
    mMouse.DraftTap(mClickX, mClickY, mClickPlan);
    return;
  }
  if (button == kGriffTap) {
    const int step = mLadder.Next(mClickX, mClickY, mClickLift, mClickRail, kLadderMm * mMillimetre, 2 * mSlop);
    if (step == kStepTap && mClickOnHandle) {
      // On the handle, which left the finger to the ladder, and the ladder did not climb: the handle's tap, never FAF's
      // click at the handle's place. As a stroke's, it starts no ladder and uses none of the rail.
      mLadder.Clear();
      mMouse.GriffTap();
      return;
    }
    mMouse.SmartTap(mClickX, mClickY, mClickRail, step, mLadder.BaseX(), mLadder.BaseY(), mLadder.Tap());
    // A tap is the next action whatever it turns out to be: the rail's buttons for it go off (Rail). Box's tap on nothing
    // leaves Box lit, so with Box the program says what was used.
    if (mClickRail != 0 && (mClickRail & kRailBox) == 0) {
      mMouse.RailUsed(mClickRail);
    }
    return;
  }
  if (button == kButtonLeft) {
    mMouse.Tap(mClickX, mClickY);
    return;
  }
  mMouse.Button(button, true, mClickX, mClickY);
  mMouse.Button(button, false, mClickX, mClickY);
}

void touch::TouchInput::StartPanning()
{
  mGhostUsed = false;
  mMouse.Move(mStartX, mStartY);
  mMouse.Button(kButtonMiddle, true, mStartX, mStartY);
  mMouse.Move(MiddleX(), MiddleY());
  mStartDistance = Distance();
  mState = State::kPanning;
}

void touch::TouchInput::StartZooming()
{
  mGhostUsed = false;
  mWheelDistance = Distance();
  mWheelRemainder = 0;
  mZoomX = MiddleX();
  mZoomY = MiddleY();
  mZoomDistance = mWheelDistance;
  mMouse.Move(mZoomX, mZoomY);
  mState = State::kZooming;
}

void touch::TouchInput::Zoom()
{
  const float now = Distance();
  if (now <= 0 || mWheelDistance <= 0) {
    return;
  }
  // One finger pinching moves the point between them by half of what the distance changes, and no more. Past that the
  // fingers go somewhere together: it has become a pan.
  if (Hypot(MiddleX() - mZoomX, MiddleY() - mZoomY) > 4 * mSlop + std::abs(now - mZoomDistance) / 2) {
    mStartX = MiddleX();
    mStartY = MiddleY();
    StartPanning();
    return;
  }
  const double wheel = std::log(static_cast<double>(now / mWheelDistance)) / kLog2 * kWheelPerDoubling + mWheelRemainder;
  const int delta = ToInt(wheel);
  if (delta != 0) {
    mMouse.Wheel(delta, MiddleX(), MiddleY());
    mWheelRemainder = wheel - delta;
    mWheelDistance = now;
  }
}

float touch::TouchInput::MiddleX() const
{
  return (mFirst.x + mSecond.x) / 2;
}

float touch::TouchInput::MiddleY() const
{
  return (mFirst.y + mSecond.y) / 2;
}

float touch::TouchInput::Distance() const
{
  return static_cast<float>(Hypot(mFirst.x - mSecond.x, mFirst.y - mSecond.y));
}
