package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertTrue;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Random;
import java.util.Set;

import org.junit.Test;

/**
 * The tokens a launcher's start carries (PlayTokens): the status provider gives one to an allowed app, and LaunchGameActivity
 * lets a start through only with one given out within the minute before and not spent, once. Without them an app that the
 * client started for a result could forward PLAY under the client's name.
 */
public class PlayTokensTest {
    private static PlayTokens tokens() {
        return new PlayTokens(new Random(7));
    }

    @Test
    public void aTokenIs128RandomBitsInHex() {
        PlayTokens tokens = tokens();
        Set<String> issued = new HashSet<>();
        for (int i = 0; i < 100; ++i) {
            String token = tokens.issue(0);
            assertTrue(token, token.matches("[0-9a-f]{32}"));
            issued.add(token);
        }
        assertEquals("each one another", 100, issued.size());
    }

    @Test
    public void aStartSpendsItsTokenOnce() {
        PlayTokens tokens = tokens();
        String token = tokens.issue(1000);
        assertTrue(tokens.spend(token, 1000));
        assertFalse("spent", tokens.spend(token, 1000));
    }

    @Test
    public void aTokenIsGoodForAMinuteAfterItsCall() {
        PlayTokens tokens = tokens();
        String inTime = tokens.issue(1000);
        String late = tokens.issue(1000);
        assertTrue(tokens.spend(inTime, 1000 + PlayTokens.LIFETIME_MS));
        assertFalse(tokens.spend(late, 1000 + PlayTokens.LIFETIME_MS + 1));
        assertFalse("gone with its late start", tokens.spend(late, 1000));
    }

    @Test
    public void noTokenOrOneTheGameGaveNotOutPassesNot() {
        PlayTokens tokens = tokens();
        String token = tokens.issue(0);
        // Without the extra, of another type, another app's guess, or the token in another case.
        for (Object start : new Object[] {null, 42, new String[] {token}, "", "0123456789abcdef0123456789abcdef",
            token.toUpperCase(Locale.ROOT)}) {
            assertFalse(String.valueOf(start), tokens.spend(start, 0));
        }
        assertTrue("the token itself is still good", tokens.spend(token, 0));
    }

    @Test
    public void aCallBeyondTheMostWaitingDropsTheOldest() {
        PlayTokens tokens = tokens();
        List<String> issued = new ArrayList<>();
        for (int i = 0; i <= PlayTokens.MOST; ++i) {
            issued.add(tokens.issue(i));
        }
        assertFalse("the oldest is gone", tokens.spend(issued.get(0), PlayTokens.MOST));
        for (int i = 1; i <= PlayTokens.MOST; ++i) {
            assertTrue("token " + i, tokens.spend(issued.get(i), PlayTokens.MOST));
        }
    }

    @Test
    public void theAppsTokensAreRandom() {
        String first = PlayTokens.APP.issue(0);
        String second = PlayTokens.APP.issue(0);
        assertNotEquals(first, second);
        assertTrue(PlayTokens.APP.spend(first, 0));
        assertTrue(PlayTokens.APP.spend(second, 0));
    }
}
