package io.github.m3rt1n99.fafre;

import android.view.View;

/**
 * What the player opened of FAF's, which nothing of the touch scheme Griff's may cover (android/TOUCH_UI.md "Griff"): FAF's
 * windows, its popups, the dropouts of its multifunction panel and the content of its tabs, as griff.lua reports those that show
 * (fafre/opened.lua, Game.griffOpened), in the picture's pixels. Each of Griff's pieces over the picture, the rail's buttons, the
 * undo's pill and the draft's tick and cross, steps aside while one of them lies under it: it takes no finger and covers nothing
 * (Covers), and comes back where it was once that has gone. The overlay filters' list, which FAF opens right of its multifunction
 * panel, lies across the rail's row; the Menu's list, the map's options and each question box's shadow lie under the pill.
 */
final class Opened {
    /** More of FAF's controls open at once than a player opens, and their numbers. */
    static final int MOST = 16;
    static final int VALUES = 4 * MOST;

    private final float[] mBoxes = new float[VALUES];
    private int mCount;

    /** What the program gives now: `count` numbers of `values`, four a box, as many whole boxes as there are. Whether it changed. */
    boolean update(float[] values, int count) {
        int numbers = Math.max(0, Math.min(count, VALUES)) / 4 * 4;
        boolean same = numbers == mCount;
        for (int at = 0; same && at < numbers; ++at) {
            same = values[at] == mBoxes[at];
        }
        if (same) {
            return false;
        }
        System.arraycopy(values, 0, mBoxes, 0, numbers);
        mCount = numbers;
        return true;
    }

    /**
     * Whether something the player opened lies under a piece with this box, in the picture's pixels: they share more than an
     * edge, as a piece just beside it covers nothing of it, and one without a size covers nothing at all.
     */
    boolean under(float left, float top, float right, float bottom) {
        if (right <= left || bottom <= top) {
            return false;
        }
        for (int at = 0; at < mCount; at += 4) {
            if (left < mBoxes[at + 2] && mBoxes[at] < right && top < mBoxes[at + 3] && mBoxes[at + 1] < bottom) {
                return true;
            }
        }
        return false;
    }

    /**
     * How a piece shows: not at all where it is not wanted; where something the player opened lies under it, kept in its place
     * unseen, which takes no finger and covers nothing while the pieces beside it stay where they are; else seen.
     */
    static int visibility(boolean wanted, boolean aside) {
        return !wanted ? View.GONE : aside ? View.INVISIBLE : View.VISIBLE;
    }
}
