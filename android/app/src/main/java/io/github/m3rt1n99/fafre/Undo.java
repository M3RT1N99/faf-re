package io.github.m3rt1n99.fafre;

/**
 * The touch scheme Griff's undo as its pill shows it (buildstage/griff-v3-spec.md T3.1, the mockup's armUndo and doUndo): for
 * 3 s after a gesture ordered, a tap on the pill takes the gesture's orders back. One level: a new order's record replaces
 * the last, and any other order ends it (the program says so as a record 0). The program holds the record (GriffAndroid.cpp,
 * include/GriffUndo.h) and says when one is made (Game.SAID_ORDERED) and when it was taken back (Game.SAID_UNDONE), which
 * takes as long as the sim takes to take the gesture and FAF's clicks that take its orders back run. Between the tap and then
 * the pill says it waits, also past its 3 s.
 */
final class Undo {
    /** How long the pill stays after an order. */
    static final long MILLISECONDS = 3000;

    /** The record (0: none), when it was made, and whether the pill was tapped for it. */
    private int mRecord;
    private long mOrderedAt;
    private boolean mTapped;

    /**
     * A gesture ordered at `time`: its record `record` is the pill's from now on. The tap ladder's attack-move in place of a move
     * tells the move's record again: its time goes on.
     */
    void ordered(int record, long time) {
        if (record != mRecord) {
            mRecord = record;
            mOrderedAt = time;
            mTapped = false;
        }
    }

    /** Whether the pill shows at `time`: for its 3 s, and while a tap on it waits for the program. */
    boolean showing(long time) {
        return mRecord != 0 && (mTapped || time - mOrderedAt < MILLISECONDS);
    }

    /** How much of the pill's time is left at `time`, from 1 to 0: the bar at its bottom. */
    float left(long time) {
        return Math.max(0, Math.min(1, (float) (MILLISECONDS - (time - mOrderedAt)) / MILLISECONDS));
    }

    /** Whether the pill says it waits: tapped, and its record not taken back yet. */
    boolean waiting() {
        return mTapped;
    }

    /** The pill was tapped at `time`: the record to take back, or 0 where there is none (gone, or tapped already). */
    int tap(long time) {
        if (!showing(time) || mTapped) {
            return 0;
        }
        mTapped = true;
        return mRecord;
    }

    /** The program took record `record` back: the pill goes. */
    void undone(int record) {
        if (record == mRecord) {
            mRecord = 0;
            mTapped = false;
        }
    }

    /** No game any more: no pill. */
    void clear() {
        mRecord = 0;
        mTapped = false;
    }
}
