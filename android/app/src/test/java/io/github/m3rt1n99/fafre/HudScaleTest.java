package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import java.util.Locale;

import org.junit.Test;

/** The HUD's factor for a screen and a picture. */
public class HudScaleTest {
    @Test
    public void theMostTheUserCanHaveLeavesTheHudItsLines() {
        assertEquals(768.0 / 460, HudScale.most(768), 0.001);
        assertEquals(1080.0 / 460, HudScale.most(1080), 0.001);
        assertEquals(1.0, HudScale.most(400), 0);
    }

    private static final double S22_ULTRA_MM = 73;

    @Test
    public void theGalaxyS22UltraAtLeastLinesIsWhatTheCensusMeasured() {
        assertEquals(1.6, HudScale.factor(768, S22_ULTRA_MM), 0.001);
    }

    @Test
    public void aBuildIconHasTheSameSizeAtEveryPictureTheAppOffers() {
        for (int lines : new int[] {768, 900, 1080, 1440}) {
            double factor = HudScale.factor(lines, S22_ULTRA_MM);
            double iconMm = 48 * factor * S22_ULTRA_MM / lines;
            assertEquals("at " + lines + " lines", 7.3, iconMm, 0.01);
        }
    }

    @Test
    public void aLargeScreenStaysAtFafsOwnSize() {
        assertEquals(1.0, HudScale.factor(768, 150), 0);
        assertEquals(1.0, HudScale.factor(1080, 250), 0);
    }

    @Test
    public void aSmallScreenKeepsTheLinesTheHudNeeds() {
        for (int lines : new int[] {768, 900, 1080, 1440}) {
            double factor = HudScale.factor(lines, 40);
            assertTrue("at " + lines + " lines", lines / factor >= 460 - 0.01);
        }
    }

    @Test
    public void thereIsAlwaysAFactorOfAtLeastOne() {
        assertEquals(1.0, HudScale.factor(400, 40), 0);
    }

    // The S22 Ultra in HD+ (android/CLIENT_PLAN.md: the display mode was 720x1544): a logical 720 at 300 dpi, its largest mode 1440.

    @Test
    public void aPhoneInHdPlusThatKeepsItsPanelsDensityIsAsLargeAsItsPanel() {
        assertEquals(73.0, HudScale.shortSideMm(720, 1440, 501, 720, 300), 0.1);
    }

    @Test
    public void aPhoneInHdPlusWhoseDensityFollowsTheModeIsAsLargeAsItsPanel() {
        assertEquals(73.0, HudScale.shortSideMm(720, 1440, 250, 720, 300), 0.3);
    }

    @Test
    public void aPhoneInItsLargestModeIsAsLargeAsItsPanel() {
        assertEquals(73.0, HudScale.shortSideMm(1440, 1440, 501, 720, 300), 0.1);
    }

    @Test
    public void aPhoneInFhdPlusIsAsLargeAsItsPanelWhateverItsDensityDoes() {
        // The same panel at a logical 1080 at 450 dpi: 55 mm from the active mode at the panel's density is nearer 61.
        assertEquals(73.0, HudScale.shortSideMm(1080, 1440, 501, 1080, 450), 0.1);
        assertEquals(73.0, HudScale.shortSideMm(1080, 1440, 375.75, 1080, 450), 0.1);
    }

    @Test
    public void aPhoneThatListsOnlyItsActiveModeIsWhatItSays() {
        assertEquals(62.3, HudScale.shortSideMm(1080, 1080, 440, 1080, 440), 0.1);
    }

    @Test
    public void aDensityFarFromTheTruthGivesWayToTheLogicalOne() {
        assertEquals(62.3, HudScale.shortSideMm(1080, 1080, 160, 1080, 440), 0.1);
        assertEquals(62.3, HudScale.shortSideMm(1080, 1080, 0, 1080, 440), 0.1);
        assertEquals(62.3, HudScale.shortSideMm(1080, 1080, 2000, 1080, 440), 0.1);
        assertEquals(62.3, HudScale.shortSideMm(1080, 2160, 5000, 1080, 440), 0.1);
    }

    @Test
    public void theReportSaysWhatWasUsedAndWhatThereWas() {
        assertEquals("73.0 mm from the largest mode (active mode 720: 36.5, largest mode 1440: 73.0, logical 720 at 300 dpi: 61.0; density 501)",
            HudScale.shortSideInWords(720, 1440, 501, 720, 300));
        assertTrue(HudScale.shortSideInWords(720, 1440, 250, 720, 300).startsWith("73.2 mm from the active mode ("));
        assertTrue(HudScale.shortSideInWords(1080, 1080, 0, 1080, 440).startsWith("62.3 mm from the logical size ("));
    }

    @Test
    public void theButtonsHaveRoomBetweenTheMultifunctionPanelAndTheUnitView() {
        // What the census measured at 1.6: the panel ends at 218, the view begins at 402.
        assertEquals(220.8, HudScale.barTopPixels(1.6), 0.01);
        assertEquals(179.2, HudScale.barRoomPixels(1.6, 768), 0.01);
    }

    @Test
    public void theRoomIsTheSameOnTheGlassAtEveryPictureTheAppOffers() {
        double mmAt768 = HudScale.barRoomPixels(HudScale.factor(768, S22_ULTRA_MM), 768) * S22_ULTRA_MM / 768;
        for (int lines : new int[] {900, 1080, 1440}) {
            double mm = HudScale.barRoomPixels(HudScale.factor(lines, S22_ULTRA_MM), lines) * S22_ULTRA_MM / lines;
            assertEquals("at " + lines + " lines", mmAt768, mm, 0.01);
        }
    }

    @Test
    public void theSwitchHasTwoDecimalsWhateverTheLanguage() {
        Locale language = Locale.getDefault();
        Locale.setDefault(Locale.GERMANY);
        try {
            assertEquals("1.60", HudScale.argument(1.6000000000000001));
            assertEquals("1.67", HudScale.argument(1.6695));
        } finally {
            Locale.setDefault(language);
        }
    }
}
