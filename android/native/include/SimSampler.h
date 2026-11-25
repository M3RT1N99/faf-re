#pragma once
// The sim thread's sampler (src/app/ProfilerAndroid.cpp, include/SimProfile.h): a POSIX timer on one thread's CPU time
// signals that thread (SIGEV_THREAD_ID, SIGPROF) each interval of its CPU time, and the signal's handler takes the
// interrupted PC, the link register and the return addresses the frame pointers chain within the thread's own stack into
// a ring. It takes nothing but async-signal-safe steps: the registers, atomics, gettid, the monotonic clock and reads of
// that stack. A SIGPROF that is not the timer's, or that reaches another thread, is let go. Linux's, as bionic declares
// it; headers only, so that tests/griff runs it on a thread of its own.

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>

#include "SimProfile.h"

namespace simsampler
{
  /// Five seconds of samples at 200 Hz, for a counting thread that empties the ring every 100 ms.
  inline constexpr std::size_t kRingSlots = 1024;

  /// What the handler reads and writes.
  struct State
  {
    /// The thread the timer samples (its tid), 0 for none.
    std::atomic<pid_t> thread{0};
    /// That thread's stack, within which alone the handler reads frame records.
    std::atomic<std::uintptr_t> stackLow{0};
    std::atomic<std::uintptr_t> stackHigh{0};
    simprofile::SampleRing<kRingSlots> ring;
    /// The samples the ring had no room for.
    std::atomic<std::uint64_t> dropped{0};
    /// The handler's own time, over all its samples.
    std::atomic<std::uint64_t> handlerNanos{0};
  };

  inline State gState;

  [[nodiscard]] inline std::uint64_t MonotonicNanos() noexcept
  {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::uint64_t>(now.tv_sec) * 1'000'000'000U + static_cast<std::uint64_t>(now.tv_nsec);
  }

  /// A return address without arm64's pointer authentication code (XPACLRI, a hint: nothing on a CPU without it).
  [[nodiscard]] inline std::uintptr_t StripCode(const std::uintptr_t address) noexcept
  {
#if defined(__aarch64__)
    register std::uintptr_t lr asm("x30") = address;
    asm("hint #7" : "+r"(lr));
    return lr;
#else
    return address;
#endif
  }

  /// How far back from a return address its call is (simprofile::StackOf).
  inline constexpr std::uintptr_t kCallSize =
#if defined(__aarch64__)
    4;
#else
    1;
#endif

  inline void OnSignal(int, siginfo_t* const info, void* const context) noexcept
  {
    const int savedErrno = errno;
    if (info != nullptr && info->si_code == SI_TIMER && context != nullptr &&
        gettid() == gState.thread.load(std::memory_order_acquire)) {
      const std::uint64_t start = MonotonicNanos();
      const auto& registers = static_cast<const ucontext_t*>(context)->uc_mcontext;
      const std::uintptr_t low = gState.stackLow.load(std::memory_order_relaxed);
      const std::uintptr_t high = gState.stackHigh.load(std::memory_order_relaxed);
#if defined(__aarch64__)
      const simprofile::Sample sample =
        simprofile::SampleOf(registers.pc, registers.regs[30], registers.regs[29], low, high);
#elif defined(__x86_64__)
      const simprofile::Sample sample = simprofile::SampleOf(static_cast<std::uintptr_t>(registers.gregs[REG_RIP]), 0,
        static_cast<std::uintptr_t>(registers.gregs[REG_RBP]), low, high);
#else
#error "the sampler knows the registers of arm64 and x86-64"
#endif
      if (!gState.ring.Push(sample)) {
        gState.dropped.fetch_add(1, std::memory_order_relaxed);
      }
      gState.handlerNanos.fetch_add(MonotonicNanos() - start, std::memory_order_relaxed);
    }
    errno = savedErrno;
  }

  /// Gives SIGPROF to the handler: 0, EBUSY where another handler has it, or sigaction's errno. The handler stays for as
  /// long as the process: a timer's signal that comes after its timer went, with no handler, would end the process.
  [[nodiscard]] inline int Install() noexcept
  {
    struct sigaction old{};
    if (sigaction(SIGPROF, nullptr, &old) != 0) {
      return errno;
    }
    const bool taken = (old.sa_flags & SA_SIGINFO) != 0 ? old.sa_sigaction != &OnSignal
                                                        : old.sa_handler != SIG_DFL && old.sa_handler != SIG_IGN;
    if (taken) {
      return EBUSY;
    }
    struct sigaction action{};
    action.sa_sigaction = &OnSignal;
    // A call the signal interrupts goes on, where the kernel can restart it.
    action.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&action.sa_mask);
    return sigaction(SIGPROF, &action, nullptr) == 0 ? 0 : errno;
  }

  /// Samples the calling thread each `intervalNanos` of its CPU time from now on, by `timer`: 0, or the errno of the step
  /// that failed, and then nothing samples it. One thread at a time: this one takes the place of the one before.
  [[nodiscard]] inline int Start(const long intervalNanos, timer_t& timer) noexcept
  {
    void* stack = nullptr;
    std::size_t size = 0;
    pthread_attr_t attributes;
    if (pthread_getattr_np(pthread_self(), &attributes) == 0) {
      (void)pthread_attr_getstack(&attributes, &stack, &size);
      (void)pthread_attr_destroy(&attributes);
    }
    // Without its bounds a sample has the PC and the link register alone.
    gState.stackLow.store(reinterpret_cast<std::uintptr_t>(stack), std::memory_order_relaxed);
    gState.stackHigh.store(reinterpret_cast<std::uintptr_t>(stack) + size, std::memory_order_relaxed);
    const pid_t thread = gettid();
    gState.thread.store(thread, std::memory_order_release);
    sigevent event{};
    event.sigev_notify = SIGEV_THREAD_ID;
    event.sigev_signo = SIGPROF;
    event.sigev_notify_thread_id = thread;
    if (timer_create(CLOCK_THREAD_CPUTIME_ID, &event, &timer) != 0) {
      const int error = errno;
      gState.thread.store(0, std::memory_order_release);
      return error;
    }
    itimerspec period{};
    period.it_interval.tv_sec = intervalNanos / 1'000'000'000L;
    period.it_interval.tv_nsec = intervalNanos % 1'000'000'000L;
    period.it_value = period.it_interval;
    if (timer_settime(timer, 0, &period, nullptr) != 0) {
      const int error = errno;
      (void)timer_delete(timer);
      gState.thread.store(0, std::memory_order_release);
      return error;
    }
    return 0;
  }

  /// No more samples by `timer`, on any thread.
  inline void Stop(const timer_t timer) noexcept
  {
    (void)timer_delete(timer);
    gState.thread.store(0, std::memory_order_release);
  }
} // namespace simsampler
