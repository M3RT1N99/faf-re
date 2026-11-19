package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;

import java.util.Collections;
import java.util.EnumSet;
import java.util.HashMap;
import java.util.Map;

import org.junit.Test;

/** SetupActivity's contract (Setup): the steps its one extra names, and what else a caller's start gets refused for. */
public class SetupTest {
    private static Map<String, Object> extras(Object... nameValue) {
        Map<String, Object> extras = new HashMap<>();
        for (int i = 0; i < nameValue.length; i += 2) {
            extras.put((String) nameValue[i], nameValue[i + 1]);
        }
        return extras;
    }

    @Test
    public void theStepNamesWhatTheScreenShows() throws GameStart.Failure {
        assertEquals("io.github.m3rt1n99.fafre.extra.STEP", Setup.EXTRA_STEP);
        assertEquals(EnumSet.of(Setup.Step.SCFA), Setup.steps(extras(Setup.EXTRA_STEP, "scfa")));
        assertEquals(EnumSet.of(Setup.Step.GAME_FILES), Setup.steps(extras(Setup.EXTRA_STEP, "game_files")));
    }

    @Test
    public void noStepShowsBoth() throws GameStart.Failure {
        assertEquals(EnumSet.allOf(Setup.Step.class), Setup.steps(Collections.emptyMap()));
        // Extras of other names, Android's or the caller's own, do not count.
        assertEquals(EnumSet.allOf(Setup.Step.class), Setup.steps(extras("android.intent.extra.REFERRER", "x", "step", 3)));
    }

    @Test
    public void anotherStepOrTypeIsRefused() {
        assertArguments("STEP is \"SCFA\", not scfa or game_files", extras(Setup.EXTRA_STEP, "SCFA"));
        assertArguments("STEP is \"\", not scfa or game_files", extras(Setup.EXTRA_STEP, ""));
        assertArguments("STEP is of the type Integer, not scfa or game_files", extras(Setup.EXTRA_STEP, 1));
        assertArguments("STEP is of the type String[], not scfa or game_files",
            extras(Setup.EXTRA_STEP, new String[] {"scfa"}));
    }

    @Test
    public void anotherExtraOfTheContractIsRefused() {
        // The setup fetches FAF's newest version alone: a start's version would go unheard.
        assertArguments("the setup has the extra io.github.m3rt1n99.fafre.extra.GAME_VERSION, which its contract has not: "
            + "STEP alone", extras(Setup.EXTRA_STEP, "game_files", GameStart.EXTRA_GAME_VERSION, 3845));
    }

    private static void assertArguments(String message, Map<String, Object> extras) {
        GameStart.Failure failure = assertThrows(GameStart.Failure.class, () -> Setup.steps(extras));
        assertEquals(GameStart.Code.ARGUMENTS, failure.code);
        assertEquals(message, failure.getMessage());
    }
}
