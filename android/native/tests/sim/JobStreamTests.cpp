// faf_jobstream_tests (android/native/README.md, Tests): the patched sim job stream
// (patches/src/moho/sim/SimJobStream.cpp.patch, patches/README.md). FinishAll runs the jobs no worker has claimed on
// the thread that asks for them, while a worker still runs an older one, and then waits only for what the workers run;
// every job runs once, wherever it runs. The stream and its worker pool are the simulation's, with one worker. Linked
// statically, so it runs on the emulator and on Linux hosts. Exit code 0 when every check passes.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <thread>

#include "moho/sim/SimJobStream.h"
#include "moho/sim/SimWorkerPool.h"

namespace moho
{
  extern int sim_JobStreamPublishBatch; // SimJobStream.cpp's console variable
}

namespace
{
  int gFailedChecks = 0;

#define CHECK(condition)                                                                                               \
  do {                                                                                                                 \
    if (!(condition)) {                                                                                                \
      std::printf("  %s:%d: %s\n", __FILE__, __LINE__, #condition);                                                    \
      ++gFailedChecks;                                                                                                 \
    }                                                                                                                  \
  } while (false)

  using Clock = std::chrono::steady_clock;
  constexpr auto kPatience = std::chrono::seconds(5);

  std::thread::id gAsker;

  struct SJob
  {
    std::atomic<int> runs{0};
    std::atomic<bool> onAsker{false};
  };

  void Note(
    SJob& job
  )
  {
    job.onAsker.store(std::this_thread::get_id() == gAsker);
    job.runs.fetch_add(1);
  }

  [[nodiscard]] bool WaitFor(
    const std::atomic<bool>& flag
  )
  {
    const auto until = Clock::now() + kPatience;
    while (!flag.load()) {
      if (Clock::now() >= until) {
        return false;
      }
      std::this_thread::yield();
    }
    return true;
  }

  // The first job holds the pool's one worker until every job queued behind it has run elsewhere, 5 s at most. The
  // stream's FinishAll before the patch waited for the oldest job first, and the rest could only run after it: the
  // held job gave up after 5 s, and the others ran on the asking thread then.
  constexpr int kHeldTestJobs = 64;
  SJob gHeldTestJobs[kHeldTestJobs];
  std::atomic<bool> gHeldStarted{false};
  std::atomic<int> gOthersDone{0};
  std::atomic<bool> gHeldSawOthers{false};

  void HeldJob(
    void* const context
  )
  {
    gHeldStarted.store(true);
    const auto until = Clock::now() + kPatience;
    while (gOthersDone.load() < kHeldTestJobs - 1 && Clock::now() < until) {
      std::this_thread::yield();
    }
    gHeldSawOthers.store(gOthersDone.load() == kHeldTestJobs - 1);
    Note(*static_cast<SJob*>(context));
  }

  void OtherJob(
    void* const context
  )
  {
    Note(*static_cast<SJob*>(context));
    gOthersDone.fetch_add(1);
  }

  void TestFinishAllRunsTheUnclaimedJobs()
  {
    moho::CSimJobStream& stream = moho::CSimJobStream::Get();
    CHECK(stream.Enqueue(&HeldJob, &gHeldTestJobs[0]) != moho::CSimJobStream::kRanInline);
    CHECK(WaitFor(gHeldStarted)); // the worker has it
    for (int index = 1; index < kHeldTestJobs; ++index) {
      CHECK(stream.Enqueue(&OtherJob, &gHeldTestJobs[index]) != moho::CSimJobStream::kRanInline);
    }

    const auto start = Clock::now();
    stream.FinishAll();
    const auto took = Clock::now() - start;
    CHECK(gHeldSawOthers.load());
    CHECK(took < kPatience);
    CHECK(gHeldTestJobs[0].runs.load() == 1);
    CHECK(!gHeldTestJobs[0].onAsker.load());
    for (int index = 1; index < kHeldTestJobs; ++index) {
      CHECK(gHeldTestJobs[index].runs.load() == 1);
      CHECK(gHeldTestJobs[index].onAsker.load()); // the one worker was held meanwhile
    }
    CHECK(stream.IsOpen()); // FinishAll keeps the stream open
    stream.Close();
    CHECK(!stream.IsOpen());
  }

  // Rounds of jobs of a few microseconds each, some of them finished on their own as a pose reader does (Finish), each
  // round ended by FinishAll and every few rounds by a close: every job runs once, before its round ends.
  constexpr int kRounds = 40;
  constexpr int kRoundJobs = 300;
  SJob gRoundJobs[kRoundJobs];

  void WorkJob(
    void* const context
  )
  {
    const auto until = Clock::now() + std::chrono::microseconds(5);
    while (Clock::now() < until) {}
    Note(*static_cast<SJob*>(context));
  }

  void TestEveryJobRunsOnce()
  {
    moho::CSimJobStream& stream = moho::CSimJobStream::Get();
    const std::uint64_t stolenBefore = stream.Stats().mStolen;
    std::int32_t tickets[kRoundJobs]{};
    for (int round = 0; round < kRounds; ++round) {
      for (SJob& job : gRoundJobs) {
        job.runs.store(0);
      }
      for (int index = 0; index < kRoundJobs; ++index) {
        tickets[index] = stream.Enqueue(&WorkJob, &gRoundJobs[index]);
        if (index % 37 == 36) {
          stream.Finish(tickets[index - 20]);
          CHECK(gRoundJobs[index - 20].runs.load() == 1);
        }
      }
      stream.FinishAll();
      int missed = 0;
      for (const SJob& job : gRoundJobs) {
        missed += job.runs.load() == 1 ? 0 : 1;
      }
      CHECK(missed == 0);
      if (round % 4 == 3) {
        stream.Close();
      }
    }
    stream.Close();
    // The asking thread ran some of them, which is what FinishAll's first pass is for.
    CHECK(stream.Stats().mStolen > stolenBefore);
  }

  struct Test
  {
    const char* name;
    void (*run)();
  };

  constexpr Test kTests[] = {
    {"FinishAll runs the unclaimed jobs while a worker runs an older one", &TestFinishAllRunsTheUnclaimedJobs},
    {"every job runs once, with Finish, FinishAll and Close among the jobs", &TestEveryJobRunsOnce},
  };
} // namespace

int main()
{
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  // One worker, and each job shown to it as it is queued, so that the first test's job is the worker's at once.
  moho::sim_Workers = 1;
  moho::sim_JobStreamPublishBatch = 1;
  gAsker = std::this_thread::get_id();
  if (!moho::CSimWorkerPool::ParallelEnabled() || moho::CSimWorkerPool::Get().WorkerCount() != 1u) {
    std::printf("the worker pool has no worker\n");
    return 1;
  }

  int failedTests = 0;
  for (const Test& test : kTests) {
    gFailedChecks = 0;
    std::printf("%s\n", test.name);
    test.run();
    if (gFailedChecks != 0) {
      ++failedTests;
      std::printf("  FAIL\n");
    }
  }
  std::printf("%zu tests, %d failed\n", std::size(kTests), failedTests);
  std::fflush(stdout);
  std::_Exit(failedTests == 0 ? 0 : 1);
}
