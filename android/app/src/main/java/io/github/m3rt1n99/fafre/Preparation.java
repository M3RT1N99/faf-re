package io.github.m3rt1n99.fafre;

import android.content.Context;
import android.net.Uri;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * What a start fetches or copies before its program starts: the map and the mods it names into the game's vault, from
 * FAF's servers as the replay test gets its own (Vault), its replay, the intent's data, into the app's files, which the
 * command line names, and last the game files of its version when the phone has not got them (GameFilesFetch), with the
 * game's init file after them. GameActivity's main runs it on SDL's thread, before the program; the activity shows its
 * step and its question, and Back cancels it (Cancellation, which this process has for nothing else).
 */
final class Preparation {
    /** The replay's copy in the app's files, which the command line's /replay names. */
    static final String REPLAY_COPY = "launcher-replay.SCFAreplay";
    /** Far more than any FAF replay: what a launcher's replay may be at most. */
    static final long REPLAY_LIMIT = 256 * Sizes.MB;

    /** What the activity shows while a preparation runs; null while none does. */
    private static volatile String sStep;
    /** What the activity asks the player while the preparation waits for the answer; null while it does not. */
    private static volatile Question sQuestion;

    /** A question over the surface, and its answer, which the activity gives. */
    static final class Question {
        final String title;
        final String message;
        /** Null until the player answers. */
        private volatile Boolean mAnswer;

        Question(String title, String message) {
            this.title = title;
            this.message = message;
        }

        void answer(boolean yes) {
            mAnswer = yes;
        }
    }

    private final Context mContext;
    private final Storage mStorage;
    private final File mVault;
    private final int mFetchVersion;
    private final String mMap;
    private final String[] mMods;
    private final String[] mModFiles;
    private final Uri mReplay;
    private final File mCopy;
    /** The download that runs, which its progress goes under. */
    private String mDownload = "";

    /**
     * {@code fetchVersion}: the game version whose files the phone has not got, which the preparation fetches, or -1;
     * {@code modFiles}: null, or one for each of {@code mods}; {@code replay} and {@code copy}: null without a replay.
     */
    Preparation(Context context, Storage storage, File vault, int fetchVersion, String map, String[] mods, String[] modFiles,
            Uri replay, File copy) {
        mContext = context.getApplicationContext();
        mStorage = storage;
        mVault = vault;
        mFetchVersion = fetchVersion;
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

    /** The question that waits for the player's answer, for the activity to show; null while none waits. */
    static Question question() {
        return sQuestion;
    }

    /**
     * All of it, or the start's failure: NO_MAP, NO_MOD, NO_GAME_FILES and DOWNLOAD for the downloads, REPLAY, INIT_FILE,
     * CANCELLED for Back and for the player's no to the game files on a metered network.
     */
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
            Http.Progress progress = (done, total) -> sStep = mDownload + "\n" + Sizes.progress(done, total);
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
            // Last, the largest by far: what can fail at once has failed before its hundreds of MB.
            if (mFetchVersion >= 0) {
                fetchGameFiles();
            }
            // Back after the last byte that a step's loop looked at, as it put its files in place: the start ends all the
            // same, as the activity said it would.
            if (Cancellation.requested()) {
                throw GameStart.Failure.cancelled();
            }
        } finally {
            sStep = null;
            sQuestion = null;
        }
    }

    /** The version's files, then the game's init file, which the command line names already. */
    private void fetchGameFiles() throws GameStart.Failure {
        // GameFiles.ensure's steps come a megabyte apart: the activity shows them, and the report gets the lines.
        GameFiles.Log files = new GameFiles.Log() {
            @Override
            public void line(String text) {
                GameReport.append(mContext, "game: " + text);
            }

            @Override
            public void step(String text) {
                sStep = text;
            }
        };
        GameFilesFetch.run(mStorage, mFetchVersion, files, Http.ONE_TRY, new PhoneNetwork(mContext),
            new PhoneRoom(mContext, mStorage),
            (version, before) -> GameFiles.ensure(mStorage, version, files, Http.WITH_RETRIES, before), this::askToDownload);
        try {
            GameFiles.gameInit(mContext.getAssets(), mStorage.game(mFetchVersion), mVault);
        } catch (IOException e) {
            throw GameStart.Failure.initFile(e.getMessage());
        }
    }

    /** GameFilesFetch's question on a metered network, which the activity shows; Back before it shows is a Cancelled. */
    private boolean askToDownload(int version, long bytes) throws IOException {
        String size = NewestGameFiles.size(bytes);
        Question question = new Question("Download " + size + " on a metered network?",
            "The game needs FAF's game version " + version + ", which is not on the phone yet. It is " + size + " from FAF's "
                + "servers, and the phone's network is metered (mobile data, a hotspot, or a Wi-Fi set so).");
        sQuestion = question;
        try {
            while (question.mAnswer == null) {
                Http.sleep(100);
            }
            return question.mAnswer;
        } finally {
            sQuestion = null;
        }
    }

    /** The launcher's replay into the app's files, through the read grant its start passed on. */
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
                        throw GameStart.Failure.replay("it is larger than " + REPLAY_LIMIT / Sizes.MB + " MB");
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
}
