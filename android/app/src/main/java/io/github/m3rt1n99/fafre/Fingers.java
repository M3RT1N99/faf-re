package io.github.m3rt1n99.fafre;

/** What a screen's fingers do, whoever makes something of them: {@link TouchInput} in a game, {@link PageInput} in the menus. */
interface Fingers {
    /** A finger came down; `first`: no other is left on the glass, whatever this class heard of them. */
    void down(int id, float x, float y, long time, boolean first);

    /** Where a finger is now. {@link #moved} follows when all of an event's fingers are in. */
    void move(int id, float x, float y);

    void moved();

    /** A finger lifted; `last`: no other is left on the glass. */
    void up(int id, long time, boolean last);

    /** Android says that a finger which lifted was a palm, with another still down. */
    void forget(int id, long time);

    /** The system took the fingers. */
    void cancel();
}
