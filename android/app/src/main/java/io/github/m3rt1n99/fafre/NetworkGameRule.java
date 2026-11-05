package io.github.m3rt1n99.fafre;

/**
 * When {@link NetworkGameService} runs: while a network lobby or game needs its socket, and not for long with nobody
 * there. It is asked for from the front, and from the moment the activity leaves it, which Android still allows; after
 * a refusal not again until the activity has come to the front anew. No Android in here, for the tests.
 */
final class NetworkGameRule {
    /** A game that stays over this long has ended: the flag is the last frame's, and a frame can see it otherwise. */
    static final long OVER_MILLISECONDS = 3000;
    /** So that a lobby left behind does not keep a phone awake: a player who is back later has lost the game anyway. */
    static final long BACK_LIMIT_MILLISECONDS = 30 * 60 * 1000L;

    private boolean mFront;
    private boolean mRunning;
    private boolean mRefused;
    private boolean mPlayed;
    private long mOverSince = -1;
    private long mBackSince = -1;

    void front(boolean front, long now) {
        mFront = front;
        mBackSince = front ? -1 : now;
        if (front) {
            mRefused = false;
        }
    }

    /**
     * Whether the network is still needed: the socket is open, and more than what is left of a game that has ended, as
     * the game over panel and the score screen hold the socket with nothing left to play. A lobby, and a game that is
     * still loading, have not played yet.
     */
    boolean needed(boolean socketOpen, boolean playing, long now) {
        if (!socketOpen) {
            mPlayed = false;
            mOverSince = -1;
            return false;
        }
        if (playing) {
            mPlayed = true;
            mOverSince = -1;
            return true;
        }
        if (!mPlayed) {
            return true;
        }
        if (mOverSince < 0) {
            mOverSince = now;
        }
        return now - mOverSince < OVER_MILLISECONDS;
    }

    boolean shouldStart(boolean needed) {
        return needed && mFront && !mRunning && !mRefused;
    }

    /** With the activity in the back for this long the running service stops, until it comes to the front anew. */
    boolean tooLongInTheBack(long now) {
        return mBackSince >= 0 && now - mBackSince >= BACK_LIMIT_MILLISECONDS;
    }

    void started() {
        mRunning = true;
    }

    void stopped() {
        mRunning = false;
    }

    /** Android said no, to the start or to the service. */
    void refused() {
        mRunning = false;
        mRefused = true;
    }
}
