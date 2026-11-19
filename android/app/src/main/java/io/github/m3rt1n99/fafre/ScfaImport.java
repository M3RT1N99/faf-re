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
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Locale;

/**
 * Copies the game archives the engine needs from the user's own Supreme Commander: Forged
 * Alliance folder (picked with the system's folder picker) into scfa/: gamedata/*.scd, fonts/
 * (the game's text), sounds/ and movies/ (the game runs without them, silent and without its films), and
 * maps/ when the folder has one (FAF's map vault has the retail maps too, so it is optional). A ZIP of
 * the folder takes the same way in (ScfaZip), by the same rules: what is taken (target), what is there
 * already (imported), and how a file is written (copy).
 */
final class ScfaImport {
    /** The folder of the game's archives, of which the imports take the archives (archive) alone. */
    static final String GAMEDATA = "gamedata";
    /** The folders the imports take whole, as the game reads each, in the folder import's order. */
    static final List<String> FOLDERS = Collections.unmodifiableList(Arrays.asList("maps", "fonts", "sounds", "movies"));

    private static final class Child {
        String id;
        String name;
        String mime;
        long size;
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

    static void run(Context context, Storage storage, Uri tree) throws IOException {
        ContentResolver resolver = context.getContentResolver();
        String rootId = DocumentsContract.getTreeDocumentId(tree);
        Child gamedata = find(resolver, tree, rootId, GAMEDATA);
        if (gamedata == null) {
            throw new IOException("the picked folder has no gamedata folder: pick the Supreme Commander Forged Alliance folder itself");
        }
        int copied = 0;
        for (Child child : children(resolver, tree, gamedata.id)) {
            if (archive(child.name)) {
                copy(resolver, tree, child, new File(storage.scfa, GAMEDATA + "/" + child.name));
                ++copied;
            }
        }
        TestState.line("Imported " + copied + " archives into " + new File(storage.scfa, GAMEDATA));
        // fonts/: the game draws its text with the folder's TrueType fonts (D3D_InitFonts).
        for (String folder : FOLDERS) {
            copyFolder(resolver, tree, rootId, storage, folder);
        }
        if (fonts(storage) == 0) {
            TestState.line("No fonts: the picked folder has no fonts folder with .ttf files, and the game cannot start without them");
        }
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
            }, storage.root::getUsableSpace);
        } catch (Cancellation.Cancelled e) {
            throw e;
        } catch (IOException e) {
            TestState.line("The ZIP is not imported: " + (e.getMessage() != null ? e.getMessage() : e.toString()));
        } finally {
            try {
                resolver.releasePersistableUriPermission(zip, Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (SecurityException e) {
                // The grant was one for this time only, which goes by itself.
            }
        }
    }

    private static void copyFolder(ContentResolver resolver, Uri tree, String rootId, Storage storage, String name) throws IOException {
        Child folder = find(resolver, tree, rootId, name);
        if (folder != null) {
            int files = copyTree(resolver, tree, folder, new File(storage.scfa, name));
            TestState.line("Imported " + name + "/ (" + files + " files)");
        }
    }

    private static int copyTree(ContentResolver resolver, Uri tree, Child dir, File dest) throws IOException {
        int files = 0;
        Storage.mkdirs(dest);
        for (Child child : children(resolver, tree, dir.id)) {
            File target = new File(dest, child.name);
            if (Document.MIME_TYPE_DIR.equals(child.mime)) {
                files += copyTree(resolver, tree, child, target);
            } else {
                copy(resolver, tree, child, target);
                ++files;
            }
        }
        return files;
    }

    private static void copy(ContentResolver resolver, Uri tree, Child child, File dest) throws IOException {
        if (imported(dest, child.size)) {
            return;
        }
        TestState.step("Importing " + child.name);
        Uri uri = DocumentsContract.buildDocumentUriUsingTree(tree, child.id);
        try (InputStream in = resolver.openInputStream(uri)) {
            if (in == null) {
                throw new IOException("cannot open " + child.name);
            }
            copy(in, dest);
        }
    }

    private static Child find(ContentResolver resolver, Uri tree, String parentId, String name) {
        for (Child child : children(resolver, tree, parentId)) {
            if (child.name.equalsIgnoreCase(name) && Document.MIME_TYPE_DIR.equals(child.mime)) {
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
                Child child = new Child();
                child.id = cursor.getString(0);
                child.name = cursor.getString(1);
                child.mime = cursor.getString(2);
                child.size = cursor.isNull(3) ? -1 : cursor.getLong(3);
                out.add(child);
            }
        }
        return out;
    }
}
