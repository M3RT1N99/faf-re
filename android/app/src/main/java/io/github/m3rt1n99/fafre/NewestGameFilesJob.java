package io.github.m3rt1n99.fafre;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * The Game card's job (TestService's ACTION_GAME_FILES): FAF's newest game version onto the phone, for the game. The
 * check, the size and the room for it (Room), then on a metered network a question, else the room claimed and the files
 * by GameFiles.ensure, as a replay's run gets its version's. Without the user's yes to the size the download runs only on
 * a network that is not metered, and stops when the network becomes metered; the next one goes on from where it stopped.
 * No Android in here, for the tests: TestService gives it the phone's network, its room and the download.
 */
final class NewestGameFilesJob {
    /** The phone's network, as far as the job asks it. */
    interface Connectivity {
        /** Whether it is metered now (mobile data, a hotspot, a Wi-Fi set so); no network counts as one. */
        boolean metered();

        /**
         * Runs {@code onMetered} whenever the network is metered: at once for the network as it is, and at each change,
         * until the watch is closed.
         */
        Watch watch(Runnable onMetered);
    }

    interface Watch {
        void close();
    }

    /** The download onto the phone. */
    interface Download {
        /** The version's files onto the phone, as a replay's run gets its version's (GameFiles.ensure). */
        void files(int version) throws IOException;
    }

    /** In parity/downloads: the version the job's last download was for, so that a later check finds what it left. */
    private static final String TARGET = "newest-target";

    private NewestGameFilesJob() {
    }

    /**
     * FAF's newest version checked and downloaded, or {@code yesTo}, the version whose size the user has said yes to
     * (-1: none), downloaded without a check. The card's state says how it ended. A Cancel goes on as a Cancelled and a
     * failure of the download as itself, for the Progress card's log, as does one of Android's calls for the room; a failed
     * check, too little room and the network becoming metered say so on the card and end the job.
     */
    static void run(Storage storage, int yesTo, Http.Requests requests, Connectivity connectivity, Room room,
        Download download) throws IOException {
        int version = yesTo;
        AtomicBoolean becameMetered = new AtomicBoolean();
        Watch watch = null;
        try {
            if (version < 0) {
                NewestGameFiles.checking();
                TestState.step("Checking FAF's newest game version");
                try {
                    version = FafRelease.newest(requests);
                } catch (FafRelease.Failed e) {
                    NewestGameFiles.stopped(NewestGameFiles.Phase.FAILED, e.getMessage());
                    TestState.line("No check of FAF's newest game version: " + e.getMessage());
                    return;
                }
                TestState.line("FAF's newest game version: " + version);
                dropAbandoned(storage, version);
            }
            NewestGameFiles.known(version);
            // The game starts the phone's newest ready version, so one above FAF's newest leaves nothing to download.
            List<Integer> ready = GameFiles.readyVersions(storage);
            int phone = ready.isEmpty() ? -1 : ready.get(ready.size() - 1);
            NewestGameFiles.Next next = NewestGameFiles.next(phone, version, connectivity.metered(), yesTo >= 0);
            if (next == NewestGameFiles.Next.NOTHING) {
                TestState.line(phone == version ? "FAF game version " + version + " is on the phone already"
                    : "The phone has FAF game version " + phone + ", newer than FAF's newest: the game starts it, and "
                        + version + " is not downloaded");
                return;
            }
            TestState.step("Measuring the download of FAF game version " + version);
            long bytes = GameFiles.toDownload(storage.game(version), storage.downloads(), version, requests);
            TestState.line("FAF game version " + version + ": " + NewestGameFiles.size(bytes) + " to download");
            String what = "FAF game version " + version;
            if (next == NewestGameFiles.Next.ASK) {
                // A yes to a download that cannot fit would only fill the phone.
                room.check(what, NewestGameFiles.counted(bytes));
                NewestGameFiles.asking(version, bytes);
                TestState.line("The network is metered: the app asks before it downloads");
                return;
            }
            // Claimed right before the download: Android clears the caches it counted now, not under the download.
            room.claim(what, NewestGameFiles.counted(bytes));
            if (yesTo < 0) {
                // From the network as it is: one that has become metered since the check stops the download at its start.
                watch = connectivity.watch(() -> {
                    if (!becameMetered.getAndSet(true)) {
                        TestState.line("The network has become metered: the download stops");
                        Cancellation.request();
                    }
                });
            }
            GameFiles.write(target(storage), String.valueOf(version));
            NewestGameFiles.downloading(version, bytes);
            download.files(version);
            NewestGameFiles.known(version);
            TestState.line("FAF game version " + version + " is ready for the game");
        } catch (Room.NoRoom e) {
            // The card names the version already.
            NewestGameFiles.stopped(NewestGameFiles.Phase.FAILED, e.line("it"));
            TestState.line(e.getMessage());
        } catch (Cancellation.Cancelled e) {
            if (!becameMetered.get()) {
                NewestGameFiles.stopped(NewestGameFiles.Phase.CANCELLED, "");
                throw e;
            }
            NewestGameFiles.stopped(NewestGameFiles.Phase.METERED, "");
        } catch (IOException | RuntimeException e) {
            NewestGameFiles.stopped(NewestGameFiles.Phase.FAILED, e.getMessage() != null ? e.getMessage() : e.toString());
            throw e;
        } finally {
            if (watch != null) {
                watch.close();
            }
        }
    }

    /**
     * Once a check has settled on FAF's newest {@code version}: the job's last download, when it was for another
     * version and did not finish, is never gone on with, so its files go. Only the job's own: a replay test's partial
     * downloads of other versions stay for its next run, and a version that is ready stays.
     */
    private static void dropAbandoned(Storage storage, int version) {
        File target = target(storage);
        int last;
        try {
            last = Integer.parseInt(new String(Files.readAllBytes(target.toPath()), StandardCharsets.UTF_8).trim());
        } catch (IOException | NumberFormatException e) {
            // No download of the job's so far, or not a version the job wrote.
            return;
        }
        if (last != version) {
            target.delete();
            if (GameFiles.dropUnfinished(storage, last)) {
                TestState.line("What the download of FAF game version " + last + " left unfinished is removed: FAF's newest is "
                    + version + " now");
            }
        }
    }

    private static File target(Storage storage) {
        return new File(storage.downloads(), TARGET);
    }
}
