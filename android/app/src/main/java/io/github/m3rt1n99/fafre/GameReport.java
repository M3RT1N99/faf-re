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
import java.time.Instant;
import java.time.format.DateTimeParseException;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * The game's report, from its process (GameActivity, ":game") to the app's: game.txt in the app's
 * files, which the activity begins and the host continues line by line
 * (android/native/src/host/GameHost.cpp), and the engine's own log. When the process is gone, the
 * report gets how it ended: the program's exit code, which the host writes when the program
 * returns, or what Android says of the process (the engine ends its process itself when it cannot
 * start, as on Windows), with what the engine's log says of it. The last game's report and
 * tombstone stay beside the new game's.
 */
final class GameReport {
    /** The tombstone's name in the app's files and in the results zip. */
    static final String TOMBSTONE = "game-tombstone.pb";
    /** The last game's report and tombstone, by these names in the app's files and in the results zip. */
    static final String PREVIOUS = "game-previous.txt";
    static final String PREVIOUS_TOMBSTONE = "game-previous-tombstone.pb";
    static final String PROCESS = ":game";
    /** The report's last line begins with this: how the game ended. */
    static final String END = "END: ";
    /** The report's first line: when the game began, as an ISO-8601 instant. */
    static final String STARTED = "game: started ";

    /** How the end's lines begin, by how the game ended; a start that failed has GameStart's (FAIL). */
    private static final String FAILED = "FAILED: ";
    private static final String RETURNED = "the program returned ";
    private static final String CRASHED = "CRASHED: ";
    private static final String EXITED = "the engine ended its process, exit code ";
    private static final String PROCESS_ENDED = "the process ended: ";
    private static final String START_FAILED = "FAIL: ";
    /** What the end adds of the engine's words (its log's) and Android's after its own, which can name a path or a player. */
    private static final String MORE = "; ";
    private static final Pattern RETURNED_CODE = Pattern.compile("^" + RETURNED + "(-?\\d+) after ");
    private static final Pattern EXITED_CODE = Pattern.compile("^" + EXITED + "(-?\\d+)");
    /** ProcessExit.End.describe's reason and status, without Android's description after them. */
    private static final Pattern REASON = Pattern.compile("^(" + CRASHED + "|" + PROCESS_ENDED + ")(.+?, status -?\\d+)");
    /** The line of a start that failed (GameStart.Failure.reportLine), with its code. */
    private static final Pattern START_FAILURE = Pattern.compile("^game: the start failed, ([A-Z_]+): ", Pattern.MULTILINE);

    /** The host's line when the program returned: "<ms> ms: exit <code>". */
    private static final Pattern EXIT = Pattern.compile("^(\\d+) ms: exit (-?\\d+)$", Pattern.MULTILINE);
    /** The host's line when the first frame went to the screen. */
    private static final Pattern FIRST_FRAME = Pattern.compile("^\\d+ ms: first frame$", Pattern.MULTILINE);
    /** The host's line when the program stopped on a fatal error, with the lines its message goes on in, also empty ones. */
    private static final Pattern FATAL = Pattern.compile("^\\d+ ms: fatal: (.*(?:\\n(?!\\d+ ms: ).*)*)", Pattern.MULTILINE);
    /** How much of the engine's log's end is read for its last warning. */
    private static final int LOG_TAIL = 1 << 16;
    /**
     * The run's end in the engine's log: its last entries, of which the END line quotes a warning or error, and none from
     * before them. An entry is a line with the indented lines that go on with it.
     */
    static final int END_OF_RUN_ENTRIES = 10;

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

    static File previous(Context context) {
        return new File(context.getFilesDir(), PREVIOUS);
    }

    static File previousTombstone(Context context) {
        return new File(context.getFilesDir(), PREVIOUS_TOMBSTONE);
    }

    /** The game's folder in the app's storage: its HOME, its vault and the engine's log. */
    static File folder(Storage storage) {
        return new File(storage.root, "game");
    }

    static File home(Storage storage) {
        return new File(folder(storage), "home");
    }

    static File log(Storage storage) {
        return new File(folder(storage), "engine.sclog");
    }

    /** The engine's newest crash report, in its HOME (gpg/core/diagnostics). */
    static File crashReport(Storage storage) {
        return new File(home(storage), "faf_crash_latest.txt");
    }

    /**
     * A new game (its own process): no report and no tombstone yet, and the last game's are the
     * previous one's. A game whose process died in the back has nobody's word on how it ended yet.
     */
    static void begin(Context context) {
        // The retired renderer test's report and tombstone (card 4, whose RendererReport.clear was all that deleted them).
        new File(context.getFilesDir(), "renderer.txt").delete();
        new File(context.getFilesDir(), "renderer-tombstone.pb").delete();
        File file = file(context);
        if (file.isFile()) {
            try {
                String report = new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
                if (!hasEnd(report)) {
                    append(context, end(context, report, file.lastModified()));
                }
            } catch (IOException e) {
                // It stays as it is.
            }
            replace(file, previous(context));
            replace(tombstone(context), previousTombstone(context));
        }
        append(context, STARTED + Instant.now());
    }

    /** `from` becomes `to`; without a `from`, `to` holds nothing of an earlier game either. */
    private static void replace(File from, File to) {
        to.delete();
        from.renameTo(to);
    }

    private static boolean hasEnd(String report) {
        return report.contains("\n" + END) || report.startsWith(END);
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
        if (!hasEnd(report)) {
            if (ProcessExit.running(context, PROCESS)) {
                sAwaiting = true;
                if (stamp != sLoaded) {
                    sLoaded = stamp;
                    TestState.game(report);
                }
                return;
            }
            report = withEnd(context, file, report);
            stamp = file.lastModified() * 31 + file.length();
        }
        sLoaded = stamp;
        TestState.game(report);
        TestState.line(TestState.gameVerdict());
    }

    /**
     * The last game's report as it is now, for the status a launcher reads (StatusProvider): with how the game ended once its
     * process is gone, as refresh adds it; "" before the first game.
     */
    static synchronized String current(Context context) {
        File file = file(context);
        String report;
        try {
            report = new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
        } catch (IOException e) {
            return "";
        }
        if (!report.isEmpty() && !hasEnd(report) && !ProcessExit.running(context, PROCESS)) {
            report = withEnd(context, file, report);
        }
        return report;
    }

    /** {@code report}, whose game's process is gone, with how it ended, which the file gets too. */
    private static String withEnd(Context context, File file, String report) {
        String end = end(context, report, file.lastModified());
        append(context, end);
        return report + end + "\n";
    }

    /** The report's END line without its "END: ", or null while the game has not ended. */
    static String endLine(String report) {
        for (String line : report.split("\n")) {
            if (line.startsWith(END)) {
                return line.substring(END.length());
            }
        }
        return null;
    }

    /** When the report's game began, in ms since 1970; null for a report without the line, an earlier release's. */
    static Long startedAt(String report) {
        for (String line : report.split("\n")) {
            if (line.startsWith(STARTED)) {
                try {
                    return Instant.parse(line.substring(STARTED.length()).trim()).toEpochMilli();
                } catch (DateTimeParseException e) {
                    return null;
                }
            }
        }
        return null;
    }

    /** The program's exit code, when it returned or the engine ended its process; null for any other end, and before it. */
    static Integer exitCode(String report) {
        String end = endLine(report);
        if (end == null) {
            return null;
        }
        for (Pattern pattern : new Pattern[] {RETURNED_CODE, EXITED_CODE}) {
            Matcher code = pattern.matcher(end);
            if (code.find()) {
                return Integer.valueOf(code.group(1));
            }
        }
        return null;
    }

    /** Whether the game crashed or stopped on a fatal error. */
    static boolean crashed(String report) {
        String end = endLine(report);
        return end != null && (end.startsWith(CRASHED) || end.startsWith(FAILED));
    }

    /**
     * How the game ended in this app's words alone: the END line without what it quotes of the engine's log, a fatal error,
     * Android's description and a failed start's message, which can name a path or a player. Null while it has not ended.
     */
    static String plainEnd(String report) {
        String end = endLine(report);
        if (end == null) {
            return null;
        }
        if (end.startsWith(START_FAILED)) {
            String code = null;
            Matcher failure = START_FAILURE.matcher(report);
            while (failure.find()) {
                code = failure.group(1);
            }
            return code != null ? "the start failed: " + code : "the start failed";
        }
        if (end.startsWith(FAILED)) {
            return FAILED + "the engine stopped on a fatal error";
        }
        Matcher reason = REASON.matcher(end);
        if (reason.find()) {
            return reason.group(1) + reason.group(2);
        }
        int more = end.indexOf(MORE);
        return more >= 0 ? end.substring(0, more) : end;
    }

    /** How the game ended, for a report whose process is gone; `since` is the report's last write. */
    private static String end(Context context, String report, long since) {
        // Before all else: a crash leaves its tombstone also after a fatal's line.
        ProcessExit.End end = ProcessExit.last(context, PROCESS, since, tombstone(context));
        // Without the file's name: a report and its tombstone change names together (begin).
        String saved = end != null && end.tombstone ? " (tombstone in the results)" : "";
        Matcher fatal = FATAL.matcher(report);
        if (fatal.find()) {
            return END + FAILED + fatal.group(1).trim().replaceAll("\\s*\\n\\s*", " ") + saved;
        }
        String engine = engineSummary(logTail(context));
        String tail = engine.isEmpty() ? "" : MORE + engine;
        Matcher exit = EXIT.matcher(report);
        String code = null;
        String after = null;
        while (exit.find()) {
            after = exit.group(1);
            code = exit.group(2);
        }
        if (code != null) {
            return END + RETURNED + code + " after " + seconds(after) + tail;
        }
        if (Build.VERSION.SDK_INT < 30) {
            return END + "the game's process ended" + tail;
        }
        if (end == null) {
            return END + "the game's process ended, and Android gives no reason" + tail;
        }
        if (end.crashed()) {
            return END + CRASHED + end.describe() + saved + tail;
        }
        if (end.exitedItself()) {
            return END + EXITED + end.status + tail;
        }
        return END + PROCESS_ENDED + end.describe() + tail;
    }

    private static String seconds(String milliseconds) {
        try {
            return String.format(java.util.Locale.ROOT, "%.1f s", Long.parseLong(milliseconds) / 1000.0);
        } catch (NumberFormatException e) {
            return milliseconds + " ms";
        }
    }

    /**
     * What the engine's log, {@code lines}, says of the end, from the run's end alone (END_OF_RUN_ENTRIES): why the Vulkan
     * renderer could not start, when it was tried ("renderer: trying Vulkan" and the GAL exception's error line after it);
     * else its last warning, with the line that continues it. "" for neither there, as a warning from before the end, such
     * as a probe of the first frames, says nothing of it.
     */
    static String engineSummary(String[] lines) {
        int end = endOfRun(lines);
        String vulkan = null;
        boolean tryingVulkan = false;
        String warning = null;
        for (int i = 0; i < lines.length; ++i) {
            String line = lines[i].trim();
            if (line.startsWith("info: renderer: trying ")) {
                tryingVulkan = line.equals("info: renderer: trying Vulkan");
            } else if (tryingVulkan && line.startsWith("error: ")) {
                vulkan = i >= end ? line.substring("error: ".length()) : null;
                tryingVulkan = false;
            }
            if (i >= end && line.startsWith("warning: ")) {
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

    /** The first of {@code lines} in the log's last END_OF_RUN_ENTRIES entries: 0 for a log of no more. */
    private static int endOfRun(String[] lines) {
        int entries = 0;
        for (int i = lines.length - 1; i >= 0; --i) {
            // An entry's own line has its severity first; the lines that go on with it are indented.
            if (!lines[i].isEmpty() && !Character.isWhitespace(lines[i].charAt(0)) && ++entries == END_OF_RUN_ENTRIES) {
                return i;
            }
        }
        return 0;
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
