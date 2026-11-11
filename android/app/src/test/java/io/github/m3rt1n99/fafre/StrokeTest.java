package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;

import org.junit.Test;

/** A stroke's waypoints (touch scheme Griff). */
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
}
