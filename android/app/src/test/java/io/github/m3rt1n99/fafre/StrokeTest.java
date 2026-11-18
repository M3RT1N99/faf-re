package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;

import java.util.Arrays;

import org.junit.Test;

/** A stroke's waypoints, what lifting it gives and what the screen says of it (touch scheme Griff). */
public class StrokeTest {
    private final float[] outX = new float[8];
    private final float[] outY = new float[8];

    private int waypoints(float[] points, float step) {
        float[] xs = new float[points.length / 2];
        float[] ys = new float[points.length / 2];
        for (int at = 0; at < xs.length; ++at) {
            xs[at] = points[2 * at];
            ys[at] = points[2 * at + 1];
        }
        return Stroke.waypoints(xs, ys, xs.length, step, 5, outX, outY);
    }

    @Test
    public void aboutOneEveryStepEndingOnTheEndWithoutTheStart() {
        assertEquals(3, waypoints(new float[] {0, 0, 50, 0, 100, 0}, 30));
        assertArrayEquals(new float[] {33.333f, 66.667f, 100}, new float[] {outX[0], outX[1], outX[2]}, 0.01f);
    }

    @Test
    public void aShortStrokeIsOnlyItsEnd() {
        assertEquals(1, waypoints(new float[] {0, 0, 10, 0}, 30));
        assertEquals(10, outX[0], 0);
    }

    @Test
    public void aCornerStaysAWaypointHoweverLongTheStep() {
        assertEquals(2, waypoints(new float[] {0, 0, 40, 0, 40, 40}, 300));
        assertEquals(40, outX[0], 0);
        assertEquals(0, outY[0], 0);
        assertEquals(40, outY[1], 0);
    }

    @Test
    public void aWobbleWithinTheToleranceIsNoCorner() {
        assertEquals(1, waypoints(new float[] {0, 0, 50, 3, 100, 0}, 300));
        assertEquals(100, outX[0], 0);
    }

    @Test
    public void tooLittleRoomStillEndsOnTheEnd() {
        assertEquals(8, waypoints(new float[] {0, 0, 1000, 0}, 10));
        assertEquals(1000, outX[7], 0);
    }

    @Test
    public void noStrokeNoWaypoints() {
        assertEquals(0, waypoints(new float[] {5, 5}, 30));
    }

    /** A stroke of these points, a mm 10 pixels, classified. */
    private static int classify(float... points) {
        float[] xs = new float[points.length / 2];
        float[] ys = new float[points.length / 2];
        for (int at = 0; at < xs.length; ++at) {
            xs[at] = points[2 * at];
            ys[at] = points[2 * at + 1];
        }
        return Stroke.classify(xs, ys, xs.length, 10);
    }

    @Test
    public void aStrokeThatEndsEightMillimetresAwayOrMoreIsARoute() {
        assertEquals(Stroke.ROUTE, classify(0, 0, 80, 0));
        assertEquals(Stroke.ROUTE, classify(0, 0, 300, 0, 300, 300, 0, 80));
    }

    @Test
    public void aLoopLongerThanFortyMillimetresIsAPatrol() {
        // 40.2 mm out and back, ending 7.9 mm from its start; 39.2 mm is a stroke taken back.
        assertEquals(Stroke.PATROL, classify(0, 0, 240, 0, 240, 1, 79, 0));
        assertEquals(Stroke.CANCEL, classify(0, 0, 235, 0, 235, 1, 79, 0));
    }

    @Test
    public void aStrokeOfOnePointGivesNothing() {
        assertEquals(Stroke.CANCEL, classify(5, 5));
    }

    @Test
    public void theLabelSaysWhatTheEndIsOnInTheMockupsOrder() {
        int engineers = Stroke.END_ENGINEERS;
        int attackMove = Stroke.END_ATTACK_MOVE;
        // A panel or the minimap first, whatever the stroke is.
        assertEquals(Stroke.SAYS_CANCEL, Stroke.meaning(Stroke.END_PANEL, Stroke.PATROL));
        assertEquals(Stroke.SAYS_MINIMAP, Stroke.meaning(Stroke.END_MINIMAP | attackMove, Stroke.CANCEL));
        // Then a loop: taken back, or a patrol where FAF offers one; not yet known, a patrol too.
        assertEquals(Stroke.SAYS_CANCEL, Stroke.meaning(Stroke.END_ATTACK, Stroke.CANCEL));
        assertEquals(Stroke.SAYS_PATROL, Stroke.meaning(Stroke.END_MOVE | Stroke.END_PATROL_OFFERED, Stroke.PATROL));
        assertEquals(Stroke.SAYS_PATROL, Stroke.meaning(0, Stroke.PATROL));
        // For factories alone a loop is their rally route.
        assertEquals(Stroke.SAYS_ROUTE, Stroke.meaning(Stroke.END_MOVE, Stroke.PATROL));
        // Then FAF's order there: an attack before the attack-move FAF's button armed, and that before an assist.
        assertEquals(Stroke.SAYS_ATTACK, Stroke.meaning(Stroke.END_ATTACK | attackMove, Stroke.ROUTE));
        assertEquals(Stroke.SAYS_ATTACK_MOVE, Stroke.meaning(Stroke.END_GUARD | engineers | attackMove, Stroke.ROUTE));
        assertEquals(Stroke.SAYS_ATTACK_MOVE, Stroke.meaning(Stroke.END_MOVE | attackMove, Stroke.ROUTE));
        assertEquals(Stroke.SAYS_ASSIST, Stroke.meaning(Stroke.END_GUARD | engineers, Stroke.ROUTE));
        assertEquals(Stroke.SAYS_ASSIST, Stroke.meaning(Stroke.END_REPAIR | engineers, Stroke.ROUTE));
        assertEquals(Stroke.SAYS_GUARD, Stroke.meaning(Stroke.END_GUARD, Stroke.ROUTE));
        assertEquals(Stroke.SAYS_RECLAIM, Stroke.meaning(Stroke.END_RECLAIM | engineers, Stroke.ROUTE));
        assertEquals(Stroke.SAYS_CAPTURE, Stroke.meaning(Stroke.END_CAPTURE | engineers, Stroke.ROUTE));
        assertEquals(Stroke.SAYS_ORDER, Stroke.meaning(Stroke.END_OTHER, Stroke.ROUTE));
        assertEquals(Stroke.SAYS_ROUTE, Stroke.meaning(Stroke.END_MOVE | engineers, Stroke.ROUTE));
        assertEquals(Stroke.SAYS_ROUTE, Stroke.meaning(0, Stroke.ROUTE));
    }

    @Test
    public void theHandlesRestSaysFafsDistributeOrdersWhateverItsEndIsOn() {
        // The finger is on the handle, over the map beside the group: what FAF's right click there would order counts for nothing.
        assertEquals(Stroke.SAYS_DISTRIBUTE, Stroke.meaning(0, Stroke.DISTRIBUTE));
        assertEquals(Stroke.SAYS_DISTRIBUTE, Stroke.meaning(Stroke.END_MOVE | Stroke.END_ATTACK_MOVE, Stroke.DISTRIBUTE));
        assertEquals(Stroke.SAYS_DISTRIBUTE, Stroke.meaning(Stroke.END_PANEL, Stroke.DISTRIBUTE));
        // No stroke's shape gives it.
        assertEquals(Stroke.CANCEL, classify(0, 0, 5, 0));
    }

    @Test
    public void theLengthIsAlongThePoints() {
        float[] xs = {0, 30, 30};
        float[] ys = {0, 0, 40};
        assertEquals(70, Stroke.length(xs, ys, 3), 1e-9);
        assertEquals(30, Stroke.length(Arrays.copyOf(xs, 2), Arrays.copyOf(ys, 2), 2), 1e-9);
    }
}
