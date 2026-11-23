// TouchInput's gestures (src/touch/TouchInput.h), as what reaches the game: the cases of the app's TouchInputTest.java,
// which the C++ layer passes as the Java one did. Each test has a fixture of its own, as each JUnit test had its instance.

#include <array>
#include <cmath>
#include <cstdlib>
#include <source_location>
#include <string>
#include <vector>

#include "touch/Stroke.h"
#include "touch/TouchInput.h"

#include "Check.h"

namespace
{
  using touch::Millis;
  using touch::TouchInput;

  /// Java's (int) of a float, as the recorder wrote places.
  std::string I(const float value)
  {
    return std::to_string(touch::ToInt(value));
  }

  /// Java's Math.round, as the recorder wrote distances and velocities.
  std::string R(const float value)
  {
    return std::to_string(touch::Round(value));
  }

  bool Has(const std::vector<std::string>& events, const std::string& event)
  {
    for (const std::string& known : events) {
      if (known == event) {
        return true;
      }
    }
    return false;
  }

  /// The event at `at`, or a text no event has where there is none.
  std::string At(const std::vector<std::string>& events, const long at)
  {
    if (at < 0 || at >= static_cast<long>(events.size())) {
      return "(no event " + std::to_string(at) + ")";
    }
    return events[static_cast<std::size_t>(at)];
  }

  /// The event `back` from the last (0: the last).
  std::string FromEnd(const std::vector<std::string>& events, const long back)
  {
    return At(events, static_cast<long>(events.size()) - 1 - back);
  }

  std::vector<std::string> Split(const std::string& text, const std::string& by)
  {
    std::vector<std::string> parts;
    std::size_t from = 0;
    for (std::size_t at = text.find(by); at != std::string::npos; at = text.find(by, from)) {
      parts.push_back(text.substr(from, at - from));
      from = at + by.size();
    }
    parts.push_back(text.substr(from));
    return parts;
  }

  /// Writes down what the game gets, and runs the timer when a test says time has passed.
  struct Recorder final : TouchInput::Mouse
  {
    std::vector<std::string> events;
    Millis timer = -1;
    bool placing = false;
    /// Whether the pointer is to leave, by the time this has passed.
    bool leaving = false;
    /// What had gone to the game by the last Stay.
    long sentBeforeStay = -1;
    int tapped = 0;

    void Move(const float x, const float y) override
    {
      events.push_back("move " + I(x) + " " + I(y));
    }

    void Button(const int button, const bool down, const float x, const float y) override
    {
      events.push_back(Name(button) + (down ? " down " : " up ") + I(x) + " " + I(y));
    }

    void Tap(const float x, const float y) override
    {
      events.push_back("left down " + I(x) + " " + I(y));
      events.push_back("left up " + I(x) + " " + I(y));
      tapped++;
    }

    void LeaveSoon() override
    {
      leaving = true;
    }

    void Stay() override
    {
      leaving = false;
      sentBeforeStay = static_cast<long>(events.size());
    }

    void TapNearGhost(const float fingerX, const float fingerY, const float ghostX, const float ghostY) override
    {
      events.push_back("tap " + I(fingerX) + " " + I(fingerY) + " near ghost " + I(ghostX) + " " + I(ghostY));
    }

    void StartLine(const float x, const float y) override
    {
      events.push_back("line from " + I(x) + " " + I(y));
    }

    void CancelLine() override
    {
      events.push_back("line given up");
    }

    void Wheel(const int delta, const float x, const float y) override
    {
      events.push_back("wheel " + std::to_string(delta) + " " + I(x) + " " + I(y));
    }

    void Held() override
    {
      events.push_back("held");
    }

    bool Placing() override
    {
      return placing;
    }

    void DeleteOrder(const float x, const float y) override
    {
      events.push_back("delete " + I(x) + " " + I(y));
    }

    void StartTimer(const Millis milliseconds) override
    {
      timer = milliseconds;
    }

    void StopTimer() override
    {
      timer = -1;
    }

    /// Griff: the handle's place, and the rail's buttons that count.
    float griffX = -1000;
    float griffY = -1000;
    float griffReach = 20;
    int rail = 0;
    /// Griff: the first fingers' places, the program's numbers of the taps sent, the ring.
    std::vector<std::string> downs;
    int lastTap = 0;
    bool ringShown = false;
    float ringX = 0;
    float ringY = 0;
    /// Griff: what the screen last got of the stroke, and where the cursor hovered for it.
    int shown = -1;
    int shownMode = -1;
    int shownWaypoints = 0;
    float shownEndX = 0;
    std::vector<std::string> hovers;

    void GriffDown(const float x, const float y, const bool box) override
    {
      downs.push_back(I(x) + " " + I(y) + (box ? " box" : ""));
    }

    void SmartTap(
      const float x,
      const float y,
      const int railNow,
      const int step,
      const float baseX,
      const float baseY,
      const int tap
    ) override
    {
      lastTap = tap;
      events.push_back(
        "smart tap " + I(x) + " " + I(y) + " rail " + std::to_string(railNow) +
        (step == TouchInput::kStepTap ? "" : " " + StepName(step) + " at " + I(baseX) + " " + I(baseY))
      );
    }

    void Ring(const bool shownNow, const float x, const float y) override
    {
      ringShown = shownNow;
      ringX = x;
      ringY = y;
    }

    void Deselect() override
    {
      events.push_back("deselect");
    }

    bool OnGriff(const float x, const float y) override
    {
      return touch::Hypot(x - griffX, y - griffY) <= griffReach;
    }

    void GriffTap() override
    {
      events.push_back("griff tap");
    }

    /// Griff: how many of the handle's rests the program was asked about, and the last one's number.
    int handleRests = 0;
    int lastHandleRest = 0;

    void HandleRest(const int rest) override
    {
      ++handleRests;
      lastHandleRest = rest;
    }

    void Distribute() override
    {
      events.push_back("distribute");
    }

    void Route(const float* const xs, const float* const ys, const int count, const int railNow, const bool patrol) override
    {
      std::string route = patrol ? "patrol" : "route";
      for (int at = 0; at < count; ++at) {
        route += " " + R(xs[at]) + " " + R(ys[at]) + (at + 1 < count ? "," : "");
      }
      events.push_back(route + " rail " + std::to_string(railNow));
    }

    void StrokeShown(
      const float* const xs,
      const float* /*ys*/,
      const int count,
      const int mode,
      const float* /*waypointXs*/,
      const float* /*waypointYs*/,
      const int waypoints
    ) override
    {
      shown = count;
      shownMode = mode;
      shownWaypoints = waypoints;
      shownEndX = count > 0 ? xs[count - 1] : 0;
    }

    void StrokeHover(const float x, const float y) override
    {
      hovers.push_back(I(x) + " " + I(y));
    }

    void StrokeGivenUp() override
    {
      events.push_back("given up");
    }

    int Rail() override
    {
      return rail;
    }

    void RailUsed(const int used) override
    {
      events.push_back("rail used " + std::to_string(used));
    }

    /// Griff: the number of the last drag sent, by which the program says it panned.
    int lastDrag = 0;

    void GriffDrag(
      const float x,
      const float y,
      const float nowX,
      const float nowY,
      const bool rested,
      const int railNow,
      const int drag
    ) override
    {
      lastDrag = drag;
      events.push_back(
        "griff drag " + I(x) + " " + I(y) + " now " + I(nowX) + " " + I(nowY) + (rested ? " rested" : "") + " rail " +
        std::to_string(railNow)
      );
    }

    void GriffDragEnd(const float x, const float y, const float velocityX, const float velocityY) override
    {
      events.push_back("griff drag end " + I(x) + " " + I(y) + Glide(velocityX, velocityY));
    }

    void GriffDragGiveUp(const float x, const float y) override
    {
      events.push_back("griff drag given up " + I(x) + " " + I(y));
    }

    void RestTap(const float x, const float y, const int railNow) override
    {
      events.push_back("rest tap " + I(x) + " " + I(y) + " rail " + std::to_string(railNow));
    }

    void GriffRest(const float x, const float y) override
    {
      events.push_back("griff rest " + I(x) + " " + I(y));
    }

    void GriffRestGone() override
    {
      events.push_back("rest gone");
    }

    void Pinch(const bool start, const float x, const float y, const float distance) override
    {
      events.push_back(std::string(start ? "pinch start " : "pinch ") + I(x) + " " + I(y) + " " + R(distance));
    }

    void PinchEnd(const float velocityX, const float velocityY) override
    {
      events.push_back("pinch end" + Glide(velocityX, velocityY));
    }

    /// Griff's draft: open or not, its building on a deposit, its plans as {number, x, y, on a deposit}, the newest last.
    bool drafting = false;
    bool picksDeposit = false;
    std::vector<std::array<int, 4>> draftPlans;
    /// A finger this far from a plan's middle each way is on it.
    int draftReach = 20;

    bool Drafting() override
    {
      return drafting;
    }

    int DraftPlan(const float x, const float y) override
    {
      for (auto plan = draftPlans.rbegin(); plan != draftPlans.rend(); ++plan) {
        if (std::abs(x - (*plan)[1]) <= draftReach && std::abs(y - (*plan)[2]) <= draftReach) {
          return (*plan)[0];
        }
      }
      return 0;
    }

    bool DraftNewest(const int plan) override
    {
      return !draftPlans.empty() && draftPlans.back()[0] == plan;
    }

    bool DraftOnDeposit(const int plan) override
    {
      for (const std::array<int, 4>& known : draftPlans) {
        if (known[0] == plan) {
          return known[3] != 0;
        }
      }
      return false;
    }

    bool DraftPicksDeposit() override
    {
      return picksDeposit;
    }

    void DraftTap(const float x, const float y, const int plan) override
    {
      events.push_back("draft tap " + I(x) + " " + I(y) + (plan != 0 ? " on " + std::to_string(plan) : ""));
    }

    void DraftLine(const int phase, const float x, const float y) override
    {
      events.push_back("draft line " + Phase(phase, x, y));
    }

    void DraftMove(const int phase, const float fromX, const float fromY, const float x, const float y) override
    {
      events.push_back(
        "draft move " + (phase == TouchInput::kDraftAbort ? "abort" : Phase(phase, fromX, fromY) + " to " + I(x) + " " + I(y))
      );
    }

    void DraftAim(const int phase, const float x, const float y) override
    {
      events.push_back("draft aim " + Phase(phase, x, y));
    }

    void DraftBox(const int phase, const float x0, const float y0, const float x1, const float y1) override
    {
      events.push_back(
        "draft box " + (phase == TouchInput::kDraftAbort ? "abort" : Phase(phase, x0, y0) + " to " + I(x1) + " " + I(y1))
      );
    }

    /// Griff's draft: where its rests asked whether they are on FAF's UI, and the last one's number.
    std::vector<std::string> draftRests;
    int lastDraftRest = 0;

    void DraftRest(const float x, const float y, const int rest) override
    {
      draftRests.push_back(I(x) + " " + I(y));
      lastDraftRest = rest;
    }

    void DraftDrop() override
    {
      events.push_back("draft drop");
    }

    /// Griff: the program's clicks run, the tick's or the undo's, and how many runs of them have begun.
    bool clicking = false;
    int clickRuns = 0;

    bool Clicking() override
    {
      return clicking;
    }

    int ClickRuns() override
    {
      return clickRuns;
    }

    /// A run of the program's clicks begins, as the tick's or the undo's does (StartClicks).
    void ClicksBegin()
    {
      clicking = true;
      ++clickRuns;
    }

    /// A draft gesture's phase, and where it is unless it is given up.
    static std::string Phase(const int phase, const float x, const float y)
    {
      switch (phase) {
        case TouchInput::kDraftShow:
          return "show " + I(x) + " " + I(y);
        case TouchInput::kDraftEnd:
          return "end " + I(x) + " " + I(y);
        default:
          return "abort";
      }
    }

    static std::string Glide(const float velocityX, const float velocityY)
    {
      return velocityX == 0 && velocityY == 0 ? "" : " glide " + R(velocityX) + " " + R(velocityY);
    }

    static std::string StepName(const int step)
    {
      switch (step) {
        case TouchInput::kStepAgain:
          return "again";
        case TouchInput::kStepAllOfType:
          return "all of type";
        case TouchInput::kStepAttackMove:
          return "attack move";
        case TouchInput::kStepAllMobile:
          return "all mobile";
        case TouchInput::kStepTypeInView:
          return "type in view";
        case TouchInput::kStepGroupAgain:
          return "group again";
        default:
          return "step " + std::to_string(step);
      }
    }

    static std::string Name(const int button)
    {
      return button == touch::kButtonLeft ? "left" : button == touch::kButtonRight ? "right" : "middle";
    }

    [[nodiscard]] int WheelTotal() const
    {
      int total = 0;
      for (const std::string& event : events) {
        if (event.starts_with("wheel ")) {
          total += std::atoi(Split(event, " ")[1].c_str());
        }
      }
      return total;
    }
  };

  /// A test's instance: the recorder, the fingers' layer over it, and the fingers' clock.
  struct TouchFixture
  {
    static constexpr float kSlop = 10;

    Recorder mouse;
    TouchInput input{mouse, kSlop};
    int fingers = 0;
    /// The time of the fingers' events: a down's or an up's, and a millisecond on for each move without its own.
    Millis clock = 0;

    void Down(const int id, const float x, const float y, const Millis time)
    {
      clock = time;
      input.Down(id, x, y, time, fingers == 0);
      ++fingers;
    }

    void Up(const int id, const Millis time)
    {
      clock = time;
      --fingers;
      input.Up(id, time, fingers == 0);
    }

    void TimerRuns(const std::source_location at = std::source_location::current())
    {
      if (mouse.timer < 0) {
        wxtest::Fail(at.file_name(), static_cast<int>(at.line()), "a timer is set");
      }
      mouse.timer = -1;
      input.Timer();
    }

    void MoveFirst(const float x, const float y)
    {
      MoveFirst(x, y, clock + 1);
    }

    void MoveFirst(const float x, const float y, const Millis time)
    {
      clock = time;
      input.Move(0, x, y);
      input.Moved(time);
    }

    void MoveBoth(const float x0, const float y0, const float x1, const float y1)
    {
      clock += 1;
      input.Move(0, x0, y0);
      input.Move(1, x1, y1);
      input.Moved(clock);
    }

    // --- Griff (buildstage/touch-concept-v2.md, the mockup buildstage/touch-preview) ------------------------------------
    // A mm is 10 pixels here: Griff's slop 25, the ladder's reach 60, two fingers' stillness 15.

    void Griff()
    {
      input.SetGriff(true);
      input.SetMillimetre(10);
    }

    /// A stroke from (x0, y0) to (x1, y1) in steps of 5 pixels, as a finger's moves a millisecond apart.
    void StrokeFirst(const float x0, const float y0, const float x1, const float y1)
    {
      const int steps = static_cast<int>(std::ceil(touch::Hypot(x1 - x0, y1 - y0) / 5));
      for (int at = 1; at <= steps; ++at) {
        MoveFirst(x0 + (x1 - x0) * at / steps, y0 + (y1 - y0) * at / steps);
      }
    }

    /// The places of a route as the recorder writes it: "route X Y, X Y rail R".
    static std::vector<std::array<float, 2>> Places(const std::string& route)
    {
      std::vector<std::array<float, 2>> places;
      const std::size_t from = route.find(' ') + 1;
      const std::string pairs = route.substr(from, route.rfind(" rail ") - from);
      for (const std::string& pair : Split(pairs, ", ")) {
        const std::vector<std::string> numbers = Split(pair, " ");
        places.push_back({std::strtof(numbers[0].c_str(), nullptr), std::strtof(numbers[1].c_str(), nullptr)});
      }
      return places;
    }

    /// A Griff tap down at `down` and up when the cursor has settled, its click; the program says what it was (`kind`, 0 not
    /// yet).
    void Tap(const float x, const float y, const Millis down, const int kind)
    {
      Down(0, x, y, down);
      Up(0, down + TouchInput::kSettleMilliseconds);
      if (kind != 0) {
        input.TapAnswered(mouse.lastTap, kind);
      }
    }

    /// Griff with its draft open, its plans at these places (number, x, y, on a deposit), the newest last.
    void OpenDraft(const std::vector<std::array<int, 4>>& plans)
    {
      Griff();
      mouse.drafting = true;
      for (const std::array<int, 4>& plan : plans) {
        mouse.draftPlans.push_back(plan);
      }
    }

    /// A tap, a rest that lifts, a rest that moves, a drag and a tap of two, the next at least a second after the last.
    void TapRestDragAndTwo(const float x, const float y)
    {
      Down(0, x, y, 1000);
      Up(0, 1040);
      Down(0, x, y, 2000);
      if (mouse.timer >= 0) {
        TimerRuns();
      }
      Up(0, 2600);
      Down(0, x, y, 3000);
      if (mouse.timer >= 0) {
        TimerRuns();
      }
      MoveFirst(x + 80, y + 40, 3500);
      Up(0, 3600);
      Down(0, x, y, 4000);
      MoveFirst(x + 80, y, 4010);
      MoveFirst(x + 160, y, 4020);
      Up(0, 4100);
      Down(0, x, y, 5000);
      Down(1, x + 200, y, 5030);
      Up(1, 5100);
      Up(0, 5110);
    }

    /// A ghost at 500 300, which a tap put there.
    void PutGhost()
    {
      mouse.placing = true;
      Down(0, 500, 300, 1000);
      Up(0, 1050);
      mouse.events.clear();
    }

    /// A finger that stayed on the ghost at 500 300 draws to 700 300.
    void StartLineTo700()
    {
      PutGhost();
      Down(0, 500, 300, 2000);
      TimerRuns();
      MoveFirst(700, 300);
    }

    /// A tap on one's own unit at 100 50, which raised the handle at 160 50: a finger on the handle within 350 ms may climb.
    void TapRaisingTheHandle(const Millis down)
    {
      Tap(100, 50, down, TouchInput::kTapOwn);
      mouse.griffX = 160;
      mouse.griffY = 50;
      mouse.events.clear();
    }
  };
} // namespace

/// A test of its own fixture (Check.h).
#define TOUCH_TEST(name) WXT_FIXTURE_TEST(TouchFixture, name)

/// What the game got, all of it, in its order.
#define EXPECT_EVENTS(...) WXT_CHECK_EQ(mouse.events, (std::vector<std::string>{__VA_ARGS__}))

TOUCH_TEST(TouchGriffAFingerThatMovesDragsWhatIsThere)
{
  Griff();
  Down(0, 100, 50, 1000);
  MoveFirst(150, 50);
  Up(0, 1200);
  EXPECT_EVENTS("move 100 50", "griff drag 100 50 now 150 50 rail 0", "move 150 50", "griff drag end 150 50");
}

TOUCH_TEST(TouchGriffTheSlopIsTwoAndAHalfMillimetres)
{
  // The system's slop, 10 here, would make these 2 mm a drag.
  Griff();
  Down(0, 100, 50, 1000);
  MoveFirst(120, 50);
  Up(0, 1000 + TouchInput::kSettleMilliseconds);
  EXPECT_EVENTS("move 100 50", "smart tap 100 50 rail 0");
  mouse.events.clear();
  Down(0, 100, 50, 2000);
  MoveFirst(127, 50);
  WXT_CHECK_EQ(At(mouse.events, 1), std::string("griff drag 100 50 now 127 50 rail 0"));
}

TOUCH_TEST(TouchGriffAPanStartsWhereTheFingerPassedTheSlop)
{
  // So the map does not jump by the slop: what is under the finger stays under it from there on.
  Griff();
  Down(0, 100, 50, 1000);
  MoveFirst(120, 50);
  MoveFirst(140, 50);
  MoveFirst(160, 50);
  EXPECT_EVENTS("move 100 50", "griff drag 100 50 now 140 50 rail 0", "move 140 50", "move 160 50");
}

TOUCH_TEST(TouchGriffAPanThatLiftsMovingGlidesOn)
{
  // 30 pixels each 10 ms: 3000 a second, of which each move takes 0.4 into its velocity, and the glide 0.9 of that.
  Griff();
  Down(0, 100, 50, 1000);
  for (int at = 1; at <= 6; ++at) {
    MoveFirst(100 + 30 * at, 50, 1000 + 10 * at);
  }
  Up(0, 1080);
  const std::string end = FromEnd(mouse.events, 0);
  WXT_CHECK(end.starts_with("griff drag end 280 50 glide "));
  const int velocity = std::atoi(At(Split(end, " "), 6).c_str());
  // The drag began at the first move; five more: 3000 * (1 - 0.6^5) * 0.9.
  WXT_CHECK(std::abs(velocity - 2490) <= 2);
  WXT_CHECK(end.ends_with(" 0"));
}

TOUCH_TEST(TouchGriffAPanThatStoppedBeforeTheLiftStops)
{
  Griff();
  Down(0, 100, 50, 1000);
  for (int at = 1; at <= 6; ++at) {
    MoveFirst(100 + 30 * at, 50, 1000 + 10 * at);
  }
  // 70 ms after the last move.
  Up(0, 1130);
  WXT_CHECK_EQ(FromEnd(mouse.events, 0), std::string("griff drag end 280 50"));
}

TOUCH_TEST(TouchGriffAPanIsNoActionForTheRail)
{
  Griff();
  mouse.rail = TouchInput::kRailQueue;
  Down(0, 100, 50, 1000);
  MoveFirst(150, 50);
  Up(0, 1200);
  EXPECT_EVENTS("move 100 50", "griff drag 100 50 now 150 50 rail 2", "move 150 50", "griff drag end 150 50");
}

TOUCH_TEST(TouchGriffARestThenADragIsABoxThatDoesNotGlide)
{
  Griff();
  Down(0, 100, 50, 1000);
  WXT_CHECK_EQ(mouse.timer, TouchInput::kRestMilliseconds);
  TimerRuns();
  // A ring shows the rest.
  WXT_CHECK(mouse.ringShown);
  MoveFirst(200, 100, 1400);
  // The box goes on without it.
  WXT_CHECK(!mouse.ringShown);
  Up(0, 1410);
  EXPECT_EVENTS(
    "move 100 50", "held", "griff rest 100 50", "griff drag 100 50 now 200 100 rested rail 0", "move 200 100",
    "griff drag end 200 100"
  );
}

TOUCH_TEST(TouchGriffARestedDragTheSystemTakesIsGivenUpNotEnded)
{
  // FAF's formation is ordered as its right button goes up: a drag no finger lifted gives it up, as a route is given up.
  Griff();
  Down(0, 100, 50, 1000);
  TimerRuns();
  MoveFirst(200, 100, 1400);
  input.Formation(mouse.lastDrag);
  input.Cancel();
  EXPECT_EVENTS(
    "move 100 50", "held", "griff rest 100 50", "griff drag 100 50 now 200 100 rested rail 0", "move 200 100",
    "griff drag given up 200 100"
  );
  // What is left of the finger gives nothing.
  MoveFirst(260, 100, 1500);
  Up(0, 1600);
  EXPECT_EVENTS(
    "move 100 50", "held", "griff rest 100 50", "griff drag 100 50 now 200 100 rested rail 0", "move 200 100",
    "griff drag given up 200 100"
  );
}

TOUCH_TEST(TouchGriffANewFirstFingerGivesARestedDragUp)
{
  // A first finger while a gesture is live: the one before never lifted, and orders nothing.
  Griff();
  Down(0, 100, 50, 1000);
  TimerRuns();
  MoveFirst(200, 100, 1400);
  input.Formation(mouse.lastDrag);
  mouse.events.clear();
  input.Down(5, 400, 300, 1500, true);
  WXT_CHECK_EQ(At(mouse.events, 0), std::string("griff drag given up 200 100"));
  for (const std::string& event : mouse.events) {
    WXT_CHECK(!event.starts_with("griff drag end"));
  }
}

TOUCH_TEST(TouchGriffASecondFingerGivesFafsFormationUp)
{
  Griff();
  Down(0, 100, 50, 1000);
  TimerRuns();
  MoveFirst(200, 100, 1400);
  // The program says that it made FAF's formation of the drag.
  input.Formation(mouse.lastDrag);
  mouse.events.clear();
  Down(1, 400, 100, 1500);
  EXPECT_EVENTS("griff drag given up 200 100", "given up");
  // Both fingers rest until they lift: no pinch, no order.
  MoveBoth(260, 100, 450, 100);
  Up(1, 1600);
  Up(0, 1700);
  EXPECT_EVENTS("griff drag given up 200 100", "given up");
}

TOUCH_TEST(TouchGriffASecondFingerLeavesABoxAlone)
{
  // A rested drag the program made a box of, or one it has not said is a formation yet: the box goes on, and its lift ends it.
  Griff();
  Down(0, 100, 50, 1000);
  TimerRuns();
  MoveFirst(200, 100, 1400);
  mouse.events.clear();
  Down(1, 400, 100, 1500);
  MoveBoth(220, 100, 420, 100);
  Up(1, 1600);
  Up(0, 1700);
  for (const std::string& event : mouse.events) {
    WXT_CHECK(!(event.starts_with("griff drag given up") || event.starts_with("pinch")));
  }
  WXT_CHECK(Has(mouse.events, "griff drag end 220 100"));
}

TOUCH_TEST(TouchGriffTheFormationsWordIsForOneDragAlone)
{
  Griff();
  Down(0, 100, 50, 1000);
  TimerRuns();
  MoveFirst(200, 100, 1400);
  const int earlier = mouse.lastDrag;
  Up(0, 1500);
  Down(0, 300, 50, 2000);
  TimerRuns();
  MoveFirst(400, 100, 2400);
  input.Formation(earlier);
  mouse.events.clear();
  Down(1, 600, 100, 2500);
  EXPECT_EVENTS();
}

TOUCH_TEST(TouchGriffARestAndALiftAreARestTapWhereTheFingerRested)
{
  Griff();
  mouse.rail = TouchInput::kRailQueue;
  Down(0, 100, 50, 1000);
  TimerRuns();
  MoveFirst(103, 50);
  WXT_CHECK(mouse.ringShown);
  // The ring follows the finger.
  WXT_CHECK_EQ(mouse.ringX, 103.0f);
  Up(0, 1600);
  WXT_CHECK(!mouse.ringShown);
  EXPECT_EVENTS("move 100 50", "held", "griff rest 100 50", "rest tap 100 50 rail 2", "rail used 2");
}

TOUCH_TEST(TouchGriffARestTellsTheProgramWhereTheFingerCameDownAsItsRingShows)
{
  // The program marks a waypoint there or makes the rest the right button's, for the drag or the lift that follows.
  Griff();
  Down(0, 100, 50, 1000);
  MoveFirst(104, 52);
  EXPECT_EVENTS("move 100 50");
  TimerRuns();
  WXT_CHECK(mouse.ringShown);
  EXPECT_EVENTS("move 100 50", "held", "griff rest 100 50");
}

TOUCH_TEST(TouchGriffARestTheSystemTookIsGoneForTheProgram)
{
  Griff();
  Down(0, 100, 50, 1000);
  TimerRuns();
  input.Cancel();
  WXT_CHECK(!mouse.ringShown);
  EXPECT_EVENTS("move 100 50", "held", "griff rest 100 50", "rest gone");
  // Once: the finger that the system took lifts with nothing more, and the next gesture's tap has no rest of its own.
  Up(0, 1500);
  mouse.events.clear();
  Down(0, 300, 50, 3000);
  Up(0, 3000 + TouchInput::kSettleMilliseconds);
  EXPECT_EVENTS("move 300 50", "smart tap 300 50 rail 0");
}

TOUCH_TEST(TouchGriffARestsDragOrLiftTakesItsRestWithIt)
{
  // The program reads what the rest found with the drag or the lift: no word that it went.
  Griff();
  Down(0, 100, 50, 1000);
  TimerRuns();
  MoveFirst(200, 100, 1400);
  Up(0, 1500);
  Down(0, 300, 50, 3000);
  TimerRuns();
  Up(0, 3600);
  WXT_CHECK(!Has(mouse.events, "rest gone"));
}

TOUCH_TEST(TouchGriffWithBoxARestIsNoneAndItsLiftIsLeftToTheProgram)
{
  // The mockup's rest is off with Box: no ring, nothing to feel; the lift is Box's tap on the map, which says what it used.
  Griff();
  mouse.rail = TouchInput::kRailBox;
  Down(0, 100, 50, 1000);
  TimerRuns();
  WXT_CHECK(!mouse.ringShown);
  Up(0, 1600);
  EXPECT_EVENTS("move 100 50", "rest tap 100 50 rail 1");
  WXT_CHECK_EQ(mouse.downs, std::vector<std::string>{"100 50 box"});
}

TOUCH_TEST(TouchGriffAGroupStoredByARestWithBoxBuzzesAsItIsStored)
{
  // The rest with Box rang for nothing, but on a slot its lift is FAF's right press, which stores the group: the store is felt
  // then, once (the mockup's one buzz a group).
  Griff();
  mouse.rail = TouchInput::kRailBox;
  Down(0, 100, 50, 1000);
  TimerRuns();
  Up(0, 1600);
  EXPECT_EVENTS("move 100 50", "rest tap 100 50 rail 1");
  // The store buzzes, and one no rest made after it.
  WXT_CHECK(input.StoreBuzzes());
  WXT_CHECK(input.StoreBuzzes());
}

TOUCH_TEST(TouchGriffAGroupStoredByARestThatRangBuzzesNoMore)
{
  Griff();
  // A store no rest made, a mouse's.
  WXT_CHECK(input.StoreBuzzes());
  Down(0, 100, 50, 1000);
  TimerRuns();
  Up(0, 1600);
  EXPECT_EVENTS("move 100 50", "held", "griff rest 100 50", "rest tap 100 50 rail 0");
  // Its ring buzzed; for one store: a mouse's after it buzzes.
  WXT_CHECK(!input.StoreBuzzes());
  WXT_CHECK(input.StoreBuzzes());
}

TOUCH_TEST(TouchGriffARestThenAQuickSecondFingerIsNoTwoFingerTap)
{
  // Both fingers count, from the first one down.
  Griff();
  Down(0, 100, 50, 1000);
  TimerRuns();
  Down(1, 200, 50, 1400);
  WXT_CHECK(!mouse.ringShown);
  Up(1, 1450);
  Up(0, 1460);
  EXPECT_EVENTS("move 100 50", "held", "griff rest 100 50", "rest gone", "move 200 50");
}

TOUCH_TEST(TouchGriffBoxOnTheRailMakesADragABoxAtOnce)
{
  Griff();
  mouse.rail = TouchInput::kRailBox;
  Down(0, 100, 50, 1000);
  MoveFirst(200, 100);
  Up(0, 1200);
  EXPECT_EVENTS(
    "move 100 50", "griff drag 100 50 now 200 100 rail 1", "move 200 100", "rail used 1", "griff drag end 200 100"
  );
}

TOUCH_TEST(TouchGriffQueueOnTheRailMakesABoxAdd)
{
  Griff();
  mouse.rail = TouchInput::kRailQueue;
  Down(0, 100, 50, 1000);
  TimerRuns();
  MoveFirst(200, 100);
  Up(0, 1600);
  EXPECT_EVENTS(
    "move 100 50", "held", "griff rest 100 50", "griff drag 100 50 now 200 100 rested rail 2", "move 200 100", "rail used 2",
    "griff drag end 200 100"
  );
}

TOUCH_TEST(TouchGriffATapIsFafsClickByWhatItLandsOnAndUsesTheRail)
{
  Griff();
  mouse.rail = TouchInput::kRailQueue;
  Down(0, 100, 50, 1000);
  Up(0, 1000 + TouchInput::kSettleMilliseconds);
  EXPECT_EVENTS("move 100 50", "smart tap 100 50 rail 2", "rail used 2");
  WXT_CHECK_EQ(mouse.downs, std::vector<std::string>{"100 50"});
}

TOUCH_TEST(TouchGriffBoxsTapLeavesTheRailToTheProgram)
{
  // Box's tap on nothing leaves Box lit: the program says what the tap used.
  Griff();
  mouse.rail = TouchInput::kRailBox | TouchInput::kRailQueue;
  Down(0, 100, 50, 1000);
  Up(0, 1000 + TouchInput::kSettleMilliseconds);
  EXPECT_EVENTS("move 100 50", "smart tap 100 50 rail 3");
}

TOUCH_TEST(TouchGriffAQuickTapWaitsForTheCursorToSettle)
{
  Griff();
  Down(0, 100, 50, 1000);
  Up(0, 1040);
  EXPECT_EVENTS("move 100 50");
  WXT_CHECK_EQ(mouse.timer, TouchInput::kSettleMilliseconds - 40);
  TimerRuns();
  EXPECT_EVENTS("move 100 50", "smart tap 100 50 rail 0");
}

TOUCH_TEST(TouchGriffTheLadderOnAUnitTakesItsTypeOnTheScreenThenOnTheMap)
{
  // The second tap 4 mm beside the first, wherever it lands: the ladder counts by what the first tap was.
  Griff();
  Tap(100, 50, 1000, TouchInput::kTapOwn);
  Tap(140, 50, 1300, 0);
  Tap(150, 60, 1600, 0);
  Tap(150, 60, 1900, 0);
  EXPECT_EVENTS(
    "move 100 50", "smart tap 100 50 rail 0", "move 140 50", "smart tap 140 50 rail 0 type in view at 100 50", "move 150 60",
    "smart tap 150 60 rail 0 all of type at 100 50", "move 150 60", "smart tap 150 60 rail 0 all of type at 100 50"
  );
}

TOUCH_TEST(TouchGriffTheLadderOnGroundMakesTheMoveAnAttackMove)
{
  Griff();
  Tap(100, 50, 1000, TouchInput::kTapGround);
  Tap(130, 60, 1300, 0);
  // The attack-move is the ladder's top: the next tap is a tap again.
  Tap(130, 60, 1600, 0);
  EXPECT_EVENTS(
    "move 100 50", "smart tap 100 50 rail 0", "move 130 60", "smart tap 130 60 rail 0 attack move at 100 50", "move 130 60",
    "smart tap 130 60 rail 0"
  );
}

TOUCH_TEST(TouchGriffTheLadderOnAGroupsIconSelectsItAgainWithTheCamera)
{
  // As a control group's double click: the second tap near the first, wherever it lands, takes the first tap's group.
  Griff();
  Tap(100, 50, 1000, TouchInput::kTapGroup);
  Tap(120, 55, 1300, 0);
  // The camera to the group is the ladder's top: the next tap is a tap again.
  Tap(120, 55, 1600, 0);
  EXPECT_EVENTS(
    "move 100 50", "smart tap 100 50 rail 0", "move 120 55", "smart tap 120 55 rail 0 group again at 100 50", "move 120 55",
    "smart tap 120 55 rail 0"
  );
}

TOUCH_TEST(TouchGriffTheLadderOnNothingTakesAllOneOwnsOnTheScreen)
{
  Griff();
  Tap(100, 50, 1000, TouchInput::kTapEmpty);
  Tap(100, 50, 1300, 0);
  EXPECT_EVENTS("move 100 50", "smart tap 100 50 rail 0", "move 100 50", "smart tap 100 50 rail 0 all mobile at 100 50");
}

TOUCH_TEST(TouchGriffTheLadderWantsItSoonNearWithoutTheRailAndKnown)
{
  Griff();
  // 350 ms from lift to lift: 360 is too late.
  Tap(100, 50, 1000, TouchInput::kTapOwn);
  Tap(100, 50, 1360, TouchInput::kTapOwn);
  // 6 mm: 61 pixels is too far.
  Tap(161, 50, 1600, TouchInput::kTapGround);
  // Plus keeps a tap off the ladder.
  mouse.rail = TouchInput::kRailQueue;
  Tap(161, 50, 1800, 0);
  mouse.rail = 0;
  // Before the program has said what the last tap was, a tap is decided by its place.
  Tap(161, 50, 2000, 0);
  Tap(161, 50, 2200, 0);
  // An attack, or anything else that is no rung: neither.
  Tap(161, 50, 2400, TouchInput::kTapOther);
  Tap(161, 50, 2600, 0);
  for (const std::string& event : mouse.events) {
    WXT_CHECK(event.find(" at ") == std::string::npos);
  }
}

TOUCH_TEST(TouchGriffTwoTapsOnTheUiClickTheSamePixel)
{
  // The game's double click on its UI wants the same place, within the system's reach and time; farther is a tap.
  Griff();
  Tap(100, 50, 1000, TouchInput::kTapUi);
  Tap(115, 58, 1200, 0);
  Tap(100, 50, 2000, TouchInput::kTapUi);
  Tap(125, 50, 2200, 0);
  EXPECT_EVENTS(
    "move 100 50", "smart tap 100 50 rail 0", "move 115 58", "smart tap 115 58 rail 0 again at 100 50", "move 100 50",
    "smart tap 100 50 rail 0", "move 125 50", "smart tap 125 50 rail 0"
  );
}

TOUCH_TEST(TouchGriffADoubleTapOnAUnitIsNotTakenByTheHandleItsFirstTapRaised)
{
  Griff();
  Tap(100, 50, 1000, 0);
  // The first tap selected the unit, and the handle came up over it; the program has not said so yet.
  mouse.griffX = 100;
  mouse.griffY = 50;
  Down(0, 102, 50, 1200);
  input.TapAnswered(mouse.lastTap, TouchInput::kTapOwn);
  Up(0, 1200 + TouchInput::kSettleMilliseconds);
  EXPECT_EVENTS("move 100 50", "smart tap 100 50 rail 0", "move 102 50", "smart tap 102 50 rail 0 type in view at 100 50");
}

TOUCH_TEST(TouchGriffTheHandleTakesADragAfterTheThirdTap)
{
  Griff();
  Tap(100, 50, 1000, TouchInput::kTapOwn);
  // The first tap selected the unit and the handle came up over it: the ladder keeps the handle away until its top.
  mouse.griffX = 100;
  mouse.griffY = 50;
  Tap(100, 50, 1300, 0);
  Tap(100, 50, 1600, 0);
  mouse.events.clear();
  Down(0, 100, 50, 1800);
  StrokeFirst(100, 50, 400, 50);
  Up(0, 2300);
  EXPECT_EVENTS("route 250 50, 400 50 rail 0");
}

TOUCH_TEST(TouchGriffAFingerTheLadderLetPastTheHandleIsNeverAPlainTapThere)
{
  // A finger 5 mm from a tap on a unit, on the handle that tap raised, comes down 300 ms after the tap's lift and lifts 390 ms
  // after it, too late to climb. The handle left it to the ladder at its down, as the ladder might climb, so its lift is the
  // ladder's rung or the handle's tap: never FAF's click at the handle's place, which with troops is a move.
  Griff();
  Down(0, 100, 50, 1000);
  Up(0, 1080);
  TimerRuns();
  input.TapAnswered(mouse.lastTap, TouchInput::kTapOwn);
  mouse.griffX = 160;
  mouse.griffY = 50;
  mouse.events.clear();
  Down(0, 150, 50, 1380);
  Up(0, 1470);
  if (mouse.timer >= 0) {
    TimerRuns();
  }
  WXT_CHECK(!Has(mouse.events, "smart tap 150 50 rail 0"));
  WXT_CHECK(Has(mouse.events, "griff tap") || Has(mouse.events, "smart tap 150 50 rail 0 type in view at 100 50"));
}

TOUCH_TEST(TouchGriffAFingerOnTheHandleBeforeTheProgramAnsweredIsNeverAPlainTapThere)
{
  // The handle leaves a finger to the ladder while the program has not said what the last tap was, as the ladder might climb;
  // the answer comes only after the finger's lift, as from a slow frame. The lift cannot climb, and is the handle's tap, not
  // FAF's click at the handle's place.
  Griff();
  Down(0, 100, 50, 1000);
  Up(0, 1080);
  TimerRuns();
  const int first = mouse.lastTap;
  mouse.griffX = 160;
  mouse.griffY = 50;
  mouse.events.clear();
  Down(0, 150, 50, 1200);
  Up(0, 1260);
  if (mouse.timer >= 0) {
    TimerRuns();
  }
  input.TapAnswered(first, TouchInput::kTapOwn);
  if (mouse.timer >= 0) {
    TimerRuns();
  }
  WXT_CHECK(!Has(mouse.events, "smart tap 150 50 rail 0"));
  WXT_CHECK(Has(mouse.events, "griff tap") || Has(mouse.events, "smart tap 150 50 rail 0 type in view at 100 50"));
}

TOUCH_TEST(TouchGriffWithPlusTheHandleTakesAStrokeRightAfterATap)
{
  // The ladder never climbs while Plus or Attack count, at the down as at the lift: a stroke from the handle 200 ms after a
  // tap on a unit, with Plus held, is the handle's, a queued route, not a pan of a finger left to the ladder.
  Griff();
  Tap(100, 50, 1000, TouchInput::kTapOwn);
  mouse.griffX = 160;
  mouse.griffY = 50;
  mouse.rail = TouchInput::kRailQueue;
  mouse.events.clear();
  Down(0, 150, 50, 1320);
  StrokeFirst(150, 50, 450, 50);
  Up(0, 1800);
  WXT_CHECK(!mouse.events.empty());
  WXT_CHECK(At(mouse.events, 0).starts_with("route ") && At(mouse.events, 0).ends_with(" rail 2"));
}

TOUCH_TEST(TouchGriffTheHandlesTapOfAFingerTheLadderLetPassUsesNoRailAndStartsNoLadder)
{
  // The handle left the finger to the ladder; Plus, pressed before the lift, keeps the tap off the ladder: it is the handle's
  // tap, as a stroke's is, and Plus stays for the next action.
  Griff();
  Tap(100, 50, 1000, TouchInput::kTapOwn);
  mouse.griffX = 160;
  mouse.griffY = 50;
  mouse.events.clear();
  Down(0, 150, 50, 1300);
  mouse.rail = TouchInput::kRailQueue;
  Up(0, 1300 + TouchInput::kSettleMilliseconds);
  EXPECT_EVENTS("move 150 50", "griff tap");
  // No ladder from it: a stroke from the handle at once is a route, not a finger left to a ladder that might climb.
  mouse.rail = 0;
  mouse.events.clear();
  Down(0, 150, 50, 1520);
  StrokeFirst(150, 50, 450, 50);
  Up(0, 1900);
  WXT_CHECK(At(mouse.events, 0).starts_with("route "));
}

TOUCH_TEST(TouchGriffARestOnTheHandleRightAfterATapIsTheHandlesNotTheMaps)
{
  // The ladder may climb, so the handle left the finger to it; but only a quick lift climbs. Its rest is the handle's, never a
  // rest at the handle's place, whose right button would be a move or a formation there: where the program does not say that
  // its lift distributes, no ring, and its lift within the slop is the handle's tap.
  Griff();
  TapRaisingTheHandle(1000);
  Down(0, 150, 50, 1300);
  TimerRuns();
  // The handle's rest asks whether its lift distributes.
  WXT_CHECK_EQ(mouse.handleRests, 1);
  MoveFirst(158, 52, 1700);
  // No ring.
  WXT_CHECK(!mouse.ringShown);
  Up(0, 1800);
  EXPECT_EVENTS("move 150 50", "griff tap");
}

TOUCH_TEST(TouchGriffARestOnTheHandleRightAfterATapGivesFafsDistributeOrdersWhereTheProgramSays)
{
  // The handle's rest of the finger the ladder had: where the program says that its lift distributes, the ring and the label
  // show, and the lift is FAF's Distribute orders, as after the ladder's time.
  Griff();
  TapRaisingTheHandle(1000);
  Down(0, 150, 50, 1300);
  TimerRuns();
  input.Distributes(mouse.lastHandleRest);
  // The ring.
  WXT_CHECK(mouse.ringShown);
  WXT_CHECK_EQ(mouse.shownMode, touch::stroke::kDistribute);
  Up(0, 1800);
  EXPECT_EVENTS("move 150 50", "held", "distribute");
}

TOUCH_TEST(TouchGriffADragFromTheHandleRightAfterATapIsItsStrokeNotAPan)
{
  // From where the finger came down, as a stroke from the handle; the moves before it left the slop count.
  Griff();
  TapRaisingTheHandle(1000);
  Down(0, 150, 50, 1300);
  StrokeFirst(150, 50, 450, 50);
  Up(0, 1800);
  EXPECT_EVENTS("move 150 50", "route 300 50, 450 50 rail 0");
  // A quick one that lifts 60 ms after it left the slop counts where it was then, not where it came down: a route.
  TapRaisingTheHandle(3000);
  Down(0, 150, 50, 3300);
  MoveFirst(240, 50, 3340);
  Up(0, 3400);
  EXPECT_EVENTS("move 150 50", "route 240 50 rail 0");
}

TOUCH_TEST(TouchGriffAQuickLiftOnTheHandleRightAfterATapClimbsTheLadder)
{
  Griff();
  TapRaisingTheHandle(1000);
  Down(0, 150, 50, 1300);
  MoveFirst(160, 52, 1320);
  Up(0, 1300 + TouchInput::kSettleMilliseconds);
  EXPECT_EVENTS("move 150 50", "smart tap 150 50 rail 0 type in view at 100 50");
}

TOUCH_TEST(TouchGriffATwoFingerTapLetsTheSelectionGo)
{
  Griff();
  Down(0, 100, 50, 1000);
  Down(1, 200, 50, 1050);
  Up(1, 1150);
  Up(0, 1160);
  WXT_CHECK(Has(mouse.events, "deselect"));
  for (const std::string& event : mouse.events) {
    WXT_CHECK(event.find("right") == std::string::npos);
  }
}

TOUCH_TEST(TouchGriffATwoFingerTapCountsFromTheFirstFingerAndStaysWithinOneAndAHalfMillimetres)
{
  Griff();
  // 360 ms from the first finger: too long, though the second was quick.
  Down(0, 100, 50, 1000);
  Down(1, 200, 50, 1300);
  Up(1, 1360);
  Up(0, 1370);
  WXT_CHECK(!Has(mouse.events, "deselect"));
  // Their distance 1.4 mm longer still taps; 1.6 mm longer pinches, though each finger moved 0.8 mm about a still middle.
  Down(0, 100, 50, 2000);
  Down(1, 200, 50, 2050);
  MoveBoth(93, 50, 207, 50);
  Up(1, 2100);
  Up(0, 2110);
  WXT_CHECK(Has(mouse.events, "deselect"));
  mouse.events.clear();
  Down(0, 100, 50, 3000);
  Down(1, 200, 50, 3050);
  MoveBoth(92, 50, 208, 50);
  Up(1, 3100);
  Up(0, 3110);
  WXT_CHECK(!Has(mouse.events, "deselect"));
  WXT_CHECK(Has(mouse.events, "pinch start 150 50 116"));
}

TOUCH_TEST(TouchGriffTwoFingersThatTwistAboutAStillMiddlePinchAndAreNoTap)
{
  // Each finger 2 mm one way, about a middle that stays, their distance 0.4 mm longer: no tap of two, which would let the
  // selection go, but a pinch, which moves nothing; a finger counts from where it was as the second came down.
  Griff();
  Down(0, 100, 50, 1000);
  MoveFirst(104, 51, 1030);
  Down(1, 300, 50, 1050);
  MoveBoth(104, 41, 300, 60);
  EXPECT_EVENTS("move 100 50", "move 300 50");
  MoveBoth(104, 31, 300, 70);
  Up(1, 1150);
  Up(0, 1160);
  WXT_CHECK(!Has(mouse.events, "deselect"));
  WXT_CHECK_EQ(At(mouse.events, 2), std::string("pinch start 202 50 200"));
}

TOUCH_TEST(TouchGriffTwoFingersAgainAfterAPinchAreNoTapOfTwo)
{
  // A pinch, one finger up and down again beside the other within 350 ms of the first down: the camera moved, no tap.
  Griff();
  Down(0, 100, 100, 1000);
  Down(1, 300, 100, 1020);
  MoveBoth(80, 100, 320, 100);
  Up(1, 1080);
  Down(2, 300, 100, 1100);
  Up(2, 1200);
  Up(0, 1210);
  WXT_CHECK(!Has(mouse.events, "deselect"));
}

TOUCH_TEST(TouchGriffTwoFingersZoomAndPanTheCameraTogether)
{
  Griff();
  Down(0, 100, 100, 1000);
  Down(1, 300, 100, 1040);
  MoveBoth(80, 100, 320, 100);
  MoveBoth(70, 120, 350, 120);
  EXPECT_EVENTS("move 100 100", "move 300 100", "pinch start 200 100 240", "pinch 210 120 280");
}

TOUCH_TEST(TouchGriffTheFingerLeftOfAPinchMovesTheCameraOnAndGlides)
{
  Griff();
  Down(0, 100, 100, 1000);
  Down(1, 300, 100, 1040);
  MoveBoth(80, 100, 320, 100);
  Up(1, 1100);
  WXT_CHECK_EQ(FromEnd(mouse.events, 0), std::string("pinch start 80 100 0"));
  mouse.events.clear();
  for (int at = 1; at <= 3; ++at) {
    MoveFirst(80 + 20 * at, 100, 1100 + 10 * at);
  }
  Up(0, 1140);
  // 2000 pixels a second from the start, each move taking 0.4 of it into the velocity: 1568, and 0.9 of that.
  EXPECT_EVENTS("pinch 100 100 0", "pinch 120 100 0", "pinch 140 100 0", "pinch end glide 1411 0");
}

TOUCH_TEST(TouchGriffAPinchThatEndsTogetherDoesNotGlide)
{
  Griff();
  Down(0, 100, 100, 1000);
  Down(1, 300, 100, 1040);
  MoveBoth(80, 100, 320, 100);
  Up(1, 1100);
  Up(0, 1200);
  WXT_CHECK_EQ(FromEnd(mouse.events, 1), std::string("pinch start 80 100 0"));
  WXT_CHECK_EQ(FromEnd(mouse.events, 0), std::string("pinch end"));
}

TOUCH_TEST(TouchGriffAFingerAgainBesideTheOneLeftOfAPinchPinchesAnew)
{
  Griff();
  Down(0, 100, 100, 1000);
  Down(1, 300, 100, 1040);
  MoveBoth(80, 100, 320, 100);
  // The first finger lifts; the second moves the camera on, then a finger comes again.
  Up(0, 1100);
  mouse.events.clear();
  input.Move(1, 340, 110);
  input.Moved(1110);
  Down(2, 500, 110, 1200);
  input.Move(1, 330, 110);
  input.Move(2, 520, 110);
  input.Moved(1210);
  EXPECT_EVENTS("pinch 340 110 0", "pinch end", "move 500 110", "pinch start 425 110 190");
}

TOUCH_TEST(TouchGriffASecondFingerTurnsAPanIntoTwoFingers)
{
  Griff();
  Down(0, 100, 100, 1000);
  MoveFirst(150, 100, 1100);
  // The program says that it made a pan of the drag.
  input.Panned(mouse.lastDrag, 1150);
  Down(1, 400, 100, 1200);
  MoveBoth(130, 100, 420, 100);
  EXPECT_EVENTS(
    "move 100 100", "griff drag 100 100 now 150 100 rail 0", "move 150 100", "griff drag end 150 100", "move 400 100",
    "pinch start 275 100 290"
  );
}

TOUCH_TEST(TouchGriffASecondFingerLeavesADragNotKnownAsAPanAlone)
{
  // Only the program tells a pan from a drag on a panel or the minimap (DragTask): a second finger leaves a drag alone that
  // the program has not said is a pan, and pinches no map behind a panel.
  Griff();
  Down(0, 100, 100, 1000);
  MoveFirst(150, 100, 1100);
  mouse.events.clear();
  Down(1, 400, 100, 1200);
  MoveBoth(130, 100, 420, 100);
  for (const std::string& event : mouse.events) {
    WXT_CHECK(!(event.starts_with("griff drag end") || event.starts_with("pinch")));
  }
}

TOUCH_TEST(TouchGriffTheProgramsWordIsForOneDragAlone)
{
  // A pan, its word in time; then a drag on a panel, which no word makes a pan, nor a late word for the pan before: the second
  // finger waits, and a third is beyond the two a gesture uses.
  Griff();
  Down(0, 100, 100, 1000);
  MoveFirst(150, 100, 1100);
  const int pan = mouse.lastDrag;
  input.Panned(pan, 1150);
  Up(0, 1200);
  Down(0, 600, 100, 2000);
  MoveFirst(650, 100, 2100);
  mouse.events.clear();
  Down(1, 900, 100, 2200);
  input.Panned(pan, 2210);
  Down(2, 900, 120, 2250);
  MoveBoth(630, 100, 920, 100);
  EXPECT_EVENTS("move 630 100");
  // Its own word does, and takes the second finger where it is now.
  input.Panned(mouse.lastDrag, 2300);
  EXPECT_EVENTS("move 630 100", "griff drag end 630 100", "move 920 100");
}

TOUCH_TEST(TouchGriffASecondFingerBeforeThePansWordIsTakenAtTheWord)
{
  // It landed before the program said that the drag is a pan, one program frame after the drag began: the word makes two
  // fingers of it, which pinch, as of a finger after the word. Their quick lift is no tap of two: the first finger had
  // travelled.
  Griff();
  Down(0, 100, 100, 1000);
  MoveFirst(150, 100, 1030);
  Down(1, 400, 100, 1060);
  input.Move(1, 410, 100);
  input.Moved(1070);
  input.Panned(mouse.lastDrag, 1090);
  MoveBoth(130, 100, 430, 100);
  Up(1, 1150);
  Up(0, 1160);
  EXPECT_EVENTS(
    "move 100 100", "griff drag 100 100 now 150 100 rail 0", "move 150 100", "move 150 100", "griff drag end 150 100",
    "move 410 100", "pinch start 280 100 300", "pinch start 130 100 0", "pinch end"
  );
  // Two that stay and lift within 350 ms of the first finger's down.
  mouse.events.clear();
  Down(0, 100, 100, 3000);
  MoveFirst(150, 100, 3030);
  Down(1, 400, 100, 3060);
  input.Panned(mouse.lastDrag, 3090);
  Up(1, 3150);
  Up(0, 3160);
  WXT_CHECK(!Has(mouse.events, "deselect"));
  // With no word, on a panel's drag or a box, it waits for its lift and is no gesture's: the drag goes on to its own lift.
  mouse.events.clear();
  Down(0, 100, 100, 2000);
  MoveFirst(150, 100, 2030);
  Down(1, 400, 100, 2060);
  Up(1, 2100);
  MoveFirst(170, 100, 2110);
  Up(0, 2200);
  EXPECT_EVENTS(
    "move 100 100", "griff drag 100 100 now 150 100 rail 0", "move 150 100", "move 170 100", "griff drag end 170 100"
  );
}

TOUCH_TEST(TouchGriffASecondFingerThatWaitedForTheWordAndWentIsNoGestures)
{
  // It lifted before the program said that the drag is a pan, or the system took it for a palm: the word makes no two fingers
  // of it, and the first finger's pan goes on to its own lift.
  Griff();
  Down(0, 100, 100, 1000);
  MoveFirst(150, 100, 1030);
  Down(1, 400, 100, 1060);
  Up(1, 1080);
  input.Panned(mouse.lastDrag, 1090);
  MoveFirst(170, 100, 1100);
  Up(0, 1200);
  EXPECT_EVENTS(
    "move 100 100", "griff drag 100 100 now 150 100 rail 0", "move 150 100", "move 170 100", "griff drag end 170 100"
  );
  mouse.events.clear();
  Down(0, 100, 100, 2000);
  MoveFirst(150, 100, 2030);
  Down(1, 400, 100, 2060);
  input.Forget(1, 2070);
  --fingers;
  input.Panned(mouse.lastDrag, 2090);
  MoveFirst(170, 100, 2100);
  Up(0, 2200);
  EXPECT_EVENTS(
    "move 100 100", "griff drag 100 100 now 150 100 rail 0", "move 150 100", "move 170 100", "griff drag end 170 100"
  );
}

TOUCH_TEST(TouchGriffTheProgramsWordAfterItsClicksBeganGivesTheGestureUp)
{
  // The clicks of the tick or the undo began after the first finger came down: the program's word on its rest or its drag
  // gives the gesture up, and nothing more of it goes to the game.
  Griff();
  mouse.griffX = 100;
  mouse.griffY = 50;
  // The handle's rest: its stroke goes, Cancelled, with no ring.
  Down(0, 100, 50, 1000);
  TimerRuns();
  mouse.ClicksBegin();
  input.Distributes(mouse.lastHandleRest);
  WXT_CHECK(!mouse.ringShown);
  Up(0, 1500);
  EXPECT_EVENTS("given up");
  // The draft's rest on FAF's UI: no panel's rest.
  mouse.clicking = false;
  mouse.drafting = true;
  mouse.events.clear();
  Down(0, 400, 50, 2000);
  TimerRuns();
  mouse.ClicksBegin();
  input.DraftRestOnUi(mouse.lastDraftRest);
  WXT_CHECK(!mouse.ringShown);
  Up(0, 2500);
  EXPECT_EVENTS("move 400 50");
  // A second finger that waited for the word on a pan: no two fingers, nor a pinch.
  mouse.clicking = false;
  mouse.drafting = false;
  mouse.events.clear();
  Down(0, 400, 100, 3000);
  MoveFirst(450, 100, 3030);
  Down(1, 700, 100, 3060);
  mouse.ClicksBegin();
  input.Panned(mouse.lastDrag, 3090);
  MoveBoth(430, 100, 720, 100);
  Up(1, 3150);
  Up(0, 3160);
  EXPECT_EVENTS("move 400 100", "griff drag 400 100 now 450 100 rail 0", "move 450 100");
  // One that waited for the word on FAF's formation: nothing more either.
  mouse.clicking = false;
  mouse.events.clear();
  Down(0, 400, 100, 4000);
  TimerRuns();
  MoveFirst(500, 150, 4400);
  Down(1, 700, 100, 4420);
  mouse.ClicksBegin();
  input.Formation(mouse.lastDrag);
  Up(1, 4500);
  Up(0, 4600);
  EXPECT_EVENTS(
    "move 400 100", "held", "griff rest 400 100", "griff drag 400 100 now 500 150 rested rail 0", "move 500 150"
  );
}

TOUCH_TEST(TouchGriffASecondFingerBeforeTheFormationsWordGivesItUpAtTheWord)
{
  Griff();
  Down(0, 100, 50, 1000);
  TimerRuns();
  MoveFirst(200, 100, 1400);
  Down(1, 400, 100, 1420);
  mouse.events.clear();
  input.Formation(mouse.lastDrag);
  EXPECT_EVENTS("griff drag given up 200 100", "given up");
  // Both rest until they lift: no pinch, no order.
  MoveBoth(260, 100, 450, 100);
  Up(1, 1600);
  Up(0, 1700);
  EXPECT_EVENTS("griff drag given up 200 100", "given up");
}

TOUCH_TEST(TouchGriffTwoFingersAfterAPanAreNoTapOfTwo)
{
  // The first finger panned: with the second still beside it, their quick lift lets nothing go, nor drops the draft.
  Griff();
  Down(0, 100, 100, 1000);
  MoveFirst(150, 100, 1040);
  input.Panned(mouse.lastDrag, 1090);
  Down(1, 400, 100, 1150);
  Up(1, 1250);
  Up(0, 1300);
  WXT_CHECK(!Has(mouse.events, "deselect"));
  mouse.drafting = true;
  Down(0, 100, 100, 2000);
  MoveFirst(150, 100, 2040);
  input.Panned(mouse.lastDrag, 2090);
  Down(1, 400, 100, 2150);
  Up(1, 2250);
  Up(0, 2300);
  WXT_CHECK(!Has(mouse.events, "draft drop"));
}

TOUCH_TEST(TouchGriffATapAndARestAreBoxsByBoxAsItCountedWhenTheFingerCameDown)
{
  // FAF's reach for a unit beside the finger is set at its down, for Box or for the selection: a held Box let go before the
  // lift leaves the tap Box's, and Box held only after the down makes it none of Box's.
  Griff();
  mouse.rail = TouchInput::kRailBox;
  Down(0, 100, 50, 1000);
  mouse.rail = 0;
  Up(0, 1000 + TouchInput::kSettleMilliseconds);
  Down(0, 200, 50, 2000);
  mouse.rail = TouchInput::kRailBox | TouchInput::kRailQueue;
  Up(0, 2000 + TouchInput::kSettleMilliseconds);
  EXPECT_EVENTS("move 100 50", "smart tap 100 50 rail 1", "move 200 50", "smart tap 200 50 rail 2", "rail used 2");
  WXT_CHECK_EQ(mouse.downs, (std::vector<std::string>{"100 50 box", "200 50"}));
  // A rest the same: Box at its down makes it none, whatever Box does by its lift; Box only after the down leaves it one.
  mouse.events.clear();
  mouse.rail = TouchInput::kRailBox;
  Down(0, 300, 50, 3000);
  mouse.rail = 0;
  TimerRuns();
  // With Box no ring.
  WXT_CHECK(!mouse.ringShown);
  Up(0, 3600);
  mouse.rail = 0;
  Down(0, 400, 50, 4000);
  mouse.rail = TouchInput::kRailBox;
  TimerRuns();
  // A ring: Box came after the down.
  WXT_CHECK(mouse.ringShown);
  Up(0, 4600);
  EXPECT_EVENTS(
    "move 300 50", "rest tap 300 50 rail 1", "move 400 50", "held", "griff rest 400 50", "rest tap 400 50 rail 0"
  );
}

TOUCH_TEST(TouchGriffAStrokeFromTheHandleIsARouteEndingWhereItLifted)
{
  Griff();
  mouse.griffX = 100;
  mouse.griffY = 50;
  Down(0, 100, 50, 1000);
  StrokeFirst(100, 50, 400, 50);
  // The screen shows the stroke, and the waypoints its lift gives.
  WXT_CHECK(mouse.shown > 1);
  WXT_CHECK_EQ(mouse.shownMode, touch::stroke::kRoute);
  WXT_CHECK_EQ(mouse.shownWaypoints, 2);
  Up(0, 1500);
  // 300 pixels at 150 a waypoint, ending on the lift; nothing went to the game before it.
  EXPECT_EVENTS("route 250 50, 400 50 rail 0");
  WXT_CHECK_EQ(mouse.shown, 0);
}

TOUCH_TEST(TouchGriffAStrokeEndsWhereTheFingerWasAMomentBeforeItLifted)
{
  // Fingers wobble as they lift: the last 50 ms do not count.
  Griff();
  mouse.griffX = 100;
  mouse.griffY = 50;
  Down(0, 100, 50, 1000);
  for (int at = 1; at <= 30; ++at) {
    MoveFirst(100 + 10 * at, 50, 1000 + 10 * at);
  }
  MoveFirst(420, 70, 1310);
  MoveFirst(440, 90, 1330);
  // 50 ms after the move to 400 50; the one before it, at 390, would count for a lift 10 ms sooner.
  Up(0, 1350);
  EXPECT_EVENTS("route 250 50, 400 50 rail 0");
}

TOUCH_TEST(TouchGriffARouteKeepsTheStrokesCornerAndTheRail)
{
  Griff();
  mouse.griffX = 100;
  mouse.griffY = 50;
  mouse.rail = TouchInput::kRailQueue;
  Down(0, 100, 50, 1000);
  StrokeFirst(100, 50, 160, 50);
  StrokeFirst(160, 50, 160, 300);
  Up(0, 1500);
  // Its points are a mm apart: the corner stays within one.
  const std::vector<std::array<float, 2>> route = Places(At(mouse.events, 0));
  WXT_CHECK(touch::Hypot(route[0][0] - 160, route[0][1] - 50) <= 10);
  WXT_CHECK(At(mouse.events, 0).ends_with("160 300 rail 2"));
  WXT_CHECK_EQ(At(mouse.events, 1), std::string("rail used 2"));
}

TOUCH_TEST(TouchGriffATapOnTheHandleTakesTheCameraToTheSelection)
{
  Griff();
  mouse.griffX = 100;
  mouse.griffY = 50;
  Down(0, 102, 51, 1000);
  MoveFirst(120, 51);
  Up(0, 1100);
  // 1.8 mm is still a tap on it.
  EXPECT_EVENTS("griff tap");
}

TOUCH_TEST(TouchGriffTheHandlesRestThatTheProgramSaysDistributesLiftsAsFafsDistributeOrders)
{
  // FAF's key action G: the program says it deals a queue out for this selection (CanDistribute), and the ring and the label
  // show, the label alone, no route; a wobble within the slop keeps them, the ring with the finger.
  Griff();
  mouse.griffX = 100;
  mouse.griffY = 50;
  Down(0, 102, 50, 1000);
  // The handle's rest.
  WXT_CHECK_EQ(mouse.timer, TouchInput::kRestMilliseconds);
  TimerRuns();
  // The program asked, and nothing shows until it says.
  WXT_CHECK_EQ(mouse.handleRests, 1);
  WXT_CHECK(!mouse.ringShown);
  input.Distributes(mouse.lastHandleRest);
  WXT_CHECK(mouse.ringShown);
  WXT_CHECK_EQ(mouse.shownMode, touch::stroke::kDistribute);
  // The label alone.
  WXT_CHECK_EQ(mouse.shownWaypoints, 0);
  MoveFirst(110, 55, 1400);
  MoveFirst(116, 57, 1410);
  WXT_CHECK_EQ(mouse.shownMode, touch::stroke::kDistribute);
  // The ring follows the finger.
  WXT_CHECK_EQ(mouse.ringX, 116.0f);
  Up(0, 1500);
  EXPECT_EVENTS("held", "distribute");
  WXT_CHECK(!mouse.ringShown);
  WXT_CHECK_EQ(mouse.shown, 0);
  // A word for an earlier rest counts for nothing, and a second finger makes two fingers: the ring goes.
  Down(0, 100, 50, 3000);
  const int earlier = mouse.lastHandleRest;
  TimerRuns();
  input.Distributes(earlier);
  WXT_CHECK(!mouse.ringShown);
  input.Distributes(mouse.lastHandleRest);
  WXT_CHECK(mouse.ringShown);
  Down(1, 300, 50, 3400);
  WXT_CHECK(!mouse.ringShown);
  Up(1, 3450);
  Up(0, 3460);
  EXPECT_EVENTS("held", "distribute", "held", "move 300 50");
}

TOUCH_TEST(TouchGriffTheHandlesRestThatTheProgramDoesNotSayDistributesShowsNothingAndLiftsAsTheHandlesTap)
{
  // So a slow tap on the handle never deals the units' queues out: the camera flies to the group, as for a quick one.
  Griff();
  mouse.griffX = 100;
  mouse.griffY = 50;
  Down(0, 100, 50, 1000);
  TimerRuns();
  WXT_CHECK_EQ(mouse.handleRests, 1);
  WXT_CHECK(!mouse.ringShown);
  Up(0, 1700);
  EXPECT_EVENTS("griff tap");
  // A word after the lift counts for nothing.
  input.Distributes(mouse.lastHandleRest);
  WXT_CHECK(!mouse.ringShown);
  EXPECT_EVENTS("griff tap");
}

TOUCH_TEST(TouchGriffAMovePastTheSlopAfterTheDistributeLabelIsTheStrokeWhichALoopBackGivesUp)
{
  Griff();
  mouse.griffX = 100;
  mouse.griffY = 50;
  Down(0, 100, 50, 1000);
  TimerRuns();
  input.Distributes(mouse.lastHandleRest);
  StrokeFirst(100, 50, 300, 50);
  // The ring went, and the screen says the route's label.
  WXT_CHECK(!mouse.ringShown);
  WXT_CHECK_EQ(mouse.shownMode, touch::stroke::kRoute);
  StrokeFirst(300, 50, 110, 50);
  WXT_CHECK_EQ(mouse.shownMode, touch::stroke::kCancel);
  Up(0, 2000);
  EXPECT_EVENTS("held", "given up");
  // Past the slop before the rest's time: no rest is asked.
  mouse.events.clear();
  Down(0, 100, 50, 3000);
  StrokeFirst(100, 50, 400, 50);
  Up(0, 3500);
  WXT_CHECK_EQ(mouse.handleRests, 1);
  EXPECT_EVENTS("route 250 50, 400 50 rail 0");
}

TOUCH_TEST(TouchGriffTheHandlesWordThatComesAfterTheFingerLeftTheSlopCountsForNothing)
{
  // The finger rested on the handle, then drew past the slop before the program said that its lift distributes: the stroke is
  // the route, with no ring and no label.
  Griff();
  mouse.griffX = 100;
  mouse.griffY = 50;
  Down(0, 100, 50, 1000);
  TimerRuns();
  StrokeFirst(100, 50, 300, 50);
  input.Distributes(mouse.lastHandleRest);
  WXT_CHECK(!mouse.ringShown);
  // The route's label.
  WXT_CHECK_EQ(mouse.shownMode, touch::stroke::kRoute);
  Up(0, 1500);
  EXPECT_EVENTS("route 300 50 rail 0");
}

TOUCH_TEST(TouchGriffAShortStrokeBackToTheHandleIsGivenUp)
{
  Griff();
  mouse.griffX = 100;
  mouse.griffY = 50;
  Down(0, 100, 50, 1000);
  StrokeFirst(100, 50, 200, 50);
  WXT_CHECK_EQ(mouse.shownMode, touch::stroke::kRoute);
  StrokeFirst(200, 50, 110, 50);
  // The screen says lifting gives it up.
  WXT_CHECK_EQ(mouse.shownMode, touch::stroke::kCancel);
  Up(0, 1500);
  EXPECT_EVENTS("given up");
}

TOUCH_TEST(TouchGriffALongLoopBackToTheHandleIsAPatrol)
{
  // 30 mm out, 10 down and back: 70 mm in all, ending 2 mm from its start.
  Griff();
  mouse.griffX = 100;
  mouse.griffY = 50;
  Down(0, 100, 50, 1000);
  StrokeFirst(100, 50, 400, 50);
  StrokeFirst(400, 50, 400, 150);
  StrokeFirst(400, 150, 110, 60);
  WXT_CHECK_EQ(mouse.shownMode, touch::stroke::kPatrol);
  Up(0, 2500);
  const std::string patrol = At(mouse.events, 0);
  WXT_CHECK(patrol.starts_with("patrol "));
  WXT_CHECK(patrol.ends_with("110 60 rail 0"));
  // The loop's corners are its waypoints, within its points' mm.
  bool corner = false;
  for (const std::array<float, 2>& place : Places(patrol)) {
    corner |= touch::Hypot(place[0] - 400, place[1] - 50) <= 10;
  }
  WXT_CHECK(corner);
  WXT_CHECK_EQ(mouse.shown, 0);
}

TOUCH_TEST(TouchGriffASecondFingerGivesTheStrokeUp)
{
  Griff();
  mouse.griffX = 100;
  mouse.griffY = 50;
  Down(0, 100, 50, 1000);
  StrokeFirst(100, 50, 300, 50);
  Down(1, 500, 200, 1300);
  Up(1, 1400);
  Up(0, 1500);
  EXPECT_EVENTS("given up");
  WXT_CHECK_EQ(mouse.shown, 0);
}

TOUCH_TEST(TouchGriffATwoFingerTapWhoseFirstFingerIsOnTheHandleLetsTheSelectionGo)
{
  // The first finger had not left the handle: no stroke was given up, and the two are two fingers, as anywhere else.
  Griff();
  mouse.griffX = 100;
  mouse.griffY = 50;
  Down(0, 102, 50, 1000);
  Down(1, 300, 50, 1020);
  // The stroke went.
  WXT_CHECK_EQ(mouse.shown, 0);
  Up(1, 1120);
  Up(0, 1130);
  EXPECT_EVENTS("move 300 50", "deselect");
  // They pinch where they move.
  mouse.events.clear();
  Down(0, 100, 50, 2000);
  Down(1, 300, 50, 2020);
  MoveBoth(80, 50, 320, 50);
  WXT_CHECK(Has(mouse.events, "pinch start 200 50 240"));
}

TOUCH_TEST(TouchGriffTheCursorHoversAtTheStrokesEndAtMostEachTenthOfASecond)
{
  Griff();
  mouse.griffX = 100;
  mouse.griffY = 50;
  Down(0, 100, 50, 1000);
  for (int at = 1; at <= 25; ++at) {
    MoveFirst(100 + 10 * at, 50, 1000 + 10 * at);
  }
  // At the handle, then at 100 and 200 ms; the place at 250 waits for 300, from the move at 210.
  WXT_CHECK_EQ(mouse.hovers, (std::vector<std::string>{"100 50", "200 50", "300 50"}));
  WXT_CHECK_EQ(mouse.timer, 90);
  TimerRuns();
  WXT_CHECK_EQ(At(mouse.hovers, 3), std::string("350 50"));
  Up(0, 1400);
  WXT_CHECK_EQ(mouse.timer, -1);
}

TOUCH_TEST(TouchGriffPlacesABuildingAsTheMouseDoes)
{
  Griff();
  mouse.placing = true;
  Down(0, 100, 50, 1000);
  Up(0, 1100);
  EXPECT_EVENTS("left down 100 50", "left up 100 50");
}

TOUCH_TEST(TouchATapClicksWhereItLanded)
{
  Down(0, 100, 50, 1000);
  // Within the slop, which does not move the click.
  MoveFirst(105, 50);
  Up(0, 1000 + TouchInput::kSettleMilliseconds);
  EXPECT_EVENTS("move 100 50", "left down 100 50", "left up 100 50");
}

TOUCH_TEST(TouchATapIsATapForTheGamesUiAndGivesAFingerItsHelp)
{
  Down(0, 100, 50, 1000);
  Up(0, 1000 + TouchInput::kSettleMilliseconds);
  WXT_CHECK_EQ(mouse.tapped, 1);
}

TOUCH_TEST(TouchAnOrderByTapIsTheRightButtonWhereItLanded)
{
  input.SetSwapped(true);
  Down(0, 100, 50, 1000);
  Up(0, 1000 + TouchInput::kSettleMilliseconds);
  EXPECT_EVENTS("move 100 50", "right down 100 50", "right up 100 50");
  WXT_CHECK_EQ(mouse.tapped, 0);
}

TOUCH_TEST(TouchThePointerLeavesWhenTheLastFingerHasLifted)
{
  Down(0, 100, 50, 1000);
  // A finger rests, and a tooltip may show.
  WXT_CHECK(!mouse.leaving);
  Up(0, 1200);
  WXT_CHECK(mouse.leaving);
}

TOUCH_TEST(TouchANewFingerKeepsThePointer)
{
  Down(0, 100, 50, 1000);
  Up(0, 1030);
  WXT_CHECK(mouse.leaving);
  Down(0, 300, 60, 1050);
  WXT_CHECK(!mouse.leaving);
}

TOUCH_TEST(TouchAFingerKeepsThePointerAheadOfAllItSends)
{
  Down(0, 100, 50, 1000);
  Up(0, 1030);
  mouse.events.clear();
  // The click that waited, and this finger's place.
  Down(0, 300, 60, 1050);
  WXT_CHECK_EQ(mouse.sentBeforeStay, 0L);
  EXPECT_EVENTS("left down 100 50", "left up 100 50", "move 300 60");
  // A second finger too.
  mouse.events.clear();
  Down(1, 400, 60, 1100);
  WXT_CHECK_EQ(mouse.sentBeforeStay, 0L);
  EXPECT_EVENTS("move 400 60");
}

TOUCH_TEST(TouchAFingerAfterAGestureThatWasCutOffKeepsThePointer)
{
  Down(0, 100, 50, 1000);
  MoveFirst(120, 50);
  fingers = 0;
  mouse.events.clear();
  Down(0, 300, 60, 1500);
  WXT_CHECK_EQ(mouse.sentBeforeStay, 0L);
  WXT_CHECK(!mouse.leaving);
  EXPECT_EVENTS("left up 120 50", "move 300 60");
}

TOUCH_TEST(TouchAFingerWhoseLiftGotLostStillLeavesThePointerToTheNext)
{
  Down(0, 100, 50, 1000);
  fingers = 0;
  Down(0, 300, 60, 1500);
  WXT_CHECK(!mouse.leaving);
}

TOUCH_TEST(TouchThePointerStaysUntilTheLastOfTwoFingersHasLifted)
{
  Down(0, 100, 50, 1000);
  Down(1, 400, 200, 1040);
  Up(1, 1100);
  WXT_CHECK(!mouse.leaving);
  Up(0, 1110);
  WXT_CHECK(mouse.leaving);
}

TOUCH_TEST(TouchThePointerLeavesAfterADragAndAfterAHold)
{
  Down(0, 100, 50, 1000);
  MoveFirst(120, 50);
  Up(0, 1200);
  WXT_CHECK(mouse.leaving);
  Down(0, 100, 50, 2000);
  TimerRuns();
  WXT_CHECK(!mouse.leaving);
  Up(0, 2500);
  WXT_CHECK(mouse.leaving);
}

TOUCH_TEST(TouchWhenTheSystemTakesTheFingersThePointerLeaves)
{
  Down(0, 100, 50, 1000);
  input.Cancel();
  WXT_CHECK(mouse.leaving);
}

TOUCH_TEST(TouchAQuickTapClicksWhenTheCursorHasSettled)
{
  Down(0, 100, 50, 1000);
  Up(0, 1030);
  EXPECT_EVENTS("move 100 50");
  WXT_CHECK_EQ(mouse.timer, TouchInput::kSettleMilliseconds - 30);
  TimerRuns();
  EXPECT_EVENTS("move 100 50", "left down 100 50", "left up 100 50");
}

TOUCH_TEST(TouchANextFingerDoesNotWaitForTheClickBeforeIt)
{
  Down(0, 100, 50, 1000);
  Up(0, 1030);
  Down(0, 300, 60, 1050);
  EXPECT_EVENTS("move 100 50", "left down 100 50", "left up 100 50", "move 300 60");
  WXT_CHECK_EQ(mouse.timer, TouchInput::kHoldMilliseconds);
}

TOUCH_TEST(TouchTwoTapsNearEachOtherClickTheSamePixel)
{
  Down(0, 100, 50, 1000);
  Up(0, 1150);
  Down(0, 112, 58, 1250);
  Up(0, 1400);
  EXPECT_EVENTS(
    "move 100 50", "left down 100 50", "left up 100 50", "move 112 58", "left down 100 50", "left up 100 50"
  );
}

TOUCH_TEST(TouchTwoTapsApartInPlaceOrTimeClickWhereTheyLanded)
{
  Down(0, 100, 50, 1000);
  Up(0, 1150);
  Down(0, 150, 50, 1250);
  Up(0, 1400);
  Down(0, 150, 50, 2000);
  Up(0, 2150);
  EXPECT_EVENTS(
    "move 100 50", "left down 100 50", "left up 100 50", "move 150 50", "left down 150 50", "left up 150 50", "move 150 50",
    "left down 150 50", "left up 150 50"
  );
}

TOUCH_TEST(TouchAFingerThatMovesDragsFromWhereItLanded)
{
  Down(0, 100, 50, 1000);
  MoveFirst(120, 50);
  MoveFirst(140, 60);
  Up(0, 1200);
  EXPECT_EVENTS("move 100 50", "left down 100 50", "move 120 50", "move 140 60", "left up 140 60");
  WXT_CHECK_EQ(mouse.timer, -1);
}

TOUCH_TEST(TouchAFingerThatStaysHoldsTheRightButtonUntilItLifts)
{
  Down(0, 100, 50, 1000);
  TimerRuns();
  EXPECT_EVENTS("move 100 50", "right down 100 50", "held");
  MoveFirst(130, 50);
  Up(0, 1500);
  EXPECT_EVENTS("move 100 50", "right down 100 50", "held", "move 130 50", "right up 130 50");
}

TOUCH_TEST(TouchSwappedTheTapIsTheRightButtonAndTheHoldTheLeft)
{
  input.SetSwapped(true);
  Down(0, 100, 50, 1000);
  Up(0, 1200);
  Down(0, 200, 50, 2000);
  TimerRuns();
  Up(0, 2500);
  EXPECT_EVENTS(
    "move 100 50", "right down 100 50", "right up 100 50", "move 200 50", "left down 200 50", "held", "left up 200 50"
  );
}

TOUCH_TEST(TouchAQuickTapOfTwoFingersClicksTheRightButtonWhereTheSecondCameDown)
{
  Down(0, 100, 50, 1000);
  Down(1, 400, 200, 1040);
  WXT_CHECK_EQ(mouse.timer, -1);
  Up(1, 1040 + TouchInput::kSettleMilliseconds);
  Up(0, 1040 + TouchInput::kSettleMilliseconds + 10);
  EXPECT_EVENTS("move 100 50", "move 400 200", "right down 400 200", "right up 400 200");
}

TOUCH_TEST(TouchTwoFingersThatStayLongAreNoTap)
{
  Down(0, 100, 50, 1000);
  Down(1, 400, 200, 1040);
  Up(1, 1040 + TouchInput::kTwoFingerTapMilliseconds + 1);
  Up(0, 2000);
  EXPECT_EVENTS("move 100 50", "move 400 200");
}

TOUCH_TEST(TouchTwoFingersMovingTogetherDragWithTheMiddleButton)
{
  Down(0, 100, 100, 1000);
  Down(1, 300, 100, 1040);
  MoveBoth(120, 100, 320, 100);
  MoveBoth(150, 110, 350, 110);
  Up(0, 1300);
  Up(1, 1310);
  EXPECT_EVENTS(
    "move 100 100", "move 300 100", "move 200 100", "middle down 200 100", "move 220 100", "move 250 110",
    "middle up 250 110"
  );
}

TOUCH_TEST(TouchTwoFingersMovingApartZoomInAndCloserOut)
{
  Down(0, 100, 100, 1000);
  Down(1, 300, 100, 1040);
  // Starts it, past two slops, without turning the wheel yet.
  MoveBoth(89, 100, 311, 100);
  WXT_CHECK_EQ(mouse.WheelTotal(), 0);
  // Twice as far apart.
  MoveBoth(0, 100, 444, 100);
  const int wheel = mouse.WheelTotal();
  WXT_CHECK(std::abs(wheel - touch::kWheelDelta / 0.4) <= 2);
  // Half as far again.
  MoveBoth(111, 100, 333, 100);
  WXT_CHECK(std::abs(mouse.WheelTotal()) <= 2);
  Up(0, 1500);
  Up(1, 1510);
  WXT_CHECK(FromEnd(mouse.events, 0).starts_with("wheel "));
}

TOUCH_TEST(TouchAPanCanBecomeAZoomAndAZoomAPan)
{
  Down(0, 100, 100, 1000);
  Down(1, 300, 100, 1040);
  MoveBoth(120, 100, 320, 100);
  // Apart by over six slops: the middle button goes up.
  MoveBoth(80, 100, 360, 100);
  WXT_CHECK(Has(mouse.events, "middle up 220 100"));
  MoveBoth(50, 100, 390, 100);
  WXT_CHECK(mouse.WheelTotal() > 0);
  mouse.events.clear();
  // The same distance, somewhere else: the middle button again.
  MoveBoth(130, 100, 470, 100);
  WXT_CHECK(At(mouse.events, 1).starts_with("middle down "));
}

TOUCH_TEST(TouchFingersThatShakeAfterAZoomStillBecomeAPan)
{
  Down(0, 100, 100, 1000);
  Down(1, 300, 100, 1040);
  MoveBoth(60, 100, 340, 100);
  MoveBoth(40, 100, 360, 100);
  WXT_CHECK(mouse.WheelTotal() > 0);
  mouse.events.clear();
  // 16 pixels an event, and the distance a pixel or two off each time, which turns the wheel.
  for (int at = 1; at <= 6; ++at) {
    MoveBoth(static_cast<float>(40 + 16 * at - at % 2), 100, static_cast<float>(360 + 16 * at + at % 2), 100);
  }
  bool middleDown = false;
  for (const std::string& event : mouse.events) {
    middleDown |= event.find("middle down") != std::string::npos;
  }
  WXT_CHECK(middleDown);
}

TOUCH_TEST(TouchOnePinchingFingerStaysAZoom)
{
  Down(0, 100, 100, 1000);
  Down(1, 300, 100, 1040);
  MoveBoth(100, 100, 330, 100);
  for (int at = 1; at <= 20; ++at) {
    MoveBoth(100, 100, static_cast<float>(330 + 30 * at), 100);
  }
  WXT_CHECK(mouse.WheelTotal() > 0);
  for (const std::string& event : mouse.events) {
    WXT_CHECK(event.find("middle") == std::string::npos);
  }
}

TOUCH_TEST(TouchATwoFingerTapClicksWhereTheSecondLandedThoughItWandered)
{
  Down(0, 100, 50, 1000);
  Down(1, 400, 200, 1040);
  input.Move(1, 410, 200);
  input.Moved(++clock);
  Up(1, 1040 + TouchInput::kSettleMilliseconds);
  Up(0, 1040 + TouchInput::kSettleMilliseconds + 10);
  EXPECT_EVENTS("move 100 50", "move 400 200", "right down 400 200", "right up 400 200");
}

TOUCH_TEST(TouchAPalmIsForgottenAndTheOtherFingerGoesOn)
{
  Down(0, 100, 50, 1000);
  Down(1, 400, 200, 1040);
  input.Forget(1, 1100);
  --fingers;
  WXT_CHECK_EQ(mouse.timer, TouchInput::kHoldMilliseconds);
  Up(0, 1100 + TouchInput::kSettleMilliseconds);
  EXPECT_EVENTS("move 100 50", "move 400 200", "move 100 50", "left down 100 50", "left up 100 50");
}

TOUCH_TEST(TouchAPalmThatCameFirstLeavesTheFingerAfterIt)
{
  Down(0, 100, 50, 1000);
  Down(1, 400, 200, 1040);
  input.Forget(0, 1100);
  --fingers;
  Up(1, 1100 + TouchInput::kSettleMilliseconds);
  EXPECT_EVENTS("move 100 50", "move 400 200", "move 400 200", "left down 400 200", "left up 400 200");
}

TOUCH_TEST(TouchAFirstFingerEndsWhatNeverHeardItsEnd)
{
  Down(0, 100, 50, 1000);
  MoveFirst(130, 50);
  // Without an up: the next first finger, as after a window that lost the fingers.
  input.Down(5, 300, 60, 2000, true);
  EXPECT_EVENTS("move 100 50", "left down 100 50", "move 130 50", "left up 130 50", "move 300 60");
  WXT_CHECK_EQ(mouse.timer, TouchInput::kHoldMilliseconds);
}

TOUCH_TEST(TouchAfterACancelWhatIsLeftOfTheFingersDoesNothing)
{
  Down(0, 100, 50, 1000);
  input.Cancel();
  mouse.events.clear();
  input.Move(0, 300, 50);
  input.Moved(++clock);
  Up(0, 1500);
  EXPECT_EVENTS();
}

TOUCH_TEST(TouchFurtherFingersAreIgnored)
{
  Down(0, 100, 50, 1000);
  MoveFirst(130, 50);
  Down(1, 400, 200, 1100);
  input.Move(1, 450, 200);
  input.Moved(++clock);
  Up(1, 1200);
  Up(0, 1300);
  EXPECT_EVENTS("move 100 50", "left down 100 50", "move 130 50", "move 130 50", "left up 130 50");
}

TOUCH_TEST(TouchWhatIsLeftOfAGestureDoesNothing)
{
  Down(0, 100, 50, 1000);
  Down(1, 400, 200, 1040);
  Up(1, 1100);
  mouse.events.clear();
  TimerRuns();
  MoveFirst(300, 50);
  Up(0, 2000);
  EXPECT_EVENTS("right down 400 200", "right up 400 200");
  Down(0, 10, 10, 3000);
  WXT_CHECK_EQ(At(mouse.events, 2), std::string("move 10 10"));
}

TOUCH_TEST(TouchACancelReleasesWhatIsHeld)
{
  Down(0, 100, 50, 1000);
  MoveFirst(130, 50);
  input.Cancel();
  // The system's cancel takes them all.
  fingers = 0;
  EXPECT_EVENTS("move 100 50", "left down 100 50", "move 130 50", "left up 130 50");
  Down(0, 10, 10, 2000);
  WXT_CHECK_EQ(mouse.timer, TouchInput::kHoldMilliseconds);
}

TOUCH_TEST(TouchWhilePlacingATapPutsTheGhostThereAndATapOnItPlacesIt)
{
  mouse.placing = true;
  Down(0, 500, 300, 1000);
  Up(0, 1050);
  EXPECT_EVENTS("left down 500 300", "left up 500 300");
  mouse.events.clear();
  // Within the reach of six slops.
  Down(0, 530, 320, 2000);
  Up(0, 2050);
  EXPECT_EVENTS("tap 530 320 near ghost 500 300");
}

TOUCH_TEST(TouchWhilePlacingATapAwayMovesTheGhost)
{
  mouse.placing = true;
  Down(0, 500, 300, 1000);
  Up(0, 1050);
  mouse.events.clear();
  Down(0, 900, 300, 2000);
  Up(0, 2050);
  EXPECT_EVENTS("left down 900 300", "left up 900 300");
}

TOUCH_TEST(TouchWhilePlacingAFingerTakesTheGhostAlongFromAnywhere)
{
  mouse.placing = true;
  Down(0, 500, 300, 1000);
  Up(0, 1050);
  mouse.events.clear();
  Down(0, 1000, 600, 2000);
  MoveFirst(1020, 600);
  MoveFirst(1020, 610);
  Up(0, 2200);
  EXPECT_EVENTS("move 520 300", "move 520 310");
  WXT_CHECK_EQ(mouse.timer, -1);
}

TOUCH_TEST(TouchWhilePlacingTheFirstDragStartsAboveTheFinger)
{
  mouse.placing = true;
  Down(0, 500, 300, 1000);
  MoveFirst(520, 300);
  Up(0, 1200);
  EXPECT_EVENTS("move 520 180");
}

TOUCH_TEST(TouchWhilePlacingAFingerThatStaysShowsTheGhostAboveItAndPlacesNothing)
{
  mouse.placing = true;
  Down(0, 500, 300, 1000);
  TimerRuns();
  Up(0, 5000);
  EXPECT_EVENTS("move 500 180", "held");
}

TOUCH_TEST(TouchWhilePlacingAFingerThatStaysMovesTheGhostByTheSmallestSteps)
{
  mouse.placing = true;
  Down(0, 500, 300, 1000);
  Up(0, 1050);
  mouse.events.clear();
  Down(0, 1000, 600, 2000);
  // Within the slop, which counts only for a tap.
  MoveFirst(1003, 600);
  TimerRuns();
  MoveFirst(1005, 600);
  MoveFirst(1006, 601);
  Up(0, 3000);
  EXPECT_EVENTS("move 503 300", "held", "move 505 300", "move 506 301");
  // Still a ghost to tap, where it is now.
  mouse.events.clear();
  Down(0, 510, 300, 4000);
  Up(0, 4050);
  EXPECT_EVENTS("tap 510 300 near ghost 506 301");
}

TOUCH_TEST(TouchWhilePlacingAFingerThatStaysOnTheGhostThenMovesDrawsALine)
{
  PutGhost();
  Down(0, 510, 300, 2000);
  TimerRuns();
  EXPECT_EVENTS("held");
  MoveFirst(600, 300);
  MoveFirst(700, 320);
  Up(0, 3000);
  EXPECT_EVENTS("held", "line from 500 300", "move 590 300", "move 690 320", "left up 690 320");
}

TOUCH_TEST(TouchWhilePlacingAFingerThatStaysOnTheGhostAndLiftsDoesNothing)
{
  PutGhost();
  Down(0, 500, 300, 2000);
  // Within the slop.
  MoveFirst(503, 300);
  TimerRuns();
  MoveFirst(504, 300);
  Up(0, 3000);
  EXPECT_EVENTS("held");
  mouse.events.clear();
  Down(0, 505, 300, 4000);
  Up(0, 4050);
  EXPECT_EVENTS("tap 505 300 near ghost 500 300");
}

TOUCH_TEST(TouchWhilePlacingAQuickDragOfTheGhostTakesItAlong)
{
  PutGhost();
  Down(0, 500, 300, 2000);
  MoveFirst(600, 300);
  Up(0, 2100);
  EXPECT_EVENTS("move 600 300");
}

TOUCH_TEST(TouchWhilePlacingASecondFingerGivesUpTheLineAndTheGhostGoesBack)
{
  StartLineTo700();
  Down(1, 100, 100, 2500);
  MoveFirst(800, 300);
  Up(0, 2600);
  Up(1, 2700);
  EXPECT_EVENTS("held", "line from 500 300", "move 700 300", "line given up", "move 500 300");
  mouse.events.clear();
  Down(0, 500, 300, 3000);
  Up(0, 3050);
  EXPECT_EVENTS("tap 500 300 near ghost 500 300");
}

TOUCH_TEST(TouchWhilePlacingTheSystemTakingTheFingersGivesUpTheLine)
{
  StartLineTo700();
  input.Cancel();
  EXPECT_EVENTS("held", "line from 500 300", "move 700 300", "line given up", "move 500 300");
}

TOUCH_TEST(TouchWhilePlacingTheTapAfterALinePutsTheGhostThoughItIsNear)
{
  StartLineTo700();
  Up(0, 2600);
  mouse.events.clear();
  Down(0, 705, 300, 4000);
  Up(0, 4050);
  EXPECT_EVENTS("left down 705 300", "left up 705 300");
}

TOUCH_TEST(TouchWhilePlacingAFingerThatStaysOnAUsedGhostMovesIt)
{
  PutGhost();
  Down(0, 500, 300, 2000);
  Up(0, 2050);
  mouse.events.clear();
  Down(0, 500, 300, 3000);
  TimerRuns();
  MoveFirst(504, 300);
  Up(0, 4000);
  EXPECT_EVENTS("move 500 300", "held", "move 504 300");
}

TOUCH_TEST(TouchWhilePlacingTwoFingersOnAFingerThatStayedOnTheGhostMoveTheCamera)
{
  PutGhost();
  Down(0, 500, 300, 2000);
  TimerRuns();
  Down(1, 700, 300, 2400);
  MoveBoth(540, 340, 740, 340);
  Up(0, 2600);
  Up(1, 2700);
  EXPECT_EVENTS(
    "held", "move 700 300", "move 600 300", "middle down 600 300", "move 640 340", "middle up 640 340", "move 500 300"
  );
}

TOUCH_TEST(TouchWhilePlacingAPalmLeavesTheOtherFingerToTheGhost)
{
  mouse.placing = true;
  Down(0, 500, 300, 1000);
  Up(0, 1050);
  mouse.events.clear();
  Down(0, 1000, 600, 2000);
  Down(1, 1200, 600, 2020);
  input.Forget(1, 2040);
  --fingers;
  MoveFirst(1040, 600);
  Up(0, 2200);
  // Last: the cursor back to the ghost, as for any two fingers.
  EXPECT_EVENTS("move 1200 600", "move 540 300", "move 540 300");
}

TOUCH_TEST(TouchWhilePlacingAQuickTapOfTwoFingersIsTheRightButtonThoughSwapped)
{
  input.SetSwapped(true);
  mouse.placing = true;
  Down(0, 500, 300, 1000);
  Down(1, 700, 300, 1020);
  Up(0, 1150);
  Up(1, 1160);
  EXPECT_EVENTS("move 700 300", "right down 700 300", "right up 700 300");
}

TOUCH_TEST(TouchWhilePlacingTheTapAfterAPlacingPutsTheGhostThoughItIsNear)
{
  mouse.placing = true;
  Down(0, 500, 300, 1000);
  Up(0, 1050);
  Down(0, 500, 300, 2000);
  Up(0, 2050);
  mouse.events.clear();
  Down(0, 530, 320, 3000);
  Up(0, 3050);
  EXPECT_EVENTS("left down 530 320", "left up 530 320");
  // That one is a ghost to tap again.
  mouse.events.clear();
  Down(0, 510, 310, 4000);
  Up(0, 4050);
  EXPECT_EVENTS("tap 510 310 near ghost 530 320");
}

TOUCH_TEST(TouchWhilePlacingAFingerTakesTheGhostOnFromWhereABuildingWentUp)
{
  mouse.placing = true;
  Down(0, 500, 300, 1000);
  Up(0, 1050);
  Down(0, 500, 300, 2000);
  Up(0, 2050);
  mouse.events.clear();
  Down(0, 1000, 600, 3000);
  MoveFirst(1040, 600);
  Up(0, 3200);
  EXPECT_EVENTS("move 540 300");
  // Moved, it is a ghost to tap again.
  mouse.events.clear();
  Down(0, 550, 300, 4000);
  Up(0, 4050);
  EXPECT_EVENTS("tap 550 300 near ghost 540 300");
}

TOUCH_TEST(TouchWhilePlacingTheCameraMakesAUsedGhostOneToTapAgain)
{
  mouse.placing = true;
  Down(0, 500, 300, 1000);
  Up(0, 1050);
  Down(0, 500, 300, 2000);
  Up(0, 2050);
  Down(0, 100, 100, 3000);
  Down(1, 300, 100, 3040);
  MoveBoth(120, 100, 320, 100);
  Up(0, 3300);
  Up(1, 3310);
  mouse.events.clear();
  Down(0, 510, 300, 4000);
  Up(0, 4050);
  EXPECT_EVENTS("tap 510 300 near ghost 500 300");
}

TOUCH_TEST(TouchWhilePlacingTheGhostStaysOnTheScreen)
{
  input.SetScreen(2400, 1080);
  mouse.placing = true;
  // Above the finger is above the screen.
  Down(0, 100, 50, 1000);
  MoveFirst(130, 50);
  MoveFirst(130, 60);
  Up(0, 1200);
  EXPECT_EVENTS("move 130 0", "move 130 10");
  mouse.events.clear();
  // It follows the finger back at once, however far the finger went.
  Down(0, 2000, 500, 2000);
  MoveFirst(1500, 500);
  MoveFirst(1550, 500);
  MoveFirst(1560, 1050);
  MoveFirst(1560, 1000);
  Up(0, 2500);
  EXPECT_EVENTS("move 0 10", "move 50 10", "move 60 560", "move 60 510");
  mouse.events.clear();
  Down(0, 100, 100, 3000);
  MoveFirst(2500, 700);
  MoveFirst(2490, 690);
  Up(0, 3500);
  EXPECT_EVENTS("move 2400 1080", "move 2390 1070");
}

TOUCH_TEST(TouchWhilePlacingTwoFingersMoveTheCameraAndTheGhostComesBack)
{
  mouse.placing = true;
  Down(0, 500, 300, 1000);
  Up(0, 1050);
  mouse.events.clear();
  Down(0, 100, 100, 2000);
  Down(1, 300, 100, 2040);
  MoveBoth(120, 100, 320, 100);
  Up(0, 2300);
  Up(1, 2310);
  EXPECT_EVENTS(
    "move 300 100", "move 200 100", "middle down 200 100", "move 220 100", "middle up 220 100", "move 500 300"
  );
}

TOUCH_TEST(TouchWhilePlacingACancelBringsTheCursorBackToTheGhost)
{
  mouse.placing = true;
  Down(0, 500, 300, 1000);
  Up(0, 1050);
  mouse.events.clear();
  Down(0, 100, 100, 2000);
  Down(1, 300, 100, 2040);
  MoveBoth(120, 100, 320, 100);
  input.Cancel();
  EXPECT_EVENTS(
    "move 300 100", "move 200 100", "middle down 200 100", "move 220 100", "middle up 220 100", "move 500 300"
  );
}

TOUCH_TEST(TouchWhilePlacingAFirstFingerBringsTheCursorBackWhenTheEndWentUnheard)
{
  mouse.placing = true;
  Down(0, 500, 300, 1000);
  Up(0, 1050);
  Down(0, 100, 100, 2000);
  Down(1, 300, 100, 2040);
  MoveBoth(120, 100, 320, 100);
  // The other's end does not come.
  Up(0, 2300);
  fingers = 0;
  mouse.events.clear();
  Down(0, 900, 600, 3000);
  EXPECT_EVENTS("move 500 300");
}

TOUCH_TEST(TouchTheGhostIsForgottenWhenThePlacingIsOver)
{
  mouse.placing = true;
  Down(0, 500, 300, 1000);
  Up(0, 1050);
  mouse.placing = false;
  Down(0, 10, 10, 2000);
  Up(0, 2200);
  mouse.events.clear();
  mouse.placing = true;
  // A new ghost starts above the finger again, not from where the last one was.
  Down(0, 520, 300, 3000);
  MoveFirst(560, 300);
  Up(0, 3300);
  EXPECT_EVENTS("move 560 180");
}

TOUCH_TEST(TouchDeletingATapDeletesWhenTheCursorHasSettled)
{
  input.SetDeleting(true);
  Down(0, 500, 300, 1000);
  Up(0, 1030);
  EXPECT_EVENTS("move 500 300");
  TimerRuns();
  EXPECT_EVENTS("move 500 300", "delete 500 300");
  // Over a ghost as well: it is the deleting that was asked for.
  mouse.events.clear();
  mouse.placing = true;
  Down(0, 600, 300, 2000);
  Up(0, 2200);
  EXPECT_EVENTS("move 600 300", "delete 600 300");
}

TOUCH_TEST(TouchDeletingAFingerThatStaysOrWandersStillOnlyDeletes)
{
  input.SetDeleting(true);
  Down(0, 500, 300, 1000);
  TimerRuns();
  MoveFirst(560, 300);
  Up(0, 1500);
  EXPECT_EVENTS("move 500 300", "delete 500 300");
}

// --- Griff's draft (buildstage/griff-v3-spec.md T2) --------------------------------------------------------------------

TOUCH_TEST(TouchGriffDraftATapPlacesAPlanWhereTheFingerCameDown)
{
  OpenDraft({});
  Down(0, 100, 50, 1000);
  MoveFirst(110, 55);
  Up(0, 1000 + TouchInput::kSettleMilliseconds);
  EXPECT_EVENTS("move 100 50", "draft tap 100 50");
  // The program hears the finger, as for any tap.
  WXT_CHECK_EQ(mouse.downs, std::vector<std::string>{"100 50"});
}

TOUCH_TEST(TouchGriffDraftATapOnAPlanRemovesIt)
{
  OpenDraft({{7, 100, 50, 0}, {8, 300, 50, 0}});
  Down(0, 110, 60, 1000);
  Up(0, 1000 + TouchInput::kSettleMilliseconds);
  EXPECT_EVENTS("move 110 60", "draft tap 110 60 on 7");
}

TOUCH_TEST(TouchGriffDraftATapIsNoActionForTheRailAndClimbsNoLadder)
{
  // Plus stays lit for the tick; a second tap soon after and near is another plan, not a rung.
  OpenDraft({});
  mouse.rail = TouchInput::kRailQueue;
  Down(0, 100, 50, 1000);
  Up(0, 1000 + TouchInput::kSettleMilliseconds);
  Down(0, 110, 50, 1200);
  Up(0, 1200 + TouchInput::kSettleMilliseconds);
  EXPECT_EVENTS("move 100 50", "draft tap 100 50", "move 110 50", "draft tap 110 50");
}

TOUCH_TEST(TouchGriffDraftHasNoHandle)
{
  OpenDraft({});
  mouse.griffX = 100;
  mouse.griffY = 50;
  Down(0, 100, 50, 1000);
  StrokeFirst(100, 50, 300, 50);
  Up(0, 1500);
  for (const std::string& event : mouse.events) {
    WXT_CHECK(event.find("route") == std::string::npos);
  }
  WXT_CHECK_EQ(mouse.shown, -1);
}

TOUCH_TEST(TouchGriffDraftADragFromTheNewestPlanDrawsALineToWhereTheFingerSettled)
{
  OpenDraft({{1, 50, 50, 0}, {2, 100, 50, 0}});
  Down(0, 100, 50, 1000);
  MoveFirst(130, 50, 1010);
  MoveFirst(200, 50, 1020);
  // It lifts 40 ms after its last move, 50 ms after the one before.
  Up(0, 1060);
  EXPECT_EVENTS("move 100 50", "draft line show 130 50", "draft line show 200 50", "draft line end 130 50");
}

TOUCH_TEST(TouchGriffDraftADragFromAnOlderPlanMovesTheWholeDraft)
{
  OpenDraft({{1, 50, 50, 0}, {2, 100, 50, 0}});
  Down(0, 55, 50, 1000);
  MoveFirst(90, 60, 1010);
  Up(0, 1100);
  EXPECT_EVENTS("move 55 50", "draft move show 55 50 to 90 60", "draft move end 55 50 to 90 60");
}

TOUCH_TEST(TouchGriffDraftADragFromAPlanOnADepositOrFromNothingPansWhateverTheRailSays)
{
  // An extractor's plan stays on its deposit: no line, no move. Box with another building pans too (the mockup's).
  OpenDraft({{1, 100, 50, 1}});
  mouse.rail = TouchInput::kRailBox;
  Down(0, 100, 50, 1000);
  MoveFirst(150, 50, 1010);
  Up(0, 1200);
  EXPECT_EVENTS("move 100 50", "griff drag 100 50 now 150 50 rail 0", "move 150 50", "griff drag end 150 50");
  mouse.events.clear();
  Down(0, 300, 50, 2000);
  MoveFirst(350, 50, 2010);
  WXT_CHECK_EQ(At(mouse.events, 1), std::string("griff drag 300 50 now 350 50 rail 0"));
}

TOUCH_TEST(TouchGriffDraftBoxWithAnExtractorPickedIsABoxOverDepositsFromAnywhere)
{
  OpenDraft({{1, 100, 50, 0}});
  mouse.picksDeposit = true;
  mouse.rail = TouchInput::kRailBox;
  Down(0, 100, 50, 1000);
  MoveFirst(200, 120, 1010);
  Up(0, 1100);
  EXPECT_EVENTS("move 100 50", "draft box show 100 50 to 200 120", "draft box end 100 50 to 200 120", "rail used 1");
}

TOUCH_TEST(TouchGriffDraftARestOnNoPlanAimsAndItsLiftPlacesWhereTheFingerSettled)
{
  OpenDraft({{1, 400, 50, 0}});
  Down(0, 100, 50, 1000);
  // Griff's rest asks whether the finger is on FAF's UI; on the map the mockup's REST_LOUPE, 400 ms, is the aim's.
  WXT_CHECK_EQ(mouse.timer, TouchInput::kRestMilliseconds);
  TimerRuns();
  WXT_CHECK_EQ(mouse.draftRests, std::vector<std::string>{"100 50"});
  // The mockup's REST_LOUPE.
  WXT_CHECK_EQ(mouse.timer, 400 - TouchInput::kRestMilliseconds);
  TimerRuns();
  MoveFirst(104, 52, 1410);
  MoveFirst(130, 60, 1500);
  Up(0, 1520);
  EXPECT_EVENTS(
    "move 100 50", "draft aim show 100 50", "held", "draft aim show 104 52", "draft aim show 130 60", "draft aim end 104 52"
  );
}

TOUCH_TEST(TouchGriffDraftARestOnFafsUiIsThePanelsRestAsOutsideTheDraft)
{
  // At Griff's 300 ms the program says the finger is on FAF's UI: the ring and the panel's rest, whose lift is FAF's right
  // click there; no aim and no loupe, which are the map's, 100 ms later.
  OpenDraft({{1, 400, 50, 0}});
  Down(0, 100, 50, 1000);
  TimerRuns();
  input.DraftRestOnUi(mouse.lastDraftRest);
  // Its ring, and no aim to come.
  WXT_CHECK(mouse.ringShown);
  WXT_CHECK_EQ(mouse.timer, -1);
  Up(0, 1500);
  EXPECT_EVENTS("move 100 50", "held", "griff rest 100 50", "rest tap 100 50 rail 0");
  // With Box no ring, as outside the draft, and its lift is Box's on the panel.
  mouse.events.clear();
  mouse.rail = TouchInput::kRailBox;
  Down(0, 100, 50, 2000);
  TimerRuns();
  input.DraftRestOnUi(mouse.lastDraftRest);
  WXT_CHECK(!mouse.ringShown);
  Up(0, 2500);
  EXPECT_EVENTS("move 100 50", "rest tap 100 50 rail 1");
  // A word that comes as the aim began counts for nothing: the aim's lift on FAF's UI is the right click there (the
  // program's, DraftAimEndTask).
  mouse.events.clear();
  mouse.rail = 0;
  Down(0, 100, 50, 3000);
  TimerRuns();
  const int late = mouse.lastDraftRest;
  TimerRuns();
  input.DraftRestOnUi(late);
  Up(0, 3600);
  EXPECT_EVENTS("move 100 50", "draft aim show 100 50", "held", "draft aim end 100 50");
}

TOUCH_TEST(TouchGriffDraftARestOnFafsUiLeavesALitPlusForTheTick)
{
  // As the draft's tap there: the panel's right click takes no Plus, which stays lit for the tick, whose plans it appends to
  // the builders' queues; nor does the rest's slide, the panel's drag.
  OpenDraft({{1, 400, 50, 0}});
  mouse.rail = TouchInput::kRailQueue;
  Down(0, 100, 50, 1000);
  TimerRuns();
  input.DraftRestOnUi(mouse.lastDraftRest);
  Up(0, 1500);
  EXPECT_EVENTS("move 100 50", "held", "griff rest 100 50", "rest tap 100 50 rail 0");
  mouse.events.clear();
  Down(0, 100, 50, 2000);
  TimerRuns();
  input.DraftRestOnUi(mouse.lastDraftRest);
  MoveFirst(160, 50, 2400);
  Up(0, 2500);
  EXPECT_EVENTS(
    "move 100 50", "held", "griff rest 100 50", "griff drag 100 50 now 160 50 rested rail 0", "move 160 50",
    "griff drag end 160 50"
  );
  // The next rest outside the draft is the rail's as ever.
  mouse.drafting = false;
  mouse.events.clear();
  Down(0, 100, 50, 3000);
  TimerRuns();
  Up(0, 3500);
  EXPECT_EVENTS("move 100 50", "held", "griff rest 100 50", "rest tap 100 50 rail 2", "rail used 2");
}

TOUCH_TEST(TouchGriffDraftARestOnAPlanOrWithBoxIsNoAimAndItsLiftIsATap)
{
  OpenDraft({{1, 100, 50, 0}});
  Down(0, 100, 50, 1000);
  TimerRuns();
  Up(0, 1600);
  EXPECT_EVENTS("move 100 50", "draft tap 100 50 on 1");
  mouse.events.clear();
  mouse.rail = TouchInput::kRailBox;
  Down(0, 300, 50, 2000);
  TimerRuns();
  Up(0, 2600);
  EXPECT_EVENTS("move 300 50", "draft tap 300 50");
}

TOUCH_TEST(TouchGriffTheFingersRestWhileTheProgramsClicksRun)
{
  // The tick's build mode is FAF's ghost to the layer, and the draft is open: a finger there would place the ghost or a plan,
  // between the tick's clicks or the undo's.
  OpenDraft({{1, 100, 50, 0}});
  mouse.placing = true;
  mouse.clicking = true;
  TapRestDragAndTwo(100, 50);
  EXPECT_EVENTS();
  // Only the first fingers' downs went, which the program takes for nothing then.
  WXT_CHECK_EQ(mouse.downs.size(), std::size_t{5});
  WXT_CHECK_EQ(mouse.timer, -1);
}

TOUCH_TEST(TouchGriffTheFingersRestWhileTheProgramsClicksRunAlsoOnTheHandleAndOnTheMap)
{
  Griff();
  mouse.clicking = true;
  mouse.griffX = 100;
  mouse.griffY = 50;
  TapRestDragAndTwo(100, 50);
  TapRestDragAndTwo(400, 300);
  EXPECT_EVENTS();
  // No stroke.
  WXT_CHECK_EQ(mouse.shown, -1);
  WXT_CHECK(mouse.hovers.empty());
  // Once they are over, a finger's gestures are its own again.
  mouse.clicking = false;
  Down(0, 400, 300, 9000);
  Up(0, 9040);
  TimerRuns();
  EXPECT_EVENTS("move 400 300", "smart tap 400 300 rail 0");
}

TOUCH_TEST(TouchGriffAFingerDownBeforeTheProgramsClicksBeganMovesNoCursorWhileTheyRun)
{
  // A pan held from before the undo's clicks: FAF's camera drag takes each move of the cursor then, the undo's aims too, and
  // FAF's cursor is the undo's to aim with. A finger still pending when they begin is the same once it drags.
  Griff();
  Down(0, 100, 50, 1000);
  MoveFirst(150, 50);
  mouse.clicking = true;
  mouse.events.clear();
  MoveFirst(200, 50);
  MoveFirst(250, 50);
  for (const std::string& event : mouse.events) {
    WXT_CHECK(!event.starts_with("move "));
  }
  mouse.clicking = false;
  Up(0, 1300);
  Down(0, 400, 300, 3000);
  mouse.clicking = true;
  mouse.events.clear();
  MoveFirst(460, 300);
  MoveFirst(500, 300);
  for (const std::string& event : mouse.events) {
    WXT_CHECK(!event.starts_with("move "));
  }
}

TOUCH_TEST(TouchGriffAFingerOnFafsGhostBeforeTheProgramsClicksBeganClicksNothingWhileTheyRun)
{
  // FAF's ghost waited (a build key's mode) as the finger came down; the pill's undo then ended that mode and arms its own:
  // the finger's lift would be a press in the undo's mode, with its selection and Shift, at the finger.
  Griff();
  mouse.placing = true;
  Down(0, 500, 300, 1000);
  mouse.placing = false;
  mouse.clicking = true;
  mouse.events.clear();
  Up(0, 1050);
  for (const std::string& event : mouse.events) {
    WXT_CHECK(!(event.starts_with("left ") || event.starts_with("tap ")));
  }
}

TOUCH_TEST(TouchGriffAGestureTheProgramsClicksCameIntoGivesNothingOnceTheyAreOver)
{
  // A finger rests on the map as the other thumb ticks: the tick's clicks run and end while it stays. Its lift would be a tap,
  // FAF's right click there, which would give the builders another order in place of their buildings.
  Griff();
  Down(0, 400, 300, 1000);
  mouse.ClicksBegin();
  mouse.clicking = false;
  mouse.events.clear();
  Up(0, 1080);
  WXT_CHECK_EQ(mouse.timer, -1);
  // A rest that would draw a box: its ring goes, and nothing is pressed.
  Down(0, 400, 300, 2000);
  TimerRuns();
  WXT_CHECK(mouse.ringShown);
  mouse.ClicksBegin();
  mouse.clicking = false;
  MoveFirst(460, 300);
  WXT_CHECK(!mouse.ringShown);
  Up(0, 2600);
  EXPECT_EVENTS("move 400 300", "held", "griff rest 400 300", "rest gone");
  // A rest that clicks began before: no ring, nothing to feel.
  mouse.events.clear();
  Down(0, 400, 300, 2800);
  mouse.ClicksBegin();
  mouse.clicking = false;
  TimerRuns();
  EXPECT_EVENTS("move 400 300");
  WXT_CHECK(!mouse.ringShown);
  Up(0, 2900);
  // A tap whose click waits for the cursor's rest, where clicks begin meanwhile; and one whose click the next finger's down
  // would make.
  mouse.events.clear();
  Down(0, 400, 300, 3000);
  Up(0, 3040);
  mouse.ClicksBegin();
  mouse.clicking = false;
  TimerRuns();
  EXPECT_EVENTS("move 400 300");
  Down(0, 400, 300, 3200);
  Up(0, 3240);
  mouse.ClicksBegin();
  mouse.clicking = false;
  Down(0, 600, 300, 3300);
  Up(0, 3500);
  EXPECT_EVENTS("move 400 300", "move 400 300", "move 600 300", "smart tap 600 300 rail 0");
  // The next gesture is the finger's own again.
  mouse.events.clear();
  Down(0, 400, 300, 4000);
  Up(0, 4040);
  TimerRuns();
  EXPECT_EVENTS("move 400 300", "smart tap 400 300 rail 0");
}

TOUCH_TEST(TouchGriffALineOnFafsGhostThatTheProgramsClicksCameIntoReleasesNothing)
{
  // The clicks gave the line up as they began (FAF's build drag cancelled) and took the ghost's mode: the line's finger, still
  // all the while, moves and lifts once they are over, with nothing more of the line.
  Griff();
  StartLineTo700();
  mouse.ClicksBegin();
  mouse.clicking = false;
  mouse.events.clear();
  MoveFirst(760, 300);
  Up(0, 2600);
  EXPECT_EVENTS();
  // A second finger joins a gesture given up, and the two rest.
  mouse.placing = false;
  Down(0, 400, 300, 3000);
  mouse.ClicksBegin();
  mouse.events.clear();
  Down(1, 600, 300, 3030);
  MoveBoth(420, 300, 640, 300);
  Up(1, 3100);
  Up(0, 3110);
  EXPECT_EVENTS();
}

TOUCH_TEST(TouchGriffWhatTheSystemTakesOfAGestureTheProgramsClicksCameIntoGivesNothing)
{
  // A line on FAF's ghost that the system takes, as clicks ran: the clicks gave it up already.
  Griff();
  StartLineTo700();
  mouse.ClicksBegin();
  mouse.clicking = false;
  mouse.events.clear();
  input.Cancel();
  EXPECT_EVENTS();
  Up(0, 2600);
  // Two fingers of which the system forgets one, a palm, once clicks ran: the one left is no gesture of its own.
  mouse.placing = false;
  Down(0, 300, 300, 3000);
  Down(1, 500, 300, 3010);
  mouse.ClicksBegin();
  mouse.clicking = false;
  mouse.events.clear();
  input.Forget(1, 3050);
  MoveFirst(360, 300, 3060);
  EXPECT_EVENTS();
}

TOUCH_TEST(TouchGriffAGestureTheProgramsClicksCameIntoLeavesTheCursorWhereTheyPutIt)
{
  // Two fingers came down over FAF's ghost and took the cursor from it, which their last lift would take back; and a first
  // finger's down comes while a drag still goes on, as where the system lost its lift.
  Griff();
  PutGhost();
  Down(0, 500, 300, 2000);
  Down(1, 700, 300, 2010);
  mouse.ClicksBegin();
  mouse.clicking = false;
  mouse.events.clear();
  MoveBoth(480, 300, 720, 300);
  Up(1, 2100);
  Up(0, 2110);
  EXPECT_EVENTS();
  mouse.placing = false;
  Down(0, 100, 50, 3000);
  MoveFirst(150, 50);
  mouse.ClicksBegin();
  mouse.clicking = false;
  mouse.events.clear();
  input.Down(5, 900, 300, 3100, true);
  EXPECT_EVENTS("move 900 300");
}

TOUCH_TEST(TouchGriffTheTapLadderClimbsNoRungOverAGestureTheProgramsClicksCameInto)
{
  Griff();
  Down(0, 100, 50, 1000);
  Up(0, 1040);
  TimerRuns();
  input.TapAnswered(mouse.lastTap, TouchInput::kTapOwn);
  Down(0, 100, 50, 1100);
  mouse.ClicksBegin();
  mouse.clicking = false;
  Up(0, 1140);
  mouse.events.clear();
  Down(0, 100, 50, 1200);
  Up(0, 1240);
  TimerRuns();
  EXPECT_EVENTS("move 100 50", "smart tap 100 50 rail 0");
}

TOUCH_TEST(TouchGriffAGestureTheProgramsClicksCameIntoTakesWhatItShowsAway)
{
  // A stroke from the handle is given up, as by a second finger; the draft's line is put away, as by one; a pinch lets the
  // camera go.
  Griff();
  mouse.griffX = 100;
  mouse.griffY = 50;
  Down(0, 100, 50, 1000);
  StrokeFirst(100, 50, 300, 50);
  mouse.ClicksBegin();
  mouse.events.clear();
  MoveFirst(320, 50);
  Up(0, 1500);
  EXPECT_EVENTS("given up");
  // No stroke shown.
  WXT_CHECK_EQ(mouse.shown, 0);
  mouse.clicking = false;
  OpenDraft({{1, 100, 50, 0}});
  mouse.events.clear();
  Down(0, 100, 50, 2000);
  MoveFirst(150, 50, 2010);
  mouse.ClicksBegin();
  MoveFirst(200, 50, 2020);
  Up(0, 2100);
  EXPECT_EVENTS("move 100 50", "draft line show 150 50", "draft line abort");
  mouse.clicking = false;
  mouse.drafting = false;
  mouse.events.clear();
  Down(0, 300, 300, 3000);
  Down(1, 500, 300, 3010);
  MoveBoth(280, 300, 520, 300);
  mouse.ClicksBegin();
  MoveBoth(260, 300, 540, 300);
  Up(1, 3100);
  Up(0, 3110);
  EXPECT_EVENTS("move 300 300", "move 500 300", "pinch start 400 300 240", "pinch end");
}

TOUCH_TEST(TouchTheMouseSchemeAsksNothingOfTheProgramsClicks)
{
  // Griff's clicks never run with the mouse scheme, and its fingers go on as they always did.
  mouse.ClicksBegin();
  Down(0, 100, 50, 1000);
  MoveFirst(120, 50);
  mouse.ClicksBegin();
  MoveFirst(140, 60);
  Up(0, 1200);
  EXPECT_EVENTS("move 100 50", "left down 100 50", "move 120 50", "move 140 60", "left up 140 60");
}

TOUCH_TEST(TouchGriffDraftASecondFingerGivesTheLineUpAndTheTwoPinchWithoutATap)
{
  OpenDraft({{1, 100, 50, 0}});
  Down(0, 100, 50, 1000);
  MoveFirst(150, 50, 1010);
  Down(1, 350, 50, 1020);
  Up(1, 1060);
  Up(0, 1070);
  EXPECT_EVENTS(
    "move 100 50", "draft line show 150 50", "draft line abort", "move 350 50", "pinch start 250 50 200",
    "pinch start 150 50 0", "pinch end"
  );
  WXT_CHECK(!Has(mouse.events, "draft drop"));
}

TOUCH_TEST(TouchGriffDraftASecondFingerPutsAMoveBackAndEndsAnAim)
{
  OpenDraft({{1, 50, 50, 0}, {2, 100, 50, 0}});
  Down(0, 50, 50, 1000);
  MoveFirst(90, 50, 1010);
  Down(1, 350, 50, 1020);
  WXT_CHECK(Has(mouse.events, "draft move abort"));
  Up(1, 1500);
  Up(0, 1510);
  mouse.events.clear();
  Down(0, 200, 50, 2000);
  TimerRuns();
  TimerRuns();
  Down(1, 350, 50, 2450);
  EXPECT_EVENTS("move 200 50", "draft aim show 200 50", "held", "draft aim abort", "move 350 50");
}

TOUCH_TEST(TouchGriffDraftATwoFingerTapDropsTheDraft)
{
  OpenDraft({{1, 100, 50, 0}});
  Down(0, 100, 50, 1000);
  Down(1, 200, 50, 1050);
  Up(1, 1150);
  Up(0, 1160);
  WXT_CHECK(Has(mouse.events, "draft drop"));
  WXT_CHECK(!Has(mouse.events, "deselect"));
}

TOUCH_TEST(TouchGriffDraftGesturesTheSystemTakesGiveNothing)
{
  OpenDraft({{1, 50, 50, 0}, {2, 100, 50, 0}});
  Down(0, 50, 50, 1000);
  MoveFirst(90, 50, 1010);
  input.Cancel();
  WXT_CHECK_EQ(FromEnd(mouse.events, 0), std::string("draft move abort"));
}
