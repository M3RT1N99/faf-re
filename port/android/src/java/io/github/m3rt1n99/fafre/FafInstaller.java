package io.github.m3rt1n99.fafre;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Puts the FAF files of the manifest (init_faf.lua and the *.nx2 archives)
 * into {@code <root>/faf}, either downloaded from FAF's content server or
 * copied from a FAF client folder the user picked, and checks them.
 *
 * <p>Every file is verified against the manifest's size and SHA-256 before it
 * gets its final name: the runtime mounts whatever is there, and a truncated
 * or foreign-version archive would fail in confusing ways much later.
 * Downloads go to "&lt;name&gt;.part" and resume with an HTTP Range request
 * after a dropped connection, a cancel, or the app being killed, which matters
 * for env.nx2 (500 MB) on a phone connection.
 *
 * <p>Pure Java (no Android APIs) so it can be exercised on a host JVM.
 */
final class FafInstaller {
    static final int CONNECT_TIMEOUT_MS = 20_000;
    static final int READ_TIMEOUT_MS = 30_000;
    private static final long[] RETRY_BACKOFF_MS = {2_000, 5_000, 15_000};
    private static final Pattern CONTENT_RANGE = Pattern.compile("bytes\\s+(\\d+)-(\\d+)/(\\d+|\\*)");

    static final class Result {
        int installed;
        int present;
        final List<String> missing = new ArrayList<>();
        final List<String> failed = new ArrayList<>();

        boolean ok() {
            return missing.isEmpty() && failed.isEmpty();
        }

        String summary() {
            StringBuilder out = new StringBuilder();
            out.append(installed).append(" installed, ").append(present).append(" already present");
            if (!missing.isEmpty()) {
                out.append("; missing: ").append(String.join(", ", missing));
            }
            if (!failed.isEmpty()) {
                out.append("; failed: ").append(String.join("; ", failed));
            }
            return out.toString();
        }
    }

    /** The server sent bytes that cannot belong to the file; the .part is discarded instead of resumed. */
    private static final class BadDataException extends IOException {
        private static final long serialVersionUID = 1L;

        BadDataException(String message) {
            super(message);
        }
    }

    private final DataRoot mRoot;
    private final LogSink mLog;
    private final Progress mProgress;
    private final Cancellation mCancel;

    FafInstaller(DataRoot root, LogSink log, Progress progress, Cancellation cancel) {
        mRoot = root;
        mLog = log;
        mProgress = progress;
        mCancel = cancel;
    }

    /**
     * The files a download or import installs: required and optional ones
     * always (the optional ones are a few kB), the large recommended archives
     * only on request.
     */
    static List<DataManifest.FafFile> select(DataManifest manifest, boolean includeRecommended) {
        List<DataManifest.FafFile> files = new ArrayList<>();
        for (DataManifest.FafFile file : manifest.fafFiles) {
            if (file.tier != DataManifest.Tier.RECOMMENDED || includeRecommended) {
                files.add(file);
            }
        }
        return files;
    }

    // ------------------------------------------------------------------ check

    /**
     * Hashes the files that already have the expected size; returns the ones
     * that are missing or differ. Matching files are counted in
     * {@code result.present}.
     */
    List<DataManifest.FafFile> checkExisting(List<DataManifest.FafFile> files, Result result) throws IOException {
        List<DataManifest.FafFile> candidates = new ArrayList<>();
        List<DataManifest.FafFile> needed = new ArrayList<>();
        long total = 0;
        for (DataManifest.FafFile file : files) {
            File existing = mRoot.find(file.dest);
            if (existing != null && existing.isFile() && existing.length() == file.size) {
                candidates.add(file);
                total += file.size;
            } else {
                needed.add(file);
            }
        }
        mProgress.phase("Checking existing files", candidates.size(), total);
        for (int i = 0; i < candidates.size(); ++i) {
            DataManifest.FafFile file = candidates.get(i);
            mProgress.item(file.name, i);
            String hash = FileOps.sha256(mRoot.find(file.dest), mCancel, mProgress);
            if (hash.equals(file.sha256)) {
                ++result.present;
            } else {
                mLog.log(file.dest + ": checksum differs from FAF " + file.name + ", replacing it");
                needed.add(file);
            }
        }
        return needed;
    }

    /** Checks every present file against its checksum without changing anything. */
    Result verify(List<DataManifest.FafFile> files) throws IOException {
        Result result = new Result();
        long total = 0;
        List<DataManifest.FafFile> present = new ArrayList<>();
        for (DataManifest.FafFile file : files) {
            File existing = mRoot.find(file.dest);
            if (existing == null || !existing.isFile()) {
                result.missing.add(file.name);
            } else if (existing.length() != file.size) {
                result.failed.add(file.name + " has " + existing.length() + " bytes, expected " + file.size);
            } else {
                present.add(file);
                total += file.size;
            }
        }
        mProgress.phase("Verifying checksums", present.size(), total);
        for (int i = 0; i < present.size(); ++i) {
            DataManifest.FafFile file = present.get(i);
            mProgress.item(file.name, i);
            String hash = FileOps.sha256(mRoot.find(file.dest), mCancel, mProgress);
            if (hash.equals(file.sha256)) {
                ++result.present;
            } else {
                result.failed.add(file.name + " checksum mismatch");
            }
        }
        mLog.log("verify: " + result.summary());
        return result;
    }

    // --------------------------------------------------------------- download

    Result download(List<DataManifest.FafFile> files, String baseUrl, int version, String userAgent)
            throws IOException {
        Result result = new Result();
        List<DataManifest.FafFile> needed = checkExisting(files, result);
        long total = 0;
        for (DataManifest.FafFile file : needed) {
            total += file.size;
        }
        mProgress.phase("Downloading", needed.size(), total);
        long completed = 0;
        for (int i = 0; i < needed.size(); ++i) {
            mCancel.throwIfCancelled();
            DataManifest.FafFile file = needed.get(i);
            mProgress.item(file.name, i);
            URL url = new URL(baseUrl + "/" + file.remoteName(version));
            try {
                fetch(file, url, userAgent, completed);
                ++result.installed;
                mLog.log("downloaded " + file.dest + " (" + FileOps.formatBytes(file.size) + ")");
            } catch (Cancellation.CancelledException e) {
                throw e;
            } catch (IOException e) {
                // Keep going: the other files may still work, and the .part of
                // this one is kept for the next attempt.
                result.failed.add(file.name + ": " + e.getMessage());
                mLog.log("download of " + file.name + " failed: " + e.getMessage());
            }
            completed += file.size;
            mProgress.bytes(completed);
        }
        mProgress.item("", needed.size());
        return result;
    }

    private void fetch(DataManifest.FafFile file, URL url, String userAgent, long completed) throws IOException {
        File dest = mRoot.prepare(file.dest);
        File part = FileOps.partFile(dest);
        mRoot.checkInside(part);
        int badData = 0;
        IOException last = null;
        for (int attempt = 0; attempt <= RETRY_BACKOFF_MS.length; ++attempt) {
            mCancel.throwIfCancelled();
            if (attempt > 0) {
                mCancel.sleep(RETRY_BACKOFF_MS[attempt - 1]);
            }
            try {
                String hash = downloadToPart(file, url, userAgent, part, completed);
                if (hash.equals(file.sha256)) {
                    FileOps.moveReplacing(part, dest);
                    return;
                }
                throw new BadDataException("checksum mismatch (got " + hash + ")");
            } catch (Cancellation.CancelledException e) {
                throw e;
            } catch (BadDataException e) {
                FileOps.deleteQuietly(part);
                last = e;
                mLog.log(file.name + ": " + e.getMessage() + "; starting over");
                // Twice in a row means the server has a different file, not a hiccup.
                if (++badData >= 2) {
                    break;
                }
            } catch (IOException e) {
                if (mCancel.isCancelled()) {
                    throw new Cancellation.CancelledException(mCancel.reason());
                }
                last = e;
                mLog.log(file.name + ": attempt " + (attempt + 1) + " failed: " + e);
            }
        }
        throw last != null ? last : new IOException("download failed");
    }

    /** One HTTP request; returns the SHA-256 of the complete .part. */
    private String downloadToPart(DataManifest.FafFile file, URL url, String userAgent, File part, long completed)
            throws IOException {
        long offset = part.isFile() ? part.length() : 0;
        if (offset > file.size) {
            FileOps.deleteQuietly(part);
            offset = 0;
        }
        MessageDigest digest = FileOps.sha256();
        if (offset > 0) {
            // Hash the bytes we keep so the checksum still covers the whole file.
            FileOps.digest(part, offset, digest, mCancel, null);
        }
        mProgress.bytes(completed + offset);
        if (offset == file.size) {
            return FileOps.hex(digest.digest());
        }

        final HttpURLConnection connection = (HttpURLConnection) url.openConnection();
        Cancellation.Registration abort = mCancel.onCancel(connection::disconnect);
        try {
            connection.setConnectTimeout(CONNECT_TIMEOUT_MS);
            connection.setReadTimeout(READ_TIMEOUT_MS);
            connection.setRequestProperty("User-Agent", userAgent);
            // Android's HttpURLConnection would otherwise ask for gzip and
            // decompress transparently, which breaks Content-Length and Range.
            connection.setRequestProperty("Accept-Encoding", "identity");
            if (offset > 0) {
                connection.setRequestProperty("Range", "bytes=" + offset + "-");
            }
            int code = connection.getResponseCode();
            if (code == HttpURLConnection.HTTP_PARTIAL) {
                long start = contentRangeStart(connection.getHeaderField("Content-Range"), file.size);
                if (start != offset) {
                    throw new BadDataException("server resumed at byte " + start + " instead of " + offset);
                }
            } else if (code == HttpURLConnection.HTTP_OK) {
                if (offset > 0) {
                    mLog.log(file.name + ": server ignored the Range request; downloading from the start");
                    offset = 0;
                    digest.reset();
                    mProgress.bytes(completed);
                }
            } else if (code == 416) {
                throw new BadDataException("server rejected resuming at byte " + offset);
            } else {
                throw new IOException("HTTP " + code + " " + connection.getResponseMessage() + " for " + url);
            }
            long length = connection.getContentLengthLong();
            if (length >= 0 && offset + length != file.size) {
                throw new BadDataException("server sends " + (offset + length) + " bytes, the manifest expects "
                        + file.size);
            }

            byte[] buffer = new byte[FileOps.BUFFER_SIZE];
            long written = offset;
            try (InputStream in = connection.getInputStream();
                    OutputStream out = new FileOutputStream(part, offset > 0)) {
                int n;
                while ((n = in.read(buffer)) > 0) {
                    mCancel.throwIfCancelled();
                    if (written + n > file.size) {
                        throw new BadDataException("server sent more than " + file.size + " bytes");
                    }
                    out.write(buffer, 0, n);
                    digest.update(buffer, 0, n);
                    written += n;
                    mProgress.addBytes(n);
                }
            }
            if (written != file.size) {
                throw new IOException("connection ended after " + written + " of " + file.size + " bytes");
            }
            return FileOps.hex(digest.digest());
        } finally {
            abort.close();
            connection.disconnect();
        }
    }

    /** Start offset from "bytes 100-199/200"; -1 if the header is missing or does not fit the file. */
    static long contentRangeStart(String header, long size) {
        if (header == null) {
            return -1;
        }
        Matcher matcher = CONTENT_RANGE.matcher(header.trim());
        if (!matcher.matches()) {
            return -1;
        }
        long start = Long.parseLong(matcher.group(1));
        long end = Long.parseLong(matcher.group(2));
        String total = matcher.group(3);
        if (end != size - 1 || (!total.equals("*") && Long.parseLong(total) != size)) {
            return -1;
        }
        return start;
    }

    // ----------------------------------------------------------------- import

    /** Copies the files from a FAF client folder (C:/ProgramData/FAForever layout) picked by the user. */
    Result importFrom(SourceTree tree, SourceTree.Node base, TreeImporter resolver, List<DataManifest.FafFile> files,
            int version) throws IOException {
        Result result = new Result();
        List<DataManifest.FafFile> needed = checkExisting(files, result);
        List<DataManifest.FafFile> copies = new ArrayList<>();
        List<SourceTree.Node> sources = new ArrayList<>();
        long total = 0;
        for (DataManifest.FafFile file : needed) {
            SourceTree.Node source = resolver.resolve(base, file.local, false);
            if (source == null) {
                if (file.tier == DataManifest.Tier.OPTIONAL) {
                    mLog.log("optional " + file.local + " not in the picked folder");
                } else {
                    result.missing.add(file.local);
                }
                continue;
            }
            copies.add(file);
            sources.add(source);
            total += file.size;
        }
        mProgress.phase("Copying", copies.size(), total);
        byte[] buffer = new byte[FileOps.BUFFER_SIZE];
        for (int i = 0; i < copies.size(); ++i) {
            mCancel.throwIfCancelled();
            DataManifest.FafFile file = copies.get(i);
            mProgress.item(file.name, i);
            try {
                copyVerified(tree, sources.get(i), file, version, buffer);
                ++result.installed;
            } catch (Cancellation.CancelledException e) {
                throw e;
            } catch (IOException e) {
                result.failed.add(file.name + ": " + e.getMessage());
                mLog.log("import of " + file.name + " failed: " + e.getMessage());
            }
        }
        mProgress.item("", copies.size());
        return result;
    }

    private void copyVerified(SourceTree tree, SourceTree.Node source, DataManifest.FafFile file, int version,
            byte[] buffer) throws IOException {
        File dest = mRoot.prepare(file.dest);
        File part = FileOps.partFile(dest);
        mRoot.checkInside(part);
        MessageDigest digest = FileOps.sha256();
        long written = 0;
        boolean done = false;
        try {
            try (InputStream in = tree.open(source); OutputStream out = new FileOutputStream(part)) {
                int n;
                while ((n = in.read(buffer)) > 0) {
                    mCancel.throwIfCancelled();
                    if (written + n > file.size) {
                        throw new IOException("larger than the FAF " + version + " file; is the FAF client "
                                + "on another version?");
                    }
                    out.write(buffer, 0, n);
                    digest.update(buffer, 0, n);
                    written += n;
                    mProgress.addBytes(n);
                }
            }
            if (written != file.size || !FileOps.hex(digest.digest()).equals(file.sha256)) {
                throw new IOException("not the FAF " + version + " file (size or checksum differs); update the "
                        + "FAF client or use the download");
            }
            FileOps.moveReplacing(part, dest);
            done = true;
        } finally {
            if (!done) {
                FileOps.deleteQuietly(part);
            }
        }
    }
}
