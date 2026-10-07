package io.github.m3rt1n99.fafre;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.graphics.drawable.Icon;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;
import android.os.SystemClock;
import android.os.storage.StorageManager;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;

/**
 * Foreground service (type dataSync) that gets game data onto the device:
 * downloads the FAF files, imports a picked SCFA / FAF / vault folder, or
 * verifies the FAF checksums. A multi-GB copy outlives the activity (rotation,
 * the user switching apps, the screen going off), so the work lives here with
 * a progress notification, a cancel action and a partial wake lock.
 *
 * <p>It also runs the replay test ({@link ReplayTest}, ACTION_REPLAY_TEST:
 * local file processing): the headless runner is a child process of this
 * process, and the foreground service plus the wake lock are what keep both
 * alive and running with the screen off. The runner never outlives the job:
 * cancel, onTimeout and onDestroy cancel the job, which destroys the process
 * ({@link RunnerProcess}).
 *
 * <p>One job at a time, so an import never rewrites data under a running test.
 * The launcher runs in the same process and observes the job through
 * {@link #state()} and {@link Listener}; the last result is also kept in
 * {@link Settings} for after a restart. The job bodies are the pure Java
 * {@link FafInstaller} and {@link TreeImporter}, and {@link ReplayTest}.
 */
public final class ImportService extends Service {
    private static final String PREFIX = "io.github.m3rt1n99.fafre.";
    static final String ACTION_DOWNLOAD_FAF = PREFIX + "action.DOWNLOAD_FAF";
    static final String ACTION_IMPORT_SCFA = PREFIX + "action.IMPORT_SCFA";
    static final String ACTION_IMPORT_FAF = PREFIX + "action.IMPORT_FAF";
    static final String ACTION_IMPORT_VAULT = PREFIX + "action.IMPORT_VAULT";
    static final String ACTION_VERIFY_FAF = PREFIX + "action.VERIFY_FAF";
    static final String ACTION_REPLAY_TEST = PREFIX + "action.REPLAY_TEST";
    static final String ACTION_CANCEL = PREFIX + "action.CANCEL";
    static final String EXTRA_RECOMMENDED = PREFIX + "extra.RECOMMENDED";
    static final String EXTRA_OPTIONAL = PREFIX + "extra.OPTIONAL";

    private static final String CHANNEL_PROGRESS = "import_progress";
    private static final String CHANNEL_RESULT = "import_result";
    private static final String CHANNEL_REPLAY = "replay_test";

    /** What kind of job State describes: the launcher shows each kind in its own card. */
    static final String KIND_IMPORT = "import";
    static final String KIND_REPLAY = "replay";
    private static final int NOTIFICATION_PROGRESS = 1;
    private static final int NOTIFICATION_RESULT = 2;
    private static final long TICK_MS = 400;
    private static final long NOTIFICATION_INTERVAL_MS = 1000;
    /** Longer than any sane job; only a backstop if the worker hangs. */
    private static final long WAKE_LOCK_TIMEOUT_MS = 8L * 60 * 60 * 1000;
    /** Headroom left on the volume so the device stays usable after a big import. */
    private static final long FREE_SPACE_MARGIN = 256L * 1024 * 1024;

    /** What the launcher shows. Immutable; replaced on the main thread. */
    static final class State {
        static final State IDLE = new State(KIND_IMPORT, false, null, null, false, null, false);

        final String kind;
        final boolean running;
        final String title;
        final Progress.Snapshot progress;
        final boolean cancelling;
        /** Outcome of the job that just ended in this process; null if none did. */
        final String result;
        final boolean resultOk;

        State(String kind, boolean running, String title, Progress.Snapshot progress, boolean cancelling,
                String result, boolean resultOk) {
            this.kind = kind;
            this.running = running;
            this.title = title;
            this.progress = progress;
            this.cancelling = cancelling;
            this.result = result;
            this.resultOk = resultOk;
        }
    }

    interface Listener {
        void onImportState(State state);
    }

    private interface Job {
        /** Returns the success message; throws with a user-readable message on failure. */
        String run(Cancellation cancel, Progress progress) throws IOException;
    }

    private static final List<Listener> sListeners = new ArrayList<>();
    private static State sState = State.IDLE;
    private static ImportService sInstance;

    private final Handler mMain = new Handler(Looper.getMainLooper());
    private NotificationManager mNotifications;
    private PowerManager.WakeLock mWakeLock;
    private LauncherLog mLog;
    private Thread mWorker;
    private String mTitle;
    private String mKind = KIND_IMPORT;
    private Cancellation mCancel;
    private Progress mProgress;
    private long mLastNotification;
    private boolean mDestroyed;

    private final Runnable mTicker = new Runnable() {
        @Override
        public void run() {
            if (mWorker == null) {
                return;
            }
            Progress.Snapshot snapshot = mProgress.snapshot();
            publish(new State(mKind, true, mTitle, snapshot, mCancel.isCancelled(), null, false));
            long now = SystemClock.elapsedRealtime();
            if (now - mLastNotification >= NOTIFICATION_INTERVAL_MS && !mDestroyed) {
                mLastNotification = now;
                mNotifications.notify(NOTIFICATION_PROGRESS, progressNotification(mKind, mTitle, snapshot));
            }
            mMain.postDelayed(this, TICK_MS);
        }
    };

    // ------------------------------------------------------------ public API

    /** Current job state. Main thread only. */
    static State state() {
        return sState;
    }

    static void addListener(Listener listener) {
        sListeners.add(listener);
    }

    static void removeListener(Listener listener) {
        sListeners.remove(listener);
    }

    static Intent downloadFaf(Context context, boolean recommended) {
        return new Intent(context, ImportService.class).setAction(ACTION_DOWNLOAD_FAF)
                .putExtra(EXTRA_RECOMMENDED, recommended);
    }

    static Intent importScfa(Context context, Uri tree, boolean recommended, Set<String> optional) {
        return new Intent(context, ImportService.class).setAction(ACTION_IMPORT_SCFA).setData(tree)
                .putExtra(EXTRA_RECOMMENDED, recommended)
                .putExtra(EXTRA_OPTIONAL, optional.toArray(new String[0]));
    }

    static Intent importFaf(Context context, Uri tree, boolean recommended) {
        return new Intent(context, ImportService.class).setAction(ACTION_IMPORT_FAF).setData(tree)
                .putExtra(EXTRA_RECOMMENDED, recommended);
    }

    static Intent importVault(Context context, Uri tree) {
        return new Intent(context, ImportService.class).setAction(ACTION_IMPORT_VAULT).setData(tree);
    }

    static Intent verifyFaf(Context context) {
        return new Intent(context, ImportService.class).setAction(ACTION_VERIFY_FAF);
    }

    static Intent replayTest(Context context, ReplayTest.Options options) {
        Intent intent = new Intent(context, ImportService.class).setAction(ACTION_REPLAY_TEST);
        options.toIntent(intent);
        return intent;
    }

    /** Cancels the running job, if any. Main thread only. */
    static void cancelRunning(String reason) {
        if (sInstance != null) {
            sInstance.cancel(reason);
        }
    }

    // ------------------------------------------------------------- lifecycle

    @Override
    public void onCreate() {
        super.onCreate();
        sInstance = this;
        mLog = LauncherLog.get(this);
        mNotifications = getSystemService(NotificationManager.class);
        NotificationChannel progress = new NotificationChannel(CHANNEL_PROGRESS, "Game data transfers",
                NotificationManager.IMPORTANCE_LOW);
        progress.setDescription("Progress of downloads and imports of game data");
        NotificationChannel result = new NotificationChannel(CHANNEL_RESULT, "Game data results",
                NotificationManager.IMPORTANCE_DEFAULT);
        result.setDescription("Whether a download or import finished or failed");
        NotificationChannel replay = new NotificationChannel(CHANNEL_REPLAY, "Replay test",
                NotificationManager.IMPORTANCE_LOW);
        replay.setDescription("Progress of the replay test");
        mNotifications.createNotificationChannels(Arrays.asList(progress, result, replay));
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent != null ? intent.getAction() : null;
        if (ACTION_CANCEL.equals(action)) {
            if (mWorker != null) {
                cancel("Cancelled");
            } else {
                stopSelf(startId);
            }
            return START_NOT_STICKY;
        }

        Job job = intent != null ? createJob(intent) : null;
        String title = titleFor(action);
        String kind = kindFor(action);
        // Every startForegroundService() must be answered with startForeground(),
        // also the ones we turn down, or the system kills the process.
        if (!goForeground(mWorker != null ? progressNotification(mKind, mTitle, mProgress.snapshot())
                : progressNotification(kind, title, null), kind)) {
            if (mWorker == null) {
                stopSelf(startId);
            }
            return START_NOT_STICKY;
        }
        if (mWorker != null) {
            mLog.log("import: '" + title + "' ignored, '" + mTitle + "' is still running");
            if (KIND_REPLAY.equals(kind)) {
                new Settings(this).setLastReplayJob("Not started: '" + mTitle + "' is still running.", false);
            }
            return START_NOT_STICKY;
        }
        if (job == null) {
            mLog.log("import: unknown request " + action);
            stopForeground(STOP_FOREGROUND_REMOVE);
            stopSelf(startId);
            return START_NOT_STICKY;
        }
        startJob(kind, title, job);
        return START_NOT_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    /**
     * Android 15+: dataSync services get 6 hours per day. The system expects
     * us to stop within seconds, so stop now and let the worker wind down; the
     * .part files and the skip-if-present logic make the next run continue.
     */
    @Override
    public void onTimeout(int startId, int fgsType) {
        mLog.log("import: Android's time limit for data transfers was reached");
        cancel(KIND_REPLAY.equals(mKind)
                ? "Stopped by Android's daily limit for background work; open the app and start it again"
                : "Stopped by Android's daily limit for background transfers; start it again later to continue");
        stopForeground(STOP_FOREGROUND_REMOVE);
        stopSelf();
    }

    @Override
    public void onDestroy() {
        mDestroyed = true;
        if (mWorker != null) {
            cancel("Stopped by the system");
        }
        mMain.removeCallbacks(mTicker);
        releaseWakeLock();
        if (sInstance == this) {
            sInstance = null;
        }
        super.onDestroy();
    }

    // ------------------------------------------------------------------ jobs

    private static String titleFor(String action) {
        if (ACTION_DOWNLOAD_FAF.equals(action)) {
            return "Downloading FAF files";
        } else if (ACTION_IMPORT_SCFA.equals(action)) {
            return "Importing SCFA files";
        } else if (ACTION_IMPORT_FAF.equals(action)) {
            return "Importing FAF files";
        } else if (ACTION_IMPORT_VAULT.equals(action)) {
            return "Importing vault maps and mods";
        } else if (ACTION_VERIFY_FAF.equals(action)) {
            return "Verifying FAF files";
        } else if (ACTION_REPLAY_TEST.equals(action)) {
            return "Replay test";
        }
        return "Game data";
    }

    private static String kindFor(String action) {
        return ACTION_REPLAY_TEST.equals(action) ? KIND_REPLAY : KIND_IMPORT;
    }

    private Job createJob(Intent intent) {
        final String action = intent.getAction();
        final boolean recommended = intent.getBooleanExtra(EXTRA_RECOMMENDED, true);
        final Uri tree = intent.getData();
        if (ACTION_DOWNLOAD_FAF.equals(action)) {
            return (cancel, progress) -> downloadFaf(recommended, cancel, progress);
        } else if (ACTION_REPLAY_TEST.equals(action)) {
            final ReplayTest.Options options = ReplayTest.Options.fromIntent(intent);
            return (cancel, progress) -> new ReplayTest(this, mLog, options).run(cancel, progress);
        } else if (ACTION_VERIFY_FAF.equals(action)) {
            return this::verifyFaf;
        } else if (tree == null) {
            return null;
        } else if (ACTION_IMPORT_SCFA.equals(action)) {
            String[] ids = intent.getStringArrayExtra(EXTRA_OPTIONAL);
            final Set<String> optional = new HashSet<>(Arrays.asList(ids != null ? ids : new String[0]));
            return (cancel, progress) -> importScfa(tree, recommended, optional, cancel, progress);
        } else if (ACTION_IMPORT_FAF.equals(action)) {
            return (cancel, progress) -> importFaf(tree, recommended, cancel, progress);
        } else if (ACTION_IMPORT_VAULT.equals(action)) {
            return (cancel, progress) -> importVault(tree, cancel, progress);
        }
        return null;
    }

    private String downloadFaf(boolean recommended, Cancellation cancel, Progress progress) throws IOException {
        DataManifest manifest = AppInfo.manifest(this);
        DataRoot root = AppInfo.dataRoot(this);
        List<DataManifest.FafFile> files = FafInstaller.select(manifest, recommended);
        long needed = 0;
        for (DataManifest.FafFile file : files) {
            File existing = root.find(file.dest);
            if (existing == null || !existing.isFile() || existing.length() != file.size) {
                // A .part from an earlier attempt is resumed, not downloaded again.
                File part = root.find(file.dest + FileOps.PART_SUFFIX);
                needed += file.size - (part != null && part.isFile() ? Math.min(part.length(), file.size) : 0);
            }
        }
        ensureSpace(root, needed);
        mLog.log("download: FAF " + manifest.fafVersion + ", " + files.size() + " files from " + manifest.baseUrl);
        FafInstaller installer = new FafInstaller(root, mLog, progress, cancel);
        FafInstaller.Result result = installer.download(files, manifest.baseUrl, manifest.fafVersion,
                AppInfo.userAgent(this));
        mLog.log("download: " + result.summary());
        if (!result.ok()) {
            throw new IOException("Download incomplete: " + result.summary());
        }
        FafVersion.write(root.prepare(manifest.layout.fafVersion), manifest.featuredMod, manifest.fafVersion,
                FafVersion.SOURCE_DOWNLOAD);
        return "FAF " + manifest.fafVersion + " files ready (" + result.summary() + ")";
    }

    private String verifyFaf(Cancellation cancel, Progress progress) throws IOException {
        DataManifest manifest = AppInfo.manifest(this);
        DataRoot root = AppInfo.dataRoot(this);
        FafInstaller.Result result = new FafInstaller(root, mLog, progress, cancel).verify(manifest.fafFiles);
        if (!result.failed.isEmpty()) {
            throw new IOException("Damaged FAF files: " + String.join("; ", result.failed)
                    + ". Download the FAF files again to replace them.");
        }
        String message = result.present + " FAF files match their checksums";
        if (!result.missing.isEmpty()) {
            message += "; not present: " + String.join(", ", result.missing);
        }
        return message;
    }

    private String importScfa(Uri tree, boolean recommended, Set<String> optional, Cancellation cancel,
            Progress progress) throws IOException {
        DataManifest manifest = AppInfo.manifest(this);
        DataRoot root = AppInfo.dataRoot(this);
        SafTree source = new SafTree(getContentResolver(), tree);
        TreeImporter importer = new TreeImporter(source, root, mLog, progress, cancel);
        progress.phase("Looking for the game files", 0, -1);
        SourceTree.Node base = importer.findBase(manifest.scfaMarker, false);
        if (base == null) {
            throw new IOException("This folder does not contain " + manifest.scfaMarker + ". Pick the Supreme "
                    + "Commander Forged Alliance folder itself (the one with gamedata, fonts and sounds).");
        }
        List<DataManifest.Entry> entries = new ArrayList<>();
        for (DataManifest.Entry entry : manifest.scfaEntries) {
            if (entry.tier == DataManifest.Tier.REQUIRED
                    || (entry.tier == DataManifest.Tier.RECOMMENDED && recommended)
                    || (entry.tier == DataManifest.Tier.OPTIONAL && optional.contains(entry.id))) {
                entries.add(entry);
            }
        }
        mLog.log("import scfa: from " + source.describe() + " / " + base.name + ", " + entries.size() + " entries");
        return copyTree(importer, base, entries, manifest.layout.scfa, manifest, root, progress);
    }

    private String importVault(Uri tree, Cancellation cancel, Progress progress) throws IOException {
        DataManifest manifest = AppInfo.manifest(this);
        DataRoot root = AppInfo.dataRoot(this);
        SafTree source = new SafTree(getContentResolver(), tree);
        TreeImporter importer = new TreeImporter(source, root, mLog, progress, cancel);
        progress.phase("Looking for maps and mods", 0, -1);
        SourceTree.Node base = null;
        for (DataManifest.Entry entry : manifest.userEntries) {
            base = importer.findBase(entry.src, true);
            if (base != null) {
                break;
            }
        }
        if (base == null) {
            throw new IOException("This folder has no maps or mods folder. Pick the FAF vault folder (on the PC: "
                    + "Documents/My Games/Gas Powered Games/Supreme Commander Forged Alliance).");
        }
        mLog.log("import vault: from " + source.describe() + " / " + base.name);
        return copyTree(importer, base, manifest.userEntries, manifest.layout.vault, manifest, root, progress);
    }

    private String copyTree(TreeImporter importer, SourceTree.Node base, List<DataManifest.Entry> entries,
            String prefix, DataManifest manifest, DataRoot root, Progress progress) throws IOException {
        progress.phase("Listing files", 0, -1);
        TreeImporter.Plan plan = importer.plan(base, entries, prefix);
        for (String warning : plan.warnings) {
            mLog.log("import: " + warning);
        }
        ensureSpace(root, importer.bytesToCopy(plan));
        File recordFile = root.prepare(manifest.layout.deployRecord);
        DeployRecord record = DeployRecord.read(recordFile, mLog);
        TreeImporter.Result result;
        try {
            result = importer.copy(plan, record);
        } finally {
            // Also after a cancel or failure: what was copied stays usable.
            try {
                record.write(recordFile);
            } catch (IOException e) {
                mLog.log("import: could not update deployed.json: " + e.getMessage());
            }
        }
        for (String warning : result.warnings) {
            mLog.log("import: " + warning);
        }
        int warnings = plan.warnings.size() + result.warnings.size();
        String message = "Copied " + result.copied + " files (" + FileOps.formatBytes(result.copiedBytes) + "), "
                + result.skipped + " already present";
        if (warnings > 0) {
            message += "; " + warnings + " warnings, first: " + (plan.warnings.isEmpty() ? result.warnings.get(0)
                    : plan.warnings.get(0)) + " (all in launcher.log)";
        }
        return message;
    }

    private String importFaf(Uri tree, boolean recommended, Cancellation cancel, Progress progress)
            throws IOException {
        DataManifest manifest = AppInfo.manifest(this);
        DataRoot root = AppInfo.dataRoot(this);
        SafTree source = new SafTree(getContentResolver(), tree);
        TreeImporter resolver = new TreeImporter(source, root, mLog, progress, cancel);
        progress.phase("Looking for the FAF files", 0, -1);
        SourceTree.Node base = resolver.findBase(manifest.fafMarker(), false);
        if (base == null) {
            throw new IOException("This folder does not contain " + manifest.fafMarker() + ". Pick a copy of the "
                    + "FAF client's data folder (C:/ProgramData/FAForever on the PC).");
        }
        List<DataManifest.FafFile> files = FafInstaller.select(manifest, recommended);
        long needed = 0;
        for (DataManifest.FafFile file : files) {
            File existing = root.find(file.dest);
            if (existing == null || !existing.isFile() || existing.length() != file.size) {
                needed += file.size;
            }
        }
        ensureSpace(root, needed);
        mLog.log("import faf: from " + source.describe() + " / " + base.name);
        FafInstaller.Result result = new FafInstaller(root, mLog, progress, cancel).importFrom(source, base, resolver,
                files, manifest.fafVersion);
        mLog.log("import faf: " + result.summary());
        if (!result.ok()) {
            throw new IOException("Import incomplete: " + result.summary());
        }
        FafVersion.write(root.prepare(manifest.layout.fafVersion), manifest.featuredMod, manifest.fafVersion,
                FafVersion.SOURCE_IMPORT);
        return "FAF " + manifest.fafVersion + " files ready (" + result.summary() + ")";
    }

    /** Fails early instead of filling the device and dying halfway through a multi-GB copy. */
    private void ensureSpace(DataRoot root, long needed) throws IOException {
        if (needed <= 0) {
            return;
        }
        long available;
        try {
            StorageManager storage = getSystemService(StorageManager.class);
            UUID volume = storage.getUuidForPath(root.dir());
            available = storage.getAllocatableBytes(volume);
        } catch (IOException | RuntimeException e) {
            available = root.dir().getUsableSpace();
        }
        if (needed + FREE_SPACE_MARGIN > available) {
            throw new IOException("Not enough free space: needs " + FileOps.formatBytes(needed) + " plus "
                    + FileOps.formatBytes(FREE_SPACE_MARGIN) + " headroom, " + FileOps.formatBytes(available)
                    + " available.");
        }
    }

    // ---------------------------------------------------------------- worker

    private void startJob(final String kind, final String title, final Job job) {
        mKind = kind;
        mTitle = title;
        final Cancellation cancel = new Cancellation();
        final Progress progress = new Progress();
        mCancel = cancel;
        mProgress = progress;
        acquireWakeLock();
        mLog.log("job start: " + title);
        mWorker = new Thread(() -> runJob(title, job, cancel, progress), "fafre-import");
        mWorker.start();
        publish(new State(kind, true, title, progress.snapshot(), false, null, false));
        mMain.postDelayed(mTicker, TICK_MS);
    }

    private void runJob(final String title, Job job, Cancellation cancel, Progress progress) {
        String message;
        boolean ok = false;
        try {
            message = job.run(cancel, progress);
            ok = true;
        } catch (Cancellation.CancelledException e) {
            message = cancel.reason() != null ? cancel.reason() : "Cancelled";
        } catch (IOException | RuntimeException e) {
            message = e.getMessage() != null ? e.getMessage() : e.toString();
            if (e instanceof RuntimeException) {
                mLog.log("job crashed: " + android.util.Log.getStackTraceString(e));
            }
        }
        final String finalMessage = message;
        final boolean finalOk = ok;
        mLog.log("job end: " + title + ": " + (ok ? "ok" : "failed") + ": " + message);
        mMain.post(() -> finishJob(title, finalMessage, finalOk));
    }

    private void finishJob(String title, String message, boolean ok) {
        mWorker = null;
        mMain.removeCallbacks(mTicker);
        releaseWakeLock();
        if (KIND_REPLAY.equals(mKind)) {
            new Settings(this).setLastReplayJob(message, ok);
        } else {
            new Settings(this).setLastJob(title, message, ok);
        }
        publish(new State(mKind, false, title, null, false, message, ok));
        postResult(mKind, title, message, ok);
        if (!mDestroyed) {
            stopForeground(STOP_FOREGROUND_REMOVE);
            stopSelf();
        }
    }

    private void cancel(String reason) {
        if (mCancel != null && mWorker != null) {
            mLog.log("job cancel: " + reason);
            mCancel.cancel(reason);
            publish(new State(mKind, true, mTitle, mProgress.snapshot(), true, null, false));
        }
    }

    private static void publish(State state) {
        sState = state;
        for (Listener listener : new ArrayList<>(sListeners)) {
            listener.onImportState(state);
        }
    }

    private void acquireWakeLock() {
        if (mWakeLock == null) {
            PowerManager power = getSystemService(PowerManager.class);
            mWakeLock = power.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "fafre:import");
            mWakeLock.setReferenceCounted(false);
        }
        mWakeLock.acquire(WAKE_LOCK_TIMEOUT_MS);
    }

    private void releaseWakeLock() {
        if (mWakeLock != null && mWakeLock.isHeld()) {
            mWakeLock.release();
        }
    }

    // --------------------------------------------------------- notifications

    private boolean goForeground(Notification notification, String kind) {
        try {
            if (Build.VERSION.SDK_INT >= 29) {
                startForeground(NOTIFICATION_PROGRESS, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
            } else {
                startForeground(NOTIFICATION_PROGRESS, notification);
            }
            return true;
        } catch (RuntimeException e) {
            // ForegroundServiceStartNotAllowedException (12+) and friends: the
            // launcher only starts jobs while visible, so this is unexpected.
            mLog.log("import: cannot run in the foreground: " + e);
            if (KIND_REPLAY.equals(kind)) {
                new Settings(this).setLastReplayJob("Android did not allow the test to start: " + e.getMessage(),
                        false);
            } else {
                new Settings(this).setLastJob("Game data", "Android did not allow the transfer to start: "
                        + e.getMessage(), false);
            }
            return false;
        }
    }

    private Notification progressNotification(String kind, String title, Progress.Snapshot snapshot) {
        boolean replay = KIND_REPLAY.equals(kind);
        Notification.Builder builder = new Notification.Builder(this, replay ? CHANNEL_REPLAY : CHANNEL_PROGRESS)
                .setSmallIcon(replay ? android.R.drawable.ic_media_play : android.R.drawable.stat_sys_download)
                .setContentTitle(title)
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .setShowWhen(false)
                .setCategory(Notification.CATEGORY_PROGRESS)
                .setContentIntent(launcherIntent())
                .addAction(new Notification.Action.Builder(
                        Icon.createWithResource(this, android.R.drawable.ic_menu_close_clear_cancel), "Cancel",
                        PendingIntent.getService(this, 1,
                                new Intent(this, ImportService.class).setAction(ACTION_CANCEL),
                                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT)).build());
        if (snapshot == null) {
            builder.setContentText("Starting…").setProgress(0, 0, true);
        } else {
            String text = snapshot.phase + (snapshot.item.isEmpty() ? "" : ": " + snapshot.item);
            builder.setContentText(text).setSubText(snapshot.describe());
            int permille = snapshot.permille();
            builder.setProgress(1000, Math.max(0, permille), permille < 0);
        }
        if (Build.VERSION.SDK_INT >= 31) {
            builder.setForegroundServiceBehavior(Notification.FOREGROUND_SERVICE_IMMEDIATE);
        }
        return builder.build();
    }

    private void postResult(String kind, String title, String message, boolean ok) {
        boolean replay = KIND_REPLAY.equals(kind);
        Notification notification = new Notification.Builder(this, CHANNEL_RESULT)
                .setSmallIcon(ok ? android.R.drawable.stat_sys_download_done : android.R.drawable.stat_notify_error)
                .setContentTitle(replay ? title + (ok ? ": passed" : message.startsWith("Not started")
                        ? ": not started" : ": did not pass") : ok ? title + ": done" : title + ": stopped")
                .setContentText(message)
                .setStyle(new Notification.BigTextStyle().bigText(message))
                .setAutoCancel(true)
                .setContentIntent(launcherIntent())
                .build();
        mNotifications.notify(NOTIFICATION_RESULT, notification);
    }

    private PendingIntent launcherIntent() {
        Intent intent = Intent.makeMainActivity(new ComponentName(this, LauncherActivity.class))
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_RESET_TASK_IF_NEEDED);
        return PendingIntent.getActivity(this, 0, intent,
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
    }
}
