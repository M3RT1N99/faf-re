package io.github.m3rt1n99.fafre;

import java.util.Locale;

/**
 * The Game card's "Get FAF's newest version": where its job (NewestGameFilesJob, TestService's ACTION_GAME_FILES) is,
 * and the card's line on it. The job's thread sets it and MainActivity polls it; every access holds the class lock. No
 * Android in here, for the tests.
 */
final class NewestGameFiles {
    enum Phase {
        /** No check since the app started. */
        IDLE,
        CHECKING,
        /**
         * FAF's newest version is known: while the job goes on to measure its download (the Progress card shows it), and
         * when it has ended with nothing to download or the files downloaded.
         */
        KNOWN,
        /** On a metered network: the card asks before the download, and its button asks again. */
        ASKING,
        DOWNLOADING,
        FAILED,
        CANCELLED,
        /** The network became metered under a download that was to run without a question. */
        METERED
    }

    /** What the job does once FAF's newest version is known. */
    enum Next {
        NOTHING,
        ASK,
        DOWNLOAD
    }

    /** A download's size when the updater does not say it: more than any version's so far (3840: 682 MB). */
    static final long UNSAID_BYTES = 750L << 20;

    final Phase phase;
    /** FAF's newest version, -1 until a check has found it. */
    final int newest;
    /** What getting it downloads, in bytes; -1 when that is not known. */
    final long bytes;
    /** Why it failed. */
    final String note;

    private static NewestGameFiles sNow = new NewestGameFiles(Phase.IDLE, -1, -1, "");
    private static boolean sQuestion;

    NewestGameFiles(Phase phase, int newest, long bytes, String note) {
        this.phase = phase;
        this.newest = newest;
        this.bytes = bytes;
        this.note = note;
    }

    static synchronized NewestGameFiles now() {
        return sNow;
    }

    /** The download to ask about, once, or null: the card asks when no job runs. */
    static synchronized NewestGameFiles question() {
        NewestGameFiles question = sQuestion ? sNow : null;
        sQuestion = false;
        return question;
    }

    /**
     * While the card asks, the download it asks about, else null: the button asks again with its version and size, with
     * no new check, as the question's dialog may be gone (Not now, Back, a screen made anew).
     */
    static synchronized NewestGameFiles pending() {
        return sNow.phase == Phase.ASKING ? sNow : null;
    }

    static void checking() {
        set(new NewestGameFiles(Phase.CHECKING, -1, -1, ""), false);
    }

    static void known(int newest) {
        set(new NewestGameFiles(Phase.KNOWN, newest, -1, ""), false);
    }

    static void asking(int newest, long bytes) {
        set(new NewestGameFiles(Phase.ASKING, newest, bytes, ""), true);
    }

    static void downloading(int newest, long bytes) {
        set(new NewestGameFiles(Phase.DOWNLOADING, newest, bytes, ""), false);
    }

    /** The job ended without the files (FAILED, CANCELLED or METERED), keeping what it knew of the version. */
    static void stopped(Phase phase, String note) {
        synchronized (NewestGameFiles.class) {
            sNow = new NewestGameFiles(phase, sNow.newest, sNow.bytes, note);
            sQuestion = false;
        }
        TestState.touch();
    }

    private static void set(NewestGameFiles now, boolean question) {
        synchronized (NewestGameFiles.class) {
            sNow = now;
            sQuestion = question;
        }
        TestState.touch();
    }

    /**
     * Nothing when the phone has FAF's newest version ready, or a newer one, which the game starts; else the download,
     * which on a metered network waits for the user's yes to its size. {@code phone} is the phone's newest ready
     * version, -1 for none.
     */
    static Next next(int phone, int newest, boolean metered, boolean yes) {
        if (phone >= newest) {
            return Next.NOTHING;
        }
        return metered && !yes ? Next.ASK : Next.DOWNLOAD;
    }

    /** What a download of {@code bytes} (-1: the updater did not say) counts for in the phone's room (Room). */
    static long counted(long bytes) {
        return bytes < 0 ? UNSAID_BYTES : bytes;
    }

    /** The card's line, {@code phone} being the newest version whose files are ready on the phone, -1 for none. */
    String line(int phone) {
        StringBuilder line = new StringBuilder("FAF game files: ").append(phone >= 0 ? String.valueOf(phone) : "none on the phone yet");
        if (phase == Phase.CHECKING) {
            line.append(" (checking FAF's newest version…)");
        } else if (newest >= 0) {
            line.append(" (newest at FAF: ").append(newest).append(phone == newest ? "), up to date"
                : phone > newest ? "); the game starts " + phone + ", as it is newer" : ")");
        }
        switch (phase) {
            case ASKING:
                line.append('\n').append(newest).append(" is ").append(size(bytes))
                    .append(" from FAF; on a metered network the app asks first, and the button asks again");
                break;
            case DOWNLOADING:
                line.append("\nDownloading ").append(newest).append(" (").append(size(bytes)).append("): see Progress");
                break;
            case FAILED:
                // Too little room ends the job here before any download, as a download that failed does: the line holds for both.
                line.append(newest < 0 ? "\nNo check of FAF's newest version: " + note
                    : "\nGetting " + newest + " failed: " + note + "; the button tries again, keeping anything downloaded so far");
                break;
            case CANCELLED:
                line.append(newest < 0 ? "\nThe check was cancelled"
                    : "\nGetting " + newest + " was cancelled; the button goes on from where it stopped");
                break;
            case METERED:
                line.append("\nGetting ").append(newest).append(" stopped, as the network became metered; the button goes on from "
                    + "where it stopped, after a question on a metered network");
                break;
            default:
                break;
        }
        return line.toString();
    }

    /**
     * "682 MB" in MB of 1,048,576 bytes, as Android's data usage counts them; "less than 1 MB" rather than "0 MB"; or the
     * order of size that every version has when the updater did not say.
     */
    static String size(long bytes) {
        if (bytes < 0) {
            return "about 700 MB";
        }
        return bytes < 1048576 ? "less than 1 MB" : String.format(Locale.ROOT, "%.0f MB", bytes / 1048576.0);
    }
}
