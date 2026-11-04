package io.github.m3rt1n99.fafre;

import android.view.Surface;

/**
 * The game (libfafgame.so: the whole engine, with its host android/native/src/host/GameHost.cpp).
 * The host runs the program as WinMain does on the PC, on a thread of its own, and turns the
 * surface, the input and the activity's lifecycle into what Windows gives the program: the screen,
 * mouse and key messages, and activation.
 */
final class Game {
    /** The mouse's buttons, the host's numbers. */
    static final int BUTTON_LEFT = 0;
    static final int BUTTON_RIGHT = 1;
    static final int BUTTON_MIDDLE = 2;
    static final int BUTTON_X1 = 3;
    static final int BUTTON_X2 = 4;

    /** WHEEL_DELTA: one notch of a mouse wheel. */
    static final int WHEEL_DELTA = 120;

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
     * Starts the program: `arguments` is its command line, the program's path first; `home` and
     * `temp` are its HOME and TMPDIR; the host's lines go to `reportPath`. False when it was
     * started already (one program per process, as on Windows) or cannot start. The process ends
     * with the program.
     */
    static native boolean start(String[] arguments, String home, String temp, String reportPath);

    /** Whether the program runs in this process: Android can make the activity anew around it. */
    static native boolean running();

    /** Whether a building's ghost waits to be placed. */
    static native boolean placingBuilding();

    /** A finger tapped near a building's ghost: it places the building, unless the game's UI is under the finger or the ghost. */
    static native void tapNearGhost(int fingerX, int fingerY, int ghostX, int ghostY);

    /** Deletes the queued order of the selected units nearest to the cursor, which is at this place, if one is near. */
    static native void deleteOrder(int x, int y);

    /** The surface and its size in pixels; null when it is gone. */
    static native void surface(Surface surface, int width, int height);

    /** The activity came to the front or went to the back. */
    static native void foreground(boolean front);

    /** The activity got the audio focus or lost it; without it the game's sound pauses. */
    static native void audioFocus(boolean held);

    /** The pointer's position in the surface's pixels. */
    static native void mouseMove(int x, int y);

    static native void mouseButton(int button, boolean down, int x, int y);

    /** A finger's button: as a mouse's, except that where a building's ghost waits, its press only puts the ghost there. */
    static native void touchButton(int button, boolean down, int x, int y);

    /** `delta` in WHEEL_DELTA units, positive away from the user. */
    static native void mouseWheel(int delta, int x, int y);

    /** A key went down or up: Android's key code, the event's scan code, and what it types (0: nothing). */
    static native void key(int keyCode, int scanCode, boolean down, int character);

    /** Text without a key, as an input method commits it. */
    static native void text(String text);
}
