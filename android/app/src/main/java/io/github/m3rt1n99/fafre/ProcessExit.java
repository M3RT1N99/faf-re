package io.github.m3rt1n99.fafre;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.os.Build;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.nio.charset.StandardCharsets;
import java.time.Instant;
import java.util.ArrayDeque;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.TimeUnit;

/**
 * The app's process of its own, the game's: whether it runs, and
 * how it ended, as Android tells it (ApplicationExitInfo, Android 11 and later).
 */
final class ProcessExit {
    /** The lines of a process's logcat kept after a signal ended it and left no tombstone (keepLogcat). */
    static final int LOGCAT_LINES = 500;
    /** How long `logcat` may take for them: the report's end can be on the main thread (GameReport.refresh). */
    private static final long LOGCAT_SECONDS = 3;
    /** Linux's signal names by their numbers, the same on arm64 and x86_64. */
    private static final String[] SIGNALS = {null, "SIGHUP", "SIGINT", "SIGQUIT", "SIGILL", "SIGTRAP", "SIGABRT", "SIGBUS",
        "SIGFPE", "SIGKILL", "SIGUSR1", "SIGSEGV", "SIGUSR2", "SIGPIPE", "SIGALRM", "SIGTERM", "SIGSTKFLT", "SIGCHLD", "SIGCONT",
        "SIGSTOP", "SIGTSTP", "SIGTTIN", "SIGTTOU", "SIGURG", "SIGXCPU", "SIGXFSZ", "SIGVTALRM", "SIGPROF", "SIGWINCH", "SIGIO",
        "SIGPWR", "SIGSYS"};

    /** How a process ended. */
    static final class End {
        final int reason;
        final int status;
        final String description;
        /** A native crash's tombstone was saved (Android 12 and later). */
        final boolean tombstone;
        /** The process's id. */
        final int pid;
        /** When it died, in ms since 1970; 0 when Android does not say. */
        final long timestamp;

        End(int reason, int status, String description, boolean tombstone, int pid, long timestamp) {
            this.reason = reason;
            this.status = status;
            this.description = description != null ? description : "";
            this.tombstone = tombstone;
            this.pid = pid;
            this.timestamp = timestamp;
        }

        boolean crashed() {
            return reason == ApplicationExitInfo.REASON_CRASH_NATIVE || reason == ApplicationExitInfo.REASON_CRASH
                || reason == ApplicationExitInfo.REASON_SIGNALED;
        }

        boolean exitedItself() {
            return reason == ApplicationExitInfo.REASON_EXIT_SELF;
        }

        /** A signal ended it: a native crash, or a kill by a signal. Its status is the signal's number then. */
        boolean signaled() {
            return reason == ApplicationExitInfo.REASON_CRASH_NATIVE || reason == ApplicationExitInfo.REASON_SIGNALED;
        }

        /** The name of the signal that ended it ("SIGABRT"); null for another end, or a number without a name. */
        String signal() {
            return signaled() ? signalName(status) : null;
        }

        /** Whether its logcat is what is left of how it died: a signal ended it, and no tombstone was saved. */
        boolean logcatWanted() {
            return signaled() && !tombstone;
        }

        /** "killed by a signal, status 6 (SIGABRT), at 2026-11-24T18:31:02.417Z, ...", the time its death's. */
        String describe() {
            StringBuilder line = new StringBuilder(reasonName(reason)).append(", status ").append(status);
            String signal = signal();
            if (signal != null) {
                line.append(" (").append(signal).append(')');
            }
            if (timestamp > 0) {
                line.append(", at ").append(Instant.ofEpochMilli(timestamp));
            }
            if (!description.isEmpty()) {
                line.append(", ").append(description);
            }
            return line.toString();
        }
    }

    /** Where a process's logcat comes from: {@code logcat} itself, a stand-in in the tests. */
    interface Logcat {
        /** The lines of process {@code pid}'s main and crash buffers, oldest first, into {@code out}. */
        void dump(int pid, File out) throws IOException, InterruptedException;
    }

    private ProcessExit() {
    }

    /** The full name of the app's process `suffix` (":game"). */
    static String name(String suffix) {
        return BuildConfig.APPLICATION_ID + suffix;
    }

    static boolean running(Context context, String suffix) {
        ActivityManager manager = context.getSystemService(ActivityManager.class);
        List<ActivityManager.RunningAppProcessInfo> processes = manager != null ? manager.getRunningAppProcesses() : null;
        if (processes != null) {
            String process = name(suffix);
            for (ActivityManager.RunningAppProcessInfo info : processes) {
                if (process.equals(info.processName)) {
                    return true;
                }
            }
        }
        return false;
    }

    /**
     * The last end of the process `suffix` since `since` (ms); after a native crash its tombstone goes to `tombstone`,
     * and after an end by a signal that left none the end of its logcat goes to `logcat` (keepLogcat). Null when Android
     * gives none, and before Android 11.
     */
    static End last(Context context, String suffix, long since, File tombstone, File logcat) {
        if (Build.VERSION.SDK_INT < 30) {
            return null;
        }
        ActivityManager manager = context.getSystemService(ActivityManager.class);
        List<ApplicationExitInfo> exits = manager.getHistoricalProcessExitReasons(null, 0, 16);
        String process = name(suffix);
        for (ApplicationExitInfo exit : exits) {
            // The file's time has a granularity of its own; a second's margin.
            if (!process.equals(exit.getProcessName()) || exit.getTimestamp() < since - 1000) {
                continue;
            }
            boolean saved = exit.getReason() == ApplicationExitInfo.REASON_CRASH_NATIVE && Build.VERSION.SDK_INT >= 31
                && saveTombstone(exit, tombstone);
            End end = new End(exit.getReason(), exit.getStatus(), exit.getDescription(), saved, exit.getPid(),
                exit.getTimestamp());
            keepLogcat(end, logcat, ProcessExit::logcat);
            return end;
        }
        return null;
    }

    /** `logcat`'s arguments for the main and crash buffers of process {@code pid}, dumped with their times and threads. */
    static List<String> logcatCommand(int pid) {
        return Arrays.asList("logcat", "-d", "-b", "main", "-b", "crash", "-v", "threadtime", "--pid=" + pid);
    }

    /**
     * After an end by a signal that left no tombstone (End.logcatWanted), the last LOGCAT_LINES lines of its process's
     * logcat into {@code file}, after a line that says what they are, or why there are none; after any other end no
     * file. The app's uid reads its own processes' lines, a dead one's too while logd's buffers hold them: libc's "Fatal
     * signal" and the abort message of whatever aborted, which nothing else kept when no tombstone was written.
     */
    static void keepLogcat(End end, File file, Logcat logcat) {
        file.delete();
        if (!end.logcatWanted()) {
            return;
        }
        File dump = new File(file.getPath() + ".part");
        try (Writer out = new OutputStreamWriter(new FileOutputStream(file), StandardCharsets.UTF_8)) {
            try {
                logcat.dump(end.pid, dump);
                try (BufferedReader in = new BufferedReader(new InputStreamReader(new FileInputStream(dump),
                    StandardCharsets.UTF_8))) {
                    out.write(String.join(" ", logcatCommand(end.pid)) + ": its last " + LOGCAT_LINES + " lines\n");
                    tail(in, LOGCAT_LINES, out);
                }
            } catch (IOException e) {
                out.write("The logcat of process " + end.pid + " cannot be read: " + e.getMessage() + "\n");
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                out.write("The logcat of process " + end.pid + " was not read: the thread was interrupted\n");
            }
        } catch (IOException e) {
            // As after an end without a signal: no file.
            file.delete();
        } finally {
            dump.delete();
        }
    }

    /** The last {@code count} lines of {@code in} into {@code out}, each with its line end. */
    static void tail(BufferedReader in, int count, Writer out) throws IOException {
        ArrayDeque<String> lines = new ArrayDeque<>(count);
        for (String line = in.readLine(); line != null; line = in.readLine()) {
            if (lines.size() == count) {
                lines.removeFirst();
            }
            lines.addLast(line);
        }
        for (String line : lines) {
            out.write(line);
            out.write('\n');
        }
    }

    /** {@code logcat} (logcatCommand) into {@code out}, for LOGCAT_SECONDS at most. */
    private static void logcat(int pid, File out) throws IOException, InterruptedException {
        Process logcat = new ProcessBuilder(logcatCommand(pid)).redirectErrorStream(true).redirectOutput(out).start();
        if (!logcat.waitFor(LOGCAT_SECONDS, TimeUnit.SECONDS)) {
            logcat.destroyForcibly();
            throw new IOException("logcat gave no end in " + LOGCAT_SECONDS + " s");
        }
        if (logcat.exitValue() != 0) {
            throw new IOException("logcat exited with " + logcat.exitValue());
        }
    }

    /** Linux's name of signal {@code number} ("SIGABRT" for 6); null for a number without one. */
    static String signalName(int number) {
        return number > 0 && number < SIGNALS.length ? SIGNALS[number] : null;
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

    /** The tombstone of a native crash (protobuf, Android 12 and later) into `file`. */
    private static boolean saveTombstone(ApplicationExitInfo exit, File file) {
        try (InputStream in = exit.getTraceInputStream()) {
            if (in == null) {
                return false;
            }
            try (OutputStream out = new FileOutputStream(file)) {
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
