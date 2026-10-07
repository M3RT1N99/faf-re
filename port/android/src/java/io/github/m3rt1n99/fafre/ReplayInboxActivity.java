package io.github.m3rt1n99.fafre;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.widget.Toast;

/**
 * Takes a replay from "Open with" (ACTION_VIEW) or "Share" (ACTION_SEND), for example the file the
 * browser saved from https://replay.faforever.com/&lt;id&gt;. The read permission that comes with
 * the intent only lasts as long as this activity, so the file is copied into {@code <root>/replays}
 * right away ({@link ReplayFiles#importUri}: content check, size cap, safe name), read by the runner,
 * selected for the replay test, and the launcher is brought to the front. No UI of its own.
 */
public final class ReplayInboxActivity extends Activity {
    static final String EXTRA_MESSAGE = "io.github.m3rt1n99.fafre.extra.INBOX_MESSAGE";
    static final String EXTRA_OK = "io.github.m3rt1n99.fafre.extra.INBOX_OK";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (savedInstanceState != null) {
            // Recreated after the copy was already started; it reports through the launcher.
            finish();
            return;
        }
        Uri uri = sourceOf(getIntent());
        final Context app = getApplicationContext();
        final LauncherLog log = LauncherLog.get(app);
        if (uri == null) {
            log.log("replay inbox: no file in " + getIntent());
            Toast.makeText(this, "No replay file in what was shared.", Toast.LENGTH_LONG).show();
            finish();
            return;
        }
        Toast.makeText(this, "Reading the replay…", Toast.LENGTH_SHORT).show();
        // One runner job at a time: while a test (or an import) runs, the copy is only stored and selected.
        // The runner reads it (/replayinfo, /convertreplay) when the next test starts, because the sidecar
        // then has no analyzed_by.
        final boolean jobRunning = ImportService.state().running;
        Background.run(() -> {
            ReplayFiles.Info info = ReplayFiles.importUri(app, uri);
            log.log("replay inbox: imported " + info.fileName() + " (" + info.format() + ", sha256 " + info.sha256()
                    + ")" + (jobRunning ? "; not analyzed now, a job is running" : ""));
            if (!jobRunning) {
                info = ReplayFiles.analyze(app, info, new Cancellation());
            }
            new Settings(app).setReplayStem(info.stem());
            return info;
        }, (info, error) -> {
            String message;
            boolean ok = error == null;
            if (ok) {
                message = "Replay " + info.fileName() + (jobRunning ? " is selected; it is read when the next test "
                        + "starts (a test is running now)." : " is ready for the replay test.");
            } else {
                message = "Cannot use this file: " + (error.getMessage() != null ? error.getMessage() : error);
                log.log("replay inbox: " + message);
            }
            Intent launcher = new Intent(app, LauncherActivity.class)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP
                            | Intent.FLAG_ACTIVITY_SINGLE_TOP)
                    .putExtra(EXTRA_MESSAGE, message).putExtra(EXTRA_OK, ok);
            app.startActivity(launcher);
            if (!isFinishing()) {
                finish();
            }
        });
    }

    @SuppressWarnings("deprecation")
    private static Uri sourceOf(Intent intent) {
        if (intent == null) {
            return null;
        }
        if (Intent.ACTION_VIEW.equals(intent.getAction())) {
            return intent.getData();
        }
        if (Intent.ACTION_SEND.equals(intent.getAction())) {
            Object stream = intent.getParcelableExtra(Intent.EXTRA_STREAM);
            return stream instanceof Uri ? (Uri) stream : null;
        }
        return null;
    }
}
