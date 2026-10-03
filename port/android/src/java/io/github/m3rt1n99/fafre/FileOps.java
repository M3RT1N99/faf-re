package io.github.m3rt1n99.fafre;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Locale;

/**
 * Small file helpers with the durability rules the importer relies on:
 * metadata files are replaced atomically (temp file + rename in the same
 * directory) so a crash never leaves half a JSON file for the native runtime or
 * the PC script to choke on, and large copies go to "&lt;name&gt;.part" first so
 * a file with its final name is always complete.
 *
 * <p>Pure Java (no Android APIs) so it can be exercised on a host JVM.
 */
final class FileOps {
    /** Copy buffer: large enough that SAF and network reads are not syscall-bound. */
    static final int BUFFER_SIZE = 1024 * 1024;
    static final String PART_SUFFIX = ".part";

    private FileOps() {
    }

    static File partFile(File target) {
        return new File(target.getParentFile(), target.getName() + PART_SUFFIX);
    }

    /** Replaces {@code target} with {@code data} atomically. */
    static void writeAtomic(File target, byte[] data) throws IOException {
        File parent = target.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs() && !parent.isDirectory()) {
            throw new IOException("cannot create " + parent);
        }
        File temp = new File(parent, target.getName() + ".tmp");
        try (FileOutputStream out = new FileOutputStream(temp)) {
            out.write(data);
            out.getFD().sync();
        }
        moveReplacing(temp, target);
    }

    static void writeAtomic(File target, String text) throws IOException {
        writeAtomic(target, text.getBytes(StandardCharsets.UTF_8));
    }

    /**
     * Writes org.json output atomically, undoing Android's "\/" escaping of
     * '/' (valid JSON, but it makes the paths in these files hard to read and
     * grep). Safe because org.json escapes every '/', so "\/" in its output is
     * always an escaped slash and never the tail of an escaped backslash.
     */
    static void writeJson(File target, String jsonText) throws IOException {
        writeAtomic(target, jsonText.replace("\\/", "/") + "\n");
    }

    /** Renames {@code from} over {@code to} (same directory, so a plain rename(2)). */
    static void moveReplacing(File from, File to) throws IOException {
        try {
            Files.move(from.toPath(), to.toPath(), StandardCopyOption.REPLACE_EXISTING,
                    StandardCopyOption.ATOMIC_MOVE);
        } catch (AtomicMoveNotSupportedException e) {
            Files.move(from.toPath(), to.toPath(), StandardCopyOption.REPLACE_EXISTING);
        }
    }

    /** Whole file as UTF-8, or null if it does not exist. Refuses files above {@code maxBytes}. */
    static String readText(File file, int maxBytes) throws IOException {
        if (file == null || !file.isFile()) {
            return null;
        }
        if (file.length() > maxBytes) {
            throw new IOException(file.getName() + " is larger than " + maxBytes + " bytes");
        }
        try (InputStream in = new FileInputStream(file)) {
            ByteArrayOutputStream out = new ByteArrayOutputStream((int) file.length());
            byte[] buffer = new byte[8192];
            int n;
            while ((n = in.read(buffer)) > 0) {
                out.write(buffer, 0, n);
            }
            return new String(out.toByteArray(), StandardCharsets.UTF_8);
        }
    }

    /** The last {@code maxBytes} of a text file, starting at a line boundary; "" if missing. */
    static String tail(File file, int maxBytes) throws IOException {
        if (file == null || !file.isFile()) {
            return "";
        }
        try (RandomAccessFile in = new RandomAccessFile(file, "r")) {
            long length = in.length();
            long start = Math.max(0, length - maxBytes);
            in.seek(start);
            byte[] bytes = new byte[(int) (length - start)];
            in.readFully(bytes);
            int offset = 0;
            if (start > 0) {
                while (offset < bytes.length && bytes[offset] != '\n') {
                    ++offset;
                }
                offset = Math.min(bytes.length, offset + 1);
            }
            return new String(bytes, offset, bytes.length - offset, StandardCharsets.UTF_8);
        }
    }

    static MessageDigest sha256() {
        try {
            return MessageDigest.getInstance("SHA-256");
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException("SHA-256 is not available", e);
        }
    }

    /**
     * Feeds the first {@code length} bytes of {@code file} into {@code digest},
     * reporting each chunk to {@code progress} (may be null).
     */
    static void digest(File file, long length, MessageDigest digest, Cancellation cancel, Progress progress)
            throws IOException {
        byte[] buffer = new byte[BUFFER_SIZE];
        try (InputStream in = new FileInputStream(file)) {
            long left = length;
            while (left > 0) {
                cancel.throwIfCancelled();
                int n = in.read(buffer, 0, (int) Math.min(buffer.length, left));
                if (n < 0) {
                    throw new IOException(file.getName() + " is shorter than " + length + " bytes");
                }
                digest.update(buffer, 0, n);
                left -= n;
                if (progress != null) {
                    progress.addBytes(n);
                }
            }
        }
    }

    static String sha256(File file, Cancellation cancel, Progress progress) throws IOException {
        MessageDigest digest = sha256();
        digest(file, file.length(), digest, cancel, progress);
        return hex(digest.digest());
    }

    static String hex(byte[] bytes) {
        char[] digits = "0123456789abcdef".toCharArray();
        char[] out = new char[bytes.length * 2];
        for (int i = 0; i < bytes.length; ++i) {
            out[i * 2] = digits[(bytes[i] >> 4) & 0xf];
            out[i * 2 + 1] = digits[bytes[i] & 0xf];
        }
        return new String(out);
    }

    /** Size of a file, or of all files below a directory. */
    static long sizeRecursive(File file) {
        if (file.isFile()) {
            return file.length();
        }
        long total = 0;
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                total += sizeRecursive(child);
            }
        }
        return total;
    }

    /** Best effort; callers that need the file gone check for it afterwards. */
    static boolean deleteQuietly(File file) {
        return file == null || !file.exists() || file.delete();
    }

    /** Decimal units like Android's own storage screens ("1.2 GB"). */
    static String formatBytes(long bytes) {
        if (bytes < 1000) {
            return bytes + " B";
        }
        String[] units = {"kB", "MB", "GB", "TB"};
        double value = bytes;
        int unit = -1;
        while (value >= 1000 && unit < units.length - 1) {
            value /= 1000;
            ++unit;
        }
        return String.format(Locale.ROOT, value < 10 ? "%.2f %s" : value < 100 ? "%.1f %s" : "%.0f %s", value,
                units[unit]);
    }
}
