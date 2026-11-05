package io.github.m3rt1n99.fafre;

import android.app.Activity;
import android.content.Context;
import android.graphics.Color;
import android.graphics.Rect;
import android.graphics.drawable.GradientDrawable;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.util.DisplayMetrics;
import android.util.TypedValue;
import android.view.Display;
import android.view.Gravity;
import android.view.HapticFeedbackConstants;
import android.view.InputDevice;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewConfiguration;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * The game (android/CLIENT_PLAN.md, C1): the engine's program on a full-screen surface, in a
 * process of its own (":game") that ends with the program, as a Windows program's does. It plays
 * FAF's game files of the newest version this app has (the replay test downloads them) on the
 * user's Supreme Commander: Forged Alliance. A mouse is the mouse and fingers act as one
 * (TouchInput), keys are the keyboard, and Back is Esc; a bar at the screen's edge has what a mouse
 * and keyboard have and fingers have not. The game draws at its own size (PictureSize). The report's
 * lines show on the screen while the game starts, and again when it ends or fails.
 */
public final class GameActivity extends Activity implements SurfaceHolder.Callback {
    /** The program's path in its install, as FAF's client starts it: the engine takes its folder from it. */
    private static final String PROGRAM = "bin/ForgedAlliance.exe";
    private static final int REPORT_LINES = 14;
    /** A test's factor for the HUD, in place of the screen's (am start --ef). */
    private static final String EXTRA_HUD_SCALE = "hudScale";
    /** How far a finger's tap looks for a small control, and what counts as one. */
    private static final double TAP_REACH_MM = 2.5;
    private static final double TAP_SMALL_MM = 5;
    /** The bar's buttons: in a session at a finger's size if there is room, in the menus smaller. */
    private static final double BAR_BUTTON_MM = 9;
    private static final double BAR_MENU_BUTTON_MM = 6.5;
    private static final double BAR_GAP_MM = 0.5;
    private static final float BAR_TEXT_PER_BUTTON = 0.27f;

    /** MotionEvent's buttons and the host's. */
    private static final int[][] BUTTONS = {
        {MotionEvent.BUTTON_PRIMARY, Game.BUTTON_LEFT},
        {MotionEvent.BUTTON_SECONDARY, Game.BUTTON_RIGHT},
        {MotionEvent.BUTTON_TERTIARY, Game.BUTTON_MIDDLE},
        {MotionEvent.BUTTON_BACK, Game.BUTTON_X1},
        {MotionEvent.BUTTON_FORWARD, Game.BUTTON_X2},
    };

    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private SurfaceView mSurface;
    private TextView mText;
    private LinearLayout mBar;
    /** What the bar has in a game and not in the menus, and the Keys, which it has in both. */
    private final List<TextView> mGameButtons = new ArrayList<>();
    private TextView mKeysButton;
    private boolean mBarInGame;
    /** The HUD's factor and the screen's short side, which the picture's lines are spread over. */
    private double mHudScale;
    private double mScreenMm;
    private View mKeyboardTarget;
    /** What the on-screen keyboard has typed: in landscape it covers most of the game's screen. */
    private TextView mTyped;
    private boolean mStarted;
    /** The mouse's buttons that are down (MotionEvent's BUTTON_ bits). */
    private int mButtons;
    /** The lines the user chose, and the picture's size as the surface has it. */
    private int mLines;
    private int mPictureHeight;
    /** Held while the activity is in front. The game's sound stops for another app's, even for a short one: the system lowers no AAudio stream. */
    private AudioFocusRequest mAudioFocus;
    /** Gone for good: nothing will give it back, as it does after a call. */
    private boolean mAudioFocusLost;
    /** Held while the activity is in front: Wi-Fi drops the broadcasts that find LAN games on a phone without it, and holding it costs battery. */
    private WifiManager.MulticastLock mDiscoveryLock;

    /** Takes the fingers: it does not move with the picture, so their places stay the screen's. */
    private View mFingerLayer;
    private final PageView mPageView = new PageView();
    private final int[] mPageRect = new int[4];
    /** A text field that was tapped or has the focus, in the picture's lines, until the keyboard has been up and gone. */
    private final int[] mField = new int[2];
    private final int[] mToShow = new int[2];
    private boolean mFieldKnown;
    private int mKeyboardPixels;
    /** What a finger has gone in the screen's pixels and nobody has taken yet: the program has not said what list is under it. */
    private float mUnsentDrag;

    private TouchInput mTouch;
    private PageInput mPageInput;
    /** Whose the fingers now on the glass are: Lua can change it, not under them. */
    private Fingers mFingers;
    private final Runnable mTouchTimer = () -> mTouch.timer();
    private final Runnable mPageTimer = () -> mPageInput.timer();
    private final Runnable mPointerLeave = Game::pointerLeave;
    private final Runnable mReadProgram = this::readProgram;
    /** On the program's thread. */
    private final Runnable mProgramSaid = () -> {
        mHandler.removeCallbacks(mReadProgram);
        mHandler.post(mReadProgram);
    };
    private final TouchInput.Mouse mTouchMouse = new TouchInput.Mouse() {
        @Override
        public void move(float x, float y) {
            Game.mouseMove(pictureX(x), pictureY(y));
        }

        @Override
        public void button(int button, boolean down, float x, float y) {
            Game.touchButton(button, down, pictureX(x), pictureY(y));
        }

        @Override
        public void tap(float x, float y) {
            Game.tap(pictureX(x), pictureY(y), pictureMm(TAP_REACH_MM), pictureMm(TAP_SMALL_MM));
        }

        @Override
        public void leaveSoon() {
            mHandler.removeCallbacks(mPointerLeave);
            mHandler.postDelayed(mPointerLeave, TouchInput.LEAVE_MILLISECONDS);
        }

        @Override
        public void stay() {
            mHandler.removeCallbacks(mPointerLeave);
            Game.pointerStay();
        }

        @Override
        public void tapNearGhost(float fingerX, float fingerY, float ghostX, float ghostY) {
            Game.tapNearGhost(pictureX(fingerX), pictureY(fingerY), pictureX(ghostX), pictureY(ghostY));
        }

        @Override
        public void startLine(float x, float y) {
            Game.mouseButton(Game.BUTTON_LEFT, true, pictureX(x), pictureY(y));
        }

        @Override
        public void cancelLine() {
            Game.cancelLine();
        }

        @Override
        public void wheel(int delta, float x, float y) {
            Game.mouseWheel(delta, pictureX(x), pictureY(y));
        }

        @Override
        public void held() {
            mSurface.performHapticFeedback(HapticFeedbackConstants.LONG_PRESS);
        }

        @Override
        public boolean placing() {
            return Game.placingBuilding();
        }

        @Override
        public void deleteOrder(float x, float y) {
            Game.deleteOrder(pictureX(x), pictureY(y));
        }

        @Override
        public void startTimer(long milliseconds) {
            mHandler.removeCallbacks(mTouchTimer);
            mHandler.postDelayed(mTouchTimer, milliseconds);
        }

        @Override
        public void stopTimer() {
            mHandler.removeCallbacks(mTouchTimer);
        }
    };

    private final PageInput.Mouse mPageMouse = new PageInput.Mouse() {
        @Override
        public void move(float x, float y) {
            Game.mouseMove(pictureX(x), pictureY(y));
        }

        @Override
        public void press(float x, float y) {
            Game.pressNear(pictureX(x), pictureY(y), pictureMm(TAP_REACH_MM), pictureMm(TAP_SMALL_MM));
        }

        @Override
        public void release(float x, float y) {
            Game.touchButton(Game.BUTTON_LEFT, false, pictureX(x), pictureY(y));
        }

        @Override
        public void tap(float x, float y) {
            mTouchMouse.tap(x, y);
        }

        @Override
        public void rightClick(float x, float y) {
            Game.rightClickNear(pictureX(x), pictureY(y), pictureMm(TAP_REACH_MM), pictureMm(TAP_SMALL_MM));
        }

        @Override
        public void lookForList(float x, float y) {
            mUnsentDrag = 0;
            Game.lookForList(pictureX(x), pictureY(y));
        }

        @Override
        public void scroll(float dy) {
            mUnsentDrag += dy;
            passDrag();
        }

        @Override
        public void held() {
            mTouchMouse.held();
        }

        @Override
        public void leave() {
            mHandler.removeCallbacks(mPointerLeave);
            Game.pointerLeave();
        }

        @Override
        public void leaveSoon() {
            mTouchMouse.leaveSoon();
        }

        @Override
        public void stay() {
            mTouchMouse.stay();
        }

        @Override
        public void startTimer(long milliseconds) {
            mHandler.removeCallbacks(mPageTimer);
            mHandler.postDelayed(mPageTimer, milliseconds);
        }

        @Override
        public void stopTimer() {
            mHandler.removeCallbacks(mPageTimer);
        }
    };

    private final Runnable mPoll = new Runnable() {
        @Override
        public void run() {
            showReport();
            mHandler.postDelayed(this, 500);
        }
    };

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        setVolumeControlStream(AudioManager.STREAM_MUSIC);
        mAudioFocus = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
            .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME).build())
            .setAcceptsDelayedFocusGain(true)
            .setWillPauseWhenDucked(true)
            .setOnAudioFocusChangeListener(this::onAudioFocusChange, mHandler)
            .build();
        WifiManager wifi = getApplicationContext().getSystemService(WifiManager.class);
        if (wifi != null) {
            mDiscoveryLock = wifi.createMulticastLock("faf-game-discovery");
            mDiscoveryLock.setReferenceCounted(false);
        }
        if (Build.VERSION.SDK_INT >= 28) {
            WindowManager.LayoutParams attributes = getWindow().getAttributes();
            attributes.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(attributes);
        }
        mLines = PictureSize.lines(this);
        mHudScale = hudScale();
        mScreenMm = screenShortSideMm();
        mTouch = new TouchInput(mTouchMouse, ViewConfiguration.get(this).getScaledTouchSlop());
        mPageInput = new PageInput(mPageMouse, ViewConfiguration.get(this).getScaledTouchSlop());
        mFingers = mTouch;
        FrameLayout root = new FrameLayout(this);
        mSurface = new SurfaceView(this);
        mSurface.getHolder().addCallback(this);
        fitPicture();
        root.addView(mSurface);
        mFingerLayer = new View(this);
        // It draws nothing, and the surface's hole in the window stays a hole.
        mFingerLayer.setWillNotDraw(true);
        mFingerLayer.setOnTouchListener((view, event) -> onSurfaceTouch(event));
        mFingerLayer.addOnLayoutChangeListener((view, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) -> {
            mTouch.setScreen(right - left, bottom - top);
            mPageView.setScreen(right - left, bottom - top);
            fitPicture(right - left, bottom - top);
            placeSurface();
        });
        root.addView(mFingerLayer);
        mKeyboardTarget = new KeyboardTarget(this);
        root.addView(mKeyboardTarget, new FrameLayout.LayoutParams(1, 1));
        mText = new TextView(this);
        mText.setTextColor(Color.WHITE);
        mText.setShadowLayer(4, 0, 0, Color.BLACK);
        mText.setTextSize(12);
        mText.setGravity(Gravity.TOP | Gravity.START);
        mText.setPadding(48, 48, 48, 48);
        root.addView(mText);
        mBar = bar();
        mBar.setVisibility(View.GONE);
        root.addView(mBar, new FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.START | Gravity.TOP));
        placeBar(false);
        mTyped = new TextView(this);
        mTyped.setTextSize(18);
        mTyped.setTextColor(Color.WHITE);
        mTyped.setHintTextColor(Color.LTGRAY);
        mTyped.setHint("Typing goes to the game's text field");
        mTyped.setBackground(barBackground(false));
        mTyped.setPadding(dp(12), dp(6), dp(12), dp(6));
        mTyped.setVisibility(View.GONE);
        root.addView(mTyped, new FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.TOP | Gravity.CENTER_HORIZONTAL));
        if (Build.VERSION.SDK_INT >= 30) {
            root.setOnApplyWindowInsetsListener((view, insets) -> {
                boolean typing = insets.isVisible(WindowInsets.Type.ime());
                showTyped(typing);
                keyboardCovers(typing ? insets.getInsets(WindowInsets.Type.ime()).bottom : 0);
                // A phone's camera can sit in the middle of the screen's short edge.
                mBar.setPadding(insets.getInsets(WindowInsets.Type.displayCutout()).left, 0, 0, 0);
                return insets;
            });
        }
        setContentView(root);
        hideSystemBars();
        Game.listen(mProgramSaid);

        // Android made the activity anew around the running program (it does when it destroys a
        // stopped activity): the same game goes on, with the callbacks of this one.
        if (Game.loadError() == null && Game.running()) {
            mStarted = true;
            mHandler.post(mPoll);
            mHandler.post(mReadProgram);
            return;
        }
        GameReport.begin(this);
        String problem = start();
        if (problem != null) {
            GameReport.append(this, GameReport.END + problem);
            mText.setText(problem + "\n\n(Back returns)");
            return;
        }
        mHandler.post(mPoll);
    }

    // --- The picture ----------------------------------------------------------------------------

    /** Before the first layout: the window as it will be once the activity has turned. */
    private void fitPicture() {
        if (Build.VERSION.SDK_INT >= 30) {
            Rect bounds = getWindowManager().getCurrentWindowMetrics().getBounds();
            fitPicture(Math.max(bounds.width(), bounds.height()), Math.min(bounds.width(), bounds.height()));
        }
    }

    private void fitPicture(int viewWidth, int viewHeight) {
        // Standing is the moment before the activity has turned, unless the window is a part of the screen.
        if (viewWidth <= 0 || viewHeight <= 0 || (viewHeight > viewWidth && !isInMultiWindowMode())) {
            return;
        }
        int[] size = PictureSize.of(viewWidth, viewHeight, mLines);
        mSurface.getHolder().setFixedSize(size[0], size[1]);
    }

    /** The HUD's factor for this screen, or what a test gives (the emulator keeps its own millimetres at another size). */
    private double hudScale() {
        double given = getIntent().getFloatExtra(EXTRA_HUD_SCALE, 0);
        return given > 0 ? given : HudScale.factor(pictureLines(), screenShortSideMm());
    }

    /** The lines the picture will have, whatever the activity's turn has done so far. */
    private int pictureLines() {
        DisplayMetrics metrics = realMetrics();
        int longSide = Math.max(metrics.widthPixels, metrics.heightPixels);
        int shortSide = Math.min(metrics.widthPixels, metrics.heightPixels);
        return PictureSize.of(longSide, shortSide, mLines)[1];
    }

    private double screenShortSideMm() {
        DisplayMetrics metrics = realMetrics();
        return HudScale.shortSideMm(shortSide(display().getMode()), largestModeShortSide(), (metrics.xdpi + metrics.ydpi) / 2,
            Math.min(metrics.widthPixels, metrics.heightPixels), metrics.densityDpi);
    }

    /** What the factor came from, for the report: phones differ in what they say of their screens. */
    private String screenInWords() {
        DisplayMetrics metrics = realMetrics();
        return String.format(Locale.ROOT, "%d lines, %s, xdpi %.0f ydpi %.0f, window %dx%d", pictureLines(),
            HudScale.shortSideInWords(shortSide(display().getMode()), largestModeShortSide(), (metrics.xdpi + metrics.ydpi) / 2,
                Math.min(metrics.widthPixels, metrics.heightPixels), metrics.densityDpi),
            metrics.xdpi, metrics.ydpi, metrics.widthPixels, metrics.heightPixels);
    }

    private int largestModeShortSide() {
        int largest = 0;
        for (Display.Mode mode : display().getSupportedModes()) {
            largest = Math.max(largest, shortSide(mode));
        }
        return largest;
    }

    private static int shortSide(Display.Mode mode) {
        return Math.min(mode.getPhysicalWidth(), mode.getPhysicalHeight());
    }

    @SuppressWarnings("deprecation")
    private Display display() {
        return getWindowManager().getDefaultDisplay();
    }

    @SuppressWarnings("deprecation")
    private DisplayMetrics realMetrics() {
        DisplayMetrics metrics = new DisplayMetrics();
        display().getRealMetrics(metrics);
        return metrics;
    }

    /** From the screen's pixels to the picture's, which are the game's screen. */
    private int pictureX(float x) {
        return mPageView.pictureX(x);
    }

    private int pictureY(float y) {
        return mPageView.pictureY(y);
    }

    // --- The page: FAF's Lua says which, and a finger scrolls it ------------------------------------

    /** On the activity's thread: what the program has said since the last time. */
    private void readProgram() {
        if (!mStarted) {
            return;
        }
        boolean changed = Game.page(mPageRect)
            ? mPageView.setPage(mPageRect[0], mPageRect[1], mPageRect[2], mPageRect[3])
            : mPageView.clearPage();
        if (changed) {
            hideKeyboard();
            placeSurface();
        }
        placeBarAsNow();
        passDrag();
        float rest = Game.takePageDrag();
        if (rest != 0) {
            movePage(rest * mPageView.scale());
        }
        int why = Game.takeToShow(mToShow);
        if (why == Game.LINES_TO_SHOW) {
            mPageView.reveal(mToShow[0], mToShow[1]);
            placeSurface();
        } else if (why != 0) {
            mField[0] = mToShow[0];
            mField[1] = mToShow[1];
            mFieldKnown = true;
            if (why == Game.FIELD_TAPPED && mTyped.getVisibility() != View.VISIBLE) {
                showKeyboard();
            }
            revealField();
        }
    }

    /** Lays the surface out at the size of the view. Its buffers keep the picture's size (setFixedSize), so the swap chain stays as it is. */
    private void placeSurface() {
        int width = mPageView.zoomed() ? mPageView.viewWidth() : FrameLayout.LayoutParams.MATCH_PARENT;
        int height = mPageView.zoomed() ? mPageView.viewHeight() : FrameLayout.LayoutParams.MATCH_PARENT;
        FrameLayout.LayoutParams place = (FrameLayout.LayoutParams) mSurface.getLayoutParams();
        if (place.width != width || place.height != height) {
            place.width = width;
            place.height = height;
            mSurface.setLayoutParams(place);
        }
        mSurface.setTranslationX(mPageView.viewLeft());
        mSurface.setTranslationY(mPageView.viewTop());
    }

    /** What a finger has gone goes to the list under it, or if it is known that none is, to the page. */
    private void passDrag() {
        if (mUnsentDrag == 0) {
            return;
        }
        int answer = Game.listAnswer();
        if (answer == Game.NO_LIST) {
            movePage(mUnsentDrag);
            mUnsentDrag = 0;
        } else if (answer == Game.LIST) {
            Game.scrollList(mUnsentDrag / mPageView.scale());
            mUnsentDrag = 0;
        }
    }

    private void movePage(float dy) {
        mPageView.scrollBy(dy);
        placeSurface();
    }

    /** The keyboard covers this much of the screen from below: what is typed in has to stay above it. */
    private void keyboardCovers(int pixels) {
        // Before it comes up, insets can say that it is not there yet.
        if (mKeyboardPixels > 0 && pixels == 0) {
            mFieldKnown = false;
        }
        mKeyboardPixels = pixels;
        mPageView.setKeyboard(pixels);
        placeSurface();
        if (pixels > 0) {
            revealField();
        }
    }

    private void revealField() {
        if (mFieldKnown && mTyped.getVisibility() == View.VISIBLE) {
            mPageView.reveal(mField[0], mField[1]);
            placeSurface();
        }
    }

    // --- The bar: what a mouse and a keyboard have and fingers have not ---------------------------

    private LinearLayout bar() {
        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.VERTICAL);
        LinearLayout top = new LinearLayout(this);
        toggle(top, "Order", mTouch::setSwapped);
        toggle(top, "Shift", on -> Game.key(KeyEvent.KEYCODE_SHIFT_LEFT, 0, on, 0));
        toggle(top, "Ctrl", on -> Game.key(KeyEvent.KEYCODE_CTRL_LEFT, 0, on, 0));
        bar.addView(top);
        LinearLayout bottom = new LinearLayout(this);
        toggle(bottom, "Del", mTouch::setDeleting);
        mKeysButton = barButton(bottom, "Keys");
        mKeysButton.setOnClickListener(view -> toggleKeyboard());
        bar.addView(bottom);
        return bar;
    }

    /** The game's buttons are for a game that fingers are on: a dialog over it has the fingers of the menus. */
    private void placeBarAsNow() {
        boolean inGame = Game.gamePlaying() && !Game.menuFingers();
        if (inGame != mBarInGame) {
            placeBar(inGame);
        }
    }

    /** In a game the bar sits where the HUD leaves room (HudScale), in the menus only the keyboard's button stays, in a corner. */
    private void placeBar(boolean inGame) {
        mBarInGame = inGame;
        int size = viewMm(inGame ? Math.min(BAR_BUTTON_MM, barRoomMm() / 2 - BAR_GAP_MM) : BAR_MENU_BUTTON_MM);
        int gap = viewMm(BAR_GAP_MM / 2);
        for (TextView button : mGameButtons) {
            button.setVisibility(inGame ? View.VISIBLE : View.GONE);
            sizeBarButton(button, size, gap);
            // The menus have no use for them, and a finger could not let go of one there.
            if (!inGame && button.isSelected()) {
                button.performClick();
            }
        }
        sizeBarButton(mKeysButton, size, gap);
        FrameLayout.LayoutParams place = (FrameLayout.LayoutParams) mBar.getLayoutParams();
        place.topMargin = inGame ? (int) Math.round(HudScale.barTopPixels(hudFactor()) * viewPixelsPerPicturePixel()) : 0;
        mBar.setLayoutParams(place);
    }

    /** The HUD's factor: the user's UI scale can have changed what the app worked out. */
    private double hudFactor() {
        double factor = Game.hudFactor();
        return factor > 0 ? factor : mHudScale;
    }

    /** The room the HUD leaves for the bar, in mm. */
    private double barRoomMm() {
        return HudScale.barRoomPixels(hudFactor(), pictureLines()) * mScreenMm / pictureLines();
    }

    private static void sizeBarButton(TextView button, int size, int gap) {
        LinearLayout.LayoutParams place = (LinearLayout.LayoutParams) button.getLayoutParams();
        place.width = size;
        place.height = size;
        place.setMargins(gap, gap, gap, gap);
        button.setLayoutParams(place);
        button.setTextSize(TypedValue.COMPLEX_UNIT_PX, size * BAR_TEXT_PER_BUTTON);
    }

    /** This many mm in the view's pixels. */
    private int viewMm(double mm) {
        DisplayMetrics metrics = realMetrics();
        return (int) Math.round(mm * Math.min(metrics.widthPixels, metrics.heightPixels) / mScreenMm);
    }

    private double viewPixelsPerPicturePixel() {
        DisplayMetrics metrics = realMetrics();
        return (double) Math.min(metrics.widthPixels, metrics.heightPixels) / pictureLines();
    }

    /** This many mm on the glass in the picture's pixels, which a page shows larger. */
    private int pictureMm(double mm) {
        return (int) Math.round(mm * mPictureHeight / mScreenMm / mPageView.zoom());
    }

    private interface Toggled {
        void set(boolean on);
    }

    private void toggle(LinearLayout row, String label, Toggled toggled) {
        TextView button = barButton(row, label);
        mGameButtons.add(button);
        button.setOnClickListener(view -> {
            view.setSelected(!view.isSelected());
            view.setBackground(barBackground(view.isSelected()));
            toggled.set(view.isSelected());
        });
    }

    private TextView barButton(LinearLayout row, String label) {
        TextView button = new TextView(this);
        button.setText(label);
        button.setTextColor(Color.WHITE);
        button.setGravity(Gravity.CENTER);
        button.setBackground(barBackground(false));
        row.addView(button, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        return button;
    }

    private GradientDrawable barBackground(boolean on) {
        GradientDrawable background = new GradientDrawable();
        background.setCornerRadius(dp(8));
        background.setColor(on ? 0xC0D08020 : 0x70202830);
        return background;
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private void toggleKeyboard() {
        if (mTyped.getVisibility() == View.VISIBLE) {
            hideKeyboard();
            return;
        }
        if (mStarted) {
            Game.askForField();
        }
        showKeyboard();
    }

    private void showKeyboard() {
        mKeyboardTarget.requestFocus();
        getSystemService(InputMethodManager.class).showSoftInput(mKeyboardTarget, 0);
        showTyped(true);
    }

    private void hideKeyboard() {
        getSystemService(InputMethodManager.class).hideSoftInputFromWindow(mKeyboardTarget.getWindowToken(), 0);
        showTyped(false);
    }

    private void showTyped(boolean shown) {
        if (!shown) {
            mTyped.setText("");
        }
        mTyped.setVisibility(shown ? View.VISIBLE : View.GONE);
        if (Build.VERSION.SDK_INT < 30) {
            // Nothing tells how high the keyboard is: in landscape about half of the screen.
            keyboardCovers(shown ? mFingerLayer.getHeight() / 2 : 0);
        }
    }

    /** A key that went down, while the keyboard covers the game's text field. */
    private void noteTyped(int keyCode, int character) {
        CharSequence typed = mTyped.getText();
        if (keyCode == KeyEvent.KEYCODE_DEL) {
            mTyped.setText(typed.length() > 0 ? typed.subSequence(0, typed.length() - 1) : typed);
        } else if (character != 0) {
            mTyped.append(String.valueOf((char) character));
        }
    }

    /** What the on-screen keyboard types at: it sends plain keys, which go on as a keyboard's (dispatchKeyEvent). */
    private static final class KeyboardTarget extends View {
        KeyboardTarget(Context context) {
            super(context);
            setFocusable(true);
            setFocusableInTouchMode(true);
        }

        @Override
        public boolean onCheckIsTextEditor() {
            return true;
        }

        @Override
        public InputConnection onCreateInputConnection(EditorInfo attributes) {
            attributes.inputType = InputType.TYPE_NULL;
            attributes.imeOptions = EditorInfo.IME_FLAG_NO_FULLSCREEN | EditorInfo.IME_FLAG_NO_EXTRACT_UI;
            return new BaseInputConnection(this, false) {
                /** Some keyboards delete through here and not with the key. The text is in the game. */
                @Override
                public boolean deleteSurroundingText(int before, int after) {
                    press(KeyEvent.KEYCODE_DEL, before);
                    press(KeyEvent.KEYCODE_FORWARD_DEL, after);
                    return true;
                }

                private void press(int key, int times) {
                    for (int at = 0; at < times; ++at) {
                        sendKeyEvent(new KeyEvent(KeyEvent.ACTION_DOWN, key));
                        sendKeyEvent(new KeyEvent(KeyEvent.ACTION_UP, key));
                    }
                }
            };
        }
    }

    /** Starts the program, or says why it cannot. */
    private String start() {
        if (Game.loadError() != null) {
            return "FAIL: libfafgame.so did not load: " + Game.loadError();
        }
        Storage storage;
        try {
            storage = new Storage(this);
        } catch (IOException e) {
            return "FAIL: " + e.getMessage();
        }
        int archives = ScfaImport.archives(storage);
        if (archives == 0) {
            return "FAIL: no Supreme Commander: Forged Alliance imported yet (card 2)";
        }
        if (ScfaImport.fonts(storage) == 0) {
            return "FAIL: no fonts of Supreme Commander: Forged Alliance yet: import its folder again (card 2), which takes fonts/ too";
        }
        int version = newestGameFiles(storage);
        if (version < 0) {
            return "FAIL: no whole FAF game files yet; the replay test downloads them (card 3)";
        }
        File game = storage.game(version);
        File folder = Storage.mkdirs(GameReport.folder(storage));
        File vault = Storage.mkdirs(new File(folder, "vault"));
        Storage.mkdirs(new File(vault, "maps"));
        Storage.mkdirs(new File(vault, "mods"));
        File init;
        try {
            init = GameFiles.gameInit(getAssets(), game, vault);
        } catch (IOException e) {
            return "FAIL: the init file: " + e.getMessage();
        }
        String program = new File(game, PROGRAM).getPath();
        String log = GameReport.log(storage).getPath();
        String hudScale = HudScale.argument(mHudScale);
        String hudMost = HudScale.argument(HudScale.most(pictureLines()));
        // An import by an earlier release has no movies, and the splash screen would wait for a tap for each.
        String[] arguments = ScfaImport.movies(storage) > 0
            ? new String[] {program, "/init", init.getPath(), "/log", log, HudScale.SWITCH, hudScale, HudScale.MOST_SWITCH, hudMost}
            : new String[] {program, "/init", init.getPath(), "/log", log, HudScale.SWITCH, hudScale, HudScale.MOST_SWITCH, hudMost, "/nomovie"};
        GameReport.append(this, "game: FAF game version " + version + ", " + archives + " SCFA archives");
        GameReport.append(this, "game: HUD scale " + hudScale + " on " + screenInWords());
        File home = Storage.mkdirs(GameReport.home(storage));
        if (!Game.start(arguments, home.getPath(), getCacheDir().getPath(), GameReport.file(this).getPath())) {
            return "FAIL: the program did not start";
        }
        mStarted = true;
        return null;
    }

    /** The newest FAF game version whose files are ready, or -1. */
    private static int newestGameFiles(Storage storage) {
        String[] names = new File(storage.parity, "game").list();
        int newest = -1;
        if (names != null) {
            for (String name : names) {
                try {
                    int version = Integer.parseInt(name);
                    if (version > newest && GameFiles.ready(storage, version)) {
                        newest = version;
                    }
                } catch (NumberFormatException e) {
                    // not a version's folder
                }
            }
        }
        return newest;
    }

    /** The report's last lines over the surface, until the first frame and again at the end. */
    private void showReport() {
        String report;
        try {
            report = new String(Files.readAllBytes(GameReport.file(this).toPath()), StandardCharsets.UTF_8);
        } catch (IOException e) {
            return;
        }
        boolean ended = GameReport.ended(report);
        mBar.setVisibility(GameReport.firstFrame(report) && !ended ? View.VISIBLE : View.GONE);
        placeBarAsNow();
        if (GameReport.firstFrame(report) && !ended) {
            mText.setVisibility(View.GONE);
            return;
        }
        String[] lines = report.split("\n");
        StringBuilder text = new StringBuilder(ended ? "Forged Alliance has ended.\n\n" : "Forged Alliance is starting.\n\n");
        for (int i = Math.max(0, lines.length - REPORT_LINES); i < lines.length; ++i) {
            text.append(lines[i]).append('\n');
        }
        mText.setText(text);
        mText.setVisibility(View.VISIBLE);
    }

    @Override
    protected void onResume() {
        super.onResume();
        hideSystemBars();
        if (mDiscoveryLock != null) {
            mDiscoveryLock.acquire();
        }
        if (mStarted) {
            // First: the output starts at the word that both are so, and it must not during a call.
            requestAudioFocus();
            Game.foreground(true);
        }
    }

    @Override
    protected void onPause() {
        if (mStarted) {
            mFingers.cancel();
            Game.foreground(false);
            getSystemService(AudioManager.class).abandonAudioFocusRequest(mAudioFocus);
        }
        if (mDiscoveryLock != null) {
            mDiscoveryLock.release();
        }
        super.onPause();
    }

    /** While a call has the focus this is delayed, and onAudioFocusChange hears when it comes. */
    private void requestAudioFocus() {
        int focus = getSystemService(AudioManager.class).requestAudioFocus(mAudioFocus);
        mAudioFocusLost = focus == AudioManager.AUDIOFOCUS_REQUEST_FAILED;
        Game.audioFocus(focus == AudioManager.AUDIOFOCUS_REQUEST_GRANTED);
    }

    private void onAudioFocusChange(int change) {
        if (change == AudioManager.AUDIOFOCUS_GAIN) {
            mAudioFocusLost = false;
            Game.audioFocus(true);
        } else if (change == AudioManager.AUDIOFOCUS_LOSS) {
            mAudioFocusLost = true;
            Game.audioFocus(false);
        } else if (change == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT || change == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK) {
            Game.audioFocus(false);
        }
    }

    /** The player is back at the game: its sound returns if the app that took the focus for good has stopped. */
    private void takeAudioFocusBack() {
        if (mAudioFocusLost && !getSystemService(AudioManager.class).isMusicActive()) {
            requestAudioFocus();
        }
    }

    @Override
    protected void onDestroy() {
        Game.stopListening(mProgramSaid);
        mHandler.removeCallbacks(mPoll);
        mHandler.removeCallbacks(mReadProgram);
        mHandler.removeCallbacks(mTouchTimer);
        mHandler.removeCallbacks(mPageTimer);
        mHandler.removeCallbacks(mPointerLeave);
        super.onDestroy();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
            if (mStarted) {
                takeAudioFocusBack();
            }
        }
    }

    private void hideSystemBars() {
        if (Build.VERSION.SDK_INT >= 30) {
            getWindow().setDecorFitsSystemWindows(false);
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
        }
    }

    // --- The surface --------------------------------------------------------------------------

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        mPictureHeight = height;
        mPageView.setPicture(width, height);
        placeSurface();
        if (mStarted) {
            Game.surface(holder.getSurface(), width, height);
        }
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        if (mStarted) {
            Game.surface(null, 0, 0);
        }
    }

    // --- Input --------------------------------------------------------------------------------

    /** What lands on the picture: on the bar's buttons a finger is theirs, and the others stay here. */
    private boolean onSurfaceTouch(MotionEvent event) {
        if (!mStarted) {
            return false;
        }
        takeAudioFocusBack();
        if (event.isFromSource(InputDevice.SOURCE_MOUSE)) {
            mouse(event);
            return true;
        }
        int index = event.getActionIndex();
        int id = event.getPointerId(index);
        int action = event.getActionMasked();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
                mFingers = Game.menuFingers() ? mPageInput : mTouch;
                mFingers.down(id, event.getX(index), event.getY(index), event.getEventTime(), true);
                break;
            case MotionEvent.ACTION_POINTER_DOWN:
                mFingers.down(id, event.getX(index), event.getY(index), event.getEventTime(), false);
                break;
            case MotionEvent.ACTION_MOVE:
                for (int at = 0; at < event.getPointerCount(); ++at) {
                    mFingers.move(event.getPointerId(at), event.getX(at), event.getY(at));
                }
                mFingers.moved();
                break;
            case MotionEvent.ACTION_UP:
                mFingers.up(id, event.getEventTime(), true);
                break;
            case MotionEvent.ACTION_POINTER_UP:
                if ((event.getFlags() & MotionEvent.FLAG_CANCELED) != 0) {
                    mFingers.forget(id, event.getEventTime());
                } else {
                    mFingers.up(id, event.getEventTime(), false);
                }
                break;
            case MotionEvent.ACTION_CANCEL:
                mFingers.cancel();
                break;
            default:
                break;
        }
        return true;
    }

    @Override
    public boolean onGenericMotionEvent(MotionEvent event) {
        if (!mStarted || !event.isFromSource(InputDevice.SOURCE_CLASS_POINTER)) {
            return super.onGenericMotionEvent(event);
        }
        if (event.getActionMasked() == MotionEvent.ACTION_SCROLL) {
            int delta = Math.round(event.getAxisValue(MotionEvent.AXIS_VSCROLL) * Game.WHEEL_DELTA);
            if (delta != 0) {
                Game.mouseWheel(delta, pictureX(event.getX()), pictureY(event.getY()));
            }
            return true;
        }
        mouse(event);
        return true;
    }

    /** A mouse's event: where it is, and the buttons that went down or up since its last one. */
    private void mouse(MotionEvent event) {
        int x = pictureX(event.getX());
        int y = pictureY(event.getY());
        Game.mouseMove(x, y);
        int buttons = event.getButtonState();
        int changed = buttons ^ mButtons;
        for (int[] button : BUTTONS) {
            if ((changed & button[0]) != 0) {
                Game.mouseButton(button[1], (buttons & button[0]) != 0, x, y);
            }
        }
        mButtons = buttons;
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (!mStarted || systemKey(event.getKeyCode())) {
            return super.dispatchKeyEvent(event);
        }
        if (event.getAction() == KeyEvent.ACTION_MULTIPLE) {
            String characters = event.getCharacters();
            if (characters != null) {
                Game.text(characters);
                if (mTyped.getVisibility() == View.VISIBLE) {
                    mTyped.append(characters);
                }
            }
            return true;
        }
        // An on-screen keyboard's capital comes with a Shift of its own, which would let go of the bar's;
        // the character says it all.
        if (event.getDeviceId() == KeyCharacterMap.VIRTUAL_KEYBOARD && KeyEvent.isModifierKey(event.getKeyCode())) {
            return true;
        }
        boolean down = event.getAction() == KeyEvent.ACTION_DOWN;
        int character = down ? event.getUnicodeChar(event.getMetaState()) : 0;
        if ((character & KeyCharacterMap.COMBINING_ACCENT) != 0) {
            // A dead key types nothing by itself.
            character = 0;
        }
        Game.key(event.getKeyCode(), event.getScanCode(), down, character);
        if (mTyped.getVisibility() == View.VISIBLE) {
            if (event.getKeyCode() == KeyEvent.KEYCODE_ENTER) {
                // Whatever it was typed for is done with it.
                if (!down) {
                    hideKeyboard();
                }
            } else if (down) {
                noteTyped(event.getKeyCode(), character);
            }
        }
        return true;
    }

    /** The keys the phone keeps for itself. */
    private static boolean systemKey(int keyCode) {
        switch (keyCode) {
            case KeyEvent.KEYCODE_VOLUME_UP:
            case KeyEvent.KEYCODE_VOLUME_DOWN:
            case KeyEvent.KEYCODE_VOLUME_MUTE:
            case KeyEvent.KEYCODE_POWER:
            case KeyEvent.KEYCODE_HOME:
            case KeyEvent.KEYCODE_APP_SWITCH:
            case KeyEvent.KEYCODE_ASSIST:
            case KeyEvent.KEYCODE_VOICE_ASSIST:
            case KeyEvent.KEYCODE_CAMERA:
            case KeyEvent.KEYCODE_FOCUS:
            case KeyEvent.KEYCODE_SLEEP:
            case KeyEvent.KEYCODE_WAKEUP:
            case KeyEvent.KEYCODE_BRIGHTNESS_UP:
            case KeyEvent.KEYCODE_BRIGHTNESS_DOWN:
            case KeyEvent.KEYCODE_HEADSETHOOK:
            case KeyEvent.KEYCODE_CALL:
            case KeyEvent.KEYCODE_ENDCALL:
            case KeyEvent.KEYCODE_MUTE:
            case KeyEvent.KEYCODE_MEDIA_PLAY:
            case KeyEvent.KEYCODE_MEDIA_PAUSE:
            case KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE:
            case KeyEvent.KEYCODE_MEDIA_STOP:
            case KeyEvent.KEYCODE_MEDIA_NEXT:
            case KeyEvent.KEYCODE_MEDIA_PREVIOUS:
            case KeyEvent.KEYCODE_MEDIA_REWIND:
            case KeyEvent.KEYCODE_MEDIA_FAST_FORWARD:
            case KeyEvent.KEYCODE_MEDIA_RECORD:
                return true;
            default:
                return false;
        }
    }
}
