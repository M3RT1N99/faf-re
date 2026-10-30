package io.github.m3rt1n99.fafre;

import java.io.IOException;

/** The user's Cancel: every long step checks it and stops with {@link Cancelled}. */
final class Cancellation {
    static final class Cancelled extends IOException {
        Cancelled() {
            super("cancelled");
        }
    }

    private static volatile boolean sRequested;

    private Cancellation() {
    }

    static void reset() {
        sRequested = false;
    }

    static void request() {
        sRequested = true;
    }

    static boolean requested() {
        return sRequested;
    }

    static void check() throws Cancelled {
        if (sRequested) {
            throw new Cancelled();
        }
    }
}
