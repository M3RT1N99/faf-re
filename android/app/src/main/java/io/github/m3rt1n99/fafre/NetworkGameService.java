package io.github.m3rt1n99.fafre;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.net.wifi.WifiManager;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;
import android.os.Process;
import android.os.SystemClock;

/**
 * Keeps a network game going with the game in the back or the screen off: Android cuts the network of an app that has
 * nothing in front (APP_BACKGROUND), and the others' games would stand still. And keeps the process of a replay or a
 * skirmish, which Android ends soon after the game went to the back otherwise (the limit of cached processes), so that the
 * player comes back to it. It runs in the game's process while any of them needs it (NetworkGameRule): the activity's poll
 * starts it, and it looks for itself when to stop, as the activity can be gone by then. It ends with the process, and
 * nothing restarts it.
 */
public final class NetworkGameService extends Service {
    private static final String CHANNEL = "network-game";
    /** The start's NetworkGameRule.Need, by its name: what it is for. */
    private static final String EXTRA_NEED = "io.github.m3rt1n99.fafre.extra.NEED";
    /** TestService's is 1. */
    private static final int NOTIFICATION = 2;
    private static final long POLL_MILLISECONDS = 500;
    /** Only if this service's own end should fail: NetworkGameRule's limit for the back ends it long before. */
    private static final long WAKE_LOCK_MILLISECONDS = 24 * 60 * 60 * 1000L;

    /** On the process's main thread, as the activity's calls and this service's callbacks all are. */
    private static final NetworkGameRule sRule = new NetworkGameRule();

    private final Handler mHandler = new Handler(Looper.getMainLooper());
    /** Made at the first network game, held while one is: a replay or a skirmish needs neither. */
    private PowerManager.WakeLock mWakeLock;
    private WifiManager.WifiLock mWifiLock;
    /** What the service keeps: what it was started for, then what the poll finds. Its locks and its words follow it. */
    private NetworkGameRule.Need mKeeps = NetworkGameRule.Need.NONE;

    private final Runnable mPoll = new Runnable() {
        @Override
        public void run() {
            NetworkGameRule.Need need = need();
            if (need == NetworkGameRule.Need.NONE) {
                stopSelf();
            } else if (sRule.tooLongInTheBack(SystemClock.elapsedRealtime())) {
                Game.report(what(mKeeps) + ": too long in the back");
                stopSelf();
            } else {
                if (need != mKeeps) {
                    keep(need);
                    getSystemService(NotificationManager.class).notify(NOTIFICATION, notification(need));
                }
                mHandler.postDelayed(this, POLL_MILLISECONDS);
            }
        }
    };

    /** The activity came to the front or left it. */
    static void front(boolean front) {
        sRule.front(front, SystemClock.elapsedRealtime());
    }

    private static NetworkGameRule.Need need() {
        return sRule.need(Game.networkInUse(), Game.gamePlaying(), Game.replaySession(), SystemClock.elapsedRealtime());
    }

    /** The activity's poll. */
    static void startIfNeeded(Context context) {
        NetworkGameRule.Need need = need();
        if (sRule.shouldStart(need)) {
            try {
                context.startForegroundService(new Intent(context, NetworkGameService.class).putExtra(EXTRA_NEED, need.name()));
                sRule.started();
            } catch (RuntimeException e) {
                refused(need, e);
            }
        }
    }

    /**
     * Whatever a phone's rules say: a network game goes on, and stops for everybody at Home, as it did without this
     * service; a replay or a skirmish goes on until Android ends its process in the back.
     */
    private static void refused(NetworkGameRule.Need need, RuntimeException reason) {
        sRule.refused();
        Game.report(what(need) + ": no foreground service: " + reason);
    }

    /** The words for what the service keeps, the report's and the notification's. */
    private static String what(NetworkGameRule.Need need) {
        switch (need) {
            case REPLAY:
                return "replay";
            case GAME:
                return "game";
            default:
                return "network game";
        }
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        // Before anything can stop it: Android ends a process whose service, started this way, stops before it has gone
        // foreground. What it was started for can have ended since; the poll stops it then.
        String startedFor = intent != null ? intent.getStringExtra(EXTRA_NEED) : null;
        NetworkGameRule.Need need = startedFor != null ? NetworkGameRule.Need.valueOf(startedFor) : need();
        mKeeps = need;
        try {
            startForeground(NOTIFICATION, notification(need));
        } catch (RuntimeException e) {
            refused(need, e);
            stopSelf();
            return START_NOT_STICKY;
        }
        keep(need);
        Game.report(what(need) + ": the service runs");
        mHandler.post(mPoll);
        return START_NOT_STICKY;
    }

    /**
     * Holds what `need` takes: a network game the CPU with the screen off, as every player waits for this phone, and Wi-Fi
     * at full power, which with the screen off would save power on the socket; a replay or a skirmish neither.
     */
    private void keep(NetworkGameRule.Need need) {
        mKeeps = need;
        if (need != NetworkGameRule.Need.NETWORK) {
            releaseLocks();
            return;
        }
        if (mWakeLock == null) {
            mWakeLock = getSystemService(PowerManager.class).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "faf-re:network-game");
            mWakeLock.setReferenceCounted(false);
            WifiManager wifi = getApplicationContext().getSystemService(WifiManager.class);
            if (wifi != null) {
                mWifiLock = wifi.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "faf-re:network-game");
                mWifiLock.setReferenceCounted(false);
            }
        }
        mWakeLock.acquire(WAKE_LOCK_MILLISECONDS);
        if (mWifiLock != null) {
            mWifiLock.acquire();
        }
    }

    /** Neither lock is counted: a release of one that is not held does nothing. */
    private void releaseLocks() {
        if (mWakeLock != null) {
            mWakeLock.release();
        }
        if (mWifiLock != null) {
            mWifiLock.release();
        }
    }

    /**
     * Swiped away. Android kills an app's processes then, unless one has a foreground service: this one would stay, a
     * network game with its socket open and the others waiting for a game that soon is frozen, a replay or a skirmish in a
     * task that is gone (stopWithTask does just that). Stopped first: a process that dies inside this call has its service
     * restarted.
     */
    @Override
    public void onTaskRemoved(Intent rootIntent) {
        Game.report(what(mKeeps) + ": the app was swiped away");
        stopSelf();
        Process.killProcess(Process.myPid());
    }

    @Override
    public void onDestroy() {
        mHandler.removeCallbacks(mPoll);
        releaseLocks();
        sRule.stopped();
        Game.report(what(mKeeps) + ": the service has stopped");
        super.onDestroy();
    }

    private Notification notification(NetworkGameRule.Need need) {
        getSystemService(NotificationManager.class).createNotificationChannel(
            new NotificationChannel(CHANNEL, getString(R.string.channel_network_game), NotificationManager.IMPORTANCE_LOW));
        // The activity that is there, wherever its task is: without the flag a second one would go on top of it.
        Intent back = new Intent(this, GameActivity.class).setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
        return new Notification.Builder(this, CHANNEL)
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentTitle(getString(R.string.app_name))
            .setContentText("A " + what(need) + " is running. Tap to go back to it.")
            .setContentIntent(PendingIntent.getActivity(this, 0, back, PendingIntent.FLAG_IMMUTABLE))
            .setOngoing(true)
            .build();
    }
}
