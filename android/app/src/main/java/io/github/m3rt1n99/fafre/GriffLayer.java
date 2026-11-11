package io.github.m3rt1n99.fafre;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.view.View;

/**
 * What the touch scheme Griff draws over the game (buildstage/touch-concept-v2.md): the handle beside the selection, with the
 * selection's count and a line to the group's middle, or where the group is out of view or under a panel, an arrow at it; and
 * the stroke a finger draws from it. In the screen's pixels. It takes no fingers: they go through to the layer under it.
 */
final class GriffLayer extends View {
    private static final int OWN = 0xFF2DD4BF;
    private static final int OWN_DEEP = 0xFF115E59;
    private static final int RING = 0xFFF2FFFB;
    private static final int ATTACK = 0xFFFBBF24;
    private static final int GIVEN_UP = 0xFF9AA5AE;
    /** With Box the handle steps aside for a box that starts under it, and shows it. */
    private static final int FAINT_ALPHA = 90;

    private final Paint mFill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mRing = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mLine = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mText = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mStroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path mArrow = new Path();
    private final Path mStrokePath = new Path();
    private float mMillimetre = 10;
    private float mRadius = 55;
    private float mArrowLength = 20;

    private boolean mShown;
    private float mX;
    private float mY;
    private float mGroupX;
    private float mGroupY;
    private int mCount;
    private boolean mOff;
    private int mRail;
    private boolean mStrokeShown;

    GriffLayer(Context context) {
        super(context);
        mFill.setColor(OWN_DEEP);
        mRing.setStyle(Paint.Style.STROKE);
        mLine.setColor(RING);
        mText.setColor(RING);
        mText.setTextAlign(Paint.Align.CENTER);
        mText.setFakeBoldText(true);
        mStroke.setStyle(Paint.Style.STROKE);
        mStroke.setStrokeJoin(Paint.Join.ROUND);
        mStroke.setStrokeCap(Paint.Cap.ROUND);
    }

    /** The screen's pixels in a mm on the glass, the handle's radius in mm and how far beyond its edge its arrow reaches. */
    void setSizes(float millimetre, double radiusMm, double arrowMm) {
        mMillimetre = millimetre;
        mRadius = (float) (radiusMm * millimetre);
        mArrowLength = (float) (arrowMm * millimetre);
        mRing.setStrokeWidth(0.4f * millimetre);
        mLine.setStrokeWidth(0.15f * millimetre);
        mText.setTextSize(3.2f * millimetre);
        mStroke.setStrokeWidth(0.6f * millimetre);
        invalidate();
    }

    /** Where the handle is, if it is shown: its middle, the group's middle, the selection's count, and whether it points at it. */
    void setHandle(boolean shown, float x, float y, float groupX, float groupY, int count, boolean off) {
        if (shown == mShown && (!shown || (x == mX && y == mY && groupX == mGroupX && groupY == mGroupY && count == mCount && off == mOff))) {
            return;
        }
        mShown = shown;
        mX = x;
        mY = y;
        mGroupX = groupX;
        mGroupY = groupY;
        mCount = count;
        mOff = off;
        invalidate();
    }

    /** The rail's buttons that are held or lit, TouchInput's RAIL_ bits. */
    void setRail(int rail) {
        if (rail != mRail) {
            mRail = rail;
            invalidate();
        }
    }

    /** The stroke as far as it goes; count 0: none. `back`: lifting now gives it up. `attack`: its orders are attack-moves. */
    void showStroke(float[] xs, float[] ys, int count, boolean back, boolean attack) {
        mStrokePath.rewind();
        mStrokeShown = count >= 2;
        if (mStrokeShown) {
            mStrokePath.moveTo(xs[0], ys[0]);
            for (int at = 1; at < count; ++at) {
                mStrokePath.lineTo(xs[at], ys[at]);
            }
            mStroke.setColor(back ? GIVEN_UP : (attack ? ATTACK : OWN));
        }
        invalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        if (mShown) {
            drawHandle(canvas);
        }
        if (mStrokeShown) {
            canvas.drawPath(mStrokePath, mStroke);
        }
    }

    private void drawHandle(Canvas canvas) {
        int alpha = (mRail & TouchInput.RAIL_BOX) != 0 ? FAINT_ALPHA : 255;
        float dx = mGroupX - mX;
        float dy = mGroupY - mY;
        float apart = (float) Math.hypot(dx, dy);
        if (apart > mRadius) {
            if (mOff) {
                // An arrow on the handle's edge, at the group.
                float ux = dx / apart;
                float uy = dy / apart;
                float tip = mRadius + mArrowLength;
                float base = mRadius + 0.3f * mMillimetre;
                float half = 1.3f * mMillimetre;
                mArrow.rewind();
                mArrow.moveTo(mX + ux * tip, mY + uy * tip);
                mArrow.lineTo(mX + ux * base - uy * half, mY + uy * base + ux * half);
                mArrow.lineTo(mX + ux * base + uy * half, mY + uy * base - ux * half);
                mArrow.close();
                mFill.setColor(RING);
                mFill.setAlpha(alpha);
                canvas.drawPath(mArrow, mFill);
            } else {
                mLine.setAlpha(alpha / 2);
                canvas.drawLine(mGroupX, mGroupY, mX, mY, mLine);
            }
        }
        mFill.setColor(OWN_DEEP);
        mFill.setAlpha(alpha * 225 / 255);
        canvas.drawCircle(mX, mY, mRadius, mFill);
        mRing.setColor((mRail & TouchInput.RAIL_ATTACK) != 0 ? ATTACK : RING);
        mRing.setAlpha(alpha);
        canvas.drawCircle(mX, mY, mRadius, mRing);
        mText.setAlpha(alpha);
        canvas.drawText(Integer.toString(mCount), mX, mY - (mText.ascent() + mText.descent()) / 2, mText);
    }
}
