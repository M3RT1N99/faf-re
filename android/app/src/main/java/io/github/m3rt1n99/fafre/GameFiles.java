package io.github.m3rt1n99.fafre;

import android.content.res.AssetManager;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.channels.FileChannel;
import java.nio.channels.OverlappingFileLockException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.nio.file.StandardOpenOption;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.ZipFile;

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
    /** The UI package (android/TOUCH_UI.md): in the APK's assets, beside the game's files, and mounted under this name. */
    static final String UI_PACKAGE = "fafre_ui";

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

    /** The manifest says how gamedata's files are packed; the init file comes last of all. */
    static boolean ready(Storage storage, int version) {
        File dir = storage.game(version);
        return packFormat(dir) == PACK_FORMAT && new File(dir, "bin/init_ci.lua").isFile();
    }

    /** The game versions whose files are ready on the phone, oldest first; the game starts with the last. */
    static List<Integer> readyVersions(Storage storage) {
        List<Integer> versions = new ArrayList<>();
        String[] names = new File(storage.parity, "game").list();
        if (names != null) {
            for (String name : names) {
                try {
                    int version = Integer.parseInt(name);
                    if (ready(storage, version)) {
                        versions.add(version);
                    }
                } catch (NumberFormatException e) {
                    // not a version's folder
                }
            }
        }
        Collections.sort(versions);
        return versions;
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
    static File ensure(Storage storage, int version, Log log, Http.Downloads downloads) throws IOException {
        return ensure(storage, version, log, downloads, () -> {
        });
    }

    /**
     * The pack folder for {@code version} as {@link #ensure} gets it, for a download that has measured nothing before, the
     * replay test's: once it holds the version's lock and finds the version not ready, what is still to download is
     * measured by {@code requests} and claimed in the phone's room with the spare (Room.claim), right before the first
     * byte, as a start's and the Game card's are. A download that does not fit gets no byte (Room.NoRoom), and a version
     * that another download finished while this one waited for it needs no room.
     */
    static File ensureInRoom(Storage storage, int version, Log log, Http.Downloads downloads, Http.Requests requests,
        Room room) throws IOException {
        String what = "FAF game version " + version;
        return ensure(storage, version, log, downloads, () -> {
            log.step("Measuring the download of " + what);
            room.claim(what, NewestGameFiles.counted(toDownload(storage.game(version), storage.downloads(), version, requests)));
        });
    }

    /** What a download of a version does under its lock right before its first byte. */
    interface BeforeDownload {
        void run() throws IOException;
    }

    /**
     * The pack folder for {@code version} as {@link #ensure} gets it, with {@code before} run once it holds the version's
     * lock and finds the version not ready, right before the first byte: what is left to download then is what this
     * download gets, as no other download can change it (dropUnfinished, too, needs the lock).
     */
    static File ensure(Storage storage, int version, Log log, Http.Downloads downloads, BeforeDownload before)
        throws IOException {
        File dir = storage.game(version);
        if (ready(storage, version)) {
            return dir;
        }
        // One download of a version at a time: the game's process gets a start's version (Preparation) while the app's
        // jobs may be getting the same one, and the two would write the same files.
        try (FileChannel lock = lock(storage, version, log)) {
            if (!ready(storage, version)) {
                before.run();
                fetch(storage, version, dir, log, downloads);
            }
        }
        return dir;
    }

    /** Held while a download of {@code version} runs. It stays, as a download that waits for it has it open. */
    static File lockFile(Storage storage, int version) {
        return new File(storage.downloads(), "game-" + version + ".lock");
    }

    /**
     * {@code version}'s lock file, open, once this holds its lock: while another download of the version holds it, the other
     * process's, this waits, which Cancel ends. Closing the channel lets go. Another thread's hold in this process is waited
     * for too (OverlappingFileLockException), but each process downloads on one thread (TestService's job, the game's
     * preparation), and so it should stay: on Linux the end of any channel of a process on the file lets go of the process's
     * lock, also one that another channel took, and the other process would download beside it (so on the emulator's Android
     * 16, 2026-11-24).
     */
    private static FileChannel lock(Storage storage, int version, Log log) throws IOException {
        FileChannel channel = FileChannel.open(lockFile(storage, version).toPath(), StandardOpenOption.CREATE,
            StandardOpenOption.WRITE);
        boolean locked = false;
        try {
            for (boolean said = false; !tryLock(channel); said = true) {
                if (!said) {
                    String waiting = "Waiting for another download of FAF game version " + version;
                    log.line(waiting);
                    log.step(waiting);
                }
                Http.sleep(500);
            }
            locked = true;
            return channel;
        } finally {
            if (!locked) {
                channel.close();
            }
        }
    }

    /** Whether this now holds {@code channel}'s lock: false while another download holds it. */
    private static boolean tryLock(FileChannel channel) throws IOException {
        try {
            return channel.tryLock() != null;
        } catch (OverlappingFileLockException e) {
            // Another thread of this process holds it.
            return false;
        }
    }

    private static void fetch(Storage storage, int version, File dir, Log log, Http.Downloads downloads) throws IOException {
        // The init file first, so that one the app cannot use fails before 700 MB of packages, and nothing on the phone
        // changes. It is written last of all.
        String initName = "init_faf_" + version + ".lua";
        log.step("Downloading " + initName);
        String initText = ciInit(Http.utf8(downloads.bytes(Http.GAME_URL + initName)));
        File gamedata = new File(dir, "gamedata");
        File manifestFile = new File(dir, "manifest.json");
        File init = new File(dir, "bin/init_ci.lua");
        JSONObject manifest = new JSONObject();
        try {
            manifest.put("game_version", String.valueOf(version));
            manifest.put("pack_format", PACK_FORMAT);
            // An older pack, finished or not, goes. From here on the folder holds whole packages
            // only, so a download that is cut off goes on with the package it was at.
            if (packFormat(dir) != PACK_FORMAT) {
                Storage.deleteTree(gamedata);
                init.delete();
                write(manifestFile, manifest.toString(2));
            }
            Storage.mkdirs(gamedata);
            for (String name : PACKAGES) {
                File packed = new File(gamedata, name + ".nx2");
                String remote = name + "." + version + ".nx2";
                if (packed.isFile()) {
                    manifest.put(remote, "whole");
                    continue;
                }
                File download = new File(storage.downloads(), remote);
                try {
                    log.step("Downloading FAF " + remote);
                    downloads.download(Http.GAME_URL + remote, download, (done, total) ->
                        log.step("Downloading FAF " + remote + ": " + Sizes.progress(done, total)));
                } catch (Http.StatusException e) {
                    if (e.status == 404 && !REQUIRED.contains(name)) {
                        log.line(remote + ": not published for " + version);
                        manifest.put(remote, "not published");
                        continue;
                    }
                    throw e;
                }
                checkArchive(download);
                Files.move(download.toPath(), packed.toPath(), StandardCopyOption.REPLACE_EXISTING);
                log.line(remote + ": " + Sizes.of(packed.length()));
                manifest.put(remote, "whole");
            }
            write(new File(dir, "fa_path.lua"),
                "fa_path = '" + storage.scfa.getAbsolutePath() + "'\nGameType = \"faf\"\nGameVersion = \"" + version
                    + "\"\nClientVersion = \"faf-re-android-" + BuildConfig.VERSION_NAME + "\"\n");
            write(manifestFile, manifest.toString(2));
            // Last: with it the pack is ready, so it comes whole or not at all.
            File part = new File(init.getPath() + ".part");
            write(part, initText);
            Files.move(part.toPath(), init.toPath(), StandardCopyOption.ATOMIC_MOVE);
        } catch (JSONException e) {
            throw new IOException(e);
        }
    }

    /**
     * What {@link #ensure} left of {@code version} when it is not ready: its folder and its partial downloads. A ready
     * version stays whole, and so does one that another download is getting; false says so.
     */
    static boolean dropUnfinished(Storage storage, int version) {
        try (FileChannel lock = FileChannel.open(lockFile(storage, version).toPath(), StandardOpenOption.CREATE,
                StandardOpenOption.WRITE)) {
            // Ready asked under the lock: a download that held it may have finished the version.
            if (!tryLock(lock) || ready(storage, version)) {
                return false;
            }
            Storage.deleteTree(storage.game(version));
            for (String name : PACKAGES) {
                File download = new File(storage.downloads(), name + "." + version + ".nx2");
                download.delete();
                new File(download.getPath() + ".part").delete();
            }
            return true;
        } catch (IOException e) {
            // Without the lock another download may be getting it.
            return false;
        }
    }

    /**
     * What {@link #ensure} downloads for {@code version} into {@code dir}, in bytes, by HEAD requests to FAF's
     * updater: every package when the folder is not packed as now (ensure starts it over), else the ones not whole
     * yet, less what a download that was cut off left to resume. -1 when the updater does not say a length.
     */
    static long toDownload(File dir, File downloads, int version, Http.Requests requests) throws IOException {
        boolean current = packFormat(dir) == PACK_FORMAT;
        long total = 0;
        for (String name : PACKAGES) {
            if (current && new File(dir, "gamedata/" + name + ".nx2").isFile()) {
                continue;
            }
            String remote = name + "." + version + ".nx2";
            long length;
            try {
                length = requests.length(Http.GAME_URL + remote);
            } catch (Http.StatusException e) {
                if (e.status == 404 && !REQUIRED.contains(name)) {
                    continue;
                }
                throw e;
            }
            if (length < 0) {
                return -1;
            }
            total += Math.max(0, length - new File(downloads, remote + ".part").length());
        }
        return total;
    }

    /**
     * A package stays in gamedata for good, so only a whole archive may get there: a response without
     * a length that was cut off, or an error page with a 200, has passed the download's checks.
     */
    private static void checkArchive(File download) throws IOException {
        try {
            new ZipFile(download).close();
        } catch (IOException e) {
            download.delete();
            throw new IOException(download.getName() + " is not a whole archive: " + e.getMessage(), e);
        }
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
        File init = new File(gameDir, "bin/init_" + label + ".lua");
        write(init, runInitText(gameDir, vault));
        return init;
    }

    /**
     * The init file of a game: a run's, and the UI package (android/TOUCH_UI.md) comes along, from the
     * APK, so that an update reaches it. A replay's run stays without it, as it pins the simulation.
     */
    static File gameInit(AssetManager assets, File gameDir, File vault) throws IOException {
        File folder = new File(gameDir, UI_PACKAGE);
        Storage.deleteTree(folder);
        copyAssets(assets, UI_PACKAGE, folder);
        File init = gameInitFile(gameDir);
        write(init, withUiPackage(runInitText(gameDir, vault)));
        return init;
    }

    /** Where {@link #gameInit} writes, which the command line names before a start's preparation has fetched the version. */
    static File gameInitFile(File gameDir) {
        return new File(gameDir, "bin/init_game.lua");
    }

    private static String runInitText(File gameDir, File vault) throws IOException {
        String text = new String(Files.readAllBytes(new File(gameDir, "bin/init_ci.lua").toPath()), StandardCharsets.UTF_8);
        String line = "custom_vault_path = '" + vault.getAbsolutePath() + "'";
        return VAULT_LINE.matcher(text).replaceFirst(Matcher.quoteReplacement(line));
    }

    /** The init file with the UI package mounted and hooked: right before its vault line, ahead of the game's content. */
    static String withUiPackage(String text) throws IOException {
        Matcher vault = VAULT_LINE.matcher(text);
        if (!vault.find()) {
            throw new IOException("init file: no vault line");
        }
        String mount = "MountDirectory(InitFileDir .. '/../" + UI_PACKAGE + "', '/" + UI_PACKAGE + "')\n"
            + "table.insert(hook, '/" + UI_PACKAGE + "')\n";
        return text.substring(0, vault.start()) + mount + text.substring(vault.start());
    }

    private static void copyAssets(AssetManager assets, String path, File target) throws IOException {
        String[] names = assets.list(path);
        if (names == null || names.length == 0) {
            // list() has nothing to say of a file.
            Storage.mkdirs(target.getParentFile());
            try (InputStream in = assets.open(path)) {
                Files.copy(in, target.toPath(), StandardCopyOption.REPLACE_EXISTING);
            }
            return;
        }
        for (String name : names) {
            copyAssets(assets, path + "/" + name, new File(target, name));
        }
    }

    static void write(File file, String text) throws IOException {
        Storage.mkdirs(file.getParentFile());
        try (OutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
    }
}
