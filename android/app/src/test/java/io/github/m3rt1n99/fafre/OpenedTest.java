package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import android.view.View;

import java.util.Arrays;

import org.junit.Test;

/** What the player opened of FAF's, which Griff's pieces step aside from: what lies under a piece, and how a piece shows then. */
public class OpenedTest {
    private final Opened opened = new Opened();

    private boolean give(float... boxes) {
        float[] values = Arrays.copyOf(boxes, Opened.VALUES);
        return opened.update(values, boxes.length);
    }

    @Test
    public void whatLiesUnderAPieceIsWhatSharesMoreThanAnEdgeWithIt() {
        // The overlay filters' list as FAF 3835 opens it right of its multifunction panel at 1.6, and the rail's Attack and Box.
        give(356.6f, 160.8f, 600, 690);
        assertTrue("Attack", opened.under(393, 231, 512, 351));
        assertFalse("Box", opened.under(117, 231, 237, 351));
        assertTrue("a pixel's corner", opened.under(355, 159, 357, 161));
    }

    @Test
    public void aPieceJustBesideItCoversNothingOfIt() {
        give(100, 100, 200, 200);
        assertFalse("left of it", opened.under(50, 120, 100, 180));
        assertFalse("right of it", opened.under(200, 120, 250, 180));
        assertFalse("above it", opened.under(120, 50, 180, 100));
        assertFalse("below it", opened.under(120, 200, 180, 250));
        assertTrue("a pixel into it", opened.under(50, 120, 101, 180));
    }

    @Test
    public void aPieceWithoutASizeCoversNothing() {
        // As the pill's place before it ever showed (Covers.Lingering), here inside what a question box's shadow covers.
        give(0, 0, 1647, 768);
        assertFalse(opened.under(300, 300, 300, 360));
        assertFalse(opened.under(300, 300, 360, 300));
        assertTrue(opened.under(300, 300, 301, 301));
    }

    @Test
    public void eachOfThemCounts() {
        give(0, 0, 10, 10, 699, 115, 948, 700);
        assertTrue("the Menu's list, the second", opened.under(530, 231, 740, 351));
        assertFalse(opened.under(300, 300, 400, 400));
    }

    @Test
    public void onlyWholeBoxesCount() {
        // A box given in part is none, also where a whole one stood before; and no more are read than there is room for.
        assertTrue(give(0, 0, 10, 10, 50, 50, 300, 300));
        assertTrue(give(0, 0, 10, 10, 100, 100));
        assertFalse("the part of a box", opened.under(150, 150, 200, 200));
        assertTrue(opened.under(5, 5, 20, 20));
        float[] many = new float[Opened.VALUES + 4];
        for (int at = 0; at < many.length; at += 4) {
            many[at] = at;
            many[at + 1] = 0;
            many[at + 2] = at + 1;
            many[at + 3] = 1;
        }
        assertTrue(opened.update(many, many.length));
        assertTrue("the last that fits", opened.under(Opened.VALUES - 4, 0, Opened.VALUES - 3, 1));
        assertFalse("one past them", opened.under(Opened.VALUES, 0, Opened.VALUES + 1, 1));
        assertTrue("none", opened.update(many, -4));
        assertFalse(opened.under(0, 0, 1, 1));
    }

    @Test
    public void itChangesWhereABoxComesGoesOrMoves() {
        assertFalse("none, as before", give());
        assertTrue("one opens", give(10, 10, 20, 20));
        assertFalse("the same", give(10, 10, 20, 20));
        assertTrue("it grows", give(10, 10, 20, 21));
        assertTrue("another", give(10, 10, 20, 21, 30, 30, 40, 40));
        assertTrue("it is gone", give(30, 30, 40, 40));
        assertFalse(opened.under(10, 10, 20, 21));
        assertTrue("all gone", give());
        assertFalse(opened.under(30, 30, 40, 40));
    }

    @Test
    public void aPieceShowsWhereItIsWantedAndStepsAsideInItsPlace() {
        assertEquals(View.VISIBLE, Opened.visibility(true, false));
        assertEquals("unseen, in its place", View.INVISIBLE, Opened.visibility(true, true));
        assertEquals(View.GONE, Opened.visibility(false, false));
        assertEquals(View.GONE, Opened.visibility(false, true));
    }
}
