package io.github.m3rt1n99.fafre;

import android.content.Context;
import android.view.View;

/**
 * Where a finger takes one of the touch scheme Griff's buttons that FAF's UI draws (griffhud.lua: the undo's button, the
 * draft's tick and cross): a view of its own over it, which draws nothing, so that a finger on it is the button's, never the
 * map's beneath. FAF's UI shows the button pressed while a finger holds it, as the activity says (Game.setGriffHud).
 */
final class HudTarget extends View {
    private final Runnable mPressedChanged;

    /** `pressedChanged` runs when a finger comes onto the view or leaves it. */
    HudTarget(Context context, String label, Runnable pressedChanged) {
        super(context);
        mPressedChanged = pressedChanged;
        setContentDescription(label);
        setVisibility(View.GONE);
    }

    @Override
    public void setPressed(boolean pressed) {
        boolean was = isPressed();
        super.setPressed(pressed);
        if (was != pressed) {
            mPressedChanged.run();
        }
    }
}
