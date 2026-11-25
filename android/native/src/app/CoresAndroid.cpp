// Where the game's threads run (android/native/README.md, The cores): by the classes of cores sysfs rates
// (include/CoreClasses.h), the sim's thread on the fastest class and the next, starting on the fastest, and the render
// thread, the issue thread and the workers off the little cores, through the engine's own placement of each thread as it
// starts (moho::PlaceCurrentThread, which the patched ThreadPlacement.cpp hands to the app: patches/README.md). FAF's init
// asks for every CPU but the first two (init_faf.lua's SetProcessAffinityMask), which on a phone whose first four cores are
// little keeps two of them, and the binary pins the main thread to the process's lowest CPU as it starts
// (THREAD_SetAffinity); while the app places the threads, the patched StartupHelpers.cpp takes no mask from Lua and the
// patched Thread.cpp pins no thread. A device of one class keeps FAF's way.
//
// Where the kernel is older than Linux 6.2, which keeps a thread's own CPUs, Android sets a thread's CPUs anew to its CPU
// group's when it moves the process into another group (its cpusets: top-app in front, others in the back). So the cores'
// watch, a thread of its own, looks each second whether the render thread's and the sim's are still those they had, and
// where they are not it places every placed thread again.
//
// The watch also has the sim's speed in the report, on every device: every 30 s of play its ticks a second, how much of
// the time its thread was on a CPU, and on which class, by the CPU it ran on last (/proc/self/task/<tid>/stat's processor)
// 20 times a second, with its time on a CPU from schedstat. Its beats tell which thread the sim runs on, and its tick.

#include "CoresAndroid.h"

#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <Windows.h>

#include <CoreClasses.h>

#include "GameAndroid.h"
#include "PerformanceAndroid.h"
#include "moho/core/ThreadPlacement.h"
#include "moho/sim/ReplayTimeline.h"
#include "moho/sim/Sim.h"

namespace
{
  using coreclasses::CpuList;
  using coreclasses::CpuSet;
  using platform::android::Report;

  constexpr std::int64_t kNanosPerSecond = 1'000'000'000;
  /// How often the watch looks whether Android set the threads' CPUs anew.
  constexpr std::int64_t kCheckNanos = kNanosPerSecond;
  /// How often the watch samples the sim's thread while a sim runs, and how often it looks for one while none does.
  constexpr auto kSamplePeriod = std::chrono::milliseconds(50);
  constexpr auto kIdlePeriod = std::chrono::milliseconds(500);
  /// A speed line's span of play.
  constexpr std::int64_t kSpeedWindowNanos = 30 * kNanosPerSecond;

  // Set on the program's thread before the engine's threads and the watch start (cores::Install), and only read after.
  coreclasses::Reading gReading;
  coreclasses::Plan gPlan;
  /// A clock tick of /proc's CPU times, in ns.
  std::uint64_t gNanosPerTick = 10'000'000;

  // The sim, as its beats tell (cores::SimBeat): its thread, its Sim, the tick its last beat began on, and a count of the
  // sims, one more for each that began: a new thread, a new Sim, or ticks that went back, as a replay's move back makes.
  std::atomic<pid_t> gSimThread{0};
  std::atomic<const moho::Sim*> gSimObject{nullptr};
  std::atomic<std::uint32_t> gSimTick{0};
  std::atomic<std::uint32_t> gSimRun{0};

  [[nodiscard]] std::int64_t NowNanos()
  {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::int64_t>(now.tv_sec) * kNanosPerSecond + now.tv_nsec;
  }

  /// A small file's text, "" where it cannot be read.
  [[nodiscard]] std::string ReadText(const std::string& path)
  {
    const int file = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (file < 0) {
      return {};
    }
    std::string text;
    char buffer[512];
    ssize_t got = 0;
    while (text.size() < 8192 && (got = ::read(file, buffer, sizeof buffer)) > 0) {
      text.append(buffer, static_cast<std::size_t>(got));
    }
    ::close(file);
    return text;
  }

  /// The classes as sysfs rates the CPUs, from cpu0 to the last one it has.
  [[nodiscard]] coreclasses::Reading ReadSysfs()
  {
    std::vector<std::string> capacities;
    std::vector<std::string> maxFrequencies;
    // One more than a CpuSet holds, so that a device with more CPUs has no classes (coreclasses::ClassesOf).
    for (std::size_t cpu = 0; cpu <= coreclasses::kMaxCpus; ++cpu) {
      const std::string folder = "/sys/devices/system/cpu/cpu" + std::to_string(cpu);
      struct stat info{};
      if (::stat(folder.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) {
        break;
      }
      capacities.push_back(ReadText(folder + "/cpu_capacity"));
      maxFrequencies.push_back(ReadText(folder + "/cpufreq/cpuinfo_max_freq"));
    }
    return coreclasses::Read(capacities, maxFrequencies);
  }

  [[nodiscard]] std::string TaskFile(const pid_t tid, const char* const name)
  {
    return "/proc/self/task/" + std::to_string(tid) + "/" + name;
  }

  [[nodiscard]] coreclasses::ThreadStat StatOf(const pid_t tid)
  {
    return coreclasses::ParseStat(ReadText(TaskFile(tid, "stat")));
  }

  /// The thread's time on a CPU so far, in ns: schedstat's, else its stat's CPU time in clock ticks. False where the thread
  /// is gone.
  [[nodiscard]] bool RanNanos(const pid_t tid, const coreclasses::ThreadStat& stat, std::uint64_t& nanos)
  {
    if (!stat.valid) {
      return false;
    }
    if (!coreclasses::ParseRunNanos(ReadText(TaskFile(tid, "schedstat")), nanos)) {
      nanos = stat.cpuTicks * gNanosPerTick;
    }
    return true;
  }

  /// The CPUs the thread `tid` may run on (0: the calling one); none where it is gone.
  [[nodiscard]] CpuSet CpusOf(const pid_t tid)
  {
    cpu_set_t set;
    CPU_ZERO(&set);
    if (::sched_getaffinity(tid, sizeof set, &set) != 0) {
      return 0;
    }
    CpuSet cpus = 0;
    for (std::size_t cpu = 0; cpu < coreclasses::kMaxCpus; ++cpu) {
      if (CPU_ISSET(cpu, &set)) {
        cpus |= CpuSet{1} << cpu;
      }
    }
    return cpus;
  }

  /// Puts the thread `tid` (0: the calling one) on `cpus`. 0, or the kernel's errno: EINVAL where none of them is in the
  /// process's CPU group, ESRCH where the thread is gone.
  [[nodiscard]] int SetCpus(const pid_t tid, const CpuSet cpus)
  {
    cpu_set_t set;
    CPU_ZERO(&set);
    for (std::size_t cpu = 0; cpu < coreclasses::kMaxCpus; ++cpu) {
      if (((cpus >> cpu) & 1U) != 0) {
        CPU_SET(cpu, &set);
      }
    }
    return ::sched_setaffinity(tid, sizeof set, &set) == 0 ? 0 : errno;
  }

  /// Puts the thread on `cpus` by way of `first` where that is another set: the kernel moves a running thread onto CPUs it
  /// may run on at once, and leaves it where it is when the next set holds that CPU.
  [[nodiscard]] int Place(const pid_t tid, const CpuSet first, const CpuSet cpus)
  {
    if (first != 0 && first != cpus) {
      (void)SetCpus(tid, first);
    }
    return SetCpus(tid, cpus);
  }

  /// A thread the app placed, for the watch to place again.
  struct Placed
  {
    moho::EThreadRole role = moho::EThreadRole::Main;
    unsigned int index = 0;
    pid_t tid = 0;
    /// When it started (/proc's starttime): a tid the system has given another thread since is not this one.
    std::uint64_t startTime = 0;
    CpuSet first = 0;
    CpuSet cpus = 0;
    /// Its CPUs as the kernel had them after its last placement.
    CpuSet seen = 0;
  };

  std::mutex gPlacedLock;
  /// One for each role and index: a new sim's thread and its issue thread take the places of the last ones, a worker that
  /// starts anew the place of its index. Under the lock.
  std::vector<Placed> gPlaced;

  /// The calling thread, placed on `cpus` by way of `first`: remembered with the CPUs the kernel gave it.
  void Remember(const moho::EThreadRole role, const unsigned int index, const CpuSet first, const CpuSet cpus)
  {
    Placed placed;
    placed.role = role;
    placed.index = index;
    placed.tid = ::gettid();
    placed.startTime = StatOf(placed.tid).startTime;
    placed.first = first;
    placed.cpus = cpus;
    placed.seen = CpusOf(0);
    const std::lock_guard<std::mutex> lock(gPlacedLock);
    for (Placed& known : gPlaced) {
      if (known.role == placed.role && known.index == placed.index) {
        known = placed;
        return;
      }
    }
    gPlaced.push_back(placed);
  }

  /// Once: a thread that Android did not let go where the plan has it.
  std::atomic<bool> gRefusalSaid{false};

  /// moho::gHostThreadPlacement: an engine thread begins (moho::PlaceCurrentThread), on that thread. It goes where the plan
  /// has its role; where Android holds the process on other CPUs, as in the back, the watch places it once it may.
  void PlaceThisThread(const moho::EThreadRole role, const unsigned int index)
  {
    const bool sim = role == moho::EThreadRole::Sim;
    const CpuSet first = sim ? gPlan.simFirst : 0;
    const CpuSet cpus = sim ? gPlan.sim : gPlan.others;
    if (const int error = Place(0, first, cpus); error != 0 && !gRefusalSaid.exchange(true)) {
      Report("cores: a thread could not go on cpu" + CpuList(cpus) + " (" + std::strerror(error) + "), it runs on cpu" +
        CpuList(CpusOf(0)) + " until Android lets it");
    }
    Remember(role, index, first, cpus);
  }

  /// Whether Android set the CPUs of the render thread or the sim's anew since their placement. A thread that is gone, or
  /// whose tid another one has now, is forgotten. Under the lock.
  [[nodiscard]] bool MovedLocked()
  {
    bool moved = false;
    for (auto at = gPlaced.begin(); at != gPlaced.end();) {
      const bool watched =
        (at->role == moho::EThreadRole::Main || at->role == moho::EThreadRole::Sim) && at->cpus != gReading.all;
      if (watched) {
        if (const CpuSet now = CpusOf(at->tid); now != at->seen) {
          if (now == 0 || StatOf(at->tid).startTime != at->startTime) {
            at = gPlaced.erase(at);
            continue;
          }
          moved = true;
        }
      }
      ++at;
    }
    return moved;
  }

  /// Each second on the watch's thread: where Android set the threads' CPUs anew, as it does when it moves the process
  /// into another of its CPU groups, every placed thread that is still there goes where the plan has it again, and the
  /// report says so, or where Android holds the process for now.
  void CheckPlaced()
  {
    const std::lock_guard<std::mutex> lock(gPlacedLock);
    if (!MovedLocked()) {
      return;
    }
    int placedAgain = 0;
    int refusal = 0;
    CpuSet held = 0;
    for (auto at = gPlaced.begin(); at != gPlaced.end();) {
      const int error = StatOf(at->tid).startTime == at->startTime ? Place(at->tid, at->first, at->cpus) : ESRCH;
      if (error == ESRCH) {
        at = gPlaced.erase(at);
        continue;
      }
      if (error != 0) {
        refusal = error;
      } else {
        ++placedAgain;
      }
      at->seen = CpusOf(at->tid);
      held |= at->seen;
      ++at;
    }
    if (refusal == 0) {
      Report("cores: Android had set the threads' CPUs anew, as it does when it moves the process into another of its CPU "
        "groups; the " + std::to_string(placedAgain) + " placed threads are back on their cores");
    } else {
      Report("cores: Android holds the process on cpu" + CpuList(held) + " for now (" + std::strerror(refusal) +
        "), as in the back; the threads go back on their cores once it lets them");
    }
  }

  /// The sim's speed lines: every 30 s of play how far its ticks went and where its thread ran, from what /proc says of the
  /// thread 20 times a second. On the watch's thread.
  class SimSpeed
  {
  public:
    /// A sim's thread is being sampled.
    [[nodiscard]] bool Sampling() const
    {
      return mThread > 0;
    }

    void Sample(const std::int64_t now)
    {
      if (const std::uint32_t run = gSimRun.load(std::memory_order_acquire); run != mRun) {
        Begin(run, now);
        return;
      }
      if (mThread <= 0) {
        return;
      }
      const coreclasses::ThreadStat stat = StatOf(mThread);
      std::uint64_t ran = 0;
      if (!RanNanos(mThread, stat, ran)) {
        mThread = 0; // its sim ended: the next sim's first beat names its own
        return;
      }
      if (ran > mRan) {
        mWindow.runNanos += ran - mRan;
        if (const int index = coreclasses::ClassOf(gReading.classes, stat.processor); index >= 0) {
          mWindow.classNanos[static_cast<std::size_t>(index)] += ran - mRan;
        }
      }
      mRan = ran;
      mWindow.moved = mWindow.moved || moho::gReplayFastForward.load(std::memory_order_relaxed);
      if (now - mStart < kSpeedWindowNanos) {
        return;
      }
      mWindow.seconds = static_cast<double>(now - mStart) / static_cast<double>(kNanosPerSecond);
      mWindow.lastTick = gSimTick.load(std::memory_order_relaxed);
      mWindow.headroom = performance::ThermalHeadroom();
      // Play is ticks that went on: a sim held, paused or over has no line.
      if (mWindow.lastTick > mWindow.firstTick) {
        Report(coreclasses::SpeedLine(mWindow, gReading.classes));
      }
      StartWindow(now);
    }

  private:
    /// The sim `run`'s first sample: where its thread's time on a CPU starts from.
    void Begin(const std::uint32_t run, const std::int64_t now)
    {
      mRun = run;
      mThread = gSimThread.load(std::memory_order_relaxed);
      if (!RanNanos(mThread, StatOf(mThread), mRan)) {
        mThread = 0;
        return;
      }
      StartWindow(now);
    }

    void StartWindow(const std::int64_t now)
    {
      mStart = now;
      mWindow = {};
      mWindow.firstTick = gSimTick.load(std::memory_order_relaxed);
      mWindow.classNanos.assign(gReading.classes.size(), 0);
    }

    std::uint32_t mRun = 0;
    pid_t mThread = 0;
    std::uint64_t mRan = 0;
    std::int64_t mStart = 0;
    coreclasses::SpeedWindow mWindow;
  };

  void* Watch(void*)
  {
    // Light as it is, the watch may run on any core.
    (void)SetCpus(0, gReading.all);
    SimSpeed speed;
    std::int64_t nextCheck = NowNanos() + kCheckNanos;
    for (;;) {
      std::this_thread::sleep_for(speed.Sampling() ? kSamplePeriod : kIdlePeriod);
      const std::int64_t now = NowNanos();
      if (gPlan.Places() && now >= nextCheck) {
        CheckPlaced();
        nextCheck = now + kCheckNanos;
      }
      speed.Sample(now);
    }
  }

  void StartWatch()
  {
    pthread_t thread{};
    if (const int error = pthread_create(&thread, nullptr, &Watch, nullptr); error != 0) {
      Report(std::string("cores: the cores' watch did not start (") + std::strerror(error) +
        "): the report has no sim speed, and a thread whose CPUs Android sets anew stays on those");
      return;
    }
    (void)pthread_setname_np(thread, "faf-cores");
    (void)pthread_detach(thread);
  }

  /// The program's thread, the engine's main thread (the render thread), off the little cores where that is plain, before
  /// the engine's threads start, which take its CPUs on; then those are the app's to place as they start. 0, or the
  /// kernel's errno where the thread could not go there.
  [[nodiscard]] int PlaceProgram()
  {
    if (gPlan.offLittle) {
      if (const int error = SetCpus(0, gPlan.others); error != 0) {
        return error;
      }
      // The engine's own call, with the same CPUs: GetProcessAffinityMask answers them from now on, by which the engine
      // sizes its worker pools (UsableCpuCount). On POSIX it sets the calling thread's CPUs, as above.
      (void)::SetProcessAffinityMask(::GetCurrentProcess(), gPlan.others);
    }
    Remember(moho::EThreadRole::Main, 0, 0, gPlan.others);
    moho::gHostThreadPlacement.store(&PlaceThisThread, std::memory_order_release);
    return 0;
  }
} // namespace

void cores::Install()
{
  if (const long ticks = ::sysconf(_SC_CLK_TCK); ticks > 0) {
    gNanosPerTick = static_cast<std::uint64_t>(kNanosPerSecond / ticks);
  }
  gReading = ReadSysfs();
  gPlan = coreclasses::PlanFor(gReading);
  if (const int error = gPlan.Places() ? PlaceProgram() : 0; error != 0) {
    Report("cores: the program's thread could not go on cpu" + CpuList(gPlan.others) + " (" + std::strerror(error) +
      "), so the threads go where FAF's init and Android put them");
    gPlan = {};
  } else {
    Report(coreclasses::PlacementLine(gReading, gPlan));
  }
  StartWatch();
}

void cores::SimBeat(const moho::Sim& sim)
{
  const pid_t thread = ::gettid();
  const std::uint32_t tick = sim.mCurTick;
  if (thread != gSimThread.load(std::memory_order_relaxed) || &sim != gSimObject.load(std::memory_order_relaxed) ||
      tick < gSimTick.load(std::memory_order_relaxed)) {
    gSimThread.store(thread, std::memory_order_relaxed);
    gSimObject.store(&sim, std::memory_order_relaxed);
    gSimTick.store(tick, std::memory_order_relaxed);
    gSimRun.fetch_add(1, std::memory_order_release);
    return;
  }
  gSimTick.store(tick, std::memory_order_relaxed);
}
