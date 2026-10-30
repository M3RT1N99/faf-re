package io.github.m3rt1n99.fafre;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.Enumeration;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.CRC32;
import java.util.zip.Deflater;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.ZipOutputStream;

/**
 * FAF's game files for one game version, packed as tools/parity-data packs them for CI
 * (prewarm.py game, make_pack.py): each package downloaded from FAF's updater and cut down
 * to the files headless reads (blueprints, Lua, animations, meshes' bones, maps), every other
 * file kept as an empty file of the same name and time; FAF's init file with the vault set
 * per run; fa_path.lua pointing at the user's own Supreme Commander: Forged Alliance.
 */
final class GameFiles {
    /** FAF's packages the init file mounts; not every version publishes every one. */
    static final List<String> PACKAGES =
        Arrays.asList("effects", "env", "etc", "loc", "lua", "meshes", "mods", "projectiles", "textures", "units");
    static final Set<String> REQUIRED = new HashSet<>(Arrays.asList("lua", "units"));
    /** make_pack.py CONTENT_TYPES: the files whose bytes headless reads. */
    static final Set<String> CONTENT_TYPES = new HashSet<>(Arrays.asList(".bp", ".lua", ".scm", ".sca", ".scmap"));

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
     * Raised when the packing changes; an older pack is packed again from itself. 2: no data
     * descriptors (0.6.0 test builds before it wrote them, which the engine cannot read).
     */
    static final int PACK_FORMAT = 2;

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
        boolean repack = packFormat(dir) >= 1;
        File gamedata = Storage.mkdirs(new File(dir, "gamedata"));
        JSONObject manifest = new JSONObject();
        try {
            manifest.put("game_version", String.valueOf(version));
            manifest.put("pack_format", PACK_FORMAT);
            for (String name : PACKAGES) {
                File packed = new File(gamedata, name + ".nx2");
                String remote = name + "." + version + ".nx2";
                if (packed.isFile()) {
                    if (repack) {
                        log.step("Packing " + remote + " again");
                        pack(packed, packed);
                    }
                    manifest.put(remote, "packed");
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
                log.step("Packing " + remote);
                long[] stats = pack(download, packed);
                log.line(String.format(Locale.ROOT, "%s: %.1f MB -> %.1f MB, %d files kept, %d emptied", remote,
                    download.length() / 1048576.0, packed.length() / 1048576.0, stats[0], stats[1]));
                download.delete();
                manifest.put(remote, "packed");
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

    /**
     * make_pack.py pack_archive: the same entries, order, names and times; bytes only for the
     * content types. Returns {kept, emptied}.
     */
    static long[] pack(File source, File dest) throws IOException {
        File temp = new File(dest.getPath() + ".tmp");
        long kept = 0;
        long emptied = 0;
        byte[] buffer = new byte[1 << 16];
        try (ZipFile in = new ZipFile(source); ZipOutputStream out = new ZipOutputStream(new FileOutputStream(temp))) {
            out.setLevel(9);
            for (Enumeration<? extends ZipEntry> entries = in.entries(); entries.hasMoreElements(); ) {
                ZipEntry entry = entries.nextElement();
                ZipEntry copy = new ZipEntry(entry.getName());
                copy.setTime(entry.getTime());
                String name = entry.getName().toLowerCase(Locale.ROOT);
                int dot = name.lastIndexOf('.');
                boolean content = !entry.isDirectory() && dot > name.lastIndexOf('/') && CONTENT_TYPES.contains(name.substring(dot));
                if (content) {
                    // Sizes and CRC in the local header, no data descriptor after the data: the
                    // engine's zip reader, like the 2007 one, does not know data descriptors, and
                    // ZipOutputStream writes one for a DEFLATED entry whose sizes it is not given.
                    // Its deflater at level 9 gives the size computed here.
                    byte[] data;
                    try (InputStream stream = in.getInputStream(entry)) {
                        data = readAll(stream, buffer);
                    }
                    CRC32 crc = new CRC32();
                    crc.update(data);
                    copy.setMethod(ZipEntry.DEFLATED);
                    copy.setSize(data.length);
                    copy.setCompressedSize(deflatedSize(data, buffer));
                    copy.setCrc(crc.getValue());
                    out.putNextEntry(copy);
                    out.write(data);
                    ++kept;
                } else {
                    copy.setMethod(ZipEntry.STORED);
                    copy.setSize(0);
                    copy.setCompressedSize(0);
                    copy.setCrc(new CRC32().getValue());
                    out.putNextEntry(copy);
                    if (!entry.isDirectory()) {
                        ++emptied;
                    }
                }
                out.closeEntry();
                Cancellation.check();
            }
        } catch (IOException e) {
            temp.delete();
            throw e;
        }
        if (dest.exists() && !dest.delete()) {
            throw new IOException("cannot replace " + dest);
        }
        if (!temp.renameTo(dest)) {
            throw new IOException("cannot rename " + temp);
        }
        return new long[] {kept, emptied};
    }

    private static byte[] readAll(InputStream in, byte[] buffer) throws IOException {
        java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
        int read;
        while ((read = in.read(buffer)) > 0) {
            out.write(buffer, 0, read);
        }
        return out.toByteArray();
    }

    /** The size of {@code data} deflated as ZipOutputStream at level 9 deflates it (raw, no zlib header). */
    private static long deflatedSize(byte[] data, byte[] buffer) {
        Deflater deflater = new Deflater(9, true);
        try {
            deflater.setInput(data);
            deflater.finish();
            long size = 0;
            while (!deflater.finished()) {
                size += deflater.deflate(buffer);
            }
            return size;
        } finally {
            deflater.end();
        }
    }

    static void write(File file, String text) throws IOException {
        Storage.mkdirs(file.getParentFile());
        try (OutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
    }
}
