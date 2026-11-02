package io.github.m3rt1n99.fafre;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.os.Build;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.List;

/**
 * The renderer test's report, from its process (GalSmokeActivity, ":renderer") to the app's:
 * renderer.txt in the app's files, which the native side writes line by line. The app's process
 * reads it into TestState. A report without a verdict whose process is gone gets the reason
 * Android gives for the end of that process and, after a native crash on Android 12 and later,
 * the tombstone.
 */
final class RendererReport {
    /** The tombstone's name in the app's files and in the results zip. */
    static final String TOMBSTONE = "renderer-tombstone.pb";
    private static final String PROCESS = BuildConfig.APPLICATION_ID + ":renderer";

    /** The file state TestState holds (lastModified and length), or none. */
    private static long sLoaded = Long.MIN_VALUE;
    /** TestState holds a report without a verdict: each refresh looks whether its process is still there. */
    private static boolean sAwaiting;

    private RendererReport() {
    }

    static File file(Context context) {
        return new File(context.getFilesDir(), "renderer.txt");
    }

    static File tombstone(Context context) {
        return new File(context.getFilesDir(), TOMBSTONE);
    }

    /** A new test (its own process): no report and no tombstone yet. */
    static void clear(Context context) {
        file(context).delete();
        tombstone(context).delete();
    }

    /** PASS and FAIL from the native side; CRASHED and INCOMPLETE from {@link #refresh}. */
    static boolean isVerdict(String line) {
        return line.startsWith("PASS") || line.startsWith("FAIL") || line.startsWith("CRASHED")
            || line.startsWith("INCOMPLETE");
    }

    /** Reads the report into TestState when the file changed (the app's process, each refresh). */
    static synchronized void refresh(Context context) {
        File file = file(context);
        long stamp = file.isFile() ? file.lastModified() * 31 + file.length() : -1;
        if (stamp == sLoaded && !sAwaiting) {
            return;
        }
        if (stamp == -1) {
            sLoaded = stamp;
            sAwaiting = false;
            TestState.renderer("");
            return;
        }
        String report;
        try {
            report = new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
        } catch (IOException e) {
            return;
        }
        boolean verdict = false;
        for (String line : report.split("\n")) {
            verdict |= isVerdict(line);
        }
        sAwaiting = false;
        if (!verdict) {
            if (running(context)) {
                // The test runs, or waits in the background: TestState shows it as incomplete.
                sAwaiting = true;
                if (stamp != sLoaded) {
                    sLoaded = stamp;
                    TestState.renderer(report);
                }
                return;
            }
            String end = end(context, file.lastModified());
            try (OutputStream out = new FileOutputStream(file, true)) {
                out.write((end + "\n").getBytes(StandardCharsets.UTF_8));
            } catch (IOException e) {
                // TestState has it anyway
            }
            report += end + "\n";
            stamp = file.lastModified() * 31 + file.length();
        }
        sLoaded = stamp;
        TestState.renderer(report);
        TestState.line(TestState.rendererVerdict());
    }

    private static boolean running(Context context) {
        ActivityManager manager = context.getSystemService(ActivityManager.class);
        List<ActivityManager.RunningAppProcessInfo> processes = manager != null ? manager.getRunningAppProcesses() : null;
        if (processes != null) {
            for (ActivityManager.RunningAppProcessInfo process : processes) {
                if (PROCESS.equals(process.processName)) {
                    return true;
                }
            }
        }
        return false;
    }

    /** The verdict for a test whose process ended without one; `since` is the report's last write. */
    private static String end(Context context, long since) {
        if (Build.VERSION.SDK_INT < 30) {
            return "INCOMPLETE: the test's process ended without a verdict";
        }
        ActivityManager manager = context.getSystemService(ActivityManager.class);
        List<ApplicationExitInfo> exits = manager.getHistoricalProcessExitReasons(null, 0, 16);
        for (ApplicationExitInfo exit : exits) {
            // The file's time has a granularity of its own; a second's margin.
            if (!PROCESS.equals(exit.getProcessName()) || exit.getTimestamp() < since - 1000) {
                continue;
            }
            int reason = exit.getReason();
            boolean crashed = reason == ApplicationExitInfo.REASON_CRASH_NATIVE || reason == ApplicationExitInfo.REASON_CRASH
                || reason == ApplicationExitInfo.REASON_SIGNALED;
            StringBuilder line = new StringBuilder(crashed ? "CRASHED: " : "INCOMPLETE: the process ended: ");
            line.append(reasonName(reason)).append(", status ").append(exit.getStatus());
            String description = exit.getDescription();
            if (description != null && !description.isEmpty()) {
                line.append(", ").append(description);
            }
            if (reason == ApplicationExitInfo.REASON_CRASH_NATIVE && Build.VERSION.SDK_INT >= 31 && saveTombstone(context, exit)) {
                line.append(" (tombstone in the results: ").append(TOMBSTONE).append(')');
            }
            return line.toString();
        }
        return "INCOMPLETE: the test's process ended without a verdict, and Android gives no reason";
    }

    private static String reasonName(int reason) {
        switch (reason) {
            case ApplicationExitInfo.REASON_CRASH_NATIVE:
                return "native crash";
            case ApplicationExitInfo.REASON_CRASH:
                return "Java crash";
            case ApplicationExitInfo.REASON_SIGNALED:
                return "killed by a signal";
            case ApplicationExitInfo.REASON_ANR:
                return "not responding";
            case ApplicationExitInfo.REASON_LOW_MEMORY:
                return "low memory";
            case ApplicationExitInfo.REASON_EXIT_SELF:
                return "exited";
            case ApplicationExitInfo.REASON_USER_REQUESTED:
            case ApplicationExitInfo.REASON_USER_STOPPED:
                return "stopped by the user";
            default:
                return "exit reason " + reason;
        }
    }

    /** The tombstone of a native crash (protobuf, Android 12 and later) into the app's files. */
    private static boolean saveTombstone(Context context, ApplicationExitInfo exit) {
        try (InputStream in = exit.getTraceInputStream()) {
            if (in == null) {
                return false;
            }
            try (OutputStream out = new FileOutputStream(tombstone(context))) {
                byte[] buffer = new byte[1 << 16];
                int read;
                while ((read = in.read(buffer)) > 0) {
                    out.write(buffer, 0, read);
                }
            }
            return true;
        } catch (IOException e) {
            return false;
        }
    }
}
