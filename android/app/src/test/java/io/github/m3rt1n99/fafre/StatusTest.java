package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;

import java.time.Instant;
import java.util.Arrays;
import java.util.Collections;

import org.junit.Test;

/**
 * The status a launcher reads (StatusProvider): the tables' columns, game_data's row from what the app knows, and last_game's
 * from the game's report in each way a game ends, with nothing of a player's name or a path in any column.
 */
public class StatusTest {
    private static final String STARTED = "2026-11-19T02:43:42.123Z";
    /** A running game's report, as GameActivity and the host write it. */
    private static final String RUNNING = "game: started " + STARTED + "\n"
        + "game: FAF game version 3845, 7 SCFA archives\n"
        + "game: command line /data/app/x/bin/ForgedAlliance.exe /init init_game.lua /numgames 3 /fullscreen 1707 768\n"
        + "7012 ms: first frame\n";
    /** What the engine's log, a fatal error or Android may say, which the report quotes and the status leaves out. */
    private static final String NAME = "NameOfAPlayer";
    private static final String PATH = "/storage/emulated/0/Android/data/io.github.m3rt1n99.fafre/files/game/vault/maps/x";

    @Test
    public void theTablesHaveTheContractsColumns() {
        assertEquals(Arrays.asList("scfa_archives", "scfa_fonts", "scfa_sound_banks", "scfa_movies", "game_versions", "newest_known",
            "job_running", "job_step", "game_running", "version_code", "version_name", "engine_commit"), Status.columns("game_data"));
        assertEquals(Arrays.asList("started_at", "ended", "exit_code", "crashed", "end"), Status.columns("last_game"));
        assertNull(Status.columns("games"));
        assertNull(Status.columns(null));
    }

    @Test
    public void theDeviceCheckHasTheContractsColumnsAndNoTableYet() {
        assertEquals(Arrays.asList("verdict", "impact", "codes", "reasons", "gpu", "details", "checked_at", "checked_version_code",
            "stale", "running"), Status.DEVICE_CHECK_COLUMNS);
        assertNull("no table without the check, so that the client keeps its own prediction", Status.columns("device_check"));
    }

    @Test
    public void gameDataHasTheImportTheReadyVersionsTheJobAndTheApp() {
        assertArrayEquals(new Object[] {7, 3, 12, 9, "3841,3845", 3846, 1, "Downloading FAF lua: 3 / 100 MB", 0, 5544, "0.14.0",
            "2bf1611970"}, Status.gameData(7, 3, 12, 9, Arrays.asList(3841, 3845), 3846, true, "Downloading FAF lua: 3 / 100 MB",
            false, 5544, "0.14.0", "2bf1611970"));
        // Nothing yet: no versions, no check of FAF's newest, no job; a job before its first step; a game running.
        assertArrayEquals(new Object[] {0, 0, 0, 0, "", null, 0, null, 1, 1, "0.14.0", "2bf1611970"},
            Status.gameData(0, 0, 0, 0, Collections.emptyList(), -1, false, "", true, 1, "0.14.0", "2bf1611970"));
        assertNull("a job's step only while it runs", Status.gameData(0, 0, 0, 0, Collections.singletonList(3845), 3845, false,
            "Importing fonts", false, 1, "", "")[7]);
        assertNull(Status.gameData(0, 0, 0, 0, Collections.singletonList(3845), 3845, true, "", false, 1, "", "")[7]);
    }

    @Test
    public void noGameYetHasNoRow() {
        assertNull(Status.lastGame(""));
    }

    @Test
    public void aRunningGameHasItsStartAndNoEnd() {
        assertArrayEquals(new Object[] {Instant.parse(STARTED).toEpochMilli(), 0, null, 0, null}, Status.lastGame(RUNNING));
    }

    @Test
    public void aReportOfAnEarlierReleaseHasNoStart() {
        assertNull(Status.lastGame(RUNNING.substring(RUNNING.indexOf('\n') + 1))[0]);
        assertNull("a broken time", Status.lastGame("game: started yesterday\n")[0]);
    }

    @Test
    public void aProgramThatReturnedHasItsExitCode() {
        assertLastGame(RUNNING + "61234 ms: exit 0\nEND: the program returned 0 after 61.2 s; the engine's last warning: LOBBY: "
            + NAME + " left " + PATH + "\n", 0, false, "the program returned 0 after 61.2 s");
        assertLastGame(RUNNING + "1234 ms: exit -1\nEND: the program returned -1 after 1.2 s\n", -1, false,
            "the program returned -1 after 1.2 s");
    }

    @Test
    public void anEngineThatEndedItsProcessHasItsExitCode() {
        assertLastGame(RUNNING + "END: the engine ended its process, exit code 3; Vulkan: no device for " + PATH + "\n", 3,
            false, "the engine ended its process, exit code 3");
    }

    @Test
    public void aCrashHasAndroidsReasonAndStatusAlone() {
        assertLastGame(RUNNING + "END: CRASHED: native crash, status 11, crash in " + PATH + " (tombstone in the results); the "
            + "engine's last warning: " + NAME + "\n", null, true, "CRASHED: native crash, status 11");
        assertLastGame(RUNNING + "END: the process ended: low memory, status 0, " + NAME + "; " + PATH + "\n", null, false,
            "the process ended: low memory, status 0");
        assertLastGame(RUNNING + "END: the process ended: stopped by the user, status 0\n", null, false,
            "the process ended: stopped by the user, status 0");
        assertLastGame(RUNNING + "END: the game's process ended, and Android gives no reason; the engine's last warning: " + NAME
            + "\n", null, false, "the game's process ended, and Android gives no reason");
    }

    @Test
    public void aFatalErrorIsACrashWithoutTheEnginesMessage() {
        assertLastGame(RUNNING + "9000 ms: fatal: Unable to open " + PATH + " for " + NAME + "\nEND: FAILED: Unable to open " + PATH
            + " for " + NAME + " (tombstone in the results)\n", null, true, "FAILED: the engine stopped on a fatal error");
    }

    @Test
    public void aStartThatFailedHasItsCodeAlone() {
        GameStart.Failure download = GameStart.Failure.download("the map scmp_007: the phone cannot write " + PATH + ".part");
        assertLastGame("game: started " + STARTED + "\n" + String.join("\n", download.reportLines()) + "\n", null, false,
            "the start failed: DOWNLOAD");
        GameStart.Failure noScfa = GameStart.Failure.noScfa();
        assertLastGame("game: started " + STARTED + "\n" + String.join("\n", noScfa.reportLines()) + "\n", null, false,
            "the start failed: NO_SCFA");
        // RUNNING's line in the running game's report, which goes on to end as itself.
        String refused = GameStart.refusal(true, false, 3845, null).reportLine();
        assertLastGame(RUNNING + refused + "\n12000 ms: exit 0\nEND: the program returned 0 after 12.0 s\n", 0, false,
            "the program returned 0 after 12.0 s");
    }

    /** last_game's row for {@code report}, which has its start and has ended. */
    private static void assertLastGame(String report, Integer exitCode, boolean crashed, String end) {
        Object[] row = Status.lastGame(report);
        assertArrayEquals(report, new Object[] {Instant.parse(STARTED).toEpochMilli(), 1, exitCode, crashed ? 1 : 0, end}, row);
        for (Object column : row) {
            String text = String.valueOf(column);
            assertFalse(text, text.contains(NAME) || text.contains("/storage") || text.contains("/data"));
        }
    }
}
