package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

/** Griff's rail: a tap lights a button for the next action, a hold counts while held. */
public class RailTest {
    private static final int BOX = TouchInput.RAIL_BOX;
    private static final int PLUS = TouchInput.RAIL_QUEUE;
    private static final int ATTACK = TouchInput.RAIL_ATTACK;

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
        rail.press(ATTACK, 0);
        assertTrue(rail.held(ATTACK));
        assertEquals(ATTACK, rail.counting(2000));
        rail.release(ATTACK, 2000, true);
        assertFalse(rail.held(ATTACK));
        assertEquals(ATTACK, rail.counting(2000 + Rail.LIFT_MILLISECONDS - 1));
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
        rail.press(ATTACK, 0);
        rail.release(ATTACK, 100, false);
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
        rail.press(ATTACK, 0);
        assertEquals(BOX | ATTACK, rail.counting(500));
        rail.used(BOX);
        assertEquals(ATTACK, rail.counting(500));
    }
}
