// The sim thread's sampler (include/SimSampler.h) on a thread of the test's, as the app's profiler runs it on the sim's: its
// timer on the thread's CPU time, the signal's handler and the walk of the thread's frame records, and a SIGPROF that is
// not the timer's, which the handler lets go.

#include <SimSampler.h>

#include <pthread.h>
#include <signal.h>
#include <time.h>

#include <cstdint>
#include <thread>

#include "Check.h"

namespace
{
  [[nodiscard]] std::uint64_t ThreadCpuNanos()
  {
    timespec now{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
    return static_cast<std::uint64_t>(now.tv_sec) * 1'000'000'000U + static_cast<std::uint64_t>(now.tv_nsec);
  }

  void Drain()
  {
    simprofile::Sample sample;
    while (simsampler::gState.ring.Pop(sample)) {
    }
  }

  std::uint64_t Spin(int depth, std::uint64_t seed);
  /// Through a pointer the compiler cannot see through: each level is a call of its own, with its frame record.
  std::uint64_t (*volatile gSpin)(int, std::uint64_t) = &Spin;

  /// Some levels of calls, then work on the CPU until the thread has spent `kBusyNanos` of its time.
  constexpr std::uint64_t kBusyNanos = 300'000'000;

  [[gnu::noinline]] std::uint64_t Spin(const int depth, const std::uint64_t seed)
  {
    if (depth > 0) {
      return gSpin(depth - 1, seed * 31 + 7) + 1;
    }
    volatile std::uint64_t value = seed;
    const std::uint64_t start = ThreadCpuNanos();
    while (ThreadCpuNanos() - start < kBusyNanos) {
      for (int at = 0; at < 100000; ++at) {
        value = value * 6364136223846793005ULL + 1442695040888963407ULL;
      }
    }
    return value;
  }
} // namespace

WXT_TEST(TheSamplerTakesAThreadsPcAndItsCallersByItsCpuTime)
{
  WXT_CHECK_EQ(simsampler::Install(), 0);
  // Its own handler is no other's.
  WXT_CHECK_EQ(simsampler::Install(), 0);
  Drain();
  const std::uint64_t droppedBefore = simsampler::gState.dropped.load();
  int started = -1;
  std::thread worker([&] {
    timer_t timer{};
    started = simsampler::Start(5'000'000, timer);
    if (started == 0) {
      (void)gSpin(6, 1);
      simsampler::Stop(timer);
    }
  });
  worker.join();
  WXT_CHECK_EQ(started, 0);
  WXT_CHECK_EQ(simsampler::gState.thread.load(), 0);

  // 300 ms of the thread's CPU time at 200 Hz is 60 samples; the kernel looks at the timer on its ticks.
  int samples = 0;
  int chained = 0;
  simprofile::Sample sample;
  while (simsampler::gState.ring.Pop(sample)) {
    ++samples;
    WXT_CHECK(sample.depth >= 1 && sample.frames[0] != 0);
    // The PC, on arm64 the link register, and at least one record of the levels of Spin's calls.
#if defined(__aarch64__)
    chained += sample.depth >= 3 ? 1 : 0;
#else
    chained += sample.depth >= 2 ? 1 : 0;
#endif
  }
  WXT_CHECK(samples >= 20);
  WXT_CHECK(chained * 2 >= samples);
  WXT_CHECK_EQ(simsampler::gState.dropped.load(), droppedBefore);
  WXT_CHECK(simsampler::gState.handlerNanos.load() > 0);
}

#if defined(__aarch64__)
WXT_TEST(AReturnAddressLosesItsAuthenticationCode)
{
  // PACIA1716 signs x17 with x16 as its modifier, as a function's prologue signs its link register: a hint, which a CPU
  // without pointer authentication passes over.
  const auto address = reinterpret_cast<std::uintptr_t>(&Spin);
  register std::uintptr_t x17 asm("x17") = address;
  register std::uintptr_t x16 asm("x16") = 0x1234;
  asm volatile("hint #8" : "+r"(x17) : "r"(x16));
  WXT_CHECK_EQ(simsampler::StripCode(x17), address);
}
#endif

WXT_TEST(ASigprofThatIsNotTheTimersIsLetGo)
{
  WXT_CHECK_EQ(simsampler::Install(), 0);
  Drain();
  // Another thread's, and one that names the sampled thread but comes from a kill, not the timer: neither is a sample, and
  // neither ends the process, as SIGPROF's default would.
  simsampler::gState.thread.store(0);
  WXT_CHECK_EQ(pthread_kill(pthread_self(), SIGPROF), 0);
  simsampler::gState.thread.store(gettid());
  WXT_CHECK_EQ(pthread_kill(pthread_self(), SIGPROF), 0);
  simsampler::gState.thread.store(0);
  simprofile::Sample sample;
  WXT_CHECK(!simsampler::gState.ring.Pop(sample));
}
