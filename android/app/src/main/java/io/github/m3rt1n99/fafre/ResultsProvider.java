package io.github.m3rt1n99.fafre;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.util.List;

/**
 * Serves the zips the app hands out, read-only: the results zips (parity/results/*.zip) to the app the user shares one with,
 * content://io.github.m3rt1n99.fafre.results/&lt;name&gt;, and the game's logs (GameLogs) to the launcher that asked for them,
 * .../game_logs/&lt;name&gt;. Not exported; access is by URI grant.
 */
public final class ResultsProvider extends ContentProvider {
    static final String AUTHORITY = "io.github.m3rt1n99.fafre.results";

    static Uri uri(File zip) {
        return Uri.parse("content://" + AUTHORITY + "/" + zip.getName());
    }

    /** The game's logs zip {@code zip} of GameLogs' folder. */
    static Uri gameLogs(File zip) {
        return Uri.parse("content://" + AUTHORITY + "/" + GameLogs.FOLDER + "/" + zip.getName());
    }

    private File file(Uri uri) throws FileNotFoundException {
        List<String> path = uri.getPathSegments();
        File file;
        if (path.size() == 2 && GameLogs.FOLDER.equals(path.get(0)) && GameLogs.NAME.matcher(path.get(1)).matches()) {
            file = new File(GameLogs.folder(getContext()), path.get(1));
        } else if (path.size() == 1 && path.get(0).matches("faf-re-results-[0-9-]+\\.zip")) {
            try {
                file = new File(new Storage(getContext()).results(), path.get(0));
            } catch (IOException e) {
                throw new FileNotFoundException(e.getMessage());
            }
        } else {
            throw new FileNotFoundException(String.valueOf(uri));
        }
        if (!file.isFile()) {
            throw new FileNotFoundException(file.getName());
        }
        return file;
    }

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] selectionArgs, String sortOrder) {
        try {
            File file = file(uri);
            MatrixCursor cursor = new MatrixCursor(new String[] {OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE});
            cursor.addRow(new Object[] {file.getName(), file.length()});
            return cursor;
        } catch (FileNotFoundException e) {
            return null;
        }
    }

    @Override
    public String getType(Uri uri) {
        return "application/zip";
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        if (!"r".equals(mode)) {
            throw new FileNotFoundException("read-only");
        }
        return ParcelFileDescriptor.open(file(uri), ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        throw new UnsupportedOperationException();
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        throw new UnsupportedOperationException();
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        throw new UnsupportedOperationException();
    }
}
