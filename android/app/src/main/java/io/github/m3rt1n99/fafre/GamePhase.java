package io.github.m3rt1n99.fafre;

import android.app.GameState;

/**
 * What the game tells Android of what it does (GameManager.setGameState, from Android 13; GameActivity's poll), from what the
 * app knows of the program: it loads until its first frame is on the screen, the start's preparation before the program
 * among that; then it is in its menus, or a game goes on behind the game's UI (Game.gamePlaying), one that cannot be
 * interrupted where its network is in use (Game.networkInUse), as the others play on. In its performance game mode Android
 * boosts a game that loads, for a few seconds at most; the rest is for the phone's game tools. One for the program, as an
 * activity that Android makes anew around it goes on from what the last one said. No Android in here but GameState's
 * constants, for the tests.
 */
final class GamePhase {
    /** What the program does, as GameState has it, with the report's words. */
    enum Phase {
        LOADING(true, GameState.MODE_NONE, "loading"),
        MENUS(false, GameState.MODE_NONE, "the menus"),
        GAME(false, GameState.MODE_GAMEPLAY_INTERRUPTIBLE, "a game"),
        NETWORK_GAME(false, GameState.MODE_GAMEPLAY_UNINTERRUPTIBLE, "a network game");

        final boolean loading;
        final int mode;
        final String words;

        Phase(boolean loading, int mode, String words) {
            this.loading = loading;
            this.mode = mode;
            this.words = words;
        }
    }

    /** The phase told last; null before the first. */
    private Phase mTold;

    /** Whether the program's first frame went to the screen (android/native/src/app/GameMainAndroid.cpp). */
    static native boolean firstFrame();

    /**
     * The program's phase: {@code shown} its first frame is on the screen, {@code playing} a game goes on behind its UI, {@code
     * network} a network lobby or game has its connections. Before the first frame nothing else counts: a launcher's game
     * connects while it loads.
     */
    static Phase of(boolean shown, boolean playing, boolean network) {
        if (!shown) {
            return Phase.LOADING;
        }
        if (!playing) {
            return Phase.MENUS;
        }
        return network ? Phase.NETWORK_GAME : Phase.GAME;
    }

    /** The phase to tell Android now, or null where it is the one told last. */
    Phase next(boolean shown, boolean playing, boolean network) {
        Phase phase = of(shown, playing, network);
        if (phase == mTold) {
            return null;
        }
        mTold = phase;
        return phase;
    }
}
