package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

/**
 * The zip of the game's logs a launcher asks for (GameLogs): report.txt first, then the texts by their names, scrubbed
 * together, so that a player whom one file names is a stand-in in every file, in the order the files name them.
 */
public class GameLogsTest {
    private static final String APP = "io.github.m3rt1n99.fafre";

    @Rule
    public final TemporaryFolder folder = new TemporaryFolder();

    private File text(String name, String... lines) throws IOException {
        File file = folder.newFile(name);
        Files.write(file.toPath(), (String.join("\n", lines) + "\n").getBytes(StandardCharsets.UTF_8));
        return file;
    }

    /** The zip's entries, by name in their order, and their texts. */
    private static Map<String, String> entries(File zip) throws IOException {
        Map<String, String> entries = new LinkedHashMap<>();
        try (ZipInputStream in = new ZipInputStream(new FileInputStream(zip))) {
            for (ZipEntry entry = in.getNextEntry(); entry != null; entry = in.getNextEntry()) {
                entries.put(entry.getName(), new String(readAll(in), StandardCharsets.UTF_8));
            }
        }
        return entries;
    }

    private static byte[] readAll(InputStream in) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buffer = new byte[8192];
        for (int read = in.read(buffer); read > 0; read = in.read(buffer)) {
            out.write(buffer, 0, read);
        }
        return out.toByteArray();
    }

    @Test
    public void theZipHasTheReportThenEachTextScrubbedWithTheNamesOfAllOfThem() throws IOException {
        Map<String, File> texts = new LinkedHashMap<>();
        texts.put("game.txt", text("game.txt", "game: started 2026-11-19T09:27:44.571428Z",
            "game: command line /storage/emulated/0/Android/data/" + APP + "/files/parity/game/3841/bin/ForgedAlliance.exe "
                + "/gpgnet 127.0.0.1:7237 /savereplay gpgnet://127.0.0.1:41011/24000001/Kestrel.SCFAreplay /clan AB-1_",
            // Marlin is named in the engine's log alone, which comes after this file.
            "network game: Marlin waits", "END: the program returned 0 after 84.4 s"));
        texts.put("game/engine.sclog", text("engine.sclog", "info: LOBBY: starting with local uid of 104857 [Kestrel]",
            "info: LOBBY: Adding peer \"Marlin\" [10.0.2.17:16010, uid=2]", "info: Initialize Armies brain nickname is Kestrel",
            "info: Initialize Armies brain nickname is Marlin"));
        texts.put("game/faf_crash_latest.txt", text("faf_crash_latest.txt", "reason: kestrel's army",
            "/data/app/~~a==/" + APP + "-b==/lib/arm64/libfafgame.so"));
        File zip = new File(folder.getRoot(), "logs.zip");
        GameLogs.write("app: 0.14.0 (5562)\n", texts, APP, zip);

        Map<String, String> expected = new LinkedHashMap<>();
        expected.put("report.txt", "app: 0.14.0 (5562)\n");
        expected.put("game.txt", "game: started 2026-11-19T09:27:44.571428Z\n"
            + "game: command line <external>/files/parity/game/3841/bin/ForgedAlliance.exe /gpgnet 127.0.0.1:7237 /savereplay "
            + "gpgnet://127.0.0.1:41011/<game>/player1.SCFAreplay /clan <account>\n"
            + "network game: player2 waits\n"
            + "END: the program returned 0 after 84.4 s\n");
        expected.put("game/engine.sclog", "info: LOBBY: starting with local uid of id1 [player1]\n"
            + "info: LOBBY: Adding peer \"player2\" [<ip>:16010, uid=id2]\n"
            + "info: Initialize Armies brain nickname is player1\n"
            + "info: Initialize Armies brain nickname is player2\n");
        expected.put("game/faf_crash_latest.txt", "reason: player1's army\n<apk>/lib/arm64/libfafgame.so\n");
        assertEquals(expected, entries(zip));
    }

    @Test
    public void theZipHasTheLogcatOfAGameASignalEndedAndOfTheOneBeforeItScrubbedAsTheOtherTexts() throws IOException {
        File files = folder.newFolder("files");
        Storage storage = new Storage(folder.newFolder("storage"));
        Storage.mkdirs(GameReport.folder(storage));
        Files.write(new File(files, "game.txt").toPath(),
            "END: CRASHED: killed by a signal, status 6 (SIGABRT), at 2026-11-24T18:31:02.417Z\n".getBytes(StandardCharsets.UTF_8));
        Files.write(GameReport.logcat(files).toPath(), ("logcat -d -b main -b crash -v threadtime --pid=4242: its last 500 lines\n"
            + "11-24 18:31:02.417  4242  4299 F libc    : Fatal signal 6 (SIGABRT), code -1 (SI_QUEUE) in tid 4299 (Kestrel's sim)\n"
            + "11-24 18:31:02.420  4242  4299 F libc    : /data/app/~~a==/" + APP + "-b==/lib/arm64/libfafgame.so\n")
            .getBytes(StandardCharsets.UTF_8));
        Files.write(GameReport.previousLogcat(files).toPath(), "the game before's\n".getBytes(StandardCharsets.UTF_8));
        Files.write(GameReport.log(storage).toPath(), "info: Initialize Armies brain nickname is Kestrel\n"
            .getBytes(StandardCharsets.UTF_8));
        File work = folder.newFolder("work");

        Map<String, File> texts = GameLogs.texts(files, storage, work);
        assertEquals(Arrays.asList("game.txt", GameReport.LOGCAT, GameReport.PREVIOUS_LOGCAT, "game/engine.sclog"),
            Arrays.asList(texts.keySet().toArray()));
        File zip = new File(folder.getRoot(), "logs.zip");
        GameLogs.write("app: x\n", texts, APP, zip);
        assertEquals("logcat -d -b main -b crash -v threadtime --pid=4242: its last 500 lines\n"
            + "11-24 18:31:02.417  4242  4299 F libc    : Fatal signal 6 (SIGABRT), code -1 (SI_QUEUE) in tid 4299 (player1's sim)\n"
            + "11-24 18:31:02.420  4242  4299 F libc    : <apk>/lib/arm64/libfafgame.so\n", entries(zip).get(GameReport.LOGCAT));
        assertEquals("the game before's\n", entries(zip).get(GameReport.PREVIOUS_LOGCAT));
        assertEquals("a copy, as a game that runs goes on writing its own", work, texts.get(GameReport.LOGCAT).getParentFile());
    }

    @Test
    public void aZipOfNoTextsHasTheReportAlone() throws IOException {
        File zip = new File(folder.getRoot(), "logs.zip");
        GameLogs.write("app: x\n", new LinkedHashMap<>(), APP, zip);
        assertEquals(Arrays.asList("report.txt"), Arrays.asList(entries(zip).keySet().toArray()));
    }

    @Test
    public void theReportHasTheAppTheEngineAndroidAndTheDeviceAndNoAccount() {
        String report = GameLogs.report("0.14.0 (5562)", "550e330c6a", "16 (API 36)", "Google sdk_gphone64_x86_64");
        assertEquals("app: 0.14.0 (5562)\n"
            + "engine commit: 550e330c6a\n"
            + "Android: 16 (API 36)\n"
            + "device: Google sdk_gphone64_x86_64\n"
            + "\n"
            + "The logs are scrubbed for a public issue: players' names are player1, player2, ..., their ids id1, id2, ..., a FAF "
            + "game's id <game>, a game's title <title>, chat <chat>, the account's values <account>, ICE credentials <secret>, "
            + "hosts' addresses <ip>, the app's folders <external>, <internal> and <apk>, and others <storage>.\n", report);
    }

    @Test
    public void aZipsNameIsItsTimeAndNothingElseIsOne() {
        assertTrue(GameLogs.NAME.matcher("faf-re-game-logs-20261119-101112-345.zip").matches());
        for (String name : new String[] {"faf-re-results-20261119-101112.zip", "faf-re-game-logs-20261119-101112.zip",
            "faf-re-game-logs-20261119-101112-345.zip.part", "../faf-re-game-logs-20261119-101112-345.zip", "work"}) {
            assertFalse(name, GameLogs.NAME.matcher(name).matches());
        }
    }
}
