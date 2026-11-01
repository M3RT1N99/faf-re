package io.github.m3rt1n99.fafre;

import android.view.Surface;

/**
 * The engine's renderer on this phone (libfafgalsmoke.so, src/platform/android/GalSmoke.cpp): the
 * engine's gal brings up its Vulkan device on a surface and clears and presents frames for a few
 * seconds, without effects or textures. The report is what the device and the engine's log said.
 */
final class GalSmoke {
    private static String sLoadError;

    static {
        try {
            System.loadLibrary("fafgalsmoke");
        } catch (UnsatisfiedLinkError e) {
            sLoadError = String.valueOf(e.getMessage());
        }
    }

    private GalSmoke() {
    }

    /** Null, or why the library did not load. */
    static String loadError() {
        return sLoadError;
    }

    /** Starts the test on `surface` for `seconds`; false if one is running or the surface has no window. */
    static native boolean start(Surface surface, int seconds);

    /** Ends the test early and waits for its thread. */
    static native void stop();

    static native boolean running();

    /** The report so far, one line per event. */
    static native String report();
}
