package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public class NetworkGameRuleTest {
    private final NetworkGameRule mRule = new NetworkGameRule();

    @Test
    public void startsInFrontWhenTheNetworkIsInUse() {
        mRule.front(true, 0);
        assertTrue(mRule.shouldStart(true));
    }

    @Test
    public void noStartWithoutTheNetwork() {
        mRule.front(true, 0);
        assertFalse(mRule.shouldStart(false));
    }

    @Test
    public void noStartBeforeTheFirstResume() {
        assertFalse(mRule.shouldStart(true));
    }

    @Test
    public void noStartFromTheBack() {
        mRule.front(true, 0);
        mRule.front(false, 0);
        assertFalse(mRule.shouldStart(true));
    }

    @Test
    public void noSecondStartUntilItHasStopped() {
        mRule.front(true, 0);
        mRule.started();
        assertFalse(mRule.shouldStart(true));
        mRule.stopped();
        assertTrue(mRule.shouldStart(true));
    }

    @Test
    public void aRefusalWaitsUntilTheActivityIsInFrontAnew() {
        mRule.front(true, 0);
        mRule.started();
        mRule.refused();
        assertFalse("not at each poll", mRule.shouldStart(true));
        mRule.stopped();
        assertFalse("the refused service's end says nothing", mRule.shouldStart(true));
        mRule.front(false, 0);
        assertFalse(mRule.shouldStart(true));
        mRule.front(true, 0);
        assertTrue(mRule.shouldStart(true));
    }

    @Test
    public void aLobbyNeedsTheNetworkAndNoSocketDoesNot() {
        assertFalse(mRule.needed(false, false, 0));
        assertTrue(mRule.needed(true, false, 0));
        assertTrue(mRule.needed(true, false, 100000));
    }

    @Test
    public void aGameThatHasEndedNeedsNoMoreThoughItsSocketStaysOpen() {
        assertTrue(mRule.needed(true, true, 1000));
        assertTrue(mRule.needed(true, false, 2000));
        assertTrue(mRule.needed(true, false, 2000 + NetworkGameRule.OVER_MILLISECONDS - 1));
        assertFalse(mRule.needed(true, false, 2000 + NetworkGameRule.OVER_MILLISECONDS));
    }

    @Test
    public void aFrameThatDoesNotSeeTheGameEndsNothing() {
        assertTrue(mRule.needed(true, true, 1000));
        assertTrue(mRule.needed(true, false, 2000));
        assertTrue(mRule.needed(true, true, 2500));
        assertTrue(mRule.needed(true, false, 3000));
        assertTrue(mRule.needed(true, false, 3000 + NetworkGameRule.OVER_MILLISECONDS - 1));
    }

    @Test
    public void theNextLobbyNeedsItAgain() {
        assertTrue(mRule.needed(true, true, 0));
        assertTrue(mRule.needed(true, false, NetworkGameRule.OVER_MILLISECONDS * 2));
        assertFalse(mRule.needed(true, false, NetworkGameRule.OVER_MILLISECONDS * 3));
        assertFalse(mRule.needed(false, false, NetworkGameRule.OVER_MILLISECONDS * 4));
        assertTrue(mRule.needed(true, false, NetworkGameRule.OVER_MILLISECONDS * 5));
    }

    @Test
    public void onlyLongInTheBackIsTooLong() {
        assertFalse(mRule.tooLongInTheBack(NetworkGameRule.BACK_LIMIT_MILLISECONDS * 2));
        mRule.front(true, 0);
        assertFalse(mRule.tooLongInTheBack(NetworkGameRule.BACK_LIMIT_MILLISECONDS * 2));
        mRule.front(false, 1000);
        assertFalse(mRule.tooLongInTheBack(1000 + NetworkGameRule.BACK_LIMIT_MILLISECONDS - 1));
        assertTrue(mRule.tooLongInTheBack(1000 + NetworkGameRule.BACK_LIMIT_MILLISECONDS));
        mRule.front(true, 1000 + NetworkGameRule.BACK_LIMIT_MILLISECONDS * 2);
        assertFalse(mRule.tooLongInTheBack(1000 + NetworkGameRule.BACK_LIMIT_MILLISECONDS * 3));
    }
}
