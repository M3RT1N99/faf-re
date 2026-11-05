package io.github.m3rt1n99.fafre;

/**
 * Fingers in the menus (android/TOUCH_UI.md), where a screen is read and scrolled:
 *
 * <ul>
 *   <li>a tap is a click, also after a rest, as a careful finger is slow;
 *   <li>a finger that rests {@link #RIGHT_CLICK_MILLISECONDS}, which it feels a second time, and lifts without moving is a click of
 *       the right button;
 *   <li>a finger that moves mainly up or down scrolls, the list under it first and then the page, and presses nothing;
 *   <li>a finger that moves mainly sideways is a mouse's drag from where it came down: a slider;
 *   <li>a finger that rested {@link #REST_MILLISECONDS} and then moves is a mouse's drag in any direction: a scroll bar's
 *       thumb, a window;
 *   <li>two fingers do nothing.
 * </ul>
 *
 * Positions are the screen's pixels, which {@link Mouse} turns into the game's. As in a game the pointer goes shortly after
 * the last finger has lifted, and in a scroll at once, as what it was over moves away from under it.
 */
final class PageInput implements Fingers {
    /** What the fingers do, on the thread that calls this class. */
    interface Mouse {
        void move(float x, float y);

        /** The left button goes down, on what a finger means by this place: small controls are near misses. */
        void press(float x, float y);

        void release(float x, float y);

        /** A tap, a click of the left button, which the game's UI may give to a small control beside it. */
        void tap(float x, float y);

        /** A click of the right button, which the game's UI may give to a small control beside it as a tap's. */
        void rightClick(float x, float y);

        /** A finger came down here: if it goes on to scroll, it is the list here that it scrolls. */
        void lookForList(float x, float y);

        /** The finger that came down last went this far on the screen, positive downwards, as a scroll. */
        void scroll(float dy);

        /** A finger rested: what it does now is different, which the user should feel. */
        void held();

        /** The pointer leaves what it is over, now. */
        void leave();

        /** In {@link TouchInput#LEAVE_MILLISECONDS} the pointer leaves what it is over. */
        void leaveSoon();

        /** A finger came: the pointer stays, whatever {@link #leaveSoon} said, also if the leave is on its way to the game. */
        void stay();

        /** Calls {@link #timer} after this long, in place of the time set before. */
        void startTimer(long milliseconds);

        void stopTimer();
    }

    /** How long a finger stays before it moves for a drag. */
    static final long REST_MILLISECONDS = 300;

    /** How long a finger stays before it lifts for a click of the right button. */
    static final long RIGHT_CLICK_MILLISECONDS = 500;

    private enum State {
        /** No finger. */
        NONE,
        /** One finger, and nothing shown yet of what it does. */
        PENDING,
        /** One finger rested: a move drags now. */
        RESTED,
        /** One finger rested longer: a move still drags, and lifting it is a click of the right button. */
        RESTED_LONG,
        SCROLLING,
        /** One finger holds the left button. */
        DRAGGING,
        /** Two fingers, or more, on a gesture that had not begun. */
        TWO,
        /** Fingers left of a gesture that is over. */
        SPENT,
    }

    private final Mouse mMouse;
    private final float mSlop;
    private State mState = State.NONE;
    private int mFirstId = -1;
    private int mSecondId = -1;
    private float mX;
    private float mY;
    private float mStartX;
    private float mStartY;
    private long mStartTime;
    /** Where the scroll has come to. */
    private float mLastY;
    private boolean mClickWaits;
    private final TouchInput.ClickPlace mClick = new TouchInput.ClickPlace();

    /** `slop`: how far a finger may wander in the screen's pixels and still count as staying. */
    PageInput(Mouse mouse, float slop) {
        mMouse = mouse;
        mSlop = slop;
    }

    @Override
    public void down(int id, float x, float y, long time, boolean first) {
        // First: a leave on its way counts as taken back, whatever this finger goes on to send.
        mMouse.stay();
        if (first) {
            if (mState != State.NONE && mState != State.SPENT) {
                endGesture();
            }
            finishClick();
            mSecondId = -1;
            startPending(id, x, y, time);
        } else if (mState == State.PENDING || mState == State.RESTED || mState == State.RESTED_LONG) {
            mMouse.stopTimer();
            mSecondId = id;
            mState = State.TWO;
        }
    }

    private void startPending(int id, float x, float y, long time) {
        mFirstId = id;
        mX = x;
        mY = y;
        mStartX = x;
        mStartY = y;
        mStartTime = time;
        mMouse.move(x, y);
        mMouse.lookForList(x, y);
        mMouse.startTimer(REST_MILLISECONDS);
        mState = State.PENDING;
    }

    @Override
    public void move(int id, float x, float y) {
        if (id == mFirstId) {
            mX = x;
            mY = y;
        }
    }

    @Override
    public void moved() {
        switch (mState) {
            case PENDING:
                if (beyondSlop()) {
                    mMouse.stopTimer();
                    if (Math.abs(mY - mStartY) >= Math.abs(mX - mStartX)) {
                        mMouse.leave();
                        mLastY = mY;
                        mState = State.SCROLLING;
                    } else {
                        startDrag();
                    }
                }
                break;
            case RESTED:
            case RESTED_LONG:
                if (beyondSlop()) {
                    mMouse.stopTimer();
                    startDrag();
                }
                break;
            case SCROLLING:
                if (mY != mLastY) {
                    mMouse.scroll(mY - mLastY);
                    mLastY = mY;
                }
                break;
            case DRAGGING:
                mMouse.move(mX, mY);
                break;
            default:
                break;
        }
    }

    private boolean beyondSlop() {
        return Math.hypot(mX - mStartX, mY - mStartY) > mSlop;
    }

    private void startDrag() {
        mMouse.press(mStartX, mStartY);
        mMouse.move(mX, mY);
        mState = State.DRAGGING;
    }

    @Override
    public void up(int id, long time, boolean last) {
        if (id == mFirstId) {
            switch (mState) {
                case PENDING:
                case RESTED:
                    mMouse.stopTimer();
                    click(time);
                    mState = State.SPENT;
                    break;
                case RESTED_LONG:
                    mMouse.rightClick(mStartX, mStartY);
                    mState = State.SPENT;
                    break;
                case DRAGGING:
                    mMouse.release(mX, mY);
                    mState = State.SPENT;
                    break;
                case SCROLLING:
                case TWO:
                    mState = State.SPENT;
                    break;
                default:
                    break;
            }
        }
        if (last) {
            mState = State.NONE;
            mFirstId = -1;
            mSecondId = -1;
            mMouse.leaveSoon();
        }
    }

    @Override
    public void forget(int id, long time) {
        if (mState == State.TWO && id == mSecondId) {
            mSecondId = -1;
            startPending(mFirstId, mX, mY, time);
        } else if (id == mFirstId) {
            cancel();
        }
    }

    /** What is held goes up, as a mouse's would, and no gesture comes of what is left of the fingers. */
    @Override
    public void cancel() {
        endGesture();
        mMouse.leaveSoon();
    }

    private void endGesture() {
        mMouse.stopTimer();
        if (mState == State.DRAGGING) {
            mMouse.release(mX, mY);
        }
        finishClick();
        mState = State.SPENT;
    }

    /** The time from {@link Mouse#startTimer} has passed. */
    void timer() {
        if (mState == State.PENDING) {
            mState = State.RESTED;
            mMouse.held();
            mMouse.startTimer(RIGHT_CLICK_MILLISECONDS - REST_MILLISECONDS);
        } else if (mState == State.RESTED) {
            mState = State.RESTED_LONG;
            mMouse.held();
        }
        finishClick();
    }

    /** A click now, or when the cursor has settled: the game looks for what is under it once a frame. */
    private void click(long time) {
        mClick.clickedAt(mStartX, mStartY, time, mSlop);
        mClickWaits = true;
        long rested = time - mStartTime;
        if (rested >= TouchInput.SETTLE_MILLISECONDS) {
            finishClick();
        } else {
            mMouse.startTimer(TouchInput.SETTLE_MILLISECONDS - rested);
        }
    }

    private void finishClick() {
        if (mClickWaits) {
            mMouse.stopTimer();
            mClickWaits = false;
            mMouse.tap(mClick.x(), mClick.y());
        }
    }
}
