package io.github.m3rt1n99.fafre;

import android.view.Surface;

/**
 * The game (libfafgame.so: the whole engine, with its host android/native/src/host/GameHost.cpp).
 * The host runs the program as WinMain does on the PC, on a thread of its own, and turns the
 * surface, the input and the activity's lifecycle into what Windows gives the program: the screen,
 * mouse and key messages, and activation.
 */
final class Game {
    /** The mouse's buttons, the host's numbers. */
    static final int BUTTON_LEFT = 0;
    static final int BUTTON_RIGHT = 1;
    static final int BUTTON_MIDDLE = 2;
    static final int BUTTON_X1 = 3;
    static final int BUTTON_X2 = 4;

    /** WHEEL_DELTA: one notch of a mouse wheel. */
    static final int WHEEL_DELTA = 120;

    /** {@link #listAnswer}: what is known of the list under the last finger. */
    static final int NO_LIST = 1;
    static final int LIST = 2;

    /** {@link #takeToShow}: why the activity is to bring these lines into view. */
    static final int FIELD_TAPPED = 1;
    static final int LINES_TO_SHOW = 3;

    private static String sLoadError;
    /** Who hears that the program has something new to read. */
    private static volatile Runnable sListener;

    static {
        try {
            System.loadLibrary("fafgame");
        } catch (UnsatisfiedLinkError e) {
            sLoadError = String.valueOf(e.getMessage());
        }
    }

    private Game() {
    }

    /** Null, or why the library did not load. */
    static String loadError() {
        return sLoadError;
    }

    /**
     * Starts the program: `arguments` is its command line, the program's path first; `home` and
     * `temp` are its HOME and TMPDIR; the host's lines go to `reportPath`. False when it was
     * started already (one program per process, as on Windows) or cannot start. The process ends
     * with the program.
     */
    static native boolean start(String[] arguments, String home, String temp, String reportPath);

    /** Whether the program runs in this process: Android can make the activity anew around it. */
    static native boolean running();

    /** `listener` hears, on the program's thread, that there is something new to read: a page, a list's answer, a text field. */
    static void listen(Runnable listener) {
        sListener = listener;
    }

    /** `listener` hears no more, unless another has taken its place, as a new activity does. */
    static void stopListening(Runnable listener) {
        if (sListener == listener) {
            sListener = null;
        }
    }

    /** The program, on its thread (src/host/MenuFingers.cpp). */
    @SuppressWarnings("unused")
    private static void fromProgram() {
        Runnable listener = sListener;
        if (listener != null) {
            listener.run();
        }
    }

    /** The page FAF's Lua has declared, in the picture's pixels: left, top, right, bottom. False when there is none. */
    static native boolean page(int[] rect);

    /** Whether fingers act as in the menus ({@link PageInput}) or as in a game ({@link TouchInput}), as FAF's Lua has declared. */
    static native boolean menuFingers();

    /** The UI factor of the session's HUD, as FAF's Lua has set it; 0 before any has. */
    static native float hudFactor();

    /** A finger came down here: what list would it scroll? {@link #listAnswer} says. */
    static native void lookForList(int x, int y);

    /** 0 until it is known, then NO_LIST or LIST. */
    static native int listAnswer();

    /** The last finger's drag in the picture's pixels, positive downwards: the list takes it, and what it cannot take comes back as {@link #takePageDrag}. */
    static native void scrollList(float drag);

    /** What no list took, since the last call. */
    static native float takePageDrag();

    /** The keyboard is coming: if a text field has the focus, {@link #takeToShow} says where. */
    static native void askForField();

    /**
     * Lines of the picture to bring into view, top and bottom, which the activity has not heard of yet: 0 when there are none, or
     * why: a text field (FIELD_TAPPED, or another for what {@link #askForField} found) or FAF's Lua (LINES_TO_SHOW).
     */
    static native int takeToShow(int[] place);

    /** A finger's press for a drag, the left button's, on the small control it meant if there is one within `reach`. */
    static native void pressNear(int x, int y, int reach, int smallSide);

    /** A finger's click of the right button in the menus, on the small control it meant if there is one within `reach`. */
    static native void rightClickNear(int x, int y, int reach, int smallSide);

    /** Whether a building's ghost waits to be placed. */
    static native boolean placingBuilding();

    /** A finger tapped near a building's ghost: it places the building, unless the game's UI is under the finger or the ghost. */
    static native void tapNearGhost(int fingerX, int fingerY, int ghostX, int ghostY);

    /** Gives up the line of buildings a finger is drawing, with nothing built. */
    static native void cancelLine();

    /** Whether a game goes on behind the game's UI, as opposed to the menus and the score screen. */
    static native boolean gamePlaying();

    /**
     * A finger's tap, a click of the left button, which hits the small control it meant if there is one within
     * `reach`; `smallSide`: what counts as small. Both in the picture's pixels. Where a ghost waits, it stays where it landed.
     */
    static native void tap(int x, int y, int reach, int smallSide);

    /**
     * No finger is on the glass: the pointer leaves what it is over, unless a building's ghost is with it over the world, or
     * {@link #pointerStay} comes before the game has got to this.
     */
    static native void pointerLeave();

    /** A finger came down, ahead of what it sends: a {@link #pointerLeave} that has not happened by now does not happen. */
    static native void pointerStay();

    /** Deletes the queued order of the selected units nearest to the cursor, which is at this place, if one is near. */
    static native void deleteOrder(int x, int y);

    /** The surface and its size in pixels; null when it is gone. */
    static native void surface(Surface surface, int width, int height);

    /** The activity came to the front or went to the back. */
    static native void foreground(boolean front);

    /** The activity got the audio focus or lost it; without it the game's sound pauses. */
    static native void audioFocus(boolean held);

    /** Whether a network lobby or game has its socket open: not a skirmish, not the LAN screen's search for games. */
    static native boolean networkInUse();

    /** A line of the game's report, which also goes to the system log. */
    static native void report(String line);

    /** The pointer's position in the surface's pixels. */
    static native void mouseMove(int x, int y);

    static native void mouseButton(int button, boolean down, int x, int y);

    /** A finger's button: as a mouse's, except that where a building's ghost waits, its press only puts the ghost there. */
    static native void touchButton(int button, boolean down, int x, int y);

    /** `delta` in WHEEL_DELTA units, positive away from the user. */
    static native void mouseWheel(int delta, int x, int y);

    /** A key went down or up: Android's key code, the event's scan code, and what it types (0: nothing). */
    static native void key(int keyCode, int scanCode, boolean down, int character);

    /** Text without a key, as an input method commits it. */
    static native void text(String text);

    // --- The touch scheme Griff (src/host/GriffAndroid.cpp) ---------------------------------------------------------

    /** {@link #griff}: the values it gives, and where in them. */
    static final int GRIFF_VALUES = 11;
    static final int GRIFF_FREE_LEFT = 6;
    static final int GRIFF_FREE_TOP = 7;
    static final int GRIFF_FREE_RIGHT = 8;
    static final int GRIFF_STROKE = 9;
    static final int GRIFF_STROKE_END = 10;

    /** {@link #griffSaid}: what the program says, three numbers each: what, and two of its own. */
    static final int SAID_TAP = 1;
    static final int SAID_ROUTE_GIVEN_UP = 2;
    static final int SAID_TOAST = 3;
    static final int SAID_COMMITTED = 4;
    static final int SAID_PAN = 5;
    static final int SAID_ORDERED = 6;
    static final int SAID_UNDONE = 7;

    /** SAID_TOAST: what the draft says (GameActivity's words), with a number where it counts. */
    static final int TOAST_NO_FREE_RESOURCE = 1;
    static final int TOAST_TAKEN = 2;
    static final int TOAST_FULL = 3;
    static final int TOAST_NO_PLAN = 4;
    static final int TOAST_NO_ENGINEER = 5;
    static final int TOAST_ALL_ON_BUILDINGS = 6;
    static final int TOAST_SOME_ON_BUILDINGS = 7;
    static final int TOAST_FIRST_TICK_OR_CROSS = 8;
    static final int TOAST_NOT_ORDERED = 9;

    /**
     * Griff on or off, and in the picture's pixels: the handle's radius, how far from its middle a finger is on it, its room
     * from the selection's units and how far from its middle the tip of its arrow is; the tap help's reach and what counts as
     * a small control (as for {@link #tap}); how far beside a unit a tap still picks it, with nothing selected that takes
     * orders and with troops; the speed, a second, under which the camera's glide stops; and how far from a finger the draft
     * finds a deposit for an extractor.
     */
    static native void setGriff(boolean on, float radius, float touchRadius, float gap, float arrowReach, float tapReach,
                                float tapSmallSide, float nearReach, float nearReachWithTroops, float glideStop, float depositReach);

    /**
     * Where the app's own pieces lie over the picture ({@link Covers}): `count` boxes in `boxes`, left, top, right and bottom in
     * its pixels. The handle keeps clear of them, and a gesture finds nothing of the game's under them.
     */
    static native void setGriffCovers(int[] boxes, int count);

    /**
     * The handle over the selection as the last frame placed it, in the picture's pixels, into GRIFF_VALUES: x, y, the group's
     * middle x and y, the selection's count, 1 if the group is out of view or under a panel and the handle points at it, else 0;
     * the free map's left, top and right at its middle; and the stroke the program last answered for ({@link #strokeHover}) and
     * what its end is on (Stroke.END_). False when there is no handle.
     */
    static native boolean griff(float[] handle);

    /**
     * Into `said` what the program has said since the last call, three numbers each (SAID_ and two of its own), as many as fit;
     * how many numbers that is. SAID_TAP: a tap's number and what it was (TouchInput.TAP_) with the rail's bits it used
     * shifted by 8; SAID_ROUTE_GIVEN_UP: a route ended on a panel; SAID_TOAST: what the draft says (TOAST_) and its number;
     * SAID_COMMITTED: the draft was ticked, with the rail's bits it used; SAID_PAN: a drag's number ({@link #griffDrag}) that the
     * program made a pan of; SAID_ORDERED: a gesture ordered, the number of its record, which the undo's pill takes back from
     * now on; SAID_UNDONE: the record of this number was taken back, 1 if only in part.
     */
    static native int griffSaid(int[] said);

    /**
     * A first finger came down here: on the map or the minimap the camera's glide stops; FAF's reach for a unit beside a tap is
     * set for the selection, or for Box, which picks units (`box`).
     */
    static native void griffDown(int x, int y, boolean box);

    /**
     * A tap where the finger was and the pointer has rested, or a step of the tap ladder (TouchInput.STEP_), which acts at the
     * ladder's first tap (`baseX`, `baseY`); `rail`: TouchInput's RAIL_ bits that count for it. A STEP_TAP is FAF's click by
     * what it lands on, which the program says, by its number `tap` ({@link #griffSaid}).
     */
    static native void smartTap(int x, int y, int rail, int step, int baseX, int baseY, int tap);

    /** Two fingers' tap: an armed command mode goes, or with none the selection. */
    static native void deselect();

    /** The camera goes to the selection, by a flight (`fly`) or at once. */
    static native void toGroup(boolean fly);

    /**
     * A route of `count` waypoints, x and y pairs in the picture's pixels: FAF's right click at each, the last on what is there;
     * all but the first with Shift, and with RAIL_QUEUE the first too. With RAIL_ATTACK each is the left click of FAF's
     * attack-move mode, armed again where FAF ended it, or the right click where FAF offers no attack-move. With `patrol` each
     * is the left click of FAF's patrol mode where FAF offers it, so armed again.
     */
    static native void route(int[] places, int count, int rail, boolean patrol);

    /**
     * The cursor hovers here for stroke number `stroke`, and the program says what a click would order there ({@link #griff}).
     */
    static native void strokeHover(int x, int y, int stroke);

    /**
     * A finger's drag from here (`rested`: after a rest), as the place wants it: on the map a pan from where the finger is now
     * (`nowX`, `nowY`), or a selection box after a rest or with RAIL_BOX, with RAIL_QUEUE adding to the selection; elsewhere the
     * left button's drag. {@link #griffDragEnd} ends it. A pan the program says by the drag's number `drag` ({@link #SAID_PAN}).
     */
    static native void griffDrag(int x, int y, int nowX, int nowY, boolean rested, int rail, int drag);

    /** The drag ends here; a pan's camera glides on at this velocity in the picture's pixels a second. */
    static native void griffDragEnd(int x, int y, float velocityX, float velocityY);

    /**
     * A finger rested here and lifted: on a panel FAF's right click, with the tap help, and with Shift for RAIL_QUEUE; on the map
     * nothing. With RAIL_BOX there was no rest, and on the map it is Box's tap, which says the rail's bits it used (SAID_TAP).
     */
    static native void restTap(int x, int y, int rail);

    /**
     * The camera follows fingers (TouchInput.Mouse#pinch): `start` keeps what lies here under them; then two fingers
     * `distance` apart zoom it, one (0) moves it. In the picture's pixels.
     */
    static native void pinch(boolean start, int x, int y, int distance);

    /** The fingers left the camera, which glides on at this velocity in the picture's pixels a second. */
    static native void pinchEnd(float velocityX, float velocityY);

    /** The rail's Plus held or let go: Shift's key, in the order of the clicks. */
    static native void holdShift(boolean down);

    // --- Griff's draft (buildstage/griff-v3-spec.md, T2; GriffAndroid.cpp, include/GriffDraft.h) --------------------------

    /**
     * The draft as the last frame drew it, into `values` ({@link Draft#VALUES} of them, {@link Draft#update} reads them), as many
     * as fit: whether it is open, and its plans, line and aim on the picture. How many numbers.
     */
    static native int griffDraft(float[] values);

    /**
     * A tap in the draft: on FAF's UI the UI's click; on the map it removes the plan numbered `plan` the finger was on (0: none),
     * else it places one of the draft's building there, an extractor on the free deposit nearest within reach.
     */
    static native void draftTap(int x, int y, int plan);

    /** The line from the newest plan to here: shown, added or given up by `phase` (TouchInput.DRAFT_). */
    static native void draftLine(int phase, int x, int y);

    /** The whole draft follows a finger from where it came down to here, by whole world units: by `phase`. */
    static native void draftMove(int phase, int fromX, int fromY, int x, int y);

    /** A finger that rests here in the draft: the plan its lift places shown, placed (or on FAF's UI its right click), gone. */
    static native void draftAim(int phase, int x, int y);

    /** Box from one corner to the other, with an extractor picked: each free deposit in it gets a plan. */
    static native void draftBox(int x0, int y0, int x1, int y1);

    /** The tick: FAF's build mode and a click per plan, all appended with `plus` ({@link #SAID_COMMITTED} when done). */
    static native void draftCommit(boolean plus);

    /** The cross, or two fingers' tap in the draft: the draft goes, with nothing ordered. */
    static native void draftDrop();

    // --- What Griff's gestures order: the pins and the undo (buildstage/griff-v3-spec.md, T3.1 and T3.2) -----------------

    /** {@link #griffOrders}: the numbers before the pins, and each pin's. */
    static final int ORDERS_UNIT_VIEW_TOP = 0;
    static final int ORDERS_PANEL_LEFT = 1;
    static final int ORDERS_PANEL_RIGHT = 2;
    static final int ORDERS_PINS = 3;
    static final int ORDERS_HEAD = 4;
    static final int ORDERS_PIN = 4;
    /** Room for the head and the pins, of which there are 64 at most (include/GriffPins.h). */
    static final int ORDERS_VALUES = ORDERS_HEAD + 64 * ORDERS_PIN;

    /**
     * What the last frame showed of the orders, into `values`, as many as fit; how many numbers. Where the undo's pill stands, in
     * the picture's pixels (FAF's unit view's top, its orders panel's left and right; below 0 not known); how many pins; then
     * each pin: its tip on the picture, its kind (GriffLayer's colours) and how much of it shows, 0 to 1.
     */
    static native int griffOrders(float[] values);

    /** The undo's pill was tapped for record `record`: its orders taken back, once no clicks of the tick's or an undo's run. */
    static native void griffUndo(int record);

    /**
     * Whether the program's clicks run, the tick's or the undo's: no finger starts a gesture meanwhile, and the rail and the pill
     * take no tap, as anything of theirs would act between those clicks. Clicks that have waited for a frame past their
     * patience (2 s) count as over, and the next of the program's tasks ends them, a finger's down too.
     */
    static native boolean griffClicking();
}
