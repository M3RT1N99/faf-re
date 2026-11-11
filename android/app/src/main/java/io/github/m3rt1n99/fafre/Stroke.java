package io.github.m3rt1n99.fafre;

/**
 * A finger's stroke from the selection's handle as a route's waypoints (touch scheme Griff, buildstage/touch-concept-v2.md):
 * about one every `step` along the straight, and every corner the finger drew, ending on the stroke's end; not its start.
 * The corners stay (Ramer-Douglas-Peucker within `tolerance`): far out, `step` is a long way on the map, and a route that cut a
 * corner would go elsewhere.
 */
final class Stroke {
    private Stroke() {
    }

    /**
     * Writes the waypoints of the first `count` points of xs/ys into outX/outY and returns how many. When out has too little room,
     * the last waypoint is the stroke's end all the same.
     */
    static int waypoints(float[] xs, float[] ys, int count, float step, float tolerance, float[] outX, float[] outY) {
        if (count < 2 || outX.length == 0) {
            return 0;
        }
        boolean[] keep = new boolean[count];
        keep[0] = true;
        keep[count - 1] = true;
        // Each split takes one segment off and puts two on: 2 + 2 * splits entries at most, and there are fewer splits than points.
        int[] stack = new int[2 * count + 4];
        int top = 0;
        stack[top++] = 0;
        stack[top++] = count - 1;
        while (top > 0) {
            int b = stack[--top];
            int a = stack[--top];
            if (b - a < 2) {
                continue;
            }
            double ax = xs[a];
            double ay = ys[a];
            double dx = xs[b] - ax;
            double dy = ys[b] - ay;
            double length = Math.hypot(dx, dy);
            int far = -1;
            double farDistance = tolerance;
            for (int i = a + 1; i < b; i++) {
                double px = xs[i] - ax;
                double py = ys[i] - ay;
                double distance = length > 0 ? Math.abs(px * dy - py * dx) / length : Math.hypot(px, py);
                if (distance > farDistance) {
                    farDistance = distance;
                    far = i;
                }
            }
            if (far >= 0) {
                keep[far] = true;
                stack[top++] = a;
                stack[top++] = far;
                stack[top++] = far;
                stack[top++] = b;
            }
        }
        int n = 0;
        int from = 0;
        for (int i = 1; i < count && n < outX.length; i++) {
            if (!keep[i]) {
                continue;
            }
            float ax = xs[from];
            float ay = ys[from];
            float bx = xs[i];
            float by = ys[i];
            int pieces = Math.max(1, Math.round((float) Math.hypot(bx - ax, by - ay) / step));
            for (int k = 1; k <= pieces && n < outX.length; k++) {
                outX[n] = ax + (bx - ax) * k / pieces;
                outY[n] = ay + (by - ay) * k / pieces;
                n++;
            }
            from = i;
        }
        outX[n - 1] = xs[count - 1];
        outY[n - 1] = ys[count - 1];
        return n;
    }
}
