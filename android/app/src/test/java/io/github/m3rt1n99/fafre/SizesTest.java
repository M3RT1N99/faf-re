package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

/**
 * Sizes as the player reads them (Sizes): MB of 1,000,000 bytes and GB of 1,000 MB, as Android's storage settings count
 * them, rounded up, so that no line says less than a write needs or the player must free.
 */
public class SizesTest {
    private static final long GB = 1000 * Sizes.MB;

    /** The bytes that a figure of {@code size} says: 716,000,000 for "716 MB", 7,600,000,000 for "7.6 GB". */
    private static long said(String size) {
        String[] figure = size.split(" ");
        long unit = figure[1].equals("GB") ? GB : Sizes.MB;
        String[] parts = figure[0].split("\\.");
        long bytes = Long.parseLong(parts[0]) * unit;
        return parts.length == 1 ? bytes : bytes + Long.parseLong(parts[1]) * unit / 10;
    }

    /**
     * 3840's packages are 715,047,073 bytes, 715.05 MB as the storage settings count them, which the line rounds up; and the
     * 620 MB of 1,048,576 bytes that a NO_ROOM line asked the player to free were 650.1 MB there, 30 MB more than it said.
     */
    @Test
    public void aMbIsAMillionBytesAsAndroidsStorageSettingsCountThem() {
        assertEquals("716 MB", Sizes.of(715_047_073L));
        assertEquals("651 MB", Sizes.of(620L << 20));
        assertEquals("1 MB", Sizes.of(Sizes.MB));
        assertEquals("999 MB", Sizes.of(999 * Sizes.MB));
    }

    /** A byte over a figure is the next one: freeing what a line says, or downloading it, is never short. */
    @Test
    public void aSizeIsRoundedUpNeverDown() {
        assertEquals("1 MB", Sizes.of(1));
        assertEquals("2 MB", Sizes.of(Sizes.MB + 1));
        assertEquals("1.1 GB", Sizes.of(GB + 1));
        for (long bytes = 1; bytes < 30 * GB; bytes += bytes / 7 + 1) {
            for (long near : new long[] {bytes, bytes / Sizes.MB * Sizes.MB, bytes / Sizes.MB * Sizes.MB + 1}) {
                long said = said(Sizes.of(near));
                long step = said <= GB ? Sizes.MB : GB / 10;
                assertTrue(near + " bytes as " + Sizes.of(near), near <= said && said - near < step);
            }
        }
    }

    @Test
    public void from1000MbOnASizeReadsInGbWithATenth() {
        assertEquals("1.0 GB", Sizes.of(999 * Sizes.MB + 1));
        assertEquals("1.0 GB", Sizes.of(GB));
        assertEquals("7.6 GB", Sizes.of(7_563_000_000L));
        assertEquals("12.0 GB", Sizes.of(12 * GB));
    }

    /** What is left of a download never reads less than it is, and a download that has its whole reads whole. */
    @Test
    public void aDownloadsProgressHasWhatItHasRoundedDownAndItsWholeUp() {
        long env = 501_595_718L;
        assertEquals("0 of 502 MB", Sizes.progress(0, env));
        assertEquals("4 of 502 MB", Sizes.progress(4_999_999L, env));
        assertEquals("501 of 502 MB", Sizes.progress(env - 1, env));
        assertEquals("502 of 502 MB", Sizes.progress(env, env));
        assertEquals("the whole not said", "4 MB", Sizes.progress(4_999_999L, -1));
        assertEquals("1.2 of 2.5 GB", Sizes.progress(1_299_999_999L, 2_400_000_001L));
        assertEquals("1.2 GB", Sizes.progress(1_299_999_999L, -1));
    }
}
