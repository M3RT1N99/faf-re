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
 * nothing in front (APP_BACKGROUND), and the others' games would stand still. It runs in the game's process while the
 * game's network is in use (NetworkGameRule): the activity's poll starts it, and it looks for itself when to stop, as the
 * activity can be gone by then. It ends with the process, and nothing restarts it.
 */
public final class NetworkGameService extends Service {
    private static final String CHANNEL = "network-game";
    /** TestService's is 1. */
    private static final int NOTIFICATION = 2;
    private static final long POLL_MILLISECONDS = 500;
    /** Only if this service's own end should fail: NetworkGameRule's limit for the back ends it long before. */
    private static final long WAKE_LOCK_MILLISECONDS = 24 * 60 * 60 * 1000L;

    /** On the process's main thread, as the activity's calls and this service's callbacks all are. */
    private static final NetworkGameRule sRule = new NetworkGameRule();

    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private PowerManager.WakeLock mWakeLock;
    private WifiManager.WifiLock mWifiLock;

    private final Runnable mPoll = new Runnable() {
        @Override
        public void run() {
            if (!needed()) {
                stopSelf();
            } else if (sRule.tooLongInTheBack(SystemClock.elapsedRealtime())) {
                Game.report("network game: too long in the back");
                stopSelf();
            } else {
                mHandler.postDelayed(this, POLL_MILLISECONDS);
            }
        }
    };

    /** The activity came to the front or left it. */
    static void front(boolean front) {
        sRule.front(front, SystemClock.elapsedRealtime());
    }

    private static boolean needed() {
        return sRule.needed(Game.networkInUse(), Game.gamePlaying(), SystemClock.elapsedRealtime());
    }

    /** The activity's poll. */
    static void startIfNeeded(Context context) {
        if (sRule.shouldStart(needed())) {
            try {
                context.startForegroundService(new Intent(context, NetworkGameService.class));
                sRule.started();
            } catch (RuntimeException e) {
                refused(e);
            }
        }
    }

    /** Whatever a phone's rules say: the game goes on, and stops for everybody at Home, as it did without this service. */
    private static void refused(RuntimeException reason) {
        sRule.refused();
        Game.report("network game: no foreground service: " + reason);
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onCreate() {
        super.onCreate();
        mWakeLock = getSystemService(PowerManager.class).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "faf-re:network-game");
        mWakeLock.setReferenceCounted(false);
        mWakeLock.acquire(WAKE_LOCK_MILLISECONDS);
        // Every player waits for what this phone's radio delivers, and with the screen off it would save power on it.
        WifiManager wifi = getApplicationContext().getSystemService(WifiManager.class);
        if (wifi != null) {
            mWifiLock = wifi.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "faf-re:network-game");
            mWifiLock.setReferenceCounted(false);
            mWifiLock.acquire();
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        // Before anything can stop it: Android ends a process whose service, started this way, stops before it has gone foreground.
        try {
            startForeground(NOTIFICATION, notification());
        } catch (RuntimeException e) {
            refused(e);
            stopSelf();
            return START_NOT_STICKY;
        }
        Game.report("network game: the service runs");
        mHandler.post(mPoll);
        return START_NOT_STICKY;
    }

    /**
     * Swiped away. Android kills an app's processes then, unless one has a foreground service: this one would stay, with its
     * socket open and the others waiting for a game that soon is frozen (stopWithTask does just that). Stopped first:
     * a process that dies inside this call has its service restarted.
     */
    @Override
    public void onTaskRemoved(Intent rootIntent) {
        Game.report("network game: the app was swiped away");
        stopSelf();
        Process.killProcess(Process.myPid());
    }

    @Override
    public void onDestroy() {
        mHandler.removeCallbacks(mPoll);
        mWakeLock.release();
        if (mWifiLock != null) {
            mWifiLock.release();
        }
        sRule.stopped();
        Game.report("network game: the service has stopped");
        super.onDestroy();
    }

    private Notification notification() {
        getSystemService(NotificationManager.class).createNotificationChannel(
            new NotificationChannel(CHANNEL, getString(R.string.channel_network_game), NotificationManager.IMPORTANCE_LOW));
        // The activity that is there, wherever its task is: without the flag a second one would go on top of it.
        Intent back = new Intent(this, GameActivity.class).setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
        return new Notification.Builder(this, CHANNEL)
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentTitle(getString(R.string.app_name))
            .setContentText("A network game is running. Tap to go back to it.")
            .setContentIntent(PendingIntent.getActivity(this, 0, back, PendingIntent.FLAG_IMMUTABLE))
            .setOngoing(true)
            .build();
    }
}
