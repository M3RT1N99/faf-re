package io.github.m3rt1n99.fafre;

import java.io.IOException;

/**
 * The room for the app's big writes where its files are: a start's game files of a version the phone has not got
 * (GameFilesFetch), the Game card's download of FAF's newest (NewestGameFilesJob) and the SCFA import's ZIP (ScfaZip).
 * It is what Android lets the app allocate there, which keeps Android's own low reserve free and counts the caches
 * Android may clear for the app, and a write must fit in it with SPARE_BYTES on top. By the free space alone, a start's
 * 682 MB on the emulator's 10 GB left 512 MB, near that reserve (496 MB there): installd emptied the apps' caches, and
 * installs failed. Right before the write the app claims the room, and Android clears those caches then, rather than
 * the write meeting a full disk. A call that Android fails throws, and the write does not start. No Android in here,
 * for the tests: PhoneRoom is the phone's.
 */
abstract class Room {
    /** What a write leaves on top of Android's reserve, for the game's and the replay runs' own files. */
    static final long SPARE_BYTES = 100L << 20;

    /** A write that does not fit: its line says what it needs and how much more the player must free. */
    static final class NoRoom extends IOException {
        /** The write's bytes and the spare. */
        final long needs;
        /** What the player must free on the phone for the write to fit, in bytes. */
        final long toFree;

        NoRoom(String what, long needs, long allocatable) {
            super(line(what, needs, needs - allocatable));
            this.needs = needs;
            toFree = needs - allocatable;
        }

        /** The line with another subject, as "it" on the Game card after the version it names. */
        String line(String what) {
            return line(what, needs, toFree);
        }

        private static String line(String what, long needs, long toFree) {
            return what + " needs " + NewestGameFiles.size(needs) + ": " + free(toFree);
        }
    }

    /** What Android lets the app allocate where its files are, in bytes. */
    abstract long allocatable() throws IOException;

    /** Android clears caches until {@code bytes} fit above its reserve, else throws. */
    abstract void allocate(long bytes) throws IOException;

    /** NoRoom unless {@code bytes} of {@code what} ("FAF game version 3840") fit with the spare on top. */
    final void check(String what, long bytes) throws IOException {
        long needs = bytes + SPARE_BYTES;
        long allocatable = allocatable();
        if (allocatable < needs) {
            throw new NoRoom(what, needs, allocatable);
        }
    }

    /** Right before the write: the room for {@code bytes} of {@code what} and the spare, checked again and claimed. */
    final void claim(String what, long bytes) throws IOException {
        check(what, bytes);
        allocate(bytes + SPARE_BYTES);
    }

    /** "free 621 MB more on the phone": the MB rounded up, so that freeing as much makes the write fit. */
    static String free(long bytes) {
        return "free " + ((bytes + (1L << 20) - 1) >> 20) + " MB more on the phone";
    }
}
