#pragma once

// Fingers as the mouse the game was made for. What a gesture is shows only after it began, so until then only the cursor's
// place goes to the game:
//
//   - a tap is a click of the left button;
//   - a finger that moves drags with the left button: a selection box, a slider;
//   - a finger that stays presses the right button until it lifts, with Mouse::Held: an order, which the game gives when the
//     button goes up, and moving before that turns a formation;
//   - a quick tap of two fingers is a click of the right button where the second came down;
//   - two fingers that move together drag with the middle button, which moves the game's camera;
//   - two fingers that move apart or closer turn the wheel between them, which zooms it.
//
// With SetSwapped the left and right buttons change places, for giving one order after the other. With SetDeleting one finger
// does nothing but this: a tap deletes the queued order under it.
//
// While a building's ghost waits to be placed (Mouse::Placing), a finger would cover the place it chooses, so one finger moves
// the ghost and only a tap on it places it:
//
//   - a tap on the game's UI is the UI's, such as another building's button;
//   - a tap away from the ghost puts it there;
//   - a tap near the ghost places the building where the ghost is, and with Shift the ghost stays for the next, which a tap
//     puts, even near there, or a finger takes along;
//   - a finger that moves takes the ghost along, from wherever on the screen it landed;
//   - a finger that stays away from the ghost takes it along too, by the smallest steps, for setting it against another
//     building, and whenever it lifts it has placed nothing;
//   - a finger that stays on the ghost and then moves draws a line of buildings from there, with the game's own preview, which
//     go up when it lifts; a second finger gives the line up, and the ghost goes back to where it began; if it lifts without
//     moving, nothing has happened;
//   - a quick tap of two fingers is the right button whatever else is set, which cancels.
//
// Fingers beyond the two a gesture uses are ignored. Positions are the screen's pixels, which Mouse turns into the game's.
//
// A mouse's pointer goes on being somewhere, and the game shows what it is over. A finger's goes, shortly after the last finger
// has lifted (Mouse::LeaveSoon), so that a rollover does not stay in the way of what it opened.
//
// With SetGriff the fingers follow the touch scheme Griff (buildstage/touch-concept-v2.md, the mockup buildstage/touch-preview)
// instead, but for a building's ghost, which they place as above. A finger stays within kGriffSlopMm:
//
//   - a finger that moves pans the camera from where it passed the slop, and glides on when it lifted moving
//     (Mouse::GriffDragEnd); with the rail's Box it draws a selection box at once; on a panel or the minimap it drags as a
//     mouse (Mouse::GriffDrag);
//   - a finger that rests kRestMilliseconds (a ring shows it, Mouse::Ring) tells the program (Mouse::GriffRest), which marks a
//     waypoint of the selection's orders under it, or on the ground with a selection makes the rest the right button's: then
//     moving turns FAF's formation to face the drag and lifting orders it; on a waypoint moving gives the mark up and lifting
//     deletes that order; elsewhere moving draws a selection box. A rest that lifts is FAF's right click on a panel; with Box,
//     which has no rest, Box's tap on the map (Mouse::RestTap);
//   - a tap is FAF's click by what it lands on (Mouse::SmartTap): one's own unit is selected, else the selection gets FAF's
//     right click there; taps soon after each other and near climb a ladder (TapLadder): all of a unit's type on the screen,
//     then on the map, an attack-move in place of a move, all one's own on the screen;
//   - a finger that starts on the handle over the selection (Mouse::OnGriff) draws a route, whose waypoints go to Mouse::Route
//     when it lifts, counted where the finger was a moment before; one that lifts where it began was a tap on the handle, but
//     after the handle's rest (kRestMilliseconds) FAF's Distribute orders where the program said its selection can
//     (Distributes: then a ring and the label show, and a move past the slop is the route still); a long loop back to the
//     handle is a patrol, a short one gives the route up, as does a second finger once the first has left the slop, before
//     which the two are two fingers. Where the ladder may climb, the handle leaves the finger's quick lift to it, which is the
//     handle's tap where it does not climb after all; a rest of that finger or a move past the slop is the handle's, from
//     where it came down;
//   - two fingers zoom at their middle and move the camera with it (Mouse::Pinch), and the one left of them moves it on; a
//     second finger on a pan, once the program has said it is one (Panned), makes two of it, as does one that came before the
//     word, at the word; a quick tap of two lets the selection go;
//   - the rail's buttons (Mouse::Rail) count for the next action, or while held (Rail says what an action uses); for a tap or
//     a rest Box as it counted when the finger came down, as FAF's reach for a unit beside the finger was set for it then
//     (Mouse::GriffDown).
//
// While Griff's draft is open (Mouse::Drafting: a build icon picked a building, buildstage/griff-v3-spec.md T2) a finger makes
// plans instead, and there is no handle:
//
//   - a tap places a plan where it came down, or removes the plan it is on (Mouse::DraftTap);
//   - a drag from the newest plan draws a line of them (Mouse::DraftLine), from another plan moves the whole draft
//     (Mouse::DraftMove), with Box and an extractor picked is a box over deposits (Mouse::DraftBox), else it pans; a second
//     finger gives the line, the move or the box up and the two pinch;
//   - a finger that rests on FAF's UI is the panel's rest there, as outside the draft but using none of the rail, once the
//     program has said at Griff's rest that it is there (DraftRestOnUi); on the map, kDraftRestMilliseconds on no plan and
//     without Box, it shows the plan its lift places under it and a loupe (Mouse::DraftAim);
//   - a quick tap of two drops the draft (Mouse::DraftDrop).
//
// While the program's own clicks run (Mouse::Clicking: the draft's tick, the undo's), a finger that comes down rests and gives
// nothing, as its gesture would act between them. A gesture they came into is given up: nothing more of it goes to the game,
// also once they are over (Mouse::ClickRuns), as it would act on what they did.

#include <array>
#include <limits>
#include <numbers>

#include "touch/Fingers.h"

namespace touch
{
  /// The mouse's buttons as the host numbers them (host::Button).
  constexpr int kButtonLeft = 0;
  constexpr int kButtonRight = 1;
  constexpr int kButtonMiddle = 2;
  /// A wheel's notch, as Windows counts it.
  constexpr int kWheelDelta = 120;

  class TouchInput final : public Fingers
  {
  public:
    /// What the fingers do, on the thread that calls this class.
    class Mouse
    {
    public:
      virtual ~Mouse() = default;

      virtual void Move(float x, float y) = 0;
      /// `button`: kButtonLeft and the rest. A press where a building's ghost waits only puts the ghost there, whatever this
      /// class knows of the ghost by then, which may be behind the game.
      virtual void Button(int button, bool down, float x, float y) = 0;
      /// A tap, a click of the left button, which the game's UI may give to a small control beside it.
      virtual void Tap(float x, float y) = 0;
      /// A tap near a building's ghost: it places the building where the ghost is, unless the game's UI is there or under the
      /// finger, and is a tap at the finger then.
      virtual void TapNearGhost(float fingerX, float fingerY, float ghostX, float ghostY) = 0;
      /// A line of buildings begins here, where the ghost is: the left button as a mouse presses it, which Button lets go.
      /// The game draws the line to wherever the cursor goes until then.
      virtual void StartLine(float x, float y) = 0;
      /// Gives up the line begun with StartLine, with nothing built.
      virtual void CancelLine() = 0;
      /// `delta` in kWheelDelta's units, positive towards the screen.
      virtual void Wheel(int delta, float x, float y) = 0;
      /// A finger stayed: it does something else now, which the user should feel.
      virtual void Held() = 0;
      /// Whether a building's ghost waits to be placed.
      [[nodiscard]] virtual bool Placing() = 0;
      /// Deletes the queued order nearest to the cursor, which has come here, unless the game's UI is here.
      virtual void DeleteOrder(float x, float y) = 0;
      /// In kLeaveMilliseconds the pointer leaves what it is over.
      virtual void LeaveSoon() = 0;
      /// A finger came: the pointer stays, whatever LeaveSoon said, also if the leave is on its way to the game.
      virtual void Stay() = 0;
      /// Calls Timer after this long, in place of the time set before.
      virtual void StartTimer(Millis milliseconds) = 0;
      virtual void StopTimer() = 0;

      /// Griff: a first finger came down here: on the map or the minimap the camera's glide stops, and FAF's reach for a unit
      /// beside a tap is set for what is selected (`box`: the rail's Box counts, which picks units).
      virtual void GriffDown(float x, float y, bool box) = 0;
      /// Griff: a tap that the program decides by what is under it, where the finger was and the cursor has rested, or by the
      /// ladder's `step` (kStep): kStepTap is decided by its place, as Tap on the game's UI; one's own unit is selected (with
      /// kRailBox added to the selection or taken out); else the selection gets FAF's right click there (kRailQueue with
      /// Shift). The other steps act at the ladder's first tap (`baseX`, `baseY`). The program answers a kStepTap with what it
      /// was (TapAnswered, by `tap`), and with Box counting, with the rail's buttons it used.
      virtual void SmartTap(float x, float y, int rail, int step, float baseX, float baseY, int tap) = 0;
      /// Griff: the ring of a finger that rests, shown about this place or gone.
      virtual void Ring(bool shown, float x, float y) = 0;
      /// Griff: no unit is selected any more.
      virtual void Deselect() = 0;
      /// Griff: whether the handle over the selection is under this place.
      [[nodiscard]] virtual bool OnGriff(float x, float y) = 0;
      /// Griff: a tap on the handle, which takes the camera to the selection.
      virtual void GriffTap() = 0;
      /// Griff: the handle's rest number `rest` (kRestMilliseconds on the handle, within the slop): the program says whether
      /// its lift gives FAF's Distribute orders (Distributes).
      virtual void HandleRest(int rest) = 0;
      /// Griff: the lift of a handle's rest that the program said distributes: FAF's Distribute orders, its key action G.
      virtual void Distribute() = 0;
      /// Griff: a route from the handle, `count` waypoints, each FAF's right click there, the last on what is there; all but
      /// the first with Shift, the first too with kRailQueue; with FAF's attack-move armed attack-moves. `patrol`: a loop, the
      /// waypoints FAF's patrol where it offers one.
      virtual void Route(const float* xs, const float* ys, int count, int rail, bool patrol) = 0;
      /// Griff: the stroke as far as it goes, for the screen: its points (count 0: none, and the handle is free again), what
      /// lifting now gives (stroke::kRoute, kPatrol or kCancel; kDistribute after the handle's rest, with no waypoints) and the
      /// waypoints it would order.
      virtual void StrokeShown(
        const float* xs,
        const float* ys,
        int count,
        int mode,
        const float* waypointXs,
        const float* waypointYs,
        int waypoints
      ) = 0;
      /// Griff: the cursor hovers here for the stroke, so that the game shows and says what its end is on.
      virtual void StrokeHover(float x, float y) = 0;
      /// Griff: a stroke or FAF's formation was given up before it gave anything, which the user should see.
      virtual void StrokeGivenUp() = 0;
      /// Griff: the rail's buttons that count now, kRail bits.
      [[nodiscard]] virtual int Rail() = 0;
      /// Griff: an action used these of the rail's buttons: one that was on for the next action goes off.
      virtual void RailUsed(int rail) = 0;
      /// Griff: a drag begins where the finger came down (`rested`: after a rest), which the program makes what the place
      /// wants: on the map a pan of the camera, from where the finger is now (`nowX`, `nowY`); with kRailBox, or after a rest
      /// that found neither a waypoint nor the ground for a formation, a selection box, with kRailQueue adding to the
      /// selection; after a rest on the ground FAF's formation, the right button's drag; after one on a waypoint nothing; on a
      /// panel or the minimap the left button's drag, as a mouse's. The pointer's moves follow, and GriffDragEnd ends it. The
      /// program says by its number `drag` when it made a pan of it (Panned).
      virtual void GriffDrag(float x, float y, float nowX, float nowY, bool rested, int rail, int drag) = 0;
      /// Griff: what GriffDrag pressed goes up here; a pan's camera glides on at this velocity (pixels a second).
      virtual void GriffDragEnd(float x, float y, float velocityX, float velocityY) = 0;
      /// Griff: what GriffDrag pressed goes without a finger's lift, here: FAF's formation is given up, not ordered, as a route
      /// is that no finger lifted; any other drag ends as at a lift, without a pan's glide.
      virtual void GriffDragGiveUp(float x, float y) = 0;
      /// Griff: a rest without a drag: on a panel FAF's right click there (kRailQueue with Shift); on the map what the rest
      /// found (GriffRest): its waypoint deleted, or the ground's right click, else nothing. With kRailBox it was no rest, and
      /// on the map it is Box's tap; with Box the program says which of the rail's buttons it used.
      virtual void RestTap(float x, float y, int rail) = 0;
      /// Griff: a finger rests here, without Box: the program marks a waypoint of the selection's orders under it for
      /// deletion, or on the ground with a selection makes the rest the right button's, for a formation's drag or the lift's
      /// click. The rest's gesture goes on as GriffDrag, RestTap or GriffRestGone.
      virtual void GriffRest(float x, float y) = 0;
      /// Griff: the resting finger's gesture went without its lift or a drag: a second finger, the system, the clicks.
      virtual void GriffRestGone() = 0;
      /// Griff: the camera follows fingers: `start` takes what lies at this place as what stays under it; then the camera
      /// keeps it there, zoomed by how far apart two fingers are now from when they started (`distance` 0: one finger, which
      /// only moves it).
      virtual void Pinch(bool start, float x, float y, float distance) = 0;
      /// Griff: the fingers have left the camera, which glides on at this velocity (pixels a second).
      virtual void PinchEnd(float velocityX, float velocityY) = 0;
      /// Griff: whether the draft is open, as the program says now: a finger that comes down makes plans.
      [[nodiscard]] virtual bool Drafting() = 0;
      /// Griff: the number of the draft's plan a finger here is on as the screen shows it, the newest first; 0: none.
      [[nodiscard]] virtual int DraftPlan(float x, float y) = 0;
      /// Griff: whether the plan numbered `plan` is the draft's newest, from which a drag draws a line.
      [[nodiscard]] virtual bool DraftNewest(int plan) = 0;
      /// Griff: whether the plan numbered `plan` stands on a deposit: it neither starts a line nor moves with the draft.
      [[nodiscard]] virtual bool DraftOnDeposit(int plan) = 0;
      /// Griff: whether the draft's building stands on a deposit: with Box a drag is a box over deposits.
      [[nodiscard]] virtual bool DraftPicksDeposit() = 0;
      /// Griff: a tap in the draft where the finger came down, on the plan numbered `plan` (0: none): the program removes that
      /// plan, places one there, or on FAF's UI gives the UI's click, with the tap help; a lit Plus stays for the tick (a held
      /// Plus is Shift as ever).
      virtual void DraftTap(float x, float y, int plan) = 0;
      /// Griff: the line from the newest plan to here: shown, added where the finger lifted, or given up (kDraft).
      virtual void DraftLine(int phase, float x, float y) = 0;
      /// Griff: the whole draft follows a finger from where it came down to here: shown, kept, or put back (kDraft).
      virtual void DraftMove(int phase, float fromX, float fromY, float x, float y) = 0;
      /// Griff: a finger rests here in the draft: the plan its lift places and the loupe shown, that plan placed, or gone.
      virtual void DraftAim(int phase, float x, float y) = 0;
      /// Griff: a finger in the draft rests here, Griff's rest: the program says whether it is on FAF's UI, by `rest`
      /// (DraftRestOnUi), where it is the panel's rest as outside the draft.
      virtual void DraftRest(float x, float y, int rest) = 0;
      /// Griff: Box over deposits from one corner to the other: shown as the finger moves, given where it lifted, or gone.
      virtual void DraftBox(int phase, float x0, float y0, float x1, float y1) = 0;
      /// Griff: two fingers' tap in the draft: it goes, with nothing ordered.
      virtual void DraftDrop() = 0;
      /// Griff: whether the program's clicks run, the draft's tick's or the undo's, as the program says now: a finger that
      /// comes down then rests, as its gesture would act between those clicks.
      [[nodiscard]] virtual bool Clicking() = 0;
      /// Griff: how many runs of the program's clicks have begun: a gesture that one began in is given up, as the program's
      /// clicks take FAF's mode, the selection, Shift and the cursor from its first click on.
      [[nodiscard]] virtual int ClickRuns() = 0;
    };

    /// Griff: the rail's buttons, as Mouse::Rail has them. FAF's orders panel has the attack-move.
    static constexpr int kRailBox = 1;
    static constexpr int kRailQueue = 2;
    /// Griff: a tap's step on the ladder (Mouse::SmartTap).
    static constexpr int kStepTap = 0;
    /// The ladder's first click on the game's UI again, which the game makes its double click there.
    static constexpr int kStepAgain = 1;
    /// All of the selected type on the map (FAF's key action select_all_units_of_same_type).
    static constexpr int kStepAllOfType = 2;
    /// An attack-move at the first tap's place, in place of its move.
    static constexpr int kStepAttackMove = 3;
    /// All of one's own mobile units on the screen.
    static constexpr int kStepAllMobile = 4;
    /// All of the selected unit's type on the screen (FAF's key action select_all_onscreen for the type), as the game's double
    /// click on it: the first tap's selection brings FAF's panels up, which can cover where it was.
    static constexpr int kStepTypeInView = 5;
    /// The group the first tap's icon selected, again, and the camera to it, as a control group's double click (the program
    /// knows the group by the first tap).
    static constexpr int kStepGroupAgain = 6;
    /// Griff: a draft gesture's phase (Mouse::DraftLine and the others).
    static constexpr int kDraftShow = 0;
    static constexpr int kDraftEnd = 1;
    static constexpr int kDraftAbort = 2;
    /// Griff: what a kStepTap was, as the program answers it (Mouse::SmartTap); 0 until it has.
    static constexpr int kTapUi = 1;
    static constexpr int kTapOwn = 2;
    static constexpr int kTapGround = 3;
    static constexpr int kTapEmpty = 4;
    static constexpr int kTapOther = 5;
    /// A group's icon, which selected the group.
    static constexpr int kTapGroup = 6;
    /// Griff: how long a finger rests before its ring shows and the program hears of the rest: its drag is no pan then, but a
    /// formation, a box, or the mark of a waypoint given up.
    static constexpr Millis kRestMilliseconds = 300;
    /// Griff: how long a finger rests on the map in the draft before the plan it would place shows under it, with the loupe.
    static constexpr Millis kDraftRestMilliseconds = 400;
    /// Griff: how far a finger may wander and still stay, for a tap, a rest and a stroke's tap on the handle (the mockup's).
    static constexpr float kGriffSlopMm = 2.5f;
    /// Griff: the tap ladder's next tap, this soon after the last one's lift and this near it.
    static constexpr Millis kLadderMilliseconds = 350;
    static constexpr float kLadderMm = 6;
    /// Griff: two fingers whose distance, middle and each finger's own place since the two began move less than this are still
    /// a tap.
    static constexpr float kTwoFingersStillMm = 1.5f;
    /// Griff: a lift counts where the finger was this long before it: fingers wobble as they lift.
    static constexpr Millis kSettleLiftMilliseconds = 50;
    /// Griff: the camera glides on only after a move this soon before the lift, at this much of the finger's speed.
    static constexpr Millis kGlideMilliseconds = 70;
    static constexpr float kGlideSpeed = 0.9f;
    /// Griff: the cursor hovers at a stroke's end at most this often.
    static constexpr Millis kHoverMilliseconds = 100;

    /// How long a finger stays before it holds the button.
    static constexpr Millis kHoldMilliseconds = 350;
    /// How long two fingers may stay for a tap: with Griff from the first of them.
    static constexpr Millis kTwoFingerTapMilliseconds = 350;
    /// How long the cursor rests before a click. The game looks for what is under the cursor once a frame, and a quick tap
    /// can be over before a slow frame has seen where it landed.
    static constexpr Millis kSettleMilliseconds = 120;
    /// How long the pointer stays after the last finger: a click is yet to come (kSettleMilliseconds), and frames to see it.
    static constexpr Millis kLeaveMilliseconds = 300;
    /// Two clicks this close, and near each other, are the game's double click.
    static constexpr Millis kDoubleTapMilliseconds = 400;

    /// Where a click goes. The game's double click wants the same few pixels, which two taps of a finger do not hit, so a
    /// click soon after the last and near it goes where that went.
    class ClickPlace
    {
    public:
      /// A click here now; whether it comes soon after the last and near it.
      bool ClickedAt(float x, float y, Millis time, float slop);
      /// Whether a click here now would be the game's double click with the last.
      [[nodiscard]] bool Recent(float x, float y, Millis time, float slop) const;

      [[nodiscard]] float X() const
      {
        return mX;
      }

      [[nodiscard]] float Y() const
      {
        return mY;
      }

    private:
      Millis mTime = kLongAgo;
      float mX = 0;
      float mY = 0;
    };

    /// Griff's tap ladder (the mockup's tapA, nextTapCount): a tap within kLadderMilliseconds of the last tap's lift and
    /// kLadderMm of it climbs a rung, by what the ladder's first tap was, whatever lies under this one: after a tap that
    /// selected one's own unit, all of its type on the screen, then on the map; after a plain move, an attack-move there;
    /// after a tap on nothing with nothing selected, all one's own mobile units on the screen; after a tap on a group's icon,
    /// the group again with the camera to it, as a control group's double click. The last three end the ladder. A tap on the
    /// game's UI is clicked again where the first went, as the game's double click wants it, within its own reach and time.
    /// Plus and Box keep a tap off the ladder, as do a drag, a rest, a stroke and two fingers.
    class TapLadder
    {
    public:
      /// The step of a tap here, lifted at `lift`, with these of the rail's buttons counting; it is the last tap from now on.
      /// `reach`: kLadderMm in pixels; `uiReach`: how near the UI's double click wants the second click.
      int Next(float x, float y, Millis lift, int rail, float reach, float uiReach);
      /// The program says what the ladder's first tap, number `tap`, was.
      void Answered(int tap, int kind);
      /// Whether a finger down here at `time`, with these of the rail's buttons counting, may become the ladder's next tap,
      /// which the handle then leaves to it: so a double tap on a unit does not land on the handle its first tap raised. Also
      /// while the program has not said what the last tap was. After the third rung there is nothing to climb, and Box and Plus
      /// keep a tap off the ladder, as Next has it.
      [[nodiscard]] bool Climbing(float x, float y, Millis time, int rail, float reach) const;

      /// Another gesture came between: the next tap starts a ladder.
      void Clear()
      {
        mLast = false;
      }

      [[nodiscard]] float BaseX() const
      {
        return mBaseX;
      }

      [[nodiscard]] float BaseY() const
      {
        return mBaseY;
      }

      [[nodiscard]] int Tap() const
      {
        return mTap;
      }

    private:
      /// Box and Plus make a tap theirs: it climbs no ladder.
      [[nodiscard]] static bool OffLadder(int rail);

      /// Whether there is a last tap to climb from.
      bool mLast = false;
      Millis mLift = 0;
      float mX = 0;
      float mY = 0;
      /// The ladder's first tap: where it was, what it was (kTap, 0 until the program says), its rung and its number.
      float mBaseX = 0;
      float mBaseY = 0;
      int mKind = 0;
      int mRung = 0;
      int mTap = 0;
    };

    /// `slop`: how far a finger may wander in the screen's pixels and still count as staying.
    TouchInput(Mouse& mouse, float slop);
    TouchInput(const TouchInput&) = delete;
    TouchInput& operator=(const TouchInput&) = delete;

    /// The touch scheme Griff in place of the mouse, for the gestures that begin after this.
    void SetGriff(bool griff);
    /// The screen's pixels in a mm on the glass, by which Griff measures its gestures.
    void SetMillimetre(float pixels);
    /// How far a finger may wander and still stay, in the screen's pixels, when their size changes.
    void SetSlop(float slop);
    void SetSwapped(bool swapped);
    void SetDeleting(bool deleting);
    /// The screen's size in its pixels, which a ghost does not leave.
    void SetScreen(float width, float height);

    /// Griff: the program says what tap number `tap` was (kTap), for the ladder.
    void TapAnswered(int tap, int kind);
    /// Griff: what the last taps ordered was taken back (the undo's button): the next tap climbs no ladder from it.
    void ClearLadder();
    /// Griff: the program made a pan of drag number `drag`, which only it can tell from a panel's drag or the minimap's: a
    /// second finger turns that drag into two fingers, and so does one that came before the word, now (`time`, as a
    /// finger's). A word for an earlier drag counts for nothing.
    void Panned(int drag, Millis time);
    /// Griff: the program made FAF's formation of drag number `drag`, after a rest on the ground: a second finger gives it up,
    /// nothing ordered, and so does one that came before the word, now. A word for an earlier drag counts for nothing.
    void Formation(int drag);
    /// Griff: FAF stored a control group (a right press on its slot): whether the phone buzzes for it now, so that each stored
    /// group is felt once, as the mockup's. It is the last rest's, which buzzed as it rang, unless Box counted: that gives a
    /// rest no ring, but on a panel leaves its lift FAF's right press. A rest's ring counts for one store, so one that no rest
    /// made after it, a mouse's right click, buzzes.
    bool StoreBuzzes();
    /// Griff: the program says that the lift of the handle's rest number `rest` gives FAF's Distribute orders: its ring shows
    /// and buzzes and the label above the finger says FAF's own words, while the finger stays within the slop. A word for an
    /// earlier rest, or after the finger left the slop or lifted, counts for nothing; with none the lift is the handle's tap.
    void Distributes(int rest);
    /// Griff: the program says that the finger of the draft's rest number `rest` is on FAF's UI: its rest is the panel's, as
    /// outside the draft, whose lift is FAF's right click there and whose slide the panel's drag, but neither uses the rail, as
    /// the draft's tap there does not: a lit Plus stays for the tick. A word that comes after the finger moved, lifted or began
    /// its aim counts for nothing.
    void DraftRestOnUi(int rest);

    void Down(int id, float x, float y, Millis time, bool first) override;
    void Move(int id, float x, float y) override;
    void Moved(Millis time) override;
    void Up(int id, Millis time, bool last) override;
    /// The system says that a finger which lifted was a palm, with another still down: it is as if it had not come down,
    /// when nothing has shown yet of what the two do.
    void Forget(int id, Millis time) override;
    /// The system took the fingers. What is held goes up, as a mouse's would, so a right button that a finger held gives its
    /// order; no gesture comes of what is left of them. A line builds nothing that the finger did not finish. Griff orders
    /// only where a finger lifted: its formation goes, as its route.
    void Cancel() override;
    /// The time from Mouse::StartTimer has passed.
    void Timer();

  private:
    enum class State
    {
      /// No finger.
      kNone,
      /// One finger, and nothing shown yet of what it does.
      kPending,
      /// One finger holds `mPressed`.
      kPressed,
      /// One finger, a building's ghost waiting, and nothing shown yet of what the finger does.
      kPlacePending,
      /// One finger takes the ghost along.
      kPlaceDragging,
      /// One finger stayed on the ghost: when it moves, it draws a line of buildings from there.
      kPlaceLineReady,
      /// One finger draws a line of buildings.
      kPlaceLine,
      /// Griff: one finger rested; when it moves, it draws a selection box.
      kRested,
      /// Griff: one finger draws a route from the handle over the selection.
      kStroke,
      /// Griff's draft: one finger, and nothing shown yet of what it does.
      kDraftPending,
      /// Griff's draft: one finger draws a line of plans from the newest.
      kDraftLine,
      /// Griff's draft: one finger moves the whole draft.
      kDraftMove,
      /// Griff's draft: one finger rested on no plan, and the plan its lift places shows under it.
      kDraftAim,
      /// Griff's draft: one finger draws a box over deposits.
      kDraftBox,
      /// Two fingers, and nothing shown yet of what they do.
      kTwoPending,
      kPanning,
      kZooming,
      /// Griff: two fingers move and zoom the camera.
      kPinching,
      /// Griff: one finger left of a pinch moves the camera on.
      kPinchPan,
      /// Fingers left of a gesture that is over.
      kSpent,
    };

    struct Finger
    {
      int id = -1;
      float x = 0;
      float y = 0;

      void Place(const float newX, const float newY)
      {
        x = newX;
        y = newY;
      }
    };

    /// Griff: a route's waypoints, about one per this many mm, and its corners within this; a stroke's points this far apart.
    static constexpr float kWaypointMm = 15;
    static constexpr float kCornerMm = 2.5f;
    static constexpr float kStrokePointMm = 1;
    static constexpr int kStrokePoints = 512;
    static constexpr int kMaxWaypoints = 32;
    /// Griff: the moves that the settled lift and a stroke's trim to it look back over (the mockup's).
    static constexpr int kTrail = 8;
    static constexpr int kTrimPoints = 12;
    /// Griff: how much of the last velocity each move keeps.
    static constexpr float kVelocityKept = 0.6f;
    /// How far above the finger, in slops, a ghost starts that nothing has put anywhere yet.
    static constexpr float kGhostAboveSlops = 12;
    /// How far from a ghost, in slops, a tap is still a tap on it.
    static constexpr float kGhostReachSlops = 6;
    /// The wheel for two fingers twice as far apart: cam_ZoomAmount's default is 0.4 notches' worth.
    static constexpr double kWheelPerDoubling = kWheelDelta / 0.4;
    static constexpr double kLog2 = std::numbers::ln2;
    /// What a waiting click does instead of a button.
    static constexpr int kNoClick = -1;
    static constexpr int kDeleteOrder = -2;
    static constexpr int kGriffTap = -3;
    static constexpr int kDraftTap = -4;

    [[nodiscard]] int TapButton() const;
    [[nodiscard]] int HoldButton() const;
    /// How far a finger may wander and still stay: Griff's own for its gestures, the system's for the mouse's.
    [[nodiscard]] float Slop() const;
    /// Griff: whether a second finger waits for the program's word on the first one's drag, which goes on: the word takes it.
    /// A drag the program's clicks came into is given up instead, and the finger with it.
    bool TakeWaiting();
    void StartTwo(int id, float x, float y, Millis time);
    /// Griff: a second finger turns a pan into two fingers, which move the camera themselves. Only a drag that the program has
    /// said is a pan: on a panel or the minimap the left button drags, and the map is not the fingers' to pinch. As the first
    /// finger travelled, their lift is no tap of two.
    void TwoOfPan(int id, float x, float y, Millis time);
    /// Griff: a second finger gives FAF's formation up, nothing ordered, as it gives a route up; both rest until they lift.
    void GiveUpFormation();
    void StartOneFinger(int id, float x, float y, Millis time);
    void StartPending(int id, float x, float y, Millis time);
    /// Griff: a finger came down in the draft, maybe on a plan; taps do not climb the ladder.
    void StartDraft(int id, float x, float y, Millis time);
    /// Griff: a finger came down on the handle. Nothing goes to the game until it lifts but the cursor's hover.
    void StartStroke(int id, float x, float y, Millis time);
    /// Griff: the stroke's finger rested on the handle: the program says whether its lift gives FAF's Distribute orders.
    void RestOnHandle();
    /// Griff: the stroke's finger left the slop: no rest on the handle any more, and what one showed goes. Whether the ring and
    /// the label went, so that the screen says the route's meaning now.
    bool LeaveHandleRest();
    /// Griff: the finger the handle left to the ladder (mOnHandle) rests or leaves the slop: only a lift climbs the ladder, and
    /// the finger is the handle's from where it came down, as one that came after the ladder's time. It stays where it is.
    void TakeForHandle();
    void StartPlacing(int id, float x, float y, Millis time);
    /// A gesture goes on: a finger is down, and what it does is not over.
    [[nodiscard]] bool Live() const;
    /// Griff: whether the program's clicks came into the gesture: they run, or one of their runs began since its first finger
    /// came down. They take FAF's mode, the selection, Shift and the cursor for themselves, and the gesture's input would come
    /// between their clicks, or act on what they did once they are over (a tap after the tick's clicks would give its builders
    /// another order). The layer may hear of them a moment after they began; what a finger gave in that moment the program
    /// drops.
    [[nodiscard]] bool ClicksCame();
    /// Griff: the program's clicks came into the gesture (ClicksCame): it is given up, and nothing more of it goes to the game;
    /// its fingers rest until they lift. Only what shows of it goes: the ring, the stroke (Cancelled, as a stroke given up),
    /// the draft's line, move, aim or box as a second finger gives them up, and a pinch lets the camera go. A line of FAF's
    /// ghost the clicks gave up as they began, as its own cancel does, and the ghost's mode they took. A tap that waits goes
    /// too.
    void GiveUpForClicks();
    void EndGesture();
    /// Griff: the first finger rests (kRestMilliseconds). With Box a rest is none (the mockup's): no ring, nothing to feel, and
    /// its lift is Box's tap on the map. Else the program hears of it: a waypoint under it is marked, or the ground gives the
    /// right button.
    void StartRest();
    void BringCursorBackToGhost();
    void StartTakingGhost();
    /// On the screen, or the ghost would wait there for the finger to come all the way back.
    void MoveGhost();
    [[nodiscard]] bool StartedOnGhost() const;
    void StartLine();
    void GiveUpLine();
    /// The game takes a finger's press to put the ghost there, unless it lands on its UI.
    void TapWhilePlacing();
    void Press(int button, float x, float y);
    /// Griff: a drag from where the finger came down, which the program makes a pan, a box or a panel's drag, with these of
    /// the rail's buttons counting for it. A pan starts where the finger passed the slop, so the map does not jump by it.
    void StartGriffDrag(bool rested, Millis time, int rail);
    void StartGriffDrag(bool rested, Millis time);
    /// Griff: a rest and no drag: on a panel the right click; on the map nothing, as the finger changed its mind. Either way the
    /// rail counted for it (Rail), but Plus not for the draft's rest on FAF's UI (mRestInDraft). With Box it was no rest, and
    /// on the map it is Box's tap, of which the program says what it used.
    void RestTap();
    /// Griff: the rail's buttons that count for the first finger's tap or rest now, Box as it counted at the finger's down.
    [[nodiscard]] int FingerRail();
    /// `glide`: the finger lifted, and a pan that moved until then glides on.
    void ReleasePressed(bool glide, Millis time);
    void HideRing();
    /// Griff: a rest the program heard of goes without its lift or a drag: what it found is over (a waypoint's mark).
    void RestGone();
    /// Griff: a finger in the draft passed the slop (the mockup's draftMoveA): with Box and a building on deposits a box over
    /// them, from wherever it came down; from the newest plan a line; from another plan the whole draft moves; from nothing,
    /// or from a plan on a deposit, it pans, whatever the rail says.
    void StartDraftDrag(Millis time);
    /// Griff: the finger of a draft gesture lifted: what it gives counts where the finger was a moment before.
    void FinishDraftGesture(Millis time);
    /// Griff: a draft gesture given up, by a second finger or the system: nothing comes of it, a move puts the plans back.
    void GiveUpDraftGesture();
    void StartTrail(float x, float y, Millis time);
    void AddTrail(Millis time);
    void Trail(float x, float y, Millis time);
    /// Where a finger lifting at `time` counts as having lifted (the mockup's settle): its slot in the trail, the newest sample
    /// at least kSettleLiftMilliseconds before `time`, else the oldest kept.
    [[nodiscard]] int Settled(Millis time) const;
    void StartFollowing(Finger& finger, Millis time);
    /// The followed finger's velocity, smoothed over its moves (the mockup's panMove).
    void Follow(Millis time);
    void StartPinch();
    void StartPinchPan(Finger& left, Millis time);
    /// `glide`: the last finger lifted, and the camera glides on if it moved until then.
    void EndPinch(bool glide, Millis time);
    void AddStrokePoint(float x, float y);
    /// Griff: the stroke follows the finger, a point a mm once it has left the handle's middle by one; the screen shows it.
    void StrokeTo(float x, float y, Millis time);
    /// The cursor at the stroke's end, at most each kHoverMilliseconds, and the last place when the time has come.
    void Hover(float x, float y, Millis time);
    [[nodiscard]] int StrokeWaypoints();
    void ShowStroke();
    void HideStroke();
    void GiveUpStroke();
    /// Cuts the stroke back to its point nearest the settled lift (the mockup's strokeTrimTo), which takes its place.
    void TrimStrokeTo(float x, float y);
    /// Griff: the finger lifted from its stroke: within the slop FAF's Distribute orders where the handle's rest said so, else a
    /// tap on the handle; or what the stroke gives where the finger was a moment before: a route, a patrol by a loop, or
    /// nothing for a loop taken back.
    void FinishStroke(Millis time);
    /// A click `rested` milliseconds after the cursor came there, now or when it has settled.
    void Click(int button, float x, float y, Millis rested, Millis time);
    void FinishClick();
    void StartPanning();
    void StartZooming();
    void Zoom();
    [[nodiscard]] float MiddleX() const;
    [[nodiscard]] float MiddleY() const;
    [[nodiscard]] float Distance() const;

    Mouse& mMouse;
    float mSlop;
    State mState = State::kNone;
    bool mSwapped = false;
    bool mDeleting = false;
    Finger mFirst;
    Finger mSecond;
    /// Where the gesture began: the first finger's place, or the point between two.
    float mStartX = 0;
    float mStartY = 0;
    float mStartDistance = 0;
    Millis mStartTime = 0;
    /// When the gesture's first finger came down.
    Millis mFirstDown = 0;
    /// Where the second finger came down, and where the first was then.
    float mSecondX = 0;
    float mSecondY = 0;
    float mFirstAtTwoX = 0;
    float mFirstAtTwoY = 0;
    int mPressed = 0;
    float mWheelDistance = 0;
    double mWheelRemainder = 0;
    /// Where the fingers were when the zoom began: moving on from there together makes it a pan.
    float mZoomX = 0;
    float mZoomY = 0;
    float mZoomDistance = 0;
    /// Where a building's ghost is, once a finger has put it somewhere.
    bool mGhostPlaced = false;
    float mGhostX = 0;
    float mGhostY = 0;
    /// A building went up where the ghost is, and neither has moved since: a tap there would only place it again.
    bool mGhostUsed = false;
    /// Where the finger that takes the ghost along last was.
    float mGhostFingerX = 0;
    float mGhostFingerY = 0;
    /// Where the ghost was when the line began, for a line given up.
    float mLineStartX = 0;
    float mLineStartY = 0;
    /// Two fingers came down over a ghost, and took the cursor from it.
    bool mGhostToRestore = false;
    /// A click that waits for the cursor to settle: kNoClick, or what it does.
    int mClickButton = kNoClick;
    float mClickX = 0;
    float mClickY = 0;
    ClickPlace mLastClick;
    float mScreenWidth = std::numeric_limits<float>::max();
    float mScreenHeight = std::numeric_limits<float>::max();
    /// Griff: the touch scheme is on; the screen's pixels in a mm, and its slop in them.
    bool mGriff = false;
    float mMillimetre = 10;
    float mGriffSlop = kGriffSlopMm * 10;
    /// Griff: the rail's buttons as they counted for a waiting tap when its finger lifted (FingerRail), and when it lifted.
    int mClickRail = 0;
    Millis mClickLift = 0;
    TapLadder mLadder;
    /// Griff: the rail's Box as it counted when the first finger came down: FAF's reach for a unit beside the finger was set
    /// for it (Mouse::GriffDown), so the finger's tap and rest are Box's by it, not by Box at their lift.
    int mFingerBox = 0;
    /// Griff: the first finger came down on the handle, which left it to the ladder as that may climb; and so did a waiting
    /// tap's finger. A tap of it that does not climb after all is the handle's; a rest of it or a move past the slop is the
    /// handle's gesture, never the map's (TakeForHandle).
    bool mOnHandle = false;
    bool mClickOnHandle = false;
    /// Griff: what the finger holds is a drag the program pressed (Mouse::GriffDrag), not `mPressed`; it may be a pan.
    bool mPressedGriff = false;
    bool mMayPan = false;
    /// Griff: the last drag's number (Mouse::GriffDrag), and whether the program has said that it made a pan of it (Panned) or
    /// FAF's formation (Formation).
    int mDrag = 0;
    bool mPanned = false;
    bool mFormation = false;
    /// Griff: a second finger that came while the program had not said what the first one's drag is: no gesture's until the
    /// word comes (Panned, Formation), which takes it as it takes a finger that comes after it; none comes for a box or a
    /// panel's drag, and it waits for its lift. Not `mSecond`, whose lift would end the drag.
    Finger mWaiting;
    /// Griff: two fingers that began where the first had travelled already, as on a pan: their lift is no tap of two.
    bool mNoTapOfTwo = false;
    /// Griff: a rest's ring is shown; the program heard of the rest, and not yet how its gesture went on (Mouse::GriffRest).
    bool mRing = false;
    bool mRestSaid = false;
    /// Griff: the last rest that lifted rang, and so buzzed, for the group its right press may store (StoreBuzzes).
    bool mRestRang = false;
    /// Griff: the first finger's last places with their times, the oldest overwritten: its down first.
    std::array<float, kTrail> mTrailX{};
    std::array<float, kTrail> mTrailY{};
    std::array<Millis, kTrail> mTrailTime{};
    int mTrailNext = 0;
    int mTrailCount = 0;
    /// Griff: the finger that moves the camera, where it last was and when, and its velocity in pixels a second.
    Finger* mFollowed = nullptr;
    float mFollowX = 0;
    float mFollowY = 0;
    Millis mFollowTime = 0;
    float mVelocityX = 0;
    float mVelocityY = 0;
    /// Griff: the stroke from the handle, in the screen's pixels, and how far the finger has gone from its start.
    std::array<float, kStrokePoints> mStrokeX{};
    std::array<float, kStrokePoints> mStrokeY{};
    int mStrokeCount = 0;
    float mStrokeFar = 0;
    std::array<float, kMaxWaypoints> mWaypointX{};
    std::array<float, kMaxWaypoints> mWaypointY{};
    /// Griff: when the cursor last hovered for the stroke, and whether a hover waits for its time.
    Millis mHoverAt = 0;
    bool mHoverWaits = false;
    /// Griff: the handle's rest (kRestMilliseconds of the stroke's finger within the slop): whether its time runs, which has
    /// the timer until then, the number of the last rest the program was asked about (Mouse::HandleRest), and whether the
    /// program said that its lift gives FAF's Distribute orders (Distributes), for which the ring and the label show.
    bool mHandleRestWaits = false;
    int mHandleRests = 0;
    bool mDistributes = false;
    /// Griff's draft: the plan the finger came down on (0: none), and the one a waiting tap was on.
    int mDraftPlan = 0;
    int mClickPlan = 0;
    /// Griff's draft: whether the finger's rest asked the program if it is on FAF's UI (Mouse::DraftRest), and the number of
    /// the last such question, of 16 bits as the program's task takes it.
    bool mDraftRestAsked = false;
    int mDraftRests = 0;
    /// Griff's draft: the finger's rest is the panel's on FAF's UI (DraftRestOnUi), whose lift and slide use none of the rail,
    /// as the draft's taps there do: a lit Plus stays for the tick (a held one is Shift for them as ever).
    bool mRestInDraft = false;
    /// Griff: how many runs of the program's clicks had begun when the gesture's first finger came down (ClicksCame).
    int mClickRuns = 0;
  };
} // namespace touch
