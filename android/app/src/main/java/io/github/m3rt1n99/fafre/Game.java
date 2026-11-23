package io.github.m3rt1n99.fafre;

/**
 * The game (libfafgame.so: the whole engine on SDL, android/native): what the activity and NetworkGameService tell the program
 * and ask of it (src/app/GameJniAndroid.cpp). The fingers are the program's own, on SDL's events (src/host/FingersSdl.cpp).
 */
final class Game {
    private static String sLoadError;

    static {
        try {
            System.loadLibrary("fafgame");
        } catch (UnsatisfiedLinkError e) {
            sLoadError = String.valueOf(e.getMessage());
        }
    }

    private Game() {
    }

    /** Null, or why the library did not load. */
    static String loadError() {
        return sLoadError;
    }

    /**
     * The command of SDL's message by which the program asks for a finger's buzz (src/app/GameJniAndroid.cpp, Buzz): the
     * second that SDL leaves to the app.
     */
    static final int BUZZ = 0x8001;

    /** The activity came to the front or went to the back. */
    static native void foreground(boolean front);

    /** The activity got the audio focus or lost it; without it the game's sound pauses. */
    static native void audioFocus(boolean held);

    /**
     * Whether a network lobby or game has its socket open, or a peer link of its own (native mode): not a skirmish, not
     * the LAN screen's search for games.
     */
    static native boolean networkInUse();

    /** Whether a game goes on behind the game's UI, as opposed to the menus and the score screen. */
    static native boolean gamePlaying();

    /** A line of the game's report, which also goes to the system log. */
    static native void report(String line);
}
