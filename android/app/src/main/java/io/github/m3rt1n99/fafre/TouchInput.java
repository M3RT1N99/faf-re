package io.github.m3rt1n99.fafre;

/**
 * Fingers as the mouse the game was made for. What a gesture is shows only after it began, so until
 * then only the cursor's place goes to the game:
 *
 * <ul>
 *   <li>a tap is a click of the left button;
 *   <li>a finger that moves drags with the left button: a selection box, a slider;
 *   <li>a finger that stays presses the right button until it lifts, with {@link Mouse#held}: an
 *       order, which the game gives when the button goes up, and moving before that turns a formation;
 *   <li>a quick tap of two fingers is a click of the right button where the second came down;
 *   <li>two fingers that move together drag with the middle button, which moves the game's camera;
 *   <li>two fingers that move apart or closer turn the wheel between them, which zooms it.
 * </ul>
 *
 * With {@link #setSwapped} the left and right buttons change places, for giving one order after the
 * other. Fingers beyond the two a gesture uses are ignored. Positions are the screen's pixels, which
 * {@link Mouse} turns into the game's.
 */
final class TouchInput {
    /** What the fingers do, on the thread that calls this class. */
    interface Mouse {
        void move(float x, float y);

        /** `button`: Game.BUTTON_LEFT and the rest. */
        void button(int button, boolean down, float x, float y);

        /** `delta` in WHEEL_DELTA's units, positive towards the screen. */
        void wheel(int delta, float x, float y);

        /** A finger stayed: it now holds a button, which the user should feel. */
        void held();

        /** Calls {@link #timer} after this long, in place of the time set before. */
        void startTimer(long milliseconds);

        void stopTimer();
    }

    /** How long a finger stays before it holds the button. */
    static final long HOLD_MILLISECONDS = 350;
    /** How long two fingers may stay for a tap. */
    static final long TWO_FINGER_TAP_MILLISECONDS = 350;
    /**
     * How long the cursor rests before a click. The game looks for what is under the cursor once a
     * frame, and a quick tap can be over before a slow frame has seen where it landed.
     */
    static final long SETTLE_MILLISECONDS = 120;
    /** Two clicks this close, and near each other, are the game's double click. */
    static final long DOUBLE_TAP_MILLISECONDS = 400;
    /** The wheel for two fingers twice as far apart: cam_ZoomAmount's default is 0.4 notches' worth. */
    private static final double WHEEL_PER_DOUBLING = Game.WHEEL_DELTA / 0.4;
    private static final double LOG_2 = Math.log(2.0);

    private enum State {
        /** No finger. */
        NONE,
        /** One finger, and nothing shown yet of what it does. */
        PENDING,
        /** One finger holds `mPressed`. */
        PRESSED,
        /** Two fingers, and nothing shown yet of what they do. */
        TWO_PENDING,
        PANNING,
        ZOOMING,
        /** Fingers left of a gesture that is over. */
        SPENT,
    }

    private static final class Finger {
        int id = -1;
        float x;
        float y;

        void place(float newX, float newY) {
            x = newX;
            y = newY;
        }
    }

    private final Mouse mMouse;
    private final float mSlop;
    private State mState = State.NONE;
    private boolean mSwapped;
    private final Finger mFirst = new Finger();
    private final Finger mSecond = new Finger();
    /** Where the gesture began: the first finger's place, or the point between two. */
    private float mStartX;
    private float mStartY;
    private float mStartDistance;
    private long mStartTime;
    /** Where the second finger came down. */
    private float mSecondX;
    private float mSecondY;
    private int mPressed;
    private float mWheelDistance;
    private double mWheelRemainder;
    /** Where the fingers were when the zoom began: moving on from there together makes it a pan. */
    private float mZoomX;
    private float mZoomY;
    private float mZoomDistance;
    /** A click that waits for the cursor to settle: -1, or its button. */
    private int mClickButton = -1;
    private float mClickX;
    private float mClickY;
    private long mLastClickTime = Long.MIN_VALUE / 2;
    private float mLastClickX;
    private float mLastClickY;

    /** `slop`: how far a finger may wander in the screen's pixels and still count as staying. */
    TouchInput(Mouse mouse, float slop) {
        mMouse = mouse;
        mSlop = slop;
    }

    void setSwapped(boolean swapped) {
        mSwapped = swapped;
    }

    private int tapButton() {
        return mSwapped ? Game.BUTTON_RIGHT : Game.BUTTON_LEFT;
    }

    private int holdButton() {
        return mSwapped ? Game.BUTTON_LEFT : Game.BUTTON_RIGHT;
    }

    /** A finger came down; `first`: no other is left on the glass, whatever this class heard of them. */
    void down(int id, float x, float y, long time, boolean first) {
        if (first) {
            if (mState != State.NONE && mState != State.SPENT) {
                cancel();
            }
            finishClick();
            mSecond.id = -1;
            startPending(id, x, y, time);
        } else if (mState == State.PENDING) {
            mMouse.stopTimer();
            mSecond.id = id;
            mSecond.place(x, y);
            mSecondX = x;
            mSecondY = y;
            mStartX = middleX();
            mStartY = middleY();
            mStartDistance = distance();
            mStartTime = time;
            mMouse.move(x, y);
            mState = State.TWO_PENDING;
        }
    }

    private void startPending(int id, float x, float y, long time) {
        mFirst.id = id;
        mFirst.place(x, y);
        mStartX = x;
        mStartY = y;
        mStartTime = time;
        mMouse.move(x, y);
        mMouse.startTimer(HOLD_MILLISECONDS);
        mState = State.PENDING;
    }

    /** Where a finger is now. {@link #moved} follows when all of an event's fingers are in. */
    void move(int id, float x, float y) {
        if (id == mFirst.id) {
            mFirst.place(x, y);
        } else if (id == mSecond.id) {
            mSecond.place(x, y);
        }
    }

    void moved() {
        switch (mState) {
            case PENDING:
                if (Math.hypot(mFirst.x - mStartX, mFirst.y - mStartY) > mSlop) {
                    mMouse.stopTimer();
                    press(tapButton(), mStartX, mStartY);
                    mMouse.move(mFirst.x, mFirst.y);
                }
                break;
            case PRESSED:
                mMouse.move(mFirst.x, mFirst.y);
                break;
            case TWO_PENDING:
                if (Math.abs(distance() - mStartDistance) > 2 * mSlop) {
                    startZooming();
                } else if (Math.hypot(middleX() - mStartX, middleY() - mStartY) > mSlop) {
                    startPanning();
                }
                break;
            case PANNING:
                mMouse.move(middleX(), middleY());
                if (Math.abs(distance() - mStartDistance) > 6 * mSlop) {
                    mMouse.button(Game.BUTTON_MIDDLE, false, middleX(), middleY());
                    startZooming();
                }
                break;
            case ZOOMING:
                zoom();
                break;
            default:
                break;
        }
    }

    /** A finger lifted; `last`: no other is left on the glass. */
    void up(int id, long time, boolean last) {
        if (id == mFirst.id || id == mSecond.id) {
            switch (mState) {
                case PENDING:
                    mMouse.stopTimer();
                    click(tapButton(), mStartX, mStartY, time - mStartTime, time);
                    mState = State.SPENT;
                    break;
                case PRESSED:
                    mMouse.button(mPressed, false, mFirst.x, mFirst.y);
                    mState = State.SPENT;
                    break;
                case TWO_PENDING:
                    if (time - mStartTime <= TWO_FINGER_TAP_MILLISECONDS) {
                        click(holdButton(), mSecondX, mSecondY, time - mStartTime, time);
                    }
                    mState = State.SPENT;
                    break;
                case PANNING:
                    mMouse.button(Game.BUTTON_MIDDLE, false, middleX(), middleY());
                    mState = State.SPENT;
                    break;
                case ZOOMING:
                    mState = State.SPENT;
                    break;
                default:
                    break;
            }
        }
        if (last) {
            mState = State.NONE;
            mFirst.id = -1;
            mSecond.id = -1;
        }
    }

    /**
     * Android says that a finger which lifted was a palm, with another still down: it is as if it had
     * not come down, when nothing has shown yet of what the two do.
     */
    void forget(int id, long time) {
        if (mState == State.TWO_PENDING && (id == mFirst.id || id == mSecond.id)) {
            Finger left = id == mFirst.id ? mSecond : mFirst;
            int leftId = left.id;
            float x = left.x;
            float y = left.y;
            mSecond.id = -1;
            startPending(leftId, x, y, time);
        } else if (id == mFirst.id || id == mSecond.id) {
            cancel();
        }
    }

    /**
     * The system took the fingers. What is held goes up, as a mouse's would, so a right button that a
     * finger held gives its order; no gesture comes of what is left of them.
     */
    void cancel() {
        mMouse.stopTimer();
        if (mState == State.PRESSED) {
            mMouse.button(mPressed, false, mFirst.x, mFirst.y);
        } else if (mState == State.PANNING) {
            mMouse.button(Game.BUTTON_MIDDLE, false, middleX(), middleY());
        }
        finishClick();
        mState = State.SPENT;
    }

    /** The time from {@link Mouse#startTimer} has passed. */
    void timer() {
        if (mState == State.PENDING) {
            press(holdButton(), mStartX, mStartY);
            mMouse.held();
        }
        finishClick();
    }

    private void press(int button, float x, float y) {
        mPressed = button;
        mMouse.button(button, true, x, y);
        mState = State.PRESSED;
    }

    /** A click `rested` milliseconds after the cursor came there, now or when it has settled. */
    private void click(int button, float x, float y, long rested, long time) {
        // The game's double click wants the same few pixels, which two taps of a finger do not hit.
        boolean again = time - mLastClickTime <= DOUBLE_TAP_MILLISECONDS && Math.hypot(x - mLastClickX, y - mLastClickY) <= 2 * mSlop;
        mLastClickTime = time;
        if (!again) {
            mLastClickX = x;
            mLastClickY = y;
        }
        mClickButton = button;
        mClickX = mLastClickX;
        mClickY = mLastClickY;
        if (rested >= SETTLE_MILLISECONDS) {
            finishClick();
        } else {
            mMouse.startTimer(SETTLE_MILLISECONDS - rested);
        }
    }

    private void finishClick() {
        if (mClickButton < 0) {
            return;
        }
        mMouse.stopTimer();
        int button = mClickButton;
        mClickButton = -1;
        mMouse.button(button, true, mClickX, mClickY);
        mMouse.button(button, false, mClickX, mClickY);
    }

    private void startPanning() {
        mMouse.move(mStartX, mStartY);
        mMouse.button(Game.BUTTON_MIDDLE, true, mStartX, mStartY);
        mMouse.move(middleX(), middleY());
        mStartDistance = distance();
        mState = State.PANNING;
    }

    private void startZooming() {
        mWheelDistance = distance();
        mWheelRemainder = 0;
        mZoomX = middleX();
        mZoomY = middleY();
        mZoomDistance = mWheelDistance;
        mMouse.move(mZoomX, mZoomY);
        mState = State.ZOOMING;
    }

    private void zoom() {
        float now = distance();
        if (now <= 0 || mWheelDistance <= 0) {
            return;
        }
        // One finger pinching moves the point between them by half of what the distance changes, and
        // no more. Past that the fingers go somewhere together: it has become a pan.
        if (Math.hypot(middleX() - mZoomX, middleY() - mZoomY) > 4 * mSlop + Math.abs(now - mZoomDistance) / 2) {
            mStartX = middleX();
            mStartY = middleY();
            startPanning();
            return;
        }
        double wheel = Math.log(now / mWheelDistance) / LOG_2 * WHEEL_PER_DOUBLING + mWheelRemainder;
        int delta = (int) wheel;
        if (delta != 0) {
            mMouse.wheel(delta, middleX(), middleY());
            mWheelRemainder = wheel - delta;
            mWheelDistance = now;
        }
    }

    private float middleX() {
        return (mFirst.x + mSecond.x) / 2;
    }

    private float middleY() {
        return (mFirst.y + mSecond.y) / 2;
    }

    private float distance() {
        return (float) Math.hypot(mFirst.x - mSecond.x, mFirst.y - mSecond.y);
    }
}
