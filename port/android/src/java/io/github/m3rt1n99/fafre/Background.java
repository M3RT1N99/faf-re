package io.github.m3rt1n99.fafre;

import android.os.Handler;
import android.os.Looper;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Runs the launcher's file work (status checks, fa_path.lua, log reads) on one
 * background thread and hands the result back on the main thread. One thread
 * keeps the operations ordered (a "Start" never races a status refresh) and is
 * plenty for work measured in milliseconds; long jobs belong to ImportService.
 */
final class Background {
    interface Task<T> {
        T run() throws Exception;
    }

    interface Callback<T> {
        /** Exactly one of value/error is meaningful; called on the main thread. */
        void done(T value, Exception error);
    }

    private static final ExecutorService IO = Executors.newSingleThreadExecutor(runnable -> {
        Thread thread = new Thread(runnable, "fafre-io");
        thread.setDaemon(true);
        return thread;
    });
    private static final Handler MAIN = new Handler(Looper.getMainLooper());

    private Background() {
    }

    static <T> void run(final Task<T> task, final Callback<T> callback) {
        IO.execute(() -> {
            T value = null;
            Exception error = null;
            try {
                value = task.run();
            } catch (Exception e) {
                error = e;
            }
            final T result = value;
            final Exception failure = error;
            MAIN.post(() -> callback.done(result, failure));
        });
    }

    static void main(Runnable runnable) {
        MAIN.post(runnable);
    }
}
