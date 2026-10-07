package io.github.m3rt1n99.fafre;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Reads the runner's stdout+stderr line by line (src/sdk/moho/app/HeadlessReplay.cpp's "[headless]"
 * lines, RunnerMain's and the low arena's "[lowarena]" lines, the crash handler's "[runner] CRASH"
 * line, the arena probe's "probe" lines) and keeps what the replay test shows and records.
 *
 * <p>The listener runs on the reader thread while the UI samples the fields, so every access is
 * synchronized. Pure Java (no Android APIs) so it can be exercised on a host JVM.
 */
final class ReplayOutput {
    private static final Pattern REPLAY = Pattern.compile(
            "^\\[headless\\] replay .*, map (\\S+), (\\d+) beats, (\\d+) recorded checksums at (\\d+) beats, (.*)$");
    private static final Pattern LOADED = Pattern.compile("^\\[headless\\] scenario loaded in ([\\d.]+)s");
    private static final Pattern SIM_CREATED = Pattern.compile("^\\[headless\\] sim created");
    private static final Pattern BEAT = Pattern.compile(
            "^\\[headless\\] beat (\\d+)/(\\d+)\\s+([\\d.]+)s\\s+([\\d.]+) beats/s\\s+desyncs (\\d+)");
    private static final Pattern CHECKSUM = Pattern.compile(
            "^\\[headless\\] checksum beat (\\d+) .* (match|MISMATCH) \\((\\d+)/(\\d+)\\)");
    private static final Pattern GAME_OVER = Pattern.compile("^\\[headless\\] game over at beat (\\d+)");
    private static final Pattern RESULT = Pattern.compile("^\\[headless\\] (RESULT(?: \\(before teardown\\))?) (.*)$");
    private static final Pattern PROBLEM = Pattern.compile("^\\[headless\\] (CRASH|FATAL|ASSERTION) (.*)$");
    private static final Pattern USAGE = Pattern.compile("^\\[headless\\] usage:");
    private static final Pattern ENGINE_AT = Pattern.compile(
            "^\\[lowarena\\] (\\S+) at (0x[0-9a-fA-F]+|\\S+), engine thread stack near (\\S+)(.*)$");
    private static final Pattern ARENA_REPORT = Pattern.compile("^\\[lowarena\\] enabled=.*");
    private static final Pattern ARENA_FULL = Pattern.compile("^\\[lowarena\\] no room below.*");
    private static final Pattern RUNNER_CRASH = Pattern.compile("^\\[runner\\] CRASH\\b.*");
    /** RunnerMain's own error lines ("faf_headless_runner: cannot load ...", "[runner] cannot convert ..."). */
    private static final Pattern RUNNER_ERROR = Pattern.compile(
            "^(?:faf_headless_runner: |\\[runner\\] (?!CRASH|input ))(.*)$");
    private static final Pattern CRASH_SIGNAL = Pattern.compile("sig=\\d+ \\((\\w+)\\)");
    private static final Pattern CRASH_PC = Pattern.compile("pc=(0x[0-9a-fA-F]+)(?: \\(([^)]+)\\))?");
    private static final Pattern PROBE_RESULT = Pattern.compile("^probe RESULT (PASS|FAIL): (\\d+) of (\\d+)(.*)$");
    private static final Pattern PROBE_CAPACITY = Pattern.compile("^probe capacity: (.*)$");
    private static final Pattern PROBE_FAIL = Pattern.compile("^probe FAIL (.*)$");
    private static final Pattern PROBE_HEADER = Pattern.compile("^probe LowArena probe: (.*)$");
    private static final Pattern KEY_VALUE = Pattern.compile("(\\w+)=(\\S+)");

    /** Where the replay step is, for the progress display. */
    enum Phase {
        STARTING("Starting"),
        LOADING("Loading the scenario"),
        PLAYING("Playing"),
        ENDING("Shutting down"),
        DONE("Done");

        final String label;

        Phase(String label) {
            this.label = label;
        }
    }

    private Phase mPhase = Phase.STARTING;
    private String mMap = "";
    private int mTotalBeats = -1;
    private int mRecordedChecksums = -1;
    private boolean mHasEndGame;
    private double mLoadSeconds = -1;
    private int mBeat = -1;
    private double mBeatsPerSecond;
    private int mDesyncs;
    private int mChecks;
    private int mMatches;
    private int mGameOverBeat = -1;
    private String mResultLine;
    private final Map<String, String> mResult = new LinkedHashMap<>();
    private final List<String> mProblems = new ArrayList<>();
    private boolean mUsage;
    private String mEngineAt;
    private String mArenaReport;
    private String mArenaFull;
    private String mCrash;
    private final List<String> mCrashLines = new ArrayList<>();
    private final List<String> mRunnerErrors = new ArrayList<>();
    private String mProbeHeader;
    private String mProbeResult;
    private boolean mProbePass;
    private String mProbeCapacity;
    private final List<String> mProbeFailures = new ArrayList<>();
    private int mLines;

    /** Takes one line; returns true if it changed what the progress display shows. */
    synchronized boolean accept(String line) {
        ++mLines;
        Matcher m;
        if ((m = BEAT.matcher(line)).find()) {
            mBeat = Integer.parseInt(m.group(1));
            mTotalBeats = Integer.parseInt(m.group(2));
            mBeatsPerSecond = parseDouble(m.group(4));
            mDesyncs = Integer.parseInt(m.group(5));
            mPhase = Phase.PLAYING;
            return true;
        }
        if ((m = CHECKSUM.matcher(line)).find()) {
            int beat = Integer.parseInt(m.group(1));
            mMatches += Integer.parseInt(m.group(3));
            mChecks += Integer.parseInt(m.group(4));
            mBeat = Math.max(mBeat, beat);
            return true;
        }
        if ((m = REPLAY.matcher(line)).find()) {
            mMap = m.group(1);
            mTotalBeats = Integer.parseInt(m.group(2));
            mRecordedChecksums = Integer.parseInt(m.group(4));
            mHasEndGame = m.group(5).contains("has EndGame");
            mPhase = Phase.LOADING;
            return true;
        }
        if ((m = LOADED.matcher(line)).find()) {
            mLoadSeconds = parseDouble(m.group(1));
            return true;
        }
        if (SIM_CREATED.matcher(line).find()) {
            mPhase = Phase.PLAYING;
            mBeat = Math.max(mBeat, 0);
            return true;
        }
        if ((m = GAME_OVER.matcher(line)).find()) {
            mGameOverBeat = Integer.parseInt(m.group(1));
            return true;
        }
        if ((m = RESULT.matcher(line)).find()) {
            mResultLine = line;
            mResult.clear();
            Matcher kv = KEY_VALUE.matcher(m.group(2));
            while (kv.find()) {
                mResult.put(kv.group(1), kv.group(2));
            }
            mPhase = Phase.ENDING;
            return true;
        }
        if ((m = PROBLEM.matcher(line)).find()) {
            if (mProblems.size() < 20) {
                mProblems.add(m.group(1) + " " + m.group(2));
            }
            return false;
        }
        if (USAGE.matcher(line).find()) {
            mUsage = true;
            return false;
        }
        if (ENGINE_AT.matcher(line).find()) {
            mEngineAt = line;
            return false;
        }
        if (ARENA_REPORT.matcher(line).find()) {
            mArenaReport = line;
            return false;
        }
        if (ARENA_FULL.matcher(line).find()) {
            if (mArenaFull == null) {
                mArenaFull = line;
            }
            return false;
        }
        if (RUNNER_CRASH.matcher(line).find()) {
            if (mCrash == null) {
                mCrash = line;
            }
            if (mCrashLines.size() < 40) {
                mCrashLines.add(line);
            }
            return true;
        }
        if ((m = RUNNER_ERROR.matcher(line)).find()) {
            if (mRunnerErrors.size() < 10) {
                mRunnerErrors.add(m.group(1));
            }
            return false;
        }
        if ((m = PROBE_RESULT.matcher(line)).find()) {
            mProbeResult = line.substring("probe RESULT ".length());
            mProbePass = "PASS".equals(m.group(1));
            return true;
        }
        if ((m = PROBE_CAPACITY.matcher(line)).find()) {
            mProbeCapacity = m.group(1);
            return true;
        }
        if ((m = PROBE_FAIL.matcher(line)).find()) {
            if (mProbeFailures.size() < 20) {
                mProbeFailures.add(m.group(1));
            }
            return false;
        }
        if ((m = PROBE_HEADER.matcher(line)).find()) {
            mProbeHeader = m.group(1);
            return false;
        }
        return false;
    }

    private static double parseDouble(String text) {
        try {
            return Double.parseDouble(text);
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    synchronized Phase phase() {
        return mPhase;
    }

    synchronized String map() {
        return mMap;
    }

    synchronized int totalBeats() {
        return mTotalBeats;
    }

    synchronized int recordedChecksums() {
        return mRecordedChecksums;
    }

    synchronized boolean hasEndGame() {
        return mHasEndGame;
    }

    synchronized double loadSeconds() {
        return mLoadSeconds;
    }

    synchronized int beat() {
        return mBeat;
    }

    synchronized double beatsPerSecond() {
        return mBeatsPerSecond;
    }

    synchronized int desyncs() {
        return mDesyncs;
    }

    synchronized int checks() {
        return mChecks;
    }

    synchronized int mismatches() {
        return mChecks - mMatches;
    }

    synchronized int gameOverBeat() {
        return mGameOverBeat;
    }

    synchronized String resultLine() {
        return mResultLine;
    }

    /** A key=value field of the RESULT line (end, exit, beats, chain, ...), or null. */
    synchronized String result(String key) {
        return mResult.get(key);
    }

    synchronized List<String> problems() {
        return new ArrayList<>(mProblems);
    }

    synchronized boolean usage() {
        return mUsage;
    }

    synchronized String engineAt() {
        return mEngineAt;
    }

    synchronized String arenaReport() {
        return mArenaReport;
    }

    synchronized String arenaFull() {
        return mArenaFull;
    }

    synchronized String crash() {
        return mCrash;
    }

    /** Every "[runner] CRASH" line: the report, then the frames. */
    synchronized List<String> crashLines() {
        return new ArrayList<>(mCrashLines);
    }

    /**
     * "SIGSEGV at libfafengine.so+0x5f2a10" from the crash handler's first line
     * ("[runner] CRASH sig=11 (SIGSEGV) code=1 (SEGV_MAPERR) addr=0x... pc=0x... (libfafengine.so+0x...) ..."),
     * or null.
     */
    synchronized String crashSummary() {
        if (mCrash == null) {
            return null;
        }
        Matcher signal = CRASH_SIGNAL.matcher(mCrash);
        Matcher pc = CRASH_PC.matcher(mCrash);
        String name = signal.find() ? signal.group(1) : "a fatal signal";
        String where = pc.find() ? (pc.group(2) != null ? pc.group(2) : pc.group(1)) : null;
        return name + (where != null ? " at " + where : "");
    }

    synchronized List<String> runnerErrors() {
        return new ArrayList<>(mRunnerErrors);
    }

    synchronized String probeHeader() {
        return mProbeHeader;
    }

    synchronized String probeResult() {
        return mProbeResult;
    }

    synchronized boolean probePass() {
        return mProbePass;
    }

    synchronized String probeCapacity() {
        return mProbeCapacity;
    }

    synchronized List<String> probeFailures() {
        return new ArrayList<>(mProbeFailures);
    }

    synchronized int lines() {
        return mLines;
    }

    /** "beat 1500/7315 · 42 beats/s · checks 30, mismatches 30" for the progress line. */
    synchronized String describeProgress() {
        StringBuilder out = new StringBuilder();
        if (mBeat >= 0) {
            out.append("beat ").append(mBeat);
            if (mTotalBeats > 0) {
                out.append('/').append(mTotalBeats);
            }
        }
        if (mBeatsPerSecond > 0) {
            out.append(out.length() > 0 ? " · " : "").append(String.format(java.util.Locale.ROOT, "%.0f beats/s",
                    mBeatsPerSecond));
        }
        if (mChecks > 0) {
            out.append(out.length() > 0 ? " · " : "").append("recorded checksums ").append(mMatches)
                    .append('/').append(mChecks).append(" match");
        }
        if (mGameOverBeat >= 0) {
            out.append(out.length() > 0 ? " · " : "").append("game over at beat ").append(mGameOverBeat);
        }
        return out.toString();
    }
}
