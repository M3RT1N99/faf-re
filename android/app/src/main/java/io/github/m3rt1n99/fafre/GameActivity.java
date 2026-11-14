package io.github.m3rt1n99.fafre;

import android.animation.ValueAnimator;
import android.app.Activity;
import android.content.Context;
import android.graphics.Bitmap;
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
import android.os.SystemClock;
import android.text.InputType;
import android.util.DisplayMetrics;
import android.util.TypedValue;
import android.view.Choreographer;
import android.view.Display;
import android.view.Gravity;
import android.view.HapticFeedbackConstants;
import android.view.InputDevice;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.PixelCopy;
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
 * FAF's game files of the newest version this app has (the Game card's "Get FAF's newest version"
 * or the replay test downloads them) on the user's Supreme Commander: Forged Alliance. A mouse is
 * the mouse and fingers act as one
 * (TouchInput), or with the touch scheme Griff (TouchScheme) as its gestures, keys are the keyboard,
 * and Back is Esc; a bar at the screen's edge has what a mouse and keyboard have and fingers have
 * not, or Griff's rail. The game draws at its own size (PictureSize). The report's
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
    /**
     * Griff's handle (the preview's sizes): its radius; how much further from it a finger is still on it; its room from the
     * selection's units' blueprint box, which is wider than their icons.
     */
    private static final double GRIFF_RADIUS_MM = 5.5;
    private static final double GRIFF_TOUCH_MM = 1.5;
    private static final double GRIFF_GAP_MM = 2;
    /** How far beyond the handle's edge its arrow at a group elsewhere reaches. */
    private static final double GRIFF_ARROW_MM = 2;
    /**
     * Griff's near misses (the mockup's pick): a tap picks one's own unit this far beside it, less with troops selected, so that
     * a tap for a move beside one's own units does not take the selection from them.
     */
    private static final double GRIFF_NEAR_MM = 3.5;
    private static final double GRIFF_NEAR_TROOPS_MM = 2;
    /** Griff: the camera's glide stops under this speed a second (the mockup's 4 pixels of its 5.64 a mm). */
    private static final double GRIFF_GLIDE_STOP_MM = 0.7;
    /**
     * Griff's draft (the mockup's): a finger is on a plan within this of the box its ground takes (Draft.LEAST_MM wide at
     * least); an extractor takes the free deposit within this of the finger.
     */
    private static final double GRIFF_PLAN_REACH_MM = 1.5;
    private static final double GRIFF_DEPOSIT_MM = 5;
    /** The draft's tick and cross: their sides, and their room from FAF's construction panel, the column at the right and each other. */
    private static final double GRIFF_TICK_MM = 13;
    private static final double GRIFF_CROSS_MM = 11;
    private static final double GRIFF_TICK_ROOM_MM = 2;
    /** The loupe: the 6 mm about a resting finger, a copy of the game's picture at most this often. */
    private static final double LOUPE_SOURCE_MM = 6;
    private static final long LOUPE_MILLISECONDS = 33;
    /**
     * How long the undo's button's place stays covered after it went (Covers.Lingering), up to the program's check of a tap
     * there: a finger meant for the button that comes down up to 400 ms late, and the tap's click a settle after its down
     * (TouchInput.SETTLE_MILLISECONDS; on the emulator the program checked 88 to 188 ms after the down, 118 the median, and a
     * tap 398 ms late was checked 542 ms after the button went).
     */
    private static final long UNDO_COVER_AFTER_MS = 600;
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
    /**
     * What the mouse's bar has in a game and not in the menus, and the Keys, which it has in both. Griff's bar has its rail in a
     * game and the Keys in the menus only: in a game the keyboard is a button of FAF's Menu tab (FafreKeyboard).
     */
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
    private int mPictureWidth;
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
    /** A mouse wheel's notch on a page, in the screen's pixels. */
    private float mWheelPixels;
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
    /**
     * The touch scheme Griff (TouchScheme) for this game, its rail, and what it draws over the game. FAF's UI draws the rail's
     * Box and Plus and the undo's button (griffhud.lua); the views here take their fingers, where it reports them
     * (Game.griffHudPlaces): Box's and Plus's, then the undo's.
     */
    private boolean mGriff;
    private final Rail mRail = new Rail();
    private final List<View> mRailTargets = new ArrayList<>();
    private View mUndoTarget;
    private final float[] mHudValues = new float[Game.HUD_VALUES];
    private GriffLayer mGriffLayer;
    /**
     * The handle as Game.griff gives it, and as the screen shows it, while a game's frames are read: it stays where a stroke
     * began, and the program's answers for the stroke are its own.
     */
    private final float[] mGriffHandle = new float[Game.GRIFF_VALUES];
    private final GriffHandle mHandle = new GriffHandle();
    private boolean mGriffPolling;
    /** What the program says (Game.griffSaid). */
    private final int[] mGriffSaid = new int[3 * 16];
    /** Griff's draft as the program shows it (Game.griffDraft), and its tick and cross. */
    private final Draft mDraft = new Draft();
    private final float[] mDraftValues = new float[Draft.VALUES];
    private final Draft.Screen mDraftScreen = new Draft.Screen() {
        @Override
        public float x(float pictureX) {
            return mPageView.screenX(pictureX);
        }

        @Override
        public float y(float pictureY) {
            return mPageView.screenY(pictureY);
        }
    };
    private DraftButton mTick;
    private DraftButton mCross;
    private final Undo mUndo = new Undo();
    /** The app's pieces over the picture, which the program hears of when they change (Game.setGriffCovers). */
    private final Covers mCovers = new Covers();
    /** The undo's button's cover, which stays a short while after the button went: a late tap meant for it lands on nothing. */
    private final Covers.Lingering mUndoCover = new Covers.Lingering(UNDO_COVER_AFTER_MS);
    /** What the player opened of FAF's that shows (Game.griffOpened), which Griff's pieces over the picture step aside from. */
    private final Opened mOpened = new Opened();
    private final float[] mOpenedValues = new float[Opened.VALUES];
    private final int[] mPlaceOnWindow = new int[2];
    private final int[] mLayerOnWindow = new int[2];
    /**
     * The loupe, while a finger rests in the draft at this place: the copy of the game's picture on its way, two pictures (the
     * one shown and the next copy's), the screen's places each was copied about (the finger's then, kept on the picture), and
     * whether the report has heard that the copy does not work here.
     */
    private boolean mLoupeOn;
    private float mLoupeX;
    private float mLoupeY;
    private boolean mLoupeCopying;
    private final Bitmap[] mLoupePictures = new Bitmap[2];
    private int mLoupeNext;
    private Bitmap mLoupeShown;
    private float mLoupeShownX;
    private float mLoupeShownY;
    private float mLoupeCopyX;
    private float mLoupeCopyY;
    private boolean mLoupeFailureSaid;
    private final Runnable mLoupeCopy = this::copyLoupe;
    private final Choreographer.FrameCallback mGriffFrame = new Choreographer.FrameCallback() {
        @Override
        public void doFrame(long frameTimeNanos) {
            readGriff();
            Choreographer.getInstance().postFrameCallback(this);
        }
    };
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
            Game.mouseMove(fingerX(x), fingerY(y));
        }

        @Override
        public void button(int button, boolean down, float x, float y) {
            Game.touchButton(button, down, fingerX(x), fingerY(y));
        }

        @Override
        public void tap(float x, float y) {
            Game.tap(fingerX(x), fingerY(y), pictureMm(TAP_REACH_MM), pictureMm(TAP_SMALL_MM));
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
        public void tapNearGhost(float x, float y, float ghostX, float ghostY) {
            Game.tapNearGhost(fingerX(x), fingerY(y), fingerX(ghostX), fingerY(ghostY));
        }

        @Override
        public void startLine(float x, float y) {
            Game.mouseButton(Game.BUTTON_LEFT, true, fingerX(x), fingerY(y));
        }

        @Override
        public void cancelLine() {
            Game.cancelLine();
        }

        @Override
        public void wheel(int delta, float x, float y) {
            Game.mouseWheel(delta, fingerX(x), fingerY(y));
        }

        @Override
        public void held() {
            buzz();
        }

        @Override
        public boolean placing() {
            return Game.placingBuilding();
        }

        @Override
        public void deleteOrder(float x, float y) {
            Game.deleteOrder(fingerX(x), fingerY(y));
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

        @Override
        public void griffDown(float x, float y, boolean box) {
            Game.griffDown(fingerX(x), fingerY(y), box);
        }

        @Override
        public void smartTap(float x, float y, int rail, int step, float baseX, float baseY, int tap) {
            Game.smartTap(fingerX(x), fingerY(y), rail, step, fingerX(baseX), fingerY(baseY), tap);
        }

        @Override
        public void ring(boolean shown, float x, float y) {
            mGriffLayer.showRing(shown, x, y);
        }

        @Override
        public void deselect() {
            Game.deselect();
        }

        @Override
        public boolean onGriff(float x, float y) {
            // As the last frame placed it. With Box the handle steps aside, for a box that starts under it.
            readGriff();
            return mHandle.shown() && (rail() & TouchInput.RAIL_BOX) == 0
                && Math.hypot(x - mHandle.x(), y - mHandle.y()) <= (GRIFF_RADIUS_MM + GRIFF_TOUCH_MM) * viewPixelsPerMm();
        }

        @Override
        public void griffTap() {
            // The camera flies to the group, unless the user asked for no animations.
            Game.toGroup(ValueAnimator.areAnimatorsEnabled());
        }

        @Override
        public void route(float[] xs, float[] ys, int count, int rail, boolean patrol) {
            int[] places = new int[2 * count];
            for (int at = 0; at < count; ++at) {
                places[2 * at] = fingerX(xs[at]);
                places[2 * at + 1] = fingerY(ys[at]);
            }
            Game.route(places, count, rail, patrol);
        }

        @Override
        public void strokeShown(float[] xs, float[] ys, int count, int mode, float[] waypointXs, float[] waypointYs, int waypoints) {
            if (mHandle.stroke(count > 0)) {
                // A new stroke: the handle stays where it began, and only the program's answers for this one count.
                mGriffLayer.setStrokeEnd(0);
            }
            mGriffLayer.showStroke(xs, ys, count, mode, waypointXs, waypointYs, waypoints);
        }

        @Override
        public void strokeHover(float x, float y) {
            Game.strokeHover(fingerX(x), fingerY(y), mHandle.strokeNumber());
        }

        @Override
        public void strokeGivenUp() {
            Game.griffHint(Game.HINT_CANCELLED, 0);
        }

        @Override
        public int rail() {
            return mRail.counting(SystemClock.uptimeMillis());
        }

        @Override
        public void railUsed(int rail) {
            mRail.used(rail);
            showRail();
        }

        @Override
        public void griffDrag(float x, float y, float nowX, float nowY, boolean rested, int rail, int drag) {
            Game.griffDrag(fingerX(x), fingerY(y), fingerX(nowX), fingerY(nowY), rested, rail, drag);
        }

        @Override
        public void griffDragEnd(float x, float y, float velocityX, float velocityY) {
            float scale = mPageView.scale();
            Game.griffDragEnd(fingerX(x), fingerY(y), velocityX / scale, velocityY / scale);
        }

        @Override
        public void restTap(float x, float y, int rail) {
            Game.restTap(fingerX(x), fingerY(y), rail);
        }

        @Override
        public void griffRest(float x, float y) {
            Game.griffRest(fingerX(x), fingerY(y));
        }

        @Override
        public void griffRestGone() {
            Game.griffRestGone();
        }

        @Override
        public void pinch(boolean start, float x, float y, float distance) {
            Game.pinch(start, fingerX(x), fingerY(y), Math.round(distance / mPageView.scale()));
        }

        @Override
        public void pinchEnd(float velocityX, float velocityY) {
            float scale = mPageView.scale();
            Game.pinchEnd(velocityX / scale, velocityY / scale);
        }

        @Override
        public boolean drafting() {
            readDraft();
            return mDraft.open();
        }

        @Override
        public int draftPlan(float x, float y) {
            float pixels = viewPixelsPerMm();
            return mDraft.planAt(x, y, (float) (GRIFF_PLAN_REACH_MM * pixels), Draft.LEAST_MM / 2 * pixels);
        }

        @Override
        public boolean draftNewest(int plan) {
            return mDraft.newest(plan);
        }

        @Override
        public boolean draftOnDeposit(int plan) {
            return mDraft.onDeposit(plan);
        }

        @Override
        public boolean draftPicksDeposit() {
            return mDraft.picksDeposit();
        }

        @Override
        public void draftTap(float x, float y, int plan) {
            Game.draftTap(fingerX(x), fingerY(y), plan);
        }

        @Override
        public void draftLine(int phase, float x, float y) {
            Game.draftLine(phase, fingerX(x), fingerY(y));
        }

        @Override
        public void draftMove(int phase, float fromX, float fromY, float x, float y) {
            Game.draftMove(phase, fingerX(fromX), fingerY(fromY), fingerX(x), fingerY(y));
        }

        @Override
        public void draftAim(int phase, float x, float y) {
            Game.draftAim(phase, fingerX(x), fingerY(y));
            if (phase == TouchInput.DRAFT_SHOW) {
                showLoupe(x, y);
            } else {
                hideLoupe();
            }
        }

        @Override
        public void draftBox(int phase, float x0, float y0, float x1, float y1) {
            mGriffLayer.showDraftBox(phase == TouchInput.DRAFT_SHOW, x0, y0, x1, y1);
            if (phase == TouchInput.DRAFT_END) {
                Game.draftBox(fingerX(x0), fingerY(y0), fingerX(x1), fingerY(y1));
            }
        }

        @Override
        public void draftDrop() {
            Game.draftDrop();
        }

        @Override
        public boolean clicking() {
            return Game.griffClicking();
        }

        @Override
        public int clickRuns() {
            return Game.griffClickRuns();
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
            NetworkGameService.startIfNeeded(GameActivity.this);
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
        mGriff = TouchScheme.of(this) == TouchScheme.GRIFF;
        mTouch.setGriff(mGriff);
        mTouch.setMillimetre(viewPixelsPerMm());
        mPageInput = new PageInput(mPageMouse, ViewConfiguration.get(this).getScaledTouchSlop());
        mWheelPixels = ViewConfiguration.get(this).getScaledVerticalScrollFactor();
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
        if (mGriff) {
            mGriffLayer = new GriffLayer(this);
            mGriffLayer.setSizes(viewPixelsPerMm(), GRIFF_RADIUS_MM, GRIFF_ARROW_MM);
            root.addView(mGriffLayer);
            // Over the layer that takes the fingers: a finger on Box, Plus, the undo's button, the tick or the cross is theirs.
            railTarget(root, TouchInput.RAIL_BOX, "Box");
            railTarget(root, TouchInput.RAIL_QUEUE, "Plus");
            mUndoTarget = new View(this);
            mUndoTarget.setVisibility(View.GONE);
            mUndoTarget.setContentDescription("Undo");
            mUndoTarget.setOnClickListener(view -> undo());
            root.addView(mUndoTarget, new FrameLayout.LayoutParams(0, 0, Gravity.TOP | Gravity.START));
            mTick = draftButton(true);
            mCross = draftButton(false);
            root.addView(mTick, new FrameLayout.LayoutParams(0, 0, Gravity.TOP | Gravity.START));
            root.addView(mCross, new FrameLayout.LayoutParams(0, 0, Gravity.TOP | Gravity.START));
        }
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
        if (mGriff) {
            mBar.addOnLayoutChangeListener((view, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) -> setGriffCovers());
        }
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

    /**
     * A finger's place in the picture, with Griff never on its edge: FAF scrolls the view while its cursor is on the window's
     * edge pixel (ui_ScreenEdgeScrollView), where a mouse is pushed on purpose and a finger at the glass's edge would hold it.
     */
    private int fingerX(float x) {
        int place = pictureX(x);
        return mGriff ? Math.max(1, Math.min(mPictureWidth - 2, place)) : place;
    }

    private int fingerY(float y) {
        int place = pictureY(y);
        return mGriff ? Math.max(1, Math.min(mPictureHeight - 2, place)) : place;
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
        if (mGriff) {
            readGriffSaid();
        }
        // The Keyboard of FAF's Menu tab, as the bar's Keys.
        if (Game.keyboardAsked()) {
            toggleKeyboard();
        }
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
        if (mGriff) {
            // The keyboard's button for the menus: Griff's rail is FAF's UI's in a game (griffhud.lua).
            bar.setOrientation(LinearLayout.HORIZONTAL);
            mKeysButton = barButton(bar, "Keys");
            mKeysButton.setOnClickListener(view -> toggleKeyboard());
            return bar;
        }
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

    /**
     * In a game the bar sits where the HUD leaves room (HudScale), in the menus only the keyboard's button stays, in a corner.
     * Griff's rail is the game's alone, in FAF's UI, and its Keys the menus': in a game the keyboard is a button of FAF's Menu
     * tab.
     */
    private void placeBar(boolean inGame) {
        mBarInGame = inGame;
        FrameLayout.LayoutParams place = (FrameLayout.LayoutParams) mBar.getLayoutParams();
        if (!mGriff || !inGame) {
            int size = viewMm(inGame ? gameButtonMm() : BAR_MENU_BUTTON_MM);
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
            place.leftMargin = 0;
            place.topMargin = inGame ? (int) Math.round(HudScale.barTopPixels(hudFactor()) * viewPixelsPerPicturePixel()) : 0;
        }
        mBar.setLayoutParams(place);
        if (mGriff) {
            mKeysButton.setVisibility(inGame ? View.GONE : View.VISIBLE);
            if (!inGame) {
                releaseRail();
            }
            pollGriff(inGame);
            placeHud();
            placeDraftButtons();
        }
    }

    /** In a game the mouse's bar's two rows fill the room the HUD leaves, up to a finger's size. */
    private double gameButtonMm() {
        return Math.min(BAR_BUTTON_MM, barRoomMm() / 2 - BAR_GAP_MM);
    }

    /** Whether something the player opened of FAF's lies under a piece at this place of the screen, of this size (Opened). */
    private boolean openedUnder(float left, float top, float width, float height) {
        return mOpened.under(pictureX(left), pictureY(top), pictureX(left + width), pictureY(top + height));
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

    /** The view's pixels in a mm on the glass. */
    private float viewPixelsPerMm() {
        DisplayMetrics metrics = realMetrics();
        return (float) (Math.min(metrics.widthPixels, metrics.heightPixels) / mScreenMm);
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

    /**
     * Where a finger takes one of Griff's rail buttons (Rail), which FAF's UI draws (griffhud.lua) where placeHud puts this view:
     * a tap lights it for the next action, a hold counts while held; Plus held is Shift held.
     */
    private void railTarget(FrameLayout root, int bit, String label) {
        View target = new View(this);
        target.setVisibility(View.GONE);
        target.setContentDescription(label);
        // Griff's slop, as for its gestures on the map: a thumb that wobbles on the rail still taps.
        target.setOnTouchListener(new RailTouch(bit, TouchInput.GRIFF_SLOP_MM * viewPixelsPerMm()));
        root.addView(target, new FrameLayout.LayoutParams(0, 0, Gravity.TOP | Gravity.START));
        mRailTargets.add(target);
    }

    /** A finger on a rail button. It is the button's own: a finger on the map is another view's, and both go on at once. */
    private final class RailTouch implements View.OnTouchListener {
        private final int mBit;
        private final float mSlop;
        private float mDownX;
        private float mDownY;
        private boolean mMoved;

        RailTouch(int bit, float slop) {
            mBit = bit;
            mSlop = slop;
        }

        @Override
        public boolean onTouch(View view, MotionEvent event) {
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    mDownX = event.getX();
                    mDownY = event.getY();
                    mMoved = false;
                    // While the program's clicks run the finger rests, as on the map: a held Plus would be Shift between them.
                    if (!mRail.held(mBit) && !Game.griffClicking()) {
                        mRail.press(mBit, event.getEventTime());
                        if (mBit == TouchInput.RAIL_QUEUE) {
                            Game.holdShift(true);
                        }
                    }
                    break;
                case MotionEvent.ACTION_MOVE:
                    mMoved |= Math.hypot(event.getX() - mDownX, event.getY() - mDownY) > mSlop;
                    break;
                case MotionEvent.ACTION_UP:
                    letGo(event.getEventTime(), !mMoved && event.getX() >= 0 && event.getY() >= 0
                        && event.getX() < view.getWidth() && event.getY() < view.getHeight(), false);
                    break;
                case MotionEvent.ACTION_CANCEL:
                    letGo(event.getEventTime(), false, true);
                    break;
                default:
                    break;
            }
            showRail();
            return true;
        }

        private void letGo(long time, boolean stayed, boolean cancelled) {
            if (!mRail.held(mBit)) {
                return;
            }
            if (cancelled) {
                mRail.cancel(mBit);
            } else {
                mRail.release(mBit, time, stayed);
            }
            if (mBit == TouchInput.RAIL_QUEUE) {
                Game.holdShift(false);
            }
        }
    }

    /**
     * The rail as it is: the handle's look by the buttons held or lit, and how each button looks (Rail#look) for FAF's UI, which
     * draws them (placeHud).
     */
    private void showRail() {
        if (mGriffLayer != null) {
            mGriffLayer.setRail(mRail.shown());
            placeHud();
        }
    }

    /** The rail put away, and with Plus the Shift it holds: nothing is held where no finger can let go of it. */
    private void releaseRail() {
        if (mRail.held(TouchInput.RAIL_QUEUE)) {
            Game.holdShift(false);
        }
        mRail.clear();
        showRail();
    }

    /**
     * The handle as the last frame placed it, where the screen shows it, while a game's frames are read; while a stroke is drawn
     * it stays where the stroke began, and with the frames it goes (GriffHandle). With it the free map, and what the stroke's
     * end is on.
     */
    private void readGriff() {
        boolean shown = mGriffPolling && Game.griff(mGriffHandle);
        readOpened();
        readDraft();
        placeHud();
        setGriffCovers();
        // While the draft is open there is no handle: no stroke starts (the mockup's).
        shown = shown && !mDraft.open();
        if (!mHandle.place(mGriffPolling, shown, mPageView.screenX(mGriffHandle[0]), mPageView.screenY(mGriffHandle[1]))) {
            if (mGriffHandle[Game.GRIFF_STROKE] == mHandle.strokeNumber()) {
                mGriffLayer.setStrokeEnd((int) mGriffHandle[Game.GRIFF_STROKE_END]);
            }
            return;
        }
        mGriffLayer.setHandle(mHandle.shown(), mHandle.x(), mHandle.y(), mPageView.screenX(mGriffHandle[2]),
            mPageView.screenY(mGriffHandle[3]), (int) mGriffHandle[4], mGriffHandle[5] != 0);
        mGriffLayer.setFreeMap(mPageView.screenX(mGriffHandle[Game.GRIFF_FREE_LEFT]), mPageView.screenY(mGriffHandle[Game.GRIFF_FREE_TOP]),
            mPageView.screenX(mGriffHandle[Game.GRIFF_FREE_RIGHT]));
    }

    /**
     * What the player opened of FAF's that shows now (Opened): where it changed, the draft's tick and cross step aside from it or
     * come back. Box, Plus and the undo's button do in FAF's UI, which says so with their places (placeHud).
     */
    private void readOpened() {
        if (mOpened.update(mOpenedValues, mGriffPolling ? Game.griffOpened(mOpenedValues) : 0)) {
            placeDraftButtons();
        }
    }

    /** The draft as the program shows it now; the layer, the tick and the cross follow where it changed. */
    private void readDraft() {
        int count = mGriffPolling ? Game.griffDraft(mDraftValues) : 0;
        if (!mDraft.update(mDraftValues, count, mDraftScreen)) {
            return;
        }
        mGriffLayer.setDraft(mDraft);
        placeDraftButtons();
        if (!mDraft.open()) {
            hideLoupe();
        }
    }

    private DraftButton draftButton(boolean tick) {
        DraftButton button = new DraftButton(this, tick);
        button.setMillimetre(viewPixelsPerMm());
        button.setVisibility(View.GONE);
        if (tick) {
            // With Plus counting, held or lit, every plan is appended to what the builders do; the program says what it used.
            button.setOnClickListener(view -> Game.draftCommit((mRail.counting(SystemClock.uptimeMillis()) & TouchInput.RAIL_QUEUE) != 0));
        } else {
            button.setOnClickListener(view -> Game.draftDrop());
        }
        return button;
    }

    /**
     * The tick and the cross while the draft is open in a game (the mockup's): on the map, their bottoms 2 mm above FAF's
     * construction panel, the tick's right 2 mm left of the column at the right (FAF's avatars), the cross 2 mm left of it;
     * against the screen's edges where FAF shows neither. Each steps aside while something the player opened of FAF's lies
     * under it (Opened), such as a window the player moved there.
     */
    private void placeDraftButtons() {
        if (mTick == null) {
            return;
        }
        if (!mDraft.open() || !mBarInGame) {
            mTick.setVisibility(View.GONE);
            mCross.setVisibility(View.GONE);
            return;
        }
        float pixels = viewPixelsPerMm();
        float room = (float) (GRIFF_TICK_ROOM_MM * pixels);
        float width = mFingerLayer.getWidth();
        float height = mFingerLayer.getHeight();
        float bottom = (mDraft.constructionTop() >= 0 ? Math.min(mDraft.constructionTop(), height) : height) - room;
        float right = (mDraft.columnLeft() >= 0 ? Math.min(mDraft.columnLeft(), width) : width) - room;
        int tick = viewMm(GRIFF_TICK_MM);
        int cross = viewMm(GRIFF_CROSS_MM);
        float crossLeft = right - tick - room - cross;
        placeView(mTick, right - tick, bottom - tick, tick);
        placeView(mCross, crossLeft, bottom - cross, cross);
        mTick.setVisibility(Opened.visibility(true, openedUnder(right - tick, bottom - tick, tick, tick)));
        mCross.setVisibility(Opened.visibility(true, openedUnder(crossLeft, bottom - cross, cross, cross)));
    }

    private static void placeView(View view, float left, float top, int side) {
        placeView(view, left, top, side, side);
    }

    private static void placeView(View view, float left, float top, int width, int height) {
        FrameLayout.LayoutParams place = (FrameLayout.LayoutParams) view.getLayoutParams();
        int x = Math.round(left);
        int y = Math.round(top);
        if (place.width == width && place.height == height && place.leftMargin == x && place.topMargin == y) {
            return;
        }
        place.width = width;
        place.height = height;
        place.leftMargin = x;
        place.topMargin = y;
        view.setLayoutParams(place);
    }

    /**
     * Where FAF's UI drew Box, Plus and the undo's button (griffhud.lua, Game.griffHudPlaces): the views that take their fingers
     * lie there, each while it is wanted in a game and nothing the player opened of FAF's lies over it, which FAF's UI says with
     * the place; the undo's while its time runs or its undo waits (Undo). And how they look, for FAF's UI to draw them
     * (Game.setGriffHud).
     */
    private void placeHud() {
        if (mUndoTarget == null) {
            return;
        }
        long now = SystemClock.uptimeMillis();
        int count = mGriffPolling ? Game.griffHudPlaces(mHudValues) : 0;
        for (int at = 0; at < mRailTargets.size(); ++at) {
            placeTarget(mRailTargets.get(at), at, count, mBarInGame);
        }
        boolean undoWanted = mBarInGame && mUndo.showing(now);
        placeTarget(mUndoTarget, mRailTargets.size(), count, undoWanted);
        int undo = !undoWanted ? Game.HUD_UNDO_NONE : mUndo.waiting() ? Game.HUD_UNDO_WAITING : Game.HUD_UNDO_OFFERED;
        Game.setGriffHud(mRail.look(TouchInput.RAIL_BOX) | mRail.look(TouchInput.RAIL_QUEUE) << 2, undo, mUndo.left(now));
    }

    /** The view of FAF's UI's piece number `piece` (Game.HUD_PIECE numbers each, of `count`): at its place, seen where wanted. */
    private void placeTarget(View target, int piece, int count, boolean wanted) {
        int at = piece * Game.HUD_PIECE;
        boolean known = count >= at + Game.HUD_PIECE && mHudValues[at + 2] > mHudValues[at] && mHudValues[at + 3] > mHudValues[at + 1];
        if (known) {
            float left = mPageView.screenX(mHudValues[at]);
            float top = mPageView.screenY(mHudValues[at + 1]);
            placeView(target, left, top, Math.round(mPageView.screenX(mHudValues[at + 2]) - left),
                Math.round(mPageView.screenY(mHudValues[at + 3]) - top));
        }
        target.setVisibility(Opened.visibility(wanted && known, mHudValues[at + 4] != 0));
    }

    /**
     * The undo's button was tapped: the program takes its record back, and the next tap climbs no ladder from an order undone.
     * Not while the program's clicks run: the finger rests, as on the map.
     */
    private void undo() {
        if (Game.griffClicking()) {
            return;
        }
        int record = mUndo.tap(SystemClock.uptimeMillis());
        if (record != 0) {
            Game.griffUndo(record);
            mTouch.clearLadder();
        }
    }

    /**
     * The app's pieces over the picture for the program (Covers): the draft's tick and cross, each while it shows, and the undo's
     * button while it shows and a moment after, in the picture's pixels; the program hears them when they changed. FAF's UI holds
     * the rail and the undo's button themselves, which the program finds there. The Keys shows in the menus only, where no
     * finger is Griff's. The undo's cover that stays after it went goes at once where something the player opened of FAF's lies,
     * as the pieces step aside from it.
     */
    private void setGriffCovers() {
        mFingerLayer.getLocationInWindow(mLayerOnWindow);
        mCovers.begin();
        cover(mTick, null);
        cover(mCross, null);
        int[] undoWas = mUndoCover.box();
        if (mOpened.under(undoWas[0], undoWas[1], undoWas[2], undoWas[3])) {
            mUndoCover.forget();
        }
        cover(mUndoTarget, mUndoCover);
        if (mCovers.end()) {
            Game.setGriffCovers(mCovers.boxes(), mCovers.count());
        }
    }

    /** A piece's cover while it shows, and with `lingering` a while after, where it last showed (Covers.Lingering). */
    private void cover(View view, Covers.Lingering lingering) {
        view.getLocationInWindow(mPlaceOnWindow);
        float left = mPlaceOnWindow[0] - mLayerOnWindow[0];
        float top = mPlaceOnWindow[1] - mLayerOnWindow[1];
        int pictureLeft = pictureX(left);
        int pictureTop = pictureY(top);
        int pictureRight = pictureX(left + view.getWidth());
        int pictureBottom = pictureY(top + view.getHeight());
        if (lingering != null) {
            mCovers.piece(lingering, view.isShown(), pictureLeft, pictureTop, pictureRight, pictureBottom, SystemClock.uptimeMillis());
        } else {
            mCovers.piece(view.isShown(), pictureLeft, pictureTop, pictureRight, pictureBottom);
        }
    }

    /**
     * The loupe follows a resting finger in the draft: copies of the game's picture about it, which the layer magnifies, each
     * where it was copied about.
     */
    private void showLoupe(float x, float y) {
        mLoupeX = x;
        mLoupeY = y;
        if (mLoupeShown != null) {
            mGriffLayer.showLoupe(mLoupeShown, mLoupeShownX, mLoupeShownY, x, y);
        }
        if (!mLoupeOn) {
            mLoupeOn = true;
            copyLoupe();
        }
    }

    private void hideLoupe() {
        mLoupeOn = false;
        mLoupeShown = null;
        mHandler.removeCallbacks(mLoupeCopy);
        if (mGriffLayer != null) {
            mGriffLayer.showLoupe(null, 0, 0, 0, 0);
        }
    }

    /**
     * The 6 mm of the game's picture about the finger, kept on the picture (Draft.loupeFrom), by PixelCopy of the surface, one
     * copy at a time and at most each LOUPE_MILLISECONDS. Where a copy fails, or comes back blank, as the emulator's of the
     * engine's Vulkan surface does, no loupe is shown, and the plan under the finger alone shows where it goes; the game's
     * report hears it once.
     */
    private void copyLoupe() {
        if (!mLoupeOn || mLoupeCopying || mPictureWidth <= 0 || mPictureHeight <= 0) {
            return;
        }
        int side = Math.max(2, Math.min(pictureMm(LOUPE_SOURCE_MM), Math.min(mPictureWidth, mPictureHeight)));
        int left = Draft.loupeFrom(pictureX(mLoupeX), side, mPictureWidth);
        int top = Draft.loupeFrom(pictureY(mLoupeY), side, mPictureHeight);
        // Where it is copied about, on the screen: the finger may have moved on by the time it shows.
        mLoupeCopyX = mPageView.screenX(left + side / 2f);
        mLoupeCopyY = mPageView.screenY(top + side / 2f);
        Bitmap picture = mLoupePictures[mLoupeNext];
        if (picture == null || picture.getWidth() != side) {
            picture = Bitmap.createBitmap(side, side, Bitmap.Config.ARGB_8888);
            mLoupePictures[mLoupeNext] = picture;
        }
        final Bitmap copy = picture;
        mLoupeCopying = true;
        try {
            PixelCopy.request(mSurface, new Rect(left, top, left + side, top + side), copy, result -> {
                mLoupeCopying = false;
                if (!mLoupeOn) {
                    return;
                }
                if (result == PixelCopy.SUCCESS && !blank(copy)) {
                    // The game's surface has no alpha that means anything.
                    copy.setHasAlpha(false);
                    mLoupeShown = copy;
                    mLoupeShownX = mLoupeCopyX;
                    mLoupeShownY = mLoupeCopyY;
                    mLoupeNext = 1 - mLoupeNext;
                } else {
                    mLoupeShown = null;
                    loupeFailed(result == PixelCopy.SUCCESS ? "came back blank" : "failed, " + result);
                }
                mGriffLayer.showLoupe(mLoupeShown, mLoupeShownX, mLoupeShownY, mLoupeX, mLoupeY);
                mHandler.postDelayed(mLoupeCopy, LOUPE_MILLISECONDS);
            }, mHandler);
        } catch (IllegalArgumentException e) {
            // The surface is gone.
            mLoupeCopying = false;
            loupeFailed(e.getMessage());
        }
    }

    /** Whether a copy holds no picture: every pixel of a grid over it black. */
    private static boolean blank(Bitmap picture) {
        for (int y = 0; y < picture.getHeight(); y += 4) {
            for (int x = 0; x < picture.getWidth(); x += 4) {
                if ((picture.getPixel(x, y) & 0xFFFFFF) != 0) {
                    return false;
                }
            }
        }
        return true;
    }

    private void loupeFailed(String why) {
        if (!mLoupeFailureSaid) {
            mLoupeFailureSaid = true;
            GameReport.append(this, "griff: no loupe, the copy of the game's picture " + why);
        }
    }

    /**
     * What the program said of Griff's taps, drags, orders and draft: the rail's buttons a Box tap or a tick used, the tap ladder's
     * answers, a drag it made a pan of, a group stored, an undo's record and its end. Its words FAF's UI shows itself.
     */
    private void readGriffSaid() {
        int count;
        while ((count = Game.griffSaid(mGriffSaid)) > 0) {
            for (int at = 0; at + 2 < count; at += 3) {
                if (mGriffSaid[at] == Game.SAID_TAP) {
                    int answer = mGriffSaid[at + 2];
                    mTouch.tapAnswered(mGriffSaid[at + 1], answer & 0xFF);
                    if ((answer >> 8) != 0) {
                        mRail.used(answer >> 8);
                        showRail();
                    }
                } else if (mGriffSaid[at] == Game.SAID_PAN) {
                    mTouch.panned(mGriffSaid[at + 1]);
                } else if (mGriffSaid[at] == Game.SAID_GROUP_STORED) {
                    // A group stored buzzes once, as the mockup's: as the ring of the rest that stored it came, or now (TouchInput).
                    if (mTouch.storeBuzzes()) {
                        buzz();
                    }
                } else if (mGriffSaid[at] == Game.SAID_COMMITTED) {
                    // The tick's last click left the pointer on the map, which a finger's lift would have taken away.
                    mTouchMouse.leaveSoon();
                    if (mGriffSaid[at + 1] != 0) {
                        mRail.used(mGriffSaid[at + 1]);
                        showRail();
                    }
                } else if (mGriffSaid[at] == Game.SAID_ORDERED) {
                    mUndo.ordered(mGriffSaid[at + 1], SystemClock.uptimeMillis());
                } else if (mGriffSaid[at] == Game.SAID_UNDONE) {
                    // The undo's last click left the pointer on the map, as the tick's; a word that changed after an undo
                    // over earlier comes without a click. What it did FAF's UI says (Game.HINT_).
                    if (mUndo.undone(mGriffSaid[at + 1])) {
                        mTouchMouse.leaveSoon();
                    }
                }
            }
            if (count < mGriffSaid.length) {
                break;
            }
        }
    }

    /**
     * While a game is on and the activity in front, the handle follows the selection each frame of the screen's, as do the views
     * that take the fingers of Box, Plus and the undo's button. Without the frames the undo goes with its record: a dialog over
     * the game, or the game's end.
     */
    private void pollGriff(boolean on) {
        if (on == mGriffPolling) {
            return;
        }
        mGriffPolling = on;
        if (on) {
            Choreographer.getInstance().postFrameCallback(mGriffFrame);
        } else {
            Choreographer.getInstance().removeFrameCallback(mGriffFrame);
            mUndo.clear();
            readGriff();
        }
    }

    /**
     * Griff's sizes in the picture's pixels (a game's page has no zoom), by which the program places the handle, helps taps,
     * picks units beside them, stops the camera's glide and finds a deposit for the draft's extractor.
     */
    private void setGriff() {
        double pixels = mPictureHeight / mScreenMm;
        Game.setGriff(mGriff, (float) (GRIFF_RADIUS_MM * pixels), (float) ((GRIFF_RADIUS_MM + GRIFF_TOUCH_MM) * pixels),
            (float) (GRIFF_GAP_MM * pixels), (float) ((GRIFF_RADIUS_MM + GRIFF_ARROW_MM) * pixels), (float) (TAP_REACH_MM * pixels),
            (float) (TAP_SMALL_MM * pixels), (float) (GRIFF_NEAR_MM * pixels), (float) (GRIFF_NEAR_TROOPS_MM * pixels),
            (float) (GRIFF_GLIDE_STOP_MM * pixels), (float) (GRIFF_DEPOSIT_MM * pixels));
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

    /** What a finger feels: a hold, a rest, a control group stored where its rest gave no buzz (TouchInput.storeBuzzes). */
    private void buzz() {
        mSurface.performHapticFeedback(HapticFeedbackConstants.LONG_PRESS);
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
            return "FAIL: no whole FAF game files yet; \"Get FAF's newest version\" on card 5 downloads them, as does the replay test (card 3)";
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
        // /notelemetry: the engine would send its records of desyncs and of a game's end to its author's server, with the
        // players' names, and nobody here has agreed to that. An import by an earlier release has no movies, and the splash
        // screen would wait for a tap for each.
        String[] arguments = ScfaImport.movies(storage) > 0
            ? new String[] {program, "/init", init.getPath(), "/log", log, "/notelemetry", HudScale.SWITCH, hudScale, HudScale.MOST_SWITCH, hudMost}
            : new String[] {program, "/init", init.getPath(), "/log", log, "/notelemetry", HudScale.SWITCH, hudScale, HudScale.MOST_SWITCH, hudMost, "/nomovie"};
        GameReport.append(this, "game: FAF game version " + version + ", " + archives + " SCFA archives");
        GameReport.append(this, "game: HUD scale " + hudScale + " on " + screenInWords());
        File home = Storage.mkdirs(GameReport.home(storage));
        if (!Game.start(arguments, home.getPath(), getCacheDir().getPath(), GameReport.file(this).getPath())) {
            return "FAIL: the program did not start";
        }
        mStarted = true;
        return null;
    }

    /** The newest FAF game version whose files are ready, or -1: the last that the Game card lists too. */
    private static int newestGameFiles(Storage storage) {
        List<Integer> versions = GameFiles.readyVersions(storage);
        return versions.isEmpty() ? -1 : versions.get(versions.size() - 1);
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
        NetworkGameService.front(true);
        hideSystemBars();
        if (mDiscoveryLock != null) {
            mDiscoveryLock.acquire();
        }
        if (mStarted) {
            // First: the output starts at the word that both are so, and it must not during a call.
            requestAudioFocus();
            Game.foreground(true);
            if (mGriff && mBarInGame) {
                pollGriff(true);
            }
        }
    }

    @Override
    protected void onPause() {
        // Still allowed now: a socket opened since the last poll has its service too.
        NetworkGameService.startIfNeeded(this);
        NetworkGameService.front(false);
        if (mStarted) {
            mFingers.cancel();
            if (mGriff) {
                releaseRail();
                pollGriff(false);
            }
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
        mHandler.removeCallbacks(mLoupeCopy);
        Choreographer.getInstance().removeFrameCallback(mGriffFrame);
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
        mPictureWidth = width;
        mPictureHeight = height;
        mPageView.setPicture(width, height);
        placeSurface();
        if (mStarted) {
            Game.surface(holder.getSurface(), width, height);
            setGriff();
            if (mGriff) {
                setGriffCovers();
            }
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
                mFingers.moved(event.getEventTime());
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
            float turned = event.getAxisValue(MotionEvent.AXIS_VSCROLL);
            if (mPageView.zoomed()) {
                if (turned != 0) {
                    mPageInput.wheel(event.getX(), event.getY(), turned * mWheelPixels, event.getEventTime());
                }
            } else {
                int delta = Math.round(turned * Game.WHEEL_DELTA);
                if (delta != 0) {
                    Game.mouseWheel(delta, pictureX(event.getX()), pictureY(event.getY()));
                }
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
