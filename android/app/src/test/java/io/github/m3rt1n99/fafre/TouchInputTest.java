package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.util.ArrayList;
import java.util.List;

import org.junit.Test;

/** TouchInput's gestures, as what reaches the game. */
public class TouchInputTest {
    private static final float SLOP = 10;

    /** Writes down what the game gets, and runs the timer when a test says time has passed. */
    private static final class Recorder implements TouchInput.Mouse {
        final List<String> events = new ArrayList<>();
        long timer = -1;
        boolean placing;
        /** Whether the pointer is to leave, by the time this has passed. */
        boolean leaving;
        /** What had gone to the game by the last {@link #stay}. */
        int sentBeforeStay = -1;
        int tapped;

        @Override
        public void move(float x, float y) {
            events.add("move " + (int) x + " " + (int) y);
        }

        @Override
        public void button(int button, boolean down, float x, float y) {
            events.add(name(button) + (down ? " down " : " up ") + (int) x + " " + (int) y);
        }

        @Override
        public void tap(float x, float y) {
            events.add("left down " + (int) x + " " + (int) y);
            events.add("left up " + (int) x + " " + (int) y);
            tapped++;
        }

        @Override
        public void leaveSoon() {
            leaving = true;
        }

        @Override
        public void stay() {
            leaving = false;
            sentBeforeStay = events.size();
        }

        @Override
        public void tapNearGhost(float fingerX, float fingerY, float ghostX, float ghostY) {
            events.add("tap " + (int) fingerX + " " + (int) fingerY + " near ghost " + (int) ghostX + " " + (int) ghostY);
        }

        @Override
        public void startLine(float x, float y) {
            events.add("line from " + (int) x + " " + (int) y);
        }

        @Override
        public void cancelLine() {
            events.add("line given up");
        }

        @Override
        public void wheel(int delta, float x, float y) {
            events.add("wheel " + delta + " " + (int) x + " " + (int) y);
        }

        @Override
        public void held() {
            events.add("held");
        }

        @Override
        public boolean placing() {
            return placing;
        }

        @Override
        public void deleteOrder(float x, float y) {
            events.add("delete " + (int) x + " " + (int) y);
        }

        @Override
        public void startTimer(long milliseconds) {
            timer = milliseconds;
        }

        @Override
        public void stopTimer() {
            timer = -1;
        }

        /** Griff: the handle's place, and the rail's buttons that count. */
        float griffX = -1000;
        float griffY = -1000;
        float griffReach = 20;
        int rail;
        /** Griff: the first fingers' places, the program's numbers of the taps sent, the ring. */
        final List<String> downs = new ArrayList<>();
        int lastTap;
        boolean ringShown;
        float ringX;
        float ringY;
        /** Griff: what the screen last got of the stroke, and where the cursor hovered for it. */
        int shown = -1;
        int shownMode = -1;
        int shownWaypoints;
        float shownEndX;
        final List<String> hovers = new ArrayList<>();

        @Override
        public void griffDown(float x, float y, boolean box) {
            downs.add((int) x + " " + (int) y + (box ? " box" : ""));
        }

        @Override
        public void smartTap(float x, float y, int railNow, int step, float baseX, float baseY, int tap) {
            lastTap = tap;
            events.add("smart tap " + (int) x + " " + (int) y + " rail " + railNow
                       + (step == TouchInput.STEP_TAP ? "" : " " + stepName(step) + " at " + (int) baseX + " " + (int) baseY));
        }

        @Override
        public void ring(boolean shownNow, float x, float y) {
            ringShown = shownNow;
            ringX = x;
            ringY = y;
        }

        @Override
        public void deselect() {
            events.add("deselect");
        }

        @Override
        public boolean onGriff(float x, float y) {
            return Math.hypot(x - griffX, y - griffY) <= griffReach;
        }

        @Override
        public void griffTap() {
            events.add("griff tap");
        }

        @Override
        public void route(float[] xs, float[] ys, int count, int railNow, boolean patrol) {
            StringBuilder route = new StringBuilder(patrol ? "patrol" : "route");
            for (int at = 0; at < count; ++at) {
                route.append(' ').append(Math.round(xs[at])).append(' ').append(Math.round(ys[at])).append(at + 1 < count ? "," : "");
            }
            events.add(route + " rail " + railNow);
        }

        @Override
        public void strokeShown(float[] xs, float[] ys, int count, int mode, float[] waypointXs, float[] waypointYs, int waypoints) {
            shown = count;
            shownMode = mode;
            shownWaypoints = waypoints;
            shownEndX = count > 0 ? xs[count - 1] : 0;
        }

        @Override
        public void strokeHover(float x, float y) {
            hovers.add((int) x + " " + (int) y);
        }

        @Override
        public void strokeGivenUp() {
            events.add("given up");
        }

        @Override
        public int rail() {
            return rail;
        }

        @Override
        public void railUsed(int used) {
            events.add("rail used " + used);
        }

        /** Griff: the number of the last drag sent, by which the program says it panned. */
        int lastDrag;

        @Override
        public void griffDrag(float x, float y, float nowX, float nowY, boolean rested, int railNow, int drag) {
            lastDrag = drag;
            events.add("griff drag " + (int) x + " " + (int) y + " now " + (int) nowX + " " + (int) nowY + (rested ? " rested" : "")
                       + " rail " + railNow);
        }

        @Override
        public void griffDragEnd(float x, float y, float velocityX, float velocityY) {
            events.add("griff drag end " + (int) x + " " + (int) y + glide(velocityX, velocityY));
        }

        @Override
        public void griffDragGiveUp(float x, float y) {
            events.add("griff drag given up " + (int) x + " " + (int) y);
        }

        @Override
        public void restTap(float x, float y, int railNow) {
            events.add("rest tap " + (int) x + " " + (int) y + " rail " + railNow);
        }

        @Override
        public void griffRest(float x, float y) {
            events.add("griff rest " + (int) x + " " + (int) y);
        }

        @Override
        public void griffRestGone() {
            events.add("rest gone");
        }

        @Override
        public void pinch(boolean start, float x, float y, float distance) {
            events.add((start ? "pinch start " : "pinch ") + (int) x + " " + (int) y + " " + Math.round(distance));
        }

        @Override
        public void pinchEnd(float velocityX, float velocityY) {
            events.add("pinch end" + glide(velocityX, velocityY));
        }

        /** Griff's draft: open or not, its building on a deposit, its plans as {number, x, y, on a deposit}, the newest last. */
        boolean drafting;
        boolean picksDeposit;
        final List<int[]> draftPlans = new ArrayList<>();
        /** A finger this far from a plan's middle each way is on it. */
        int draftReach = 20;

        @Override
        public boolean drafting() {
            return drafting;
        }

        @Override
        public int draftPlan(float x, float y) {
            for (int at = draftPlans.size() - 1; at >= 0; --at) {
                int[] plan = draftPlans.get(at);
                if (Math.abs(x - plan[1]) <= draftReach && Math.abs(y - plan[2]) <= draftReach) {
                    return plan[0];
                }
            }
            return 0;
        }

        @Override
        public boolean draftNewest(int plan) {
            return !draftPlans.isEmpty() && draftPlans.get(draftPlans.size() - 1)[0] == plan;
        }

        @Override
        public boolean draftOnDeposit(int plan) {
            for (int[] known : draftPlans) {
                if (known[0] == plan) {
                    return known[3] != 0;
                }
            }
            return false;
        }

        @Override
        public boolean draftPicksDeposit() {
            return picksDeposit;
        }

        @Override
        public void draftTap(float x, float y, int plan) {
            events.add("draft tap " + (int) x + " " + (int) y + (plan != 0 ? " on " + plan : ""));
        }

        @Override
        public void draftLine(int phase, float x, float y) {
            events.add("draft line " + phase(phase, x, y));
        }

        @Override
        public void draftMove(int phase, float fromX, float fromY, float x, float y) {
            events.add("draft move " + (phase == TouchInput.DRAFT_ABORT ? "abort" : phase(phase, fromX, fromY) + " to " + (int) x + " " + (int) y));
        }

        @Override
        public void draftAim(int phase, float x, float y) {
            events.add("draft aim " + phase(phase, x, y));
        }

        @Override
        public void draftBox(int phase, float x0, float y0, float x1, float y1) {
            events.add("draft box " + (phase == TouchInput.DRAFT_ABORT ? "abort" : phase(phase, x0, y0) + " to " + (int) x1 + " " + (int) y1));
        }

        @Override
        public void draftDrop() {
            events.add("draft drop");
        }

        /** Griff: the program's clicks run, the tick's or the undo's, and how many runs of them have begun. */
        boolean clicking;
        int clickRuns;

        @Override
        public boolean clicking() {
            return clicking;
        }

        @Override
        public int clickRuns() {
            return clickRuns;
        }

        /** A run of the program's clicks begins, as the tick's or the undo's does (StartClicks). */
        void clicksBegin() {
            clicking = true;
            ++clickRuns;
        }

        /** A draft gesture's phase, and where it is unless it is given up. */
        private static String phase(int phase, float x, float y) {
            switch (phase) {
                case TouchInput.DRAFT_SHOW:
                    return "show " + (int) x + " " + (int) y;
                case TouchInput.DRAFT_END:
                    return "end " + (int) x + " " + (int) y;
                default:
                    return "abort";
            }
        }

        private static String glide(float velocityX, float velocityY) {
            return velocityX == 0 && velocityY == 0 ? "" : " glide " + Math.round(velocityX) + " " + Math.round(velocityY);
        }

        private static String stepName(int step) {
            switch (step) {
                case TouchInput.STEP_AGAIN:
                    return "again";
                case TouchInput.STEP_ALL_OF_TYPE:
                    return "all of type";
                case TouchInput.STEP_ATTACK_MOVE:
                    return "attack move";
                case TouchInput.STEP_ALL_MOBILE:
                    return "all mobile";
                case TouchInput.STEP_TYPE_IN_VIEW:
                    return "type in view";
                case TouchInput.STEP_GROUP_AGAIN:
                    return "group again";
                default:
                    return "step " + step;
            }
        }

        private static String name(int button) {
            return button == Game.BUTTON_LEFT ? "left" : button == Game.BUTTON_RIGHT ? "right" : "middle";
        }

        int wheelTotal() {
            int total = 0;
            for (String event : events) {
                if (event.startsWith("wheel ")) {
                    total += Integer.parseInt(event.split(" ")[1]);
                }
            }
            return total;
        }
    }

    private final Recorder mouse = new Recorder();
    private final TouchInput touch = new TouchInput(mouse, SLOP);

    private int fingers;
    /** The time of the fingers' events: a down's or an up's, and a millisecond on for each move without its own. */
    private long clock;

    private void down(int id, float x, float y, long time) {
        clock = time;
        touch.down(id, x, y, time, fingers == 0);
        ++fingers;
    }

    private void up(int id, long time) {
        clock = time;
        --fingers;
        touch.up(id, time, fingers == 0);
    }

    private void timerRuns() {
        assertTrue("a timer is set", mouse.timer >= 0);
        mouse.timer = -1;
        touch.timer();
    }

    private void moveFirst(float x, float y) {
        moveFirst(x, y, clock + 1);
    }

    private void moveFirst(float x, float y, long time) {
        clock = time;
        touch.move(0, x, y);
        touch.moved(time);
    }

    private void moveBoth(float x0, float y0, float x1, float y1) {
        clock += 1;
        touch.move(0, x0, y0);
        touch.move(1, x1, y1);
        touch.moved(clock);
    }

    private void expect(String... events) {
        assertEquals(List.of(events), mouse.events);
    }

    // --- Griff (buildstage/touch-concept-v2.md, the mockup buildstage/touch-preview) ---------------------------------
    // A mm is 10 pixels here: Griff's slop 25, the ladder's reach 60, two fingers' stillness 15.

    private void griff() {
        touch.setGriff(true);
        touch.setMillimetre(10);
    }

    /** A stroke from (x0, y0) to (x1, y1) in steps of 5 pixels, as a finger's moves a millisecond apart. */
    private void strokeFirst(float x0, float y0, float x1, float y1) {
        int steps = (int) Math.ceil(Math.hypot(x1 - x0, y1 - y0) / 5);
        for (int at = 1; at <= steps; ++at) {
            moveFirst(x0 + (x1 - x0) * at / steps, y0 + (y1 - y0) * at / steps);
        }
    }

    /** The places of a route as the recorder writes it: "route X Y, X Y rail R". */
    private static List<float[]> places(String route) {
        List<float[]> places = new ArrayList<>();
        String[] pairs = route.substring(route.indexOf(' ') + 1, route.lastIndexOf(" rail ")).split(", ");
        for (String pair : pairs) {
            String[] numbers = pair.split(" ");
            places.add(new float[] {Float.parseFloat(numbers[0]), Float.parseFloat(numbers[1])});
        }
        return places;
    }

    /** A Griff tap down at `down` and up when the cursor has settled, its click; the program says what it was (`kind`, 0 not yet). */
    private void tap(float x, float y, long down, int kind) {
        down(0, x, y, down);
        up(0, down + TouchInput.SETTLE_MILLISECONDS);
        if (kind != 0) {
            touch.tapAnswered(mouse.lastTap, kind);
        }
    }

    @Test
    public void griffAFingerThatMovesDragsWhatIsThere() {
        griff();
        down(0, 100, 50, 1000);
        moveFirst(150, 50);
        up(0, 1200);
        expect("move 100 50", "griff drag 100 50 now 150 50 rail 0", "move 150 50", "griff drag end 150 50");
    }

    @Test
    public void griffTheSlopIsTwoAndAHalfMillimetres() {
        // The system's slop, 10 here, would make these 2 mm a drag.
        griff();
        down(0, 100, 50, 1000);
        moveFirst(120, 50);
        up(0, 1000 + TouchInput.SETTLE_MILLISECONDS);
        expect("move 100 50", "smart tap 100 50 rail 0");
        mouse.events.clear();
        down(0, 100, 50, 2000);
        moveFirst(127, 50);
        assertEquals("griff drag 100 50 now 127 50 rail 0", mouse.events.get(1));
    }

    @Test
    public void griffAPanStartsWhereTheFingerPassedTheSlop() {
        // So the map does not jump by the slop: what is under the finger stays under it from there on.
        griff();
        down(0, 100, 50, 1000);
        moveFirst(120, 50);
        moveFirst(140, 50);
        moveFirst(160, 50);
        expect("move 100 50", "griff drag 100 50 now 140 50 rail 0", "move 140 50", "move 160 50");
    }

    @Test
    public void griffAPanThatLiftsMovingGlidesOn() {
        // 30 pixels each 10 ms: 3000 a second, of which each move takes 0.4 into its velocity, and the glide 0.9 of that.
        griff();
        down(0, 100, 50, 1000);
        for (int at = 1; at <= 6; ++at) {
            moveFirst(100 + 30 * at, 50, 1000 + 10 * at);
        }
        up(0, 1080);
        String end = mouse.events.get(mouse.events.size() - 1);
        assertTrue(end, end.startsWith("griff drag end 280 50 glide "));
        int velocity = Integer.parseInt(end.split(" ")[6]);
        // The drag began at the first move; five more: 3000 * (1 - 0.6^5) * 0.9.
        assertEquals(2490, velocity, 2);
        assertTrue(end, end.endsWith(" 0"));
    }

    @Test
    public void griffAPanThatStoppedBeforeTheLiftStops() {
        griff();
        down(0, 100, 50, 1000);
        for (int at = 1; at <= 6; ++at) {
            moveFirst(100 + 30 * at, 50, 1000 + 10 * at);
        }
        // 70 ms after the last move.
        up(0, 1130);
        assertEquals("griff drag end 280 50", mouse.events.get(mouse.events.size() - 1));
    }

    @Test
    public void griffAPanIsNoActionForTheRail() {
        griff();
        mouse.rail = TouchInput.RAIL_QUEUE;
        down(0, 100, 50, 1000);
        moveFirst(150, 50);
        up(0, 1200);
        expect("move 100 50", "griff drag 100 50 now 150 50 rail 2", "move 150 50", "griff drag end 150 50");
    }

    @Test
    public void griffARestThenADragIsABoxThatDoesNotGlide() {
        griff();
        down(0, 100, 50, 1000);
        assertEquals(TouchInput.REST_MILLISECONDS, mouse.timer);
        timerRuns();
        assertTrue("a ring shows the rest", mouse.ringShown);
        moveFirst(200, 100, 1400);
        assertFalse("the box goes on without it", mouse.ringShown);
        up(0, 1410);
        expect("move 100 50", "held", "griff rest 100 50", "griff drag 100 50 now 200 100 rested rail 0", "move 200 100",
               "griff drag end 200 100");
    }

    @Test
    public void griffARestedDragTheSystemTakesIsGivenUpNotEnded() {
        // FAF's formation is ordered as its right button goes up: a drag no finger lifted gives it up, as a route is given up.
        griff();
        down(0, 100, 50, 1000);
        timerRuns();
        moveFirst(200, 100, 1400);
        touch.formation(mouse.lastDrag);
        touch.cancel();
        expect("move 100 50", "held", "griff rest 100 50", "griff drag 100 50 now 200 100 rested rail 0", "move 200 100",
               "griff drag given up 200 100");
        // What is left of the finger gives nothing.
        moveFirst(260, 100, 1500);
        up(0, 1600);
        expect("move 100 50", "held", "griff rest 100 50", "griff drag 100 50 now 200 100 rested rail 0", "move 200 100",
               "griff drag given up 200 100");
    }

    @Test
    public void griffANewFirstFingerGivesARestedDragUp() {
        // A first finger while a gesture is live: the one before never lifted, and orders nothing.
        griff();
        down(0, 100, 50, 1000);
        timerRuns();
        moveFirst(200, 100, 1400);
        touch.formation(mouse.lastDrag);
        mouse.events.clear();
        touch.down(5, 400, 300, 1500, true);
        assertEquals("griff drag given up 200 100", mouse.events.get(0));
        for (String event : mouse.events) {
            assertFalse(mouse.events.toString(), event.startsWith("griff drag end"));
        }
    }

    @Test
    public void griffASecondFingerGivesFafsFormationUp() {
        griff();
        down(0, 100, 50, 1000);
        timerRuns();
        moveFirst(200, 100, 1400);
        // The program says that it made FAF's formation of the drag (Game.SAID_FORMATION).
        touch.formation(mouse.lastDrag);
        mouse.events.clear();
        down(1, 400, 100, 1500);
        expect("griff drag given up 200 100", "given up");
        // Both fingers rest until they lift: no pinch, no order.
        moveBoth(260, 100, 450, 100);
        up(1, 1600);
        up(0, 1700);
        expect("griff drag given up 200 100", "given up");
    }

    @Test
    public void griffASecondFingerLeavesABoxAlone() {
        // A rested drag the program made a box of, or one it has not said is a formation yet: the box goes on, and its lift ends it.
        griff();
        down(0, 100, 50, 1000);
        timerRuns();
        moveFirst(200, 100, 1400);
        mouse.events.clear();
        down(1, 400, 100, 1500);
        moveBoth(220, 100, 420, 100);
        up(1, 1600);
        up(0, 1700);
        for (String event : mouse.events) {
            assertFalse(mouse.events.toString(), event.startsWith("griff drag given up") || event.startsWith("pinch"));
        }
        assertTrue(mouse.events.toString(), mouse.events.contains("griff drag end 220 100"));
    }

    @Test
    public void griffTheFormationsWordIsForOneDragAlone() {
        griff();
        down(0, 100, 50, 1000);
        timerRuns();
        moveFirst(200, 100, 1400);
        int earlier = mouse.lastDrag;
        up(0, 1500);
        down(0, 300, 50, 2000);
        timerRuns();
        moveFirst(400, 100, 2400);
        touch.formation(earlier);
        mouse.events.clear();
        down(1, 600, 100, 2500);
        assertTrue(mouse.events.toString(), mouse.events.isEmpty());
    }

    @Test
    public void griffARestAndALiftAreARestTapWhereTheFingerRested() {
        griff();
        mouse.rail = TouchInput.RAIL_QUEUE;
        down(0, 100, 50, 1000);
        timerRuns();
        moveFirst(103, 50);
        assertTrue(mouse.ringShown);
        assertEquals("the ring follows the finger", 103, mouse.ringX, 0);
        up(0, 1600);
        assertFalse(mouse.ringShown);
        expect("move 100 50", "held", "griff rest 100 50", "rest tap 100 50 rail 2", "rail used 2");
    }

    @Test
    public void griffARestTellsTheProgramWhereTheFingerCameDownAsItsRingShows() {
        // The program marks a waypoint there or makes the rest the right button's, for the drag or the lift that follows.
        griff();
        down(0, 100, 50, 1000);
        moveFirst(104, 52);
        expect("move 100 50");
        timerRuns();
        assertTrue(mouse.ringShown);
        expect("move 100 50", "held", "griff rest 100 50");
    }

    @Test
    public void griffARestTheSystemTookIsGoneForTheProgram() {
        griff();
        down(0, 100, 50, 1000);
        timerRuns();
        touch.cancel();
        assertFalse(mouse.ringShown);
        expect("move 100 50", "held", "griff rest 100 50", "rest gone");
        // Once: the finger that the system took lifts with nothing more, and the next gesture's tap has no rest of its own.
        up(0, 1500);
        mouse.events.clear();
        down(0, 300, 50, 3000);
        up(0, 3000 + TouchInput.SETTLE_MILLISECONDS);
        expect("move 300 50", "smart tap 300 50 rail 0");
    }

    @Test
    public void griffARestsDragOrLiftTakesItsRestWithIt() {
        // The program reads what the rest found with the drag or the lift: no word that it went.
        griff();
        down(0, 100, 50, 1000);
        timerRuns();
        moveFirst(200, 100, 1400);
        up(0, 1500);
        down(0, 300, 50, 3000);
        timerRuns();
        up(0, 3600);
        for (String event : mouse.events) {
            assertFalse(event, event.equals("rest gone"));
        }
    }

    @Test
    public void griffWithBoxARestIsNoneAndItsLiftIsLeftToTheProgram() {
        // The mockup's rest is off with Box: no ring, nothing to feel; the lift is Box's tap on the map, which says what it used.
        griff();
        mouse.rail = TouchInput.RAIL_BOX;
        down(0, 100, 50, 1000);
        timerRuns();
        assertFalse(mouse.ringShown);
        up(0, 1600);
        expect("move 100 50", "rest tap 100 50 rail 1");
        assertEquals(List.of("100 50 box"), mouse.downs);
    }

    @Test
    public void griffAGroupStoredByARestWithBoxBuzzesAsItIsStored() {
        // The rest with Box rang for nothing, but on a slot its lift is FAF's right press, which stores the group: the store is
        // felt then, once (the mockup's one buzz a group).
        griff();
        mouse.rail = TouchInput.RAIL_BOX;
        down(0, 100, 50, 1000);
        timerRuns();
        up(0, 1600);
        expect("move 100 50", "rest tap 100 50 rail 1");
        assertTrue("the store buzzes", touch.storeBuzzes());
        assertTrue("and one no rest made after it", touch.storeBuzzes());
    }

    @Test
    public void griffAGroupStoredByARestThatRangBuzzesNoMore() {
        griff();
        assertTrue("a store no rest made, a mouse's", touch.storeBuzzes());
        down(0, 100, 50, 1000);
        timerRuns();
        up(0, 1600);
        expect("move 100 50", "held", "griff rest 100 50", "rest tap 100 50 rail 0");
        assertFalse("its ring buzzed", touch.storeBuzzes());
        assertTrue("for one store: a mouse's after it buzzes", touch.storeBuzzes());
    }

    @Test
    public void griffARestThenAQuickSecondFingerIsNoTwoFingerTap() {
        // Both fingers count, from the first one down.
        griff();
        down(0, 100, 50, 1000);
        timerRuns();
        down(1, 200, 50, 1400);
        assertFalse(mouse.ringShown);
        up(1, 1450);
        up(0, 1460);
        expect("move 100 50", "held", "griff rest 100 50", "rest gone", "move 200 50");
    }

    @Test
    public void griffBoxOnTheRailMakesADragABoxAtOnce() {
        griff();
        mouse.rail = TouchInput.RAIL_BOX;
        down(0, 100, 50, 1000);
        moveFirst(200, 100);
        up(0, 1200);
        expect("move 100 50", "griff drag 100 50 now 200 100 rail 1", "move 200 100", "rail used 1", "griff drag end 200 100");
    }

    @Test
    public void griffQueueOnTheRailMakesABoxAdd() {
        griff();
        mouse.rail = TouchInput.RAIL_QUEUE;
        down(0, 100, 50, 1000);
        timerRuns();
        moveFirst(200, 100);
        up(0, 1600);
        expect("move 100 50", "held", "griff rest 100 50", "griff drag 100 50 now 200 100 rested rail 2", "move 200 100", "rail used 2",
               "griff drag end 200 100");
    }

    @Test
    public void griffATapIsFafsClickByWhatItLandsOnAndUsesTheRail() {
        griff();
        mouse.rail = TouchInput.RAIL_QUEUE;
        down(0, 100, 50, 1000);
        up(0, 1000 + TouchInput.SETTLE_MILLISECONDS);
        expect("move 100 50", "smart tap 100 50 rail 2", "rail used 2");
        assertEquals(List.of("100 50"), mouse.downs);
    }

    @Test
    public void griffBoxsTapLeavesTheRailToTheProgram() {
        // Box's tap on nothing leaves Box lit: the program says what the tap used.
        griff();
        mouse.rail = TouchInput.RAIL_BOX | TouchInput.RAIL_QUEUE;
        down(0, 100, 50, 1000);
        up(0, 1000 + TouchInput.SETTLE_MILLISECONDS);
        expect("move 100 50", "smart tap 100 50 rail 3");
    }

    @Test
    public void griffAQuickTapWaitsForTheCursorToSettle() {
        griff();
        down(0, 100, 50, 1000);
        up(0, 1040);
        expect("move 100 50");
        assertEquals(TouchInput.SETTLE_MILLISECONDS - 40, mouse.timer);
        timerRuns();
        expect("move 100 50", "smart tap 100 50 rail 0");
    }

    @Test
    public void griffTheLadderOnAUnitTakesItsTypeOnTheScreenThenOnTheMap() {
        // The second tap 4 mm beside the first, wherever it lands: the ladder counts by what the first tap was.
        griff();
        tap(100, 50, 1000, TouchInput.TAP_OWN);
        tap(140, 50, 1300, 0);
        tap(150, 60, 1600, 0);
        tap(150, 60, 1900, 0);
        expect("move 100 50", "smart tap 100 50 rail 0", "move 140 50", "smart tap 140 50 rail 0 type in view at 100 50", "move 150 60",
               "smart tap 150 60 rail 0 all of type at 100 50", "move 150 60", "smart tap 150 60 rail 0 all of type at 100 50");
    }

    @Test
    public void griffTheLadderOnGroundMakesTheMoveAnAttackMove() {
        griff();
        tap(100, 50, 1000, TouchInput.TAP_GROUND);
        tap(130, 60, 1300, 0);
        // The attack-move is the ladder's top: the next tap is a tap again.
        tap(130, 60, 1600, 0);
        expect("move 100 50", "smart tap 100 50 rail 0", "move 130 60", "smart tap 130 60 rail 0 attack move at 100 50",
               "move 130 60", "smart tap 130 60 rail 0");
    }

    @Test
    public void griffTheLadderOnAGroupsIconSelectsItAgainWithTheCamera() {
        // As a control group's double click: the second tap near the first, wherever it lands, takes the first tap's group.
        griff();
        tap(100, 50, 1000, TouchInput.TAP_GROUP);
        tap(120, 55, 1300, 0);
        // The camera to the group is the ladder's top: the next tap is a tap again.
        tap(120, 55, 1600, 0);
        expect("move 100 50", "smart tap 100 50 rail 0", "move 120 55", "smart tap 120 55 rail 0 group again at 100 50",
               "move 120 55", "smart tap 120 55 rail 0");
    }

    @Test
    public void griffTheLadderOnNothingTakesAllOneOwnsOnTheScreen() {
        griff();
        tap(100, 50, 1000, TouchInput.TAP_EMPTY);
        tap(100, 50, 1300, 0);
        expect("move 100 50", "smart tap 100 50 rail 0", "move 100 50", "smart tap 100 50 rail 0 all mobile at 100 50");
    }

    @Test
    public void griffTheLadderWantsItSoonNearWithoutTheRailAndKnown() {
        griff();
        // 350 ms from lift to lift: 360 is too late.
        tap(100, 50, 1000, TouchInput.TAP_OWN);
        tap(100, 50, 1360, TouchInput.TAP_OWN);
        // 6 mm: 61 pixels is too far.
        tap(161, 50, 1600, TouchInput.TAP_GROUND);
        // Plus keeps a tap off the ladder.
        mouse.rail = TouchInput.RAIL_QUEUE;
        tap(161, 50, 1800, 0);
        mouse.rail = 0;
        // Before the program has said what the last tap was, a tap is decided by its place.
        tap(161, 50, 2000, 0);
        tap(161, 50, 2200, 0);
        // An attack, or anything else that is no rung: neither.
        tap(161, 50, 2400, TouchInput.TAP_OTHER);
        tap(161, 50, 2600, 0);
        for (String event : mouse.events) {
            assertFalse(event, event.contains(" at "));
        }
    }

    @Test
    public void griffTwoTapsOnTheUiClickTheSamePixel() {
        // The game's double click on its UI wants the same place, within the system's reach and time; farther is a tap.
        griff();
        tap(100, 50, 1000, TouchInput.TAP_UI);
        tap(115, 58, 1200, 0);
        tap(100, 50, 2000, TouchInput.TAP_UI);
        tap(125, 50, 2200, 0);
        expect("move 100 50", "smart tap 100 50 rail 0", "move 115 58", "smart tap 115 58 rail 0 again at 100 50", "move 100 50",
               "smart tap 100 50 rail 0", "move 125 50", "smart tap 125 50 rail 0");
    }

    @Test
    public void griffADoubleTapOnAUnitIsNotTakenByTheHandleItsFirstTapRaised() {
        griff();
        tap(100, 50, 1000, 0);
        // The first tap selected the unit, and the handle came up over it; the program has not said so yet.
        mouse.griffX = 100;
        mouse.griffY = 50;
        down(0, 102, 50, 1200);
        touch.tapAnswered(mouse.lastTap, TouchInput.TAP_OWN);
        up(0, 1200 + TouchInput.SETTLE_MILLISECONDS);
        expect("move 100 50", "smart tap 100 50 rail 0", "move 102 50", "smart tap 102 50 rail 0 type in view at 100 50");
    }

    @Test
    public void griffTheHandleTakesADragAfterTheThirdTap() {
        griff();
        tap(100, 50, 1000, TouchInput.TAP_OWN);
        // The first tap selected the unit and the handle came up over it: the ladder keeps the handle away until its top.
        mouse.griffX = 100;
        mouse.griffY = 50;
        tap(100, 50, 1300, 0);
        tap(100, 50, 1600, 0);
        mouse.events.clear();
        down(0, 100, 50, 1800);
        strokeFirst(100, 50, 400, 50);
        up(0, 2300);
        expect("route 250 50, 400 50 rail 0");
    }

    @Test
    public void griffAFingerTheLadderLetPastTheHandleIsNeverAPlainTapThere() {
        // A finger 5 mm from a tap on a unit, on the handle that tap raised, comes down 300 ms after the tap's lift and lifts
        // 390 ms after it, too late to climb. The handle left it to the ladder at its down, as the ladder might climb, so its
        // lift is the ladder's rung or the handle's tap: never FAF's click at the handle's place, which with troops is a move.
        griff();
        down(0, 100, 50, 1000);
        up(0, 1080);
        timerRuns();
        touch.tapAnswered(mouse.lastTap, TouchInput.TAP_OWN);
        mouse.griffX = 160;
        mouse.griffY = 50;
        mouse.events.clear();
        down(0, 150, 50, 1380);
        up(0, 1470);
        if (mouse.timer >= 0) {
            timerRuns();
        }
        String events = mouse.events.toString();
        assertFalse(events, mouse.events.contains("smart tap 150 50 rail 0"));
        assertTrue(events, mouse.events.contains("griff tap") || mouse.events.contains("smart tap 150 50 rail 0 type in view at 100 50"));
    }

    @Test
    public void griffAFingerOnTheHandleBeforeTheProgramAnsweredIsNeverAPlainTapThere() {
        // The handle leaves a finger to the ladder while the program has not said what the last tap was, as the ladder might
        // climb; the answer comes only after the finger's lift, as from a slow frame. The lift cannot climb, and is the
        // handle's tap, not FAF's click at the handle's place.
        griff();
        down(0, 100, 50, 1000);
        up(0, 1080);
        timerRuns();
        int first = mouse.lastTap;
        mouse.griffX = 160;
        mouse.griffY = 50;
        mouse.events.clear();
        down(0, 150, 50, 1200);
        up(0, 1260);
        if (mouse.timer >= 0) {
            timerRuns();
        }
        touch.tapAnswered(first, TouchInput.TAP_OWN);
        if (mouse.timer >= 0) {
            timerRuns();
        }
        String events = mouse.events.toString();
        assertFalse(events, mouse.events.contains("smart tap 150 50 rail 0"));
        assertTrue(events, mouse.events.contains("griff tap") || mouse.events.contains("smart tap 150 50 rail 0 type in view at 100 50"));
    }

    @Test
    public void griffWithPlusTheHandleTakesAStrokeRightAfterATap() {
        // The ladder never climbs while Plus or Attack count, at the down as at the lift: a stroke from the handle 200 ms after
        // a tap on a unit, with Plus held, is the handle's, a queued route, not a pan of a finger left to the ladder.
        griff();
        tap(100, 50, 1000, TouchInput.TAP_OWN);
        mouse.griffX = 160;
        mouse.griffY = 50;
        mouse.rail = TouchInput.RAIL_QUEUE;
        mouse.events.clear();
        down(0, 150, 50, 1320);
        strokeFirst(150, 50, 450, 50);
        up(0, 1800);
        assertFalse(mouse.events.toString(), mouse.events.isEmpty());
        assertTrue(mouse.events.toString(), mouse.events.get(0).startsWith("route ") && mouse.events.get(0).endsWith(" rail 2"));
    }

    @Test
    public void griffTheHandlesTapOfAFingerTheLadderLetPassUsesNoRailAndStartsNoLadder() {
        // The handle left the finger to the ladder; Plus, pressed before the lift, keeps the tap off the ladder: it is the
        // handle's tap, as a stroke's is, and Plus stays for the next action.
        griff();
        tap(100, 50, 1000, TouchInput.TAP_OWN);
        mouse.griffX = 160;
        mouse.griffY = 50;
        mouse.events.clear();
        down(0, 150, 50, 1300);
        mouse.rail = TouchInput.RAIL_QUEUE;
        up(0, 1300 + TouchInput.SETTLE_MILLISECONDS);
        expect("move 150 50", "griff tap");
        // No ladder from it: a stroke from the handle at once is a route, not a finger left to a ladder that might climb.
        mouse.rail = 0;
        mouse.events.clear();
        down(0, 150, 50, 1520);
        strokeFirst(150, 50, 450, 50);
        up(0, 1900);
        assertTrue(mouse.events.toString(), mouse.events.get(0).startsWith("route "));
    }

    @Test
    public void griffATwoFingerTapLetsTheSelectionGo() {
        griff();
        down(0, 100, 50, 1000);
        down(1, 200, 50, 1050);
        up(1, 1150);
        up(0, 1160);
        assertTrue(mouse.events.contains("deselect"));
        assertFalse(mouse.events.toString().contains("right"));
    }

    @Test
    public void griffATwoFingerTapCountsFromTheFirstFingerAndStaysWithinOneAndAHalfMillimetres() {
        griff();
        // 360 ms from the first finger: too long, though the second was quick.
        down(0, 100, 50, 1000);
        down(1, 200, 50, 1300);
        up(1, 1360);
        up(0, 1370);
        assertFalse(mouse.events.contains("deselect"));
        // 1.4 mm apart more still taps; 1.5 mm pinches.
        down(0, 100, 50, 2000);
        down(1, 200, 50, 2050);
        moveBoth(93, 50, 207, 50);
        up(1, 2100);
        up(0, 2110);
        assertTrue(mouse.events.contains("deselect"));
        mouse.events.clear();
        down(0, 100, 50, 3000);
        down(1, 200, 50, 3050);
        moveBoth(92, 50, 208, 50);
        up(1, 3100);
        up(0, 3110);
        assertFalse(mouse.events.contains("deselect"));
        assertTrue(mouse.events.toString(), mouse.events.contains("pinch start 150 50 116"));
    }

    @Test
    public void griffTwoFingersZoomAndPanTheCameraTogether() {
        griff();
        down(0, 100, 100, 1000);
        down(1, 300, 100, 1040);
        moveBoth(80, 100, 320, 100);
        moveBoth(70, 120, 350, 120);
        expect("move 100 100", "move 300 100", "pinch start 200 100 240", "pinch 210 120 280");
    }

    @Test
    public void griffTheFingerLeftOfAPinchMovesTheCameraOnAndGlides() {
        griff();
        down(0, 100, 100, 1000);
        down(1, 300, 100, 1040);
        moveBoth(80, 100, 320, 100);
        up(1, 1100);
        assertEquals("pinch start 80 100 0", mouse.events.get(mouse.events.size() - 1));
        mouse.events.clear();
        for (int at = 1; at <= 3; ++at) {
            moveFirst(80 + 20 * at, 100, 1100 + 10 * at);
        }
        up(0, 1140);
        // 2000 pixels a second from the start, each move taking 0.4 of it into the velocity: 1568, and 0.9 of that.
        expect("pinch 100 100 0", "pinch 120 100 0", "pinch 140 100 0", "pinch end glide 1411 0");
    }

    @Test
    public void griffAPinchThatEndsTogetherDoesNotGlide() {
        griff();
        down(0, 100, 100, 1000);
        down(1, 300, 100, 1040);
        moveBoth(80, 100, 320, 100);
        up(1, 1100);
        up(0, 1200);
        assertEquals("pinch start 80 100 0", mouse.events.get(mouse.events.size() - 2));
        assertEquals("pinch end", mouse.events.get(mouse.events.size() - 1));
    }

    @Test
    public void griffAFingerAgainBesideTheOneLeftOfAPinchPinchesAnew() {
        griff();
        down(0, 100, 100, 1000);
        down(1, 300, 100, 1040);
        moveBoth(80, 100, 320, 100);
        // The first finger lifts; the second moves the camera on, then a finger comes again.
        up(0, 1100);
        mouse.events.clear();
        touch.move(1, 340, 110);
        touch.moved(1110);
        down(2, 500, 110, 1200);
        touch.move(1, 330, 110);
        touch.move(2, 520, 110);
        touch.moved(1210);
        expect("pinch 340 110 0", "pinch end", "move 500 110", "pinch start 425 110 190");
    }

    @Test
    public void griffASecondFingerTurnsAPanIntoTwoFingers() {
        griff();
        down(0, 100, 100, 1000);
        moveFirst(150, 100, 1100);
        // The program says that it made a pan of the drag (Game.SAID_PAN).
        touch.panned(mouse.lastDrag);
        down(1, 400, 100, 1200);
        moveBoth(130, 100, 420, 100);
        expect("move 100 100", "griff drag 100 100 now 150 100 rail 0", "move 150 100", "griff drag end 150 100", "move 400 100",
               "pinch start 275 100 290");
    }

    @Test
    public void griffASecondFingerLeavesADragNotKnownAsAPanAlone() {
        // Only the program tells a pan from a drag on a panel or the minimap (DragTask): a second finger leaves a drag alone
        // that the program has not said is a pan, and pinches no map behind a panel.
        griff();
        down(0, 100, 100, 1000);
        moveFirst(150, 100, 1100);
        mouse.events.clear();
        down(1, 400, 100, 1200);
        moveBoth(130, 100, 420, 100);
        for (String event : mouse.events) {
            assertFalse(mouse.events.toString(), event.startsWith("griff drag end") || event.startsWith("pinch"));
        }
    }

    @Test
    public void griffTheProgramsWordIsForOneDragAlone() {
        // A pan, its word in time; then a drag on a panel, which no word makes a pan, nor a late word for the pan before.
        griff();
        down(0, 100, 100, 1000);
        moveFirst(150, 100, 1100);
        int pan = mouse.lastDrag;
        touch.panned(pan);
        up(0, 1200);
        down(0, 600, 100, 2000);
        moveFirst(650, 100, 2100);
        mouse.events.clear();
        down(1, 900, 100, 2200);
        touch.panned(pan);
        down(2, 900, 120, 2250);
        moveBoth(630, 100, 920, 100);
        expect("move 630 100");
        // Its own word does.
        touch.panned(mouse.lastDrag);
        down(3, 900, 100, 2300);
        assertEquals("griff drag end 630 100", mouse.events.get(1));
    }

    @Test
    public void griffATapAndARestAreBoxsByBoxAsItCountedWhenTheFingerCameDown() {
        // FAF's reach for a unit beside the finger is set at its down, for Box or for the selection: a held Box let go before
        // the lift leaves the tap Box's, and Box held only after the down makes it none of Box's.
        griff();
        mouse.rail = TouchInput.RAIL_BOX;
        down(0, 100, 50, 1000);
        mouse.rail = 0;
        up(0, 1000 + TouchInput.SETTLE_MILLISECONDS);
        down(0, 200, 50, 2000);
        mouse.rail = TouchInput.RAIL_BOX | TouchInput.RAIL_QUEUE;
        up(0, 2000 + TouchInput.SETTLE_MILLISECONDS);
        expect("move 100 50", "smart tap 100 50 rail 1", "move 200 50", "smart tap 200 50 rail 2", "rail used 2");
        assertEquals(List.of("100 50 box", "200 50"), mouse.downs);
        // A rest the same: Box at its down makes it none, whatever Box does by its lift; Box only after the down leaves it one.
        mouse.events.clear();
        mouse.rail = TouchInput.RAIL_BOX;
        down(0, 300, 50, 3000);
        mouse.rail = 0;
        timerRuns();
        assertFalse("with Box no ring", mouse.ringShown);
        up(0, 3600);
        mouse.rail = 0;
        down(0, 400, 50, 4000);
        mouse.rail = TouchInput.RAIL_BOX;
        timerRuns();
        assertTrue("a ring: Box came after the down", mouse.ringShown);
        up(0, 4600);
        expect("move 300 50", "rest tap 300 50 rail 1", "move 400 50", "held", "griff rest 400 50", "rest tap 400 50 rail 0");
    }

    @Test
    public void griffAStrokeFromTheHandleIsARouteEndingWhereItLifted() {
        griff();
        mouse.griffX = 100;
        mouse.griffY = 50;
        down(0, 100, 50, 1000);
        strokeFirst(100, 50, 400, 50);
        assertTrue("the screen shows the stroke", mouse.shown > 1);
        assertEquals(Stroke.ROUTE, mouse.shownMode);
        assertEquals("the waypoints its lift gives", 2, mouse.shownWaypoints);
        up(0, 1500);
        // 300 pixels at 150 a waypoint, ending on the lift; nothing went to the game before it.
        expect("route 250 50, 400 50 rail 0");
        assertEquals(0, mouse.shown);
    }

    @Test
    public void griffAStrokeEndsWhereTheFingerWasAMomentBeforeItLifted() {
        // Fingers wobble as they lift: the last 50 ms do not count.
        griff();
        mouse.griffX = 100;
        mouse.griffY = 50;
        down(0, 100, 50, 1000);
        for (int at = 1; at <= 30; ++at) {
            moveFirst(100 + 10 * at, 50, 1000 + 10 * at);
        }
        moveFirst(420, 70, 1310);
        moveFirst(440, 90, 1330);
        // 50 ms after the move to 400 50; the one before it, at 390, would count for a lift 10 ms sooner.
        up(0, 1350);
        expect("route 250 50, 400 50 rail 0");
    }

    @Test
    public void griffARouteKeepsTheStrokesCornerAndTheRail() {
        griff();
        mouse.griffX = 100;
        mouse.griffY = 50;
        mouse.rail = TouchInput.RAIL_QUEUE;
        down(0, 100, 50, 1000);
        strokeFirst(100, 50, 160, 50);
        strokeFirst(160, 50, 160, 300);
        up(0, 1500);
        // Its points are a mm apart: the corner stays within one.
        List<float[]> route = places(mouse.events.get(0));
        assertTrue(mouse.events.get(0), Math.hypot(route.get(0)[0] - 160, route.get(0)[1] - 50) <= 10);
        assertTrue(mouse.events.get(0), mouse.events.get(0).endsWith("160 300 rail 2"));
        assertEquals("rail used 2", mouse.events.get(1));
    }

    @Test
    public void griffATapOnTheHandleTakesTheCameraToTheSelection() {
        griff();
        mouse.griffX = 100;
        mouse.griffY = 50;
        down(0, 102, 51, 1000);
        moveFirst(120, 51);
        up(0, 1100);
        // 1.8 mm is still a tap on it.
        expect("griff tap");
    }

    @Test
    public void griffAShortStrokeBackToTheHandleIsGivenUp() {
        griff();
        mouse.griffX = 100;
        mouse.griffY = 50;
        down(0, 100, 50, 1000);
        strokeFirst(100, 50, 200, 50);
        assertEquals(Stroke.ROUTE, mouse.shownMode);
        strokeFirst(200, 50, 110, 50);
        assertEquals("the screen says lifting gives it up", Stroke.CANCEL, mouse.shownMode);
        up(0, 1500);
        expect("given up");
    }

    @Test
    public void griffALongLoopBackToTheHandleIsAPatrol() {
        // 30 mm out, 10 down and back: 70 mm in all, ending 2 mm from its start.
        griff();
        mouse.griffX = 100;
        mouse.griffY = 50;
        down(0, 100, 50, 1000);
        strokeFirst(100, 50, 400, 50);
        strokeFirst(400, 50, 400, 150);
        strokeFirst(400, 150, 110, 60);
        assertEquals(Stroke.PATROL, mouse.shownMode);
        up(0, 2500);
        String patrol = mouse.events.get(0);
        assertTrue(patrol, patrol.startsWith("patrol "));
        assertTrue(patrol, patrol.endsWith("110 60 rail 0"));
        // The loop's corners are its waypoints, within its points' mm.
        boolean corner = false;
        for (float[] place : places(patrol)) {
            corner |= Math.hypot(place[0] - 400, place[1] - 50) <= 10;
        }
        assertTrue(patrol, corner);
        assertEquals(0, mouse.shown);
    }

    @Test
    public void griffASecondFingerGivesTheStrokeUp() {
        griff();
        mouse.griffX = 100;
        mouse.griffY = 50;
        down(0, 100, 50, 1000);
        strokeFirst(100, 50, 300, 50);
        down(1, 500, 200, 1300);
        up(1, 1400);
        up(0, 1500);
        expect("given up");
        assertEquals(0, mouse.shown);
    }

    @Test
    public void griffTheCursorHoversAtTheStrokesEndAtMostEachTenthOfASecond() {
        griff();
        mouse.griffX = 100;
        mouse.griffY = 50;
        down(0, 100, 50, 1000);
        for (int at = 1; at <= 25; ++at) {
            moveFirst(100 + 10 * at, 50, 1000 + 10 * at);
        }
        // At the handle, then at 100 and 200 ms; the place at 250 waits for 300, from the move at 210.
        assertEquals(List.of("100 50", "200 50", "300 50"), mouse.hovers);
        assertEquals(90, mouse.timer);
        timerRuns();
        assertEquals("350 50", mouse.hovers.get(3));
        up(0, 1400);
        assertEquals(-1, mouse.timer);
    }

    @Test
    public void griffPlacesABuildingAsTheMouseDoes() {
        griff();
        mouse.placing = true;
        down(0, 100, 50, 1000);
        up(0, 1100);
        expect("left down 100 50", "left up 100 50");
    }

    @Test
    public void aTapClicksWhereItLanded() {
        down(0, 100, 50, 1000);
        // Within the slop, which does not move the click.
        moveFirst(105, 50);
        up(0, 1000 + TouchInput.SETTLE_MILLISECONDS);
        expect("move 100 50", "left down 100 50", "left up 100 50");
    }

    @Test
    public void aTapIsATapForTheGamesUiAndGivesAFingerItsHelp() {
        down(0, 100, 50, 1000);
        up(0, 1000 + TouchInput.SETTLE_MILLISECONDS);
        assertEquals(1, mouse.tapped);
    }

    @Test
    public void anOrderByTapIsTheRightButtonWhereItLanded() {
        touch.setSwapped(true);
        down(0, 100, 50, 1000);
        up(0, 1000 + TouchInput.SETTLE_MILLISECONDS);
        expect("move 100 50", "right down 100 50", "right up 100 50");
        assertEquals(0, mouse.tapped);
    }

    @Test
    public void thePointerLeavesWhenTheLastFingerHasLifted() {
        down(0, 100, 50, 1000);
        assertFalse("a finger rests, and a tooltip may show", mouse.leaving);
        up(0, 1200);
        assertTrue(mouse.leaving);
    }

    @Test
    public void aNewFingerKeepsThePointer() {
        down(0, 100, 50, 1000);
        up(0, 1030);
        assertTrue(mouse.leaving);
        down(0, 300, 60, 1050);
        assertFalse(mouse.leaving);
    }

    @Test
    public void aFingerKeepsThePointerAheadOfAllItSends() {
        down(0, 100, 50, 1000);
        up(0, 1030);
        mouse.events.clear();
        // The click that waited, and this finger's place.
        down(0, 300, 60, 1050);
        assertEquals(0, mouse.sentBeforeStay);
        expect("left down 100 50", "left up 100 50", "move 300 60");
        // A second finger too.
        mouse.events.clear();
        down(1, 400, 60, 1100);
        assertEquals(0, mouse.sentBeforeStay);
        expect("move 400 60");
    }

    @Test
    public void aFingerAfterAGestureThatWasCutOffKeepsThePointer() {
        down(0, 100, 50, 1000);
        moveFirst(120, 50);
        fingers = 0;
        mouse.events.clear();
        down(0, 300, 60, 1500);
        assertEquals(0, mouse.sentBeforeStay);
        assertFalse(mouse.leaving);
        expect("left up 120 50", "move 300 60");
    }

    @Test
    public void aFingerWhoseLiftGotLostStillLeavesThePointerToTheNext() {
        down(0, 100, 50, 1000);
        fingers = 0;
        down(0, 300, 60, 1500);
        assertFalse(mouse.leaving);
    }

    @Test
    public void thePointerStaysUntilTheLastOfTwoFingersHasLifted() {
        down(0, 100, 50, 1000);
        down(1, 400, 200, 1040);
        up(1, 1100);
        assertFalse(mouse.leaving);
        up(0, 1110);
        assertTrue(mouse.leaving);
    }

    @Test
    public void thePointerLeavesAfterADragAndAfterAHold() {
        down(0, 100, 50, 1000);
        moveFirst(120, 50);
        up(0, 1200);
        assertTrue(mouse.leaving);
        down(0, 100, 50, 2000);
        timerRuns();
        assertFalse(mouse.leaving);
        up(0, 2500);
        assertTrue(mouse.leaving);
    }

    @Test
    public void whenTheSystemTakesTheFingersThePointerLeaves() {
        down(0, 100, 50, 1000);
        touch.cancel();
        assertTrue(mouse.leaving);
    }

    @Test
    public void aQuickTapClicksWhenTheCursorHasSettled() {
        down(0, 100, 50, 1000);
        up(0, 1030);
        expect("move 100 50");
        assertEquals(TouchInput.SETTLE_MILLISECONDS - 30, mouse.timer);
        timerRuns();
        expect("move 100 50", "left down 100 50", "left up 100 50");
    }

    @Test
    public void aNextFingerDoesNotWaitForTheClickBeforeIt() {
        down(0, 100, 50, 1000);
        up(0, 1030);
        down(0, 300, 60, 1050);
        expect("move 100 50", "left down 100 50", "left up 100 50", "move 300 60");
        assertEquals(TouchInput.HOLD_MILLISECONDS, mouse.timer);
    }

    @Test
    public void twoTapsNearEachOtherClickTheSamePixel() {
        down(0, 100, 50, 1000);
        up(0, 1150);
        down(0, 112, 58, 1250);
        up(0, 1400);
        expect("move 100 50", "left down 100 50", "left up 100 50", "move 112 58", "left down 100 50", "left up 100 50");
    }

    @Test
    public void twoTapsApartInPlaceOrTimeClickWhereTheyLanded() {
        down(0, 100, 50, 1000);
        up(0, 1150);
        down(0, 150, 50, 1250);
        up(0, 1400);
        down(0, 150, 50, 2000);
        up(0, 2150);
        expect("move 100 50", "left down 100 50", "left up 100 50", "move 150 50", "left down 150 50", "left up 150 50", "move 150 50",
            "left down 150 50", "left up 150 50");
    }

    @Test
    public void aFingerThatMovesDragsFromWhereItLanded() {
        down(0, 100, 50, 1000);
        moveFirst(120, 50);
        moveFirst(140, 60);
        up(0, 1200);
        expect("move 100 50", "left down 100 50", "move 120 50", "move 140 60", "left up 140 60");
        assertEquals(-1, mouse.timer);
    }

    @Test
    public void aFingerThatStaysHoldsTheRightButtonUntilItLifts() {
        down(0, 100, 50, 1000);
        timerRuns();
        expect("move 100 50", "right down 100 50", "held");
        moveFirst(130, 50);
        up(0, 1500);
        expect("move 100 50", "right down 100 50", "held", "move 130 50", "right up 130 50");
    }

    @Test
    public void swappedTheTapIsTheRightButtonAndTheHoldTheLeft() {
        touch.setSwapped(true);
        down(0, 100, 50, 1000);
        up(0, 1200);
        down(0, 200, 50, 2000);
        timerRuns();
        up(0, 2500);
        expect("move 100 50", "right down 100 50", "right up 100 50", "move 200 50", "left down 200 50", "held", "left up 200 50");
    }

    @Test
    public void aQuickTapOfTwoFingersClicksTheRightButtonWhereTheSecondCameDown() {
        down(0, 100, 50, 1000);
        down(1, 400, 200, 1040);
        assertEquals(-1, mouse.timer);
        up(1, 1040 + TouchInput.SETTLE_MILLISECONDS);
        up(0, 1040 + TouchInput.SETTLE_MILLISECONDS + 10);
        expect("move 100 50", "move 400 200", "right down 400 200", "right up 400 200");
    }

    @Test
    public void twoFingersThatStayLongAreNoTap() {
        down(0, 100, 50, 1000);
        down(1, 400, 200, 1040);
        up(1, 1040 + TouchInput.TWO_FINGER_TAP_MILLISECONDS + 1);
        up(0, 2000);
        expect("move 100 50", "move 400 200");
    }

    @Test
    public void twoFingersMovingTogetherDragWithTheMiddleButton() {
        down(0, 100, 100, 1000);
        down(1, 300, 100, 1040);
        moveBoth(120, 100, 320, 100);
        moveBoth(150, 110, 350, 110);
        up(0, 1300);
        up(1, 1310);
        expect("move 100 100", "move 300 100", "move 200 100", "middle down 200 100", "move 220 100", "move 250 110",
            "middle up 250 110");
    }

    @Test
    public void twoFingersMovingApartZoomInAndCloserOut() {
        down(0, 100, 100, 1000);
        down(1, 300, 100, 1040);
        moveBoth(89, 100, 311, 100); // starts it, past two slops, without turning the wheel yet
        assertEquals(0, mouse.wheelTotal());
        moveBoth(0, 100, 444, 100); // twice as far apart
        int wheel = mouse.wheelTotal();
        assertEquals(Game.WHEEL_DELTA / 0.4, wheel, 2);
        moveBoth(111, 100, 333, 100); // half as far again
        assertEquals(0, mouse.wheelTotal(), 2);
        up(0, 1500);
        up(1, 1510);
        assertTrue(mouse.events.get(mouse.events.size() - 1).startsWith("wheel "));
    }

    @Test
    public void aPanCanBecomeAZoomAndAZoomAPan() {
        down(0, 100, 100, 1000);
        down(1, 300, 100, 1040);
        moveBoth(120, 100, 320, 100);
        moveBoth(80, 100, 360, 100); // apart by over six slops: the middle button goes up
        assertTrue(mouse.events.contains("middle up 220 100"));
        moveBoth(50, 100, 390, 100);
        assertTrue(mouse.wheelTotal() > 0);
        mouse.events.clear();
        moveBoth(130, 100, 470, 100); // the same distance, somewhere else: the middle button again
        assertTrue(mouse.events.get(1).startsWith("middle down "));
    }

    @Test
    public void fingersThatShakeAfterAZoomStillBecomeAPan() {
        down(0, 100, 100, 1000);
        down(1, 300, 100, 1040);
        moveBoth(60, 100, 340, 100);
        moveBoth(40, 100, 360, 100);
        assertTrue(mouse.wheelTotal() > 0);
        mouse.events.clear();
        // 16 pixels an event, and the distance a pixel or two off each time, which turns the wheel.
        for (int at = 1; at <= 6; ++at) {
            moveBoth(40 + 16 * at - at % 2, 100, 360 + 16 * at + at % 2, 100);
        }
        assertTrue(mouse.events.toString(), mouse.events.toString().contains("middle down"));
    }

    @Test
    public void onePinchingFingerStaysAZoom() {
        down(0, 100, 100, 1000);
        down(1, 300, 100, 1040);
        moveBoth(100, 100, 330, 100);
        for (int at = 1; at <= 20; ++at) {
            moveBoth(100, 100, 330 + 30 * at, 100);
        }
        assertTrue(mouse.wheelTotal() > 0);
        assertTrue(mouse.events.toString(), !mouse.events.toString().contains("middle"));
    }

    @Test
    public void aTwoFingerTapClicksWhereTheSecondLandedThoughItWandered() {
        down(0, 100, 50, 1000);
        down(1, 400, 200, 1040);
        touch.move(1, 410, 200);
        touch.moved(++clock);
        up(1, 1040 + TouchInput.SETTLE_MILLISECONDS);
        up(0, 1040 + TouchInput.SETTLE_MILLISECONDS + 10);
        expect("move 100 50", "move 400 200", "right down 400 200", "right up 400 200");
    }

    @Test
    public void aPalmIsForgottenAndTheOtherFingerGoesOn() {
        down(0, 100, 50, 1000);
        down(1, 400, 200, 1040);
        touch.forget(1, 1100);
        --fingers;
        assertEquals(TouchInput.HOLD_MILLISECONDS, mouse.timer);
        up(0, 1100 + TouchInput.SETTLE_MILLISECONDS);
        expect("move 100 50", "move 400 200", "move 100 50", "left down 100 50", "left up 100 50");
    }

    @Test
    public void aPalmThatCameFirstLeavesTheFingerAfterIt() {
        down(0, 100, 50, 1000);
        down(1, 400, 200, 1040);
        touch.forget(0, 1100);
        --fingers;
        up(1, 1100 + TouchInput.SETTLE_MILLISECONDS);
        expect("move 100 50", "move 400 200", "move 400 200", "left down 400 200", "left up 400 200");
    }

    @Test
    public void aFirstFingerEndsWhatNeverHeardItsEnd() {
        down(0, 100, 50, 1000);
        moveFirst(130, 50);
        // Without an up: the next first finger, as after a window that lost the fingers.
        touch.down(5, 300, 60, 2000, true);
        expect("move 100 50", "left down 100 50", "move 130 50", "left up 130 50", "move 300 60");
        assertEquals(TouchInput.HOLD_MILLISECONDS, mouse.timer);
    }

    @Test
    public void afterACancelWhatIsLeftOfTheFingersDoesNothing() {
        down(0, 100, 50, 1000);
        touch.cancel();
        mouse.events.clear();
        touch.move(0, 300, 50);
        touch.moved(++clock);
        up(0, 1500);
        assertEquals(List.of(), mouse.events);
    }

    @Test
    public void furtherFingersAreIgnored() {
        down(0, 100, 50, 1000);
        moveFirst(130, 50);
        down(1, 400, 200, 1100);
        touch.move(1, 450, 200);
        touch.moved(++clock);
        up(1, 1200);
        up(0, 1300);
        expect("move 100 50", "left down 100 50", "move 130 50", "move 130 50", "left up 130 50");
    }

    @Test
    public void whatIsLeftOfAGestureDoesNothing() {
        down(0, 100, 50, 1000);
        down(1, 400, 200, 1040);
        up(1, 1100);
        mouse.events.clear();
        timerRuns();
        moveFirst(300, 50);
        up(0, 2000);
        expect("right down 400 200", "right up 400 200");
        down(0, 10, 10, 3000);
        assertEquals("move 10 10", mouse.events.get(2));
    }

    @Test
    public void aCancelReleasesWhatIsHeld() {
        down(0, 100, 50, 1000);
        moveFirst(130, 50);
        touch.cancel();
        fingers = 0; // the system's cancel takes them all
        expect("move 100 50", "left down 100 50", "move 130 50", "left up 130 50");
        down(0, 10, 10, 2000);
        assertEquals(TouchInput.HOLD_MILLISECONDS, mouse.timer);
    }

    @Test
    public void whilePlacingATapPutsTheGhostThereAndATapOnItPlacesIt() {
        mouse.placing = true;
        down(0, 500, 300, 1000);
        up(0, 1050);
        expect("left down 500 300", "left up 500 300");
        mouse.events.clear();
        down(0, 530, 320, 2000); // within the reach of six slops
        up(0, 2050);
        expect("tap 530 320 near ghost 500 300");
    }

    @Test
    public void whilePlacingATapAwayMovesTheGhost() {
        mouse.placing = true;
        down(0, 500, 300, 1000);
        up(0, 1050);
        mouse.events.clear();
        down(0, 900, 300, 2000);
        up(0, 2050);
        expect("left down 900 300", "left up 900 300");
    }

    @Test
    public void whilePlacingAFingerTakesTheGhostAlongFromAnywhere() {
        mouse.placing = true;
        down(0, 500, 300, 1000);
        up(0, 1050);
        mouse.events.clear();
        down(0, 1000, 600, 2000);
        moveFirst(1020, 600);
        moveFirst(1020, 610);
        up(0, 2200);
        expect("move 520 300", "move 520 310");
        assertEquals(-1, mouse.timer);
    }

    @Test
    public void whilePlacingTheFirstDragStartsAboveTheFinger() {
        mouse.placing = true;
        down(0, 500, 300, 1000);
        moveFirst(520, 300);
        up(0, 1200);
        expect("move 520 180");
    }

    @Test
    public void whilePlacingAFingerThatStaysShowsTheGhostAboveItAndPlacesNothing() {
        mouse.placing = true;
        down(0, 500, 300, 1000);
        timerRuns();
        up(0, 5000);
        expect("move 500 180", "held");
    }

    @Test
    public void whilePlacingAFingerThatStaysMovesTheGhostByTheSmallestSteps() {
        mouse.placing = true;
        down(0, 500, 300, 1000);
        up(0, 1050);
        mouse.events.clear();
        down(0, 1000, 600, 2000);
        moveFirst(1003, 600); // within the slop, which counts only for a tap
        timerRuns();
        moveFirst(1005, 600);
        moveFirst(1006, 601);
        up(0, 3000);
        expect("move 503 300", "held", "move 505 300", "move 506 301");
        // Still a ghost to tap, where it is now.
        mouse.events.clear();
        down(0, 510, 300, 4000);
        up(0, 4050);
        expect("tap 510 300 near ghost 506 301");
    }

    /** A ghost at 500 300, which a tap put there. */
    private void putGhost() {
        mouse.placing = true;
        down(0, 500, 300, 1000);
        up(0, 1050);
        mouse.events.clear();
    }

    /** A finger that stayed on the ghost at 500 300 draws to 700 300. */
    private void startLineTo700() {
        putGhost();
        down(0, 500, 300, 2000);
        timerRuns();
        moveFirst(700, 300);
    }

    @Test
    public void whilePlacingAFingerThatStaysOnTheGhostThenMovesDrawsALine() {
        putGhost();
        down(0, 510, 300, 2000);
        timerRuns();
        expect("held");
        moveFirst(600, 300);
        moveFirst(700, 320);
        up(0, 3000);
        expect("held", "line from 500 300", "move 590 300", "move 690 320", "left up 690 320");
    }

    @Test
    public void whilePlacingAFingerThatStaysOnTheGhostAndLiftsDoesNothing() {
        putGhost();
        down(0, 500, 300, 2000);
        moveFirst(503, 300); // within the slop
        timerRuns();
        moveFirst(504, 300);
        up(0, 3000);
        expect("held");
        mouse.events.clear();
        down(0, 505, 300, 4000);
        up(0, 4050);
        expect("tap 505 300 near ghost 500 300");
    }

    @Test
    public void whilePlacingAQuickDragOfTheGhostTakesItAlong() {
        putGhost();
        down(0, 500, 300, 2000);
        moveFirst(600, 300);
        up(0, 2100);
        expect("move 600 300");
    }

    @Test
    public void whilePlacingASecondFingerGivesUpTheLineAndTheGhostGoesBack() {
        startLineTo700();
        down(1, 100, 100, 2500);
        moveFirst(800, 300);
        up(0, 2600);
        up(1, 2700);
        expect("held", "line from 500 300", "move 700 300", "line given up", "move 500 300");
        mouse.events.clear();
        down(0, 500, 300, 3000);
        up(0, 3050);
        expect("tap 500 300 near ghost 500 300");
    }

    @Test
    public void whilePlacingTheSystemTakingTheFingersGivesUpTheLine() {
        startLineTo700();
        touch.cancel();
        expect("held", "line from 500 300", "move 700 300", "line given up", "move 500 300");
    }

    @Test
    public void whilePlacingTheTapAfterALinePutsTheGhostThoughItIsNear() {
        startLineTo700();
        up(0, 2600);
        mouse.events.clear();
        down(0, 705, 300, 4000);
        up(0, 4050);
        expect("left down 705 300", "left up 705 300");
    }

    @Test
    public void whilePlacingAFingerThatStaysOnAUsedGhostMovesIt() {
        putGhost();
        down(0, 500, 300, 2000);
        up(0, 2050);
        mouse.events.clear();
        down(0, 500, 300, 3000);
        timerRuns();
        moveFirst(504, 300);
        up(0, 4000);
        expect("move 500 300", "held", "move 504 300");
    }

    @Test
    public void whilePlacingTwoFingersOnAFingerThatStayedOnTheGhostMoveTheCamera() {
        putGhost();
        down(0, 500, 300, 2000);
        timerRuns();
        down(1, 700, 300, 2400);
        moveBoth(540, 340, 740, 340);
        up(0, 2600);
        up(1, 2700);
        expect("held", "move 700 300", "move 600 300", "middle down 600 300", "move 640 340", "middle up 640 340", "move 500 300");
    }

    @Test
    public void whilePlacingAPalmLeavesTheOtherFingerToTheGhost() {
        mouse.placing = true;
        down(0, 500, 300, 1000);
        up(0, 1050);
        mouse.events.clear();
        down(0, 1000, 600, 2000);
        down(1, 1200, 600, 2020);
        touch.forget(1, 2040);
        --fingers;
        moveFirst(1040, 600);
        up(0, 2200);
        expect("move 1200 600", "move 540 300", "move 540 300"); // last: the cursor back to the ghost, as for any two fingers
    }

    @Test
    public void whilePlacingAQuickTapOfTwoFingersIsTheRightButtonThoughSwapped() {
        touch.setSwapped(true);
        mouse.placing = true;
        down(0, 500, 300, 1000);
        down(1, 700, 300, 1020);
        up(0, 1150);
        up(1, 1160);
        expect("move 700 300", "right down 700 300", "right up 700 300");
    }

    @Test
    public void whilePlacingTheTapAfterAPlacingPutsTheGhostThoughItIsNear() {
        mouse.placing = true;
        down(0, 500, 300, 1000);
        up(0, 1050);
        down(0, 500, 300, 2000);
        up(0, 2050);
        mouse.events.clear();
        down(0, 530, 320, 3000);
        up(0, 3050);
        expect("left down 530 320", "left up 530 320");
        // That one is a ghost to tap again.
        mouse.events.clear();
        down(0, 510, 310, 4000);
        up(0, 4050);
        expect("tap 510 310 near ghost 530 320");
    }

    @Test
    public void whilePlacingAFingerTakesTheGhostOnFromWhereABuildingWentUp() {
        mouse.placing = true;
        down(0, 500, 300, 1000);
        up(0, 1050);
        down(0, 500, 300, 2000);
        up(0, 2050);
        mouse.events.clear();
        down(0, 1000, 600, 3000);
        moveFirst(1040, 600);
        up(0, 3200);
        expect("move 540 300");
        // Moved, it is a ghost to tap again.
        mouse.events.clear();
        down(0, 550, 300, 4000);
        up(0, 4050);
        expect("tap 550 300 near ghost 540 300");
    }

    @Test
    public void whilePlacingTheCameraMakesAUsedGhostOneToTapAgain() {
        mouse.placing = true;
        down(0, 500, 300, 1000);
        up(0, 1050);
        down(0, 500, 300, 2000);
        up(0, 2050);
        down(0, 100, 100, 3000);
        down(1, 300, 100, 3040);
        moveBoth(120, 100, 320, 100);
        up(0, 3300);
        up(1, 3310);
        mouse.events.clear();
        down(0, 510, 300, 4000);
        up(0, 4050);
        expect("tap 510 300 near ghost 500 300");
    }

    @Test
    public void whilePlacingTheGhostStaysOnTheScreen() {
        touch.setScreen(2400, 1080);
        mouse.placing = true;
        // Above the finger is above the screen.
        down(0, 100, 50, 1000);
        moveFirst(130, 50);
        moveFirst(130, 60);
        up(0, 1200);
        expect("move 130 0", "move 130 10");
        mouse.events.clear();
        // It follows the finger back at once, however far the finger went.
        down(0, 2000, 500, 2000);
        moveFirst(1500, 500);
        moveFirst(1550, 500);
        moveFirst(1560, 1050);
        moveFirst(1560, 1000);
        up(0, 2500);
        expect("move 0 10", "move 50 10", "move 60 560", "move 60 510");
        mouse.events.clear();
        down(0, 100, 100, 3000);
        moveFirst(2500, 700);
        moveFirst(2490, 690);
        up(0, 3500);
        expect("move 2400 1080", "move 2390 1070");
    }

    @Test
    public void whilePlacingTwoFingersMoveTheCameraAndTheGhostComesBack() {
        mouse.placing = true;
        down(0, 500, 300, 1000);
        up(0, 1050);
        mouse.events.clear();
        down(0, 100, 100, 2000);
        down(1, 300, 100, 2040);
        moveBoth(120, 100, 320, 100);
        up(0, 2300);
        up(1, 2310);
        expect("move 300 100", "move 200 100", "middle down 200 100", "move 220 100", "middle up 220 100", "move 500 300");
    }

    @Test
    public void whilePlacingACancelBringsTheCursorBackToTheGhost() {
        mouse.placing = true;
        down(0, 500, 300, 1000);
        up(0, 1050);
        mouse.events.clear();
        down(0, 100, 100, 2000);
        down(1, 300, 100, 2040);
        moveBoth(120, 100, 320, 100);
        touch.cancel();
        expect("move 300 100", "move 200 100", "middle down 200 100", "move 220 100", "middle up 220 100", "move 500 300");
    }

    @Test
    public void whilePlacingAFirstFingerBringsTheCursorBackWhenTheEndWentUnheard() {
        mouse.placing = true;
        down(0, 500, 300, 1000);
        up(0, 1050);
        down(0, 100, 100, 2000);
        down(1, 300, 100, 2040);
        moveBoth(120, 100, 320, 100);
        up(0, 2300); // the other's end does not come
        fingers = 0;
        mouse.events.clear();
        down(0, 900, 600, 3000);
        expect("move 500 300");
    }

    @Test
    public void theGhostIsForgottenWhenThePlacingIsOver() {
        mouse.placing = true;
        down(0, 500, 300, 1000);
        up(0, 1050);
        mouse.placing = false;
        down(0, 10, 10, 2000);
        up(0, 2200);
        mouse.events.clear();
        mouse.placing = true;
        // A new ghost starts above the finger again, not from where the last one was.
        down(0, 520, 300, 3000);
        moveFirst(560, 300);
        up(0, 3300);
        expect("move 560 180");
    }

    @Test
    public void deletingATapDeletesWhenTheCursorHasSettled() {
        touch.setDeleting(true);
        down(0, 500, 300, 1000);
        up(0, 1030);
        expect("move 500 300");
        timerRuns();
        expect("move 500 300", "delete 500 300");
        // Over a ghost as well: it is the deleting that was asked for.
        mouse.events.clear();
        mouse.placing = true;
        down(0, 600, 300, 2000);
        up(0, 2200);
        expect("move 600 300", "delete 600 300");
    }

    @Test
    public void deletingAFingerThatStaysOrWandersStillOnlyDeletes() {
        touch.setDeleting(true);
        down(0, 500, 300, 1000);
        timerRuns();
        moveFirst(560, 300);
        up(0, 1500);
        expect("move 500 300", "delete 500 300");
    }

    // --- Griff's draft (buildstage/griff-v3-spec.md T2) ------------------------------------------------------------------

    /** Griff with its draft open, its plans at these places (number, x, y, on a deposit), the newest last. */
    private void draft(int[]... plans) {
        griff();
        mouse.drafting = true;
        for (int[] plan : plans) {
            mouse.draftPlans.add(plan);
        }
    }

    @Test
    public void griffDraftATapPlacesAPlanWhereTheFingerCameDown() {
        draft();
        down(0, 100, 50, 1000);
        moveFirst(110, 55);
        up(0, 1000 + TouchInput.SETTLE_MILLISECONDS);
        expect("move 100 50", "draft tap 100 50");
        assertEquals("the program hears the finger, as for any tap", List.of("100 50"), mouse.downs);
    }

    @Test
    public void griffDraftATapOnAPlanRemovesIt() {
        draft(new int[] {7, 100, 50, 0}, new int[] {8, 300, 50, 0});
        down(0, 110, 60, 1000);
        up(0, 1000 + TouchInput.SETTLE_MILLISECONDS);
        expect("move 110 60", "draft tap 110 60 on 7");
    }

    @Test
    public void griffDraftATapIsNoActionForTheRailAndClimbsNoLadder() {
        // Plus stays lit for the tick; a second tap soon after and near is another plan, not a rung.
        draft();
        mouse.rail = TouchInput.RAIL_QUEUE;
        down(0, 100, 50, 1000);
        up(0, 1000 + TouchInput.SETTLE_MILLISECONDS);
        down(0, 110, 50, 1200);
        up(0, 1200 + TouchInput.SETTLE_MILLISECONDS);
        expect("move 100 50", "draft tap 100 50", "move 110 50", "draft tap 110 50");
    }

    @Test
    public void griffDraftHasNoHandle() {
        draft();
        mouse.griffX = 100;
        mouse.griffY = 50;
        down(0, 100, 50, 1000);
        strokeFirst(100, 50, 300, 50);
        up(0, 1500);
        assertFalse(mouse.events.toString(), mouse.events.toString().contains("route"));
        assertEquals(-1, mouse.shown);
    }

    @Test
    public void griffDraftADragFromTheNewestPlanDrawsALineToWhereTheFingerSettled() {
        draft(new int[] {1, 50, 50, 0}, new int[] {2, 100, 50, 0});
        down(0, 100, 50, 1000);
        moveFirst(130, 50, 1010);
        moveFirst(200, 50, 1020);
        // It lifts 40 ms after its last move, 50 ms after the one before.
        up(0, 1060);
        expect("move 100 50", "draft line show 130 50", "draft line show 200 50", "draft line end 130 50");
    }

    @Test
    public void griffDraftADragFromAnOlderPlanMovesTheWholeDraft() {
        draft(new int[] {1, 50, 50, 0}, new int[] {2, 100, 50, 0});
        down(0, 55, 50, 1000);
        moveFirst(90, 60, 1010);
        up(0, 1100);
        expect("move 55 50", "draft move show 55 50 to 90 60", "draft move end 55 50 to 90 60");
    }

    @Test
    public void griffDraftADragFromAPlanOnADepositOrFromNothingPansWhateverTheRailSays() {
        // An extractor's plan stays on its deposit: no line, no move. Box with another building pans too (the mockup's).
        draft(new int[] {1, 100, 50, 1});
        mouse.rail = TouchInput.RAIL_BOX;
        down(0, 100, 50, 1000);
        moveFirst(150, 50, 1010);
        up(0, 1200);
        expect("move 100 50", "griff drag 100 50 now 150 50 rail 0", "move 150 50", "griff drag end 150 50");
        mouse.events.clear();
        down(0, 300, 50, 2000);
        moveFirst(350, 50, 2010);
        assertEquals("griff drag 300 50 now 350 50 rail 0", mouse.events.get(1));
    }

    @Test
    public void griffDraftBoxWithAnExtractorPickedIsABoxOverDepositsFromAnywhere() {
        draft(new int[] {1, 100, 50, 0});
        mouse.picksDeposit = true;
        mouse.rail = TouchInput.RAIL_BOX;
        down(0, 100, 50, 1000);
        moveFirst(200, 120, 1010);
        up(0, 1100);
        expect("move 100 50", "draft box show 100 50 to 200 120", "draft box end 100 50 to 200 120", "rail used 1");
    }

    @Test
    public void griffDraftARestOnNoPlanAimsAndItsLiftPlacesWhereTheFingerSettled() {
        draft(new int[] {1, 400, 50, 0});
        down(0, 100, 50, 1000);
        assertEquals("the mockup's REST_LOUPE", 400, mouse.timer);
        timerRuns();
        moveFirst(104, 52, 1410);
        moveFirst(130, 60, 1500);
        up(0, 1520);
        expect("move 100 50", "draft aim show 100 50", "held", "draft aim show 104 52", "draft aim show 130 60", "draft aim end 104 52");
    }

    @Test
    public void griffDraftARestOnAPlanOrWithBoxIsNoAimAndItsLiftIsATap() {
        draft(new int[] {1, 100, 50, 0});
        down(0, 100, 50, 1000);
        timerRuns();
        up(0, 1600);
        expect("move 100 50", "draft tap 100 50 on 1");
        mouse.events.clear();
        mouse.rail = TouchInput.RAIL_BOX;
        down(0, 300, 50, 2000);
        timerRuns();
        up(0, 2600);
        expect("move 300 50", "draft tap 300 50");
    }

    /** A tap, a rest that lifts, a rest that moves, a drag and a tap of two, the next at least a second after the last. */
    private void tapRestDragAndTwo(float x, float y) {
        down(0, x, y, 1000);
        up(0, 1040);
        down(0, x, y, 2000);
        if (mouse.timer >= 0) {
            timerRuns();
        }
        up(0, 2600);
        down(0, x, y, 3000);
        if (mouse.timer >= 0) {
            timerRuns();
        }
        moveFirst(x + 80, y + 40, 3500);
        up(0, 3600);
        down(0, x, y, 4000);
        moveFirst(x + 80, y, 4010);
        moveFirst(x + 160, y, 4020);
        up(0, 4100);
        down(0, x, y, 5000);
        down(1, x + 200, y, 5030);
        up(1, 5100);
        up(0, 5110);
    }

    @Test
    public void griffTheFingersRestWhileTheProgramsClicksRun() {
        // The tick's build mode is FAF's ghost to the activity, and the draft is open: a finger there would place the ghost or
        // a plan, between the tick's clicks or the undo's.
        draft(new int[] {1, 100, 50, 0});
        mouse.placing = true;
        mouse.clicking = true;
        tapRestDragAndTwo(100, 50);
        expect();
        assertEquals("only the first fingers' downs went, which the program takes for nothing then", 5, mouse.downs.size());
        assertEquals(-1, mouse.timer);
    }

    @Test
    public void griffTheFingersRestWhileTheProgramsClicksRunAlsoOnTheHandleAndOnTheMap() {
        griff();
        mouse.clicking = true;
        mouse.griffX = 100;
        mouse.griffY = 50;
        tapRestDragAndTwo(100, 50);
        tapRestDragAndTwo(400, 300);
        expect();
        assertEquals("no stroke", -1, mouse.shown);
        assertTrue(mouse.hovers.isEmpty());
        // Once they are over, a finger's gestures are its own again.
        mouse.clicking = false;
        down(0, 400, 300, 9000);
        up(0, 9040);
        timerRuns();
        expect("move 400 300", "smart tap 400 300 rail 0");
    }

    @Test
    public void griffAFingerDownBeforeTheProgramsClicksBeganMovesNoCursorWhileTheyRun() {
        // A pan held from before the undo's clicks: FAF's camera drag takes each move of the cursor then, the undo's aims too,
        // and FAF's cursor is the undo's to aim with. A finger still pending when they begin is the same once it drags.
        griff();
        down(0, 100, 50, 1000);
        moveFirst(150, 50);
        mouse.clicking = true;
        mouse.events.clear();
        moveFirst(200, 50);
        moveFirst(250, 50);
        for (String event : mouse.events) {
            assertFalse(event, event.startsWith("move "));
        }
        mouse.clicking = false;
        up(0, 1300);
        down(0, 400, 300, 3000);
        mouse.clicking = true;
        mouse.events.clear();
        moveFirst(460, 300);
        moveFirst(500, 300);
        for (String event : mouse.events) {
            assertFalse(event, event.startsWith("move "));
        }
    }

    @Test
    public void griffAFingerOnFafsGhostBeforeTheProgramsClicksBeganClicksNothingWhileTheyRun() {
        // FAF's ghost waited (a build key's mode) as the finger came down; the pill's undo then ended that mode and arms its
        // own: the finger's lift would be a press in the undo's mode, with its selection and Shift, at the finger.
        griff();
        mouse.placing = true;
        down(0, 500, 300, 1000);
        mouse.placing = false;
        mouse.clicking = true;
        mouse.events.clear();
        up(0, 1050);
        for (String event : mouse.events) {
            assertFalse(event, event.startsWith("left ") || event.startsWith("tap "));
        }
    }

    @Test
    public void griffAGestureTheProgramsClicksCameIntoGivesNothingOnceTheyAreOver() {
        // A finger rests on the map as the other thumb ticks: the tick's clicks run and end while it stays. Its lift would be a
        // tap, FAF's right click there, which would give the builders another order in place of their buildings.
        griff();
        down(0, 400, 300, 1000);
        mouse.clicksBegin();
        mouse.clicking = false;
        mouse.events.clear();
        up(0, 1080);
        assertEquals(-1, mouse.timer);
        // A rest that would draw a box: its ring goes, and nothing is pressed.
        down(0, 400, 300, 2000);
        timerRuns();
        assertTrue(mouse.ringShown);
        mouse.clicksBegin();
        mouse.clicking = false;
        moveFirst(460, 300);
        assertFalse(mouse.ringShown);
        up(0, 2600);
        expect("move 400 300", "held", "griff rest 400 300", "rest gone");
        // A rest that clicks began before: no ring, nothing to feel.
        mouse.events.clear();
        down(0, 400, 300, 2800);
        mouse.clicksBegin();
        mouse.clicking = false;
        timerRuns();
        expect("move 400 300");
        assertFalse(mouse.ringShown);
        up(0, 2900);
        // A tap whose click waits for the cursor's rest, where clicks begin meanwhile; and one whose click the next finger's
        // down would make.
        mouse.events.clear();
        down(0, 400, 300, 3000);
        up(0, 3040);
        mouse.clicksBegin();
        mouse.clicking = false;
        timerRuns();
        expect("move 400 300");
        down(0, 400, 300, 3200);
        up(0, 3240);
        mouse.clicksBegin();
        mouse.clicking = false;
        down(0, 600, 300, 3300);
        up(0, 3500);
        expect("move 400 300", "move 400 300", "move 600 300", "smart tap 600 300 rail 0");
        // The next gesture is the finger's own again.
        mouse.events.clear();
        down(0, 400, 300, 4000);
        up(0, 4040);
        timerRuns();
        expect("move 400 300", "smart tap 400 300 rail 0");
    }

    @Test
    public void griffALineOnFafsGhostThatTheProgramsClicksCameIntoReleasesNothing() {
        // The clicks gave the line up as they began (FAF's build drag cancelled) and took the ghost's mode: the line's finger,
        // still all the while, moves and lifts once they are over, with nothing more of the line.
        griff();
        startLineTo700();
        mouse.clicksBegin();
        mouse.clicking = false;
        mouse.events.clear();
        moveFirst(760, 300);
        up(0, 2600);
        expect();
        // A second finger joins a gesture given up, and the two rest.
        mouse.placing = false;
        down(0, 400, 300, 3000);
        mouse.clicksBegin();
        mouse.events.clear();
        down(1, 600, 300, 3030);
        moveBoth(420, 300, 640, 300);
        up(1, 3100);
        up(0, 3110);
        expect();
    }

    @Test
    public void griffWhatTheSystemTakesOfAGestureTheProgramsClicksCameIntoGivesNothing() {
        // A line on FAF's ghost that the system takes, as clicks ran: the clicks gave it up already.
        griff();
        startLineTo700();
        mouse.clicksBegin();
        mouse.clicking = false;
        mouse.events.clear();
        touch.cancel();
        expect();
        up(0, 2600);
        // Two fingers of which the system forgets one, a palm, once clicks ran: the one left is no gesture of its own.
        mouse.placing = false;
        down(0, 300, 300, 3000);
        down(1, 500, 300, 3010);
        mouse.clicksBegin();
        mouse.clicking = false;
        mouse.events.clear();
        touch.forget(1, 3050);
        moveFirst(360, 300, 3060);
        expect();
    }

    @Test
    public void griffAGestureTheProgramsClicksCameIntoLeavesTheCursorWhereTheyPutIt() {
        // Two fingers came down over FAF's ghost and took the cursor from it, which their last lift would take back; and a
        // first finger's down comes while a drag still goes on, as where Android lost its lift.
        griff();
        putGhost();
        down(0, 500, 300, 2000);
        down(1, 700, 300, 2010);
        mouse.clicksBegin();
        mouse.clicking = false;
        mouse.events.clear();
        moveBoth(480, 300, 720, 300);
        up(1, 2100);
        up(0, 2110);
        expect();
        mouse.placing = false;
        down(0, 100, 50, 3000);
        moveFirst(150, 50);
        mouse.clicksBegin();
        mouse.clicking = false;
        mouse.events.clear();
        touch.down(5, 900, 300, 3100, true);
        expect("move 900 300");
    }

    @Test
    public void griffTheTapLadderClimbsNoRungOverAGestureTheProgramsClicksCameInto() {
        griff();
        down(0, 100, 50, 1000);
        up(0, 1040);
        timerRuns();
        touch.tapAnswered(mouse.lastTap, TouchInput.TAP_OWN);
        down(0, 100, 50, 1100);
        mouse.clicksBegin();
        mouse.clicking = false;
        up(0, 1140);
        mouse.events.clear();
        down(0, 100, 50, 1200);
        up(0, 1240);
        timerRuns();
        expect("move 100 50", "smart tap 100 50 rail 0");
    }

    @Test
    public void griffAGestureTheProgramsClicksCameIntoTakesWhatItShowsAway() {
        // A stroke from the handle is given up, as by a second finger; the draft's line is put away, as by one; a pinch lets
        // the camera go.
        griff();
        mouse.griffX = 100;
        mouse.griffY = 50;
        down(0, 100, 50, 1000);
        strokeFirst(100, 50, 300, 50);
        mouse.clicksBegin();
        mouse.events.clear();
        moveFirst(320, 50);
        up(0, 1500);
        expect("given up");
        assertEquals("no stroke shown", 0, mouse.shown);
        mouse.clicking = false;
        draft(new int[] {1, 100, 50, 0});
        mouse.events.clear();
        down(0, 100, 50, 2000);
        moveFirst(150, 50, 2010);
        mouse.clicksBegin();
        moveFirst(200, 50, 2020);
        up(0, 2100);
        expect("move 100 50", "draft line show 150 50", "draft line abort");
        mouse.clicking = false;
        mouse.drafting = false;
        mouse.events.clear();
        down(0, 300, 300, 3000);
        down(1, 500, 300, 3010);
        moveBoth(280, 300, 520, 300);
        mouse.clicksBegin();
        moveBoth(260, 300, 540, 300);
        up(1, 3100);
        up(0, 3110);
        expect("move 300 300", "move 500 300", "pinch start 400 300 240", "pinch end");
    }

    @Test
    public void theMouseSchemeAsksNothingOfTheProgramsClicks() {
        // Griff's clicks never run with the mouse scheme, and its fingers go on as they always did.
        mouse.clicksBegin();
        down(0, 100, 50, 1000);
        moveFirst(120, 50);
        mouse.clicksBegin();
        moveFirst(140, 60);
        up(0, 1200);
        expect("move 100 50", "left down 100 50", "move 120 50", "move 140 60", "left up 140 60");
    }

    @Test
    public void griffDraftASecondFingerGivesTheLineUpAndTheTwoPinchWithoutATap() {
        draft(new int[] {1, 100, 50, 0});
        down(0, 100, 50, 1000);
        moveFirst(150, 50, 1010);
        down(1, 350, 50, 1020);
        up(1, 1060);
        up(0, 1070);
        expect("move 100 50", "draft line show 150 50", "draft line abort", "move 350 50", "pinch start 250 50 200", "pinch start 150 50 0",
               "pinch end");
        assertFalse(mouse.events.contains("draft drop"));
    }

    @Test
    public void griffDraftASecondFingerPutsAMoveBackAndEndsAnAim() {
        draft(new int[] {1, 50, 50, 0}, new int[] {2, 100, 50, 0});
        down(0, 50, 50, 1000);
        moveFirst(90, 50, 1010);
        down(1, 350, 50, 1020);
        assertTrue(mouse.events.toString(), mouse.events.contains("draft move abort"));
        up(1, 1500);
        up(0, 1510);
        mouse.events.clear();
        down(0, 200, 50, 2000);
        timerRuns();
        down(1, 350, 50, 2450);
        assertEquals(List.of("move 200 50", "draft aim show 200 50", "held", "draft aim abort", "move 350 50"), mouse.events);
    }

    @Test
    public void griffDraftATwoFingerTapDropsTheDraft() {
        draft(new int[] {1, 100, 50, 0});
        down(0, 100, 50, 1000);
        down(1, 200, 50, 1050);
        up(1, 1150);
        up(0, 1160);
        assertTrue(mouse.events.toString(), mouse.events.contains("draft drop"));
        assertFalse(mouse.events.contains("deselect"));
    }

    @Test
    public void griffDraftGesturesTheSystemTakesGiveNothing() {
        draft(new int[] {1, 50, 50, 0}, new int[] {2, 100, 50, 0});
        down(0, 50, 50, 1000);
        moveFirst(90, 50, 1010);
        touch.cancel();
        assertEquals("draft move abort", mouse.events.get(mouse.events.size() - 1));
    }

    @Test
    public void aPictureKeepsTheViewsShapeAtTheChosenLines() {
        int[] size = PictureSize.of(2400, 1080, 768);
        assertEquals(1707, size[0]);
        assertEquals(768, size[1]);
    }

    @Test
    public void aPictureHasTheLeastLinesOnAScreenWithFewer() {
        int[] size = PictureSize.of(1544, 720, 768);
        assertEquals(1647, size[0]);
        assertEquals(768, size[1]);
        size = PictureSize.of(1544, 720, PictureSize.AS_THE_SCREEN);
        assertEquals(768, size[1]);
    }

    @Test
    public void aPictureIsWideEnoughInANarrowWindow() {
        int[] size = PictureSize.of(1195, 1080, 768);
        assertEquals(1024, size[0]);
        assertEquals(925, size[1]);
    }

    @Test
    public void aPictureAsTheScreenHasItsSize() {
        int[] size = PictureSize.of(3088, 1440, PictureSize.AS_THE_SCREEN);
        assertEquals(3088, size[0]);
        assertEquals(1440, size[1]);
    }
}
