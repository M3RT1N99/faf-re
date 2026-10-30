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
 * Downloads from FAF's servers as tools/parity-data does: one request at a time, at least a
 * second apart, with a User-Agent that names the project. A large file goes to
 * {@code <name>.part} and resumes with an HTTP Range request after a dropped connection or a
 * cancel.
 */
final class Http {
    /** FAF's game files by version (the FAF client's updater), replays, maps and mods. */
    static final String GAME_URL = "https://content.faforever.com/faf/updaterNew/updates_faf_files/";
    static final String REPLAY_URL = "https://replay.faforever.com/";
    static final String MAP_URL = "https://content.faforever.com/maps/";
    static final String MOD_URL = "https://content.faforever.com/mods/";

    private static final long REQUEST_GAP_MS = 1000;
    private static final int[] RETRY_DELAYS_MS = {2_000, 5_000, 15_000};
    private static long sLastRequest;

    /** A status other than 200/206; 404 means the file is not there. */
    static final class StatusException extends IOException {
        final int status;

        StatusException(String url, int status) {
            super("HTTP " + status + " for " + url);
            this.status = status;
        }
    }

    interface Progress {
        void bytes(long done, long total);
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

    private static HttpURLConnection open(String url, long from) throws IOException {
        pace();
        HttpURLConnection connection = (HttpURLConnection) new URL(url).openConnection();
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
                sleep(RETRY_DELAYS_MS[attempt - 1]);
            }
            HttpURLConnection connection = null;
            try {
                connection = open(url, 0);
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
                sleep(RETRY_DELAYS_MS[attempt - 1]);
            }
            long have = part.isFile() ? part.length() : 0;
            HttpURLConnection connection = null;
            try {
                connection = open(url, have);
                boolean resumed = connection.getResponseCode() == 206;
                if (!resumed) {
                    have = 0;
                }
                long length = connection.getContentLengthLong();
                long total = length < 0 ? -1 : have + length;
                try (InputStream in = connection.getInputStream(); OutputStream out = new FileOutputStream(part, resumed)) {
                    copy(in, out, progress, have, total);
                }
                if (total >= 0 && part.length() != total) {
                    throw new IOException("short download of " + url + ": " + part.length() + " of " + total + " bytes");
                }
                if (dest.exists() && !dest.delete()) {
                    throw new IOException("cannot replace " + dest);
                }
                if (!part.renameTo(dest)) {
                    throw new IOException("cannot rename " + part + " to " + dest.getName());
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

    private static void sleep(long ms) throws IOException {
        try {
            Thread.sleep(ms);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new IOException("interrupted");
        }
        Cancellation.check();
    }

    static String utf8(byte[] data) {
        try {
            return new String(data, "UTF-8");
        } catch (UnsupportedEncodingException e) {
            throw new IllegalStateException(e);
        }
    }
}
