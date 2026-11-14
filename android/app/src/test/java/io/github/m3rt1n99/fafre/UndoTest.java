package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

/** Griff's undo as its pill shows it: 3 s after an order, one level, a tap once, the waiting mark, the toast's word. */
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
        assertFalse("another record's", undo.undone(2));
        assertTrue(undo.showing(300));
        assertTrue(undo.waiting());
        assertTrue("the pill's", undo.undone(3));
        assertFalse(undo.showing(300));
        assertFalse(undo.waiting());
        assertEquals(0, undo.tap(300));
    }

    @Test
    public void aWordThatChangedAfterAnUndoOverEarlierIsNoneOfThePillsRecord() {
        // The undo of record 9 said all of it went; FAF's assist-to-upgrade ordered an upgrade for one of its Guards only after
        // that, and the program tells the undo's word once more, now in part, while the pill shows the next record.
        undo.ordered(9, 0);
        undo.tap(100);
        assertTrue(undo.undone(9));
        undo.ordered(10, 500);
        assertFalse(undo.undone(9));
        assertTrue("the next record's pill stays", undo.showing(600));
        assertEquals(10, undo.tap(600));
        assertFalse("nor before any record", new Undo().undone(9));
    }

    @Test
    public void aTappedPillStaysWhileItWaitsPastItsThreeSeconds() {
        // The undo waits for the sim to take the gesture first, a network game's lag.
        undo.ordered(4, 0);
        assertEquals(4, undo.tap(2900));
        assertTrue(undo.showing(3500));
        assertTrue(undo.waiting());
        assertEquals("tapped already", 0, undo.tap(3500));
        undo.undone(4);
        assertFalse(undo.showing(3600));
    }

    @Test
    public void anOrderThatEndsTheRecordTakesThePillAway() {
        // FAF's ghost, a key's order or the construction panel's: only the last order counts, and it is no gesture's.
        undo.ordered(5, 0);
        undo.ordered(0, 500);
        assertFalse(undo.showing(600));
        assertEquals(0, undo.tap(600));
        undo.ordered(6, 700);
        assertTrue(undo.showing(800));
    }

    @Test
    public void aNewOrderAfterATapIsTheNewRecordsNotTapped() {
        undo.ordered(1, 0);
        assertEquals(1, undo.tap(100));
        undo.ordered(2, 200);
        assertFalse(undo.waiting());
        assertEquals(2, undo.tap(300));
    }

    // The next two characterize how the pill takes what the program tells after an undo that clicked nothing. They cannot pin
    // the rule that such a record holds and is offered again: the program decides it (GriffAndroid.cpp FinishUndo and
    // OfferAgain, which tell SAID_UNDONE and then SAID_ORDERED anew), and Undo has always taken a record told after its undo
    // as a new one, so they pass on Undo as it was before that rule too. They keep the pill to what the program relies on.
    @Test
    public void characterizationARecordNotUndoneThatTheProgramTellsAgainIsOfferedForItsTimeOnceMore() {
        // Nothing of it was clicked (its clicks ended before their first step): the record holds, and the program tells it
        // anew after the undo's word.
        undo.ordered(7, 0);
        assertEquals(7, undo.tap(1000));
        undo.undone(7);
        assertFalse(undo.showing(1100));
        undo.ordered(7, 5000);
        assertTrue(undo.showing(5000));
        assertFalse("not tapped", undo.waiting());
        assertEquals(1, undo.left(5000), 0);
        assertTrue(undo.showing(7999));
        assertFalse(undo.showing(8000));
        assertEquals("tapped again", 7, undo.tap(6000));
        assertTrue(undo.waiting());
    }

    @Test
    public void characterizationARecordNotUndoneThatTheProgramDroppedIsGone() {
        // A new order ended it while its undo waited for the sim, or the game did: the pill goes with the word.
        undo.ordered(8, 0);
        assertEquals(8, undo.tap(100));
        undo.undone(8);
        assertFalse(undo.showing(200));
        assertFalse(undo.waiting());
        assertEquals(0, undo.tap(200));
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
