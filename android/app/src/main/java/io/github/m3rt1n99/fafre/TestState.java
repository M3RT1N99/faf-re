package io.github.m3rt1n99.fafre;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;

/**
 * What the tests are doing and what they found, shared by TestService (which runs them) and
 * MainActivity (which shows it, polling). Every access holds the class lock.
 */
final class TestState {
    /** One replay of a parity run. */
    static final class Replay {
        final long id;
        String state = "queued";
        String map = "";
        int players;
        int gameVersion;
        Integer expectedBeats;
        int beat = -1;
        Integer firstMismatch;
        int mismatches;
        int outvoted;
        double loadSeconds;
        double playSeconds;
        double wallSeconds;
        String note = "";
        SpeedSampler.Summary speed;

        Replay(long id) {
            this.id = id;
        }

        String line() {
            StringBuilder out = new StringBuilder();
            out.append(id).append("  ").append(state.toUpperCase());
            if (!map.isEmpty()) {
                out.append("  ").append(map);
            }
            if (players > 0) {
                out.append(", ").append(players).append(" players");
            }
            if ("running".equals(state) && beat >= 0) {
                out.append("  beat ").append(beat);
                if (expectedBeats != null) {
                    out.append(" / ").append(expectedBeats);
                }
            }
            if (firstMismatch != null) {
                out.append("  first mismatch at beat ").append(firstMismatch).append(" (").append(mismatches).append(" in all)");
            }
            if (playSeconds > 0) {
                out.append(String.format(java.util.Locale.ROOT, "  %.0f s", playSeconds));
                if (beat > 0) {
                    out.append(String.format(java.util.Locale.ROOT, " (%.0f beats/s)", beat / playSeconds));
                }
            }
            if (outvoted > 0) {
                out.append("  [").append(outvoted).append(" live-game desync(s) ignored]");
            }
            if (!note.isEmpty()) {
                out.append("  ").append(note);
            }
            String speedLine = speed != null ? speed.line() : "";
            if (!speedLine.isEmpty()) {
                out.append("\n    ").append(speedLine);
            }
            return out.toString();
        }
    }

    private static final int LOG_LINES = 300;
    private static final ArrayDeque<String> sLog = new ArrayDeque<>();
    private static final List<Replay> sReplays = new ArrayList<>();
    private static boolean sRunning;
    private static String sJob = "";
    private static String sStep = "";
    private static String sMathOutput = "";
    private static String sMathVerdict = "";
    private static long sRevision;
    private static boolean sVisible;
    private static String sRendererReport = "";

    private TestState() {
    }

    static synchronized boolean begin(String job) {
        if (sRunning) {
            return false;
        }
        sRunning = true;
        sJob = job;
        sStep = "";
        Cancellation.reset();
        ++sRevision;
        return true;
    }

    static synchronized void end() {
        sRunning = false;
        sStep = "";
        ++sRevision;
    }

    static synchronized boolean running() {
        return sRunning;
    }

    static synchronized String job() {
        return sJob;
    }

    static synchronized void step(String text) {
        sStep = text;
        ++sRevision;
    }

    static synchronized String step() {
        return sStep;
    }

    static synchronized void line(String text) {
        sLog.addLast(text);
        while (sLog.size() > LOG_LINES) {
            sLog.removeFirst();
        }
        ++sRevision;
    }

    static synchronized List<String> log() {
        return new ArrayList<>(sLog);
    }

    static synchronized void math(String verdict, String output) {
        sMathVerdict = verdict;
        sMathOutput = output;
        ++sRevision;
    }

    static synchronized String mathVerdict() {
        return sMathVerdict;
    }

    static synchronized String mathOutput() {
        return sMathOutput;
    }

    static synchronized void replays(List<Replay> replays) {
        sReplays.clear();
        sReplays.addAll(replays);
        ++sRevision;
    }

    static synchronized List<String> replayLines() {
        List<String> lines = new ArrayList<>();
        for (Replay replay : sReplays) {
            lines.add(replay.line());
        }
        return lines;
    }

    static synchronized List<Replay> replaySnapshot() {
        return new ArrayList<>(sReplays);
    }

    /** The renderer test's report, from its process (RendererReport). */
    static synchronized void renderer(String report) {
        sRendererReport = report;
        ++sRevision;
    }

    static synchronized String rendererReport() {
        return sRendererReport;
    }

    /** "Renderer: PASS: 600 frames ..." from the report, or that it has not run. */
    static synchronized String rendererVerdict() {
        for (String line : sRendererReport.split("\n")) {
            if (RendererReport.isVerdict(line)) {
                return "Renderer: " + line;
            }
        }
        return sRendererReport.isEmpty() ? "" : "Renderer: running, no verdict yet";
    }

    /** Whether MainActivity is on screen (between onStart and onStop). */
    static synchronized void visible(boolean visible) {
        sVisible = visible;
    }

    static synchronized boolean visible() {
        return sVisible;
    }

    /** Called after a field of a Replay changed. */
    static synchronized void touch() {
        ++sRevision;
    }

    static synchronized long revision() {
        return sRevision;
    }
}
