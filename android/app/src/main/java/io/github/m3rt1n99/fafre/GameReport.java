package io.github.m3rt1n99.fafre;

import android.content.Context;
import android.os.Build;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * The game's report, from its process (GameActivity, ":game") to the app's: game.txt in the app's
 * files, which the activity begins and the host continues line by line
 * (android/native/src/host/GameHost.cpp), and the engine's own log. When the process is gone, the
 * report gets how it ended: the program's exit code, which the host writes when the program
 * returns, or what Android says of the process (the engine ends its process itself when it cannot
 * start, as on Windows), with what the engine's log says of it.
 */
final class GameReport {
    /** The tombstone's name in the app's files and in the results zip. */
    static final String TOMBSTONE = "game-tombstone.pb";
    static final String PROCESS = ":game";
    /** The report's last line begins with this: how the game ended. */
    static final String END = "END: ";

    /** The host's line when the program returned: "<ms> ms: exit <code>". */
    private static final Pattern EXIT = Pattern.compile("^(\\d+) ms: exit (-?\\d+)$", Pattern.MULTILINE);
    /** The host's line when the first frame went to the screen. */
    private static final Pattern FIRST_FRAME = Pattern.compile("^\\d+ ms: first frame$", Pattern.MULTILINE);
    /** The host's line when the program stopped on a fatal error, with the lines its message goes on in. */
    private static final Pattern FATAL = Pattern.compile("^\\d+ ms: fatal: (.*(?:\\n(?!\\d+ ms: ).+)*)", Pattern.MULTILINE);
    /** How much of the engine's log's end is read for its last warning. */
    private static final int LOG_TAIL = 1 << 16;

    private static long sLoaded = Long.MIN_VALUE;
    private static boolean sAwaiting;

    private GameReport() {
    }

    static File file(Context context) {
        return new File(context.getFilesDir(), "game.txt");
    }

    static File tombstone(Context context) {
        return new File(context.getFilesDir(), TOMBSTONE);
    }

    /** The game's folder in the app's storage: its HOME, its vault and the engine's log. */
    static File folder(Storage storage) {
        return new File(storage.root, "game");
    }

    static File log(Storage storage) {
        return new File(folder(storage), "engine.sclog");
    }

    /** A new game (its own process): no report and no tombstone yet. */
    static void clear(Context context) {
        file(context).delete();
        tombstone(context).delete();
    }

    /** Whether the report says that the first frame came. */
    static boolean firstFrame(String report) {
        return FIRST_FRAME.matcher(report).find();
    }

    /** Whether the report says that the program ended or failed, with its process still there. */
    static boolean ended(String report) {
        return EXIT.matcher(report).find() || FATAL.matcher(report).find();
    }

    /** A line from the activity's side into the report. */
    static void append(Context context, String line) {
        try (OutputStream out = new FileOutputStream(file(context), true)) {
            out.write((line + "\n").getBytes(StandardCharsets.UTF_8));
        } catch (IOException e) {
            // The report has the host's lines anyway.
        }
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
            TestState.game("");
            return;
        }
        String report;
        try {
            report = new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
        } catch (IOException e) {
            return;
        }
        sAwaiting = false;
        if (!report.contains("\n" + END) && !report.startsWith(END)) {
            if (ProcessExit.running(context, PROCESS)) {
                sAwaiting = true;
                if (stamp != sLoaded) {
                    sLoaded = stamp;
                    TestState.game(report);
                }
                return;
            }
            String end = end(context, report, file.lastModified());
            append(context, end);
            report += end + "\n";
            stamp = file.lastModified() * 31 + file.length();
        }
        sLoaded = stamp;
        TestState.game(report);
        TestState.line(TestState.gameVerdict());
    }

    /** How the game ended, for a report whose process is gone; `since` is the report's last write. */
    private static String end(Context context, String report, long since) {
        Matcher fatal = FATAL.matcher(report);
        if (fatal.find()) {
            return END + "FAILED: " + fatal.group(1).replace('\n', ' ');
        }
        String engine = engineSummary(context);
        String tail = engine.isEmpty() ? "" : "; " + engine;
        Matcher exit = EXIT.matcher(report);
        String code = null;
        String after = null;
        while (exit.find()) {
            after = exit.group(1);
            code = exit.group(2);
        }
        if (code != null) {
            return END + "the program returned " + code + " after " + seconds(after) + tail;
        }
        if (Build.VERSION.SDK_INT < 30) {
            return END + "the game's process ended" + tail;
        }
        ProcessExit.End end = ProcessExit.last(context, PROCESS, since, tombstone(context));
        if (end == null) {
            return END + "the game's process ended, and Android gives no reason" + tail;
        }
        if (end.crashed()) {
            return END + "CRASHED: " + end.describe() + (end.tombstone ? " (tombstone in the results: " + TOMBSTONE + ")" : "")
                + tail;
        }
        if (end.exitedItself()) {
            return END + "the engine ended its process, exit code " + end.status + tail;
        }
        return END + "the process ended: " + end.describe() + tail;
    }

    private static String seconds(String milliseconds) {
        try {
            return String.format(java.util.Locale.ROOT, "%.1f s", Long.parseLong(milliseconds) / 1000.0);
        } catch (NumberFormatException e) {
            return milliseconds + " ms";
        }
    }

    /**
     * What the engine's log says of the end: why the Vulkan renderer could not start, when it was
     * tried ("renderer: trying Vulkan" and the GAL exception's error line after it); else its last
     * warning, with the line that continues it. "" without a log.
     */
    private static String engineSummary(Context context) {
        String[] lines = logTail(context);
        String vulkan = null;
        boolean tryingVulkan = false;
        String warning = null;
        for (int i = 0; i < lines.length; ++i) {
            String line = lines[i].trim();
            if (line.startsWith("info: renderer: trying ")) {
                tryingVulkan = line.equals("info: renderer: trying Vulkan");
            } else if (tryingVulkan && line.startsWith("error: ")) {
                vulkan = line.substring("error: ".length());
                tryingVulkan = false;
            }
            if (line.startsWith("warning: ")) {
                warning = line.substring("warning: ".length());
                // A message of several lines goes on indented, without a severity.
                if (i + 1 < lines.length && lines[i + 1].startsWith(" ") && !lines[i + 1].trim().isEmpty()) {
                    warning += " " + lines[i + 1].trim();
                }
            }
        }
        if (vulkan != null) {
            return "Vulkan: " + vulkan;
        }
        return warning != null ? "the engine's last warning: " + warning : "";
    }

    /** The lines of the engine log's last LOG_TAIL bytes; none without a log. */
    private static String[] logTail(Context context) {
        File log;
        try {
            log = log(new Storage(context));
        } catch (IOException e) {
            return new String[0];
        }
        if (!log.isFile()) {
            return new String[0];
        }
        try (RandomAccessFile in = new RandomAccessFile(log, "r")) {
            long length = in.length();
            int size = (int) Math.min(length, LOG_TAIL);
            byte[] bytes = new byte[size];
            in.seek(length - size);
            in.readFully(bytes);
            return new String(bytes, StandardCharsets.UTF_8).split("\n");
        } catch (IOException e) {
            return new String[0];
        }
    }
}
