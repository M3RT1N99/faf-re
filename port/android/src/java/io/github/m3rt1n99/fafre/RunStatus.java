package io.github.m3rt1n99.fafre;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.File;
import java.io.IOException;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.Locale;

/**
 * {@code <root>/launch/status.json}, written by the native runtime at every
 * stage change (atomically) and read by the launcher's "Last run" panel.
 * Missing keys read as empty/zero so an older or newer runtime still shows
 * whatever it did write.
 */
final class RunStatus {
    static final String STATE_LOADING = "loading";
    static final String STATE_RUNNING = "running";
    static final String STATE_ERROR = "error";
    static final String STATE_EXITED = "exited";

    final String state;
    final String stage;
    final String message;
    final String renderer;
    final long mounts;
    final long archives;
    final long archiveEntries;
    final double scriptMs;
    final double mountMs;
    final String splash;
    final String splashFormat;
    final int width;
    final int height;
    final String timestamp;
    final String versionName;
    /** Modification time of status.json in ms, for "is this from the last start?" checks. */
    final long fileTime;

    private RunStatus(JSONObject json, long fileTime) {
        state = json.optString("state", "");
        stage = json.optString("stage", "");
        message = json.optString("message", "");
        renderer = json.optString("renderer", "");
        mounts = json.optLong("mounts", 0);
        archives = json.optLong("archives", 0);
        archiveEntries = json.optLong("archiveEntries", 0);
        scriptMs = json.optDouble("scriptMs", 0);
        mountMs = json.optDouble("mountMs", 0);
        splash = json.optString("splash", "");
        splashFormat = json.optString("splashFormat", "");
        width = json.optInt("width", 0);
        height = json.optInt("height", 0);
        timestamp = json.optString("timestamp", "");
        versionName = json.optString("versionName", "");
        this.fileTime = fileTime;
    }

    /** The last status, or null if there is none. A damaged file is reported as an IOException. */
    static RunStatus read(File file) throws IOException {
        String text = FileOps.readText(file, 256 * 1024);
        if (text == null) {
            return null;
        }
        try {
            return new RunStatus(new JSONObject(text), file.lastModified());
        } catch (JSONException e) {
            throw new IOException("status.json is not valid JSON: " + e.getMessage(), e);
        }
    }

    boolean isTerminal() {
        return STATE_ERROR.equals(state) || STATE_EXITED.equals(state);
    }

    /** Short counters line for the UI, or "" when the run never got that far. */
    String counters() {
        StringBuilder out = new StringBuilder();
        if (mounts > 0 || archives > 0) {
            out.append(String.format(Locale.ROOT, "%d mounts · %d archives · %,d entries", mounts, archives,
                    archiveEntries));
        }
        if (scriptMs > 0 || mountMs > 0) {
            if (out.length() > 0) {
                out.append('\n');
            }
            out.append(String.format(Locale.ROOT, "script %.0f ms · mount %.0f ms", scriptMs, mountMs));
        }
        if (!splash.isEmpty()) {
            if (out.length() > 0) {
                out.append('\n');
            }
            out.append(splash);
            if (!splashFormat.isEmpty()) {
                out.append(" (").append(splashFormat).append(' ').append(width).append('x').append(height)
                        .append(')');
            }
        }
        return out.toString();
    }

    /**
     * Writes a minimal error status in the runtime's schema. Used only by the
     * Java side when the native runtime cannot even be loaded, so the launcher
     * still has something to show.
     */
    static void writeError(File file, String stage, String message, String versionName) throws IOException {
        try {
            JSONObject json = new JSONObject();
            json.put("schema", 1);
            json.put("state", STATE_ERROR);
            json.put("stage", stage);
            json.put("message", message);
            json.put("renderer", "none");
            json.put("timestamp", Instant.now().truncatedTo(ChronoUnit.SECONDS).toString());
            json.put("versionName", versionName);
            FileOps.writeJson(file, json.toString());
        } catch (JSONException e) {
            throw new IOException(e.getMessage(), e);
        }
    }
}
