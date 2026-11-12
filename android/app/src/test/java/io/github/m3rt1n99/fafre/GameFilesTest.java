package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
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
        File game = folder.newFolder("3835");
        File bin = new File(game, "bin");
        bin.mkdirs();
        Files.write(new File(bin, "init_ci.lua").toPath(), INIT.getBytes(StandardCharsets.UTF_8));
        File vault = folder.newFolder("vault");

        File init = GameFiles.runInit(game, "r1234", vault);

        String text = new String(Files.readAllBytes(init.toPath()), StandardCharsets.UTF_8);
        assertEquals("init_r1234.lua", init.getName());
        assertFalse(text.contains("fafre_ui"));
        assertEquals(INIT.replace("custom_vault_path = InitFileDir .. '/../vault'", "custom_vault_path = '" + vault.getAbsolutePath() + "'"), text);
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
    private static final class Updater implements Http.Downloads {
        String init = INIT;
        /** A package whose download fails as the network's, after all its tries. */
        String failing;
        final List<String> downloaded = new ArrayList<>();

        @Override
        public byte[] bytes(String url) {
            assertEquals(Http.GAME_URL + "init_faf_3840.lua", url);
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
}
