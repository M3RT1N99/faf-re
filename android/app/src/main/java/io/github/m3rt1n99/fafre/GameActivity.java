package io.github.m3rt1n99.fafre;

import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Rect;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.net.Uri;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.system.ErrnoException;
import android.system.Os;
import android.util.DisplayMetrics;
import android.util.TypedValue;
import android.view.Display;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.PointerIcon;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.RelativeLayout;
import android.widget.TextView;
import android.widget.Toast;

import org.libsdl.app.HIDDeviceManager;
import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

import java.io.File;
import java.io.IOException;
import java.util.Arrays;
import java.util.Hashtable;
import java.util.List;
import java.util.Locale;

/**
 * The game: the engine's program (libfafgame.so, the engine's own POSIX client on SDL3, android/native) in
 * SDL's activity, in a process of its own (":game") that ends with the program, as a Windows program's does.
 * It plays FAF's game files of the newest version this app has (the Game card's "Get FAF's newest version" or
 * the replay test downloads them), or of the version a launcher's start asks for (GameStart: its extras, and the
 * result of a start that fails), on the user's Supreme Commander: Forged Alliance. Not exported: a launcher in
 * another app comes through LaunchGameActivity, which checks it; what its start needs fetched or copied first (the
 * map, the mods, its replay) the Preparation gets on SDL's thread before the program, shown on the activity.
 *
 * <p>SDL gives the program the screen (the activity's surface, through Vulkan), the fingers as the mouse,
 * the keys and the sound. The app gives the command line (getArguments, the program's path first, as FAF's
 * client starts the game, and a launcher's arguments before the display mode), the program's folders (HOME, TMPDIR)
 * and what a game on a phone needs around it: the whole screen, the picture's own size (PictureSize), the UI package
 * for fingers and the HUD's factor (android/TOUCH_UI.md), Back as Esc, the multicast lock that finds LAN games, the
 * audio focus that the game's sound follows. The program keeps running while the activity is in the back
 * (android/native/src/app/GameMainAndroid.cpp), without its sound. The touch scheme Griff and the bar are not on this
 * activity yet: android-client's GameActivity has them on the earlier host.
 */
public final class GameActivity extends SDLActivity {
    /**
     * Tests' extras (am start --ef, --es, --ez): the HUD's factor in place of the screen's, as the emulator keeps its own
     * millimetres at another size; more of the engine's arguments, as one string; BC textures decoded on the CPU, as on
     * a phone's GPU without them, also on one with them (android/native/patches/README.md, DeviceVulkan.cpp).
     */
    private static final String EXTRA_HUD_SCALE = "hudScale";
    private static final String EXTRA_ARGUMENTS = "arguments";
    private static final String EXTRA_BC_ON_CPU = "bcOnCpu";

    /**
     * The program's command line without its display mode, which onCreate prepares and SDL's thread asks of the activity
     * that shows the program then (getArguments): one program to a process.
     */
    private static List<String> sArguments;
    /** What the start needs fetched or copied before the program, which main runs; null for nothing. */
    private static Preparation sPreparation;
    /** SDL's thread runs the preparation, or waits for a surface after it: the program has not started. */
    private static volatile boolean sPreparing;
    private GameStart.Failure mFailure;
    /** The preparation's step over the surface, while it runs. */
    private TextView mPreparing;
    /** A launcher's start refused while the program runs, for which SDL set nothing up (refuseBesideProgram). */
    private boolean mBesideProgram;
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
    /** While the activity is there: a network lobby or game that has begun since the last look has its service too. */
    private final Runnable mPoll = new Runnable() {
        @Override
        public void run() {
            NetworkGameService.startIfNeeded(GameActivity.this);
            showPreparation();
            mHandler.postDelayed(this, 500);
        }
    };

    @Override
    protected void onCreate(Bundle state) {
        // With the program running, Android made the activity anew around it (onDestroy kept it), or another start came: the
        // same game goes on, shown by this one, unless a launcher's start is refused (GameStart.refusal).
        Thread program = runningProgram();
        mLines = PictureSize.lines(this);
        mFailure = GameStart.refusal(program != null, state != null, extra(GameStart.EXTRA_GAME_VERSION),
            extra(GameStart.EXTRA_LAUNCHER_ARGUMENTS));
        if (mFailure != null && program != null) {
            refuseBesideProgram(state);
            return;
        }
        if (program == null) {
            // Before SDL's activity loads the library: the program reads its environment when it starts. A launcher's game
            // that Android brings back without its process begins a report too, which ends the one of the game that is gone.
            GameReport.begin(this);
            if (mFailure == null) {
                try {
                    sArguments = prepare();
                } catch (GameStart.Failure failure) {
                    mFailure = failure;
                }
            }
        }
        Hashtable<Integer, PointerIcon> cursors = mCursors;
        int lastCursor = mLastCursorID;
        super.onCreate(state);
        if (program != null) {
            // SDL's onCreate takes each activity for the program's first: it forgot the program's thread, which it would
            // start anew, and the cursors the program made. With its lifecycle's queue kept (patches/README.md,
            // SDL_android.c), the next resume is the program's resume.
            mSDLThread = program;
            mCursors = cursors;
            mLastCursorID = lastCursor;
            Game.report("game: a new activity shows the running program");
            if (state == null && getIntent().getExtras() != null) {
                // Not Android's re-creation of the activity, which keeps the first start's intent: a start of its own,
                // with extras (the README's tests) that only a program's start reads (prepare).
                Game.report("game: the start's extras do not reach the running program; am start -S starts it anew");
            }
        }
        if (mFailure != null) {
            for (String line : mFailure.reportLines()) {
                GameReport.append(this, line);
            }
            result();
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
        mHandler.post(mPoll);
    }

    /**
     * A launcher's start while the program runs (RUNNING): this activity ends at once with the result, and the program goes on
     * as it is, shown by its own activity. SDL's onCreate would take SDL's statics for this one (the activity, its surface,
     * which the program draws on, the lifecycle's state) and its onDestroy end the program: the first stops at its first step
     * for it (loadLibraries), the second lets go of nothing (onDestroy).
     */
    private void refuseBesideProgram(Bundle state) {
        mBesideProgram = true;
        try {
            super.onCreate(state);
        } catch (BesideProgram stopped) {
            // Activity's onCreate has run, and SDL's stopped before it set anything up. The running program's report has a
            // line of it, and no end.
            Game.report(mFailure.reportLine());
            result();
            finish();
            return;
        }
        throw new IllegalStateException("SDL's onCreate went past loadLibraries beside the running program");
    }

    /** SDL's onCreate loads the libraries before it sets anything up: an activity beside the running program stops it there. */
    @Override
    public void loadLibraries() {
        if (mBesideProgram) {
            throw new BesideProgram();
        }
        super.loadLibraries();
    }

    /** What stops SDL's onCreate (loadLibraries): an Error, as SDL's onCreate takes the libraries' exceptions for their failure. */
    private static final class BesideProgram extends Error {
    }

    /**
     * The result of a start that runs no program of its own (mFailure), before the activity finishes: in onCreate, before it
     * is resumed, so SDL starts no program; on SDL's thread for a preparation that failed, as SDL finishes it after main.
     */
    private void result() {
        // For a starter that waits for the result, a launcher's activity (GameStart).
        setResult(RESULT_FIRST_USER, new Intent().putExtra(GameStart.RESULT_FAILURE, mFailure.code.name())
            .putExtra(GameStart.RESULT_MESSAGE, mFailure.getMessage()));
        if (getCallingActivity() == null) {
            // The Game card's start or a test's: a starter that waits for the result says it in its own words.
            String line = mFailure.cardLine();
            runOnUiThread(() -> Toast.makeText(this, line, Toast.LENGTH_LONG).show());
        }
    }

    /**
     * On SDL's thread, before the program: what the start needs fetched or copied (Preparation), shown on the activity, and
     * then the program, on the surface of the activity that shows it then. A preparation that fails, or that Back cancels,
     * ends the start with its code, and SDL finishes the activity after main as it does at a program's end.
     */
    @Override
    protected void main() {
        Preparation preparation = sPreparation;
        sPreparation = null;
        if (preparation != null) {
            sPreparing = true;
            try {
                preparation.run();
            } catch (GameStart.Failure failure) {
                // The activity that shows the start now: Android may have made it anew meanwhile.
                ((GameActivity) mSingleton).endPreparation(failure);
                return;
            }
            // Android may have destroyed the activity in the back meanwhile, and with it the surface: the program starts on the
            // next one's, which SDL's onCreate sets up (and clears the field for, for a moment).
            for (SDLSurface surface = mSurface; surface == null || !((GameSurface) surface).ready(); surface = mSurface) {
                SystemClock.sleep(50);
            }
            sPreparing = false;
        }
        super.main();
    }

    private void endPreparation(GameStart.Failure failure) {
        mFailure = failure;
        for (String line : failure.reportLines()) {
            GameReport.append(this, line);
        }
        result();
    }

    /** The preparation's step over the surface while it runs, which the poll keeps up to date; gone when it ends. */
    private void showPreparation() {
        String step = Preparation.step();
        if (step == null) {
            if (mPreparing != null) {
                mLayout.removeView(mPreparing);
                mPreparing = null;
            }
            return;
        }
        if (mPreparing == null) {
            mPreparing = new TextView(this);
            mPreparing.setTextColor(Color.WHITE);
            mPreparing.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
            mPreparing.setGravity(Gravity.CENTER);
            RelativeLayout.LayoutParams place = new RelativeLayout.LayoutParams(RelativeLayout.LayoutParams.WRAP_CONTENT,
                RelativeLayout.LayoutParams.WRAP_CONTENT);
            place.addRule(RelativeLayout.CENTER_IN_PARENT);
            mLayout.addView(mPreparing, place);
        }
        mPreparing.setText(step + "\n\nBack cancels the start");
    }

    /** The program's command line without its display mode, with its environment set; or why the game cannot start. */
    private List<String> prepare() throws GameStart.Failure {
        Storage storage;
        try {
            storage = new Storage(this);
        } catch (IOException e) {
            throw GameStart.Failure.storage(e.getMessage());
        }
        int archives = ScfaImport.archives(storage);
        if (archives == 0) {
            throw GameStart.Failure.noScfa();
        }
        if (ScfaImport.fonts(storage) == 0) {
            throw GameStart.Failure.noFonts();
        }
        int version = GameStart.version(storage, extra(GameStart.EXTRA_GAME_VERSION));
        // A launcher's map and mods, which LaunchGameActivity has checked already; a test's are checked here.
        String map = Launch.map(extra(Launch.EXTRA_MAP));
        String[] mods = Launch.mods(extra(Launch.EXTRA_MODS));
        String[] modFiles = Launch.modFiles(extra(Launch.EXTRA_MOD_FILES), mods);
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
            throw GameStart.Failure.initFile(e.getMessage());
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
            throw GameStart.Failure.environment(e.getMessage());
        }
        String hudScale = HudScale.argument(hudScale());
        List<String> arguments = GameStart.arguments(game, init, GameReport.log(storage), hudScale,
            HudScale.argument(HudScale.most(pictureLines())), ScfaImport.movies(storage) == 0,
            getIntent().getStringExtra(EXTRA_ARGUMENTS), extra(GameStart.EXTRA_LAUNCHER_ARGUMENTS));
        Uri replay = getIntent().getData();
        File copy = null;
        if (replay != null) {
            // The launcher's replay, which the preparation copies here through the grant its start passed on.
            copy = new File(getCacheDir(), Preparation.REPLAY_COPY);
            arguments.addAll(Arrays.asList("/replay", copy.getPath()));
        }
        sPreparation = map != null || mods.length > 0 || replay != null
            ? new Preparation(this, storage, vault, map, mods, modFiles, replay, copy) : null;
        GameReport.append(this, "game: FAF game version " + version + ", " + archives + " SCFA archives");
        GameReport.append(this, "game: HUD scale " + hudScale + " on " + screenInWords());
        return arguments;
    }

    /** The start's extra of that name, of whatever type the starter gave it (GameStart checks it); null without one. */
    @SuppressWarnings("deprecation")
    private Object extra(String name) {
        Bundle extras = getIntent().getExtras();
        return extras != null ? extras.get(name) : null;
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
        String[] arguments = GameStart.commandLine(sArguments, picture.width(), picture.height());
        GameReport.append(this, String.format(Locale.ROOT, "game: display mode %dx%d on a %dx%d view", picture.width(),
            picture.height(), mSurface.getWidth(), mSurface.getHeight()));
        GameReport.append(this, "game: command line " + GameStart.inWords(arguments));
        return arguments;
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
        mHandler.removeCallbacks(mPoll);
        if (mBesideProgram) {
            // SDL set nothing up for this activity (refuseBesideProgram): the HID manager that its onDestroy lets go of is the
            // one of the activity that shows the program.
            HIDDeviceManager hid = mHIDDeviceManager;
            mHIDDeviceManager = null;
            try {
                destroyWithoutQuit();
            } finally {
                mHIDDeviceManager = hid;
            }
            return;
        }
        if (runningProgram() != null && !isFinishing()) {
            // Android destroys a stopped activity when it wants the memory, and at once under the developer option "Don't
            // keep activities": the program goes on, as a Windows program does without its window, and the next activity
            // shows it (onCreate).
            Game.report("game: the activity is destroyed, the program goes on");
            // SDL's clipboard handler listens on this activity's clipboard, which the system then holds the activity by;
            // the next activity makes a handler of its own.
            getSystemService(ClipboardManager.class).removePrimaryClipChangedListener(mClipboardHandler);
            destroyWithoutQuit();
            return;
        }
        super.onDestroy();
        // A game that could not start leaves no process behind: SDL's activity takes the next start in this
        // process for one made anew around a program, and would end it. So does one whose launcher finished it while its
        // preparation ran (Activity.finishActivity), which would start the program after SDL's quit.
        if (mFailure != null || sPreparing) {
            System.exit(0);
        }
    }

    /**
     * SDL's onDestroy for an activity while the program goes on: SDL's would end the program (nativeSendQuit) and take down
     * the lifecycle's mutexes under it (nativeQuit); for an activity whose libraries did not load it does only the rest.
     */
    private void destroyWithoutQuit() {
        mBrokenLibraries = true;
        try {
            super.onDestroy();
        } finally {
            mBrokenLibraries = false;
        }
    }

    /** SDL's thread with the program on it, which this process has until the program ends; null before it starts. */
    private static Thread runningProgram() {
        Thread program = mSDLThread;
        return program != null && program.isAlive() ? program : null;
    }

    @Override
    protected void onResume() {
        super.onResume();
        NetworkGameService.front(true);
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
            // Still allowed now, which it is not from the back: a network game that began since the last poll has its
            // service too.
            NetworkGameService.startIfNeeded(this);
        }
        NetworkGameService.front(false);
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
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK && Preparation.step() != null) {
            // While a preparation runs, which knows no Esc yet, Back cancels the start.
            if (event.getAction() == KeyEvent.ACTION_UP) {
                Cancellation.request();
            }
            return true;
        }
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
