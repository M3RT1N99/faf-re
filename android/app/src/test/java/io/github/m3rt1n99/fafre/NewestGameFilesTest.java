package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertThrows;

import java.io.IOException;

import org.junit.Test;

/** The Game card's line on FAF's newest version, and what its job does next. */
public class NewestGameFilesTest {
    /** 3840's packages on FAF's updater, 716 MB. */
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
        assertEquals("FAF game files: 3835 (newest at FAF: 3840)\n3840 is 716 MB from FAF; on a metered network the app asks first, "
            + "and the button asks again", line(NewestGameFiles.Phase.ASKING, 3840, BYTES, "", 3835));
        assertEquals("FAF game files: 3835 (newest at FAF: 3840)\nDownloading 3840 (716 MB): see Progress",
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
            + "again, keeping anything downloaded so far", line(NewestGameFiles.Phase.FAILED, 3840, BYTES, "HTTP 500 for units.3840.nx2", 3835));
        assertEquals("FAF game files: 3835 (newest at FAF: 3840)\nGetting 3840 was cancelled; the button goes on from where it stopped",
            line(NewestGameFiles.Phase.CANCELLED, 3840, BYTES, "", 3835));
        assertEquals("FAF game files: 3835\nThe check was cancelled", line(NewestGameFiles.Phase.CANCELLED, -1, -1, "", 3835));
        assertEquals("FAF game files: 3835 (newest at FAF: 3840)\nGetting 3840 stopped, as the network became metered; the button goes "
            + "on from where it stopped, after a question on a metered network", line(NewestGameFiles.Phase.METERED, 3840, BYTES, "", 3835));
    }

    /** Too little room stops the job before a download begins, on the same line as a download that failed: it promises none. */
    @Test
    public void tooLittleRoomPromisesNoDownloadThatBegan() {
        Room.NoRoom noRoom = new Room.NoRoom("FAF game version 3840", BYTES + Room.SPARE_BYTES, 300_000_000L);
        assertEquals("FAF game files: 3835 (newest at FAF: 3840)\nGetting 3840 failed: it needs 816 MB: free 516 MB more on the "
            + "phone; the button tries again, keeping anything downloaded so far",
            line(NewestGameFiles.Phase.FAILED, 3840, BYTES, noRoom.line("it"), 3835));
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
        NewestGameFiles.stopped(NewestGameFiles.Phase.FAILED, "it needs 816 MB: free 516 MB more on the phone");
        assertNull("a job that ended asks nothing", NewestGameFiles.pending());
    }

    /** A size the updater did not say counts as 750 MB in the room, more than any version's so far. */
    @Test
    public void aSizeNotSaidIsTakenAsMoreThanAnyVersions() throws IOException {
        assertEquals(NewestGameFiles.UNSAID_BYTES, NewestGameFiles.counted(-1));
        assertEquals(BYTES, NewestGameFiles.counted(BYTES));
        assertEquals(0, NewestGameFiles.counted(0));
        Room.NoRoom noRoom = assertThrows(Room.NoRoom.class,
            () -> new RoomTest.Fake(800_000_000L).check("FAF game version 3841", NewestGameFiles.counted(-1)));
        assertEquals("FAF game version 3841 needs 850 MB: free 50 MB more on the phone", noRoom.getMessage());
        RoomTest.Fake fits = new RoomTest.Fake(NewestGameFiles.UNSAID_BYTES + Room.SPARE_BYTES);
        fits.check("FAF game version 3841", NewestGameFiles.counted(-1));
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
