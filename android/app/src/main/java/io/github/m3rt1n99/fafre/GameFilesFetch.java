package io.github.m3rt1n99.fafre;

import java.io.IOException;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * The game files of the version a start asks for (GameStart.version) when the phone has not got them, before the program
 * starts: from FAF's updater by the rules of the Game card's download of FAF's newest (NewestGameFilesJob). Their size
 * first, which must fit in the phone's free space with room to spare; then on a metered network the player's yes to it,
 * and without a yes the download runs only on a network that is not metered: one that becomes metered under it stops it,
 * and the player is asked about the rest. GameFiles.ensure keeps what the phone has, the other versions and what an earlier
 * try got of this one, and goes on from there. Its end is the start's: the files are ready, or NO_GAME_FILES when FAF has
 * none of the version, DOWNLOAD for the network or the phone's storage, too little room among it, CANCELLED for Back and
 * for the player's no. No Android in here, for the tests: the Preparation gives it the phone's network, the download and
 * the question.
 */
final class GameFilesFetch {
    /** The player, whom the start asks before a download on a metered network. */
    interface Question {
        /** Whether the player downloads {@code bytes} of {@code version} (-1: the updater did not say); a Cancelled for Back. */
        boolean yes(int version, long bytes) throws IOException;
    }

    private GameFilesFetch() {
    }

    /** {@code version}'s files ready on the phone, or the start's failure. */
    static void run(Storage storage, int version, GameFiles.Log log, Http.Requests requests,
        NewestGameFilesJob.Connectivity connectivity, NewestGameFilesJob.Download download, Question question)
        throws GameStart.Failure {
        String what = "FAF game version " + version;
        try {
            for (boolean yes = false;;) {
                log.step("Measuring the download of " + what);
                long bytes = GameFiles.toDownload(storage.game(version), storage.downloads(), version, requests);
                String noRoom = NewestGameFiles.noRoom(bytes, download.room());
                if (noRoom != null) {
                    throw GameStart.Failure.download(what + ": " + noRoom);
                }
                if (!yes && connectivity.metered()) {
                    log.step(what + ": " + NewestGameFiles.size(bytes) + " to download on a metered network");
                    if (!question.yes(version, bytes)) {
                        throw GameStart.Failure.notDownloaded(version, bytes);
                    }
                    yes = true;
                }
                log.line(what + " is not on the phone: " + NewestGameFiles.size(bytes) + " to download"
                    + (yes ? ", which the player said yes to on a metered network" : ""));
                if (yes) {
                    download.files(version);
                } else if (!downloadedUnmetered(version, connectivity, download, log)) {
                    // Measured again: the rest is what the player is asked about.
                    continue;
                }
                log.line(what + " is ready");
                return;
            }
        } catch (Cancellation.Cancelled e) {
            throw GameStart.Failure.cancelled();
        } catch (IOException e) {
            throw GameStart.fetchFailure(e, GameStart.Code.NO_GAME_FILES, what);
        }
    }

    /**
     * The files on a network that is not metered: false when it became metered under the download, which stopped it as Cancel
     * does, though it is no Cancel.
     */
    private static boolean downloadedUnmetered(int version, NewestGameFilesJob.Connectivity connectivity,
        NewestGameFilesJob.Download download, GameFiles.Log log) throws IOException {
        AtomicBoolean becameMetered = new AtomicBoolean();
        NewestGameFilesJob.Watch watch = connectivity.watch(() -> {
            if (!becameMetered.getAndSet(true)) {
                Cancellation.request();
            }
        });
        boolean ready;
        try {
            download.files(version);
            ready = true;
        } catch (Cancellation.Cancelled e) {
            if (!becameMetered.get()) {
                throw e;
            }
            ready = false;
        } finally {
            watch.close();
        }
        if (becameMetered.get()) {
            // Its stop, or its word after the last file, which nothing has to stop any more.
            Cancellation.reset();
            if (!ready) {
                log.line("The network has become metered: the download stops, and the player is asked about the rest");
            }
        }
        return ready;
    }
}
