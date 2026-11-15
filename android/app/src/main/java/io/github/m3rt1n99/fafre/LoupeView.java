package io.github.m3rt1n99.fafre;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Rect;
import android.graphics.RectF;
import android.view.View;

/**
 * The loupe of the touch scheme Griff's draft (buildstage/touch-concept-v2.md, the mockup buildstage/touch-preview): the 6 mm
 * of the game's picture about a resting finger, which the finger covers, at twice their size in a circle above it, with the
 * ghost of the plan its lift places that the game draws there. FAF's UI cannot show a magnified copy of the picture (it has no
 * picture of a picture, no clipping, and every camera's zoom stops at the console's nearest and has its field of view by the
 * zoom), so the copy is the system's (PixelCopy) and this view shows it; FAF's UI frames it with its marker ring
 * (griffmap.lua), where the activity says the circle is (Game.SHOWN_LOUPE). It takes no fingers. In the screen's pixels.
 */
final class LoupeView extends View {
    /** The loupe: a circle of this radius, this far above the finger (below it where the top has no room), the copy twice its size. */
    static final float RADIUS_MM = 6;
    private static final float ABOVE_MM = 14;
    private static final float ROOM_MM = 1;
    private static final float MAGNIFIED = 2;
    /** Behind the copy where it does not reach: the game's own black past the map's edges. */
    private static final int BACKGROUND = 0xFF000000;

    /** Where the loupe's circle goes, for FAF's UI to frame it: its middle and radius in the screen's pixels, or none. */
    interface Placed {
        void placed(boolean shown, float x, float y, float radius);
    }

    private final Placed mPlaced;
    private final Paint mBackground = new Paint();
    private final Paint mPicture = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final Path mCircle = new Path();
    private final Rect mSource = new Rect();
    private final RectF mPlace = new RectF();
    private float mMillimetre = 10;
    private Bitmap mCopy;
    private float mCopyX;
    private float mCopyY;
    private float mFingerX;
    private float mFingerY;
    private boolean mWasShown;
    private float mWasX;
    private float mWasY;

    LoupeView(Context context, Placed placed) {
        super(context);
        mPlaced = placed;
        mBackground.setColor(BACKGROUND);
    }

    /** The screen's pixels in a mm on the glass. */
    void setMillimetre(float millimetre) {
        mMillimetre = millimetre;
        invalidate();
    }

    /**
     * The loupe of a finger at `fingerX`, `fingerY`: the picture of the 6 mm about it (the game's, copied about `copyX`, `copyY`,
     * which is the finger's place but where the copy met the picture's edge or the finger has moved since), or none.
     */
    void show(Bitmap copy, float copyX, float copyY, float fingerX, float fingerY) {
        mCopy = copy;
        mCopyX = copyX;
        mCopyY = copyY;
        mFingerX = fingerX;
        mFingerY = fingerY;
        invalidate();
        if (copy == null) {
            place(false, 0, 0);
            return;
        }
        float radius = RADIUS_MM * mMillimetre;
        float room = radius + ROOM_MM * mMillimetre;
        float x = Math.max(room, Math.min(getWidth() - room, fingerX));
        float above = fingerY - ABOVE_MM * mMillimetre;
        place(true, x, above < room ? fingerY + ABOVE_MM * mMillimetre : above);
    }

    private void place(boolean shown, float x, float y) {
        if (shown != mWasShown || (shown && (x != mWasX || y != mWasY))) {
            mWasShown = shown;
            mWasX = x;
            mWasY = y;
            mPlaced.placed(shown, x, y, RADIUS_MM * mMillimetre);
        }
    }

    /**
     * The copy where it was taken, at twice its size about the finger's spot in the circle: within 3 mm of the picture's edge,
     * or where the finger has moved on since the copy, beside the finger over black, so the finger's spot is always the
     * circle's middle.
     */
    @Override
    protected void onDraw(Canvas canvas) {
        if (mCopy == null || !mWasShown) {
            return;
        }
        float radius = RADIUS_MM * mMillimetre;
        float x = mWasX;
        float y = mWasY;
        mCircle.rewind();
        mCircle.addCircle(x, y, radius, Path.Direction.CW);
        canvas.save();
        canvas.clipPath(mCircle);
        canvas.drawCircle(x, y, radius, mBackground);
        mSource.set(0, 0, mCopy.getWidth(), mCopy.getHeight());
        float pictureX = x + (mCopyX - mFingerX) * MAGNIFIED;
        float pictureY = y + (mCopyY - mFingerY) * MAGNIFIED;
        mPlace.set(pictureX - radius, pictureY - radius, pictureX + radius, pictureY + radius);
        canvas.drawBitmap(mCopy, mSource, mPlace, mPicture);
        canvas.restore();
    }
}
