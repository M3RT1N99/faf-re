package io.github.m3rt1n99.fafre;

import android.content.Context;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * The engine's sim-math tests (tests/sim_math, faf_sim_math_tests) on this phone: every golden
 * case recorded on the x87 (windows-x64) computed again by the simulation's own code, on the
 * phone's CPU, without any game data.
 */
final class MathCheck {
    private static final Pattern SUMMARY = Pattern.compile("(\\d+) cases, (\\d+) differ, (\\d+) missing");
    private static final Pattern FUNCTION = Pattern.compile("^FAIL (\\S+): (\\d+) case", Pattern.MULTILINE);

    /** Every case this phone computes differently, in tests/sim_math's overlay format (--generate-known). */
    static final String KNOWN_FILE = "sim_math_known.txt";

    private MathCheck() {
    }

    static void run(Context context) throws IOException {
        File golden = new File(context.getFilesDir(), "golden.txt");
        File known = new File(context.getFilesDir(), KNOWN_FILE);
        known.delete();
        try (InputStream in = context.getAssets().open("golden.txt"); OutputStream out = new FileOutputStream(golden)) {
            byte[] buffer = new byte[1 << 16];
            int read;
            while ((read = in.read(buffer)) > 0) {
                out.write(buffer, 0, read);
            }
        }
        TestState.step("Computing the sim-math golden cases");
        long started = System.nanoTime();
        ProcessBuilder builder =
            new ProcessBuilder(HeadlessRun.program(context, "libfafsimmath.so").getAbsolutePath(), golden.getAbsolutePath())
                .redirectErrorStream(true);
        HeadlessRun.environment(context, new Storage(context), builder);
        Process process = builder.start();
        StringBuilder output = new StringBuilder();
        try (BufferedReader in = new BufferedReader(new InputStreamReader(process.getInputStream(), StandardCharsets.UTF_8))) {
            String line;
            while ((line = in.readLine()) != null) {
                output.append(line).append('\n');
                TestState.line(line);
            }
        }
        int exit;
        try {
            exit = process.waitFor();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IOException("interrupted");
        }
        double seconds = (System.nanoTime() - started) / 1e9;
        Matcher m = SUMMARY.matcher(output);
        String verdict;
        if (m.find() && exit == 0 && "0".equals(m.group(2)) && "0".equals(m.group(3))) {
            verdict = String.format(Locale.ROOT, "PASS: %s cases, all equal to the x87 (%.1f s)", m.group(1), seconds);
        } else if (m.find(0)) {
            StringBuilder functions = new StringBuilder();
            Matcher f = FUNCTION.matcher(output);
            while (f.find()) {
                functions.append(functions.length() == 0 ? "" : ", ").append(f.group(1)).append(' ').append(f.group(2));
            }
            verdict = String.format(Locale.ROOT, "FAIL: %s of %s cases differ, %s missing (%s)", m.group(2), m.group(1), m.group(3),
                functions.length() > 0 ? functions.toString() : "exit " + exit);
            writeKnown(context, golden, known);
        } else {
            verdict = "FAIL: no summary (exit " + exit + ")";
        }
        TestState.math(verdict, output.toString());
        TestState.line(verdict);
    }

    /** The full list of differing cases, for the results zip (the run itself names one per function). */
    private static void writeKnown(Context context, File golden, File known) throws IOException {
        TestState.step("Listing every differing case");
        ProcessBuilder builder = new ProcessBuilder(HeadlessRun.program(context, "libfafsimmath.so").getAbsolutePath(),
            "--generate-known", golden.getAbsolutePath(), known.getAbsolutePath(),
            "measured on " + Results.device() + " by faf-re-android " + BuildConfig.VERSION_NAME + "; not reviewed")
            .redirectErrorStream(true);
        HeadlessRun.environment(context, new Storage(context), builder);
        Process process = builder.start();
        try (BufferedReader in = new BufferedReader(new InputStreamReader(process.getInputStream(), StandardCharsets.UTF_8))) {
            String line;
            while ((line = in.readLine()) != null) {
                TestState.line(line);
            }
        }
        try {
            process.waitFor();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IOException("interrupted");
        }
    }
}
