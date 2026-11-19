package io.github.m3rt1n99.fafre;

import java.util.EnumSet;
import java.util.Map;
import java.util.Set;

/**
 * SetupActivity's contract (android/README.md, The setup): the action another app, the FAF client, opens it with, and its
 * one extra, the step it shows. No Android in here, for the tests.
 */
final class Setup {
    static final String ACTION = "io.github.m3rt1n99.fafre.action.SETUP_GAME_DATA";
    /** A String, "scfa" or "game_files": the step the screen shows; without it, both. */
    static final String EXTRA_STEP = Launch.EXTRA_PREFIX + "STEP";

    /** What the screen shows. */
    enum Step {
        /** The SCFA import, from its folder or a ZIP of it. */
        SCFA("scfa"),
        /** FAF's newest game files. */
        GAME_FILES("game_files");

        /** STEP's value for it. */
        final String value;

        Step(String value) {
            this.value = value;
        }
    }

    private Setup() {
    }

    /**
     * The steps a start shows, by its extras as the intent has them: the one STEP names, both without it. ARGUMENTS for
     * another value or type, and for another extra of the contract's prefix, as the caller counts on what it does.
     */
    static Set<Step> steps(Map<String, ?> extras) throws GameStart.Failure {
        for (String name : extras.keySet()) {
            if (name.startsWith(Launch.EXTRA_PREFIX) && !name.equals(EXTRA_STEP)) {
                throw GameStart.Failure.arguments("the setup has the extra " + name + ", which its contract has not: STEP alone");
            }
        }
        Object step = extras.get(EXTRA_STEP);
        if (step == null) {
            return EnumSet.allOf(Step.class);
        }
        for (Step each : Step.values()) {
            if (each.value.equals(step)) {
                return EnumSet.of(each);
            }
        }
        throw GameStart.Failure.arguments("STEP is " + (step instanceof String ? "\"" + step + "\""
            : "of the type " + step.getClass().getSimpleName()) + ", not scfa or game_files");
    }
}
