package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

import org.junit.Test;

/** PageInput's gestures, as what reaches the game. */
public class PageInputTest {
    private static final float SLOP = 10;
    private static final int FINGER = 3;
    private static final int OTHER = 4;

    /** Writes down what the game gets, and runs the timer when a test says time has passed. */
    private static final class Recorder implements PageInput.Mouse {
        final List<String> events = new ArrayList<>();
        long timer = -1;
        boolean leaving;

        @Override
        public void move(float x, float y) {
            events.add("move " + (int) x + " " + (int) y);
        }

        @Override
        public void press(float x, float y) {
            events.add("press " + (int) x + " " + (int) y);
        }

        @Override
        public void release(float x, float y) {
            events.add("release " + (int) x + " " + (int) y);
        }

        @Override
        public void tap(float x, float y) {
            events.add("tap " + (int) x + " " + (int) y);
        }

        @Override
        public void lookForList(float x, float y) {
            events.add("list? " + (int) x + " " + (int) y);
        }

        @Override
        public void scroll(float dy) {
            events.add("scroll " + (int) dy);
        }

        @Override
        public void held() {
            events.add("held");
        }

        @Override
        public void leave() {
            events.add("leave");
        }

        @Override
        public void leaveSoon() {
            leaving = true;
        }

        @Override
        public void stay() {
            leaving = false;
        }

        @Override
        public void startTimer(long milliseconds) {
            timer = milliseconds;
        }

        @Override
        public void stopTimer() {
            timer = -1;
        }
    }

    private final Recorder mouse = new Recorder();
    private final PageInput input = new PageInput(mouse, SLOP);

    private void moveTo(float x, float y) {
        input.move(FINGER, x, y);
        input.moved();
    }

    private List<String> events(String... expected) {
        return Arrays.asList(expected);
    }

    @Test
    public void aFingerThatLandsShowsWhatItIsOverAndAsksForTheList() {
        input.down(FINGER, 100, 200, 0, true);
        assertEquals(events("move 100 200", "list? 100 200"), mouse.events);
        assertEquals(PageInput.REST_MILLISECONDS, mouse.timer);
    }

    @Test
    public void aQuickTapClicksWhenTheCursorHasSettled() {
        input.down(FINGER, 100, 200, 0, true);
        mouse.events.clear();
        input.up(FINGER, 50, true);
        assertEquals(events(), mouse.events);
        assertEquals(TouchInput.SETTLE_MILLISECONDS - 50, mouse.timer);
        input.timer();
        assertEquals(events("tap 100 200"), mouse.events);
        assertEquals(-1, mouse.timer);
        assertTrue(mouse.leaving);
    }

    @Test
    public void aTapAfterALongRestIsAClickAtOnce() {
        input.down(FINGER, 100, 200, 0, true);
        input.timer();
        mouse.events.clear();
        input.up(FINGER, 900, true);
        assertEquals(events("tap 100 200"), mouse.events);
    }

    @Test
    public void aRestIsFelt() {
        input.down(FINGER, 100, 200, 0, true);
        mouse.events.clear();
        input.timer();
        assertEquals(events("held"), mouse.events);
    }

    @Test
    public void wanderingWithinTheSlopIsAStillATap() {
        input.down(FINGER, 100, 200, 0, true);
        moveTo(106, 206);
        mouse.events.clear();
        input.up(FINGER, 200, true);
        assertEquals(events("tap 100 200"), mouse.events);
    }

    @Test
    public void aFingerThatGoesDownScrollsAndPressesNothing() {
        input.down(FINGER, 100, 200, 0, true);
        mouse.events.clear();
        moveTo(102, 215);
        assertEquals(events("leave"), mouse.events);
        assertEquals("the rest's timer goes", -1, mouse.timer);
        moveTo(103, 240);
        moveTo(103, 230);
        input.up(FINGER, 300, true);
        assertEquals(events("leave", "scroll 25", "scroll -10"), mouse.events);
        assertEquals(-1, mouse.timer);
        assertTrue(mouse.leaving);
    }

    @Test
    public void aScrollThatPausesGoesOnAsAScroll() {
        input.down(FINGER, 100, 200, 0, true);
        moveTo(100, 220);
        input.timer();
        mouse.events.clear();
        moveTo(100, 230);
        assertEquals(events("scroll 10"), mouse.events);
    }

    @Test
    public void aFingerThatGoesSidewaysDragsFromWhereItLanded() {
        input.down(FINGER, 100, 200, 0, true);
        mouse.events.clear();
        moveTo(120, 203);
        moveTo(150, 240);
        input.up(FINGER, 300, true);
        assertEquals(events("press 100 200", "move 120 203", "move 150 240", "release 150 240"), mouse.events);
        assertEquals(-1, mouse.timer);
    }

    @Test
    public void aFingerThatRestedAndThenGoesDownDrags() {
        input.down(FINGER, 100, 200, 0, true);
        input.timer();
        mouse.events.clear();
        moveTo(100, 240);
        moveTo(100, 260);
        input.up(FINGER, 800, true);
        assertEquals(events("press 100 200", "move 100 240", "move 100 260", "release 100 260"), mouse.events);
    }

    @Test
    public void twoFingersDoNothing() {
        input.down(FINGER, 100, 200, 0, true);
        input.down(OTHER, 300, 200, 50, false);
        assertEquals(-1, mouse.timer);
        mouse.events.clear();
        moveTo(100, 300);
        input.move(OTHER, 300, 300);
        input.moved();
        input.timer();
        input.up(OTHER, 400, false);
        input.up(FINGER, 500, true);
        assertEquals(events(), mouse.events);
        assertTrue(mouse.leaving);
    }

    @Test
    public void aSecondFingerLeavesAScrollAndADragAsTheyAre() {
        input.down(FINGER, 100, 200, 0, true);
        moveTo(100, 230);
        input.down(OTHER, 300, 200, 50, false);
        mouse.events.clear();
        moveTo(100, 240);
        input.move(OTHER, 400, 200);
        input.moved();
        assertEquals(events("scroll 10"), mouse.events);
        input.up(FINGER, 200, false);
        moveTo(100, 300);
        input.up(OTHER, 300, true);
        assertEquals(events("scroll 10"), mouse.events);
    }

    @Test
    public void aPalmIsAsIfItHadNotComeDown() {
        input.down(FINGER, 100, 200, 0, true);
        input.down(OTHER, 300, 200, 50, false);
        mouse.events.clear();
        input.forget(OTHER, 60);
        assertEquals(PageInput.REST_MILLISECONDS, mouse.timer);
        mouse.events.clear();
        input.up(FINGER, 300, true);
        assertEquals(events("tap 100 200"), mouse.events);
    }

    @Test
    public void whenTheSystemTakesTheFingersWhatIsHeldGoesUp() {
        input.down(FINGER, 100, 200, 0, true);
        moveTo(150, 200);
        mouse.events.clear();
        input.cancel();
        assertEquals(events("release 150 200"), mouse.events);
        assertTrue(mouse.leaving);
        mouse.events.clear();
        moveTo(200, 200);
        assertEquals(events(), mouse.events);
    }

    @Test
    public void whenTheSystemTakesAFingerThatDidNothingNoClickComes() {
        input.down(FINGER, 100, 200, 0, true);
        mouse.events.clear();
        input.cancel();
        input.timer();
        assertEquals(events(), mouse.events);
    }

    @Test
    public void aNewFingerSendsTheWaitingClickFirst() {
        input.down(FINGER, 100, 200, 0, true);
        input.up(FINGER, 20, true);
        mouse.events.clear();
        input.down(FINGER, 300, 400, 40, true);
        assertEquals(events("tap 100 200", "move 300 400", "list? 300 400"), mouse.events);
    }

    @Test
    public void twoTapsNearEachOtherGoToTheSamePlaceForTheDoubleClick() {
        input.down(FINGER, 100, 200, 0, true);
        input.up(FINGER, 150, true);
        input.down(FINGER, 110, 205, 250, true);
        mouse.events.clear();
        input.up(FINGER, 400, true);
        assertEquals(events("tap 100 200"), mouse.events);
    }

    @Test
    public void twoTapsFarApartStayWhereTheyLanded() {
        input.down(FINGER, 100, 200, 0, true);
        input.up(FINGER, 150, true);
        input.down(FINGER, 200, 205, 250, true);
        mouse.events.clear();
        input.up(FINGER, 400, true);
        assertEquals(events("tap 200 205"), mouse.events);
    }

    @Test
    public void aComingFingerTakesTheLeaveBack() {
        input.down(FINGER, 100, 200, 0, true);
        input.up(FINGER, 150, true);
        assertTrue(mouse.leaving);
        input.down(FINGER, 100, 200, 250, true);
        assertFalse(mouse.leaving);
    }

    @Test
    public void aMoveOfAFingerThatDoesNotCountIsNoMove() {
        input.down(FINGER, 100, 200, 0, true);
        mouse.events.clear();
        input.move(OTHER, 500, 500);
        input.moved();
        assertEquals(events(), mouse.events);
    }
}
