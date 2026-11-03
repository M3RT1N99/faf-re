package io.github.m3rt1n99.fafre;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * FAF's game files for one game version: each package as FAF's updater publishes it (whole, as
 * the game reads textures, shaders and sounds that headless never reads; tools/parity-data's
 * CI pack cuts them down to headless's files, which leaves the replays' checksums as they are),
 * FAF's init file with the vault set per run, fa_path.lua pointing at the user's own Supreme
 * Commander: Forged Alliance.
 */
final class GameFiles {
    /** FAF's packages the init file mounts; not every version publishes every one. */
    static final List<String> PACKAGES =
        Arrays.asList("effects", "env", "etc", "loc", "lua", "meshes", "mods", "projectiles", "textures", "units");
    static final Set<String> REQUIRED = new HashSet<>(Arrays.asList("lua", "units"));

    static final String VAULT_ANCHOR = "if custom_vault_path then";
    private static final Pattern VAULT_LINE = Pattern.compile("^custom_vault_path = .*$", Pattern.MULTILINE);
    private static final Pattern PARITY_UI = Pattern.compile("^MountDirectory\\('[^']*parity_ui', '/'\\)\n", Pattern.MULTILINE);

    interface Log {
        void line(String text);

        void step(String text);
    }

    private GameFiles() {
    }

    /**
     * Raised when the packing changes. 2: no data descriptors (0.6.0 test builds before it wrote
     * them, which the engine cannot read). 3: the packages whole, for the game; packs before it
     * were cut down to headless's files, so their packages are downloaded again.
     */
    static final int PACK_FORMAT = 3;

    static boolean ready(Storage storage, int version) {
        File dir = storage.game(version);
        return packFormat(dir) == PACK_FORMAT && new File(dir, "bin/init_ci.lua").isFile();
    }

    /** The manifest's pack format: -1 without a readable manifest, 1 before the field existed. */
    private static int packFormat(File dir) {
        File manifest = new File(dir, "manifest.json");
        if (!manifest.isFile()) {
            return -1;
        }
        try {
            return new JSONObject(new String(Files.readAllBytes(manifest.toPath()), StandardCharsets.UTF_8)).optInt("pack_format", 1);
        } catch (IOException | JSONException e) {
            return -1;
        }
    }

    /** The pack folder for {@code version}, downloading and packing what is missing. */
    static File ensure(Storage storage, int version, Log log) throws IOException {
        File dir = storage.game(version);
        if (ready(storage, version)) {
            return dir;
        }
        boolean stale = packFormat(dir) >= 1 && packFormat(dir) < PACK_FORMAT;
        File gamedata = Storage.mkdirs(new File(dir, "gamedata"));
        JSONObject manifest = new JSONObject();
        try {
            manifest.put("game_version", String.valueOf(version));
            manifest.put("pack_format", PACK_FORMAT);
            for (String name : PACKAGES) {
                File packed = new File(gamedata, name + ".nx2");
                String remote = name + "." + version + ".nx2";
                if (packed.isFile() && !stale) {
                    manifest.put(remote, "whole");
                    continue;
                }
                File download = new File(storage.downloads(), remote);
                try {
                    log.step("Downloading FAF " + remote);
                    Http.download(Http.GAME_URL + remote, download, (done, total) ->
                        log.step(String.format(Locale.ROOT, "Downloading FAF %s: %.0f / %s MB", remote, done / 1048576.0,
                            total < 0 ? "?" : String.format(Locale.ROOT, "%.0f", total / 1048576.0))));
                } catch (Http.StatusException e) {
                    if (e.status == 404 && !REQUIRED.contains(name)) {
                        log.line(remote + ": not published for " + version);
                        manifest.put(remote, "not published");
                        continue;
                    }
                    throw e;
                }
                Files.move(download.toPath(), packed.toPath(), StandardCopyOption.REPLACE_EXISTING);
                log.line(String.format(Locale.ROOT, "%s: %.1f MB", remote, packed.length() / 1048576.0));
                manifest.put(remote, "whole");
            }
            String initName = "init_faf_" + version + ".lua";
            log.step("Downloading " + initName);
            String init = Http.utf8(Http.bytes(Http.GAME_URL + initName));
            File bin = Storage.mkdirs(new File(dir, "bin"));
            write(new File(bin, "init_ci.lua"), ciInit(init));
            write(new File(dir, "fa_path.lua"),
                "fa_path = '" + storage.scfa.getAbsolutePath() + "'\nGameType = \"faf\"\nGameVersion = \"" + version
                    + "\"\nClientVersion = \"faf-re-android-" + BuildConfig.VERSION_NAME + "\"\n");
            write(new File(dir, "manifest.json"), manifest.toString(2));
        } catch (JSONException e) {
            throw new IOException(e);
        }
        return dir;
    }

    /**
     * FAF's init file with the vault line the runs set (make_pack.py ci_init): it has to come
     * right before {@code if custom_vault_path then}, as the engine resets one set earlier.
     */
    static String ciInit(String text) throws IOException {
        text = text.replace("\r\n", "\n");
        String line = "custom_vault_path = InitFileDir .. '/../vault'";
        Matcher existing = VAULT_LINE.matcher(text);
        if (existing.find()) {
            text = existing.replaceAll(Matcher.quoteReplacement(line));
        } else {
            int first = text.indexOf("\n" + VAULT_ANCHOR);
            if (first < 0 || text.indexOf("\n" + VAULT_ANCHOR, first + 1) >= 0) {
                throw new IOException("init file: expected one '" + VAULT_ANCHOR + "' line");
            }
            text = text.substring(0, first) + "\n" + line + text.substring(first);
        }
        return PARITY_UI.matcher(text).replaceAll("");
    }

    /** The init file of one run: init_ci.lua with the run's own vault. */
    static File runInit(File gameDir, String label, File vault) throws IOException {
        String text = new String(Files.readAllBytes(new File(gameDir, "bin/init_ci.lua").toPath()), StandardCharsets.UTF_8);
        String line = "custom_vault_path = '" + vault.getAbsolutePath() + "'";
        text = VAULT_LINE.matcher(text).replaceFirst(Matcher.quoteReplacement(line));
        File init = new File(gameDir, "bin/init_" + label + ".lua");
        write(init, text);
        return init;
    }

    static void write(File file, String text) throws IOException {
        Storage.mkdirs(file.getParentFile());
        try (OutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
    }
}
