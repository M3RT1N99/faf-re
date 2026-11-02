package io.github.m3rt1n99.fafre;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Rect;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.DisplayMetrics;
import android.view.Display;
import android.view.Gravity;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.TextView;

import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

/**
 * The renderer test (GalSmoke) on a full-screen surface: the engine's Vulkan device clears it in
 * changing colours for ten seconds, then the report shows over it. Back ends it early.
 *
 * <p>The activity runs in a process of its own (":renderer" in the manifest): if the phone's
 * Vulkan driver takes the process down, the app stays. The report reaches the app through
 * {@link RendererReport#file}, which the native side writes line by line.
 */
public final class GalSmokeActivity extends Activity implements SurfaceHolder.Callback {
    static final int SECONDS = 10;

    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private TextView mText;
    /** The screen's facts, the report's first line (from Java; the native side appends). */
    private String mScreen = "";
    private boolean mStarted;
    private boolean mFinished;

    private final Runnable mPoll = new Runnable() {
        @Override
        public void run() {
            String report = GalSmoke.report();
            mText.setText(mScreen + report);
            if (mStarted && !GalSmoke.running()) {
                finishTest(report);
                return;
            }
            mHandler.postDelayed(this, 500);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        FrameLayout root = new FrameLayout(this);
        SurfaceView surface = new SurfaceView(this);
        surface.getHolder().addCallback(this);
        root.addView(surface);
        mText = new TextView(this);
        mText.setTextColor(Color.WHITE);
        mText.setShadowLayer(4, 0, 0, Color.BLACK);
        mText.setTextSize(12);
        mText.setGravity(Gravity.TOP | Gravity.START);
        mText.setPadding(24, 48, 24, 24);
        root.addView(mText);
        setContentView(root);
        RendererReport.clear(this);
        mScreen = screen() + "\n";
        append(mScreen);
        if (GalSmoke.loadError() != null) {
            failed("FAIL: libfafgalsmoke.so did not load: " + GalSmoke.loadError());
        }
    }

    /**
     * The display's mode and the app's share of it. The Galaxy S22's 0.7.0 run drew 720x1544, which
     * is 2/3 of its FHD+ 1080x2316. Either the phone is set to HD+, or it scales the app down.
     */
    private String screen() {
        @SuppressWarnings("deprecation")
        Display display = getWindowManager().getDefaultDisplay();
        Display.Mode mode = display.getMode();
        StringBuilder line = new StringBuilder("screen: display mode ")
            .append(mode.getPhysicalWidth()).append('x').append(mode.getPhysicalHeight())
            .append(" at ").append(Math.round(mode.getRefreshRate())).append(" Hz (modes");
        for (Display.Mode supported : display.getSupportedModes()) {
            line.append(' ').append(supported.getPhysicalWidth()).append('x').append(supported.getPhysicalHeight())
                .append('@').append(Math.round(supported.getRefreshRate()));
        }
        DisplayMetrics metrics = getResources().getDisplayMetrics();
        line.append("), app ").append(metrics.widthPixels).append('x').append(metrics.heightPixels)
            .append(" at ").append(metrics.densityDpi).append(" dpi");
        if (Build.VERSION.SDK_INT >= 30) {
            Rect bounds = getWindowManager().getCurrentWindowMetrics().getBounds();
            line.append(", window ").append(bounds.width()).append('x').append(bounds.height());
        }
        return line.toString();
    }

    /** Lines from this side of the test into the report file, ahead of or instead of the native ones. */
    private void append(String text) {
        try (FileOutputStream out = new FileOutputStream(RendererReport.file(this), true)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        } catch (IOException e) {
            mScreen += "(the report file: " + e.getMessage() + ")\n";
        }
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        if (!mStarted && !mFinished) {
            mStarted = GalSmoke.start(holder.getSurface(), SECONDS, RendererReport.file(this).getPath());
            if (!mStarted) {
                String report = GalSmoke.report();
                if (report.isEmpty()) {
                    failed("FAIL: the test did not start");
                } else {
                    finishTest(report);
                }
                return;
            }
            mHandler.post(mPoll);
        }
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        // The device must let go of the window before the surface goes: stop waits for it.
        if (mStarted) {
            GalSmoke.stop();
            finishTest(GalSmoke.report());
        }
    }

    /** A failure before the native side runs: the report file gets it from here. */
    private void failed(String line) {
        append(line + "\n");
        finishTest(line + "\n");
    }

    /** `report`: the native side's lines (or the failure), shown under the screen's facts. */
    private void finishTest(String report) {
        mHandler.removeCallbacks(mPoll);
        if (mFinished) {
            return;
        }
        mFinished = true;
        mText.setText(mScreen + report + "(back to return)");
    }
}
