package io.github.m3rt1n99.fafre;

import java.security.SecureRandom;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Random;

/**
 * The one-time tokens a launcher's start carries (LaunchGameActivity, the extra TOKEN): StatusProvider's call "play_token"
 * gives one to an allowed app, as a provider sees the app that asks it, and the start spends it. Android names the start's
 * caller only as the app its result goes to (getCallingPackage), which an app the client started for a result can borrow by
 * forwarding PLAY (FLAG_ACTIVITY_FORWARD_RESULT); the token says that an allowed app gave the start. The provider and the
 * trampoline both run in the app's main process, which holds {@link #APP}. No Android in here, for the tests.
 */
final class PlayTokens {
    /** How long a token stays good after its call: the launcher asks for one right before each start. */
    static final long LIFETIME_MS = 60_000;
    /** The tokens given out that wait for their start at most: a further call drops the oldest. */
    static final int MOST = 8;

    /** The app's main process's, where its provider gives them out and its trampoline spends them. */
    static final PlayTokens APP = new PlayTokens(new SecureRandom());

    private final Random mRandom;
    /** Each token given out and not spent, with the time of its call, oldest first. */
    private final Map<String, Long> mIssued = new LinkedHashMap<>();

    PlayTokens(Random random) {
        mRandom = random;
    }

    /** A new token, 128 random bits in lower-case hex, given out at {@code now} (ms of a clock that only goes forward). */
    synchronized String issue(long now) {
        byte[] bits = new byte[16];
        mRandom.nextBytes(bits);
        String token = Callers.hex(bits);
        if (mIssued.size() == MOST) {
            Iterator<String> oldest = mIssued.keySet().iterator();
            oldest.next();
            oldest.remove();
        }
        mIssued.put(token, now);
        return token;
    }

    /**
     * Whether {@code token}, the start's extra as it came (null without one), is one given out at most LIFETIME_MS before
     * {@code now} and not spent yet: it is spent then, for good.
     */
    synchronized boolean spend(Object token, long now) {
        if (!(token instanceof String)) {
            return false;
        }
        Long issued = mIssued.remove(token);
        return issued != null && now - issued <= LIFETIME_MS;
    }
}
