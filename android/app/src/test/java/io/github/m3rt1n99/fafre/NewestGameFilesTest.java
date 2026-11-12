package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
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

    @Test
    public void theSizeComesFirst() {
        assertEquals("FAF game files: 3835 (newest at FAF: 3840)\n3840 is 682 MB from FAF; on a metered network the app asks first",
            line(NewestGameFiles.Phase.ASKING, 3840, BYTES, "", 3835));
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
        assertEquals(NewestGameFiles.Next.DOWNLOAD, NewestGameFiles.next(false, false, false));
        assertEquals(NewestGameFiles.Next.ASK, NewestGameFiles.next(false, true, false));
        assertEquals(NewestGameFiles.Next.DOWNLOAD, NewestGameFiles.next(false, true, true));
        assertEquals(NewestGameFiles.Next.NOTHING, NewestGameFiles.next(true, true, false));
        assertEquals(NewestGameFiles.Next.NOTHING, NewestGameFiles.next(true, false, true));
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
