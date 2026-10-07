package io.github.m3rt1n99.fafre;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * Small readers for procfs and sysfs files (the replay test's telemetry, {@link RunTelemetry}): whole
 * small files, /proc/&lt;pid&gt;/stat lines, schedstat, the "Key:\tvalue" lines of status, and CPU lists
 * such as "0-3,7".
 *
 * <p>Pure Java (no Android APIs) so it can be exercised on a host JVM against a copied /proc tree.
 * Every reader returns null (or an empty result) instead of throwing when a file is missing or SELinux
 * refuses it: what the app may read differs between Android versions and vendors, and a missing value
 * is recorded as such.
 */
final class ProcFiles {
    private static final int MAX_SMALL = 64 * 1024;

    private ProcFiles() {
    }

    /** The file as text (at most {@code maxBytes}), or null when it cannot be read. */
    static String read(File file, int maxBytes) {
        try {
            return readOrThrow(file, maxBytes);
        } catch (IOException | RuntimeException e) {
            return null;
        }
    }

    static String read(File file) {
        return read(file, MAX_SMALL);
    }

    /** The trimmed text, or "unreadable: <reason>" / "empty", for recording what a file said. */
    static String describe(File file) {
        try {
            String text = readOrThrow(file, MAX_SMALL);
            text = text.trim();
            return text.isEmpty() ? "empty" : text;
        } catch (IOException | RuntimeException e) {
            String message = e.getMessage() != null ? e.getMessage() : e.getClass().getSimpleName();
            // FileNotFoundException's message repeats the path; keep the reason in parentheses.
            int paren = message.lastIndexOf(" (");
            if (paren >= 0 && message.endsWith(")")) {
                message = message.substring(paren + 2, message.length() - 1);
            }
            return "unreadable: " + message;
        }
    }

    static String readOrThrow(File file, int maxBytes) throws IOException {
        // procfs and sysfs report a size of 0 or 4096; read until EOF instead of trusting length().
        byte[] buffer = new byte[Math.min(maxBytes, 8192)];
        int total = 0;
        try (InputStream in = new FileInputStream(file)) {
            int n;
            while (total < maxBytes && (n = in.read(buffer, total, Math.min(buffer.length - total, maxBytes - total))) > 0) {
                total += n;
                if (total == buffer.length && total < maxBytes) {
                    byte[] bigger = new byte[Math.min(maxBytes, buffer.length * 2)];
                    System.arraycopy(buffer, 0, bigger, 0, total);
                    buffer = bigger;
                }
            }
        }
        return new String(buffer, 0, total, StandardCharsets.UTF_8);
    }

    /** First line's integer, or {@code fallback}. */
    static long readLong(File file, long fallback) {
        String text = read(file, 256);
        if (text == null) {
            return fallback;
        }
        try {
            return Long.parseLong(text.trim().split("\\s+")[0]);
        } catch (NumberFormatException | ArrayIndexOutOfBoundsException e) {
            return fallback;
        }
    }

    /** The value of a "Key:\tvalue" line (status files), or null. */
    static String statusValue(String status, String key) {
        if (status == null) {
            return null;
        }
        String prefix = key + ":";
        int at = 0;
        while (at < status.length()) {
            int end = status.indexOf('\n', at);
            if (end < 0) {
                end = status.length();
            }
            if (status.startsWith(prefix, at)) {
                return status.substring(at + prefix.length(), end).trim();
            }
            at = end + 1;
        }
        return null;
    }

    static long statusLong(String status, String key, long fallback) {
        String value = statusValue(status, key);
        if (value == null) {
            return fallback;
        }
        try {
            return Long.parseLong(value.split("\\s+")[0]);
        } catch (NumberFormatException e) {
            return fallback;
        }
    }

    /** "0-3,5,7" → [0, 1, 2, 3, 5, 7]; an empty list for null or garbage. */
    static List<Integer> parseCpuList(String text) {
        List<Integer> out = new ArrayList<>();
        if (text == null) {
            return out;
        }
        for (String part : text.trim().split(",")) {
            part = part.trim();
            if (part.isEmpty()) {
                continue;
            }
            try {
                int dash = part.indexOf('-');
                if (dash > 0) {
                    int from = Integer.parseInt(part.substring(0, dash).trim());
                    int to = Integer.parseInt(part.substring(dash + 1).trim());
                    for (int cpu = from; cpu <= to && cpu - from < 4096; ++cpu) {
                        out.add(cpu);
                    }
                } else {
                    out.add(Integer.parseInt(part));
                }
            } catch (NumberFormatException e) {
                return new ArrayList<>();
            }
        }
        return out;
    }

    /** One /proc/&lt;pid&gt;[/task/&lt;tid&gt;]/stat line (proc(5)); fields by their 1-based number. */
    static final class Stat {
        String comm = "";
        char state = '?';
        int ppid;
        long utime;
        long stime;
        int priority;
        int nice;
        int numThreads;
        long startTime;
        long rssPages;
        int processor = -1;
        int rtPriority;
        int policy;

        long ticks() {
            return utime + stime;
        }

        /**
         * Parses a stat line. The command name sits in parentheses and may itself contain spaces and
         * parentheses, so the fields after it are counted from the last ')'.
         */
        static Stat parse(String text) {
            if (text == null) {
                return null;
            }
            int open = text.indexOf('(');
            int close = text.lastIndexOf(')');
            if (open < 0 || close < open || close + 2 > text.length()) {
                return null;
            }
            String[] f = text.substring(close + 2).trim().split("\\s+");
            // f[n - 3] is field n.
            if (f.length < 20 || f[0].isEmpty()) {
                return null;
            }
            Stat stat = new Stat();
            try {
                stat.comm = text.substring(open + 1, close);
                stat.state = f[0].charAt(0);
                stat.ppid = Integer.parseInt(f[1]);
                stat.utime = Long.parseLong(f[11]);
                stat.stime = Long.parseLong(f[12]);
                stat.priority = Integer.parseInt(f[15]);
                stat.nice = Integer.parseInt(f[16]);
                stat.numThreads = Integer.parseInt(f[17]);
                stat.startTime = Long.parseLong(f[19]);
                stat.rssPages = f.length > 21 ? Long.parseLong(f[21]) : 0;
                stat.processor = f.length > 36 ? Integer.parseInt(f[36]) : -1;
                stat.rtPriority = f.length > 37 ? Integer.parseInt(f[37]) : 0;
                stat.policy = f.length > 38 ? Integer.parseInt(f[38]) : 0;
            } catch (NumberFormatException e) {
                return null;
            }
            return stat;
        }
    }

    /** /proc/&lt;pid&gt;/task/&lt;tid&gt;/schedstat: time on a CPU, time runnable on a run queue (both ns), slices. */
    static long[] parseSchedstat(String text) {
        if (text == null) {
            return null;
        }
        String[] f = text.trim().split("\\s+");
        if (f.length < 3) {
            return null;
        }
        try {
            return new long[] {Long.parseLong(f[0]), Long.parseLong(f[1]), Long.parseLong(f[2])};
        } catch (NumberFormatException e) {
            return null;
        }
    }

    /** A value of /proc/&lt;pid&gt;/task/&lt;tid&gt;/sched ("se.nr_migrations    :   12"), or -1. */
    static long schedValue(String sched, String key) {
        if (sched == null) {
            return -1;
        }
        int at = 0;
        while (at < sched.length()) {
            int end = sched.indexOf('\n', at);
            if (end < 0) {
                end = sched.length();
            }
            String line = sched.substring(at, end);
            int colon = line.indexOf(':');
            if (colon > 0 && line.substring(0, colon).trim().equals(key)) {
                try {
                    String value = line.substring(colon + 1).trim();
                    int dot = value.indexOf('.');
                    return Long.parseLong(dot >= 0 ? value.substring(0, dot) : value);
                } catch (NumberFormatException e) {
                    return -1;
                }
            }
            at = end + 1;
        }
        return -1;
    }

    static String policyName(int policy) {
        switch (policy) {
            case 0:
                return "SCHED_OTHER";
            case 1:
                return "SCHED_FIFO";
            case 2:
                return "SCHED_RR";
            case 3:
                return "SCHED_BATCH";
            case 5:
                return "SCHED_IDLE";
            case 6:
                return "SCHED_DEADLINE";
            default:
                return "policy " + policy;
        }
    }

    /** Numeric names in a /proc directory (pids or tids), ascending. */
    static List<Integer> numericEntries(File dir) {
        List<Integer> out = new ArrayList<>();
        String[] names = dir.list();
        if (names == null) {
            return out;
        }
        for (String name : names) {
            if (!name.isEmpty() && name.length() <= 10 && name.chars().allMatch(Character::isDigit)) {
                out.add(Integer.parseInt(name));
            }
        }
        java.util.Collections.sort(out);
        return out;
    }

    /** The NUL-separated cmdline's first argument, or null. */
    static String firstArgument(File cmdline) {
        String text = read(cmdline, 8192);
        if (text == null || text.isEmpty()) {
            return null;
        }
        int nul = text.indexOf('\0');
        return nul >= 0 ? text.substring(0, nul) : text.trim();
    }

    /**
     * The program a process runs, from its NUL-separated cmdline: argv[0], or argv[1] when argv[0] is
     * the ARM translation's binfmt_misc runner (an emulator running an arm64 APK: the kernel starts
     * {@code /system/bin/ndk_translation_program_runner_binfmt_misc_arm64 <program> <args>}). Null when
     * unreadable.
     */
    static String programOf(File cmdline) {
        String text = read(cmdline, 8192);
        if (text == null || text.isEmpty()) {
            return null;
        }
        String[] args = text.split("\0", 3);
        String first = args[0].trim();
        String name = first.substring(first.lastIndexOf('/') + 1);
        if (args.length >= 2 && name.startsWith("ndk_translation_program_runner") && !args[1].isEmpty()) {
            return args[1];
        }
        return first;
    }
}
