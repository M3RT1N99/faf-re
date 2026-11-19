package io.github.m3rt1n99.fafre;

/**
 * The touch scheme Griff's rail (buildstage/touch-concept-v2.md; Griff v4): Box and Plus, as TouchInput's RAIL_ bits. A tap
 * lights a button for the next action, which puts it out, as does a second tap; held, it counts until it goes up, and a moment
 * after, as a thumb and a finger often lift together. A press that an action used lights nothing; a press that was no tap (a
 * hold, a cancel, a finger that slid off) leaves the light as it was, unless the hold used it.
 *
 * <p>What a finger's tap or rest uses: the buttons that counted for it, whatever it turned out to be, Box as it counted when
 * the finger came down (TouchInput). Box only where it took one's own unit into the selection or out of it: through a tap on
 * FAF's UI, an armed command mode's tap or a rest's right click on a panel Box stays lit while Plus goes out, and Box's tap on
 * nothing at all is no action and uses none (the mockup's). Without Box the activity puts them out at once; with Box the
 * program says what it used (Game.SAID_TAP, its rule in include/GriffTap.h). Of the other actions a box on the map uses Box
 * and Plus, a formation Plus, the draft's box over deposits Box, a route Plus, the draft's tick Plus; a pan, the handle's tap
 * and its Distribute orders use none, and in the draft its plans and its taps, rests and slides on FAF's UI none, so that a lit
 * Plus stays for the tick (a held one is Shift for them as ever).
 *
 * <p>FAF's UI draws it (griffhud.lua: FAF's order buttons at the HUD's factor, at the top left, the user's decision of
 * 2026-11-13), as each button looks ({@link #look}); the activity takes the fingers on it. FAF's orders panel has the
 * attack-move, so the rail has no Attack.
 */
final class Rail {
    /** A press shorter than this, which no action used and which stayed on the button, is a tap. */
    static final long TAP_MILLISECONDS = 300;
    /** How long a held button still counts after it went up. */
    static final long LIFT_MILLISECONDS = 150;

    /** How a button looks ({@link #look}): idle, held, or lit for the next action. */
    static final int IDLE = 0;
    static final int HELD = 1;
    static final int LIT = 2;

    private static final int BUTTONS = 2;

    private static final class Button {
        boolean held;
        boolean lit;
        boolean litBefore;
        boolean used;
        long pressedAt;
        long upAt = Long.MIN_VALUE / 2;
    }

    private final Button[] mButtons = new Button[BUTTONS];

    Rail() {
        for (int at = 0; at < BUTTONS; ++at) {
            mButtons[at] = new Button();
        }
    }

    private Button button(int bit) {
        return mButtons[Integer.numberOfTrailingZeros(bit)];
    }

    /** `bit`: one of TouchInput's RAIL_ bits. */
    void press(int bit, long time) {
        Button button = button(bit);
        button.held = true;
        button.pressedAt = time;
        button.litBefore = button.lit;
        button.lit = false;
        button.used = false;
    }

    /** `stayed`: the finger went up on the button, without having moved away. */
    void release(int bit, long time, boolean stayed) {
        Button button = button(bit);
        if (!button.held) {
            return;
        }
        button.held = false;
        if (stayed && !button.used && time - button.pressedAt < TAP_MILLISECONDS) {
            // A tap: it held nothing, so no moment after it counts.
            button.lit = !button.litBefore;
            button.upAt = Long.MIN_VALUE / 2;
        } else {
            button.lit = button.litBefore && !button.used;
            button.upAt = time;
        }
    }

    /** The system took the finger: no tap, and no lift whose moment after counts. */
    void cancel(int bit) {
        Button button = button(bit);
        if (!button.held) {
            return;
        }
        button.held = false;
        button.lit = button.litBefore && !button.used;
        button.upAt = Long.MIN_VALUE / 2;
    }

    /** The RAIL_ bits that count for an action now. */
    int counting(long time) {
        int bits = 0;
        for (int at = 0; at < BUTTONS; ++at) {
            Button button = mButtons[at];
            if (button.held || button.lit || time - button.upAt < LIFT_MILLISECONDS) {
                bits |= 1 << at;
            }
        }
        return bits;
    }

    /** The RAIL_ bits held or lit, as the handle shows them. */
    int shown() {
        int bits = 0;
        for (int at = 0; at < BUTTONS; ++at) {
            if (mButtons[at].held || mButtons[at].lit) {
                bits |= 1 << at;
            }
        }
        return bits;
    }

    /**
     * How the button of `bit` looks: held while a finger is on it, also where it was lit before (the finger's press may yet
     * put it out); lit while it waits for the next action; else idle, also in the moment after a lift, which counts unseen.
     */
    int look(int bit) {
        Button button = button(bit);
        return button.held ? HELD : button.lit ? LIT : IDLE;
    }

    boolean held(int bit) {
        return button(bit).held;
    }

    /** All out, as if none had been touched: the rail is put away. */
    void clear() {
        for (int at = 0; at < BUTTONS; ++at) {
            mButtons[at] = new Button();
        }
    }

    /** An action used these: a held one counts as used, a lit one goes out, and the moment after a lift is spent. */
    void used(int bits) {
        for (int at = 0; at < BUTTONS; ++at) {
            if ((bits & 1 << at) == 0) {
                continue;
            }
            Button button = mButtons[at];
            if (button.held) {
                button.used = true;
            }
            button.lit = false;
            button.upAt = Long.MIN_VALUE / 2;
        }
    }
}
