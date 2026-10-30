package io.github.m3rt1n99.fafre;

import android.Manifest;
import android.app.Activity;
import android.content.ClipData;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/**
 * The test app's one screen: the simulation's arithmetic, the game data, and FAF vault replays
 * played on the engine's headless simulation. The tests run in TestService; this screen polls
 * TestState and shows it.
 */
public final class MainActivity extends Activity {
    // res/values/colors.xml
    private static final int BACKGROUND = 0xFF101820;
    private static final int CARD = 0xFF1A2934;
    private static final int ACCENT = 0xFFF2A33A;
    private static final int TEXT = 0xFFE8EEF2;
    private static final int MUTED = 0xFF9AAAB6;
    private static final int PASS = 0xFF6BD06B;
    private static final int FAIL = 0xFFFF6B6B;

    private static final int PICK_SCFA = 1;

    /** docs/PR_REPLAY_CHECK.md, the core set (FAF game version 3835), shortest first. */
    private static final long[] CORE_SET = {26902315, 26930624, 26943656, 26883423, 26881085, 26928211, 26989770, 26897073};
    private static final String QUICK_SET = "26930624 26902315";

    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private long mShownRevision = -1;
    private boolean mScreenKeptOn;

    private TextView mMathResult;
    private TextView mDataStatus;
    private TextView mStep;
    private TextView mReplayResults;
    private TextView mLog;
    private EditText mReplayIds;
    private EditText mWorkers;
    private final List<Button> mStartButtons = new ArrayList<>();
    private Button mCancel;

    private final Runnable mRefresh = new Runnable() {
        @Override
        public void run() {
            refresh();
            mHandler.postDelayed(this, 500);
        }
    };

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(16);
        column.setPadding(pad, pad, pad, pad);

        TextView title = text(getString(R.string.app_name) + " " + BuildConfig.VERSION_NAME, 22, ACCENT);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        column.addView(title);
        column.addView(text("Engine test build " + BuildConfig.GIT_COMMIT + " · headless simulation, no graphics", 13, MUTED));
        column.addView(text(Results.device(), 13, MUTED));

        // 1. Arithmetic.
        LinearLayout math = card(column, "1 · Simulation arithmetic",
            "The engine's sim-math tests: 22,188 cases recorded on the PC's x87 unit, computed again on this phone's "
                + "CPU (arm64 has no x87; the engine computes those steps in software). Needs no game data.");
        mStartButtons.add(button(math, "Run math check", v -> TestService.start(this, new Intent(TestService.ACTION_MATH))));
        mMathResult = text("", 14, TEXT);
        math.addView(mMathResult);

        // 2. Game data.
        LinearLayout data = card(column, "2 · Game data",
            "Your own Supreme Commander: Forged Alliance (its gamedata/*.scd archives), imported once. Earlier releases of "
                + "this app imported it already. FAF's own files for a replay's game version are downloaded on the first run "
                + "(about 700 MB from FAF, 35 MB kept), replays and maps as needed.");
        mDataStatus = text("", 14, TEXT);
        data.addView(mDataStatus);
        mStartButtons.add(button(data, "Import SCFA folder…", v -> pickScfa()));

        // 3. Replays.
        LinearLayout replays = card(column, "3 · Replay parity",
            "FAF vault replays played to the end on the engine's simulation. The players' games recorded a checksum every "
                + "50 beats; PASS means every one of them equals this phone's. The two default replays take a few minutes; "
                + "the core set (8 games, 13 to 34 minutes long) longer.");
        replays.addView(text("Replay IDs", 13, MUTED));
        mReplayIds = new EditText(this);
        mReplayIds.setText(QUICK_SET);
        mReplayIds.setTextColor(TEXT);
        mReplayIds.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        replays.addView(mReplayIds);
        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.addView(text("Sim worker threads  ", 13, MUTED));
        mWorkers = new EditText(this);
        mWorkers.setText("1");
        mWorkers.setTextColor(TEXT);
        mWorkers.setInputType(InputType.TYPE_CLASS_NUMBER);
        mWorkers.setEms(3);
        row.addView(mWorkers);
        replays.addView(row);
        button(replays, "Use the core set", v -> {
            StringBuilder ids = new StringBuilder();
            for (long id : CORE_SET) {
                ids.append(ids.length() == 0 ? "" : " ").append(id);
            }
            mReplayIds.setText(ids);
        });
        mStartButtons.add(button(replays, "Play replays", v -> playReplays()));
        mReplayResults = text("", 14, TEXT);
        mReplayResults.setTypeface(Typeface.MONOSPACE);
        replays.addView(mReplayResults);

        // Running.
        LinearLayout running = card(column, "Progress", null);
        mStep = text("", 14, ACCENT);
        running.addView(mStep);
        mCancel = button(running, "Cancel", v -> TestService.start(this, new Intent(TestService.ACTION_CANCEL)));
        button(running, "Share results", v -> share());
        mLog = text("", 11, MUTED);
        mLog.setTypeface(Typeface.MONOSPACE);
        running.addView(mLog);

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(BACKGROUND);
        scroll.addView(column);
        setContentView(scroll);
        if (Build.VERSION.SDK_INT >= 35) {
            scroll.setOnApplyWindowInsetsListener((v, insets) -> {
                android.graphics.Insets bars = insets.getInsets(android.view.WindowInsets.Type.systemBars());
                v.setPadding(bars.left, bars.top, bars.right, bars.bottom);
                return insets;
            });
        }
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] {Manifest.permission.POST_NOTIFICATIONS}, 0);
        }
    }

    @Override
    protected void onStart() {
        super.onStart();
        TestState.visible(true);
    }

    @Override
    protected void onStop() {
        TestState.visible(false);
        super.onStop();
    }

    @Override
    protected void onResume() {
        super.onResume();
        mShownRevision = -1;
        mHandler.post(mRefresh);
    }

    @Override
    protected void onPause() {
        super.onPause();
        mHandler.removeCallbacks(mRefresh);
    }

    private void refresh() {
        long revision = TestState.revision();
        boolean running = TestState.running();
        for (Button button : mStartButtons) {
            button.setEnabled(!running);
        }
        mCancel.setEnabled(running);
        if (running != mScreenKeptOn) {
            // A game is played with the screen on; a phone with the screen off may run the CPU slower.
            mScreenKeptOn = running;
            if (running) {
                getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            } else {
                getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            }
        }
        if (revision == mShownRevision) {
            return;
        }
        mShownRevision = revision;
        String math = TestState.mathVerdict();
        mMathResult.setText(math);
        mMathResult.setTextColor(math.startsWith("PASS") ? PASS : math.startsWith("FAIL") ? FAIL : TEXT);
        mStep.setText(running ? (TestState.step().isEmpty() ? "Running…" : TestState.step()) : "Idle");
        mReplayResults.setText(String.join("\n", TestState.replayLines()));
        List<String> log = TestState.log();
        mLog.setText(String.join("\n", log.subList(Math.max(0, log.size() - 40), log.size())));
        try {
            Storage storage = new Storage(this);
            int archives = ScfaImport.archives(storage);
            StringBuilder status = new StringBuilder();
            status.append(archives > 0 ? "SCFA: " + archives + " archives in " + storage.scfa : "SCFA: not imported yet");
            String[] versions = new File(storage.parity, "game").list();
            status.append("\nFAF game files: ");
            boolean any = false;
            if (versions != null) {
                for (String version : versions) {
                    if (GameFiles.ready(storage, Integer.parseInt(version))) {
                        status.append(any ? ", " : "").append(version);
                        any = true;
                    }
                }
            }
            status.append(any ? "" : "none yet (downloaded on the first replay)");
            mDataStatus.setText(status);
            mDataStatus.setTextColor(archives > 0 ? TEXT : FAIL);
        } catch (IOException | NumberFormatException e) {
            mDataStatus.setText("Storage: " + e.getMessage());
        }
    }

    private void playReplays() {
        List<Long> ids = new ArrayList<>();
        for (String part : mReplayIds.getText().toString().split("[\\s,;]+")) {
            if (!part.isEmpty()) {
                try {
                    ids.add(Long.parseLong(part));
                } catch (NumberFormatException e) {
                    Toast.makeText(this, "Not a replay ID: " + part, Toast.LENGTH_LONG).show();
                    return;
                }
            }
        }
        if (ids.isEmpty()) {
            Toast.makeText(this, "Enter at least one replay ID", Toast.LENGTH_LONG).show();
            return;
        }
        int workers = 1;
        try {
            workers = Math.max(0, Math.min(16, Integer.parseInt(mWorkers.getText().toString().trim())));
        } catch (NumberFormatException e) {
            // keep 1
        }
        long[] array = new long[ids.size()];
        for (int i = 0; i < array.length; ++i) {
            array[i] = ids.get(i);
        }
        TestService.start(this, TestService.replays(array, workers));
    }

    private void pickScfa() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(intent, PICK_SCFA);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == PICK_SCFA && resultCode == RESULT_OK && data != null && data.getData() != null) {
            Uri tree = data.getData();
            try {
                getContentResolver().takePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (SecurityException e) {
                // a one-time grant is enough for this import
            }
            TestService.start(this, TestService.importScfa(tree));
        }
    }

    private void share() {
        try {
            File zip = Results.write(this, new Storage(this));
            Uri uri = ResultsProvider.uri(zip);
            Intent send = new Intent(Intent.ACTION_SEND)
                .setType("application/zip")
                .putExtra(Intent.EXTRA_STREAM, uri)
                .putExtra(Intent.EXTRA_SUBJECT, "faf-re " + BuildConfig.VERSION_NAME + " test results")
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            send.setClipData(ClipData.newRawUri("results", uri));
            startActivity(Intent.createChooser(send, "Share results"));
        } catch (IOException e) {
            Toast.makeText(this, "Cannot write the results: " + e.getMessage(), Toast.LENGTH_LONG).show();
        }
    }

    // --- Views -------------------------------------------------------------------------------

    private int dp(int value) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value, getResources().getDisplayMetrics()));
    }

    private TextView text(String value, int sp, int color) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        view.setTextColor(color);
        view.setPadding(0, dp(2), 0, dp(2));
        return view;
    }

    private LinearLayout card(LinearLayout parent, String title, String description) {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(14);
        card.setPadding(pad, pad, pad, pad);
        GradientDrawable shape = new GradientDrawable();
        shape.setColor(CARD);
        shape.setCornerRadius(dp(10));
        card.setBackground(shape);
        TextView heading = text(title, 17, TEXT);
        heading.setTypeface(Typeface.DEFAULT_BOLD);
        card.addView(heading);
        if (description != null) {
            card.addView(text(description, 13, MUTED));
        }
        LinearLayout.LayoutParams params =
            new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        params.topMargin = dp(14);
        parent.addView(card, params);
        return card;
    }

    private Button button(LinearLayout parent, String label, View.OnClickListener listener) {
        Button button = new Button(this);
        button.setText(label);
        button.setAllCaps(false);
        button.setTextColor(Color.BLACK);
        button.getBackground().setTint(ACCENT);
        button.setOnClickListener(listener);
        parent.addView(button);
        return button;
    }
}
