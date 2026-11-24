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
        assertEquals("less than 1 MB", NewestGameFiles.size(Sizes.MB - 1));
    }

    /**
     * In MB of 1,000,000 bytes, as every size line of the app and Android's storage settings (Sizes), not the 1,048,576
     * bytes of Android's data usage, in which 3840 is 682 MB; rounded up, so that the yes covers the whole download.
     */
    @Test
    public void theSizeIsInTheMbOfAndroidsStorageSettingsRoundedUp() {
        assertEquals("716 MB", NewestGameFiles.size(715_047_073L));
        assertEquals("1 MB", NewestGameFiles.size(Sizes.MB));
    }
}
