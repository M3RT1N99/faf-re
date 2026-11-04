package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
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

        @Override
        public void move(float x, float y) {
            events.add("move " + (int) x + " " + (int) y);
        }

        @Override
        public void button(int button, boolean down, float x, float y) {
            events.add(name(button) + (down ? " down " : " up ") + (int) x + " " + (int) y);
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
        public void startTimer(long milliseconds) {
            timer = milliseconds;
        }

        @Override
        public void stopTimer() {
            timer = -1;
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

    private void down(int id, float x, float y, long time) {
        touch.down(id, x, y, time, fingers == 0);
        ++fingers;
    }

    private void up(int id, long time) {
        --fingers;
        touch.up(id, time, fingers == 0);
    }

    private void timerRuns() {
        assertTrue("a timer is set", mouse.timer >= 0);
        mouse.timer = -1;
        touch.timer();
    }

    private void moveFirst(float x, float y) {
        touch.move(0, x, y);
        touch.moved();
    }

    private void moveBoth(float x0, float y0, float x1, float y1) {
        touch.move(0, x0, y0);
        touch.move(1, x1, y1);
        touch.moved();
    }

    private void expect(String... events) {
        assertEquals(List.of(events), mouse.events);
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
        touch.moved();
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
        touch.moved();
        up(0, 1500);
        assertEquals(List.of(), mouse.events);
    }

    @Test
    public void furtherFingersAreIgnored() {
        down(0, 100, 50, 1000);
        moveFirst(130, 50);
        down(1, 400, 200, 1100);
        touch.move(1, 450, 200);
        touch.moved();
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
