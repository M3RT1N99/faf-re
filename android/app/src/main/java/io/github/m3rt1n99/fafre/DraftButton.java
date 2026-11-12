package io.github.m3rt1n99.fafre;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.view.View;

/**
 * The touch scheme Griff's tick or cross while its draft is open (buildstage/griff-v3-spec.md T2.10, the mockup's drawButton with
 * ICON_TICK and ICON_CROSS): a square with rounded corners in the mockup's button colours, its icon a tick in green or a cross in
 * red. A view of its own over the game, so a finger on it is its own, not the map's.
 */
final class DraftButton extends View {
    private static final int BUTTON = 0xDB14212C;
    private static final int BUTTON_ON = 0xFF0F766E;
    private static final int EDGE = 0xFF3F5868;
    private static final int OK = 0xFF4ADE80;
    private static final int ALARM = 0xFFF87171;
    /** The mockup's CSS pixel on the phone it was made for, in mm. */
    private static final float CSS_PIXEL_MM = 1 / 5.64f;
    private static final float CORNER_MM = 1.2f;
    /** The icon's size in the button's side, and its line's in the icon's. */
    private static final float ICON = 0.27f;
    private static final float ICON_LINE = 0.32f;

    private final boolean mTick;
    private final Paint mFill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mEdge = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mIcon = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF mBox = new RectF();
    private final Path mPath = new Path();
    private float mMillimetre = 10;

    /** `tick`: the tick, else the cross. */
    DraftButton(Context context, boolean tick) {
        super(context);
        mTick = tick;
        mEdge.setStyle(Paint.Style.STROKE);
        mEdge.setColor(EDGE);
        mIcon.setStyle(Paint.Style.STROKE);
        mIcon.setStrokeCap(Paint.Cap.ROUND);
        mIcon.setStrokeJoin(Paint.Join.ROUND);
        mIcon.setColor(tick ? OK : ALARM);
        setContentDescription(tick ? "Order the draft" : "Drop the draft");
    }

    /** The screen's pixels in a mm on the glass. */
    void setMillimetre(float millimetre) {
        mMillimetre = millimetre;
        mEdge.setStrokeWidth(Math.max(1, CSS_PIXEL_MM * millimetre));
        invalidate();
    }

    @Override
    protected void drawableStateChanged() {
        super.drawableStateChanged();
        invalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        float half = mEdge.getStrokeWidth() / 2;
        mBox.set(half, half, getWidth() - half, getHeight() - half);
        float corner = CORNER_MM * mMillimetre;
        mFill.setColor(isPressed() ? BUTTON_ON : BUTTON);
        canvas.drawRoundRect(mBox, corner, corner, mFill);
        canvas.drawRoundRect(mBox, corner, corner, mEdge);
        float x = getWidth() / 2f;
        float y = getHeight() / 2f;
        float r = Math.min(getWidth(), getHeight()) * ICON;
        mIcon.setStrokeWidth(Math.max(2 * CSS_PIXEL_MM * mMillimetre, ICON_LINE * r));
        mPath.rewind();
        if (mTick) {
            mPath.moveTo(x - r, y + 0.1f * r);
            mPath.lineTo(x - 0.25f * r, y + 0.8f * r);
            mPath.lineTo(x + r, y - 0.75f * r);
        } else {
            mPath.moveTo(x - 0.8f * r, y - 0.8f * r);
            mPath.lineTo(x + 0.8f * r, y + 0.8f * r);
            mPath.moveTo(x + 0.8f * r, y - 0.8f * r);
            mPath.lineTo(x - 0.8f * r, y + 0.8f * r);
        }
        canvas.drawPath(mPath, mIcon);
    }
}
