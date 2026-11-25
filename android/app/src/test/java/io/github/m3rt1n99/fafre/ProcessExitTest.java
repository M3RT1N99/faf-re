package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.fail;

import android.app.ApplicationExitInfo;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.StringReader;
import java.io.StringWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

/**
 * How the game's process ended, in the report's END line (ProcessExit.End.describe): Android's reason and status, the
 * signal a native crash or a kill by a signal names by its number, and the time of the death. And what is kept of its
 * logcat when a signal ended it and no tombstone was saved (ProcessExit.keepLogcat).
 */
public class ProcessExitTest {
    private static final long DIED = Instant.parse("2026-11-24T18:31:02.417Z").toEpochMilli();
    private static final String COMMAND = "logcat -d -b main -b crash -v threadtime --pid=4242";

    @Rule
    public final TemporaryFolder folder = new TemporaryFolder();

    /** A logcat that writes {@code lines} for the process it is asked for, which it notes. */
    private static ProcessExit.Logcat logcat(List<Integer> asked, List<String> lines) {
        return (pid, out) -> {
            asked.add(pid);
            Files.write(out.toPath(), lines, StandardCharsets.UTF_8);
        };
    }

    private static List<String> numbered(int count) {
        List<String> lines = new ArrayList<>();
        for (int i = 1; i <= count; ++i) {
            lines.add("11-24 18:31:02.417  4242  4299 I faf     : line " + i);
        }
        return lines;
    }

    private static String read(File file) throws IOException {
        return new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
    }

    @Test
    public void aKillBySignalWithoutATombstoneKeepsTheEndOfItsProcesssLogcat() throws IOException {
        File file = new File(folder.getRoot(), "game-logcat.txt");
        List<Integer> asked = new ArrayList<>();
        ProcessExit.End end = new ProcessExit.End(ApplicationExitInfo.REASON_SIGNALED, 6, null, false, 4242, DIED);
        ProcessExit.keepLogcat(end, file, logcat(asked, numbered(ProcessExit.LOGCAT_LINES + 100)));
        assertEquals(Arrays.asList(4242), asked);
        StringBuilder expected = new StringBuilder(COMMAND + ": its last " + ProcessExit.LOGCAT_LINES + " lines\n");
        for (String line : numbered(ProcessExit.LOGCAT_LINES + 100).subList(100, ProcessExit.LOGCAT_LINES + 100)) {
            expected.append(line).append('\n');
        }
        assertEquals(expected.toString(), read(file));
        assertFalse("the dump goes", new File(folder.getRoot(), "game-logcat.txt.part").exists());
    }

    @Test
    public void aNativeCrashWithoutItsTombstoneKeepsItsLogcatToo() throws IOException {
        File file = new File(folder.getRoot(), "game-logcat.txt");
        List<Integer> asked = new ArrayList<>();
        ProcessExit.End end = new ProcessExit.End(ApplicationExitInfo.REASON_CRASH_NATIVE, 11, null, false, 4242, DIED);
        ProcessExit.keepLogcat(end, file, logcat(asked, Arrays.asList("a", "b")));
        assertEquals(COMMAND + ": its last " + ProcessExit.LOGCAT_LINES + " lines\na\nb\n", read(file));
    }

    @Test
    public void anEndWithATombstoneOrWithoutASignalKeepsNoLogcat() throws IOException {
        File file = folder.newFile("game-logcat.txt");
        ProcessExit.Logcat none = (pid, out) -> fail("no logcat is read for process " + pid);
        ProcessExit.keepLogcat(new ProcessExit.End(ApplicationExitInfo.REASON_CRASH_NATIVE, 11, null, true, 4242, DIED), file,
            none);
        assertFalse("an earlier game's file goes", file.exists());
        Files.write(file.toPath(), Arrays.asList("an earlier game's"), StandardCharsets.UTF_8);
        ProcessExit.keepLogcat(new ProcessExit.End(ApplicationExitInfo.REASON_EXIT_SELF, 0, null, false, 4242, DIED), file,
            none);
        assertFalse(file.exists());
        ProcessExit.keepLogcat(new ProcessExit.End(ApplicationExitInfo.REASON_LOW_MEMORY, 9, null, false, 4242, DIED), file,
            none);
        assertFalse(file.exists());
    }

    @Test
    public void aLogcatThatCannotBeReadSaysWhy() throws IOException {
        File file = new File(folder.getRoot(), "game-logcat.txt");
        ProcessExit.End end = new ProcessExit.End(ApplicationExitInfo.REASON_SIGNALED, 6, null, false, 4242, DIED);
        ProcessExit.keepLogcat(end, file, (pid, out) -> {
            throw new IOException("logcat exited with 1");
        });
        assertEquals("The logcat of process 4242 cannot be read: logcat exited with 1\n", read(file));
    }

    @Test
    public void theCommandDumpsTheMainAndCrashBuffersOfTheProcess() {
        assertEquals(Arrays.asList("logcat", "-d", "-b", "main", "-b", "crash", "-v", "threadtime", "--pid=4242"),
            ProcessExit.logcatCommand(4242));
    }

    @Test
    public void theTailIsTheLastLines() throws IOException {
        StringWriter out = new StringWriter();
        ProcessExit.tail(new BufferedReader(new StringReader("a\nb\nc\n")), 2, out);
        assertEquals("b\nc\n", out.toString());
        out = new StringWriter();
        ProcessExit.tail(new BufferedReader(new StringReader("a\nb")), 5, out);
        assertEquals("fewer lines than asked are all of them", "a\nb\n", out.toString());
        out = new StringWriter();
        ProcessExit.tail(new BufferedReader(new StringReader("")), 5, out);
        assertEquals("", out.toString());
    }

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
