package io.github.m3rt1n99.fafre;

import android.content.Context;
import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;

/**
 * The headless replay runner as the APK ships it (milestone M3c, port/engine/runner): three files
 * that the installer extracts into {@code nativeLibraryDir} because the manifest sets
 * extractNativeLibs=true.
 *
 * <ul>
 * <li>{@code libfafrunner.so}: the executable {@code faf_headless_runner} (a PIE with interpreter
 * /system/bin/linker64; Android only extracts lib*.so names). It carries the low arena and loads
 * the engine library below 2 GB.</li>
 * <li>{@code libfafengine.so}: the engine, loaded by the executable, never by this process.</li>
 * <li>{@code libfafarenaprobe.so}: a stand-in engine that checks where the arena puts everything
 * (port/engine/lowarena/probe), the first step of the self-test.</li>
 * </ul>
 *
 * <p>Also the lowercase alias of the data root. The engine lower-cases every directory it mounts and
 * every file it opens (CVFSImpl, FileWaitHandleSet), so the runner must see a data root whose path
 * has no upper-case letter; {@code /storage/emulated/0/Android/data/...} has one. The alias is a
 * symlink {@code <filesDir>/r} to the external files dir, and every path the runner gets is built on
 * it.
 */
final class Runner {
    static final String EXECUTABLE = "libfafrunner.so";
    static final String ENGINE = "libfafengine.so";
    static final String PROBE = "libfafarenaprobe.so";
    static final String[] FILES = {EXECUTABLE, ENGINE, PROBE};
    /** Name of the alias symlink in getFilesDir(). */
    static final String ALIAS_NAME = "r";
    static final String BUILD_INFO_ASSET = "build.json";

    private static JSONObject sBuildInfo;

    private Runner() {
    }

    static File nativeDir(Context context) {
        return new File(context.getApplicationInfo().nativeLibraryDir);
    }

    static File file(Context context, String name) {
        return new File(nativeDir(context), name);
    }

    /** Why the runner cannot be started, or null if it can. Touches the disk; call off the UI thread. */
    static String problem(Context context) {
        File dir = nativeDir(context);
        File executable = new File(dir, EXECUTABLE);
        File engine = new File(dir, ENGINE);
        if (!executable.isFile() && !engine.isFile()) {
            if (AppInfo.nativeRuntimeProblem(context) != null) {
                return "This APK has no native code for this device.";
            }
            return "This APK does not contain the replay runner (" + EXECUTABLE + "); it was built with "
                    + "-SkipRunner.";
        }
        if (!executable.isFile() || !engine.isFile()) {
            return "The replay runner is incomplete in " + dir + " (" + (executable.isFile() ? ENGINE : EXECUTABLE)
                    + " is missing). Reinstall the APK.";
        }
        if (!executable.canExecute()) {
            return EXECUTABLE + " is not executable in " + dir + ". The APK must be built with "
                    + "extractNativeLibs=true.";
        }
        return null;
    }

    /** assets/build.json written by build_android.ps1 (version, commit, packaged binaries); empty if absent. */
    static synchronized JSONObject buildInfo(Context context) {
        if (sBuildInfo == null) {
            JSONObject info = new JSONObject();
            try (InputStream in = context.getAssets().open(BUILD_INFO_ASSET)) {
                ByteArrayOutputStream bytes = new ByteArrayOutputStream();
                byte[] buffer = new byte[8192];
                int n;
                while ((n = in.read(buffer)) > 0) {
                    bytes.write(buffer, 0, n);
                }
                info = new JSONObject(new String(bytes.toByteArray(), StandardCharsets.UTF_8));
            } catch (IOException | JSONException e) {
                // A build without the asset (older script): the meta file says so.
            }
            sBuildInfo = info;
        }
        return sBuildInfo;
    }

    /**
     * Creates (or repairs) {@code <filesDir>/r -> getExternalFilesDir(null)} and returns the alias
     * path. Call off the UI thread.
     */
    static String ensureAlias(Context context) throws IOException {
        File target = context.getExternalFilesDir(null);
        if (target == null) {
            throw new IOException("shared storage is not available (getExternalFilesDir returned null)");
        }
        String want = target.getAbsolutePath();
        File link = new File(context.getFilesDir(), ALIAS_NAME);
        String path = link.getAbsolutePath();
        File parent = link.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs() && !parent.isDirectory()) {
            throw new IOException("cannot create " + parent);
        }
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (Files.isSymbolicLink(link.toPath())) {
                try {
                    if (want.equals(Os.readlink(path))) {
                        return path;
                    }
                } catch (ErrnoException e) {
                    throw new IOException("cannot read the link " + path + ": " + e.getMessage(), e);
                }
                if (!link.delete()) {
                    throw new IOException("cannot replace the stale link " + path);
                }
            } else if (link.exists()) {
                // Nothing else of ours is called "r"; an empty file or folder in its place is debris.
                if (!link.delete()) {
                    throw new IOException(path + " exists and is not the data root alias");
                }
            }
            try {
                Os.symlink(want, path);
                return path;
            } catch (ErrnoException e) {
                if (e.errno != OsConstants.EEXIST) {
                    throw new IOException("cannot create the link " + path + " -> " + want + ": " + e.getMessage(), e);
                }
                // Created concurrently (launcher and inbox); check it again.
            }
        }
        throw new IOException("cannot create the link " + path);
    }

    /** True when the engine would see the path unchanged after lower-casing it. */
    static boolean isLowerCase(String path) {
        return path.equals(path.toLowerCase(Locale.ROOT));
    }

    /**
     * The GNU build id of an ELF file (NT_GNU_BUILD_ID in a PT_NOTE segment) as lower-case hex, or
     * "" when it has none or is not a little-endian ELF64.
     */
    static String buildId(File file) {
        try (RandomAccessFile in = new RandomAccessFile(file, "r")) {
            byte[] header = new byte[64];
            in.readFully(header);
            if (header[0] != 0x7f || header[1] != 'E' || header[2] != 'L' || header[3] != 'F' || header[4] != 2
                    || header[5] != 1) {
                return "";
            }
            long phoff = le64(header, 32);
            int phentsize = le16(header, 54);
            int phnum = le16(header, 56);
            if (phentsize < 56 || phnum <= 0 || phnum > 128) {
                return "";
            }
            byte[] entry = new byte[phentsize];
            for (int i = 0; i < phnum; ++i) {
                in.seek(phoff + (long) i * phentsize);
                in.readFully(entry);
                if (le32(entry, 0) != 4 /* PT_NOTE */) {
                    continue;
                }
                long offset = le64(entry, 8);
                long size = le64(entry, 32);
                if (size <= 0 || size > 65536) {
                    continue;
                }
                byte[] notes = new byte[(int) size];
                in.seek(offset);
                in.readFully(notes);
                int at = 0;
                while (at + 12 <= notes.length) {
                    int nameSize = le32(notes, at);
                    int descSize = le32(notes, at + 4);
                    int type = le32(notes, at + 8);
                    int name = at + 12;
                    int desc = name + align4(nameSize);
                    if (nameSize < 0 || descSize < 0 || desc + descSize > notes.length) {
                        break;
                    }
                    if (type == 3 /* NT_GNU_BUILD_ID */ && nameSize == 4 && notes[name] == 'G' && notes[name + 1] == 'N'
                            && notes[name + 2] == 'U') {
                        byte[] id = new byte[descSize];
                        System.arraycopy(notes, desc, id, 0, descSize);
                        return FileOps.hex(id);
                    }
                    at = desc + align4(descSize);
                }
            }
        } catch (IOException e) {
            return "";
        }
        return "";
    }

    /** sha256, build id and size of each runner file in nativeLibraryDir (for meta.json). */
    static Map<String, JSONObject> describeBinaries(Context context, Cancellation cancel) throws IOException {
        Map<String, JSONObject> out = new LinkedHashMap<>();
        JSONObject packaged = buildInfo(context).optJSONObject("libraries");
        for (String name : new String[] {AppInfo.NATIVE_LIBRARY_FILE, EXECUTABLE, ENGINE, PROBE}) {
            File file = file(context, name);
            JSONObject entry = new JSONObject();
            try {
                entry.put("path", file.getAbsolutePath());
                entry.put("present", file.isFile());
                if (file.isFile()) {
                    entry.put("size", file.length());
                    entry.put("sha256", FileOps.sha256(file, cancel, null));
                    entry.put("build_id", buildId(file));
                    JSONObject expected = packaged != null ? packaged.optJSONObject(name) : null;
                    if (expected != null) {
                        entry.put("matches_build_json", expected.optString("sha256").equals(entry.getString("sha256")));
                    }
                }
            } catch (JSONException e) {
                throw new IOException(e.getMessage(), e);
            }
            out.put(name, entry);
        }
        return out;
    }

    private static int align4(int value) {
        return (value + 3) & ~3;
    }

    private static int le16(byte[] b, int at) {
        return (b[at] & 0xff) | (b[at + 1] & 0xff) << 8;
    }

    private static int le32(byte[] b, int at) {
        return (b[at] & 0xff) | (b[at + 1] & 0xff) << 8 | (b[at + 2] & 0xff) << 16 | (b[at + 3] & 0xff) << 24;
    }

    private static long le64(byte[] b, int at) {
        return (le32(b, at) & 0xffffffffL) | ((long) le32(b, at + 4)) << 32;
    }
}
