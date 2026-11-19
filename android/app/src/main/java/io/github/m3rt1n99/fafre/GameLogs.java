package io.github.m3rt1n99.fafre;

import android.content.Context;
import android.content.Intent;
import android.os.Build;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;
import java.util.regex.Pattern;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/**
 * The game's recent logs for the FAF client's problem report (StatusProvider's call game_logs, android/README.md, The logs):
 * a zip in the app's cache of what Share results takes of the game, the last game's report, the engine's log and newest
 * crash report and a native crash's tombstone, and the game's before it, after report.txt (the app's version, the engine's
 * commit, Android's version and the device's model). It goes into public issues, so its texts are scrubbed together
 * (LogScrubber), and a tombstone comes as its summary (Tombstone). A zip stays until the next one, which takes its grant.
 */
final class GameLogs {
    /** The zips' folder in the app's cache, and their path in ResultsProvider's URIs. */
    static final String FOLDER = "game_logs";
    /** A zip's name, with its time to the millisecond. */
    static final Pattern NAME = Pattern.compile("faf-re-game-logs-\\d{8}-\\d{6}-\\d{3}\\.zip");
    /** What report.txt says of the texts after it. */
    static final String SCRUBBED = "The logs are scrubbed for a public issue: players' names are player1, player2, ..., their "
        + "ids id1, id2, ..., a FAF game's id <game>, a game's title <title>, chat <chat>, the account's values <account>, ICE "
        + "credentials <secret>, hosts' addresses <ip>, the app's folders <external>, <internal> and <apk>, and others <storage>.";

    private GameLogs() {
    }

    static File folder(Context context) {
        return new File(context.getCacheDir(), FOLDER);
    }

    /**
     * The zip of the game's logs as they are now, in folder(); the zips of earlier calls go, with the grants on them. One call
     * at a time: each takes the one before it.
     */
    static synchronized File write(Context context) throws IOException {
        File folder = Storage.mkdirs(folder(context));
        File[] earlier = folder.listFiles();
        if (earlier != null) {
            for (File file : earlier) {
                if (NAME.matcher(file.getName()).matches()) {
                    context.revokeUriPermission(ResultsProvider.gameLogs(file), Intent.FLAG_GRANT_READ_URI_PERMISSION);
                }
                Storage.deleteTree(file);
            }
        }
        // The report's end, once the game's process is gone, and with it a native crash's tombstone.
        GameReport.current(context);
        Storage storage = null;
        try {
            storage = new Storage(context);
        } catch (IOException e) {
            // No shared storage: the zip has what the app's own folder holds.
        }
        // Copies: the scrubber reads each text twice, and a game that runs goes on writing its own.
        File work = Storage.mkdirs(new File(folder, "work"));
        try {
            Map<String, File> texts = new LinkedHashMap<>();
            copy(texts, work, "game.txt", GameReport.file(context));
            summary(texts, work, "game-tombstone.txt", GameReport.tombstone(context));
            copy(texts, work, GameReport.PREVIOUS, GameReport.previous(context));
            summary(texts, work, "game-previous-tombstone.txt", GameReport.previousTombstone(context));
            if (storage != null) {
                copy(texts, work, "game/engine.sclog", GameReport.log(storage));
                copy(texts, work, "game/" + GameReport.crashReport(storage).getName(), GameReport.crashReport(storage));
            }
            String stamp = new SimpleDateFormat("yyyyMMdd-HHmmss-SSS", Locale.ROOT).format(new Date());
            File zip = new File(folder, "faf-re-game-logs-" + stamp + ".zip");
            File part = new File(work, zip.getName());
            write(report(BuildConfig.VERSION_NAME + " (" + BuildConfig.VERSION_CODE + ")", BuildConfig.GIT_COMMIT,
                Build.VERSION.RELEASE + " (API " + Build.VERSION.SDK_INT + ")", Build.MANUFACTURER + " " + Build.MODEL), texts,
                BuildConfig.APPLICATION_ID, part);
            if (!part.renameTo(zip)) {
                throw new IOException("cannot move " + part.getName() + " into " + folder);
            }
            return zip;
        } finally {
            Storage.deleteTree(work);
        }
    }

    /** report.txt: the app's version, the engine's commit, Android's version and the device's model, and SCRUBBED. No account. */
    static String report(String app, String commit, String android, String device) {
        return "app: " + app + "\nengine commit: " + commit + "\nAndroid: " + android + "\ndevice: " + device + "\n\n" + SCRUBBED
            + "\n";
    }

    /**
     * The zip of {@code texts}, each its name there and its file, after report.txt: the texts scrubbed together, the players'
     * names learned from all of them first, and each line written as the scrubber gives it.
     */
    static void write(String report, Map<String, File> texts, String packageName, File zip) throws IOException {
        LogScrubber scrubber = new LogScrubber(packageName);
        for (File text : texts.values()) {
            try (BufferedReader in = reader(text)) {
                for (String line = in.readLine(); line != null; line = in.readLine()) {
                    scrubber.learn(line);
                }
            }
        }
        try (ZipOutputStream out = new ZipOutputStream(new FileOutputStream(zip))) {
            // Not closed itself: it would close the zip; flushed at each entry's end.
            Writer writer = new BufferedWriter(new OutputStreamWriter(out, StandardCharsets.UTF_8));
            out.putNextEntry(new ZipEntry("report.txt"));
            writer.write(report);
            writer.flush();
            out.closeEntry();
            for (Map.Entry<String, File> text : texts.entrySet()) {
                out.putNextEntry(new ZipEntry(text.getKey()));
                try (BufferedReader in = reader(text.getValue())) {
                    for (String line = in.readLine(); line != null; line = in.readLine()) {
                        writer.write(scrubber.scrub(line));
                        writer.write('\n');
                    }
                }
                writer.flush();
                out.closeEntry();
            }
        }
    }

    private static BufferedReader reader(File text) throws IOException {
        return new BufferedReader(new InputStreamReader(new FileInputStream(text), StandardCharsets.UTF_8));
    }

    /** {@code file}, where it is, as the text {@code name} of the zip: a copy in {@code work}. */
    private static void copy(Map<String, File> texts, File work, String name, File file) throws IOException {
        if (file.isFile()) {
            File copy = new File(work, String.valueOf(texts.size()));
            Files.copy(file.toPath(), copy.toPath());
            texts.put(name, copy);
        }
    }

    /** A tombstone {@code file}, where it is, as its summary, the text {@code name} of the zip; one that cannot be read says so. */
    private static void summary(Map<String, File> texts, File work, String name, File file) throws IOException {
        if (!file.isFile()) {
            return;
        }
        String summary;
        try {
            summary = Tombstone.summary(Files.readAllBytes(file.toPath()));
        } catch (IOException e) {
            summary = "The tombstone cannot be read: " + e.getMessage() + "\n";
        }
        File copy = new File(work, String.valueOf(texts.size()));
        Files.write(copy.toPath(), summary.getBytes(StandardCharsets.UTF_8));
        texts.put(name, copy);
    }
}
