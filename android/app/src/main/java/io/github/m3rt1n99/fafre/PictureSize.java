package io.github.m3rt1n99.fafre;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.DisplayMetrics;

/**
 * The size the game draws at, which is the size of its screen. Its UI has a fixed size in pixels,
 * made for a PC's screen, so on a phone's panel it comes out too small for a finger; fewer lines make
 * all of it larger, and the GPU has fewer pixels to fill. The compositor scales the picture to the
 * panel at no cost to the game (SurfaceHolder.setFixedSize).
 */
final class PictureSize {
    /** The least the game's screens are made for: the engine's least mode. */
    static final int LEAST_LINES = 768;
    static final int LEAST_WIDTH = 1024;
    /** As the panel, at least LEAST_LINES. */
    static final int AS_THE_SCREEN = 0;

    /** The sizes to choose from, by the lines of the picture's short side. */
    private static final int[] CHOICES = {768, 900, 1080, AS_THE_SCREEN};
    /** A screen this short is a phone's: its UI is small even at the least lines. */
    private static final double PHONE_INCHES = 4.5;

    private static final String PREFERENCES = "game";
    private static final String LINES = "lines";

    private PictureSize() {
    }

    /** The picture for a view of this size, with the view's shape: {width, height}. */
    static int[] of(int viewWidth, int viewHeight, int lines) {
        int shortSide = Math.min(viewWidth, viewHeight);
        int target = Math.max(lines == AS_THE_SCREEN ? shortSide : lines, LEAST_LINES);
        double scale = Math.max((double) target / shortSide, (double) LEAST_WIDTH / viewWidth);
        return new int[] {(int) Math.round(viewWidth * scale), (int) Math.round(viewHeight * scale)};
    }

    /** What the user chose, or what suits the screen. */
    static int lines(Context context) {
        DisplayMetrics metrics = context.getResources().getDisplayMetrics();
        double inches = Math.min(metrics.widthPixels / metrics.xdpi, metrics.heightPixels / metrics.ydpi);
        return preferences(context).getInt(LINES, inches < PHONE_INCHES ? 768 : 1080);
    }

    /** Takes the next size to choose from; the next game starts with it. */
    static void next(Context context) {
        int lines = lines(context);
        int at = 0;
        while (at < CHOICES.length && CHOICES[at] != lines) {
            ++at;
        }
        // In another process the game reads what this wrote, so it has to be there when it starts.
        preferences(context).edit().putInt(LINES, CHOICES[(at + 1) % CHOICES.length]).commit();
    }

    /** The choice's name, for its button. */
    static String label(Context context) {
        switch (lines(context)) {
            case 768:
                return "UI size: large (768 lines, fastest)";
            case 900:
                return "UI size: medium (900 lines)";
            case 1080:
                return "UI size: small (1080 lines)";
            default:
                return "UI size: as the screen (sharpest, slowest)";
        }
    }

    private static SharedPreferences preferences(Context context) {
        return context.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE);
    }
}
