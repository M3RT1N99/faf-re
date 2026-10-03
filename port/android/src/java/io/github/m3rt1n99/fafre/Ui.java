package io.github.m3rt1n99.fafre;

import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * View factory for the launcher's programmatic UI (no XML layouts, so the
 * build needs no generated R class). Colours follow the dark style of the
 * earlier launcher.
 */
final class Ui {
    static final int BACKGROUND = 0xff101820;
    static final int CARD = 0xff1a2934;
    static final int TITLE = 0xffffffff;
    static final int TEXT = 0xffe3ebf1;
    static final int BODY = 0xffd2dce4;
    static final int MUTED = 0xffa9bac8;
    static final int ACCENT = 0xfff2a33a;
    static final int ON_ACCENT = 0xff1b1205;
    static final int GOOD = 0xff8fd19e;
    static final int WARN = 0xffffcc80;
    static final int BAD = 0xffff8a80;

    /** Content width cap so lines stay readable on tablets and in landscape. */
    static final int MAX_CONTENT_DP = 760;

    private final Context mContext;
    private final float mDensity;

    Ui(Context context) {
        mContext = context;
        mDensity = context.getResources().getDisplayMetrics().density;
    }

    int dp(float value) {
        return Math.round(value * mDensity);
    }

    TextView text(CharSequence value, float sp, int color) {
        TextView view = new TextView(mContext);
        view.setText(value);
        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        view.setTextColor(color);
        view.setLineSpacing(0, 1.1f);
        return view;
    }

    TextView body(CharSequence value) {
        return text(value, 14, BODY);
    }

    TextView hint(CharSequence value) {
        return text(value, 13, MUTED);
    }

    TextView mono(CharSequence value) {
        TextView view = text(value, 12, TEXT);
        view.setTypeface(Typeface.MONOSPACE);
        view.setTextIsSelectable(true);
        return view;
    }

    TextView heading(CharSequence value) {
        TextView view = text(value, 18, TITLE);
        view.setTypeface(Typeface.DEFAULT_BOLD);
        return view;
    }

    LinearLayout card() {
        LinearLayout card = new LinearLayout(mContext);
        card.setOrientation(LinearLayout.VERTICAL);
        GradientDrawable background = new GradientDrawable();
        background.setColor(CARD);
        background.setCornerRadius(dp(10));
        card.setBackground(background);
        card.setPadding(dp(16), dp(14), dp(16), dp(16));
        return card;
    }

    Button button(CharSequence value) {
        Button button = new Button(mContext);
        button.setText(value);
        button.setAllCaps(false);
        button.setMinHeight(dp(48));
        return button;
    }

    Button primaryButton(CharSequence value) {
        Button button = button(value);
        button.setBackgroundTintList(ColorStateList.valueOf(ACCENT));
        button.setTextColor(ON_ACCENT);
        button.setTypeface(Typeface.DEFAULT_BOLD);
        button.setTextSize(TypedValue.COMPLEX_UNIT_SP, 17);
        button.setMinHeight(dp(56));
        return button;
    }

    CheckBox checkBox(CharSequence value, boolean checked) {
        CheckBox box = new CheckBox(mContext);
        box.setText(value);
        box.setTextColor(TEXT);
        box.setChecked(checked);
        box.setButtonTintList(ColorStateList.valueOf(ACCENT));
        box.setMinHeight(dp(44));
        return box;
    }

    /** Two or more views side by side with equal width. */
    LinearLayout row(View... views) {
        LinearLayout row = new LinearLayout(mContext);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        for (int i = 0; i < views.length; ++i) {
            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0,
                    ViewGroup.LayoutParams.WRAP_CONTENT, 1);
            if (i > 0) {
                params.leftMargin = dp(8);
            }
            row.addView(views[i], params);
        }
        return row;
    }

    LinearLayout.LayoutParams matchWrap(int topMarginDp) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        params.topMargin = dp(topMarginDp);
        return params;
    }

    /** A vertical LinearLayout that never grows wider than {@code maxWidthPx}. */
    static final class Column extends LinearLayout {
        private final int mMaxWidth;

        Column(Context context) {
            this(context, Math.round(MAX_CONTENT_DP * context.getResources().getDisplayMetrics().density));
        }

        Column(Context context, int maxWidthPx) {
            super(context);
            mMaxWidth = maxWidthPx;
            setOrientation(VERTICAL);
        }

        @Override
        protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
            int width = MeasureSpec.getSize(widthMeasureSpec);
            if (width > mMaxWidth) {
                widthMeasureSpec = MeasureSpec.makeMeasureSpec(mMaxWidth, MeasureSpec.EXACTLY);
            }
            super.onMeasure(widthMeasureSpec, heightMeasureSpec);
        }
    }
}
