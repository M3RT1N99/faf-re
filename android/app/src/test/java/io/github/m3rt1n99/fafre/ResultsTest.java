package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Map;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

/**
 * What the results zip a tester sends takes of the game's files beside its report (Results.gameFiles): a native crash's
 * tombstone or the logcat of a process a signal ended without one, and the last game's report, tombstone and logcat, as
 * the FAF client's logs have them.
 */
public class ResultsTest {
    @Rule
    public final TemporaryFolder folder = new TemporaryFolder();

    private void write(String name) throws IOException {
        Files.write(new File(folder.getRoot(), name).toPath(), name.getBytes(StandardCharsets.UTF_8));
    }

    @Test
    public void theZipHasTheLogcatOfAGameASignalEndedWithoutATombstoneAndTheLastGamesFiles() throws IOException {
        for (String name : new String[] {GameReport.LOGCAT, GameReport.PREVIOUS, GameReport.PREVIOUS_TOMBSTONE,
                GameReport.PREVIOUS_LOGCAT}) {
            write(name);
        }
        Map<String, File> files = Results.gameFiles(folder.getRoot());
        assertEquals(Arrays.asList(GameReport.LOGCAT, GameReport.PREVIOUS, GameReport.PREVIOUS_TOMBSTONE,
            GameReport.PREVIOUS_LOGCAT), new ArrayList<>(files.keySet()));
        assertEquals(new File(folder.getRoot(), GameReport.LOGCAT), files.get(GameReport.LOGCAT));
    }

    @Test
    public void aNativeCrashsTombstoneGoesInAndFilesThatAreNotThereDoNot() throws IOException {
        assertEquals(Collections.emptyMap(), Results.gameFiles(folder.getRoot()));
        write(GameReport.TOMBSTONE);
        assertEquals(Arrays.asList(GameReport.TOMBSTONE), new ArrayList<>(Results.gameFiles(folder.getRoot()).keySet()));
    }
}
