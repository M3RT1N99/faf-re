package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import android.provider.DocumentsContract;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;

import org.junit.After;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

/**
 * The SCFA imports' shared rules (ScfaImport), which the folder import and the ZIP's (ScfaZip) both go by: what they take,
 * what is there already, how a file is written, whole or not at all, and the room for it; and the folder import itself.
 */
public class ScfaImportTest {
    @Rule
    public final TemporaryFolder folder = new TemporaryFolder();

    @After
    public void noCancelLeft() {
        Cancellation.reset();
    }

    /** A picked folder as its provider lists it: its top is "root", and each document's id is its path below the top. */
    private static final class Picked implements ScfaImport.Folder {
        private final Map<String, List<ScfaImport.Child>> mChildren = new HashMap<>();
        private final Map<String, byte[]> mBytes = new HashMap<>();
        private final Set<String> mFolders = new HashSet<>(Collections.singleton("root"));
        /** The documents the import read, in turn. */
        final List<String> opened = new ArrayList<>();

        /** The file at {@code path} ("gamedata/lua.scd"), and its folders. */
        Picked file(String path, String text) {
            byte[] data = text.getBytes(StandardCharsets.UTF_8);
            return file(path, data, data.length);
        }

        /** A file whose size the provider does not say. */
        Picked unsaid(String path, byte[] data) {
            return file(path, data, -1);
        }

        private Picked file(String path, byte[] data, long size) {
            add(path, "application/octet-stream", size);
            mBytes.put(path, data);
            return this;
        }

        private void add(String path, String mime, long size) {
            int slash = path.lastIndexOf('/');
            String parent = slash < 0 ? "root" : path.substring(0, slash);
            if (mFolders.add(parent)) {
                add(parent, DocumentsContract.Document.MIME_TYPE_DIR, -1);
            }
            mChildren.computeIfAbsent(parent, id -> new ArrayList<>())
                .add(new ScfaImport.Child(path, path.substring(slash + 1), mime, size));
        }

        @Override
        public List<ScfaImport.Child> children(String id) {
            return mChildren.getOrDefault(id, Collections.emptyList());
        }

        @Override
        public InputStream open(ScfaImport.Child file) {
            opened.add(file.id);
            return new ByteArrayInputStream(mBytes.get(file.id));
        }
    }

    /** The Steam folder's shape: the import takes 7 of its files, 53 bytes. */
    private static Picked steam() {
        return new Picked()
            .file("bin/SupremeCommander.exe", "program")
            .file("fonts/ARIAL.TTF", "arial")
            .file("fonts/zeroes_3.ttf", "zeroes")
            .file("gamedata/lua.scd", "lua archive")
            .file("gamedata/Advanced strategic icons.scd", "icons archive")
            .file("gamedata/notes.txt", "not an archive")
            .file("maps/SCMP_012/SCMP_012_scenario.lua", "scenario")
            .file("movies/X01_Dostya_M02_03667.sfd", "movie")
            .file("sounds/Voice/us/Tutorial.xwb", "voice")
            .file("installscript.vdf", "steam");
    }

    private static void importFolder(Storage storage, Picked picked, Room room) throws IOException {
        ScfaImport.run(storage, picked, "root", room, System::nanoTime);
    }

    private Storage storage() throws IOException {
        return new Storage(folder.newFolder());
    }

    private static String lastLog(int back) {
        List<String> log = TestState.log();
        return log.get(log.size() - 1 - back);
    }

    private static String target(String... path) {
        return ScfaImport.target(Arrays.asList(path));
    }

    @Test
    public void theImportsTakeGamedatasArchivesAndTheFourFoldersWhole() {
        assertEquals("gamedata/lua.scd", target("gamedata", "lua.scd"));
        assertEquals("gamedata/Advanced strategic icons.scd", target("gamedata", "Advanced strategic icons.scd"));
        // The folders in any case, named in scfa/ as the game reads them; the files keep their names.
        assertEquals("gamedata/ENV.SCD", target("GameData", "ENV.SCD"));
        assertEquals("fonts/ARIAL.TTF", target("Fonts", "ARIAL.TTF"));
        assertEquals("sounds/Voice/us/Tutorial.xwb", target("SOUNDS", "Voice", "us", "Tutorial.xwb"));
        assertEquals("movies/X01_Dostya_M02_03667.sfd", target("movies", "X01_Dostya_M02_03667.sfd"));
        assertEquals("maps/SCMP_012/SCMP_012_scenario.lua", target("maps", "SCMP_012", "SCMP_012_scenario.lua"));
    }

    @Test
    public void theImportsLeaveEverythingElse() {
        // gamedata: its archives alone, not what else it holds, nor its folders.
        assertNull(target("gamedata", "readme.txt"));
        assertNull(target("gamedata", "old", "lua.scd"));
        assertNull(target("bin", "SupremeCommander.exe"));
        assertNull(target("DirectX", "DXSETUP.exe"));
        assertNull(target("installscript.vdf"));
        // A folder's name alone, and a file named as a folder.
        assertNull(target("fonts"));
        assertNull(target("gamedata"));
        assertNull(target());
        // Below another folder: the ZIP's import strips the SCFA folder's own place first.
        assertNull(target("Supreme Commander Forged Alliance", "gamedata", "lua.scd"));
    }

    @Test
    public void whatIsThereAlreadyIsAFileOfTheSameSize() throws IOException {
        File file = folder.newFile("lua.scd");
        Files.write(file.toPath(), new byte[] {1, 2, 3});
        assertTrue(ScfaImport.imported(file, 3));
        assertFalse(ScfaImport.imported(file, 4));
        assertFalse(ScfaImport.imported(new File(folder.getRoot(), "none.scd"), 0));
        assertFalse("a folder is no file", ScfaImport.imported(folder.newFolder("fonts"), 0));
    }

    @Test
    public void aCopyIsWholeOrNotThere() throws IOException {
        File dest = new File(folder.getRoot(), "gamedata/lua.scd");
        ScfaImport.copy(new ByteArrayInputStream("first".getBytes(StandardCharsets.UTF_8)), dest);
        assertEquals("first", new String(Files.readAllBytes(dest.toPath()), StandardCharsets.UTF_8));
        // Another copy takes its place.
        ScfaImport.copy(new ByteArrayInputStream("second".getBytes(StandardCharsets.UTF_8)), dest);
        assertEquals("second", new String(Files.readAllBytes(dest.toPath()), StandardCharsets.UTF_8));
        assertArrayEquals(new String[] {"lua.scd"}, dest.getParentFile().list());
    }

    @Test
    public void aCopyCutOffLeavesNoPartAndWhatWasThere() throws IOException {
        File dest = new File(folder.getRoot(), "gamedata/lua.scd");
        ScfaImport.copy(new ByteArrayInputStream("before".getBytes(StandardCharsets.UTF_8)), dest);
        // A read that fails after its first bytes, as a damaged ZIP's or a provider's does.
        InputStream failing = new FilterInputStream(new ByteArrayInputStream(new byte[1 << 20])) {
            private int reads;

            @Override
            public int read(byte[] buffer, int offset, int length) throws IOException {
                if (++reads == 3) {
                    throw new IOException("invalid entry CRC");
                }
                return super.read(buffer, offset, length);
            }
        };
        IOException failure = assertThrows(IOException.class, () -> ScfaImport.copy(failing, dest));
        assertEquals("invalid entry CRC", failure.getMessage());
        assertEquals("before", new String(Files.readAllBytes(dest.toPath()), StandardCharsets.UTF_8));
        assertArrayEquals(new String[] {"lua.scd"}, dest.getParentFile().list());
        // The user's Cancel, between two reads.
        Cancellation.request();
        assertThrows(Cancellation.Cancelled.class,
            () -> ScfaImport.copy(new ByteArrayInputStream(new byte[1 << 20]), new File(folder.getRoot(), "fonts/a.ttf")));
        assertArrayEquals(new String[0], new File(folder.getRoot(), "fonts").list());
    }

    // --- The folder import -----------------------------------------------------------------------

    @Test
    public void theFolderImportTakesWhatTheZipImportTakesAndClaimsItsRoomFirst() throws IOException {
        Storage storage = storage();
        RoomTest.Fake room = new RoomTest.Fake(1L << 40);

        importFolder(storage, steam(), room);

        assertEquals(Arrays.asList("fonts/ARIAL.TTF", "fonts/zeroes_3.ttf", "gamedata/Advanced strategic icons.scd",
            "gamedata/lua.scd", "maps/SCMP_012/SCMP_012_scenario.lua", "movies/X01_Dostya_M02_03667.sfd",
            "sounds/Voice/us/Tutorial.xwb"), ScfaZipTest.files(storage.scfa));
        assertEquals("the files and the spare", Collections.singletonList(53 + Room.SPARE_BYTES), room.claims);
        assertEquals("Imported 2 archives into " + new File(storage.scfa, "gamedata"), lastLog(4));
        assertEquals("Imported maps/ (1 files)", lastLog(3));
        assertEquals("Imported fonts/ (2 files)", lastLog(2));
        assertEquals("Imported sounds/ (1 files)", lastLog(1));
        assertEquals("Imported movies/ (1 files)", lastLog(0));
    }

    /** Neither read nor written: the folder is listed first, and its files must fit with the spare, as the ZIP's must. */
    @Test
    public void tooLittleRoomIsSaidBeforeTheFolderImportReadsAFile() throws IOException {
        Storage storage = storage();
        Picked picked = steam();
        RoomTest.Fake tooLittle = new RoomTest.Fake(Room.SPARE_BYTES + 52);

        Room.NoRoom noRoom = assertThrows(Room.NoRoom.class, () -> importFolder(storage, picked, tooLittle));

        assertEquals("the import needs 101 MB: free 1 MB more on the phone", noRoom.getMessage());
        assertEquals(Collections.emptyList(), tooLittle.claims);
        assertEquals(Collections.emptyList(), picked.opened);
        assertEquals(Collections.emptyList(), ScfaZipTest.files(storage.scfa));
    }

    /** Never another measure in its place: Android's failure is the import's, before it reads a file. */
    @Test
    public void aRoomCallThatAndroidFailsStopsTheFolderImportBeforeItReadsAFile() throws IOException {
        Picked picked = steam();
        RoomTest.Fake failing = new RoomTest.Fake(1L << 40);
        failing.allocatableFails = new IOException("Android cannot say how much the app may write to the phone's storage: "
            + "no such volume");

        assertSame(failing.allocatableFails, assertThrows(IOException.class, () -> importFolder(storage(), picked, failing)));

        assertEquals(Collections.emptyList(), picked.opened);
    }

    /** What an earlier import left is neither counted nor read again: an import with nothing to write asks for no room. */
    @Test
    public void whatIsThereAlreadyIsNeitherCountedNorWrittenAgain() throws IOException {
        Storage storage = storage();
        importFolder(storage, steam(), new RoomTest.Fake(1L << 40));

        Picked again = steam();
        RoomTest.Fake none = new RoomTest.Fake(0);
        importFolder(storage, again, none);
        assertEquals("not even asked", 0, none.asked);
        assertEquals(Collections.emptyList(), again.opened);

        Picked newer = new Picked().file("gamedata/lua.scd", "a newer lua archive").file("fonts/ARIAL.TTF", "arial");
        RoomTest.Fake room = new RoomTest.Fake(1L << 40);
        importFolder(storage, newer, room);
        assertEquals(Collections.singletonList("gamedata/lua.scd"), newer.opened);
        assertEquals(Collections.singletonList(19 + Room.SPARE_BYTES), room.claims);
    }

    /**
     * Room at the claim and the first two files, then 10 bytes, as another app fills the phone while the import writes: it
     * stops with what the next import needs, the 29 bytes of the five files left and the spare. The two written stay
     * whole, and the next import reads the five alone.
     */
    @Test
    public void roomThatRunsShortAsTheFolderImportWritesStopsItWithWhatItWroteWhole() throws IOException {
        Storage storage = storage();
        AtomicInteger asked = new AtomicInteger();

        IOException stopped = assertThrows(IOException.class, () -> ScfaImport.run(storage, steam(), "root",
            new RoomTest.Fake(() -> asked.incrementAndGet() <= 3 ? 1L << 40 : 10), ScfaZipTest.everyLookAQuestion()));

        assertEquals("the phone's storage ran short while the import wrote: free 101 MB more on the phone", stopped.getMessage());
        assertEquals(Arrays.asList("gamedata/Advanced strategic icons.scd", "gamedata/lua.scd"), ScfaZipTest.files(storage.scfa));
        Picked rest = steam();
        RoomTest.Fake room = new RoomTest.Fake(1L << 40);
        importFolder(storage, rest, room);
        assertEquals(5, rest.opened.size());
        assertEquals(Collections.singletonList(29 + Room.SPARE_BYTES), room.claims);
    }

    /** As the ZIP's: the claim's question for 7 files written at once, and with a clock 10 s on at each look every third. */
    @Test
    public void theFolderImportAsksForTheRoomAtMostEvery30Seconds() throws IOException {
        RoomTest.Fake room = new RoomTest.Fake(1L << 40);
        importFolder(storage(), steam(), room);
        assertEquals("the claim's question alone", 1, room.asked);

        AtomicLong clock = new AtomicLong();
        RoomTest.Fake paced = new RoomTest.Fake(1L << 40);
        ScfaImport.run(storage(), steam(), "root", paced, () -> clock.addAndGet(TimeUnit.SECONDS.toNanos(10)));
        assertEquals("the claim's, the third file's at 40 s and the sixth's at 70 s", 3, paced.asked);
    }

    /**
     * A file whose size the provider does not say counts for nothing in the claim, and its bytes nothing against the rest:
     * the room that runs short for the file after it is still seen.
     */
    @Test
    public void aFileWhoseSizeTheProviderDoesNotSayCountsForNothing() throws IOException {
        Storage storage = storage();
        Picked picked = new Picked().unsaid("gamedata/env.scd", new byte[1000]).file("fonts/a.ttf", "font");
        AtomicInteger asked = new AtomicInteger();
        RoomTest.Fake room = new RoomTest.Fake(() -> asked.incrementAndGet() <= 2 ? 1L << 40 : 3);

        IOException stopped = assertThrows(IOException.class,
            () -> ScfaImport.run(storage, picked, "root", room, ScfaZipTest.everyLookAQuestion()));

        assertEquals(Collections.singletonList(4 + Room.SPARE_BYTES), room.claims);
        assertEquals("the phone's storage ran short while the import wrote: free 101 MB more on the phone", stopped.getMessage());
        assertEquals(Collections.singletonList("gamedata/env.scd"), ScfaZipTest.files(storage.scfa));
    }
}
