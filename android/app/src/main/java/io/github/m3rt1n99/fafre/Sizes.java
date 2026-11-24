package io.github.m3rt1n99.fafre;

/**
 * Sizes as the player reads them: in MB of 1,000,000 bytes, and GB of 1,000 MB where a size comes to that, as Android's
 * own screens count storage from API 26 on, the app's least (Formatter.formatFileSize): what a line says the player must
 * free is what the storage settings count. Rounded up, so that a line never says less than a write needs or the player
 * must free; where a caller reads the bytes themselves (bytes_to_free), they stay exact. No Android in here, for the tests.
 */
final class Sizes {
    static final long MB = 1_000_000L;
    /** From 1,000 MB on a size reads in GB with a tenth: "7.6 GB", not "7563 MB". */
    private static final long TENTH_GB = 100 * MB;

    private Sizes() {
    }

    /** "716 MB", or from 1,000 MB on "7.6 GB": {@code bytes} rounded up to the figure. */
    static String of(long bytes) {
        long mb = up(bytes, MB);
        return mb < 1000 ? mb + " MB" : figure(up(bytes, TENTH_GB), true) + " GB";
    }

    /**
     * A download's progress, "4 of 502 MB", or from 1,000 MB of the whole on "1.2 of 2.4 GB": what it has rounded down and
     * the whole rounded up, so that what is left never reads less than it is, and once it has the whole, both alike; what
     * it has alone, "4 MB", when the server does not say the whole (-1).
     */
    static String progress(long done, long total) {
        boolean known = total >= 0;
        boolean gb = up(known ? total : done, MB) >= 1000;
        long unit = gb ? TENTH_GB : MB;
        String has = figure(known && done >= total ? up(total, unit) : done / unit, gb);
        return has + (known ? " of " + figure(up(total, unit), gb) : "") + (gb ? " GB" : " MB");
    }

    /** {@code bytes} in {@code unit}s, rounded up; none for none. */
    private static long up(long bytes, long unit) {
        return bytes <= 0 ? 0 : (bytes - 1) / unit + 1;
    }

    /** {@code units} as the figure says them: whole, or in tenths. */
    private static String figure(long units, boolean tenths) {
        return tenths ? units / 10 + "." + units % 10 : String.valueOf(units);
    }
}
