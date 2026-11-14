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
    public void theUndosPillCoversItsPlaceAShortWhileAfterItWent() {
        // The pill shows at two places, its time runs out at 3000, and a tap meant for it is on its way: the place where it last
        // showed stays covered until 500 ms after its last frame, then the program hears it go.
        Covers.Lingering pill = new Covers.Lingering(500);
        covers.begin();
        covers.piece(pill, true, 30, 300, 330, 360, 1000);
        assertTrue(covers.end());
        covers.begin();
        covers.piece(pill, true, 30, 310, 330, 370, 2990);
        assertTrue("moved", covers.end());
        covers.begin();
        covers.piece(pill, false, 0, 0, 0, 0, 3000);
        assertFalse("gone, and covering still", covers.end());
        assertArrayEquals(new int[] {30, 310, 330, 370}, given());
        covers.begin();
        covers.piece(pill, false, 0, 0, 0, 0, 3489);
        assertFalse(covers.end());
        assertEquals(1, covers.count());
        covers.begin();
        covers.piece(pill, false, 0, 0, 0, 0, 3490);
        assertTrue(covers.end());
        assertEquals(0, covers.count());
        // It shows again for a new order: it covers again, where it shows.
        covers.begin();
        covers.piece(pill, true, 30, 300, 330, 360, 5000);
        assertTrue(covers.end());
        assertArrayEquals(new int[] {30, 300, 330, 360}, given());
    }

    @Test
    public void thePillsCoverGoesAtOnceWhereThePlayerOpenedSomethingOfFafs() {
        // The pill went at 3000 and would cover its place until 3500; FAF's Menu opens over it at 3100: a tap there is the Menu's
        // from then on. Shown again, it covers again.
        Covers.Lingering pill = new Covers.Lingering(500);
        covers.begin();
        covers.piece(pill, true, 30, 300, 330, 360, 3000);
        assertTrue(covers.end());
        covers.begin();
        covers.piece(pill, false, 0, 0, 0, 0, 3050);
        assertFalse("still covering", covers.end());
        pill.forget();
        covers.begin();
        covers.piece(pill, false, 0, 0, 0, 0, 3100);
        assertTrue("gone at once", covers.end());
        assertEquals(0, covers.count());
        assertTrue(pill.covers(true, 30, 300, 330, 360, 5000));
        assertTrue(pill.covers(false, 0, 0, 0, 0, 5100));
    }

    @Test
    public void aPieceThatNeverShowedNeverCoversNorDoesOneWithoutASize() {
        Covers.Lingering never = new Covers.Lingering(500);
        assertFalse(never.covers(false, 30, 300, 330, 360, 0));
        assertFalse("without a size", never.covers(true, 30, 300, 30, 360, 0));
        Covers.Lingering pill = new Covers.Lingering(500);
        covers.begin();
        covers.piece(pill, false, 30, 300, 330, 360, 0);
        covers.piece(new Covers.Lingering(500), true, 30, 300, 30, 360, 0);
        covers.end();
        assertEquals(0, covers.count());
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
