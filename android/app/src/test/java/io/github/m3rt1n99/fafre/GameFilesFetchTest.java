package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Deque;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
import org.junit.rules.Timeout;

/**
 * A start's game files of a version the phone has not got, on each of its paths: FAF's updater, the network, the phone's
 * room and the player's answers as each test sets them up, and the start's code and line where it fails.
 */
public class GameFilesFetchTest {
    /** 3840's packages on FAF's updater, as it had them (mods not published), 716 MB. */
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
    /** The fetch loops after a metered stop: one that never ends fails here rather than hanging the run. */
    @Rule
    public final Timeout timeout = Timeout.seconds(30);

    private Storage mStorage;
    private final Updater mUpdater = new Updater();
    private final Connectivity mConnectivity = new Connectivity();
    /** The phone's room, which a test may set up anew before the fetch. */
    private RoomTest.Fake mRoom = new RoomTest.Fake(4_000_000_000L);
    private final Download mDownload = new Download();
    private final Player mPlayer = new Player();
    private final List<String> mLines = new ArrayList<>();
    /** The measures the fetch began: the updater's lengths come once, so HEAD requests do not count them. */
    private int mMeasures;
    private final GameFiles.Log mLog = new GameFiles.Log() {
        @Override
        public void line(String text) {
            mLines.add(text);
        }

        @Override
        public void step(String text) {
            if (text.startsWith("Measuring ")) {
                ++mMeasures;
            }
        }
    };

    @Before
    public void setUp() {
        Cancellation.reset();
        mStorage = new Storage(folder.getRoot());
    }

    @After
    public void noCancelLeft() {
        Cancellation.reset();
    }

    /** FAF's updater by HEAD requests: 3840's packages, and no other version's. */
    private static final class Updater implements Http.Requests {
        final List<String> heads = new ArrayList<>();

        @Override
        public String text(String url) {
            throw new AssertionError("no text: " + url);
        }

        /** As Http.ONE_TRY's, a Cancelled while a Back or a stop is there. */
        @Override
        public long length(String url) throws IOException {
            heads.add(url);
            Cancellation.check();
            String name = url.substring(Http.GAME_URL.length());
            String nx2 = name.substring(0, name.indexOf('.'));
            if (!name.endsWith(".3840.nx2") || !LENGTHS.containsKey(nx2)) {
                throw new Http.StatusException(url, 404);
            }
            return LENGTHS.get(nx2);
        }
    }

    /** The phone's network: metered or not, and the watch on it. */
    private static final class Connectivity implements NewestGameFilesJob.Connectivity {
        boolean metered;
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

    /** The download, which checks the Cancel at its start as ensure does. */
    private final class Download implements NewestGameFilesJob.Download {
        /** What each download meets once it has started, in turn; none for the rest. */
        final Deque<Step> during = new ArrayDeque<>();
        /** What the download does after its last file, before it returns. */
        Step after;
        int downloads;

        @Override
        public void files(int version) throws IOException {
            assertEquals(3840, version);
            ++downloads;
            assertEquals("the room is claimed right before each download", downloads, mRoom.claims.size());
            Cancellation.check();
            Step step = during.poll();
            if (step != null) {
                step.run();
            }
            GameFilesTest.makeReady(mStorage, version);
            if (after != null) {
                after.run();
            }
        }
    }

    /** The player, who answers the question with each of {@code answers} in turn. */
    private final class Player implements GameFilesFetch.Question {
        final Deque<Boolean> answers = new ArrayDeque<>();
        /** Back while the question shows. */
        boolean back;
        /** What the app's own download of the version, the lock's other holder, got while the question showed. */
        Step meanwhile;
        final List<Long> askedAbout = new ArrayList<>();

        @Override
        public boolean yes(int version, long bytes) throws IOException {
            assertEquals(3840, version);
            assertEquals("the room is claimed after the yes", mDownload.downloads, mRoom.claims.size());
            askedAbout.add(bytes);
            if (meanwhile != null) {
                meanwhile.run();
            }
            if (back) {
                Cancellation.request();
                Cancellation.check();
            }
            Boolean answer = answers.poll();
            if (answer == null) {
                throw new AssertionError("a question nobody expected, about " + bytes + " bytes");
            }
            return answer;
        }
    }

    private void fetch(int version) throws GameStart.Failure {
        GameFilesFetch.run(mStorage, version, mLog, mUpdater, mConnectivity, mRoom, mDownload, mPlayer);
    }

    private GameStart.Failure failure(int version) {
        return assertThrows(GameStart.Failure.class, () -> fetch(version));
    }

    private static void assertFailure(GameStart.Code code, String message, GameStart.Failure failure) {
        assertEquals(code, failure.code);
        assertEquals(message, failure.getMessage());
    }

    /** Neither a Back nor a stop left behind: the start's next step runs. */
    private static void assertNothingStops() {
        try {
            Cancellation.check();
        } catch (Cancellation.Cancelled e) {
            throw new AssertionError(Cancellation.requested() ? "a Back left behind" : "a stop left behind");
        }
    }

    /** What a download stopped half way leaves: env and textures whole, as ensure keeps them. */
    private void envAndTexturesWhole() throws IOException {
        File game = mStorage.game(3840);
        GameFiles.write(new File(game, "manifest.json"), "{\"pack_format\": " + GameFiles.PACK_FORMAT + "}");
        GameFilesTest.touch(new File(game, "gamedata/env.nx2"));
        GameFilesTest.touch(new File(game, "gamedata/textures.nx2"));
    }

    private static final long REST = BYTES - LENGTHS.get("env") - LENGTHS.get("textures");
    /** 3840's room with the spare: 816 MB, which 300,000,000 bytes leave 516 MB short of. */
    private static final String NO_ROOM_LINE = "FAF game version 3840 needs 816 MB: free 516 MB more on the phone";

    @Test
    public void onANetworkThatIsNotMeteredTheVersionIsMeasuredAndDownloadedWithoutAQuestion() throws Exception {
        fetch(3840);

        assertTrue(GameFiles.ready(mStorage, 3840));
        assertEquals("every package measured", 10, mUpdater.heads.size());
        assertEquals(Collections.emptyList(), mPlayer.askedAbout);
        assertEquals("the download and the spare", Collections.singletonList(BYTES + Room.SPARE_BYTES), mRoom.claims);
        assertEquals(1, mDownload.downloads);
        assertEquals("watched until it ended", 1, mConnectivity.watches);
        assertEquals(1, mConnectivity.closed);
        assertEquals(Arrays.asList("FAF game version 3840 is not on the phone: 716 MB to download", "FAF game version 3840 is ready"),
            mLines);
    }

    @Test
    public void onAMeteredNetworkThePlayerIsAskedAboutTheMeasuredSizeAndAYesDownloadsWithoutAWatch() throws Exception {
        mConnectivity.metered = true;
        mPlayer.answers.add(true);

        fetch(3840);

        assertTrue(GameFiles.ready(mStorage, 3840));
        assertEquals(Collections.singletonList(BYTES), mPlayer.askedAbout);
        assertEquals("asked before the question, and again at the claim after it", 2, mRoom.asked);
        assertEquals(Collections.singletonList(BYTES + Room.SPARE_BYTES), mRoom.claims);
        assertEquals(0, mConnectivity.watches);
        assertEquals("FAF game version 3840 is not on the phone: 716 MB to download, which the player said yes to on a metered "
            + "network", mLines.get(0));
    }

    @Test
    public void aNoOnAMeteredNetworkDownloadsNothingAndCancelsTheStart() {
        mConnectivity.metered = true;
        mPlayer.answers.add(false);

        assertFailure(GameStart.Code.CANCELLED, "the player did not download FAF game version 3840 (716 MB) on a metered network",
            failure(3840));

        assertEquals(0, mDownload.downloads);
        assertFalse(GameFiles.ready(mStorage, 3840));
    }

    @Test
    public void backWhileThePlayerIsAskedCancelsTheStart() {
        mConnectivity.metered = true;
        mPlayer.back = true;

        assertFailure(GameStart.Code.CANCELLED, "Back ended the start while it fetched what the game needs", failure(3840));

        assertEquals(0, mDownload.downloads);
    }

    /** It stops as Back does, but it is no Back: the rest is measured, and the player says whether it comes on this network. */
    @Test
    public void aNetworkThatBecomesMeteredUnderTheDownloadStopsItAndThePlayerIsAskedAboutTheRest() throws Exception {
        mDownload.during.add(() -> {
            envAndTexturesWhole();
            mConnectivity.becomeMetered();
            Cancellation.check();
        });
        mPlayer.answers.add(true);

        fetch(3840);

        assertTrue(GameFiles.ready(mStorage, 3840));
        assertEquals("asked about what the first download left", Collections.singletonList(REST), mPlayer.askedAbout);
        assertEquals("each download's room, the rest's after the yes",
            Arrays.asList(BYTES + Room.SPARE_BYTES, REST + Room.SPARE_BYTES), mRoom.claims);
        assertEquals(2, mDownload.downloads);
        assertEquals("the second, with the yes, unwatched", 1, mConnectivity.watches);
        assertEquals(1, mConnectivity.closed);
        assertNothingStops();
        assertTrue(mLines.contains("The network has become metered: the download stops, and the player is asked about the rest"));
    }

    /** Back in the same moment as the network's word: the start ends, as the activity says Back does, and nothing asks. */
    @Test
    public void backAsTheNetworkBecomesMeteredUnderTheDownloadCancelsTheStart() {
        mDownload.during.add(() -> {
            mConnectivity.becomeMetered();
            Cancellation.request();
            Cancellation.check();
        });

        assertFailure(GameStart.Code.CANCELLED, "Back ended the start while it fetched what the game needs", failure(3840));

        assertEquals(Collections.emptyList(), mPlayer.askedAbout);
        assertEquals(1, mDownload.downloads);
        assertEquals(1, mConnectivity.closed);
        assertEquals("not measured again", 1, mMeasures);
        assertFalse(mLines.contains("The network has become metered: the download stops, and the player is asked about the rest"));
    }

    /** The app's own download got the version since the start looked: nothing is measured, asked about or downloaded. */
    @Test
    public void aVersionTheAppGotSinceTheStartLookedIsReadyWithoutAQuestion() throws Exception {
        GameFilesTest.makeReady(mStorage, 3840);
        mConnectivity.metered = true;

        fetch(3840);

        assertEquals(Collections.emptyList(), mUpdater.heads);
        assertEquals(Collections.emptyList(), mPlayer.askedAbout);
        assertEquals(0, mDownload.downloads);
        assertEquals(Collections.singletonList("FAF game version 3840 is ready"), mLines);
    }

    /** The app's own download, which the stopped one waited for, finished it: no question about a rest there is not. */
    @Test
    public void aVersionTheAppGotAfterAMeteredStopIsReadyWithoutAQuestion() throws Exception {
        mDownload.during.add(() -> {
            mConnectivity.becomeMetered();
            GameFilesTest.makeReady(mStorage, 3840);
            Cancellation.check();
        });

        fetch(3840);

        assertTrue(GameFiles.ready(mStorage, 3840));
        assertEquals(Collections.emptyList(), mPlayer.askedAbout);
        assertEquals(1, mDownload.downloads);
        assertNothingStops();
    }

    @Test
    public void aNoToTheRestCancelsTheStartAndKeepsWhatTheDownloadGot() {
        mDownload.during.add(() -> {
            envAndTexturesWhole();
            mConnectivity.becomeMetered();
            Cancellation.check();
        });
        mPlayer.answers.add(false);

        assertFailure(GameStart.Code.CANCELLED, "the player did not download FAF game version 3840 (123 MB) on a metered network",
            failure(3840));

        assertTrue("kept for the next start", new File(mStorage.game(3840), "gamedata/env.nx2").isFile());
        assertFalse(GameFiles.ready(mStorage, 3840));
    }

    /** The network's word that it is metered came after the last file, before the watch was closed: nothing stops the start. */
    @Test
    public void aMeteredWordAfterTheLastFileLeavesNoCancelBehind() throws Exception {
        mDownload.after = mConnectivity::becomeMetered;

        fetch(3840);

        assertTrue(GameFiles.ready(mStorage, 3840));
        assertNothingStops();
        assertEquals(1, mDownload.downloads);
        assertEquals(Collections.emptyList(), mPlayer.askedAbout);
    }

    /** Neither asked about nor started: the room is measured before both, and the result says how much to free. */
    @Test
    public void aDownloadThatDoesNotFitEndsTheStartWithNoRoomBeforeAQuestionAndAnyByte() {
        mRoom = new RoomTest.Fake(300_000_000L);
        mConnectivity.metered = true;

        GameStart.Failure failure = failure(3840);

        assertFailure(GameStart.Code.NO_ROOM, NO_ROOM_LINE, failure);
        assertEquals(BYTES + Room.SPARE_BYTES - 300_000_000L, failure.bytesToFree);
        assertEquals(Collections.emptyList(), mPlayer.askedAbout);
        assertEquals(Collections.emptyList(), mRoom.claims);
        assertEquals(0, mDownload.downloads);
    }

    /** Without a question the claim is the measure, before the network is watched. */
    @Test
    public void onANetworkThatIsNotMeteredADownloadThatDoesNotFitEndsTheStartBeforeAnyByte() {
        mRoom = new RoomTest.Fake(300_000_000L);

        assertFailure(GameStart.Code.NO_ROOM, NO_ROOM_LINE, failure(3840));

        assertEquals(1, mRoom.asked);
        assertEquals(0, mConnectivity.watches);
        assertEquals(0, mDownload.downloads);
    }

    /** The question waited for the player, and another app took the room meanwhile: the claim after the yes finds it gone. */
    @Test
    public void roomThatWentWhileThePlayerWasAskedEndsTheStartWithNoRoom() {
        mConnectivity.metered = true;
        mPlayer.answers.add(true);
        mRoom = new RoomTest.Fake(() -> mPlayer.askedAbout.isEmpty() ? 4_000_000_000L : 300_000_000L);

        assertFailure(GameStart.Code.NO_ROOM, NO_ROOM_LINE, failure(3840));

        assertEquals(Collections.singletonList(BYTES), mPlayer.askedAbout);
        assertEquals(Collections.emptyList(), mRoom.claims);
        assertEquals(0, mDownload.downloads);
    }

    /**
     * The app's own download, the lock's other holder, finished the version while the player was asked, which took its room:
     * nothing is claimed for it any more, and no NO_ROOM ends a start whose files are there.
     */
    @Test
    public void aVersionTheAppGotWhileThePlayerWasAskedIsReadyWithoutAClaim() throws Exception {
        mConnectivity.metered = true;
        mPlayer.answers.add(true);
        mPlayer.meanwhile = () -> GameFilesTest.makeReady(mStorage, 3840);
        mRoom = new RoomTest.Fake(() -> mPlayer.askedAbout.isEmpty() ? 1_000_000_000L : 300_000_000L);

        fetch(3840);

        assertEquals(Collections.singletonList(BYTES), mPlayer.askedAbout);
        assertEquals(Collections.emptyList(), mRoom.claims);
        assertEquals(0, mDownload.downloads);
        assertEquals("FAF game version 3840 is ready", mLines.get(mLines.size() - 1));
    }

    /**
     * The lock's other holder got env and textures while the player was asked: the claim counts what is left now, from the
     * HEAD requests' answers before the question, without asking the updater again (a second apart each).
     */
    @Test
    public void whatTheAppGotWhileThePlayerWasAskedIsNotClaimed() throws Exception {
        mConnectivity.metered = true;
        mPlayer.answers.add(true);
        mPlayer.meanwhile = this::envAndTexturesWhole;

        fetch(3840);

        assertTrue(GameFiles.ready(mStorage, 3840));
        assertEquals(Collections.singletonList(BYTES), mPlayer.askedAbout);
        assertEquals(Collections.singletonList(REST + Room.SPARE_BYTES), mRoom.claims);
        assertEquals("measured again after the yes", 2, mMeasures);
        assertEquals("the updater asked once", 10, mUpdater.heads.size());
        assertEquals(1, mDownload.downloads);
    }

    /** Never another measure in its place: a room call that Android fails is the start's DOWNLOAD, before any byte. */
    @Test
    public void aRoomCallThatAndroidFailsEndsTheStartWithDownloadBeforeAQuestionAndAnyByte() {
        mConnectivity.metered = true;
        mRoom.allocatableFails = new IOException("Android cannot say how much the app may write to the phone's storage: "
            + "no such volume");
        assertFailure(GameStart.Code.DOWNLOAD, "FAF game version 3840: Android cannot say how much the app may write to the "
            + "phone's storage: no such volume", failure(3840));
        assertEquals(Collections.emptyList(), mPlayer.askedAbout);

        mConnectivity.metered = false;
        mRoom.allocatableFails = null;
        mRoom.allocateFails = new IOException("Android cannot clear 816 MB on the phone's storage for the write: not freed");
        assertFailure(GameStart.Code.DOWNLOAD, "FAF game version 3840: Android cannot clear 816 MB on the phone's storage for "
            + "the write: not freed", failure(3840));

        assertEquals(0, mConnectivity.watches);
        assertEquals(0, mDownload.downloads);
    }

    @Test
    public void aVersionFafHasNoFilesOfFailsWithNoGameFilesBeforeAnyByte() {
        mConnectivity.metered = true;

        assertFailure(GameStart.Code.NO_GAME_FILES,
            "FAF game version 3633: HTTP 404 for https://content.faforever.com/faf/updaterNew/updates_faf_files/lua.3633.nx2",
            failure(3633));

        assertEquals(Collections.emptyList(), mPlayer.askedAbout);
        assertEquals(0, mDownload.downloads);
    }

    @Test
    public void aFailedDownloadIsTheStartsDownloadFailureWithItsReason() {
        mDownload.during.add(() -> {
            throw new IOException("Connection reset");
        });
        assertFailure(GameStart.Code.DOWNLOAD, "FAF game version 3840: Connection reset", failure(3840));

        // A disk that became full under the download stays DOWNLOAD (GameFilesFetch says why): the next start's NO_ROOM
        // says how much to free.
        mDownload.during.add(() -> {
            throw new Http.StorageException("the phone cannot write env.3840.nx2.part (write failed: ENOSPC (No space left on device))");
        });
        assertFailure(GameStart.Code.DOWNLOAD, "FAF game version 3840: the phone cannot write env.3840.nx2.part (write failed: ENOSPC "
            + "(No space left on device))", failure(3840));
        assertEquals("each closed its watch", 2, mConnectivity.closed);
    }

    @Test
    public void backDuringTheDownloadCancelsTheStart() {
        mDownload.during.add(() -> {
            Cancellation.request();
            Cancellation.check();
        });

        assertFailure(GameStart.Code.CANCELLED, "Back ended the start while it fetched what the game needs", failure(3840));

        assertEquals(1, mDownload.downloads);
        assertEquals(1, mConnectivity.closed);
    }

    /** Back during the measurement's last HEAD, which Http.ONE_TRY makes a Cancelled. */
    @Test
    public void backWhileItMeasuresCancelsTheStart() {
        Http.Requests cancelled = new Http.Requests() {
            @Override
            public String text(String url) {
                throw new AssertionError("no text: " + url);
            }

            @Override
            public long length(String url) throws IOException {
                Cancellation.request();
                throw new Cancellation.Cancelled();
            }
        };

        GameStart.Failure failure = assertThrows(GameStart.Failure.class,
            () -> GameFilesFetch.run(mStorage, 3840, mLog, cancelled, mConnectivity, mRoom, mDownload, mPlayer));

        assertEquals(GameStart.Code.CANCELLED, failure.code);
        assertEquals(0, mDownload.downloads);
    }

    /** With GameFiles.ensure and FAF's updater as it downloads: the versions on the phone stay, and the new one joins them. */
    @Test
    public void theVersionsOnThePhoneStayAndTheFetchedOneJoinsThem() throws Exception {
        GameFilesTest.makeReady(mStorage, 3835);
        GameFilesTest.makeReady(mStorage, 3841);
        File kept = new File(mStorage.game(3841), "gamedata/units.nx2");
        GameFilesTest.touch(kept);
        GameFilesTest.Updater updater = new GameFilesTest.Updater();
        NewestGameFilesJob.Download phone = version -> GameFiles.ensure(mStorage, version, mLog, updater);

        GameFilesFetch.run(mStorage, 3840, mLog, mUpdater, mConnectivity, mRoom, phone, mPlayer);

        assertEquals(Arrays.asList(3835, 3840, 3841), GameFiles.readyVersions(mStorage));
        assertEquals("partial", new String(Files.readAllBytes(kept.toPath()), StandardCharsets.UTF_8));
        assertEquals(Arrays.asList("effects", "env", "etc", "loc", "lua", "meshes", "projectiles", "textures", "units"),
            updater.downloaded);
    }
}
