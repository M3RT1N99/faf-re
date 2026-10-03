package io.github.m3rt1n99.fafre;

import android.content.Context;
import android.content.SharedPreferences;

import java.util.Collections;
import java.util.HashSet;
import java.util.Set;

/**
 * Launcher settings and small bits of state that must survive the activity
 * (rotation, process death): launch options, import choices, the folders the
 * user picked and the outcome of the last import job.
 */
final class Settings {
    static final String TREE_SCFA = "scfa";
    static final String TREE_FAF = "faf";
    static final String TREE_VAULT = "vault";

    private static final String FILE = "launcher";
    private static final String RENDERER = "renderer";
    private static final String NO_MOVIE = "nomovie";
    private static final String NO_SOUND = "nosound";
    private static final String EXTRA_ARGS = "extra_args";
    private static final String IMPORT_RECOMMENDED = "import_recommended";
    private static final String IMPORT_OPTIONAL = "import_optional";
    private static final String TREE_PREFIX = "tree_";
    private static final String LAST_JOB_TITLE = "last_job_title";
    private static final String LAST_JOB_MESSAGE = "last_job_message";
    private static final String LAST_JOB_OK = "last_job_ok";
    private static final String LAST_JOB_TIME = "last_job_time";
    private static final String LAST_LAUNCH = "last_launch";

    private final SharedPreferences mPrefs;

    Settings(Context context) {
        mPrefs = context.getApplicationContext().getSharedPreferences(FILE, Context.MODE_PRIVATE);
    }

    String renderer() {
        String value = mPrefs.getString(RENDERER, LaunchArgs.RENDERER_VULKAN);
        return LaunchArgs.RENDERER_GLES.equals(value) ? LaunchArgs.RENDERER_GLES : LaunchArgs.RENDERER_VULKAN;
    }

    void setRenderer(String renderer) {
        mPrefs.edit().putString(RENDERER, renderer).apply();
    }

    /** /nomovie: on by default because movies are optional data. */
    boolean noMovie() {
        return mPrefs.getBoolean(NO_MOVIE, true);
    }

    void setNoMovie(boolean value) {
        mPrefs.edit().putBoolean(NO_MOVIE, value).apply();
    }

    /** /nosound: on by default for M1, which has no audio backend yet. */
    boolean noSound() {
        return mPrefs.getBoolean(NO_SOUND, true);
    }

    void setNoSound(boolean value) {
        mPrefs.edit().putBoolean(NO_SOUND, value).apply();
    }

    String extraArgs() {
        return mPrefs.getString(EXTRA_ARGS, "");
    }

    void setExtraArgs(String value) {
        mPrefs.edit().putString(EXTRA_ARGS, value).apply();
    }

    boolean importRecommended() {
        return mPrefs.getBoolean(IMPORT_RECOMMENDED, true);
    }

    void setImportRecommended(boolean value) {
        mPrefs.edit().putBoolean(IMPORT_RECOMMENDED, value).apply();
    }

    /** Ids of the optional manifest entries the user ticked. */
    Set<String> importOptional() {
        return Collections.unmodifiableSet(new HashSet<>(mPrefs.getStringSet(IMPORT_OPTIONAL,
                Collections.<String>emptySet())));
    }

    void setImportOptional(String id, boolean selected) {
        // Never mutate the set SharedPreferences returns; write a copy.
        Set<String> ids = new HashSet<>(importOptional());
        if (selected) {
            ids.add(id);
        } else {
            ids.remove(id);
        }
        mPrefs.edit().putStringSet(IMPORT_OPTIONAL, ids).apply();
    }

    String tree(String kind) {
        return mPrefs.getString(TREE_PREFIX + kind, null);
    }

    void setTree(String kind, String uri) {
        mPrefs.edit().putString(TREE_PREFIX + kind, uri).apply();
    }

    void setLastJob(String title, String message, boolean ok) {
        mPrefs.edit()
                .putString(LAST_JOB_TITLE, title)
                .putString(LAST_JOB_MESSAGE, message)
                .putBoolean(LAST_JOB_OK, ok)
                .putLong(LAST_JOB_TIME, System.currentTimeMillis())
                .apply();
    }

    String lastJobTitle() {
        return mPrefs.getString(LAST_JOB_TITLE, null);
    }

    String lastJobMessage() {
        return mPrefs.getString(LAST_JOB_MESSAGE, null);
    }

    boolean lastJobOk() {
        return mPrefs.getBoolean(LAST_JOB_OK, false);
    }

    long lastJobTime() {
        return mPrefs.getLong(LAST_JOB_TIME, 0);
    }

    long lastLaunch() {
        return mPrefs.getLong(LAST_LAUNCH, 0);
    }

    void setLastLaunch(long millis) {
        mPrefs.edit().putLong(LAST_LAUNCH, millis).apply();
    }
}
