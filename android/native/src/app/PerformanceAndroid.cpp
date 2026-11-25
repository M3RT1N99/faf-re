// Android's performance hints for the game's two busy threads, and the phone's heat (ADPF; android/native/README.md,
// Performance hints and the heat): the app tells Android how long each thread takes for its work, against what it should
// take, and Android sets their cores and clocks to fit; Android tells the app how near the phone is to throttling.
//
// - The render thread, the program's own (the engine's main thread: the UI, the sim's sync and the drawing), has a session
//   whose target is the display's frame time. Its work for a frame runs from the head's frame start (moho::
//   gViewportBeforeFrame, WRenViewport.cpp) to that frame's present (FramePresented): CD3DDevice::Paint presents a frame at
//   the start of the next paint, and the backend waits for the GPU and for the swap chain's next image only after it
//   (DeviceVulkan::Present), so the wait for the display is not in the work.
// - The sim's thread, a new one for each game and for each sim a replay's move back makes, has one whose target is
//   a beat at the game's speed: 100 ms at +0, as the engine paces the beats (WLD_GetSimRate). The engine marks only a
//   beat's start (moho::gSimBeforeBeat, SimTick.cpp), and the thread waits for the next beat between two, so a beat's work
//   is the thread's CPU time from one start to the next: the beat and its sync, without the wait. What a beat waits for the
//   sim's workers is not in it either. The beat's hook is set on every phone, sessions or none: it also tells the sim's
//   speed lines which thread the sim runs on, and its tick (CoresAndroid.cpp).
// - The thermal headroom, how much of the phone's thermal envelope is in use (1.0: Android throttles the phone), is read
//   every 2 s on a thread of its own, as each reading is a call into Android's thermal service. The report has the first
//   reading, and each time the headroom passes 0.8 and 1.0 or comes back under them; ThermalHeadroom has the last, for a
//   policy that lowers the game's settings. Nothing lowers them yet.
//
// Android has hint sessions from 13 (API 33) and the headroom from 12 (API 31). Below, and where Android makes no session
// (an emulator's power HAL may have none), the game asks for none, with one line in the report. A session is Android's
// object for one thread, unsafe to share: each is made, told and closed on its own thread.

#include "PerformanceAndroid.h"

#include <android/api-level.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>
#include <mutex>
#include <string>
#include <thread>

#include <SDL3/SDL_video.h>

#include "AdpfAndroid.h"
#include "CoresAndroid.h"
#include "GameAndroid.h"
#include "moho/net/CClientManagerImpl.h"
#include "moho/render/WRenViewport.h"
#include "moho/sim/CWldSession.h"
#include "moho/sim/ISTIDriver.h"
#include "moho/sim/SimEditorHook.h"

namespace
{
  using platform::android::Report;

  constexpr std::int64_t kNanosPerSecond = 1'000'000'000;
  /// A beat at the game's normal speed, +0: the sim's 10 a second.
  constexpr std::int64_t kBeatNanos = kNanosPerSecond / 10;
  /// The display's refresh rate where SDL knows none.
  constexpr float kDefaultHz = 60.0f;
  /// How often the render thread looks again at the display's refresh rate and the game's speed.
  constexpr std::int64_t kRecheckNanos = kNanosPerSecond;

  /// A frame at the display's refresh rate `hz` (60 Hz where it is none).
  constexpr std::int64_t FrameNanos(const float hz)
  {
    const double rate = hz > 0.0f ? hz : kDefaultHz;
    return static_cast<std::int64_t>(static_cast<double>(kNanosPerSecond) / rate + 0.5);
  }

  // The self-check of the targets' arithmetic, which the build runs.
  static_assert(FrameNanos(120.0f) == 8'333'333 && FrameNanos(60.0f) == 16'666'667 && FrameNanos(90.0f) == 11'111'111);
  static_assert(FrameNanos(0.0f) == FrameNanos(kDefaultHz), "SDL knows no rate");

  /// A beat at the game's speed `rate`: the client manager's, -10 to 50 (CClientManagerImpl::GetSimRate), which makes the
  /// beats 10^(rate / 10) times as fast as at +0, from 1 s down to 1 us.
  [[nodiscard]] std::int64_t BeatNanos(const int rate)
  {
    return std::llround(static_cast<double>(kBeatNanos) / std::pow(10.0, rate * 0.1));
  }

  [[nodiscard]] std::int64_t NowNanos()
  {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::int64_t>(now.tv_sec) * kNanosPerSecond + now.tv_nsec;
  }

  /// The calling thread's CPU time.
  [[nodiscard]] std::int64_t ThreadCpuNanos()
  {
    timespec now{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
    return static_cast<std::int64_t>(now.tv_sec) * kNanosPerSecond + now.tv_nsec;
  }

  /// "8.333 ms": a target, in the report.
  [[nodiscard]] std::string Milliseconds(const std::int64_t nanos)
  {
    char text[32];
    std::snprintf(text, sizeof text, "%.4g ms", static_cast<double>(nanos) / 1e6);
    return text;
  }

  /// Set once the game asks Android for no more sessions: Android made none, or one failed. The report has said why.
  std::atomic<bool> gHintsOff{false};

  /// The report's one line of why the game asks for no more sessions; the sessions there are go on.
  void TurnOff(const std::string& why)
  {
    if (!gHintsOff.exchange(true)) {
      Report("performance hints: " + why + "; the game asks for no more");
    }
  }

  /// Why Android made no session for `thread`, as far as it tells: the phone's power HAL has none, which a preferred update
  /// rate of -1 says, as it is Android's own test (HintManagerService); or Android refused this one, for which the NDK's call
  /// gives no reason, and from Android 16 the system log's perf_hint line has Android's.
  [[nodiscard]] std::string NoSession(const char* const thread)
  {
    if (const std::int64_t rate = adpf::PreferredUpdateRateNanos(); rate >= 0) {
      return std::string("Android refused a session for ") + thread + ", though its power HAL has them (preferred update rate "
        + Milliseconds(rate) + "; from Android 16 the system log's perf_hint line says why)";
    }
    return std::string("Android made no session for ") + thread + ": the phone's power HAL has none (preferred update rate -1)";
  }

  /// One thread's hint session, made, told and closed on that thread alone.
  class HintSession
  {
  public:
    explicit HintSession(const char* const thread)
      : mThread(thread)
    {}

    /// A session for the calling thread, `targetNanos` a cycle, in place of the one there was. The report has a line of the
    /// first alone, with `cycle`'s words for what a cycle is: the sim's are many.
    void Open(const std::int64_t targetNanos, const std::string& cycle)
    {
      Close();
      if (gHintsOff.load()) {
        return;
      }
      mSession = adpf::CreateSession(gettid(), targetNanos);
      if (mSession == nullptr) {
        TurnOff(NoSession(mThread));
        return;
      }
      mTarget = targetNanos;
      if (!mSaid) {
        mSaid = true;
        Report(std::string("performance hints: ") + mThread + "'s session, " + Milliseconds(targetNanos) + " " + cycle);
      }
    }

    void Close()
    {
      if (mSession != nullptr) {
        adpf::CloseSession(mSession);
        mSession = nullptr;
      }
    }

    [[nodiscard]] bool IsOpen() const noexcept
    {
      return mSession != nullptr;
    }

    void SetTarget(const std::int64_t targetNanos)
    {
      if (mSession == nullptr || targetNanos == mTarget) {
        return;
      }
      if (const int error = adpf::UpdateTarget(mSession, targetNanos); error != 0) {
        Fail(error);
        return;
      }
      mTarget = targetNanos;
    }

    /// The work of the last cycle.
    void ReportWork(const std::int64_t nanos)
    {
      if (mSession == nullptr || nanos <= 0) {
        return;
      }
      if (const int error = adpf::ReportActual(mSession, nanos); error != 0) {
        Fail(error);
      }
    }

  private:
    /// Android's service is gone (EPIPE), for the other session too.
    void Fail(const int error)
    {
      Close();
      TurnOff(std::string(mThread) + "'s session failed (" + std::strerror(error) + ")");
    }

    const char* const mThread;
    APerformanceHintSession* mSession = nullptr;
    std::int64_t mTarget = 0;
    bool mSaid = false;
  };

  // The render thread's alone.
  HintSession gRenderSession("the render thread");
  /// When the head's frame began, or -1 while no frame waits for its present.
  std::int64_t gFrameStart = -1;
  std::int64_t gNextRecheck = 0;

  /// The game's speed (CClientManagerImpl::GetSimRate), as the render thread last read it, for the sim's thread.
  std::atomic<int> gSimRate{0};

  /// The sim's thread's session, which the next game's thread takes over. Under the lock: one sim runs at a time in this
  /// process, and a second one's beats would still come one by one.
  struct SimBeats
  {
    std::mutex lock;
    HintSession session{"the sim thread"};
    pid_t thread = 0;
    /// The thread's CPU time at the last beat's start.
    std::int64_t cpuAtBeat = 0;
  };
  SimBeats gSim;

  /// Once a second on the render thread: the display's refresh rate, its session's target, and the game's speed.
  void Recheck()
  {
    // The client manager keeps it under its lock, and the sim's thread cannot reach it safely: the main thread asks, as
    // FAF's UI does (GetSimRate).
    if (moho::ISTIDriver* const driver = moho::WLD_GetDriver()) {
      if (moho::CClientManagerImpl* const clients = driver->GetClientManager()) {
        gSimRate.store(clients->GetSimRate());
      }
    }
    // SDL's desktop mode has the rate the activity's display gave at the surface's last change (Display.getRefreshRate).
    const SDL_DisplayMode* const mode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    const float hz = mode != nullptr ? mode->refresh_rate : 0.0f;
    const std::int64_t frame = FrameNanos(hz);
    if (gRenderSession.IsOpen()) {
      gRenderSession.SetTarget(frame);
    } else if (!gHintsOff.load()) {
      char display[48];
      std::snprintf(display, sizeof display, "a frame (the display's %.0f Hz)", hz > 0.0f ? hz : kDefaultHz);
      gRenderSession.Open(frame, display);
    }
  }

  /// moho::gViewportBeforeFrame: a head's frame begins, on the render thread.
  void BeforeFrame(const int head)
  {
    // The program's one head.
    if (head != 0) {
      return;
    }
    if (const std::int64_t now = NowNanos(); now >= gNextRecheck) {
      gNextRecheck = now + kRecheckNanos;
      Recheck();
    }
    gFrameStart = NowNanos();
  }

  /// moho::gSimBeforeBeat: a beat begins, on the sim's thread. The sim's speed lines hear it first (CoresAndroid.cpp).
  void BeforeBeat(moho::Sim& sim)
  {
    cores::SimBeat(sim);
    const std::int64_t cpu = ThreadCpuNanos();
    const std::lock_guard<std::mutex> lock(gSim.lock);
    const int rate = gSimRate.load();
    const std::int64_t beat = BeatNanos(rate);
    if (const pid_t thread = gettid(); thread != gSim.thread) {
      // A sim's first beat: each sim has a thread of its own, a game's and one a replay's move back makes.
      gSim.thread = thread;
      gSim.cpuAtBeat = cpu;
      gSim.session.Open(beat, "a beat (speed " + std::string(rate >= 0 ? "+" : "") + std::to_string(rate) + ")");
      return;
    }
    gSim.session.SetTarget(beat);
    gSim.session.ReportWork(cpu - gSim.cpuAtBeat);
    gSim.cpuAtBeat = cpu;
  }

  /// The engine's hooks: the beats' on every phone, for the sim's speed lines and the sim's session, and the frames' where
  /// Android has hint sessions. Where it has none the game asks for none (gHintsOff), with the report's line of why.
  void InstallHooks(const int api)
  {
    // Both are the map editor's, and null in the game.
    moho::SimBeforeBeatHook none = nullptr;
    if (!moho::gSimBeforeBeat.compare_exchange_strong(none, &BeforeBeat)) {
      gHintsOff = true;
      Report("performance hints: the engine's beat hook is taken, the game asks for none and the report has no sim speed");
      return;
    }
    if (api < 33) {
      gHintsOff = true;
      Report("performance hints: none below Android 13 (API 33), this phone has API " + std::to_string(api));
      return;
    }
    if (!adpf::HasHintManager()) {
      gHintsOff = true;
      Report("performance hints: Android has no hint manager here");
      return;
    }
    if (moho::gViewportBeforeFrame != nullptr) {
      gHintsOff = true;
      Report("performance hints: the engine's frame hook is taken, the game asks for none");
      return;
    }
    moho::gViewportBeforeFrame = &BeforeFrame;
  }

  /// How often the headroom is read: Android's follows slow sensors, the skin's, and may answer NaN when asked much
  /// more often than once a second (android/thermal.h).
  constexpr auto kHeatPoll = std::chrono::seconds(2);
  /// The headroom's marks in the report: from 0.8 the phone nears throttling, from 1.0 Android throttles it. A mark
  /// passed is left only kHeatHysteresis under it, so that a reading that wavers about a mark writes no line each time.
  constexpr float kHeatMarks[] = {0.8f, 1.0f};
  constexpr int kHeatMarkCount = static_cast<int>(std::size(kHeatMarks));
  constexpr float kHeatHysteresis = 0.05f;

  /// How many marks the headroom is past after the reading `headroom`, with `level` passed before it. A reading that is no
  /// number (NaN: Android has none now) leaves the level as it is.
  constexpr int HeatLevel(const int level, const float headroom)
  {
    if (!(headroom >= 0.0f)) {
      return level;
    }
    int next = 0;
    for (int mark = 0; mark < kHeatMarkCount; ++mark) {
      if (headroom >= kHeatMarks[mark] || (level > mark && headroom >= kHeatMarks[mark] - kHeatHysteresis)) {
        next = mark + 1;
      }
    }
    return next;
  }

  // The self-check of the marks, which the build runs.
  static_assert(HeatLevel(0, 0.79f) == 0 && HeatLevel(0, 0.8f) == 1 && HeatLevel(0, 1.0f) == 2, "the marks");
  static_assert(HeatLevel(1, 0.76f) == 1 && HeatLevel(1, 0.74f) == 0, "a mark is left 0.05 under it");
  static_assert(HeatLevel(2, 0.96f) == 2 && HeatLevel(2, 0.77f) == 1 && HeatLevel(2, 0.3f) == 0, "the marks left");
  static_assert(HeatLevel(0, 1.3f) == 2 && HeatLevel(1, 1.0f) == 2, "past both, also at once");
  static_assert(HeatLevel(1, std::numeric_limits<float>::quiet_NaN()) == 1, "no reading leaves the level");

  /// The report's line of a reading that took the level from `from` to `to`, or of the first, from 0.
  [[nodiscard]] std::string HeatLine(const float headroom, const int from, const int to)
  {
    char line[96];
    if (to > from) {
      std::snprintf(line, sizeof line, "thermal: headroom %.2f, past %.1f%s", headroom, kHeatMarks[to - 1],
        to == kHeatMarkCount ? ", where Android throttles the phone" : "");
    } else if (to < from) {
      std::snprintf(line, sizeof line, "thermal: headroom %.2f, back under %.1f", headroom, kHeatMarks[to]);
    } else {
      std::snprintf(line, sizeof line, "thermal: headroom %.2f", headroom);
    }
    return line;
  }

  /// How many readings that are no number come before the report says that Android gives none: its thermal service may not
  /// have a sample yet for the first.
  constexpr int kHeatMissesBeforeNone = 3;

  /// The last headroom Android gave, NaN before it gave one.
  std::atomic<float> gHeadroom{std::numeric_limits<float>::quiet_NaN()};

  /// The headroom's watch, for as long as the process runs.
  void* WatchHeat(void*)
  {
    int level = 0;
    bool known = false;
    int misses = 0;
    for (;;) {
      if (const float headroom = adpf::ThermalHeadroom(); headroom >= 0.0f) {
        gHeadroom.store(headroom);
        const int next = HeatLevel(level, headroom);
        if (!known || next != level) {
          Report(HeatLine(headroom, level, next));
        }
        known = true;
        level = next;
      } else if (!known && ++misses == kHeatMissesBeforeNone) {
        // A phone without the sensors; a reading that comes later still has its line.
        Report("thermal: Android gives no headroom");
      }
      std::this_thread::sleep_for(kHeatPoll);
    }
  }

  void StartHeatWatch(const int api)
  {
    if (api < 31) {
      Report("thermal: no headroom below Android 12 (API 31), this phone has API " + std::to_string(api));
      return;
    }
    pthread_t thread{};
    if (const int error = pthread_create(&thread, nullptr, &WatchHeat, nullptr); error != 0) {
      Report("thermal: the headroom's watch did not start (" + std::string(std::strerror(error)) + ")");
      return;
    }
    (void)pthread_setname_np(thread, "faf-thermal");
    (void)pthread_detach(thread);
  }
} // namespace

namespace performance
{
  void Install()
  {
    const int api = android_get_device_api_level();
    InstallHooks(api);
    StartHeatWatch(api);
  }

  float ThermalHeadroom() noexcept
  {
    return gHeadroom.load();
  }

  void FramePresented()
  {
    if (gFrameStart < 0) {
      return;
    }
    const std::int64_t work = NowNanos() - gFrameStart;
    gFrameStart = -1;
    gRenderSession.ReportWork(work);
  }
} // namespace performance
