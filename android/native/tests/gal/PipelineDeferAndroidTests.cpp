// faf_gal_tests (android/native/README.md, Tests): the patched gpg/gal/PipelineDefer.h (patches/README.md, the pipelines
// a draw waits for). A deferred build is queued only once the pending list holds its slot, so that no job the worker has
// outlives the device that waits for its list; a pipeline refused for want of memory is tried six times in all, over
// 15.5 s, the last in place; a build's end says whether it may build later; the worker runs each job, two at once at
// least, and counts the builds it finished with their time and what their driver said of them; the pipeline cache is
// saved once a batch of builds is over, and every 10 s while builds go on. Upstream's own tests of the header
// (../../../tests/gal/PipelineDeferTests.cpp) run beside these, on the patched header.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gpg/gal/PipelineDefer.h"

namespace
{
  using gpg::gal::PipelineBuildEnd;
  using gpg::gal::PipelineRetries;
  using namespace std::chrono_literals;

  /// While set, the pending lists of ScarceAllocator have no memory, as on a phone that ran out of it.
  bool gNoMemory = false;

  template <class T>
  struct ScarceAllocator
  {
    using value_type = T;

    ScarceAllocator() = default;
    template <class U>
    ScarceAllocator(const ScarceAllocator<U>&) noexcept
    {}

    T* allocate(const std::size_t count)
    {
      if (gNoMemory) {
        throw std::bad_alloc();
      }
      return std::allocator<T>().allocate(count);
    }

    void deallocate(T* const block, const std::size_t count) noexcept
    {
      std::allocator<T>().deallocate(block, count);
    }

    template <class U>
    bool operator==(const ScarceAllocator<U>&) const noexcept
    {
      return true;
    }
    template <class U>
    bool operator!=(const ScarceAllocator<U>&) const noexcept
    {
      return false;
    }
  };

  using Pending = std::unordered_map<std::string, std::future<int>>;
  using ScarcePending = std::unordered_map<
    std::string,
    std::future<int>,
    std::hash<std::string>,
    std::equal_to<std::string>,
    ScarceAllocator<std::pair<const std::string, std::future<int>>>>;

  std::future<int> Ready(
    const int value
  )
  {
    std::promise<int> promise;
    promise.set_value(value);
    return promise.get_future();
  }

  template <class Exception>
  std::future<int> Threw(
    const Exception& exception
  )
  {
    std::promise<int> promise;
    promise.set_exception(std::make_exception_ptr(exception));
    return promise.get_future();
  }
} // namespace

TEST(
  AndroidPipelineDefer,
  AQueuedBuildIsInItsSlot
)
{
  Pending pending;
  gpg::gal::PostPendingPipeline(pending, "mesh.fx 24", [] {
    return Ready(7);
  });
  ASSERT_EQ(pending.count("mesh.fx 24"), 1U);
  EXPECT_EQ(pending.at("mesh.fx 24").get(), 7);
}

TEST(
  AndroidPipelineDefer,
  ABuildTheWorkerCannotTakeLeavesNoSlot
)
{
  Pending pending;
  pending.emplace("other", Ready(1));
  EXPECT_THROW(
    gpg::gal::PostPendingPipeline(
      pending,
      "mesh.fx 24",
      []() -> std::future<int> {
    throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
  }
    ),
    std::system_error
  );
  EXPECT_EQ(pending.count("mesh.fx 24"), 0U);
  EXPECT_EQ(pending.size(), 1U) << "the other builds stay";
}

TEST(
  AndroidPipelineDefer,
  NoMemoryForTheSlotQueuesNoJob
)
{
  ScarcePending pending;
  bool posted = false;
  gNoMemory = true;
  EXPECT_THROW(
    gpg::gal::PostPendingPipeline(
      pending,
      "mesh.fx 24",
      [&posted] {
    posted = true;
    return Ready(1);
  }
    ),
    std::bad_alloc
  );
  gNoMemory = false;
  EXPECT_FALSE(posted) << "a job whose future the device does not hold would outlive the device, and build twice";
  EXPECT_TRUE(pending.empty());
}

TEST(
  AndroidPipelineDefer,
  ARefusalForWantOfMemoryIsTriedSixTimesInAllTheLastInPlace
)
{
  ASSERT_EQ(gpg::gal::kPipelineTries, 6U);
  PipelineRetries retries;
  const PipelineRetries::Clock::time_point start{};
  EXPECT_EQ(retries.TurnOf("p", start), PipelineRetries::Turn::kFirst);
  const std::chrono::milliseconds waits[] = {500ms, 1000ms, 2000ms, 4000ms, 8000ms};
  PipelineRetries::Clock::time_point now = start;
  for (unsigned refusals = 1U; refusals < gpg::gal::kPipelineTries; ++refusals) {
    ASSERT_TRUE(retries.Refused("p", now)) << "refusal " << refusals << " has another try after it";
    EXPECT_EQ(retries.Refusals("p"), refusals);
    const std::chrono::milliseconds wait = waits[refusals - 1U];
    EXPECT_EQ(PipelineRetries::Spacing(refusals), wait);
    EXPECT_EQ(retries.TurnOf("p", now + wait - 1ns), PipelineRetries::Turn::kWaiting);
    now += wait;
    EXPECT_EQ(
      retries.TurnOf("p", now),
      (refusals + 1U < gpg::gal::kPipelineTries) ? PipelineRetries::Turn::kAgain : PipelineRetries::Turn::kLast
    ) << "try " << refusals + 1U;
  }
  EXPECT_EQ(now - start, 15500ms);
  EXPECT_FALSE(retries.Refused("p", now)) << "the last try's refusal is for good";
  EXPECT_FALSE(retries.Has("p"));
  EXPECT_EQ(retries.TurnOf("p", now), PipelineRetries::Turn::kFirst);

  ASSERT_TRUE(retries.Refused("q", now));
  EXPECT_TRUE(retries.Forget("q")) << "built after a refusal";
  EXPECT_FALSE(retries.Forget("q"));
  EXPECT_EQ(retries.Size(), 0U);
}

TEST(
  AndroidPipelineDefer,
  ABuildsEndSaysWhetherItMayBuildLater
)
{
  PipelineBuildEnd end = PipelineBuildEnd::kRefused;
  std::future<int> built = Ready(3);
  EXPECT_EQ(gpg::gal::TakeBuiltPipeline(built, &end), 3);
  EXPECT_EQ(end, PipelineBuildEnd::kBuilt);

  std::future<int> forNow = Threw(gpg::gal::PipelineRefused{true});
  EXPECT_EQ(gpg::gal::TakeBuiltPipeline(forNow, &end), 0);
  EXPECT_EQ(end, PipelineBuildEnd::kRefusedForNow);

  std::future<int> noMemory = Threw(std::bad_alloc());
  EXPECT_EQ(gpg::gal::TakeBuiltPipeline(noMemory, &end), 0);
  EXPECT_EQ(end, PipelineBuildEnd::kRefusedForNow) << "the build itself ran out of memory";

  std::future<int> refused = Threw(gpg::gal::PipelineRefused{false});
  EXPECT_EQ(gpg::gal::TakeBuiltPipeline(refused, &end), 0);
  EXPECT_EQ(end, PipelineBuildEnd::kRefused);

  std::future<int> other = Threw(std::runtime_error("a shader the driver rejects"));
  EXPECT_EQ(gpg::gal::TakeBuiltPipeline(other, &end), 0);
  EXPECT_EQ(end, PipelineBuildEnd::kRefused);
}

TEST(
  AndroidPipelineDefer,
  TheWorkerRunsEachJobAndWhatItThrowsGoesIntoItsFuture
)
{
  gpg::gal::PipelineWorker<int>& worker = gpg::gal::PipelineWorker<int>::Instance();
  std::future<int> first = worker.Post(std::packaged_task<int()>([] {
    return 1;
  }));
  std::future<int> second = worker.Post(std::packaged_task<int()>([]() -> int {
    throw gpg::gal::PipelineRefused{true};
  }));
  std::future<int> third = worker.Post(std::packaged_task<int()>([] {
    return 3;
  }));
  EXPECT_EQ(first.get(), 1);
  PipelineBuildEnd end = PipelineBuildEnd::kBuilt;
  EXPECT_EQ(gpg::gal::TakeBuiltPipeline(second, &end), 0);
  EXPECT_EQ(end, PipelineBuildEnd::kRefusedForNow);
  EXPECT_EQ(third.get(), 3);
  EXPECT_EQ(&gpg::gal::PipelineWorker<int>::Instance(), &worker) << "one worker for the process";
}

TEST(
  AndroidPipelineDefer,
  TwoBuildAtOnce
)
{
  static_assert(gpg::gal::kMaxPendingPipelines > gpg::gal::kPipelineWorkerThreads, "a slot's build can wait for a thread");
  // A worker of its own (a type no other test builds), whose two jobs each wait for the other: one thread would run the
  // second only after the first gave up.
  gpg::gal::PipelineWorker<short>& worker = gpg::gal::PipelineWorker<short>::Instance();
  std::atomic<int> started{0};
  const auto meet = [&started]() -> short {
    ++started;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (started.load() < 2 && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(1ms);
    }
    return static_cast<short>(started.load());
  };
  std::future<short> first = worker.Post(std::packaged_task<short()>(meet));
  std::future<short> second = worker.Post(std::packaged_task<short()>(meet));
  EXPECT_EQ(first.get(), 2) << "the second build ran while the first did";
  EXPECT_EQ(second.get(), 2);
}

TEST(
  AndroidPipelineDefer,
  TheWorkerCountsTheBuildsItFinishedAndTheirTime
)
{
  gpg::gal::PipelineWorker<long>& worker = gpg::gal::PipelineWorker<long>::Instance();
  EXPECT_EQ(worker.TakeFinished().count, 0U) << "none built yet";
  std::vector<std::future<long>> builds;
  for (int index = 0; index < 3; ++index) {
    builds.push_back(worker.Post(std::packaged_task<long()>([] {
      std::this_thread::sleep_for(20ms);
      return 1L;
    })));
  }
  for (std::future<long>& build : builds) {
    EXPECT_EQ(build.get(), 1L);
  }
  // A build counts a moment after its future is ready.
  gpg::gal::PipelineBuildTimes finished{};
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (finished.count < 3U && std::chrono::steady_clock::now() < deadline) {
    const gpg::gal::PipelineBuildTimes more = worker.TakeFinished();
    finished.count += more.count;
    finished.time += more.time;
    std::this_thread::sleep_for(1ms);
  }
  EXPECT_EQ(finished.count, 3U);
  EXPECT_GE(finished.time, 60ms) << "each from its start on a thread to its end";
  EXPECT_EQ(worker.TakeFinished().count, 0U) << "each build is taken once";
}

TEST(
  AndroidPipelineDefer,
  TheWorkerTakesAlongWhatTheDriverSaidOfABuild
)
{
  // A worker of its own: a job that says what the driver said (Told), and one that says nothing after it on the same
  // threads, which must not take the earlier job's word for its own.
  gpg::gal::PipelineWorker<unsigned char>& worker = gpg::gal::PipelineWorker<unsigned char>::Instance();
  using Worker = gpg::gal::PipelineWorker<unsigned char>;
  std::vector<std::future<unsigned char>> builds;
  builds.push_back(worker.Post(std::packaged_task<unsigned char()>([] {
    Worker::Told(true, 5ms);
    return static_cast<unsigned char>(1);
  })));
  builds.push_back(worker.Post(std::packaged_task<unsigned char()>([] {
    Worker::Told(false, 7ms);
    return static_cast<unsigned char>(2);
  })));
  for (std::future<unsigned char>& build : builds) {
    (void)build.get();
  }
  for (int index = 0; index < 4; ++index) {
    builds.push_back(worker.Post(std::packaged_task<unsigned char()>([] {
      return static_cast<unsigned char>(3);
    })));
  }
  for (std::future<unsigned char>& build : builds) {
    if (build.valid()) {
      (void)build.get();
    }
  }
  gpg::gal::PipelineBuildTimes finished{};
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (finished.count < 6U && std::chrono::steady_clock::now() < deadline) {
    const gpg::gal::PipelineBuildTimes more = worker.TakeFinished();
    finished.count += more.count;
    finished.told += more.told;
    finished.fromCache += more.fromCache;
    finished.driverTime += more.driverTime;
    std::this_thread::sleep_for(1ms);
  }
  EXPECT_EQ(finished.count, 6U);
  EXPECT_EQ(finished.told, 2U) << "only the builds whose job said so";
  EXPECT_EQ(finished.fromCache, 1U);
  EXPECT_EQ(finished.driverTime, 12ms);
}

TEST(
  AndroidPipelineDefer,
  ThePipelineCacheIsSavedOnceABatchOfBuildsIsOver
)
{
  using Saves = gpg::gal::PipelineCacheSaves;
  Saves saves;
  const Saves::Clock::time_point opened{};
  saves.Saved(opened); // read from its file
  EXPECT_FALSE(saves.Due(opened + 1s, false, false)) << "a cache as its file has it";
  EXPECT_FALSE(saves.Due(opened + 1s, true, true)) << "builds go on, and the last save is recent";
  EXPECT_TRUE(saves.Due(opened + 1s, true, false)) << "the batch is over: no build is pending";
}

TEST(
  AndroidPipelineDefer,
  ThePipelineCacheIsSavedEveryTenSecondsWhileBuildsGoOn
)
{
  using Saves = gpg::gal::PipelineCacheSaves;
  ASSERT_EQ(gpg::gal::kPipelineCacheSaveEvery, 10s);
  Saves saves;
  const Saves::Clock::time_point start{};
  saves.Saved(start);
  EXPECT_FALSE(saves.Due(start + 10s - 1ns, true, true));
  EXPECT_TRUE(saves.Due(start + 10s, true, true)) << "a run Android ends keeps what was built until 10 s before";
  saves.Saved(start + 10s);
  EXPECT_FALSE(saves.Due(start + 15s, true, true)) << "counted from the last save";
  EXPECT_TRUE(saves.Due(start + 20s, true, true));
  EXPECT_FALSE(saves.Due(start + 30s, false, true)) << "nothing grew";
}
