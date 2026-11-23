// The fingers on SDL's events (FingersSdl.h): the app's TouchLayer.java in C++, without its views. SDL gives a finger's place
// over the window (GameSurface.java scales it to the picture), so the layer works in the picture's pixels; Griff measures its
// gestures in millimetres of the glass, from the screen's short side the activity measured (FAF_SCREEN_MM). Where a page is
// zoomed (TouchLayer's page view) the screen and the picture would differ: the page's zoom waits for its design
// (buildstage/griff-cpp-views-design.md), and until then a page fills the screen as the picture does (touch::PageView, which
// maps between them, without a page).
//
// A finger on one of the pieces of Griff's that FAF's UI draws (griffhud.lua: Box, Plus, the undo's button, the draft's tick
// and cross) is the piece's, never the map's: the views that took them on the activity are the hit tests here. The program's
// words for the layer (griffinput::TakeSaid) and the menus' (menufingers) come in a task of their own after the task that
// had them, and the handle and the draft are read after each frame of the session's UI (GriffAndroid.cpp PlaceTask). The
// layer's timers are SDL's, whose callbacks put an event of the layer's in SDL's queue: they come on the program's thread, in
// order with the fingers.

#include "FingersSdl.h"

#include <SDL3/SDL.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "../app/GameAndroid.h"
#include "../app/ScreenKeyboardAndroid.h"
#include "FingerInput.h"
#include "GriffInput.h"
#include "GriffShown.h"
#include "HostSdl.h"
#include "MenuFingers.h"
#include "gpg/core/utils/Logging.h"
#include "moho/lua/CScrLuaBinder.h"
#include "moho/misc/StartupHelpers.h"
#include "touch/Covers.h"
#include "touch/Draft.h"
#include "touch/GriffHandle.h"
#include "touch/Numbers.h"
#include "touch/Opened.h"
#include "touch/PageInput.h"
#include "touch/PageView.h"
#include "touch/Rail.h"
#include "touch/Stroke.h"
#include "touch/TouchInput.h"
#include "touch/Undo.h"

namespace moho
{
  [[nodiscard]] bool UI_GamePlaying() noexcept;
  // The patched CD3DPrimBatcher.cpp's (patches/README.md): the UI's view, x * scaleX + offsetX in the window's pixels.
  void UI_SetView(float scaleX, float scaleY, float offsetX, float offsetY) noexcept;
} // namespace moho

namespace
{
  using touch::Millis;
  using touch::TouchInput;

  /// How far a finger's tap looks for a small control, and what counts as one.
  constexpr float kTapReachMm = 2.5f;
  constexpr float kTapSmallMm = 5;
  /// How far a finger may wander and still stay, for the mouse scheme and the menus: Android's touch slop, 8 of its
  /// density-independent pixels, which are a 160th of an inch.
  constexpr float kSlopMm = 25.4f * 8 / 160;
  /// Griff's handle, which FAF's UI draws (griffmap.lua): as large as FAF's order buttons, 48 of FAF's pixels at the HUD's
  /// factor, its arrow at a group elsewhere FAF's ping arrow, its middle on the handle's edge; how much further from its edge
  /// a finger is still on it, and its room from the selection's units' blueprint box, which is wider than their icons.
  constexpr double kHandleSide = 48;
  constexpr double kArrowSide = 52;
  constexpr double kHandleTouchMm = 1.5;
  constexpr double kHandleGapMm = 2;
  /// The label of what a stroke's lift gives (FAF's tooltip, griffmap.lua): this far above the finger, kept this far into
  /// the free map and below its top.
  constexpr float kLabelAboveMm = 14;
  constexpr float kLabelInMm = 7;
  constexpr float kLabelBelowTopMm = 2.5f;
  /// Griff's near misses (the mockup's pick): a tap picks one's own unit this far beside it, less with troops selected, so
  /// that a tap for a move beside one's own units does not take the selection from them.
  constexpr double kNearMm = 3.5;
  constexpr double kNearTroopsMm = 2;
  /// The camera's glide stops under this speed a second (the mockup's 4 pixels of its 5.64 a mm).
  constexpr double kGlideStopMm = 0.7;
  /// The draft (the mockup's): an extractor takes the free deposit within this of the finger.
  constexpr double kDepositMm = 5;
  /// How long the undo's button's place stays covered after it went (Covers::Lingering), up to the program's check of a tap
  /// there: a finger meant for the button that comes down up to 400 ms late, and the tap's click a settle after its down.
  constexpr Millis kUndoCoverAfter = 600;
  /// The pieces FAF's UI draws (griffinput::HudPlaces), in its order.
  constexpr int kBoxPiece = 0;
  constexpr int kPlusPiece = 1;
  constexpr int kUndoPiece = 2;
  constexpr int kTickPiece = 3;
  constexpr int kCrossPiece = 4;
  /// A stroke's numbers for FAF's UI (griffshown::kStroke): what lifting it gives, whether FAF's attack-move is armed, the
  /// label's place, how many places follow; then its start and its waypoints.
  constexpr int kStrokeHead = 5;
  /// A mouse wheel's notch on a page, as a finger's drag: Android's scroll factor, 64 of its density-independent pixels.
  constexpr float kWheelNotchMm = 25.4f * 64 / 160;
  /// How much of the screen the keyboard covers where nothing says (Android before 11): in landscape about half.
  constexpr float kKeyboardShareUnsaid = 0.5f;

  /// How much of the screen's height the phone's keyboard covers, as the activity's window insets say (KeyboardCovers), 0
  /// while it is not there; -1 where nothing says (Android before 11).
  std::atomic<float> gKeyboardShare{-1};

  /// The activity's marks of its touch events (fingers::MoveDone, fingers::Palm), in SDL's queue among SDL's events of the same
  /// touch event: their type, 0 until the program's thread has registered it, and whether moves are marked at all.
  std::atomic<Uint32> gMarkType{0};
  std::atomic<bool> gMovesMarked{false};
  constexpr Sint32 kMarkMoved = 0;
  constexpr Sint32 kMarkPalm = 1;

  [[nodiscard]] Millis Now()
  {
    return static_cast<Millis>(SDL_GetTicks());
  }

  /// An event's time on the layer's clock, SDL's.
  [[nodiscard]] Millis At(const Uint64 timestamp)
  {
    return static_cast<Millis>(timestamp / SDL_NS_PER_MS);
  }

  /// The environment's word, or empty.
  [[nodiscard]] std::string Environment(const char* const name)
  {
    const char* const value = std::getenv(name);
    return value != nullptr ? value : "";
  }

  // --- The layer's timers ---------------------------------------------------------------------------------------------------

  enum TimerName : int
  {
    kTouchTimer,
    kPageTimer,
    kLeaveTimer,
    /// Every half second: a game that began or ended since, which no word of the program's says (TouchLayer's poll).
    kPollTimer,
    kTimers,
  };

  constexpr Millis kPollMilliseconds = 500;

  /// SDL's timers, which say their time by an event of the layer's in SDL's queue (`user.code` the timer, `user.data1` its
  /// round): a round stopped or started anew since counts for nothing when its event comes. The callback's pointer carries
  /// both, so nothing is left to free whether a timer fires or is taken back.
  class Timers
  {
  public:
    void Start(const int name, const Millis milliseconds)
    {
      Stop(name);
      Timer& timer = mTimers[static_cast<std::size_t>(name)];
      timer.round = NextRound();
      const Uint32 delay = static_cast<Uint32>(std::clamp<Millis>(milliseconds, 1, 60000));
      const std::uintptr_t said = static_cast<std::uintptr_t>(name) | static_cast<std::uintptr_t>(timer.round) << 8;
      timer.id = SDL_AddTimer(delay, &Fire, reinterpret_cast<void*>(said));
      if (timer.id == 0) {
        gpg::Warnf("Fingers: no timer: %s", SDL_GetError());
      }
    }

    void Stop(const int name)
    {
      Timer& timer = mTimers[static_cast<std::size_t>(name)];
      timer.round = NextRound();
      if (timer.id != 0) {
        // Whether or not it fired already, its round is over.
        (void)SDL_RemoveTimer(timer.id);
        timer.id = 0;
      }
    }

    [[nodiscard]] bool Running(const int name) const
    {
      return mTimers[static_cast<std::size_t>(name)].id != 0;
    }

    /// The timer whose time came with `event`, or -1: none, or a round that is over.
    [[nodiscard]] int Came(const SDL_Event& event)
    {
      const auto said = reinterpret_cast<std::uintptr_t>(event.user.data1);
      const int name = static_cast<int>(said & 0xFF);
      if (name >= kTimers) {
        return -1;
      }
      Timer& timer = mTimers[static_cast<std::size_t>(name)];
      if (static_cast<std::uint32_t>(said >> 8) != timer.round) {
        return -1;
      }
      timer.id = 0;
      return name;
    }

    [[nodiscard]] static Uint32 EventType()
    {
      static const Uint32 type = SDL_RegisterEvents(1);
      return type;
    }

  private:
    struct Timer
    {
      SDL_TimerID id = 0;
      std::uint32_t round = 0;
    };

    /// A round no timer of any layer had before (24 bits, beside the timer's 8): an event of a layer's that went comes to
    /// nothing in the next.
    [[nodiscard]] static std::uint32_t NextRound()
    {
      static std::uint32_t round = 0;
      round = (round + 1) & 0xFFFFFF;
      return round == 0 ? NextRound() : round;
    }

    /// On SDL's timer thread: the time came.
    static Uint32 SDLCALL Fire(void* const said, SDL_TimerID, Uint32)
    {
      SDL_Event event;
      SDL_zero(event);
      event.type = EventType();
      event.user.data1 = said;
      (void)SDL_PushEvent(&event);
      return 0;
    }

    std::array<Timer, kTimers> mTimers{};
  };

  // --- The layer ----------------------------------------------------------------------------------------------------------

  /// A finger on one of FAF's UI's pieces of Griff's, as the activity's view over it took it: the rail's buttons count while
  /// held and light by a tap (touch::Rail), the others are pressed while the finger stays on them and act at its lift.
  struct PieceFinger
  {
    int piece = -1;
    float downX = 0;
    float downY = 0;
    bool moved = false;
    /// A tap target's press: the finger is on it.
    bool pressed = false;
  };

  /// A finger on the glass: the layer's (`id` its number for TouchInput or PageInput), or a piece's.
  struct Finger
  {
    SDL_FingerID sdl = 0;
    bool layers = true;
    int id = 0;
    PieceFinger piece;
  };

  class Layer;

  /// What a game's fingers do (TouchLayer's touch mouse): the layer's, by name.
  class GameMouse final : public TouchInput::Mouse
  {
  public:
    explicit GameMouse(Layer& layer) : mLayer(layer)
    {}

    void Move(float x, float y) override;
    void Button(int button, bool down, float x, float y) override;
    void Tap(float x, float y) override;
    void TapNearGhost(float fingerX, float fingerY, float ghostX, float ghostY) override;
    void StartLine(float x, float y) override;
    void CancelLine() override;
    void Wheel(int delta, float x, float y) override;
    void Held() override;
    bool Placing() override;
    void DeleteOrder(float x, float y) override;
    void LeaveSoon() override;
    void Stay() override;
    void StartTimer(Millis milliseconds) override;
    void StopTimer() override;
    void GriffDown(float x, float y, bool box) override;
    void SmartTap(float x, float y, int rail, int step, float baseX, float baseY, int tap) override;
    void Ring(bool shown, float x, float y) override;
    void Deselect() override;
    bool OnGriff(float x, float y) override;
    void GriffTap() override;
    void HandleRest(int rest) override;
    void Distribute() override;
    void Route(const float* xs, const float* ys, int count, int rail, bool patrol) override;
    void StrokeShown(
      const float* xs,
      const float* ys,
      int count,
      int mode,
      const float* waypointXs,
      const float* waypointYs,
      int waypoints
    ) override;
    void StrokeHover(float x, float y) override;
    void StrokeGivenUp() override;
    int Rail() override;
    void RailUsed(int rail) override;
    void GriffDrag(float x, float y, float nowX, float nowY, bool rested, int rail, int drag) override;
    void GriffDragEnd(float x, float y, float velocityX, float velocityY) override;
    void GriffDragGiveUp(float x, float y) override;
    void RestTap(float x, float y, int rail) override;
    void GriffRest(float x, float y) override;
    void GriffRestGone() override;
    void Pinch(bool start, float x, float y, float distance) override;
    void PinchEnd(float velocityX, float velocityY) override;
    bool Drafting() override;
    int DraftPlan(float x, float y) override;
    bool DraftNewest(int plan) override;
    bool DraftOnDeposit(int plan) override;
    bool DraftPicksDeposit() override;
    void DraftTap(float x, float y, int plan) override;
    void DraftLine(int phase, float x, float y) override;
    void DraftMove(int phase, float fromX, float fromY, float x, float y) override;
    void DraftAim(int phase, float x, float y) override;
    void DraftRest(float x, float y, int rest) override;
    void DraftBox(int phase, float x0, float y0, float x1, float y1) override;
    void DraftDrop() override;
    bool Clicking() override;
    int ClickRuns() override;

  private:
    Layer& mLayer;
  };

  /// What the menus' fingers do (TouchLayer's page mouse).
  class PageMouse final : public touch::PageInput::Mouse
  {
  public:
    explicit PageMouse(Layer& layer) : mLayer(layer)
    {}

    void Move(float x, float y) override;
    void Press(float x, float y) override;
    void Release(float x, float y) override;
    void Tap(float x, float y) override;
    void RightClick(float x, float y) override;
    void LookForList(float x, float y) override;
    void Scroll(float dy) override;
    void Held() override;
    void Leave() override;
    void LeaveSoon() override;
    void Stay() override;
    void StartTimer(Millis milliseconds) override;
    void StopTimer() override;

  private:
    Layer& mLayer;
  };

  class Layer
  {
  public:
    Layer(const int pictureWidth, const int pictureHeight, const float screenMm, const bool griff, const double hudScale)
      : mScreenMm(screenMm), mGriff(griff), mHudScale(hudScale), mPictureWidth(pictureWidth), mPictureHeight(pictureHeight),
        mGameMouse(*this), mPageMouse(*this), mTouch(mGameMouse, kSlopMm * Millimetre()),
        mPage(mPageMouse, kSlopMm * Millimetre())
    {
      mView.SetScreen(pictureWidth, pictureHeight);
      mView.SetPicture(pictureWidth, pictureHeight);
      mTouch.SetGriff(griff);
      mTouch.SetMillimetre(Millimetre());
      mTouch.SetScreen(static_cast<float>(pictureWidth), static_cast<float>(pictureHeight));
      mTimers.Start(kPollTimer, kPollMilliseconds);
      if (griff) {
        // Griff on from the start, as the activity set it with its surface: griff.lua makes Griff's pieces in FAF's UI only
        // where Griff is wanted as the game's UI is made. A game's start sets the sizes anew, at its HUD's factor (PollGriff).
        SetGriff();
      }
    }

    Layer(const Layer&) = delete;
    Layer& operator=(const Layer&) = delete;

    /// The layer goes, as its scheme does: nothing of it stays held or shown, and Griff is off.
    ~Layer()
    {
      mBarWanted = 0;
      ApplyBar();
      (void)mView.ClearPage();
      ApplyView();
      // A mouse's button that the host holds for a page goes up where the host has it.
      for (int button = touch::kButtonLeft; button <= touch::kButtonMiddle; ++button) {
        if ((mHostButtons & (1 << button)) != 0) {
          fingerinput::MouseButton(button, false, mMouseX, mMouseY);
        }
      }
      Release();
      // The gesture's end asked for the pointer to leave, by the leave timer, which goes with the layer: it leaves now.
      const bool leaving = mTimers.Running(kLeaveTimer);
      mTimers.Stop(kTouchTimer);
      mTimers.Stop(kPageTimer);
      mTimers.Stop(kLeaveTimer);
      mTimers.Stop(kPollTimer);
      if (leaving) {
        fingerinput::PointerLeave();
      }
      for (const griffshown::Piece piece : {griffshown::kHandle, griffshown::kRing, griffshown::kStroke, griffshown::kBox}) {
        griffshown::Set(piece, {});
      }
      if (mGriff) {
        // Griff's draft goes with it: nothing would be left to place it or drop it.
        griffinput::DraftDrop();
        griffinput::SetGriff(false, griffinput::Sizes{});
      }
    }

    [[nodiscard]] bool Griff() const
    {
      return mGriff;
    }

    /// The bar's toggles (bar.lua): Order, Del, Shift, Ctrl, as FAF's UI asks; they hold while a game has the fingers, not
    /// under a dialog, and the game's end lets them go.
    enum BarToggle : int
    {
      kBarOrder = 1,
      kBarDelete = 2,
      kBarShift = 4,
      kBarCtrl = 8,
    };

    /// Whether the bar's toggle is wanted now (FAF's UI lights it).
    [[nodiscard]] bool BarHeld(const int toggle) const
    {
      return (mBarWanted & toggle) != 0;
    }

    /// Turns a toggle of the bar over; whether it is wanted now.
    bool FlipBar(const int toggle)
    {
      mBarWanted ^= toggle;
      ApplyBar();
      return (mBarWanted & toggle) != 0;
    }

    /// The picture's size now: Griff's millimetre and sizes follow it.
    void Picture(const int width, const int height)
    {
      if (width == mPictureWidth && height == mPictureHeight) {
        return;
      }
      mPictureWidth = width;
      mPictureHeight = height;
      mView.SetScreen(width, height);
      mView.SetPicture(width, height);
      mTouch.SetMillimetre(Millimetre());
      // The slops are the picture's pixels, which a mm on the glass now has more or fewer of.
      mTouch.SetSlop(kSlopMm * Millimetre());
      mPage.SetSlop(kSlopMm * Millimetre());
      mTouch.SetScreen(static_cast<float>(width), static_cast<float>(height));
      if (mGriff && mGriffPolling) {
        SetGriff();
      }
      KeyboardAsNow();
      ApplyView();
    }

    /// A mouse's event on a page the UI's view zooms: its place goes back to the UI's pixels, as a finger's does, and its
    /// wheel to the list under it, or else to the page, as a finger's drag. A button that went down so goes up so, and the
    /// mouse's moves with it, whatever the page did meanwhile: the host's buttons and the engine's stay one. False elsewhere,
    /// and for a button the game has no use for: those keep SDL's way.
    bool MouseOnPage(const SDL_Event& event)
    {
      const bool zoomed = mView.Zoomed();
      if (!zoomed && mHostButtons == 0) {
        return false;
      }
      // The window's pixels, as the engine's window layer takes a mouse's place (WindowSdl.cpp, ToPixels).
      const auto pixelsOf = [](const SDL_WindowID window) {
        const float density = SDL_GetWindowPixelDensity(SDL_GetWindowFromID(window));
        return density > 0 ? density : 1.0f;
      };
      switch (event.type) {
        case SDL_EVENT_MOUSE_MOTION: {
          const float pixels = pixelsOf(event.motion.windowID);
          mMouseX = PictureX(event.motion.x * pixels);
          mMouseY = PictureY(event.motion.y * pixels);
          fingerinput::MouseMove(mMouseX, mMouseY);
          return true;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
          const int button = event.button.button == SDL_BUTTON_LEFT ? touch::kButtonLeft
            : event.button.button == SDL_BUTTON_RIGHT                ? touch::kButtonRight
            : event.button.button == SDL_BUTTON_MIDDLE               ? touch::kButtonMiddle
                                                                     : -1;
          if (button < 0) {
            return false;
          }
          const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
          const int bit = 1 << button;
          if (!zoomed && (down || (mHostButtons & bit) == 0)) {
            return false;
          }
          mHostButtons = down ? mHostButtons | bit : mHostButtons & ~bit;
          const float pixels = pixelsOf(event.button.windowID);
          mMouseX = PictureX(event.button.x * pixels);
          mMouseY = PictureY(event.button.y * pixels);
          fingerinput::MouseButton(button, down, mMouseX, mMouseY);
          return true;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
          // Away from the user is up the page, as a finger's drag downwards.
          if (!zoomed) {
            return false;
          }
          const float notches = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
          if (notches != 0) {
            const float pixels = pixelsOf(event.wheel.windowID);
            // The list under it is asked once for the notches that come before its answer, which takes them all (PassDrag).
            if (mUnsentDrag == 0) {
              LookForList(event.wheel.mouse_x * pixels, event.wheel.mouse_y * pixels);
            }
            mUnsentDrag += notches * kWheelNotchMm * Millimetre();
            PassDrag();
          }
          return true;
        }
        default:
          return false;
      }
    }

    /// SDL's keyboard came or went (SDL_EVENT_SCREEN_KEYBOARD_SHOWN, _HIDDEN), which counts where the insets say nothing.
    void Keyboard(const bool up)
    {
      mKeyboardUp = up;
      KeyboardAsNow();
    }

    /// The keyboard as it covers the picture now: a page can go up that much further, and goes up as far as it takes to show
    /// the field FAF's Lua asked for (FafreShowLines) above it.
    void KeyboardAsNow()
    {
      const float said = gKeyboardShare.load();
      const float share = said >= 0 ? std::min(said, 1.0f) : mKeyboardUp ? kKeyboardShareUnsaid : 0;
      const int pixels = touch::Round(share * static_cast<float>(mPictureHeight));
      if (pixels == mKeyboardPixels) {
        return;
      }
      if (pixels == 0) {
        // The keyboard went: the field it was for is done with.
        mFieldKnown = false;
      }
      mKeyboardPixels = pixels;
      mView.SetKeyboard(pixels);
      if (pixels > 0 && mFieldKnown) {
        mView.Reveal(mFieldTop, mFieldBottom);
      }
      ApplyView();
    }

    /// A finger came down, at `time`, in the picture's pixels.
    /// All of a move's fingers are in, as the activity marks it (fingers::MoveDone): the layer takes them together.
    void MovesDone()
    {
      FlushMove();
    }

    /// The finger whose lift comes next was a palm the system took back (fingers::Palm): it is forgotten, not lifted.
    void Palm(const SDL_FingerID sdl)
    {
      if (Find(sdl) != nullptr) {
        mPalms.push_back(sdl);
      }
    }

    void FingerDown(const SDL_FingerID sdl, const float x, const float y, const Millis time)
    {
      FlushMove();
      PlaceBarAsNow();
      Finger finger;
      finger.sdl = sdl;
      const int piece = mGriff && mBarInGame ? PieceAt(x, y, time) : -1;
      if (piece >= 0) {
        finger.layers = false;
        finger.piece.piece = piece;
        finger.piece.downX = x;
        finger.piece.downY = y;
        mFingers.push_back(finger);
        PieceDown(mFingers.back().piece, time);
        return;
      }
      const bool first = LayersDown() == 0;
      if (first) {
        // Lua can change whose they are, not under them.
        mLayer = menufingers::MenuFingers() ? static_cast<touch::Fingers*>(&mPage) : &mTouch;
        mCanceled = false;
      }
      finger.id = mNextId;
      mNextId = (mNextId + 1) & 0xFFFF;
      mFingers.push_back(finger);
      mLayer->Down(finger.id, x, y, time, first);
    }

    void FingerMoved(const SDL_FingerID sdl, const float x, const float y, const Millis time)
    {
      Finger* const finger = Find(sdl);
      if (finger == nullptr) {
        return;
      }
      if (!finger->layers) {
        PieceMoved(finger->piece, x, y);
        return;
      }
      mLayer->Move(finger->id, x, y);
      // SDL gives one motion a finger: where the activity marks a move's end the layer takes its fingers together, as Android
      // gave them (Fingers::Moved); else each one on its own.
      if (gMovesMarked.load()) {
        mMovePending = true;
        mMoveTime = time;
      } else {
        mLayer->Moved(time);
      }
    }

    void FingerUp(const SDL_FingerID sdl, const float x, const float y, const Millis time)
    {
      FlushMove();
      Finger* const found = Find(sdl);
      if (found == nullptr) {
        return;
      }
      Finger finger = *found;
      Forget(sdl);
      const bool palm = TakePalm(sdl);
      if (!finger.layers) {
        PieceUp(finger.piece, x, y, time, palm);
        return;
      }
      if (!palm) {
        mLayer->Up(finger.id, time, LayersDown() == 0);
      } else if (LayersDown() > 0) {
        mLayer->Forget(finger.id, time);
      } else {
        // No finger of the gesture is left: it ends as the system's.
        mLayer->Cancel();
      }
    }

    /// The system took a finger: the layer's gesture ends, as a canceled MotionEvent ended it, once for all its fingers.
    void FingerCanceled(const SDL_FingerID sdl, const Millis time)
    {
      Finger* const found = Find(sdl);
      if (found == nullptr) {
        return;
      }
      Finger finger = *found;
      Forget(sdl);
      (void)TakePalm(sdl);
      mMovePending = false;
      if (!finger.layers) {
        PieceUp(finger.piece, finger.piece.downX, finger.piece.downY, time, true);
        return;
      }
      if (!mCanceled) {
        mCanceled = true;
        mLayer->Cancel();
      }
    }

    /// In the back: the fingers' gesture ends, and so do the rail and the frames' reading (TouchLayer's pause).
    void Release()
    {
      if (LayersDown() > 0) {
        mLayer->Cancel();
      }
      mFingers.clear();
      mCanceled = false;
      mMovePending = false;
      mPalms.clear();
      if (mGriff) {
        ReleaseRail();
        PollGriff(false);
      }
    }

    /// The move that waits for its mark (MovesDone), before what comes after it.
    void FlushMove()
    {
      if (mMovePending) {
        mMovePending = false;
        mLayer->Moved(mMoveTime);
      }
    }

    /// Whether the activity said that this finger's lift is a palm's; it says so once.
    bool TakePalm(const SDL_FingerID sdl)
    {
      const auto palm = std::find(mPalms.begin(), mPalms.end(), sdl);
      if (palm == mPalms.end()) {
        return false;
      }
      mPalms.erase(palm);
      return true;
    }

    /// In front again: a game's frames are read again (TouchLayer's resume).
    void Resume()
    {
      if (mGriff && mBarInGame) {
        PollGriff(true);
      }
    }

    void Timer(const int name)
    {
      switch (name) {
        case kTouchTimer:
          mTouch.Timer();
          break;
        case kPageTimer:
          mPage.Timer();
          break;
        case kLeaveTimer:
          fingerinput::PointerLeave();
          break;
        case kPollTimer:
          PlaceBarAsNow();
          mTimers.Start(kPollTimer, kPollMilliseconds);
          break;
        default:
          break;
      }
    }

    /// What the program and FAF's Lua said, and, while a game's frames are read, the handle and the draft as the last frame
    /// placed them (TouchLayer's readProgram and readGriff).
    void Read()
    {
      // The page FAF's Lua declared, which the engine draws larger (ApplyView); a new page has no field the keyboard was for.
      const menufingers::Page page = menufingers::PageNow();
      if (page.shown != mPageSeen.shown || page.left != mPageSeen.left || page.top != mPageSeen.top ||
          page.right != mPageSeen.right || page.bottom != mPageSeen.bottom) {
        mPageSeen = page;
        mFieldKnown = false;
        screenkeyboard::Dismiss();
        if (page.shown) {
          (void)mView.SetPage(page.left, page.top, page.right, page.bottom);
        } else {
          (void)mView.ClearPage();
        }
      }
      PlaceBarAsNow();
      if (mGriff) {
        ReadGriffSaid();
      }
      PassDrag();
      const float rest = menufingers::TakePageDrag();
      if (rest != 0) {
        mView.ScrollBy(rest * mView.Scale());
      }
      const menufingers::ToShow toShow = menufingers::TakeToShow();
      if (toShow.why == menufingers::kLinesToShow) {
        mView.Reveal(toShow.top, toShow.bottom);
      } else if (toShow.why != menufingers::kNoPlace) {
        // The text field the keyboard comes for (a finger's tap, the bar's Keys): above the keyboard, now or as it comes.
        mFieldKnown = true;
        mFieldTop = toShow.top;
        mFieldBottom = toShow.bottom;
        if (mKeyboardPixels > 0) {
          mView.Reveal(mFieldTop, mFieldBottom);
        }
      }
      ApplyView();
      if (mGriffPolling) {
        ReadGriff();
      }
    }

    /// The view the engine draws FAF's UI with now (ApplyView): the scales and the offsets.
    [[nodiscard]] const std::array<float, 4>& UiView() const
    {
      return mUiView;
    }

    /// The page's view, as the engine draws FAF's UI (UI_SetView): PageView's own mapping from the picture's pixels to the
    /// screen's, whose inverse fingers and a mouse go through (PictureX, PictureY). The identity without a zoomed page.
    void ApplyView()
    {
      std::array<float, 4> view{1, 1, 0, 0};
      if (mView.Zoomed() && mPictureWidth > 0 && mPictureHeight > 0) {
        view = {
          static_cast<float>(mView.ViewWidth()) / static_cast<float>(mPictureWidth),
          static_cast<float>(mView.ViewHeight()) / static_cast<float>(mPictureHeight),
          mView.ViewLeft(),
          mView.ViewTop(),
        };
      }
      if (view != mUiView) {
        mUiView = view;
        moho::UI_SetView(view[0], view[1], view[2], view[3]);
      }
    }

    // --- What the mice ask, by name -------------------------------------------------------------------------------------

    [[nodiscard]] int PictureX(const float x) const
    {
      return mView.PictureX(x);
    }

    [[nodiscard]] int PictureY(const float y) const
    {
      return mView.PictureY(y);
    }

    /// A finger's place in the picture, with Griff never on its edge: FAF scrolls the view while its cursor is on the window's
    /// edge pixel (ui_ScreenEdgeScrollView), where a mouse is pushed on purpose and a finger at the glass's edge would hold it.
    [[nodiscard]] int FingerX(const float x) const
    {
      const int place = PictureX(x);
      return mGriff ? std::max(1, std::min(mPictureWidth - 2, place)) : place;
    }

    [[nodiscard]] int FingerY(const float y) const
    {
      const int place = PictureY(y);
      return mGriff ? std::max(1, std::min(mPictureHeight - 2, place)) : place;
    }

    /// This many mm on the glass in the picture's pixels, which a page shows larger.
    [[nodiscard]] int PictureMm(const double mm) const
    {
      return static_cast<int>(std::lround(mm * mPictureHeight / mScreenMm / mView.Zoom()));
    }

    /// The picture's pixels in a mm on the glass.
    [[nodiscard]] float Millimetre() const
    {
      return static_cast<float>(std::min(mPictureWidth, mPictureHeight)) / mScreenMm;
    }

    [[nodiscard]] float Scale() const
    {
      return mView.Scale();
    }

    Timers& TimersOf()
    {
      return mTimers;
    }

    void LeaveSoon()
    {
      mTimers.Start(kLeaveTimer, TouchInput::kLeaveMilliseconds);
    }

    void Stay()
    {
      mTimers.Stop(kLeaveTimer);
      fingerinput::PointerStay();
    }

    [[nodiscard]] int RailCounting() const
    {
      return mRail.Counting(Now());
    }

    void RailUsed(const int rail)
    {
      mRail.Used(rail);
      ShowRail();
    }

    /// Griff: whether the handle over the selection is under this place, as the last frame placed it. With Box the handle
    /// steps aside, for a box that starts under it.
    [[nodiscard]] bool OnHandle(const float x, const float y)
    {
      ReadGriff();
      return mHandle.Shown() && (RailCounting() & TouchInput::kRailBox) == 0 &&
        touch::Hypot(x - mHandle.X(), y - mHandle.Y()) <= HandleRadius() + kHandleTouchMm * Millimetre();
    }

    /// Griff: a stroke is drawn (count > 0) or gone, for FAF's UI to draw.
    void StrokeShown(
      const float* const xs,
      const float* const ys,
      const int count,
      const int mode,
      const float* const waypointXs,
      const float* const waypointYs,
      const int waypoints
    )
    {
      if (mHandle.Stroke(count > 0)) {
        // A new stroke: the handle stays where it began, and only the program's answers for this one count.
        mStrokeEnd = 0;
      }
      ShowStroke(xs, ys, count, mode, waypointXs, waypointYs, waypoints);
    }

    [[nodiscard]] int StrokeNumber() const
    {
      return mHandle.StrokeNumber();
    }

    /// A piece of one place for FAF's UI: there, or none.
    void ShowPiece(const griffshown::Piece piece, const bool shown, const float x, const float y)
    {
      griffshown::Set(
        piece,
        shown ? std::vector<float>{static_cast<float>(PictureX(x)), static_cast<float>(PictureY(y))} : std::vector<float>{}
      );
    }

    [[nodiscard]] const touch::Draft& DraftNow()
    {
      ReadDraft();
      return mDraft;
    }

    [[nodiscard]] const touch::Draft& DraftRead() const
    {
      return mDraft;
    }

    /// What a finger has gone in the menus, for the list under it or the page.
    void Scroll(const float dy)
    {
      mUnsentDrag += dy;
      PassDrag();
    }

    void LookForList(const float x, const float y)
    {
      mUnsentDrag = 0;
      menufingers::LookForList(PictureX(x), PictureY(y));
    }

  private:
    [[nodiscard]] double HudFactor() const
    {
      const float factor = menufingers::HudFactor();
      return factor > 0 ? factor : mHudScale;
    }

    /// The handle's radius: half an order button of FAF's at the HUD's factor.
    [[nodiscard]] float HandleRadius() const
    {
      return static_cast<float>(kHandleSide / 2 * HudFactor());
    }

    /// Griff's sizes in the picture's pixels (a game's page has no zoom), by which the program places the handle, helps taps,
    /// picks units beside them, stops the camera's glide and finds a deposit for the draft's extractor. The handle's are FAF's
    /// at the HUD's factor, as FAF's UI draws it.
    void SetGriff()
    {
      const double pixels = mPictureHeight / static_cast<double>(mScreenMm);
      const double factor = HudFactor();
      const double radius = kHandleSide / 2 * factor;
      griffinput::Sizes sizes;
      sizes.radius = static_cast<float>(radius);
      sizes.touchRadius = static_cast<float>(radius + kHandleTouchMm * pixels);
      sizes.gap = static_cast<float>(kHandleGapMm * pixels);
      sizes.arrowReach = static_cast<float>(radius + kArrowSide / 2 * factor);
      sizes.tapReach = static_cast<float>(kTapReachMm * pixels);
      sizes.tapSmallSide = static_cast<float>(kTapSmallMm * pixels);
      sizes.nearReach = static_cast<float>(kNearMm * pixels);
      sizes.nearReachWithTroops = static_cast<float>(kNearTroopsMm * pixels);
      sizes.glideStop = static_cast<float>(kGlideStopMm * pixels);
      sizes.depositReach = static_cast<float>(kDepositMm * pixels);
      griffinput::SetGriff(true, sizes);
    }

    // --- Fingers ----------------------------------------------------------------------------------------------------------

    [[nodiscard]] Finger* Find(const SDL_FingerID sdl)
    {
      const auto at = std::find_if(mFingers.begin(), mFingers.end(), [sdl](const Finger& finger) { return finger.sdl == sdl; });
      return at != mFingers.end() ? &*at : nullptr;
    }

    void Forget(const SDL_FingerID sdl)
    {
      std::erase_if(mFingers, [sdl](const Finger& finger) { return finger.sdl == sdl; });
    }

    [[nodiscard]] int LayersDown() const
    {
      return static_cast<int>(std::count_if(mFingers.begin(), mFingers.end(), [](const Finger& finger) { return finger.layers; }));
    }

    // --- FAF's UI's pieces of Griff's (griffhud.lua) --------------------------------------------------------------------

    [[nodiscard]] std::size_t PieceAt(const int piece) const
    {
      return static_cast<std::size_t>(piece * griffinput::kHudPiece);
    }

    /// Whether piece `piece` takes fingers now: wanted, placed by FAF's UI, and not aside under something the player opened of
    /// FAF's or with FAF's UI hidden.
    [[nodiscard]] bool PieceTakes(const int piece, const Millis time) const
    {
      return PieceKnown(piece) && mHudPlaces[PieceAt(piece) + 4] == 0 && PieceWanted(piece, time);
    }

    /// Wanted in a game: the rail always, the undo's button while its time runs or its undo waits, the tick while the draft is
    /// open, the cross also while Griff's Esc has something to put away.
    [[nodiscard]] bool PieceWanted(const int piece, const Millis time) const
    {
      if (!mBarInGame) {
        return false;
      }
      switch (piece) {
        case kBoxPiece:
        case kPlusPiece:
          return true;
        case kUndoPiece:
          return mUndo.Showing(time);
        case kTickPiece:
          return mDraft.Open();
        case kCrossPiece:
          return mDraft.Open() || griffinput::Escapes();
        default:
          return false;
      }
    }

    [[nodiscard]] bool PieceKnown(const int piece) const
    {
      const std::size_t at = PieceAt(piece);
      return mHudPlaces[at + 2] > mHudPlaces[at] && mHudPlaces[at + 3] > mHudPlaces[at + 1];
    }

    /// Whether this place is on the piece, or `margin` beside it.
    [[nodiscard]] bool OnPiece(const int piece, const float x, const float y, const float margin = 0) const
    {
      const std::size_t at = PieceAt(piece);
      const float px = static_cast<float>(PictureX(x));
      const float py = static_cast<float>(PictureY(y));
      return px >= mHudPlaces[at] - margin && px < mHudPlaces[at + 2] + margin && py >= mHudPlaces[at + 1] - margin &&
        py < mHudPlaces[at + 3] + margin;
    }

    /// The piece a finger down here takes, the last drawn first; -1: none.
    [[nodiscard]] int PieceAt(const float x, const float y, const Millis time)
    {
      mHudPlaces = griffinput::HudPlaces();
      for (int piece = griffinput::kHudPieces - 1; piece >= 0; --piece) {
        if (PieceTakes(piece, time) && OnPiece(piece, x, y)) {
          return piece;
        }
      }
      return -1;
    }

    [[nodiscard]] static int RailBit(const int piece)
    {
      return piece == kBoxPiece ? TouchInput::kRailBox : piece == kPlusPiece ? TouchInput::kRailQueue : 0;
    }

    void PieceDown(PieceFinger& finger, const Millis time)
    {
      if (const int bit = RailBit(finger.piece); bit != 0) {
        // While the program's clicks run the finger rests, as on the map: a held Plus would be Shift between them. A second
        // finger on a held button is no press of its own.
        if (!mRail.Held(bit) && !griffinput::Clicking()) {
          mRail.Press(bit, time);
          if (bit == TouchInput::kRailQueue) {
            griffinput::HoldShift(true);
          }
        }
        ShowRail();
        return;
      }
      finger.pressed = true;
      PlaceHud();
    }

    void PieceMoved(PieceFinger& finger, const float x, const float y)
    {
      if (RailBit(finger.piece) != 0) {
        // Griff's slop, as for its gestures on the map: a thumb that wobbles on the rail still taps.
        finger.moved |= touch::Hypot(x - finger.downX, y - finger.downY) > TouchInput::kGriffSlopMm * Millimetre();
        return;
      }
      // A view's press goes once the finger leaves it by more than the slop, and does not come back.
      if (finger.pressed && !OnPiece(finger.piece, x, y, kSlopMm * Millimetre())) {
        finger.pressed = false;
        PlaceHud();
      }
    }

    void PieceUp(PieceFinger& finger, const float x, const float y, const Millis time, const bool canceled)
    {
      if (const int bit = RailBit(finger.piece); bit != 0) {
        if (mRail.Held(bit) && !RailHeldByOther(finger)) {
          if (canceled) {
            mRail.Cancel(bit);
          } else {
            mRail.Release(bit, time, !finger.moved && OnPiece(finger.piece, x, y));
          }
          if (bit == TouchInput::kRailQueue) {
            griffinput::HoldShift(false);
          }
        }
        ShowRail();
        return;
      }
      const bool clicked = finger.pressed && !canceled;
      finger.pressed = false;
      if (clicked) {
        PieceClicked(finger.piece, time);
      }
      PlaceHud();
    }

    /// Whether another finger, which came first, still holds the rail's button of this one: its lift is the button's.
    [[nodiscard]] bool RailHeldByOther(const PieceFinger& finger) const
    {
      return std::any_of(mFingers.begin(), mFingers.end(), [&finger](const Finger& other) {
        return !other.layers && other.piece.piece == finger.piece && &other.piece != &finger;
      });
    }

    void PieceClicked(const int piece, const Millis time)
    {
      switch (piece) {
        case kUndoPiece:
          Undo(time);
          break;
        case kTickPiece:
          // With Plus counting, held or lit, every plan is appended to what the builders do; the program says what it used.
          griffinput::DraftCommit((mRail.Counting(time) & TouchInput::kRailQueue) != 0);
          break;
        case kCrossPiece:
          // Griff's Esc: the draft, else the armed command mode, else the selection goes.
          griffinput::Escape();
          break;
        default:
          break;
      }
    }

    /// The undo's button was tapped: the program takes its record back, and the next tap climbs no ladder from an order undone.
    /// Not while the program's clicks run: the finger rests, as on the map.
    void Undo(const Millis time)
    {
      if (griffinput::Clicking()) {
        return;
      }
      const int record = mUndo.Tap(time);
      if (record != 0) {
        griffinput::Undo(record);
        mTouch.ClearLadder();
      }
    }

    [[nodiscard]] bool PiecePressed(const int piece) const
    {
      return std::any_of(mFingers.begin(), mFingers.end(), [piece](const Finger& finger) {
        return !finger.layers && finger.piece.piece == piece && finger.piece.pressed;
      });
    }

    /// How the rail's buttons and the pieces look, held or lit, and the undo's button, for FAF's UI to draw them; the undo's
    /// lingering cover for the program (TouchLayer's placeHud and setGriffCovers).
    void PlaceHud()
    {
      if (!mGriff) {
        return;
      }
      const Millis now = Now();
      const bool undoWanted = PieceWanted(kUndoPiece, now);
      int undo = griffinput::kHudUndoNone;
      if (undoWanted) {
        undo = mUndo.Waiting() ? griffinput::kHudUndoWaiting : griffinput::kHudUndoOffered;
      }
      const int held = (PiecePressed(kTickPiece) ? griffinput::kHudHeldTick : 0) |
        (PiecePressed(kCrossPiece) ? griffinput::kHudHeldCross : 0) | (PiecePressed(kUndoPiece) ? griffinput::kHudHeldUndo : 0);
      griffinput::SetHud(mRail.Look(TouchInput::kRailBox) | mRail.Look(TouchInput::kRailQueue) << 2 | held, undo, mUndo.Left(now));
      SetCovers(now, undoWanted);
    }

    /// The undo's button's cover, while it shows and a moment after; where something the player opened of FAF's lies, it goes
    /// at once.
    void SetCovers(const Millis now, const bool undoWanted)
    {
      const std::array<int, 4>& was = mUndoCover.Box();
      if (mOpened.Under(
            static_cast<float>(was[0]), static_cast<float>(was[1]), static_cast<float>(was[2]), static_cast<float>(was[3])
          )) {
        mUndoCover.Forget();
      }
      const std::size_t at = PieceAt(kUndoPiece);
      const bool shown = undoWanted && PieceKnown(kUndoPiece) && mHudPlaces[at + 4] == 0;
      mCovers.Begin();
      mCovers.Piece(
        mUndoCover, shown, static_cast<int>(mHudPlaces[at]), static_cast<int>(mHudPlaces[at + 1]),
        static_cast<int>(mHudPlaces[at + 2]), static_cast<int>(mHudPlaces[at + 3]), now
      );
      if (mCovers.End()) {
        griffinput::SetCovers(mCovers.Boxes().data(), mCovers.Count());
      }
    }

    /// The rail as it is: the handle's look by the buttons held or lit, and how each button looks for FAF's UI.
    void ShowRail()
    {
      if (mGriff) {
        ShowHandle();
        PlaceHud();
      }
    }

    /// The rail put away, and with Plus the Shift it holds: nothing is held where no finger can let go of it.
    void ReleaseRail()
    {
      if (mRail.Held(TouchInput::kRailQueue)) {
        griffinput::HoldShift(false);
      }
      mRail.Clear();
      ShowRail();
    }

    // --- Griff's frame ----------------------------------------------------------------------------------------------------

    /// The bar's toggles as they hold now: what FAF's UI asked for while a game has the fingers, else none.
    void ApplyBar()
    {
      const int applied = mBarInGame ? mBarWanted : 0;
      const int changed = applied ^ mBarApplied;
      mBarApplied = applied;
      if ((changed & kBarOrder) != 0) {
        mTouch.SetSwapped((applied & kBarOrder) != 0);
      }
      if ((changed & kBarDelete) != 0) {
        mTouch.SetDeleting((applied & kBarDelete) != 0);
      }
      if ((changed & kBarShift) != 0) {
        fingerinput::Key(VK_SHIFT, (applied & kBarShift) != 0);
      }
      if ((changed & kBarCtrl) != 0) {
        fingerinput::Key(VK_CONTROL, (applied & kBarCtrl) != 0);
      }
    }

    /// The game's pieces are for a game that fingers are on: a dialog over it has the fingers of the menus.
    void PlaceBarAsNow()
    {
      const bool inGame = moho::UI_GamePlaying() && !menufingers::MenuFingers();
      if (inGame == mBarInGame) {
        return;
      }
      mBarInGame = inGame;
      if (!moho::UI_GamePlaying()) {
        // The game is over, and its bar with it: the next game's begins with none held.
        mBarWanted = 0;
      }
      ApplyBar();
      if (mGriff) {
        if (!inGame) {
          ReleaseRail();
        }
        PollGriff(inGame);
        PlaceHud();
      }
    }

    /// While a game is on, the handle follows the selection each frame. Without the frames the undo goes with its record: a
    /// dialog over the game, or the game's end.
    void PollGriff(const bool on)
    {
      if (on == mGriffPolling) {
        return;
      }
      mGriffPolling = on;
      if (on) {
        // The HUD's factor as the game has it now, which the user's UI scale may have changed: the handle's size follows it.
        SetGriff();
      } else {
        mUndo.Clear();
        ReadGriff();
      }
    }

    /// The handle as the last frame placed it, while a game's frames are read; while a stroke is drawn it stays where the
    /// stroke began, and with the frames it goes (touch::GriffHandle). With it the free map, and what the stroke's end is on.
    void ReadGriff()
    {
      if (mGriffPolling) {
        mHandleRead = griffinput::HandleNow();
      }
      const std::vector<float> opened = mGriffPolling ? griffinput::Opened() : std::vector<float>{};
      mOpened.Update(opened.data(), static_cast<int>(opened.size()));
      ReadDraft();
      mHudPlaces = griffinput::HudPlaces();
      PlaceHud();
      // While the draft is open there is no handle: no stroke starts (the mockup's).
      const bool shown = mGriffPolling && mHandleRead.shown && !mDraft.Open();
      if (!mHandle.Place(mGriffPolling, shown, mView.ScreenX(mHandleRead.x), mView.ScreenY(mHandleRead.y))) {
        if (mHandleRead.stroke == mHandle.StrokeNumber() && mHandleRead.strokeEnd != mStrokeEnd) {
          mStrokeEnd = mHandleRead.strokeEnd;
          SayStroke();
        }
        return;
      }
      mFreeLeft = mView.ScreenX(mHandleRead.freeLeft);
      mFreeTop = mView.ScreenY(mHandleRead.freeTop);
      mFreeRight = mView.ScreenX(mHandleRead.freeRight);
      ShowHandle();
    }

    /// The draft as the program shows it now, which a finger's place is told by.
    void ReadDraft()
    {
      const std::vector<float> shown = mGriffPolling ? griffinput::DraftShown() : std::vector<float>{};
      (void)mDraft.Update(shown.data(), static_cast<int>(shown.size()), mOnScreen);
    }

    /// The handle as the screen keeps it, for FAF's UI to draw it, where that changed: its middle, its group's middle and
    /// count, whether it points at its group from elsewhere, and whether Box is lit, for which it steps aside.
    void ShowHandle()
    {
      std::vector<float> values;
      if (mHandle.Shown()) {
        values = {
          static_cast<float>(PictureX(mHandle.X())),
          static_cast<float>(PictureY(mHandle.Y())),
          mHandleRead.groupX,
          mHandleRead.groupY,
          static_cast<float>(mHandleRead.count),
          mHandleRead.off ? 1.0f : 0.0f,
          (mRail.Shown() & TouchInput::kRailBox) != 0 ? 1.0f : 0.0f,
        };
      }
      if (values != mHandleSaid) {
        mHandleSaid = values;
        griffshown::Set(griffshown::kHandle, std::move(values));
      }
    }

    /// The stroke a finger draws (count 0: none), what lifting it now gives and the waypoints that would be ordered, for FAF's
    /// UI to draw as FAF's command graph will draw the route: where the stroke began, then each waypoint. After the handle's
    /// rest (stroke::kDistribute) only where it began, for the label alone.
    void ShowStroke(
      const float* const xs,
      const float* const ys,
      const int count,
      const int mode,
      const float* const waypointXs,
      const float* const waypointYs,
      const int waypoints
    )
    {
      const bool labelAlone = mode == touch::stroke::kDistribute && count > 0;
      if (!labelAlone && (count < 2 || waypoints < 1)) {
        mStrokeShown.clear();
        griffshown::Set(griffshown::kStroke, {});
        return;
      }
      mStrokeMode = mode;
      mStrokeEndX = xs[count - 1];
      mStrokeEndY = ys[count - 1];
      mStrokeShown.assign(kStrokeHead, 0);
      mStrokeShown[4] = static_cast<float>(1 + waypoints);
      mStrokeShown.push_back(static_cast<float>(PictureX(xs[0])));
      mStrokeShown.push_back(static_cast<float>(PictureY(ys[0])));
      for (int point = 0; point < waypoints; ++point) {
        mStrokeShown.push_back(static_cast<float>(PictureX(waypointXs[point])));
        mStrokeShown.push_back(static_cast<float>(PictureY(waypointYs[point])));
      }
      SayStroke();
    }

    /// The stroke as it is, with what lifting it gives by what its end is on, and its label's place: above the finger, which
    /// covers what is under it, kept on the free map where it is known and wide enough.
    void SayStroke()
    {
      if (mStrokeShown.empty()) {
        return;
      }
      const float pixels = Millimetre();
      const float left = mFreeLeft + kLabelInMm * pixels;
      const float right = mFreeRight - kLabelInMm * pixels;
      const float x = left <= right ? std::max(left, std::min(right, mStrokeEndX)) : mStrokeEndX;
      const float y = std::max(mStrokeEndY - kLabelAboveMm * pixels, mFreeTop + kLabelBelowTopMm * pixels);
      mStrokeShown[0] = static_cast<float>(touch::stroke::Meaning(mStrokeEnd, mStrokeMode));
      mStrokeShown[1] = (mStrokeEnd & touch::stroke::kEndAttackMove) != 0 ? 1.0f : 0.0f;
      mStrokeShown[2] = static_cast<float>(PictureX(x));
      mStrokeShown[3] = static_cast<float>(PictureY(y));
      griffshown::Set(griffshown::kStroke, mStrokeShown);
    }

    /// What the program said of Griff's taps, drags, orders and draft: the rail's buttons a Box tap or a tick used, the tap
    /// ladder's answers, a drag it made a pan of, a group stored, an undo's record and its end. Its words FAF's UI shows itself.
    void ReadGriffSaid()
    {
      for (const std::array<int, 3>& said : griffinput::TakeSaid()) {
        switch (said[0]) {
          case griffinput::kSaidTap:
            mTouch.TapAnswered(said[1], said[2] & 0xFF);
            if ((said[2] >> 8) != 0) {
              RailUsed(said[2] >> 8);
            }
            break;
          case griffinput::kSaidPan:
            // A second finger that waited for the word becomes one now, on the fingers' clock.
            mTouch.Panned(said[1], Now());
            break;
          case griffinput::kSaidFormation:
            mTouch.Formation(said[1]);
            break;
          case griffinput::kSaidDraftRest:
            if (said[2] != 0) {
              mTouch.DraftRestOnUi(said[1]);
            }
            break;
          case griffinput::kSaidDistributes:
            if (said[2] != 0) {
              mTouch.Distributes(said[1]);
            }
            break;
          case griffinput::kSaidGroupStored:
            // A group stored buzzes once, as the mockup's: as the ring of the rest that stored it came, or now.
            if (mTouch.StoreBuzzes()) {
              platform::android::Buzz();
            }
            break;
          case griffinput::kSaidCommitted:
            // The tick's last click left the pointer on the map, which a finger's lift would have taken away.
            LeaveSoon();
            if (said[1] != 0) {
              RailUsed(said[1]);
            }
            break;
          case griffinput::kSaidOrdered:
            mUndo.Ordered(said[1], Now());
            break;
          case griffinput::kSaidUndone:
            // The undo's last click left the pointer on the map, as the tick's; a word that changed after an undo over earlier
            // comes without a click.
            if (mUndo.Undone(said[1])) {
              LeaveSoon();
            }
            break;
          default:
            break;
        }
      }
    }

    /// What a finger has gone goes to the list under it, or if it is known that none is, to the page.
    void PassDrag()
    {
      if (mUnsentDrag == 0) {
        return;
      }
      const int answer = menufingers::ListAnswer();
      if (answer == menufingers::kNoList) {
        mView.ScrollBy(mUnsentDrag);
        mUnsentDrag = 0;
        ApplyView();
      } else if (answer == menufingers::kList) {
        menufingers::ScrollList(mUnsentDrag / mView.Scale());
        mUnsentDrag = 0;
      }
    }

    /// From the picture's pixels to the layer's, for the draft.
    struct OnScreen final : touch::Draft::Screen
    {
      explicit OnScreen(const touch::PageView& view) : mView(view)
      {}

      float X(const float pictureX) const override
      {
        return mView.ScreenX(pictureX);
      }

      float Y(const float pictureY) const override
      {
        return mView.ScreenY(pictureY);
      }

      const touch::PageView& mView;
    };

    float mScreenMm;
    bool mGriff;
    double mHudScale;
    int mPictureWidth;
    int mPictureHeight;
    touch::PageView mView;
    menufingers::Page mPageSeen;
    /// The view the engine draws FAF's UI with (ApplyView): the scales and the offsets.
    std::array<float, 4> mUiView{1, 1, 0, 0};
    /// SDL's keyboard is up, and covers this many of the picture's lines; the lines FAF's Lua asked to show above it.
    bool mKeyboardUp = false;
    int mKeyboardPixels = 0;
    bool mFieldKnown = false;
    int mFieldTop = 0;
    int mFieldBottom = 0;
    OnScreen mOnScreen{mView};
    Timers mTimers;
    GameMouse mGameMouse;
    PageMouse mPageMouse;
    TouchInput mTouch;
    touch::PageInput mPage;
    /// Whose the fingers now on the glass are: Lua can change it, not under them.
    touch::Fingers* mLayer = &mTouch;
    std::vector<Finger> mFingers;
    int mNextId = 0;
    /// A move whose fingers wait for the activity's mark (MovesDone), and its time.
    bool mMovePending = false;
    Millis mMoveTime = 0;
    /// The fingers whose lift the activity said is a palm's (Palm).
    std::vector<SDL_FingerID> mPalms;
    /// A mouse on a page: the buttons the host holds for it (1 << touch::kButtonLeft and the rest), and its last place.
    int mHostButtons = 0;
    int mMouseX = 0;
    int mMouseY = 0;
    /// The system took the layer's fingers: what is left of them is no gesture's until they lift.
    bool mCanceled = false;
    /// What a finger has gone and nobody has taken yet: the program has not said what list is under it.
    float mUnsentDrag = 0;
    /// Griff: a game's fingers are the game's, and its frames are read.
    bool mBarInGame = false;
    /// The bar's toggles FAF's UI asked for, and those that hold (BarToggle bits).
    int mBarWanted = 0;
    int mBarApplied = 0;
    bool mGriffPolling = false;
    touch::Rail mRail;
    touch::Undo mUndo;
    touch::Draft mDraft;
    touch::Opened mOpened;
    touch::Covers mCovers;
    touch::Covers::Lingering mUndoCover{kUndoCoverAfter};
    std::array<float, griffinput::kHudPiece * griffinput::kHudPieces> mHudPlaces{};
    touch::GriffHandle mHandle;
    griffinput::Handle mHandleRead;
    std::vector<float> mHandleSaid;
    /// The stroke as FAF's UI hears it, what its end is on (stroke::kEnd, the program's answer, 0 not yet) and how it goes;
    /// its end and the free map's left, top and right at its middle, for its label.
    std::vector<float> mStrokeShown;
    int mStrokeEnd = 0;
    int mStrokeMode = 0;
    float mStrokeEndX = 0;
    float mStrokeEndY = 0;
    float mFreeLeft = 0;
    float mFreeTop = 0;
    float mFreeRight = 0;
  };

  // --- The mice: what the layer's fingers give -----------------------------------------------------------------------------

  void GameMouse::Move(const float x, const float y)
  {
    fingerinput::MouseMove(mLayer.FingerX(x), mLayer.FingerY(y));
  }

  void GameMouse::Button(const int button, const bool down, const float x, const float y)
  {
    fingerinput::TouchButton(button, down, mLayer.FingerX(x), mLayer.FingerY(y));
  }

  void GameMouse::Tap(const float x, const float y)
  {
    fingerinput::Tap(mLayer.FingerX(x), mLayer.FingerY(y), mLayer.PictureMm(kTapReachMm), mLayer.PictureMm(kTapSmallMm));
  }

  void GameMouse::TapNearGhost(const float fingerX, const float fingerY, const float ghostX, const float ghostY)
  {
    fingerinput::TapNearGhost(mLayer.FingerX(fingerX), mLayer.FingerY(fingerY), mLayer.FingerX(ghostX), mLayer.FingerY(ghostY));
  }

  void GameMouse::StartLine(const float x, const float y)
  {
    fingerinput::MouseButton(touch::kButtonLeft, true, mLayer.FingerX(x), mLayer.FingerY(y));
  }

  void GameMouse::CancelLine()
  {
    fingerinput::CancelLine();
  }

  void GameMouse::Wheel(const int delta, const float x, const float y)
  {
    fingerinput::MouseWheel(delta, mLayer.FingerX(x), mLayer.FingerY(y));
  }

  void GameMouse::Held()
  {
    platform::android::Buzz();
  }

  bool GameMouse::Placing()
  {
    return fingerinput::PlacingBuilding();
  }

  void GameMouse::DeleteOrder(const float x, const float y)
  {
    fingerinput::DeleteOrder(mLayer.FingerX(x), mLayer.FingerY(y));
  }

  void GameMouse::LeaveSoon()
  {
    mLayer.LeaveSoon();
  }

  void GameMouse::Stay()
  {
    mLayer.Stay();
  }

  void GameMouse::StartTimer(const Millis milliseconds)
  {
    mLayer.TimersOf().Start(kTouchTimer, milliseconds);
  }

  void GameMouse::StopTimer()
  {
    mLayer.TimersOf().Stop(kTouchTimer);
  }

  void GameMouse::GriffDown(const float x, const float y, const bool box)
  {
    griffinput::Down(mLayer.FingerX(x), mLayer.FingerY(y), box);
  }

  void GameMouse::SmartTap(
    const float x,
    const float y,
    const int rail,
    const int step,
    const float baseX,
    const float baseY,
    const int tap
  )
  {
    griffinput::SmartTap(mLayer.FingerX(x), mLayer.FingerY(y), rail, step, mLayer.FingerX(baseX), mLayer.FingerY(baseY), tap);
  }

  void GameMouse::Ring(const bool shown, const float x, const float y)
  {
    mLayer.ShowPiece(griffshown::kRing, shown, x, y);
  }

  void GameMouse::Deselect()
  {
    griffinput::Deselect();
  }

  bool GameMouse::OnGriff(const float x, const float y)
  {
    return mLayer.OnHandle(x, y);
  }

  void GameMouse::GriffTap()
  {
    // The camera flies to the group.
    griffinput::ToGroup(true);
  }

  void GameMouse::HandleRest(const int rest)
  {
    griffinput::HandleRest(rest);
  }

  void GameMouse::Distribute()
  {
    griffinput::Distribute();
  }

  void GameMouse::Route(const float* const xs, const float* const ys, const int count, const int rail, const bool patrol)
  {
    std::vector<int> places(static_cast<std::size_t>(2 * std::max(0, count)));
    for (int at = 0; at < count; ++at) {
      places[static_cast<std::size_t>(2 * at)] = mLayer.FingerX(xs[at]);
      places[static_cast<std::size_t>(2 * at + 1)] = mLayer.FingerY(ys[at]);
    }
    griffinput::Route(places.data(), count, rail, patrol);
  }

  void GameMouse::StrokeShown(
    const float* const xs,
    const float* const ys,
    const int count,
    const int mode,
    const float* const waypointXs,
    const float* const waypointYs,
    const int waypoints
  )
  {
    mLayer.StrokeShown(xs, ys, count, mode, waypointXs, waypointYs, waypoints);
  }

  void GameMouse::StrokeHover(const float x, const float y)
  {
    griffinput::StrokeHover(mLayer.FingerX(x), mLayer.FingerY(y), mLayer.StrokeNumber());
  }

  void GameMouse::StrokeGivenUp()
  {
    griffinput::Hint(griffinput::kHintCancelled, 0);
  }

  int GameMouse::Rail()
  {
    return mLayer.RailCounting();
  }

  void GameMouse::RailUsed(const int rail)
  {
    mLayer.RailUsed(rail);
  }

  void GameMouse::GriffDrag(
    const float x,
    const float y,
    const float nowX,
    const float nowY,
    const bool rested,
    const int rail,
    const int drag
  )
  {
    griffinput::Drag(mLayer.FingerX(x), mLayer.FingerY(y), mLayer.FingerX(nowX), mLayer.FingerY(nowY), rested, rail, drag);
  }

  void GameMouse::GriffDragEnd(const float x, const float y, const float velocityX, const float velocityY)
  {
    const float scale = mLayer.Scale();
    griffinput::DragEnd(mLayer.FingerX(x), mLayer.FingerY(y), velocityX / scale, velocityY / scale);
  }

  void GameMouse::GriffDragGiveUp(const float x, const float y)
  {
    griffinput::DragGiveUp(mLayer.FingerX(x), mLayer.FingerY(y));
  }

  void GameMouse::RestTap(const float x, const float y, const int rail)
  {
    griffinput::RestTap(mLayer.FingerX(x), mLayer.FingerY(y), rail);
  }

  void GameMouse::GriffRest(const float x, const float y)
  {
    griffinput::Rest(mLayer.FingerX(x), mLayer.FingerY(y));
  }

  void GameMouse::GriffRestGone()
  {
    griffinput::RestGone();
  }

  void GameMouse::Pinch(const bool start, const float x, const float y, const float distance)
  {
    griffinput::Pinch(start, mLayer.FingerX(x), mLayer.FingerY(y), touch::Round(distance / mLayer.Scale()));
  }

  void GameMouse::PinchEnd(const float velocityX, const float velocityY)
  {
    const float scale = mLayer.Scale();
    griffinput::PinchEnd(velocityX / scale, velocityY / scale);
  }

  bool GameMouse::Drafting()
  {
    return mLayer.DraftNow().Open();
  }

  int GameMouse::DraftPlan(const float x, const float y)
  {
    return mLayer.DraftRead().PlanAt(x, y, touch::Draft::kLeastMm / 2 * mLayer.Millimetre());
  }

  bool GameMouse::DraftNewest(const int plan)
  {
    return mLayer.DraftRead().Newest(plan);
  }

  bool GameMouse::DraftOnDeposit(const int plan)
  {
    return mLayer.DraftRead().OnDeposit(plan);
  }

  bool GameMouse::DraftPicksDeposit()
  {
    return mLayer.DraftRead().PicksDeposit();
  }

  void GameMouse::DraftTap(const float x, const float y, const int plan)
  {
    griffinput::DraftTap(mLayer.FingerX(x), mLayer.FingerY(y), plan);
  }

  void GameMouse::DraftLine(const int phase, const float x, const float y)
  {
    griffinput::DraftLine(phase, mLayer.FingerX(x), mLayer.FingerY(y));
  }

  void GameMouse::DraftMove(const int phase, const float fromX, const float fromY, const float x, const float y)
  {
    griffinput::DraftMove(phase, mLayer.FingerX(fromX), mLayer.FingerY(fromY), mLayer.FingerX(x), mLayer.FingerY(y));
  }

  void GameMouse::DraftAim(const int phase, const float x, const float y)
  {
    // The loupe beside the aim waits for its design (buildstage/griff-cpp-views-design.md): the plan under the finger shows
    // where it goes.
    griffinput::DraftAim(phase, mLayer.FingerX(x), mLayer.FingerY(y));
  }

  void GameMouse::DraftRest(const float x, const float y, const int rest)
  {
    griffinput::DraftRest(mLayer.FingerX(x), mLayer.FingerY(y), rest);
  }

  void GameMouse::DraftBox(const int phase, const float x0, const float y0, const float x1, const float y1)
  {
    if (phase == TouchInput::kDraftShow) {
      griffshown::Set(
        griffshown::kBox,
        {static_cast<float>(mLayer.PictureX(x0)), static_cast<float>(mLayer.PictureY(y0)),
         static_cast<float>(mLayer.PictureX(x1)), static_cast<float>(mLayer.PictureY(y1))}
      );
    } else {
      griffshown::Set(griffshown::kBox, {});
    }
    if (phase == TouchInput::kDraftEnd) {
      griffinput::DraftBox(mLayer.FingerX(x0), mLayer.FingerY(y0), mLayer.FingerX(x1), mLayer.FingerY(y1));
    }
  }

  void GameMouse::DraftDrop()
  {
    griffinput::DraftDrop();
  }

  bool GameMouse::Clicking()
  {
    return griffinput::Clicking();
  }

  int GameMouse::ClickRuns()
  {
    return griffinput::ClickRuns();
  }

  void PageMouse::Move(const float x, const float y)
  {
    fingerinput::MouseMove(mLayer.PictureX(x), mLayer.PictureY(y));
  }

  void PageMouse::Press(const float x, const float y)
  {
    fingerinput::PressNear(mLayer.PictureX(x), mLayer.PictureY(y), mLayer.PictureMm(kTapReachMm), mLayer.PictureMm(kTapSmallMm));
  }

  void PageMouse::Release(const float x, const float y)
  {
    fingerinput::TouchButton(touch::kButtonLeft, false, mLayer.PictureX(x), mLayer.PictureY(y));
  }

  void PageMouse::Tap(const float x, const float y)
  {
    fingerinput::Tap(mLayer.FingerX(x), mLayer.FingerY(y), mLayer.PictureMm(kTapReachMm), mLayer.PictureMm(kTapSmallMm));
  }

  void PageMouse::RightClick(const float x, const float y)
  {
    fingerinput::RightClickNear(
      mLayer.PictureX(x), mLayer.PictureY(y), mLayer.PictureMm(kTapReachMm), mLayer.PictureMm(kTapSmallMm)
    );
  }

  void PageMouse::LookForList(const float x, const float y)
  {
    mLayer.LookForList(x, y);
  }

  void PageMouse::Scroll(const float dy)
  {
    mLayer.Scroll(dy);
  }

  void PageMouse::Held()
  {
    platform::android::Buzz();
  }

  void PageMouse::Leave()
  {
    mLayer.TimersOf().Stop(kLeaveTimer);
    fingerinput::PointerLeave();
  }

  void PageMouse::LeaveSoon()
  {
    mLayer.LeaveSoon();
  }

  void PageMouse::Stay()
  {
    mLayer.Stay();
  }

  void PageMouse::StartTimer(const Millis milliseconds)
  {
    mLayer.TimersOf().Start(kPageTimer, milliseconds);
  }

  void PageMouse::StopTimer()
  {
    mLayer.TimersOf().Stop(kPageTimer);
  }

  // --- The layer's place in the program ------------------------------------------------------------------------------------

  /// The layer, while the scheme is on and SDL's window is there; made at its first need, as the picture's size and the
  /// screen's millimetres are known then.
  std::unique_ptr<Layer> gLayer;
  fingers::Scheme gScheme = fingers::Scheme::kOff;
  bool gSchemeRead = false;

  /// The scheme the activity chose (FAF_TOUCH), the first time it is asked.
  fingers::Scheme ReadScheme()
  {
    if (!gSchemeRead) {
      gSchemeRead = true;
      const std::string scheme = Environment("FAF_TOUCH");
      gScheme = scheme == "griff" ? fingers::Scheme::kGriff : scheme == "mouse" ? fingers::Scheme::kMouse : fingers::Scheme::kOff;
    }
    return gScheme;
  }

  /// The screen's short side in mm, as the activity measured it (FAF_SCREEN_MM); 0: not known, and the layer cannot measure.
  [[nodiscard]] float ScreenMm()
  {
    const float mm = static_cast<float>(std::atof(Environment("FAF_SCREEN_MM").c_str()));
    return mm > 0 ? mm : 0;
  }

  /// The HUD's factor the activity worked out (/fafre_hud_scale), for before FAF's Lua says its own (FafreSetHud).
  [[nodiscard]] double HudScale()
  {
    fa::vector<fa::string> value;
    if (moho::CFG_GetArgOption("/fafre_hud_scale", 1, &value) && !value.empty()) {
      const double factor = std::atof(value[0].c_str());
      if (factor > 0) {
        return factor;
      }
    }
    return 1;
  }

  /// Whether the layer takes fingers: its scheme is on, and the screen can be measured. Without the measure fingers stay
  /// SDL's mouse, and the report says why.
  [[nodiscard]] bool LayerWanted()
  {
    if (ReadScheme() == fingers::Scheme::kOff) {
      return false;
    }
    static const bool measured = [] {
      const bool known = ScreenMm() > 0;
      if (!known) {
        platform::android::Report("fingers: no FAF_SCREEN_MM, so the touch layer cannot measure: fingers stay SDL's mouse");
      }
      return known;
    }();
    return measured;
  }

  /// SDL makes no mouse of a finger while the layer takes fingers: its mouse's place and buttons would follow the first finger,
  /// which the engine reads besides the events (the view's edge scroll, a mouse's held buttons), and a finger lifted on the
  /// glass's edge scrolled the view on. With the layer off fingers are SDL's mouse, as on a desktop. On the program's thread.
  void TouchMouseAsWanted()
  {
    static int said = -1;
    const int wanted = LayerWanted() ? 0 : 1;
    if (wanted != said) {
      said = wanted;
      (void)SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, wanted != 0 ? "1" : "0");
    }
  }

  /// The layer for a finger in `window`, made where it is not yet; null where it takes no fingers.
  [[nodiscard]] Layer* LayerFor(SDL_Window* const window)
  {
    if (!LayerWanted() || window == nullptr) {
      return nullptr;
    }
    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSizeInPixels(window, &width, &height) || width <= 0 || height <= 0) {
      return nullptr;
    }
    if (gLayer == nullptr) {
      gLayer = std::make_unique<Layer>(width, height, ScreenMm(), gScheme == fingers::Scheme::kGriff, HudScale());
      platform::android::Report(
        "fingers: the touch layer takes them, " + std::string(gScheme == fingers::Scheme::kGriff ? "Griff" : "as a mouse") +
        ", on a picture of " + std::to_string(width) + "x" + std::to_string(height) + ", " +
        std::to_string(gLayer->Millimetre()) + " of its pixels a mm"
      );
      // What the program and FAF's Lua said before it: a page is shown as declared from the layer's first finger on.
      gLayer->Read();
    } else {
      gLayer->Picture(width, height);
    }
    return gLayer.get();
  }

  /// Whether a touch device's finger is a finger: not SDL's touch made of a mouse or a pen, and on the screen itself. Android's
  /// virtual device (SDL's -3, from Java's -1) is in SDL's list of touch devices on any phone, so it tells nothing of a
  /// touchscreen there; but what comes from it is fingers all the same, injected ones (adb's input, an accessibility service).
  [[nodiscard]] bool RealTouch(const SDL_TouchID touch)
  {
    if (touch == SDL_MOUSE_TOUCHID || touch == SDL_PEN_TOUCHID) {
      return false;
    }
    return SDL_GetTouchDeviceType(touch) == SDL_TOUCH_DEVICE_DIRECT;
  }

  /// The program and FAF's Lua said something, or a frame was placed (griffinput::SetListener, menufingers::SetListener).
  void Heard()
  {
    if (gLayer != nullptr) {
      gLayer->Read();
    }
  }

  constexpr char kSetTouchHelp[] =
    "FafreSetTouch(scheme) - the touch layer from now on: 'griff', 'mouse' (a finger as the mouse) or 'off' (fingers are SDL's "
    "mouse, as on a desktop). Griff's pieces in FAF's UI come with the next game's UI";

  /// The scheme from FAF's Lua or its console, at run time (fingers::SetScheme).
  int SetTouch(lua_State* const lua)
  {
    const char* const scheme = lua_tostring(lua, 1);
    const std::string name = scheme != nullptr ? scheme : "";
    fingers::SetScheme(name == "griff" ? fingers::Scheme::kGriff : name == "mouse" ? fingers::Scheme::kMouse : fingers::Scheme::kOff);
    return 0;
  }

  moho::CScrLuaBinder gSetTouchBinder(moho::SCR_UserLuaInitSet(), "FafreSetTouch", &SetTouch, nullptr, "<global>", kSetTouchHelp);

  constexpr char kTouchHelp[] =
    "FafreTouch() - the touch layer's scheme ('griff', 'mouse' or 'off') and the screen's short side in mm (0: not known)";

  /// What the bar (bar.lua) is to be: the scheme, and the screen's short side, by which its buttons are sized.
  int Touch(lua_State* const lua)
  {
    const fingers::Scheme scheme = LayerWanted() ? fingers::SchemeNow() : fingers::Scheme::kOff;
    lua_pushstring(lua, scheme == fingers::Scheme::kGriff ? "griff" : scheme == fingers::Scheme::kMouse ? "mouse" : "off");
    lua_pushnumber(lua, ScreenMm());
    return 2;
  }

  moho::CScrLuaBinder gTouchBinder(moho::SCR_UserLuaInitSet(), "FafreTouch", &Touch, nullptr, "<global>", kTouchHelp);

  constexpr char kViewHelp[] =
    "FafreView() - the view the engine draws FAF's UI with, a page's zoom: scale x, scale y, offset x, offset y, the window's "
    "pixels of a UI's x * scale + offset";

  /// What the bar (bar.lua) undoes, to stay where it is on the glass and as large while FAF's page is zoomed.
  int View(lua_State* const lua)
  {
    static constexpr std::array<float, 4> kIdentity{1, 1, 0, 0};
    for (const float number : gLayer != nullptr ? gLayer->UiView() : kIdentity) {
      lua_pushnumber(lua, number);
    }
    return 4;
  }

  moho::CScrLuaBinder gViewBinder(moho::SCR_UserLuaInitSet(), "FafreView", &View, nullptr, "<global>", kViewHelp);

  constexpr char kTouchBarHelp[] =
    "FafreTouchBar(toggle) - turns the bar's toggle 'order', 'delete', 'shift' or 'ctrl' over and says whether it is on now; "
    "it holds while a game has the fingers, and only where a finger is the mouse";

  /// The toggle that Lua's first argument names (Layer::BarToggle), 0 for none; and the layer that has the bar, null for none: a
  /// layer of another scheme holds nothing.
  [[nodiscard]] int BarToggle(lua_State* const lua)
  {
    const char* const name = lua_tostring(lua, 1);
    const std::string toggle = name != nullptr ? name : "";
    return toggle == "order" ? Layer::kBarOrder
      : toggle == "delete"   ? Layer::kBarDelete
      : toggle == "shift"    ? Layer::kBarShift
      : toggle == "ctrl"     ? Layer::kBarCtrl
                             : 0;
  }

  [[nodiscard]] Layer* BarLayer()
  {
    return gLayer != nullptr && !gLayer->Griff() ? gLayer.get() : nullptr;
  }

  /// The light of the bar's button (bar.lua) is what the layer holds.
  int TouchBar(lua_State* const lua)
  {
    const int bit = BarToggle(lua);
    Layer* const layer = BarLayer();
    const bool on = bit != 0 && layer != nullptr && layer->FlipBar(bit);
    lua_pushboolean(lua, on ? 1 : 0);
    return 1;
  }

  moho::CScrLuaBinder gTouchBarBinder(moho::SCR_UserLuaInitSet(), "FafreTouchBar", &TouchBar, nullptr, "<global>", kTouchBarHelp);

  constexpr char kTouchBarHeldHelp[] =
    "FafreTouchBarHeld(toggle) - whether the touch layer holds the bar's toggle 'order', 'delete', 'shift' or 'ctrl' now";

  /// What the bar's button shows each frame (bar.lua): the layer lets go of its toggles when a game ends or the layer goes.
  int TouchBarHeld(lua_State* const lua)
  {
    const int bit = BarToggle(lua);
    Layer* const layer = BarLayer();
    lua_pushboolean(lua, bit != 0 && layer != nullptr && layer->BarHeld(bit) ? 1 : 0);
    return 1;
  }

  moho::CScrLuaBinder
    gTouchBarHeldBinder(moho::SCR_UserLuaInitSet(), "FafreTouchBarHeld", &TouchBarHeld, nullptr, "<global>", kTouchBarHeldHelp);

  /// One of the activity's marks into SDL's queue, behind SDL's events of the same touch event; none before the program's thread
  /// has a type for them, which it has before the first finger.
  bool PushMark(const Sint32 code, const SDL_FingerID finger)
  {
    const Uint32 type = gMarkType.load();
    if (type == 0) {
      return false;
    }
    SDL_Event event{};
    event.type = type;
    event.user.code = code;
    event.user.data1 = reinterpret_cast<void*>(static_cast<std::uintptr_t>(finger));
    return SDL_PushEvent(&event);
  }

  /// The activity said how much the keyboard covers (fingers::KeyboardCovers), on the program's thread.
  void KeyboardTask(std::uintptr_t)
  {
    if (gLayer != nullptr) {
      gLayer->KeyboardAsNow();
    }
  }

  /// The listeners, once: before the first finger, on the program's thread.
  void Listen()
  {
    static bool listening = false;
    if (!listening) {
      listening = true;
      griffinput::SetListener(&Heard);
      menufingers::SetListener(&Heard);
    }
  }
} // namespace

void fingers::SetScheme(const Scheme scheme)
{
  (void)ReadScheme();
  if (scheme == gScheme) {
    return;
  }
  gScheme = scheme;
  // The layer of the scheme before goes with what it held; the next finger makes one of this.
  gLayer.reset();
  TouchMouseAsWanted();
}

void fingers::MoveDone()
{
  if (PushMark(kMarkMoved, 0)) {
    gMovesMarked.store(true);
  }
}

void fingers::Palm(const int pointer)
{
  // SDL's finger is Android's pointer id + 1 (SDL_androidtouch.c, Android_OnTouch).
  (void)PushMark(kMarkPalm, static_cast<SDL_FingerID>(pointer) + 1);
}

void fingers::KeyboardCovers(const float share)
{
  gKeyboardShare.store(share > 0 ? share : 0);
  host::RunNewestOnGameThread(&KeyboardTask, 0);
}

fingers::Scheme fingers::SchemeNow()
{
  return ReadScheme();
}

bool fingers::OnSdlEvent(const SDL_Event& event)
{
  TouchMouseAsWanted();
  if (gMarkType.load() == 0) {
    // Before any finger, on the program's thread: the activity's marks come from now on.
    gMarkType.store(SDL_RegisterEvents(1));
  }
  if (event.type == gMarkType.load()) {
    if (gLayer != nullptr) {
      if (event.user.code == kMarkMoved) {
        gLayer->MovesDone();
      } else if (event.user.code == kMarkPalm) {
        gLayer->Palm(static_cast<SDL_FingerID>(reinterpret_cast<std::uintptr_t>(event.user.data1)));
      }
    }
    return true;
  }
  if (gLayer == nullptr && event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST &&
      LayerWanted()) {
    // The layer with the window, before any finger: Griff's rail and handle come with a game, not with its first touch.
    Listen();
    (void)LayerFor(SDL_GetWindowFromID(event.window.windowID));
  }
  if (event.type == Timers::EventType()) {
    if (gLayer != nullptr) {
      const int name = gLayer->TimersOf().Came(event);
      if (name >= 0) {
        gLayer->Timer(name);
      }
    }
    return true;
  }
  switch (event.type) {
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED: {
      if (!RealTouch(event.tfinger.touchID)) {
        return false;
      }
      Listen();
      Layer* const layer = LayerFor(SDL_GetWindowFromID(event.tfinger.windowID));
      if (layer == nullptr) {
        return false;
      }
      int width = 0;
      int height = 0;
      (void)SDL_GetWindowSizeInPixels(SDL_GetWindowFromID(event.tfinger.windowID), &width, &height);
      const float x = event.tfinger.x * static_cast<float>(width);
      const float y = event.tfinger.y * static_cast<float>(height);
      const Millis time = At(event.tfinger.timestamp);
      if (event.type == SDL_EVENT_FINGER_DOWN) {
        layer->FingerDown(event.tfinger.fingerID, x, y, time);
      } else if (event.type == SDL_EVENT_FINGER_MOTION) {
        layer->FingerMoved(event.tfinger.fingerID, x, y, time);
      } else if (event.type == SDL_EVENT_FINGER_UP) {
        layer->FingerUp(event.tfinger.fingerID, x, y, time);
      } else {
        layer->FingerCanceled(event.tfinger.fingerID, time);
      }
      return true;
    }
    // SDL's mouse made of a finger: the layer has the finger. SDL sends it ahead of the finger's own event. A mouse on a page
    // the UI's view zooms goes back to the UI's pixels (MouseOnPage).
    case SDL_EVENT_MOUSE_MOTION:
      return event.motion.which == SDL_TOUCH_MOUSEID ? LayerWanted() : gLayer != nullptr && gLayer->MouseOnPage(event);
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
      return event.button.which == SDL_TOUCH_MOUSEID ? LayerWanted() : gLayer != nullptr && gLayer->MouseOnPage(event);
    case SDL_EVENT_MOUSE_WHEEL:
      return event.wheel.which == SDL_TOUCH_MOUSEID ? LayerWanted() : gLayer != nullptr && gLayer->MouseOnPage(event);
    case SDL_EVENT_SCREEN_KEYBOARD_SHOWN:
    case SDL_EVENT_SCREEN_KEYBOARD_HIDDEN:
      if (gLayer != nullptr) {
        gLayer->Keyboard(event.type == SDL_EVENT_SCREEN_KEYBOARD_SHOWN);
      }
      return false;
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
      if (gLayer != nullptr) {
        gLayer->Release();
      }
      return false;
    case SDL_EVENT_DID_ENTER_FOREGROUND:
      if (gLayer != nullptr) {
        gLayer->Resume();
      }
      return false;
    default:
      return false;
  }
}
