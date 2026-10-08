package io.github.m3rt1n99.fafre;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * The menu replay's verdict from what one run read back: per frame PASS when its hash (FNV-1a 64 over R,G,B, as
 * the PC frame harness hashes frames) is the PC's Diligent-Vulkan hash from the trace, FAIL when it differs; for
 * the run PASS when every frame passed, FAIL on a differing frame or an error of the native side, INCOMPLETE when
 * it stopped before every frame was read back. The parity rule (max |delta| &lt;= 1 on &lt;= 0.1 % of the
 * pixels) needs the PC's pixels, so a differing frame is judged on the PC from the run's BMPs.
 *
 * <p>Pure Java (no Android APIs) so it can be exercised on a host JVM.
 */
final class MenuReplayVerdict {
    static final String PASS = "PASS";
    static final String FAIL = "FAIL";
    static final String INCOMPLETE = "INCOMPLETE";
    static final String NO_REFERENCE = "NO_REFERENCE";
    static final String FRAME_PASS = "pass";
    static final String FRAME_DIFFERS = "differs";
    static final String FRAME_NO_REFERENCE = "no-reference";
    static final String FRAME_NOT_REACHED = "not_reached";
    static final String FRAME_PENDING = "pending";

    /** One read-back frame as the app saw it, with what the native side said about it. */
    static final class Frame {
        final int frame;
        /** The PC's Diligent-Vulkan hash from the trace's metadata ("" if it has none). */
        String reference = "";
        /** Computed by the app from the pixels ("" when it got none). */
        String hash = "";
        String nativeHash = "";
        String nativeReference = "";
        String nativeVerdict = "";
        /** The native side: 1 when the bytes equal the trace's recorded readback, 0 when not, -1 unknown. */
        int identicalToRecording = -1;
        String error = "";
        String verdict = FRAME_NOT_REACHED;
        int width;
        int height;
        boolean opaque = true;
        String png = "";
        String bmp = "";
        String bmpBy = "";

        Frame(int frame) {
            this.frame = frame;
        }

        /** The app's own hash, else the native side's. */
        String effectiveHash() {
            return !hash.isEmpty() ? hash : nativeHash;
        }

        /** The trace's reference as the app read it, else the native side's. */
        String effectiveReference() {
            return !reference.isEmpty() ? reference : nativeReference;
        }

        /** Recomputes {@link #verdict} from the hashes. */
        String judge() {
            String value = effectiveHash();
            String expected = effectiveReference();
            if (value.isEmpty()) {
                verdict = error.isEmpty() ? FRAME_NOT_REACHED : FRAME_DIFFERS;
            } else if (expected.isEmpty()) {
                verdict = FRAME_NO_REFERENCE;
            } else {
                verdict = value.equalsIgnoreCase(expected) ? FRAME_PASS : FRAME_DIFFERS;
            }
            return verdict;
        }

        /** True when the app and the native side both hashed this frame and disagree. */
        boolean hashesDisagree() {
            return !hash.isEmpty() && !nativeHash.isEmpty() && !hash.equalsIgnoreCase(nativeHash);
        }

        /** The card's line for this frame: tone, text. */
        String[] line() {
            String value = effectiveHash();
            switch (verdict) {
                case FRAME_PASS:
                    return new String[] {"good", "Frame " + frame + ": PASS (" + value + ", equal to the PC's)"};
                case FRAME_DIFFERS:
                    if (value.isEmpty()) {
                        return new String[] {"bad", "Frame " + frame + ": FAIL: " + error};
                    }
                    return new String[] {"bad", "Frame " + frame + ": FAIL: " + value + ", the PC's Vulkan frame is "
                            + effectiveReference()};
                case FRAME_NO_REFERENCE:
                    return new String[] {"warn", "Frame " + frame + ": " + value + " (the trace has no PC hash for it)"};
                default:
                    return new String[] {"warn", "Frame " + frame + ": not reached"};
            }
        }
    }

    final String verdict;
    final String headline;
    final int pass;
    final int differs;
    final int noReference;
    final int notReached;
    final int total;
    final boolean error;
    final boolean stopped;

    private MenuReplayVerdict(String verdict, String headline, int pass, int differs, int noReference, int notReached,
            int total, boolean error, boolean stopped) {
        this.verdict = verdict;
        this.headline = headline;
        this.pass = pass;
        this.differs = differs;
        this.noReference = noReference;
        this.notReached = notReached;
        this.total = total;
        this.error = error;
        this.stopped = stopped;
    }

    boolean ok() {
        return PASS.equals(verdict);
    }

    /**
     * @param frames        every frame the trace reads back (and any other the native side reported), judged
     * @param nativeResult  galplay.json "result": PASS, DIFFERS, INCOMPLETE or ERROR ("" when there is none)
     * @param exitCode      galplay.json "exitCode" (GalPlay.h GalPlayExitCode), -1 when unknown
     * @param nativeMessage galplay.json "message"
     * @param closedReason  non-null when GameActivity went away before the native side finished
     * @param lastFrame     the last frame the progress reports named
     */
    static MenuReplayVerdict judge(List<Frame> frames, String nativeResult, int exitCode, String nativeMessage,
            String closedReason, int lastFrame) {
        int pass = 0;
        int differs = 0;
        int noReference = 0;
        int notReached = 0;
        for (Frame frame : frames) {
            switch (frame.verdict) {
                case FRAME_PASS:
                    ++pass;
                    break;
                case FRAME_DIFFERS:
                    ++differs;
                    break;
                case FRAME_NO_REFERENCE:
                    ++noReference;
                    break;
                default:
                    ++notReached;
                    break;
            }
        }
        int total = frames.size();
        String message = nativeMessage == null ? "" : nativeMessage.trim();
        // GalPlay.h: 3 bad options, 5 missing or different data, 6 no device, 7 internal; 2 stopped; 4 differs.
        boolean error = "ERROR".equalsIgnoreCase(nativeResult) || exitCode == 3 || exitCode >= 5;
        boolean stopped = closedReason != null || "INCOMPLETE".equalsIgnoreCase(nativeResult) || exitCode == 2;
        String verdict;
        String headline;
        if (error) {
            verdict = FAIL;
            headline = "FAIL · " + (message.isEmpty() ? "the replay stopped with an error"
                    + (exitCode >= 0 ? " (exit code " + exitCode + ")" : "") : message);
        } else if (differs > 0) {
            verdict = FAIL;
            headline = "FAIL · " + differs + " of " + total + " frames differ from the PC's Vulkan frames"
                    + (pass > 0 ? ", " + pass + " equal" : "");
        } else if (stopped || notReached > 0 || total == 0) {
            verdict = INCOMPLETE;
            String how = closedReason != null ? closedReason : stopped ? "stopped" : "ended";
            headline = "INCOMPLETE · " + how + " at frame " + lastFrame
                    + (total == 0 ? " · no frame was read back"
                    : " · " + pass + " of " + total + " frames equal the PC's so far");
        } else if (pass == 0) {
            verdict = NO_REFERENCE;
            headline = "NO REFERENCE · the trace has no PC Vulkan hashes to compare the " + total + " frames with";
        } else {
            verdict = PASS;
            headline = "PASS · " + pass + " of " + total + " frames equal the PC's Vulkan frames"
                    + (noReference > 0 ? " (" + noReference + " without a reference)" : "");
        }
        return new MenuReplayVerdict(verdict, headline, pass, differs, noReference, notReached, total, error, stopped);
    }

    /** Frame time statistics in milliseconds. */
    static final class Stats {
        int count;
        double mean;
        double p50;
        double p95;
        double max;

        static Stats of(List<Double> values) {
            Stats stats = new Stats();
            List<Double> sorted = new ArrayList<>();
            for (Double value : values) {
                if (value != null && !value.isNaN() && !value.isInfinite() && value >= 0) {
                    sorted.add(value);
                }
            }
            if (sorted.isEmpty()) {
                return stats;
            }
            Collections.sort(sorted);
            double sum = 0;
            for (double value : sorted) {
                sum += value;
            }
            stats.count = sorted.size();
            stats.mean = sum / sorted.size();
            stats.p50 = percentile(sorted, 0.50);
            stats.p95 = percentile(sorted, 0.95);
            stats.max = sorted.get(sorted.size() - 1);
            return stats;
        }

        /** Nearest rank: the smallest value with at least {@code fraction} of the values at or below it. */
        private static double percentile(List<Double> sorted, double fraction) {
            int rank = (int) Math.ceil(fraction * sorted.size());
            return sorted.get(Math.max(0, Math.min(sorted.size() - 1, rank - 1)));
        }

        static double round(double value) {
            return Math.round(value * 100) / 100.0;
        }
    }
}
