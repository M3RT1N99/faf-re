package io.github.m3rt1n99.fafre;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/**
 * "Save run (zip)": one replay test run directory as a zip, written to a document the user picked
 * (ACTION_CREATE_DOCUMENT), because file managers cannot open Android/data and the app has no
 * FileProvider. Replay files are never included (they carry player names; the run's meta.json has
 * their sha256), and neither is anything outside the run directory except the tail of launcher.log.
 *
 * <p>Pure Java (no Android APIs) so it can be exercised on a host JVM.
 */
final class RunZip {
    private static final long MAX_FILE_BYTES = 64L * 1024 * 1024;
    private static final long HEAD_BYTES = 16L * 1024 * 1024;
    private static final int LAUNCHER_LOG_TAIL = 256 * 1024;

    private RunZip() {
    }

    /** Writes the zip; returns the number of files in it. */
    static int write(File runDir, File launcherLog, OutputStream target) throws IOException {
        int count = 0;
        String prefix = "fafre-run-" + runDir.getName() + "/";
        try (ZipOutputStream zip = new ZipOutputStream(target)) {
            zip.setLevel(9);
            File[] files = runDir.listFiles();
            if (files != null) {
                Arrays.sort(files);
                for (File file : files) {
                    if (!Files.isRegularFile(file.toPath(), java.nio.file.LinkOption.NOFOLLOW_LINKS)
                            || excluded(file.getName())) {
                        continue;
                    }
                    zip.putNextEntry(entry(prefix + file.getName(), file.lastModified()));
                    copy(file, zip, MAX_FILE_BYTES);
                    zip.closeEntry();
                    ++count;
                }
            }
            if (launcherLog != null && launcherLog.isFile()) {
                zip.putNextEntry(entry(prefix + "launcher.log", launcherLog.lastModified()));
                zip.write(FileOps.tail(launcherLog, LAUNCHER_LOG_TAIL).getBytes(StandardCharsets.UTF_8));
                zip.closeEntry();
                ++count;
            }
        }
        return count;
    }

    static boolean excluded(String name) {
        String lower = name.toLowerCase(Locale.ROOT);
        return lower.endsWith(ReplayFiles.FAF_EXTENSION) || lower.endsWith(ReplayFiles.SCFA_EXTENSION)
                || lower.endsWith(FileOps.PART_SUFFIX) || lower.endsWith(".tmp");
    }

    private static ZipEntry entry(String name, long time) {
        ZipEntry entry = new ZipEntry(name);
        entry.setTime(time > 0 ? time : System.currentTimeMillis());
        return entry;
    }

    /**
     * Copies the file, or for one larger than {@code limit} its first {@link #HEAD_BYTES} and its last
     * {@code limit - HEAD_BYTES} bytes (a long run's engine log can reach hundreds of MB; the start shows the
     * setup, the end how the run finished).
     */
    private static void copy(File file, OutputStream out, long limit) throws IOException {
        long length = file.length();
        try (InputStream in = new FileInputStream(file)) {
            if (length <= limit) {
                copyBytes(in, out, Long.MAX_VALUE);
                return;
            }
            long head = Math.min(HEAD_BYTES, limit);
            long tail = limit - head;
            copyBytes(in, out, head);
            long skip = length - head - tail;
            out.write(("\n[zip] " + skip + " of " + length + " bytes left out here\n").getBytes(StandardCharsets.UTF_8));
            while (skip > 0) {
                long skipped = in.skip(skip);
                if (skipped <= 0) {
                    break;
                }
                skip -= skipped;
            }
            copyBytes(in, out, tail);
        }
    }

    private static void copyBytes(InputStream in, OutputStream out, long limit) throws IOException {
        byte[] buffer = new byte[64 * 1024];
        long left = limit;
        int n;
        while (left > 0 && (n = in.read(buffer, 0, (int) Math.min(buffer.length, left))) > 0) {
            out.write(buffer, 0, n);
            left -= n;
        }
    }
}
