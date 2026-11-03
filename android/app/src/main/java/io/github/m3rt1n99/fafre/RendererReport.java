package io.github.m3rt1n99.fafre;

import android.content.Context;
import android.os.Build;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

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
    private static final String PROCESS = ":renderer";

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
        return ProcessExit.running(context, PROCESS);
    }

    /** The verdict for a test whose process ended without one; `since` is the report's last write. */
    private static String end(Context context, long since) {
        if (Build.VERSION.SDK_INT < 30) {
            return "INCOMPLETE: the test's process ended without a verdict";
        }
        ProcessExit.End end = ProcessExit.last(context, PROCESS, since, tombstone(context));
        if (end == null) {
            return "INCOMPLETE: the test's process ended without a verdict, and Android gives no reason";
        }
        return (end.crashed() ? "CRASHED: " : "INCOMPLETE: the process ended: ") + end.describe()
            + (end.tombstone ? " (tombstone in the results: " + TOMBSTONE + ")" : "");
    }
}
