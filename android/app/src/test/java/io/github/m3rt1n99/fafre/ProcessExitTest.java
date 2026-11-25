package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import android.app.ApplicationExitInfo;

import java.time.Instant;

import org.junit.Test;

/**
 * How the game's process ended, in the report's END line (ProcessExit.End.describe): Android's reason and status, the
 * signal a native crash or a kill by a signal names by its number, and the time of the death.
 */
public class ProcessExitTest {
    private static final long DIED = Instant.parse("2026-11-24T18:31:02.417Z").toEpochMilli();

    @Test
    public void aKillBySignalNamesTheSignalAndWhenTheProcessDied() {
        ProcessExit.End end = new ProcessExit.End(ApplicationExitInfo.REASON_SIGNALED, 6, null, false, 12345, DIED);
        assertEquals("killed by a signal, status 6 (SIGABRT), at 2026-11-24T18:31:02.417Z", end.describe());
        assertEquals(12345, end.pid);
        assertEquals(DIED, end.timestamp);
    }

    @Test
    public void aNativeCrashNamesItsSignalBeforeAndroidsDescription() {
        ProcessExit.End end =
            new ProcessExit.End(ApplicationExitInfo.REASON_CRASH_NATIVE, 11, "crash in libfafgame.so", true, 77, DIED);
        assertEquals("native crash, status 11 (SIGSEGV), at 2026-11-24T18:31:02.417Z, crash in libfafgame.so", end.describe());
    }

    @Test
    public void anExitsStatusIsItsCodeAndNoSignal() {
        ProcessExit.End end = new ProcessExit.End(ApplicationExitInfo.REASON_EXIT_SELF, 6, "", false, 77, DIED);
        assertNull(end.signal());
        assertEquals("exited, status 6, at 2026-11-24T18:31:02.417Z", end.describe());
        ProcessExit.End lowMemory = new ProcessExit.End(ApplicationExitInfo.REASON_LOW_MEMORY, 9, null, false, 77, DIED);
        assertNull("only a native crash and a kill by a signal have a signal for a status", lowMemory.signal());
    }

    @Test
    public void anEndWithoutItsTimeSaysNone() {
        ProcessExit.End end = new ProcessExit.End(ApplicationExitInfo.REASON_SIGNALED, 9, null, false, 77, 0);
        assertEquals("killed by a signal, status 9 (SIGKILL)", end.describe());
    }

    @Test
    public void aSignalNumberWithoutANameIsGivenAsItIs() {
        ProcessExit.End end = new ProcessExit.End(ApplicationExitInfo.REASON_SIGNALED, 35, null, false, 77, 0);
        assertNull(end.signal());
        assertEquals("killed by a signal, status 35", end.describe());
    }

    @Test
    public void theSignalsAreLinuxsByTheirNumbers() {
        assertEquals("SIGHUP", ProcessExit.signalName(1));
        assertEquals("SIGILL", ProcessExit.signalName(4));
        assertEquals("SIGBUS", ProcessExit.signalName(7));
        assertEquals("SIGFPE", ProcessExit.signalName(8));
        assertEquals("SIGTERM", ProcessExit.signalName(15));
        assertEquals("SIGSYS", ProcessExit.signalName(31));
        assertNull(ProcessExit.signalName(0));
        assertNull(ProcessExit.signalName(32));
        assertNull(ProcessExit.signalName(-6));
    }
}
