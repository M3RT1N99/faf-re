package io.github.m3rt1n99.fafre;

import android.content.Context;
import android.content.SharedPreferences;

import java.util.Collections;
import java.util.HashSet;
import java.util.Set;

/**
 * Launcher settings and small bits of state that must survive the activity
 * (rotation, process death): launch options, import choices, the folders the
 * user picked and the outcome of the last import job.
 */
final class Settings {
    static final String TREE_SCFA = "scfa";
    static final String TREE_FAF = "faf";
    static final String TREE_VAULT = "vault";

    private static final String FILE = "launcher";
    private static final String RENDERER = "renderer";
    private static final String NO_MOVIE = "nomovie";
    private static final String NO_SOUND = "nosound";
    private static final String EXTRA_ARGS = "extra_args";
    private static final String IMPORT_RECOMMENDED = "import_recommended";
    private static final String IMPORT_OPTIONAL = "import_optional";
    private static final String TREE_PREFIX = "tree_";
    private static final String LAST_JOB_TITLE = "last_job_title";
    private static final String LAST_JOB_MESSAGE = "last_job_message";
    private static final String LAST_JOB_OK = "last_job_ok";
    private static final String LAST_JOB_TIME = "last_job_time";
    private static final String LAST_LAUNCH = "last_launch";
    private static final String REPLAY_STEM = "replay_stem";
    private static final String REPLAY_REPEAT = "replay_repeat";
    private static final String REPLAY_INTERLOCKED = "replay_interlocked";
    private static final String REPLAY_NO_ARENA = "replay_no_arena";
    private static final String REPLAY_SKIP_SELF_TEST = "replay_skip_self_test";
    private static final String REPLAY_ADVANCED = "replay_advanced";
    private static final String REPLAY_OPTIMIZED = "replay_optimized";
    private static final String REPLAY_SPEED = "replay_speed_experiment";
    private static final String REPLAY_AFFINITY_BIG = "replay_affinity_big";
    private static final String LAST_PROBE_RUN = "last_probe_run";
    private static final String LAST_REPLAY_RUN = "last_replay_run";
    private static final String LAST_REPLAY_MESSAGE = "last_replay_message";
    private static final String LAST_REPLAY_OK = "last_replay_ok";
    private static final String LAST_REPLAY_TIME = "last_replay_time";
    private static final String LAST_LAUNCH_MODE = "last_launch_mode";
    private static final String MENU_FAST = "menu_replay_fast";
    private static final String MENU_ADVANCED = "menu_replay_advanced";
    private static final String MENU_CPU_DECODE = "menu_replay_cpu_decode";
    private static final String MENU_NO_SHADER_CACHE = "menu_replay_no_shader_cache";
    private static final String LAST_MENU_RUN = "last_menu_run";
    /** {@link #lastLaunchMode()} of a menu replay; a game start records {@link #LAUNCH_GAME}. */
    static final String LAUNCH_MENU_REPLAY = "menu-replay";
    static final String LAUNCH_GAME = "game";

    private final SharedPreferences mPrefs;

    Settings(Context context) {
        mPrefs = context.getApplicationContext().getSharedPreferences(FILE, Context.MODE_PRIVATE);
    }

    String renderer() {
        String value = mPrefs.getString(RENDERER, LaunchArgs.RENDERER_VULKAN);
        return LaunchArgs.RENDERER_GLES.equals(value) ? LaunchArgs.RENDERER_GLES : LaunchArgs.RENDERER_VULKAN;
    }

    void setRenderer(String renderer) {
        mPrefs.edit().putString(RENDERER, renderer).apply();
    }

    /** /nomovie: on by default because movies are optional data. */
    boolean noMovie() {
        return mPrefs.getBoolean(NO_MOVIE, true);
    }

    void setNoMovie(boolean value) {
        mPrefs.edit().putBoolean(NO_MOVIE, value).apply();
    }

    /** /nosound: on by default for M1, which has no audio backend yet. */
    boolean noSound() {
        return mPrefs.getBoolean(NO_SOUND, true);
    }

    void setNoSound(boolean value) {
        mPrefs.edit().putBoolean(NO_SOUND, value).apply();
    }

    String extraArgs() {
        return mPrefs.getString(EXTRA_ARGS, "");
    }

    void setExtraArgs(String value) {
        mPrefs.edit().putString(EXTRA_ARGS, value).apply();
    }

    boolean importRecommended() {
        return mPrefs.getBoolean(IMPORT_RECOMMENDED, true);
    }

    void setImportRecommended(boolean value) {
        mPrefs.edit().putBoolean(IMPORT_RECOMMENDED, value).apply();
    }

    /** Ids of the optional manifest entries the user ticked. */
    Set<String> importOptional() {
        return Collections.unmodifiableSet(new HashSet<>(mPrefs.getStringSet(IMPORT_OPTIONAL,
                Collections.<String>emptySet())));
    }

    void setImportOptional(String id, boolean selected) {
        // Never mutate the set SharedPreferences returns; write a copy.
        Set<String> ids = new HashSet<>(importOptional());
        if (selected) {
            ids.add(id);
        } else {
            ids.remove(id);
        }
        mPrefs.edit().putStringSet(IMPORT_OPTIONAL, ids).apply();
    }

    String tree(String kind) {
        return mPrefs.getString(TREE_PREFIX + kind, null);
    }

    void setTree(String kind, String uri) {
        mPrefs.edit().putString(TREE_PREFIX + kind, uri).apply();
    }

    void setLastJob(String title, String message, boolean ok) {
        mPrefs.edit()
                .putString(LAST_JOB_TITLE, title)
                .putString(LAST_JOB_MESSAGE, message)
                .putBoolean(LAST_JOB_OK, ok)
                .putLong(LAST_JOB_TIME, System.currentTimeMillis())
                .apply();
    }

    String lastJobTitle() {
        return mPrefs.getString(LAST_JOB_TITLE, null);
    }

    String lastJobMessage() {
        return mPrefs.getString(LAST_JOB_MESSAGE, null);
    }

    boolean lastJobOk() {
        return mPrefs.getBoolean(LAST_JOB_OK, false);
    }

    long lastJobTime() {
        return mPrefs.getLong(LAST_JOB_TIME, 0);
    }

    long lastLaunch() {
        return mPrefs.getLong(LAST_LAUNCH, 0);
    }

    void setLastLaunch(long millis) {
        mPrefs.edit().putLong(LAST_LAUNCH, millis).putString(LAST_LAUNCH_MODE, LAUNCH_GAME).apply();
    }

    /**
     * What the last start of the :game process was: {@link #LAUNCH_GAME} or {@link #LAUNCH_MENU_REPLAY}. Both write
     * launch/status.json; this tells the launcher whose status it shows.
     */
    String lastLaunchMode() {
        return mPrefs.getString(LAST_LAUNCH_MODE, LAUNCH_GAME);
    }

    // ----------------------------------------------------------- menu replay

    /**
     * Records a menu replay start, before GameActivity is started: the run (its provisional result.json says
     * INTERRUPTED until GameActivity replaces it) and the launch mode. Written synchronously (commit), because
     * the :game process may be ended right after.
     */
    void startMenuReplay(String run, long millis) {
        mPrefs.edit()
                .putString(LAST_MENU_RUN, run)
                .putLong(LAST_LAUNCH, millis)
                .putString(LAST_LAUNCH_MODE, LAUNCH_MENU_REPLAY)
                .commit();
    }

    /** The run directory name of the last menu replay, or null. */
    String lastMenuRun() {
        return mPrefs.getString(LAST_MENU_RUN, null);
    }

    /** "As fast as possible" instead of the recorded 30 fps. */
    boolean menuFast() {
        return mPrefs.getBoolean(MENU_FAST, false);
    }

    void setMenuFast(boolean value) {
        mPrefs.edit().putBoolean(MENU_FAST, value).apply();
    }

    boolean menuAdvanced() {
        return mPrefs.getBoolean(MENU_ADVANCED, false);
    }

    void setMenuAdvanced(boolean value) {
        mPrefs.edit().putBoolean(MENU_ADVANCED, value).apply();
    }

    /** Advanced: BC textures decoded on the CPU even when the GPU samples them (the path for GPUs without BC). */
    boolean menuCpuDecode() {
        return mPrefs.getBoolean(MENU_CPU_DECODE, false);
    }

    void setMenuCpuDecode(boolean value) {
        mPrefs.edit().putBoolean(MENU_CPU_DECODE, value).apply();
    }

    /** Advanced: neither read nor write the shader and pipeline caches (a cold first launch). */
    boolean menuNoShaderCache() {
        return mPrefs.getBoolean(MENU_NO_SHADER_CACHE, false);
    }

    void setMenuNoShaderCache(boolean value) {
        mPrefs.edit().putBoolean(MENU_NO_SHADER_CACHE, value).apply();
    }

    // ------------------------------------------------------------ replay test

    /** The replay picked for the test (its stem in replays/), or null. */
    String replayStem() {
        return mPrefs.getString(REPLAY_STEM, null);
    }

    void setReplayStem(String stem) {
        mPrefs.edit().putString(REPLAY_STEM, stem).apply();
    }

    boolean replayRepeat() {
        return mPrefs.getBoolean(REPLAY_REPEAT, false);
    }

    void setReplayRepeat(boolean value) {
        mPrefs.edit().putBoolean(REPLAY_REPEAT, value).apply();
    }

    boolean replayInterlocked() {
        return mPrefs.getBoolean(REPLAY_INTERLOCKED, false);
    }

    void setReplayInterlocked(boolean value) {
        mPrefs.edit().putBoolean(REPLAY_INTERLOCKED, value).apply();
    }

    /** FAF_LOWARENA=0: the run is expected to crash (the M3d truncation oracle). */
    boolean replayNoArena() {
        return mPrefs.getBoolean(REPLAY_NO_ARENA, false);
    }

    void setReplayNoArena(boolean value) {
        mPrefs.edit().putBoolean(REPLAY_NO_ARENA, value).apply();
    }

    boolean replaySkipSelfTest() {
        return mPrefs.getBoolean(REPLAY_SKIP_SELF_TEST, false);
    }

    void setReplaySkipSelfTest(boolean value) {
        mPrefs.edit().putBoolean(REPLAY_SKIP_SELF_TEST, value).apply();
    }

    boolean replayAdvanced() {
        return mPrefs.getBoolean(REPLAY_ADVANCED, false);
    }

    void setReplayAdvanced(boolean value) {
        mPrefs.edit().putBoolean(REPLAY_ADVANCED, value).apply();
    }

    /** The -O2 runner and engine (release 0.4.1 experiment). */
    boolean replayOptimized() {
        return mPrefs.getBoolean(REPLAY_OPTIMIZED, false);
    }

    void setReplayOptimized(boolean value) {
        mPrefs.edit().putBoolean(REPLAY_OPTIMIZED, value).apply();
    }

    /** FAF_RUNNER_TIMERSLACK_NS=1 plus an affinity for the replay runs (release 0.4.1 experiment). */
    boolean replaySpeedExperiment() {
        return mPrefs.getBoolean(REPLAY_SPEED, false);
    }

    void setReplaySpeedExperiment(boolean value) {
        mPrefs.edit().putBoolean(REPLAY_SPEED, value).apply();
    }

    /** The experiment's affinity: only the biggest cores, instead of every core but the little ones. */
    boolean replayAffinityBig() {
        return mPrefs.getBoolean(REPLAY_AFFINITY_BIG, false);
    }

    void setReplayAffinityBig(boolean value) {
        mPrefs.edit().putBoolean(REPLAY_AFFINITY_BIG, value).apply();
    }

    /** The run directory of the last device probe, or null. */
    String lastProbeRun() {
        return mPrefs.getString(LAST_PROBE_RUN, null);
    }

    /** Name of the last run directory under runs/ (its result.json may still be the provisional one), or null. */
    String lastReplayRun() {
        return mPrefs.getString(LAST_REPLAY_RUN, null);
    }

    /**
     * Records a run as it starts, before its first step: if Android ends the app during the run, the
     * launcher shows that run (its provisional result.json says INTERRUPTED) and "Save run (zip)" exports
     * it. The previous job's message is cleared so it is not shown as this run's. Written synchronously
     * (commit, on the job's thread), because a kill right after would lose an apply().
     */
    void startReplayRun(String run, boolean deviceProbe) {
        SharedPreferences.Editor editor = mPrefs.edit();
        if (deviceProbe) {
            // The probe's result block shows this run; the replay card shows it too as the last run.
            editor.putString(LAST_PROBE_RUN, run);
        }
        editor
                .putString(LAST_REPLAY_RUN, run)
                .remove(LAST_REPLAY_MESSAGE)
                .remove(LAST_REPLAY_OK)
                .remove(LAST_REPLAY_TIME)
                .commit();
    }

    /** Outcome of the last replay job as the service saw it (also when it ended before writing a result). */
    void setLastReplayJob(String message, boolean ok) {
        mPrefs.edit()
                .putString(LAST_REPLAY_MESSAGE, message)
                .putBoolean(LAST_REPLAY_OK, ok)
                .putLong(LAST_REPLAY_TIME, System.currentTimeMillis())
                .apply();
    }

    String lastReplayMessage() {
        return mPrefs.getString(LAST_REPLAY_MESSAGE, null);
    }

    boolean lastReplayOk() {
        return mPrefs.getBoolean(LAST_REPLAY_OK, false);
    }

    long lastReplayTime() {
        return mPrefs.getLong(LAST_REPLAY_TIME, 0);
    }
}
