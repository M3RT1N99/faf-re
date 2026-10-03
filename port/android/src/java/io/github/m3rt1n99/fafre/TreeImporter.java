package io.github.m3rt1n99.fafre;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * Copies a manifest selection (scfa.entries or user.entries) from a picked
 * folder tree into the data root, with the semantics port/data/gamedata.json
 * defines and scripts/port/deploy_android.ps1 implements for the PC route:
 *
 * <ul>
 *   <li>{@code src} is resolved below the picked folder one component at a
 *       time, case-insensitively;</li>
 *   <li>names directly inside it are selected by include/exclude patterns,
 *       {@code kind=file} takes regular files, {@code kind=dir} takes folders
 *       and copies them recursively;</li>
 *   <li>the destination is {@code <prefix>/<src>/<name>} with the source's
 *       spelling (prefix "scfa" or "vault").</li>
 * </ul>
 *
 * Files whose destination already has the source size are skipped, so an
 * interrupted import continues where it stopped. Everything is written as
 * "&lt;name&gt;.part" and renamed when complete.
 *
 * <p>Pure Java (no Android APIs) so it can be exercised on a host JVM.
 */
final class TreeImporter {
    /** Deep enough for any map or mod; stops a provider that reports a cycle. */
    static final int MAX_DEPTH = 16;

    static final class Item {
        final SourceTree.Node node;
        final String dest;
        final long size;
        final String entryId;

        Item(SourceTree.Node node, String dest, long size, String entryId) {
            this.node = node;
            this.dest = dest;
            this.size = size;
            this.entryId = entryId;
        }
    }

    static final class Plan {
        final List<Item> items = new ArrayList<>();
        final List<String> warnings = new ArrayList<>();

        /** Sum of the sizes the provider reported (unknown sizes count as 0). */
        long knownBytes() {
            long total = 0;
            for (Item item : items) {
                total += Math.max(0, item.size);
            }
            return total;
        }
    }

    static final class Result {
        int copied;
        int skipped;
        long copiedBytes;
        final List<String> warnings = new ArrayList<>();
    }

    private final SourceTree mTree;
    private final DataRoot mRoot;
    private final LogSink mLog;
    private final Progress mProgress;
    private final Cancellation mCancel;
    private final Map<String, List<SourceTree.Node>> mListings = new HashMap<>();

    TreeImporter(SourceTree tree, DataRoot root, LogSink log, Progress progress, Cancellation cancel) {
        mTree = tree;
        mRoot = root;
        mLog = log;
        mProgress = progress;
        mCancel = cancel;
    }

    /**
     * The folder that holds {@code marker} ("gamedata/textures.scd", or the
     * directory "maps" with {@code directory} set): the picked folder itself,
     * or one of its direct subfolders for users who pick the parent
     * ("steamapps/common"). Null if neither has it.
     */
    SourceTree.Node findBase(String marker, boolean directory) throws IOException {
        SourceTree.Node root = mTree.root();
        if (!root.directory) {
            return null;
        }
        if (resolve(root, marker, directory) != null) {
            return root;
        }
        for (SourceTree.Node child : list(root)) {
            mCancel.throwIfCancelled();
            if (child.directory && resolve(child, marker, directory) != null) {
                return child;
            }
        }
        return null;
    }

    /** {@code relative} below {@code base}, each component matched case-insensitively; null if missing. */
    SourceTree.Node resolve(SourceTree.Node base, String relative, boolean directory) throws IOException {
        List<SourceTree.Node> path = resolvePath(base, relative);
        if (path == null) {
            return null;
        }
        SourceTree.Node last = path.get(path.size() - 1);
        return last.directory == directory ? last : null;
    }

    private List<SourceTree.Node> resolvePath(SourceTree.Node base, String relative) throws IOException {
        String[] parts = Names.splitRelative(relative);
        if (parts == null) {
            throw new IOException("unsafe path in the manifest: " + relative);
        }
        List<SourceTree.Node> path = new ArrayList<>();
        SourceTree.Node current = base;
        for (int i = 0; i < parts.length; ++i) {
            if (!current.directory) {
                return null;
            }
            current = child(current, parts[i]);
            if (current == null) {
                return null;
            }
            path.add(current);
        }
        return path;
    }

    private SourceTree.Node child(SourceTree.Node directory, String name) throws IOException {
        SourceTree.Node match = null;
        for (SourceTree.Node node : list(directory)) {
            if (node.name.equals(name)) {
                return node;
            }
            if (Names.equalsIgnoreCaseAscii(node.name, name)
                    && (match == null || node.name.compareTo(match.name) < 0)) {
                match = node;
            }
        }
        return match;
    }

    /** Listings are cached for the job: several entries look into the same folders (gamedata, sounds). */
    private List<SourceTree.Node> list(SourceTree.Node directory) throws IOException {
        List<SourceTree.Node> nodes = mListings.get(directory.id);
        if (nodes == null) {
            nodes = Collections.unmodifiableList(new ArrayList<>(mTree.list(directory)));
            mListings.put(directory.id, nodes);
        }
        return nodes;
    }

    /** Expands {@code entries} below {@code base} into the files to copy. */
    Plan plan(SourceTree.Node base, List<DataManifest.Entry> entries, String destPrefix) throws IOException {
        Plan plan = new Plan();
        Set<String> seen = new HashSet<>();
        for (DataManifest.Entry entry : entries) {
            mCancel.throwIfCancelled();
            List<SourceTree.Node> srcPath = resolvePath(base, entry.src);
            if (srcPath == null || !srcPath.get(srcPath.size() - 1).directory) {
                plan.warnings.add(entry.label + ": folder '" + entry.src + "' not found");
                continue;
            }
            StringBuilder destDir = new StringBuilder(destPrefix);
            for (SourceTree.Node node : srcPath) {
                destDir.append('/').append(node.name);
            }
            SourceTree.Node srcDir = srcPath.get(srcPath.size() - 1);
            Set<String> found = new HashSet<>();
            for (SourceTree.Node child : list(srcDir)) {
                if (!entry.matches(child.name, child.directory)) {
                    continue;
                }
                if (!Names.isSafeName(child.name)) {
                    plan.warnings.add(entry.label + ": skipped unsafe name '" + child.name + "'");
                    continue;
                }
                found.add(Names.toLowerAscii(child.name));
                String dest = destDir + "/" + child.name;
                if (child.directory) {
                    addTree(plan, seen, child, dest, entry.id, 1);
                } else {
                    add(plan, seen, new Item(child, dest, child.size, entry.id));
                }
            }
            for (String name : entry.literalNames()) {
                if (!found.contains(Names.toLowerAscii(name))) {
                    plan.warnings.add(entry.label + ": '" + entry.src + "/" + name + "' not found");
                }
            }
        }
        return plan;
    }

    private void addTree(Plan plan, Set<String> seen, SourceTree.Node directory, String dest, String entryId,
            int depth) throws IOException {
        mCancel.throwIfCancelled();
        if (depth > MAX_DEPTH) {
            plan.warnings.add("skipped '" + dest + "': folders nested too deep");
            return;
        }
        for (SourceTree.Node child : list(directory)) {
            if (!Names.isSafeName(child.name)) {
                plan.warnings.add("skipped unsafe name '" + child.name + "' in " + dest);
                continue;
            }
            String childDest = dest + "/" + child.name;
            if (child.directory) {
                addTree(plan, seen, child, childDest, entryId, depth + 1);
            } else {
                add(plan, seen, new Item(child, childDest, child.size, entryId));
            }
        }
    }

    private static void add(Plan plan, Set<String> seen, Item item) {
        if (seen.add(Names.toLowerAscii(item.dest))) {
            plan.items.add(item);
        }
    }

    /** Bytes that still have to be written: items whose destination is missing or has another size. */
    long bytesToCopy(Plan plan) throws IOException {
        long total = 0;
        for (Item item : plan.items) {
            if (!isPresent(item)) {
                total += Math.max(0, item.size);
            }
        }
        return total;
    }

    private boolean isPresent(Item item) throws IOException {
        File existing = mRoot.find(item.dest);
        return existing != null && existing.isFile() && item.size >= 0 && existing.length() == item.size;
    }

    /**
     * Copies the plan. Every file that ends up in place is added to
     * {@code record}, also when the job is cancelled or fails halfway, so the
     * caller can save what was done.
     */
    Result copy(Plan plan, DeployRecord record) throws IOException {
        Result result = new Result();
        mProgress.phase("Copying", plan.items.size(), plan.knownBytes());
        byte[] buffer = new byte[FileOps.BUFFER_SIZE];
        for (int i = 0; i < plan.items.size(); ++i) {
            mCancel.throwIfCancelled();
            Item item = plan.items.get(i);
            mProgress.item(item.dest, i);
            File dest = mRoot.prepare(item.dest);
            if (dest.isDirectory()) {
                result.warnings.add("skipped '" + item.dest + "': a folder with that name exists");
                continue;
            }
            if (dest.isFile() && item.size >= 0 && dest.length() == item.size) {
                ++result.skipped;
                record.put(item.dest, item.size);
                mProgress.addBytes(item.size);
                continue;
            }
            long written = copyOne(item, dest, buffer);
            ++result.copied;
            result.copiedBytes += written;
            record.put(item.dest, written);
        }
        mProgress.item("", plan.items.size());
        mLog.log("copied " + result.copied + " files (" + FileOps.formatBytes(result.copiedBytes) + "), "
                + result.skipped + " already present");
        return result;
    }

    private long copyOne(Item item, File dest, byte[] buffer) throws IOException {
        File part = FileOps.partFile(dest);
        mRoot.checkInside(part);
        long written = 0;
        boolean done = false;
        try {
            try (InputStream in = mTree.open(item.node); OutputStream out = new FileOutputStream(part)) {
                int n;
                while ((n = in.read(buffer)) > 0) {
                    mCancel.throwIfCancelled();
                    out.write(buffer, 0, n);
                    written += n;
                    mProgress.addBytes(n);
                }
            }
            if (item.size >= 0 && written != item.size) {
                throw new IOException(item.dest + ": read " + written + " of " + item.size
                        + " bytes (was the source changed or removed?)");
            }
            FileOps.moveReplacing(part, dest);
            done = true;
            return written;
        } finally {
            if (!done) {
                FileOps.deleteQuietly(part);
            }
        }
    }
}
