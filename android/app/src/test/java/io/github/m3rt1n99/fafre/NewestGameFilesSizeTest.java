package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;

import org.junit.Test;

/** The size the metered question asks a yes to. */
public class NewestGameFilesSizeTest {
    /**
     * Every package whole and only the init file left (a stop at the last step, or an init file the app cannot use):
     * toDownload says 0, which the init file's 26,887 bytes would not change. The question asks no yes to "0 MB".
     */
    @Test
    public void lessThanAMegabyteIsNotZeroMegabytes() {
        assertNotEquals("0 MB", NewestGameFiles.size(0));
        assertNotEquals("0 MB", NewestGameFiles.size(26_887));
    }

    /** In MB of 1,048,576 bytes, as Android's data usage counts them (Settings formats data usage in IEC units). */
    @Test
    public void theSizeStaysInTheUnitsOfAndroidsDataUsage() {
        assertEquals("682 MB", NewestGameFiles.size(715_047_073L));
    }
}
