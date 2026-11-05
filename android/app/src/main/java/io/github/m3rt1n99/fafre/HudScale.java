package io.github.m3rt1n99.fafre;

import java.util.Locale;

/**
 * The factor of the game's HUD on this screen (android/TOUCH_UI.md). FAF draws its HUD in fixed pixels
 * and has a factor for them, its UI scale, which a session takes from here: the picture's lines over
 * the screen's height say how large a pixel is on the glass, and the factor makes a build icon, FAF's
 * 48 pixels, the size a finger hits.
 */
final class HudScale {
    /** The command line's switches, which FAF's Lua reads (assets/fafre_ui/lua/fafre/scale.lua): the factor, and the most that the user's UI scale can make of it. */
    static final String SWITCH = "/fafre_hud_scale";
    static final String MOST_SWITCH = "/fafre_hud_most";

    private static final double ICON_PIXELS = 48;
    private static final double ICON_MM = 7.3;
    /** FAF's lines the HUD and the app's buttons need: a larger factor would leave them fewer of the picture's. */
    private static final int HUD_LINES = 460;
    private static final double MM_PER_INCH = 25.4;
    /**
     * How much larger a phone's panel is than its logical screen, which runs small: the S22 Ultra's by a fifth, in all of
     * its resolutions. Without this the S22 Ultra in FHD+ at its panel's density would be wrong: 1080 pixels at 501 dpi make
     * 55 mm, which is nearer the logical 61 than the true 73 is. With it the choice holds for a panel from 1.0 to 1.3 times
     * the logical size at a mode of 3/4 of the largest, and from 0.8 to 1.5 times at 1/2.
     */
    private static final double PANEL_PER_LOGICAL = 1.15;
    /** FAF's pixels above and below the room for the app's buttons at the left edge (android/TOUCH_UI.md). */
    private static final double BAR_ROOM_TOP = 138;
    private static final double BAR_ROOM_BOTTOM = 230;

    private HudScale() {
    }

    /** The factor for a picture of `lines` lines on a screen whose short side is `shortSideMm`. */
    static double factor(int lines, double shortSideMm) {
        double wanted = ICON_MM / ICON_PIXELS * lines / shortSideMm;
        return Math.max(1.0, Math.min(most(lines), wanted));
    }

    /** The most a HUD on a picture of `lines` lines can have: a larger factor would leave it too few of FAF's lines. */
    static double most(int lines) {
        return Math.max(1.0, (double) lines / HUD_LINES);
    }

    /**
     * The screen's short side in mm. A phone set to a lower resolution, such as HD+, draws a smaller screen on its
     * panel: its active mode goes down with it, and the real density it reports may stay the panel's or follow the
     * mode. So the size is worked out from the active mode and from the largest one, and the one nearer what the logical
     * size, a rough one, says the panel is. Some phones report a density far from the truth, and then the logical size
     * is all there is.
     */
    static double shortSideMm(int activeModePixels, int largestModePixels, double realDpi, int logicalPixels, int logicalDpi) {
        double logical = logicalMm(logicalPixels, logicalDpi);
        double panel = logical * PANEL_PER_LOGICAL;
        double active = modeMm(activeModePixels, realDpi);
        double largest = modeMm(largestModePixels, realDpi);
        double real = Math.abs(active - panel) <= Math.abs(largest - panel) ? active : largest;
        return real >= logical / 2 && real <= logical * 2 ? real : logical;
    }

    /** What {@link #shortSideMm} took and what it had to choose from, for the report: a phone's own numbers are the only proof. */
    static String shortSideInWords(int activeModePixels, int largestModePixels, double realDpi, int logicalPixels, int logicalDpi) {
        double used = shortSideMm(activeModePixels, largestModePixels, realDpi, logicalPixels, logicalDpi);
        double active = modeMm(activeModePixels, realDpi);
        double largest = modeMm(largestModePixels, realDpi);
        double logical = logicalMm(logicalPixels, logicalDpi);
        String from = used == active ? "the active mode" : used == largest ? "the largest mode" : "the logical size";
        return String.format(Locale.ROOT, "%.1f mm from %s (active mode %d: %.1f, largest mode %d: %.1f, logical %d at %d dpi: %.1f; density %.0f)",
            used, from, activeModePixels, active, largestModePixels, largest, logicalPixels, logicalDpi, logical, realDpi);
    }

    private static double logicalMm(int pixels, int dpi) {
        return pixels * MM_PER_INCH / dpi;
    }

    private static double modeMm(int pixels, double dpi) {
        return dpi > 0 ? pixels * MM_PER_INCH / dpi : 0;
    }

    /** The top of the room the HUD leaves at the left edge, in the picture's pixels: below the multifunction panel. */
    static double barTopPixels(double factor) {
        return BAR_ROOM_TOP * factor;
    }

    /** How high the room is on a picture of this many lines: up to the unit view, which stands on the picture's bottom. */
    static double barRoomPixels(double factor, int lines) {
        return lines - (BAR_ROOM_TOP + BAR_ROOM_BOTTOM) * factor;
    }

    /** The factor as the switch gives it. */
    static String argument(double factor) {
        return String.format(Locale.ROOT, "%.2f", factor);
    }
}
