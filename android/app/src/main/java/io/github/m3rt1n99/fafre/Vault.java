package io.github.m3rt1n99.fafre;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * The maps and M28AI versions a replay needs, as tools/parity-data/shard.py gets them: a map
 * the user's SCFA install has (maps/, mounted by the init file) is used from there; any other
 * comes from FAF's map vault into a per-run vault folder. The game's own vault gets the map and
 * the mods a launcher's start names (Preparation) the same way.
 */
final class Vault {
    /** FAF has no such map or mod: its file holds none of the name or uid asked for. */
    static final class Missing extends IOException {
        Missing(String message) {
            super(message);
        }
    }

    /** A mod's uid in its mod_info.lua: {@code uid = "..."}. */
    private static final Pattern MOD_UID = Pattern.compile("^\\s*uid\\s*=\\s*[\"']([^\"'\\r\\n]+)[\"']", Pattern.MULTILINE);

    private Vault() {
    }

    /** The cached map folder for {@code folder}, downloaded when missing; null when SCFA has it. */
    static File map(Storage storage, String folder, GameFiles.Log log) throws IOException {
        if (inScfa(storage, folder)) {
            return null;
        }
        return fetchMap(storage, storage.maps(), folder, log, Http.WITH_RETRIES, null);
    }

    /** Whether the user's SCFA has the map {@code folder} (maps/, which every init file mounts). */
    static boolean inScfa(Storage storage, String folder) {
        File retail = Storage.findChild(new File(storage.scfa, "maps"), folder);
        return retail != null && hasScenario(retail);
    }

    /** The map {@code folder} in {@code maps}, from FAF's map vault when it is not there yet. */
    static File fetchMap(Storage storage, File maps, String folder, GameFiles.Log log, Http.Downloads downloads,
            Http.Progress progress) throws IOException {
        File have = Storage.findChild(maps, folder);
        if (have != null && hasScenario(have)) {
            return have;
        }
        File zip = new File(storage.downloads(), folder + ".zip");
        log.step("Downloading map " + folder);
        downloads.download(Http.MAP_URL + Http.quote(folder) + ".zip", zip, progress);
        try {
            return extractMap(zip, maps, folder);
        } finally {
            zip.delete();
        }
    }

    /**
     * The mod {@code uid} in the game's vault {@code mods}: the folder there whose mod_info.lua has it, or else FAF's
     * {@code file} of it (content.faforever.com/mods/; null when the start names none), which must hold that uid, as the
     * zip's own folder, in place of an older version there.
     */
    static File fetchMod(Storage storage, File mods, String uid, String file, GameFiles.Log log, Http.Downloads downloads,
            Http.Progress progress) throws IOException {
        File have = modWithUid(mods, uid);
        if (have != null) {
            return have;
        }
        if (file == null) {
            throw new Missing("the phone has no mod " + uid + ", and the start names no file of it");
        }
        File zip = new File(storage.downloads(), uid + ".zip");
        log.step("Downloading mod " + file);
        downloads.download(Http.MOD_URL + Http.quote(file), zip, progress);
        File raw = new File(mods, ".unzip-" + uid);
        try {
            Storage.deleteTree(raw);
            unzip(zip, raw);
            File found = findModInfo(raw);
            if (found == null) {
                throw new Missing(file + " holds no mod_info.lua");
            }
            String held = modUid(new File(found, "mod_info.lua"));
            if (!uid.equalsIgnoreCase(held)) {
                throw new Missing(file + " holds the mod " + held + ", not " + uid);
            }
            File path = new File(mods, found.equals(raw) ? uid : found.getName());
            Storage.deleteTree(path);
            if (!found.renameTo(path)) {
                throw new IOException("cannot move " + found + " to " + path);
            }
            return path;
        } finally {
            Storage.deleteTree(raw);
            zip.delete();
        }
    }

    /** The folder of {@code mods} whose mod_info.lua has {@code uid}, in any case; null when none has. */
    static File modWithUid(File mods, String uid) throws IOException {
        File[] folders = mods.listFiles(File::isDirectory);
        if (folders != null) {
            for (File folder : folders) {
                File info = new File(folder, "mod_info.lua");
                if (info.isFile() && uid.equalsIgnoreCase(modUid(info))) {
                    return folder;
                }
            }
        }
        return null;
    }

    /** The uid in a mod_info.lua, or null without one. */
    static String modUid(File modInfo) throws IOException {
        Matcher uid = MOD_UID.matcher(new String(Files.readAllBytes(modInfo.toPath()), StandardCharsets.UTF_8));
        return uid.find() ? uid.group(1) : null;
    }

    /** The cached M28AI mod folder of {@code version}, downloaded when missing. */
    static File m28(Storage storage, int version, GameFiles.Log log) throws IOException {
        String tag = String.format(Locale.ROOT, "m28ai.v%04d", version);
        File dir = new File(storage.m28(), tag);
        File found = findModInfo(dir);
        if (found != null) {
            return found;
        }
        File zip = new File(storage.downloads(), tag + ".zip");
        log.step("Downloading M28AI v" + version);
        Http.download(Http.MOD_URL + tag + ".zip", zip, null);
        try {
            Storage.deleteTree(dir);
            unzip(zip, dir);
        } finally {
            zip.delete();
        }
        found = findModInfo(dir);
        if (found == null) {
            throw new IOException(tag + ".zip holds no mod_info.lua");
        }
        return found;
    }

    /**
     * The map zip's folder that holds the scenario file, as {@code maps/<folder>} (common.py
     * extract_map): the names may differ in case alone, and the folder need not be at the top.
     */
    static File extractMap(File zip, File mapsDir, String folder) throws IOException {
        File raw = new File(mapsDir, ".unzip-" + folder);
        Storage.deleteTree(raw);
        unzip(zip, raw);
        List<File> found = new ArrayList<>();
        collectMapFolders(raw, found);
        List<File> named = new ArrayList<>();
        for (File dir : found) {
            if (dir.getName().equalsIgnoreCase(folder)) {
                named.add(dir);
            }
        }
        if (named.size() != 1 && found.size() != 1) {
            Storage.deleteTree(raw);
            throw new Missing("no one map folder " + folder + " in the map zip (" + found.size() + " with a scenario file)");
        }
        File path = new File(mapsDir, folder);
        Storage.deleteTree(path);
        File source = named.size() == 1 ? named.get(0) : found.get(0);
        if (!source.renameTo(path)) {
            throw new IOException("cannot move " + source + " to " + path);
        }
        Storage.deleteTree(raw);
        return path;
    }

    static boolean hasScenario(File dir) {
        String[] names = dir.list();
        if (names != null) {
            for (String name : names) {
                if (name.toLowerCase(Locale.ROOT).endsWith("_scenario.lua")) {
                    return true;
                }
            }
        }
        return false;
    }

    private static void collectMapFolders(File dir, List<File> out) {
        if (hasScenario(dir)) {
            out.add(dir);
        }
        File[] children = dir.listFiles();
        if (children != null) {
            for (File child : children) {
                if (child.isDirectory()) {
                    collectMapFolders(child, out);
                }
            }
        }
    }

    private static File findModInfo(File dir) {
        if (new File(dir, "mod_info.lua").isFile()) {
            return dir;
        }
        File[] children = dir.listFiles();
        if (children != null) {
            for (File child : children) {
                if (child.isDirectory()) {
                    File found = findModInfo(child);
                    if (found != null) {
                        return found;
                    }
                }
            }
        }
        return null;
    }

    static void unzip(File zip, File dest) throws IOException {
        String root = dest.getCanonicalPath() + File.separator;
        byte[] buffer = new byte[1 << 16];
        try (ZipInputStream in = new ZipInputStream(new FileInputStream(zip))) {
            ZipEntry entry;
            while ((entry = in.getNextEntry()) != null) {
                File target = new File(dest, entry.getName());
                if (!target.getCanonicalPath().startsWith(root)) {
                    throw new IOException("zip entry outside its folder: " + entry.getName());
                }
                if (entry.isDirectory()) {
                    Storage.mkdirs(target);
                    continue;
                }
                Storage.mkdirs(target.getParentFile());
                try (OutputStream out = new FileOutputStream(target)) {
                    int read;
                    while ((read = in.read(buffer)) > 0) {
                        out.write(buffer, 0, read);
                    }
                }
            }
        }
    }

    /** A copy of the folder {@code source} as {@code dest} (shared storage has no links). */
    static void copyTree(File source, File dest) throws IOException {
        if (source.isDirectory()) {
            Storage.mkdirs(dest);
            File[] children = source.listFiles();
            if (children != null) {
                for (File child : children) {
                    copyTree(child, new File(dest, child.getName()));
                }
            }
            return;
        }
        byte[] buffer = new byte[1 << 16];
        try (InputStream in = new FileInputStream(source); OutputStream out = new FileOutputStream(dest)) {
            int read;
            while ((read = in.read(buffer)) > 0) {
                out.write(buffer, 0, read);
            }
        }
    }
}
