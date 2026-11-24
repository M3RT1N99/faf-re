package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.time.Instant;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

import org.junit.Test;

/**
 * The game's end when its activity is finished while the program runs (ProgramEnd): the report's lines, and the app's end
 * of a process whose program its quit did not end, which last_game gives in the app's words.
 */
public class ProgramEndTest {
    @Test
    public void aProgramThatOutlivesItsQuitHasItsProcessEndedByTheAppWithTheReportsEnd() throws InterruptedException {
        CountDownLatch quits = new CountDownLatch(1);
        Thread program = new Thread(() -> {
            try {
                quits.await();
            } catch (InterruptedException e) {
                // ends
            }
        });
        program.start();
        List<String> report = new CopyOnWriteArrayList<>();
        AtomicBoolean ended = new AtomicBoolean();
        try {
            Thread watch = ProgramEnd.watch(program, 50, report::add, () -> ended.set(true));
            watch.join(TimeUnit.SECONDS.toMillis(10));
            assertFalse(watch.isAlive());
        } finally {
            quits.countDown();
        }
        assertEquals(Collections.singletonList(ProgramEnd.QUIT_ENDED), report);
        assertTrue(ended.get());
    }

    @Test
    public void aProgramThatEndsAtItsQuitEndsItsProcessItself() throws InterruptedException {
        Thread program = new Thread(() -> {
        });
        program.start();
        List<String> report = new CopyOnWriteArrayList<>();
        AtomicBoolean ended = new AtomicBoolean();

        Thread watch = ProgramEnd.watch(program, TimeUnit.SECONDS.toMillis(30), report::add, () -> ended.set(true));
        watch.join(TimeUnit.SECONDS.toMillis(10));

        assertFalse("it waits no longer than the program", watch.isAlive());
        assertEquals(Collections.emptyList(), report);
        assertFalse(ended.get());
    }

    @Test
    public void theReportSaysWhatWasFinishedAndLastGameHowTheAppEndedIt() {
        assertEquals("game: the activity is destroyed, finished while the program runs (its task cleared or ended, or its "
            + "launcher finished it)", ProgramEnd.finished("is destroyed", false));
        assertEquals("game: the activity stops, finished while the start's preparation runs (its task cleared or ended, or "
            + "its launcher finished it)", ProgramEnd.finished("stops", true));
        String started = "2026-11-24T10:00:00Z";
        String report = "game: started " + started + "\n7012 ms: first frame\n"
            + "64000 ms: " + ProgramEnd.finished("is destroyed", false) + "\n" + ProgramEnd.QUIT_ENDED + "\n";
        assertArrayEquals(new Object[] {Instant.parse(started).toEpochMilli(), 1, null, 0, "the app ended the process: its "
            + "activity was finished while the program ran, and the program had not ended 5 s after it was asked to quit"},
            Status.lastGame(report));
        assertEquals("the app ended the process: its activity was finished while the start's preparation ran, before the "
            + "program", Status.lastGame("game: started " + started + "\n" + ProgramEnd.PREPARATION_ENDED + "\n")[4]);
    }
}
