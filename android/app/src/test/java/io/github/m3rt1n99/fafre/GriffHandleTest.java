package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

/** Griff's handle as the screen shows it: the program's, kept while a stroke is drawn, gone without a game's frames. */
public class GriffHandleTest {
    private final GriffHandle handle = new GriffHandle();

    @Test
    public void theHandleIsWhereTheProgramPlacedIt() {
        assertTrue(handle.place(true, true, 100, 50));
        assertTrue(handle.shown());
        assertEquals(100, handle.x(), 0);
        assertEquals(50, handle.y(), 0);
        assertTrue(handle.place(true, true, 120, 60));
        assertEquals(120, handle.x(), 0);
        assertTrue(handle.place(true, false, 0, 0));
        assertFalse(handle.shown());
    }

    @Test
    public void aStrokeKeepsTheHandleWhereItBegan() {
        handle.place(true, true, 100, 50);
        assertTrue("a new stroke", handle.stroke(true));
        assertFalse("the same stroke", handle.stroke(true));
        assertFalse(handle.place(true, true, 300, 80));
        assertFalse(handle.place(true, false, 0, 0));
        assertTrue(handle.shown());
        assertEquals(100, handle.x(), 0);
        handle.stroke(false);
        assertTrue(handle.place(true, true, 300, 80));
        assertEquals(300, handle.x(), 0);
    }

    @Test
    public void withoutTheFramesThereIsNoHandleThoughAStrokeIsDrawn() {
        // The game ends, or a dialog takes the fingers, while a finger draws a stroke: nothing reads the frames from then on,
        // so nothing would put a kept handle away; over the score screen or the dialog there is none.
        handle.place(true, true, 100, 50);
        handle.stroke(true);
        assertTrue(handle.place(false, false, 0, 0));
        assertFalse(handle.shown());
        // The dialog goes while the finger still draws: the stroke keeps what the screen shows, none, until it ends.
        assertFalse(handle.place(true, true, 300, 80));
        assertFalse(handle.shown());
        handle.stroke(false);
        assertTrue(handle.place(true, true, 300, 80));
        assertTrue(handle.shown());
        // Placed without the frames, as a caller might say it: still none.
        assertTrue(handle.place(false, true, 300, 80));
        assertFalse(handle.shown());
    }

    @Test
    public void strokesAreNumberedInSixteenBits() {
        int first = handle.strokeNumber();
        for (int stroke = 1; stroke <= 0x10000; ++stroke) {
            handle.stroke(true);
            handle.stroke(false);
            assertTrue(handle.strokeNumber() >= 0 && handle.strokeNumber() <= 0xFFFF);
        }
        assertEquals(first, handle.strokeNumber());
    }
}
