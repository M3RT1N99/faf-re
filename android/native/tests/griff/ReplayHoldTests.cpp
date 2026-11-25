// A replay's move back (include/ReplayHold.h): when the hold of the new sim's beats ends, and the slice rule by which the
// held beats are applied after it, a slice a frame.

#include <ReplayHold.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Check.h"

namespace
{
  using replayhold::End;
  using namespace std::chrono_literals;

  constexpr std::int64_t kMegabyte = 1 << 20;

  [[nodiscard]] replayhold::Hold Held(const std::int32_t tick, const std::size_t beats, const std::int64_t growth)
  {
    replayhold::Hold hold;
    hold.heldTick = tick;
    hold.targetTick = 786;
    hold.beats = beats;
    hold.heapGrowth = growth;
    return hold;
  }

  /// What the slices do to beats of `costs`, applied one after the other on a clock that only they move: the frames they
  /// take, and the longest slice.
  struct Slices
  {
    std::size_t frames = 0;
    std::chrono::nanoseconds longest{0};
  };

  [[nodiscard]] Slices Apply(const std::vector<std::chrono::nanoseconds>& costs)
  {
    Slices slices;
    std::size_t next = 0;
    while (next < costs.size()) {
      ++slices.frames;
      std::chrono::nanoseconds spent{0};
      for (std::size_t applied = 0; next < costs.size() && replayhold::SliceGoesOn(applied, spent); ++applied) {
        spent += costs[next++];
      }
      slices.longest = spent > slices.longest ? spent : slices.longest;
    }
    return slices;
  }
} // namespace

WXT_TEST(AHoldEndsAtItsTargetFirstThenAtItsCountThenAtItsHeap)
{
  const replayhold::Caps caps{3000, 256 * kMegabyte};
  WXT_CHECK_EQ(replayhold::HoldEnds(Held(400, 400, 100 * kMegabyte), caps), End::kHolding);
  WXT_CHECK_EQ(replayhold::HoldEnds(Held(786, 786, 100 * kMegabyte), caps), End::kTarget);
  // The target counts first, past both caps.
  WXT_CHECK_EQ(replayhold::HoldEnds(Held(790, 3000, 300 * kMegabyte), caps), End::kTarget);
  WXT_CHECK_EQ(replayhold::HoldEnds(Held(700, 3000, 300 * kMegabyte), caps), End::kBeats);
  WXT_CHECK_EQ(replayhold::HoldEnds(Held(97, 97, 256 * kMegabyte), caps), End::kMemory);
  WXT_CHECK_EQ(replayhold::HoldEnds(Held(97, 97, 256 * kMegabyte - 1), caps), End::kHolding);
  // A heap that shrank since the hold began, as the old sim's last memory went, holds on.
  WXT_CHECK_EQ(replayhold::HoldEnds(Held(10, 10, -40 * kMegabyte), caps), End::kHolding);
  // No beat yet is at no target.
  replayhold::Hold none = Held(-1, 0, 0);
  none.targetTick = 0;
  WXT_CHECK_EQ(replayhold::HoldEnds(none, caps), End::kHolding);
}

WXT_TEST(AHoldWithoutAMemoryCapEndsAtItsFirstBeatAndAStallEndsOneThatHolds)
{
  // 0 MB: none past the first; the move goes on in view at once.
  const replayhold::Caps none{3000, 0};
  WXT_CHECK_EQ(replayhold::HoldEnds(Held(1, 1, 0), none), End::kMemory);
  WXT_CHECK_EQ(replayhold::HoldEnds(Held(1, 1, -kMegabyte), none), End::kMemory);
  WXT_CHECK_EQ(replayhold::HoldEnds(Held(786, 1, 0), none), End::kTarget);
  // Before the first beat no cap of the heap binds, with none or with a growth since the hold before.
  WXT_CHECK_EQ(replayhold::HoldEnds(Held(-1, 0, 0), none), End::kHolding);
  WXT_CHECK_EQ(replayhold::HoldEnds(Held(-1, 0, 2048 * kMegabyte), replayhold::Caps{3000, 1024 * kMegabyte}), End::kHolding);

  replayhold::Hold stalled = Held(500, 500, kMegabyte);
  stalled.stalled = true;
  WXT_CHECK_EQ(replayhold::HoldEnds(stalled, replayhold::Caps{3000, 256 * kMegabyte}), End::kStalled);
  stalled.heldTick = 786;
  WXT_CHECK_EQ(replayhold::HoldEnds(stalled, replayhold::Caps{3000, 256 * kMegabyte}), End::kTarget);
}

WXT_TEST(ASliceTakesABeatAtLeastThenMoreWhileItsBudgetLasts)
{
  WXT_CHECK(replayhold::SliceGoesOn(0, 0ns));
  // A first beat goes even into a frame whose budget is gone: each frame moves the move on.
  WXT_CHECK(replayhold::SliceGoesOn(0, 40ms));
  WXT_CHECK(replayhold::SliceGoesOn(1, 14ms));
  WXT_CHECK(!replayhold::SliceGoesOn(1, 15ms));
  WXT_CHECK(!replayhold::SliceGoesOn(5, 20ms));
  WXT_CHECK(replayhold::SliceGoesOn(5, 20ms, 30ms));
}

WXT_TEST(TheHeldBeatsOfTheGalaxyS22sMoveBackGoInSlicesOfAFrameNotInOne)
{
  // 786 beats applied in 9.3 s in one frame: 11.8 ms each. In slices no frame takes more than its budget and a beat.
  const std::vector<std::chrono::nanoseconds> even(786, 11'800us);
  const Slices slices = Apply(even);
  WXT_CHECK_EQ(slices.frames, std::size_t{393});
  WXT_CHECK(slices.longest == 23'600us);

  // A first beat of 900 ms (the map's props) takes a frame of its own, and the next frame goes on with the rest.
  std::vector<std::chrono::nanoseconds> heavy(4, 2ms);
  heavy.insert(heavy.begin(), 900ms);
  const Slices lopsided = Apply(heavy);
  WXT_CHECK_EQ(lopsided.frames, std::size_t{2});
  WXT_CHECK(lopsided.longest == 900ms);
  WXT_CHECK_EQ(Apply({}).frames, std::size_t{0});
}
