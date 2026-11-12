package io.github.m3rt1n99.fafre;

/**
 * The touch scheme Griff's covers (buildstage/griff-v3-spec.md T3.7): the app's own pieces over the game's picture, the rail's
 * buttons, the draft's tick and cross, and the undo's pill while it shows. A finger on one is its view's, never the game's;
 * the program keeps the handle clear of them and finds nothing of the game's under them for a gesture, as under FAF's panels
 * (Game.setGriffCovers). Each frame the activity gives every piece, shown or not, in the picture's pixels; the program hears
 * the shown ones when they changed.
 */
final class Covers {
    /** More pieces than the app has: the rail's three and its Keys, the tick, the cross, the pill. */
    static final int MOST = 16;

    private final int[] mBoxes = new int[4 * MOST];
    private int mCount;
    private final int[] mHeard = new int[4 * MOST];
    private int mHeardCount = -1;

    /** The pieces of a frame begin. */
    void begin() {
        mCount = 0;
    }

    /** A piece, in the picture's pixels: it covers while it is shown and has a size. */
    void piece(boolean shown, int left, int top, int right, int bottom) {
        if (!shown || right <= left || bottom <= top || mCount == MOST) {
            return;
        }
        int at = 4 * mCount++;
        mBoxes[at] = left;
        mBoxes[at + 1] = top;
        mBoxes[at + 2] = right;
        mBoxes[at + 3] = bottom;
    }

    /** The frame's pieces are given: whether they are not what the program heard last, which it is to hear now. */
    boolean end() {
        boolean same = mCount == mHeardCount;
        for (int at = 0; same && at < 4 * mCount; ++at) {
            same = mBoxes[at] == mHeard[at];
        }
        if (same) {
            return false;
        }
        System.arraycopy(mBoxes, 0, mHeard, 0, 4 * mCount);
        mHeardCount = mCount;
        return true;
    }

    /** The covers, `count()` boxes of left, top, right and bottom. */
    int[] boxes() {
        return mBoxes;
    }

    int count() {
        return mCount;
    }
}
