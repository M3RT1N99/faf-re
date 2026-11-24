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
    /**
     * A stop that is no Cancel, after which the step's caller goes on: a start's download whose network became metered
     * (GameFilesFetch). The user's Cancel stays apart from it, so that a Back in the same moment still ends the start.
     */
    private static volatile boolean sStopped;

    private Cancellation() {
    }

    static void reset() {
        sRequested = false;
        sStopped = false;
    }

    static void request() {
        sRequested = true;
    }

    /** Stops the long step as the user's Cancel does, which {@link #requested} does not count as one. */
    static void stop() {
        sStopped = true;
    }

    /** Once the caller has handled a stop: the next step runs, unless the user's Cancel came meanwhile. */
    static void clearStop() {
        sStopped = false;
    }

    /** The user's Cancel, not a stop. */
    static boolean requested() {
        return sRequested;
    }

    static void check() throws Cancelled {
        if (sRequested || sStopped) {
            throw new Cancelled();
        }
    }
}
