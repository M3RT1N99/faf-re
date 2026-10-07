package io.github.m3rt1n99.fafre;

import android.app.ActivityManager;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.Process;
import android.system.Os;
import android.system.OsConstants;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.FileVisitResult;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.SimpleFileVisitor;
import java.nio.file.attribute.BasicFileAttributes;
import java.text.SimpleDateFormat;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * The replay test, run by {@link ImportService} as a foreground job (milestone M3c on the phone,
 * without adb): the headless replay runner as its own process, exec'd from nativeLibraryDir.
 *
 * <p>Steps, each one runner process with its output in {@code <root>/runs/<run>/}:
 * <ol>
 * <li>Self-test (unless turned off): the arena probe (libfafarenaprobe.so in place of the engine:
 * exit 0 means every placement check passed), the probe with {@code capacity} (fills the arena to its
 * limit), and the engine load (the runner without a replay: it loads libfafengine.so below 2 GB, runs
 * its static initialisers and stops with the usage line, exit 1).</li>
 * <li>The replay: {@code /headlessreplay <replay> /init <init_faf.lua> /log ... /headlesssummary ...},
 * optionally twice to check that the result is deterministic.</li>
 * </ol>
 *
 * <p>Every path the runner sees is on the lowercase alias of the data root ({@link Runner#ensureAlias}),
 * and fa_path.lua is rewritten with that root before the run (GUI Start writes it back with the real
 * root before every game start; the jobs never overlap). The environment is the app's minus LD_PRELOAD
 * plus FAF_LOWARENA, FAF_ENGINE_LIB, FAF_KNOWN_FOLDERS (a runner home of its own, without Game.prefs,
 * equal to run_runner_android.py --host-prefs none) and TMPDIR; the working directory is the run
 * directory.
 *
 * <p>Verdict (docs/port/android.md, "Replay test"): the replay passes when the runner exits 0 and the
 * game ended ("game over at beat N"). Checksum mismatches against the recording are expected (the data
 * is FAF 3839, the recordings are older) and do not count; the checkpoint chain is compared with the
 * reference table instead, for replays whose file the table knows.
 */
final class ReplayTest {
    static final String RUNS_DIR = "runs";
    static final String RUNNER_HOME = "runner/home";
    static final String LOG_NAME = "replaytest.log";
    static final String RESULT_JSON = "result.json";
    static final String META_JSON = "meta.json";
    static final String SUMMARY_TXT = "summary.txt";
    static final String VERDICT_PASS = "PASS";
    static final String VERDICT_FAIL = "FAIL";
    static final String VERDICT_INCOMPLETE = "INCOMPLETE";
    static final String VERDICT_EXPECTED_CRASH = "EXPECTED_CRASH";
    static final String VERDICT_NOT_RUN = "NOT_RUN";
    static final String VERDICT_CANCELLED = "CANCELLED";
    /** The provisional result.json of a run that has not finished (or never will: Android ended the app). */
    static final String VERDICT_INTERRUPTED = "INTERRUPTED";
    /** result.json key: true while the run that wrote it has not finished. */
    static final String IN_PROGRESS = "in_progress";
    /** result.json key of a provisional result: the app process that runs the test. */
    static final String APP_PID = "app_pid";

    private static final long PROBE_WATCHDOG_MS = 180_000;
    private static final long REPLAY_WATCHDOG_MS = 600_000;
    private static final long GAME_EXIT_WAIT_MS = 2000;
    private static final String PREFIX = "io.github.m3rt1n99.fafre.extra.replay.";

    /** What the user asked for; travels in the service intent. */
    static final class Options {
        String replayStem;
        boolean selfTest = true;
        boolean replay = true;
        boolean repeat;
        boolean interlocked;
        boolean lowArena = true;

        void toIntent(Intent intent) {
            intent.putExtra(PREFIX + "stem", replayStem)
                    .putExtra(PREFIX + "selftest", selfTest)
                    .putExtra(PREFIX + "replay", replay)
                    .putExtra(PREFIX + "repeat", repeat)
                    .putExtra(PREFIX + "interlocked", interlocked)
                    .putExtra(PREFIX + "lowarena", lowArena);
        }

        static Options fromIntent(Intent intent) {
            Options options = new Options();
            options.replayStem = intent.getStringExtra(PREFIX + "stem");
            options.selfTest = intent.getBooleanExtra(PREFIX + "selftest", true);
            options.replay = intent.getBooleanExtra(PREFIX + "replay", true) && options.replayStem != null;
            options.repeat = intent.getBooleanExtra(PREFIX + "repeat", false);
            options.interlocked = intent.getBooleanExtra(PREFIX + "interlocked", false);
            options.lowArena = intent.getBooleanExtra(PREFIX + "lowarena", true);
            return options;
        }

        JSONObject toJson() throws JSONException {
            return new JSONObject().put("replay", replay ? replayStem : JSONObject.NULL).put("self_test", selfTest)
                    .put("repeat", repeat).put("interlocked", interlocked).put("lowarena", lowArena);
        }
    }

    /** One step's outcome, as recorded in result.json. */
    private static final class Step {
        final String name;
        final String title;
        String verdict = VERDICT_NOT_RUN;
        String detail = "";
        RunnerProcess.Outcome outcome;
        ReplayOutput output;
        JSONObject summary;
        String outFile = "";
        String logcatFile = "";
        String startedAt = "";
        List<String> argv = new ArrayList<>();
        Map<String, String> env = new java.util.TreeMap<>();

        Step(String name, String title) {
            this.name = name;
            this.title = title;
        }

        boolean passed() {
            return VERDICT_PASS.equals(verdict) || VERDICT_EXPECTED_CRASH.equals(verdict);
        }

        String chain() {
            if (summary != null && !summary.optString("checkpoint_chain_fnv1a").isEmpty()) {
                return summary.optString("checkpoint_chain_fnv1a");
            }
            return output != null && output.result("chain") != null ? output.result("chain") : "";
        }

        int gameOverBeat() {
            if (summary != null && summary.optBoolean("game_over", false)) {
                return summary.optInt("game_over_beat", -1);
            }
            return output != null ? output.gameOverBeat() : -1;
        }

        JSONObject toJson() throws JSONException {
            JSONObject json = new JSONObject().put("name", name).put("title", title).put("verdict", verdict)
                    .put("detail", detail).put("out", outFile).put("started", startedAt)
                    .put("argv", new JSONArray(argv)).put("env", new JSONObject(env));
            if (!logcatFile.isEmpty()) {
                json.put("logcat", logcatFile);
            }
            if (outcome != null) {
                json.put("exit_code", outcome.exitCode).put("signal", outcome.signal)
                        .put("signal_name", outcome.signal > 0 ? RunnerProcess.signalName(outcome.signal) : "")
                        .put("cancelled", outcome.cancelled).put("watchdog", outcome.watchdog)
                        .put("forced_kill", outcome.forced).put("wall_ms", outcome.wallMs);
                if (!outcome.started) {
                    json.put("start_error", outcome.startError);
                }
            }
            if (output != null) {
                putIfSet(json, "lowarena_report", output.arenaReport());
                putIfSet(json, "lowarena_engine", output.engineAt());
                putIfSet(json, "lowarena_full", output.arenaFull());
                putIfSet(json, "crash", output.crash());
                if (!output.crashLines().isEmpty()) {
                    json.put("crash_lines", new JSONArray(output.crashLines()));
                }
                putIfSet(json, "probe_result", output.probeResult());
                putIfSet(json, "probe_capacity", output.probeCapacity());
                putIfSet(json, "result_line", output.resultLine());
                if (!output.probeFailures().isEmpty()) {
                    json.put("probe_failures", new JSONArray(output.probeFailures()));
                }
                if (!output.problems().isEmpty()) {
                    json.put("engine_problems", new JSONArray(output.problems()));
                }
                if (!output.runnerErrors().isEmpty()) {
                    json.put("runner_errors", new JSONArray(output.runnerErrors()));
                }
            }
            if (summary != null) {
                JSONObject brief = new JSONObject();
                for (String key : new String[] {"end_reason", "exit_code", "reached_end", "beats_in_replay", "last_beat",
                    "replay_has_end_game", "game_over", "game_over_beat", "wall_seconds", "load_seconds", "sim_seconds",
                    "beats_per_second", "checkpoints_recorded", "checkpoints_reached", "checksum_checks",
                    "checksum_mismatches", "first_mismatch_beat", "engine_desync_reports", "checkpoint_chain_fnv1a",
                    "warnings", "lua_errors_load", "lua_errors_sim", "assertions", "sim_failure", "header_version",
                    "map"}) {
                    if (summary.has(key)) {
                        brief.put(key, summary.get(key));
                    }
                }
                json.put("summary", brief);
            }
            return json;
        }
    }

    private final Context mContext;
    private final LauncherLog mLog;
    private final Options mOptions;

    ReplayTest(Context context, LauncherLog log, Options options) {
        mContext = context.getApplicationContext();
        mLog = log;
        mOptions = options;
    }

    // ------------------------------------------------------------------ run

    /** Runs the steps; returns the headline, or throws it when the test did not pass. */
    String run(Cancellation cancel, Progress progress) throws IOException {
        DataManifest manifest = AppInfo.manifest(mContext);
        DataRoot root = AppInfo.dataRoot(mContext);
        String problem = Runner.problem(mContext);
        if (problem != null) {
            throw new IOException(problem);
        }
        progress.phase("Preparing", 0, -1);
        String alias = Runner.ensureAlias(mContext);
        endFinishedGameProcess(manifest, root);

        File runDir = newRunDirectory(root);
        String runName = runDir.getName();
        String aliasRun = alias + "/" + RUNS_DIR + "/" + runName;
        mLog.log("replay test " + runName + ": " + describeOptions());

        List<Step> steps = new ArrayList<>();
        if (mOptions.selfTest) {
            steps.add(new Step("probe", "Arena probe"));
            steps.add(new Step("capacity", "Arena capacity"));
            steps.add(new Step("engine-load", "Engine load"));
        }
        if (mOptions.replay) {
            steps.add(new Step("replay", mOptions.lowArena ? "Replay" : "Replay without the arena"));
            if (mOptions.repeat) {
                steps.add(new Step("replay-2", "Replay, second run"));
            }
        }
        JSONObject meta = new JSONObject();
        String started = Instant.now().toString();
        // A run that Android ends (battery management, a swipe on some devices, low memory) never reaches
        // writeResults below. Record it now, with a result.json that says so, so the card shows this run after
        // the app is reopened and "Save run (zip)" exports what it wrote; the end of the run replaces it.
        writeProvisional(runDir, runName, started, steps, 0, null);
        new Settings(mContext).startReplayRun(runName);
        ReplayFiles.Info info = null;
        ReplayFiles.Check check = null;
        String engineFile = "";
        String notRunReason = null;
        String setupError = null;
        boolean cancelled = false;
        try {
            meta.put("schema", 1).put("run", runName).put("started", started).put("options", mOptions.toJson());
            describeApp(meta);
            describeDevice(meta);
            progress.phase("Recording the binaries", 0, -1);
            JSONObject binaries = new JSONObject();
            for (Map.Entry<String, JSONObject> entry : Runner.describeBinaries(mContext, cancel).entrySet()) {
                binaries.put(entry.getKey(), entry.getValue());
            }
            meta.put("binaries", binaries);
            meta.put("paths", new JSONObject().put("data_root", root.path()).put("alias", alias)
                    .put("alias_lowercase", Runner.isLowerCase(alias))
                    .put("native_library_dir", Runner.nativeDir(mContext).getAbsolutePath())
                    .put("cache_dir", mContext.getCacheDir().getAbsolutePath()).put("run_dir", aliasRun)
                    .put("ld_preload_removed", System.getenv("LD_PRELOAD") != null ? System.getenv("LD_PRELOAD") : ""));

            // The data root as the runner expects it (run_runner_android.py creates the same).
            progress.phase("Preparing the data root", 0, -1);
            for (String dir : new String[] {ReplayFiles.DIR, RUNS_DIR, manifest.layout.vault + "/maps",
                manifest.layout.vault + "/mods", manifest.layout.scfa + "/movies", manifest.layout.scfa + "/sounds",
                manifest.layout.scfa + "/fonts", manifest.layout.logs, "runner"}) {
                mkdirs(root, dir);
            }
            File home = root.file(RUNNER_HOME);
            deleteTree(home);
            mkdirs(root, RUNNER_HOME);
            File init = root.find(manifest.initScript());
            if (init == null || !init.isFile()) {
                throw new IOException(manifest.initScript() + " is missing; get the FAF files first.");
            }
            String initArg = alias + "/" + relative(root, init);
            FafVersion installed = FafVersion.read(root.file(manifest.layout.fafVersion));
            int fafVersion = installed != null ? installed.version : manifest.fafVersion;
            File faPath = FaPathWriter.write(manifest, root, alias, fafVersion, AppInfo.versionName(mContext));
            meta.put("fa_path_lua", FileOps.readText(faPath, 64 * 1024));

            if (mOptions.replay) {
                progress.phase("Checking the replay and the data", 0, -1);
                info = ReplayFiles.read(root, mOptions.replayStem);
                if (info == null) {
                    notRunReason = "The selected replay is gone; pick it again.";
                } else {
                    File source = ReplayFiles.file(root, info);
                    String sha = source.isFile() ? FileOps.sha256(source, cancel, null) : "";
                    if (!sha.equals(info.sha256())) {
                        notRunReason = "The replay file changed or disappeared since it was picked; pick it again.";
                    } else {
                        String runnerId = Runner.buildId(Runner.file(mContext, Runner.EXECUTABLE));
                        if (!runnerId.equals(info.json.optString("analyzed_by"))) {
                            progress.phase("Reading the replay", 0, -1);
                            info = ReplayFiles.analyze(mContext, info, cancel);
                        }
                        DataStatus status = DataStatus.check(manifest, root, mLog);
                        check = ReplayFiles.check(manifest, root, status, info);
                        if (!check.ok()) {
                            notRunReason = String.join(" ", check.problems);
                        }
                        engineFile = engineFileFor(root, info, cancel);
                    }
                }
                meta.put("replay", describeReplay(info, engineFile, check));
            }

            int index = 0;
            for (Step step : steps) {
                ++index;
                cancel.throwIfCancelled();
                String label = "Step " + index + "/" + steps.size() + ": " + step.title;
                if (step.name.startsWith("replay") && notRunReason != null) {
                    step.verdict = VERDICT_NOT_RUN;
                    step.detail = notRunReason;
                    continue;
                }
                writeProvisional(runDir, runName, started, steps, index, meta);
                if (step.name.startsWith("replay")) {
                    if (step.name.equals("replay-2")) {
                        deleteTree(home);
                        mkdirs(root, RUNNER_HOME);
                    }
                    runReplay(step, label, index, runDir, aliasRun, alias, initArg, engineFile, cancel, progress);
                } else {
                    runSelfTest(step, label, index, runDir, alias, cancel, progress);
                }
                mLog.log("replay test " + runName + ": " + step.name + " " + step.verdict + " " + step.detail);
                if (step.outcome != null && step.outcome.cancelled) {
                    cancelled = true;
                    break;
                }
            }
        } catch (Cancellation.CancelledException e) {
            cancelled = true;
        } catch (IOException e) {
            // Recorded like any other result, so the run directory explains itself in the zip.
            setupError = e.getMessage() != null ? e.getMessage() : e.toString();
            mLog.log("replay test " + runName + ": " + setupError);
        } catch (JSONException e) {
            throw new IOException(e.getMessage(), e);
        } finally {
            if (cancelled || cancel.isCancelled()) {
                for (Step step : steps) {
                    if (VERDICT_NOT_RUN.equals(step.verdict) && step.outcome == null && step.detail.isEmpty()) {
                        step.detail = "cancelled before it ran";
                    }
                }
            }
        }

        cancelled |= cancel.isCancelled();
        Verdict verdict = evaluate(steps, info, cancelled, notRunReason);
        if (setupError != null && !cancelled) {
            verdict.verdict = VERDICT_FAIL;
            verdict.ok = false;
            verdict.headline = "FAIL · " + setupError;
            verdict.lines.add(0, new String[] {"bad", "Stopped before or between the steps: " + setupError});
        }
        try {
            writeResults(root, runDir, runName, started, meta, steps, info, check, verdict);
        } catch (JSONException e) {
            throw new IOException(e.getMessage(), e);
        }
        if (cancelled) {
            throw new Cancellation.CancelledException(cancel.reason() != null ? cancel.reason() : "Cancelled");
        }
        if (!verdict.ok) {
            throw new IOException(verdict.headline);
        }
        return verdict.headline;
    }

    private String describeOptions() {
        return (mOptions.selfTest ? "self-test" : "no self-test")
                + (mOptions.replay ? ", replay " + mOptions.replayStem : "")
                + (mOptions.replay && mOptions.repeat ? ", twice" : "") + (mOptions.interlocked ? ", interlocked" : "")
                + (mOptions.lowArena ? "" : ", FAF_LOWARENA=0");
    }

    /**
     * The test never runs next to the game. A :game process whose run has ended (launch/status.json says
     * exited or error; Android keeps the process cached) is ended first, as GUI Start does. A game that is
     * still loading or running is not ended: the test refuses to start and says how to close it.
     */
    private void endFinishedGameProcess(DataManifest manifest, DataRoot root) throws IOException {
        int pid = AppInfo.gameProcessPid(mContext);
        if (pid <= 0) {
            return;
        }
        RunStatus status;
        try {
            status = RunStatus.read(root.file(manifest.layout.launch + "/status.json"));
        } catch (IOException e) {
            status = null;
        }
        if (status == null || !status.isTerminal()) {
            mLog.log("replay test: not started, the game process " + pid + " is running (status "
                    + (status == null ? "none" : status.state) + ")");
            throw new IOException("Not started: the game is running. Close it first (Back in the game), then start "
                    + "the test again. If the game is not on screen, end the app in Android's settings (App info › "
                    + "Force stop) and open it again.");
        }
        mLog.log("replay test: ending the finished game's process " + pid + " (status " + status.state + ")");
        Process.killProcess(pid);
        long deadline = System.currentTimeMillis() + GAME_EXIT_WAIT_MS;
        while (AppInfo.gameProcessPid(mContext) > 0 && System.currentTimeMillis() < deadline) {
            try {
                Thread.sleep(50);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                break;
            }
        }
        if (AppInfo.gameProcessPid(mContext) > 0) {
            throw new IOException("The game is still running; close it and start the test again.");
        }
    }

    private static File newRunDirectory(DataRoot root) throws IOException {
        String base = new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(new Date());
        mkdirs(root, RUNS_DIR);
        for (int i = 0; i < 100; ++i) {
            String name = i == 0 ? base : base + "-" + (i + 1);
            File dir = root.file(RUNS_DIR + "/" + name);
            if (dir.mkdir()) {
                return dir;
            }
        }
        throw new IOException("cannot create a run directory under " + RUNS_DIR);
    }

    /** The .scfareplay for /headlessreplay: the converted copy when it is intact, else the file itself. */
    private static String engineFileFor(DataRoot root, ReplayFiles.Info info, Cancellation cancel) throws IOException {
        String name = info.engineFileName();
        if (!name.isEmpty()) {
            File file = root.file(ReplayFiles.DIR + "/" + name);
            if (file.isFile() && FileOps.sha256(file, cancel, null).equals(info.engineFileSha256())) {
                return name;
            }
        }
        return info.fileName();
    }

    // ---------------------------------------------------------------- steps

    private void runSelfTest(Step step, String label, int index, File runDir, String alias, Cancellation cancel,
            Progress progress) throws IOException {
        progress.phase(label, 0, -1);
        Map<String, String> env = ReplayFiles.baseEnvironment(mContext, alias);
        step.argv.add(Runner.file(mContext, Runner.EXECUTABLE).getAbsolutePath());
        if (!step.name.equals("engine-load")) {
            env.put("FAF_ENGINE_LIB", Runner.file(mContext, Runner.PROBE).getAbsolutePath());
            if (step.name.equals("capacity")) {
                step.argv.add("capacity");
            }
        }
        ReplayOutput output = new ReplayOutput();
        execute(step, index, runDir, env, PROBE_WATCHDOG_MS, output, cancel, line -> {
            if (output.accept(line) && output.probeResult() != null) {
                progress.item(output.probeResult(), 0);
            }
        });
        RunnerProcess.Outcome outcome = step.outcome;
        if (outcome.cancelled) {
            step.verdict = VERDICT_CANCELLED;
            step.detail = "cancelled";
            return;
        }
        if (step.name.equals("engine-load")) {
            boolean loaded = output.engineAt() != null;
            if (outcome.exitCode == 1 && output.usage() && loaded) {
                step.verdict = VERDICT_PASS;
                step.detail = "libfafengine.so loaded" + addressOf(output.engineAt()) + ", static initialisers ran";
            } else {
                step.verdict = VERDICT_FAIL;
                step.detail = outcome.describeExit() + (loaded ? "" : ", the engine library did not load")
                        + firstOf(output.runnerErrors()) + crashOf(output);
            }
            return;
        }
        if (outcome.exitCode == 0 && output.probePass()) {
            step.verdict = VERDICT_PASS;
            step.detail = step.name.equals("capacity") && output.probeCapacity() != null
                    ? output.probeCapacity() : output.probeResult();
        } else {
            step.verdict = VERDICT_FAIL;
            step.detail = outcome.describeExit() + (output.probeResult() != null ? ", " + output.probeResult() : "")
                    + firstOf(output.probeFailures()) + firstOf(output.runnerErrors()) + crashOf(output);
        }
    }

    private void runReplay(Step step, String label, int index, File runDir, String aliasRun, String alias,
            String initArg, String engineFile, Cancellation cancel, Progress progress) throws IOException {
        progress.phase(label + " · starting", 0, -1);
        String prefix = index + "-" + step.name;
        step.argv.addAll(Arrays.asList(Runner.file(mContext, Runner.EXECUTABLE).getAbsolutePath(),
                "/headlessreplay", alias + "/" + ReplayFiles.DIR + "/" + engineFile,
                "/init", initArg,
                "/log", aliasRun + "/" + prefix + ".log",
                "/headlesssummary", aliasRun + "/" + prefix + ".summary.json",
                "/headlessprogress", "100"));
        if (mOptions.interlocked) {
            step.argv.add("/headlessinterlocked");
        }
        Map<String, String> env = ReplayFiles.baseEnvironment(mContext, alias);
        env.put("FAF_LOWARENA", mOptions.lowArena ? "1" : "0");
        ReplayOutput output = new ReplayOutput();
        final ReplayOutput.Phase[] shown = {null};
        execute(step, index, runDir, env, REPLAY_WATCHDOG_MS, output, cancel, line -> {
            if (!output.accept(line)) {
                return;
            }
            ReplayOutput.Phase phase = output.phase();
            if (phase != shown[0]) {
                shown[0] = phase;
                progress.phase(label + " · " + phase.label.toLowerCase(Locale.ROOT),
                        phase == ReplayOutput.Phase.PLAYING ? Math.max(0, output.totalBeats()) : 0, -1);
            }
            progress.item(output.describeProgress(), Math.max(0, output.beat()));
        });
        File summaryFile = new File(runDir, prefix + ".summary.json");
        try {
            String text = FileOps.readText(summaryFile, 16 * 1024 * 1024);
            step.summary = text != null ? new JSONObject(text) : null;
        } catch (IOException | JSONException e) {
            step.summary = null;
        }
        RunnerProcess.Outcome outcome = step.outcome;
        int gameOver = step.gameOverBeat();
        int exit = outcome.exitCode;
        if (outcome.cancelled) {
            step.verdict = VERDICT_CANCELLED;
            step.detail = "cancelled at beat " + output.beat();
        } else if (!mOptions.lowArena && outcome.signalled()) {
            step.verdict = VERDICT_EXPECTED_CRASH;
            step.detail = "crashed without the low arena, as expected: " + outcome.describeExit() + crashOf(output);
        } else if (outcome.started && exit == 0 && gameOver >= 0) {
            step.verdict = VERDICT_PASS;
            step.detail = "played to the end: game over at beat " + gameOver
                    + (output.totalBeats() > 0 ? " (the replay has " + output.totalBeats() + " beats)" : "") + ", exit 0"
                    + (mOptions.lowArena ? "" : " (without the low arena; a crash was expected)");
        } else if (outcome.started && exit == 0) {
            step.verdict = VERDICT_INCOMPLETE;
            step.detail = "the replay ended without a game over (" + endReason(step, output) + ")";
        } else {
            step.verdict = VERDICT_FAIL;
            step.detail = (outcome.watchdog ? "no output for " + REPLAY_WATCHDOG_MS / 1000 + " s, stopped; " : "")
                    + outcome.describeExit() + meaningOf(exit) + (output.beat() > 0 ? " at beat " + output.beat()
                    : " while " + output.phase().label.toLowerCase(Locale.ROOT))
                    + (step.summary != null || output.resultLine() != null ? " (" + endReason(step, output) + ")" : "")
                    + crashOf(output) + firstOf(output.problems()) + firstOf(output.runnerErrors());
        }
    }

    /** Starts the step's process with its output in {@code <index>-<name>.out}; logcat after a signal. */
    private void execute(Step step, int index, File runDir, Map<String, String> env, long watchdogMs,
            ReplayOutput output, Cancellation cancel, RunnerProcess.LineListener listener) throws IOException {
        step.output = output;
        step.env.putAll(env);
        step.outFile = index + "-" + step.name + ".out";
        File outFile = new File(runDir, step.outFile);
        long since = System.currentTimeMillis();
        step.startedAt = Instant.now().toString();
        try (FileOutputStream header = new FileOutputStream(outFile)) {
            StringBuilder text = new StringBuilder("[app] ").append(step.startedAt).append(" cwd ")
                    .append(runDir.getName()).append("\n[app] env");
            for (Map.Entry<String, String> entry : new java.util.TreeMap<>(env).entrySet()) {
                text.append(' ').append(entry.getKey()).append('=').append(entry.getValue());
            }
            text.append("\n[app] argv ").append(LaunchArgs.describe(step.argv.toArray(new String[0]))).append('\n');
            header.write(text.toString().getBytes(StandardCharsets.UTF_8));
        }
        step.outcome = RunnerProcess.run(step.argv, env, runDir, outFile, watchdogMs, cancel, listener);
        try (FileOutputStream footer = new FileOutputStream(outFile, true)) {
            footer.write(("[app] " + step.outcome.describeExit() + " after " + step.outcome.wallMs + " ms"
                    + (step.outcome.watchdog ? " (watchdog)" : "") + (step.outcome.cancelled ? " (cancelled)" : "")
                    + "\n").getBytes(StandardCharsets.UTF_8));
        }
        if (step.outcome.signalled() && !step.outcome.cancelled) {
            step.logcatFile = index + "-" + step.name + ".logcat.txt";
            captureLogcat(new File(runDir, step.logcatFile), since);
        }
    }

    /** The app's own log buffers (an app sees only its uid's lines) since the step started. */
    private void captureLogcat(File target, long sinceMillis) {
        String since = new SimpleDateFormat("MM-dd HH:mm:ss.SSS", Locale.ROOT).format(new Date(sinceMillis - 2000));
        List<String> argv = new ArrayList<>(Arrays.asList("/system/bin/logcat", "-d", "-v", "threadtime", "-b",
                "main,system,crash", "-T", since));
        try {
            Cancellation own = new Cancellation();
            RunnerProcess.Outcome outcome = RunnerProcess.run(argv, new java.util.HashMap<String, String>(),
                    target.getParentFile(), target, 20_000, own, null);
            if (outcome.exitCode != 0) {
                argv.subList(argv.size() - 2, argv.size()).clear();
                RunnerProcess.run(argv, new java.util.HashMap<String, String>(), target.getParentFile(), target,
                        20_000, own, null);
            }
        } catch (IOException e) {
            mLog.log("replay test: logcat failed: " + e.getMessage());
        }
    }

    private static String endReason(Step step, ReplayOutput output) {
        if (step.summary != null && !step.summary.optString("end_reason").isEmpty()) {
            return step.summary.optString("end_reason");
        }
        String end = output.result("end");
        return end != null ? end : "no summary";
    }

    private static String meaningOf(int exit) {
        switch (exit) {
            case 1:
                return " (bad arguments or setup)";
            case 2:
                return " (the scenario failed to load)";
            case 3:
                return " (the sim failed or the engine died)";
            case 4:
                return " (no progress within the timeout)";
            default:
                return "";
        }
    }

    private static String addressOf(String engineLine) {
        if (engineLine == null) {
            return "";
        }
        java.util.regex.Matcher m = java.util.regex.Pattern.compile(" at (0x[0-9a-fA-F]+)").matcher(engineLine);
        return m.find() ? " at " + m.group(1) + (engineLine.contains("FAF_LOWARENA=0") ? " (arena off)" : "") : "";
    }

    private static String firstOf(List<String> lines) {
        return lines.isEmpty() ? "" : "; " + lines.get(0);
    }

    private static String crashOf(ReplayOutput output) {
        return output.crash() != null ? "; crash handler: " + output.crashSummary() : "";
    }

    // -------------------------------------------------------------- verdict

    private static final class Verdict {
        String verdict;
        String headline;
        boolean ok;
        String referenceStatus = "none";
        String referenceText = "";
        String determinism = "not_run";
        final List<String[]> lines = new ArrayList<>();
    }

    private Verdict evaluate(List<Step> steps, ReplayFiles.Info info, boolean cancelled, String notRunReason) {
        Verdict v = new Verdict();
        Step replay = null;
        Step second = null;
        List<String> selfTestFailures = new ArrayList<>();
        int selfTests = 0;
        for (Step step : steps) {
            if (step.name.equals("replay")) {
                replay = step;
            } else if (step.name.equals("replay-2")) {
                second = step;
            } else {
                ++selfTests;
                if (!step.passed() && !VERDICT_NOT_RUN.equals(step.verdict) && !VERDICT_CANCELLED.equals(step.verdict)) {
                    selfTestFailures.add(step.title.toLowerCase(Locale.ROOT));
                }
            }
        }

        if (selfTests > 0) {
            StringBuilder line = new StringBuilder("Self-test:");
            boolean all = true;
            for (Step step : steps) {
                if (!step.name.startsWith("replay")) {
                    line.append(' ').append(step.title.toLowerCase(Locale.ROOT)).append(' ')
                            .append(step.verdict.equals(VERDICT_PASS) ? "PASS" : step.verdict).append(';');
                    all &= step.passed();
                }
            }
            line.setLength(line.length() - 1);
            v.lines.add(new String[] {all ? "good" : "bad", line.toString()});
            for (Step step : steps) {
                if (!step.name.startsWith("replay") && !step.detail.isEmpty()) {
                    v.lines.add(new String[] {step.passed() ? "muted" : "bad", "  " + step.title + ": " + step.detail});
                }
            }
        }

        if (replay != null) {
            String tone = replay.passed() ? "good" : VERDICT_INCOMPLETE.equals(replay.verdict)
                    || VERDICT_NOT_RUN.equals(replay.verdict) ? "warn" : "bad";
            String what = info != null ? (info.uid().isEmpty() ? info.stem() : info.uid()) : mOptions.replayStem;
            v.lines.add(new String[] {tone, "Replay " + what + ": " + label(replay.verdict) + ", " + replay.detail});
            String chain = replay.chain();
            if (replay.summary != null || !chain.isEmpty()) {
                compareReference(v, info, chain, replay);
                JSONObject s = replay.summary;
                if (s != null) {
                    int checks = s.optInt("checksum_checks", 0);
                    int mismatches = s.optInt("checksum_mismatches", 0);
                    if (checks > 0) {
                        int recorded = info != null ? info.recordedGameVersion() : -1;
                        v.lines.add(new String[] {"muted", "Recorded checksums: " + (checks - mismatches) + "/" + checks
                                + " match" + (mismatches > 0 ? " (expected: " + (recorded > 0 ? "recorded on "
                                + recorded + ", " : "") + "the data is FAF 3839; the chain is what counts)" : "")});
                    }
                    v.lines.add(new String[] {"muted", String.format(Locale.ROOT,
                            "Times: load %.1f s, sim %.1f s (%.0f beats/s), wall %.1f s · Lua errors: sim %d, load %d"
                                    + " · engine desyncs %d · assertions %d",
                            s.optDouble("load_seconds", 0), s.optDouble("sim_seconds", 0),
                            s.optDouble("beats_per_second", 0), s.optDouble("wall_seconds", 0),
                            s.optInt("lua_errors_sim", 0), s.optInt("lua_errors_load", 0),
                            s.optInt("engine_desync_reports", 0), s.optInt("assertions", 0))});
                }
            }
            if (second != null) {
                // Identical: the same verdict, game-over beat and chain. Two crashes without the arena have no
                // chain; they count as identical when everything else matches.
                boolean sameEnd = second.verdict.equals(replay.verdict) && second.gameOverBeat() == replay.gameOverBeat();
                boolean sameChain = second.chain().equals(chain)
                        && (!chain.isEmpty() || VERDICT_EXPECTED_CRASH.equals(replay.verdict));
                if (second.outcome == null || VERDICT_CANCELLED.equals(second.verdict)) {
                    v.determinism = "not_run";
                } else if (sameEnd && sameChain) {
                    v.determinism = "identical";
                    v.lines.add(new String[] {"good", "Second run: identical (" + (chain.isEmpty() ? label(second.verdict)
                            : "chain " + chain) + (second.gameOverBeat() >= 0 ? ", game over at beat "
                            + second.gameOverBeat() : "") + ")"});
                } else {
                    v.determinism = "differs";
                    v.lines.add(new String[] {"bad", "Second run differs: " + label(second.verdict) + ", chain "
                            + (second.chain().isEmpty() ? "none" : second.chain()) + ", " + second.detail
                            + ". The same binaries and data must give the same result; send the run zip."});
                }
            }
            String arena = replay.output != null ? replay.output.arenaReport() : null;
            if (arena != null) {
                v.lines.add(new String[] {"muted", arena});
            }
        }

        // The overall verdict and the one-line headline.
        if (cancelled) {
            v.verdict = VERDICT_CANCELLED;
            v.headline = "Cancelled";
        } else if (!selfTestFailures.isEmpty()) {
            v.verdict = VERDICT_FAIL;
            v.headline = "FAIL · self-test: " + String.join(", ", selfTestFailures) + " failed"
                    + (replay != null ? " · replay " + label(replay.verdict).toLowerCase(Locale.ROOT) : "");
        } else if (replay == null) {
            v.verdict = VERDICT_PASS;
            v.headline = "PASS · self-test (no replay selected)";
        } else if (VERDICT_NOT_RUN.equals(replay.verdict)) {
            v.verdict = VERDICT_NOT_RUN;
            v.headline = (selfTests > 0 ? "Self-test passed · " : "") + "replay not run: " + notRunReason;
        } else {
            v.verdict = replay.verdict;
            StringBuilder headline = new StringBuilder(label(replay.verdict));
            if (replay.passed() && replay.gameOverBeat() >= 0) {
                headline.append(" · game over at beat ").append(replay.gameOverBeat());
            } else if (!replay.passed()) {
                headline.append(" · ").append(replay.detail);
            }
            if (!replay.chain().isEmpty()) {
                headline.append(" · chain ").append(replay.chain()).append(' ').append(v.referenceText);
            }
            if ("differs".equals(v.determinism)) {
                // The second run counts: a run that does not repeat itself has not passed.
                if (replay.passed()) {
                    v.verdict = VERDICT_FAIL;
                    headline.insert(0, "FAIL · second run differs (" + label(second.verdict).toLowerCase(Locale.ROOT)
                            + ", chain " + (second.chain().isEmpty() ? "none" : second.chain()) + ") · first run: ");
                } else {
                    headline.append(" · second run differs");
                }
            }
            v.headline = headline.toString();
        }
        v.ok = VERDICT_PASS.equals(v.verdict) || VERDICT_EXPECTED_CRASH.equals(v.verdict);
        return v;
    }

    private void compareReference(Verdict v, ReplayFiles.Info info, String chain, Step replay) {
        ReplayRefs refs = ReplayRefs.get(mContext);
        ReplayRefs.Ref ref = info != null ? refs.find(info.sha256(), info.engineFileSha256()) : null;
        if (chain.isEmpty()) {
            v.referenceStatus = "no-chain";
            v.referenceText = "";
            v.lines.add(new String[] {"warn", "No checkpoint chain (no checkpoint was reached)."});
            return;
        }
        if (ref == null) {
            v.referenceStatus = refs.error() != null ? "unavailable" : "unknown-replay";
            v.referenceText = "(no reference for this replay)";
            v.lines.add(new String[] {"muted", "Checkpoint chain " + chain + ": no reference for this replay file"
                    + (refs.error() != null ? " (" + refs.error() + ")" : refs.size() > 0 ? " (the table has "
                    + String.join(", ", refs.ids()) + ")" : "") + "."});
            return;
        }
        String status = ref.compare(chain);
        v.referenceStatus = status;
        String expected = ref.chains.isEmpty() ? "none" : String.join(" / ", ref.chains);
        String where = ref.measuredOn.isEmpty() ? "" : " (measured on " + ref.measuredOn + ")";
        if ("match".equals(status)) {
            v.referenceText = "matches the reference";
            v.lines.add(new String[] {"good", "Checkpoint chain " + chain + " matches the reference" + where + "."});
        } else if ("no-chain".equals(status)) {
            v.referenceText = "(the reference has no chain)";
            v.lines.add(new String[] {"muted", "Checkpoint chain " + chain + ": the reference entry has no chain."});
        } else {
            v.referenceText = "differs from the reference " + expected;
            v.lines.add(new String[] {"warn", "Checkpoint chain " + chain + " differs from the reference " + expected
                    + where + ". Not necessarily a port bug: send the run zip."});
        }
        if (!ref.buildIds.isEmpty()) {
            // G10: a chain is only comparable between identical binaries.
            String engine = Runner.buildId(Runner.file(mContext, Runner.ENGINE));
            String runner = Runner.buildId(Runner.file(mContext, Runner.EXECUTABLE));
            if (ref.measuredWith(engine) && ref.measuredWith(runner)) {
                v.lines.add(new String[] {"muted", "The reference was measured with these binaries (engine "
                        + shortId(engine) + ", runner " + shortId(runner) + ")."});
            } else {
                v.lines.add(new String[] {"warn", "These binaries are not the ones the reference was measured with "
                        + "(engine " + shortId(engine) + ", runner " + shortId(runner) + "); a different chain may come "
                        + "from that."});
            }
        }
        int gameOver = replay.gameOverBeat();
        if (ref.gameOverBeat >= 0 && gameOver >= 0 && ref.gameOverBeat != gameOver) {
            v.lines.add(new String[] {"warn", "Game over at beat " + gameOver + ", the reference ended at beat "
                    + ref.gameOverBeat + "."});
        }
        if (!ref.windowsChain.isEmpty() || !ref.firstDivergingVsWindows.isEmpty()) {
            v.lines.add(new String[] {"muted", "Windows: " + (ref.windowsChain.isEmpty() ? "" : "chain "
                    + ref.windowsChain + (chain.equals(ref.windowsChain) ? " (equal)" : "")) + (ref.firstDivergingVsWindows
                    .isEmpty() ? "" : (ref.windowsChain.isEmpty() ? "" : ", ") + "the reference first differs at "
                    + ref.firstDivergingVsWindows)});
        }
    }

    private static String label(String verdict) {
        switch (verdict) {
            case VERDICT_EXPECTED_CRASH:
                return "CRASHED (expected)";
            case VERDICT_NOT_RUN:
                return "NOT RUN";
            default:
                return verdict;
        }
    }

    // --------------------------------------------------------------- output

    private void writeResults(DataRoot root, File runDir, String runName, String started, JSONObject meta,
            List<Step> steps, ReplayFiles.Info info, ReplayFiles.Check check, Verdict verdict)
            throws IOException, JSONException {
        String ended = Instant.now().toString();
        JSONArray stepJson = new JSONArray();
        for (Step step : steps) {
            stepJson.put(step.toJson());
        }
        meta.put("ended", ended).put("steps", stepJson);

        JSONObject result = new JSONObject();
        result.put("schema", 1).put("run", runName).put("started", started).put("ended", ended)
                .put("verdict", verdict.verdict).put("ok", verdict.ok).put("headline", verdict.headline)
                .put("reference", verdict.referenceStatus).put("determinism", verdict.determinism)
                .put("options", mOptions.toJson()).put("steps", stepJson);
        if (info != null) {
            result.put("replay", new JSONObject().put("stem", info.stem()).put("uid", info.uid())
                    .put("sha256", info.sha256()).put("map", info.mapFolder()).put("beats", info.beats()));
        }
        JSONArray lines = new JSONArray();
        if (check != null) {
            for (String warning : check.warnings) {
                verdict.lines.add(new String[] {"warn", "Note: " + warning});
            }
        }
        for (String[] line : verdict.lines) {
            lines.put(new JSONObject().put("tone", line[0]).put("text", line[1]));
        }
        result.put("lines", lines);
        String summary = summaryText(runName, verdict, meta);
        result.put("summary_text", summary);

        FileOps.writeJson(new File(runDir, META_JSON), meta.toString(1));
        FileOps.writeJson(new File(runDir, RESULT_JSON), result.toString(1));
        FileOps.writeAtomic(new File(runDir, SUMMARY_TXT), summary);
        File log = root.prepare(LauncherLog.LOGS_DIR + "/" + LOG_NAME);
        try (FileOutputStream out = new FileOutputStream(log, true)) {
            out.write((summary + "\n").getBytes(StandardCharsets.UTF_8));
        }
        mLog.log("replay test " + runName + ": " + verdict.headline);
    }

    /**
     * The result.json (and summary.txt, and meta.json once it is known) of a run that has not finished: what
     * stays when the run never reaches {@link #writeResults}, because Android ended the app's process (the
     * runner, its child, is ended with it) or the app crashed. Written before the first step and again as
     * each step starts, so it names the step that was running; {@link #writeResults} replaces all three.
     * Best effort: a failed write is logged and the run goes on.
     *
     * @param current the 1-based index of the step about to start, 0 while preparing
     */
    private void writeProvisional(File runDir, String runName, String started, List<Step> steps,
            int current, JSONObject meta) {
        try {
            String where = current <= 0 ? "while preparing"
                    : "during step " + current + "/" + steps.size() + " (" + steps.get(current - 1).title + ")";
            String headline = VERDICT_INTERRUPTED + " · the run stopped " + where
                    + ": Android ended the app, or it crashed";
            List<String[]> lines = new ArrayList<>();
            lines.add(new String[] {"bad", "This run did not finish: the app was ended " + where + " (Android's "
                    + "battery or memory management, the app swiped away, or a crash), and the runner with it."});
            JSONArray stepJson = new JSONArray();
            for (int i = 0; i < steps.size(); ++i) {
                Step step = steps.get(i);
                boolean done = i + 1 < current;
                boolean running = i + 1 == current;
                JSONObject json = new JSONObject().put("name", step.name).put("title", step.title)
                        .put("verdict", done ? step.verdict : running ? "RUNNING" : VERDICT_NOT_RUN)
                        .put("detail", done ? step.detail : "");
                if (done || running) {
                    json.put("out", (i + 1) + "-" + step.name + ".out");
                }
                stepJson.put(json);
                if (done && !VERDICT_NOT_RUN.equals(step.verdict)) {
                    // As in the final result: the detail of a passed step ("PASS: 41 of 41 ...") says enough.
                    lines.add(new String[] {step.passed() ? "muted" : "bad", "  " + step.title + ": "
                            + (step.passed() && !step.detail.isEmpty() ? step.detail
                            : label(step.verdict) + (step.detail.isEmpty() ? "" : ", " + step.detail))});
                }
            }
            lines.add(new String[] {"warn", "Save run (zip) has what the run wrote until then (runner output, engine "
                    + "log). Run the test again with the app open or in the background; on Samsung set the app's "
                    + "battery use to Unrestricted."});

            StringBuilder summary = new StringBuilder("faf-re replay test ").append(runName).append(": ")
                    .append(headline).append('\n').append("App ").append(AppInfo.versionName(mContext)).append(" · ")
                    .append(Build.MANUFACTURER).append(' ').append(Build.MODEL).append(" · Android ")
                    .append(Build.VERSION.RELEASE).append('\n');
            JSONArray lineJson = new JSONArray();
            for (String[] line : lines) {
                lineJson.put(new JSONObject().put("tone", line[0]).put("text", line[1]));
                summary.append(line[1]).append('\n');
            }
            JSONObject result = new JSONObject().put("schema", 1).put("run", runName).put("started", started)
                    .put("ended", "").put(IN_PROGRESS, true).put(APP_PID, Process.myPid())
                    .put("verdict", VERDICT_INTERRUPTED).put("ok", false).put("headline", headline)
                    .put("reference", "none").put("determinism", "not_run").put("options", mOptions.toJson())
                    .put("steps", stepJson).put("lines", lineJson).put("summary_text", summary.toString());
            if (meta != null) {
                FileOps.writeJson(new File(runDir, META_JSON), new JSONObject(meta.toString()).put(IN_PROGRESS, true)
                        .toString(1));
            }
            FileOps.writeJson(new File(runDir, RESULT_JSON), result.toString(1));
            FileOps.writeAtomic(new File(runDir, SUMMARY_TXT), summary.toString());
        } catch (IOException | JSONException e) {
            mLog.log("replay test " + runName + ": cannot write the provisional result: " + e.getMessage());
        }
    }

    /** The text "Copy summary" puts on the clipboard: enough to judge the run without the zip. */
    private String summaryText(String runName, Verdict verdict, JSONObject meta) {
        StringBuilder out = new StringBuilder();
        out.append("faf-re replay test ").append(runName).append(": ").append(verdict.headline).append('\n');
        JSONObject app = meta.optJSONObject("app");
        JSONObject device = meta.optJSONObject("device");
        if (app != null) {
            out.append("App ").append(app.optString("version_name")).append(" (").append(app.optInt("version_code"))
                    .append(", ").append(shortCommit(app.optString("commit"))).append(app.optBoolean("dirty") ? "+" : "")
                    .append(") ").append(app.optString("abi")).append('\n');
        }
        if (device != null) {
            out.append("Device ").append(device.optString("manufacturer")).append(' ').append(device.optString("model"))
                    .append(" (").append(device.optString("soc")).append(") · Android ").append(device.optString("release"))
                    .append(" (API ").append(device.optInt("sdk")).append(") · ").append(device.optString("supported_abis"))
                    .append(" · page ").append(device.optLong("page_size")).append('\n');
            String kernel = device.optString("proc_version");
            if (!kernel.isEmpty()) {
                out.append("Kernel ").append(kernel.length() > 120 ? kernel.substring(0, 120) + "…" : kernel).append('\n');
            }
        }
        JSONObject binaries = meta.optJSONObject("binaries");
        if (binaries != null) {
            JSONObject engine = binaries.optJSONObject(Runner.ENGINE);
            JSONObject runner = binaries.optJSONObject(Runner.EXECUTABLE);
            out.append("Build ids: engine ").append(engine != null ? engine.optString("build_id") : "?")
                    .append(", runner ").append(runner != null ? runner.optString("build_id") : "?").append('\n');
        }
        JSONObject replay = meta.optJSONObject("replay");
        if (replay != null) {
            out.append("Replay ").append(replay.optString("stem")).append(" sha256 ").append(replay.optString("sha256"))
                    .append('\n');
        }
        for (String[] line : verdict.lines) {
            out.append(line[1]).append('\n');
        }
        return out.toString();
    }

    private static String shortId(String id) {
        return id == null || id.isEmpty() ? "?" : id.length() > 12 ? id.substring(0, 12) : id;
    }

    private static String shortCommit(String commit) {
        return commit.length() > 10 ? commit.substring(0, 10) : commit.isEmpty() ? "unknown commit" : commit;
    }

    private void describeApp(JSONObject meta) throws JSONException {
        JSONObject build = Runner.buildInfo(mContext);
        JSONObject app = new JSONObject();
        app.put("package", mContext.getPackageName());
        app.put("version_name", AppInfo.versionName(mContext));
        app.put("version_code", build.optInt("versionCode", -1));
        app.put("commit", build.optString("commit", ""));
        app.put("dirty", build.optBoolean("dirty", false));
        app.put("abi", build.optString("abi", ""));
        app.put("configuration", build.optString("configuration", ""));
        app.put("build_json", build.length() > 0);
        meta.put("app", app);
    }

    private void describeDevice(JSONObject meta) throws JSONException {
        JSONObject device = new JSONObject();
        device.put("manufacturer", Build.MANUFACTURER).put("brand", Build.BRAND).put("model", Build.MODEL)
                .put("device", Build.DEVICE).put("hardware", Build.HARDWARE).put("board", Build.BOARD)
                .put("release", Build.VERSION.RELEASE).put("sdk", Build.VERSION.SDK_INT)
                .put("security_patch", Build.VERSION.SECURITY_PATCH).put("fingerprint", Build.FINGERPRINT)
                .put("supported_abis", String.join(",", Build.SUPPORTED_ABIS));
        if (Build.VERSION.SDK_INT >= 31) {
            device.put("soc", Build.SOC_MANUFACTURER + " " + Build.SOC_MODEL);
        } else {
            device.put("soc", Build.HARDWARE);
        }
        device.put("page_size", Os.sysconf(OsConstants._SC_PAGESIZE));
        device.put("cpus", Runtime.getRuntime().availableProcessors());
        ActivityManager activities = mContext.getSystemService(ActivityManager.class);
        if (activities != null) {
            ActivityManager.MemoryInfo memory = new ActivityManager.MemoryInfo();
            activities.getMemoryInfo(memory);
            device.put("total_mem", memory.totalMem).put("avail_mem", memory.availMem)
                    .put("low_ram", activities.isLowRamDevice());
        }
        device.put("proc_version", readSmall("/proc/version"));
        device.put("mmap_min_addr", readSmall("/proc/sys/vm/mmap_min_addr"));
        device.put("uid", Process.myUid());
        meta.put("device", device);
    }

    private JSONObject describeReplay(ReplayFiles.Info info, String engineFile, ReplayFiles.Check check)
            throws JSONException {
        JSONObject replay = new JSONObject();
        if (info == null) {
            return replay.put("stem", mOptions.replayStem).put("missing", true);
        }
        replay.put("stem", info.stem()).put("file", info.fileName()).put("format", info.format())
                .put("sha256", info.sha256()).put("size", info.size()).put("engine_file", engineFile)
                .put("engine_file_sha256", engineFile.equals(info.engineFileName()) ? info.engineFileSha256()
                        : engineFile.equals(info.fileName()) ? info.sha256() : "")
                .put("header", ReplayFiles.publicHeader(info))
                .put("runner_info", info.runnerInfo() != null ? info.runnerInfo() : JSONObject.NULL)
                .put("runner_info_error", info.runnerInfoError()).put("conversion_error", info.conversionError())
                .put("map_folder", info.mapFolder());
        if (check != null) {
            replay.put("problems", new JSONArray(check.problems)).put("warnings", new JSONArray(check.warnings));
        }
        ReplayRefs refs = ReplayRefs.get(mContext);
        ReplayRefs.Ref ref = refs.find(info.sha256(), info.engineFileSha256());
        replay.put("reference", ref != null ? ref.json : JSONObject.NULL);
        replay.put("reference_table", refs.error() != null ? refs.error() : refs.size() + " entries");
        return replay;
    }

    // ---------------------------------------------------------------- files

    private static String readSmall(String path) {
        try {
            String text = FileOps.readText(new File(path), 64 * 1024);
            return text == null ? "" : text.trim();
        } catch (IOException | RuntimeException e) {
            return "unreadable: " + e.getMessage();
        }
    }

    private static String relative(DataRoot root, File file) throws IOException {
        Path base = root.dir().toPath().toAbsolutePath().normalize();
        Path path = file.toPath().toAbsolutePath().normalize();
        if (!path.startsWith(base)) {
            throw new IOException(file + " is outside the data root");
        }
        return base.relativize(path).toString().replace('\\', '/');
    }

    static void mkdirs(DataRoot root, String relative) throws IOException {
        File dir = root.prepare(relative);
        if (!dir.isDirectory() && !dir.mkdir() && !dir.isDirectory()) {
            throw new IOException("cannot create " + dir);
        }
    }

    /** Deletes a directory tree without following symbolic links out of it. */
    static void deleteTree(File dir) throws IOException {
        Path start = dir.toPath();
        if (!Files.exists(start, java.nio.file.LinkOption.NOFOLLOW_LINKS)) {
            return;
        }
        if (Files.isSymbolicLink(start) || !Files.isDirectory(start, java.nio.file.LinkOption.NOFOLLOW_LINKS)) {
            Files.delete(start);
            return;
        }
        Files.walkFileTree(start, new SimpleFileVisitor<Path>() {
            @Override
            public FileVisitResult visitFile(Path file, BasicFileAttributes attrs) throws IOException {
                Files.delete(file);
                return FileVisitResult.CONTINUE;
            }

            @Override
            public FileVisitResult postVisitDirectory(Path directory, IOException error) throws IOException {
                if (error != null) {
                    throw error;
                }
                Files.delete(directory);
                return FileVisitResult.CONTINUE;
            }
        });
    }

    /** The result.json of a run, or null. */
    static JSONObject readResult(DataRoot root, String runName) {
        if (runName == null || !runName.matches("[0-9]{8}-[0-9]{6}(-[0-9]+)?")) {
            return null;
        }
        try {
            String text = FileOps.readText(root.file(RUNS_DIR + "/" + runName + "/" + RESULT_JSON), 4 * 1024 * 1024);
            return text != null ? new JSONObject(text) : null;
        } catch (IOException | JSONException e) {
            return null;
        }
    }

    private static void putIfSet(JSONObject json, String key, String value) throws JSONException {
        if (value != null && !value.isEmpty()) {
            json.put(key, value);
        }
    }
}
