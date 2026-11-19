package io.github.m3rt1n99.fafre;

import android.content.Intent;
import android.graphics.Typeface;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;

/**
 * The game data's setup for another app, the FAF client (action SETUP_GAME_DATA, android/README.md, The setup): this app's
 * own screen for a step, the SCFA import from its folder or a ZIP of it, or FAF's newest game files, which the player starts
 * with a tap and leaves with Back. Any app may open it, as it shows only this app's UI and takes nothing of its caller's but
 * the step: no file, no URI, no job without the player. The jobs are TestService's, as the test app's are, and go on after
 * Back; the caller reads what came of them in StatusProvider's game_data.
 */
public final class SetupActivity extends Screen {
    private static final String TAG = "faf-setup";
    /** The log's lines the screen shows, the newest. */
    private static final int LOG_LINES = 12;

    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private final List<Button> mStartButtons = new ArrayList<>();
    private long mShownRevision = -1;
    private TextView mScfa;
    private TextView mGameFiles;
    private TextView mStep;
    private TextView mLog;
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
        Set<Setup.Step> steps;
        try {
            steps = Setup.steps(LaunchGameActivity.extras(getIntent()));
        } catch (GameStart.Failure failure) {
            Log.w(TAG, getCallingPackage() + ": " + failure.code + ": " + failure.getMessage());
            setResult(RESULT_FIRST_USER, LaunchGameActivity.result(failure));
            finish();
            return;
        }
        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(16);
        column.setPadding(pad, pad, pad, pad);

        TextView title = text("Game data", 22, ACCENT);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        column.addView(title);
        column.addView(text("What the game (" + getString(R.string.app_name) + " " + BuildConfig.VERSION_NAME + ") needs on "
            + "this phone. Back returns to the app that opened this; a job you start goes on there.", 13, MUTED));

        if (steps.contains(Setup.Step.SCFA)) {
            LinearLayout scfa = card(column, "Supreme Commander: Forged Alliance",
                "Your own copy of the game, imported once: its gamedata/*.scd archives, fonts/, sounds/, movies/ and maps/, "
                    + "about 7.5 GB. Pick its folder, or a ZIP file of it, which may hold the folder itself or what is in it. "
                    + "An import copies only what is not here yet; a ZIP's import changes nothing here unless it ends whole.");
            mScfa = text("", 14, TEXT);
            scfa.addView(mScfa);
            mStartButtons.add(button(scfa, "Folder…", v -> pickScfaFolder()));
            mStartButtons.add(button(scfa, "ZIP file…", v -> pickScfaZip()));
        }
        if (steps.contains(Setup.Step.GAME_FILES)) {
            LinearLayout files = card(column, "FAF's game files",
                "FAF's newest game version, which PCs on FAF play now: about 700 MB from FAF's servers, kept whole. On a "
                    + "metered network (mobile data, a hotspot, a Wi-Fi set so) the app asks first.");
            mGameFiles = text("", 14, TEXT);
            files.addView(mGameFiles);
            mStartButtons.add(button(files, "Get FAF's newest version", v -> getNewestGameFiles()));
        }

        LinearLayout progress = card(column, "Progress", null);
        mStep = text("", 14, ACCENT);
        progress.addView(mStep);
        mCancel = button(progress, "Cancel", v -> TestService.start(this, new Intent(TestService.ACTION_CANCEL)));
        mLog = text("", 11, MUTED);
        mLog.setTypeface(Typeface.MONOSPACE);
        progress.addView(mLog);
        show(column);
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        // A start in a task of its own (FLAG_ACTIVITY_NEW_TASK) finds this screen in the task it left: the new start's step.
        setIntent(intent);
        recreate();
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
        keepScreenOn(running);
        if (revision == mShownRevision) {
            return;
        }
        mShownRevision = revision;
        mStep.setText(running ? (TestState.step().isEmpty() ? "Running…" : TestState.step()) : "Idle");
        List<String> log = TestState.log();
        mLog.setText(String.join("\n", log.subList(Math.max(0, log.size() - LOG_LINES), log.size())));
        Storage storage;
        try {
            storage = new Storage(this);
        } catch (IOException e) {
            (mScfa != null ? mScfa : mGameFiles).setText("Storage: " + e.getMessage());
            return;
        }
        if (mScfa != null) {
            boolean fonts = ScfaImport.fonts(storage) > 0;
            boolean imported = ScfaImport.archives(storage) > 0;
            mScfa.setText(ScfaImport.summary(storage) + (imported && !fonts
                ? "\nNo fonts, without which the game cannot start: import the folder or the ZIP again, which takes fonts/ too"
                : ""));
            mScfa.setTextColor(imported && fonts ? TEXT : FAIL);
        }
        if (mGameFiles != null) {
            // Only the screen that offers the download asks about it.
            NewestGameFiles question = running ? null : NewestGameFiles.question();
            if (question != null) {
                askToDownload(question);
            }
            List<Integer> versions = GameFiles.readyVersions(storage);
            NewestGameFiles newest = NewestGameFiles.now();
            mGameFiles.setText(newest.line(versions.isEmpty() ? -1 : versions.get(versions.size() - 1)));
            mGameFiles.setTextColor(newest.phase == NewestGameFiles.Phase.FAILED ? FAIL : TEXT);
        }
    }
}
