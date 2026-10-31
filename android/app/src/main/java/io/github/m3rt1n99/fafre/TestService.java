package io.github.m3rt1n99.fafre;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;

import java.util.ArrayList;
import java.util.List;

/**
 * Runs the tests as a foreground service, so downloads and replays go on with the screen off
 * or the app in the background, one job at a time: the sim-math check, an SCFA import, or a
 * list of replays.
 */
public final class TestService extends Service {
    static final String ACTION_MATH = "io.github.m3rt1n99.fafre.MATH";
    static final String ACTION_REPLAYS = "io.github.m3rt1n99.fafre.REPLAYS";
    static final String ACTION_IMPORT = "io.github.m3rt1n99.fafre.IMPORT";
    static final String ACTION_CANCEL = "io.github.m3rt1n99.fafre.CANCEL";
    static final String EXTRA_IDS = "ids";
    static final String EXTRA_WORKERS = "workers";
    static final String EXTRA_HINT = "hint";

    private static final String CHANNEL = "tests";
    private static final int NOTIFICATION = 1;

    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private String mShownStep = "";

    static void start(Context context, Intent intent) {
        intent.setClass(context, TestService.class);
        if (Build.VERSION.SDK_INT >= 26) {
            context.startForegroundService(intent);
        } else {
            context.startService(intent);
        }
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String action = intent != null ? intent.getAction() : null;
        goForeground("Starting");
        if (ACTION_CANCEL.equals(action)) {
            Cancellation.request();
            HeadlessRun.stop();
            if (!TestState.running()) {
                stopSelf();
            }
            return START_NOT_STICKY;
        }
        if (action == null || !TestState.begin(action)) {
            if (!TestState.running()) {
                stopSelf();
            }
            return START_NOT_STICKY;
        }
        Intent job = intent;
        Thread worker = new Thread(() -> runJob(job), "faf-tests");
        worker.start();
        mHandler.post(mUpdate);
        return START_NOT_STICKY;
    }

    private final Runnable mUpdate = new Runnable() {
        @Override
        public void run() {
            if (!TestState.running()) {
                return;
            }
            String step = TestState.step();
            if (!step.equals(mShownStep)) {
                mShownStep = step;
                NotificationManager manager = getSystemService(NotificationManager.class);
                manager.notify(NOTIFICATION, notification(step.isEmpty() ? "Running" : step));
            }
            mHandler.postDelayed(this, 2000);
        }
    };

    private void runJob(Intent intent) {
        PowerManager power = getSystemService(PowerManager.class);
        PowerManager.WakeLock lock = power.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "faf-re:tests");
        lock.acquire(6 * 60 * 60 * 1000L);
        try {
            Storage storage = new Storage(this);
            String action = intent.getAction();
            if (ACTION_MATH.equals(action)) {
                MathCheck.run(this);
            } else if (ACTION_IMPORT.equals(action)) {
                ScfaImport.run(this, storage, intent.getData());
            } else if (ACTION_REPLAYS.equals(action)) {
                playReplays(storage, intent.getLongArrayExtra(EXTRA_IDS), intent.getIntExtra(EXTRA_WORKERS, 1),
                    intent.getBooleanExtra(EXTRA_HINT, true));
            }
        } catch (Cancellation.Cancelled e) {
            TestState.line("Cancelled.");
        } catch (Exception e) {
            TestState.line("Error: " + e);
        } finally {
            lock.release();
            TestState.end();
            mHandler.post(() -> {
                stopForeground(true);
                stopSelf();
            });
        }
    }

    private void playReplays(Storage storage, long[] ids, int workers, boolean hint) {
        List<TestState.Replay> replays = new ArrayList<>();
        for (long id : ids) {
            replays.add(new TestState.Replay(id));
        }
        TestState.replays(replays);
        GameFiles.Log log = new GameFiles.Log() {
            @Override
            public void line(String text) {
                TestState.line(text);
            }

            @Override
            public void step(String text) {
                TestState.step(text);
            }
        };
        // First everything from the network: a download problem shows while the tester is still
        // there, and the games, which can take hours, need no network.
        List<TestState.Replay> ready = new ArrayList<>();
        for (TestState.Replay replay : replays) {
            if (Cancellation.requested()) {
                replay.state = "cancelled";
                continue;
            }
            try {
                if (HeadlessRun.fetch(storage, replay, log)) {
                    ready.add(replay);
                }
            } catch (Cancellation.Cancelled e) {
                replay.state = "cancelled";
            } catch (Exception e) {
                failed(replay, "download: ", e);
            }
            if (!ready.contains(replay)) {
                TestState.line(replay.line());
            }
            TestState.touch();
        }
        TestState.line(ready.size() + " of " + replays.size() + " replays ready; no more downloads in this run");
        for (TestState.Replay replay : ready) {
            if (Cancellation.requested()) {
                replay.state = "cancelled";
                continue;
            }
            try {
                HeadlessRun.play(this, storage, replay, workers, hint, log);
            } catch (Cancellation.Cancelled e) {
                replay.state = "cancelled";
            } catch (Exception e) {
                failed(replay, "", e);
            }
            TestState.line(replay.line());
            TestState.touch();
        }
    }

    /**
     * A replay that stopped with an exception. Cancel closes the running engine's output, so the
     * read fails ("interrupted by close()"): after a cancel that is the cancel, not an error.
     */
    private static void failed(TestState.Replay replay, String what, Exception e) {
        if (Cancellation.requested()) {
            replay.state = "cancelled";
        } else {
            replay.state = "error";
            replay.note = what + e.getMessage();
        }
    }

    private void goForeground(String text) {
        NotificationManager manager = getSystemService(NotificationManager.class);
        if (Build.VERSION.SDK_INT >= 26 && manager.getNotificationChannel(CHANNEL) == null) {
            manager.createNotificationChannel(
                new NotificationChannel(CHANNEL, getString(R.string.channel_name), NotificationManager.IMPORTANCE_LOW));
        }
        if (Build.VERSION.SDK_INT >= 29) {
            startForeground(NOTIFICATION, notification(text), ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
        } else {
            startForeground(NOTIFICATION, notification(text));
        }
    }

    private Notification notification(String text) {
        Intent open = new Intent(this, MainActivity.class).setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        PendingIntent pending = PendingIntent.getActivity(this, 0, open, PendingIntent.FLAG_IMMUTABLE);
        Notification.Builder builder = Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(this, CHANNEL) : new Notification.Builder(this);
        return builder.setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentTitle(getString(R.string.app_name))
            .setContentText(text)
            .setContentIntent(pending)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .build();
    }

    static Intent replays(long[] ids, int workers, boolean hint) {
        return new Intent(ACTION_REPLAYS).putExtra(EXTRA_IDS, ids).putExtra(EXTRA_WORKERS, workers).putExtra(EXTRA_HINT, hint);
    }

    static Intent importScfa(Uri tree) {
        return new Intent(ACTION_IMPORT).setData(tree);
    }
}
