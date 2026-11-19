package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.EnumSet;
import java.util.List;
import java.util.Locale;

import org.junit.After;
import org.junit.Rule;
import org.junit.Test;
import org.junit.function.ThrowingRunnable;
import org.junit.rules.TemporaryFolder;

/**
 * A start of the game by a launcher, the Game card or a test: the command line's order, the game version, when a launcher's
 * start runs no program (beside the running one, and in Android's restore of a game whose process ended), and each
 * failure's code and line, which the activity's result gives a launcher (am start reads no result, so these are the
 * result's oracle), and the test app's wording, which its cards keep.
 */
public class GameStartTest {
    /** Relative, so that only the switches start with a slash. */
    private static final File GAME = new File("parity/game/3841");
    private static final File INIT = new File("parity/game/3841/bin/init_game.lua");
    private static final File LOG = new File("game/engine.sclog");
    private static final String PROGRAM = new File(GAME, GameStart.PROGRAM).getPath();

    @Rule
    public final TemporaryFolder folder = new TemporaryFolder();

    @After
    public void forgetTheReport() {
        TestState.game("");
    }

    private static List<String> arguments(boolean noMovie, String test, Object launcher) throws GameStart.Failure {
        return GameStart.arguments(GAME, INIT, LOG, "1.67", "2.00", noMovie, test, launcher);
    }

    private static GameStart.Failure failure(ThrowingRunnable start) {
        return assertThrows(GameStart.Failure.class, start);
    }

    private static void assertFailure(GameStart.Code code, String message, ThrowingRunnable start) {
        GameStart.Failure failure = failure(start);
        assertEquals(code, failure.code);
        assertEquals(message, failure.getMessage());
    }

    private Storage storage(int... ready) throws IOException {
        Storage storage = new Storage(folder.getRoot());
        for (int version : ready) {
            GameFilesTest.makeReady(storage, version);
        }
        return storage;
    }

    @Test
    public void aStartWithoutExtrasHasTheProgramTheActivitysArgumentsAndTheDisplayModeLast() throws GameStart.Failure {
        assertEquals(Arrays.asList(PROGRAM, "/init", INIT.getPath(), "/log", LOG.getPath(), "/notelemetry",
            "/fafre_hud_scale", "1.67", "/fafre_hud_most", "2.00", "/fullscreen", "1707", "768"),
            Arrays.asList(GameStart.commandLine(arguments(false, null, null), 1707, 768)));
    }

    @Test
    public void theLaunchersArgumentsComeAfterTheActivitysAndBeforeTheDisplayModeAndAreNotSplit() throws GameStart.Failure {
        String[] launcher = {"/gpgnet", "127.0.0.1:7237", "/nativenet", "/numgames", "3", "/clan", "a b",
            "/savereplay", "gpgnet://127.0.0.1:7237/42/a b.SCFAreplay"};
        assertEquals(Arrays.asList(PROGRAM, "/init", INIT.getPath(), "/log", LOG.getPath(), "/notelemetry",
            "/fafre_hud_scale", "1.67", "/fafre_hud_most", "2.00", "/nomovie", "/spewsound",
            "/gpgnet", "127.0.0.1:7237", "/nativenet", "/numgames", "3", "/clan", "a b",
            "/savereplay", "gpgnet://127.0.0.1:7237/42/a b.SCFAreplay", "/fullscreen", "1707", "768"),
            Arrays.asList(GameStart.commandLine(arguments(true, " /spewsound ", launcher), 1707, 768)));
        assertEquals("no arguments add nothing", arguments(false, null, null), arguments(false, null, new String[0]));
    }

    @Test
    public void aTestsArgumentsStayOneStringSplitAtSpacesAndMayHoldAnySwitch() throws GameStart.Failure {
        List<String> own = arguments(false, null, null);
        List<String> expected = new ArrayList<>(own);
        expected.addAll(Arrays.asList("/spewsound", "/nomovie", "x"));
        assertEquals(expected, arguments(false, "  /spewsound \t /nomovie  x ", null));
        assertEquals(own, arguments(false, "   ", null));
    }

    @Test
    public void eachOfTheActivitysSwitchesAmongTheLaunchersArgumentsFailsTheStart() throws GameStart.Failure {
        List<String> switches = new ArrayList<>();
        for (String argument : GameStart.commandLine(arguments(true, null, null), 1707, 768)) {
            if (argument.startsWith("/")) {
                switches.add(argument);
            }
        }
        assertEquals("the command line's own, the contract's five among them",
            Arrays.asList("/init", "/log", "/notelemetry", "/fafre_hud_scale", "/fafre_hud_most", "/nomovie", "/fullscreen"), switches);
        assertEquals(switches, GameStart.OWN_SWITCHES);
        for (String own : switches) {
            // The engine finds a switch in any case, also where it would be another switch's value.
            for (String given : new String[] {own, own.toUpperCase(Locale.ROOT)}) {
                assertFailure(GameStart.Code.ARGUMENTS, "LAUNCHER_ARGUMENTS has " + given + ", which the activity gives the program itself",
                    () -> arguments(false, null, new String[] {"/gpgnet", "127.0.0.1:7237", "/country", given}));
            }
        }
    }

    @Test
    public void launchersArgumentsOfAnotherTypeOrWithANullFailTheStart() {
        assertFailure(GameStart.Code.ARGUMENTS, "LAUNCHER_ARGUMENTS has the type String, not String[]",
            () -> arguments(false, null, "/gpgnet 127.0.0.1:7237"));
        assertFailure(GameStart.Code.ARGUMENTS, "LAUNCHER_ARGUMENTS has the type ArrayList, not String[]",
            () -> arguments(false, null, new ArrayList<>(Collections.singletonList("/gpgnet"))));
        assertFailure(GameStart.Code.ARGUMENTS, "LAUNCHER_ARGUMENTS has null at index 1",
            () -> arguments(false, null, new String[] {"/gpgnet", null}));
    }

    @Test
    public void aReadyVersionTheStartAsksForIsTakenAlsoWhenItIsNotTheNewest() throws Exception {
        Storage storage = storage(3835, 3841);
        assertEquals(3835, GameStart.version(storage, 3835));
        assertEquals(3841, GameStart.version(storage, 3841));
    }

    @Test
    public void aVersionThatIsNotReadyFailsWithNoGameFilesAndNoOtherIsTaken() throws Exception {
        Storage storage = storage(3841);
        // A download that stopped: the version's folder is there, its init file is not (GameFiles' ready check).
        GameFiles.write(new File(storage.game(3840), "manifest.json"), "{\"pack_format\": " + GameFiles.PACK_FORMAT + "}");
        for (int asked : new int[] {3840, 3842, 0, -1}) {
            assertFailure(GameStart.Code.NO_GAME_FILES, "no whole FAF game files of version " + asked + " yet",
                () -> GameStart.version(storage, asked));
        }
    }

    @Test
    public void withoutTheExtraTheNewestReadyVersionIsTakenAsBefore() throws Exception {
        Storage storage = storage();
        assertFailure(GameStart.Code.NO_GAME_FILES, "no whole FAF game files yet", () -> GameStart.version(storage, null));
        GameFilesTest.makeReady(storage, 3835);
        GameFilesTest.makeReady(storage, 3841);
        assertEquals(3841, GameStart.version(storage, null));
    }

    @Test
    public void aVersionOfAnotherTypeFailsWithArguments() throws IOException {
        Storage storage = storage(3841);
        assertFailure(GameStart.Code.ARGUMENTS, "GAME_VERSION has the type String, not int", () -> GameStart.version(storage, "3841"));
        assertFailure(GameStart.Code.ARGUMENTS, "GAME_VERSION has the type Long, not int", () -> GameStart.version(storage, 3841L));
    }

    @Test
    public void eachFailureHasItsCodeAndALineThatNamesNoCard() throws IOException {
        Storage storage = storage();
        List<GameStart.Failure> failures = Arrays.asList(GameStart.Failure.noScfa(), GameStart.Failure.noFonts(),
            failure(() -> GameStart.version(storage, null)), failure(() -> GameStart.version(storage, 3841)),
            GameStart.Failure.initFile("no room left"), GameStart.Failure.environment("setenv failed: EINVAL (Invalid argument)"),
            GameStart.Failure.storage("shared storage is not available"),
            failure(() -> arguments(false, null, new String[] {"/init", "x"})),
            GameStart.refusal(true, false, 3841, null), GameStart.refusal(false, true, null, new String[0]),
            GameStart.Failure.caller(), GameStart.Failure.token(),
            GameStart.fetchFailure(new Http.StatusException(Http.MAP_URL + "x.zip", 404), GameStart.Code.NO_MAP, "the map x"),
            GameStart.fetchFailure(new Vault.Missing("y.zip holds the mod 1234abcd, not 5678abcd"), GameStart.Code.NO_MOD,
                "the mod 5678abcd"),
            GameStart.fetchFailure(new IOException("Connection reset"), GameStart.Code.NO_MAP, "the map x"),
            GameStart.Failure.replay("it is empty"), GameStart.Failure.cancelled());
        String[][] expected = {
            {"NO_SCFA", "no Supreme Commander: Forged Alliance imported yet"},
            {"NO_FONTS", "no fonts of Supreme Commander: Forged Alliance yet: import its folder or a ZIP of it again, which takes "
                + "fonts/ too"},
            {"NO_GAME_FILES", "no whole FAF game files yet"},
            {"NO_GAME_FILES", "no whole FAF game files of version 3841 yet"},
            {"INIT_FILE", "the init file: no room left"},
            {"ENVIRONMENT", "the program's environment: setenv failed: EINVAL (Invalid argument)"},
            {"STORAGE", "shared storage is not available"},
            {"ARGUMENTS", "LAUNCHER_ARGUMENTS has /init, which the activity gives the program itself"},
            {"RUNNING", "a game runs already, which this start's version and arguments cannot reach: end it first"},
            {"PROCESS_ENDED", "the game's process ended while the game was in the back (Android ended it, it crashed, or the "
                + "program ended): its game is gone, and the activity Android brought back starts none with the launcher's arguments"},
            {"CALLER", "the app that started the game is not one it allows: start it for a result, from an app signed with a key "
                + "on the game's allowlist"},
            {"CALLER", "the start has no token the game gave out, or one spent or older than 60 s: ask its status provider for one "
                + "(play_token) right before each start"},
            {"NO_MAP", "the map x: HTTP 404 for https://content.faforever.com/maps/x.zip"},
            {"NO_MOD", "the mod 5678abcd: y.zip holds the mod 1234abcd, not 5678abcd"},
            {"DOWNLOAD", "the map x: Connection reset"},
            {"REPLAY", "the replay: it is empty"},
            {"CANCELLED", "Back ended the start while it fetched what the game needs"},
        };
        EnumSet<GameStart.Code> codes = EnumSet.noneOf(GameStart.Code.class);
        for (int i = 0; i < expected.length; ++i) {
            GameStart.Failure failure = failures.get(i);
            assertEquals(expected[i][0], failure.code.name());
            assertEquals(expected[i][1], failure.getMessage());
            assertFalse(failure.getMessage(), failure.getMessage().toLowerCase(Locale.ROOT).contains("card"));
            codes.add(failure.code);
        }
        assertEquals("each code is here", EnumSet.allOf(GameStart.Code.class), codes);
    }

    @Test
    public void theExtrasTheResultAndItsCodesHaveTheContractsNames() {
        assertEquals("io.github.m3rt1n99.fafre.extra.GAME_VERSION", GameStart.EXTRA_GAME_VERSION);
        assertEquals("io.github.m3rt1n99.fafre.extra.LAUNCHER_ARGUMENTS", GameStart.EXTRA_LAUNCHER_ARGUMENTS);
        assertEquals("failure", GameStart.RESULT_FAILURE);
        assertEquals("message", GameStart.RESULT_MESSAGE);
        List<String> names = new ArrayList<>();
        for (GameStart.Code code : GameStart.Code.values()) {
            names.add(code.name());
        }
        assertEquals(Arrays.asList("NO_SCFA", "NO_FONTS", "NO_GAME_FILES", "INIT_FILE", "ENVIRONMENT", "STORAGE", "ARGUMENTS",
            "RUNNING", "PROCESS_ENDED", "CALLER", "NO_MAP", "NO_MOD", "DOWNLOAD", "REPLAY", "CANCELLED"), names);
    }

    @Test
    public void aLaunchersStartBesideTheRunningProgramOrInAndroidsRestoreOfItsEndedProcessRunsNoProgram() {
        // Either extra makes a launcher's start, of whatever type it came (ARGUMENTS checks the type when a program starts).
        Object[][] launchers = {{3841, null}, {null, new String[] {"/gpgnet", "127.0.0.1:7311"}},
            {3835, new String[] {"/gpgnet", "127.0.0.1:7312"}}, {"3841", null}};
        for (Object[] extras : launchers) {
            // Beside the running program, which its version and arguments would not reach.
            assertEquals(GameStart.Code.RUNNING, GameStart.refusal(true, false, extras[0], extras[1]).code);
            // The activity of a launcher's game whose process ended in the back, which Android brings back: it would start
            // anew with the launcher's arguments, a GPGNet session that the launcher saw end.
            assertEquals(GameStart.Code.PROCESS_ENDED, GameStart.refusal(false, true, extras[0], extras[1]).code);
            // A new start, which prepare checks, and Android's re-creation around the running program, the same game going on.
            assertNull(GameStart.refusal(false, false, extras[0], extras[1]));
            assertNull(GameStart.refusal(true, true, extras[0], extras[1]));
        }
        // A start without them, the Game card's or a test's, goes as before: it starts the program, shows the running one,
        // or starts anew in Android's restore.
        for (boolean programRuns : new boolean[] {false, true}) {
            for (boolean restored : new boolean[] {false, true}) {
                assertNull(GameStart.refusal(programRuns, restored, null, null));
            }
        }
    }

    @Test
    public void aRefusalBesideTheRunningProgramGivesItsReportALineAndNoEnd() {
        GameStart.Failure running = GameStart.refusal(true, false, null, new String[] {"/gpgnet", "127.0.0.1:7312"});
        assertEquals("game: the start failed, RUNNING: a game runs already, which this start's version and arguments cannot "
            + "reach: end it first", running.reportLine());
        assertEquals(running.reportLine(), running.reportLines().get(0));
        // The running program's report, which the activity's refusal gives the line: the Game card still shows it running.
        TestState.game("120 ms: game: FAF game version 3841, 7 SCFA archives\n15713 ms: " + running.reportLine() + "\n");
        assertEquals("Game: running", TestState.gameVerdict());
    }

    /**
     * The lines GameActivity gave the report's end and its toast before the codes (fb096228e), with the same details; card 2
     * imports a ZIP of the folder too since E2c, which the line for no fonts names.
     */
    @Test
    public void theTestAppsLinesAreTheOnesBeforeTheCodes() throws IOException {
        Storage storage = storage();
        assertEquals("FAIL: no Supreme Commander: Forged Alliance imported yet (card 2)", GameStart.Failure.noScfa().cardLine());
        assertEquals("FAIL: no fonts of Supreme Commander: Forged Alliance yet: import its folder or a ZIP of it again (card 2), which "
            + "takes fonts/ too", GameStart.Failure.noFonts().cardLine());
        assertEquals("FAIL: no whole FAF game files yet; \"Get FAF's newest version\" on card 5 downloads them, as does the replay test (card 3)",
            failure(() -> GameStart.version(storage, null)).cardLine());
        assertEquals("FAIL: the init file: no room left", GameStart.Failure.initFile("no room left").cardLine());
        assertEquals("FAIL: the program's environment: setenv failed: EINVAL (Invalid argument)",
            GameStart.Failure.environment("setenv failed: EINVAL (Invalid argument)").cardLine());
        assertEquals("FAIL: shared storage is not available", GameStart.Failure.storage("shared storage is not available").cardLine());
        // The new failures, which no start had before.
        assertEquals("FAIL: no whole FAF game files of version 3841 yet; \"Get FAF's newest version\" on card 5 downloads them, as does "
            + "the replay test (card 3)", failure(() -> GameStart.version(storage, 3841)).cardLine());
        assertEquals("FAIL: LAUNCHER_ARGUMENTS has /log, which the activity gives the program itself",
            failure(() -> arguments(false, null, new String[] {"/log", "x"})).cardLine());
    }

    @Test
    public void theGameCardShowsTheReportsEndAsBeforeAndTheReportHasTheCode() {
        GameStart.Failure failure = GameStart.Failure.noScfa();
        assertEquals(Arrays.asList("game: the start failed, NO_SCFA: no Supreme Commander: Forged Alliance imported yet",
            "END: FAIL: no Supreme Commander: Forged Alliance imported yet (card 2)"), failure.reportLines());
        // A failed start's report, as GameActivity writes it.
        TestState.game(String.join("\n", failure.reportLines()) + "\n");
        assertEquals("Game: FAIL: no Supreme Commander: Forged Alliance imported yet (card 2)", TestState.gameVerdict());
    }

    @Test
    public void theReportsCommandLineQuotesAnArgumentWithASpaceOrNone() {
        assertEquals("game.exe /clan \"a b\" /country \"\" /numgames 3",
            GameStart.inWords(new String[] {"game.exe", "/clan", "a b", "/country", "", "/numgames", "3"}));
    }
}
