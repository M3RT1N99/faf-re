package io.github.m3rt1n99.fafre;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
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
    private boolean mStarted;
    private boolean mFinished;

    private final Runnable mPoll = new Runnable() {
        @Override
        public void run() {
            String report = GalSmoke.report();
            mText.setText(report);
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
        if (GalSmoke.loadError() != null) {
            failed("FAIL: libfafgalsmoke.so did not load: " + GalSmoke.loadError());
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
        try (FileOutputStream out = new FileOutputStream(RendererReport.file(this))) {
            out.write((line + "\n").getBytes(StandardCharsets.UTF_8));
        } catch (IOException e) {
            line += "\n(the report file: " + e.getMessage() + ")";
        }
        finishTest(line);
    }

    private void finishTest(String report) {
        mHandler.removeCallbacks(mPoll);
        if (mFinished) {
            return;
        }
        mFinished = true;
        mText.setText(report + "\n(back to return)");
    }
}
