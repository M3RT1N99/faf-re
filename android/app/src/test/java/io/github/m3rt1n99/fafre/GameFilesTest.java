package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

/** The init files of a game and of a replay's run, as the engine reads them. */
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
}
