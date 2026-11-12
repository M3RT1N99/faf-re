package io.github.m3rt1n99.fafre;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.os.Build;
import android.os.SystemClock;
import android.view.View;

/**
 * What the touch scheme Griff draws over the game (buildstage/touch-concept-v2.md, the mockup buildstage/touch-preview): the
 * handle beside the selection, with the selection's count and a line to the group's middle, or where the group is out of view
 * or under a panel, an arrow at it; the stroke a finger draws from it, with the waypoints its lift gives and a label of what it
 * gives; the ring of a finger that rests; the draft's plans, the line and the plan a finger would add, Box's box over deposits
 * and the loupe of what a resting finger covers; the pins at the places orders went to, in the colour of each kind, which fade
 * (include/GriffPins.h); and a toast, a short word for the user ({@link #toast}). In the screen's pixels, in the mockup's
 * sizes and colours. It takes no fingers: they go through to the layer under it.
 */
final class GriffLayer extends View {
    private static final int OWN = 0xFF2DD4BF;
    private static final int OWN_DEEP = 0xFF115E59;
    private static final int RING = 0xFFF2FFFB;
    private static final int ATTACK = 0xFFFBBF24;
    private static final int PATROL = 0xFFC4B5FD;
    private static final int BUILD = 0xFF7DD3FC;
    private static final int ALARM = 0xFFF87171;
    private static final int BACKGROUND = 0xFF0A1118;
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
    /** A plan: a box of its ground (at least Draft.LEAST_MM wide), the newest fuller than the others, a line's and the aim's between. */
    private static final float PLAN_EDGE_MM = 1.5f * CSS_PIXEL_MM;
    private static final int NEWEST_ALPHA = 140;
    private static final int OLDER_ALPHA = 82;
    private static final int LINE_ALPHA = 89;
    /** Box's box over deposits: the own colour, faint, its edge the ring's. */
    private static final int BOX_ALPHA = 36;
    /** The loupe: a circle of this radius, this far above the finger, the 6 mm about it at twice its size, a cross in it. */
    private static final float LOUPE_MM = 6;
    private static final float LOUPE_ABOVE_MM = 14;
    private static final float LOUPE_ROOM_MM = 1;
    private static final float LOUPE_CROSS_MM = 1.2f;
    private static final float LOUPE_MAGNIFIED = 2;
    /** A pin: a drop of this size, its tip at the order's place, its edge the background colour (the mockup's drawPins). */
    private static final float PIN_MM = 1.5f;
    /** The pins' kinds as the program gives them (include/GriffPins.h Kind): move, attack, patrol, build. */
    private static final int[] PIN_COLOURS = {OWN, OWN, ATTACK, PATROL, BUILD};
    private static final int MOST_PINS = 64;

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
    private final Paint mPlanFill = new Paint();
    private final Paint mPlanEdge = new Paint();
    private final Paint mBoxFill = new Paint();
    private final Paint mBoxEdge = new Paint();
    private final Paint mLoupeEdge = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mLoupeBackground = new Paint();
    private final Paint mLoupePicture = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final Paint mPinFill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mPinEdge = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path mPin = new Path();
    private final RectF mPinHead = new RectF();
    private final Path mArrow = new Path();
    private final Path mStrokePath = new Path();
    private final Path mLoupeCircle = new Path();
    private final RectF mPill = new RectF();
    private final RectF mPlace = new RectF();
    private final Rect mLoupeSource = new Rect();
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
    /** The draft as the program shows it, or none; Box's box over deposits; the loupe's picture and the finger it is of. */
    private Draft mDraft;
    private boolean mBoxShown;
    private final RectF mBox = new RectF();
    private Bitmap mLoupe;
    private float mLoupeX;
    private float mLoupeY;
    /** The pins that show: their tips, kinds and how much of each shows. */
    private final float[] mPinX = new float[MOST_PINS];
    private final float[] mPinY = new float[MOST_PINS];
    private final int[] mPinKind = new int[MOST_PINS];
    private final float[] mPinAlpha = new float[MOST_PINS];
    private int mPins;

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
        mPlanEdge.setStyle(Paint.Style.STROKE);
        mBoxFill.setColor(OWN);
        mBoxFill.setAlpha(BOX_ALPHA);
        mBoxEdge.setStyle(Paint.Style.STROKE);
        mBoxEdge.setColor(RING);
        mLoupeEdge.setStyle(Paint.Style.STROKE);
        mLoupeEdge.setColor(RING);
        mLoupeBackground.setColor(BACKGROUND);
        mPinEdge.setStyle(Paint.Style.STROKE);
        mPinEdge.setColor(BACKGROUND);
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
        mPlanEdge.setStrokeWidth(PLAN_EDGE_MM * millimetre);
        mBoxEdge.setStrokeWidth(PLAN_EDGE_MM * millimetre);
        mLoupeEdge.setStrokeWidth(PLAN_EDGE_MM * millimetre);
        mPinEdge.setStrokeWidth(Math.max(1, CSS_PIXEL_MM * millimetre));
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

    /** The draft as the program shows it now ({@link Draft#update} said it changed), or none. */
    void setDraft(Draft draft) {
        mDraft = draft;
        invalidate();
    }

    /** Box's box over deposits in the draft, from one corner to the other, or none. */
    void showDraftBox(boolean shown, float x0, float y0, float x1, float y1) {
        mBoxShown = shown;
        mBox.set(Math.min(x0, x1), Math.min(y0, y1), Math.max(x0, x1), Math.max(y0, y1));
        invalidate();
    }

    /** The loupe of a finger at this place: the picture of the 6 mm about it (the game's, copied), or none. */
    void showLoupe(Bitmap picture, float x, float y) {
        mLoupe = picture;
        mLoupeX = x;
        mLoupeY = y;
        invalidate();
    }

    /**
     * The pins as the program showed them (Game.griffOrders, from `from` on, `count` of them), with the picture's pixels mapped to
     * the screen's: each one's tip, kind and how much of it shows.
     */
    void setPins(float[] values, int from, int count, Draft.Screen screen) {
        int pins = Math.min(count, MOST_PINS);
        boolean changed = pins != mPins;
        for (int pin = 0; pin < pins; ++pin) {
            int at = from + pin * Game.ORDERS_PIN;
            float x = screen.x(values[at]);
            float y = screen.y(values[at + 1]);
            int kind = (int) values[at + 2];
            float alpha = values[at + 3];
            changed |= x != mPinX[pin] || y != mPinY[pin] || kind != mPinKind[pin] || alpha != mPinAlpha[pin];
            mPinX[pin] = x;
            mPinY[pin] = y;
            mPinKind[pin] = kind;
            mPinAlpha[pin] = alpha;
        }
        mPins = pins;
        if (changed) {
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
        drawPins(canvas);
        if (mDraft != null && mDraft.open()) {
            drawDraft(canvas);
        }
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

    /**
     * The mockup's drawPins: a drop with its tip at the order's place, its sides out to 1.1 mm and up 2.4 mm into a circle of
     * 1.5 mm whose middle is 3 mm above the tip, in its kind's colour, edged in the background colour, as much of it as shows.
     */
    private void drawPins(Canvas canvas) {
        float size = PIN_MM * mMillimetre;
        for (int pin = 0; pin < mPins; ++pin) {
            float x = mPinX[pin];
            float y = mPinY[pin];
            int kind = mPinKind[pin];
            int alpha = Math.round(255 * Math.max(0, Math.min(1, mPinAlpha[pin])));
            mPin.rewind();
            mPin.moveTo(x, y);
            mPin.lineTo(x - size * 0.75f, y - size * 1.6f);
            mPinHead.set(x - size, y - 3 * size, x + size, y - size);
            mPin.arcTo(mPinHead, 144, 252, false);
            mPin.close();
            mPinFill.setColor(PIN_COLOURS[kind > 0 && kind < PIN_COLOURS.length ? kind : 0]);
            mPinFill.setAlpha(alpha);
            canvas.drawPath(mPin, mPinFill);
            mPinEdge.setAlpha(alpha);
            canvas.drawPath(mPin, mPinEdge);
        }
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

    /** The mockup's drawDraft and drawLinePreview: each plan in the build colour, red where its ground is taken. */
    private void drawDraft(Canvas canvas) {
        Draft draft = mDraft;
        for (int plan = 0; plan < draft.plans(); ++plan) {
            drawPlace(canvas, draft, plan, draft.has(plan, Draft.NEWEST) ? NEWEST_ALPHA : OLDER_ALPHA);
        }
        for (int at = 0; at < draft.lineCount(); ++at) {
            drawPlace(canvas, draft, draft.lineItem(at), LINE_ALPHA);
        }
        if (draft.aimShown()) {
            drawPlace(canvas, draft, draft.aimItem(), NEWEST_ALPHA);
        }
        if (mBoxShown) {
            canvas.drawRect(mBox, mBoxFill);
            canvas.drawRect(mBox, mBoxEdge);
        }
        if (mLoupe != null && draft.restsOnMap()) {
            drawLoupe(canvas, draft);
        }
    }

    /** A place's box: the ground its skirt takes on the screen, at least Draft.LEAST_MM wide about its middle. */
    private void placeBox(Draft draft, int item, RectF box) {
        float half = Draft.LEAST_MM * mMillimetre / 2;
        float x = draft.x(item);
        float y = draft.y(item);
        box.set(Math.min(draft.left(item), x - half), Math.min(draft.top(item), y - half), Math.max(draft.right(item), x + half),
                Math.max(draft.bottom(item), y + half));
    }

    private void drawPlace(Canvas canvas, Draft draft, int item, int alpha) {
        int colour = draft.has(item, Draft.TAKEN) ? ALARM : BUILD;
        placeBox(draft, item, mPlace);
        mPlanFill.setColor(colour);
        mPlanFill.setAlpha(alpha);
        canvas.drawRect(mPlace, mPlanFill);
        mPlanEdge.setColor(colour);
        canvas.drawRect(mPlace, mPlanEdge);
    }

    /**
     * The mockup's drawLoupe: a circle of 12 mm, 14 mm above the finger (below it where the top has no room), kept off the
     * screen's sides, the 6 mm about the finger in it at twice their size over the background colour, with the plan its lift
     * places, a ring and a cross at its middle.
     */
    private void drawLoupe(Canvas canvas, Draft draft) {
        float radius = LOUPE_MM * mMillimetre;
        float room = radius + LOUPE_ROOM_MM * mMillimetre;
        float x = Math.max(room, Math.min(getWidth() - room, mLoupeX));
        float above = mLoupeY - LOUPE_ABOVE_MM * mMillimetre;
        float y = above < room ? mLoupeY + LOUPE_ABOVE_MM * mMillimetre : above;
        mLoupeCircle.rewind();
        mLoupeCircle.addCircle(x, y, radius, Path.Direction.CW);
        canvas.save();
        canvas.clipPath(mLoupeCircle);
        canvas.drawCircle(x, y, radius, mLoupeBackground);
        mLoupeSource.set(0, 0, mLoupe.getWidth(), mLoupe.getHeight());
        mPlace.set(x - radius, y - radius, x + radius, y + radius);
        canvas.drawBitmap(mLoupe, mLoupeSource, mPlace, mLoupePicture);
        if (draft.aimShown()) {
            // The plan where it lands, at the loupe's scale about the finger.
            int aim = draft.aimItem();
            placeBox(draft, aim, mPlace);
            mPlace.set(x + (mPlace.left - mLoupeX) * LOUPE_MAGNIFIED, y + (mPlace.top - mLoupeY) * LOUPE_MAGNIFIED,
                       x + (mPlace.right - mLoupeX) * LOUPE_MAGNIFIED, y + (mPlace.bottom - mLoupeY) * LOUPE_MAGNIFIED);
            int colour = draft.has(aim, Draft.TAKEN) ? ALARM : BUILD;
            mPlanFill.setColor(colour);
            mPlanFill.setAlpha(NEWEST_ALPHA);
            canvas.drawRect(mPlace, mPlanFill);
        }
        canvas.restore();
        canvas.drawCircle(x, y, radius, mLoupeEdge);
        float cross = LOUPE_CROSS_MM * mMillimetre;
        canvas.drawLine(x - cross, y, x + cross, y, mLoupeEdge);
        canvas.drawLine(x, y - cross, x, y + cross, mLoupeEdge);
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
