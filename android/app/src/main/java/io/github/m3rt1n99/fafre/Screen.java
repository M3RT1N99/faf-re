package io.github.m3rt1n99.fafre;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.util.TypedValue;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * The app's screens, built in code: their look (the colours of res/values/colors.xml, the cards, texts and buttons on a page
 * that scrolls) and what more than one offers, the SCFA import's picker and FAF's newest game files with the question
 * before a download on a metered network. MainActivity, the test app, is its screen; its jobs run in TestService.
 */
abstract class Screen extends Activity {
    // res/values/colors.xml
    static final int BACKGROUND = 0xFF101820;
    static final int CARD = 0xFF1A2934;
    static final int ACCENT = 0xFFF2A33A;
    static final int TEXT = 0xFFE8EEF2;
    static final int MUTED = 0xFF9AAAB6;
    static final int PASS = 0xFF6BD06B;
    static final int FAIL = 0xFFFF6B6B;

    private static final int PICK_SCFA_FOLDER = 1;

    private boolean mScreenKeptOn;

    /** The SCFA import from its folder, which the system's folder picker gives. */
    void pickScfaFolder() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(intent, PICK_SCFA_FOLDER);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK_SCFA_FOLDER) {
            return;
        }
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            return;
        }
        Uri picked = data.getData();
        try {
            // The import runs in TestService and may outlast this screen, whose grant goes with it.
            getContentResolver().takePersistableUriPermission(picked, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (SecurityException e) {
            // a one-time grant is enough for this import
        }
        TestService.start(this, TestService.importScfa(picked));
    }

    /** The button for FAF's newest game files: a check of the newest version, or while a question waits, the question again. */
    void getNewestGameFiles() {
        NewestGameFiles pending = NewestGameFiles.pending();
        if (pending != null) {
            askToDownload(pending);
        } else {
            TestService.start(this, TestService.checkNewestGameFiles());
        }
    }

    /** A download on a metered network waits for a yes to its size. */
    void askToDownload(NewestGameFiles question) {
        String size = NewestGameFiles.size(question.bytes);
        new AlertDialog.Builder(this)
            .setTitle("Download " + size + " on a metered network?")
            .setMessage("FAF's game version " + question.newest + " is " + size + " from FAF's servers, and the phone's network "
                + "was metered when the app checked (mobile data, a hotspot, or a Wi-Fi set so). Until a yes, the button asks "
                + "again.")
            .setPositiveButton("Download", (dialog, which) -> TestService.start(this, TestService.gameFiles(question.newest)))
            .setNegativeButton("Not now", null)
            .show();
    }

    /** While a job runs the screen stays on: a replay test is played with it on, as a phone with it off may run the CPU slower. */
    void keepScreenOn(boolean running) {
        if (running == mScreenKeptOn) {
            return;
        }
        mScreenKeptOn = running;
        if (running) {
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        } else {
            getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        }
    }

    // --- Views -------------------------------------------------------------------------------

    /** The screen's column on a page that scrolls, clear of the system bars where Android 15 draws the app under them. */
    void show(LinearLayout column) {
        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(BACKGROUND);
        scroll.addView(column);
        setContentView(scroll);
        if (Build.VERSION.SDK_INT >= 35) {
            scroll.setOnApplyWindowInsetsListener((v, insets) -> {
                android.graphics.Insets bars = insets.getInsets(android.view.WindowInsets.Type.systemBars());
                v.setPadding(bars.left, bars.top, bars.right, bars.bottom);
                return insets;
            });
        }
    }

    int dp(int value) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value, getResources().getDisplayMetrics()));
    }

    TextView text(String value, int sp, int color) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        view.setTextColor(color);
        view.setPadding(0, dp(2), 0, dp(2));
        return view;
    }

    LinearLayout card(LinearLayout parent, String title, String description) {
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(14);
        card.setPadding(pad, pad, pad, pad);
        GradientDrawable shape = new GradientDrawable();
        shape.setColor(CARD);
        shape.setCornerRadius(dp(10));
        card.setBackground(shape);
        TextView heading = text(title, 17, TEXT);
        heading.setTypeface(Typeface.DEFAULT_BOLD);
        card.addView(heading);
        if (description != null) {
            card.addView(text(description, 13, MUTED));
        }
        LinearLayout.LayoutParams params =
            new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        params.topMargin = dp(14);
        parent.addView(card, params);
        return card;
    }

    Button button(LinearLayout parent, String label, View.OnClickListener listener) {
        Button button = new Button(this);
        button.setText(label);
        button.setAllCaps(false);
        button.setTextColor(Color.BLACK);
        button.getBackground().setTint(ACCENT);
        button.setOnClickListener(listener);
        parent.addView(button);
        return button;
    }
}
