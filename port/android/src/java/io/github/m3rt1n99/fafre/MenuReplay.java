package io.github.m3rt1n99.fafre;

import android.content.Context;
import android.content.Intent;
import android.content.res.AssetFileDescriptor;
import android.content.res.AssetManager;
import android.os.Build;
import android.os.Process;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.text.SimpleDateFormat;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * The launcher's side of the menu replay (milestone M7a1, release 0.5.0): FAF's main menu with its opening
 * animation, as the engine's gal call stream recorded on the PC (a galtrace, port/graphics/trace), replayed by
 * the port's own Diligent backend over Vulkan in GameActivity, with the textures and effects of the user's own
 * game data. The trace ships in the APK ({@link #ASSET_TRACE}); it holds references (VFS path and content hash)
 * to game files instead of their bytes, plus what the engine generated itself.
 *
 * <p>What this class does, all off the UI thread: the checks before a start (the APK has a trace and the native
 * runtime, the data the trace refers to is there, neither the game nor the replay test is running), unpacking
 * the trace into the app's internal storage, the run directory {@code runs/<run>/} with a provisional result.json
 * (INTERRUPTED until GameActivity writes the real one, {@link MenuReplaySession}), and the Intent for
 * GameActivity with the extras of the native side's contract (port/android/src/GalPlay.h). After a run whose
 * process died it adds what is left to know to the run directory ({@link #closeInterrupted}).
 */
final class MenuReplay {
    // Intent extras of GameActivity's "menu-replay" mode (port/android/src/GalPlay.h, the Java contract).
    static final String EXTRA_MODE = "mode";
    static final String MODE = "menu-replay";
    static final String EXTRA_TRACE = "menuReplay.trace";
    static final String EXTRA_RUN_DIR = "menuReplay.runDir";
    static final String EXTRA_FAST = "menuReplay.fast";
    static final String EXTRA_FORCE_CPU_DECODE = "menuReplay.forceCpuDecode";
    static final String EXTRA_NO_SHADER_CACHE = "menuReplay.noShaderCache";

    /** result.json "kind" of a menu replay run (a replay test's has none). */
    static final String KIND = "menu-replay";
    static final String ASSET_TRACE = "galplay/menu.galtrace";
    /** Written by build_android.ps1 next to the trace: its size, sha256 and header metadata. */
    static final String ASSET_INFO = "galplay/menu.json";
    static final String LOG_NAME = "menureplay.log";
    /** Native side's files in the run directory (GalPlay.h). */
    static final String GALPLAY_JSON = "galplay.json";
    static final String GALPLAY_LOG = "galplay.log";
    static final String GALREPORT_JSON = "galreport.json";
    /** The engine-style log of the data-path script (the argv's /log), in the run directory. */
    static final String DATAPATH_LOG = "game.sclog";
    static final String VERDICT_PASS = MenuReplayVerdict.PASS;
    static final String VERDICT_FAIL = MenuReplayVerdict.FAIL;
    static final String VERDICT_INCOMPLETE = MenuReplayVerdict.INCOMPLETE;
    static final String VERDICT_NO_REFERENCE = MenuReplayVerdict.NO_REFERENCE;
    static final String VERDICT_INTERRUPTED = "INTERRUPTED";
    static final String VERDICT_CRASHED = "CRASHED";
    /** result.json key: the :game process that runs the replay (-1 until GameActivity has started). */
    static final String GAME_PID = "game_pid";
    static final String REQUESTED_MILLIS = "requested_millis";

    private static final String TRACE_DIR = "galplay";
    private static final long GAME_EXIT_WAIT_MS = 2000;
    /** GameActivity has this long to record its pid in the provisional result before the run counts as lost. */
    private static final long START_GRACE_MS = 60_000;
    private static final long SPACE_MARGIN = 64L * 1024 * 1024;
    private static final int LOG_TAIL = 256 * 1024;

    private static TraceAsset sAsset;

    private MenuReplay() {
    }

    /** What the user chose in the card. */
    static final class Options {
        boolean fast;
        boolean forceCpuDecode;
        boolean noShaderCache;

        JSONObject toJson() throws JSONException {
            return new JSONObject().put("fast", fast).put("force_cpu_decode", forceCpuDecode)
                    .put("no_shader_cache", noShaderCache);
        }

        static Options fromJson(JSONObject json) {
            Options options = new Options();
            if (json != null) {
                options.fast = json.optBoolean("fast");
                options.forceCpuDecode = json.optBoolean("force_cpu_decode");
                options.noShaderCache = json.optBoolean("no_shader_cache");
            }
            return options;
        }

        String describe() {
            StringBuilder out = new StringBuilder(fast ? "as fast as possible" : "recorded pace");
            if (forceCpuDecode) {
                out.append(", BC decoded on the CPU");
            }
            if (noShaderCache) {
                out.append(", no shader cache");
            }
            return out.toString();
        }
    }

    // ------------------------------------------------------------------ the trace in the APK

    /** The trace asset: whether the APK has one, its size and hash (menu.json) and its header. */
    static final class TraceAsset {
        final boolean present;
        final String problem;
        final long size;
        final String sha256;
        final String sourceName;
        final GalTrace header;
        /** The archives the trace's PayloadRef records name, from build_android.ps1's scan (menu.json). */
        final List<String> scannedArchives;

        TraceAsset(boolean present, String problem, long size, String sha256, String sourceName, GalTrace header,
                List<String> scannedArchives) {
            this.present = present;
            this.problem = problem;
            this.size = size;
            this.sha256 = sha256;
            this.sourceName = sourceName;
            this.header = header;
            this.scannedArchives = scannedArchives;
        }

        boolean usable() {
            return present && problem == null && header != null;
        }

        /** The game archives the trace refers to: its {@code ref_archives} metadata, else the build's scan. */
        List<String> archives() {
            List<String> named = header != null ? header.referencedArchives() : new ArrayList<String>();
            return !named.isEmpty() ? named : scannedArchives;
        }

        /** One line for the card. */
        String describe() {
            if (!present) {
                return "This APK has no menu trace (built without it).";
            }
            if (problem != null) {
                return "The menu trace in this APK is unusable: " + problem;
            }
            StringBuilder out = new StringBuilder("Trace ");
            out.append(sourceName.isEmpty() ? "menu.galtrace" : sourceName).append(" · v").append(header.version)
                    .append(" · ").append(FileOps.formatBytes(size));
            int presents = header.presents();
            if (presents > 0) {
                out.append(" · ").append(presents).append(" frames at ").append(header.framesPerSecond())
                        .append(" fps");
            }
            out.append("\nRead back: frames ").append(join(header.readbackFrames()));
            int references = header.vulkanReferences().size();
            out.append(" · PC Vulkan references: ").append(references == 0 ? "none" : references + "");
            List<String> archives = archives();
            if (!archives.isEmpty()) {
                out.append("\nUses your ").append(String.join(", ", archives));
            }
            return out.toString();
        }
    }

    /** Reads the asset's description once per process. Call off the UI thread. */
    static synchronized TraceAsset traceAsset(Context context) {
        if (sAsset == null) {
            sAsset = readTraceAsset(context.getApplicationContext().getAssets());
        }
        return sAsset;
    }

    private static TraceAsset readTraceAsset(AssetManager assets) {
        GalTrace header;
        try (InputStream in = new BufferedInputStream(assets.open(ASSET_TRACE), 64 * 1024)) {
            header = GalTrace.readHeader(in);
        } catch (FileNotFoundException e) {
            return new TraceAsset(false, null, 0, "", "", null, new ArrayList<String>());
        } catch (IOException e) {
            return new TraceAsset(true, describe(e), 0, "", "", null, new ArrayList<String>());
        }
        long size = -1;
        String sha256 = "";
        String sourceName = "";
        long scannedReferences = -1;
        List<String> scanned = new ArrayList<>();
        try (InputStream in = assets.open(ASSET_INFO)) {
            JSONObject info = new JSONObject(readAll(in, 4 * 1024 * 1024));
            size = info.optLong("size", -1);
            sha256 = info.optString("sha256", "").toLowerCase(Locale.ROOT);
            sourceName = info.optString("name", "");
            JSONObject payloads = info.optJSONObject("payloads");
            JSONObject references = payloads != null ? payloads.optJSONObject("references") : null;
            scannedReferences = references != null ? references.optLong("count", -1) : -1;
            JSONArray archives = info.optJSONArray("archives");
            for (int i = 0; archives != null && i < archives.length(); ++i) {
                String name = archives.optString(i, "").trim().toLowerCase(Locale.ROOT);
                if (!name.isEmpty() && !scanned.contains(name)) {
                    scanned.add(name);
                }
            }
        } catch (IOException | JSONException e) {
            // Without menu.json the size comes from the entry when it is stored; the hash stays unknown.
            try (AssetFileDescriptor fd = assets.openFd(ASSET_TRACE)) {
                size = fd.getLength();
            } catch (IOException compressed) {
                size = -1;
            }
        }
        String problem = null;
        // The header's payload_refs, else the build's scan of the PayloadRef records (menu.json).
        if (!header.declaresReferences() && !(header.version >= 2 && scannedReferences > 0)) {
            problem = "it is a format version " + header.version + " trace without game-file references";
        } else if (size <= 0) {
            problem = "its size is unknown (" + ASSET_INFO + " is missing)";
        }
        return new TraceAsset(true, problem, Math.max(size, 0), sha256, sourceName, header, scanned);
    }

    /**
     * The trace as a file the :game process can read: unpacked from the APK into the app's internal storage
     * (noBackupFilesDir/galplay/menu-<sha>.galtrace) once, checked against menu.json's size and sha256. Older
     * unpacked traces are deleted. Call off the UI thread.
     */
    static File unpackTrace(Context context, TraceAsset asset, Cancellation cancel, Progress progress)
            throws IOException {
        File dir = new File(context.getNoBackupFilesDir(), TRACE_DIR);
        if (!dir.isDirectory() && !dir.mkdirs() && !dir.isDirectory()) {
            throw new IOException("cannot create " + dir);
        }
        String tag = asset.sha256.length() >= 16 ? asset.sha256.substring(0, 16) : "size" + asset.size;
        File target = new File(dir, "menu-" + tag + ".galtrace");
        File stamp = new File(dir, target.getName() + ".ok");
        String stampText = asset.sha256 + " " + asset.size + "\n";
        String[] old = dir.list();
        for (String name : old != null ? old : new String[0]) {
            if (!name.equals(target.getName()) && !name.equals(stamp.getName())) {
                FileOps.deleteQuietly(new File(dir, name));
            }
        }
        if (target.isFile() && target.length() == asset.size && stampText.equals(FileOps.readText(stamp, 4096))) {
            return target;
        }
        FileOps.deleteQuietly(stamp);
        FileOps.deleteQuietly(target);
        long free = dir.getUsableSpace();
        if (free < asset.size + SPACE_MARGIN) {
            throw new IOException("Not enough internal storage for the menu trace: it needs "
                    + FileOps.formatBytes(asset.size + SPACE_MARGIN) + ", " + FileOps.formatBytes(free) + " is free.");
        }
        File part = FileOps.partFile(target);
        MessageDigest digest = FileOps.sha256();
        long copied = 0;
        progress.phase("Unpacking the menu trace", 0, asset.size);
        try (InputStream in = context.getAssets().open(ASSET_TRACE);
             FileOutputStream out = new FileOutputStream(part)) {
            byte[] buffer = new byte[FileOps.BUFFER_SIZE];
            int n;
            while ((n = in.read(buffer)) > 0) {
                cancel.throwIfCancelled();
                out.write(buffer, 0, n);
                digest.update(buffer, 0, n);
                copied += n;
                progress.bytes(copied);
            }
            out.getFD().sync();
        } catch (IOException | RuntimeException e) {
            FileOps.deleteQuietly(part);
            throw e;
        }
        String sha = FileOps.hex(digest.digest());
        if (copied != asset.size || (!asset.sha256.isEmpty() && !sha.equals(asset.sha256))) {
            FileOps.deleteQuietly(part);
            throw new IOException("The menu trace in this APK is damaged (" + copied + " bytes, sha256 " + sha
                    + "; expected " + asset.size + " bytes, " + (asset.sha256.isEmpty() ? "?" : asset.sha256)
                    + "). Install the APK again.");
        }
        FileOps.moveReplacing(part, target);
        FileOps.writeAtomic(stamp, stampText);
        return target;
    }

    // ------------------------------------------------------------------ the data check

    /** What the trace needs from the user's data, checked before a start. */
    static final class DataCheck {
        final List<String> problems = new ArrayList<>();
        final List<String> notes = new ArrayList<>();
        /** Archive -> where it was looked for and whether it is there, for meta.json. */
        final Map<String, String> archives = new java.util.TreeMap<>();

        boolean ok() {
            return problems.isEmpty();
        }
    }

    /**
     * The main menu's data (the required tier: init_faf.lua mounts it, and its archives hold the textures and
     * effects) and every archive the trace names ({@code ref_archives}): a FAF file of the manifest, or a file of
     * an SCFA selection. The native side checks every referenced file's hash as it reads it.
     */
    static DataCheck checkData(DataManifest manifest, DataRoot root, DataStatus status, TraceAsset asset)
            throws IOException {
        DataCheck check = new DataCheck();
        if (!status.requiredComplete()) {
            check.problems.add("Missing required game data: " + String.join(", ", status.missingRequired()) + ".");
        }
        if (asset == null || asset.header == null) {
            return check;
        }
        List<String> missing = new ArrayList<>();
        for (String archive : asset.archives()) {
            String where = null;
            boolean present = false;
            for (DataStatus.FafFileState faf : status.faf) {
                if (Names.equalsIgnoreCaseAscii(faf.file.name, archive)) {
                    where = "FAF " + faf.file.name;
                    present = faf.present;
                }
            }
            if (where == null) {
                for (DataManifest.Entry entry : manifest.scfaEntries) {
                    for (String name : entry.literalNames()) {
                        if (Names.equalsIgnoreCaseAscii(name, archive)) {
                            where = "SCFA " + entry.src + "/" + name + " (" + entry.label + ")";
                            File file = root.find(manifest.layout.scfa + "/" + entry.src + "/" + name);
                            present = file != null && file.isFile() && file.length() > 0;
                        }
                    }
                }
            }
            if (where == null) {
                check.notes.add("The trace names " + archive + ", which the data manifest does not list; the replay "
                        + "says so if it needs a file from it.");
                check.archives.put(archive, "unknown");
                continue;
            }
            check.archives.put(archive, (present ? "present: " : "missing: ") + where);
            if (!present) {
                missing.add(where);
            }
        }
        if (!missing.isEmpty()) {
            check.problems.add("The menu trace needs " + String.join(", ", missing) + ", which "
                    + (missing.size() == 1 ? "is" : "are") + " not in the data folder. Get "
                    + (missing.size() == 1 ? "it" : "them") + " below (Get game data).");
        }
        FafVersion installed = FafVersion.read(root.file(manifest.layout.fafVersion));
        int recorded = asset.header.fafVersion();
        if (installed != null && recorded > 0 && installed.version != recorded) {
            check.notes.add("The trace was recorded with FAF " + recorded + ", your files are FAF " + installed.version
                    + ": files that changed between the versions stop the replay with a hash mismatch.");
        }
        return check;
    }

    // ------------------------------------------------------------------ who uses the :game process

    /** What the :game process is doing, as far as the launcher can tell. */
    enum GameProcess {
        /** No :game process. */
        NONE,
        /** Alive, and its last start (game or menu replay) has ended: Android keeps it cached. */
        FINISHED,
        /** The game is loading or running. */
        GAME_RUNNING,
        /** A menu replay is loading or running (also paused in the background). */
        MENU_REPLAY_RUNNING
    }

    /**
     * Both the game and the menu replay run in the :game process and write launch/status.json; Settings says
     * which one started last. A process whose status is not terminal is busy.
     */
    static GameProcess gameProcess(Context context, DataRoot root, DataManifest manifest) {
        int pid = AppInfo.gameProcessPid(context);
        Settings settings = new Settings(context);
        boolean menu = Settings.LAUNCH_MENU_REPLAY.equals(settings.lastLaunchMode());
        JSONObject result = menu ? ReplayTest.readResult(root, settings.lastMenuRun()) : null;
        // Started a moment ago: the :game process may not be up yet, or not have recorded itself.
        if (starting(result)) {
            return GameProcess.MENU_REPLAY_RUNNING;
        }
        if (pid <= 0) {
            return GameProcess.NONE;
        }
        RunStatus status;
        try {
            status = RunStatus.read(root.file(manifest.layout.launch + "/status.json"));
        } catch (IOException e) {
            status = null;
        }
        if (menu) {
            // The run's own result tells too: still in progress and written by this very process.
            boolean inProgress = result != null && result.optBoolean(ReplayTest.IN_PROGRESS);
            int runPid = result != null ? result.optInt(GAME_PID, -1) : -1;
            if ((status != null && !status.isTerminal()) || (inProgress && runPid == pid)) {
                return GameProcess.MENU_REPLAY_RUNNING;
            }
            return GameProcess.FINISHED;
        }
        return status == null || !status.isTerminal() ? GameProcess.GAME_RUNNING : GameProcess.FINISHED;
    }

    /**
     * A menu replay run whose provisional result does not name its :game process yet, requested less than
     * {@link #START_GRACE_MS} ago: GameActivity is still starting (or never came up; after the grace time the
     * launcher closes the run as interrupted).
     */
    private static boolean starting(JSONObject result) {
        if (result == null || !KIND.equals(result.optString("kind")) || !result.optBoolean(ReplayTest.IN_PROGRESS)
                || result.optInt(GAME_PID, -1) > 0) {
            return false;
        }
        long age = System.currentTimeMillis() - result.optLong(REQUESTED_MILLIS, 0);
        return age >= 0 && age < START_GRACE_MS;
    }

    /** Ends a cached :game process whose run has finished; refuses (throws) when it is busy. */
    static void endFinishedGameProcess(Context context, DataRoot root, DataManifest manifest, LauncherLog log,
            String what) throws IOException {
        GameProcess state = gameProcess(context, root, manifest);
        if (state == GameProcess.GAME_RUNNING) {
            throw new IOException("Not started: the game is running. Close it first (Back in the game), then start "
                    + what + " again. If the game is not on screen, end the app in Android's settings (App info › "
                    + "Force stop) and open it again.");
        }
        if (state == GameProcess.MENU_REPLAY_RUNNING) {
            throw new IOException("Not started: the menu replay is running. Close it first (Back), then start " + what
                    + " again. If it is not on screen, end the app in Android's settings (App info › Force stop) and "
                    + "open it again.");
        }
        int pid = AppInfo.gameProcessPid(context);
        if (pid <= 0) {
            return;
        }
        log.log(what + ": ending the finished :game process " + pid);
        Process.killProcess(pid);
        long deadline = System.currentTimeMillis() + GAME_EXIT_WAIT_MS;
        while (AppInfo.gameProcessPid(context) > 0 && System.currentTimeMillis() < deadline) {
            try {
                Thread.sleep(50);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                break;
            }
        }
        if (AppInfo.gameProcessPid(context) > 0) {
            throw new IOException("The previous run's process is still there; try again in a moment.");
        }
    }

    // ------------------------------------------------------------------ start

    /** Everything GameActivity needs, ready on the UI thread. */
    static final class Launch {
        String run;
        File runDir;
        File trace;
        String[] argv;
        Options options;

        Intent intent(Context context) {
            return new Intent(context, GameActivity.class)
                    .putExtra(EXTRA_MODE, MODE)
                    .putExtra(LaunchArgs.EXTRA_ARGV, argv)
                    .putExtra(EXTRA_TRACE, trace.getAbsolutePath())
                    .putExtra(EXTRA_RUN_DIR, runDir.getAbsolutePath())
                    .putExtra(EXTRA_FAST, options.fast)
                    .putExtra(EXTRA_FORCE_CPU_DECODE, options.forceCpuDecode)
                    .putExtra(EXTRA_NO_SHADER_CACHE, options.noShaderCache);
        }
    }

    /**
     * The checks and the preparation of one menu replay, on a background thread. Throws with a message for the
     * user when it cannot start. The caller has checked on the UI thread that no import service job runs.
     */
    static Launch prepare(Context context, Options options, Cancellation cancel, Progress progress)
            throws IOException {
        Context app = context.getApplicationContext();
        LauncherLog log = LauncherLog.get(app);
        String problem = AppInfo.nativeRuntimeProblem(app);
        if (problem != null) {
            throw new IOException(problem);
        }
        TraceAsset asset = traceAsset(app);
        if (!asset.usable()) {
            throw new IOException(asset.describe());
        }
        DataManifest manifest = AppInfo.manifest(app);
        DataRoot root = AppInfo.dataRoot(app);
        progress.phase("Checking the game data", 0, -1);
        endFinishedGameProcess(app, root, manifest, log, "the menu replay");
        cancel.throwIfCancelled();
        DataStatus status = DataStatus.check(manifest, root, log);
        DataCheck check = checkData(manifest, root, status, asset);
        if (!check.ok()) {
            throw new IOException(String.join(" ", check.problems));
        }
        File init = root.find(manifest.initScript());
        if (init == null || !init.isFile()) {
            throw new IOException(manifest.initScript() + " is missing; get the FAF files first.");
        }
        // The data root as for Start: the same folders, and fa_path.lua with the real root (the replay test
        // rewrites it with its lower-case alias; the native VFS takes the real one).
        for (String dir : new String[] {manifest.layout.logs, manifest.layout.launch, manifest.layout.localAppData,
            manifest.layout.documents, manifest.layout.vault, ReplayTest.RUNS_DIR}) {
            ReplayTest.mkdirs(root, dir);
        }
        FafVersion installed = FafVersion.read(root.file(manifest.layout.fafVersion));
        int fafVersion = installed != null ? installed.version : manifest.fafVersion;
        File faPath = FaPathWriter.write(manifest, root, fafVersion, AppInfo.versionName(app));
        File statusFile = root.file(manifest.layout.launch + "/status.json");
        if (statusFile.exists() && !statusFile.delete()) {
            throw new IOException("cannot remove the previous " + statusFile.getName());
        }

        File trace = unpackTrace(app, asset, cancel, progress);
        cancel.throwIfCancelled();
        progress.phase("Starting", 0, -1);
        File runDir = ReplayTest.newRunDirectory(root);
        String run = runDir.getName();
        String started = Instant.now().toString();
        long requested = System.currentTimeMillis();
        try {
            JSONObject meta = new JSONObject().put("schema", 1).put("kind", KIND).put("run", run)
                    .put("started", started).put("options", options.toJson());
            ReplayTest.describeApp(app, meta);
            ReplayTest.describeDevice(app, meta);
            meta.put("trace", traceJson(asset, trace));
            meta.put("data", new JSONObject().put("faf_version", fafVersion).put("archives", new JSONObject(check.archives))
                    .put("notes", new JSONArray(check.notes)).put("fa_path_lua", FileOps.readText(faPath, 64 * 1024)));
            meta.put("paths", new JSONObject().put("data_root", root.path()).put("trace", trace.getAbsolutePath())
                    .put("run_dir", runDir.getAbsolutePath()).put("init", init.getAbsolutePath()));
            meta.put("vulkan_feature", vulkanFeature(app));
            meta.put(ReplayTest.IN_PROGRESS, true);
            FileOps.writeJson(new File(runDir, ReplayTest.META_JSON), meta.toString(1));
            writeProvisional(runDir, run, started, requested, options, asset.header, check.notes, app);
        } catch (JSONException e) {
            throw new IOException(e.getMessage(), e);
        }
        new Settings(app).startMenuReplay(run, requested);

        Launch launch = new Launch();
        launch.run = run;
        launch.runDir = runDir;
        launch.trace = trace;
        launch.options = options;
        launch.argv = LaunchArgs.build(init.getAbsolutePath().replace('\\', '/'),
                runDir.getAbsolutePath().replace('\\', '/') + "/" + DATAPATH_LOG, LaunchArgs.RENDERER_VULKAN, true, true,
                "");
        log.log("menu replay " + run + ": " + options.describe() + "; trace " + trace.getName() + " ("
                + FileOps.formatBytes(asset.size) + "); argv " + LaunchArgs.describe(launch.argv));
        return launch;
    }

    static JSONObject traceJson(TraceAsset asset, File unpacked) throws JSONException {
        JSONObject json = new JSONObject().put("asset", ASSET_TRACE).put("name", asset.sourceName)
                .put("size", asset.size).put("sha256", asset.sha256);
        if (unpacked != null) {
            json.put("file", unpacked.getAbsolutePath());
        }
        if (asset.header != null) {
            GalTrace header = asset.header;
            JSONObject metadata = new JSONObject();
            for (Map.Entry<String, String> entry : header.metadata.entrySet()) {
                // The recorder's command line names the PC's paths; it says nothing the phone needs.
                if (!entry.getKey().equals("command_line")) {
                    metadata.put(entry.getKey(), entry.getValue());
                }
            }
            json.put("version", header.version).put("metadata", metadata)
                    .put("readback_frames", new JSONArray(toList(header.readbackFrames())))
                    .put("reference_frames", new JSONObject(stringKeys(header.vulkanReferences())))
                    .put("ref_archives", new JSONArray(asset.archives()))
                    .put("frame_rate", header.framesPerSecond()).put("presents", header.presents());
        }
        return json;
    }

    private static Object vulkanFeature(Context context) {
        android.content.pm.PackageManager pm = context.getPackageManager();
        // 0x401000: Vulkan 1.1; 0x400003: 1.0.3, the oldest any device reports.
        return pm.hasSystemFeature(android.content.pm.PackageManager.FEATURE_VULKAN_HARDWARE_VERSION, 0x401000) ? "1.1+"
                : pm.hasSystemFeature(android.content.pm.PackageManager.FEATURE_VULKAN_HARDWARE_VERSION, 0x400003)
                ? "1.0" : "none reported";
    }

    /**
     * The result.json (and summary.txt) a run has until GameActivity writes the real one: INTERRUPTED, because
     * that is what stays when the :game process dies (a crash, Android's memory management) before the end.
     */
    static void writeProvisional(File runDir, String run, String started, long requested, Options options,
            GalTrace header, List<String> notes, Context context) throws IOException, JSONException {
        List<String[]> lines = new ArrayList<>();
        lines.add(new String[] {"bad", "The menu replay did not finish: GameActivity ended (closed by Android, or "
                + "crashed) before it wrote a result. Save run (zip) has what the run wrote until then."});
        for (String note : notes) {
            lines.add(new String[] {"warn", "Note: " + note});
        }
        JSONArray frames = new JSONArray();
        for (int frame : header.readbackFrames()) {
            String reference = header.vulkanReferences().get(frame);
            frames.put(new JSONObject().put("frame", frame).put("verdict", "not_reached")
                    .put("reference", reference != null ? reference : ""));
        }
        String headline = VERDICT_INTERRUPTED + " · the menu replay did not finish: the app was closed or crashed";
        JSONObject result = new JSONObject().put("schema", 1).put("kind", KIND).put("run", run)
                .put("started", started).put("ended", "").put(ReplayTest.IN_PROGRESS, true)
                .put(ReplayTest.APP_PID, Process.myPid()).put(GAME_PID, -1).put(REQUESTED_MILLIS, requested)
                .put("verdict", VERDICT_INTERRUPTED).put("ok", false).put("headline", headline)
                .put("options", options.toJson()).put("frames", frames).put("lines", linesJson(lines))
                .put("summary_text", summaryText(context, run, headline, lines));
        FileOps.writeJson(new File(runDir, ReplayTest.RESULT_JSON), result.toString(1));
        FileOps.writeAtomic(new File(runDir, ReplayTest.SUMMARY_TXT), result.getString("summary_text"));
    }

    /**
     * A prepared run that was not started after all (cancelled at the last moment, or the launcher was left
     * meanwhile): its provisional result becomes final, so the run is not taken for a crashed one.
     */
    static void markNotStarted(Context context, Launch launch, String reason) {
        try {
            File file = new File(launch.runDir, ReplayTest.RESULT_JSON);
            String text = FileOps.readText(file, 4 * 1024 * 1024);
            JSONObject result = text != null ? new JSONObject(text) : new JSONObject().put("schema", 1)
                    .put("kind", KIND).put("run", launch.run);
            List<String[]> lines = new ArrayList<>();
            lines.add(new String[] {"warn", reason});
            String headline = "NOT STARTED · " + reason;
            result.put(ReplayTest.IN_PROGRESS, false).put("ended", Instant.now().toString())
                    .put("verdict", ReplayTest.VERDICT_NOT_RUN).put("ok", false).put("headline", headline)
                    .put("lines", linesJson(lines)).put("summary_text", summaryText(context, launch.run, headline, lines));
            FileOps.writeJson(file, result.toString(1));
            FileOps.writeAtomic(new File(launch.runDir, ReplayTest.SUMMARY_TXT), result.getString("summary_text"));
        } catch (IOException | JSONException e) {
            LauncherLog.get(context).log("menu replay " + launch.run + ": cannot record that it did not start: "
                    + e.getMessage());
        }
    }

    /**
     * A menu replay whose result is still the provisional one while its :game process is gone: the process died
     * (a crash, or Android ended it) before GameActivity could write the result. Adds the tail of the runtime log
     * and the app's logcat since the start to the run directory, says CRASHED when they show a native crash, and
     * marks the result final. Returns true when it changed the run. Call off the UI thread.
     */
    static boolean closeInterrupted(Context context, DataRoot root, String run) {
        JSONObject result = ReplayTest.readResult(root, run);
        if (result == null || !KIND.equals(result.optString("kind")) || !result.optBoolean(ReplayTest.IN_PROGRESS)) {
            return false;
        }
        int pid = AppInfo.gameProcessPid(context);
        int runPid = result.optInt(GAME_PID, -1);
        long requested = result.optLong(REQUESTED_MILLIS, 0);
        if (starting(result) || (pid > 0 && runPid == pid)) {
            return false; // still starting (the :game process may not be up yet), or running
        }
        LauncherLog log = LauncherLog.get(context);
        try {
            File runDir = root.file(ReplayTest.RUNS_DIR + "/" + run);
            File runtimeLog = root.find(LauncherLog.LOGS_DIR + "/faf_android_vulkan.log");
            String tail = "";
            if (runtimeLog != null && runtimeLog.lastModified() >= requested - 5000) {
                tail = FileOps.tail(runtimeLog, LOG_TAIL);
                FileOps.writeAtomic(new File(runDir, "faf_android_vulkan.log"), tail);
            }
            File logcat = new File(runDir, "logcat.txt");
            captureLogcat(logcat, requested);
            String logcatText = FileOps.tail(logcat, 2 * 1024 * 1024);
            String crash = crashLine(tail + "\n" + logcatText);
            List<String[]> lines = new ArrayList<>();
            JSONArray old = result.optJSONArray("lines");
            String verdict = crash != null ? VERDICT_CRASHED : VERDICT_INTERRUPTED;
            String headline = crash != null ? VERDICT_CRASHED + " · the menu replay's process crashed: " + crash
                    : VERDICT_INTERRUPTED + " · the menu replay's process ended before the replay finished";
            lines.add(new String[] {"bad", crash != null ? "The native replay crashed (" + crash + "). The run has the "
                    + "runtime log's tail (faf_android_vulkan.log) and the app's logcat (logcat.txt) with the backtrace."
                    : "The :game process ended without a result (Android ended it, or it crashed without a trace in "
                    + "the logs). The run has the runtime log's tail and the app's logcat."});
            for (int i = 1; old != null && i < old.length(); ++i) {
                JSONObject line = old.optJSONObject(i);
                if (line != null) {
                    lines.add(new String[] {line.optString("tone"), line.optString("text")});
                }
            }
            result.put(ReplayTest.IN_PROGRESS, false).put("ended", Instant.now().toString()).put("verdict", verdict)
                    .put("headline", headline).put("post_mortem", true).put("lines", linesJson(lines))
                    .put("summary_text", summaryText(context, run, headline, lines));
            FileOps.writeJson(new File(runDir, ReplayTest.RESULT_JSON), result.toString(1));
            FileOps.writeAtomic(new File(runDir, ReplayTest.SUMMARY_TXT), result.getString("summary_text"));
            appendLog(root, result.getString("summary_text"));
            log.log("menu replay " + run + ": " + headline);
            return true;
        } catch (IOException | JSONException e) {
            log.log("menu replay " + run + ": cannot close the interrupted run: " + e.getMessage());
            return false;
        }
    }

    /** "SIGSEGV ... in libfaf_android.so ..." from a runtime log or logcat, or null. */
    static String crashLine(String text) {
        String[] lines = text.split("\n");
        for (String line : lines) {
            int at = line.indexOf("Fatal signal");
            if (at >= 0) {
                return line.substring(at).trim();
            }
        }
        for (String line : lines) {
            if (line.contains("CRASH") || line.contains("crash handler") || line.contains("SIGSEGV")
                    || line.contains("SIGABRT")) {
                String trimmed = line.trim();
                return trimmed.length() > 160 ? trimmed.substring(0, 160) + "…" : trimmed;
            }
        }
        return null;
    }

    /** The app's own log buffers since {@code sinceMillis} (an app sees only its uid's lines). */
    static void captureLogcat(File target, long sinceMillis) {
        String since = new SimpleDateFormat("MM-dd HH:mm:ss.SSS", Locale.ROOT).format(new Date(sinceMillis - 2000));
        List<String> argv = new ArrayList<>(Arrays.asList("/system/bin/logcat", "-d", "-v", "threadtime", "-b",
                "main,system,crash", "-T", since));
        try {
            Cancellation own = new Cancellation();
            RunnerProcess.Outcome outcome = RunnerProcess.run(argv, new HashMap<String, String>(), target.getParentFile(),
                    target, 20_000, own, null);
            if (outcome.exitCode != 0) {
                argv.subList(argv.size() - 2, argv.size()).clear();
                RunnerProcess.run(argv, new HashMap<String, String>(), target.getParentFile(), target, 20_000, own,
                        null);
            }
        } catch (IOException e) {
            try {
                FileOps.writeAtomic(target, "logcat failed: " + e.getMessage() + "\n");
            } catch (IOException ignored) {
                // nothing more to save
            }
        }
    }

    // ------------------------------------------------------------------ shared by the card and the session

    static JSONArray linesJson(List<String[]> lines) throws JSONException {
        JSONArray out = new JSONArray();
        for (String[] line : lines) {
            out.put(new JSONObject().put("tone", line[0]).put("text", line[1]));
        }
        return out;
    }

    /** The text "Copy summary" puts on the clipboard. */
    static String summaryText(Context context, String run, String headline, List<String[]> lines) {
        StringBuilder out = new StringBuilder("faf-re menu replay ").append(run).append(": ").append(headline)
                .append('\n');
        JSONObject build = Runner.buildInfo(context);
        out.append("App ").append(AppInfo.versionName(context)).append(" (").append(build.optInt("versionCode", -1))
                .append(", ").append(shortCommit(build.optString("commit", ""))).append(build.optBoolean("dirty") ? "+" : "")
                .append(") ").append(build.optString("abi", "")).append(" · ").append(Build.MANUFACTURER).append(' ')
                .append(Build.MODEL).append(" · Android ").append(Build.VERSION.RELEASE).append('\n');
        for (String[] line : lines) {
            out.append(line[1]).append('\n');
        }
        return out.toString();
    }

    static void appendLog(DataRoot root, String summary) {
        try {
            File log = root.prepare(LauncherLog.LOGS_DIR + "/" + LOG_NAME);
            try (FileOutputStream out = new FileOutputStream(log, true)) {
                out.write((summary + "\n").getBytes(StandardCharsets.UTF_8));
            }
        } catch (IOException ignored) {
            // the run directory has the same text
        }
    }

    /** The PNGs a menu replay run's result.json names, checked to be plain names. */
    static List<String> frameImages(JSONObject run) {
        List<String> out = new ArrayList<>();
        JSONArray frames = run != null ? run.optJSONArray("frames") : null;
        for (int i = 0; frames != null && i < frames.length(); ++i) {
            JSONObject frame = frames.optJSONObject(i);
            String name = frame != null ? frame.optString("png", "") : "";
            if (name.matches("frame_[0-9]{1,9}\\.png") && !out.contains(name)) {
                out.add(name);
            }
        }
        return out;
    }

    static String shortCommit(String commit) {
        return commit.length() > 10 ? commit.substring(0, 10) : commit.isEmpty() ? "unknown commit" : commit;
    }

    static String join(int[] values) {
        StringBuilder out = new StringBuilder();
        for (int value : values) {
            if (out.length() > 0) {
                out.append(", ");
            }
            out.append(value);
        }
        return out.toString();
    }

    private static List<Integer> toList(int[] values) {
        List<Integer> out = new ArrayList<>();
        for (int value : values) {
            out.add(value);
        }
        return out;
    }

    private static Map<String, String> stringKeys(Map<Integer, String> map) {
        Map<String, String> out = new java.util.LinkedHashMap<>();
        for (Map.Entry<Integer, String> entry : map.entrySet()) {
            out.put(Integer.toString(entry.getKey()), entry.getValue());
        }
        return out;
    }

    private static String readAll(InputStream in, int max) throws IOException {
        java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
        byte[] buffer = new byte[8192];
        int n;
        while ((n = in.read(buffer)) > 0) {
            out.write(buffer, 0, n);
            if (out.size() > max) {
                throw new IOException("larger than " + max + " bytes");
            }
        }
        return new String(out.toByteArray(), StandardCharsets.UTF_8);
    }

    static String describe(Exception error) {
        return error.getMessage() != null ? error.getMessage() : error.toString();
    }
}
