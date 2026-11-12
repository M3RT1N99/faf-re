package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

/** Griff's undo as its pill shows it: 3 s after an order, one level, a tap once, the waiting mark. */
public class UndoTest {
    private final Undo undo = new Undo();

    @Test
    public void thePillShowsForThreeSecondsAfterAnOrderWithItsTimeLeft() {
        assertFalse("no order yet", undo.showing(0));
        undo.ordered(1, 1000);
        assertTrue(undo.showing(1000));
        assertTrue(undo.showing(3999));
        assertFalse(undo.showing(4000));
        assertEquals(1, undo.left(1000), 0);
        assertEquals(0.5f, undo.left(2500), 1e-6);
        assertEquals(0, undo.left(4000), 0);
        assertEquals(0, undo.tap(4000));
    }

    @Test
    public void aNewOrdersRecordReplacesTheLastWithItsOwnTime() {
        undo.ordered(1, 1000);
        undo.ordered(2, 2500);
        assertTrue(undo.showing(5000));
        assertEquals(2, undo.tap(5000));
    }

    @Test
    public void theLaddersAttackMoveKeepsTheMovesRecordAndTime() {
        undo.ordered(1, 1000);
        undo.ordered(1, 1300);
        assertTrue(undo.showing(3999));
        assertFalse(undo.showing(4000));
    }

    @Test
    public void aTapTakesTheRecordBackOnceAndWaitsTillItIs() {
        undo.ordered(3, 0);
        assertFalse("not tapped", undo.waiting());
        assertEquals(3, undo.tap(100));
        assertTrue(undo.waiting());
        assertEquals("tapped already", 0, undo.tap(200));
        undo.undone(2);
        assertTrue("another record's", undo.showing(300));
        assertTrue(undo.waiting());
        undo.undone(3);
        assertFalse(undo.showing(300));
        assertFalse(undo.waiting());
        assertEquals(0, undo.tap(300));
    }

    @Test
    public void aNewOrderAfterATapIsTheNewRecordsNotTapped() {
        undo.ordered(1, 0);
        assertEquals(1, undo.tap(100));
        undo.ordered(2, 200);
        assertFalse(undo.waiting());
        assertEquals(2, undo.tap(300));
    }

    @Test
    public void withoutTheGameThereIsNoPill() {
        undo.ordered(1, 0);
        undo.tap(5);
        undo.clear();
        assertFalse(undo.showing(10));
        assertFalse(undo.waiting());
        assertEquals(0, undo.tap(10));
    }
}
