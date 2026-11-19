package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

/** Griff's rail, Box and Plus: a tap lights a button for the next action, a hold counts while held; how they look. */
public class RailTest {
    private static final int BOX = TouchInput.RAIL_BOX;
    private static final int PLUS = TouchInput.RAIL_QUEUE;

    private final Rail rail = new Rail();

    private void tap(int bit, long time) {
        rail.press(bit, time);
        rail.release(bit, time + 100, true);
    }

    @Test
    public void aTapLightsItForTheNextActionWhichPutsItOut() {
        tap(PLUS, 0);
        assertEquals(PLUS, rail.counting(5000));
        assertEquals(PLUS, rail.shown());
        rail.used(PLUS);
        assertEquals(0, rail.counting(5000));
        assertEquals(0, rail.shown());
    }

    @Test
    public void aSecondTapPutsItOut() {
        tap(BOX, 0);
        tap(BOX, 1000);
        assertEquals(0, rail.counting(5000));
    }

    @Test
    public void heldItCountsUntilItGoesUpAndAMomentAfter() {
        rail.press(PLUS, 0);
        assertTrue(rail.held(PLUS));
        assertEquals(PLUS, rail.counting(2000));
        rail.release(PLUS, 2000, true);
        assertFalse(rail.held(PLUS));
        assertEquals(PLUS, rail.counting(2000 + Rail.LIFT_MILLISECONDS - 1));
        assertEquals(0, rail.counting(2000 + Rail.LIFT_MILLISECONDS));
        assertEquals(0, rail.shown());
    }

    @Test
    public void aHoldThatAnActionUsedLightsNothingAndItsMomentAfterStillCounts() {
        rail.press(PLUS, 0);
        rail.used(PLUS);
        assertEquals(PLUS, rail.counting(50));
        rail.release(PLUS, 100, true);
        assertEquals(0, rail.shown());
        assertEquals("a finger that lifts with the thumb", PLUS, rail.counting(101));
        rail.used(PLUS);
        assertEquals("one action takes the moment", 0, rail.counting(102));
    }

    @Test
    public void aShortPressThatAnActionUsedLightsNothing() {
        rail.press(PLUS, 0);
        rail.used(PLUS);
        rail.release(PLUS, 100, true);
        assertEquals(0, rail.counting(5000));
    }

    @Test
    public void aLongPressLeavesALitButtonLit() {
        tap(BOX, 0);
        rail.press(BOX, 1000);
        rail.release(BOX, 1000 + Rail.TAP_MILLISECONDS, true);
        assertEquals(BOX, rail.shown());
    }

    @Test
    public void aCancelLightsNothingAndLeavesALitButtonLit() {
        rail.press(BOX, 0);
        rail.cancel(BOX);
        assertEquals(0, rail.counting(1));
        tap(BOX, 1000);
        rail.press(BOX, 2000);
        rail.cancel(BOX);
        assertEquals(BOX, rail.shown());
    }

    @Test
    public void aFingerThatSlidOffIsNoTap() {
        rail.press(BOX, 0);
        rail.release(BOX, 100, false);
        assertEquals(0, rail.shown());
    }

    @Test
    public void aTapGivesNoMomentAfter() {
        tap(PLUS, 0);
        tap(PLUS, 1000);
        assertEquals(0, rail.counting(1101));
    }

    @Test
    public void theButtonsCountEachForItself() {
        tap(BOX, 0);
        rail.press(PLUS, 0);
        assertEquals(BOX | PLUS, rail.counting(500));
        rail.used(BOX);
        assertEquals(PLUS, rail.counting(500));
    }

    @Test
    public void aButtonLooksIdleThenHeldThenLitUntilAnActionUsesIt() {
        assertEquals(Rail.IDLE, rail.look(PLUS));
        rail.press(PLUS, 0);
        assertEquals(Rail.HELD, rail.look(PLUS));
        rail.release(PLUS, 100, true);
        assertEquals(Rail.LIT, rail.look(PLUS));
        rail.used(PLUS);
        assertEquals(Rail.IDLE, rail.look(PLUS));
    }

    @Test
    public void aLitButtonLooksHeldWhileAFingerIsOnItAndLitAgainAfterALongPress() {
        tap(BOX, 0);
        rail.press(BOX, 1000);
        assertEquals(Rail.HELD, rail.look(BOX));
        rail.release(BOX, 1000 + Rail.TAP_MILLISECONDS, true);
        assertEquals(Rail.LIT, rail.look(BOX));
    }

    @Test
    public void theMomentAfterALiftCountsButLooksIdle() {
        rail.press(PLUS, 0);
        rail.release(PLUS, 2000, false);
        assertEquals(PLUS, rail.counting(2001));
        assertEquals(Rail.IDLE, rail.look(PLUS));
    }

    @Test
    public void eachButtonLooksAsItIs() {
        tap(BOX, 0);
        assertEquals(Rail.LIT, rail.look(BOX));
        assertEquals(Rail.IDLE, rail.look(PLUS));
        rail.press(PLUS, 0);
        assertEquals(Rail.LIT, rail.look(BOX));
        assertEquals(Rail.HELD, rail.look(PLUS));
    }

    @Test
    public void allOutLeavesNothingCounting() {
        tap(BOX, 0);
        rail.press(PLUS, 0);
        rail.clear();
        assertEquals(0, rail.counting(1));
        assertEquals(0, rail.shown());
        assertFalse(rail.held(PLUS));
    }
}
