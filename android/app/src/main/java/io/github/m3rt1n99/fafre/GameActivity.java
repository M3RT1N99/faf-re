package io.github.m3rt1n99.fafre;

import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

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
 * the whole screen, Back as Esc, the multicast lock that finds LAN games. The program keeps running while the
 * activity is in the back (android/native/src/app/GameMainAndroid.cpp). The touch scheme Griff, the bar, the
 * keyboard's button and the HUD's scale are not on this activity yet; {@link LegacyGameActivity} has them on
 * the earlier host.
 */
public final class GameActivity extends SDLActivity {
    /** The program's path in its install, as FAF's client starts it: the engine takes its folder from it. */
    private static final String PROGRAM = "bin/ForgedAlliance.exe";

    /** The program's command line without its display mode, or null with mProblem saying why there is none. */
    private List<String> mArguments;
    private String mProblem;
    /** Held while the activity is in front: Wi-Fi drops the broadcasts that find LAN games on a phone without it. */
    private WifiManager.MulticastLock mDiscoveryLock;

    @Override
    protected void onCreate(Bundle state) {
        // Before SDL's activity loads the library: the program reads its environment when it starts.
        GameReport.begin(this);
        mProblem = prepare();
        super.onCreate(state);
        if (mProblem != null) {
            GameReport.append(this, GameReport.END + mProblem);
            Toast.makeText(this, mProblem, Toast.LENGTH_LONG).show();
            // Before it is resumed, so SDL's activity starts no program.
            finish();
            return;
        }
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
            // The game's own UI: the UI package for fingers (android/TOUCH_UI.md) comes back with Griff.
            init = GameFiles.runInit(game, "game", vault);
        } catch (IOException e) {
            return "FAIL: the init file: " + e.getMessage();
        }
        File home = Storage.mkdirs(GameReport.home(storage));
        try {
            Os.setenv("HOME", home.getPath(), true);
            Os.setenv("TMPDIR", getCacheDir().getPath(), true);
            Os.setenv("FAF_GAME_REPORT", GameReport.file(this).getPath(), true);
        } catch (ErrnoException e) {
            return "FAIL: the program's environment: " + e.getMessage();
        }
        mArguments = new ArrayList<>(Arrays.asList(new File(game, PROGRAM).getPath(), "/init", init.getPath(),
            "/log", GameReport.log(storage).getPath(),
            // The engine would send its records of desyncs and of a game's end to its author's server, with the
            // players' names, and nobody here has agreed to that.
            "/notelemetry"));
        // An import by an earlier release has no movies, and the splash screen would wait for a tap for each.
        if (ScfaImport.movies(storage) == 0) {
            mArguments.add("/nomovie");
        }
        GameReport.append(this, "game: FAF game version " + version + ", " + archives + " SCFA archives");
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

    /** On SDL's thread, once the surface is there: the program draws in the screen's mode, the surface's size. */
    @Override
    protected String[] getArguments() {
        int width = mSurface.getWidth();
        int height = mSurface.getHeight();
        List<String> arguments = new ArrayList<>(mArguments);
        arguments.addAll(Arrays.asList("/fullscreen", Integer.toString(width), Integer.toString(height)));
        GameReport.append(this, String.format(Locale.ROOT, "game: display mode %dx%d", width, height));
        return arguments.toArray(new String[0]);
    }

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
    }

    @Override
    protected void onPause() {
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
