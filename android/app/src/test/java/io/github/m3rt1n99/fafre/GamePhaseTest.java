package io.github.m3rt1n99.fafre;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import android.app.GameState;

import org.junit.Test;

/**
 * What the game tells Android of what it does (GamePhase): loading until the program's first frame, then the menus, a game or
 * a network game, with GameState's values for each, and each change told once.
 */
public class GamePhaseTest {
    @Test
    public void theProgramLoadsUntilItsFirstFrame() {
        assertEquals(GamePhase.Phase.LOADING, GamePhase.of(false, false, false));
        assertEquals("a launcher's game connects while it loads", GamePhase.Phase.LOADING, GamePhase.of(false, true, true));
        assertTrue(GamePhase.Phase.LOADING.loading);
        assertEquals(GameState.MODE_NONE, GamePhase.Phase.LOADING.mode);
    }

    @Test
    public void afterItTheMenusOrAGame() {
        assertEquals(GamePhase.Phase.MENUS, GamePhase.of(true, false, false));
        assertEquals("a network lobby, and the score screen after a network game", GamePhase.Phase.MENUS,
            GamePhase.of(true, false, true));
        assertEquals(GamePhase.Phase.GAME, GamePhase.of(true, true, false));
        assertEquals(GamePhase.Phase.NETWORK_GAME, GamePhase.of(true, true, true));

        assertEquals(GameState.MODE_NONE, GamePhase.Phase.MENUS.mode);
        assertEquals(GameState.MODE_GAMEPLAY_INTERRUPTIBLE, GamePhase.Phase.GAME.mode);
        assertEquals("the others play on", GameState.MODE_GAMEPLAY_UNINTERRUPTIBLE, GamePhase.Phase.NETWORK_GAME.mode);
        for (GamePhase.Phase phase : new GamePhase.Phase[] {GamePhase.Phase.MENUS, GamePhase.Phase.GAME,
                GamePhase.Phase.NETWORK_GAME}) {
            assertFalse(phase.name(), phase.loading);
        }
    }

    @Test
    public void androidHearsEachChangeOnce() {
        GamePhase phase = new GamePhase();
        assertEquals("the first poll's", GamePhase.Phase.LOADING, phase.next(false, false, false));
        assertNull(phase.next(false, false, false));
        assertEquals(GamePhase.Phase.MENUS, phase.next(true, false, false));
        assertNull("a lobby", phase.next(true, false, true));
        assertEquals(GamePhase.Phase.NETWORK_GAME, phase.next(true, true, true));
        assertNull(phase.next(true, true, true));
        assertEquals("its score screen", GamePhase.Phase.MENUS, phase.next(true, false, true));
        assertEquals("a skirmish", GamePhase.Phase.GAME, phase.next(true, true, false));
    }
}
