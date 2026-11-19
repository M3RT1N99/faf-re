package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;

import org.junit.After;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

/**
 * The SCFA imports' shared rules (ScfaImport), which the folder import and the ZIP's (ScfaZip) both go by: what they take,
 * what is there already, and how a file is written, whole or not at all.
 */
public class ScfaImportTest {
    @Rule
    public final TemporaryFolder folder = new TemporaryFolder();

    @After
    public void noCancelLeft() {
        Cancellation.reset();
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
}
