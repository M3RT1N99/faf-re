package io.github.m3rt1n99.fafre;

import android.content.Context;
import android.net.Uri;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Locale;

/**
 * What a launcher's start fetches or copies before its program starts: the map and the mods it names into the game's vault,
 * from FAF's servers as the replay test gets its own (Vault), and its replay, the intent's data, into the app's cache, which
 * the command line names. GameActivity's main runs it on SDL's thread, before the program; the activity shows its step,
 * and Back cancels it (Cancellation, which this process has for nothing else).
 */
final class Preparation {
    /** The replay's copy in the app's cache, which the command line's /replay names. */
    static final String REPLAY_COPY = "launcher-replay.SCFAreplay";
    /** Far more than any FAF replay: what a launcher's replay may be at most. */
    static final long REPLAY_LIMIT = 256L << 20;

    /** What the activity shows while a preparation runs; null while none does. */
    private static volatile String sStep;

    private final Context mContext;
    private final Storage mStorage;
    private final File mVault;
    private final String mMap;
    private final String[] mMods;
    private final String[] mModFiles;
    private final Uri mReplay;
    private final File mCopy;
    /** The download that runs, which its progress goes under. */
    private String mDownload = "";

    /** {@code modFiles}: null, or one for each of {@code mods}; {@code replay} and {@code copy}: null without a replay. */
    Preparation(Context context, Storage storage, File vault, String map, String[] mods, String[] modFiles, Uri replay,
            File copy) {
        mContext = context.getApplicationContext();
        mStorage = storage;
        mVault = vault;
        mMap = map;
        mMods = mods;
        mModFiles = modFiles;
        mReplay = replay;
        mCopy = copy;
    }

    /** The step of the preparation that runs, for the activity to show; null while none runs. */
    static String step() {
        return sStep;
    }

    /** All of it, or the start's failure: NO_MAP, NO_MOD and DOWNLOAD for the downloads, REPLAY, CANCELLED for Back. */
    void run() throws GameStart.Failure {
        Cancellation.reset();
        sStep = "Preparing the game";
        try {
            GameFiles.Log log = new GameFiles.Log() {
                @Override
                public void line(String text) {
                    GameReport.append(mContext, "game: " + text);
                }

                @Override
                public void step(String text) {
                    mDownload = text;
                    sStep = text;
                    GameReport.append(mContext, "game: " + text);
                }
            };
            Http.Progress progress = (done, total) -> sStep = mDownload + "\n" + megabytes(done, total);
            if (mMap != null && !Vault.inScfa(mStorage, mMap)) {
                try {
                    Vault.fetchMap(mStorage, Storage.mkdirs(new File(mVault, "maps")), mMap, log, Http.WITH_RETRIES, progress);
                    Cancellation.check();
                } catch (IOException e) {
                    throw GameStart.fetchFailure(e, GameStart.Code.NO_MAP, "the map " + mMap);
                }
            }
            for (int i = 0; i < mMods.length; ++i) {
                try {
                    Vault.fetchMod(mStorage, Storage.mkdirs(new File(mVault, "mods")), mMods[i],
                        mModFiles != null ? mModFiles[i] : null, log, Http.WITH_RETRIES, progress);
                    Cancellation.check();
                } catch (IOException e) {
                    throw GameStart.fetchFailure(e, GameStart.Code.NO_MOD, "the mod " + mMods[i]);
                }
            }
            if (mReplay != null) {
                copyReplay();
            }
            // Back after the last byte that a step's loop looked at, as it put its files in place: the start ends all the
            // same, as the activity said it would.
            if (Cancellation.requested()) {
                throw GameStart.Failure.cancelled();
            }
        } finally {
            sStep = null;
        }
    }

    /** The launcher's replay into the app's cache, through the read grant its start passed on. */
    private void copyReplay() throws GameStart.Failure {
        sStep = "Copying the replay";
        long copied = 0;
        boolean done = false;
        try (InputStream in = mContext.getContentResolver().openInputStream(mReplay)) {
            if (in == null) {
                throw GameStart.Failure.replay("its provider opens no stream");
            }
            try (OutputStream out = new FileOutputStream(mCopy)) {
                byte[] buffer = new byte[1 << 16];
                int read;
                while ((read = in.read(buffer)) > 0) {
                    copied += read;
                    if (copied > REPLAY_LIMIT) {
                        throw GameStart.Failure.replay("it is larger than " + (REPLAY_LIMIT >> 20) + " MB");
                    }
                    out.write(buffer, 0, read);
                    Cancellation.check();
                }
            }
            if (copied == 0) {
                throw GameStart.Failure.replay("it is empty");
            }
            done = true;
        } catch (Cancellation.Cancelled e) {
            throw GameStart.Failure.cancelled();
        } catch (IOException | SecurityException e) {
            throw GameStart.Failure.replay(e.toString());
        } finally {
            if (!done) {
                mCopy.delete();
            }
        }
        GameReport.append(mContext, "game: the launcher's replay, " + copied + " bytes, copied for /replay");
    }

    private static String megabytes(long done, long total) {
        return total > 0
            ? String.format(Locale.ROOT, "%.1f of %.1f MB", done / 1048576.0, total / 1048576.0)
            : String.format(Locale.ROOT, "%.1f MB", done / 1048576.0);
    }
}
