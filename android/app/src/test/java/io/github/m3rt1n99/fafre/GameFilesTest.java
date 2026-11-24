package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.nio.file.StandardOpenOption;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

/**
 * The init files of a game and of a replay's run, as the engine reads them, the size of a version's download, and a
 * version's files on the phone: which are ready, how they come, and what an unfinished download leaves.
 */
public class GameFilesTest {
    private static final String INIT = "hook = {\n    '/schook'\n}\n"
        + "local allowed = {}\n"
        + "custom_vault_path = InitFileDir .. '/../vault'\n"
        + "if custom_vault_path then\n    LoadVaultContent(custom_vault_path)\nend\n";

    private static final GameFiles.Log LOG = new GameFiles.Log() {
        @Override
        public void line(String text) {
        }

        @Override
        public void step(String text) {
        }
    };

    @Rule
    public final TemporaryFolder folder = new TemporaryFolder();

    /** {@code version}'s files as ensure leaves them when they are ready. */
    static void makeReady(Storage storage, int version) throws IOException {
        File dir = storage.game(version);
        GameFiles.write(new File(dir, "manifest.json"),
            "{\"game_version\": \"" + version + "\", \"pack_format\": " + GameFiles.PACK_FORMAT + "}");
        GameFiles.write(new File(dir, "bin/init_ci.lua"), INIT);
    }

    /** A file with something in it, and its folders. */
    static void touch(File file) throws IOException {
        GameFiles.write(file, "partial");
    }

    @Test
    public void aGameMountsTheUiPackageAndHooksItRightBeforeTheVaultLine() throws IOException {
        assertEquals("hook = {\n    '/schook'\n}\n"
            + "local allowed = {}\n"
            + "MountDirectory(InitFileDir .. '/../fafre_ui', '/fafre_ui')\n"
            + "table.insert(hook, '/fafre_ui')\n"
            + "custom_vault_path = InitFileDir .. '/../vault'\n"
            + "if custom_vault_path then\n    LoadVaultContent(custom_vault_path)\nend\n", GameFiles.withUiPackage(INIT));
    }

    @Test
    public void anInitFileWithoutAVaultLineIsNotAGames() {
        assertThrows(IOException.class, () -> GameFiles.withUiPackage("hook = {}\n"));
    }

    @Test
    public void aReplaysRunStaysWithoutTheUiPackage() throws IOException {
        Storage storage = new Storage(folder.getRoot());
        makeReady(storage, 3835);
        File vault = folder.newFolder("vault");

        File init = GameFiles.runInit(storage, 3835, "r1234", vault);

        String text = new String(Files.readAllBytes(init.toPath()), StandardCharsets.UTF_8);
        assertEquals(new File(storage.game(3835), "bin/init_r1234.lua"), init);
        assertFalse(text.contains("fafre_ui"));
        assertEquals(INIT.replace("custom_vault_path = InitFileDir .. '/../vault'", "custom_vault_path = '" + vault.getAbsolutePath() + "'"), text);
    }

    @Test
    public void eachStartOfTheEngineNamesTheAppThatStartsItInFaPath() throws IOException {
        Storage storage = new Storage(folder.getRoot());
        makeReady(storage, 3840);
        File vault = folder.newFolder("vault");
        File faPath = new File(storage.game(3840), "fa_path.lua");
        // As 0.14.0 wrote it when it fetched the version, which FAF's init file then logged as the client of 0.16.0's game.
        String fetched = "fa_path = '" + storage.scfa.getAbsolutePath() + "'\nGameType = \"faf\"\nGameVersion = \"3840\"\n"
            + "ClientVersion = \"faf-re-android-0.14.0\"\n";
        String now = fetched.replace("0.14.0", BuildConfig.VERSION_NAME);

        GameFiles.write(faPath, fetched);
        File game = GameFiles.gameInitFiles(storage, 3840, vault);
        assertEquals("the game's start", now, new String(Files.readAllBytes(faPath.toPath()), StandardCharsets.UTF_8));
        assertEquals(GameFiles.gameInitFile(storage.game(3840)), game);
        assertTrue(new String(Files.readAllBytes(game.toPath()), StandardCharsets.UTF_8).contains("'/fafre_ui'"));

        GameFiles.write(faPath, fetched);
        GameFiles.runInit(storage, 3840, "r1", vault);
        assertEquals("the replay test's run", now, new String(Files.readAllBytes(faPath.toPath()), StandardCharsets.UTF_8));
        assertEquals("no part left beside it", Arrays.asList("bin", "fa_path.lua", "manifest.json"),
            sorted(storage.game(3840).list()));
    }

    private static List<String> sorted(String[] names) {
        List<String> list = new ArrayList<>(Arrays.asList(names));
        Collections.sort(list);
        return list;
    }

    /** FAF's updater by HEAD requests: 3840's packages as it had them (mods not published), or one length unsaid. */
    private static Http.Requests updater(long unitsLength) {
        Map<String, Long> lengths = new HashMap<>();
        lengths.put("effects", 3162052L);
        lengths.put("env", 501595718L);
        lengths.put("etc", 3837L);
        lengths.put("loc", 1765808L);
        lengths.put("lua", 3435946L);
        lengths.put("meshes", 3144266L);
        lengths.put("projectiles", 923326L);
        lengths.put("textures", 91406892L);
        lengths.put("units", unitsLength);
        return new Http.Requests() {
            @Override
            public String text(String url) {
                throw new AssertionError("no text: " + url);
            }

            @Override
            public long length(String url) throws IOException {
                String name = url.substring(Http.GAME_URL.length(), url.indexOf(".3840.nx2"));
                if (!lengths.containsKey(name)) {
                    throw new Http.StatusException(url, 404);
                }
                return lengths.get(name);
            }
        };
    }

    @Test
    public void aDownloadsSizeIsWhatTheUpdaterHasLessWhatACutOffDownloadLeft() throws IOException {
        File game = folder.newFolder("3840");
        File downloads = folder.newFolder("downloads");
        Files.write(new File(downloads, "units.3840.nx2.part").toPath(), new byte[1000]);

        assertEquals(715_047_073L - 1000, GameFiles.toDownload(game, downloads, 3840, updater(109609228L)));
    }

    @Test
    public void aFolderWithoutItsManifestIsDownloadedWhole() throws IOException {
        File game = folder.newFolder("3840");
        new File(game, "gamedata").mkdirs();
        Files.write(new File(game, "gamedata/env.nx2").toPath(), new byte[10]);

        assertEquals("ensure starts it over", 715_047_073L, GameFiles.toDownload(game, folder.newFolder("downloads"), 3840, updater(109609228L)));
    }

    @Test
    public void aLengthTheUpdaterDoesNotSayLeavesTheSizeUnknown() throws IOException {
        assertEquals(-1, GameFiles.toDownload(folder.newFolder("3840"), folder.newFolder("downloads"), 3840, updater(-1)));
    }

    @Test
    public void aRequiredPackageTheUpdaterHasNotIsAnError() {
        Http.StatusException missing = assertThrows(Http.StatusException.class,
            () -> GameFiles.toDownload(folder.newFolder("3841"), folder.newFolder("downloads"), 3841, new Http.Requests() {
                @Override
                public String text(String url) {
                    throw new AssertionError("no text: " + url);
                }

                @Override
                public long length(String url) throws IOException {
                    throw new Http.StatusException(url, 404);
                }
            }));
        assertEquals(404, missing.status);
    }

    /** A download of 3840 that stopped after env and textures: those stay, and only the rest is measured. */
    @Test
    public void aFolderPackedAsNowSkipsItsWholePackages() throws IOException {
        File game = folder.newFolder("3840");
        GameFiles.write(new File(game, "manifest.json"), "{\"pack_format\": " + GameFiles.PACK_FORMAT + "}");
        touch(new File(game, "gamedata/env.nx2"));
        touch(new File(game, "gamedata/textures.nx2"));

        assertEquals(715_047_073L - 501_595_718L - 91_406_892L,
            GameFiles.toDownload(game, folder.newFolder("downloads"), 3840, updater(109609228L)));
    }

    /** The Game card lists them and the game starts the last: one listing for both. */
    @Test
    public void theReadyVersionsAreListedByNumber() throws IOException {
        Storage storage = new Storage(folder.getRoot());
        makeReady(storage, 3840);
        makeReady(storage, 999);
        makeReady(storage, 3835);
        Storage.mkdirs(storage.game(3841));
        GameFiles.write(new File(storage.game(3842), "manifest.json"), "{\"pack_format\": " + GameFiles.PACK_FORMAT + "}");
        GameFiles.write(new File(storage.game(3790), "manifest.json"), "{\"pack_format\": 2}");
        GameFiles.write(new File(storage.game(3790), "bin/init_ci.lua"), INIT);
        Storage.mkdirs(new File(storage.parity, "game/tmp"));

        assertEquals("unfinished, older packs and other folders left out", Arrays.asList(999, 3835, 3840),
            GameFiles.readyVersions(storage));
    }

    /** FAF's updater as ensure downloads from it: 3840's packages as small archives (mods not published), its init file. */
    static final class Updater implements Http.Downloads {
        String init = INIT;
        /** A package whose download fails as the network's, after all its tries. */
        String failing;
        final List<String> downloaded = new ArrayList<>();
        int initsDownloaded;

        @Override
        public byte[] bytes(String url) {
            assertEquals(Http.GAME_URL + "init_faf_3840.lua", url);
            ++initsDownloaded;
            return init.getBytes(StandardCharsets.UTF_8);
        }

        @Override
        public void download(String url, File dest, Http.Progress progress) throws IOException {
            String name = url.substring(Http.GAME_URL.length(), url.indexOf(".3840.nx2"));
            if (name.equals("mods")) {
                throw new Http.StatusException(url, 404);
            }
            if (name.equals(failing)) {
                throw new IOException("Connection reset");
            }
            if (name.equals("env")) {
                // As far as a download of env's 501,595,718 bytes has come.
                progress.bytes(4_999_999L, 501_595_718L);
            }
            downloaded.add(name);
            try (ZipOutputStream zip = new ZipOutputStream(new FileOutputStream(dest))) {
                zip.putNextEntry(new ZipEntry(name + "/a.bp"));
                zip.write(1);
                zip.closeEntry();
            }
        }
    }

    /** One that does not have exactly one vault anchor: after 700 MB of packages, it would fail at every try. */
    @Test
    public void anInitFileTheAppCannotUseFailsBeforeAnyPackage() {
        Storage storage = new Storage(folder.getRoot());
        Updater updater = new Updater();
        updater.init = "hook = {}\nif custom_vault_path then\nend\nif custom_vault_path then\nend\n";

        IOException failed = assertThrows(IOException.class, () -> GameFiles.ensure(storage, 3840, LOG, updater));

        assertEquals("init file: expected one 'if custom_vault_path then' line", failed.getMessage());
        assertEquals("no package downloaded", Collections.emptyList(), updater.downloaded);
        assertFalse("nothing changed on the phone", storage.game(3840).exists());
    }

    /** The init file comes last of all, so a download that stopped is no ready version, and the next one goes on. */
    @Test
    public void aVersionIsReadyOnlyWithAllItsPackages() throws IOException {
        Storage storage = new Storage(folder.getRoot());
        Updater updater = new Updater();
        updater.failing = "textures";

        assertThrows(IOException.class, () -> GameFiles.ensure(storage, 3840, LOG, updater));

        assertFalse(GameFiles.ready(storage, 3840));
        assertFalse(new File(storage.game(3840), "bin/init_ci.lua").exists());
        assertTrue("whole ones stay", new File(storage.game(3840), "gamedata/env.nx2").isFile());

        updater.failing = null;
        updater.downloaded.clear();
        GameFiles.ensure(storage, 3840, LOG, updater);

        assertTrue(GameFiles.ready(storage, 3840));
        assertEquals("what was missing", Arrays.asList("textures", "units"), updater.downloaded);
        assertEquals(GameFiles.ciInit(INIT), new String(Files.readAllBytes(new File(storage.game(3840), "bin/init_ci.lua").toPath()),
            StandardCharsets.UTF_8));
    }

    /** The steps and lines say sizes as the player reads them (Sizes): what has come rounded down, the whole up. */
    @Test
    public void aDownloadsStepsAndLinesSaySizesAsThePlayerReadsThem() throws IOException {
        List<String> steps = new ArrayList<>();
        List<String> lines = new ArrayList<>();
        GameFiles.Log log = new GameFiles.Log() {
            @Override
            public void line(String text) {
                lines.add(text);
            }

            @Override
            public void step(String text) {
                steps.add(text);
            }
        };

        GameFiles.ensure(new Storage(folder.getRoot()), 3840, log, new Updater());

        assertTrue(steps.toString(), steps.contains("Downloading FAF env.3840.nx2: 4 of 502 MB"));
        assertTrue("the small archive's line, " + lines, lines.contains("env.3840.nx2: 1 MB"));
    }

    /** What a download of 3840 left (a part, a whole package, the manifest) goes; another version's partial download stays. */
    @Test
    public void anUnfinishedVersionGoesWithItsPartialDownloads() throws IOException {
        Storage storage = new Storage(folder.getRoot());
        File unfinished = storage.game(3840);
        GameFiles.write(new File(unfinished, "manifest.json"), "{\"pack_format\": " + GameFiles.PACK_FORMAT + "}");
        touch(new File(unfinished, "gamedata/effects.nx2"));
        touch(new File(storage.downloads(), "env.3840.nx2.part"));
        touch(new File(storage.downloads(), "units.3839.nx2.part"));
        makeReady(storage, 3835);

        assertTrue(GameFiles.dropUnfinished(storage, 3840));

        assertFalse(unfinished.exists());
        assertFalse(new File(storage.downloads(), "env.3840.nx2.part").exists());
        assertTrue("another version's", new File(storage.downloads(), "units.3839.nx2.part").exists());
        assertEquals(Collections.singletonList(3835), GameFiles.readyVersions(storage));
    }

    @Test
    public void aReadyVersionIsNotDropped() throws IOException {
        Storage storage = new Storage(folder.getRoot());
        makeReady(storage, 3840);

        assertFalse(GameFiles.dropUnfinished(storage, 3840));

        assertTrue(GameFiles.ready(storage, 3840));
    }

    /**
     * Another thread's hold on {@code version} in this process: its lock file, open and locked till closed. A second channel
     * of this JVM's meets the JVM's own lock table (OverlappingFileLockException), not the system's lock: for the phone's two
     * processes, where tryLock gives null, a test needs {@link OtherProcess}.
     */
    private static FileChannel hold(Storage storage, int version) throws IOException {
        FileChannel channel = FileChannel.open(GameFiles.lockFile(storage, version).toPath(), StandardOpenOption.CREATE,
            StandardOpenOption.WRITE);
        channel.lock();
        return channel;
    }

    /** Another process's hold on {@code version}, as the game's process has it while its start downloads: a JVM of its own. */
    private static final class OtherProcess implements AutoCloseable {
        private final Process mProcess;

        OtherProcess(Storage storage, int version) throws Exception {
            File java = new File(System.getProperty("java.home"),
                System.getProperty("os.name").startsWith("Windows") ? "bin/java.exe" : "bin/java");
            String classes = new File(LockHolder.class.getProtectionDomain().getCodeSource().getLocation().toURI()).getPath();
            mProcess = new ProcessBuilder(java.getPath(), "-cp", classes, LockHolder.class.getName(),
                GameFiles.lockFile(storage, version).getPath()).redirectError(ProcessBuilder.Redirect.INHERIT).start();
            String said = new BufferedReader(new InputStreamReader(mProcess.getInputStream(), StandardCharsets.UTF_8)).readLine();
            if (!LockHolder.LOCKED.equals(said)) {
                close();
                throw new AssertionError("the other process holds no lock: " + said);
            }
        }

        /** The end of its input ends it, and the system lets go of its lock. */
        @Override
        public void close() throws Exception {
            mProcess.getOutputStream().close();
            if (!mProcess.waitFor(10, TimeUnit.SECONDS)) {
                mProcess.destroyForcibly();
                throw new AssertionError("the other process did not end");
            }
        }
    }

    /** The other process's program: locks the file it is given, says so, and holds it until its input ends. */
    static final class LockHolder {
        static final String LOCKED = "locked";

        public static void main(String[] args) throws IOException {
            try (FileChannel channel = FileChannel.open(Paths.get(args[0]), StandardOpenOption.CREATE, StandardOpenOption.WRITE)) {
                channel.lock();
                System.out.println(LOCKED);
                System.out.flush();
                while (System.in.read() >= 0) {
                    // Held.
                }
            }
        }
    }

    /** {@code version}'s download on a thread of its own, which records how it ended. */
    private static Thread ensureAside(Storage storage, int version, GameFiles.Log log, Http.Downloads updater,
        AtomicReference<Throwable> failed) {
        Thread thread = new Thread(() -> {
            try {
                GameFiles.ensure(storage, version, log, updater);
            } catch (Throwable e) {
                failed.set(e);
            }
        });
        thread.start();
        return thread;
    }

    /** A log that hears when ensure says it waits for another download of 3840. */
    private static GameFiles.Log waiting(CountDownLatch said) {
        return new GameFiles.Log() {
            @Override
            public void line(String text) {
            }

            @Override
            public void step(String text) {
                if (text.equals("Waiting for another download of FAF game version 3840")) {
                    said.countDown();
                }
            }
        };
    }

    /**
     * The app's job gets 3840 while a start's preparation asks for it in the game's process: the second waits until the first
     * lets go, and takes what it got without a request of its own.
     */
    @Test
    public void aDownloadOfAVersionAnotherDownloadHoldsWaitsAndTakesWhatItGot() throws Exception {
        Cancellation.reset();
        Storage storage = new Storage(folder.getRoot());
        Updater updater = new Updater();
        CountDownLatch said = new CountDownLatch(1);
        AtomicReference<Throwable> failed = new AtomicReference<>();
        Thread second;
        try (OtherProcess other = new OtherProcess(storage, 3840)) {
            second = ensureAside(storage, 3840, waiting(said), updater, failed);
            assertTrue("it says it waits", said.await(10, TimeUnit.SECONDS));
            assertTrue("still waiting", second.isAlive());
            // The other download finishes it, and lets go.
            makeReady(storage, 3840);
        }
        second.join(10_000);

        assertFalse(second.isAlive());
        assertNull(failed.get());
        assertTrue(GameFiles.ready(storage, 3840));
        assertEquals("nothing downloaded", 0, updater.initsDownloaded);
        assertEquals(Collections.emptyList(), updater.downloaded);
    }

    /** Here the other download is another thread's of this process, whose hold the wait meets as the JVM's own. */
    @Test
    public void theUsersCancelEndsAWaitForAnotherDownload() throws Exception {
        Cancellation.reset();
        Storage storage = new Storage(folder.getRoot());
        Updater updater = new Updater();
        CountDownLatch said = new CountDownLatch(1);
        AtomicReference<Throwable> failed = new AtomicReference<>();
        try (FileChannel other = hold(storage, 3840)) {
            Thread second = ensureAside(storage, 3840, waiting(said), updater, failed);
            assertTrue(said.await(10, TimeUnit.SECONDS));
            Cancellation.request();
            second.join(10_000);
            assertFalse(second.isAlive());
        } finally {
            Cancellation.reset();
        }

        assertTrue("a Cancelled, not " + failed.get(), failed.get() instanceof Cancellation.Cancelled);
        assertEquals(0, updater.initsDownloaded);
        // Nothing of the wait is left in this process: once the other has let go, the next download gets the lock.
        GameFiles.ensure(storage, 3840, LOG, updater);
        assertTrue(GameFiles.ready(storage, 3840));
    }

    /** Updater's download, which finds the room claimed before its first byte, the init file's. */
    private static Http.Downloads claimedBefore(Updater updater, RoomTest.Fake room) {
        return new Http.Downloads() {
            @Override
            public byte[] bytes(String url) {
                assertEquals("claimed before the first byte", 1, room.claims.size());
                return updater.bytes(url);
            }

            @Override
            public void download(String url, File dest, Http.Progress progress) throws IOException {
                updater.download(url, dest, progress);
            }
        };
    }

    /** The replay test's download measures what is still to download and claims its room with the spare, as a start's does. */
    @Test
    public void theReplayTestsDownloadClaimsItsRoomRightBeforeItsFirstByte() throws IOException {
        Storage storage = new Storage(folder.getRoot());
        Updater updater = new Updater();
        RoomTest.Fake room = new RoomTest.Fake(4_000_000_000L);

        GameFiles.ensureInRoom(storage, 3840, LOG, claimedBefore(updater, room), updater(109609228L), room);

        assertTrue(GameFiles.ready(storage, 3840));
        assertEquals(Collections.singletonList(715_047_073L + Room.SPARE_BYTES), room.claims);

        // A version that is ready asks for no room.
        RoomTest.Fake untouched = new RoomTest.Fake(0);
        GameFiles.ensureInRoom(storage, 3840, LOG, updater, updater(109609228L), untouched);
        assertEquals(0, untouched.asked);
    }

    /** A replay's version that does not fit is its error, with how much more to free, before any byte. */
    @Test
    public void theReplayTestsDownloadThatDoesNotFitGetsNoByte() {
        Storage storage = new Storage(folder.getRoot());
        Updater updater = new Updater();
        RoomTest.Fake room = new RoomTest.Fake(300_000_000L);

        Room.NoRoom noRoom = assertThrows(Room.NoRoom.class,
            () -> GameFiles.ensureInRoom(storage, 3840, LOG, updater, updater(109609228L), room));

        assertEquals("FAF game version 3840 needs 816 MB: free 516 MB more on the phone", noRoom.getMessage());
        assertEquals(Collections.emptyList(), room.claims);
        assertEquals(0, updater.initsDownloaded);
        assertEquals(Collections.emptyList(), updater.downloaded);
        assertFalse(storage.game(3840).exists());
    }

    /**
     * A start's download in the game's process finishes the version while the replay test waits for it: measured under the
     * lock, the version that is there needs no room, where a measure before the wait would have claimed all of it.
     */
    @Test
    public void aVersionAnotherDownloadFinishedWhileTheReplayTestWaitedNeedsNoRoom() throws Exception {
        Cancellation.reset();
        Storage storage = new Storage(folder.getRoot());
        Updater updater = new Updater();
        RoomTest.Fake room = new RoomTest.Fake(300_000_000L);
        CountDownLatch said = new CountDownLatch(1);
        AtomicReference<Throwable> failed = new AtomicReference<>();
        Thread replayTest = new Thread(() -> {
            try {
                GameFiles.ensureInRoom(storage, 3840, waiting(said), updater, updater(109609228L), room);
            } catch (Throwable e) {
                failed.set(e);
            }
        });
        try (OtherProcess start = new OtherProcess(storage, 3840)) {
            replayTest.start();
            assertTrue("it says it waits", said.await(10, TimeUnit.SECONDS));
            makeReady(storage, 3840);
        }
        replayTest.join(10_000);

        assertFalse(replayTest.isAlive());
        assertNull(failed.get());
        assertEquals("not even asked", 0, room.asked);
        assertEquals(0, updater.initsDownloaded);
    }

    /** The Game card's job drops what its last download left, but not while a start's preparation is getting that version. */
    @Test
    public void aVersionAnotherDownloadHoldsIsNotDropped() throws Exception {
        Storage storage = new Storage(folder.getRoot());
        File unfinished = storage.game(3840);
        GameFiles.write(new File(unfinished, "manifest.json"), "{\"pack_format\": " + GameFiles.PACK_FORMAT + "}");
        File part = new File(storage.downloads(), "env.3840.nx2.part");
        touch(part);

        try (OtherProcess other = new OtherProcess(storage, 3840)) {
            assertFalse(GameFiles.dropUnfinished(storage, 3840));
        }

        assertTrue(unfinished.exists());
        assertTrue(part.exists());
        assertTrue("dropped once it is let go", GameFiles.dropUnfinished(storage, 3840));
        assertFalse(unfinished.exists());
    }
}
