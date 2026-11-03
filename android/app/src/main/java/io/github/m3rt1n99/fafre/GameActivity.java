package io.github.m3rt1n99.fafre;

import android.app.Activity;
import android.graphics.Color;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.TextView;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

/**
 * The game (android/CLIENT_PLAN.md, C1): the engine's program on a full-screen surface, in a
 * process of its own (":game") that ends with the program, as a Windows program's does. It plays
 * FAF's game files of the newest version this app has (the replay test downloads them) on the
 * user's Supreme Commander: Forged Alliance. The pointer and touch are the mouse, keys the
 * keyboard, and Back is Esc. Until the renderer draws (C2), the report's lines show on the screen.
 */
public final class GameActivity extends Activity implements SurfaceHolder.Callback {
    /** The program's path in its install, as FAF's client starts it: the engine takes its folder from it. */
    private static final String PROGRAM = "bin/ForgedAlliance.exe";
    private static final int REPORT_LINES = 14;

    /** MotionEvent's buttons and the host's. */
    private static final int[][] BUTTONS = {
        {MotionEvent.BUTTON_PRIMARY, Game.BUTTON_LEFT},
        {MotionEvent.BUTTON_SECONDARY, Game.BUTTON_RIGHT},
        {MotionEvent.BUTTON_TERTIARY, Game.BUTTON_MIDDLE},
        {MotionEvent.BUTTON_BACK, Game.BUTTON_X1},
        {MotionEvent.BUTTON_FORWARD, Game.BUTTON_X2},
    };

    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private TextView mText;
    private boolean mStarted;
    /** The mouse's buttons that are down (MotionEvent's BUTTON_ bits). */
    private int mButtons;

    private final Runnable mPoll = new Runnable() {
        @Override
        public void run() {
            showReport();
            mHandler.postDelayed(this, 500);
        }
    };

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (Build.VERSION.SDK_INT >= 28) {
            WindowManager.LayoutParams attributes = getWindow().getAttributes();
            attributes.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(attributes);
        }
        FrameLayout root = new FrameLayout(this);
        SurfaceView surface = new SurfaceView(this);
        surface.getHolder().addCallback(this);
        root.addView(surface);
        mText = new TextView(this);
        mText.setTextColor(Color.WHITE);
        mText.setShadowLayer(4, 0, 0, Color.BLACK);
        mText.setTextSize(12);
        mText.setGravity(Gravity.TOP | Gravity.START);
        mText.setPadding(48, 48, 48, 48);
        root.addView(mText);
        setContentView(root);
        hideSystemBars();

        GameReport.clear(this);
        String problem = start();
        if (problem != null) {
            GameReport.append(this, GameReport.END + problem);
            mText.setText(problem + "\n\n(Back returns)");
            return;
        }
        mHandler.post(mPoll);
    }

    /** Starts the program, or says why it cannot. */
    private String start() {
        if (Game.loadError() != null) {
            return "FAIL: libfafgame.so did not load: " + Game.loadError();
        }
        Storage storage;
        try {
            storage = new Storage(this);
        } catch (IOException e) {
            return "FAIL: " + e.getMessage();
        }
        int archives = ScfaImport.archives(storage);
        if (archives == 0) {
            return "FAIL: no Supreme Commander: Forged Alliance imported yet (card 2)";
        }
        String[] fonts = new File(storage.scfa, "fonts").list();
        if (fonts == null || fonts.length == 0) {
            return "FAIL: no fonts of Supreme Commander: Forged Alliance yet: import it again (card 2), which now takes fonts/ too";
        }
        int version = newestGameFiles(storage);
        if (version < 0) {
            return "FAIL: no whole FAF game files yet; the replay test downloads them (card 3)";
        }
        File game = storage.game(version);
        File folder = Storage.mkdirs(GameReport.folder(storage));
        File vault = Storage.mkdirs(new File(folder, "vault"));
        Storage.mkdirs(new File(vault, "maps"));
        Storage.mkdirs(new File(vault, "mods"));
        File init;
        try {
            init = GameFiles.runInit(game, "game", vault);
        } catch (IOException e) {
            return "FAIL: the init file: " + e.getMessage();
        }
        // /nomovie as FAF's client passes it: the splash screen waits for its movies to end, and
        // without FFmpeg (C3) no movie opens, so it would wait for a tap.
        String[] arguments = {
            new File(game, PROGRAM).getPath(), "/init", init.getPath(), "/log", GameReport.log(storage).getPath(), "/nomovie",
        };
        GameReport.append(this, "game: FAF game version " + version + ", " + archives + " SCFA archives");
        File home = Storage.mkdirs(new File(folder, "home"));
        if (!Game.start(arguments, home.getPath(), getCacheDir().getPath(), GameReport.file(this).getPath())) {
            return "FAIL: the program did not start";
        }
        mStarted = true;
        return null;
    }

    /** The newest FAF game version whose files are ready, or -1. */
    private static int newestGameFiles(Storage storage) {
        String[] names = new File(storage.parity, "game").list();
        int newest = -1;
        if (names != null) {
            for (String name : names) {
                try {
                    int version = Integer.parseInt(name);
                    if (version > newest && GameFiles.ready(storage, version)) {
                        newest = version;
                    }
                } catch (NumberFormatException e) {
                    // not a version's folder
                }
            }
        }
        return newest;
    }

    /** The report's last lines over the surface. */
    private void showReport() {
        String report;
        try {
            report = new String(Files.readAllBytes(GameReport.file(this).toPath()), StandardCharsets.UTF_8);
        } catch (IOException e) {
            return;
        }
        String[] lines = report.split("\n");
        StringBuilder text = new StringBuilder("Forged Alliance: the engine runs (C1); the picture comes with C2.\n\n");
        for (int i = Math.max(0, lines.length - REPORT_LINES); i < lines.length; ++i) {
            text.append(lines[i]).append('\n');
        }
        mText.setText(text);
    }

    @Override
    protected void onResume() {
        super.onResume();
        hideSystemBars();
        if (mStarted) {
            Game.foreground(true);
        }
    }

    @Override
    protected void onPause() {
        if (mStarted) {
            Game.foreground(false);
        }
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        mHandler.removeCallbacks(mPoll);
        super.onDestroy();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
        }
    }

    private void hideSystemBars() {
        if (Build.VERSION.SDK_INT >= 30) {
            getWindow().setDecorFitsSystemWindows(false);
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
        }
    }

    // --- The surface --------------------------------------------------------------------------

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        if (mStarted) {
            Game.surface(holder.getSurface(), width, height);
        }
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        if (mStarted) {
            Game.surface(null, 0, 0);
        }
    }

    // --- Input --------------------------------------------------------------------------------

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (!mStarted) {
            return super.onTouchEvent(event);
        }
        if (event.isFromSource(InputDevice.SOURCE_MOUSE)) {
            mouse(event);
            return true;
        }
        // A touch is the left button where the first finger is.
        int x = Math.round(event.getX(0));
        int y = Math.round(event.getY(0));
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                Game.mouseMove(x, y);
                Game.mouseButton(Game.BUTTON_LEFT, true, x, y);
                break;
            case MotionEvent.ACTION_MOVE:
                Game.mouseMove(x, y);
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                Game.mouseButton(Game.BUTTON_LEFT, false, x, y);
                break;
            default:
                break;
        }
        return true;
    }

    @Override
    public boolean onGenericMotionEvent(MotionEvent event) {
        if (!mStarted || !event.isFromSource(InputDevice.SOURCE_CLASS_POINTER)) {
            return super.onGenericMotionEvent(event);
        }
        if (event.getActionMasked() == MotionEvent.ACTION_SCROLL) {
            int delta = Math.round(event.getAxisValue(MotionEvent.AXIS_VSCROLL) * Game.WHEEL_DELTA);
            if (delta != 0) {
                Game.mouseWheel(delta, Math.round(event.getX()), Math.round(event.getY()));
            }
            return true;
        }
        mouse(event);
        return true;
    }

    /** A mouse's event: where it is, and the buttons that went down or up since its last one. */
    private void mouse(MotionEvent event) {
        int x = Math.round(event.getX());
        int y = Math.round(event.getY());
        Game.mouseMove(x, y);
        int buttons = event.getButtonState();
        int changed = buttons ^ mButtons;
        for (int[] button : BUTTONS) {
            if ((changed & button[0]) != 0) {
                Game.mouseButton(button[1], (buttons & button[0]) != 0, x, y);
            }
        }
        mButtons = buttons;
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (!mStarted || systemKey(event.getKeyCode())) {
            return super.dispatchKeyEvent(event);
        }
        if (event.getAction() == KeyEvent.ACTION_MULTIPLE) {
            String characters = event.getCharacters();
            if (characters != null) {
                Game.text(characters);
            }
            return true;
        }
        boolean down = event.getAction() == KeyEvent.ACTION_DOWN;
        int character = down ? event.getUnicodeChar(event.getMetaState()) : 0;
        if ((character & KeyCharacterMap.COMBINING_ACCENT) != 0) {
            // A dead key types nothing by itself.
            character = 0;
        }
        Game.key(event.getKeyCode(), event.getScanCode(), down, character);
        return true;
    }

    /** The keys the phone keeps for itself. */
    private static boolean systemKey(int keyCode) {
        switch (keyCode) {
            case KeyEvent.KEYCODE_VOLUME_UP:
            case KeyEvent.KEYCODE_VOLUME_DOWN:
            case KeyEvent.KEYCODE_VOLUME_MUTE:
            case KeyEvent.KEYCODE_POWER:
            case KeyEvent.KEYCODE_HOME:
            case KeyEvent.KEYCODE_APP_SWITCH:
            case KeyEvent.KEYCODE_ASSIST:
            case KeyEvent.KEYCODE_VOICE_ASSIST:
            case KeyEvent.KEYCODE_CAMERA:
            case KeyEvent.KEYCODE_FOCUS:
            case KeyEvent.KEYCODE_SLEEP:
            case KeyEvent.KEYCODE_WAKEUP:
            case KeyEvent.KEYCODE_BRIGHTNESS_UP:
            case KeyEvent.KEYCODE_BRIGHTNESS_DOWN:
            case KeyEvent.KEYCODE_HEADSETHOOK:
            case KeyEvent.KEYCODE_CALL:
            case KeyEvent.KEYCODE_ENDCALL:
            case KeyEvent.KEYCODE_MUTE:
            case KeyEvent.KEYCODE_MEDIA_PLAY:
            case KeyEvent.KEYCODE_MEDIA_PAUSE:
            case KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE:
            case KeyEvent.KEYCODE_MEDIA_STOP:
            case KeyEvent.KEYCODE_MEDIA_NEXT:
            case KeyEvent.KEYCODE_MEDIA_PREVIOUS:
            case KeyEvent.KEYCODE_MEDIA_REWIND:
            case KeyEvent.KEYCODE_MEDIA_FAST_FORWARD:
            case KeyEvent.KEYCODE_MEDIA_RECORD:
                return true;
            default:
                return false;
        }
    }
}
