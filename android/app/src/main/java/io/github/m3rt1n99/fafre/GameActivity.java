package io.github.m3rt1n99.fafre;

import android.content.Context;
import android.graphics.Rect;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.system.ErrnoException;
import android.system.Os;
import android.util.DisplayMetrics;
import android.view.Display;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.RelativeLayout;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;

/**
 * The game: the engine's program (libfafgame.so, the engine's own POSIX client on SDL3, android/native) in
 * SDL's activity, in a process of its own (":game") that ends with the program, as a Windows program's does.
 * It plays FAF's game files of the newest version this app has (the Game card's "Get FAF's newest version" or
 * the replay test downloads them) on the user's Supreme Commander: Forged Alliance.
 *
 * <p>SDL gives the program the screen (the activity's surface, through Vulkan), the fingers as the mouse,
 * the keys and the sound. The app gives the command line (getArguments, the program's path first, as FAF's
 * client starts the game), the program's folders (HOME, TMPDIR) and what a game on a phone needs around it:
 * the whole screen, the picture's own size (PictureSize), the UI package for fingers and the HUD's factor
 * (android/TOUCH_UI.md), Back as Esc, the multicast lock that finds LAN games, the audio focus that the game's sound
 * follows. The program keeps running while the activity is in the back (android/native/src/app/GameMainAndroid.cpp),
 * without its sound. The touch scheme Griff and the bar are not on this activity yet; {@link LegacyGameActivity} has
 * them on the earlier host.
 */
public final class GameActivity extends SDLActivity {
    /** The program's path in its install, as FAF's client starts it: the engine takes its folder from it. */
    private static final String PROGRAM = "bin/ForgedAlliance.exe";
    /**
     * Tests' extras (am start --ef, --es, --ez): the HUD's factor in place of the screen's, as the emulator keeps its own
     * millimetres at another size; more of the engine's arguments, as one string; BC textures decoded on the CPU, as on
     * a phone's GPU without them, also on one with them (android/native/patches/README.md, DeviceVulkan.cpp).
     */
    private static final String EXTRA_HUD_SCALE = "hudScale";
    private static final String EXTRA_ARGUMENTS = "arguments";
    private static final String EXTRA_BC_ON_CPU = "bcOnCpu";

    /** The program's command line without its display mode, or null with mProblem saying why there is none. */
    private List<String> mArguments;
    private String mProblem;
    /** The lines the user chose (PictureSize), which the picture has on any screen. */
    private int mLines;
    /** The program can run, or runs, in this activity: no problem stopped it, and SDL's library loaded. */
    private boolean mShowsProgram;
    /** Held while the activity is in front: Wi-Fi drops the broadcasts that find LAN games on a phone without it. */
    private WifiManager.MulticastLock mDiscoveryLock;
    private final Handler mHandler = new Handler(Looper.getMainLooper());
    /**
     * Asked for while the activity is in front, where the game's sound plays while the focus is the activity's
     * (android/native/src/app/SoundAndroid.cpp). It stops for another app's sound, even for a short one: the system lowers
     * no AAudio stream.
     */
    private AudioFocusRequest mAudioFocus;
    /** Gone for good: nothing gives it back, as the end of a call does. */
    private boolean mAudioFocusLost;

    @Override
    protected void onCreate(Bundle state) {
        // Before SDL's activity loads the library: the program reads its environment when it starts.
        GameReport.begin(this);
        mLines = PictureSize.lines(this);
        mProblem = prepare();
        super.onCreate(state);
        if (mProblem != null) {
            GameReport.append(this, GameReport.END + mProblem);
            Toast.makeText(this, mProblem, Toast.LENGTH_LONG).show();
            // Before it is resumed, so SDL's activity starts no program.
            finish();
            return;
        }
        if (mBrokenLibraries) {
            // SDL's activity says why in a dialog of its own, and has no surface.
            return;
        }
        // The surface fills the screen whatever size its buffers have: laid out by their size, it would take only as much
        // of the screen as the picture has pixels.
        mSurface.setLayoutParams(new RelativeLayout.LayoutParams(
            RelativeLayout.LayoutParams.MATCH_PARENT, RelativeLayout.LayoutParams.MATCH_PARENT));
        fitPicture();
        mSurface.addOnLayoutChangeListener((view, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) ->
            fitPicture(right - left, bottom - top));
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (Build.VERSION.SDK_INT >= 28) {
            WindowManager.LayoutParams attributes = getWindow().getAttributes();
            attributes.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(attributes);
        }
        hideSystemBars();
        WifiManager wifi = getApplicationContext().getSystemService(WifiManager.class);
        if (wifi != null) {
            mDiscoveryLock = wifi.createMulticastLock("faf-game-discovery");
            mDiscoveryLock.setReferenceCounted(false);
        }
        setVolumeControlStream(AudioManager.STREAM_MUSIC);
        mAudioFocus = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
            .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME).build())
            .setAcceptsDelayedFocusGain(true)
            .setWillPauseWhenDucked(true)
            .setOnAudioFocusChangeListener(this::onAudioFocusChange, mHandler)
            .build();
        mShowsProgram = true;
    }

    /** The program's command line and its environment, or why the game cannot start. */
    private String prepare() {
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
        if (ScfaImport.fonts(storage) == 0) {
            return "FAIL: no fonts of Supreme Commander: Forged Alliance yet: import its folder again (card 2), which takes fonts/ too";
        }
        List<Integer> versions = GameFiles.readyVersions(storage);
        if (versions.isEmpty()) {
            return "FAIL: no whole FAF game files yet; \"Get FAF's newest version\" on card 5 downloads them, as does the replay test (card 3)";
        }
        int version = versions.get(versions.size() - 1);
        File game = storage.game(version);
        File folder = Storage.mkdirs(GameReport.folder(storage));
        File vault = Storage.mkdirs(new File(folder, "vault"));
        Storage.mkdirs(new File(vault, "maps"));
        Storage.mkdirs(new File(vault, "mods"));
        File init;
        try {
            // With the UI package for fingers (android/TOUCH_UI.md), from the APK.
            init = GameFiles.gameInit(getAssets(), game, vault);
        } catch (IOException e) {
            return "FAIL: the init file: " + e.getMessage();
        }
        File home = Storage.mkdirs(GameReport.home(storage));
        try {
            Os.setenv("HOME", home.getPath(), true);
            Os.setenv("TMPDIR", getCacheDir().getPath(), true);
            Os.setenv("FAF_GAME_REPORT", GameReport.file(this).getPath(), true);
            if (getIntent().getBooleanExtra(EXTRA_BC_ON_CPU, false)) {
                Os.setenv("FAF_VULKAN_BC_ON_CPU", "1", true);
            }
        } catch (ErrnoException e) {
            return "FAIL: the program's environment: " + e.getMessage();
        }
        String hudScale = HudScale.argument(hudScale());
        mArguments = new ArrayList<>(Arrays.asList(new File(game, PROGRAM).getPath(), "/init", init.getPath(),
            "/log", GameReport.log(storage).getPath(),
            // The engine would send its records of desyncs and of a game's end to its author's server, with the
            // players' names, and nobody here has agreed to that.
            "/notelemetry",
            // The HUD's factor, which the UI package's Lua reads (fafre/scale.lua), and the most the user's UI scale can make of it.
            HudScale.SWITCH, hudScale, HudScale.MOST_SWITCH, HudScale.argument(HudScale.most(pictureLines()))));
        // An import by an earlier release has no movies, and the splash screen would wait for a tap for each.
        if (ScfaImport.movies(storage) == 0) {
            mArguments.add("/nomovie");
        }
        String more = getIntent().getStringExtra(EXTRA_ARGUMENTS);
        if (more != null && !more.trim().isEmpty()) {
            mArguments.addAll(Arrays.asList(more.trim().split("\\s+")));
        }
        GameReport.append(this, "game: FAF game version " + version + ", " + archives + " SCFA archives");
        GameReport.append(this, "game: HUD scale " + hudScale + " on " + screenInWords());
        return null;
    }

    @Override
    protected String[] getLibraries() {
        // SDL is linked into the game's library.
        return new String[] {"fafgame"};
    }

    @Override
    protected String getMainFunction() {
        // The app's entry (android/native/src/app/GameMainAndroid.cpp), which runs the engine's SDL_main.
        return "FafGameMain";
    }

    @Override
    protected SDLSurface createSDLSurface(Context context) {
        return new GameSurface(context);
    }

    /** On SDL's thread, once the surface is there: the program draws in the screen's mode, the picture's size. */
    @Override
    protected String[] getArguments() {
        Rect picture = mSurface.getHolder().getSurfaceFrame();
        List<String> arguments = new ArrayList<>(mArguments);
        arguments.addAll(Arrays.asList("/fullscreen", Integer.toString(picture.width()), Integer.toString(picture.height())));
        GameReport.append(this, String.format(Locale.ROOT, "game: display mode %dx%d on a %dx%d view", picture.width(),
            picture.height(), mSurface.getWidth(), mSurface.getHeight()));
        return arguments.toArray(new String[0]);
    }

    // --- The picture ----------------------------------------------------------------------------

    /**
     * The surface's buffers at the picture's size, which the compositor scales to the screen: the game's UI has a fixed size
     * in pixels, so fewer lines make it larger on the glass (PictureSize). Before the first layout, the window as it will be
     * once the activity has turned, so that the first surface has it.
     */
    private void fitPicture() {
        if (Build.VERSION.SDK_INT >= 30) {
            Rect bounds = getWindowManager().getCurrentWindowMetrics().getBounds();
            fitPicture(Math.max(bounds.width(), bounds.height()), Math.min(bounds.width(), bounds.height()));
        }
    }

    private void fitPicture(int viewWidth, int viewHeight) {
        // Standing is the moment before the activity has turned, unless the window is a part of the screen.
        if (viewWidth <= 0 || viewHeight <= 0 || (viewHeight > viewWidth && !isInMultiWindowMode())) {
            return;
        }
        int[] size = PictureSize.of(viewWidth, viewHeight, mLines);
        mSurface.getHolder().setFixedSize(size[0], size[1]);
    }

    /** The HUD's factor for this screen, or what a test gives. */
    private double hudScale() {
        double given = getIntent().getFloatExtra(EXTRA_HUD_SCALE, 0);
        return given > 0 ? given : HudScale.factor(pictureLines(), screenShortSideMm());
    }

    /** The lines the picture will have, whatever the activity's turn has done so far. */
    private int pictureLines() {
        DisplayMetrics metrics = realMetrics();
        int longSide = Math.max(metrics.widthPixels, metrics.heightPixels);
        int shortSide = Math.min(metrics.widthPixels, metrics.heightPixels);
        return PictureSize.of(longSide, shortSide, mLines)[1];
    }

    private double screenShortSideMm() {
        DisplayMetrics metrics = realMetrics();
        return HudScale.shortSideMm(shortSide(display().getMode()), largestModeShortSide(), (metrics.xdpi + metrics.ydpi) / 2,
            Math.min(metrics.widthPixels, metrics.heightPixels), metrics.densityDpi);
    }

    /** What the factor came from, for the report: phones differ in what they say of their screens. */
    private String screenInWords() {
        DisplayMetrics metrics = realMetrics();
        return String.format(Locale.ROOT, "%d lines, %s, xdpi %.0f ydpi %.0f, window %dx%d", pictureLines(),
            HudScale.shortSideInWords(shortSide(display().getMode()), largestModeShortSide(), (metrics.xdpi + metrics.ydpi) / 2,
                Math.min(metrics.widthPixels, metrics.heightPixels), metrics.densityDpi),
            metrics.xdpi, metrics.ydpi, metrics.widthPixels, metrics.heightPixels);
    }

    private int largestModeShortSide() {
        int largest = 0;
        for (Display.Mode mode : display().getSupportedModes()) {
            largest = Math.max(largest, shortSide(mode));
        }
        return largest;
    }

    private static int shortSide(Display.Mode mode) {
        return Math.min(mode.getPhysicalWidth(), mode.getPhysicalHeight());
    }

    @SuppressWarnings("deprecation")
    private Display display() {
        return getWindowManager().getDefaultDisplay();
    }

    @SuppressWarnings("deprecation")
    private DisplayMetrics realMetrics() {
        DisplayMetrics metrics = new DisplayMetrics();
        display().getRealMetrics(metrics);
        return metrics;
    }

    // --- The activity ---------------------------------------------------------------------------

    @Override
    protected void onDestroy() {
        super.onDestroy();
        // A game that could not start leaves no process behind: SDL's activity takes the next start in this
        // process for one made anew around a program, and would end it.
        if (mProblem != null) {
            System.exit(0);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        hideSystemBars();
        if (mDiscoveryLock != null) {
            mDiscoveryLock.acquire();
        }
        if (mShowsProgram) {
            // First: the sound plays at the word that both are so, and it must not during a call.
            requestAudioFocus();
            Game.foreground(true);
        }
    }

    @Override
    protected void onPause() {
        if (mShowsProgram) {
            Game.foreground(false);
            getSystemService(AudioManager.class).abandonAudioFocusRequest(mAudioFocus);
        }
        if (mDiscoveryLock != null) {
            mDiscoveryLock.release();
        }
        super.onPause();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
            takeAudioFocusBack();
        }
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        if (event.getActionMasked() == MotionEvent.ACTION_DOWN) {
            takeAudioFocusBack();
        }
        return super.dispatchTouchEvent(event);
    }

    // --- The sound --------------------------------------------------------------------------------

    /** While a call has the focus this is delayed, and onAudioFocusChange hears when it comes. */
    private void requestAudioFocus() {
        int focus = getSystemService(AudioManager.class).requestAudioFocus(mAudioFocus);
        mAudioFocusLost = focus == AudioManager.AUDIOFOCUS_REQUEST_FAILED;
        Game.audioFocus(focus == AudioManager.AUDIOFOCUS_REQUEST_GRANTED);
    }

    private void onAudioFocusChange(int change) {
        if (change == AudioManager.AUDIOFOCUS_GAIN) {
            mAudioFocusLost = false;
            Game.audioFocus(true);
        } else if (change == AudioManager.AUDIOFOCUS_LOSS) {
            mAudioFocusLost = true;
            Game.audioFocus(false);
        } else if (change == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT || change == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK) {
            Game.audioFocus(false);
        }
    }

    /** The player is back at the game: its sound returns if the app that took the focus for good has stopped. */
    private void takeAudioFocusBack() {
        if (mShowsProgram && mAudioFocusLost && !getSystemService(AudioManager.class).isMusicActive()) {
            requestAudioFocus();
        }
    }

    /**
     * Back is Esc, the game's own way back (its escape menu, a dialog's cancel), as on the earlier host. A mouse's
     * back button stays SDL's.
     */
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK && !event.isFromSource(InputDevice.SOURCE_MOUSE)) {
            event = new KeyEvent(event.getDownTime(), event.getEventTime(), event.getAction(), KeyEvent.KEYCODE_ESCAPE,
                event.getRepeatCount(), event.getMetaState(), event.getDeviceId(), event.getScanCode(), event.getFlags(),
                event.getSource());
        }
        return super.dispatchKeyEvent(event);
    }

    /** The whole screen: the game is one full-screen window, which SDL leaves with the system's bars. */
    private void hideSystemBars() {
        if (Build.VERSION.SDK_INT >= 30) {
            getWindow().setDecorFitsSystemWindows(false);
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        }
    }
}
