package io.github.m3rt1n99.fafre;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.Bundle;
import android.os.SystemClock;

import java.io.IOException;
import java.util.Collections;
import java.util.List;

/**
 * The game's status for a launcher in another app, read-only: content://io.github.m3rt1n99.fafre.status/game_data and
 * /last_game (Status: their columns), and the token a launcher's start carries (call "play_token", PlayTokens). Exported, as
 * the FAF client is another app, and it answers allowed callers only (Callers): a refused one gets the table's columns and no
 * row, and no token. No change notifications: Android lets any app that sees the provider observe its URIs, whatever a query
 * would answer it, and each would tell it when the player plays; the launcher queries when it needs to know. In the app's
 * main process, where the jobs run and LaunchGameActivity spends the tokens.
 */
public final class StatusProvider extends ContentProvider {
    static final String AUTHORITY = "io.github.m3rt1n99.fafre.status";
    /** The call that gives an allowed launcher a token for its next start, in the answer's TOKEN. */
    static final String PLAY_TOKEN = "play_token";
    static final String TOKEN = "token";

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] selectionArgs, String sortOrder) {
        List<String> path = uri.getPathSegments();
        String table = path.size() == 1 ? path.get(0) : null;
        List<String> columns = Status.columns(table);
        if (columns == null) {
            return null;
        }
        Context context = getContext();
        MatrixCursor cursor = new MatrixCursor(columns.toArray(new String[0]));
        if (!Callers.allowed(context, getCallingPackage())) {
            return cursor;
        }
        Object[] row = Status.GAME_DATA.equals(table) ? gameData(context) : Status.lastGame(GameReport.current(context));
        if (row != null) {
            cursor.addRow(row);
        }
        return cursor;
    }

    /** A token for the caller's next start, when it is allowed: the provider, unlike the start, sees the app that asks. */
    @Override
    public Bundle call(String method, String arg, Bundle extras) {
        if (!PLAY_TOKEN.equals(method) || !Callers.allowed(getContext(), getCallingPackage())) {
            return null;
        }
        Bundle answer = new Bundle();
        answer.putString(TOKEN, PlayTokens.APP.issue(SystemClock.elapsedRealtime()));
        return answer;
    }

    private static Object[] gameData(Context context) {
        int archives = 0;
        int fonts = 0;
        int soundBanks = 0;
        int movies = 0;
        List<Integer> versions = Collections.emptyList();
        try {
            Storage storage = new Storage(context);
            archives = ScfaImport.archives(storage);
            fonts = ScfaImport.fonts(storage);
            soundBanks = ScfaImport.soundBanks(storage);
            movies = ScfaImport.movies(storage);
            versions = GameFiles.readyVersions(storage);
        } catch (IOException e) {
            // No shared storage: nothing of the game's data is there.
        }
        return Status.gameData(archives, fonts, soundBanks, movies, versions, NewestGameFiles.now().newest, TestState.running(),
            TestState.step(), ProcessExit.running(context, GameReport.PROCESS), BuildConfig.VERSION_CODE,
            BuildConfig.VERSION_NAME, BuildConfig.GIT_COMMIT);
    }

    @Override
    public String getType(Uri uri) {
        List<String> path = uri.getPathSegments();
        return path.size() == 1 && Status.columns(path.get(0)) != null
            ? "vnd.android.cursor.item/vnd." + AUTHORITY + "." + path.get(0) : null;
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        throw new UnsupportedOperationException("read-only");
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        throw new UnsupportedOperationException("read-only");
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        throw new UnsupportedOperationException("read-only");
    }
}
