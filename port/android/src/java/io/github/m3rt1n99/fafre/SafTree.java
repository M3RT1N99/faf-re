package io.github.m3rt1n99.fafre;

import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;

import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;

/**
 * A folder the user picked with ACTION_OPEN_DOCUMENT_TREE, walked through
 * DocumentsContract. This is the only way to read a USB stick, an SD card or a
 * folder copied to shared storage without broad storage permissions, which
 * Play policy and scoped storage both rule out for this app.
 */
final class SafTree implements SourceTree {
    private static final String[] COLUMNS = {
        Document.COLUMN_DOCUMENT_ID,
        Document.COLUMN_DISPLAY_NAME,
        Document.COLUMN_MIME_TYPE,
        Document.COLUMN_SIZE,
    };

    private final ContentResolver mResolver;
    private final Uri mTree;

    SafTree(ContentResolver resolver, Uri tree) {
        mResolver = resolver;
        mTree = tree;
    }

    @Override
    public Node root() throws IOException {
        String id;
        try {
            id = DocumentsContract.getTreeDocumentId(mTree);
        } catch (IllegalArgumentException e) {
            throw new IOException("not a folder link: " + mTree, e);
        }
        List<Node> nodes = query(DocumentsContract.buildDocumentUriUsingTree(mTree, id));
        if (nodes.isEmpty()) {
            throw new IOException("the picked folder is no longer available; pick it again");
        }
        return nodes.get(0);
    }

    @Override
    public List<Node> list(Node directory) throws IOException {
        return query(DocumentsContract.buildChildDocumentsUriUsingTree(mTree, directory.id));
    }

    @Override
    public InputStream open(Node file) throws IOException {
        Uri uri = DocumentsContract.buildDocumentUriUsingTree(mTree, file.id);
        InputStream in;
        try {
            in = mResolver.openInputStream(uri);
        } catch (SecurityException e) {
            throw new IOException("access to the picked folder was revoked; pick it again", e);
        }
        if (in == null) {
            throw new IOException("cannot open " + file.name);
        }
        return in;
    }

    @Override
    public String describe() {
        try {
            return DocumentsContract.getTreeDocumentId(mTree);
        } catch (IllegalArgumentException e) {
            return mTree.toString();
        }
    }

    private List<Node> query(Uri uri) throws IOException {
        List<Node> nodes = new ArrayList<>();
        Cursor cursor;
        try {
            cursor = mResolver.query(uri, COLUMNS, null, null, null);
        } catch (SecurityException e) {
            throw new IOException("access to the picked folder was revoked; pick it again", e);
        } catch (IllegalArgumentException | UnsupportedOperationException e) {
            throw new IOException("cannot list " + uri + ": " + e.getMessage(), e);
        }
        if (cursor == null) {
            throw new IOException("the document provider returned nothing for " + uri);
        }
        try {
            int idColumn = cursor.getColumnIndexOrThrow(Document.COLUMN_DOCUMENT_ID);
            int nameColumn = cursor.getColumnIndexOrThrow(Document.COLUMN_DISPLAY_NAME);
            int mimeColumn = cursor.getColumnIndexOrThrow(Document.COLUMN_MIME_TYPE);
            int sizeColumn = cursor.getColumnIndex(Document.COLUMN_SIZE);
            while (cursor.moveToNext()) {
                String id = cursor.getString(idColumn);
                String name = cursor.getString(nameColumn);
                if (id == null || name == null) {
                    continue;
                }
                boolean directory = Document.MIME_TYPE_DIR.equals(cursor.getString(mimeColumn));
                long size = sizeColumn >= 0 && !cursor.isNull(sizeColumn) ? cursor.getLong(sizeColumn) : -1;
                nodes.add(new Node(id, name, directory, directory ? -1 : size));
            }
        } catch (IllegalArgumentException e) {
            throw new IOException("unexpected listing from the document provider: " + e.getMessage(), e);
        } finally {
            cursor.close();
        }
        return nodes;
    }
}
