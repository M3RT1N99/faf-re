package io.github.m3rt1n99.fafre;

import android.app.Activity;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.style.ForegroundColorSpan;
import android.util.TypedValue;
import android.view.DisplayCutout;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.widget.LinearLayout;
import android.widget.PopupWindow;
import android.widget.TextView;

import java.util.Locale;
import java.util.Map;

/**
 * The menu replay's overlay in GameActivity: frame number, frame time, and PASS/FAIL of the read-back frames so
 * far; at the end the result. NativeActivity hands its window's surface to the native renderer, so views in the
 * activity's own window are never drawn; the overlay is a {@link PopupWindow}, a sub-window with a surface of
 * its own above the replay. It is neither focusable nor touchable: taps and Back reach the native side, which
 * stops the replay (Back) or closes the activity after the end (GalPlay.h).
 */
final class MenuReplayOverlay implements MenuReplaySession.Listener {
    private static final long UPDATE_DELAY_MS = 40;
    private static final int BACKGROUND = 0xc0101820;

    private final Activity mActivity;
    private final MenuReplaySession mSession;
    private final Handler mMain = new Handler(Looper.getMainLooper());
    private final float mDensity;
    private PopupWindow mPopup;
    private TextView mTitle;
    private TextView mStatus;
    private TextView mFrames;
    private TextView mResult;
    private boolean mUpdatePending;
    private boolean mDismissed;

    MenuReplayOverlay(Activity activity, MenuReplaySession session) {
        mActivity = activity;
        mSession = session;
        mDensity = activity.getResources().getDisplayMetrics().density;
        session.setListener(this);
    }

    /** Shows the overlay once the activity's window is attached (its token is the popup's parent). */
    void show() {
        final View decor = mActivity.getWindow().getDecorView();
        decor.post(() -> {
            if (mDismissed || mPopup != null || mActivity.isFinishing() || decor.getWindowToken() == null) {
                return;
            }
            LinearLayout panel = new LinearLayout(mActivity);
            panel.setOrientation(LinearLayout.VERTICAL);
            GradientDrawable background = new GradientDrawable();
            background.setColor(BACKGROUND);
            background.setCornerRadius(dp(8));
            panel.setBackground(background);
            panel.setPadding(dp(10), dp(8), dp(10), dp(8));
            mTitle = text(13, Ui.TITLE, true);
            mStatus = text(12, Ui.TEXT, false);
            mFrames = text(12, Ui.TEXT, false);
            mResult = text(12, Ui.BODY, false);
            mResult.setVisibility(View.GONE);
            panel.addView(mTitle);
            panel.addView(mStatus);
            panel.addView(mFrames);
            panel.addView(mResult);
            mPopup = new PopupWindow(panel, ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT,
                    false);
            mPopup.setTouchable(false);
            mPopup.setFocusable(false);
            mPopup.setOutsideTouchable(false);
            mPopup.setAnimationStyle(0);
            render();
            int[] offset = offset(decor);
            try {
                mPopup.showAtLocation(decor, Gravity.TOP | Gravity.START, offset[0], offset[1]);
            } catch (RuntimeException e) {
                // A window that is going away (BadTokenException): the replay runs without its overlay.
                mPopup = null;
            }
        });
    }

    void dismiss() {
        mDismissed = true;
        mSession.setListener(null);
        mMain.removeCallbacksAndMessages(null);
        if (mPopup != null) {
            try {
                mPopup.dismiss();
            } catch (RuntimeException ignored) {
                // the window is already gone
            }
            mPopup = null;
        }
    }

    /** From any thread: coalesces the session's changes into one update per {@link #UPDATE_DELAY_MS}. */
    @Override
    public void onChanged() {
        synchronized (this) {
            if (mUpdatePending) {
                return;
            }
            mUpdatePending = true;
        }
        mMain.postDelayed(() -> {
            synchronized (this) {
                mUpdatePending = false;
            }
            render();
        }, UPDATE_DELAY_MS);
    }

    private void render() {
        if (mDismissed || mTitle == null) {
            return;
        }
        MenuReplaySession.Snapshot snapshot = mSession.snapshot();
        mTitle.setText("FAF menu replay · Vulkan · " + (snapshot.fast ? "as fast as possible" : "recorded pace"));
        mStatus.setText(status(snapshot));
        mFrames.setText(readbacks(snapshot));
        mFrames.setVisibility(snapshot.readbacks.isEmpty() ? View.GONE : View.VISIBLE);
        if (snapshot.finished) {
            SpannableStringBuilder result = new SpannableStringBuilder();
            append(result, snapshot.headline, MenuReplay.VERDICT_PASS.equals(snapshot.verdict) ? Ui.GOOD
                    : MenuReplay.VERDICT_FAIL.equals(snapshot.verdict) ? Ui.BAD : Ui.WARN);
            for (String[] line : snapshot.lines) {
                if (line[1].startsWith("Replay:") || line[1].startsWith("Shaders:")) {
                    result.append('\n');
                    append(result, line[1], Ui.MUTED);
                }
            }
            result.append('\n');
            append(result, "Tap or press Back to return to the launcher.", Ui.ACCENT);
            mResult.setText(result);
            mResult.setVisibility(View.VISIBLE);
        }
        if (mPopup != null && mPopup.isShowing()) {
            mPopup.update();
        }
    }

    private static CharSequence status(MenuReplaySession.Snapshot snapshot) {
        if ("replay".equals(snapshot.stage) || ("done".equals(snapshot.stage) && snapshot.frame > 0)) {
            StringBuilder out = new StringBuilder();
            out.append("Frame ").append(snapshot.frame);
            if (snapshot.frames > 0) {
                out.append(" / ").append(snapshot.frames);
            }
            if (snapshot.frameMs > 0) {
                out.append(String.format(Locale.ROOT, " · %.1f ms", snapshot.frameMs));
            }
            if (snapshot.gpuFrameMs > 0) {
                out.append(String.format(Locale.ROOT, " (work %.1f ms)", snapshot.gpuFrameMs));
            }
            if (snapshot.avgFrameMs > 0) {
                out.append(String.format(Locale.ROOT, " · avg %.1f ms", snapshot.avgFrameMs));
            }
            return out;
        }
        return snapshot.message.isEmpty() ? snapshot.stage : snapshot.message;
    }

    private static CharSequence readbacks(MenuReplaySession.Snapshot snapshot) {
        SpannableStringBuilder out = new SpannableStringBuilder();
        for (Map.Entry<Integer, String> entry : snapshot.readbacks.entrySet()) {
            if (out.length() > 0) {
                out.append("  ");
            }
            String verdict = entry.getValue();
            String label;
            int color;
            switch (verdict) {
                case "pass":
                    label = "PASS";
                    color = Ui.GOOD;
                    break;
                case "differs":
                    label = "FAIL";
                    color = Ui.BAD;
                    break;
                case "no-reference":
                    label = "no ref";
                    color = Ui.WARN;
                    break;
                case "pending":
                    label = "…";
                    color = Ui.MUTED;
                    break;
                default:
                    label = snapshot.finished ? "not reached" : "·";
                    color = Ui.MUTED;
                    break;
            }
            append(out, entry.getKey() + " ", Ui.TEXT);
            append(out, label, color);
        }
        return out;
    }

    private static void append(SpannableStringBuilder out, String text, int color) {
        int start = out.length();
        out.append(text);
        out.setSpan(new ForegroundColorSpan(color), start, out.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
    }

    /** Top-left, clear of a display cutout on that edge. */
    private int[] offset(View decor) {
        int x = dp(12);
        int y = dp(12);
        if (Build.VERSION.SDK_INT >= 28) {
            WindowInsets insets = decor.getRootWindowInsets();
            DisplayCutout cutout = insets != null ? insets.getDisplayCutout() : null;
            if (cutout != null) {
                x += cutout.getSafeInsetLeft();
                y += cutout.getSafeInsetTop();
            }
        }
        return new int[] {x, y};
    }

    private TextView text(float sp, int color, boolean bold) {
        TextView view = new TextView(mActivity);
        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        view.setTextColor(color);
        view.setTypeface(bold ? Typeface.DEFAULT_BOLD : Typeface.MONOSPACE);
        view.setMaxWidth(Math.round(mActivity.getResources().getDisplayMetrics().widthPixels * 0.55f));
        view.setLineSpacing(0, 1.05f);
        return view;
    }

    private int dp(float value) {
        return Math.round(value * mDensity);
    }
}
