package io.github.m3rt1n99.fafre;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.os.Build;
import android.view.View;

/**
 * One of the touch scheme Griff's rail buttons (buildstage/griff-v3-spec.md T3.5, the mockup's drawRail): a square with rounded
 * corners and its label in the middle, Attack's in the attack colour, in the mockup's colours by how it looks (Rail#look): idle in
 * the button's fill with the line's edge; held in the button-on fill with the ring's edge; lit for the next action in the
 * button-on fill with the mark's edge and "1×" in its upper right corner. A view of its own over the game, so a finger on it is
 * its own.
 */
final class RailButton extends View {
    private static final int BUTTON = 0xDB14212C;
    private static final int BUTTON_ON = 0xFF0F766E;
    private static final int LINE = 0xFF3F5868;
    private static final int RING = 0xFFF2FFFB;
    private static final int MARK = 0xFFFDE68A;
    private static final int TEXT = 0xFFE8EEF2;
    private static final int ATTACK = 0xFFFBBF24;
    /** The mockup's CSS pixel on the phone it was made for, in mm. */
    private static final float CSS_PIXEL_MM = 1 / 5.64f;
    private static final float CORNER_MM = 1.2f;
    /** The mockup's font M for the label and its font XS for "1×", 600 both. */
    private static final float FONT_MM = 2.5f;
    private static final float SMALL_FONT_MM = 1.95f;
    /** The middle of "1×": this far from the right and the top. */
    private static final float ONCE_RIGHT_MM = 2.2f;
    private static final float ONCE_TOP_MM = 2;
    private static final String ONCE = "1×";

    private final int mBit;
    private final String mLabel;
    private final Paint mFill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mEdge = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mWords = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mOnce = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF mBox = new RectF();
    private float mMillimetre = 10;
    private int mLook = Rail.IDLE;

    /** `bit`: its TouchInput.RAIL_ bit. */
    RailButton(Context context, int bit, String label) {
        super(context);
        mBit = bit;
        mLabel = label;
        Typeface semiBold = Build.VERSION.SDK_INT >= 28 ? Typeface.create(Typeface.DEFAULT, 600, false) : Typeface.DEFAULT_BOLD;
        mEdge.setStyle(Paint.Style.STROKE);
        mWords.setColor(bit == TouchInput.RAIL_ATTACK ? ATTACK : TEXT);
        mWords.setTextAlign(Paint.Align.CENTER);
        mWords.setTypeface(semiBold);
        mOnce.setColor(MARK);
        mOnce.setTextAlign(Paint.Align.CENTER);
        mOnce.setTypeface(semiBold);
        setContentDescription(label);
    }

    int bit() {
        return mBit;
    }

    /** The screen's pixels in a mm on the glass. */
    void setMillimetre(float millimetre) {
        mMillimetre = millimetre;
        mEdge.setStrokeWidth(Math.max(1, CSS_PIXEL_MM * millimetre));
        mWords.setTextSize(FONT_MM * millimetre);
        mOnce.setTextSize(SMALL_FONT_MM * millimetre);
        invalidate();
    }

    /** How it looks: Rail.IDLE, HELD or LIT. */
    void setLook(int look) {
        if (look != mLook) {
            mLook = look;
            invalidate();
        }
    }

    @Override
    protected void onDraw(Canvas canvas) {
        float width = getWidth();
        float height = getHeight();
        float half = mEdge.getStrokeWidth() / 2;
        mBox.set(half, half, width - half, height - half);
        float corner = CORNER_MM * mMillimetre;
        mFill.setColor(mLook == Rail.IDLE ? BUTTON : BUTTON_ON);
        mEdge.setColor(mLook == Rail.HELD ? RING : mLook == Rail.LIT ? MARK : LINE);
        canvas.drawRoundRect(mBox, corner, corner, mFill);
        canvas.drawRoundRect(mBox, corner, corner, mEdge);
        canvas.drawText(mLabel, width / 2, middle(mWords, height / 2), mWords);
        if (mLook == Rail.LIT) {
            canvas.drawText(ONCE, width - ONCE_RIGHT_MM * mMillimetre, middle(mOnce, ONCE_TOP_MM * mMillimetre), mOnce);
        }
    }

    /** The baseline that puts a line of this paint's text with its middle at `y`, as the mockup's textBaseline middle. */
    private static float middle(Paint paint, float y) {
        return y - (paint.ascent() + paint.descent()) / 2;
    }
}
