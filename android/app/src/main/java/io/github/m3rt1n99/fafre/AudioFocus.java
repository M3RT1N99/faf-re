package io.github.m3rt1n99.fafre;

import android.media.AudioManager;

/**
 * The program's hold on Android's audio focus, which its sound follows (GameActivity asks for it in front, and the sound
 * pauses without it: android/native/src/app/SoundAndroid.cpp), and the report's line when another app takes it and when
 * it comes back, with the sound. On the activity's thread; one for the program, as an activity that Android makes anew
 * around it goes on from what the last one had. No Android in here but AudioManager's constants, for the tests.
 */
final class AudioFocus {
    /** As the program's sound starts with it. */
    private boolean mHeld = true;
    /** Gone for good: nothing gives it back, as the end of a call does; the game asks for it again (GameActivity). */
    private boolean mLostForGood;

    /** Whether the game holds it, as the program is told. */
    boolean held() {
        return mHeld;
    }

    boolean lostForGood() {
        return mLostForGood;
    }

    /** Android's answer to the game's request (AudioManager.requestAudioFocus); the report's line, or null for none. */
    String requested(int answer) {
        mLostForGood = answer == AudioManager.AUDIOFOCUS_REQUEST_FAILED;
        switch (answer) {
            case AudioManager.AUDIOFOCUS_REQUEST_GRANTED:
                return held(true, null);
            case AudioManager.AUDIOFOCUS_REQUEST_DELAYED:
                return held(false, "another app has the audio focus until it lets go of it, as a call does");
            default:
                return held(false, "Android refused the game the audio focus: the game asks again at a touch, once no "
                    + "other app plays");
        }
    }

    /** Android's change of it (AudioManager.OnAudioFocusChangeListener); the report's line, or null for none. */
    String changed(int change) {
        switch (change) {
            case AudioManager.AUDIOFOCUS_GAIN:
                mLostForGood = false;
                return held(true, null);
            case AudioManager.AUDIOFOCUS_LOSS:
                mLostForGood = true;
                return held(false, "another app took the audio focus for good: the game asks for it again at a touch, "
                    + "once no other app plays");
            case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT:
            case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK:
                return held(false, "another app has the audio focus for a while");
            default:
                return null;
        }
    }

    /** The line of a change of the hold, {@code lost} the loss's words; null for none. */
    private String held(boolean held, String lost) {
        if (held == mHeld) {
            return null;
        }
        mHeld = held;
        return "game: " + (held ? "the audio focus is back, and with it the game's sound" : lost);
    }
}
