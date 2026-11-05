package io.github.m3rt1n99.fafre;

import java.util.HashMap;
import java.util.Map;

/**
 * How the game's picture lies on the screen (android/TOUCH_UI.md). Without a page it fills the screen. With a page, a part of
 * the picture that FAF's Lua has declared, the picture is larger: the page's width fills the screen's, and its height is what a
 * finger scrolls through. All in the screen's pixels, unless it says the picture's; the picture has a size of its own, which
 * this does not change, as the game goes on drawing at it.
 */
final class PageView {
    /** How far from the keyboard and the screen's top what is brought into view stays, in the picture's pixels. */
    private static final float SHOW_MARGIN = 8;

    private int mScreenWidth;
    private int mScreenHeight;
    private int mPictureWidth;
    private int mPictureHeight;
    private boolean mPage;
    private int mLeft;
    private int mTop;
    private int mRight;
    private int mBottom;
    /** Where the view's top is on the screen, which the scroll moves. */
    private float mViewTop;
    private int mKeyboard;
    /** Where the pages that a page now open covers stood. */
    private final Map<String, Float> mCovered = new HashMap<>();

    void setScreen(int width, int height) {
        mScreenWidth = width;
        mScreenHeight = height;
        settle();
    }

    void setPicture(int width, int height) {
        mPictureWidth = width;
        mPictureHeight = height;
        settle();
    }

    /** A page in the picture's pixels; false if it is the one there already. It starts at its top, or where it stood when one opened over it. */
    boolean setPage(int left, int top, int right, int bottom) {
        if (mPage && left == mLeft && top == mTop && right == mRight && bottom == mBottom) {
            return false;
        }
        if (mPage) {
            mCovered.put(key(mLeft, mTop, mRight, mBottom), mViewTop);
        }
        mPage = true;
        mLeft = left;
        mTop = top;
        mRight = right;
        mBottom = bottom;
        Float stood = mCovered.remove(key(left, top, right, bottom));
        mViewTop = stood != null ? stood : mostTop();
        settle();
        return true;
    }

    /** No page; false if there was none. */
    boolean clearPage() {
        boolean had = mPage;
        mPage = false;
        mCovered.clear();
        settle();
        return had;
    }

    private static String key(int left, int top, int right, int bottom) {
        return left + "," + top + "," + right + "," + bottom;
    }

    /** Whether the view is larger than the screen. */
    boolean zoomed() {
        return mPage && mRight > mLeft && mScreenWidth > 0 && mPictureWidth > 0 && mPictureHeight > 0 && mRight - mLeft < mPictureWidth;
    }

    /** The screen's pixels to a picture's pixel. */
    float scale() {
        if (zoomed()) {
            return (float) mScreenWidth / (mRight - mLeft);
        }
        return mPictureHeight > 0 ? (float) mScreenHeight / mPictureHeight : 1;
    }

    /** How much larger than without a page the picture is. */
    float zoom() {
        return mPictureHeight > 0 && mScreenHeight > 0 ? scale() * mPictureHeight / mScreenHeight : 1;
    }

    int viewWidth() {
        return zoomed() ? Math.round(mPictureWidth * scale()) : mScreenWidth;
    }

    int viewHeight() {
        return zoomed() ? Math.round(mPictureHeight * scale()) : mScreenHeight;
    }

    float viewLeft() {
        return zoomed() ? -Math.round(mLeft * scale()) : 0;
    }

    float viewTop() {
        return zoomed() ? mViewTop : 0;
    }

    /** The part of the screen that the keyboard covers, from the bottom: a page can go up that much further. */
    void setKeyboard(int pixels) {
        mKeyboard = pixels;
        settle();
    }

    /** The finger went `dy`, positive downwards. */
    void scrollBy(float dy) {
        if (zoomed()) {
            mViewTop += dy;
            settle();
        }
    }

    /** Moves the page as far as it takes to show these lines of the picture above the keyboard. */
    void reveal(int top, int bottom) {
        if (!zoomed()) {
            return;
        }
        float margin = SHOW_MARGIN * scale();
        float screenBottom = mViewTop + bottom * scale();
        float screenTop = mViewTop + top * scale();
        float lowest = mScreenHeight - mKeyboard - margin;
        if (screenBottom > lowest) {
            scrollBy(lowest - screenBottom);
        } else if (screenTop < margin) {
            scrollBy(margin - screenTop);
        }
    }

    private float pageHeight() {
        return (mBottom - mTop) * scale();
    }

    /** The view's top with the page's top at the top of the screen, as far down as it goes. */
    private float mostTop() {
        float room = mScreenHeight - mKeyboard - pageHeight();
        return (room > 0 ? room / 2 : 0) - mTop * scale();
    }

    /** The view's top with the page's bottom above the keyboard, as far up as it goes. */
    private float leastTop() {
        float room = mScreenHeight - mKeyboard - pageHeight();
        return room > 0 ? mostTop() : room - mTop * scale();
    }

    private void settle() {
        if (zoomed()) {
            mViewTop = Math.max(leastTop(), Math.min(mostTop(), mViewTop));
        }
    }

    /** From the screen's pixels to the picture's. Always on the picture: a finger can leave it, and a mouse button that goes up off the game's window is lost. */
    int pictureX(float x) {
        return toPicture(x - viewLeft(), viewWidth(), mPictureWidth);
    }

    int pictureY(float y) {
        return toPicture(y - viewTop(), viewHeight(), mPictureHeight);
    }

    private static int toPicture(float place, int viewSize, int pictureSize) {
        if (viewSize <= 0 || pictureSize <= 0) {
            return Math.round(place);
        }
        return Math.max(0, Math.min(pictureSize - 1, Math.round(place * pictureSize / viewSize)));
    }
}
