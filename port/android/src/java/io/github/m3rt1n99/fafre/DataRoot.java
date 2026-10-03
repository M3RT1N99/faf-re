package io.github.m3rt1n99.fafre;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.util.ArrayDeque;
import java.util.Arrays;
import java.util.Deque;

/**
 * The app's data root, {@code Context.getExternalFilesDir(null)}
 * (/sdcard/Android/data/io.github.m3rt1n99.fafre/files), which is also what
 * NativeActivity hands the runtime as {@code externalDataPath}. The PC deploy
 * script pushes into the same directory with adb.
 *
 * <p>Every path the launcher writes is built here from validated components and
 * checked to stay inside the root after symlinks are resolved. Lookups match
 * each component case-insensitively when the exact spelling does not exist:
 * the PC script keeps the case of the user's install, the SAF importer keeps
 * the case the document provider reports, and shared storage may or may not
 * fold case, so the exact spelling on disk is never assumed.
 *
 * <p>Pure Java (no Android APIs) so it can be exercised on a host JVM.
 */
final class DataRoot {
    /** Fixed by the launch contract; GameActivity uses it before the manifest is available. */
    static final String STATUS_JSON = "launch/status.json";

    private final File mDir;
    private final Path mReal;

    DataRoot(File dir) throws IOException {
        if (dir == null) {
            throw new IOException("shared storage is not available (getExternalFilesDir returned null)");
        }
        mDir = dir.getAbsoluteFile();
        if (!mDir.isDirectory() && !mDir.mkdirs() && !mDir.isDirectory()) {
            throw new IOException("cannot create the data folder " + mDir);
        }
        mReal = mDir.toPath().toRealPath();
    }

    File dir() {
        return mDir;
    }

    /** Absolute path with '/' separators, as written into fa_path.lua and argv. */
    String path() {
        return mDir.getAbsolutePath().replace('\\', '/');
    }

    /** Absolute '/'-path of a root-relative path, for argv and display. */
    String path(String relative) {
        return path() + "/" + relative;
    }

    /** The exact path {@code relative} names, validated but not resolved against the disk. */
    File file(String relative) throws IOException {
        String[] parts = split(relative);
        File current = mDir;
        for (String part : parts) {
            current = new File(current, part);
        }
        checkInside(current);
        return current;
    }

    /**
     * The existing file or directory {@code relative} names, matching each
     * component case-insensitively when the exact spelling is missing; null if
     * any component does not exist.
     */
    File find(String relative) throws IOException {
        String[] parts = split(relative);
        File current = mDir;
        for (String part : parts) {
            current = child(current, part);
            if (current == null) {
                return null;
            }
        }
        checkInside(current);
        return current;
    }

    /**
     * Where to write {@code relative}: existing directories are reused even if
     * their case differs, missing ones are created with the given spelling, and
     * an existing file with a different case is reused so a re-import replaces
     * it instead of leaving a second copy next to it. The returned file may or
     * may not exist yet.
     */
    File prepare(String relative) throws IOException {
        String[] parts = split(relative);
        File current = mDir;
        for (int i = 0; i < parts.length; ++i) {
            File next = child(current, parts[i]);
            boolean last = i == parts.length - 1;
            if (next == null) {
                next = new File(current, parts[i]);
                // Checked before mkdir: a symlinked parent must not let us
                // create anything outside the root, not even an empty folder.
                checkInside(next);
                if (!last && !next.mkdir() && !next.isDirectory()) {
                    throw new IOException("cannot create directory " + next);
                }
            } else if (!last && !next.isDirectory()) {
                throw new IOException(next + " exists but is not a directory");
            }
            current = next;
        }
        checkInside(current);
        return current;
    }

    /**
     * Throws unless {@code file} resolves, symlinks included, to the root or a
     * path below it. The deepest existing ancestor is resolved with
     * toRealPath (File.getCanonicalPath does not follow links on every
     * platform) and the missing rest is appended; a dangling link fails to
     * resolve and is refused too.
     */
    void checkInside(File file) throws IOException {
        Path path = file.toPath().toAbsolutePath().normalize();
        Deque<Path> missing = new ArrayDeque<>();
        Path existing = path;
        while (existing != null && !Files.exists(existing, LinkOption.NOFOLLOW_LINKS)) {
            missing.push(existing.getFileName());
            existing = existing.getParent();
        }
        if (existing == null) {
            throw new IOException("refusing to touch " + file + ": no existing parent");
        }
        Path real = existing.toRealPath();
        while (!missing.isEmpty()) {
            real = real.resolve(missing.pop());
        }
        if (!real.startsWith(mReal)) {
            throw new IOException("refusing to touch " + file + ": outside the data folder");
        }
    }

    /** {@code name} in {@code dir}: the exact spelling if it exists, else the first case-insensitive match. */
    static File child(File dir, String name) {
        File exact = new File(dir, name);
        if (exact.exists()) {
            return exact;
        }
        String[] names = dir.list();
        if (names == null) {
            return null;
        }
        // Sorted so that two case variants (possible on a case-sensitive file
        // system) always resolve the same way.
        Arrays.sort(names);
        for (String candidate : names) {
            if (Names.equalsIgnoreCaseAscii(candidate, name)) {
                return new File(dir, candidate);
            }
        }
        return null;
    }

    private static String[] split(String relative) throws IOException {
        String[] parts = Names.splitRelative(relative);
        if (parts == null) {
            throw new IOException("unsafe path inside the data folder: " + relative);
        }
        return parts;
    }
}
