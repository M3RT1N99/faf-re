package io.github.m3rt1n99.fafre;

/**
 * The touch scheme Griff's rail (buildstage/touch-concept-v2.md): Box, Plus and Attack at the screen's edge, as TouchInput's
 * RAIL_ bits. A tap lights a button for the next action, which puts it out, as does a second tap; held, it counts until it
 * goes up, and a moment after, as a thumb and a finger often lift together. A press that an action used lights nothing; a
 * press that was no tap (a hold, a cancel, a finger that slid off) leaves the light as it was, unless the hold used it.
 */
final class Rail {
    /** A press shorter than this, which no action used and which stayed on the button, is a tap. */
    static final long TAP_MILLISECONDS = 300;
    /** How long a held button still counts after it went up. */
    static final long LIFT_MILLISECONDS = 150;

    private static final int BUTTONS = 3;

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

    /** The RAIL_ bits held or lit, as the rail shows them. */
    int shown() {
        int bits = 0;
        for (int at = 0; at < BUTTONS; ++at) {
            if (mButtons[at].held || mButtons[at].lit) {
                bits |= 1 << at;
            }
        }
        return bits;
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
