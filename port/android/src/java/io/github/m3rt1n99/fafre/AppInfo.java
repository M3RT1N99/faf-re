package io.github.m3rt1n99.fafre;

import android.app.ActivityManager;
import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.os.Build;

import dalvik.system.BaseDexClassLoader;

import java.io.IOException;
import java.util.Arrays;
import java.util.List;

/**
 * Process-wide facts the launcher, the import service and the game activity
 * share: the version strings, the parsed manifest (loaded once, off the UI
 * thread) and whether the native runtime is part of this APK.
 */
final class AppInfo {
    /** android.app.lib_name of GameActivity in AndroidManifest.xml. */
    static final String NATIVE_LIBRARY = "faf_android";
    static final String NATIVE_LIBRARY_FILE = "lib" + NATIVE_LIBRARY + ".so";
    static final String GAME_PROCESS_SUFFIX = ":game";
    static final String ABI = "arm64-v8a";

    private static DataManifest sManifest;

    private AppInfo() {
    }

    static String versionName(Context context) {
        try {
            PackageInfo info = packageInfo(context);
            return info.versionName != null ? info.versionName : "dev";
        } catch (PackageManager.NameNotFoundException e) {
            return "dev";
        }
    }

    @SuppressWarnings("deprecation")
    private static PackageInfo packageInfo(Context context) throws PackageManager.NameNotFoundException {
        PackageManager pm = context.getPackageManager();
        if (Build.VERSION.SDK_INT >= 33) {
            return pm.getPackageInfo(context.getPackageName(), PackageManager.PackageInfoFlags.of(0));
        }
        return pm.getPackageInfo(context.getPackageName(), 0);
    }

    /** HTTP User-Agent for FAF's content server, so they can tell our traffic apart. */
    static String userAgent(Context context) {
        return "faf-re-android/" + versionName(context);
    }

    /** The bundled gamedata.json; parsed on first use. Call off the UI thread. */
    static synchronized DataManifest manifest(Context context) throws IOException {
        if (sManifest == null) {
            sManifest = DataManifest.fromAssets(context.getApplicationContext().getAssets());
        }
        return sManifest;
    }

    /** The data root; creates it if needed. Call off the UI thread. */
    static DataRoot dataRoot(Context context) throws IOException {
        return new DataRoot(context.getExternalFilesDir(null));
    }

    /** Pid of the ":game" process if it is alive (running or cached), else -1. */
    static int gameProcessPid(Context context) {
        ActivityManager activities = context.getSystemService(ActivityManager.class);
        List<ActivityManager.RunningAppProcessInfo> processes =
                activities != null ? activities.getRunningAppProcesses() : null;
        if (processes == null) {
            return -1;
        }
        String name = context.getPackageName() + GAME_PROCESS_SUFFIX;
        for (ActivityManager.RunningAppProcessInfo process : processes) {
            if (name.equals(process.processName)) {
                return process.pid;
            }
        }
        return -1;
    }

    /**
     * Why GameActivity cannot load libfaf_android.so, or null if it can. A
     * Java-only build (build_android.ps1 -SkipNative) and devices without
     * arm64 are the expected cases. Scans the APK; call off the UI thread.
     */
    static String nativeRuntimeProblem(Context context) {
        ClassLoader loader = context.getClassLoader();
        if (loader instanceof BaseDexClassLoader
                && ((BaseDexClassLoader) loader).findLibrary(NATIVE_LIBRARY) != null) {
            return null;
        }
        if (!Arrays.asList(Build.SUPPORTED_ABIS).contains(ABI)) {
            return "This device does not run " + ABI + " code (it supports " + String.join(", ", Build.SUPPORTED_ABIS)
                    + ").";
        }
        return "This APK does not contain the native game runtime (lib" + NATIVE_LIBRARY + ".so); it was built "
                + "with -SkipNative.";
    }
}
