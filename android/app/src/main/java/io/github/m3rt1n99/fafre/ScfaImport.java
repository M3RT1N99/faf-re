package io.github.m3rt1n99.fafre;

import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;

import java.io.File;
import java.io.FileOutputStream;
import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.TimeUnit;
import java.util.function.LongSupplier;

/**
 * Copies the game archives the engine needs from the user's own Supreme Commander: Forged
 * Alliance folder (picked with the system's folder picker) into scfa/: gamedata/*.scd, fonts/
 * (the game's text), sounds/ and movies/ (the game runs without them, silent and without its films), and
 * maps/ when the folder has one (FAF's map vault has the retail maps too, so it is optional). A ZIP of
 * the folder takes the same way in (ScfaZip), by the same rules: what is taken (target), what is there
 * already (imported), how a file is written (copy), and the room for what is written (Room, ToWrite).
 */
final class ScfaImport {
    /** The folder of the game's archives, of which the imports take the archives (archive) alone. */
    static final String GAMEDATA = "gamedata";
    /** The folders the imports take whole, as the game reads each, in the folder import's order. */
    static final List<String> FOLDERS = Collections.unmodifiableList(Arrays.asList("maps", "fonts", "sounds", "movies"));
    /** How often, in bytes written, an import checks the phone's room again within a file. */
    static final long CHECK_ROOM_EVERY = 64 * Sizes.MB;
    /**
     * The least time between two of an import's questions for the room while it writes, as StorageManager asks of a write
     * that goes on: Android counts the caches it may clear for each answer (getAllocatableBytes, seconds at worst), and an
     * SCFA folder has some 1,800 files.
     */
    static final long ASK_ROOM_EVERY_NANOS = TimeUnit.SECONDS.toNanos(30);

    /** A document of the picked folder, as its provider lists it. */
    static final class Child {
        final String id;
        final String name;
        final String mime;
        /** Its bytes; -1 where the provider does not say. */
        final long size;

        Child(String id, String name, String mime, long size) {
            this.id = id;
            this.name = name;
            this.mime = mime;
            this.size = size;
        }

        boolean folder() {
            return Document.MIME_TYPE_DIR.equals(mime);
        }
    }

    /** The picked folder as its provider has it, through the grant on it; the tests have one of their own. */
    interface Folder {
        /** The documents in the folder {@code id}. */
        List<Child> children(String id);

        /** The bytes of the document {@code file}, from their start. */
        InputStream open(Child file) throws IOException;
    }

    /** A file the folder import writes, and its place in scfa/. */
    private static final class Copy {
        final Child file;
        final File dest;

        Copy(Child file, File dest) {
            this.file = file;
            this.dest = dest;
        }
    }

    private ScfaImport() {
    }

    /** How many archives scfa/gamedata holds. */
    static int archives(Storage storage) {
        return count(new File(storage.scfa, GAMEDATA), ".scd");
    }

    /** How many fonts scfa/fonts holds, of the kind the game reads (not a .part file, which a copy whose process ended leaves). */
    static int fonts(Storage storage) {
        return count(new File(storage.scfa, "fonts"), ".ttf");
    }

    /** How many sound banks scfa/sounds holds, without the voices of its subfolders. */
    static int soundBanks(Storage storage) {
        return count(new File(storage.scfa, "sounds"), ".xwb");
    }

    /** How many movies scfa/movies holds. */
    static int movies(Storage storage) {
        return count(new File(storage.scfa, "movies"), ".sfd");
    }

    /** "SCFA: 18 archives, 14 fonts, 98 sound banks, 1004 movies", or that none is imported. */
    static String summary(Storage storage) {
        int archives = archives(storage);
        return archives > 0
            ? "SCFA: " + archives + " archives, " + fonts(storage) + " fonts, " + soundBanks(storage) + " sound banks, "
                + movies(storage) + " movies"
            : "SCFA: not imported yet";
    }

    private static int count(File dir, String suffix) {
        String[] names = dir.list();
        int count = 0;
        if (names != null) {
            for (String name : names) {
                if (name.toLowerCase(Locale.ROOT).endsWith(suffix)) {
                    ++count;
                }
            }
        }
        return count;
    }

    /** Whether a file of gamedata is one of the game's archives. */
    static boolean archive(String name) {
        return name.toLowerCase(Locale.ROOT).endsWith(".scd");
    }

    /**
     * Where the file at {@code path} in the SCFA folder (its folders, then its name) goes in scfa/, as either import takes
     * it: gamedata's archives, and the files of FOLDERS at any depth, those folders found in any case and named as the
     * game reads them; null for anything else, which no import takes.
     */
    static String target(List<String> path) {
        if (path.size() < 2) {
            return null;
        }
        String top = path.get(0);
        if (top.equalsIgnoreCase(GAMEDATA)) {
            return path.size() == 2 && archive(path.get(1)) ? GAMEDATA + "/" + path.get(1) : null;
        }
        for (String folder : FOLDERS) {
            if (top.equalsIgnoreCase(folder)) {
                return folder + "/" + String.join("/", path.subList(1, path.size()));
            }
        }
        return null;
    }

    /** Whether {@code dest} is there already, as an earlier import left it: a file of the same size. */
    static boolean imported(File dest, long size) {
        return dest.isFile() && dest.length() == size;
    }

    /**
     * {@code in}, read to its end, into {@code dest} by way of dest.part, which takes dest's place once whole, so that the
     * game never reads half a file; a copy cut off (the user's Cancel, a failure) leaves no part behind. {@code in} stays
     * open, as its owner's.
     */
    static void copy(InputStream in, File dest) throws IOException {
        Storage.mkdirs(dest.getParentFile());
        File part = new File(dest.getPath() + ".part");
        boolean whole = false;
        try {
            try (OutputStream out = new FileOutputStream(part)) {
                byte[] buffer = new byte[1 << 16];
                int read;
                while ((read = in.read(buffer)) != -1) {
                    out.write(buffer, 0, read);
                    Cancellation.check();
                }
            }
            replace(part, dest);
            whole = true;
        } finally {
            if (!whole) {
                part.delete();
            }
        }
    }

    /** {@code file} in the place of {@code dest}, which goes. */
    static void replace(File file, File dest) throws IOException {
        if (dest.exists() && !dest.delete()) {
            throw new IOException("cannot replace " + dest);
        }
        if (!file.renameTo(dest)) {
            throw new IOException("cannot rename " + file + " to " + dest);
        }
    }

    /**
     * What an import still has to write while it writes, which must fit in the phone's room: checked at each file and every
     * CHECK_ROOM_EVERY bytes in one, Android asked at most every ASK_ROOM_EVERY_NANOS, so that the import stops before the
     * storage is full where something else took the room. The spare that the claim before held takes what the file system
     * adds of its own, and what something else takes between two questions.
     */
    static final class ToWrite {
        private final Room room;
        private final LongSupplier clock;
        /** What is still to write of the bytes the claim counted. */
        private long left;
        private long unchecked;
        /** When Android was asked last: the claim's question, right before the import writes. */
        private long asked;

        /** For an import that has just claimed the room for {@code bytes}, with {@code clock} the nanoseconds. */
        ToWrite(Room room, long bytes, LongSupplier clock) {
            this.room = room;
            this.clock = clock;
            left = bytes;
            asked = clock.getAsLong();
        }

        /** At each file, before it is written, and every CHECK_ROOM_EVERY bytes in one. */
        void check() throws IOException {
            unchecked = 0;
            long time = clock.getAsLong();
            if (time - asked < ASK_ROOM_EVERY_NANOS) {
                return;
            }
            asked = time;
            long now = room.allocatable();
            if (now < left) {
                // The next import needs the rest and the spare: what this one wrote goes, and its room with it (the ZIP's),
                // or stays where the next finds it (the folder's).
                throw new IOException("the phone's storage ran short while the import wrote: "
                    + Room.free(left + Room.SPARE_BYTES - now));
            }
        }

        /**
         * {@code in}, the bytes of a file as the import writes them, which count as written as they are read: as many as
         * the claim counted of it, {@code size} (-1: none), for what is left.
         */
        InputStream counted(InputStream in, long size) {
            return new FilterInputStream(in) {
                /** What the claim counted of the file and is still to read. */
                private long share = Math.max(size, 0);

                @Override
                public int read() throws IOException {
                    byte[] one = new byte[1];
                    return read(one, 0, 1) == -1 ? -1 : one[0] & 0xFF;
                }

                @Override
                public int read(byte[] buffer, int offset, int length) throws IOException {
                    int read = super.read(buffer, offset, length);
                    if (read > 0) {
                        long counts = Math.min(read, share);
                        share -= counts;
                        left -= counts;
                        unchecked += read;
                        if (unchecked >= CHECK_ROOM_EVERY) {
                            check();
                        }
                    }
                    return read;
                }
            };
        }
    }

    /**
     * The SCFA import from the folder {@code tree}, which the user picked. Its lasting grant goes with the import, as the
     * ZIP's does: the app keeps no access to the user's folder that it no longer needs.
     */
    static void run(Context context, Storage storage, Uri tree) throws IOException {
        ContentResolver resolver = context.getContentResolver();
        try {
            run(storage, new Folder() {
                @Override
                public List<Child> children(String id) {
                    return ScfaImport.children(resolver, tree, id);
                }

                @Override
                public InputStream open(Child file) throws IOException {
                    InputStream in = resolver.openInputStream(DocumentsContract.buildDocumentUriUsingTree(tree, file.id));
                    if (in == null) {
                        throw new IOException("cannot open " + file.name);
                    }
                    return in;
                }
            }, DocumentsContract.getTreeDocumentId(tree), new PhoneRoom(context, storage), System::nanoTime);
        } finally {
            release(resolver, tree);
        }
    }

    /**
     * The folder import from {@code folder}, whose top is the document {@code rootId}, into {@code storage}'s scfa/, with
     * {@code room} the phone's where it writes, by the ZIP import's rule: first what it takes, of which the files not there
     * yet must fit in the room with the spare, claimed before the first byte (Room.NoRoom where they do not fit); then those
     * files, each whole in its place or not at all, with the room checked again as they go (ToWrite), {@code clock} the
     * nanoseconds that space its questions. A stop leaves the files written before it, which the next import does not
     * write again. No Android in here, for the tests.
     */
    static void run(Storage storage, Folder folder, String rootId, Room room, LongSupplier clock) throws IOException {
        // First, as the room it holds counts against this import's own.
        ScfaZip.dropStaging(storage.scfa);
        Child gamedata = find(folder, rootId, GAMEDATA);
        if (gamedata == null) {
            throw new IOException("the picked folder has no gamedata folder: pick the Supreme Commander Forged Alliance folder itself");
        }
        List<Copy> copies = new ArrayList<>();
        int archives = 0;
        for (Child child : folder.children(gamedata.id)) {
            if (archive(child.name)) {
                take(child, new File(storage.scfa, GAMEDATA + "/" + child.name), copies);
                ++archives;
            }
        }
        // fonts/: the game draws its text with the folder's TrueType fonts (D3D_InitFonts).
        Map<String, Integer> folders = new LinkedHashMap<>();
        for (String name : FOLDERS) {
            Child dir = find(folder, rootId, name);
            if (dir != null) {
                folders.put(name, takeTree(folder, dir, new File(storage.scfa, name), copies));
            }
        }
        if (!copies.isEmpty()) {
            long bytes = 0;
            for (Copy copy : copies) {
                bytes += Math.max(copy.file.size, 0);
            }
            // With the spare that FAF's game files leave too, claimed: Android clears the caches it counted now.
            room.claim("the import", bytes);
            ToWrite toWrite = new ToWrite(room, bytes, clock);
            for (Copy copy : copies) {
                TestState.step("Importing " + copy.file.name);
                toWrite.check();
                try (InputStream in = folder.open(copy.file)) {
                    copy(toWrite.counted(in, copy.file.size), copy.dest);
                }
            }
        }
        TestState.line("Imported " + archives + " archives into " + new File(storage.scfa, GAMEDATA));
        for (Map.Entry<String, Integer> taken : folders.entrySet()) {
            TestState.line("Imported " + taken.getKey() + "/ (" + taken.getValue() + " files)");
        }
        if (fonts(storage) == 0) {
            TestState.line("No fonts: the picked folder has no fonts folder with .ttf files, and the game cannot start without them");
        }
    }

    /** {@code file} for {@code dest}, unless an earlier import left it there. */
    private static void take(Child file, File dest, List<Copy> copies) {
        if (!imported(dest, file.size)) {
            copies.add(new Copy(file, dest));
        }
    }

    /** The files of {@code dir} at any depth, each for its place under {@code dest}: how many it has. */
    private static int takeTree(Folder folder, Child dir, File dest, List<Copy> copies) throws IOException {
        TestState.step("Checking the folder: " + dir.name);
        Cancellation.check();
        int files = 0;
        for (Child child : folder.children(dir.id)) {
            File target = new File(dest, child.name);
            if (child.folder()) {
                files += takeTree(folder, child, target, copies);
            } else {
                take(child, target, copies);
                ++files;
            }
        }
        return files;
    }

    /**
     * The SCFA import from a ZIP of the folder at {@code zip}, a document the user picked (ScfaZip). What stops it, a refusal
     * or a failure, is the log's line, as scfa/ stays as it was; the user's Cancel goes on as a Cancelled. The ZIP is read
     * once: its grant goes with the import.
     */
    static void runZip(Context context, Storage storage, Uri zip) throws IOException {
        ContentResolver resolver = context.getContentResolver();
        try {
            ScfaZip.run(storage, () -> {
                InputStream in = resolver.openInputStream(zip);
                if (in == null) {
                    throw new IOException("the ZIP cannot be opened");
                }
                return in;
            }, new PhoneRoom(context, storage));
        } catch (Cancellation.Cancelled e) {
            throw e;
        } catch (IOException e) {
            TestState.line("The ZIP is not imported: " + (e.getMessage() != null ? e.getMessage() : e.toString()));
        } finally {
            release(resolver, zip);
        }
    }

    /** The lasting grant on {@code picked} that the screen took for its import (Screen.onActivityResult), given back. */
    private static void release(ContentResolver resolver, Uri picked) {
        try {
            resolver.releasePersistableUriPermission(picked, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException e) {
            // The grant was one for this time only, which goes by itself.
        }
    }

    private static Child find(Folder folder, String parentId, String name) {
        for (Child child : folder.children(parentId)) {
            if (child.name.equalsIgnoreCase(name) && child.folder()) {
                return child;
            }
        }
        return null;
    }

    private static List<Child> children(ContentResolver resolver, Uri tree, String parentId) {
        List<Child> out = new ArrayList<>();
        Uri uri = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parentId);
        String[] columns = {Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE, Document.COLUMN_SIZE};
        try (Cursor cursor = resolver.query(uri, columns, null, null, null)) {
            while (cursor != null && cursor.moveToNext()) {
                out.add(new Child(cursor.getString(0), cursor.getString(1), cursor.getString(2),
                    cursor.isNull(3) ? -1 : cursor.getLong(3)));
            }
        }
        return out;
    }
}
