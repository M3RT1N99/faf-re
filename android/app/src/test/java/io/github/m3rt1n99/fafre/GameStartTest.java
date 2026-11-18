package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
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
 * A start of the game by a launcher, the Game card or a test: the command line's order, the game version, and each
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
            failure(() -> arguments(false, null, new String[] {"/init", "x"})));
        String[][] expected = {
            {"NO_SCFA", "no Supreme Commander: Forged Alliance imported yet"},
            {"NO_FONTS", "no fonts of Supreme Commander: Forged Alliance yet: import its folder again, which takes fonts/ too"},
            {"NO_GAME_FILES", "no whole FAF game files yet"},
            {"NO_GAME_FILES", "no whole FAF game files of version 3841 yet"},
            {"INIT_FILE", "the init file: no room left"},
            {"ENVIRONMENT", "the program's environment: setenv failed: EINVAL (Invalid argument)"},
            {"STORAGE", "shared storage is not available"},
            {"ARGUMENTS", "LAUNCHER_ARGUMENTS has /init, which the activity gives the program itself"},
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
        assertEquals(Arrays.asList("NO_SCFA", "NO_FONTS", "NO_GAME_FILES", "INIT_FILE", "ENVIRONMENT", "STORAGE", "ARGUMENTS"), names);
    }

    /** The lines GameActivity gave the report's end and its toast before the codes (fb096228e), with the same details. */
    @Test
    public void theTestAppsLinesAreTheOnesBeforeTheCodes() throws IOException {
        Storage storage = storage();
        assertEquals("FAIL: no Supreme Commander: Forged Alliance imported yet (card 2)", GameStart.Failure.noScfa().cardLine());
        assertEquals("FAIL: no fonts of Supreme Commander: Forged Alliance yet: import its folder again (card 2), which takes fonts/ too",
            GameStart.Failure.noFonts().cardLine());
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
