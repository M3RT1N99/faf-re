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
 * other. With {@link #setDeleting} one finger does nothing but this: a tap deletes the queued order
 * under it.
 *
 * <p>While a building's ghost waits to be placed ({@link Mouse#placing}), a finger would cover the
 * place it chooses, so one finger moves the ghost and only a tap on it places it:
 *
 * <ul>
 *   <li>a tap on the game's UI is the UI's, such as another building's button;
 *   <li>a tap away from the ghost puts it there;
 *   <li>a tap near the ghost places the building where the ghost is, and with Shift the ghost stays
 *       for the next, which a tap puts, even near there, or a finger takes along;
 *   <li>a finger that moves takes the ghost along, from wherever on the screen it landed;
 *   <li>a finger that stays away from the ghost takes it along too, by the smallest steps, for setting
 *       it against another building, and whenever it lifts it has placed nothing;
 *   <li>a finger that stays on the ghost and then moves draws a line of buildings from there, with the
 *       game's own preview, which go up when it lifts; a second finger gives the line up, and the ghost
 *       goes back to where it began; if it lifts without moving, nothing has happened;
 *   <li>a quick tap of two fingers is the right button whatever else is set, which cancels.
 * </ul>
 *
 * Fingers beyond the two a gesture uses are ignored. Positions are the screen's pixels, which {@link
 * Mouse} turns into the game's.
 *
 * <p>A mouse's pointer goes on being somewhere, and the game shows what it is over. A finger's goes, shortly after the
 * last finger has lifted ({@link Mouse#leaveSoon}), so that a rollover does not stay in the way of what it opened.
 */
final class TouchInput implements Fingers {
    /** What the fingers do, on the thread that calls this class. */
    interface Mouse {
        void move(float x, float y);

        /**
         * `button`: Game.BUTTON_LEFT and the rest. A press where a building's ghost waits only puts the
         * ghost there, whatever this class knows of the ghost by then, which may be behind the game.
         */
        void button(int button, boolean down, float x, float y);

        /** A tap, a click of the left button, which the game's UI may give to a small control beside it. */
        void tap(float x, float y);

        /**
         * A tap near a building's ghost: it places the building where the ghost is, unless the game's UI
         * is there or under the finger, and is a tap at the finger then.
         */
        void tapNearGhost(float fingerX, float fingerY, float ghostX, float ghostY);

        /**
         * A line of buildings begins here, where the ghost is: the left button as a mouse presses it, which
         * {@link #button} lets go. The game draws the line to wherever the cursor goes until then.
         */
        void startLine(float x, float y);

        /** Gives up the line begun with {@link #startLine}, with nothing built. */
        void cancelLine();

        /** `delta` in WHEEL_DELTA's units, positive towards the screen. */
        void wheel(int delta, float x, float y);

        /** A finger stayed: it does something else now, which the user should feel. */
        void held();

        /** Whether a building's ghost waits to be placed. */
        boolean placing();

        /** Deletes the queued order nearest to the cursor, which has come here, unless the game's UI is here. */
        void deleteOrder(float x, float y);

        /** In {@link #LEAVE_MILLISECONDS} the pointer leaves what it is over. */
        void leaveSoon();

        /** A finger came: the pointer stays, whatever {@link #leaveSoon} said, also if the leave is on its way to the game. */
        void stay();

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
    /** How long the pointer stays after the last finger: a click is yet to come (SETTLE_MILLISECONDS), and frames to see it. */
    static final long LEAVE_MILLISECONDS = 300;
    /** Two clicks this close, and near each other, are the game's double click. */
    static final long DOUBLE_TAP_MILLISECONDS = 400;
    /** How far above the finger, in slops, a ghost starts that nothing has put anywhere yet. */
    private static final float GHOST_ABOVE_SLOPS = 12;
    /** How far from a ghost, in slops, a tap is still a tap on it. */
    private static final float GHOST_REACH_SLOPS = 6;
    /** The wheel for two fingers twice as far apart: cam_ZoomAmount's default is 0.4 notches' worth. */
    private static final double WHEEL_PER_DOUBLING = Game.WHEEL_DELTA / 0.4;
    private static final double LOG_2 = Math.log(2.0);
    /** What a waiting click does instead of a button. */
    private static final int NO_CLICK = -1;
    private static final int DELETE_ORDER = -2;

    private enum State {
        /** No finger. */
        NONE,
        /** One finger, and nothing shown yet of what it does. */
        PENDING,
        /** One finger holds `mPressed`. */
        PRESSED,
        /** One finger, a building's ghost waiting, and nothing shown yet of what the finger does. */
        PLACE_PENDING,
        /** One finger takes the ghost along. */
        PLACE_DRAGGING,
        /** One finger stayed on the ghost: when it moves, it draws a line of buildings from there. */
        PLACE_LINE_READY,
        /** One finger draws a line of buildings. */
        PLACE_LINE,
        /** Two fingers, and nothing shown yet of what they do. */
        TWO_PENDING,
        PANNING,
        ZOOMING,
        /** Fingers left of a gesture that is over. */
        SPENT,
    }

    /**
     * Where a click goes. The game's double click wants the same few pixels, which two taps of a finger do not hit, so a
     * click soon after the last and near it goes where that went.
     */
    static final class ClickPlace {
        private long mTime = Long.MIN_VALUE / 2;
        private float mX;
        private float mY;

        void clickedAt(float x, float y, long time, float slop) {
            boolean again = time - mTime <= DOUBLE_TAP_MILLISECONDS && Math.hypot(x - mX, y - mY) <= 2 * slop;
            mTime = time;
            if (!again) {
                mX = x;
                mY = y;
            }
        }

        float x() {
            return mX;
        }

        float y() {
            return mY;
        }
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
    private boolean mDeleting;
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
    /** Where a building's ghost is, once a finger has put it somewhere. */
    private boolean mGhostPlaced;
    private float mGhostX;
    private float mGhostY;
    /** A building went up where the ghost is, and neither has moved since: a tap there would only place it again. */
    private boolean mGhostUsed;
    /** Where the finger that takes the ghost along last was. */
    private float mGhostFingerX;
    private float mGhostFingerY;
    /** Where the ghost was when the line began, for a line given up. */
    private float mLineStartX;
    private float mLineStartY;
    /** Two fingers came down over a ghost, and took the cursor from it. */
    private boolean mGhostToRestore;
    /** A click that waits for the cursor to settle: NO_CLICK, or what it does. */
    private int mClickButton = NO_CLICK;
    private float mClickX;
    private float mClickY;
    private final ClickPlace mLastClick = new ClickPlace();
    private float mScreenWidth = Float.MAX_VALUE;
    private float mScreenHeight = Float.MAX_VALUE;

    /** `slop`: how far a finger may wander in the screen's pixels and still count as staying. */
    TouchInput(Mouse mouse, float slop) {
        mMouse = mouse;
        mSlop = slop;
    }

    void setSwapped(boolean swapped) {
        mSwapped = swapped;
    }

    void setDeleting(boolean deleting) {
        mDeleting = deleting;
    }

    /** The screen's size in its pixels, which a ghost does not leave. */
    void setScreen(float width, float height) {
        mScreenWidth = width;
        mScreenHeight = height;
    }

    private int tapButton() {
        return mSwapped ? Game.BUTTON_RIGHT : Game.BUTTON_LEFT;
    }

    private int holdButton() {
        return mSwapped ? Game.BUTTON_LEFT : Game.BUTTON_RIGHT;
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
            bringCursorBackToGhost();
            mSecond.id = -1;
            startOneFinger(id, x, y, time);
        } else if (mState == State.PLACE_LINE) {
            giveUpLine();
            mState = State.SPENT;
        } else if (mState == State.PENDING || mState == State.PLACE_PENDING || mState == State.PLACE_LINE_READY) {
            mGhostToRestore = mState != State.PENDING && mGhostPlaced;
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

    private void startOneFinger(int id, float x, float y, long time) {
        if (!mDeleting && mMouse.placing()) {
            startPlacing(id, x, y, time);
        } else {
            mGhostPlaced = false;
            startPending(id, x, y, time);
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

    private void startPlacing(int id, float x, float y, long time) {
        mFirst.id = id;
        mFirst.place(x, y);
        mStartX = x;
        mStartY = y;
        mStartTime = time;
        mMouse.startTimer(HOLD_MILLISECONDS);
        mState = State.PLACE_PENDING;
    }

    @Override
    public void move(int id, float x, float y) {
        if (id == mFirst.id) {
            mFirst.place(x, y);
        } else if (id == mSecond.id) {
            mSecond.place(x, y);
        }
    }

    @Override
    public void moved() {
        switch (mState) {
            case PENDING:
                if (!mDeleting && Math.hypot(mFirst.x - mStartX, mFirst.y - mStartY) > mSlop) {
                    mMouse.stopTimer();
                    press(tapButton(), mStartX, mStartY);
                    mMouse.move(mFirst.x, mFirst.y);
                }
                break;
            case PRESSED:
                mMouse.move(mFirst.x, mFirst.y);
                break;
            case PLACE_PENDING:
                if (Math.hypot(mFirst.x - mStartX, mFirst.y - mStartY) > mSlop) {
                    mMouse.stopTimer();
                    startTakingGhost();
                }
                break;
            case PLACE_LINE_READY:
                if (Math.hypot(mFirst.x - mStartX, mFirst.y - mStartY) > mSlop) {
                    startLine();
                }
                break;
            case PLACE_DRAGGING:
            case PLACE_LINE:
                moveGhost();
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

    @Override
    public void up(int id, long time, boolean last) {
        if (id == mFirst.id || id == mSecond.id) {
            switch (mState) {
                case PENDING:
                    mMouse.stopTimer();
                    click(mDeleting ? DELETE_ORDER : tapButton(), mStartX, mStartY, time - mStartTime, time);
                    mState = State.SPENT;
                    break;
                case PRESSED:
                    mMouse.button(mPressed, false, mFirst.x, mFirst.y);
                    mState = State.SPENT;
                    break;
                case PLACE_PENDING:
                    mMouse.stopTimer();
                    tapWhilePlacing();
                    mState = State.SPENT;
                    break;
                case PLACE_DRAGGING:
                case PLACE_LINE_READY:
                    mState = State.SPENT;
                    break;
                case PLACE_LINE:
                    mMouse.button(Game.BUTTON_LEFT, false, mGhostX, mGhostY);
                    mGhostUsed = true;
                    mState = State.SPENT;
                    break;
                case TWO_PENDING:
                    if (time - mStartTime <= TWO_FINGER_TAP_MILLISECONDS) {
                        int button = mMouse.placing() ? Game.BUTTON_RIGHT : holdButton();
                        click(button, mSecondX, mSecondY, time - mStartTime, time);
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
            bringCursorBackToGhost();
            mState = State.NONE;
            mFirst.id = -1;
            mSecond.id = -1;
            mMouse.leaveSoon();
        }
    }

    /**
     * Android says that a finger which lifted was a palm, with another still down: it is as if it had
     * not come down, when nothing has shown yet of what the two do.
     */
    @Override
    public void forget(int id, long time) {
        if (mState == State.TWO_PENDING && (id == mFirst.id || id == mSecond.id)) {
            Finger left = id == mFirst.id ? mSecond : mFirst;
            int leftId = left.id;
            float x = left.x;
            float y = left.y;
            mSecond.id = -1;
            startOneFinger(leftId, x, y, time);
        } else if (id == mFirst.id || id == mSecond.id) {
            cancel();
        }
    }

    /**
     * The system took the fingers. What is held goes up, as a mouse's would, so a right button that a
     * finger held gives its order; no gesture comes of what is left of them. A line builds nothing that
     * the finger did not finish.
     */
    @Override
    public void cancel() {
        endGesture();
        mMouse.leaveSoon();
    }

    private void endGesture() {
        mMouse.stopTimer();
        if (mState == State.PRESSED) {
            mMouse.button(mPressed, false, mFirst.x, mFirst.y);
        } else if (mState == State.PANNING) {
            mMouse.button(Game.BUTTON_MIDDLE, false, middleX(), middleY());
        } else if (mState == State.PLACE_LINE) {
            giveUpLine();
        }
        finishClick();
        bringCursorBackToGhost();
        mState = State.SPENT;
    }

    /** The time from {@link Mouse#startTimer} has passed. */
    void timer() {
        if (mState == State.PENDING && !mDeleting) {
            press(holdButton(), mStartX, mStartY);
            mMouse.held();
        } else if (mState == State.PLACE_PENDING) {
            if (startedOnGhost()) {
                mState = State.PLACE_LINE_READY;
            } else {
                startTakingGhost();
            }
            mMouse.held();
        }
        finishClick();
    }

    private void bringCursorBackToGhost() {
        if (mGhostToRestore) {
            mGhostToRestore = false;
            mMouse.move(mGhostX, mGhostY);
        }
    }

    private void startTakingGhost() {
        if (!mGhostPlaced) {
            // Its first place: above the finger, where the finger does not cover it.
            mGhostPlaced = true;
            mGhostX = mStartX;
            mGhostY = mStartY - GHOST_ABOVE_SLOPS * mSlop;
        }
        mGhostFingerX = mStartX;
        mGhostFingerY = mStartY;
        mGhostUsed = false;
        mState = State.PLACE_DRAGGING;
        moveGhost();
    }

    /** On the screen, or the ghost would wait there for the finger to come all the way back. */
    private void moveGhost() {
        mGhostX = Math.max(0, Math.min(mScreenWidth, mGhostX + mFirst.x - mGhostFingerX));
        mGhostY = Math.max(0, Math.min(mScreenHeight, mGhostY + mFirst.y - mGhostFingerY));
        mGhostFingerX = mFirst.x;
        mGhostFingerY = mFirst.y;
        mMouse.move(mGhostX, mGhostY);
    }

    private boolean startedOnGhost() {
        return mGhostPlaced && !mGhostUsed && Math.hypot(mStartX - mGhostX, mStartY - mGhostY) <= GHOST_REACH_SLOPS * mSlop;
    }

    private void startLine() {
        mLineStartX = mGhostX;
        mLineStartY = mGhostY;
        mMouse.startLine(mGhostX, mGhostY);
        mGhostFingerX = mStartX;
        mGhostFingerY = mStartY;
        mState = State.PLACE_LINE;
        moveGhost();
    }

    private void giveUpLine() {
        mMouse.cancelLine();
        mGhostX = mLineStartX;
        mGhostY = mLineStartY;
        mMouse.move(mGhostX, mGhostY);
    }

    /** The game takes a finger's press to put the ghost there, unless it lands on its UI. */
    private void tapWhilePlacing() {
        boolean onGhost = startedOnGhost();
        mGhostUsed = onGhost;
        if (onGhost) {
            mMouse.tapNearGhost(mStartX, mStartY, mGhostX, mGhostY);
            return;
        }
        mGhostPlaced = true;
        mGhostX = mStartX;
        mGhostY = mStartY;
        mMouse.button(Game.BUTTON_LEFT, true, mGhostX, mGhostY);
        mMouse.button(Game.BUTTON_LEFT, false, mGhostX, mGhostY);
    }

    private void press(int button, float x, float y) {
        mPressed = button;
        mMouse.button(button, true, x, y);
        mState = State.PRESSED;
    }

    /** A click `rested` milliseconds after the cursor came there, now or when it has settled. */
    private void click(int button, float x, float y, long rested, long time) {
        mLastClick.clickedAt(x, y, time, mSlop);
        mClickButton = button;
        mClickX = mLastClick.x();
        mClickY = mLastClick.y();
        if (rested >= SETTLE_MILLISECONDS) {
            finishClick();
        } else {
            mMouse.startTimer(SETTLE_MILLISECONDS - rested);
        }
    }

    private void finishClick() {
        if (mClickButton == NO_CLICK) {
            return;
        }
        mMouse.stopTimer();
        int button = mClickButton;
        mClickButton = NO_CLICK;
        if (button == DELETE_ORDER) {
            mMouse.deleteOrder(mClickX, mClickY);
            return;
        }
        if (button == Game.BUTTON_LEFT) {
            mMouse.tap(mClickX, mClickY);
            return;
        }
        mMouse.button(button, true, mClickX, mClickY);
        mMouse.button(button, false, mClickX, mClickY);
    }

    private void startPanning() {
        mGhostUsed = false;
        mMouse.move(mStartX, mStartY);
        mMouse.button(Game.BUTTON_MIDDLE, true, mStartX, mStartY);
        mMouse.move(middleX(), middleY());
        mStartDistance = distance();
        mState = State.PANNING;
    }

    private void startZooming() {
        mGhostUsed = false;
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
