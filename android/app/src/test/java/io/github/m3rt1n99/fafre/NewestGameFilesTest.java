package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;

import org.junit.Test;

/** The Game card's line on FAF's newest version, and what its job does next. */
public class NewestGameFilesTest {
    /** 3840's packages on FAF's updater, 682 MB. */
    private static final long BYTES = 715_047_073L;

    private static String line(NewestGameFiles.Phase phase, int newest, long bytes, String note, int phone) {
        return new NewestGameFiles(phase, newest, bytes, note).line(phone);
    }

    @Test
    public void beforeACheckThePhonesNewestVersionOrPlainlyNone() {
        assertEquals("FAF game files: 3835", line(NewestGameFiles.Phase.IDLE, -1, -1, "", 3835));
        assertEquals("FAF game files: none on the phone yet", line(NewestGameFiles.Phase.IDLE, -1, -1, "", -1));
    }

    @Test
    public void checking() {
        assertEquals("FAF game files: 3835 (checking FAF's newest version…)", line(NewestGameFiles.Phase.CHECKING, -1, -1, "", 3835));
    }

    @Test
    public void upToDateOrNewerAvailable() {
        assertEquals("FAF game files: 3840 (newest at FAF: 3840), up to date", line(NewestGameFiles.Phase.KNOWN, 3840, -1, "", 3840));
        assertEquals("FAF game files: 3835 (newest at FAF: 3840)", line(NewestGameFiles.Phase.KNOWN, 3840, -1, "", 3835));
        assertEquals("FAF game files: none on the phone yet (newest at FAF: 3840)", line(NewestGameFiles.Phase.KNOWN, 3840, -1, "", -1));
    }

    /** A replay brought a version above FAF's newest (GitHub's latest moved back): the game starts the phone's. */
    @Test
    public void aNewerVersionOnThePhoneIsTheOneTheGameStarts() {
        assertEquals("FAF game files: 3841 (newest at FAF: 3840); the game starts 3841, as it is newer",
            line(NewestGameFiles.Phase.KNOWN, 3840, -1, "", 3841));
    }

    @Test
    public void theSizeComesFirst() {
        assertEquals("FAF game files: 3835 (newest at FAF: 3840)\n3840 is 682 MB from FAF; on a metered network the app asks first, "
            + "and the button asks again", line(NewestGameFiles.Phase.ASKING, 3840, BYTES, "", 3835));
        assertEquals("FAF game files: 3835 (newest at FAF: 3840)\nDownloading 3840 (682 MB): see Progress",
            line(NewestGameFiles.Phase.DOWNLOADING, 3840, BYTES, "", 3835));
        assertEquals("about 700 MB", NewestGameFiles.size(-1));
    }

    @Test
    public void aFailedCheckSaysWhyAndNamesNoVersion() {
        assertEquals("FAF game files: 3835\nNo check of FAF's newest version: GitHub is not reachable (timeout); is the phone online?",
            line(NewestGameFiles.Phase.FAILED, -1, -1, "GitHub is not reachable (timeout); is the phone online?", 3835));
    }

    @Test
    public void aDownloadThatStoppedGoesOnFromWhereItStopped() {
        assertEquals("FAF game files: 3835 (newest at FAF: 3840)\nGetting 3840 failed: HTTP 500 for units.3840.nx2; the button tries "
            + "again, from where it stopped", line(NewestGameFiles.Phase.FAILED, 3840, BYTES, "HTTP 500 for units.3840.nx2", 3835));
        assertEquals("FAF game files: 3835 (newest at FAF: 3840)\nGetting 3840 was cancelled; the button goes on from where it stopped",
            line(NewestGameFiles.Phase.CANCELLED, 3840, BYTES, "", 3835));
        assertEquals("FAF game files: 3835\nThe check was cancelled", line(NewestGameFiles.Phase.CANCELLED, -1, -1, "", 3835));
        assertEquals("FAF game files: 3835 (newest at FAF: 3840)\nGetting 3840 stopped, as the network became metered; the button goes "
            + "on from where it stopped, after a question on a metered network", line(NewestGameFiles.Phase.METERED, 3840, BYTES, "", 3835));
    }

    @Test
    public void aMeteredNetworkWaitsForAYesAndTheFilesOnThePhoneForNothing() {
        assertEquals(NewestGameFiles.Next.DOWNLOAD, NewestGameFiles.next(3835, 3840, false, false));
        assertEquals(NewestGameFiles.Next.ASK, NewestGameFiles.next(3835, 3840, true, false));
        assertEquals(NewestGameFiles.Next.ASK, NewestGameFiles.next(-1, 3840, true, false));
        assertEquals(NewestGameFiles.Next.DOWNLOAD, NewestGameFiles.next(3835, 3840, true, true));
        assertEquals(NewestGameFiles.Next.NOTHING, NewestGameFiles.next(3840, 3840, true, false));
        assertEquals(NewestGameFiles.Next.NOTHING, NewestGameFiles.next(3840, 3840, false, true));
    }

    /** The game starts the phone's newest version, so FAF's older one would be 700 MB for nothing, also after a yes. */
    @Test
    public void aNewerVersionOnThePhoneLeavesNothingToDownload() {
        assertEquals(NewestGameFiles.Next.NOTHING, NewestGameFiles.next(3841, 3840, false, false));
        assertEquals(NewestGameFiles.Next.NOTHING, NewestGameFiles.next(3841, 3840, true, true));
    }

    @Test
    public void theQuestionIsAskedOnceAndKeepsWhatItAsksAbout() {
        NewestGameFiles.asking(3840, BYTES);
        NewestGameFiles question = NewestGameFiles.question();
        assertSame(NewestGameFiles.now(), question);
        assertEquals(3840, question.newest);
        assertEquals(BYTES, question.bytes);
        assertNull("once", NewestGameFiles.question());

        NewestGameFiles.asking(3840, BYTES);
        NewestGameFiles.downloading(3840, BYTES);
        assertNull("a download asks nothing", NewestGameFiles.question());
    }

    /** The question's dialog went (Not now, Back, a screen made anew): while the card asks, the button asks again. */
    @Test
    public void whileTheCardAsksTheButtonAsksAgainWithWhatItKnows() {
        NewestGameFiles.asking(3840, BYTES);
        NewestGameFiles.question();

        NewestGameFiles pending = NewestGameFiles.pending();
        assertEquals(3840, pending.newest);
        assertEquals(BYTES, pending.bytes);
        assertSame("asked again, as often as the button is pressed", pending, NewestGameFiles.pending());

        NewestGameFiles.checking();
        assertNull("a check asks nothing yet", NewestGameFiles.pending());
        NewestGameFiles.asking(3840, BYTES);
        NewestGameFiles.downloading(3840, BYTES);
        assertNull("a download asks nothing", NewestGameFiles.pending());
        NewestGameFiles.asking(3840, BYTES);
        NewestGameFiles.stopped(NewestGameFiles.Phase.FAILED, "the phone has 286 MB free, and it needs 782 MB");
        assertNull("a job that ended asks nothing", NewestGameFiles.pending());
    }

    /** The room on the phone: the download and 100 MB to spare for the game's and the runs' files. */
    @Test
    public void aDownloadThatDoesNotFitSaysWhatItNeedsAndWhatIsFree() {
        assertEquals("the phone has 286 MB free, and it needs 782 MB", NewestGameFiles.noRoom(BYTES, 300_000_000L));
        assertNull(NewestGameFiles.noRoom(BYTES, 2_000_000_000L));
        assertNull("just fits", NewestGameFiles.noRoom(BYTES, BYTES + NewestGameFiles.SPARE_BYTES));
        assertNotNull("a byte short", NewestGameFiles.noRoom(BYTES, BYTES + NewestGameFiles.SPARE_BYTES - 1));
        assertEquals("the phone has less than 1 MB free, and it needs 100 MB", NewestGameFiles.noRoom(0, 1000));
    }

    /** A size the updater did not say counts as 750 MB, more than any version's so far. */
    @Test
    public void aSizeNotSaidIsTakenAsMoreThanAnyVersions() {
        assertEquals("the phone has 763 MB free, and it needs 850 MB", NewestGameFiles.noRoom(-1, 800_000_000L));
        assertNull(NewestGameFiles.noRoom(-1, 900_000_000L));
    }

    @Test
    public void aStopKeepsTheVersionAndItsSize() {
        NewestGameFiles.downloading(3840, BYTES);
        NewestGameFiles.stopped(NewestGameFiles.Phase.FAILED, "timeout");

        NewestGameFiles now = NewestGameFiles.now();
        assertEquals(NewestGameFiles.Phase.FAILED, now.phase);
        assertEquals(3840, now.newest);
        assertEquals(BYTES, now.bytes);
        assertEquals("timeout", now.note);
    }

    @Test
    public void aNewCheckForgetsTheLastOnesVersion() {
        NewestGameFiles.known(3840);
        NewestGameFiles.checking();

        assertEquals(-1, NewestGameFiles.now().newest);
    }
}
