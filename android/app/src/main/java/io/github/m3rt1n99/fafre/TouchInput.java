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
 *
 * <p>With {@link #setGriff} the fingers follow the touch scheme Griff (buildstage/touch-concept-v2.md, the mockup
 * buildstage/touch-preview) instead, but for a building's ghost, which they place as above. A finger stays within
 * {@link #GRIFF_SLOP_MM}:
 *
 * <ul>
 *   <li>a finger that moves pans the camera from where it passed the slop, and glides on when it lifted moving
 *       ({@link Mouse#griffDragEnd}); one that rests {@link #REST_MILLISECONDS} (a ring shows it, {@link Mouse#ring}) and
 *       then moves draws a selection box, with the rail's Box without the rest; on a panel or the minimap it drags as a
 *       mouse ({@link Mouse#griffDrag});
 *   <li>a finger that rests and lifts is FAF's right click on a panel, and nothing on the map, but with Box, which has no
 *       rest, Box's tap there ({@link Mouse#restTap});
 *   <li>a tap is FAF's click by what it lands on ({@link Mouse#smartTap}): one's own unit is selected, else the selection
 *       gets FAF's right click there; taps soon after each other and near climb a ladder ({@link TapLadder}): all of a
 *       unit's type on the screen, then on the map, an attack-move in place of a move, all one's own on the screen;
 *   <li>a finger that starts on the handle over the selection ({@link Mouse#onGriff}) draws a route, whose waypoints go to
 *       {@link Mouse#route} when it lifts, counted where the finger was a moment before; one that lifts where it began was
 *       a tap on the handle, a long loop back to the handle is a patrol, a short one gives the route up, as does a second
 *       finger. Where the ladder may climb, the handle leaves the finger to it, and its lift is the handle's tap where it
 *       does not climb after all;
 *   <li>two fingers zoom at their middle and move the camera with it ({@link Mouse#pinch}), and the one left of them
 *       moves it on; a second finger on a pan, once the program has said it is one ({@link #panned}), makes two of it; a
 *       quick tap of two lets the selection go;
 *   <li>the rail's buttons ({@link Mouse#rail}) count for the next action, or while held (Rail says what an action uses); for
 *       a tap or a rest Box as it counted when the finger came down, as FAF's reach for a unit beside the finger was set for
 *       it then ({@link Mouse#griffDown}).
 * </ul>
 *
 * While Griff's draft is open ({@link Mouse#drafting}: a build icon picked a building, buildstage/griff-v3-spec.md T2) a finger
 * makes plans instead, and there is no handle:
 *
 * <ul>
 *   <li>a tap places a plan where it came down, or removes the plan it is on ({@link Mouse#draftTap});
 *   <li>a drag from the newest plan draws a line of them ({@link Mouse#draftLine}), from another plan moves the whole draft
 *       ({@link Mouse#draftMove}), with Box and an extractor picked is a box over deposits ({@link Mouse#draftBox}), else it
 *       pans; a second finger gives the line, the move or the box up and the two pinch;
 *   <li>a finger that rests {@link #DRAFT_REST_MILLISECONDS} on no plan, without Box, shows the plan its lift places under it
 *       and a loupe ({@link Mouse#draftAim});
 *   <li>a quick tap of two drops the draft ({@link Mouse#draftDrop}).
 * </ul>
 *
 * While the program's own clicks run ({@link Mouse#clicking}: the draft's tick, the undo's), a finger that comes down rests and
 * gives nothing, as its gesture would act between them. A gesture they came into is given up: nothing more of it goes to the
 * game, also once they are over ({@link Mouse#clickRuns}), as it would act on what they did.
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

        /**
         * Griff: a first finger came down here: on the map or the minimap the camera's glide stops, and FAF's reach for a unit
         * beside a tap is set for what is selected (`box`: the rail's Box counts, which picks units).
         */
        void griffDown(float x, float y, boolean box);

        /**
         * Griff: a tap that the program decides by what is under it, where the finger was and the cursor has rested, or by the
         * ladder's `step` (STEP_): STEP_TAP is decided by its place, as {@link #tap} on the game's UI; one's own unit is
         * selected (with RAIL_BOX added to the selection or taken out); else the selection gets FAF's right click there
         * (RAIL_QUEUE with Shift, RAIL_ATTACK as an attack-move). The other steps act at the ladder's first tap (`baseX`,
         * `baseY`). The program answers a STEP_TAP with what it was ({@link #tapAnswered}, by `tap`), and with Box counting,
         * with the rail's buttons it used.
         */
        void smartTap(float x, float y, int rail, int step, float baseX, float baseY, int tap);

        /** Griff: the ring of a finger that rests, shown about this place or gone. */
        void ring(boolean shown, float x, float y);

        /** Griff: no unit is selected any more. */
        void deselect();

        /** Griff: whether the handle over the selection is under this place. */
        boolean onGriff(float x, float y);

        /** Griff: a tap on the handle, which takes the camera to the selection. */
        void griffTap();

        /**
         * Griff: a route from the handle, `count` waypoints, each FAF's right click there, the last on what is there; all but the
         * first with Shift, the first too with RAIL_QUEUE; with RAIL_ATTACK attack-moves ({@link Game#route}). `patrol`: a loop,
         * the waypoints FAF's patrol where it offers one.
         */
        void route(float[] xs, float[] ys, int count, int rail, boolean patrol);

        /**
         * Griff: the stroke as far as it goes, for the screen: its points (count 0: none, and the handle is free again), what
         * lifting now gives (Stroke.ROUTE, PATROL or CANCEL) and the waypoints it would order.
         */
        void strokeShown(float[] xs, float[] ys, int count, int mode, float[] waypointXs, float[] waypointYs, int waypoints);

        /** Griff: the cursor hovers here for the stroke, so that the game shows and says what its end is on. */
        void strokeHover(float x, float y);

        /** Griff: the stroke was given up before it gave anything, which the user should see. */
        void strokeGivenUp();

        /** Griff: the rail's buttons that count now, RAIL_ bits. */
        int rail();

        /** Griff: an action used these of the rail's buttons: one that was on for the next action goes off. */
        void railUsed(int rail);

        /**
         * Griff: a drag begins where the finger came down (`rested`: after a rest), which the program makes what the place
         * wants: on the map a pan of the camera, from where the finger is now (`nowX`, `nowY`), or a selection box after a rest
         * or with RAIL_BOX, with RAIL_QUEUE adding to the selection; on a panel or the minimap the left button's drag, as a
         * mouse's. The pointer's moves follow, and {@link #griffDragEnd} ends it. The program says by its number `drag` when it
         * made a pan of it ({@link TouchInput#panned}).
         */
        void griffDrag(float x, float y, float nowX, float nowY, boolean rested, int rail, int drag);

        /** Griff: what {@link #griffDrag} pressed goes up here; a pan's camera glides on at this velocity (pixels a second). */
        void griffDragEnd(float x, float y, float velocityX, float velocityY);

        /**
         * Griff: a rest without a drag: on a panel FAF's right click there (RAIL_QUEUE with Shift), on the map nothing. With
         * RAIL_BOX it was no rest, and on the map it is Box's tap; with Box the program says which of the rail's buttons it used.
         */
        void restTap(float x, float y, int rail);

        /**
         * Griff: the camera follows fingers: `start` takes what lies at this place as what stays under it; then the camera
         * keeps it there, zoomed by how far apart two fingers are now from when they started (`distance` 0: one finger,
         * which only moves it).
         */
        void pinch(boolean start, float x, float y, float distance);

        /** Griff: the fingers have left the camera, which glides on at this velocity (pixels a second). */
        void pinchEnd(float velocityX, float velocityY);

        /** Griff: whether the draft is open, as the program says now: a finger that comes down makes plans. */
        boolean drafting();

        /** Griff: the number of the draft's plan a finger here is on as the screen shows it, the newest first; 0: none. */
        int draftPlan(float x, float y);

        /** Griff: whether the plan numbered `plan` is the draft's newest, from which a drag draws a line. */
        boolean draftNewest(int plan);

        /** Griff: whether the plan numbered `plan` stands on a deposit: it neither starts a line nor moves with the draft. */
        boolean draftOnDeposit(int plan);

        /** Griff: whether the draft's building stands on a deposit: with Box a drag is a box over deposits. */
        boolean draftPicksDeposit();

        /**
         * Griff: a tap in the draft where the finger came down, on the plan numbered `plan` (0: none): the program removes that
         * plan, places one there, or on FAF's UI gives the UI's click, with the tap help; a lit Plus stays for the tick (a held
         * Plus is Shift as ever).
         */
        void draftTap(float x, float y, int plan);

        /** Griff: the line from the newest plan to here: shown, added where the finger lifted, or given up (DRAFT_). */
        void draftLine(int phase, float x, float y);

        /** Griff: the whole draft follows a finger from where it came down to here: shown, kept, or put back (DRAFT_). */
        void draftMove(int phase, float fromX, float fromY, float x, float y);

        /** Griff: a finger rests here in the draft: the plan its lift places and the loupe shown, that plan placed, or gone. */
        void draftAim(int phase, float x, float y);

        /** Griff: Box over deposits from one corner to the other: shown as the finger moves, given where it lifted, or gone. */
        void draftBox(int phase, float x0, float y0, float x1, float y1);

        /** Griff: two fingers' tap in the draft: it goes, with nothing ordered. */
        void draftDrop();

        /**
         * Griff: whether the program's clicks run, the draft's tick's or the undo's, as the program says now (Game.griffClicking):
         * a finger that comes down then rests, as its gesture would act between those clicks.
         */
        boolean clicking();

        /**
         * Griff: how many runs of the program's clicks have begun (Game.griffClickRuns): a gesture that one began in is given up,
         * as the program's clicks take FAF's mode, the selection, Shift and the cursor from its first click on.
         */
        int clickRuns();
    }

    /** Griff: the rail's buttons, as Mouse#rail has them. */
    static final int RAIL_BOX = 1;
    static final int RAIL_QUEUE = 2;
    static final int RAIL_ATTACK = 4;
    /** Griff: a tap's step on the ladder (Mouse#smartTap). */
    static final int STEP_TAP = 0;
    /** The ladder's first click on the game's UI again, which the game makes its double click there. */
    static final int STEP_AGAIN = 1;
    /** All of the selected type on the map (FAF's key action select_all_units_of_same_type). */
    static final int STEP_ALL_OF_TYPE = 2;
    /** An attack-move at the first tap's place, in place of its move. */
    static final int STEP_ATTACK_MOVE = 3;
    /** All of one's own mobile units on the screen. */
    static final int STEP_ALL_MOBILE = 4;
    /**
     * All of the selected unit's type on the screen (FAF's key action select_all_onscreen for the type), as the game's double
     * click on it: the first tap's selection brings FAF's panels up, which can cover where it was.
     */
    static final int STEP_TYPE_IN_VIEW = 5;
    /** Griff: a draft gesture's phase (Mouse#draftLine and the others). */
    static final int DRAFT_SHOW = 0;
    static final int DRAFT_END = 1;
    static final int DRAFT_ABORT = 2;
    /** Griff: what a STEP_TAP was, as the program answers it (Mouse#smartTap); 0 until it has. */
    static final int TAP_UI = 1;
    static final int TAP_OWN = 2;
    static final int TAP_GROUND = 3;
    static final int TAP_EMPTY = 4;
    static final int TAP_OTHER = 5;
    /** Griff: how long a finger rests before its drag draws a box instead of panning. */
    static final long REST_MILLISECONDS = 300;
    /** Griff: how long a finger rests in the draft before the plan it would place shows under it, with the loupe. */
    static final long DRAFT_REST_MILLISECONDS = 400;
    /** Griff: how far a finger may wander and still stay, for a tap, a rest and a stroke's tap on the handle (the mockup's). */
    static final float GRIFF_SLOP_MM = 2.5f;
    /** Griff: the tap ladder's next tap, this soon after the last one's lift and this near it. */
    static final long LADDER_MILLISECONDS = 350;
    static final float LADDER_MM = 6;
    /** Griff: two fingers that move less than this, apart or together, are still a tap. */
    static final float TWO_FINGERS_STILL_MM = 1.5f;
    /** Griff: a lift counts where the finger was this long before it: fingers wobble as they lift. */
    static final long SETTLE_LIFT_MILLISECONDS = 50;
    /** Griff: the camera glides on only after a move this soon before the lift, at this much of the finger's speed. */
    static final long GLIDE_MILLISECONDS = 70;
    static final float GLIDE_SPEED = 0.9f;
    /** Griff: the cursor hovers at a stroke's end at most this often. */
    static final long HOVER_MILLISECONDS = 100;
    /** Griff: a route's waypoints, about one per this many mm, and its corners within this; a stroke's points this far apart. */
    private static final float WAYPOINT_MM = 15;
    private static final float CORNER_MM = 2.5f;
    private static final float STROKE_POINT_MM = 1;
    private static final int STROKE_POINTS = 512;
    private static final int MAX_WAYPOINTS = 32;
    /** Griff: the moves that the settled lift and a stroke's trim to it look back over (the mockup's). */
    private static final int TRAIL = 8;
    private static final int TRIM_POINTS = 12;
    /** Griff: how much of the last velocity each move keeps. */
    private static final float VELOCITY_KEPT = 0.6f;

    /** How long a finger stays before it holds the button. */
    static final long HOLD_MILLISECONDS = 350;
    /** How long two fingers may stay for a tap: with Griff from the first of them. */
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
    private static final int GRIFF_TAP = -3;
    private static final int DRAFT_TAP = -4;

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
        /** Griff: one finger rested; when it moves, it draws a selection box. */
        RESTED,
        /** Griff: one finger draws a route from the handle over the selection. */
        STROKE,
        /** Griff's draft: one finger, and nothing shown yet of what it does. */
        DRAFT_PENDING,
        /** Griff's draft: one finger draws a line of plans from the newest. */
        DRAFT_LINE,
        /** Griff's draft: one finger moves the whole draft. */
        DRAFT_MOVE,
        /** Griff's draft: one finger rested on no plan, and the plan its lift places shows under it. */
        DRAFT_AIM,
        /** Griff's draft: one finger draws a box over deposits. */
        DRAFT_BOX,
        /** Two fingers, and nothing shown yet of what they do. */
        TWO_PENDING,
        PANNING,
        ZOOMING,
        /** Griff: two fingers move and zoom the camera. */
        PINCHING,
        /** Griff: one finger left of a pinch moves the camera on. */
        PINCH_PAN,
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

        /** A click here now; whether it comes soon after the last and near it. */
        boolean clickedAt(float x, float y, long time, float slop) {
            boolean again = recent(x, y, time, slop);
            mTime = time;
            if (!again) {
                mX = x;
                mY = y;
            }
            return again;
        }

        /** Whether a click here now would be the game's double click with the last. */
        boolean recent(float x, float y, long time, float slop) {
            return time - mTime <= DOUBLE_TAP_MILLISECONDS && Math.hypot(x - mX, y - mY) <= 2 * slop;
        }

        float x() {
            return mX;
        }

        float y() {
            return mY;
        }
    }

    /**
     * Griff's tap ladder (the mockup's tapA, nextTapCount): a tap within LADDER_MILLISECONDS of the last tap's lift and
     * LADDER_MM of it climbs a rung, by what the ladder's first tap was, whatever lies under this one: after a tap that
     * selected one's own unit, all of its type on the screen, then on the map; after a plain move, an attack-move there; after
     * a tap on nothing with nothing selected, all one's own mobile units on the screen. The last two end the ladder. A tap on
     * the game's UI is clicked again where the first went, as the game's double click wants it, within its own reach and time.
     * Plus, Attack and Box keep a tap off the ladder, as do a drag, a rest, a stroke and two fingers.
     */
    static final class TapLadder {
        /** Whether there is a last tap to climb from. */
        private boolean mLast;
        private long mLift;
        private float mX;
        private float mY;
        /** The ladder's first tap: where it was, what it was (TAP_, 0 until the program says), its rung and its number. */
        private float mBaseX;
        private float mBaseY;
        private int mKind;
        private int mRung;
        private int mTap;

        /**
         * The step of a tap here, lifted at `lift`, with these of the rail's buttons counting; it is the last tap from now on.
         * `reach`: LADDER_MM in pixels; `uiReach`: how near the UI's double click wants the second click.
         */
        int next(float x, float y, long lift, int rail, float reach, float uiReach) {
            int step = STEP_TAP;
            if (mLast && !offLadder(rail)) {
                boolean climbs = lift - mLift <= LADDER_MILLISECONDS && Math.hypot(x - mX, y - mY) <= reach;
                switch (mKind) {
                    case TAP_UI:
                        if (lift - mLift <= DOUBLE_TAP_MILLISECONDS && Math.hypot(x - mBaseX, y - mBaseY) <= uiReach) {
                            step = STEP_AGAIN;
                        }
                        break;
                    case TAP_OWN:
                        if (climbs) {
                            step = mRung >= 2 ? STEP_ALL_OF_TYPE : STEP_TYPE_IN_VIEW;
                        }
                        break;
                    case TAP_GROUND:
                        step = climbs ? STEP_ATTACK_MOVE : STEP_TAP;
                        break;
                    case TAP_EMPTY:
                        step = climbs ? STEP_ALL_MOBILE : STEP_TAP;
                        break;
                    default:
                        // Unknown yet, or nothing to climb from.
                        break;
                }
            }
            if (step == STEP_TAP) {
                mBaseX = x;
                mBaseY = y;
                mKind = 0;
                mRung = 1;
                ++mTap;
            } else {
                mRung = Math.min(mRung + 1, 3);
            }
            // An attack-move and the grab of the army are the top of their ladders.
            mLast = step != STEP_ATTACK_MOVE && step != STEP_ALL_MOBILE;
            mLift = lift;
            mX = x;
            mY = y;
            return step;
        }

        /** The program says what the ladder's first tap, number `tap`, was. */
        void answered(int tap, int kind) {
            if (tap == mTap && mRung == 1) {
                mKind = kind;
            }
        }

        /**
         * Whether a finger down here at `time`, with these of the rail's buttons counting, may become the ladder's next tap,
         * which the handle then leaves to it: so a double tap on a unit does not land on the handle its first tap raised. Also
         * while the program has not said what the last tap was. After the third rung there is nothing to climb, and Box, Plus
         * and Attack keep a tap off the ladder, as {@link #next} has it.
         */
        boolean climbing(float x, float y, long time, int rail, float reach) {
            return mLast && !offLadder(rail) && mRung < 3 && mKind != TAP_UI && mKind != TAP_OTHER
                && time - mLift <= LADDER_MILLISECONDS && Math.hypot(x - mX, y - mY) <= reach;
        }

        /** Box, Plus and Attack make a tap theirs: it climbs no ladder. */
        private static boolean offLadder(int rail) {
            return (rail & (RAIL_BOX | RAIL_QUEUE | RAIL_ATTACK)) != 0;
        }

        /** Another gesture came between: the next tap starts a ladder. */
        void clear() {
            mLast = false;
        }

        float baseX() {
            return mBaseX;
        }

        float baseY() {
            return mBaseY;
        }

        int tap() {
            return mTap;
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
    /** When the gesture's first finger came down. */
    private long mFirstDown;
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
    /** Griff: the touch scheme is on; the screen's pixels in a mm, and its slop in them. */
    private boolean mGriff;
    private float mMillimetre = 10;
    private float mGriffSlop = GRIFF_SLOP_MM * 10;
    /** Griff: the rail's buttons as they counted for a waiting tap when its finger lifted (#fingerRail), and when it lifted. */
    private int mClickRail;
    private long mClickLift;
    private final TapLadder mLadder = new TapLadder();
    /**
     * Griff: the rail's Box as it counted when the first finger came down: FAF's reach for a unit beside the finger was set for
     * it (Mouse#griffDown), so the finger's tap and rest are Box's by it, not by Box at their lift.
     */
    private int mFingerBox;
    /**
     * Griff: the first finger came down on the handle, which left it to the ladder as that may climb; and so did a waiting tap's
     * finger. A tap of it that does not climb after all is the handle's.
     */
    private boolean mOnHandle;
    private boolean mClickOnHandle;
    /** Griff: what the finger holds is a drag the program pressed (Mouse#griffDrag), not `mPressed`; it may be a pan. */
    private boolean mPressedGriff;
    private boolean mMayPan;
    /** Griff: the last drag's number (Mouse#griffDrag), and whether the program has said that it made a pan of it (#panned). */
    private int mDrag;
    private boolean mPanned;
    /** Griff: a rest's ring is shown. */
    private boolean mRing;
    /** Griff: the last rest that lifted rang, and so buzzed, for the group its right press may store (#storeBuzzes). */
    private boolean mRestRang;
    /** Griff: the first finger's last places with their times, the oldest overwritten: its down first. */
    private final float[] mTrailX = new float[TRAIL];
    private final float[] mTrailY = new float[TRAIL];
    private final long[] mTrailTime = new long[TRAIL];
    private int mTrailNext;
    private int mTrailCount;
    /** Griff: the finger that moves the camera, where it last was and when, and its velocity in pixels a second. */
    private Finger mFollowed;
    private float mFollowX;
    private float mFollowY;
    private long mFollowTime;
    private float mVelocityX;
    private float mVelocityY;
    /** Griff: the stroke from the handle, in the screen's pixels, and how far the finger has gone from its start. */
    private final float[] mStrokeX = new float[STROKE_POINTS];
    private final float[] mStrokeY = new float[STROKE_POINTS];
    private int mStrokeCount;
    private float mStrokeFar;
    private final float[] mWaypointX = new float[MAX_WAYPOINTS];
    private final float[] mWaypointY = new float[MAX_WAYPOINTS];
    /** Griff: when the cursor last hovered for the stroke, and whether a hover waits for its time. */
    private long mHoverAt;
    private boolean mHoverWaits;
    /** Griff's draft: the plan the finger came down on (0: none), and the one a waiting tap was on. */
    private int mDraftPlan;
    private int mClickPlan;
    /** Griff: how many runs of the program's clicks had begun when the gesture's first finger came down (#clicksCame). */
    private int mClickRuns;

    /** `slop`: how far a finger may wander in the screen's pixels and still count as staying. */
    TouchInput(Mouse mouse, float slop) {
        mMouse = mouse;
        mSlop = slop;
    }

    /** The touch scheme Griff in place of the mouse, for the gestures that begin after this. */
    void setGriff(boolean griff) {
        mGriff = griff;
    }

    /** The screen's pixels in a mm on the glass, by which Griff measures its gestures. */
    void setMillimetre(float pixels) {
        mMillimetre = pixels;
        mGriffSlop = GRIFF_SLOP_MM * pixels;
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

    /** Griff: the program says what tap number `tap` was (TAP_), for the ladder. */
    void tapAnswered(int tap, int kind) {
        mLadder.answered(tap, kind);
    }

    /** Griff: what the last taps ordered was taken back (the undo's pill): the next tap climbs no ladder from it. */
    void clearLadder() {
        mLadder.clear();
    }

    /**
     * Griff: the program made a pan of drag number `drag` (Game.SAID_PAN), which only it can tell from a panel's drag or the
     * minimap's: a second finger turns that drag into two fingers. A word for an earlier drag counts for nothing.
     */
    void panned(int drag) {
        if (drag == mDrag) {
            mPanned = true;
        }
    }

    /**
     * Griff: FAF stored a control group (Game.TOAST_GROUP_STORED, a right press on its slot): whether the phone buzzes for it now,
     * so that each stored group is felt once, as the mockup's. It is the last rest's, which buzzed as it rang, unless Box counted:
     * that gives a rest no ring, but on a panel leaves its lift FAF's right press. A rest's ring counts for one store, so one that
     * no rest made after it, a mouse's right click, buzzes.
     */
    boolean storeBuzzes() {
        boolean buzzed = mRestRang;
        mRestRang = false;
        return !buzzed;
    }

    private int tapButton() {
        return mSwapped ? Game.BUTTON_RIGHT : Game.BUTTON_LEFT;
    }

    private int holdButton() {
        return mSwapped ? Game.BUTTON_LEFT : Game.BUTTON_RIGHT;
    }

    /** How far a finger may wander and still stay: Griff's own for its gestures, the system's for the mouse's. */
    private float slop() {
        return mGriff ? mGriffSlop : mSlop;
    }

    @Override
    public void down(int id, float x, float y, long time, boolean first) {
        // First: a leave on its way counts as taken back, whatever this finger goes on to send.
        mMouse.stay();
        if (first) {
            if (live() && clicksCame()) {
                giveUpForClicks();
            } else if (live()) {
                endGesture();
            }
            finishClick();
            bringCursorBackToGhost();
            mSecond.id = -1;
            startOneFinger(id, x, y, time);
        } else if (live() && clicksCame()) {
            // The finger joins a gesture the program's clicks came into: both rest until they lift.
            giveUpForClicks();
        } else if (mState == State.PLACE_LINE) {
            giveUpLine();
            mState = State.SPENT;
        } else if (mState == State.STROKE) {
            // A second finger gives the route up.
            giveUpStroke();
            mState = State.SPENT;
        } else if (mState == State.DRAFT_LINE || mState == State.DRAFT_MOVE || mState == State.DRAFT_BOX) {
            // The mockup's: a second finger gives the line, the move or the box up, and the two pinch at once, no tap of two,
            // which would drop the whole draft.
            giveUpDraftGesture();
            startTwo(id, x, y, time);
            startPinch();
        } else if (mState == State.DRAFT_PENDING || mState == State.DRAFT_AIM) {
            giveUpDraftGesture();
            mMouse.stopTimer();
            startTwo(id, x, y, time);
        } else if (mState == State.PENDING || mState == State.RESTED || mState == State.PLACE_PENDING
                   || mState == State.PLACE_LINE_READY) {
            mGhostToRestore = mState != State.PENDING && mState != State.RESTED && mGhostPlaced;
            mMouse.stopTimer();
            hideRing();
            startTwo(id, x, y, time);
        } else if (mState == State.PRESSED && mPressedGriff && mPanned) {
            // Griff: a second finger turns a pan into two fingers, which move the camera themselves. Only a drag that the program
            // has said is a pan: on a panel or the minimap the left button drags, and the map is not the fingers' to pinch.
            mMouse.griffDragEnd(mFirst.x, mFirst.y, 0, 0);
            mPressedGriff = false;
            startTwo(id, x, y, time);
        } else if (mState == State.PINCH_PAN) {
            // Griff: a finger again beside the one left of a pinch pinches anew, from where the two are.
            endPinch(false, time);
            if (mFollowed != mFirst) {
                mFirst.id = mFollowed.id;
                mFirst.place(mFollowed.x, mFollowed.y);
            }
            startTwo(id, x, y, time);
        }
    }

    private void startTwo(int id, float x, float y, long time) {
        mSecond.id = id;
        mSecond.place(x, y);
        mSecondX = x;
        mSecondY = y;
        mStartX = middleX();
        mStartY = middleY();
        mStartDistance = distance();
        mStartTime = time;
        mLadder.clear();
        mMouse.move(x, y);
        mState = State.TWO_PENDING;
    }

    private void startOneFinger(int id, float x, float y, long time) {
        mFirstDown = time;
        mOnHandle = false;
        int rail = mGriff ? mMouse.rail() : 0;
        mFingerBox = rail & RAIL_BOX;
        if (mGriff) {
            // The runs of the program's clicks so far: one that begins while the gesture goes on gives it up (#clicksCame).
            mClickRuns = mMouse.clickRuns();
            // A finger on the map stops the camera's glide, and FAF's reach for a unit beside a tap follows the selection, or
            // Box, which picks units: the finger's tap and rest go by this Box (#fingerRail).
            mMouse.griffDown(x, y, mFingerBox != 0);
            if (mMouse.clicking()) {
                // The program's clicks run: the fingers rest until they are over, also on FAF's ghost of the tick's build mode
                // and on the handle. Only the down went, which then does nothing but end clicks that wait past their patience.
                mState = State.SPENT;
                return;
            }
        }
        if (!mDeleting && mMouse.placing()) {
            startPlacing(id, x, y, time);
        } else if (mGriff && mMouse.drafting()) {
            // The draft: fingers make plans, and there is no handle.
            mGhostPlaced = false;
            startDraft(id, x, y, time);
        } else if (mGriff && mMouse.onGriff(x, y)) {
            mGhostPlaced = false;
            if (mLadder.climbing(x, y, time, rail, LADDER_MM * mMillimetre)) {
                // The ladder first: a second tap on a unit is its double click, even where the first one has raised the handle.
                // Where its lift does not climb after all, it is the handle's tap (finishClick).
                mOnHandle = true;
                startPending(id, x, y, time);
            } else {
                // After the third rung, or with Plus or Attack counting, the handle takes a drag at once.
                startStroke(id, x, y, time);
            }
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
        startTrail(x, y, time);
        mMouse.move(x, y);
        mMouse.startTimer(mGriff ? REST_MILLISECONDS : HOLD_MILLISECONDS);
        mState = State.PENDING;
    }

    /** Griff: a finger came down in the draft, maybe on a plan; taps do not climb the ladder. */
    private void startDraft(int id, float x, float y, long time) {
        mFirst.id = id;
        mFirst.place(x, y);
        mStartX = x;
        mStartY = y;
        mStartTime = time;
        startTrail(x, y, time);
        mLadder.clear();
        mDraftPlan = mMouse.draftPlan(x, y);
        mMouse.move(x, y);
        mMouse.startTimer(DRAFT_REST_MILLISECONDS);
        mState = State.DRAFT_PENDING;
    }

    /** Griff: a finger came down on the handle. Nothing goes to the game until it lifts but the cursor's hover. */
    private void startStroke(int id, float x, float y, long time) {
        mFirst.id = id;
        mFirst.place(x, y);
        mStartX = x;
        mStartY = y;
        mStartTime = time;
        mStrokeCount = 0;
        mStrokeFar = 0;
        addStrokePoint(x, y);
        startTrail(x, y, time);
        mLadder.clear();
        showStroke();
        mHoverAt = time;
        mHoverWaits = false;
        mMouse.strokeHover(x, y);
        mState = State.STROKE;
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
    public void moved(long time) {
        if (live() && clicksCame()) {
            giveUpForClicks();
            return;
        }
        switch (mState) {
            case PENDING:
                addTrail(time);
                if (!mDeleting && Math.hypot(mFirst.x - mStartX, mFirst.y - mStartY) > slop()) {
                    mMouse.stopTimer();
                    if (!mGriff) {
                        press(tapButton(), mStartX, mStartY);
                        mMouse.move(mFirst.x, mFirst.y);
                    } else {
                        startGriffDrag(false, time);
                    }
                }
                break;
            case RESTED:
                addTrail(time);
                if (Math.hypot(mFirst.x - mStartX, mFirst.y - mStartY) > mGriffSlop) {
                    hideRing();
                    startGriffDrag(true, time);
                } else if (mRing) {
                    mMouse.ring(true, mFirst.x, mFirst.y);
                }
                break;
            case STROKE:
                addTrail(time);
                strokeTo(mFirst.x, mFirst.y, time);
                break;
            case DRAFT_PENDING:
                addTrail(time);
                if (Math.hypot(mFirst.x - mStartX, mFirst.y - mStartY) > mGriffSlop) {
                    mMouse.stopTimer();
                    startDraftDrag(time);
                }
                break;
            case DRAFT_LINE:
                addTrail(time);
                mMouse.draftLine(DRAFT_SHOW, mFirst.x, mFirst.y);
                break;
            case DRAFT_MOVE:
                addTrail(time);
                mMouse.draftMove(DRAFT_SHOW, mStartX, mStartY, mFirst.x, mFirst.y);
                break;
            case DRAFT_AIM:
                addTrail(time);
                mMouse.draftAim(DRAFT_SHOW, mFirst.x, mFirst.y);
                break;
            case DRAFT_BOX:
                addTrail(time);
                mMouse.draftBox(DRAFT_SHOW, mStartX, mStartY, mFirst.x, mFirst.y);
                break;
            case PRESSED:
                mMouse.move(mFirst.x, mFirst.y);
                if (mPressedGriff) {
                    follow(time);
                }
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
                if (mGriff) {
                    // The mockup's: two fingers that hardly move may still be a tap, and past that they pinch at once.
                    float still = TWO_FINGERS_STILL_MM * mMillimetre;
                    if (Math.abs(distance() - mStartDistance) >= still || Math.hypot(middleX() - mStartX, middleY() - mStartY) >= still) {
                        startPinch();
                    }
                } else if (Math.abs(distance() - mStartDistance) > 2 * mSlop) {
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
            case PINCHING:
                mMouse.pinch(false, middleX(), middleY(), distance());
                break;
            case PINCH_PAN:
                mMouse.pinch(false, mFollowed.x, mFollowed.y, 0);
                follow(time);
                break;
            default:
                break;
        }
    }

    @Override
    public void up(int id, long time, boolean last) {
        if ((id == mFirst.id || id == mSecond.id) && live() && clicksCame()) {
            giveUpForClicks();
        }
        if (id == mFirst.id || id == mSecond.id) {
            switch (mState) {
                case PENDING:
                    mMouse.stopTimer();
                    if (mGriff) {
                        mClickRail = fingerRail();
                        mClickOnHandle = mOnHandle;
                        click(GRIFF_TAP, mStartX, mStartY, time - mStartTime, time);
                    } else {
                        click(mDeleting ? DELETE_ORDER : tapButton(), mStartX, mStartY, time - mStartTime, time);
                    }
                    mState = State.SPENT;
                    break;
                case RESTED:
                    restTap();
                    mState = State.SPENT;
                    break;
                case STROKE:
                    finishStroke(time);
                    mState = State.SPENT;
                    break;
                case DRAFT_PENDING:
                    mMouse.stopTimer();
                    mClickPlan = mDraftPlan;
                    click(DRAFT_TAP, mStartX, mStartY, time - mStartTime, time);
                    mState = State.SPENT;
                    break;
                case DRAFT_LINE:
                case DRAFT_MOVE:
                case DRAFT_AIM:
                case DRAFT_BOX:
                    finishDraftGesture(time);
                    mState = State.SPENT;
                    break;
                case PRESSED:
                    releasePressed(true, time);
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
                    // Griff counts both fingers, from the first one down: a finger that rested and a quick second are no tap.
                    if (time - (mGriff ? mFirstDown : mStartTime) <= TWO_FINGER_TAP_MILLISECONDS) {
                        if (mGriff && !mMouse.placing() && mMouse.drafting()) {
                            mMouse.draftDrop();
                        } else if (mGriff && !mMouse.placing()) {
                            mMouse.deselect();
                        } else {
                            int button = mMouse.placing() ? Game.BUTTON_RIGHT : holdButton();
                            click(button, mSecondX, mSecondY, time - mStartTime, time);
                        }
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
                case PINCHING:
                    // The finger left moves the camera on from where it is, as a pan.
                    startPinchPan(id == mFirst.id ? mSecond : mFirst, time);
                    break;
                case PINCH_PAN:
                    if (id == mFollowed.id) {
                        endPinch(true, time);
                        mState = State.SPENT;
                    }
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
        if ((id == mFirst.id || id == mSecond.id) && live() && clicksCame()) {
            // The finger left rests, as the gesture it was part of is given up.
            giveUpForClicks();
        } else if (mState == State.TWO_PENDING && (id == mFirst.id || id == mSecond.id)) {
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
        if (live() && clicksCame()) {
            giveUpForClicks();
        } else {
            endGesture();
        }
        mMouse.leaveSoon();
    }

    /** A gesture goes on: a finger is down, and what it does is not over. */
    private boolean live() {
        return mState != State.NONE && mState != State.SPENT;
    }

    /**
     * Griff: whether the program's clicks came into the gesture: they run, or one of their runs began since its first finger came
     * down. They take FAF's mode, the selection, Shift and the cursor for themselves, and the gesture's input would come between
     * their clicks, or act on what they did once they are over (a tap after the tick's clicks would give its builders another
     * order). Java hears of them a moment after they began; what a finger gave in that moment the program drops.
     */
    private boolean clicksCame() {
        return mGriff && (mMouse.clicking() || mMouse.clickRuns() != mClickRuns);
    }

    /**
     * Griff: the program's clicks came into the gesture (#clicksCame): it is given up, and nothing more of it goes to the game;
     * its fingers rest until they lift. Only what shows of it goes: the ring, the stroke (Cancelled, as a stroke given up), the
     * draft's line, move, aim or box as a second finger gives them up, and a pinch lets the camera go. A line of FAF's ghost the
     * clicks gave up as they began, as its own cancel does, and the ghost's mode they took. A tap that waits goes too.
     */
    private void giveUpForClicks() {
        mMouse.stopTimer();
        switch (mState) {
            case STROKE:
                hideStroke();
                mMouse.strokeGivenUp();
                break;
            case DRAFT_LINE:
            case DRAFT_MOVE:
            case DRAFT_AIM:
            case DRAFT_BOX:
                giveUpDraftGesture();
                break;
            case PINCHING:
            case PINCH_PAN:
                endPinch(false, 0);
                break;
            default:
                break;
        }
        hideRing();
        mPressedGriff = false;
        mGhostToRestore = false;
        mClickButton = NO_CLICK;
        mLadder.clear();
        if (mState != State.NONE) {
            mState = State.SPENT;
        }
    }

    private void endGesture() {
        mMouse.stopTimer();
        if (mState == State.PRESSED) {
            releasePressed(false, 0);
        } else if (mState == State.PANNING) {
            mMouse.button(Game.BUTTON_MIDDLE, false, middleX(), middleY());
        } else if (mState == State.PLACE_LINE) {
            giveUpLine();
        } else if (mState == State.STROKE) {
            // A route goes only where a finger lifted: the system's taking the fingers gives it up.
            hideStroke();
        } else if (mState == State.DRAFT_LINE || mState == State.DRAFT_MOVE || mState == State.DRAFT_AIM || mState == State.DRAFT_BOX) {
            giveUpDraftGesture();
        } else if (mState == State.PINCHING || mState == State.PINCH_PAN) {
            endPinch(false, 0);
        }
        hideRing();
        finishClick();
        bringCursorBackToGhost();
        mState = State.SPENT;
    }

    /** The time from {@link Mouse#startTimer} has passed. */
    void timer() {
        if ((live() || mClickButton != NO_CLICK) && clicksCame()) {
            giveUpForClicks();
            return;
        }
        if (mState == State.PENDING && !mDeleting) {
            if (mGriff) {
                mState = State.RESTED;
                mLadder.clear();
                // With Box a rest is none (the mockup's): no ring, nothing to feel, and its lift is Box's tap on the map.
                if (mFingerBox == 0) {
                    mRing = true;
                    mMouse.ring(true, mFirst.x, mFirst.y);
                    mMouse.held();
                }
            } else {
                press(holdButton(), mStartX, mStartY);
                mMouse.held();
            }
        } else if (mState == State.PLACE_PENDING) {
            if (startedOnGhost()) {
                mState = State.PLACE_LINE_READY;
            } else {
                startTakingGhost();
            }
            mMouse.held();
        } else if (mState == State.STROKE && mHoverWaits) {
            mHoverWaits = false;
            mHoverAt += HOVER_MILLISECONDS;
            mMouse.strokeHover(mFirst.x, mFirst.y);
        } else if (mState == State.DRAFT_PENDING && mDraftPlan == 0 && (mMouse.rail() & RAIL_BOX) == 0) {
            // A rest on no plan, without Box (the mockup's): the plan its lift places shows under it, with the loupe.
            mState = State.DRAFT_AIM;
            mMouse.draftAim(DRAFT_SHOW, mFirst.x, mFirst.y);
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
        mPressedGriff = false;
        mMouse.button(button, true, x, y);
        mState = State.PRESSED;
    }

    /**
     * Griff: a drag from where the finger came down, which the program makes a pan, a box or a panel's drag. A pan starts
     * where the finger passed the slop, so the map does not jump by it.
     */
    private void startGriffDrag(boolean rested, long time) {
        startGriffDrag(rested, time, mMouse.rail());
    }

    /** With these of the rail's buttons counting for it. */
    private void startGriffDrag(boolean rested, long time, int rail) {
        mPressedGriff = true;
        // A pan glides on (releasePressed); only the program can tell it from a panel's or the minimap's drag (#panned).
        mMayPan = !rested && (rail & RAIL_BOX) == 0;
        mPanned = false;
        ++mDrag;
        mLadder.clear();
        mMouse.griffDrag(mStartX, mStartY, mFirst.x, mFirst.y, rested, rail, mDrag);
        mMouse.move(mFirst.x, mFirst.y);
        startFollowing(mFirst, time);
        mState = State.PRESSED;
        // A box is an action that Box and Plus count for, a pan is none.
        int used = !mMayPan ? rail & (RAIL_BOX | RAIL_QUEUE) : 0;
        if (used != 0) {
            mMouse.railUsed(used);
        }
    }

    /**
     * Griff: a rest and no drag: on a panel the right click; on the map nothing, as the finger changed its mind. Either way the
     * rail counted for it (Rail). With Box it was no rest, and on the map it is Box's tap, of which the program says what it used.
     */
    private void restTap() {
        hideRing();
        // It rang and buzzed unless Box counted at the finger's down (#timer), which on a panel is a rest all the same.
        mRestRang = mFingerBox == 0;
        int rail = fingerRail();
        mMouse.restTap(mStartX, mStartY, rail);
        if (rail != 0 && (rail & RAIL_BOX) == 0) {
            mMouse.railUsed(rail);
        }
    }

    /** Griff: the rail's buttons that count for the first finger's tap or rest now, Box as it counted at the finger's down. */
    private int fingerRail() {
        return (mMouse.rail() & ~RAIL_BOX) | mFingerBox;
    }

    /** `glide`: the finger lifted, and a pan that moved until then glides on. */
    private void releasePressed(boolean glide, long time) {
        if (mPressedGriff) {
            boolean moving = glide && mMayPan && time - mFollowTime < GLIDE_MILLISECONDS;
            mMouse.griffDragEnd(mFirst.x, mFirst.y, moving ? GLIDE_SPEED * mVelocityX : 0, moving ? GLIDE_SPEED * mVelocityY : 0);
            mPressedGriff = false;
        } else {
            mMouse.button(mPressed, false, mFirst.x, mFirst.y);
        }
    }

    private void hideRing() {
        if (mRing) {
            mRing = false;
            mMouse.ring(false, 0, 0);
        }
    }

    // --- Griff's draft -------------------------------------------------------------------------------------------------

    /**
     * Griff: a finger in the draft passed the slop (the mockup's draftMoveA): with Box and a building on deposits a box over
     * them, from wherever it came down; from the newest plan a line; from another plan the whole draft moves; from nothing, or
     * from a plan on a deposit, it pans, whatever the rail says.
     */
    private void startDraftDrag(long time) {
        boolean movable = mDraftPlan != 0 && !mMouse.draftOnDeposit(mDraftPlan);
        mLadder.clear();
        if ((mMouse.rail() & RAIL_BOX) != 0 && mMouse.draftPicksDeposit()) {
            mState = State.DRAFT_BOX;
            mMouse.draftBox(DRAFT_SHOW, mStartX, mStartY, mFirst.x, mFirst.y);
        } else if (movable && mMouse.draftNewest(mDraftPlan)) {
            mState = State.DRAFT_LINE;
            mMouse.draftLine(DRAFT_SHOW, mFirst.x, mFirst.y);
        } else if (movable) {
            mState = State.DRAFT_MOVE;
            mMouse.draftMove(DRAFT_SHOW, mStartX, mStartY, mFirst.x, mFirst.y);
        } else {
            startGriffDrag(false, time, 0);
        }
    }

    /** Griff: the finger of a draft gesture lifted: what it gives counts where the finger was a moment before. */
    private void finishDraftGesture(long time) {
        int settled = settled(time);
        float x = mTrailX[settled];
        float y = mTrailY[settled];
        switch (mState) {
            case DRAFT_LINE:
                mMouse.draftLine(DRAFT_END, x, y);
                break;
            case DRAFT_MOVE:
                mMouse.draftMove(DRAFT_END, mStartX, mStartY, x, y);
                break;
            case DRAFT_AIM:
                mMouse.draftAim(DRAFT_END, x, y);
                break;
            case DRAFT_BOX:
                mMouse.draftBox(DRAFT_END, mStartX, mStartY, x, y);
                mMouse.railUsed(RAIL_BOX);
                break;
            default:
                break;
        }
    }

    /** Griff: a draft gesture given up, by a second finger or the system: nothing comes of it, a move puts the plans back. */
    private void giveUpDraftGesture() {
        switch (mState) {
            case DRAFT_LINE:
                mMouse.draftLine(DRAFT_ABORT, mFirst.x, mFirst.y);
                break;
            case DRAFT_MOVE:
                mMouse.draftMove(DRAFT_ABORT, mStartX, mStartY, mFirst.x, mFirst.y);
                break;
            case DRAFT_AIM:
                mMouse.draftAim(DRAFT_ABORT, mFirst.x, mFirst.y);
                break;
            case DRAFT_BOX:
                mMouse.draftBox(DRAFT_ABORT, mStartX, mStartY, mFirst.x, mFirst.y);
                break;
            default:
                break;
        }
    }

    // --- Griff's trail, velocity and settled lift ---------------------------------------------------------------------

    private void startTrail(float x, float y, long time) {
        mTrailCount = 0;
        mTrailNext = 0;
        trail(x, y, time);
    }

    private void addTrail(long time) {
        trail(mFirst.x, mFirst.y, time);
    }

    private void trail(float x, float y, long time) {
        mTrailX[mTrailNext] = x;
        mTrailY[mTrailNext] = y;
        mTrailTime[mTrailNext] = time;
        mTrailNext = (mTrailNext + 1) % TRAIL;
        mTrailCount = Math.min(mTrailCount + 1, TRAIL);
    }

    /**
     * Where a finger lifting at `time` counts as having lifted (the mockup's settle): its slot in the trail, the newest sample
     * at least SETTLE_LIFT_MILLISECONDS before `time`, else the oldest kept.
     */
    private int settled(long time) {
        int at = (mTrailNext - 1 + TRAIL) % TRAIL;
        for (int k = 1; k < mTrailCount && mTrailTime[at] > time - SETTLE_LIFT_MILLISECONDS; ++k) {
            at = (at - 1 + TRAIL) % TRAIL;
        }
        return at;
    }

    private void startFollowing(Finger finger, long time) {
        mFollowed = finger;
        mFollowX = finger.x;
        mFollowY = finger.y;
        mFollowTime = time;
        mVelocityX = 0;
        mVelocityY = 0;
    }

    /** The followed finger's velocity, smoothed over its moves (the mockup's panMove). */
    private void follow(long time) {
        float seconds = Math.max(1, time - mFollowTime) / 1000f;
        mVelocityX = VELOCITY_KEPT * mVelocityX + (1 - VELOCITY_KEPT) * (mFollowed.x - mFollowX) / seconds;
        mVelocityY = VELOCITY_KEPT * mVelocityY + (1 - VELOCITY_KEPT) * (mFollowed.y - mFollowY) / seconds;
        mFollowX = mFollowed.x;
        mFollowY = mFollowed.y;
        mFollowTime = time;
    }

    // --- Griff's pinch -------------------------------------------------------------------------------------------------

    private void startPinch() {
        mGhostUsed = false;
        mMouse.pinch(true, middleX(), middleY(), distance());
        mState = State.PINCHING;
    }

    private void startPinchPan(Finger left, long time) {
        mMouse.pinch(true, left.x, left.y, 0);
        startFollowing(left, time);
        mState = State.PINCH_PAN;
    }

    /** `glide`: the last finger lifted, and the camera glides on if it moved until then. */
    private void endPinch(boolean glide, long time) {
        boolean moving = glide && mState == State.PINCH_PAN && time - mFollowTime < GLIDE_MILLISECONDS;
        mMouse.pinchEnd(moving ? GLIDE_SPEED * mVelocityX : 0, moving ? GLIDE_SPEED * mVelocityY : 0);
    }

    // --- Griff's stroke ------------------------------------------------------------------------------------------------

    private void addStrokePoint(float x, float y) {
        // Past the room for points the last one follows the finger: the route keeps its end.
        int at = Math.min(mStrokeCount, STROKE_POINTS - 1);
        mStrokeX[at] = x;
        mStrokeY[at] = y;
        mStrokeCount = at + 1;
    }

    /** Griff: the stroke follows the finger, a point a mm once it has left the handle's middle by one; the screen shows it. */
    private void strokeTo(float x, float y, long time) {
        mStrokeFar = Math.max(mStrokeFar, (float) Math.hypot(x - mStartX, y - mStartY));
        hover(x, y, time);
        int last = mStrokeCount - 1;
        if (mStrokeFar <= STROKE_POINT_MM * mMillimetre
            || Math.hypot(x - mStrokeX[last], y - mStrokeY[last]) < STROKE_POINT_MM * mMillimetre) {
            return;
        }
        addStrokePoint(x, y);
        showStroke();
    }

    /** The cursor at the stroke's end, at most each HOVER_MILLISECONDS, and the last place when the time has come. */
    private void hover(float x, float y, long time) {
        if (time - mHoverAt >= HOVER_MILLISECONDS) {
            mHoverAt = time;
            mHoverWaits = false;
            mMouse.stopTimer();
            mMouse.strokeHover(x, y);
        } else if (!mHoverWaits) {
            mHoverWaits = true;
            mMouse.startTimer(mHoverAt + HOVER_MILLISECONDS - time);
        }
    }

    private int strokeWaypoints() {
        return Stroke.waypoints(mStrokeX, mStrokeY, mStrokeCount, WAYPOINT_MM * mMillimetre, CORNER_MM * mMillimetre, mWaypointX,
                                mWaypointY);
    }

    private void showStroke() {
        mMouse.strokeShown(mStrokeX, mStrokeY, mStrokeCount, Stroke.classify(mStrokeX, mStrokeY, mStrokeCount, mMillimetre), mWaypointX,
                           mWaypointY, strokeWaypoints());
    }

    private void hideStroke() {
        mHoverWaits = false;
        mMouse.strokeShown(mStrokeX, mStrokeY, 0, Stroke.CANCEL, mWaypointX, mWaypointY, 0);
    }

    private void giveUpStroke() {
        mMouse.stopTimer();
        hideStroke();
        mMouse.strokeGivenUp();
    }

    /** Cuts the stroke back to its point nearest the settled lift (the mockup's strokeTrimTo), which takes its place. */
    private void trimStrokeTo(float x, float y) {
        int best = mStrokeCount - 1;
        double bestDistance = Double.MAX_VALUE;
        for (int k = mStrokeCount - 1; k >= Math.max(1, mStrokeCount - TRIM_POINTS); --k) {
            double distance = Math.hypot(mStrokeX[k] - x, mStrokeY[k] - y);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = k;
            }
        }
        mStrokeCount = best + 1;
        mStrokeX[best] = x;
        mStrokeY[best] = y;
    }

    /**
     * Griff: the finger lifted from its stroke: a tap on the handle, or what the stroke gives where the finger was a moment
     * before: a route, a patrol by a loop, or nothing for a loop taken back.
     */
    private void finishStroke(long time) {
        mMouse.stopTimer();
        hideStroke();
        if (mStrokeFar <= mGriffSlop) {
            mMouse.griffTap();
            return;
        }
        int settled = settled(time);
        addStrokePoint(mFirst.x, mFirst.y);
        trimStrokeTo(mTrailX[settled], mTrailY[settled]);
        int mode = Stroke.classify(mStrokeX, mStrokeY, mStrokeCount, mMillimetre);
        if (mode == Stroke.CANCEL) {
            mMouse.strokeGivenUp();
            return;
        }
        int rail = mMouse.rail();
        mMouse.route(mWaypointX, mWaypointY, strokeWaypoints(), rail, mode == Stroke.PATROL);
        if ((rail & (RAIL_QUEUE | RAIL_ATTACK)) != 0) {
            mMouse.railUsed(rail & (RAIL_QUEUE | RAIL_ATTACK));
        }
    }

    /** A click `rested` milliseconds after the cursor came there, now or when it has settled. */
    private void click(int button, float x, float y, long rested, long time) {
        mClickButton = button;
        if (button == GRIFF_TAP || button == DRAFT_TAP) {
            // Griff's tap is decided by what the cursor rests on, at the finger's place, so it clicks there; its ladder decides
            // where a double click goes.
            mClickLift = time;
            mClickX = x;
            mClickY = y;
        } else {
            mLastClick.clickedAt(x, y, time, mSlop);
            mClickX = mLastClick.x();
            mClickY = mLastClick.y();
        }
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
        if (clicksCame()) {
            // The program's clicks began since the tap's finger came down, as while its click waited for the cursor's rest: they
            // took what it would act on.
            return;
        }
        if (button == DELETE_ORDER) {
            mMouse.deleteOrder(mClickX, mClickY);
            return;
        }
        if (button == DRAFT_TAP) {
            // A plan is no action of the rail's: Plus stays for the tick.
            mMouse.draftTap(mClickX, mClickY, mClickPlan);
            return;
        }
        if (button == GRIFF_TAP) {
            int step = mLadder.next(mClickX, mClickY, mClickLift, mClickRail, LADDER_MM * mMillimetre, 2 * mSlop);
            if (step == STEP_TAP && mClickOnHandle) {
                // On the handle, which left the finger to the ladder, and the ladder did not climb: the handle's tap, never FAF's
                // click at the handle's place. As a stroke's, it starts no ladder and uses none of the rail.
                mLadder.clear();
                mMouse.griffTap();
                return;
            }
            mMouse.smartTap(mClickX, mClickY, mClickRail, step, mLadder.baseX(), mLadder.baseY(), mLadder.tap());
            // A tap is the next action whatever it turns out to be: the rail's buttons for it go off (Rail). Box's tap on
            // nothing leaves Box lit, so with Box the program says what was used.
            if (mClickRail != 0 && (mClickRail & RAIL_BOX) == 0) {
                mMouse.railUsed(mClickRail);
            }
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
