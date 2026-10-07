package io.github.m3rt1n99.fafre;

import android.os.SystemClock;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.List;
import java.util.Map;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;

/**
 * Runs one runner process (a real execve of nativeLibraryDir/libfafrunner.so: a fresh address space
 * without ART, which the low arena needs) and waits for it, with output going to a file and to a line
 * listener as it arrives.
 *
 * <p>Lifetime rules (the process is a "phantom" child of this app process and must never outlive the
 * job that started it): a cancel of the job ({@link Cancellation}, which ImportService also triggers
 * from onTimeout and onDestroy) sends SIGTERM through {@link Process#destroy()} right away and
 * SIGKILL ({@link Process#destroyForcibly()}) 3 s later if it is still there. A process that prints
 * nothing for {@code watchdogMs} of uptime ({@link SystemClock#uptimeMillis()}, which stops in deep sleep)
 * is ended the same way. No PR_SET_PDEATHSIG: it fires when the forking
 * thread ends, not the process.
 */
final class RunnerProcess {
    interface LineListener {
        void onLine(String line);
    }

    static final long KILL_GRACE_MS = 3000;
    private static final int TAIL_LINES = 80;
    private static final int MAX_LINE_CHARS = 8192;

    static final class Outcome {
        /** Process exit value; for a signal Java reports 0x80 + the signal number. -1 if it never started. */
        int exitCode = -1;
        /** Signal number when the process was killed by one, else 0. */
        int signal;
        boolean started;
        boolean cancelled;
        boolean watchdog;
        boolean forced;
        long wallMs;
        String startError;
        final List<String> tail = new ArrayList<>();

        boolean signalled() {
            return signal > 0;
        }

        String describeExit() {
            if (!started) {
                return "did not start: " + startError;
            }
            if (signal > 0) {
                return "killed by " + signalName(signal) + " (exit " + exitCode + ")";
            }
            return "exit " + exitCode;
        }
    }

    private RunnerProcess() {
    }

    /**
     * Starts {@code argv} in {@code cwd} with the app's environment minus LD_PRELOAD plus {@code env},
     * stdout and stderr merged into {@code outFile}, and waits for it.
     */
    static Outcome run(List<String> argv, Map<String, String> env, File cwd, File outFile, long watchdogMs,
            Cancellation cancel, LineListener listener) throws IOException {
        Outcome outcome = new Outcome();
        ProcessBuilder builder = new ProcessBuilder(argv).directory(cwd).redirectErrorStream(true);
        Map<String, String> environment = builder.environment();
        environment.remove("LD_PRELOAD");
        environment.putAll(env);

        cancel.throwIfCancelled();
        long start = SystemClock.elapsedRealtime();
        final Process process;
        try {
            process = builder.start();
        } catch (IOException e) {
            outcome.startError = e.getMessage() != null ? e.getMessage() : e.toString();
            try (OutputStream out = new FileOutputStream(outFile, true)) {
                out.write(("[app] cannot start " + argv.get(0) + ": " + outcome.startError + "\n")
                        .getBytes(StandardCharsets.UTF_8));
            }
            return outcome;
        }
        outcome.started = true;
        try {
            process.getOutputStream().close();
        } catch (IOException ignored) {
            // stdin is not used; a failed close changes nothing.
        }

        // The watchdog and the kill grace count uptime, not elapsed real time: a device that sleeps deeply
        // despite the wake lock has not run the process, so that time is no sign of a hang.
        final AtomicLong lastOutput = new AtomicLong(SystemClock.uptimeMillis());
        final AtomicLong destroyAt = new AtomicLong(0);
        final Deque<String> tail = new ArrayDeque<>();
        final IOException[] readError = new IOException[1];
        Thread reader = new Thread(() -> {
            try {
                drain(process.getInputStream(), outFile, lastOutput, tail, listener);
            } catch (IOException e) {
                readError[0] = e;
            }
        }, "fafre-runner-out");
        reader.setDaemon(true);
        reader.start();

        Cancellation.Registration registration = cancel.onCancel(() -> {
            destroyAt.compareAndSet(0, SystemClock.uptimeMillis());
            process.destroy();
        });
        try {
            while (!process.waitFor(250, TimeUnit.MILLISECONDS)) {
                long now = SystemClock.uptimeMillis();
                long asked = destroyAt.get();
                if (asked == 0 && watchdogMs > 0 && now - lastOutput.get() > watchdogMs) {
                    outcome.watchdog = true;
                    destroyAt.compareAndSet(0, now);
                    process.destroy();
                } else if (asked != 0 && !outcome.forced && now - asked >= KILL_GRACE_MS) {
                    outcome.forced = true;
                    process.destroyForcibly();
                }
            }
        } catch (InterruptedException e) {
            process.destroyForcibly();
            Thread.currentThread().interrupt();
            throw new Cancellation.CancelledException("interrupted");
        } finally {
            registration.close();
        }
        try {
            reader.join(5000);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        outcome.wallMs = SystemClock.elapsedRealtime() - start;
        outcome.exitCode = process.exitValue();
        outcome.signal = outcome.exitCode > 128 && outcome.exitCode <= 128 + 64 ? outcome.exitCode - 128 : 0;
        outcome.cancelled = cancel.isCancelled();
        synchronized (tail) {
            outcome.tail.addAll(tail);
        }
        // destroy() closes the pipe under the reader, so a read error after a cancel or the watchdog is expected.
        if (readError[0] != null && !outcome.cancelled && !outcome.watchdog) {
            try (OutputStream out = new FileOutputStream(outFile, true)) {
                out.write(("[app] reading the output failed: " + readError[0].getMessage() + "\n")
                        .getBytes(StandardCharsets.UTF_8));
            }
        }
        return outcome;
    }

    /** Copies the process output to {@code outFile} and hands each complete line to the listener. */
    private static void drain(InputStream in, File outFile, AtomicLong lastOutput, Deque<String> tail,
            LineListener listener) throws IOException {
        byte[] buffer = new byte[16 * 1024];
        ByteArrayOutputStream pending = new ByteArrayOutputStream();
        try (InputStream input = in; OutputStream out = new FileOutputStream(outFile, true)) {
            int n;
            while ((n = input.read(buffer)) > 0) {
                lastOutput.set(SystemClock.uptimeMillis());
                out.write(buffer, 0, n);
                out.flush();
                for (int i = 0; i < n; ++i) {
                    byte b = buffer[i];
                    if (b == '\n') {
                        emit(pending, tail, listener);
                    } else if (pending.size() < MAX_LINE_CHARS) {
                        pending.write(b);
                    }
                }
            }
            if (pending.size() > 0) {
                emit(pending, tail, listener);
            }
        }
    }

    private static void emit(ByteArrayOutputStream pending, Deque<String> tail, LineListener listener) {
        String text = new String(pending.toByteArray(), StandardCharsets.UTF_8);
        pending.reset();
        if (text.endsWith("\r")) {
            text = text.substring(0, text.length() - 1);
        }
        synchronized (tail) {
            tail.addLast(text);
            while (tail.size() > TAIL_LINES) {
                tail.removeFirst();
            }
        }
        if (listener != null) {
            try {
                listener.onLine(text);
            } catch (RuntimeException e) {
                // A parser bug must not stop the draining (the process would block on a full pipe).
                android.util.Log.w(LauncherLog.TAG, "runner output parser: " + e);
            }
        }
    }

    static String signalName(int signal) {
        switch (signal) {
            case 1:
                return "SIGHUP";
            case 2:
                return "SIGINT";
            case 3:
                return "SIGQUIT";
            case 4:
                return "SIGILL";
            case 5:
                return "SIGTRAP";
            case 6:
                return "SIGABRT";
            case 7:
                return "SIGBUS";
            case 8:
                return "SIGFPE";
            case 9:
                return "SIGKILL";
            case 11:
                return "SIGSEGV";
            case 13:
                return "SIGPIPE";
            case 15:
                return "SIGTERM";
            case 31:
                return "SIGSYS";
            default:
                return "signal " + signal;
        }
    }
}
