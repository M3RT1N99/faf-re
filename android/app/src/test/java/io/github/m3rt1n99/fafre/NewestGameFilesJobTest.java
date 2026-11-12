package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import java.io.File;
import java.io.IOException;
import java.net.UnknownHostException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

/** The Game card's job on each of its paths, with GitHub, FAF's updater, the network and the phone as each test sets them up. */
public class NewestGameFilesJobTest {
    /** 3840's packages on FAF's updater, as it had them (mods not published), 682 MB. */
    private static final Map<String, Long> LENGTHS = new HashMap<>();
    private static final long BYTES = 715_047_073L;

    static {
        LENGTHS.put("effects", 3162052L);
        LENGTHS.put("env", 501595718L);
        LENGTHS.put("etc", 3837L);
        LENGTHS.put("loc", 1765808L);
        LENGTHS.put("lua", 3435946L);
        LENGTHS.put("meshes", 3144266L);
        LENGTHS.put("projectiles", 923326L);
        LENGTHS.put("textures", 91406892L);
        LENGTHS.put("units", 109609228L);
    }

    @Rule
    public final TemporaryFolder folder = new TemporaryFolder();

    private Storage mStorage;
    private final Network mNetwork = new Network();
    private final Connectivity mConnectivity = new Connectivity();
    private final Download mDownload = new Download();

    @Before
    public void setUp() {
        Cancellation.reset();
        mStorage = new Storage(folder.getRoot());
    }

    @After
    public void noCancelLeft() {
        Cancellation.reset();
    }

    /** GitHub, whose latest release is {@code latest}, and FAF's updater, which has every version's init file. */
    private final class Network implements Http.Requests {
        String latest = "3840";
        IOException gitHubFails;
        /** The package whose HEAD the user cancels while it runs, which Http.ONE_TRY makes a Cancelled. */
        String cancelledHead;
        /** The phone's network becomes metered while the job measures. */
        boolean meteredWhileMeasuring;
        final List<String> asked = new ArrayList<>();
        /** The card's phase at each HEAD of the measurement. */
        final List<NewestGameFiles.Phase> phases = new ArrayList<>();

        @Override
        public String text(String url) throws IOException {
            asked.add(url);
            if (gitHubFails != null) {
                throw gitHubFails;
            }
            assertEquals(FafRelease.LATEST_URL, url);
            return "{\"tag_name\":\"" + latest + "\"}";
        }

        @Override
        public long length(String url) throws IOException {
            asked.add(url);
            String name = url.substring(Http.GAME_URL.length());
            if (name.startsWith("init_faf_")) {
                return 26887;
            }
            phases.add(NewestGameFiles.now().phase);
            if (meteredWhileMeasuring) {
                mConnectivity.metered = true;
            }
            String nx2 = name.substring(0, name.indexOf('.'));
            if (nx2.equals(cancelledHead)) {
                Cancellation.request();
                throw new Cancellation.Cancelled();
            }
            if (!LENGTHS.containsKey(nx2)) {
                throw new Http.StatusException(url, 404);
            }
            return LENGTHS.get(nx2);
        }
    }

    /** The phone's network: metered or not, and the watch on it. */
    private static final class Connectivity implements NewestGameFilesJob.Connectivity {
        boolean metered;
        /** The open watch's, or null. */
        Runnable onMetered;
        int watches;
        int closed;

        @Override
        public boolean metered() {
            return metered;
        }

        @Override
        public NewestGameFilesJob.Watch watch(Runnable onMetered) {
            assertNull("one watch at a time", this.onMetered);
            ++watches;
            this.onMetered = onMetered;
            if (metered) {
                // As Android's default-network callback starts with the network as it is.
                onMetered.run();
            }
            return () -> {
                ++closed;
                this.onMetered = null;
            };
        }

        void becomeMetered() {
            metered = true;
            if (onMetered != null) {
                onMetered.run();
            }
        }
    }

    private interface Step {
        void run() throws IOException;
    }

    /** The phone's side: its free bytes, and the download, which checks the Cancel at its start as ensure does. */
    private final class Download implements NewestGameFilesJob.Download {
        long room = 4_000_000_000L;
        /** What the download meets once it has started. */
        Step during;
        final List<Integer> versions = new ArrayList<>();

        @Override
        public long room() {
            return room;
        }

        @Override
        public void files(int version) throws IOException {
            versions.add(version);
            Cancellation.check();
            if (during != null) {
                during.run();
            }
            GameFilesTest.makeReady(mStorage, version);
        }
    }

    private void run(int yesTo) throws IOException {
        NewestGameFilesJob.run(mStorage, yesTo, mNetwork, mConnectivity, mDownload);
    }

    private List<String> heads() {
        List<String> heads = new ArrayList<>();
        for (String url : mNetwork.asked) {
            if (url.endsWith(".nx2")) {
                heads.add(url);
            }
        }
        return heads;
    }

    private static void assertState(NewestGameFiles.Phase phase, int newest) {
        NewestGameFiles now = NewestGameFiles.now();
        assertEquals(phase, now.phase);
        assertEquals(newest, now.newest);
    }

    @Test
    public void aCheckThatFailsMeasuresAndDownloadsNothing() throws IOException {
        mNetwork.gitHubFails = new UnknownHostException("Unable to resolve host \"api.github.com\"");

        run(-1);

        assertState(NewestGameFiles.Phase.FAILED, -1);
        assertEquals("GitHub is not reachable (Unable to resolve host \"api.github.com\"); is the phone online?",
            NewestGameFiles.now().note);
        assertEquals(Collections.singletonList(FafRelease.LATEST_URL), mNetwork.asked);
        assertEquals(0, mConnectivity.watches);
        assertEquals(Collections.emptyList(), mDownload.versions);
    }

    @Test
    public void aVersionThePhoneHasIsNeitherMeasuredNorDownloaded() throws IOException {
        GameFilesTest.makeReady(mStorage, 3840);

        run(-1);

        assertState(NewestGameFiles.Phase.KNOWN, 3840);
        assertEquals(Collections.emptyList(), heads());
        assertEquals(0, mConnectivity.watches);
        assertEquals(Collections.emptyList(), mDownload.versions);
    }

    /** GitHub's latest moved back below a version a replay brought: the game starts the phone's, so FAF's is not got. */
    @Test
    public void aNewerVersionOnThePhoneLeavesFafsNewestUndownloaded() throws IOException {
        GameFilesTest.makeReady(mStorage, 3841);

        run(-1);

        assertState(NewestGameFiles.Phase.KNOWN, 3840);
        assertEquals("FAF game files: 3841 (newest at FAF: 3840); the game starts 3841, as it is newer",
            NewestGameFiles.now().line(3841));
        assertEquals(Collections.emptyList(), heads());
        assertEquals(Collections.emptyList(), mDownload.versions);
    }

    @Test
    public void onAMeteredNetworkTheCardAsksWithTheMeasuredSize() throws IOException {
        mConnectivity.metered = true;

        run(-1);

        assertState(NewestGameFiles.Phase.ASKING, 3840);
        assertEquals(BYTES, NewestGameFiles.now().bytes);
        assertSame(NewestGameFiles.now(), NewestGameFiles.question());
        assertEquals(0, mConnectivity.watches);
        assertEquals(Collections.emptyList(), mDownload.versions);
    }

    /** While it measures (about 10 s of HEAD requests) the card already names FAF's newest version. */
    @Test
    public void whileItMeasuresTheCardKnowsFafsNewestVersion() throws IOException {
        run(-1);

        assertEquals(10, mNetwork.phases.size());
        assertEquals(Collections.singleton(NewestGameFiles.Phase.KNOWN), new HashSet<>(mNetwork.phases));
    }

    @Test
    public void aYesSkipsTheCheckAndDownloadsWithoutAWatch() throws IOException {
        mConnectivity.metered = true;

        run(3840);

        assertFalse("no check", mNetwork.asked.contains(FafRelease.LATEST_URL));
        assertEquals(Collections.singletonList(3840), mDownload.versions);
        assertEquals(0, mConnectivity.watches);
        assertState(NewestGameFiles.Phase.KNOWN, 3840);
    }

    @Test
    public void aDownloadWithoutAYesIsWatchedUntilItEnds() throws IOException {
        run(-1);

        assertEquals(Collections.singletonList(3840), mDownload.versions);
        assertState(NewestGameFiles.Phase.KNOWN, 3840);
        assertTrue(GameFiles.ready(mStorage, 3840));
        assertEquals(1, mConnectivity.watches);
        assertEquals("closed", 1, mConnectivity.closed);
    }

    /** It stops as Cancel does, but it is no Cancel: the job ends as METERED, and the log has no "Cancelled.". */
    @Test
    public void aDownloadWithoutAYesStopsWhenTheNetworkBecomesMetered() throws IOException {
        mDownload.during = () -> {
            mConnectivity.becomeMetered();
            Cancellation.check();
        };

        run(-1);

        assertState(NewestGameFiles.Phase.METERED, 3840);
        assertEquals(1, mConnectivity.closed);
    }

    /** The network became metered while the job measured: the watch hears of it as it starts, before the first byte. */
    @Test
    public void aNetworkMeteredByTheDownloadsStartStopsIt() throws IOException {
        mNetwork.meteredWhileMeasuring = true;
        mDownload.during = () -> {
            throw new AssertionError("no download on a metered network without a yes");
        };

        run(-1);

        assertState(NewestGameFiles.Phase.METERED, 3840);
        assertEquals(1, mConnectivity.closed);
    }

    @Test
    public void theUsersCancelEndsTheDownloadAndGoesOnAsACancel() {
        mDownload.during = () -> {
            Cancellation.request();
            Cancellation.check();
        };

        assertThrows(Cancellation.Cancelled.class, () -> run(-1));

        assertState(NewestGameFiles.Phase.CANCELLED, 3840);
        assertEquals(1, mConnectivity.closed);
    }

    @Test
    public void aFailedDownloadSaysWhyAndGoesOn() {
        mDownload.during = () -> {
            throw new Http.StorageException("the phone cannot write env.3840.nx2.part (write failed: ENOSPC (No space left on device))");
        };

        IOException failed = assertThrows(IOException.class, () -> run(-1));

        assertTrue(failed instanceof Http.StorageException);
        assertState(NewestGameFiles.Phase.FAILED, 3840);
        assertEquals(failed.getMessage(), NewestGameFiles.now().note);
        assertEquals(1, mConnectivity.closed);
    }

    /** Neither asked about nor started, also after a yes: the room is measured before both. */
    @Test
    public void aDownloadThatDoesNotFitIsNotStarted() throws IOException {
        mDownload.room = 300_000_000L;
        mConnectivity.metered = true;

        run(-1);

        assertState(NewestGameFiles.Phase.FAILED, 3840);
        assertEquals("the phone has 286 MB free, and it needs 782 MB", NewestGameFiles.now().note);
        assertNull("no question", NewestGameFiles.question());

        run(3840);

        assertState(NewestGameFiles.Phase.FAILED, 3840);
        assertEquals(Collections.emptyList(), mDownload.versions);
        assertEquals(0, mConnectivity.watches);
    }

    /** The user's Cancel during the measurement's last HEAD, which Http.ONE_TRY makes a Cancelled: no question after it. */
    @Test
    public void aCancelWhileItMeasuresIsACancelWithoutAQuestion() {
        mConnectivity.metered = true;
        mNetwork.cancelledHead = "units";

        assertThrows(Cancellation.Cancelled.class, () -> run(-1));

        assertState(NewestGameFiles.Phase.CANCELLED, 3840);
        assertNull(NewestGameFiles.question());
        assertNull(NewestGameFiles.pending());
    }

    /** A download of 3840 that the user cancelled halfway: env's part, and effects whole. */
    private void stopHalfway() {
        mDownload.during = () -> {
            GameFilesTest.touch(new File(mStorage.downloads(), "env.3840.nx2.part"));
            GameFilesTest.touch(new File(mStorage.game(3840), "gamedata/effects.nx2"));
            Cancellation.request();
            Cancellation.check();
        };
        assertThrows(Cancellation.Cancelled.class, () -> run(-1));
        mDownload.during = null;
        Cancellation.reset();
    }

    /** FAF's newest became 3841 after a download of 3840 stopped: the job never goes on with 3840, so its files go. */
    @Test
    public void aCheckOnAnotherVersionDropsWhatTheLastDownloadLeft() throws IOException {
        stopHalfway();
        // A replay test's partial download of another version, and a version that is ready.
        GameFilesTest.touch(new File(mStorage.downloads(), "units.3839.nx2.part"));
        GameFilesTest.makeReady(mStorage, 3835);
        mNetwork.latest = "3841";

        run(-1);

        assertFalse(mStorage.game(3840).exists());
        assertFalse(new File(mStorage.downloads(), "env.3840.nx2.part").exists());
        assertTrue("a replay test's", new File(mStorage.downloads(), "units.3839.nx2.part").exists());
        assertEquals(Arrays.asList(3835, 3841), GameFiles.readyVersions(mStorage));
        assertEquals(Arrays.asList(3840, 3841), mDownload.versions);
    }

    @Test
    public void aCheckOnTheSameVersionGoesOnWithIt() throws IOException {
        stopHalfway();
        File part = new File(mStorage.downloads(), "env.3840.nx2.part");
        mDownload.during = () -> assertTrue("kept for the download to go on with", part.isFile());

        run(-1);

        assertEquals(Arrays.asList(3840, 3840), mDownload.versions);
        assertTrue(GameFiles.ready(mStorage, 3840));
    }

    /** What the last download got whole stays, though FAF's newest moved on: older versions stay for the replay test. */
    @Test
    public void aVersionTheLastDownloadFinishedStays() throws IOException {
        run(-1);
        mNetwork.latest = "3841";

        run(-1);

        assertEquals(Arrays.asList(3840, 3841), GameFiles.readyVersions(mStorage));
    }
}
