package io.github.m3rt1n99.fafre;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * Replays for the replay test, in {@code <root>/replays}: copying a picked or shared file in,
 * recognising the format, and the sidecar {@code <stem>.json} with what is known about it.
 *
 * <p>Formats (scripts/perf/convert_replay.py, docs/port/headless-replay.md):
 * <ul>
 * <li>{@code .fafreplay}: one JSON line (FAF's game info), "\n", then the body: one zstd frame
 * ({@code "compression":"zstd"}, every vault and browser download) or base64 of a 4-byte big-endian
 * length plus a zlib stream (the Java client's local recordings).</li>
 * <li>{@code .scfareplay}: the engine's own format, starting with "Supreme Commander v1.50.NNNN\0".
 * The engine only takes 3764.</li>
 * </ul>
 * Java only reads the JSON line. Decoding and the version rewrite are the runner's
 * ({@code /replayinfo <file>} prints one JSON object, {@code /convertreplay <in> <out>} writes the
 * .scfareplay the engine loads), so the phone uses the code the reference runs used.
 *
 * <p>The copy is checked before it gets a name: a size cap, then the content (never the file name or
 * the MIME type a provider claims). File names are made from the replay id or a sanitized display
 * name, so nothing a sender controls can leave {@code replays/}.
 */
final class ReplayFiles {
    static final String DIR = "replays";
    static final String FAF_EXTENSION = ".fafreplay";
    static final String SCFA_EXTENSION = ".scfareplay";
    /** What the engine loads: the source converted to "Supreme Commander v1.50.3764". */
    static final String CONVERTED_SUFFIX = ".3764" + SCFA_EXTENSION;
    static final String ENGINE_VERSION = "3764";
    static final long MAX_BYTES = 64L * 1024 * 1024;
    private static final int HEAD_BYTES = 1024 * 1024;
    private static final String SCFA_MAGIC = "Supreme Commander v";
    private static final long ANALYZE_WATCHDOG_MS = 60_000;

    static final String FORMAT_ZSTD = "fafreplay-zstd";
    static final String FORMAT_LEGACY = "fafreplay-legacy";
    static final String FORMAT_SCFA = "scfareplay";

    /** A replay longer than this (20 min of game time) gets the long-run note. */
    private static final int LONG_REPLAY_BEATS = 12000;
    private static final long LOW_SPACE_BYTES = 1024L * 1024 * 1024;

    /**
     * Header fields worth keeping; names of players (teams, recorder, host) are left out on purpose, and so
     * is the title, which FAF fills with player names ("A Vs B") for most games.
     */
    private static final List<String> HEADER_KEYS = Arrays.asList("uid", "mapname", "map_file_path", "featured_mod",
            "num_players", "max_players", "complete", "launched_at", "game_end", "compression", "version",
            "game_type", "state");

    /** Everything known about one imported replay; backed by the sidecar JSON. */
    static final class Info {
        final JSONObject json;

        Info(JSONObject json) {
            this.json = json;
        }

        String stem() {
            return json.optString("stem");
        }

        /** The copy as received: {@code <stem>.fafreplay} or {@code <stem>.scfareplay}. */
        String fileName() {
            return json.optString("file");
        }

        String sourceName() {
            return json.optString("source_name");
        }

        String format() {
            return json.optString("format");
        }

        String sha256() {
            return json.optString("sha256");
        }

        long size() {
            return json.optLong("size", -1);
        }

        JSONObject header() {
            JSONObject header = json.optJSONObject("header");
            return header != null ? header : new JSONObject();
        }

        JSONObject runnerInfo() {
            return json.optJSONObject("runner_info");
        }

        String runnerInfoError() {
            return json.optString("runner_info_error", "");
        }

        /** The file /headlessreplay gets: the converted copy, or the original when it is already 3764. */
        String engineFileName() {
            return json.optString("engine_file", "");
        }

        String engineFileSha256() {
            return json.optString("engine_file_sha256", "");
        }

        String conversionError() {
            return json.optString("conversion_error", "");
        }

        /** The replay id (FAF game uid), or "" for a file without a header. */
        String uid() {
            Object uid = header().opt("uid");
            if (uid == null) {
                uid = runnerString("id", "uid");
            }
            String text = uid == null ? "" : uid.toString();
            return text.matches("\\d{1,12}") ? text : "";
        }

        String featuredMod() {
            String mod = header().optString("featured_mod", "");
            if (mod.isEmpty()) {
                Object fromRunner = runnerString("featured_mod", "mod");
                mod = fromRunner != null ? fromRunner.toString() : "";
            }
            return mod;
        }

        /** The map's folder name (scmp_026, name.v0003), from the runner's info or the header. */
        String mapFolder() {
            // The runner's map_dir comes from the replay body's map path, the most reliable source.
            Object runnerMap = runnerString("map_dir", "map_path", "map", "mapname");
            String folder = runnerMap != null ? mapFolderOf(runnerMap.toString()) : "";
            if (folder.isEmpty()) {
                folder = mapFolderOf(header().optString("mapname", ""));
            }
            if (folder.isEmpty()) {
                folder = mapFolderOf(header().optString("map_file_path", ""));
            }
            return folder;
        }

        /** Beats in the replay (game time = beats / 10 s), or -1. */
        int beats() {
            JSONObject info = runnerInfo();
            if (info == null) {
                return -1;
            }
            for (String key : new String[] {"beats", "total_beats", "beats_in_replay"}) {
                if (info.has(key)) {
                    return info.optInt(key, -1);
                }
            }
            return -1;
        }

        /** "1.50.3831"-style recorded engine version, or "". */
        String recordedVersion() {
            Object version = runnerString("header_version", "recorded_version", "original_version", "version");
            if (version != null) {
                return version.toString();
            }
            return json.optString("scfa_version", "");
        }

        /** The 4-digit FAF game version the replay was recorded with, or -1. */
        int recordedGameVersion() {
            JSONObject info = runnerInfo();
            if (info != null && info.optInt("featured_mod_version", -1) > 0) {
                return info.optInt("featured_mod_version");
            }
            java.util.regex.Matcher m = java.util.regex.Pattern.compile("(\\d{4})\\D*$").matcher(recordedVersion());
            return m.find() ? Integer.parseInt(m.group(1)) : -1;
        }

        Boolean hasEndGame() {
            JSONObject info = runnerInfo();
            if (info == null) {
                return null;
            }
            for (String key : new String[] {"has_end_game", "has_endgame", "end_game", "endgame"}) {
                if (info.has(key)) {
                    return info.optBoolean(key);
                }
            }
            return null;
        }

        /** Number of sim mods the header names (Python client headers list them), or 0. */
        int simMods() {
            return json.optInt("sim_mods", 0);
        }

        private Object runnerString(String... keys) {
            JSONObject info = runnerInfo();
            if (info == null) {
                return null;
            }
            for (String key : keys) {
                Object value = info.opt(key);
                if (value != null && value != JSONObject.NULL && !value.toString().isEmpty()) {
                    return value;
                }
            }
            return null;
        }

        /** One line for the launcher: "26675870 · scmp_026 · faf · 465 beats (0:46)". */
        String describe() {
            StringBuilder out = new StringBuilder(fileName());
            if (!mapFolder().isEmpty()) {
                out.append(" · ").append(mapFolder());
            }
            if (!featuredMod().isEmpty()) {
                out.append(" · ").append(featuredMod());
            }
            int beats = beats();
            if (beats > 0) {
                int seconds = beats / 10;
                out.append(" · ").append(beats).append(" beats (")
                        .append(String.format(Locale.ROOT, "%d:%02d", seconds / 60, seconds % 60)).append(')');
            }
            if (recordedGameVersion() > 0) {
                out.append(" · recorded on FAF ").append(recordedGameVersion());
            } else if (!recordedVersion().isEmpty()) {
                out.append(" · recorded on ").append(recordedVersion());
            }
            return out.toString();
        }
    }

    /** What stops the replay step (problems) and what only explains a result (warnings). */
    static final class Check {
        final List<String> problems = new ArrayList<>();
        final List<String> warnings = new ArrayList<>();

        boolean ok() {
            return problems.isEmpty();
        }
    }

    private ReplayFiles() {
    }

    // ---------------------------------------------------------------- import

    /** Copies a picked or shared document into replays/. Call off the UI thread. */
    static Info importUri(Context context, Uri uri) throws IOException {
        if (uri == null || !ContentResolver.SCHEME_CONTENT.equals(uri.getScheme())) {
            throw new IOException("Only files from a document provider (content://) can be opened, not " + uri);
        }
        ContentResolver resolver = context.getContentResolver();
        String name = "";
        long size = -1;
        try (Cursor cursor = resolver.query(uri, new String[] {OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE},
                null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int nameColumn = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                int sizeColumn = cursor.getColumnIndex(OpenableColumns.SIZE);
                if (nameColumn >= 0 && !cursor.isNull(nameColumn)) {
                    name = cursor.getString(nameColumn);
                }
                if (sizeColumn >= 0 && !cursor.isNull(sizeColumn)) {
                    size = cursor.getLong(sizeColumn);
                }
            }
        } catch (RuntimeException e) {
            // Some providers refuse the query; the stream decides.
        }
        if (size > MAX_BYTES) {
            throw new IOException(describeName(name) + " is " + FileOps.formatBytes(size) + "; replays are at most "
                    + FileOps.formatBytes(MAX_BYTES) + ".");
        }
        InputStream in;
        try {
            in = resolver.openInputStream(uri);
        } catch (SecurityException e) {
            throw new IOException("No permission to read " + describeName(name) + ": " + e.getMessage(), e);
        }
        if (in == null) {
            throw new IOException("Cannot open " + describeName(name) + ".");
        }
        try (InputStream stream = in) {
            return importStream(stream, name, AppInfo.dataRoot(context));
        }
    }

    private static String describeName(String name) {
        return name == null || name.isEmpty() ? "the file" : "'" + name + "'";
    }

    /** Copies {@code in} into replays/ after checking its content. */
    static Info importStream(InputStream in, String displayName, DataRoot root) throws IOException {
        File dir = root.prepare(DIR + "/x").getParentFile();
        if (dir == null || (!dir.isDirectory() && !dir.mkdirs() && !dir.isDirectory())) {
            throw new IOException("cannot create the replays folder");
        }
        File part = new File(dir, "incoming" + FileOps.PART_SUFFIX);
        MessageDigest digest = FileOps.sha256();
        ByteArrayOutputStream head = new ByteArrayOutputStream();
        long total = 0;
        try (FileOutputStream out = new FileOutputStream(part)) {
            byte[] buffer = new byte[64 * 1024];
            int n;
            while ((n = in.read(buffer)) > 0) {
                total += n;
                if (total > MAX_BYTES) {
                    throw new IOException(describeName(displayName) + " is larger than "
                            + FileOps.formatBytes(MAX_BYTES) + "; that is not a replay.");
                }
                digest.update(buffer, 0, n);
                if (head.size() < HEAD_BYTES) {
                    head.write(buffer, 0, Math.min(n, HEAD_BYTES - head.size()));
                }
                out.write(buffer, 0, n);
            }
            out.getFD().sync();
        } catch (IOException e) {
            FileOps.deleteQuietly(part);
            throw e;
        }
        try {
            String sha256 = FileOps.hex(digest.digest());
            JSONObject json = sniff(head.toByteArray(), total, displayName);
            String extension = FORMAT_SCFA.equals(json.optString("format")) ? SCFA_EXTENSION : FAF_EXTENSION;
            String stem = chooseStem(dir, json, displayName, sha256);
            File target = new File(dir, stem + extension);
            root.checkInside(target);
            // The sidecar's path is checked before the copy gets its name, so a refused name leaves no orphan.
            root.prepare(DIR + "/" + stem + ".json");
            FileOps.moveReplacing(part, target);
            json.put("schema", 1);
            json.put("stem", stem);
            json.put("file", target.getName());
            json.put("source_name", displayName == null ? "" : displayName);
            json.put("sha256", sha256);
            json.put("size", total);
            json.put("imported_at", System.currentTimeMillis());
            if (FORMAT_SCFA.equals(json.optString("format"))
                    && ENGINE_VERSION.equals(versionDigits(json.optString("scfa_version")))) {
                json.put("engine_file", target.getName());
                json.put("engine_file_sha256", sha256);
            }
            Info info = new Info(json);
            write(root, info);
            return info;
        } catch (JSONException e) {
            throw new IOException(e.getMessage(), e);
        } finally {
            FileOps.deleteQuietly(part);
        }
    }

    /** Recognises the format from the first bytes; throws a user-readable message for anything else. */
    static JSONObject sniff(byte[] head, long size, String displayName) throws IOException, JSONException {
        JSONObject json = new JSONObject();
        String what = describeName(displayName);
        if (size < 32) {
            throw new IOException(what + " is empty or too small to be a replay.");
        }
        if (startsWith(head, SCFA_MAGIC)) {
            int end = indexOf(head, (byte) 0, 0, 64);
            String version = new String(head, 0, end > 0 ? end : Math.min(head.length, 40), StandardCharsets.ISO_8859_1);
            json.put("format", FORMAT_SCFA);
            json.put("scfa_version", version.substring(SCFA_MAGIC.length()).trim());
            return json;
        }
        if (head.length == 0 || head[0] != '{') {
            throw new IOException(what + " is not a FAF replay (.fafreplay or .scfareplay). Download the replay from "
                    + "https://replay.faforever.com/<id> and open that file.");
        }
        int newline = indexOf(head, (byte) '\n', 0, head.length);
        if (newline < 0) {
            throw new IOException(what + " starts like a .fafreplay but its header line does not end within "
                    + FileOps.formatBytes(HEAD_BYTES) + ".");
        }
        JSONObject header;
        try {
            header = new JSONObject(new String(head, 0, newline, StandardCharsets.UTF_8));
        } catch (JSONException e) {
            throw new IOException(what + " is not a FAF replay: its first line is not JSON.");
        }
        int body = newline + 1;
        if (body + 4 <= head.length && (head[body] & 0xff) == 0x28 && (head[body + 1] & 0xff) == 0xb5
                && (head[body + 2] & 0xff) == 0x2f && (head[body + 3] & 0xff) == 0xfd) {
            json.put("format", FORMAT_ZSTD);
        } else if (body < head.length && isBase64(head[body])) {
            json.put("format", FORMAT_LEGACY);
        } else {
            throw new IOException(what + " has a FAF replay header but an unknown body (neither zstd nor base64).");
        }
        JSONObject kept = new JSONObject();
        for (String key : HEADER_KEYS) {
            Object value = header.opt(key);
            if (value != null && value != JSONObject.NULL) {
                kept.put(key, value);
            }
        }
        json.put("header", kept);
        Object simMods = header.opt("sim_mods");
        if (simMods instanceof JSONObject) {
            json.put("sim_mods", ((JSONObject) simMods).length());
        } else if (simMods instanceof JSONArray) {
            json.put("sim_mods", ((JSONArray) simMods).length());
        }
        return json;
    }

    /**
     * A name from the replay id when the header has one (vault downloads: "26675870"), else from the
     * display name; lower case, [a-z0-9._-] only. A different file under the same name gets the start
     * of its sha256 appended.
     */
    static String chooseStem(File dir, JSONObject json, String displayName, String sha256) {
        JSONObject header = json.optJSONObject("header");
        String uid = header != null ? header.optString("uid", "") : "";
        String stem;
        if (uid.matches("\\d{1,12}")) {
            stem = uid;
        } else {
            String name = displayName == null ? "" : displayName;
            String lower = name.toLowerCase(Locale.ROOT);
            for (String extension : new String[] {CONVERTED_SUFFIX, FAF_EXTENSION, SCFA_EXTENSION}) {
                if (lower.endsWith(extension)) {
                    name = name.substring(0, name.length() - extension.length());
                    break;
                }
            }
            stem = sanitize(name);
        }
        if (stem.isEmpty()) {
            stem = "replay-" + sha256.substring(0, 8);
        }
        Info existing = read(dir, stem);
        boolean taken = existing != null ? !sha256.equals(existing.sha256())
                : new File(dir, stem + FAF_EXTENSION).exists() || new File(dir, stem + SCFA_EXTENSION).exists();
        return taken ? stem + "-" + sha256.substring(0, 8) : stem;
    }

    /**
     * Lower-case ASCII letters, digits, '.', '_' and '-'; everything else becomes '-'; runs of '-' and of '.'
     * collapse to one (a name with ".." is refused as a path component); at most 60 chars.
     */
    static String sanitize(String name) {
        StringBuilder out = new StringBuilder();
        for (int i = 0; i < name.length() && out.length() < 60; ++i) {
            char c = Character.toLowerCase(name.charAt(i));
            boolean ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
            char next = ok ? c : '-';
            if ((next == '-' || next == '.') && out.length() > 0 && out.charAt(out.length() - 1) == next) {
                continue;
            }
            out.append(next);
        }
        String text = out.toString();
        while (text.startsWith(".") || text.startsWith("-")) {
            text = text.substring(1);
        }
        while (text.endsWith(".") || text.endsWith("-")) {
            text = text.substring(0, text.length() - 1);
        }
        return text;
    }

    private static boolean startsWith(byte[] data, String prefix) {
        if (data.length < prefix.length()) {
            return false;
        }
        for (int i = 0; i < prefix.length(); ++i) {
            if (data[i] != (byte) prefix.charAt(i)) {
                return false;
            }
        }
        return true;
    }

    private static int indexOf(byte[] data, byte value, int from, int to) {
        for (int i = from; i < Math.min(to, data.length); ++i) {
            if (data[i] == value) {
                return i;
            }
        }
        return -1;
    }

    private static boolean isBase64(byte b) {
        return (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') || (b >= '0' && b <= '9') || b == '+' || b == '/';
    }

    static String versionDigits(String version) {
        java.util.regex.Matcher m = java.util.regex.Pattern.compile("(\\d{4})\\D*$").matcher(version);
        return m.find() ? m.group(1) : "";
    }

    /** "maps/SCMP_026/SCMP_026_scenario.lua", "/maps/scmp_026/x.scmap" or "SCMP_026" -> "scmp_026". */
    static String mapFolderOf(String value) {
        String text = value.replace('\\', '/').trim();
        int maps = text.toLowerCase(Locale.ROOT).indexOf("maps/");
        if (maps >= 0) {
            text = text.substring(maps + "maps/".length());
        }
        while (text.startsWith("/")) {
            text = text.substring(1);
        }
        int slash = text.indexOf('/');
        if (slash >= 0) {
            text = text.substring(0, slash);
        }
        return Names.isSafeName(text) ? text.toLowerCase(Locale.ROOT) : "";
    }

    // --------------------------------------------------------------- sidecar

    static Info read(DataRoot root, String stem) throws IOException {
        if (stem == null || stem.isEmpty() || !stem.equals(sanitize(stem))) {
            return null;
        }
        File file = root.find(DIR + "/" + stem + ".json");
        return file == null ? null : read(file.getParentFile(), stem);
    }

    private static Info read(File dir, String stem) {
        File file = new File(dir, stem + ".json");
        try {
            String text = FileOps.readText(file, 4 * 1024 * 1024);
            if (text == null) {
                return null;
            }
            JSONObject json = new JSONObject(text);
            if (!stem.equals(json.optString("stem")) || !json.optString("file").startsWith(stem + ".")) {
                return null;
            }
            return new Info(json);
        } catch (IOException | JSONException e) {
            return null;
        }
    }

    static void write(DataRoot root, Info info) throws IOException {
        File target = root.prepare(DIR + "/" + info.stem() + ".json");
        try {
            FileOps.writeJson(target, info.json.toString(1));
        } catch (JSONException e) {
            throw new IOException(e.getMessage(), e);
        }
    }

    /** The replay file itself (as received). */
    static File file(DataRoot root, Info info) throws IOException {
        return root.file(DIR + "/" + info.fileName());
    }

    // --------------------------------------------------------------- runner

    /**
     * Asks the runner about the replay ({@code /replayinfo}) and converts it for the engine
     * ({@code /convertreplay} into {@code <stem>.3764.scfareplay}), then updates the sidecar. A runner
     * without these modes (built before them) leaves the fields empty; the replay step then passes the
     * file as it is. Call off the UI thread.
     */
    static Info analyze(Context context, Info info, Cancellation cancel) throws IOException {
        DataRoot root = AppInfo.dataRoot(context);
        String problem = Runner.problem(context);
        JSONObject json = info.json;
        try {
            if (problem != null) {
                json.put("runner_info_error", problem);
                write(root, info);
                return info;
            }
            String alias = Runner.ensureAlias(context);
            // FAF_KNOWN_FOLDERS must exist, or the shim falls back to directories an app cannot write.
            ReplayTest.mkdirs(root, "runner");
            ReplayTest.mkdirs(root, ReplayTest.RUNNER_HOME);
            File runDir = root.prepare(DIR + "/x").getParentFile();
            String aliasDir = alias + "/" + DIR;
            File outFile = new File(context.getCacheDir(), "replayinfo.out");
            Map<String, String> env = baseEnvironment(context, alias);

            FileOps.deleteQuietly(outFile);
            final StringBuilder output = new StringBuilder();
            RunnerProcess.Outcome infoRun = RunnerProcess.run(
                    Arrays.asList(Runner.file(context, Runner.EXECUTABLE).getAbsolutePath(), "/replayinfo",
                            aliasDir + "/" + info.fileName()),
                    env, runDir, outFile, ANALYZE_WATCHDOG_MS, cancel, line -> {
                        if (output.length() < 256 * 1024) {
                            output.append(line).append('\n');
                        }
                    });
            JSONObject runnerInfo = lastJsonObject(output.toString());
            if (runnerInfo != null) {
                // Exit 1 with "ok": false still describes what could be read.
                json.put("runner_info", runnerInfo);
            } else {
                json.remove("runner_info");
            }
            if (infoRun.exitCode == 0 && runnerInfo != null && runnerInfo.optBoolean("ok", true)) {
                json.remove("runner_info_error");
            } else {
                String why = runnerInfo != null && !runnerInfo.optString("error").isEmpty()
                        ? runnerInfo.optString("error") : infoRun.describeExit() + lastLines(infoRun, 3);
                json.put("runner_info_error", "the runner cannot read the replay: " + why);
            }

            if (!FORMAT_SCFA.equals(info.format())
                    || !ENGINE_VERSION.equals(versionDigits(json.optString("scfa_version")))) {
                String converted = info.stem() + CONVERTED_SUFFIX;
                File convertedFile = new File(runDir, converted);
                // Written beside it and renamed over it, so a run still reading the old copy is unaffected.
                String partName = info.stem() + "." + ENGINE_VERSION + ".part" + SCFA_EXTENSION;
                File partFile = new File(runDir, partName);
                FileOps.deleteQuietly(partFile);
                FileOps.deleteQuietly(outFile);
                RunnerProcess.Outcome convertRun = RunnerProcess.run(
                        Arrays.asList(Runner.file(context, Runner.EXECUTABLE).getAbsolutePath(), "/convertreplay",
                                aliasDir + "/" + info.fileName(), aliasDir + "/" + partName),
                        env, runDir, outFile, ANALYZE_WATCHDOG_MS, cancel, null);
                if (convertRun.exitCode == 0 && partFile.isFile() && partFile.length() > 32) {
                    FileOps.moveReplacing(partFile, convertedFile);
                    json.put("engine_file", converted);
                    json.put("engine_file_sha256", FileOps.sha256(convertedFile, cancel, null));
                    json.remove("conversion_error");
                } else {
                    FileOps.deleteQuietly(partFile);
                    FileOps.deleteQuietly(convertedFile);
                    json.remove("engine_file");
                    json.remove("engine_file_sha256");
                    json.put("conversion_error", "/convertreplay: " + convertRun.describeExit() + lastLines(convertRun, 2));
                }
            }
            json.put("analyzed_at", System.currentTimeMillis());
            json.put("analyzed_by", Runner.buildId(Runner.file(context, Runner.EXECUTABLE)));
        } catch (JSONException e) {
            throw new IOException(e.getMessage(), e);
        }
        write(root, info);
        return info;
    }

    /** The environment every runner start gets (on top of the app's, minus LD_PRELOAD). */
    static Map<String, String> baseEnvironment(Context context, String alias) {
        Map<String, String> env = new HashMap<>();
        env.put("TMPDIR", context.getCacheDir().getAbsolutePath());
        env.put("FAF_ENGINE_LIB", Runner.file(context, Runner.ENGINE).getAbsolutePath());
        env.put("FAF_KNOWN_FOLDERS", alias + "/" + ReplayTest.RUNNER_HOME);
        env.put("FAF_LOWARENA", "1");
        return env;
    }

    private static String lastLines(RunnerProcess.Outcome outcome, int count) {
        List<String> tail = outcome.tail;
        if (tail.isEmpty()) {
            return "";
        }
        StringBuilder out = new StringBuilder(":");
        for (int i = Math.max(0, tail.size() - count); i < tail.size(); ++i) {
            out.append(' ').append(tail.get(i).trim());
        }
        return out.toString();
    }

    /** The last line of {@code text} that parses as a JSON object, or null. */
    static JSONObject lastJsonObject(String text) {
        String[] lines = text.split("\n");
        for (int i = lines.length - 1; i >= 0; --i) {
            String line = lines[i].trim();
            if (line.startsWith("{") && line.endsWith("}")) {
                try {
                    return new JSONObject(line);
                } catch (JSONException ignored) {
                    // Not the info object; keep looking.
                }
            }
        }
        int start = text.indexOf('{');
        int end = text.lastIndexOf('}');
        if (start >= 0 && end > start) {
            try {
                return new JSONObject(text.substring(start, end + 1));
            } catch (JSONException ignored) {
                // Pretty-printed or mixed output that is not one object.
            }
        }
        return null;
    }

    // ----------------------------------------------------------- data check

    /**
     * Whether the data root has what the replay needs: the required tier, the FAF and SCFA files the
     * sim reads (the recommended tier without sounds and voices, which the runner never plays), the
     * replay's map, and a featured mod this data can run.
     */
    static Check check(DataManifest manifest, DataRoot root, DataStatus status, Info info) throws IOException {
        Check check = new Check();
        if (!status.requiredComplete()) {
            check.problems.add("Required game data is missing: " + String.join(", ", status.missingRequired()) + ".");
        }
        List<String> missingFaf = new ArrayList<>();
        for (DataStatus.FafFileState file : status.faf) {
            if (file.file.tier == DataManifest.Tier.RECOMMENDED && !file.present) {
                missingFaf.add(file.file.name);
            }
        }
        if (!missingFaf.isEmpty()) {
            check.problems.add("FAF files the sim needs are missing: " + String.join(", ", missingFaf)
                    + " (Download FAF files with 'Recommended' on).");
        }
        for (DataStatus.EntryState entry : status.scfa) {
            boolean audio = entry.entry.id.startsWith("scfa-sounds") || entry.entry.id.startsWith("scfa-voice");
            if (entry.entry.tier == DataManifest.Tier.RECOMMENDED && !entry.complete && !audio
                    && !entry.entry.id.equals("scfa-maps-skirmish")) {
                check.problems.add("SCFA " + entry.entry.label + " is incomplete (" + entry.counts()
                        + "); import the SCFA folder with 'Recommended' on.");
            }
        }
        // The reference runs had mods.scd and skins.scd mounted (init_faf.lua mounts them when present).
        // Checked by name: the manifest entry also lists sc_music.scd, which many installs do not have.
        List<String> missingExtra = new ArrayList<>();
        for (String name : new String[] {"mods.scd", "skins.scd"}) {
            if (root.find(manifest.layout.scfa + "/gamedata/" + name) == null) {
                missingExtra.add(name);
            }
        }
        if (!missingExtra.isEmpty()) {
            check.warnings.add("SCFA " + String.join(" and ", missingExtra) + " " + (missingExtra.size() > 1 ? "are" : "is")
                    + " not imported (optional entry 'Sample mods, music archive'). The reference runs had them mounted; "
                    + "26675870's chain is the same without them, other replays may differ.");
        }
        if (info == null) {
            check.problems.add("No replay selected.");
            return check;
        }
        String mod = info.featuredMod();
        if (!mod.isEmpty() && !mod.equalsIgnoreCase(manifest.featuredMod)) {
            check.problems.add("The replay's featured mod is '" + mod + "'; only '" + manifest.featuredMod
                    + "' replays can run with this data.");
        }
        String map = info.mapFolder();
        if (map.isEmpty()) {
            check.warnings.add("The replay's map is not known before the run (no map name in the header).");
        } else if (root.find(manifest.layout.scfa + "/maps/" + map) == null
                && root.find(manifest.layout.vault + "/maps/" + map) == null) {
            check.problems.add("The replay's map '" + map + "' is neither in scfa/maps (Recommended: skirmish maps) "
                    + "nor in vault/maps (Import vault folder).");
        }
        File vaultMods = root.find(manifest.layout.vault + "/mods");
        String[] mods = vaultMods != null && vaultMods.isDirectory() ? vaultMods.list() : null;
        if (mods != null && mods.length > 0) {
            check.warnings.add("vault/mods has " + mods.length + " entries; the reference runs mounted no vault mods "
                    + "(without Game.prefs they should not reach the sim).");
        }
        // Measured on the emulator: the engine log grows by 1.6-22 MB per 1000 beats (its diagnostic warnings
        // scale with the unit count), so a long game can write hundreds of MB and run for many minutes.
        if (info.beats() > LONG_REPLAY_BEATS) {
            check.warnings.add("Long replay (" + info.beats() / 600 + " min of game time): the run takes long and "
                    + "its engine log can grow to hundreds of MB. 26675870 is the reference test.");
        }
        long free = root.dir().getUsableSpace();
        if (free > 0 && free < LOW_SPACE_BYTES) {
            check.warnings.add("Only " + (free >> 20) + " MB free on the data folder's storage; the run's logs may "
                    + "fill it.");
        }
        if (info.simMods() > 0) {
            check.warnings.add("The replay lists " + info.simMods() + " sim mod(s); the device has no mods, so it "
                    + "may stop early.");
        }
        int recorded = info.recordedGameVersion();
        if (recorded > 0 && !Integer.toString(recorded).equals(ENGINE_VERSION) && recorded != manifest.fafVersion) {
            check.warnings.add("Recorded on FAF " + recorded + ", the data is FAF " + manifest.fafVersion
                    + ": checksum mismatches against the recording are expected.");
        }
        if (!info.conversionError().isEmpty()) {
            check.warnings.add("The runner could not convert the replay (" + info.conversionError()
                    + "); the run passes the file as it is.");
        }
        return check;
    }

    /** Stems of the imported replays, newest first (for logs and housekeeping). */
    static List<String> list(DataRoot root) throws IOException {
        List<String> stems = new ArrayList<>();
        File dir = root.find(DIR);
        File[] files = dir != null ? dir.listFiles() : null;
        if (files == null) {
            return stems;
        }
        Arrays.sort(files, (a, b) -> Long.compare(b.lastModified(), a.lastModified()));
        for (File file : files) {
            String name = file.getName();
            if (name.endsWith(".json")) {
                stems.add(name.substring(0, name.length() - ".json".length()));
            }
        }
        return stems;
    }

    /** Header fields that are safe to put into meta.json (no titles, no names). */
    static JSONObject publicHeader(Info info) throws JSONException {
        JSONObject out = new JSONObject();
        JSONObject header = info.header();
        for (Iterator<String> keys = header.keys(); keys.hasNext();) {
            String key = keys.next();
            if (!key.equals("title")) {
                out.put(key, header.get(key));
            }
        }
        return out;
    }
}
