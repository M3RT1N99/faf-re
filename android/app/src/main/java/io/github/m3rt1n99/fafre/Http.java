package io.github.m3rt1n99.fafre;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.UnsupportedEncodingException;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

/**
 * Downloads from FAF's servers as tools/parity-data does, and asks GitHub which game version is
 * FAF's newest (FafRelease): one request at a time, at least a second apart, with a User-Agent
 * that names the project. A large file goes to
 * {@code <name>.part} and resumes with an HTTP Range request after a dropped connection or a
 * cancel. A failure on the phone's side, such as a full disk, ends a download at once: another
 * try would fail the same way.
 */
final class Http {
    /** FAF's game files by version (the FAF client's updater), replays, maps and mods. */
    static final String GAME_URL = "https://content.faforever.com/faf/updaterNew/updates_faf_files/";
    static final String REPLAY_URL = "https://replay.faforever.com/";
    static final String MAP_URL = "https://content.faforever.com/maps/";
    static final String MOD_URL = "https://content.faforever.com/mods/";

    private static final long REQUEST_GAP_MS = 1000;
    /** About ten minutes in all: a phone left running overnight can lose its network for a while. */
    private static final int[] RETRY_DELAYS_MS = {2_000, 5_000, 15_000, 30_000, 60_000, 120_000, 300_000};
    private static long sLastRequest;

    /** A status other than 200/206; 404 means the file is not there. */
    static final class StatusException extends IOException {
        final int status;

        StatusException(String url, int status) {
            super("HTTP " + status + " for " + url);
            this.status = status;
        }
    }

    /** A failure on the phone's side of a download, such as a full disk: no new try of the network mends it. */
    static final class StorageException extends IOException {
        StorageException(String message) {
            super(message);
        }

        StorageException(String message, IOException cause) {
            super(message, cause);
        }
    }

    interface Progress {
        void bytes(long done, long total);
    }

    /**
     * What a check asks of GitHub and FAF's updater while the user waits for its answer: one try each, so that a
     * failure shows at once rather than after minutes of retries. The tests answer instead of the network.
     */
    interface Requests {
        /** A small text, whole. */
        String text(String url) throws IOException;

        /** A file's length by a HEAD request, -1 when the server does not say; a StatusException with 404 when it is not there. */
        long length(String url) throws IOException;
    }

    /** The network's answers; the user's Cancel before or during a request makes it a Cancelled. */
    static final Requests ONE_TRY = new Requests() {
        @Override
        public String text(String url) throws IOException {
            return unlessCancelled(() -> {
                HttpURLConnection connection = open(url, "GET", 0);
                try (InputStream in = connection.getInputStream()) {
                    ByteArrayOutputStream out = new ByteArrayOutputStream();
                    copy(in, out, null, 0, -1);
                    return utf8(out.toByteArray());
                } finally {
                    connection.disconnect();
                }
            });
        }

        @Override
        public long length(String url) throws IOException {
            return unlessCancelled(() -> {
                HttpURLConnection connection = open(url, "HEAD", 0);
                try {
                    return connection.getContentLengthLong();
                } finally {
                    connection.disconnect();
                }
            });
        }
    };

    /**
     * What a download gets from FAF's servers: each file tried again after a failure of the network, which a phone
     * left running overnight can lose for a while. The tests answer instead of the network.
     */
    interface Downloads {
        /** A small file, whole, in memory. */
        byte[] bytes(String url) throws IOException;

        /** {@code url} into {@code dest}, through {@code dest.part}, resuming what an earlier try left. */
        void download(String url, File dest, Progress progress) throws IOException;
    }

    /** The network's answers, tried again. */
    static final Downloads WITH_RETRIES = new Downloads() {
        @Override
        public byte[] bytes(String url) throws IOException {
            return Http.bytes(url);
        }

        @Override
        public void download(String url, File dest, Progress progress) throws IOException {
            Http.download(url, dest, progress);
        }
    };

    private interface Request<T> {
        T answer() throws IOException;
    }

    /**
     * A check's request: a Cancelled when the user's Cancel came before it or while it ran, whether it then failed (a
     * stalled request at its timeout) or got an answer that nothing wants any more.
     */
    private static <T> T unlessCancelled(Request<T> request) throws IOException {
        Cancellation.check();
        T answer;
        try {
            answer = request.answer();
        } catch (IOException e) {
            Cancellation.check();
            throw e;
        }
        Cancellation.check();
        return answer;
    }

    private Http() {
    }

    static String userAgent() {
        return "faf-re-android/" + BuildConfig.VERSION_NAME + " (+https://github.com/M3RT1N99/faf-re)";
    }

    /** {@code text} percent-encoded for a URL path segment, as Python's urllib.parse.quote does. */
    static String quote(String text) {
        StringBuilder out = new StringBuilder();
        for (byte b : text.getBytes(StandardCharsets.UTF_8)) {
            int c = b & 0xFF;
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || "-._~/".indexOf(c) >= 0) {
                out.append((char) c);
            } else {
                out.append(String.format("%%%02X", c));
            }
        }
        return out.toString();
    }

    private static synchronized void pace() throws IOException {
        long wait = sLastRequest + REQUEST_GAP_MS - System.currentTimeMillis();
        if (wait > 0) {
            try {
                Thread.sleep(wait);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                throw new IOException("interrupted");
            }
        }
        sLastRequest = System.currentTimeMillis();
    }

    private static HttpURLConnection open(String url, String method, long from) throws IOException {
        pace();
        HttpURLConnection connection = (HttpURLConnection) new URL(url).openConnection();
        connection.setRequestMethod(method);
        connection.setConnectTimeout(20_000);
        connection.setReadTimeout(60_000);
        connection.setRequestProperty("User-Agent", userAgent());
        if (from > 0) {
            connection.setRequestProperty("Range", "bytes=" + from + "-");
        }
        int status = connection.getResponseCode();
        if (status != 200 && status != 206) {
            connection.disconnect();
            throw new StatusException(url, status);
        }
        return connection;
    }

    /** A small file, whole, in memory. */
    static byte[] bytes(String url) throws IOException {
        IOException last = null;
        for (int attempt = 0; attempt <= RETRY_DELAYS_MS.length; ++attempt) {
            if (attempt > 0) {
                retryAfter(attempt, url, last);
            }
            HttpURLConnection connection = null;
            try {
                connection = open(url, "GET", 0);
                try (InputStream in = connection.getInputStream()) {
                    ByteArrayOutputStream out = new ByteArrayOutputStream();
                    copy(in, out, null, 0, -1);
                    return out.toByteArray();
                }
            } catch (StatusException e) {
                if (e.status == 404 || e.status == 403) {
                    throw e;
                }
                last = e;
            } catch (IOException e) {
                last = e;
            } finally {
                if (connection != null) {
                    connection.disconnect();
                }
            }
            Cancellation.check();
        }
        throw last;
    }

    /** {@code url} into {@code dest}, through {@code dest.part}, resuming what an earlier try left. */
    static void download(String url, File dest, Progress progress) throws IOException {
        File part = new File(dest.getPath() + ".part");
        IOException last = null;
        for (int attempt = 0; attempt <= RETRY_DELAYS_MS.length; ++attempt) {
            if (attempt > 0) {
                retryAfter(attempt, url, last);
            }
            long have = part.isFile() ? part.length() : 0;
            HttpURLConnection connection = null;
            try {
                connection = open(url, "GET", have);
                boolean resumed = connection.getResponseCode() == 206;
                if (!resumed) {
                    have = 0;
                }
                long length = connection.getContentLengthLong();
                long total = length < 0 ? -1 : have + length;
                try (InputStream in = connection.getInputStream(); OutputStream out = new PartFile(part, resumed)) {
                    copy(in, out, progress, have, total);
                }
                if (total >= 0 && part.length() != total) {
                    throw new IOException("short download of " + url + ": " + part.length() + " of " + total + " bytes");
                }
                if (dest.exists() && !dest.delete()) {
                    throw new StorageException("cannot replace " + dest);
                }
                if (!part.renameTo(dest)) {
                    throw new StorageException("cannot rename " + part + " to " + dest.getName());
                }
                return;
            } catch (StatusException e) {
                if (e.status == 416) {
                    // The part is longer than the file (it changed on the server): start over.
                    part.delete();
                    last = e;
                } else if (e.status == 404 || e.status == 403) {
                    throw e;
                } else {
                    last = e;
                }
            } catch (StorageException e) {
                // Not the network's: another try would fail the same way.
                throw e;
            } catch (IOException e) {
                Cancellation.check();
                last = e;
            } finally {
                if (connection != null) {
                    connection.disconnect();
                }
            }
            Cancellation.check();
        }
        throw last;
    }

    /** A download's .part, whose failures (opening, writing, closing it) are the phone's, not the network's. */
    private static final class PartFile extends OutputStream {
        private final File mFile;
        private final OutputStream mOut;

        PartFile(File file, boolean append) throws StorageException {
            mFile = file;
            try {
                mOut = new FileOutputStream(file, append);
            } catch (IOException e) {
                throw failed(e);
            }
        }

        @Override
        public void write(int b) throws IOException {
            write(new byte[] {(byte) b}, 0, 1);
        }

        @Override
        public void write(byte[] bytes, int offset, int length) throws IOException {
            try {
                mOut.write(bytes, offset, length);
            } catch (IOException e) {
                throw failed(e);
            }
        }

        @Override
        public void close() throws IOException {
            try {
                mOut.close();
            } catch (IOException e) {
                throw failed(e);
            }
        }

        private StorageException failed(IOException e) {
            return new StorageException("the phone cannot write " + mFile.getName() + " (" + e.getMessage() + ")", e);
        }
    }

    private static void copy(InputStream in, OutputStream out, Progress progress, long done, long total) throws IOException {
        byte[] buffer = new byte[1 << 16];
        long reported = 0;
        int read;
        while ((read = in.read(buffer)) > 0) {
            out.write(buffer, 0, read);
            done += read;
            if (progress != null && done - reported >= (1 << 20)) {
                progress.bytes(done, total);
                reported = done;
            }
            Cancellation.check();
        }
        if (progress != null) {
            progress.bytes(done, total);
        }
    }

    /** Waits before the next try, and says so in the log. */
    private static void retryAfter(int attempt, String url, IOException last) throws IOException {
        long ms = RETRY_DELAYS_MS[attempt - 1];
        TestState.line("Network: " + (last != null ? last.getMessage() : "error") + "; trying " + url + " again in " + ms / 1000
            + " s (" + attempt + " of " + RETRY_DELAYS_MS.length + ")");
        sleep(ms);
    }

    /** A wait between tries, up to 5 minutes, which the user's Cancel ends within half a second. */
    private static void sleep(long ms) throws IOException {
        long end = System.currentTimeMillis() + ms;
        for (long left = ms; left > 0; left = end - System.currentTimeMillis()) {
            try {
                Thread.sleep(Math.min(left, 500));
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                throw new IOException("interrupted");
            }
            Cancellation.check();
        }
    }

    static String utf8(byte[] data) {
        try {
            return new String(data, "UTF-8");
        } catch (UnsupportedEncodingException e) {
            throw new IllegalStateException(e);
        }
    }
}
