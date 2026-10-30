package io.github.m3rt1n99.fafre;

import android.content.ContentResolver;
import android.content.Context;
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
import java.util.List;
import java.util.Locale;

/**
 * Copies the game archives the engine needs from the user's own Supreme Commander: Forged
 * Alliance folder (picked with the system's folder picker) into scfa/: gamedata/*.scd, and
 * maps/ when the folder has one (FAF's map vault has the retail maps too, so it is optional).
 */
final class ScfaImport {
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
        String[] names = new File(storage.scfa, "gamedata").list();
        int count = 0;
        if (names != null) {
            for (String name : names) {
                if (name.toLowerCase(Locale.ROOT).endsWith(".scd")) {
                    ++count;
                }
            }
        }
        return count;
    }

    static void run(Context context, Storage storage, Uri tree) throws IOException {
        ContentResolver resolver = context.getContentResolver();
        String rootId = DocumentsContract.getTreeDocumentId(tree);
        Child gamedata = find(resolver, tree, rootId, "gamedata");
        if (gamedata == null) {
            throw new IOException("the picked folder has no gamedata folder: pick the Supreme Commander Forged Alliance folder itself");
        }
        int copied = 0;
        for (Child child : children(resolver, tree, gamedata.id)) {
            if (child.name.toLowerCase(Locale.ROOT).endsWith(".scd")) {
                copy(resolver, tree, child, new File(storage.scfa, "gamedata/" + child.name));
                ++copied;
            }
        }
        TestState.line("Imported " + copied + " archives into " + new File(storage.scfa, "gamedata"));
        Child maps = find(resolver, tree, rootId, "maps");
        if (maps != null) {
            int files = copyTree(resolver, tree, maps, new File(storage.scfa, "maps"));
            TestState.line("Imported maps/ (" + files + " files)");
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
        if (dest.isFile() && dest.length() == child.size) {
            return; // already imported
        }
        TestState.step("Importing " + child.name);
        Storage.mkdirs(dest.getParentFile());
        File part = new File(dest.getPath() + ".part");
        Uri uri = DocumentsContract.buildDocumentUriUsingTree(tree, child.id);
        try (InputStream in = resolver.openInputStream(uri); OutputStream out = new FileOutputStream(part)) {
            if (in == null) {
                throw new IOException("cannot open " + child.name);
            }
            byte[] buffer = new byte[1 << 16];
            int read;
            while ((read = in.read(buffer)) > 0) {
                out.write(buffer, 0, read);
                Cancellation.check();
            }
        }
        if (dest.exists() && !dest.delete()) {
            throw new IOException("cannot replace " + dest);
        }
        if (!part.renameTo(dest)) {
            throw new IOException("cannot rename " + part);
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
