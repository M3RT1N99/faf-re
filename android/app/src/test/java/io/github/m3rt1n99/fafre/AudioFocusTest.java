package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import android.media.AudioManager;

import org.junit.Test;

/**
 * The program's hold on the audio focus (AudioFocus): what the program is told, and the report's line when another app
 * takes it and when it comes back with the sound, however it comes back, and no line where nothing changed.
 */
public class AudioFocusTest {
    private static final String BACK = "game: the audio focus is back, and with it the game's sound";

    @Test
    public void theGamesOwnRequestsSayNothingWhileItHoldsIt() {
        AudioFocus focus = new AudioFocus();
        assertNull(focus.requested(AudioManager.AUDIOFOCUS_REQUEST_GRANTED));
        assertNull("another resume", focus.requested(AudioManager.AUDIOFOCUS_REQUEST_GRANTED));
        assertNull(focus.changed(AudioManager.AUDIOFOCUS_GAIN));
        assertTrue(focus.held());
    }

    @Test
    public void aShortLossComesBackWhenAndroidGivesItBack() {
        AudioFocus focus = new AudioFocus();
        assertEquals("game: another app has the audio focus for a while", focus.changed(AudioManager.AUDIOFOCUS_LOSS_TRANSIENT));
        assertFalse(focus.held());
        assertNull("still lost", focus.changed(AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK));
        assertFalse(focus.lostForGood());
        assertEquals(BACK, focus.changed(AudioManager.AUDIOFOCUS_GAIN));
        assertTrue(focus.held());
        assertNull(focus.changed(AudioManager.AUDIOFOCUS_GAIN));
    }

    @Test
    public void aLossBeforeTheBackComesBackWithTheResumesRequest() {
        AudioFocus focus = new AudioFocus();
        focus.changed(AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK);
        assertEquals(BACK, focus.requested(AudioManager.AUDIOFOCUS_REQUEST_GRANTED));
    }

    @Test
    public void aLossForGoodComesBackWithTheRequestAfterIt() {
        AudioFocus focus = new AudioFocus();
        assertEquals("game: another app took the audio focus for good: the game asks for it again at a touch, once no other "
            + "app plays", focus.changed(AudioManager.AUDIOFOCUS_LOSS));
        assertTrue(focus.lostForGood());
        assertEquals(BACK, focus.requested(AudioManager.AUDIOFOCUS_REQUEST_GRANTED));
        assertFalse(focus.lostForGood());
        assertTrue(focus.held());
        // Android's own word gives it back too.
        focus.changed(AudioManager.AUDIOFOCUS_LOSS);
        assertEquals(BACK, focus.changed(AudioManager.AUDIOFOCUS_GAIN));
        assertFalse(focus.lostForGood());
    }

    @Test
    public void aCallsFocusComesLaterAndARefusalIsAskedForAgain() {
        AudioFocus focus = new AudioFocus();
        assertEquals("game: another app has the audio focus until it lets go of it, as a call does",
            focus.requested(AudioManager.AUDIOFOCUS_REQUEST_DELAYED));
        assertFalse(focus.held());
        assertFalse("Android gives it later", focus.lostForGood());
        assertEquals(BACK, focus.changed(AudioManager.AUDIOFOCUS_GAIN));

        assertEquals("game: Android refused the game the audio focus: the game asks again at a touch, once no other app plays",
            focus.requested(AudioManager.AUDIOFOCUS_REQUEST_FAILED));
        assertTrue(focus.lostForGood());
        assertNull("no change", focus.changed(AudioManager.AUDIOFOCUS_GAIN_TRANSIENT));
        assertFalse(focus.held());
        assertEquals(BACK, focus.requested(AudioManager.AUDIOFOCUS_REQUEST_GRANTED));
    }
}
