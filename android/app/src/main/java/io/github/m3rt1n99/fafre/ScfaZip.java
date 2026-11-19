package io.github.m3rt1n99.fafre;

import java.io.BufferedInputStream;
import java.io.EOFException;
import java.io.File;
import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.function.LongSupplier;
import java.util.zip.ZipEntry;
import java.util.zip.ZipException;
import java.util.zip.ZipInputStream;

/**
 * The SCFA import from a ZIP of the Supreme Commander: Forged Alliance folder (SetupActivity's "ZIP file"), which takes
 * what the folder import takes, by its rules (ScfaImport: target, imported, copy). The ZIP comes as a stream, a content
 * URI, read by ZipInputStream (ZIP64, stored or deflated): a stream cannot reach the list at the ZIP's end first, and an
 * entry's local header need not hold its size (a ZIP written as a stream has it after the data). So the import reads the
 * ZIP twice. First its list: that it is a ZIP at all, by its first bytes, whatever its name or type say; each entry's
 * path, refused where it would leave the ZIP's folder; the SCFA folder's place in it, the ZIP's top or one folder there,
 * where its gamedata folder is; what the import takes; and whether that fits on the phone, which it says before it writes
 * anything. Then the files, into a folder of scfa/'s own with the room checked again as they go, which leave it for their
 * places in scfa/ once the whole ZIP has been read: a refusal, the user's Cancel or a failure leaves scfa/ as it was. No
 * Android in here, for the tests.
 */
final class ScfaZip {
    /** In scfa/, the files of the import that runs until it ends; what one left whose process ended goes at the next. */
    static final String STAGING = ".zip-import";
    /** Far more entries than an SCFA folder has files (1,826 on Steam): the list stays in memory. */
    static final int MOST_ENTRIES = 100_000;
    /** How often, in bytes written, the import checks the phone's room again within a file. */
    static final long CHECK_ROOM_EVERY = 64L << 20;

    /** A ZIP's first bytes: its first entry's local header. */
    private static final byte[] LOCAL_HEADER = {'P', 'K', 3, 4};
    /** An empty ZIP's: its end record. */
    private static final byte[] END = {'P', 'K', 5, 6};

    /** Why the import refuses a ZIP, or stopped, in English for the Progress card's log. */
    static final class Refused extends IOException {
        Refused(String message) {
            super(message);
        }
    }

    /** The ZIP's bytes from their start, as often as the import asks. */
    interface Source {
        InputStream open() throws IOException;
    }

    /** One entry of the ZIP's list. */
    static final class Entry {
        /** As the ZIP names it. */
        final String name;
        /** Its folders and name (ScfaZip.path). */
        final List<String> path;
        /** A folder's own entry: a name that ends in a slash. */
        final boolean folder;
        /** Its bytes, as read. */
        final long size;

        Entry(String name, List<String> path, long size) {
            this.name = name;
            this.path = path;
            folder = name.endsWith("/") || name.endsWith("\\");
            this.size = size;
        }

        /** Its name without its folders, for the steps. */
        String shortName() {
            return last(path);
        }
    }

    /** A path's name without its folders: the steps show no path in the user's ZIP, as they go to the status a launcher reads. */
    private static String last(List<String> path) {
        return path.isEmpty() ? "" : path.get(path.size() - 1);
    }

    /** What the import takes of a ZIP's list. */
    static final class Plan {
        /** The SCFA folder's place in the ZIP: "" for its top, else the name of the folder there. */
        final String root;
        /** For each entry of the list, its place in scfa/ when the import writes it; null when it leaves it or has it already. */
        final String[] writes;
        /** The files the import takes into each folder of scfa/ (GAMEDATA, FOLDERS), also those there already. */
        final Map<String, Integer> taken;
        /** Of those, the files there already, and the ZIP's files it does not take. */
        final int there;
        final int others;
        /** The files it writes, their bytes, and the last one's entry (-1 for none). */
        final int files;
        final long bytes;
        final int last;

        Plan(String root, String[] writes, Map<String, Integer> taken, int there, int others, long bytes) {
            this.root = root;
            this.writes = writes;
            this.taken = taken;
            this.there = there;
            this.others = others;
            this.bytes = bytes;
            int files = 0;
            int last = -1;
            for (int i = 0; i < writes.length; ++i) {
                if (writes[i] != null) {
                    ++files;
                    last = i;
                }
            }
            this.files = files;
            this.last = last;
        }
    }

    private ScfaZip() {
    }

    /**
     * The import of the ZIP that {@code source} reads into {@code storage}'s scfa/, with {@code free} the bytes free where it
     * writes. The Progress card's step says where it is and its log what came of it, as the folder import's do. Refused
     * when the ZIP is not one it takes, cannot be read or does not fit.
     */
    static void run(Storage storage, Source source, LongSupplier free) throws IOException {
        Plan plan;
        try {
            List<Entry> entries = list(source);
            plan = plan(entries, storage.scfa);
            TestState.line("The ZIP has the SCFA folder " + (plan.root.isEmpty() ? "at its top" : "in " + plan.root + "/")
                + ": the import takes " + (plan.files + plan.there) + " of its files (" + plan.there + " there already) and leaves "
                + plan.others);
            if (plan.files > 0) {
                // With the spare that FAF's game files leave too, for the game's and the replay runs' own files.
                String noRoom = NewestGameFiles.noRoom(plan.bytes, free.getAsLong());
                if (noRoom != null) {
                    throw new Refused("its files need more room than the phone has: " + noRoom);
                }
                write(source, entries, plan, storage.scfa, free);
            }
        } catch (ZipException | EOFException e) {
            // A damaged or cut-off ZIP, or one Android 14's ZipInputStream refuses (ZipPathValidator: a name with "..").
            throw new Refused("the ZIP cannot be read: " + e.getMessage());
        }
        // The folder import's lines.
        TestState.line("Imported " + plan.taken.getOrDefault(ScfaImport.GAMEDATA, 0) + " archives into "
            + new File(storage.scfa, ScfaImport.GAMEDATA));
        for (String folder : ScfaImport.FOLDERS) {
            Integer files = plan.taken.get(folder);
            if (files != null) {
                TestState.line("Imported " + folder + "/ (" + files + " files)");
            }
        }
        if (ScfaImport.fonts(storage) == 0) {
            TestState.line("No fonts: the ZIP has no fonts folder with .ttf files, and the game cannot start without them");
        }
    }

    /**
     * The files of {@code plan}, from a second read of the ZIP, into the import's own folder in {@code scfa}, which they leave
     * for their places once all are whole. Its entries must be the list's, and the phone's room must hold what is still to
     * write.
     */
    private static void write(Source source, List<Entry> entries, Plan plan, File scfa, LongSupplier free) throws IOException {
        File staging = new File(scfa, STAGING);
        Storage.deleteTree(staging);
        try {
            Room room = new Room(free, plan.bytes);
            byte[] buffer = new byte[1 << 16];
            try (ZipInputStream zip = open(source)) {
                for (int i = 0; i <= plan.last; ++i) {
                    ZipEntry zipEntry = next(zip);
                    Entry entry = entries.get(i);
                    if (zipEntry == null || !zipEntry.getName().equals(entry.name)) {
                        throw changed();
                    }
                    String target = plan.writes[i];
                    if (target == null) {
                        TestState.step("Skipping " + entry.shortName());
                        if (drain(zip, buffer) != entry.size) {
                            throw changed();
                        }
                        continue;
                    }
                    TestState.step("Importing " + entry.shortName());
                    room.check();
                    Sized data = new Sized(zip, entry.size, room);
                    ScfaImport.copy(data, under(staging, target));
                    if (data.left != 0) {
                        throw changed();
                    }
                }
            }
            TestState.step("Putting the files in their places");
            move(plan, staging, scfa);
        } finally {
            Storage.deleteTree(staging);
        }
    }

    /**
     * The files written, from the import's own folder to their places in scfa/, each in the place of what was there. The
     * user's Cancel does not stop it: it renames, which takes no time.
     */
    private static void move(Plan plan, File staging, File scfa) throws IOException {
        int moved = 0;
        try {
            for (String target : plan.writes) {
                if (target != null) {
                    File dest = under(scfa, target);
                    Storage.mkdirs(dest.getParentFile());
                    ScfaImport.replace(under(staging, target), dest);
                    ++moved;
                }
            }
        } catch (IOException e) {
            throw new IOException("the import stopped as it put its files in their places, after " + moved + " of "
                + plan.files + ", which scfa/ has now: " + e.getMessage(), e);
        }
    }

    /**
     * The phone's room while the import writes: what it still has to write must fit, checked at each file and every
     * CHECK_ROOM_EVERY bytes in one, so that it stops before the storage is full where something else took the room. The
     * spare that the check before asked for takes what the file system adds of its own.
     */
    private static final class Room {
        private final LongSupplier free;
        private long left;
        private long unchecked;

        Room(LongSupplier free, long bytes) {
            this.free = free;
            left = bytes;
        }

        void check() throws Refused {
            unchecked = 0;
            long now = free.getAsLong();
            if (now < left) {
                throw new Refused("the phone's storage ran short while the import wrote: " + NewestGameFiles.size(now)
                    + " free, and " + NewestGameFiles.size(left) + " still to write");
            }
        }

        void wrote(long bytes) throws Refused {
            left -= bytes;
            unchecked += bytes;
            if (unchecked >= CHECK_ROOM_EVERY) {
                check();
            }
        }
    }

    /** An entry's data as the import writes it: no more than the list read of it, with the room checked as it goes. */
    private static final class Sized extends FilterInputStream {
        private final Room room;
        long left;

        Sized(InputStream in, long size, Room room) {
            super(in);
            left = size;
            this.room = room;
        }

        @Override
        public int read() throws IOException {
            byte[] one = new byte[1];
            return read(one, 0, 1) == -1 ? -1 : one[0] & 0xFF;
        }

        @Override
        public int read(byte[] buffer, int offset, int length) throws IOException {
            int read = super.read(buffer, offset, length);
            if (read > 0) {
                left -= read;
                if (left < 0) {
                    throw changed();
                }
                room.wrote(read);
            }
            return read;
        }
    }

    /** The ZIP's list, each entry read through for its size and its path checked; Refused for a stream that is no ZIP. */
    static List<Entry> list(Source source) throws IOException {
        List<Entry> entries = new ArrayList<>();
        byte[] buffer = new byte[1 << 16];
        try (ZipInputStream zip = open(source)) {
            ZipEntry zipEntry;
            while ((zipEntry = next(zip)) != null) {
                if (entries.size() == MOST_ENTRIES) {
                    throw new Refused("the ZIP has more than " + MOST_ENTRIES + " entries, far more than the SCFA folder has files");
                }
                // Its path first: an entry that would leave the ZIP's folder ends the import before it is read.
                List<String> path = path(zipEntry.getName());
                TestState.step("Checking the ZIP: " + last(path));
                entries.add(new Entry(zipEntry.getName(), path, drain(zip, buffer)));
            }
        }
        return entries;
    }

    /**
     * What the import takes of {@code entries}, by the folder import's rules, below the one place in the ZIP that has a
     * gamedata folder, its top or a folder there; {@code scfa} holds what earlier imports took. Refused for a ZIP with no
     * such place, or more than one.
     */
    static Plan plan(List<Entry> entries, File scfa) throws Refused {
        Map<String, String> roots = new LinkedHashMap<>();
        for (Entry entry : entries) {
            for (int depth = 0; depth <= 1 && depth < entry.path.size(); ++depth) {
                boolean inside = entry.path.size() > depth + 1 || entry.folder;
                if (inside && entry.path.get(depth).equalsIgnoreCase(ScfaImport.GAMEDATA)) {
                    String root = depth == 0 ? "" : entry.path.get(0);
                    roots.putIfAbsent(root.toLowerCase(Locale.ROOT), root);
                }
            }
        }
        if (roots.isEmpty()) {
            throw new Refused("the ZIP has no gamedata folder, neither at its top nor in a folder there: zip the Supreme "
                + "Commander Forged Alliance folder");
        }
        if (roots.size() > 1) {
            throw new Refused("the ZIP has a gamedata folder in " + roots.size() + " places (" + places(roots.values())
                + "): zip one Supreme Commander Forged Alliance folder");
        }
        String root = roots.values().iterator().next();
        // A file the ZIP holds twice, also in another case, which Android's shared storage and the engine take for the
        // same, counts once: its later entry, as an unzip leaves it.
        String[] targets = new String[entries.size()];
        Map<String, Integer> latest = new HashMap<>();
        for (int i = 0; i < entries.size(); ++i) {
            Entry entry = entries.get(i);
            if (entry.folder) {
                continue;
            }
            if (root.isEmpty()) {
                targets[i] = ScfaImport.target(entry.path);
            } else if (!entry.path.isEmpty() && entry.path.get(0).equalsIgnoreCase(root)) {
                targets[i] = ScfaImport.target(entry.path.subList(1, entry.path.size()));
            }
            if (targets[i] != null) {
                latest.put(targets[i].toLowerCase(Locale.ROOT), i);
            }
        }
        String[] writes = new String[entries.size()];
        Map<String, Integer> taken = new LinkedHashMap<>();
        int there = 0;
        int others = 0;
        long bytes = 0;
        for (int i = 0; i < entries.size(); ++i) {
            Entry entry = entries.get(i);
            String target = targets[i];
            if (entry.folder) {
                continue;
            }
            if (target == null || latest.get(target.toLowerCase(Locale.ROOT)) != i) {
                ++others;
                continue;
            }
            taken.merge(target.substring(0, target.indexOf('/')), 1, Integer::sum);
            if (ScfaImport.imported(new File(scfa, target), entry.size)) {
                ++there;
                continue;
            }
            writes[i] = target;
            bytes += entry.size;
        }
        return new Plan(root, writes, taken, there, others, bytes);
    }

    private static String places(Collection<String> roots) {
        StringBuilder out = new StringBuilder();
        for (String root : roots) {
            out.append(out.length() > 0 ? ", " : "").append(root.isEmpty() ? "its top" : root + "/");
        }
        return out.toString();
    }

    /**
     * An entry's name as the folders and the name of a path in the ZIP's folder: "\" read as "/", as older tools on Windows
     * write it (Windows PowerShell 5.1's Compress-Archive), and "." and empty parts left out. Refused for a name that would
     * leave the ZIP's folder (Zip Slip): an absolute one, or one with a "..". Android 14's ZipInputStream refuses those
     * too, for an app that targets it (ZipPathValidator), but with "/" alone; this holds for "\" and before 14 as well.
     */
    static List<String> path(String name) throws Refused {
        String slashes = name.replace('\\', '/');
        boolean leaves = slashes.startsWith("/");
        List<String> path = new ArrayList<>();
        for (String part : slashes.split("/")) {
            leaves |= part.equals("..");
            if (!part.isEmpty() && !part.equals(".")) {
                path.add(part);
            }
        }
        if (leaves) {
            throw new Refused("the ZIP's entry " + name + " would leave its folder (Zip Slip): the import takes nothing of it");
        }
        return path;
    }

    /** {@code relative} in {@code folder}, which it must not leave, not even by a link: the guard on each file the import writes. */
    static File under(File folder, String relative) throws IOException {
        File file = new File(folder, relative);
        if (!file.getCanonicalPath().startsWith(folder.getCanonicalPath() + File.separator)) {
            throw new Refused(relative + " would leave " + folder.getName() + "/ (Zip Slip): the import writes nothing there");
        }
        return file;
    }

    /**
     * Why a file whose first {@code length} bytes are {@code head} is no ZIP the import reads, or null when it begins with an
     * entry's local header ("PK\3\4"), as a ZIP that holds anything does. Its name and type, which may say anything, do
     * not count.
     */
    static String notZip(byte[] head, int length) {
        if (length == 0) {
            return "the file is empty";
        }
        if (begins(head, length, LOCAL_HEADER)) {
            return null;
        }
        if (begins(head, length, END)) {
            return "the ZIP is empty";
        }
        return "the file does not begin as a ZIP does, whatever its name or type say";
    }

    private static boolean begins(byte[] head, int length, byte[] signature) {
        if (length < signature.length) {
            return false;
        }
        for (int i = 0; i < signature.length; ++i) {
            if (head[i] != signature[i]) {
                return false;
            }
        }
        return true;
    }

    /** The ZIP's stream from its start, its first bytes checked: Refused when they are not a ZIP's. */
    private static ZipInputStream open(Source source) throws IOException {
        InputStream in = new BufferedInputStream(source.open(), 1 << 16);
        try {
            byte[] head = new byte[LOCAL_HEADER.length];
            in.mark(head.length);
            int length = 0;
            int read;
            while (length < head.length && (read = in.read(head, length, head.length - length)) != -1) {
                length += read;
            }
            String notZip = notZip(head, length);
            if (notZip != null) {
                throw new Refused(notZip);
            }
            in.reset();
            return new ZipInputStream(in);
        } catch (IOException | RuntimeException e) {
            in.close();
            throw e;
        }
    }

    /** The ZIP's next entry, or null after its last. */
    private static ZipEntry next(ZipInputStream zip) throws IOException {
        try {
            return zip.getNextEntry();
        } catch (IllegalArgumentException e) {
            // ZipInputStream reads a name without the UTF-8 flag as UTF-8 too, which another code page's breaks.
            throw new Refused("the ZIP has an entry whose name is not UTF-8, as older tools on Windows write them: zip the "
                + "folder again with 7-Zip");
        }
    }

    /** The rest of the entry read and dropped, as a stream has no way past it but through it: its bytes. */
    private static long drain(InputStream in, byte[] buffer) throws IOException {
        long size = 0;
        int read;
        while ((read = in.read(buffer)) != -1) {
            size += read;
            Cancellation.check();
        }
        return size;
    }

    /** The ZIP's changed since the import read its list: what it would write is not what it checked. */
    private static Refused changed() {
        return new Refused("the ZIP changed while the import read it: pick it again once nothing writes it");
    }
}
