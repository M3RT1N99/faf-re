package io.github.m3rt1n99.fafre;

import android.app.GameManager;
import android.content.pm.ApplicationInfo;
import android.os.Build;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/** The results zip a tester sends back: summary.json and each replay's output and engine log. */
final class Results {
    private Results() {
    }

    static String device() {
        StringBuilder out = new StringBuilder();
        out.append(Build.MANUFACTURER).append(' ').append(Build.MODEL);
        if (Build.VERSION.SDK_INT >= 31) {
            out.append(" (").append(Build.SOC_MANUFACTURER).append(' ').append(Build.SOC_MODEL).append(')');
        }
        out.append(", Android ").append(Build.VERSION.RELEASE).append(" (API ").append(Build.VERSION.SDK_INT).append(')');
        out.append(", ").append(String.join("/", Build.SUPPORTED_ABIS));
        out.append(", ").append(Runtime.getRuntime().availableProcessors()).append(" cores");
        return out.toString();
    }

    /**
     * How Android files the app: its category (the manifest says game, which Samsung's Game
     * Booster may treat differently) and the game mode the system gives it.
     */
    static String game(android.content.Context context) {
        ApplicationInfo app = context.getApplicationInfo();
        @SuppressWarnings("deprecation")
        boolean isGame = (app.flags & ApplicationInfo.FLAG_IS_GAME) != 0;
        String category = (app.category == ApplicationInfo.CATEGORY_GAME ? "category game" : "not a game")
            + (isGame ? ", isGame" : "");
        if (Build.VERSION.SDK_INT < 31) {
            return category;
        }
        GameManager games = context.getSystemService(GameManager.class);
        int mode = games != null ? games.getGameMode() : GameManager.GAME_MODE_UNSUPPORTED;
        String[] names = {"unsupported", "standard", "performance", "battery", "custom"};
        return category + ", game mode " + (mode >= 0 && mode < names.length ? names[mode] : String.valueOf(mode));
    }

    static File write(android.content.Context context, Storage storage) throws IOException {
        RendererReport.refresh(context);
        String stamp = new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(new Date());
        File zip = new File(storage.results(), "faf-re-results-" + stamp + ".zip");
        JSONObject summary = new JSONObject();
        try {
            summary.put("app", BuildConfig.VERSION_NAME + " (" + BuildConfig.GIT_COMMIT + ")");
            summary.put("device", device());
            summary.put("cores", SpeedSampler.cores().describe());
            summary.put("game", game(context));
            summary.put("math", TestState.mathVerdict());
            summary.put("renderer", TestState.rendererVerdict());
            JSONArray replays = new JSONArray();
            for (TestState.Replay replay : TestState.replaySnapshot()) {
                JSONObject entry = new JSONObject();
                entry.put("id", replay.id);
                entry.put("verdict", replay.state);
                entry.put("map", replay.map);
                entry.put("players", replay.players);
                entry.put("game_version", replay.gameVersion);
                entry.put("beats", replay.beat);
                if (replay.firstMismatch != null) {
                    entry.put("first_mismatch", replay.firstMismatch);
                    entry.put("mismatches", replay.mismatches);
                }
                entry.put("live_desyncs_ignored", replay.outvoted);
                entry.put("load_s", replay.loadSeconds);
                entry.put("play_s", replay.playSeconds);
                entry.put("wall_s", replay.wallSeconds);
                entry.put("note", replay.note);
                if (replay.speed != null && replay.speed.seconds > 0) {
                    SpeedSampler.Summary speed = replay.speed;
                    JSONObject cpu = new JSONObject();
                    cpu.put("line", speed.line());
                    cpu.put("thread", speed.thread);
                    cpu.put("sampled_s", Math.round(speed.seconds));
                    cpu.put("cpu_s", Math.round(speed.cpuSeconds * 10) / 10.0);
                    JSONObject classes = new JSONObject();
                    for (String name : speed.classOrder) {
                        Double share = speed.classShare.get(name);
                        classes.put(name, share != null ? Math.round(share * 1000) / 1000.0 : 0);
                    }
                    cpu.put("classes", classes);
                    cpu.put("average_mhz", Math.round(speed.averageMhz));
                    cpu.put("lowest_cap_mhz", speed.lowestCapMhz);
                    cpu.put("rated_mhz", speed.ratedMhz);
                    cpu.put("thermal_max", speed.thermal);
                    cpu.put("performance_hint", speed.hint);
                    if (!Double.isNaN(speed.batteryStartC)) {
                        cpu.put("battery_start_c", speed.batteryStartC);
                        cpu.put("battery_max_c", speed.batteryMaxC);
                    }
                    cpu.put("charging_s", speed.chargingSeconds);
                    cpu.put("screen_off_s", speed.screenOffSeconds);
                    cpu.put("app_not_in_front_s", speed.hiddenSeconds);
                    entry.put("speed", cpu);
                }
                replays.put(entry);
            }
            summary.put("replays", replays);
        } catch (JSONException e) {
            throw new IOException(e);
        }
        try (ZipOutputStream out = new ZipOutputStream(new FileOutputStream(zip))) {
            add(out, "summary.json", summary.toString(2).getBytes(StandardCharsets.UTF_8));
            add(out, "math.txt", TestState.mathOutput().getBytes(StandardCharsets.UTF_8));
            File known = new File(context.getFilesDir(), MathCheck.KNOWN_FILE);
            if (known.isFile()) {
                addFile(out, MathCheck.KNOWN_FILE, known);
            }
            add(out, "log.txt", String.join("\n", TestState.log()).getBytes(StandardCharsets.UTF_8));
            if (!TestState.rendererReport().isEmpty()) {
                add(out, "renderer.txt", TestState.rendererReport().getBytes(StandardCharsets.UTF_8));
            }
            File tombstone = RendererReport.tombstone(context);
            if (tombstone.isFile()) {
                addFile(out, RendererReport.TOMBSTONE, tombstone);
            }
            for (TestState.Replay replay : TestState.replaySnapshot()) {
                File run = new File(storage.runs(), String.valueOf(replay.id));
                for (String name : new String[] {"headless.out", "engine.log", "speed.csv"}) {
                    File file = new File(run, name);
                    if (file.isFile()) {
                        addFile(out, replay.id + "/" + name, file);
                    }
                }
            }
        } catch (JSONException e) {
            throw new IOException(e);
        }
        return zip;
    }

    private static void add(ZipOutputStream out, String name, byte[] data) throws IOException {
        out.putNextEntry(new ZipEntry(name));
        out.write(data);
        out.closeEntry();
    }

    private static void addFile(ZipOutputStream out, String name, File file) throws IOException {
        out.putNextEntry(new ZipEntry(name));
        try (InputStream in = new FileInputStream(file)) {
            byte[] buffer = new byte[1 << 16];
            int read;
            while ((read = in.read(buffer)) > 0) {
                out.write(buffer, 0, read);
            }
        }
        out.closeEntry();
    }
}
