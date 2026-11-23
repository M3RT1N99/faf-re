package io.github.m3rt1n99.fafre;

import android.content.Context;
import android.graphics.Matrix;
import android.os.Build;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

import java.util.function.Predicate;

/**
 * SDL's surface for the game. Its buffers have the picture's size (PictureSize, SurfaceHolder.setFixedSize), which the
 * compositor scales to the view, and the view fills the screen. SDL takes a finger's place over the buffers' size
 * (SDLSurface.onTouch) and the mouse's as the view gives it, both in the view's pixels: so they come to SDL in the picture's
 * pixels, and land where they are on the glass.
 */
final class GameSurface extends SDLSurface {
    private final Matrix mToPicture = new Matrix();

    GameSurface(Context context) {
        super(context);
        // In place of the one SDLSurface sets: the mouse's moves, its wheel and its buttons without a press.
        View.OnGenericMotionListener sdl = SDLActivity.getMotionListener();
        setOnGenericMotionListener((view, event) -> inPicture(event, picture -> sdl.onGenericMotion(view, picture)));
        // SDL keeps the focus on the surface: after a keyboard's keys Android framed the whole picture as the focused view.
        setDefaultFocusHighlightEnabled(false);
    }

    @Override
    public boolean onTouch(View view, MotionEvent event) {
        return inPicture(event, picture -> super.onTouch(view, picture));
    }

    /**
     * SDL's watch of the keyboard (SDLSurface), and how much of the view's height the keyboard covers, which the program keeps
     * a page's text field above (Game.keyboardCovers). Android 11 and later say it; before, the program takes half.
     */
    @Override
    public WindowInsets onApplyWindowInsets(View view, WindowInsets insets) {
        WindowInsets passed = super.onApplyWindowInsets(view, insets);
        int height = getHeight();
        if (Build.VERSION.SDK_INT >= 30 && height > 0 && Game.loadError() == null) {
            boolean shown = insets.isVisible(WindowInsets.Type.ime());
            Game.keyboardCovers(shown ? (float) insets.getInsets(WindowInsets.Type.ime()).bottom / height : 0f);
        }
        return passed;
    }

    /** SDL has the surface, from surfaceChanged to surfaceDestroyed: what a program needs to start on. */
    boolean ready() {
        return mIsSurfaceReady;
    }

    /**
     * What `handler` makes of `event` in the picture's pixels: of a copy, where the picture's size is not the view's. Only a
     * pointer's places are on the screen (a finger's, a mouse's, a pen's): a joystick's axes and a touchpad's places are not,
     * and older Androids' transform scales those too.
     */
    private boolean inPicture(MotionEvent event, Predicate<MotionEvent> handler) {
        int viewWidth = getWidth();
        int viewHeight = getHeight();
        // mWidth and mHeight are the buffers' size from SDL's surfaceChanged on, 1 before it.
        if (!event.isFromSource(InputDevice.SOURCE_CLASS_POINTER) || viewWidth <= 0 || viewHeight <= 0 || mWidth <= 1
            || mHeight <= 1 || (mWidth == viewWidth && mHeight == viewHeight)) {
            return handler.test(event);
        }
        MotionEvent picture = MotionEvent.obtain(event);
        mToPicture.setScale(mWidth / viewWidth, mHeight / viewHeight);
        picture.transform(mToPicture);
        try {
            return handler.test(picture);
        } finally {
            picture.recycle();
        }
    }
}
