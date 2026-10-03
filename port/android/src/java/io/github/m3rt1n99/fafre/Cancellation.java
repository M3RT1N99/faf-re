package io.github.m3rt1n99.fafre;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/**
 * Cooperative cancellation for one import job. Workers poll it between buffer
 * copies; blocking network reads are interrupted by the hooks registered with
 * {@link #onCancel} (they disconnect the HttpURLConnection), because a thread
 * stuck in read() would otherwise only notice after the socket timeout.
 *
 * <p>Pure Java (no Android APIs) so it can be exercised on a host JVM.
 */
final class Cancellation {
    /** Thrown out of IO loops when the job was cancelled; an IOException so it unwinds the same paths. */
    static final class CancelledException extends IOException {
        private static final long serialVersionUID = 1L;

        CancelledException(String reason) {
            super(reason);
        }
    }

    /** Removes a hook registered with {@link #onCancel}. */
    interface Registration extends AutoCloseable {
        @Override
        void close();
    }

    private final Object mLock = new Object();
    private final List<Runnable> mHooks = new ArrayList<>();
    private String mReason;

    void cancel(String reason) {
        List<Runnable> hooks;
        synchronized (mLock) {
            if (mReason != null) {
                return;
            }
            mReason = reason;
            hooks = new ArrayList<>(mHooks);
            mLock.notifyAll();
        }
        for (Runnable hook : hooks) {
            try {
                hook.run();
            } catch (RuntimeException ignored) {
                // A hook that fails to abort IO only delays the cancel until the next poll.
            }
        }
    }

    boolean isCancelled() {
        synchronized (mLock) {
            return mReason != null;
        }
    }

    String reason() {
        synchronized (mLock) {
            return mReason;
        }
    }

    void throwIfCancelled() throws CancelledException {
        String reason = reason();
        if (reason != null) {
            throw new CancelledException(reason);
        }
    }

    /** Runs {@code hook} on cancel (immediately if already cancelled) until the registration is closed. */
    Registration onCancel(final Runnable hook) {
        boolean runNow;
        synchronized (mLock) {
            runNow = mReason != null;
            if (!runNow) {
                mHooks.add(hook);
            }
        }
        if (runNow) {
            hook.run();
        }
        return () -> {
            synchronized (mLock) {
                mHooks.remove(hook);
            }
        };
    }

    /** Sleeps for a retry backoff, waking up early (with an exception) on cancel. */
    void sleep(long millis) throws CancelledException {
        long deadline = System.currentTimeMillis() + millis;
        synchronized (mLock) {
            while (mReason == null) {
                long left = deadline - System.currentTimeMillis();
                if (left <= 0) {
                    return;
                }
                try {
                    mLock.wait(left);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    throw new CancelledException("interrupted");
                }
            }
        }
        throwIfCancelled();
    }
}
