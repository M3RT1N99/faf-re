package io.github.m3rt1n99.fafre;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.os.Build;
import android.os.SystemClock;
import android.view.View;

/**
 * What the touch scheme Griff draws over the game (buildstage/touch-concept-v2.md, the mockup buildstage/touch-preview): the
 * handle beside the selection, with the selection's count and a line to the group's middle, or where the group is out of view
 * or under a panel, an arrow at it; the stroke a finger draws from it, with the waypoints its lift gives and a label of what it
 * gives; the ring of a finger that rests; and a toast, a short word for the user ({@link #toast}). In the screen's pixels, in
 * the mockup's sizes and colours. It takes no fingers: they go through to the layer under it.
 */
final class GriffLayer extends View {
    private static final int OWN = 0xFF2DD4BF;
    private static final int OWN_DEEP = 0xFF115E59;
    private static final int RING = 0xFFF2FFFB;
    private static final int ATTACK = 0xFFFBBF24;
    private static final int PATROL = 0xFFC4B5FD;
    private static final int BUILD = 0xFF7DD3FC;
    private static final int MUTED = 0xFFA8BAC5;
    private static final int TEXT = 0xFFE8EEF2;
    private static final int PANEL_SOLID = 0xFF0E1922;
    private static final int LINE = 0xFF3F5868;
    /** With Box the handle steps aside for a box that starts under it, and shows it. */
    private static final int FAINT_ALPHA = 90;
    /** The mockup's CSS pixel on the phone it was made for, in mm (its mm is 5.64 of them). */
    private static final float CSS_PIXEL_MM = 1 / 5.64f;
    /** The stroke: its line, its waypoints' dots, and its label above the finger, kept this far into the free map. */
    private static final float STROKE_LINE_MM = 2.5f * CSS_PIXEL_MM;
    private static final float WAYPOINT_MM = 0.9f;
    private static final float LABEL_ABOVE_MM = 14;
    private static final float LABEL_IN_MM = 7;
    private static final float LABEL_BELOW_TOP_MM = 2.5f;
    /** The mockup's font M: 600, 2.5 mm. */
    private static final float FONT_MM = 2.5f;
    /** The ring about a finger that rests. */
    private static final float RING_MM = 7;
    private static final float RING_LINE_MM = 2 * CSS_PIXEL_MM;
    /** A toast: a pill this high, as wide as its text and this, this far under the free map's top, for this long. */
    private static final float TOAST_MM = 7;
    private static final float TOAST_PAD_MM = 3;
    private static final float TOAST_BELOW_TOP_MM = 2;
    private static final long TOAST_MILLISECONDS = 1800;

    private final Paint mFill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mRing = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mLine = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mText = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mStroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mDots = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mWords = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mRest = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mToastFill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mToastEdge = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path mArrow = new Path();
    private final Path mStrokePath = new Path();
    private final RectF mPill = new RectF();
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
    /** The free map, the screen less FAF's panels at its middle's cross, which the label and the toast keep within. */
    private float mFreeLeft;
    private float mFreeTop;
    private float mFreeRight = Float.MAX_VALUE;
    /** The stroke: shown, its end, what lifting gives (Stroke.ROUTE, PATROL, CANCEL), what its end is on, its waypoints. */
    private boolean mStrokeShown;
    private float mEndX;
    private float mEndY;
    private int mMode;
    private int mEnd;
    private float[] mWaypoints = new float[0];
    private int mWaypointCount;
    private boolean mRinged;
    private float mRingX;
    private float mRingY;
    private String mToast;
    private long mToastUntil;

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
        Typeface semiBold = Build.VERSION.SDK_INT >= 28 ? Typeface.create(Typeface.DEFAULT, 600, false) : Typeface.DEFAULT_BOLD;
        mWords.setTextAlign(Paint.Align.CENTER);
        mWords.setTypeface(semiBold);
        mRest.setStyle(Paint.Style.STROKE);
        mRest.setColor(RING);
        mToastFill.setColor(PANEL_SOLID);
        mToastEdge.setStyle(Paint.Style.STROKE);
        mToastEdge.setColor(LINE);
    }

    /** The screen's pixels in a mm on the glass, the handle's radius in mm and how far beyond its edge its arrow reaches. */
    void setSizes(float millimetre, double radiusMm, double arrowMm) {
        mMillimetre = millimetre;
        mRadius = (float) (radiusMm * millimetre);
        mArrowLength = (float) (arrowMm * millimetre);
        mRing.setStrokeWidth(0.4f * millimetre);
        mLine.setStrokeWidth(0.15f * millimetre);
        mText.setTextSize(3.2f * millimetre);
        mStroke.setStrokeWidth(STROKE_LINE_MM * millimetre);
        mWords.setTextSize(FONT_MM * millimetre);
        mRest.setStrokeWidth(RING_LINE_MM * millimetre);
        mToastEdge.setStrokeWidth(Math.max(1, CSS_PIXEL_MM * millimetre));
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

    /** The free map's left, top and right edges at its middle, in the screen's pixels. */
    void setFreeMap(float left, float top, float right) {
        if (left != mFreeLeft || top != mFreeTop || right != mFreeRight) {
            mFreeLeft = left;
            mFreeTop = top;
            mFreeRight = right;
            invalidate();
        }
    }

    /** The rail's buttons that are held or lit, TouchInput's RAIL_ bits. */
    void setRail(int rail) {
        if (rail != mRail) {
            mRail = rail;
            invalidate();
        }
    }

    /**
     * The stroke as far as it goes (count 0: none), what lifting it now gives (Stroke.ROUTE, PATROL, CANCEL) and the waypoints
     * that would be ordered.
     */
    void showStroke(float[] xs, float[] ys, int count, int mode, float[] waypointXs, float[] waypointYs, int waypoints) {
        mStrokePath.rewind();
        mStrokeShown = count >= 2;
        if (mStrokeShown) {
            mStrokePath.moveTo(xs[0], ys[0]);
            for (int at = 1; at < count; ++at) {
                mStrokePath.lineTo(xs[at], ys[at]);
            }
            mEndX = xs[count - 1];
            mEndY = ys[count - 1];
            mMode = mode;
            if (mWaypoints.length < 2 * waypoints) {
                mWaypoints = new float[2 * waypoints];
            }
            for (int at = 0; at < waypoints; ++at) {
                mWaypoints[2 * at] = waypointXs[at];
                mWaypoints[2 * at + 1] = waypointYs[at];
            }
            mWaypointCount = waypoints;
        }
        invalidate();
    }

    /** What the stroke's end is on, as the program says (Stroke.END_ and its flags; 0 not yet). */
    void setStrokeEnd(int end) {
        if (end != mEnd) {
            mEnd = end;
            if (mStrokeShown) {
                invalidate();
            }
        }
    }

    /** The ring about a finger that rests, at this place, or none. */
    void showRing(boolean shown, float x, float y) {
        if (shown != mRinged || x != mRingX || y != mRingY) {
            mRinged = shown;
            mRingX = x;
            mRingY = y;
            invalidate();
        }
    }

    /** Says `text` for a moment, under the top of the free map, in place of what it said before. */
    void toast(String text) {
        mToast = text;
        mToastUntil = SystemClock.uptimeMillis() + TOAST_MILLISECONDS;
        invalidate();
        postInvalidateDelayed(TOAST_MILLISECONDS);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        if (mShown) {
            drawHandle(canvas);
        }
        if (mStrokeShown) {
            drawStroke(canvas);
        }
        if (mRinged) {
            canvas.drawCircle(mRingX, mRingY, RING_MM * mMillimetre, mRest);
        }
        if (mToast != null && SystemClock.uptimeMillis() < mToastUntil) {
            drawToast(canvas);
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

    /** The mockup's drawStroke: line, dots and label in the colour of what lifting gives. */
    private void drawStroke(Canvas canvas) {
        boolean attack = (mRail & TouchInput.RAIL_ATTACK) != 0;
        int says = Stroke.meaning(mEnd, mMode, attack);
        int colour = colour(says, attack);
        mStroke.setColor(colour);
        canvas.drawPath(mStrokePath, mStroke);
        mDots.setColor(colour);
        for (int at = 0; at < mWaypointCount; ++at) {
            canvas.drawCircle(mWaypoints[2 * at], mWaypoints[2 * at + 1], WAYPOINT_MM * mMillimetre, mDots);
        }
        // Above the finger, which covers what is under it, kept on the free map where it is known and wide enough.
        float left = mFreeLeft + LABEL_IN_MM * mMillimetre;
        float right = mFreeRight - LABEL_IN_MM * mMillimetre;
        float x = left <= right ? Math.max(left, Math.min(right, mEndX)) : mEndX;
        float y = Math.max(mEndY - LABEL_ABOVE_MM * mMillimetre, mFreeTop + LABEL_BELOW_TOP_MM * mMillimetre);
        mWords.setColor(colour);
        canvas.drawText(words(says), x, y - (mWords.ascent() + mWords.descent()) / 2, mWords);
    }

    private void drawToast(Canvas canvas) {
        float width = mWords.measureText(mToast) + TOAST_PAD_MM * mMillimetre;
        float height = TOAST_MM * mMillimetre;
        float middle = getWidth() / 2f;
        float top = mFreeTop + TOAST_BELOW_TOP_MM * mMillimetre;
        mPill.set(middle - width / 2, top, middle + width / 2, top + height);
        canvas.drawRoundRect(mPill, height / 2, height / 2, mToastFill);
        canvas.drawRoundRect(mPill, height / 2, height / 2, mToastEdge);
        mWords.setColor(TEXT);
        canvas.drawText(mToast, middle, top + height / 2 - (mWords.ascent() + mWords.descent()) / 2, mWords);
    }

    /** The mockup's labels in the app's English, and FAF's own name where FAF orders what the mockup has not. */
    private static String words(int says) {
        switch (says) {
            case Stroke.SAYS_ATTACK_MOVE:
                return "Attack move";
            case Stroke.SAYS_CANCEL:
                return "Cancel";
            case Stroke.SAYS_MINIMAP:
                return "Minimap";
            case Stroke.SAYS_PATROL:
                return "Patrol";
            case Stroke.SAYS_ATTACK:
                return "Attack";
            case Stroke.SAYS_ASSIST:
                return "Assist";
            case Stroke.SAYS_GUARD:
                return "Guard";
            case Stroke.SAYS_RECLAIM:
                return "Reclaim";
            case Stroke.SAYS_CAPTURE:
                return "Capture";
            case Stroke.SAYS_ORDER:
                return "Order";
            default:
                return "Route";
        }
    }

    private static int colour(int says, boolean attack) {
        switch (says) {
            case Stroke.SAYS_ATTACK_MOVE:
            case Stroke.SAYS_ATTACK:
            case Stroke.SAYS_CAPTURE:
                return ATTACK;
            case Stroke.SAYS_CANCEL:
                return MUTED;
            case Stroke.SAYS_MINIMAP:
                return attack ? ATTACK : OWN;
            case Stroke.SAYS_PATROL:
                return PATROL;
            case Stroke.SAYS_ASSIST:
            case Stroke.SAYS_GUARD:
            case Stroke.SAYS_RECLAIM:
                return BUILD;
            default:
                return OWN;
        }
    }
}
