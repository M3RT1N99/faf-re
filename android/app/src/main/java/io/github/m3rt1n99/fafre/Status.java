package io.github.m3rt1n99.fafre;

import java.util.Arrays;
import java.util.Collections;
import java.util.List;

/**
 * The status a launcher reads (StatusProvider): its two tables, their columns, and each one's row from what the app knows.
 * No player's name and no path in any column. No Android in here, for the tests.
 */
final class Status {
    static final String GAME_DATA = "game_data";
    static final String LAST_GAME = "last_game";

    /** The game's data on the phone, and the app's: one row. */
    static final List<String> GAME_DATA_COLUMNS = Collections.unmodifiableList(Arrays.asList(
        "scfa_archives", "scfa_fonts", "scfa_sound_banks", "scfa_movies", "game_versions", "newest_known", "job_running",
        "job_step", "game_running", "version_code", "version_name", "engine_commit"));
    /** The last game's start and end, from its report: one row, none before the first game. */
    static final List<String> LAST_GAME_COLUMNS = Collections.unmodifiableList(Arrays.asList(
        "started_at", "ended", "exit_code", "crashed", "end"));
    /**
     * The device check's verdict on this phone (the README's contract 2, The device check): one row, also before the first
     * check. No table of the provider's until the app checks the device: an app without the check has none (columns answers
     * null for it), so that the client keeps its own prediction.
     */
    static final List<String> DEVICE_CHECK_COLUMNS = Collections.unmodifiableList(Arrays.asList(
        "verdict", "impact", "codes", "reasons", "gpu", "details", "checked_at", "checked_version_code", "stale", "running"));

    private Status() {
    }

    /** The table's columns, or null for no table of the provider's. */
    static List<String> columns(String table) {
        if (GAME_DATA.equals(table)) {
            return GAME_DATA_COLUMNS;
        }
        return LAST_GAME.equals(table) ? LAST_GAME_COLUMNS : null;
    }

    /**
     * game_data's row: the SCFA import's counts, the ready game versions ("3841,3845", "" for none), FAF's newest version as
     * the last check found it (null before one, -1 here), whether one of the app's jobs runs (1 or 0) and its step (null
     * while none runs, or before its first), whether the game's process runs, and the app's version and commit.
     */
    static Object[] gameData(int archives, int fonts, int soundBanks, int movies, List<Integer> versions, int newest,
            boolean jobRunning, String jobStep, boolean gameRunning, int versionCode, String versionName, String commit) {
        StringBuilder ready = new StringBuilder();
        for (int version : versions) {
            ready.append(ready.length() > 0 ? "," : "").append(version);
        }
        return new Object[] {archives, fonts, soundBanks, movies, ready.toString(), newest >= 0 ? newest : null,
            jobRunning ? 1 : 0, jobRunning && !jobStep.isEmpty() ? jobStep : null, gameRunning ? 1 : 0, versionCode,
            versionName, commit};
    }

    /**
     * last_game's row from the game's report as it is now (GameReport.current), or null for "" (no game yet): when it began
     * (ms since 1970, null for an earlier release's report), whether it has ended (1 or 0), the program's exit code (null
     * but for a program that returned one or an engine that ended its process), whether it crashed or stopped on a fatal
     * error, and how it ended in the app's own words (GameReport.plainEnd, null until it has).
     */
    static Object[] lastGame(String report) {
        if (report.isEmpty()) {
            return null;
        }
        return new Object[] {GameReport.startedAt(report), GameReport.endLine(report) != null ? 1 : 0,
            GameReport.exitCode(report), GameReport.crashed(report) ? 1 : 0, GameReport.plainEnd(report)};
    }
}
