package io.github.m3rt1n99.fafre;

import android.Manifest;
import android.content.ClipData;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.view.Gravity;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
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
public final class MainActivity extends Screen {
    /** docs/PR_REPLAY_CHECK.md, the core set (FAF game version 3835), shortest first. */
    private static final long[] CORE_SET = {26902315, 26930624, 26943656, 26883423, 26881085, 26928211, 26989770, 26897073};
    /** The extended set's ten games beyond the core set, shortest first (28 to 83 minutes). */
    private static final long[] EXTENDED_SET = {26975290, 26953273, 27034831, 26917867, 26876279, 26935800, 26898759, 27004084, 26915859, 26905122};
    private static final String QUICK_SET = "26930624 26902315";

    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private long mShownRevision = -1;

    private TextView mMathResult;
    private TextView mGameResult;
    private TextView mNewest;
    private TextView mDataStatus;
    private TextView mStep;
    private TextView mReplayResults;
    private TextView mLog;
    private EditText mReplayIds;
    private EditText mWorkers;
    private CheckBox mHint;
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
        column.addView(text("Engine test build " + BuildConfig.GIT_COMMIT + " · headless simulation, game start", 13, MUTED));
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
            "Your own Supreme Commander: Forged Alliance (its gamedata/*.scd archives, fonts/, sounds/ and movies/, about "
                + "7.5 GB), imported once from its folder or a ZIP file of it, which may hold the folder itself or what is in "
                + "it; a ZIP's import changes nothing here unless it ends whole. An import by an earlier release has no sounds "
                + "or movies, without which the game is silent and has no films: import the folder or the ZIP again, which "
                + "copies only what is missing. FAF's own files for a replay's game version are downloaded on the first run, "
                + "FAF's newest by the button on card 5 (about 700 MB from FAF each, kept whole), replays and maps as needed.");
        mDataStatus = text("", 14, TEXT);
        data.addView(mDataStatus);
        mStartButtons.add(button(data, "Import SCFA folder…", v -> pickScfaFolder()));
        mStartButtons.add(button(data, "Import SCFA ZIP…", v -> pickScfaZip()));

        // 3. Replays.
        LinearLayout replays = card(column, "3 · Replay parity",
            "FAF vault replays played to the end on the engine's simulation. The players' games recorded a checksum every "
                + "50 beats; PASS means every one of them equals this phone's. The two default replays take a few minutes, "
                + "the core set (8 games, 13 to 34 minutes long) about 40 minutes, the extended set (those and 10 games of "
                + "28 to 83 minutes) a few hours. Keep the app in front: Android gives a background app only the slow cores.");
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
        mHint = new CheckBox(this);
        mHint.setText("Ask Android for full speed (performance hint)");
        mHint.setTextColor(TEXT);
        mHint.setChecked(true);
        replays.addView(mHint);
        button(replays, "Use the core set", v -> mReplayIds.setText(ids(CORE_SET)));
        button(replays, "Use the extended set", v -> mReplayIds.setText(ids(CORE_SET) + " " + ids(EXTENDED_SET)));
        mStartButtons.add(button(replays, "Play replays", v -> playReplays()));
        mReplayResults = text("", 14, TEXT);
        mReplayResults.setTypeface(Typeface.MONOSPACE);
        replays.addView(mReplayResults);

        // 5. The game; card 4, the renderer test, has gone: the game's report names the GPU, the swap chain and the BC path.
        LinearLayout game = card(column, "5 · Game",
            "The whole game on this phone, as the PC starts it: with your SCFA and FAF's game files of the newest version "
                + "on the phone. The replay test brings each replay's own version (cards 2 and 3); \"Get FAF's newest "
                + "version\" below brings the one PCs on FAF play now, about 700 MB, by itself on a network that is not "
                + "metered and after a question on mobile data. Older versions stay, for the replays. The app's UI hooks "
                + "(fafre_ui) are appended to FAF's files of whatever version, each in a pcall, so a change in FAF's file "
                + "shows as a \"fafre_ui:\" warning in the game's log rather than a broken game. "
                + "It plays skirmishes and LAN games against FAF's game on a PC, with sound and movies. "
                + "A tap is a click, a finger that moves drags, a finger that stays is the right button (an order); two fingers "
                + "move the camera and zoom. The bar at the screen's edge has Order (taps give orders), Shift, Ctrl and the "
                + "keyboard, which the game's Menu tab has too (Keyboard), and Back is Esc. The report says how it ended. "
                + "With the touch controls Griff (the button below) "
                + "one finger pans instead and lets the map glide on, a rest and a drag boxes, a tap selects your own unit "
                + "(also a little beside it) or gives the selection FAF's order there, two taps take all of its type on the "
                + "screen and three on the map, two taps on the ground make the move an attack-move, two fingers zoom where "
                + "they are, a stroke from the round handle beside the selection lays a route that ends in the order its label "
                + "names (your factory with engineers: assist), a long loop back to the handle patrols, and two fingers tapping "
                + "deselect; the rail at the top left has Box, Plus (queue) and Attack side by side, lit by a tap for "
                + "the next action or held for as long as they are held, and in a game the keyboard is in its Menu tab. Control "
                + "groups 1, 2 and 3 always show: a tap selects one, two taps go to it, a rest stores the selection there (with "
                + "Plus: adds it). A build icon opens a draft: taps place plans (an extractor on its mass spot), a drag from the "
                + "newest draws a line of them, from another moves them all, a rest shows a loupe; the tick orders them, the "
                + "cross drops them. After an order a pill Undo stands for 3 s beside Attack, and a tap on it takes the order "
                + "back; pins mark the order's places for a moment. The touch controls count from the game's next start: a "
                + "game that runs keeps its own.");
        mStartButtons.add(button(game, "Start the game", v -> startActivity(new Intent(this, GameActivity.class))));
        Button size = button(game, PictureSize.label(this), null);
        size.setOnClickListener(v -> {
            PictureSize.next(this);
            size.setText(PictureSize.label(this));
        });
        // The game is SDL's activity, whose fingers are SDL's mouse until the touch schemes come back to it (android/CLIENT_PLAN.md).
        Button touch = button(game, "Touch controls: a finger is the mouse (Griff and the bar come back later)", null);
        touch.setEnabled(false);
        mStartButtons.add(button(game, "Get FAF's newest version", v -> getNewestGameFiles()));
        mNewest = text("", 14, TEXT);
        game.addView(mNewest);
        mGameResult = text("", 14, TEXT);
        game.addView(mGameResult);

        // Running.
        LinearLayout running = card(column, "Progress", null);
        mStep = text("", 14, ACCENT);
        running.addView(mStep);
        mCancel = button(running, "Cancel", v -> TestService.start(this, new Intent(TestService.ACTION_CANCEL)));
        button(running, "Share results", v -> share());
        mLog = text("", 11, MUTED);
        mLog.setTypeface(Typeface.MONOSPACE);
        running.addView(mLog);

        show(column);
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
        GameReport.refresh(this);
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
        NewestGameFiles question = running ? null : NewestGameFiles.question();
        if (question != null) {
            askToDownload(question);
        }
        String math = TestState.mathVerdict();
        mMathResult.setText(math);
        mMathResult.setTextColor(math.startsWith("PASS") ? PASS : math.startsWith("FAIL") ? FAIL : TEXT);
        String gameVerdict = TestState.gameVerdict();
        mGameResult.setText(gameVerdict);
        mGameResult.setTextColor(gameVerdict.contains("CRASHED") || gameVerdict.contains("FAIL") ? FAIL : TEXT);
        mStep.setText(running ? (TestState.step().isEmpty() ? "Running…" : TestState.step()) : "Idle");
        mReplayResults.setText(String.join("\n", TestState.replayLines()));
        List<String> log = TestState.log();
        mLog.setText(String.join("\n", log.subList(Math.max(0, log.size() - 40), log.size())));
        try {
            Storage storage = new Storage(this);
            int archives = ScfaImport.archives(storage);
            int fonts = ScfaImport.fonts(storage);
            StringBuilder status = new StringBuilder(ScfaImport.summary(storage));
            status.append(archives > 0 ? " in " + storage.scfa : "");
            List<Integer> versions = GameFiles.readyVersions(storage);
            status.append("\nFAF game files: ");
            for (int i = 0; i < versions.size(); ++i) {
                status.append(i > 0 ? ", " : "").append(versions.get(i));
            }
            status.append(versions.isEmpty() ? "none yet (downloaded on the first replay, or by card 5's button)" : "");
            mDataStatus.setText(status);
            mDataStatus.setTextColor(archives > 0 && fonts > 0 ? TEXT : FAIL);
            NewestGameFiles newest = NewestGameFiles.now();
            mNewest.setText(newest.line(versions.isEmpty() ? -1 : versions.get(versions.size() - 1)));
            mNewest.setTextColor(newest.phase == NewestGameFiles.Phase.FAILED ? FAIL : TEXT);
        } catch (IOException e) {
            mDataStatus.setText("Storage: " + e.getMessage());
        }
    }

    /** "26902315 26930624 ...": a replay set for the ID field. */
    private static String ids(long[] set) {
        StringBuilder out = new StringBuilder();
        for (long id : set) {
            out.append(out.length() == 0 ? "" : " ").append(id);
        }
        return out.toString();
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
        TestService.start(this, TestService.replays(array, workers, mHint.isChecked()));
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
}
