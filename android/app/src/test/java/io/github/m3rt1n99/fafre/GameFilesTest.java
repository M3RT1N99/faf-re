package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.HashMap;
import java.util.Map;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

/** The init files of a game and of a replay's run, as the engine reads them, and the size of a version's download. */
public class GameFilesTest {
    private static final String INIT = "hook = {\n    '/schook'\n}\n"
        + "local allowed = {}\n"
        + "custom_vault_path = InitFileDir .. '/../vault'\n"
        + "if custom_vault_path then\n    LoadVaultContent(custom_vault_path)\nend\n";

    @Rule
    public final TemporaryFolder folder = new TemporaryFolder();

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
}
