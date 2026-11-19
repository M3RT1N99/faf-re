package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

import org.junit.After;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

/**
 * The map and the mods a launcher's start names, into the game's vault (Preparation): from FAF's servers when the phone has
 * not got them, checked against the name and the uid asked for, and each failure as the start's code. Also M28AI, into the
 * replay test's cache.
 */
public class VaultTest {
    private static final String UID = "e7846e9b-23a4-4b95-ae3a-fb69b289a585";
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

    @After
    public void noCancelLeft() {
        Cancellation.reset();
    }

    /** FAF's content server as the fetches download from it: each URL's zip, of these entries; others are not there. */
    private static final class Server implements Http.Downloads {
        final Map<String, Map<String, String>> zips = new LinkedHashMap<>();
        final List<String> downloaded = new ArrayList<>();
        /** The user's Back right after a download's last byte, which the download's own loop no longer sees. */
        boolean backAfterLastByte;

        @Override
        public byte[] bytes(String url) {
            throw new AssertionError(url);
        }

        @Override
        public void download(String url, File dest, Http.Progress progress) throws IOException {
            Map<String, String> entries = zips.get(url);
            if (entries == null) {
                throw new Http.StatusException(url, 404);
            }
            downloaded.add(url);
            try (ZipOutputStream zip = new ZipOutputStream(new FileOutputStream(dest))) {
                for (Map.Entry<String, String> entry : entries.entrySet()) {
                    zip.putNextEntry(new ZipEntry(entry.getKey()));
                    zip.write(entry.getValue().getBytes(StandardCharsets.UTF_8));
                    zip.closeEntry();
                }
            }
            if (backAfterLastByte) {
                Cancellation.request();
            }
        }
    }

    private static Map<String, String> entries(String... nameText) {
        Map<String, String> entries = new LinkedHashMap<>();
        for (int i = 0; i < nameText.length; i += 2) {
            entries.put(nameText[i], nameText[i + 1]);
        }
        return entries;
    }

    private static String modInfo(String uid) {
        return "name = \"M28AI\"\nversion = 247\n  uid = '" + uid + "'\ndescription = \"an AI\"\n";
    }

    @Test
    public void aMapComesFromFafsVaultIntoTheGamesOnceAndNotWhenSomeoneHasIt() throws IOException {
        Storage storage = new Storage(folder.getRoot());
        File maps = new File(folder.getRoot(), "game/vault/maps");
        Server server = new Server();
        server.zips.put(Http.MAP_URL + "scmp_007.zip", entries("SCMP_007/SCMP_007_scenario.lua", "version = 3",
            "SCMP_007/SCMP_007.scmap", "x"));

        File map = Vault.fetchMap(storage, Storage.mkdirs(maps), "scmp_007", LOG, server, null);

        assertEquals(new File(maps, "scmp_007"), map);
        assertTrue(new File(map, "SCMP_007_scenario.lua").isFile());
        assertEquals(map, Vault.fetchMap(storage, maps, "scmp_007", LOG, server, null));
        assertEquals("downloaded once, and the zip is gone", 1, server.downloaded.size());
        assertFalse(new File(storage.parity, "downloads/scmp_007.zip").exists());
        // The user's SCFA has its own maps, which every init file mounts.
        Storage.mkdirs(new File(storage.scfa, "maps/X1MP_005"));
        GameFiles.write(new File(storage.scfa, "maps/X1MP_005/X1MP_005_scenario.lua"), "version = 3");
        assertTrue(Vault.inScfa(storage, "x1mp_005"));
        assertFalse(Vault.inScfa(storage, "scmp_007"));
    }

    @Test
    public void aMapFafHasNotIsNoMapAndANetworkFailureADownloadsFailure() {
        Storage storage = new Storage(folder.getRoot());
        File maps = Storage.mkdirs(new File(folder.getRoot(), "game/vault/maps"));
        Server server = new Server();
        server.zips.put(Http.MAP_URL + "empty.v0001.zip", entries("readme.txt", "no map"));

        IOException missing = assertThrows(IOException.class, () -> Vault.fetchMap(storage, maps, "nothing.v0001", LOG, server, null));
        assertEquals(GameStart.Code.NO_MAP, GameStart.fetchFailure(missing, GameStart.Code.NO_MAP, "the map nothing.v0001").code);
        IOException empty = assertThrows(IOException.class, () -> Vault.fetchMap(storage, maps, "empty.v0001", LOG, server, null));
        GameStart.Failure noMap = GameStart.fetchFailure(empty, GameStart.Code.NO_MAP, "the map empty.v0001");
        assertEquals(GameStart.Code.NO_MAP, noMap.code);
        assertEquals("the map empty.v0001: no one map folder empty.v0001 in the map zip (0 with a scenario file)", noMap.getMessage());
        assertEquals("nothing left behind", Collections.emptyList(), Arrays.asList(maps.list()));
    }

    @Test
    public void aFetchsFailureHasTheStartsCode() {
        assertEquals(GameStart.Code.NO_MOD, GameStart.fetchFailure(new Http.StatusException("u", 404), GameStart.Code.NO_MOD, "m").code);
        assertEquals(GameStart.Code.NO_MAP, GameStart.fetchFailure(new Http.StatusException("u", 403), GameStart.Code.NO_MAP, "m").code);
        assertEquals(GameStart.Code.NO_MAP, GameStart.fetchFailure(new Vault.Missing("x"), GameStart.Code.NO_MAP, "m").code);
        for (IOException network : new IOException[] {new Http.StatusException("u", 500), new IOException("Connection reset"),
            new Http.StorageException("the phone cannot write x.part (ENOSPC)")}) {
            GameStart.Failure failure = GameStart.fetchFailure(network, GameStart.Code.NO_MAP, "the map x");
            assertEquals(GameStart.Code.DOWNLOAD, failure.code);
            assertEquals("the map x: " + network.getMessage(), failure.getMessage());
        }
        GameStart.Failure cancelled = GameStart.fetchFailure(new Cancellation.Cancelled(), GameStart.Code.NO_MAP, "the map x");
        assertEquals(GameStart.Code.CANCELLED, cancelled.code);
        assertEquals("Back ended the start while it fetched what the game needs", cancelled.getMessage());
    }

    @Test
    public void aModComesAsItsZipsFolderInPlaceOfAnOlderVersion() throws IOException {
        Storage storage = new Storage(folder.getRoot());
        File mods = Storage.mkdirs(new File(folder.getRoot(), "game/vault/mods"));
        // The older version, in the folder the new one's zip has.
        Storage.mkdirs(new File(mods, "M28AI"));
        GameFiles.write(new File(mods, "M28AI/mod_info.lua"), modInfo("an-older-uid"));
        GameFiles.write(new File(mods, "M28AI/old.lua"), "x");
        Server server = new Server();
        server.zips.put(Http.MOD_URL + "m28ai.v0247.zip", entries("M28AI/mod_info.lua", modInfo(UID), "M28AI/lua/ai.lua", "x"));

        File mod = Vault.fetchMod(storage, mods, UID, "m28ai.v0247.zip", LOG, server, null);

        assertEquals(new File(mods, "M28AI"), mod);
        assertTrue(new File(mod, "lua/ai.lua").isFile());
        assertFalse("the older version is gone", new File(mod, "old.lua").exists());
        assertEquals(UID, Vault.modUid(new File(mod, "mod_info.lua")));
        assertEquals(Collections.singletonList("M28AI"), Arrays.asList(mods.list()));
        // The older version left the vault whole and went beside the zip, which is gone too.
        assertEquals("nothing in downloads", Collections.emptyList(), Arrays.asList(storage.downloads().list()));
        // There now, in any case: no download, and no file needed.
        assertEquals(mod, Vault.fetchMod(storage, mods, UID.toUpperCase(java.util.Locale.ROOT), null, LOG, server, null));
        assertEquals(1, server.downloaded.size());
    }

    @Test
    public void aModsFileMustHoldItsUid() {
        Storage storage = new Storage(folder.getRoot());
        File mods = Storage.mkdirs(new File(folder.getRoot(), "game/vault/mods"));
        Server server = new Server();
        server.zips.put(Http.MOD_URL + "other.v0001.zip", entries("Other/mod_info.lua", modInfo("12345678-aaaa")));
        server.zips.put(Http.MOD_URL + "none.v0001.zip", entries("Other/readme.txt", "x"));

        Vault.Missing other = assertThrows(Vault.Missing.class,
            () -> Vault.fetchMod(storage, mods, UID, "other.v0001.zip", LOG, server, null));
        assertEquals("other.v0001.zip holds the mod 12345678-aaaa, not " + UID, other.getMessage());
        Vault.Missing none = assertThrows(Vault.Missing.class,
            () -> Vault.fetchMod(storage, mods, UID, "none.v0001.zip", LOG, server, null));
        assertEquals("none.v0001.zip holds no mod_info.lua", none.getMessage());
        Vault.Missing unnamed = assertThrows(Vault.Missing.class, () -> Vault.fetchMod(storage, mods, UID, null, LOG, server, null));
        assertEquals("the phone has no mod " + UID + ", and the start names no file of it", unnamed.getMessage());
        IOException notThere = assertThrows(IOException.class,
            () -> Vault.fetchMod(storage, mods, UID, "gone.v0001.zip", LOG, server, null));
        assertEquals(GameStart.Code.NO_MOD, GameStart.fetchFailure(notThere, GameStart.Code.NO_MOD, "the mod " + UID).code);
        assertEquals("nothing left behind", Collections.emptyList(), Arrays.asList(mods.list()));
    }

    @Test
    public void aModWhoseZipHasNoFolderGetsItsUidAsOne() throws IOException {
        Storage storage = new Storage(folder.getRoot());
        File mods = Storage.mkdirs(new File(folder.getRoot(), "game/vault/mods"));
        Server server = new Server();
        server.zips.put(Http.MOD_URL + "flat.v0001.zip", entries("mod_info.lua", modInfo(UID)));

        assertEquals(new File(mods, UID), Vault.fetchMod(storage, mods, UID, "flat.v0001.zip", LOG, server, null));
    }

    @Test
    public void backAfterADownloadsLastByteStopsItsUnpackingAndLeavesNothing() {
        Storage storage = new Storage(folder.getRoot());
        File maps = Storage.mkdirs(new File(folder.getRoot(), "game/vault/maps"));
        File mods = Storage.mkdirs(new File(folder.getRoot(), "game/vault/mods"));
        Server server = new Server();
        server.zips.put(Http.MAP_URL + "scmp_007.zip", entries("SCMP_007/SCMP_007_scenario.lua", "version = 3"));
        server.zips.put(Http.MOD_URL + "m28ai.v0247.zip", entries("M28AI/mod_info.lua", modInfo(UID)));
        server.backAfterLastByte = true;

        assertThrows(Cancellation.Cancelled.class, () -> Vault.fetchMap(storage, maps, "scmp_007", LOG, server, null));
        assertThrows(Cancellation.Cancelled.class,
            () -> Vault.fetchMod(storage, mods, UID, "m28ai.v0247.zip", LOG, server, null));

        assertEquals(2, server.downloaded.size());
        assertEquals("nothing in the vault", Collections.emptyList(), Arrays.asList(maps.list()));
        assertEquals("nothing in the vault", Collections.emptyList(), Arrays.asList(mods.list()));
        assertEquals("nothing in downloads", Collections.emptyList(), Arrays.asList(storage.downloads().list()));
    }

    @Test
    public void aZipUnpacksBesideItOutOfTheVaultWhereAHalfUnpackedOneIsNoMod() throws IOException {
        Storage storage = new Storage(folder.getRoot());
        File mods = Storage.mkdirs(new File(folder.getRoot(), "game/vault/mods"));
        File zip = new File(storage.downloads(), UID + ".zip");
        File unpacked = Vault.unpacked(zip);
        assertEquals(storage.downloads(), unpacked.getParentFile());
        // What a process that ended while the zip unpacked left: its mod_info.lua, and not the rest.
        GameFiles.write(new File(Storage.mkdirs(new File(unpacked, "M28AI")), "mod_info.lua"), modInfo(UID));
        assertNull(Vault.modWithUid(mods, UID));
        Server server = new Server();
        server.zips.put(Http.MOD_URL + "m28ai.v0247.zip", entries("M28AI/mod_info.lua", modInfo(UID), "M28AI/lua/ai.lua", "x"));

        File mod = Vault.fetchMod(storage, mods, UID, "m28ai.v0247.zip", LOG, server, null);

        assertEquals(new File(mods, "M28AI"), mod);
        assertTrue("whole", new File(mod, "lua/ai.lua").isFile());
        assertFalse(unpacked.exists());
        assertFalse(zip.exists());
    }

    @Test
    public void aCancelWhileM28AIUnpacksLeavesNoneTheReplayTestsNextRunTakes() throws IOException {
        // The replay test's cache takes any folder with a mod_info.lua for whole, and its Cancel stops an unpack: one right
        // after the download leaves nothing there, and the next run fetches M28AI again.
        Storage storage = new Storage(folder.getRoot());
        Server server = new Server();
        server.zips.put(Http.MOD_URL + "m28ai.v0247.zip", entries("M28AI/mod_info.lua", modInfo(UID), "M28AI/lua/ai.lua", "x"));
        server.backAfterLastByte = true;
        assertThrows(Cancellation.Cancelled.class, () -> Vault.m28(storage, 247, LOG, server));
        assertEquals("nothing in the cache", Collections.emptyList(), Arrays.asList(storage.m28().list()));

        Cancellation.reset();
        server.backAfterLastByte = false;
        File mod = Vault.m28(storage, 247, LOG, server);

        assertEquals(new File(storage.m28(), "m28ai.v0247/M28AI"), mod);
        assertTrue("whole", new File(mod, "lua/ai.lua").isFile());
        assertEquals(2, server.downloaded.size());
        assertEquals("nothing in downloads", Collections.emptyList(), Arrays.asList(storage.downloads().list()));
    }

    @Test
    public void aModInfoWithoutAUidHasNone() throws IOException {
        File info = new File(folder.getRoot(), "mod_info.lua");
        GameFiles.write(info, "name = \"x\"\nversion = 1\n");
        assertNull(Vault.modUid(info));
        GameFiles.write(info, "uid=\"a1b2c3d4\"");
        assertEquals("a1b2c3d4", Vault.modUid(info));
    }
}
