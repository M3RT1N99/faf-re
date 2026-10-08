package io.github.m3rt1n99.fafre;

import android.app.NativeActivity;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.os.Process;
import android.util.Log;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import java.io.File;
import java.io.IOException;
import java.nio.ByteBuffer;

/**
 * Hosts the native runtime (libfaf_android.so via android.app.lib_name) in
 * its own ":game" process. The Java side only does what NativeActivity cannot:
 * keep the screen on and run immersive fullscreen. Everything else, including
 * reading the "argv" extra and finishing the activity, is native.
 *
 * <p>Runs in a separate process so a native crash or abort never takes the
 * launcher (and a running import) down with it, and so every start gets fresh
 * native globals: the launcher kills a leftover ":game" process before it
 * starts the next run.
 *
 * <p>Mode "menu-replay" (Intent extra {@code mode}, release 0.5.0): the native
 * side replays the menu trace instead of starting the game
 * (port/android/src/GalPlay.h) and calls back into this activity
 * ({@link #onMenuReplayProgress}, {@link #onMenuReplayReadback},
 * {@link #onMenuReplayFinished}); {@link MenuReplaySession} writes the run's
 * files and {@link MenuReplayOverlay} shows the progress over the replay.
 */
public final class GameActivity extends NativeActivity {
    private static final String TAG = "fafre-game";

    /** The menu replay of this activity, or null in the game's mode. */
    private MenuReplaySession mMenuReplay;
    private MenuReplayOverlay mOverlay;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        Window window = getWindow();
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (Build.VERSION.SDK_INT >= 28) {
            // Landscape puts the cutout on a short edge; draw under it rather
            // than letterbox the whole game.
            WindowManager.LayoutParams attributes = window.getAttributes();
            attributes.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            window.setAttributes(attributes);
        }
        drawEdgeToEdge(window);
        // Before super.onCreate: the native side may call back as soon as it runs.
        if (MenuReplay.MODE.equals(getIntent().getStringExtra(MenuReplay.EXTRA_MODE))) {
            mMenuReplay = new MenuReplaySession(this, getIntent());
            mOverlay = new MenuReplayOverlay(this, mMenuReplay);
            Log.i(TAG, "menu replay " + mMenuReplay.run());
        }
        // NativeActivity does the same lookup (on this thread) right after this.
        String problem = AppInfo.nativeRuntimeProblem(this);
        if (problem != null) {
            if (mMenuReplay != null) {
                mMenuReplay.failWithoutRuntime(problem);
            }
            failWithoutRuntime(problem);
        }
        super.onCreate(savedInstanceState);
        enterImmersive();
    }

    @Override
    public void onAttachedToWindow() {
        super.onAttachedToWindow();
        if (mOverlay != null) {
            mOverlay.show();
        }
    }

    @Override
    protected void onDestroy() {
        // The overlay is a window of this activity's: gone before the activity's window is.
        if (mOverlay != null) {
            mOverlay.dismiss();
        }
        // Returns once the native side has ended (it stops a running replay, which reports its finish).
        super.onDestroy();
        if (mMenuReplay != null) {
            mMenuReplay.onActivityDestroyed();
        }
    }

    // ------------------------------------------------- menu replay callbacks (native side, GalPlay.h)
    // Called on the native replay thread. Nothing may be thrown back into native code.

    /** A stage change or a replayed frame: {"stage","message","frame","frames","frameMs",...}. */
    public void onMenuReplayProgress(String json) {
        try {
            if (mMenuReplay != null) {
                mMenuReplay.onProgress(json);
            }
        } catch (Throwable e) {
            Log.e(TAG, "onMenuReplayProgress", e);
        }
    }

    /** A read-back frame: R G B A, top row first, valid only during this call. */
    public void onMenuReplayReadback(int frame, int width, int height, ByteBuffer rgba, String json) {
        try {
            if (mMenuReplay != null) {
                mMenuReplay.onReadback(frame, width, height, rgba, json);
            }
        } catch (Throwable e) {
            Log.e(TAG, "onMenuReplayReadback", e);
        }
    }

    /** The end (also after an error or a stop): galplay.json's content. */
    public void onMenuReplayFinished(String json) {
        try {
            if (mMenuReplay != null) {
                mMenuReplay.onFinished(json);
            }
        } catch (Throwable e) {
            Log.e(TAG, "onMenuReplayFinished", e);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        enterImmersive();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            // The bars come back after a swipe, a dialog or the IME; hide them again.
            enterImmersive();
        }
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        // singleTask: a second start while running only brings us to the front.
        // The runtime read its arguments at startup, so new ones are not applied.
        setIntent(intent);
        Log.i(TAG, "already running; arguments of the new start are ignored");
    }

    private void enterImmersive() {
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            enterImmersiveLegacy();
        }
    }

    /**
     * Lets the window draw under the system bars. Android 15 does this for
     * every app targeting it (and deprecated the call); 11-14 need it so the
     * surface covers the screen once the bars are hidden.
     */
    @SuppressWarnings("deprecation")
    static void drawEdgeToEdge(Window window) {
        if (Build.VERSION.SDK_INT >= 30 && Build.VERSION.SDK_INT < 35) {
            window.setDecorFitsSystemWindows(false);
        }
    }

    /** API 26-29: the flag based API that WindowInsetsController replaced. */
    @SuppressWarnings("deprecation")
    private void enterImmersiveLegacy() {
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_FULLSCREEN);
    }

    /**
     * NativeActivity.onCreate throws when the library is missing, which would
     * crash this process with a system error dialog and no explanation. Write
     * the reason where the launcher looks (status.json, launcher.log) and end
     * this process quietly instead; the launcher underneath shows the message.
     * The launcher already refuses to start in this case, so this is a backstop.
     */
    private void failWithoutRuntime(String problem) {
        Log.e(TAG, problem);
        LauncherLog.get(this).log("GameActivity" + (mMenuReplay != null ? " (menu replay)" : "") + ": " + problem);
        File root = getExternalFilesDir(null);
        if (root != null) {
            try {
                RunStatus.writeError(new File(root, DataRoot.STATUS_JSON), "args", problem,
                        AppInfo.versionName(this));
            } catch (IOException e) {
                Log.e(TAG, "could not write status.json: " + e.getMessage());
            }
        }
        finish();
        // The log line is written asynchronously; give it a moment before the process goes.
        try {
            Thread.sleep(200);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        Process.killProcess(Process.myPid());
    }
}
