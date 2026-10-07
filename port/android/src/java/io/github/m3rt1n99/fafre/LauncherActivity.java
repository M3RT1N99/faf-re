package io.github.m3rt1n99.fafre;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.res.ColorStateList;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Insets;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Process;
import android.provider.DocumentsContract;
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.inputmethod.EditorInfo;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.text.DateFormat;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.Set;

/**
 * The app's entry point: shows what game data is on the device, gets the rest
 * (FAF download, folder imports through the import service, or the PC deploy
 * script), keeps the launch options, starts GameActivity with the same
 * command line the desktop game takes, and runs the replay test (the headless
 * runner on a picked or shared replay, {@link ReplayTest}).
 *
 * <p>All file work runs on {@link Background}; the activity holds no state that
 * is not also in {@link Settings}, the data root or the import service, so a
 * rotation or process death loses nothing (scroll position and an open log
 * view are restored from the instance state).
 */
public final class LauncherActivity extends Activity implements ImportService.Listener {
    private static final int REQUEST_SCFA_TREE = 1;
    private static final int REQUEST_FAF_TREE = 2;
    private static final int REQUEST_VAULT_TREE = 3;
    private static final int REQUEST_REPLAY_FILE = 4;
    private static final int REQUEST_SAVE_ZIP = 5;
    private static final int REQUEST_SAVE_ALL_ZIP = 6;
    private static final int REQUEST_NOTIFICATIONS = 10;
    /** Fixed id so the ScrollView restores its position after a rotation. */
    private static final int SCROLL_VIEW_ID = 0x0f0a0001;

    private static final String ACTION_DOWNLOAD = "download";
    private static final String ACTION_IMPORT_SCFA = "import_scfa";
    private static final String ACTION_IMPORT_FAF = "import_faf";
    private static final String ACTION_IMPORT_VAULT = "import_vault";
    private static final String ACTION_VERIFY = "verify";
    private static final String ACTION_REPLAY_TEST = "replay_test";
    private static final String ACTION_SELF_TEST = "self_test";
    private static final String ACTION_DEVICE_PROBE = "device_probe";

    private static final String STATE_PENDING_ACTION = "pending_action";
    private static final String STATE_PENDING_URI = "pending_uri";
    private static final String STATE_LOG_DIALOG = "log_dialog";
    private static final String STATE_ZIP_RUN = "zip_run";
    /** Marks a pending "Save all runs (zip)" in mPendingZipRun. */
    private static final String ALL_RUNS = "*";

    /** The runtime logs per graphics backend; each keeps its previous run as .1.log. */
    private static final String VULKAN_LOG = runtimeLog(LaunchArgs.RENDERER_VULKAN);
    private static final String GLES_LOG = runtimeLog(LaunchArgs.RENDERER_GLES);
    private static final String[] CLEARED_LOGS = {
            VULKAN_LOG, previousLog(VULKAN_LOG), GLES_LOG, previousLog(GLES_LOG),
            "faf_android.log" /* the single log of 0.3.0 and 0.3.1 */, LaunchArgs.GAME_LOG, ReplayTest.LOG_NAME};
    private static final String DEPLOY_COMMAND =
            "powershell -ExecutionPolicy Bypass -File scripts/port/deploy_android.ps1";
    private static final int LOG_TAIL_BYTES = 96 * 1024;
    private static final long GAME_EXIT_WAIT_MS = 2000;

    /** Everything a refresh reads from disk, handed to the UI thread in one piece. */
    private static final class Snapshot {
        DataManifest manifest;
        DataRoot root;
        String nativeProblem;
        DataStatus status;
        RunStatus run;
        String runError;
        boolean gameAlive;
        String runnerProblem;
        ReplayFiles.Info replay;
        ReplayFiles.Check replayCheck;
        String lastRunName;
        JSONObject lastRun;
        ReplayRefs refs;
        boolean hasOptimized;
        boolean hasDeviceProbe;
        String lastProbeName;
        JSONObject lastProbe;
        int runCount;
    }

    private Settings mSettings;
    private LauncherLog mLog;
    private Ui mUi;
    private String mVersionName;

    private Snapshot mSnapshot;
    private String mLoadError;
    private boolean mRefreshRunning;
    private boolean mRefreshAgain;
    private boolean mStarting;
    private boolean mJobWasRunning;
    private boolean mOptionsBuilt;
    private String mPendingAction;
    private String mPendingUri;
    private String mLogDialogFile;
    private String mPendingZipRun;
    private boolean mReplayBusy;
    /** A replay test job is running (ImportService state); the shown result is then the previous run's. */
    private boolean mReplayJobRunning;
    private AlertDialog mDialog;

    private TextView mRootView;
    private Button mStartButton;
    private TextView mStartHint;
    private TextView mRunState;
    private TextView mRunMessage;
    private TextView mRunCounters;
    private TextView mRunTime;
    private TextView mDataSummary;
    private TextView mDataDetails;
    private Button mCheckButton;
    private Button mVerifyButton;
    private LinearLayout mJobPanel;
    private TextView mJobTitle;
    private ProgressBar mJobProgress;
    private TextView mJobDetail;
    private Button mCancelButton;
    private TextView mJobResult;
    private Button mDownloadButton;
    private TextView mDownloadHint;
    private Button mImportFafButton;
    private Button mImportScfaButton;
    private CheckBox mRecommendedBox;
    private LinearLayout mOptionalList;
    private Button mImportVaultButton;
    private RadioButton mVulkanButton;
    private RadioButton mGlesButton;
    private CheckBox mNoMovieBox;
    private CheckBox mNoSoundBox;
    private EditText mExtraArgs;
    private TextView mReplayRunnerHint;
    private TextView mReplayInfo;
    private TextView mReplayNotes;
    private Button mPickReplayButton;
    private Button mSelfTestButton;
    private Button mRunTestButton;
    private LinearLayout mReplayAdvanced;
    private LinearLayout mReplayJobPanel;
    private TextView mReplayJobTitle;
    private ProgressBar mReplayJobProgress;
    private TextView mReplayJobDetail;
    private Button mReplayCancelButton;
    private TextView mReplayVerdict;
    private LinearLayout mReplayLines;
    private TextView mReplayRunTime;
    private Button mSaveZipButton;
    private Button mCopySummaryButton;
    private Button mRunOutputButton;
    private Button mSaveAllZipButton;
    private CheckBox mOptimizedBox;
    private CheckBox mSpeedBox;
    private CheckBox mAffinityBigBox;
    private Button mDeviceProbeButton;
    private Button mProbeImageButton;
    private TextView mProbeVerdict;
    private LinearLayout mProbeLines;

    // ------------------------------------------------------------- lifecycle

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        mSettings = new Settings(this);
        mLog = LauncherLog.get(this);
        mUi = new Ui(this);
        mVersionName = AppInfo.versionName(this);
        // Handle the system bar and IME insets ourselves on 11+, the way
        // Android 15 enforces it, so the layout behaves the same everywhere.
        GameActivity.drawEdgeToEdge(getWindow());
        setContentView(buildContent());
        if (savedInstanceState != null) {
            mPendingAction = savedInstanceState.getString(STATE_PENDING_ACTION);
            mPendingUri = savedInstanceState.getString(STATE_PENDING_URI);
            mPendingZipRun = savedInstanceState.getString(STATE_ZIP_RUN);
            String log = savedInstanceState.getString(STATE_LOG_DIALOG);
            if (log != null) {
                showLog(log);
            }
        } else {
            mLog.log("launcher " + mVersionName + " opened");
            handleInboxResult(getIntent());
        }
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        handleInboxResult(intent);
        refresh();
    }

    /** ReplayInboxActivity hands over the outcome of an "Open with" / "Share". */
    private void handleInboxResult(Intent intent) {
        String message = intent != null ? intent.getStringExtra(ReplayInboxActivity.EXTRA_MESSAGE) : null;
        if (message != null) {
            toast(message);
            intent.removeExtra(ReplayInboxActivity.EXTRA_MESSAGE);
        }
    }

    @Override
    protected void onStart() {
        super.onStart();
        ImportService.addListener(this);
        onImportState(ImportService.state());
    }

    @Override
    protected void onResume() {
        super.onResume();
        // The game, the PC deploy script or an import may have changed the data root.
        refresh();
    }

    @Override
    protected void onStop() {
        ImportService.removeListener(this);
        super.onStop();
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        super.onSaveInstanceState(outState);
        outState.putString(STATE_PENDING_ACTION, mPendingAction);
        outState.putString(STATE_PENDING_URI, mPendingUri);
        outState.putString(STATE_LOG_DIALOG, mLogDialogFile);
        outState.putString(STATE_ZIP_RUN, mPendingZipRun);
    }

    @Override
    protected void onDestroy() {
        if (mDialog != null) {
            // Not a user dismissal: keep mLogDialogFile for the recreated activity.
            mDialog.setOnDismissListener(null);
            mDialog.dismiss();
            mDialog = null;
        }
        super.onDestroy();
    }

    // -------------------------------------------------------------------- UI

    private View buildContent() {
        ScrollView scroll = new ScrollView(this);
        scroll.setId(SCROLL_VIEW_ID);
        scroll.setFillViewport(true);
        scroll.setClipToPadding(false);
        scroll.setBackgroundColor(Ui.BACKGROUND);

        FrameLayout frame = new FrameLayout(this);
        scroll.addView(frame, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        Ui.Column column = new Ui.Column(this, mUi.dp(Ui.MAX_CONTENT_DP));
        column.setPadding(mUi.dp(16), mUi.dp(16), mUi.dp(16), mUi.dp(28));
        frame.addView(column, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.CENTER_HORIZONTAL));

        column.addView(buildHeader(), mUi.matchWrap(0));
        column.addView(buildStartCard(), mUi.matchWrap(16));
        column.addView(buildRunCard(), mUi.matchWrap(12));
        column.addView(buildReplayCard(), mUi.matchWrap(12));
        column.addView(buildDataCard(), mUi.matchWrap(12));
        column.addView(buildImportCard(), mUi.matchWrap(12));
        column.addView(buildOptionsCard(), mUi.matchWrap(12));
        column.addView(buildLogsCard(), mUi.matchWrap(12));

        applyInsets(scroll);
        return scroll;
    }

    private View buildHeader() {
        LinearLayout header = new LinearLayout(this);
        header.setOrientation(LinearLayout.VERTICAL);
        TextView title = mUi.text("FAF (faf-re)", 26, Ui.TITLE);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        header.addView(title, mUi.matchWrap(0));
        header.addView(mUi.hint("Forged Alliance Forever on Android, native engine port · version " + mVersionName),
                mUi.matchWrap(2));
        mRootView = mUi.mono("Data folder: …");
        mRootView.setTextColor(Ui.MUTED);
        header.addView(mRootView, mUi.matchWrap(8));
        return header;
    }

    private View buildStartCard() {
        LinearLayout card = mUi.card();
        mStartButton = mUi.primaryButton("Start");
        mStartButton.setEnabled(false);
        mStartButton.setOnClickListener(v -> startGame());
        card.addView(mStartButton, mUi.matchWrap(0));
        mStartHint = mUi.hint("Checking game data…");
        card.addView(mStartHint, mUi.matchWrap(8));
        return card;
    }

    private View buildRunCard() {
        LinearLayout card = mUi.card();
        card.addView(mUi.heading("Last run"), mUi.matchWrap(0));
        mRunState = mUi.text("", 15, Ui.TEXT);
        mRunState.setTypeface(Typeface.DEFAULT_BOLD);
        card.addView(mRunState, mUi.matchWrap(6));
        mRunMessage = mUi.body("");
        mRunMessage.setTextIsSelectable(true);
        card.addView(mRunMessage, mUi.matchWrap(4));
        mRunCounters = mUi.mono("");
        card.addView(mRunCounters, mUi.matchWrap(6));
        mRunTime = mUi.hint("");
        card.addView(mRunTime, mUi.matchWrap(4));
        return card;
    }

    private View buildReplayCard() {
        LinearLayout card = mUi.card();
        card.addView(mUi.heading("Replay test"), mUi.matchWrap(0));
        card.addView(mUi.hint("Plays a FAF replay to its end with the headless engine, in a process of its own, "
                + "after a self-test of the low-address memory arena. Download a replay in the browser "
                + "(https://replay.faforever.com/<id>) and open it with this app, or pick it here. It needs the "
                + "required and recommended game data and the replay's map. Keep the app open or in the "
                + "background; do not swipe it away while the test runs."), mUi.matchWrap(4));
        mReplayRunnerHint = mUi.body("");
        mReplayRunnerHint.setTextColor(Ui.BAD);
        mReplayRunnerHint.setVisibility(View.GONE);
        card.addView(mReplayRunnerHint, mUi.matchWrap(8));
        mReplayInfo = mUi.mono("No replay selected.");
        card.addView(mReplayInfo, mUi.matchWrap(10));
        mReplayNotes = mUi.body("");
        mReplayNotes.setTextIsSelectable(true);
        mReplayNotes.setVisibility(View.GONE);
        card.addView(mReplayNotes, mUi.matchWrap(6));

        mPickReplayButton = mUi.button("Pick replay…");
        mPickReplayButton.setOnClickListener(v -> pickReplay());
        mSelfTestButton = mUi.button("Self-test only");
        mSelfTestButton.setOnClickListener(v -> request(ACTION_SELF_TEST, null));
        card.addView(mUi.row(mPickReplayButton, mSelfTestButton), mUi.matchWrap(10));
        mRunTestButton = mUi.primaryButton("Run test");
        mRunTestButton.setOnClickListener(v -> request(ACTION_REPLAY_TEST, null));
        card.addView(mRunTestButton, mUi.matchWrap(8));
        mDeviceProbeButton = mUi.button("Device probe");
        mDeviceProbeButton.setOnClickListener(v -> request(ACTION_DEVICE_PROBE, null));
        mProbeImageButton = mUi.button("Probe images");
        mProbeImageButton.setOnClickListener(v -> showProbeImages());
        card.addView(mUi.row(mDeviceProbeButton, mProbeImageButton), mUi.matchWrap(8));

        CheckBox advanced = mUi.checkBox("Advanced options", mSettings.replayAdvanced());
        card.addView(advanced, mUi.matchWrap(6));
        mReplayAdvanced = new LinearLayout(this);
        mReplayAdvanced.setOrientation(LinearLayout.VERTICAL);
        mReplayAdvanced.setVisibility(mSettings.replayAdvanced() ? View.VISIBLE : View.GONE);
        advanced.setOnCheckedChangeListener((box, checked) -> {
            mSettings.setReplayAdvanced(checked);
            mReplayAdvanced.setVisibility(checked ? View.VISIBLE : View.GONE);
        });
        CheckBox repeat = mUi.checkBox("Run the replay twice (is the result deterministic?)", mSettings.replayRepeat());
        repeat.setOnCheckedChangeListener((box, checked) -> mSettings.setReplayRepeat(checked));
        mReplayAdvanced.addView(repeat, mUi.matchWrap(0));
        CheckBox skip = mUi.checkBox("Skip the self-test", mSettings.replaySkipSelfTest());
        skip.setOnCheckedChangeListener((box, checked) -> {
            mSettings.setReplaySkipSelfTest(checked);
            renderReplay(); // the run button's label names the steps
        });
        mReplayAdvanced.addView(skip, mUi.matchWrap(0));
        CheckBox interlocked = mUi.checkBox("Interlocked sim (/headlessinterlocked)", mSettings.replayInterlocked());
        interlocked.setOnCheckedChangeListener((box, checked) -> mSettings.setReplayInterlocked(checked));
        mReplayAdvanced.addView(interlocked, mUi.matchWrap(0));
        CheckBox noArena = mUi.checkBox("Without the low arena (FAF_LOWARENA=0): expected to crash",
                mSettings.replayNoArena());
        noArena.setOnCheckedChangeListener((box, checked) -> {
            mSettings.setReplayNoArena(checked);
            renderReplay();
        });
        mReplayAdvanced.addView(noArena, mUi.matchWrap(0));
        mReplayAdvanced.addView(mUi.hint("Without the arena the engine's 32-bit pointer fields truncate heap "
                + "addresses; the crash and where it happens are the measurement."), mUi.matchWrap(2));
        mOptimizedBox = mUi.checkBox("Optimised engine (-O2 build)", mSettings.replayOptimized());
        mOptimizedBox.setOnCheckedChangeListener((box, checked) -> {
            mSettings.setReplayOptimized(checked);
            renderReplay();
        });
        mReplayAdvanced.addView(mOptimizedBox, mUi.matchWrap(6));
        mSpeedBox = mUi.checkBox("Speed experiment: timer slack 1 ns, no little cores", mSettings.replaySpeedExperiment());
        mSpeedBox.setOnCheckedChangeListener((box, checked) -> {
            mSettings.setReplaySpeedExperiment(checked);
            mAffinityBigBox.setEnabled(checked);
            renderReplay();
        });
        mReplayAdvanced.addView(mSpeedBox, mUi.matchWrap(0));
        mAffinityBigBox = mUi.checkBox("…only the biggest cores", mSettings.replayAffinityBig());
        mAffinityBigBox.setEnabled(mSettings.replaySpeedExperiment());
        mAffinityBigBox.setOnCheckedChangeListener((box, checked) -> {
            mSettings.setReplayAffinityBig(checked);
            renderReplay();
        });
        mReplayAdvanced.addView(mAffinityBigBox, mUi.matchWrap(0));
        mReplayAdvanced.addView(mUi.hint("Both experiments must give the reference chain of their own build. The -O2 "
                + "build is the same code compiled with optimisation; the speed experiment makes the runner set "
                + "its timer slack to 1 ns and keep its threads off the little cores (FAF_RUNNER_TIMERSLACK_NS, "
                + "FAF_RUNNER_AFFINITY)."), mUi.matchWrap(2));
        card.addView(mReplayAdvanced, mUi.matchWrap(0));

        mReplayJobPanel = new LinearLayout(this);
        mReplayJobPanel.setOrientation(LinearLayout.VERTICAL);
        mReplayJobPanel.setVisibility(View.GONE);
        mReplayJobTitle = mUi.text("", 15, Ui.TITLE);
        mReplayJobTitle.setTypeface(Typeface.DEFAULT_BOLD);
        mReplayJobPanel.addView(mReplayJobTitle, mUi.matchWrap(0));
        mReplayJobProgress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        mReplayJobProgress.setMax(1000);
        mReplayJobProgress.setProgressTintList(ColorStateList.valueOf(Ui.ACCENT));
        mReplayJobProgress.setIndeterminateTintList(ColorStateList.valueOf(Ui.ACCENT));
        mReplayJobPanel.addView(mReplayJobProgress, mUi.matchWrap(6));
        mReplayJobDetail = mUi.hint("");
        mReplayJobPanel.addView(mReplayJobDetail, mUi.matchWrap(4));
        mReplayCancelButton = mUi.button("Cancel");
        mReplayCancelButton.setOnClickListener(v -> ImportService.cancelRunning("Cancelled"));
        mReplayJobPanel.addView(mReplayCancelButton, mUi.matchWrap(6));
        card.addView(mReplayJobPanel, mUi.matchWrap(12));

        mReplayVerdict = mUi.text("", 15, Ui.TEXT);
        mReplayVerdict.setTypeface(Typeface.DEFAULT_BOLD);
        mReplayVerdict.setTextIsSelectable(true);
        mReplayVerdict.setVisibility(View.GONE);
        card.addView(mReplayVerdict, mUi.matchWrap(12));
        mReplayLines = new LinearLayout(this);
        mReplayLines.setOrientation(LinearLayout.VERTICAL);
        card.addView(mReplayLines, mUi.matchWrap(4));
        mReplayRunTime = mUi.hint("");
        mReplayRunTime.setVisibility(View.GONE);
        card.addView(mReplayRunTime, mUi.matchWrap(4));
        mSaveZipButton = mUi.button("Save run (zip)…");
        mSaveZipButton.setOnClickListener(v -> saveRunZip());
        mCopySummaryButton = mUi.button("Copy summary");
        mCopySummaryButton.setOnClickListener(v -> copySummary());
        card.addView(mUi.row(mSaveZipButton, mCopySummaryButton), mUi.matchWrap(10));
        mRunOutputButton = mUi.button("Runner output");
        mRunOutputButton.setOnClickListener(v -> showRunOutput());
        mSaveAllZipButton = mUi.button("Save all runs (zip)…");
        mSaveAllZipButton.setOnClickListener(v -> saveAllRunsZip());
        card.addView(mUi.row(mRunOutputButton, mSaveAllZipButton), mUi.matchWrap(4));
        card.addView(mUi.hint("Send the saved zip (or the copied summary) to the developers. It contains the run's "
                + "logs and device details, never the replay file; the engine log names the replay's players (their "
                + "FAF nicknames, as on FAF's replay pages). \"Save all runs\" puts every run in runs/ into one zip "
                + "(Clear logs empties runs/)."), mUi.matchWrap(4));

        TextView probeHeading = mUi.text("Device probe", 15, Ui.TITLE);
        probeHeading.setTypeface(Typeface.DEFAULT_BOLD);
        card.addView(probeHeading, mUi.matchWrap(16));
        card.addView(mUi.hint("What the GPU drivers offer the graphics port: Vulkan and OpenGL ES features, texture "
                + "formats, an offscreen test pattern read back into a PNG, and shader compile times (placeholder "
                + "shaders, not FA's). Runs in a process of its own, like the replay test."), mUi.matchWrap(2));
        mProbeVerdict = mUi.text("Not run yet.", 14, Ui.MUTED);
        mProbeVerdict.setTextIsSelectable(true);
        card.addView(mProbeVerdict, mUi.matchWrap(6));
        mProbeLines = new LinearLayout(this);
        mProbeLines.setOrientation(LinearLayout.VERTICAL);
        card.addView(mProbeLines, mUi.matchWrap(2));
        return card;
    }

    private View buildDataCard() {
        LinearLayout card = mUi.card();
        card.addView(mUi.heading("Game data"), mUi.matchWrap(0));
        mDataSummary = mUi.body("Checking…");
        card.addView(mDataSummary, mUi.matchWrap(6));
        mDataDetails = mUi.mono("");
        card.addView(mDataDetails, mUi.matchWrap(8));
        mCheckButton = mUi.button("Check again");
        mCheckButton.setOnClickListener(v -> {
            refresh();
            toast("Checking the data folder…");
        });
        mVerifyButton = mUi.button("Verify checksums");
        mVerifyButton.setOnClickListener(v -> request(ACTION_VERIFY, null));
        card.addView(mUi.row(mCheckButton, mVerifyButton), mUi.matchWrap(10));
        return card;
    }

    private View buildImportCard() {
        LinearLayout card = mUi.card();
        card.addView(mUi.heading("Get game data"), mUi.matchWrap(0));
        card.addView(mUi.body("The app ships no game files. FAF's files come from FAF; the Supreme Commander: "
                + "Forged Alliance files come from your own copy of the game."), mUi.matchWrap(4));

        mJobPanel = new LinearLayout(this);
        mJobPanel.setOrientation(LinearLayout.VERTICAL);
        mJobPanel.setVisibility(View.GONE);
        mJobTitle = mUi.text("", 15, Ui.TITLE);
        mJobTitle.setTypeface(Typeface.DEFAULT_BOLD);
        mJobPanel.addView(mJobTitle, mUi.matchWrap(0));
        mJobProgress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        mJobProgress.setMax(1000);
        mJobProgress.setProgressTintList(ColorStateList.valueOf(Ui.ACCENT));
        mJobProgress.setIndeterminateTintList(ColorStateList.valueOf(Ui.ACCENT));
        mJobPanel.addView(mJobProgress, mUi.matchWrap(6));
        mJobDetail = mUi.hint("");
        mJobPanel.addView(mJobDetail, mUi.matchWrap(4));
        mCancelButton = mUi.button("Cancel");
        mCancelButton.setOnClickListener(v -> ImportService.cancelRunning("Cancelled"));
        mJobPanel.addView(mCancelButton, mUi.matchWrap(6));
        card.addView(mJobPanel, mUi.matchWrap(12));
        mJobResult = mUi.body("");
        mJobResult.setTextIsSelectable(true);
        card.addView(mJobResult, mUi.matchWrap(8));

        card.addView(subheading("FAF files"), mUi.matchWrap(14));
        mDownloadButton = mUi.button("Download FAF files");
        mDownloadButton.setOnClickListener(v -> confirmDownload());
        card.addView(mDownloadButton, mUi.matchWrap(6));
        mDownloadHint = mUi.hint("From FAF's content server, checked against the manifest's SHA-256. Interrupted "
                + "downloads continue where they stopped.");
        card.addView(mDownloadHint, mUi.matchWrap(4));
        mImportFafButton = mUi.button("Import FAF folder…");
        mImportFafButton.setOnClickListener(v -> pickTree(REQUEST_FAF_TREE, Settings.TREE_FAF));
        card.addView(mImportFafButton, mUi.matchWrap(8));
        card.addView(mUi.hint("Or copy the FAF client's data folder (C:/ProgramData/FAForever) to this device and "
                + "pick it. The files must be the same FAF version."), mUi.matchWrap(4));

        card.addView(subheading("Supreme Commander: Forged Alliance files"), mUi.matchWrap(16));
        mImportScfaButton = mUi.button("Import SCFA folder…");
        mImportScfaButton.setOnClickListener(v -> pickTree(REQUEST_SCFA_TREE, Settings.TREE_SCFA));
        card.addView(mImportScfaButton, mUi.matchWrap(6));
        card.addView(mUi.hint("Copy the game folder (Steam: steamapps/common/Supreme Commander Forged Alliance) to "
                + "this device, an SD card or a USB drive and pick it. Android does not let apps pick the top of "
                + "internal storage or the Download folder itself; use a subfolder. Only the parts selected below "
                + "are copied; files already in place are skipped."), mUi.matchWrap(4));
        mRecommendedBox = mUi.checkBox("Recommended for normal play", mSettings.importRecommended());
        mRecommendedBox.setOnCheckedChangeListener((box, checked) -> {
            mSettings.setImportRecommended(checked);
            renderDownloadHint();
        });
        card.addView(mRecommendedBox, mUi.matchWrap(6));
        mOptionalList = new LinearLayout(this);
        mOptionalList.setOrientation(LinearLayout.VERTICAL);
        card.addView(mOptionalList, mUi.matchWrap(0));

        card.addView(subheading("From a PC"), mUi.matchWrap(16));
        card.addView(mUi.hint("With USB debugging on, run this in the faf-re checkout. It copies the required and "
                + "recommended files from your SCFA and FAF installs straight into the data folder:"),
                mUi.matchWrap(4));
        card.addView(mUi.mono(DEPLOY_COMMAND), mUi.matchWrap(6));

        card.addView(subheading("Your maps and mods"), mUi.matchWrap(16));
        mImportVaultButton = mUi.button("Import vault folder…");
        mImportVaultButton.setOnClickListener(v -> pickTree(REQUEST_VAULT_TREE, Settings.TREE_VAULT));
        card.addView(mImportVaultButton, mUi.matchWrap(6));
        card.addView(mUi.hint("Optional: the FAF vault folder with maps/ and mods/ (on the PC: Documents/My "
                + "Games/Gas Powered Games/Supreme Commander Forged Alliance)."), mUi.matchWrap(4));
        return card;
    }

    private View buildOptionsCard() {
        LinearLayout card = mUi.card();
        card.addView(mUi.heading("Launch options"), mUi.matchWrap(0));
        card.addView(mUi.hint("Graphics backend (/renderer). The runtime falls back to OpenGL ES by itself when "
                + "Vulkan fails."), mUi.matchWrap(6));
        RadioGroup renderer = new RadioGroup(this);
        renderer.setOrientation(RadioGroup.HORIZONTAL);
        mVulkanButton = radio("Vulkan");
        mGlesButton = radio("OpenGL ES");
        renderer.addView(mVulkanButton);
        renderer.addView(mGlesButton);
        renderer.check(LaunchArgs.RENDERER_GLES.equals(mSettings.renderer()) ? mGlesButton.getId()
                : mVulkanButton.getId());
        renderer.setOnCheckedChangeListener((group, id) -> {
            mSettings.setRenderer(id == mGlesButton.getId() ? LaunchArgs.RENDERER_GLES : LaunchArgs.RENDERER_VULKAN);
            renderStart();
        });
        card.addView(renderer, mUi.matchWrap(2));
        mNoMovieBox = mUi.checkBox("Skip movies (/nomovie)", mSettings.noMovie());
        mNoMovieBox.setOnCheckedChangeListener((box, checked) -> {
            mSettings.setNoMovie(checked);
            renderStart();
        });
        card.addView(mNoMovieBox, mUi.matchWrap(6));
        mNoSoundBox = mUi.checkBox("No sound (/nosound)", mSettings.noSound());
        mNoSoundBox.setOnCheckedChangeListener((box, checked) -> {
            mSettings.setNoSound(checked);
            renderStart();
        });
        card.addView(mNoSoundBox, mUi.matchWrap(0));
        card.addView(mUi.hint("Extra arguments, as on the desktop command line (for example /map SCMP_009). An "
                + "option given here replaces the launcher's own (/init, /log, /renderer)."), mUi.matchWrap(10));
        mExtraArgs = new EditText(this);
        mExtraArgs.setSingleLine(true);
        mExtraArgs.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        mExtraArgs.setImeOptions(EditorInfo.IME_ACTION_DONE);
        mExtraArgs.setTypeface(Typeface.MONOSPACE);
        mExtraArgs.setTextColor(Ui.TEXT);
        mExtraArgs.setHintTextColor(Ui.MUTED);
        mExtraArgs.setBackgroundTintList(ColorStateList.valueOf(Ui.ACCENT));
        mExtraArgs.setHint("/map SCMP_009");
        mExtraArgs.setText(mSettings.extraArgs());
        mExtraArgs.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {
            }

            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {
            }

            @Override
            public void afterTextChanged(Editable s) {
                mSettings.setExtraArgs(s.toString());
            }
        });
        card.addView(mExtraArgs, mUi.matchWrap(2));
        return card;
    }

    private View buildLogsCard() {
        LinearLayout card = mUi.card();
        card.addView(mUi.heading("Logs"), mUi.matchWrap(0));
        card.addView(mUi.hint("Files in the logs folder. From a PC: adb pull /sdcard/Android/data/"
                + getPackageName() + "/files/logs"), mUi.matchWrap(4));
        Button vulkan = mUi.button("Vulkan log");
        vulkan.setOnClickListener(v -> showLog(VULKAN_LOG));
        Button gles = mUi.button("GLES log");
        gles.setOnClickListener(v -> showLog(GLES_LOG));
        card.addView(mUi.row(vulkan, gles), mUi.matchWrap(8));
        Button launcher = mUi.button("Launcher log");
        launcher.setOnClickListener(v -> showLog(LauncherLog.FILE_NAME));
        Button game = mUi.button("Game log");
        game.setOnClickListener(v -> showLog(LaunchArgs.GAME_LOG));
        card.addView(mUi.row(launcher, game), mUi.matchWrap(4));
        Button replays = mUi.button("Replay test log");
        replays.setOnClickListener(v -> showLog(ReplayTest.LOG_NAME));
        Button licenses = mUi.button("Licenses");
        licenses.setOnClickListener(v -> showLicenses());
        card.addView(mUi.row(replays, licenses), mUi.matchWrap(4));
        Button clear = mUi.button("Clear logs");
        clear.setOnClickListener(v -> confirmClearLogs());
        card.addView(clear, mUi.matchWrap(4));
        return card;
    }

    private static String runtimeLog(String renderer) {
        return "faf_android_" + renderer + ".log";
    }

    private static String previousLog(String name) {
        return name.substring(0, name.length() - ".log".length()) + ".1.log";
    }

    private TextView subheading(String value) {
        TextView view = mUi.text(value, 15, Ui.ACCENT);
        view.setTypeface(Typeface.DEFAULT_BOLD);
        return view;
    }

    private RadioButton radio(String label) {
        RadioButton button = new RadioButton(this);
        button.setId(View.generateViewId());
        button.setText(label);
        button.setTextColor(Ui.TEXT);
        button.setButtonTintList(ColorStateList.valueOf(Ui.ACCENT));
        button.setMinHeight(mUi.dp(44));
        button.setPadding(button.getPaddingLeft(), 0, mUi.dp(16), 0);
        return button;
    }

    /** Pads the content by the system bars, the display cutout and the keyboard. */
    private void applyInsets(View root) {
        root.setOnApplyWindowInsetsListener((view, insets) -> {
            if (Build.VERSION.SDK_INT >= 30) {
                Insets bars = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                Insets ime = insets.getInsets(WindowInsets.Type.ime());
                view.setPadding(bars.left, bars.top, bars.right, Math.max(bars.bottom, ime.bottom));
            } else {
                applyLegacyInsets(view, insets);
            }
            return insets;
        });
    }

    /** API 26-29: the window fits the system bars itself; only pass on what is left. */
    @SuppressWarnings("deprecation")
    private static void applyLegacyInsets(View view, WindowInsets insets) {
        view.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
    }

    // --------------------------------------------------------------- refresh

    private void refresh() {
        if (mRefreshRunning) {
            mRefreshAgain = true;
            return;
        }
        mRefreshRunning = true;
        final Context app = getApplicationContext();
        Background.run(() -> loadSnapshot(app), (snapshot, error) -> {
            mRefreshRunning = false;
            if (isDestroyed()) {
                return;
            }
            if (error != null) {
                mSnapshot = null;
                mLoadError = describe(error);
                mLog.log("status check failed: " + mLoadError);
            } else {
                mSnapshot = snapshot;
                mLoadError = null;
                buildOptionalEntries(snapshot.manifest);
            }
            render();
            if (mRefreshAgain) {
                mRefreshAgain = false;
                refresh();
            }
        });
    }

    private static Snapshot loadSnapshot(Context app) throws IOException {
        Snapshot snapshot = new Snapshot();
        snapshot.manifest = AppInfo.manifest(app);
        snapshot.root = AppInfo.dataRoot(app);
        snapshot.nativeProblem = AppInfo.nativeRuntimeProblem(app);
        snapshot.status = DataStatus.check(snapshot.manifest, snapshot.root, LauncherLog.get(app));
        try {
            snapshot.run = RunStatus.read(snapshot.root.file(statusPath(snapshot.manifest)));
        } catch (IOException e) {
            snapshot.runError = e.getMessage();
        }
        snapshot.gameAlive = AppInfo.gameProcessPid(app) > 0;
        snapshot.runnerProblem = Runner.problem(app);
        Settings settings = new Settings(app);
        String stem = settings.replayStem();
        snapshot.replay = stem != null ? ReplayFiles.read(snapshot.root, stem) : null;
        if (snapshot.replay != null) {
            snapshot.replayCheck = ReplayFiles.check(snapshot.manifest, snapshot.root, snapshot.status,
                    snapshot.replay);
        }
        snapshot.lastRunName = settings.lastReplayRun();
        snapshot.lastRun = ReplayTest.readResult(snapshot.root, snapshot.lastRunName);
        snapshot.refs = ReplayRefs.get(app);
        snapshot.hasOptimized = Runner.hasOptimized(app);
        snapshot.hasDeviceProbe = Runner.hasDeviceProbe(app);
        snapshot.lastProbeName = settings.lastProbeRun();
        snapshot.lastProbe = ReplayTest.readResult(snapshot.root, snapshot.lastProbeName);
        File runs = snapshot.root.find(ReplayTest.RUNS_DIR);
        String[] names = runs != null ? runs.list() : null;
        snapshot.runCount = names != null ? names.length : 0;
        return snapshot;
    }

    private static String statusPath(DataManifest manifest) {
        return manifest.layout.launch + "/status.json";
    }

    /** Optional SCFA entries become checkboxes once the manifest is known. */
    private void buildOptionalEntries(DataManifest manifest) {
        if (mOptionsBuilt) {
            return;
        }
        mOptionsBuilt = true;
        long recommended = 0;
        for (DataManifest.Entry entry : manifest.scfaEntries) {
            if (entry.tier == DataManifest.Tier.RECOMMENDED) {
                recommended += Math.max(0, entry.approxBytes);
            }
        }
        mRecommendedBox.setText("Recommended for normal play: units, terrain, sounds, skirmish maps (~"
                + FileOps.formatBytes(recommended) + ")");
        Set<String> selected = mSettings.importOptional();
        for (final DataManifest.Entry entry : manifest.scfaEntries) {
            if (entry.tier != DataManifest.Tier.OPTIONAL) {
                continue;
            }
            String label = entry.label + (entry.approxBytes > 0 ? " (~" + FileOps.formatBytes(entry.approxBytes)
                    + ")" : "");
            CheckBox box = mUi.checkBox(label, selected.contains(entry.id));
            box.setOnCheckedChangeListener((view, checked) -> mSettings.setImportOptional(entry.id, checked));
            mOptionalList.addView(box, mUi.matchWrap(0));
        }
        mDownloadButton.setText("Download FAF files (v" + manifest.fafVersion + ")");
    }

    private void render() {
        Snapshot snapshot = mSnapshot;
        if (snapshot == null) {
            mDataSummary.setText(mLoadError != null ? "Cannot check the game data: " + mLoadError : "Checking…");
            mDataSummary.setTextColor(mLoadError != null ? Ui.BAD : Ui.BODY);
            renderStart();
            updateButtons();
            return;
        }
        mRootView.setText("Data folder: " + snapshot.root.path());
        renderRun(snapshot);
        renderReplay();
        renderData(snapshot);
        renderDownloadHint();
        renderStart();
        updateButtons();
    }

    private void renderStart() {
        Snapshot snapshot = mSnapshot;
        String hint;
        int color;
        if (snapshot == null) {
            hint = mLoadError != null ? "Cannot start: " + mLoadError : "Checking game data…";
            color = mLoadError != null ? Ui.BAD : Ui.MUTED;
        } else if (snapshot.nativeProblem != null) {
            hint = snapshot.nativeProblem;
            color = Ui.BAD;
        } else if (!snapshot.status.requiredComplete()) {
            hint = "Missing required game data: " + String.join(", ", snapshot.status.missingRequired())
                    + ". Get it below.";
            color = Ui.WARN;
        } else if (ImportService.state().running) {
            hint = ImportService.KIND_REPLAY.equals(ImportService.state().kind)
                    ? "Wait for the replay test to finish." : "Wait for the running transfer to finish.";
            color = Ui.WARN;
        } else {
            StringBuilder text = new StringBuilder("Ready · ");
            text.append(LaunchArgs.RENDERER_GLES.equals(mSettings.renderer()) ? "OpenGL ES" : "Vulkan");
            if (mSettings.noMovie()) {
                text.append(" · no movies");
            }
            if (mSettings.noSound()) {
                text.append(" · no sound");
            }
            hint = text.toString();
            color = Ui.GOOD;
        }
        mStartHint.setText(hint);
        mStartHint.setTextColor(color);
    }

    private void renderRun(Snapshot snapshot) {
        long lastLaunch = mSettings.lastLaunch();
        RunStatus run = snapshot.run;
        String state;
        int color;
        StringBuilder message = new StringBuilder();
        String counters = "";
        String time = "";
        if (snapshot.runError != null) {
            state = "Status unreadable";
            color = Ui.BAD;
            message.append(snapshot.runError);
        } else if (run == null) {
            if (lastLaunch > 0) {
                state = "No status from the last start";
                color = Ui.WARN;
                message.append("The runtime stopped before it wrote launch/status.json. Check the runtime log.");
                time = "Started " + formatTime(lastLaunch);
            } else {
                state = "Not started yet";
                color = Ui.MUTED;
            }
        } else {
            String name = run.state.isEmpty() ? "unknown" : run.state;
            state = name.substring(0, 1).toUpperCase(Locale.ROOT) + name.substring(1)
                    + (run.stage.isEmpty() ? "" : " · stage " + run.stage)
                    + (run.renderer.isEmpty() ? "" : " · " + run.renderer);
            if (RunStatus.STATE_ERROR.equals(run.state)) {
                color = Ui.BAD;
            } else if (RunStatus.STATE_LOADING.equals(run.state)) {
                color = Ui.WARN;
            } else {
                color = Ui.GOOD;
            }
            message.append(run.message);
            if (!run.isTerminal() && !snapshot.gameAlive) {
                color = Ui.BAD;
                appendLine(message, "The game process ended without a final status; it probably crashed during "
                        + "this stage. Check the runtime log.");
            }
            if (lastLaunch > 0 && run.fileTime > 0 && run.fileTime + 2000 < lastLaunch) {
                appendLine(message, "(This status is from an earlier start.)");
            }
            counters = run.counters();
            time = run.timestamp + (run.versionName.isEmpty() ? "" : " · " + run.versionName);
        }
        mRunState.setText(state);
        mRunState.setTextColor(color);
        mRunMessage.setText(message);
        mRunMessage.setVisibility(message.length() == 0 ? View.GONE : View.VISIBLE);
        mRunCounters.setText(counters);
        mRunCounters.setVisibility(counters.isEmpty() ? View.GONE : View.VISIBLE);
        mRunTime.setText(time);
        mRunTime.setVisibility(time.isEmpty() ? View.GONE : View.VISIBLE);
    }

    private void renderData(Snapshot snapshot) {
        DataStatus status = snapshot.status;
        DataManifest manifest = snapshot.manifest;
        StringBuilder summary = new StringBuilder();
        if (status.fafVersion != null) {
            summary.append("FAF ").append(status.fafVersion.version);
            if (!status.fafVersion.source.isEmpty()) {
                summary.append(" (").append(status.fafVersion.source).append(')');
            }
        } else {
            summary.append("FAF ").append(manifest.fafVersion).append(" (no version.json yet)");
        }
        summary.append(" · ").append(status.fafPresentCount()).append('/').append(status.faf.size())
                .append(" files");
        for (DataManifest.Tier tier : DataManifest.Tier.values()) {
            DataStatus.TierSummary tiers = status.tiers.get(tier);
            summary.append('\n').append(tier.label()).append(": ")
                    .append(tiers.complete() ? "complete" : "incomplete")
                    .append(" · FAF ").append(tiers.fafPresent).append('/').append(tiers.fafTotal)
                    .append(" · SCFA ").append(tiers.entriesComplete).append('/').append(tiers.entriesTotal);
        }
        summary.append("\nRequired + recommended: ").append(FileOps.formatBytes(status.presentBytes))
                .append(" of about ").append(FileOps.formatBytes(status.expectedBytes));
        mDataSummary.setText(summary);
        mDataSummary.setTextColor(status.requiredComplete() ? Ui.BODY : Ui.WARN);

        StringBuilder details = new StringBuilder("FAF");
        for (DataStatus.FafFileState file : status.faf) {
            details.append('\n').append(mark(file.present, false, file.file.tier)).append(' ')
                    .append(file.file.name).append("  ").append(FileOps.formatBytes(file.file.size))
                    .append("  ").append(file.file.tier.id);
        }
        details.append("\n\nSCFA");
        for (DataStatus.EntryState entry : status.scfa) {
            details.append('\n').append(mark(entry.complete, entry.presentCount > 0, entry.entry.tier)).append(' ')
                    .append(entry.entry.label).append("  ").append(entry.counts()).append("  ")
                    .append(entry.entry.tier.id);
        }
        details.append("\n\nVault");
        for (DataStatus.EntryState entry : status.vault) {
            details.append('\n').append(entry.presentCount > 0 ? "✓" : "·").append(' ').append(entry.entry.label)
                    .append("  ").append(entry.counts());
        }
        mDataDetails.setText(details);
    }

    private static String mark(boolean complete, boolean partial, DataManifest.Tier tier) {
        if (complete) {
            return "✓";
        }
        if (partial) {
            return "◐";
        }
        return tier == DataManifest.Tier.OPTIONAL ? "·" : "✗";
    }

    private void renderDownloadHint() {
        if (mSnapshot == null) {
            return;
        }
        long bytes = mSnapshot.manifest.fafBytes(mSettings.importRecommended());
        mDownloadHint.setText("About " + FileOps.formatBytes(bytes) + " from FAF's content server"
                + (mSettings.importRecommended() ? "" : " (required files only: 'Recommended' is off)")
                + ", checked against the manifest's SHA-256. Interrupted downloads continue where they stopped.");
    }

    private void updateButtons() {
        ImportService.State job = ImportService.state();
        boolean busy = job.running || mStarting;
        boolean ready = mSnapshot != null;
        mStartButton.setEnabled(ready && !busy && mSnapshot.nativeProblem == null
                && mSnapshot.status.requiredComplete());
        mDownloadButton.setEnabled(ready && !busy);
        mImportFafButton.setEnabled(ready && !busy);
        mImportScfaButton.setEnabled(ready && !busy);
        mImportVaultButton.setEnabled(ready && !busy);
        mVerifyButton.setEnabled(ready && !busy);
        boolean runner = ready && mSnapshot.runnerProblem == null;
        mPickReplayButton.setEnabled(ready && !busy && !mReplayBusy);
        mSelfTestButton.setEnabled(runner && !busy && !mReplayBusy);
        mRunTestButton.setEnabled(runner && !busy && !mReplayBusy && mSnapshot.replay != null
                && mSnapshot.replayCheck != null && mSnapshot.replayCheck.ok());
        boolean haveRun = ready && mSnapshot.lastRun != null && !isCurrentRun(mSnapshot.lastRun);
        mSaveZipButton.setEnabled(haveRun && !mReplayBusy);
        mCopySummaryButton.setEnabled(haveRun);
        mRunOutputButton.setEnabled(haveRun);
        mSaveAllZipButton.setEnabled(ready && mSnapshot.runCount > 0 && !mReplayBusy && !mReplayJobRunning);
        mDeviceProbeButton.setEnabled(ready && mSnapshot.hasDeviceProbe && !busy && !mReplayBusy);
        mProbeImageButton.setEnabled(ready && !probeImages(mSnapshot.lastProbe).isEmpty());
        mOptimizedBox.setEnabled(ready && mSnapshot.hasOptimized);
    }

    // ----------------------------------------------------------- replay test

    private void renderReplay() {
        Snapshot snapshot = mSnapshot;
        if (snapshot == null || mReplayInfo == null) {
            return;
        }
        mReplayRunnerHint.setText(snapshot.runnerProblem != null ? snapshot.runnerProblem : "");
        mReplayRunnerHint.setVisibility(snapshot.runnerProblem != null ? View.VISIBLE : View.GONE);

        ReplayFiles.Info replay = snapshot.replay;
        StringBuilder notes = new StringBuilder();
        int notesColor = Ui.MUTED;
        if (replay == null) {
            mReplayInfo.setText("No replay selected. Open a downloaded .fafreplay with this app, or pick one.");
        } else {
            StringBuilder info = new StringBuilder(replay.describe());
            info.append("\nsha256 ").append(replay.sha256());
            ReplayRefs.Ref ref = snapshot.refs.find(replay.sha256(), replay.engineFileSha256());
            if (ref != null) {
                info.append("\nreference: chain ").append(ref.chains.isEmpty() ? "none" : String.join(" / ", ref.chains))
                        .append(ref.gameOverBeat >= 0 ? ", game over at beat " + ref.gameOverBeat : "");
            } else {
                info.append("\nreference: none for this file").append(snapshot.refs.error() != null ? " ("
                        + snapshot.refs.error() + ")" : snapshot.refs.size() > 0 ? " (known: "
                        + String.join(", ", snapshot.refs.ids()) + ")" : "");
            }
            if (!replay.runnerInfoError().isEmpty()) {
                info.append("\nrunner: ").append(replay.runnerInfoError());
            }
            mReplayInfo.setText(info);
            ReplayFiles.Check check = snapshot.replayCheck;
            if (check != null) {
                for (String problem : check.problems) {
                    appendLine(notes, problem);
                    notesColor = Ui.BAD;
                }
                for (String warning : check.warnings) {
                    appendLine(notes, warning);
                    if (notesColor != Ui.BAD) {
                        notesColor = Ui.WARN;
                    }
                }
            }
        }
        if (mSettings.replayNoArena()) {
            appendLine(notes, "Advanced: the replay runs without the low arena (FAF_LOWARENA=0) and is expected to "
                    + "crash.");
            if (notesColor == Ui.MUTED) {
                notesColor = Ui.WARN;
            }
        }
        boolean optimized = mSettings.replayOptimized() && snapshot.hasOptimized;
        if (mSettings.replayOptimized() && !snapshot.hasOptimized) {
            appendLine(notes, "Advanced: this APK has no -O2 build; the test uses the -O0 engine.");
        } else if (optimized) {
            appendLine(notes, "Advanced: the optimised engine (-O2 build) runs.");
        }
        if (mSettings.replaySpeedExperiment()) {
            appendLine(notes, "Advanced: speed experiment on (timer slack 1 ns, " + (mSettings.replayAffinityBig()
                    ? "only the biggest cores" : "no little cores") + ").");
        }
        mReplayNotes.setText(notes);
        mReplayNotes.setTextColor(notesColor);
        mReplayNotes.setVisibility(notes.length() == 0 ? View.GONE : View.VISIBLE);
        String variant = (optimized ? "-O2" : "") + (mSettings.replaySpeedExperiment() ? (optimized ? ", " : "")
                + "speed experiment" : "");
        mRunTestButton.setText((mSettings.replaySkipSelfTest() ? "Run the replay" : "Run test (self-test + replay)")
                + (variant.isEmpty() ? "" : " · " + variant));
        mDeviceProbeButton.setText(snapshot.hasDeviceProbe ? "Device probe" : "Device probe (not in this APK)");
        renderProbe(snapshot);

        JSONObject run = snapshot.lastRun;
        String jobMessage = mSettings.lastReplayMessage();
        mReplayLines.removeAllViews();
        if (run == null && jobMessage == null) {
            mReplayVerdict.setVisibility(View.GONE);
            mReplayRunTime.setVisibility(View.GONE);
            return;
        }
        boolean newerMessage = jobMessage != null && (run == null || !jobMessage.equals(run.optString("headline")))
                && mSettings.lastReplayTime() > 0;
        if (run != null && isCurrentRun(run)) {
            // The run the job is doing right now: its provisional result only matters if the app is ended.
            mReplayVerdict.setText("Run " + run.optString("run") + " is in progress…");
            mReplayVerdict.setTextColor(Ui.MUTED);
            mReplayVerdict.setVisibility(View.VISIBLE);
            mReplayRunTime.setVisibility(View.GONE);
        } else if (run != null) {
            String verdict = run.optString("verdict");
            // While a test runs, the result below is the previous run's; say so.
            mReplayVerdict.setText(mReplayJobRunning ? "Previous run: " + run.optString("headline")
                    : run.optString("headline"));
            mReplayVerdict.setTextColor(mReplayJobRunning ? Ui.MUTED : ReplayTest.VERDICT_PASS.equals(verdict)
                    || ReplayTest.VERDICT_EXPECTED_CRASH.equals(verdict) ? Ui.GOOD
                    : ReplayTest.VERDICT_FAIL.equals(verdict) ? Ui.BAD : Ui.WARN);
            mReplayVerdict.setVisibility(View.VISIBLE);
            JSONArray lines = run.optJSONArray("lines");
            for (int i = 0; lines != null && i < lines.length(); ++i) {
                JSONObject line = lines.optJSONObject(i);
                if (line == null) {
                    continue;
                }
                TextView view = mUi.text(line.optString("text"), 13, toneColor(line.optString("tone")));
                view.setTextIsSelectable(true);
                mReplayLines.addView(view, mUi.matchWrap(3));
            }
            mReplayRunTime.setText("Run " + run.optString("run") + " · " + (run.optBoolean(ReplayTest.IN_PROGRESS)
                    ? "started " + run.optString("started") + ", did not finish" : run.optString("ended"))
                    + " · runs/" + run.optString("run"));
            mReplayRunTime.setVisibility(View.VISIBLE);
        } else {
            mReplayVerdict.setVisibility(View.GONE);
            mReplayRunTime.setVisibility(View.GONE);
        }
        if (newerMessage) {
            TextView view = mUi.text("Last test (" + formatTime(mSettings.lastReplayTime()) + "): " + jobMessage, 13,
                    mSettings.lastReplayOk() ? Ui.GOOD : Ui.BAD);
            view.setTextIsSelectable(true);
            mReplayLines.addView(view, 0, mUi.matchWrap(3));
        }
    }

    /**
     * Whether {@code run} is the provisional result of the test this process is running now (and not of a run
     * that an earlier process of the app left unfinished: that one shows as INTERRUPTED).
     */
    private boolean isCurrentRun(JSONObject run) {
        return mReplayJobRunning && run.optBoolean(ReplayTest.IN_PROGRESS)
                && run.optInt(ReplayTest.APP_PID, -1) == Process.myPid();
    }

    private static int toneColor(String tone) {
        switch (tone) {
            case "good":
                return Ui.GOOD;
            case "bad":
                return Ui.BAD;
            case "warn":
                return Ui.WARN;
            default:
                return Ui.BODY;
        }
    }

    private void onReplayState(ImportService.State state) {
        if (state.running != mReplayJobRunning) {
            mReplayJobRunning = state.running;
            renderReplay();
        }
        if (state.running) {
            mReplayJobPanel.setVisibility(View.VISIBLE);
            mReplayJobTitle.setText(state.cancelling ? state.title + " (cancelling…)" : state.title);
            Progress.Snapshot progress = state.progress;
            int permille = progress != null ? progress.permille() : -1;
            mReplayJobProgress.setIndeterminate(permille < 0);
            if (permille >= 0) {
                mReplayJobProgress.setProgress(permille);
            }
            if (progress != null) {
                mReplayJobDetail.setText(progress.phase + (progress.item.isEmpty() ? "" : "\n" + progress.item));
            }
            mReplayCancelButton.setEnabled(!state.cancelling);
        } else {
            mReplayJobPanel.setVisibility(View.GONE);
        }
    }

    private void pickReplay() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
        try {
            startActivityForResult(intent, REQUEST_REPLAY_FILE);
        } catch (ActivityNotFoundException e) {
            toast("This device has no file picker (Files app).");
        }
    }

    private void importReplay(final Uri uri) {
        mReplayBusy = true;
        updateButtons();
        toast("Reading the replay…");
        final Context app = getApplicationContext();
        // As in ReplayInboxActivity: no runner next to a running job; the test reads the replay when it starts.
        final boolean jobRunning = ImportService.state().running;
        Background.run(() -> {
            ReplayFiles.Info info = ReplayFiles.importUri(app, uri);
            mLog.log("replay: imported " + info.fileName() + " (" + info.format() + ", sha256 " + info.sha256() + ")"
                    + (jobRunning ? "; not analyzed now, a job is running" : ""));
            if (!jobRunning) {
                info = ReplayFiles.analyze(app, info, new Cancellation());
            }
            new Settings(app).setReplayStem(info.stem());
            return info;
        }, (info, error) -> {
            mReplayBusy = false;
            if (isDestroyed()) {
                return;
            }
            if (error != null) {
                mLog.log("replay: import failed: " + describe(error));
                toast("Cannot use this file: " + describe(error));
            } else {
                toast("Replay " + info.fileName() + " selected.");
            }
            refresh();
        });
    }

    private ReplayTest.Options replayOptions(boolean replay) {
        ReplayTest.Options options = new ReplayTest.Options();
        options.replayStem = mSettings.replayStem();
        options.replay = replay && options.replayStem != null;
        options.selfTest = !replay || !mSettings.replaySkipSelfTest();
        options.repeat = mSettings.replayRepeat();
        options.interlocked = mSettings.replayInterlocked();
        options.lowArena = !mSettings.replayNoArena();
        options.optimized = mSettings.replayOptimized() && mSnapshot != null && mSnapshot.hasOptimized;
        options.speedExperiment = mSettings.replaySpeedExperiment();
        options.affinity = mSettings.replayAffinityBig() ? ReplayTest.AFFINITY_BIG : ReplayTest.AFFINITY_FAST;
        return options;
    }

    /** The device probe's own block: the last probe run's headline and lines, unless the block above shows it. */
    private void renderProbe(Snapshot snapshot) {
        mProbeLines.removeAllViews();
        JSONObject probe = snapshot.lastProbe;
        if (probe == null) {
            mProbeVerdict.setText(snapshot.hasDeviceProbe ? "Not run yet." : "This APK has no device probe.");
            mProbeVerdict.setTextColor(Ui.MUTED);
            return;
        }
        boolean shownAbove = snapshot.lastRunName != null && snapshot.lastRunName.equals(snapshot.lastProbeName);
        if (shownAbove || isCurrentRun(probe)) {
            mProbeVerdict.setText(isCurrentRun(probe) ? "Running…" : "The last run above is the device probe ("
                    + probe.optString("run") + ").");
            mProbeVerdict.setTextColor(Ui.MUTED);
            return;
        }
        String verdict = probe.optString("verdict");
        mProbeVerdict.setText(probe.optString("headline"));
        mProbeVerdict.setTextColor(ReplayTest.VERDICT_PASS.equals(verdict) ? Ui.GOOD
                : ReplayTest.VERDICT_FAIL.equals(verdict) ? Ui.BAD : Ui.WARN);
        JSONArray lines = probe.optJSONArray("lines");
        for (int i = 0; lines != null && i < lines.length(); ++i) {
            JSONObject line = lines.optJSONObject(i);
            if (line != null) {
                TextView view = mUi.text(line.optString("text"), 13, toneColor(line.optString("tone")));
                view.setTextIsSelectable(true);
                mProbeLines.addView(view, mUi.matchWrap(3));
            }
        }
        TextView when = mUi.hint("Run " + probe.optString("run") + " · runs/" + probe.optString("run"));
        mProbeLines.addView(when, mUi.matchWrap(4));
    }

    /** The PNG files a probe run's result.json names (run-relative), checked to be plain names. */
    private static List<String> probeImages(JSONObject run) {
        List<String> out = new ArrayList<>();
        JSONArray steps = run != null ? run.optJSONArray("steps") : null;
        for (int i = 0; steps != null && i < steps.length(); ++i) {
            JSONObject step = steps.optJSONObject(i);
            JSONObject probe = step != null ? step.optJSONObject("device_probe") : null;
            JSONArray images = probe != null ? probe.optJSONArray("images") : null;
            for (int j = 0; images != null && j < images.length(); ++j) {
                String name = images.optString(j, "");
                if (name.matches("[A-Za-z0-9._-]+\\.png") && !out.contains(name)) {
                    out.add(name);
                }
            }
        }
        return out;
    }

    private void showProbeImages() {
        Snapshot snapshot = mSnapshot;
        if (snapshot == null || snapshot.lastProbe == null) {
            return;
        }
        final String run = snapshot.lastProbe.optString("run");
        final List<String> names = probeImages(snapshot.lastProbe);
        final Context app = getApplicationContext();
        Background.run(() -> {
            DataRoot root = AppInfo.dataRoot(app);
            List<Object[]> images = new ArrayList<>();
            for (String name : names) {
                File file = root.file(ReplayTest.RUNS_DIR + "/" + run + "/" + name);
                Bitmap bitmap = file.isFile() && file.length() < 16L * 1024 * 1024
                        ? BitmapFactory.decodeFile(file.getAbsolutePath()) : null;
                images.add(new Object[] {name, bitmap});
            }
            return images;
        }, (images, error) -> {
            if (isDestroyed() || isFinishing()) {
                return;
            }
            LinearLayout column = new LinearLayout(this);
            column.setOrientation(LinearLayout.VERTICAL);
            column.setPadding(mUi.dp(20), mUi.dp(8), mUi.dp(20), mUi.dp(8));
            if (error != null) {
                column.addView(mUi.body("Cannot read the images: " + describe(error)));
            } else {
                for (Object[] image : images) {
                    column.addView(mUi.hint((String) image[0]), mUi.matchWrap(8));
                    if (image[1] == null) {
                        column.addView(mUi.body("(not readable)"), mUi.matchWrap(2));
                        continue;
                    }
                    ImageView view = new ImageView(this);
                    view.setImageBitmap((Bitmap) image[1]);
                    view.setAdjustViewBounds(true);
                    view.setScaleType(ImageView.ScaleType.FIT_START);
                    // Nearest-neighbour: the pattern's pixels are the measurement.
                    view.getDrawable().setFilterBitmap(false);
                    column.addView(view, new LinearLayout.LayoutParams(mUi.dp(256), ViewGroup.LayoutParams.WRAP_CONTENT));
                }
                column.addView(mUi.hint("Both images should look the same: red rises from left to right, green from "
                        + "top to bottom (each API's own row order), with a blue checkerboard of 32-pixel squares. "
                        + "deviceprobe.json says whether each one matched the pattern exactly."), mUi.matchWrap(8));
            }
            ScrollView scroll = new ScrollView(this);
            scroll.addView(column);
            showDialog(new AlertDialog.Builder(this).setTitle("Device probe " + run).setView(scroll)
                    .setPositiveButton("Close", null).create(), null);
        });
    }

    private void saveAllRunsZip() {
        mPendingZipRun = ALL_RUNS;
        String stamp = new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(new Date());
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
                .setType("application/zip").putExtra(Intent.EXTRA_TITLE, "fafre-runs-" + stamp + ".zip");
        try {
            startActivityForResult(intent, REQUEST_SAVE_ALL_ZIP);
        } catch (ActivityNotFoundException e) {
            mPendingZipRun = null;
            toast("This device has no file picker (Files app).");
        }
    }

    private void saveRunZip() {
        Snapshot snapshot = mSnapshot;
        if (snapshot == null || snapshot.lastRun == null) {
            return;
        }
        mPendingZipRun = snapshot.lastRun.optString("run");
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
                .setType("application/zip").putExtra(Intent.EXTRA_TITLE, "fafre-run-" + mPendingZipRun + ".zip");
        try {
            startActivityForResult(intent, REQUEST_SAVE_ZIP);
        } catch (ActivityNotFoundException e) {
            mPendingZipRun = null;
            toast("This device has no file picker (Files app).");
        }
    }

    private void writeRunZip(final Uri target, final String runName) {
        mReplayBusy = true;
        updateButtons();
        final Context app = getApplicationContext();
        Background.run(() -> {
            DataRoot root = AppInfo.dataRoot(app);
            boolean all = ALL_RUNS.equals(runName);
            File runDir = all ? root.file(ReplayTest.RUNS_DIR) : root.file(ReplayTest.RUNS_DIR + "/" + runName);
            if (!runDir.isDirectory()) {
                throw new IOException(all ? "there are no runs" : "the run directory " + runName + " is gone");
            }
            // "wt" truncates; some providers (cloud storage) refuse that mode. The document was just created
            // by ACTION_CREATE_DOCUMENT, so plain "w" writes the same bytes there.
            OutputStream out;
            try {
                out = app.getContentResolver().openOutputStream(target, "wt");
            } catch (IOException | IllegalArgumentException | UnsupportedOperationException | SecurityException e) {
                mLog.log("replay: the zip target refused mode wt (" + e + "); trying w");
                out = null;
            }
            if (out == null) {
                out = app.getContentResolver().openOutputStream(target, "w");
            }
            if (out == null) {
                throw new IOException("cannot write to the chosen file");
            }
            File launcherLog = root.find(LauncherLog.LOGS_DIR + "/" + LauncherLog.FILE_NAME);
            try (OutputStream stream = out) {
                return all ? RunZip.writeAll(runDir, launcherLog, stream) : RunZip.write(runDir, launcherLog, stream);
            }
        }, (count, error) -> {
            mReplayBusy = false;
            if (isDestroyed()) {
                return;
            }
            if (error != null) {
                mLog.log("replay: saving the zip failed: " + describe(error));
                toast("Saving the zip failed: " + describe(error));
            } else {
                boolean all = ALL_RUNS.equals(runName);
                mLog.log("replay: saved " + (all ? "all runs" : "run " + runName) + " as a zip (" + count + " files)");
                toast("Saved " + (all ? "all runs" : "the run") + " (" + count + " files). Send that zip to the "
                        + "developers.");
            }
            updateButtons();
        });
    }

    private void copySummary() {
        Snapshot snapshot = mSnapshot;
        if (snapshot == null || snapshot.lastRun == null) {
            return;
        }
        String text = snapshot.lastRun.optString("summary_text", snapshot.lastRun.optString("headline"));
        ClipboardManager clipboard = getSystemService(ClipboardManager.class);
        if (clipboard != null) {
            clipboard.setPrimaryClip(ClipData.newPlainText("faf-re replay test", text));
            toast("Summary copied.");
        }
    }

    /** The runner output of the last run's most telling step: the replay, else the last one that ran. */
    private void showRunOutput() {
        Snapshot snapshot = mSnapshot;
        if (snapshot == null || snapshot.lastRun == null) {
            return;
        }
        JSONArray steps = snapshot.lastRun.optJSONArray("steps");
        String out = null;
        for (int i = 0; steps != null && i < steps.length(); ++i) {
            JSONObject step = steps.optJSONObject(i);
            if (step != null && !step.optString("out").isEmpty()) {
                if (out == null || step.optString("name").equals("replay") || !out.contains("-replay")) {
                    out = step.optString("out");
                }
            }
        }
        String run = snapshot.lastRun.optString("run");
        showLog(ReplayTest.RUNS_DIR + "/" + run + "/" + (out != null ? out : ReplayTest.SUMMARY_TXT));
    }

    private void showLicenses() {
        final Context app = getApplicationContext();
        Background.run(() -> {
            StringBuilder text = new StringBuilder("Third-party code in this APK, and the license texts below:\n\n"
                    + "libfafrunner.so, libfafrunner_o2.so (the replay runner):\n"
                    + "  dlmalloc (port/engine/lowarena): Doug Lea, public domain.\n"
                    + "  Zstandard decoder: zstd.txt.\n"
                    + "libfaf_android.so (the game's native code):\n"
                    + "  Diligent Engine core: DiligentCore.txt (Apache License 2.0).\n"
                    + "  glslang: glslang.txt. SPIRV-Tools: SPIRV-Tools.txt (Apache License 2.0).\n"
                    + "  SPIR-V grammar tables from SPIRV-Headers: SPIRV-Headers.txt.\n"
                    + "  SPIRV-Cross: SPIRV-Cross.txt (Apache License 2.0). volk: volk.txt.\n"
                    + "libfafdeviceprobe.so (the device probe):\n"
                    + "  glslang, SPIRV-Tools and the SPIRV-Headers tables, as above.\n");
            String[] names = app.getAssets().list("licenses");
            if (names == null || names.length == 0) {
                text.append("\n(no license files in this APK)\n");
            } else {
                for (String name : names) {
                    try (InputStream in = app.getAssets().open("licenses/" + name)) {
                        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
                        byte[] buffer = new byte[8192];
                        int n;
                        while ((n = in.read(buffer)) > 0) {
                            bytes.write(buffer, 0, n);
                        }
                        text.append("\n--- ").append(name).append(" ---\n")
                                .append(new String(bytes.toByteArray(), StandardCharsets.UTF_8));
                    }
                }
            }
            return text.toString();
        }, (text, error) -> {
            if (isDestroyed() || isFinishing()) {
                return;
            }
            TextView view = mUi.mono(error != null ? "Cannot read the licenses: " + describe(error) : text);
            view.setPadding(mUi.dp(20), mUi.dp(8), mUi.dp(20), mUi.dp(8));
            ScrollView scroll = new ScrollView(this);
            scroll.addView(view);
            showDialog(new AlertDialog.Builder(this).setTitle("Licenses").setView(scroll)
                    .setPositiveButton("Close", null).create(), null);
        });
    }

    // ---------------------------------------------------------------- import

    @Override
    public void onImportState(ImportService.State state) {
        if (ImportService.KIND_REPLAY.equals(state.kind)) {
            onReplayState(state);
            mJobPanel.setVisibility(View.GONE);
            // The import card keeps showing the last transfer's outcome (from Settings).
            renderJobResult(ImportService.State.IDLE);
            if (!state.running && mJobWasRunning) {
                // The test wrote its result (and fa_path.lua); read it.
                refresh();
            }
            mJobWasRunning = state.running;
            renderStart();
            updateButtons();
            return;
        }
        onReplayState(ImportService.State.IDLE);
        if (state.running) {
            mJobPanel.setVisibility(View.VISIBLE);
            mJobTitle.setText(state.cancelling ? state.title + " (cancelling…)" : state.title);
            Progress.Snapshot progress = state.progress;
            int permille = progress != null ? progress.permille() : -1;
            mJobProgress.setIndeterminate(permille < 0);
            if (permille >= 0) {
                mJobProgress.setProgress(permille);
            }
            if (progress != null) {
                String item = progress.item.isEmpty() ? "" : "\n" + progress.item;
                String counts = progress.describe();
                mJobDetail.setText(progress.phase + (counts.isEmpty() ? "" : " · " + counts) + item);
            }
            mCancelButton.setEnabled(!state.cancelling);
            mJobResult.setVisibility(View.GONE);
        } else {
            mJobPanel.setVisibility(View.GONE);
            renderJobResult(state);
            if (mJobWasRunning) {
                // A transfer just ended: the data root changed.
                refresh();
            }
        }
        mJobWasRunning = state.running;
        renderStart();
        updateButtons();
    }

    private void renderJobResult(ImportService.State state) {
        String title = state.result != null ? state.title : mSettings.lastJobTitle();
        String message = state.result != null ? state.result : mSettings.lastJobMessage();
        boolean ok = state.result != null ? state.resultOk : mSettings.lastJobOk();
        if (message == null) {
            mJobResult.setVisibility(View.GONE);
            return;
        }
        String when = mSettings.lastJobTime() > 0 ? " (" + formatTime(mSettings.lastJobTime()) + ")" : "";
        mJobResult.setText((title != null ? title : "Last transfer") + when + ": " + message);
        mJobResult.setTextColor(ok ? Ui.GOOD : Ui.BAD);
        mJobResult.setVisibility(View.VISIBLE);
    }

    private void confirmDownload() {
        if (mSnapshot == null) {
            return;
        }
        boolean recommended = mSettings.importRecommended();
        DataManifest manifest = mSnapshot.manifest;
        String message = "Downloads up to " + FileOps.formatBytes(manifest.fafBytes(recommended)) + " of FAF "
                + manifest.fafVersion + " game data from content.faforever.com into the data folder. Files that "
                + "are already present are checked and kept. Use Wi-Fi if your data plan is limited.";
        if (!recommended) {
            message += "\n\n'Recommended for normal play' is off, so only the files needed for the main menu are "
                    + "downloaded.";
        }
        showDialog(new AlertDialog.Builder(this)
                .setTitle("Download FAF files?")
                .setMessage(message)
                .setPositiveButton("Download", (dialog, which) -> request(ACTION_DOWNLOAD, null))
                .setNegativeButton("Not now", null)
                .create(), null);
    }

    private void pickTree(int requestCode, String kind) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        // Older DocumentsUI versions hide internal storage unless asked.
        intent.putExtra("android.content.extra.SHOW_ADVANCED", true);
        String previous = mSettings.tree(kind);
        if (previous != null) {
            try {
                Uri tree = Uri.parse(previous);
                intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI,
                        DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree)));
            } catch (IllegalArgumentException ignored) {
                // A stale or foreign link: start the picker at its default location.
            }
        }
        try {
            startActivityForResult(intent, requestCode);
        } catch (ActivityNotFoundException e) {
            toast("This device has no folder picker (Files app).");
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_REPLAY_FILE) {
            if (resultCode == RESULT_OK && data != null && data.getData() != null) {
                importReplay(data.getData());
            }
            return;
        }
        if (requestCode == REQUEST_SAVE_ZIP || requestCode == REQUEST_SAVE_ALL_ZIP) {
            String run = mPendingZipRun;
            mPendingZipRun = null;
            if (resultCode == RESULT_OK && data != null && data.getData() != null && run != null) {
                writeRunZip(data.getData(), run);
            }
            return;
        }
        String kind;
        String action;
        if (requestCode == REQUEST_SCFA_TREE) {
            kind = Settings.TREE_SCFA;
            action = ACTION_IMPORT_SCFA;
        } else if (requestCode == REQUEST_FAF_TREE) {
            kind = Settings.TREE_FAF;
            action = ACTION_IMPORT_FAF;
        } else if (requestCode == REQUEST_VAULT_TREE) {
            kind = Settings.TREE_VAULT;
            action = ACTION_IMPORT_VAULT;
        } else {
            return;
        }
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            return;
        }
        Uri tree = data.getData();
        try {
            // Persisted so a re-import or a restarted service can still read the folder.
            getContentResolver().takePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException e) {
            mLog.log("could not keep access to " + tree + ": " + e.getMessage());
        }
        String previous = mSettings.tree(kind);
        if (previous != null && !previous.equals(tree.toString())) {
            try {
                getContentResolver().releasePersistableUriPermission(Uri.parse(previous),
                        Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (SecurityException ignored) {
                // Already gone; nothing to release.
            }
        }
        mSettings.setTree(kind, tree.toString());
        request(action, tree.toString());
    }

    /** Asks for the notification permission first (13+); the job runs either way. */
    private void request(String action, String uri) {
        if (Build.VERSION.SDK_INT >= 33
                && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            mPendingAction = action;
            mPendingUri = uri;
            requestPermissions(new String[] {Manifest.permission.POST_NOTIFICATIONS}, REQUEST_NOTIFICATIONS);
            return;
        }
        perform(action, uri);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != REQUEST_NOTIFICATIONS || mPendingAction == null) {
            return;
        }
        String action = mPendingAction;
        String uri = mPendingUri;
        mPendingAction = null;
        mPendingUri = null;
        // Without the permission the transfer still runs; Android lists it in the task manager.
        perform(action, uri);
    }

    private void perform(String action, String uri) {
        if (ImportService.state().running) {
            toast(ImportService.KIND_REPLAY.equals(ImportService.state().kind) ? "The replay test is running."
                    : "Another transfer is running.");
            return;
        }
        boolean recommended = mSettings.importRecommended();
        Intent intent;
        if (ACTION_DEVICE_PROBE.equals(action)) {
            ReplayTest.Options options = new ReplayTest.Options();
            options.deviceProbe = true;
            options.selfTest = false;
            options.replay = false;
            mLog.log("device probe requested");
            intent = ImportService.replayTest(this, options);
        } else if (ACTION_REPLAY_TEST.equals(action) || ACTION_SELF_TEST.equals(action)) {
            ReplayTest.Options options = replayOptions(ACTION_REPLAY_TEST.equals(action));
            if (ACTION_REPLAY_TEST.equals(action) && !options.replay) {
                toast("Pick a replay first.");
                return;
            }
            mLog.log("replay test requested: " + (options.selfTest ? "self-test" : "")
                    + (options.replay ? " replay " + options.replayStem : "") + (options.optimized ? " -O2" : "")
                    + (options.speedExperiment ? " speed experiment (" + options.affinity + ")" : ""));
            intent = ImportService.replayTest(this, options);
        } else if (ACTION_DOWNLOAD.equals(action)) {
            intent = ImportService.downloadFaf(this, recommended);
        } else if (ACTION_VERIFY.equals(action)) {
            intent = ImportService.verifyFaf(this);
        } else if (uri == null) {
            return;
        } else if (ACTION_IMPORT_SCFA.equals(action)) {
            intent = ImportService.importScfa(this, Uri.parse(uri), recommended, mSettings.importOptional());
        } else if (ACTION_IMPORT_FAF.equals(action)) {
            intent = ImportService.importFaf(this, Uri.parse(uri), recommended);
        } else if (ACTION_IMPORT_VAULT.equals(action)) {
            intent = ImportService.importVault(this, Uri.parse(uri));
        } else {
            return;
        }
        try {
            startForegroundService(intent);
        } catch (RuntimeException e) {
            mLog.log("could not start the import service: " + e);
            toast("Android did not allow the job to start: " + e.getMessage());
        }
    }

    // ------------------------------------------------------------------ start

    private void startGame() {
        final Snapshot snapshot = mSnapshot;
        if (mStarting || snapshot == null || snapshot.nativeProblem != null || !snapshot.status.requiredComplete()
                || ImportService.state().running) {
            return;
        }
        mStarting = true;
        updateButtons();
        final Context app = getApplicationContext();
        final Settings settings = mSettings;
        final String renderer = settings.renderer();
        final boolean noMovie = settings.noMovie();
        final boolean noSound = settings.noSound();
        final String extra = settings.extraArgs();
        final String versionName = mVersionName;
        Background.run(() -> prepareLaunch(app, settings, renderer, noMovie, noSound, extra, versionName),
                (argv, error) -> {
                    mStarting = false;
                    if (error != null) {
                        String message = describe(error);
                        mLog.log("start failed: " + message);
                        if (!isDestroyed()) {
                            toast("Cannot start: " + message);
                            refresh();
                        }
                        return;
                    }
                    Intent intent = new Intent(app, GameActivity.class).putExtra(LaunchArgs.EXTRA_ARGV, argv);
                    if (isDestroyed()) {
                        // Rotated while preparing: start from the application context.
                        app.startActivity(intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
                    } else {
                        startActivity(intent);
                        updateButtons();
                    }
                });
    }

    /**
     * Writes fa_path.lua, clears the previous status.json and returns argv.
     * Runs on the background thread.
     */
    private static String[] prepareLaunch(Context app, Settings settings, String renderer, boolean noMovie,
            boolean noSound, String extra, String versionName) throws IOException {
        LauncherLog log = LauncherLog.get(app);
        DataManifest manifest = AppInfo.manifest(app);
        DataRoot root = AppInfo.dataRoot(app);

        // A finished run leaves its process cached with the old native globals;
        // every start gets a fresh one.
        int pid = AppInfo.gameProcessPid(app);
        if (pid > 0) {
            log.log("start: ending the previous game process " + pid);
            Process.killProcess(pid);
            long deadline = System.currentTimeMillis() + GAME_EXIT_WAIT_MS;
            while (AppInfo.gameProcessPid(app) > 0 && System.currentTimeMillis() < deadline) {
                try {
                    Thread.sleep(50);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    break;
                }
            }
        }

        DataStatus status = DataStatus.check(manifest, root, log);
        if (!status.requiredComplete()) {
            throw new IOException("missing required game data: " + String.join(", ", status.missingRequired()));
        }
        File init = root.find(manifest.initScript());
        if (init == null || !init.isFile()) {
            throw new IOException(manifest.initScript() + " is missing");
        }
        for (String dir : new String[] {manifest.layout.logs, manifest.layout.launch, manifest.layout.localAppData,
            manifest.layout.documents, manifest.layout.vault}) {
            File directory = root.prepare(dir);
            if (!directory.isDirectory() && !directory.mkdir() && !directory.isDirectory()) {
                throw new IOException("cannot create " + directory);
            }
        }
        FafVersion installed = FafVersion.read(root.file(manifest.layout.fafVersion));
        int fafVersion = installed != null ? installed.version : manifest.fafVersion;
        File faPath = FaPathWriter.write(manifest, root, fafVersion, versionName);

        File statusFile = root.file(statusPath(manifest));
        if (statusFile.exists() && !statusFile.delete()) {
            throw new IOException("cannot remove the previous " + statusFile.getName());
        }

        String[] argv = LaunchArgs.build(init.getAbsolutePath().replace('\\', '/'),
                root.path(manifest.layout.logs + "/" + LaunchArgs.GAME_LOG), renderer, noMovie, noSound, extra);
        log.log("start: wrote " + faPath.getName() + " (GameVersion " + fafVersion + ")");
        log.log("start: argv " + LaunchArgs.describe(argv));
        settings.setLastLaunch(System.currentTimeMillis());
        return argv;
    }

    // ------------------------------------------------------------------- logs

    /** Shows the tail of a file in the logs folder, or of a root-relative path that contains '/'. */
    private void showLog(final String file) {
        // Recorded right away so a rotation before the read finishes reopens it.
        mLogDialogFile = file;
        final String relative = file.indexOf('/') >= 0 ? file : LauncherLog.LOGS_DIR + "/" + file;
        final String name = relative.substring(relative.lastIndexOf('/') + 1);
        final Context app = getApplicationContext();
        Background.run(() -> FileOps.tail(AppInfo.dataRoot(app).file(relative), LOG_TAIL_BYTES), (text, error) -> {
                    if (isDestroyed() || isFinishing()) {
                        return;
                    }
                    String body = error != null ? "Cannot read " + name + ": " + describe(error)
                            : text.isEmpty() ? "(empty or not written yet)" : text;
                    TextView view = mUi.mono(body);
                    view.setPadding(mUi.dp(20), mUi.dp(8), mUi.dp(20), mUi.dp(8));
                    final ScrollView scroll = new ScrollView(this);
                    scroll.addView(view);
                    showDialog(new AlertDialog.Builder(this)
                            .setTitle(name)
                            .setView(scroll)
                            .setPositiveButton("Close", null)
                            .create(), () -> mLogDialogFile = null);
                    mLogDialogFile = file;
                    scroll.post(() -> scroll.fullScroll(View.FOCUS_DOWN));
                });
    }

    private void confirmClearLogs() {
        showDialog(new AlertDialog.Builder(this)
                .setTitle("Clear logs?")
                .setMessage("Deletes the Vulkan and GLES runtime logs (with their previous runs), "
                        + LauncherLog.FILE_NAME + ", " + LaunchArgs.GAME_LOG + " and " + ReplayTest.LOG_NAME
                        + " from the logs folder, and the replay test runs (runs/). Imported replays stay.")
                .setPositiveButton("Clear", (dialog, which) -> clearLogs())
                .setNegativeButton("Keep", null)
                .create(), null);
    }

    private void clearLogs() {
        final Context app = getApplicationContext();
        // A running test keeps writing into its run directory; its runs/ stay then.
        final boolean testRunning = ImportService.state().running
                && ImportService.KIND_REPLAY.equals(ImportService.state().kind);
        mLog.clear();
        Background.run(() -> {
            DataRoot root = AppInfo.dataRoot(app);
            boolean ok = true;
            for (String name : CLEARED_LOGS) {
                File file = root.find(LauncherLog.LOGS_DIR + "/" + name);
                ok &= FileOps.deleteQuietly(file);
            }
            if (!testRunning) {
                File runs = root.find(ReplayTest.RUNS_DIR);
                if (runs != null) {
                    try {
                        ReplayTest.deleteTree(runs);
                    } catch (IOException e) {
                        ok = false;
                    }
                }
            }
            return ok;
        }, (ok, error) -> {
            if (!isDestroyed()) {
                toast(error == null && ok ? "Logs cleared" : "Some logs could not be deleted");
                refresh();
            }
        });
    }

    // ---------------------------------------------------------------- helpers

    private void showDialog(AlertDialog dialog, final Runnable onDismiss) {
        if (mDialog != null) {
            mDialog.setOnDismissListener(null);
            mDialog.dismiss();
            mLogDialogFile = null;
        }
        mDialog = dialog;
        dialog.setOnDismissListener(d -> {
            if (mDialog == d) {
                mDialog = null;
            }
            if (onDismiss != null) {
                onDismiss.run();
            }
        });
        dialog.show();
    }

    private void toast(String message) {
        Toast.makeText(this, message, Toast.LENGTH_LONG).show();
    }

    private static void appendLine(StringBuilder text, String line) {
        if (text.length() > 0) {
            text.append('\n');
        }
        text.append(line);
    }

    private static String formatTime(long millis) {
        return DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(new Date(millis));
    }

    private static String describe(Exception error) {
        return error.getMessage() != null ? error.getMessage() : error.toString();
    }
}
