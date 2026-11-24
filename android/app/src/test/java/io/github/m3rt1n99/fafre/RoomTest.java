package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertThrows;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.LongSupplier;

import org.junit.Test;

/**
 * The room for the app's big writes (Room): what Android lets the app allocate must hold the write and the spare, the
 * claim right before the write asks again and claims both, a call Android fails stops the write before it starts, and the
 * line says how much more to free. PhoneRoom's StorageManager cannot run in the JVM: Fake is Android's side here, for the
 * writers' tests too.
 */
public class RoomTest {
    /** 3840's packages on FAF's updater, 716 MB; with the spare 815,047,073 bytes, 816 MB. */
    private static final long BYTES = 715_047_073L;
    private static final long NEEDS = BYTES + Room.SPARE_BYTES;

    /** Android's side as a test sets it up: the room it says at each call, the claims it got, and its failures. */
    static final class Fake extends Room {
        private final LongSupplier mAllocatable;
        /** The bytes claimed (allocateBytes), in turn. */
        final List<Long> claims = new ArrayList<>();
        /** How often the room was asked (getAllocatableBytes). */
        int asked;
        /** What getAllocatableBytes throws, when set. */
        IOException allocatableFails;
        /** What allocateBytes throws, when set. */
        IOException allocateFails;

        Fake(long allocatable) {
            this(() -> allocatable);
        }

        Fake(LongSupplier allocatable) {
            mAllocatable = allocatable;
        }

        @Override
        long allocatable() throws IOException {
            ++asked;
            if (allocatableFails != null) {
                throw allocatableFails;
            }
            return mAllocatable.getAsLong();
        }

        @Override
        void allocate(long bytes) throws IOException {
            if (allocateFails != null) {
                throw allocateFails;
            }
            claims.add(bytes);
        }
    }

    @Test
    public void aWriteFitsWhenWhatAndroidLetsTheAppAllocateHoldsItAndTheSpare() throws IOException {
        new Fake(NEEDS).check("FAF game version 3840", BYTES);

        Room.NoRoom noRoom = assertThrows(Room.NoRoom.class, () -> new Fake(NEEDS - 1).check("FAF game version 3840", BYTES));
        assertEquals("a byte short", 1, noRoom.toFree);
        assertEquals(NEEDS, noRoom.needs);
        assertEquals("FAF game version 3840 needs 816 MB: free 1 MB more on the phone", noRoom.getMessage());
    }

    /**
     * A start's NO_ROOM line: what the write needs and how much more the player must free, as the storage settings count
     * it, where the bytes to free stay exact: 620 MB of 1,048,576 bytes are 650.1 MB there, which the line rounds up.
     */
    @Test
    public void theLineSaysWhatTheWriteNeedsAndHowMuchMoreToFree() {
        long toFree = 620L << 20;
        Room.NoRoom noRoom = assertThrows(Room.NoRoom.class,
            () -> new Fake(NEEDS - toFree).check("FAF game version 3839", BYTES));

        assertEquals("FAF game version 3839 needs 816 MB: free 651 MB more on the phone", noRoom.getMessage());
        assertEquals(toFree, noRoom.toFree);
        assertEquals("it needs 816 MB: free 651 MB more on the phone", noRoom.line("it"));
    }

    /** Freeing what the line says makes the write fit: a byte over a MB is a MB more, and over a tenth of a GB a tenth. */
    @Test
    public void theBytesToFreeAreRoundedUp() {
        assertEquals("free 1 MB more on the phone", Room.free(1));
        assertEquals("free 1 MB more on the phone", Room.free(Sizes.MB));
        assertEquals("free 621 MB more on the phone", Room.free(620 * Sizes.MB + 1));
        assertEquals("free 2.1 GB more on the phone", Room.free(2000 * Sizes.MB + 1));
    }

    @Test
    public void aClaimAsksAgainAndClaimsTheWriteAndTheSpare() throws IOException {
        Fake room = new Fake(NEEDS);

        room.claim("FAF game version 3840", BYTES);

        assertEquals(1, room.asked);
        assertEquals(Collections.singletonList(NEEDS), room.claims);
    }

    /** The room a check found before a question is gone at the claim, which another app took meanwhile: nothing is claimed. */
    @Test
    public void aClaimAsksAgainAndClaimsNothingThatNoLongerFits() throws IOException {
        AtomicInteger asked = new AtomicInteger();
        Fake room = new Fake(() -> asked.incrementAndGet() == 1 ? NEEDS : NEEDS - 1);
        room.check("the import", BYTES);

        Room.NoRoom noRoom = assertThrows(Room.NoRoom.class, () -> room.claim("the import", BYTES));

        assertEquals("the import needs 816 MB: free 1 MB more on the phone", noRoom.getMessage());
        assertEquals(Collections.emptyList(), room.claims);
    }

    /** Never another measure in its place: Android's failure is the write's, before it starts. */
    @Test
    public void aCallThatAndroidFailsStopsTheWriteWithItsOwnFailure() {
        IOException noVolume = new IOException("Android cannot say how much the app may write to the phone's storage: "
            + "Failed to find a storage device");
        Fake room = new Fake(Long.MAX_VALUE);
        room.allocatableFails = noVolume;
        assertSame(noVolume, assertThrows(IOException.class, () -> room.check("FAF game version 3840", BYTES)));
        assertSame(noVolume, assertThrows(IOException.class, () -> room.claim("FAF game version 3840", BYTES)));
        assertEquals(Collections.emptyList(), room.claims);

        IOException notCleared = new IOException("Android cannot clear 816 MB on the phone's storage for the write: "
            + "Failed to free 815047073");
        Fake clearing = new Fake(Long.MAX_VALUE);
        clearing.allocateFails = notCleared;
        assertSame(notCleared, assertThrows(IOException.class, () -> clearing.claim("FAF game version 3840", BYTES)));
    }
}
