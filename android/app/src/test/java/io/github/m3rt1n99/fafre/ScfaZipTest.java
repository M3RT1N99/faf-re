package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.junit.Assume.assumeTrue;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.LongSupplier;
import java.util.zip.CRC32;
import java.util.zip.Deflater;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

import org.junit.After;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

/**
 * The SCFA import from a ZIP (ScfaZip): which ZIPs it takes, by their first bytes; where the SCFA folder is in one and what
 * it takes of it, by the folder import's rules; the guard on every path (Zip Slip); the room it needs, up front and as it
 * goes; that a refusal, a failure or a Cancel leaves scfa/ as it was; and ZIP64, an entry of more than 4 GB before the
 * game's files. The stored one, which needs 4 GB on the disk, runs with FAFRE_ZIP64_DIR naming a folder for it:
 * {@code FAFRE_ZIP64_DIR=<a folder with 4.1 GB free> ./gradlew :app:testReleaseUnitTest --tests '*ScfaZipTest' --rerun}.
 */
public class ScfaZipTest {
    /** More than ZIP's 32 bits hold: 4 GiB and 1 MiB. */
    private static final long OVER_4_GB = (4L << 30) + (1 << 20);
    /** The phone's room: far more than any import here needs. */
    private final Room roomy = new RoomTest.Fake(1L << 40);

    @Rule
    public final TemporaryFolder folder = new TemporaryFolder();

    @After
    public void noCancelLeft() {
        Cancellation.reset();
    }

    /** A ZIP made in memory, entry by entry, each deflated or stored, as its maker writes them. */
    private static final class Zip {
        private final ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        private final ZipOutputStream out;

        Zip() {
            out = new ZipOutputStream(bytes);
        }

        Zip(Charset names) {
            out = new ZipOutputStream(bytes, names);
        }

        Zip deflated(String name, String text) throws IOException {
            out.putNextEntry(new ZipEntry(name));
            out.write(text.getBytes(StandardCharsets.UTF_8));
            out.closeEntry();
            return this;
        }

        Zip stored(String name, String text) throws IOException {
            byte[] data = text.getBytes(StandardCharsets.UTF_8);
            ZipEntry entry = new ZipEntry(name);
            entry.setMethod(ZipEntry.STORED);
            entry.setSize(data.length);
            entry.setCompressedSize(data.length);
            CRC32 crc = new CRC32();
            crc.update(data);
            entry.setCrc(crc.getValue());
            out.putNextEntry(entry);
            out.write(data);
            out.closeEntry();
            return this;
        }

        Zip folder(String name) throws IOException {
            out.putNextEntry(new ZipEntry(name));
            out.closeEntry();
            return this;
        }

        byte[] bytes() throws IOException {
            out.close();
            return bytes.toByteArray();
        }
    }

    /** The Steam folder's shape, in a folder of its own as Windows' "Send to, Compressed folder" makes it. */
    private static byte[] scfaInAFolder() throws IOException {
        return new Zip()
            .folder("Supreme Commander Forged Alliance/")
            .stored("Supreme Commander Forged Alliance/bin/SupremeCommander.exe", "program")
            .deflated("Supreme Commander Forged Alliance/fonts/ARIAL.TTF", "arial")
            .stored("Supreme Commander Forged Alliance/fonts/zeroes_3.ttf", "zeroes")
            .folder("Supreme Commander Forged Alliance/gamedata/")
            .stored("Supreme Commander Forged Alliance/gamedata/lua.scd", "lua archive")
            .deflated("Supreme Commander Forged Alliance/gamedata/Advanced strategic icons.scd", "icons archive")
            .deflated("Supreme Commander Forged Alliance/gamedata/notes.txt", "not an archive")
            .deflated("Supreme Commander Forged Alliance/maps/SCMP_012/SCMP_012_scenario.lua", "scenario")
            .deflated("Supreme Commander Forged Alliance/movies/X01_Dostya_M02_03667.sfd", "movie")
            .stored("Supreme Commander Forged Alliance/sounds/Voice/us/Tutorial.xwb", "voice")
            .deflated("Supreme Commander Forged Alliance/installscript.vdf", "steam")
            .bytes();
    }

    private static ScfaZip.Source source(byte[] zip) {
        return () -> new ByteArrayInputStream(zip);
    }

    private static ScfaZip.Source source(File zip) {
        return () -> new FileInputStream(zip);
    }

    private Storage storage() throws IOException {
        return new Storage(folder.newFolder("files"));
    }

    private static String read(File file) throws IOException {
        return new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
    }

    /** Every file under {@code dir}, as paths relative to it with "/", sorted. */
    private static List<String> files(File dir) {
        List<String> out = new ArrayList<>();
        collect(dir, "", out);
        out.sort(null);
        return out;
    }

    private static void collect(File dir, String prefix, List<String> out) {
        File[] children = dir.listFiles();
        if (children != null) {
            for (File child : children) {
                if (child.isDirectory()) {
                    collect(child, prefix + child.getName() + "/", out);
                } else {
                    out.add(prefix + child.getName());
                }
            }
        }
    }

    private static ScfaZip.Entry entry(String name, long size) throws ScfaZip.Refused {
        return new ScfaZip.Entry(name, ScfaZip.path(name), size);
    }

    private static String lastLog(int back) {
        List<String> log = TestState.log();
        return log.get(log.size() - 1 - back);
    }

    // --- The first bytes -----------------------------------------------------------------------

    @Test
    public void aZipIsKnownByItsFirstBytes() throws IOException {
        byte[] zip = scfaInAFolder();
        assertNull(ScfaZip.notZip(zip, 4));
        assertEquals("the ZIP is empty", ScfaZip.notZip(new Zip().bytes(), 22));
        assertEquals("the file is empty", ScfaZip.notZip(new byte[4], 0));
        String not = "the file does not begin as a ZIP does, whatever its name or type say";
        assertEquals(not, ScfaZip.notZip(new byte[] {'P', 'K', 3}, 3));
        assertEquals(not, ScfaZip.notZip(new byte[] {'7', 'z', (byte) 0xBC, (byte) 0xAF, 0x27, 0x1C}, 6));
        assertEquals(not, ScfaZip.notZip(new byte[] {'R', 'a', 'r', '!'}, 4));
        assertEquals(not, ScfaZip.notZip("MZ\u0090\u0000".getBytes(StandardCharsets.ISO_8859_1), 4));
    }

    @Test
    public void theImportGoesByTheBytesNotByTheFilesName() throws IOException {
        Storage storage = storage();
        // A text file a provider calls application/zip: the import never asks the type.
        File text = folder.newFile("scfa.zip");
        Files.write(text.toPath(), "Supreme Commander".getBytes(StandardCharsets.UTF_8));
        ScfaZip.Refused refused = assertThrows(ScfaZip.Refused.class, () -> ScfaZip.run(storage, source(text), roomy));
        assertEquals("the file does not begin as a ZIP does, whatever its name or type say", refused.getMessage());
        // A ZIP of any name.
        File zip = folder.newFile("scfa.bin");
        Files.write(zip.toPath(), scfaInAFolder());
        ScfaZip.run(storage, source(zip), roomy);
        assertEquals("lua archive", read(new File(storage.scfa, "gamedata/lua.scd")));
    }

    // --- Paths -----------------------------------------------------------------------------------

    @Test
    public void anEntrysNameIsItsPathInTheZipsFolder() throws ScfaZip.Refused {
        assertEquals(Arrays.asList("gamedata", "lua.scd"), ScfaZip.path("gamedata/lua.scd"));
        assertEquals(Arrays.asList("SCFA", "gamedata"), ScfaZip.path("SCFA/gamedata/"));
        // "\" as older tools on Windows write it, "." and empty parts left out.
        assertEquals(Arrays.asList("SCFA", "gamedata", "lua.scd"), ScfaZip.path("SCFA\\gamedata\\lua.scd"));
        assertEquals(Arrays.asList("SCFA", "fonts", "a.ttf"), ScfaZip.path("./SCFA//fonts/./a.ttf"));
        // Names that only look odd stay: a drive's letter names a folder on Android, as dots in a name do.
        assertEquals(Arrays.asList("C:", "gamedata"), ScfaZip.path("C:/gamedata"));
        assertEquals(Arrays.asList("maps", "a..b", "x.."), ScfaZip.path("maps/a..b/x.."));
    }

    @Test
    public void aNameThatLeavesTheZipsFolderIsRefused() {
        for (String name : new String[] {"../evil.txt", "gamedata/../../evil.scd", "/etc/evil", "\\evil", "..\\evil.txt",
            "gamedata\\..\\..\\evil.scd", "SCFA/..", "../"}) {
            ScfaZip.Refused refused = assertThrows(name, ScfaZip.Refused.class, () -> ScfaZip.path(name));
            assertEquals("the ZIP's entry " + name + " would leave its folder (Zip Slip): the import takes nothing of it",
                refused.getMessage());
        }
    }

    @Test
    public void everyFileTheImportWritesStaysInItsFolder() throws IOException {
        File scfa = folder.newFolder("scfa");
        assertEquals(new File(scfa, "gamedata/lua.scd").getCanonicalPath(),
            ScfaZip.under(scfa, "gamedata/lua.scd").getCanonicalPath());
        for (String relative : new String[] {"../evil.txt", "gamedata/../../evil.txt", "..", "."}) {
            ScfaZip.Refused refused = assertThrows(relative, ScfaZip.Refused.class, () -> ScfaZip.under(scfa, relative));
            assertEquals(relative + " would leave scfa/ (Zip Slip): the import writes nothing there", refused.getMessage());
        }
        // A sibling whose name begins as the folder's is outside too.
        assertThrows(ScfaZip.Refused.class, () -> ScfaZip.under(scfa, "../scfa2/x"));
    }

    @Test
    public void aZipSlipEntryRefusesTheWholeZipBeforeAnythingIsWritten() throws IOException {
        for (String evil : new String[] {"../evil.txt", "gamedata\\..\\..\\evil.scd"}) {
            Storage storage = new Storage(folder.newFolder());
            byte[] zip = new Zip().deflated("gamedata/lua.scd", "lua archive").deflated(evil, "evil")
                .deflated("fonts/a.ttf", "font").bytes();
            ScfaZip.Refused refused = assertThrows(evil, ScfaZip.Refused.class, () -> ScfaZip.run(storage, source(zip), roomy));
            assertTrue(refused.getMessage(), refused.getMessage().contains("(Zip Slip)"));
            assertEquals(evil, Arrays.asList(), files(storage.root.getParentFile()).stream()
                .filter(name -> name.contains("evil")).collect(java.util.stream.Collectors.toList()));
            assertFalse(evil, storage.scfa.exists());
        }
    }

    // --- The SCFA folder's place and what is taken -------------------------------------------------

    @Test
    public void theScfaFolderIsTheZipsTopOrOneFolderThere() throws IOException {
        File scfa = folder.newFolder("scfa");
        ScfaZip.Plan top = ScfaZip.plan(Arrays.asList(entry("bin/x.exe", 1), entry("fonts/a.ttf", 2),
            entry("gamedata/lua.scd", 3)), scfa);
        assertEquals("", top.root);
        assertArrayEquals(new String[] {null, "fonts/a.ttf", "gamedata/lua.scd"}, top.writes);
        // One folder down, found in any case, spelled as first seen: a gamedata folder's own entry is enough to place it.
        ScfaZip.Plan down = ScfaZip.plan(Arrays.asList(entry("SCFA/fonts/a.ttf", 2), entry("scfa/GameData/", 0),
            entry("Scfa/Gamedata/LUA.SCD", 3)), scfa);
        assertEquals("scfa", down.root);
        assertArrayEquals(new String[] {"fonts/a.ttf", null, "gamedata/LUA.SCD"}, down.writes);
    }

    @Test
    public void aZipWithoutOneScfaFolderIsRefused() throws IOException {
        File scfa = folder.newFolder("scfa");
        String none = "the ZIP has no gamedata folder, neither at its top nor in a folder there: zip the Supreme Commander "
            + "Forged Alliance folder";
        // Two folders down, as a ZIP of Steam's common/ folder has it.
        assertRefused(none, Arrays.asList(entry("common/SCFA/gamedata/lua.scd", 3)), scfa);
        // A file named gamedata is no folder; fonts alone are not the SCFA folder.
        assertRefused(none, Arrays.asList(entry("gamedata", 3), entry("fonts/a.ttf", 2)), scfa);
        assertRefused("the ZIP has a gamedata folder in 2 places (Supreme Commander/, Supreme Commander Forged Alliance/): "
            + "zip one Supreme Commander Forged Alliance folder", Arrays.asList(entry("Supreme Commander/gamedata/lua.scd", 3),
                entry("Supreme Commander Forged Alliance/gamedata/lua.scd", 4)), scfa);
        assertRefused("the ZIP has a gamedata folder in 2 places (its top, backup/): zip one Supreme Commander Forged Alliance "
            + "folder", Arrays.asList(entry("gamedata/lua.scd", 3), entry("backup/gamedata/", 0)), scfa);
    }

    private static void assertRefused(String message, List<ScfaZip.Entry> entries, File scfa) {
        ScfaZip.Refused refused = assertThrows(ScfaZip.Refused.class, () -> ScfaZip.plan(entries, scfa));
        assertEquals(message, refused.getMessage());
    }

    @Test
    public void thePlanCountsWhatItTakesWhatIsThereAndWhatItLeaves() throws IOException {
        File scfa = folder.newFolder("scfa");
        File there = new File(scfa, "fonts/a.ttf");
        Storage.mkdirs(there.getParentFile());
        Files.write(there.toPath(), new byte[2]);
        File older = new File(scfa, "gamedata/lua.scd");
        Storage.mkdirs(older.getParentFile());
        Files.write(older.toPath(), new byte[1]);
        ScfaZip.Plan plan = ScfaZip.plan(Arrays.asList(entry("X/", 0), entry("X/bin/x.exe", 100), entry("X/fonts/a.ttf", 2),
            entry("X/gamedata/lua.scd", 30), entry("X/gamedata/notes.txt", 5), entry("X/sounds/v/a.xwb", 7),
            entry("Y/gamedata.txt", 1)), scfa);
        // The font is there; the archive of another size goes in the place of the one there.
        assertArrayEquals(new String[] {null, null, null, "gamedata/lua.scd", null, "sounds/v/a.xwb", null}, plan.writes);
        assertEquals(37, plan.bytes);
        assertEquals(2, plan.files);
        assertEquals(5, plan.last);
        assertEquals(1, plan.there);
        assertEquals(3, plan.others);
        assertEquals("{fonts=1, gamedata=1, sounds=1}", plan.taken.toString());
    }

    @Test
    public void aFileTheZipHoldsTwiceCountsOnceAsItsLaterEntry() throws IOException {
        Storage storage = storage();
        // Also in another case, which Android's shared storage and the engine take for the same file.
        byte[] zip = new Zip().deflated("gamedata/lua.scd", "first").deflated("fonts/a.ttf", "font")
            .stored("GameData/Lua.SCD", "second").bytes();
        ScfaZip.Plan plan = ScfaZip.plan(ScfaZip.list(source(zip)), storage.scfa);
        assertArrayEquals(new String[] {null, "fonts/a.ttf", "gamedata/Lua.SCD"}, plan.writes);
        assertEquals(1, plan.others);
        ScfaZip.run(storage, source(zip), roomy);
        assertEquals(Arrays.asList("fonts/a.ttf", "gamedata/Lua.SCD"), files(storage.scfa));
        assertEquals("second", read(new File(storage.scfa, "gamedata/Lua.SCD")));
    }

    // --- The import ------------------------------------------------------------------------------

    @Test
    public void theScfaFolderInAFolderOfTheZipComesIntoScfa() throws IOException {
        Storage storage = storage();
        ScfaZip.run(storage, source(scfaInAFolder()), roomy);
        assertEquals(Arrays.asList("fonts/ARIAL.TTF", "fonts/zeroes_3.ttf", "gamedata/Advanced strategic icons.scd",
            "gamedata/lua.scd", "maps/SCMP_012/SCMP_012_scenario.lua", "movies/X01_Dostya_M02_03667.sfd",
            "sounds/Voice/us/Tutorial.xwb"), files(storage.scfa));
        assertEquals("arial", read(new File(storage.scfa, "fonts/ARIAL.TTF")));
        assertEquals("icons archive", read(new File(storage.scfa, "gamedata/Advanced strategic icons.scd")));
        assertEquals("voice", read(new File(storage.scfa, "sounds/Voice/us/Tutorial.xwb")));
        assertEquals(2, ScfaImport.archives(storage));
        assertEquals(2, ScfaImport.fonts(storage));
        // The folder import's lines, after the ZIP's own.
        assertEquals("The ZIP has the SCFA folder in Supreme Commander Forged Alliance/: the import takes 7 of its files "
            + "(0 there already) and leaves 3", lastLog(5));
        assertEquals("Imported 2 archives into " + new File(storage.scfa, "gamedata"), lastLog(4));
        assertEquals("Imported maps/ (1 files)", lastLog(3));
        assertEquals("Imported fonts/ (2 files)", lastLog(2));
        assertEquals("Imported sounds/ (1 files)", lastLog(1));
        assertEquals("Imported movies/ (1 files)", lastLog(0));
    }

    @Test
    public void theScfaFolderAtTheZipsTopComesIntoScfa() throws IOException {
        Storage storage = storage();
        byte[] zip = new Zip().stored("bin/x.exe", "program").deflated("gamedata/lua.scd", "lua archive")
            .deflated("Movies/a.sfd", "movie").bytes();
        ScfaZip.run(storage, source(zip), roomy);
        assertEquals(Arrays.asList("gamedata/lua.scd", "movies/a.sfd"), files(storage.scfa));
        assertEquals("No fonts: the ZIP has no fonts folder with .ttf files, and the game cannot start without them",
            lastLog(0));
    }

    @Test
    public void whatIsThereAlreadyIsNotWrittenAgain() throws IOException {
        Storage storage = storage();
        ScfaZip.run(storage, source(scfaInAFolder()), roomy);
        File lua = new File(storage.scfa, "gamedata/lua.scd");
        long written = lua.lastModified();
        // A second read would find what changed: the ZIP is read once when it has nothing to write, which needs no room.
        AtomicInteger opens = new AtomicInteger();
        byte[] zip = scfaInAFolder();
        RoomTest.Fake none = new RoomTest.Fake(0);
        ScfaZip.run(storage, () -> {
            opens.incrementAndGet();
            return new ByteArrayInputStream(zip);
        }, none);
        assertEquals(1, opens.get());
        assertEquals("not even asked", 0, none.asked);
        assertEquals(written, lua.lastModified());
        assertTrue(lastLog(5), lastLog(5).endsWith("the import takes 7 of its files (7 there already) and leaves 3"));
    }

    @Test
    public void tooLittleRoomIsSaidBeforeAnythingIsWritten() throws IOException {
        Storage storage = storage();
        long bytes = (300L << 20) + 1;
        byte[] zip = new Zip().deflated("gamedata/lua.scd", "lua archive").bytes();
        // The list's size is what counts: a big file, small in the ZIP.
        File big = folder.newFile("big.zip");
        try (ZipOutputStream out = new ZipOutputStream(new FileOutputStream(big))) {
            out.putNextEntry(new ZipEntry("gamedata/env.scd"));
            zeros(out, bytes);
            out.closeEntry();
        }
        assertTrue(big.length() < (1 << 20));
        RoomTest.Fake tooLittle = new RoomTest.Fake(350L << 20);
        Room.NoRoom noRoom = assertThrows(Room.NoRoom.class, () -> ScfaZip.run(storage, source(big), tooLittle));
        assertEquals("the import needs 400 MB: free 51 MB more on the phone", noRoom.getMessage());
        assertEquals(Arrays.asList(), tooLittle.claims);
        assertFalse(storage.scfa.exists());
        // What fits, with the spare the downloads leave too, claimed before the import writes.
        RoomTest.Fake fits = new RoomTest.Fake(Room.SPARE_BYTES + 11);
        ScfaZip.run(storage, source(zip), fits);
        assertEquals(Arrays.asList(Room.SPARE_BYTES + 11), fits.claims);
        assertEquals("lua archive", read(new File(storage.scfa, "gamedata/lua.scd")));
    }

    /** Never another measure in its place: Android's failure is the import's, before it writes anything. */
    @Test
    public void aRoomCallThatAndroidFailsStopsTheImportBeforeItWrites() throws IOException {
        Storage storage = storage();
        RoomTest.Fake failing = new RoomTest.Fake(1L << 40);
        failing.allocatableFails = new IOException("Android cannot say how much the app may write to the phone's storage: "
            + "no such volume");
        assertSame(failing.allocatableFails,
            assertThrows(IOException.class, () -> ScfaZip.run(storage, source(scfaInAFolder()), failing)));
        failing.allocatableFails = null;
        failing.allocateFails = new IOException("Android cannot clear 100 MB on the phone's storage for the write: not freed");
        assertSame(failing.allocateFails,
            assertThrows(IOException.class, () -> ScfaZip.run(storage, source(scfaInAFolder()), failing)));
        assertEquals(Arrays.asList(), files(storage.scfa));
    }

    /** A clock that has gone on by the time between two of the import's questions for the room at each look at it. */
    private static LongSupplier everyLookAQuestion() {
        AtomicLong clock = new AtomicLong();
        return () -> clock.addAndGet(ScfaZip.ASK_ROOM_EVERY_NANOS);
    }

    @Test
    public void roomThatRunsShortAsTheImportWritesStopsItCleanly() throws IOException {
        Storage storage = storage();
        AtomicInteger asked = new AtomicInteger();
        // Room at the claim, then 10 bytes, as another app fills the phone while the import writes: the next import needs
        // the 53 bytes of the files and the spare, and finds these 10 bytes.
        ScfaZip.Refused refused = assertThrows(ScfaZip.Refused.class, () -> ScfaZip.run(storage, source(scfaInAFolder()),
            new RoomTest.Fake(() -> asked.incrementAndGet() == 1 ? 1L << 40 : 10), everyLookAQuestion()));
        assertEquals("the phone's storage ran short while the import wrote: free 101 MB more on the phone", refused.getMessage());
        assertEquals(Arrays.asList(), files(storage.scfa));
    }

    @Test
    public void roomIsCheckedAgainWithinABigFile() throws IOException {
        Storage storage = storage();
        File big = folder.newFile("big.zip");
        try (ZipOutputStream out = new ZipOutputStream(new FileOutputStream(big))) {
            out.putNextEntry(new ZipEntry("gamedata/env.scd"));
            zeros(out, 3 * ScfaZip.CHECK_ROOM_EVERY);
            out.closeEntry();
        }
        AtomicInteger asked = new AtomicInteger();
        // At the claim and at the file's start there is room; after 64 MB of it there is none, and the next import needs the
        // 128 MB still to write and the spare.
        ScfaZip.Refused refused = assertThrows(ScfaZip.Refused.class, () -> ScfaZip.run(storage, source(big),
            new RoomTest.Fake(() -> asked.incrementAndGet() <= 2 ? 1L << 40 : 0), everyLookAQuestion()));
        assertEquals(3, asked.get());
        assertEquals("the phone's storage ran short while the import wrote: free 228 MB more on the phone", refused.getMessage());
        assertEquals(Arrays.asList(), files(storage.scfa));
    }

    /**
     * Android answers by counting the caches it may clear (StorageManager.getAllocatableBytes, which may take seconds): an
     * import of an SCFA folder's 1,800 files asks at its claim and then at most every 30 s, not at each file.
     */
    @Test
    public void theRoomIsAskedAgainAtMostEvery30SecondsNotAtEachFile() throws IOException {
        RoomTest.Fake room = new RoomTest.Fake(1L << 40);
        ScfaZip.run(storage(), source(scfaInAFolder()), room);
        assertEquals("the claim's question alone for 7 files written at once", 1, room.asked);

        // 10 s on at each look at the clock: the claim's at 10 s, then the 7 files at 20 to 80 s.
        AtomicLong clock = new AtomicLong();
        RoomTest.Fake paced = new RoomTest.Fake(1L << 40);
        ScfaZip.run(new Storage(folder.newFolder()), source(scfaInAFolder()), paced,
            () -> clock.addAndGet(TimeUnit.SECONDS.toNanos(10)));
        assertEquals("the claim's, the third file's at 40 s and the sixth's at 70 s", 3, paced.asked);
    }

    @Test
    public void aCancelLeavesScfaAsItWas() throws IOException {
        Storage storage = storage();
        byte[] earlier = new Zip().deflated("gamedata/lua.scd", "old lua").bytes();
        ScfaZip.run(storage, source(earlier), roomy);
        byte[] zip = scfaInAFolder();
        AtomicInteger opens = new AtomicInteger();
        // The user's Cancel while the second read writes the files.
        ScfaZip.Source cancelling = () -> {
            InputStream in = new ByteArrayInputStream(zip);
            return opens.incrementAndGet() == 1 ? in : new FilterInputStream(in) {
                private long read;

                @Override
                public int read(byte[] buffer, int offset, int length) throws IOException {
                    int n = super.read(buffer, offset, length);
                    if ((read += Math.max(n, 0)) > zip.length / 2) {
                        Cancellation.request();
                    }
                    return n;
                }
            };
        };
        assertThrows(Cancellation.Cancelled.class, () -> ScfaZip.run(storage, cancelling, roomy));
        assertEquals(2, opens.get());
        assertEquals(Arrays.asList("gamedata/lua.scd"), files(storage.scfa));
        assertEquals("old lua", read(new File(storage.scfa, "gamedata/lua.scd")));
    }

    @Test
    public void whatAnImportWhoseProcessEndedLeftGoesAtTheNext() throws IOException {
        Storage storage = storage();
        leftover(storage, 3);
        ScfaZip.run(storage, source(scfaInAFolder()), roomy);
        assertFalse(new File(storage.scfa, ScfaZip.STAGING).exists());
        assertEquals(7, files(storage.scfa).size());
        // Also at a ZIP the import refuses, which writes nothing.
        leftover(storage, 3);
        assertThrows(ScfaZip.Refused.class,
            () -> ScfaZip.run(storage, source("not a zip".getBytes(StandardCharsets.UTF_8)), roomy));
        assertFalse(new File(storage.scfa, ScfaZip.STAGING).exists());
    }

    @Test
    public void theRoomWhatAnImportWhoseProcessEndedLeftHeldIsTheNextOnes() throws IOException {
        Storage storage = storage();
        leftover(storage, 1 << 20);
        byte[] zip = new Zip().deflated("gamedata/lua.scd", "lua archive").bytes();
        // The phone's room as Android has it, less what the app's files hold: room for the ZIP's file and the spare, and
        // half of what the leftover holds.
        long room = Room.SPARE_BYTES + "lua archive".length() + (512 << 10);
        ScfaZip.run(storage, source(zip), new RoomTest.Fake(() -> room - held(storage.root)));
        assertEquals("lua archive", read(new File(storage.scfa, "gamedata/lua.scd")));
        assertFalse(new File(storage.scfa, ScfaZip.STAGING).exists());
    }

    /** A file of {@code bytes} that an import whose process ended left in its folder. */
    private static void leftover(Storage storage, int bytes) throws IOException {
        File left = new File(storage.scfa, ScfaZip.STAGING + "/movies/a.sfd.part");
        Storage.mkdirs(left.getParentFile());
        Files.write(left.toPath(), new byte[bytes]);
    }

    /** The bytes of the files under {@code dir}. */
    private static long held(File dir) {
        long bytes = 0;
        File[] children = dir.listFiles();
        if (children != null) {
            for (File child : children) {
                bytes += child.isDirectory() ? held(child) : child.length();
            }
        }
        return bytes;
    }

    @Test
    public void aZipThatChangesBetweenItsReadsStops() throws IOException {
        Storage storage = storage();
        byte[] first = scfaInAFolder();
        byte[] second = new Zip().deflated("Supreme Commander Forged Alliance/fonts/ARIAL.TTF", "arial")
            .deflated("Supreme Commander Forged Alliance/gamedata/evil.scd", "another archive").bytes();
        AtomicInteger opens = new AtomicInteger();
        ScfaZip.Refused refused = assertThrows(ScfaZip.Refused.class, () -> ScfaZip.run(storage,
            () -> new ByteArrayInputStream(opens.incrementAndGet() == 1 ? first : second), roomy));
        assertEquals("the ZIP changed while the import read it: pick it again once nothing writes it", refused.getMessage());
        assertEquals(Arrays.asList(), files(storage.scfa));
    }

    @Test
    public void aDamagedZipIsRefusedBeforeAnythingIsWritten() throws IOException {
        Storage storage = storage();
        byte[] zip = scfaInAFolder();
        // A byte of the stored archive's data: its CRC no longer holds.
        int at = indexOf(zip, "lua archive".getBytes(StandardCharsets.UTF_8));
        zip[at] ^= 1;
        ScfaZip.Refused refused = assertThrows(ScfaZip.Refused.class, () -> ScfaZip.run(storage, source(zip), roomy));
        assertTrue(refused.getMessage(), refused.getMessage().startsWith("the ZIP cannot be read: invalid entry CRC"));
        assertFalse(storage.scfa.exists());
        // Cut off in the middle.
        byte[] cut = Arrays.copyOf(scfaInAFolder(), at);
        refused = assertThrows(ScfaZip.Refused.class, () -> ScfaZip.run(storage, source(cut), roomy));
        assertTrue(refused.getMessage(), refused.getMessage().startsWith("the ZIP cannot be read: "));
        assertFalse(storage.scfa.exists());
    }

    @Test
    public void aNameNotInUtf8IsSaidSo() throws IOException {
        Storage storage = storage();
        // As an older tool on Windows writes names, in its code page without ZIP's UTF-8 flag.
        byte[] zip = new Zip(Charset.forName("IBM437")).deflated("Spiele/gamedata/lua.scd", "lua archive")
            .deflated("Spiele/maps/Kühler Fluss/x_scenario.lua", "scenario").bytes();
        ScfaZip.Refused refused = assertThrows(ScfaZip.Refused.class, () -> ScfaZip.run(storage, source(zip), roomy));
        assertEquals("the ZIP has an entry whose name is not UTF-8, as older tools on Windows write them: zip the folder "
            + "again with 7-Zip", refused.getMessage());
        assertFalse(storage.scfa.exists());
    }

    @Test
    public void aZipOfFarMoreEntriesThanScfaHasIsRefused() throws IOException {
        Zip zip = new Zip().deflated("gamedata/lua.scd", "lua archive");
        for (int i = 0; i < ScfaZip.MOST_ENTRIES; ++i) {
            zip.folder("maps/" + i + "/");
        }
        byte[] bytes = zip.bytes();
        ScfaZip.Refused refused = assertThrows(ScfaZip.Refused.class, () -> ScfaZip.list(source(bytes)));
        assertEquals("the ZIP has more than 100000 entries, far more than the SCFA folder has files", refused.getMessage());
    }

    // --- ZIP64 -----------------------------------------------------------------------------------

    @Test
    public void zip64DeflatedEntryOver4GbBeforeTheGamesFiles() throws IOException {
        // Small on the disk: zeros deflate to a two-hundredth at the fastest level (18.7 MB). The ZIP's maker writes the
        // size after the data (a data descriptor), in ZIP64's 8 bytes, as Java's ZipOutputStream does.
        File zip = folder.newFile("zip64-deflated.zip");
        try (ZipOutputStream out = new ZipOutputStream(new FileOutputStream(zip))) {
            out.setLevel(Deflater.BEST_SPEED);
            out.putNextEntry(new ZipEntry("SCFA/bin/zeros.bin"));
            zeros(out, OVER_4_GB);
            out.closeEntry();
            gamesFiles(out);
        }
        assertTrue(zip.length() + " bytes", zip.length() < (32 << 20));
        assertZip64Import(zip);
    }

    @Test
    public void zip64StoredEntryOver4GbBeforeTheGamesFiles() throws IOException {
        // 4 GB on the disk: only where FAFRE_ZIP64_DIR names a folder for it.
        String dir = System.getenv("FAFRE_ZIP64_DIR");
        assumeTrue("FAFRE_ZIP64_DIR names a folder with 4.1 GB free", dir != null && !dir.isEmpty());
        File zip = new File(dir, "zip64-stored.zip");
        try {
            try (OutputStream out0 = new FileOutputStream(zip); ZipOutputStream out = new ZipOutputStream(
                new java.io.BufferedOutputStream(out0, 1 << 20))) {
                // Stored: its size and CRC in its local header, ZIP64's 8 bytes in its extra field.
                ZipEntry entry = new ZipEntry("SCFA/bin/zeros.bin");
                entry.setMethod(ZipEntry.STORED);
                entry.setSize(OVER_4_GB);
                entry.setCompressedSize(OVER_4_GB);
                CRC32 crc = new CRC32();
                byte[] block = new byte[1 << 20];
                for (long left = OVER_4_GB; left > 0; left -= block.length) {
                    crc.update(block, 0, (int) Math.min(block.length, left));
                }
                entry.setCrc(crc.getValue());
                out.putNextEntry(entry);
                zeros(out, OVER_4_GB);
                out.closeEntry();
                gamesFiles(out);
            }
            assertTrue(zip.length() > OVER_4_GB);
            try (RandomAccessFile header = new RandomAccessFile(zip, "r")) {
                byte[] sizes = new byte[8];
                header.seek(18);
                header.readFully(sizes);
                assertArrayEquals("ZIP64's mark in place of the sizes", new byte[] {-1, -1, -1, -1, -1, -1, -1, -1}, sizes);
            }
            assertZip64Import(zip);
        } finally {
            zip.delete();
        }
    }

    /** The game's files after the big one, one stored and one deflated. */
    private static void gamesFiles(ZipOutputStream out) throws IOException {
        byte[] lua = "lua archive".getBytes(StandardCharsets.UTF_8);
        ZipEntry stored = new ZipEntry("SCFA/gamedata/lua.scd");
        stored.setMethod(ZipEntry.STORED);
        stored.setSize(lua.length);
        stored.setCompressedSize(lua.length);
        CRC32 crc = new CRC32();
        crc.update(lua);
        stored.setCrc(crc.getValue());
        out.putNextEntry(stored);
        out.write(lua);
        out.closeEntry();
        out.putNextEntry(new ZipEntry("SCFA/fonts/ARIAL.TTF"));
        out.write("arial".getBytes(StandardCharsets.UTF_8));
        out.closeEntry();
    }

    private void assertZip64Import(File zip) throws IOException {
        List<ScfaZip.Entry> entries = ScfaZip.list(source(zip));
        assertEquals(3, entries.size());
        assertEquals(OVER_4_GB, entries.get(0).size);
        Storage storage = storage();
        ScfaZip.run(storage, source(zip), roomy);
        assertEquals(Arrays.asList("fonts/ARIAL.TTF", "gamedata/lua.scd"), files(storage.scfa));
        assertEquals("lua archive", read(new File(storage.scfa, "gamedata/lua.scd")));
        assertEquals("arial", read(new File(storage.scfa, "fonts/ARIAL.TTF")));
    }

    private static void zeros(OutputStream out, long bytes) throws IOException {
        byte[] block = new byte[1 << 20];
        for (long left = bytes; left > 0; left -= block.length) {
            out.write(block, 0, (int) Math.min(block.length, left));
        }
    }

    private static int indexOf(byte[] bytes, byte[] part) {
        outer:
        for (int i = 0; i + part.length <= bytes.length; ++i) {
            for (int j = 0; j < part.length; ++j) {
                if (bytes[i + j] != part[j]) {
                    continue outer;
                }
            }
            return i;
        }
        throw new AssertionError("not found");
    }
}
