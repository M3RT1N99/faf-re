package io.github.m3rt1n99.fafre;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.os.Build;
import android.view.View;

/**
 * The touch scheme Griff's undo pill (buildstage/griff-v3-spec.md T3.1, the mockup's drawUndo with ICON_BACK): a pill in the
 * mockup's panel colour with an edge in its mark colour, a back arrow 4 mm from its left, "Undo" right of its middle, "Undo…"
 * while it waits ({@link Undo#waiting}), and at its bottom a bar of the time it has left. A view of its own over the game, so
 * a finger on it is its own, exactly where it is drawn, and never the map's.
 */
final class UndoPill extends View {
    private static final int PANEL = 0xE00C141C;
    private static final int MARK = 0xFFFDE68A;
    private static final int TEXT = 0xFFE8EEF2;
    /** The mockup's CSS pixel on the phone it was made for, in mm. */
    private static final float CSS_PIXEL_MM = 1 / 5.64f;
    /** The arrow: its middle this far from the pill's left, its arc's radius; the words this far right of the pill's middle. */
    private static final float ARROW_MM = 4;
    private static final float ARROW_RADIUS_MM = 1.2f;
    private static final float WORDS_RIGHT_MM = 2.5f;
    /** The mockup's font M: 600, 2.5 mm. */
    private static final float FONT_MM = 2.5f;
    private static final String UNDO = "Undo";
    private static final String WAITING = "Undo…";

    private final Paint mFill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mEdge = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mArrow = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mWords = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mBar = new Paint();
    private final RectF mBox = new RectF();
    private final RectF mArc = new RectF();
    private final Path mPath = new Path();
    private float mMillimetre = 10;
    private boolean mWaiting;
    private float mLeft = 1;

    UndoPill(Context context) {
        super(context);
        mFill.setColor(PANEL);
        mEdge.setStyle(Paint.Style.STROKE);
        mEdge.setColor(MARK);
        mArrow.setStyle(Paint.Style.STROKE);
        mArrow.setStrokeCap(Paint.Cap.ROUND);
        mArrow.setStrokeJoin(Paint.Join.ROUND);
        mArrow.setColor(MARK);
        mWords.setColor(TEXT);
        mWords.setTextAlign(Paint.Align.CENTER);
        mWords.setTypeface(Build.VERSION.SDK_INT >= 28 ? Typeface.create(Typeface.DEFAULT, 600, false) : Typeface.DEFAULT_BOLD);
        mBar.setColor(MARK);
        setContentDescription("Undo the last order");
    }

    /** The screen's pixels in a mm on the glass. */
    void setMillimetre(float millimetre) {
        mMillimetre = millimetre;
        mEdge.setStrokeWidth(Math.max(1, CSS_PIXEL_MM * millimetre));
        mArrow.setStrokeWidth(Math.max(2 * CSS_PIXEL_MM * millimetre, 0.32f * ARROW_RADIUS_MM * millimetre));
        mWords.setTextSize(FONT_MM * millimetre);
        invalidate();
    }

    /** Whether it waits, and how much of its time is left, 1 to 0. */
    void show(boolean waiting, float left) {
        if (waiting != mWaiting || left != mLeft) {
            mWaiting = waiting;
            mLeft = left;
            invalidate();
        }
    }

    @Override
    protected void onDraw(Canvas canvas) {
        float width = getWidth();
        float height = getHeight();
        float half = mEdge.getStrokeWidth() / 2;
        mBox.set(half, half, width - half, height - half);
        float round = height / 2;
        canvas.drawRoundRect(mBox, round, round, mFill);
        canvas.drawRoundRect(mBox, round, round, mEdge);
        float middle = height / 2;
        drawArrow(canvas, ARROW_MM * mMillimetre, middle, ARROW_RADIUS_MM * mMillimetre);
        String words = mWaiting ? WAITING : UNDO;
        canvas.drawText(words, width / 2 + WORDS_RIGHT_MM * mMillimetre, middle - (mWords.ascent() + mWords.descent()) / 2, mWords);
        float bar = 2 * CSS_PIXEL_MM * mMillimetre;
        canvas.drawRect(height / 2, height - bar, height / 2 + (width - height) * mLeft, height, mBar);
    }

    /** The mockup's ICON_BACK: an arc round from the upper left, and the arrow's head at its start. */
    private void drawArrow(Canvas canvas, float x, float y, float r) {
        mPath.rewind();
        mArc.set(x - r, y + 0.1f * r - r, x + r, y + 0.1f * r + r);
        mPath.arcTo(mArc, 207, 243, true);
        mPath.moveTo(x - 1.25f * r, y - 0.95f * r);
        mPath.lineTo(x - 0.95f * r, y - 0.05f * r);
        mPath.lineTo(x - 0.05f * r, y - 0.35f * r);
        canvas.drawPath(mPath, mArrow);
    }
}
