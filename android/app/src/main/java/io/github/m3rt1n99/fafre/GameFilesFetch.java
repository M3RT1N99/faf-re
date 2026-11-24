package io.github.m3rt1n99.fafre;

import java.io.IOException;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * The game files of the version a start asks for (GameStart.version) when the phone has not got them, before the program
 * starts: from FAF's updater by the rules of the Game card's download of FAF's newest (NewestGameFilesJob). Their size
 * first, which must fit in the phone's room with room to spare (Room); then on a metered network the player's yes to it,
 * and without a yes the download runs only on a network that is not metered: one that becomes metered under it stops it,
 * and the player is asked about the rest. A yes covers no more than its question named: a rest that grew past it while
 * the player was asked, as the Game card's job dropped the version's unfinished files (GameFiles.dropUnfinished), is
 * asked about again. Right before each download the room is claimed for what is left then, measured again after a
 * question, as the app's own download of the version (GameFiles.ensure's lock) may have gone on meanwhile.
 * GameFiles.ensure keeps what the phone has, the other versions and what an earlier try got of this one, and goes on from
 * there. Its end is the start's: the files are ready, or NO_GAME_FILES when FAF has none of the version, NO_ROOM when they
 * do not fit, DOWNLOAD for the network or the phone's storage, Android's calls for the room among it, CANCELLED for Back
 * and for the player's no.
 *
 * <p>A disk that becomes full under the download (ENOSPC) stays DOWNLOAD. Only the writes through java.io carry its errno
 * (an ErrnoException as the cause), not ensure's moves and lock through java.nio nor File.renameTo, so it cannot be told
 * apart for all of them; and NO_ROOM says how much to free, which a write that met a full disk does not know. The next
 * start measures before any byte, and its NO_ROOM says it. No Android in here, for the tests: the Preparation gives it
 * the phone's network, its room, the download and the question.
 */
final class GameFilesFetch {
    /** The player, whom the start asks before a download on a metered network. */
    interface Question {
        /** Whether the player downloads {@code bytes} of {@code version} (-1: the updater did not say); a Cancelled for Back. */
        boolean yes(int version, long bytes) throws IOException;
    }

    /** Before a question: the bytes one was about are -1 or more. */
    private static final long NOT_ASKED = Long.MIN_VALUE;

    private GameFilesFetch() {
    }

    /** {@code version}'s files ready on the phone, or the start's failure. */
    static void run(Storage storage, int version, GameFiles.Log log, Http.Requests requests,
        NewestGameFilesJob.Connectivity connectivity, Room room, NewestGameFilesJob.Download download, Question question)
        throws GameStart.Failure {
        String what = "FAF game version " + version;
        Http.Requests lengths = lengthsOnce(requests);
        try {
            // Ready also when the app's own download (GameFiles.ensure's lock) got it since the start looked, or since the
            // network became metered under this one, or while the player was asked: nothing to measure, ask about or claim.
            for (long askedAbout = NOT_ASKED; !GameFiles.ready(storage, version);) {
                log.step("Measuring the download of " + what);
                long bytes = GameFiles.toDownload(storage.game(version), storage.downloads(), version, lengths);
                // What the player's yes covers, on any network: what is left, unless it grew past the size the question
                // named, as the Game card's job dropped the version's unfinished files while the player was asked.
                boolean yes = askedAbout != NOT_ASKED
                    && NewestGameFiles.counted(bytes) <= NewestGameFiles.named(askedAbout);
                if (!yes && connectivity.metered()) {
                    // A yes to a download that cannot fit would only fill the phone.
                    room.check(what, NewestGameFiles.counted(bytes));
                    if (askedAbout != NOT_ASKED) {
                        log.line("The player said yes to " + NewestGameFiles.size(askedAbout) + " of " + what + ", and "
                            + NewestGameFiles.size(bytes) + " are to download now: the player is asked again");
                    }
                    log.step(what + ": " + NewestGameFiles.size(bytes) + " to download on a metered network");
                    if (!question.yes(version, bytes)) {
                        throw GameStart.Failure.notDownloaded(version, bytes);
                    }
                    askedAbout = bytes;
                    // Measured again: the app's own download may have got some of it, or all, while the player was asked, and
                    // the claim is for what is left; or the Game card's job dropped it.
                    continue;
                }
                log.line(what + " is not on the phone: " + NewestGameFiles.size(bytes) + " to download"
                    + (yes ? ", which the player said yes to on a metered network" : ""));
                // Checked right before the download, as a question waited for the player, and claimed: Android clears the
                // caches it counted now, rather than the download meeting a full disk.
                room.claim(what, NewestGameFiles.counted(bytes));
                if (yes) {
                    download.files(version);
                } else if (!downloadedUnmetered(version, connectivity, download, log)) {
                    // Measured again: the player is asked about the rest, unless a yes covers it.
                    continue;
                }
                break;
            }
            log.line(what + " is ready");
        } catch (Room.NoRoom e) {
            throw GameStart.Failure.noRoom(e);
        } catch (Cancellation.Cancelled e) {
            throw GameStart.Failure.cancelled();
        } catch (IOException e) {
            throw GameStart.fetchFailure(e, GameStart.Code.NO_GAME_FILES, what);
        }
    }

    /**
     * {@code requests} with FAF's updater asked once a package in a fetch: the measures after the first, after a question or a
     * metered stop, count what the phone has now against the lengths it said, without a HEAD request a second apart (Http's
     * pace) for each package again. A Back or a stop ends a measure as a request's own check would.
     */
    private static Http.Requests lengthsOnce(Http.Requests requests) {
        Map<String, Long> lengths = new HashMap<>();
        Map<String, Http.StatusException> statuses = new HashMap<>();
        return new Http.Requests() {
            @Override
            public String text(String url) throws IOException {
                return requests.text(url);
            }

            @Override
            public long length(String url) throws IOException {
                Long length = lengths.get(url);
                Http.StatusException status = statuses.get(url);
                if (length != null || status != null) {
                    Cancellation.check();
                    if (status != null) {
                        throw status;
                    }
                    return length;
                }
                try {
                    length = requests.length(url);
                } catch (Http.StatusException e) {
                    // A package that is not published, which GameFiles.toDownload passes over (a 404 of one not required);
                    // any other status ends the fetch.
                    statuses.put(url, e);
                    throw e;
                }
                lengths.put(url, length);
                return length;
            }
        };
    }

    /**
     * The files on a network that is not metered: false when it became metered under the download, which stopped it as Cancel
     * does, though it is no Cancel (Cancellation.stop). A Back meanwhile, even in the same moment, is the start's Cancelled.
     */
    private static boolean downloadedUnmetered(int version, NewestGameFilesJob.Connectivity connectivity,
        NewestGameFilesJob.Download download, GameFiles.Log log) throws IOException {
        AtomicBoolean becameMetered = new AtomicBoolean();
        NewestGameFilesJob.Watch watch = connectivity.watch(() -> {
            if (!becameMetered.getAndSet(true)) {
                Cancellation.stop();
            }
        });
        boolean ready;
        try {
            download.files(version);
            ready = true;
        } catch (Cancellation.Cancelled e) {
            if (Cancellation.requested() || !becameMetered.get()) {
                throw e;
            }
            ready = false;
        } finally {
            watch.close();
        }
        if (becameMetered.get()) {
            // Its stop, or its word after the last file, which nothing has to stop any more. A Back stays.
            Cancellation.clearStop();
            if (!ready) {
                log.line("The network has become metered: the download stops, and the player is asked about the rest");
            }
        }
        return ready;
    }
}
