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

/**
 * The renderer test (GalSmoke) on a full-screen surface: the engine's Vulkan device clears it in
 * changing colours for ten seconds, then the report shows over it and goes to the results. Back
 * ends it early.
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
        if (GalSmoke.loadError() != null) {
            finishTest("FAIL: libfafgalsmoke.so did not load: " + GalSmoke.loadError());
        }
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        if (!mStarted && GalSmoke.loadError() == null) {
            mStarted = GalSmoke.start(holder.getSurface(), SECONDS);
            if (!mStarted) {
                finishTest("FAIL: the test did not start\n" + GalSmoke.report());
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

    private void finishTest(String report) {
        mHandler.removeCallbacks(mPoll);
        if (mFinished) {
            return;
        }
        mFinished = true;
        TestState.renderer(report);
        TestState.line(TestState.rendererVerdict());
        mText.setText(report + "\n(back to return)");
    }
}
