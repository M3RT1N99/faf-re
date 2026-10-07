package io.github.m3rt1n99.fafre;

import android.app.ActivityManager;
import android.app.usage.UsageStatsManager;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.BatteryManager;
import android.os.Build;
import android.os.PowerManager;
import android.os.Process;
import android.system.ErrnoException;
import android.system.Os;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;

/**
 * The app's own process state for the replay test's telemetry ({@link RunTelemetry.AppProbe}): how
 * important Android considers the app, whether the screen is on, battery saver, the thermal status and
 * the cgroups the app process is in. Also the scheduling state of the thread that starts a runner, which
 * the child inherits at fork: timer slack, nice value and cpuset.
 *
 * <p>Every call is best effort: a value the device does not give is left out.
 */
final class AppState implements RunTelemetry.AppProbe {
    /** prctl(2) option numbers (linux/prctl.h). */
    static final int PR_SET_TIMERSLACK = 29;
    static final int PR_GET_TIMERSLACK = 30;

    private final Context mContext;

    AppState(Context context) {
        mContext = context.getApplicationContext();
    }

    @Override
    public JSONObject sample() {
        JSONObject json = new JSONObject();
        try {
            ActivityManager.RunningAppProcessInfo info = new ActivityManager.RunningAppProcessInfo();
            ActivityManager.getMyMemoryState(info);
            json.put("importance", importanceName(info.importance)).put("lru", info.lru)
                    .put("trim_level", info.lastTrimLevel);
        } catch (RuntimeException | JSONException e) {
            putQuietly(json, "importance_error", e.toString());
        }
        try {
            PowerManager power = mContext.getSystemService(PowerManager.class);
            if (power != null) {
                json.put("interactive", power.isInteractive()).put("power_save", power.isPowerSaveMode())
                        .put("device_idle", power.isDeviceIdleMode())
                        .put("ignoring_battery_optimizations", power.isIgnoringBatteryOptimizations(
                                mContext.getPackageName()));
                if (Build.VERSION.SDK_INT >= 29) {
                    json.put("thermal_status", power.getCurrentThermalStatus());
                }
                if (Build.VERSION.SDK_INT >= 30) {
                    float headroom = power.getThermalHeadroom(10);
                    if (!Float.isNaN(headroom)) {
                        json.put("thermal_headroom_10s", Math.round(headroom * 1000) / 1000.0);
                    }
                }
            }
        } catch (RuntimeException | JSONException e) {
            putQuietly(json, "power_error", e.toString());
        }
        try {
            ActivityManager activities = mContext.getSystemService(ActivityManager.class);
            if (activities != null && Build.VERSION.SDK_INT >= 28) {
                json.put("background_restricted", activities.isBackgroundRestricted());
            }
            UsageStatsManager usage = mContext.getSystemService(UsageStatsManager.class);
            if (usage != null && Build.VERSION.SDK_INT >= 28) {
                json.put("standby_bucket", usage.getAppStandbyBucket());
            }
        } catch (RuntimeException | JSONException e) {
            putQuietly(json, "restrictions_error", e.toString());
        }
        try {
            // A sticky broadcast: no receiver is registered.
            Intent battery = mContext.registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
            if (battery != null) {
                int temperature = battery.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, Integer.MIN_VALUE);
                if (temperature != Integer.MIN_VALUE) {
                    json.put("battery_temp_c", temperature / 10.0);
                }
                json.put("plugged", battery.getIntExtra(BatteryManager.EXTRA_PLUGGED, -1))
                        .put("battery_level", battery.getIntExtra(BatteryManager.EXTRA_LEVEL, -1));
            }
        } catch (RuntimeException | JSONException e) {
            putQuietly(json, "battery_error", e.toString());
        }
        putQuietly(json, "cpuset", ProcFiles.describe(new File("/proc/self/cpuset")));
        putQuietly(json, "oom_score_adj", ProcFiles.describe(new File("/proc/self/oom_score_adj")));
        return json;
    }

    /** What the app's main process looks like to the scheduler, once per run (meta.json). */
    JSONObject describeProcess() {
        JSONObject json = sample();
        putQuietly(json, "pid", Process.myPid());
        putQuietly(json, "cgroup", trim(ProcFiles.describe(new File("/proc/self/cgroup"))));
        String status = ProcFiles.read(new File("/proc/self/status"));
        putQuietly(json, "cpus_allowed_list", status != null ? String.valueOf(ProcFiles.statusValue(status,
                "Cpus_allowed_list")) : "unreadable");
        return json;
    }

    /**
     * The calling thread's scheduling state, read on the thread that is about to start a runner: the child
     * starts with this timer slack (fork copies it, exec keeps it), nice value, policy, cpuset and affinity.
     */
    static JSONObject callingThread() {
        JSONObject json = new JSONObject();
        int tid = Process.myTid();
        putQuietly(json, "tid", tid);
        putQuietly(json, "name", Thread.currentThread().getName());
        try {
            putQuietly(json, "timerslack_ns", Os.prctl(PR_GET_TIMERSLACK, 0, 0, 0, 0));
        } catch (ErrnoException | RuntimeException e) {
            putQuietly(json, "timerslack_ns", "unreadable: " + e.getMessage());
        }
        try {
            putQuietly(json, "nice", Process.getThreadPriority(tid));
        } catch (RuntimeException e) {
            putQuietly(json, "nice", "unreadable: " + e.getMessage());
        }
        File task = new File("/proc/self/task/" + tid);
        ProcFiles.Stat stat = ProcFiles.Stat.parse(ProcFiles.read(new File(task, "stat"), 4096));
        if (stat != null) {
            putQuietly(json, "policy", ProcFiles.policyName(stat.policy));
            putQuietly(json, "cpu", stat.processor);
        }
        putQuietly(json, "cpuset", ProcFiles.describe(new File(task, "cpuset")));
        String status = ProcFiles.read(new File(task, "status"));
        putQuietly(json, "cpus_allowed_list", status != null ? String.valueOf(ProcFiles.statusValue(status,
                "Cpus_allowed_list")) : "unreadable");
        return json;
    }

    static String importanceName(int importance) {
        switch (importance) {
            case ActivityManager.RunningAppProcessInfo.IMPORTANCE_FOREGROUND:
                return "foreground";
            case ActivityManager.RunningAppProcessInfo.IMPORTANCE_FOREGROUND_SERVICE:
                return "foreground_service";
            case ActivityManager.RunningAppProcessInfo.IMPORTANCE_VISIBLE:
                return "visible";
            case ActivityManager.RunningAppProcessInfo.IMPORTANCE_PERCEPTIBLE:
                return "perceptible";
            case ActivityManager.RunningAppProcessInfo.IMPORTANCE_SERVICE:
                return "service";
            case ActivityManager.RunningAppProcessInfo.IMPORTANCE_CACHED:
                return "cached";
            case ActivityManager.RunningAppProcessInfo.IMPORTANCE_GONE:
                return "gone";
            default:
                return String.valueOf(importance);
        }
    }

    private static String trim(String text) {
        return text.length() > 1024 ? text.substring(0, 1024) + "…" : text;
    }

    private static void putQuietly(JSONObject json, String key, Object value) {
        try {
            json.put(key, value);
        } catch (JSONException e) {
            // Only a NaN or infinite double fails here; leave the key out.
        }
    }
}
