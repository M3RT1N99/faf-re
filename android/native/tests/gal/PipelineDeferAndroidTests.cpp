// faf_gal_tests (android/native/README.md, Tests): the patched gpg/gal/PipelineDefer.h (patches/README.md, the pipelines
// a draw waits for). A deferred build is queued only once the pending list holds its slot, so that no job the worker has
// outlives the device that waits for its list; a pipeline refused for want of memory is tried six times in all, over
// 15.5 s, the last in place; a build's end says whether it may build later; the worker runs each job. Upstream's own
// tests of the header (../../../tests/gal/PipelineDeferTests.cpp) run beside these, on the patched header.

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>

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
