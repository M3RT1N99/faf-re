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

    /** {@link #listAnswer}: what is known of the list under the last finger. */
    static final int NO_LIST = 1;
    static final int LIST = 2;

    /** {@link #takeToShow}: why the activity is to bring these lines into view. */
    static final int FIELD_TAPPED = 1;
    static final int LINES_TO_SHOW = 3;

    private static String sLoadError;
    /** Who hears that the program has something new to read. */
    private static volatile Runnable sListener;

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

    /** `listener` hears, on the program's thread, that there is something new to read: a page, a list's answer, a text field. */
    static void listen(Runnable listener) {
        sListener = listener;
    }

    /** `listener` hears no more, unless another has taken its place, as a new activity does. */
    static void stopListening(Runnable listener) {
        if (sListener == listener) {
            sListener = null;
        }
    }

    /** The program, on its thread (src/host/MenuFingers.cpp). */
    @SuppressWarnings("unused")
    private static void fromProgram() {
        Runnable listener = sListener;
        if (listener != null) {
            listener.run();
        }
    }

    /** The page FAF's Lua has declared, in the picture's pixels: left, top, right, bottom. False when there is none. */
    static native boolean page(int[] rect);

    /** Whether fingers act as in the menus ({@link PageInput}) or as in a game ({@link TouchInput}), as FAF's Lua has declared. */
    static native boolean menuFingers();

    /** The UI factor of the session's HUD, as FAF's Lua has set it; 0 before any has. */
    static native float hudFactor();

    /** A finger came down here: what list would it scroll? {@link #listAnswer} says. */
    static native void lookForList(int x, int y);

    /** 0 until it is known, then NO_LIST or LIST. */
    static native int listAnswer();

    /** The last finger's drag in the picture's pixels, positive downwards: the list takes it, and what it cannot take comes back as {@link #takePageDrag}. */
    static native void scrollList(float drag);

    /** What no list took, since the last call. */
    static native float takePageDrag();

    /** The keyboard is coming: if a text field has the focus, {@link #takeToShow} says where. */
    static native void askForField();

    /**
     * Lines of the picture to bring into view, top and bottom, which the activity has not heard of yet: 0 when there are none, or
     * why: a text field (FIELD_TAPPED, or another for what {@link #askForField} found) or FAF's Lua (LINES_TO_SHOW).
     */
    static native int takeToShow(int[] place);

    /** A finger's press for a drag, the left button's, on the small control it meant if there is one within `reach`. */
    static native void pressNear(int x, int y, int reach, int smallSide);

    /** A finger's click of the right button in the menus, on the small control it meant if there is one within `reach`. */
    static native void rightClickNear(int x, int y, int reach, int smallSide);

    /** Whether a building's ghost waits to be placed. */
    static native boolean placingBuilding();

    /** A finger tapped near a building's ghost: it places the building, unless the game's UI is under the finger or the ghost. */
    static native void tapNearGhost(int fingerX, int fingerY, int ghostX, int ghostY);

    /** Gives up the line of buildings a finger is drawing, with nothing built. */
    static native void cancelLine();

    /** Whether a game goes on behind the game's UI, as opposed to the menus and the score screen. */
    static native boolean gamePlaying();

    /**
     * A finger's tap, a click of the left button, which hits the small control it meant if there is one within
     * `reach`; `smallSide`: what counts as small. Both in the picture's pixels. Where a ghost waits, it stays where it landed.
     */
    static native void tap(int x, int y, int reach, int smallSide);

    /**
     * No finger is on the glass: the pointer leaves what it is over, unless a building's ghost is with it over the world, or
     * {@link #pointerStay} comes before the game has got to this.
     */
    static native void pointerLeave();

    /** A finger came down, ahead of what it sends: a {@link #pointerLeave} that has not happened by now does not happen. */
    static native void pointerStay();

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
