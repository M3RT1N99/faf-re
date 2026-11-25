package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Locale;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

/**
 * What the report's END line quotes of the engine's log (GameReport.engineSummary): the Vulkan renderer's failure or a
 * warning from the run's end alone, the log's last entries, and nothing from before them. And what a new game makes of
 * the last one's files (GameReport.keepAsPrevious).
 */
public class GameReportTest {
    @Rule
    public final TemporaryFolder folder = new TemporaryFolder();

    /** The log's start on a phone: Direct3D 12 tried first, Vulkan up, a probe of the first frames. */
    private static final List<String> START = Arrays.asList(
        "info: graphics_api option: (unset)",
        "info: renderer: trying Direct3D 12",
        "warning: GAL Exception: file : c:\\work\\rts\\main\\code\\src\\libs\\gpggal\\Device.cpp(135)",
        "         error: unknown API requested",
        "warning: Direct3D 12 is unavailable; falling back to Vulkan",
        "info: renderer: trying Vulkan",
        "info: Vulkan: device created on 'Adreno (TM) 730'",
        "warning: [COMPDIAG] RenderCompositeTerrain head=0 drewNormals=1 target=0x7af3816071c0 shadowCtx=0x7b04e1a07838");

    /** {@code count} entries of a game that goes on, as FAF's UI logs them every 11 s or so. */
    private static List<String> playing(int count) {
        List<String> lines = new ArrayList<>();
        for (int i = 0; i < count; ++i) {
            lines.add(String.format(Locale.ROOT, "debug: Session time: 00:%02d:%02d\tGame time: 00:00:%02d\tHeap: 87.05M / 87.05M",
                i / 5, i % 5 * 11, i % 60));
        }
        return lines;
    }

    @SafeVarargs
    private static String[] log(List<String>... parts) {
        List<String> lines = new ArrayList<>();
        for (List<String> part : parts) {
            lines.addAll(part);
        }
        return lines.toArray(new String[0]);
    }

    @Test
    public void aWarningOfTheFirstFramesSaysNothingOfAnEndAfterThem() {
        assertEquals("", GameReport.engineSummary(log(START, playing(GameReport.END_OF_RUN_ENTRIES))));
        assertEquals("the end's last entries hold it", "the engine's last warning: [COMPDIAG] RenderCompositeTerrain head=0 "
                + "drewNormals=1 target=0x7af3816071c0 shadowCtx=0x7b04e1a07838",
            GameReport.engineSummary(log(START, playing(GameReport.END_OF_RUN_ENTRIES - 1))));
    }

    @Test
    public void theEndsLastWarningComesWithTheLineThatGoesOnWithIt() {
        assertEquals("the engine's last warning: Vulkan: acquiring head 0's image failed (VK_ERROR_SURFACE_LOST_KHR)",
            GameReport.engineSummary(log(START, playing(30),
                Collections.singletonList("warning: Vulkan: acquiring head 0's image failed (VK_ERROR_SURFACE_LOST_KHR)"),
                Collections.singletonList("info: Minimized true"), playing(3))));
        assertEquals("the engine's last warning: Error running lua command: import.lua(164): Error importing '/lua/x.lua' "
                + "stack traceback:",
            GameReport.engineSummary(log(START, playing(30), Arrays.asList(
                "warning: Error running lua command: import.lua(164): Error importing '/lua/x.lua'",
                "         stack traceback:",
                "         [C]: in function `error'"), playing(2))));
    }

    @Test
    public void anEntrysIndentedLinesAreOfTheEntry() {
        // The warning is the 10th entry from the log's end, with more lines after it than that.
        List<String> traceback = new ArrayList<>(Collections.singletonList("warning: Error running '/lua/user/prefs.lua:GetOption'"));
        for (int i = 0; i < 2 * GameReport.END_OF_RUN_ENTRIES; ++i) {
            traceback.add("         [string \"x\"](" + i + "): in main chunk");
        }
        traceback.add("");
        assertEquals("the engine's last warning: Error running '/lua/user/prefs.lua:GetOption' [string \"x\"](0): in main chunk",
            GameReport.engineSummary(log(START, playing(30), traceback, playing(GameReport.END_OF_RUN_ENTRIES - 1))));
    }

    @Test
    public void aVulkanRendererThatCouldNotStartIsTheEnd() {
        assertEquals("Vulkan: no device with Vulkan 1.1", GameReport.engineSummary(log(Arrays.asList(
            "info: renderer: trying Direct3D 12",
            "warning: GAL Exception: file : c:\\work\\rts\\main\\code\\src\\libs\\gpggal\\Device.cpp(135)",
            "         error: unknown API requested",
            "warning: Direct3D 12 is unavailable; falling back to Vulkan",
            "info: renderer: trying Vulkan",
            "warning: GAL Exception: file : DeviceVulkan.cpp(812)",
            "         error: no device with Vulkan 1.1",
            "warning: GAL Exception: file : c:\\work\\rts\\main\\code\\src\\libs\\gpggal\\Device.cpp(135)",
            "         error: unknown API requested"))));
    }

    @Test
    public void aVulkanFailureTheGameWentOnFromIsNotTheEnds() {
        assertEquals("", GameReport.engineSummary(log(Arrays.asList(
            "info: renderer: trying Vulkan",
            "warning: GAL Exception: file : DeviceVulkan.cpp(812)",
            "         error: no device with Vulkan 1.1",
            "warning: Vulkan is unavailable; falling back to Direct3D 11",
            "info: renderer: trying Direct3D 11"), playing(GameReport.END_OF_RUN_ENTRIES))));
    }

    @Test
    public void noLogSaysNothing() {
        assertEquals("", GameReport.engineSummary(new String[0]));
        assertEquals("", GameReport.engineSummary(log(playing(3))));
    }

    @Test
    public void thePlainEndKeepsTheSignalAndDropsTheTimeAndAndroidsWords() {
        assertEquals("CRASHED: killed by a signal, status 6 (SIGABRT)", GameReport.plainEnd("game: started 2026-11-24T18:29:00Z\n"
            + "END: CRASHED: killed by a signal, status 6 (SIGABRT), at 2026-11-24T18:31:02.417Z; the engine's last warning: x\n"));
        assertEquals("CRASHED: native crash, status 11 (SIGSEGV)", GameReport.plainEnd("END: CRASHED: native crash, status 11 "
            + "(SIGSEGV), at 2026-11-24T18:31:02.417Z, crash in /data/app/libfafgame.so (tombstone in the results)\n"));
        assertEquals("an earlier release's line", "CRASHED: native crash, status 11",
            GameReport.plainEnd("END: CRASHED: native crash, status 11, crash in /data/app/libfafgame.so\n"));
        assertEquals("the process ended: low memory, status 9", GameReport.plainEnd(
            "END: the process ended: low memory, status 9, at 2026-11-24T18:31:02.417Z, lmkd killed it\n"));
    }

    @Test
    public void aNewGameKeepsTheLastOnesReportTombstoneAndLogcatAsThePreviousOnes() throws IOException {
        File files = folder.getRoot();
        write(new File(files, "game.txt"), "END: CRASHED: killed by a signal, status 6 (SIGABRT)");
        write(new File(files, GameReport.TOMBSTONE), "tombstone");
        write(new File(files, GameReport.LOGCAT), "logcat");
        write(new File(files, GameReport.PREVIOUS_LOGCAT), "the game before's logcat");
        GameReport.keepAsPrevious(files);
        assertEquals("END: CRASHED: killed by a signal, status 6 (SIGABRT)", read(new File(files, GameReport.PREVIOUS)));
        assertEquals("tombstone", read(new File(files, GameReport.PREVIOUS_TOMBSTONE)));
        assertEquals("logcat", read(new File(files, GameReport.PREVIOUS_LOGCAT)));
        for (String now : new String[] {"game.txt", GameReport.TOMBSTONE, GameReport.LOGCAT}) {
            assertFalse(now, new File(files, now).exists());
        }

        // A game without a logcat leaves none of the game before it.
        write(new File(files, "game.txt"), "END: the program returned 0 after 84.4 s");
        GameReport.keepAsPrevious(files);
        assertFalse(new File(files, GameReport.PREVIOUS_LOGCAT).exists());
    }

    private static void write(File file, String text) throws IOException {
        Files.write(file.toPath(), text.getBytes(StandardCharsets.UTF_8));
    }

    private static String read(File file) throws IOException {
        return new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
    }
}
