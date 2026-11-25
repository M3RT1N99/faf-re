package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import io.github.m3rt1n99.fafre.NetworkGameRule.Need;

import org.junit.Test;

public class NetworkGameRuleTest {
    private final NetworkGameRule mRule = new NetworkGameRule();

    /** What a network lobby or game needs, with no replay. */
    private Need network(boolean socketOpen, boolean playing, long now) {
        return mRule.need(socketOpen, playing, false, now);
    }

    @Test
    public void startsInFrontWhenTheNetworkIsInUseOrAReplayPlays() {
        mRule.front(true, 0);
        assertTrue(mRule.shouldStart(Need.NETWORK));
        assertTrue(mRule.shouldStart(Need.REPLAY));
    }

    @Test
    public void noStartWhenNothingNeedsIt() {
        mRule.front(true, 0);
        assertFalse(mRule.shouldStart(Need.NONE));
    }

    @Test
    public void noStartBeforeTheFirstResume() {
        assertFalse(mRule.shouldStart(Need.NETWORK));
        assertFalse(mRule.shouldStart(Need.REPLAY));
    }

    @Test
    public void noStartFromTheBack() {
        mRule.front(true, 0);
        mRule.front(false, 0);
        assertFalse(mRule.shouldStart(Need.NETWORK));
        assertFalse(mRule.shouldStart(Need.REPLAY));
    }

    @Test
    public void noSecondStartUntilItHasStopped() {
        mRule.front(true, 0);
        mRule.started();
        assertFalse(mRule.shouldStart(Need.NETWORK));
        assertFalse("a replay after a network game takes the running service", mRule.shouldStart(Need.REPLAY));
        mRule.stopped();
        assertTrue(mRule.shouldStart(Need.NETWORK));
    }

    @Test
    public void aRefusalWaitsUntilTheActivityIsInFrontAnew() {
        mRule.front(true, 0);
        mRule.started();
        mRule.refused();
        assertFalse("not at each poll", mRule.shouldStart(Need.NETWORK));
        mRule.stopped();
        assertFalse("the refused service's end says nothing", mRule.shouldStart(Need.NETWORK));
        assertFalse("nor does a replay", mRule.shouldStart(Need.REPLAY));
        mRule.front(false, 0);
        assertFalse(mRule.shouldStart(Need.NETWORK));
        mRule.front(true, 0);
        assertTrue(mRule.shouldStart(Need.NETWORK));
    }

    @Test
    public void aLobbyNeedsTheNetworkAndNoSocketDoesNot() {
        assertEquals(Need.NONE, network(false, false, 0));
        assertEquals(Need.NETWORK, network(true, false, 0));
        assertEquals(Need.NETWORK, network(true, false, 100000));
    }

    @Test
    public void aGameThatHasEndedNeedsNoMoreThoughItsSocketStaysOpen() {
        assertEquals(Need.NETWORK, network(true, true, 1000));
        assertEquals(Need.NETWORK, network(true, false, 2000));
        assertEquals(Need.NETWORK, network(true, false, 2000 + NetworkGameRule.OVER_MILLISECONDS - 1));
        assertEquals(Need.NONE, network(true, false, 2000 + NetworkGameRule.OVER_MILLISECONDS));
    }

    @Test
    public void aFrameThatDoesNotSeeTheGameEndsNothing() {
        assertEquals(Need.NETWORK, network(true, true, 1000));
        assertEquals(Need.NETWORK, network(true, false, 2000));
        assertEquals(Need.NETWORK, network(true, true, 2500));
        assertEquals(Need.NETWORK, network(true, false, 3000));
        assertEquals(Need.NETWORK, network(true, false, 3000 + NetworkGameRule.OVER_MILLISECONDS - 1));
    }

    @Test
    public void theNextLobbyNeedsItAgain() {
        assertEquals(Need.NETWORK, network(true, true, 0));
        assertEquals(Need.NETWORK, network(true, false, NetworkGameRule.OVER_MILLISECONDS * 2));
        assertEquals(Need.NONE, network(true, false, NetworkGameRule.OVER_MILLISECONDS * 3));
        assertEquals(Need.NONE, network(false, false, NetworkGameRule.OVER_MILLISECONDS * 4));
        assertEquals(Need.NETWORK, network(true, false, NetworkGameRule.OVER_MILLISECONDS * 5));
    }

    /** A replay has no socket: Android ended its process in the back (TOO MANY CACHED PROCS) before it had a service. */
    @Test
    public void aReplayThatPlaysNeedsTheServiceWithoutTheNetwork() {
        assertEquals(Need.REPLAY, mRule.need(false, true, true, 0));
        assertEquals("for as long as it plays", Need.REPLAY, mRule.need(false, true, true, 100000));
    }

    @Test
    public void aReplayThatIsNotPlayingNeedsNothing() {
        // As it loads, and once it is over: the game over panel and the score screen have nothing left to play.
        assertEquals(Need.NONE, mRule.need(false, false, true, 0));
        assertEquals(Need.REPLAY, mRule.need(false, true, true, 1000));
        assertEquals(Need.NONE, mRule.need(false, false, true, 2000));
    }

    @Test
    public void aSkirmishNeedsNothing() {
        assertEquals(Need.NONE, mRule.need(false, true, false, 0));
    }

    @Test
    public void theNetworkComesBeforeAReplay() {
        assertEquals(Need.NETWORK, mRule.need(true, true, true, 0));
        assertEquals("an open socket after the game, as before", Need.NETWORK, mRule.need(true, false, true, 1000));
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
