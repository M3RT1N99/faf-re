package io.github.m3rt1n99.fafre;

/**
 * The touch scheme Griff's covers (buildstage/griff-v3-spec.md T3.7): the app's own pieces over the game's picture, the rail's
 * buttons, the draft's tick and cross, and the undo's pill while it shows and a short while after it went (Lingering). A finger
 * on one is its view's, never the game's; the program keeps the handle clear of them and finds nothing of the game's under them
 * for a gesture, as under FAF's panels (Game.setGriffCovers). Each frame the activity gives every piece, shown or not, in the
 * picture's pixels; the program hears the covering ones when they changed.
 */
final class Covers {
    /**
     * A piece whose cover stays a short while after it went, where it last showed: the undo's pill, which goes when its time
     * runs out, maybe under a finger already on its way to it. A tap meant for it that comes late then lands on nothing, never
     * on the map beneath. The program finds a tap's place covered when it clicks, a settle after the finger's down
     * (TouchInput.SETTLE_MILLISECONDS), so the time counts from the piece's last frame to that click.
     */
    static final class Lingering {
        private final long mAfter;
        private boolean mShownOnce;
        private long mShownAt;
        private final int[] mBox = new int[4];

        /** A piece that covers for `after` milliseconds past its last frame shown. */
        Lingering(long after) {
            mAfter = after;
        }

        /** The piece's frame at `time`: whether it covers, at the box of its last frame shown (box()). */
        boolean covers(boolean shown, int left, int top, int right, int bottom, long time) {
            if (shown && right > left && bottom > top) {
                mShownOnce = true;
                mShownAt = time;
                mBox[0] = left;
                mBox[1] = top;
                mBox[2] = right;
                mBox[3] = bottom;
            }
            return mShownOnce && time - mShownAt < mAfter;
        }

        /** Where it last showed: left, top, right and bottom. */
        int[] box() {
            return mBox;
        }
    }

    /** More pieces than the app has: the rail's three, the tick, the cross, the pill. */
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

    /** A piece that lingers, at `time`: it covers while it is shown and a while after, where it last showed. */
    void piece(Lingering lingering, boolean shown, int left, int top, int right, int bottom, long time) {
        int[] box = lingering.box();
        piece(lingering.covers(shown, left, top, right, bottom, time), box[0], box[1], box[2], box[3]);
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
