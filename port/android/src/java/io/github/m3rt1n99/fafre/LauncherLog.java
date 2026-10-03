package io.github.m3rt1n99.fafre;

import android.content.Context;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * {@code <root>/logs/launcher.log}: what the launcher and the import service
 * did, next to the runtime's faf_android_*.log so one {@code adb pull} of the
 * logs folder captures both. Lines are also mirrored to logcat (tag "fafre").
 *
 * <p>Writes happen on a dedicated thread so callers on the UI thread never
 * touch the disk. Appends use O_APPEND, so the game process can log here too
 * without interleaving partial lines.
 */
final class LauncherLog implements LogSink {
    static final String TAG = "fafre";
    static final String LOGS_DIR = "logs";
    static final String FILE_NAME = "launcher.log";
    private static final long MAX_BYTES = 1024 * 1024;
    private static final int KEEP_BYTES = 256 * 1024;
    private static final DateTimeFormatter STAMP = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss.SSS");

    private static LauncherLog sInstance;

    private final Context mContext;
    private final ExecutorService mWriter = Executors.newSingleThreadExecutor(runnable -> {
        Thread thread = new Thread(runnable, "fafre-log");
        thread.setDaemon(true);
        return thread;
    });

    private LauncherLog(Context context) {
        mContext = context.getApplicationContext();
    }

    static synchronized LauncherLog get(Context context) {
        if (sInstance == null) {
            sInstance = new LauncherLog(context);
        }
        return sInstance;
    }

    @Override
    public void log(String line) {
        Log.i(TAG, line);
        final String stamped = LocalDateTime.now().format(STAMP) + "  " + line + "\n";
        mWriter.execute(() -> append(stamped));
    }

    /** Truncates launcher.log, ordered after every line logged before. */
    void clear() {
        mWriter.execute(() -> {
            File file = file();
            if (file != null && file.exists() && !file.delete()) {
                Log.w(TAG, "could not delete " + file);
            }
        });
    }

    /** Null when shared storage is unavailable. */
    File file() {
        File root = mContext.getExternalFilesDir(null);
        return root == null ? null : new File(new File(root, LOGS_DIR), FILE_NAME);
    }

    private void append(String text) {
        File file = file();
        if (file == null) {
            return;
        }
        File dir = file.getParentFile();
        if (dir != null && !dir.isDirectory() && !dir.mkdirs()) {
            return;
        }
        try {
            if (file.length() > MAX_BYTES) {
                // Keep the recent part; the log is for diagnosing the last few runs.
                String recent = FileOps.tail(file, KEEP_BYTES);
                FileOps.writeAtomic(file, recent);
            }
            try (FileOutputStream out = new FileOutputStream(file, true)) {
                out.write(text.getBytes(StandardCharsets.UTF_8));
            }
        } catch (IOException e) {
            Log.w(TAG, "launcher.log: " + e.getMessage());
        }
    }
}
