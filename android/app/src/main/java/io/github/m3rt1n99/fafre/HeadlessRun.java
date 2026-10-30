package io.github.m3rt1n99.fafre;

import android.content.Context;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Plays one FAF vault replay on the engine's headless simulation, as
 * tools/parity-data/shard.py does on CI: the replay, its map and M28AI version in a vault of its
 * own, FAF's game files of its version, then {@code headless --replay}, whose output names every
 * checksum the simulation computed differently from the one a player's game recorded.
 */
final class HeadlessRun {
    private static final Pattern MISMATCH =
        Pattern.compile("Checksum for beat (\\d+) mismatched: ([0-9a-f]{32}) \\(sim\\) != ([0-9a-f]{32}) \\((.*)\\)\\.");
    private static final Pattern PROGRESS = Pattern.compile("\\] beat (-?\\d+) game time \\d+:\\d+:\\d+ after");
    private static final Pattern ENDED = Pattern.compile("\\] GAME-ENDED beat (-?\\d+)");
    private static final Pattern SUMMARY =
        Pattern.compile("\\] beats=\\d+ last-beat=(-?\\d+) game-over=(\\w+).* load=([\\d.]+)s play=([\\d.]+)s");
    private static final Pattern VERDICT = Pattern.compile("\\] VERDICT ended=(\\w+) .*crash=(\\w+)");

    private static volatile Process sProcess;

    private HeadlessRun() {
    }

    /** Stops the running replay (Cancel). */
    static void stop() {
        Process process = sProcess;
        if (process != null) {
            process.destroy();
        }
    }

    static File program(Context context, String library) {
        return new File(context.getApplicationInfo().nativeLibraryDir, library);
    }

    /**
     * The environment the engine's POSIX layer expects of a Linux login: HOME (its known
     * folders, e.g. the LOCAL_APPDATA FAF's init file asks for) and TMPDIR. An app process has
     * neither; both point into the app's own folders.
     */
    static void environment(Context context, Storage storage, ProcessBuilder builder) {
        Map<String, String> environment = builder.environment();
        environment.put("HOME", Storage.mkdirs(new File(storage.parity, "home")).getAbsolutePath());
        environment.put("TMPDIR", context.getCacheDir().getAbsolutePath());
    }

    static void play(Context context, Storage storage, TestState.Replay replay, int simWorkers, GameFiles.Log log)
        throws IOException {
        // The replay.
        replay.state = "preparing";
        TestState.touch();
        File cached = new File(storage.replays(), replay.id + ".fafreplay");
        if (!cached.isFile()) {
            log.step("Downloading replay " + replay.id);
            Http.download(Http.REPLAY_URL + replay.id, cached, null);
        }
        ReplayFile file = ReplayFile.decode(Files.readAllBytes(cached.toPath()));
        ReplayFile.Info info = file.info();
        replay.map = info.map != null ? info.map : "";
        replay.players = info.players;
        replay.gameVersion = info.gameVersion;
        replay.expectedBeats = info.beats;
        TestState.touch();
        if (info.unsupported != null) {
            replay.state = "unsupported";
            replay.note = info.unsupported;
            TestState.touch();
            return;
        }

        // Its game files, map and M28AI.
        File gameDir = GameFiles.ensure(storage, info.gameVersion, log);
        File map = Vault.map(storage, info.map, log);
        File mod = info.m28 != null ? Vault.m28(storage, info.m28, log) : null;

        File run = new File(storage.runs(), String.valueOf(replay.id));
        Storage.deleteTree(run);
        File vault = new File(run, "vault");
        Storage.mkdirs(new File(vault, "maps"));
        Storage.mkdirs(new File(vault, "mods"));
        if (map != null) {
            log.step("Copying map " + info.map);
            Vault.copyTree(map, new File(vault, "maps/" + info.map));
        }
        if (mod != null) {
            Vault.copyTree(mod, new File(vault, "mods/m28ai"));
        }
        File scfaReplay = new File(run, "replay.scfareplay");
        try (OutputStream out = new FileOutputStream(scfaReplay)) {
            out.write(file.body);
        }
        String label = "r" + replay.id;
        File init = GameFiles.runInit(gameDir, label, vault);
        Map<Integer, Map<String, List<String>>> recorded = file.checksums();
        Cancellation.check();

        // The run.
        List<String> command = new ArrayList<>();
        command.add(program(context, "libfafheadless.so").getAbsolutePath());
        command.add("--replay");
        command.add(scfaReplay.getAbsolutePath());
        command.add("/init");
        command.add(init.getAbsolutePath());
        command.add("/nobugreport");
        command.add("/nosound");
        command.add("/log");
        command.add(new File(run, "engine.log").getAbsolutePath());
        command.add("--label");
        command.add(label);
        command.add("--timeout");
        command.add("120");
        command.add("/simworkers");
        command.add(String.valueOf(simWorkers));
        replay.state = "running";
        replay.beat = 0;
        TestState.touch();
        log.step("Playing " + replay.id + " (" + info.map + ")");

        long started = System.nanoTime();
        ProcessBuilder builder = new ProcessBuilder(command).directory(run).redirectErrorStream(true);
        environment(context, storage, builder);
        Process process = builder.start();
        sProcess = process;
        boolean ended = false;
        boolean over = false;
        Boolean verdictCrash = null;
        Set<String> outvoted = new HashSet<>();
        List<String> trouble = new ArrayList<>();
        try (BufferedReader in = new BufferedReader(new InputStreamReader(process.getInputStream(), StandardCharsets.ISO_8859_1));
             OutputStream out = new FileOutputStream(new File(run, "headless.out"))) {
            String text;
            while ((text = in.readLine()) != null) {
                out.write((text + "\n").getBytes(StandardCharsets.ISO_8859_1));
                Matcher m = MISMATCH.matcher(text);
                if (m.find()) {
                    int beat = Integer.parseInt(m.group(1));
                    String client = m.group(4);
                    if (outvoted.contains(client)) {
                        continue; // desynced in the live game earlier; stays wrong for the rest of it
                    }
                    if (ReplayFile.liveClientDesync(recorded, beat, m.group(2), m.group(3))) {
                        outvoted.add(client);
                        replay.outvoted = outvoted.size();
                    } else {
                        ++replay.mismatches;
                        if (replay.firstMismatch == null) {
                            replay.firstMismatch = beat;
                        }
                    }
                    TestState.touch();
                    continue;
                }
                if ((m = PROGRESS.matcher(text)).find()) {
                    replay.beat = Math.max(replay.beat, Integer.parseInt(m.group(1)));
                    replay.playSeconds = (System.nanoTime() - started) / 1e9;
                } else if ((m = ENDED.matcher(text)).find()) {
                    replay.beat = Math.max(replay.beat, Integer.parseInt(m.group(1)));
                    ended = true;
                } else if ((m = SUMMARY.matcher(text)).find()) {
                    replay.beat = Math.max(replay.beat, Integer.parseInt(m.group(1)));
                    over = "True".equals(m.group(2));
                    replay.loadSeconds = Double.parseDouble(m.group(3));
                    replay.playSeconds = Double.parseDouble(m.group(4));
                } else if ((m = VERDICT.matcher(text)).find()) {
                    ended |= "True".equals(m.group(1));
                    verdictCrash = "True".equals(m.group(2));
                } else if (text.contains("] FATAL ") || text.contains("] CRASH ") || text.contains("] ERROR ")) {
                    if (trouble.size() < 5) {
                        trouble.add(text.trim());
                    }
                }
                TestState.touch();
            }
        } finally {
            sProcess = null;
            if (!waitFor(process, 10)) {
                process.destroyForcibly();
                waitFor(process, 10);
            }
        }
        replay.wallSeconds = (System.nanoTime() - started) / 1e9;
        int exit = process.isAlive() ? -1 : process.exitValue();

        // shard.py's verdicts.
        if (Cancellation.requested()) {
            replay.state = "cancelled";
        } else if (replay.firstMismatch != null) {
            replay.state = "desync";
        } else if (verdictCrash == null || verdictCrash) {
            replay.state = "crash";
            replay.note = "exit code " + exit + (verdictCrash == null ? ", no verdict" : "")
                + (trouble.isEmpty() ? "" : ": " + trouble.get(0));
        } else if (ended || over || (info.beats != null && replay.beat >= info.beats - 2)) {
            replay.state = "pass";
        } else {
            replay.state = "incomplete";
            replay.note = "stopped at beat " + replay.beat + " of " + (info.beats != null ? info.beats : "?");
        }
        TestState.touch();
    }

    private static boolean waitFor(Process process, long seconds) {
        try {
            return process.waitFor(seconds, TimeUnit.SECONDS);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return false;
        }
    }
}
