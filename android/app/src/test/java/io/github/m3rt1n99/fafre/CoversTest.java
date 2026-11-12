package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.util.Arrays;

import org.junit.Test;

/** Griff's covers: the app's pieces that show, and the program hears them when they changed. */
public class CoversTest {
    private final Covers covers = new Covers();

    private int[] given() {
        return Arrays.copyOf(covers.boxes(), 4 * covers.count());
    }

    @Test
    public void aPieceCoversWhileItShowsAndHasASize() {
        covers.begin();
        covers.piece(true, 0, 10, 20, 40);
        covers.piece(false, 50, 50, 60, 60);
        covers.piece(true, 70, 70, 70, 90);
        covers.piece(true, 80, 80, 90, 80);
        assertTrue(covers.end());
        assertArrayEquals(new int[] {0, 10, 20, 40}, given());
    }

    @Test
    public void theProgramHearsThemOnlyWhenTheyChanged() {
        covers.begin();
        covers.piece(true, 0, 10, 20, 40);
        covers.piece(false, 30, 300, 330, 360);
        assertTrue("the first", covers.end());
        covers.begin();
        covers.piece(true, 0, 10, 20, 40);
        covers.piece(false, 30, 300, 330, 360);
        assertFalse("the same", covers.end());
        // The pill shows, and goes again.
        covers.begin();
        covers.piece(true, 0, 10, 20, 40);
        covers.piece(true, 30, 300, 330, 360);
        assertTrue(covers.end());
        assertArrayEquals(new int[] {0, 10, 20, 40, 30, 300, 330, 360}, given());
        covers.begin();
        covers.piece(true, 0, 10, 20, 40);
        covers.piece(false, 30, 300, 330, 360);
        assertTrue(covers.end());
        assertEquals(1, covers.count());
        // None at all is a change too: the program's go.
        covers.begin();
        assertTrue(covers.end());
        assertEquals(0, covers.count());
    }

    @Test
    public void aPieceThatMovedIsAChange() {
        covers.begin();
        covers.piece(true, 0, 10, 20, 40);
        covers.end();
        covers.begin();
        covers.piece(true, 0, 11, 20, 41);
        assertTrue(covers.end());
    }

    @Test
    public void thereAreAtMostSoMany() {
        covers.begin();
        for (int piece = 0; piece <= Covers.MOST; ++piece) {
            covers.piece(true, piece, 0, piece + 1, 1);
        }
        covers.end();
        assertEquals(Covers.MOST, covers.count());
    }
}
