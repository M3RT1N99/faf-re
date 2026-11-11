package io.github.m3rt1n99.fafre;

import android.content.Context;
import android.content.SharedPreferences;

/**
 * Which way fingers play a game (buildstage/touch-concept-v2.md): as the mouse the game was made for, or the touch
 * scheme "Griff", in which a finger pans the map, a tap is FAF's click by what it lands on, and a stroke from the
 * selection's handle gives its orders. The menus are the same in both.
 */
final class TouchScheme {
    static final int MOUSE = 0;
    static final int GRIFF = 1;

    private static final String PREFERENCES = "game";
    private static final String SCHEME = "touch";

    private TouchScheme() {
    }

    /**
     * What the user chose; the mouse until they choose. In the game's process it stays what that process read first: a choice
     * made in another process reaches it when it starts again.
     */
    static int of(Context context) {
        int scheme = preferences(context).getInt(SCHEME, MOUSE);
        return scheme == GRIFF ? GRIFF : MOUSE;
    }

    /**
     * Takes the other one, from the game's next start on: a game that runs, also in the back, keeps the touch controls it
     * started with (its process ends with it).
     */
    static void next(Context context) {
        // In another process the game reads what this wrote, so it has to be there when it starts.
        preferences(context).edit().putInt(SCHEME, of(context) == GRIFF ? MOUSE : GRIFF).commit();
    }

    /** The choice's name, for its button. */
    static String label(Context context) {
        return of(context) == GRIFF
            ? "Touch controls: Griff (a finger pans, a tap orders, strokes from the handle)"
            : "Touch controls: mouse (a finger boxes, a hold orders)";
    }

    private static SharedPreferences preferences(Context context) {
        return context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE);
    }
}
