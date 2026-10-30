package io.github.m3rt1n99.fafre;

import android.content.Context;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
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
    private static final Pattern APPLIED = Pattern.compile("\\((\\d+) known platform difference");

    /** Every case this phone computes differently, in tests/sim_math's overlay format (--generate-known). */
    static final String KNOWN_FILE = "sim_math_known.txt";

    private MathCheck() {
    }

    /** faf_sim_math_tests' platform for this APK's native code (PlatformName in tests/sim_math/SimMathTests.cpp). */
    static String platform(Context context) {
        String abi = new File(context.getApplicationInfo().nativeLibraryDir).getName(); // arm64, x86_64
        return "android-" + ("x86_64".equals(abi) ? "x64" : abi);
    }

    static void run(Context context) throws IOException {
        File golden = new File(context.getFilesDir(), "golden.txt");
        File known = new File(context.getFilesDir(), KNOWN_FILE);
        known.delete();
        copyAsset(context, "golden.txt", golden);
        // tests/sim_math/known_<platform>.txt: the cases this platform computes differently for a
        // recorded reason (AArch64: the square root of a negative number gives a positive NaN).
        File overlay = new File(context.getFilesDir(), "known_" + platform(context) + ".txt");
        if (!copyAsset(context, overlay.getName(), overlay)) {
            overlay = null;
        }
        TestState.step("Computing the sim-math golden cases");
        long started = System.nanoTime();
        List<String> command = new ArrayList<>();
        command.add(HeadlessRun.program(context, "libfafsimmath.so").getAbsolutePath());
        command.add(golden.getAbsolutePath());
        if (overlay != null) {
            command.add("--known");
            command.add(overlay.getAbsolutePath());
        }
        ProcessBuilder builder = new ProcessBuilder(command).redirectErrorStream(true);
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
        Matcher applied = APPLIED.matcher(output);
        int knownCases = applied.find() ? Integer.parseInt(applied.group(1)) : 0;
        if (m.find() && exit == 0 && "0".equals(m.group(2)) && "0".equals(m.group(3))) {
            int cases = Integer.parseInt(m.group(1));
            verdict = knownCases > 0
                ? String.format(Locale.ROOT, "PASS: %d cases: %d equal to the x87, %d known platform differences (%.1f s)",
                    cases, cases - knownCases, knownCases, seconds)
                : String.format(Locale.ROOT, "PASS: %d cases, all equal to the x87 (%.1f s)", cases, seconds);
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

    /** Copies an asset into the app's files; false if the APK has no such asset. */
    private static boolean copyAsset(Context context, String name, File file) throws IOException {
        InputStream in;
        try {
            in = context.getAssets().open(name);
        } catch (FileNotFoundException e) {
            return false;
        }
        try (InputStream source = in; OutputStream out = new FileOutputStream(file)) {
            byte[] buffer = new byte[1 << 16];
            int read;
            while ((read = source.read(buffer)) > 0) {
                out.write(buffer, 0, read);
            }
        }
        return true;
    }

    /** The full list of cases differing from the golden values (known ones too), for the results zip. */
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
