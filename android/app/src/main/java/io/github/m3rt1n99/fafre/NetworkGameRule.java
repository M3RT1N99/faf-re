package io.github.m3rt1n99.fafre;

/**
 * When {@link NetworkGameService} runs: while a network lobby or game needs its socket, or a replay plays, and not for
 * long with nobody there. It is asked for from the front, and from the moment the activity leaves it, which Android still
 * allows; after a refusal not again until the activity has come to the front anew. No Android in here, for the tests.
 */
final class NetworkGameRule {
    /** A game that stays over this long has ended: the flag is the last frame's, and a frame can see it otherwise. */
    static final long OVER_MILLISECONDS = 3000;
    /**
     * So that a lobby or a replay left behind does not keep a phone busy: a player who is back later has lost the game
     * anyway, and the replay has played on without them.
     */
    static final long BACK_LIMIT_MILLISECONDS = 30 * 60 * 1000L;

    /** What the service keeps going in the back. */
    enum Need {
        /** Nothing: the service stops. */
        NONE,
        /**
         * A replay that plays: its process alone, which Android ends soon after the activity left the front otherwise
         * (the limit of cached processes). Nobody waits for it, so it lets the phone sleep with the screen off.
         */
        REPLAY,
        /** A network lobby or game, which the others wait for: the process, its network, and the CPU with the screen off. */
        NETWORK
    }

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
     * What is needed now: {@code socketOpen}, a network lobby or game has its socket open; {@code playing}, a game goes on
     * behind the game's UI; {@code replay}, the session the game plays is a replay's. The network comes first, as a replay
     * has none of its own; a replay is kept while it plays, not while it loads, which says nothing yet, nor once it is over.
     */
    Need need(boolean socketOpen, boolean playing, boolean replay, long now) {
        if (networkNeeded(socketOpen, playing, now)) {
            return Need.NETWORK;
        }
        return playing && replay ? Need.REPLAY : Need.NONE;
    }

    /**
     * Whether the network is still needed: the socket is open, and more than what is left of a game that has ended, as
     * the game over panel and the score screen hold the socket with nothing left to play. A lobby, and a game that is
     * still loading, have not played yet.
     */
    private boolean networkNeeded(boolean socketOpen, boolean playing, long now) {
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

    boolean shouldStart(Need need) {
        return need != Need.NONE && mFront && !mRunning && !mRefused;
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
