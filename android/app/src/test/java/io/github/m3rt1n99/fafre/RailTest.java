package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

/** Griff's rail: a tap lights a button for the next action, a hold counts while held; how they look, and where they stand. */
public class RailTest {
    private static final int BOX = TouchInput.RAIL_BOX;
    private static final int PLUS = TouchInput.RAIL_QUEUE;
    private static final int ATTACK = TouchInput.RAIL_ATTACK;
    /** The view's pixels in a mm, for the rail's places. */
    private static final float MM = 10;

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
        rail.press(ATTACK, 0);
        rail.release(ATTACK, 2000, false);
        assertEquals(ATTACK, rail.counting(2001));
        assertEquals(Rail.IDLE, rail.look(ATTACK));
    }

    @Test
    public void eachButtonLooksAsItIs() {
        tap(BOX, 0);
        rail.press(ATTACK, 0);
        assertEquals(Rail.LIT, rail.look(BOX));
        assertEquals(Rail.IDLE, rail.look(PLUS));
        assertEquals(Rail.HELD, rail.look(ATTACK));
    }

    @Test
    public void inABandWithRoomAButtonIsElevenMillimetres() {
        assertEquals(11 * MM, Rail.side(60 * MM, MM), 0.001f);
    }

    @Test
    public void inANarrowBandThreeButtonsAndTheirTwoGapsFillIt() {
        // The emulator at the phone's size: 12.9 mm between FAF's top panels and its unit view, an H in from each.
        float band = 12.9f * MM;
        float side = Rail.side(band, MM);
        assertEquals((12.9f - 3) / 3 * MM, side, 0.001f);
        assertEquals(band, 3 * side + 2 * Rail.GAP_MM * MM, 0.001f);
    }

    @Test
    public void aBandWithoutRoomForTheGapsHasNoButtons() {
        assertEquals(0, Rail.side(2 * MM, MM), 0);
    }

    @Test
    public void fromTheBottomUpBoxPlusAndAttackStandOnTheBandsBottom() {
        float bottom = 400;
        float side = 40;
        assertEquals("Box ends at the band's bottom", bottom - side, Rail.top(0, bottom, side, MM), 0.001f);
        assertEquals("Plus a gap above it", bottom - 2 * side - Rail.GAP_MM * MM, Rail.top(1, bottom, side, MM), 0.001f);
        assertEquals("Attack at the top", bottom - 3 * side - 2 * Rail.GAP_MM * MM, Rail.top(2, bottom, side, MM), 0.001f);
    }
}
