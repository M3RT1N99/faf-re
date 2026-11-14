package io.github.m3rt1n99.fafre;

/**
 * A finger's stroke from the selection's handle (touch scheme Griff, buildstage/touch-concept-v2.md): what lifting it gives,
 * its waypoints, and what the screen says it gives.
 */
final class Stroke {
    /** What lifting the stroke gives (the mockup's classifyStroke): a route out, a patrol by a loop, or nothing. */
    static final int ROUTE = 0;
    static final int PATROL = 1;
    static final int CANCEL = 2;
    /** A stroke that ends this near its start is a loop: a patrol if it is longer than PATROL_MM, else taken back. */
    static final float LOOP_MM = 8;
    static final float PATROL_MM = 40;

    /**
     * What the stroke's end is on, as the program says (GriffAndroid.cpp StrokeAnswerTask): FAF's right click there, which the
     * world view decides by what the cursor is over, a panel or the minimap; 0 while it has not said.
     */
    static final int END_MOVE = 1;
    static final int END_ATTACK = 2;
    static final int END_GUARD = 3;
    static final int END_REPAIR = 4;
    static final int END_RECLAIM = 5;
    static final int END_CAPTURE = 6;
    static final int END_OTHER = 7;
    static final int END_PANEL = 8;
    static final int END_MINIMAP = 9;
    private static final int END_PLACE = 15;
    /**
     * With it: engineers are selected; FAF offers its patrol for the selection (not for factories alone); FAF's attack-move is
     * armed (its orders panel's button), whose waypoints the route gives.
     */
    static final int END_ENGINEERS = 16;
    static final int END_PATROL_OFFERED = 32;
    static final int END_ATTACK_MOVE = 64;

    /** What the screen says lifting gives, the mockup's labels (strokeMeaning) and FAF's own for what it decides otherwise. */
    static final int SAYS_ROUTE = 0;
    static final int SAYS_ATTACK_MOVE = 1;
    static final int SAYS_CANCEL = 2;
    static final int SAYS_MINIMAP = 3;
    static final int SAYS_PATROL = 4;
    static final int SAYS_ATTACK = 5;
    static final int SAYS_ASSIST = 6;
    static final int SAYS_GUARD = 7;
    static final int SAYS_RECLAIM = 8;
    static final int SAYS_CAPTURE = 9;
    static final int SAYS_ORDER = 10;

    private Stroke() {
    }

    /** The length of the first `count` points of xs/ys. */
    static double length(float[] xs, float[] ys, int count) {
        double length = 0;
        for (int i = 1; i < count; i++) {
            length += Math.hypot(xs[i] - xs[i - 1], ys[i] - ys[i - 1]);
        }
        return length;
    }

    /** ROUTE, PATROL or CANCEL for the first `count` points, `millimetre` pixels to a mm. */
    static int classify(float[] xs, float[] ys, int count, float millimetre) {
        if (count < 2) {
            return CANCEL;
        }
        if (Math.hypot(xs[count - 1] - xs[0], ys[count - 1] - ys[0]) >= LOOP_MM * millimetre) {
            return ROUTE;
        }
        return length(xs, ys, count) > PATROL_MM * millimetre ? PATROL : CANCEL;
    }

    /**
     * What lifting gives, in the mockup's order, by what the end is on (END_ and its flags, 0 unknown) and the stroke's mode: a
     * panel gives nothing, the minimap its place; then a loop; then FAF's order there: an attack, and with FAF's attack-move
     * armed an attack-move, assist with engineers or guard, reclaim; a plain move is the route. A loop is a patrol where FAF
     * offers one; for factories alone it is their rally route, as any stroke.
     */
    static int meaning(int end, int mode) {
        int place = end & END_PLACE;
        if (place == END_PANEL) {
            return SAYS_CANCEL;
        }
        if (place == END_MINIMAP) {
            return SAYS_MINIMAP;
        }
        if (mode == CANCEL) {
            return SAYS_CANCEL;
        }
        if (mode == PATROL && (place == 0 || (end & END_PATROL_OFFERED) != 0)) {
            return SAYS_PATROL;
        }
        if (place == END_ATTACK) {
            return SAYS_ATTACK;
        }
        if ((end & END_ATTACK_MOVE) != 0) {
            return SAYS_ATTACK_MOVE;
        }
        switch (place) {
            case END_GUARD:
            case END_REPAIR:
                return (end & END_ENGINEERS) != 0 ? SAYS_ASSIST : SAYS_GUARD;
            case END_RECLAIM:
                return SAYS_RECLAIM;
            case END_CAPTURE:
                return SAYS_CAPTURE;
            case END_OTHER:
                return SAYS_ORDER;
            default:
                return SAYS_ROUTE;
        }
    }

    /**
     * Writes the waypoints of the first `count` points of xs/ys into outX/outY and returns how many: about one every `step`
     * along the straight, and every corner the finger drew, ending on the stroke's end; not its start. The corners stay
     * (Ramer-Douglas-Peucker within `tolerance`): far out, `step` is a long way on the map, and a route that cut a corner would
     * go elsewhere. When out has too little room, the last waypoint is the stroke's end all the same.
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
