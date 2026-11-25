// The sim's profile (include/SimProfile.h): the walk of the frame records within the stack, the ring the samples wait in,
// a sample's stack by module and offset, and the counting with profile.txt's text.

#include <SimProfile.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "Check.h"

namespace
{
  using simprofile::Frame;
  using simprofile::Sample;
  using simprofile::Stack;

  /// A stack of words, whose frame records the tests write as a thread's would be.
  struct Words
  {
    std::uintptr_t words[16] = {};

    [[nodiscard]] std::uintptr_t At(const std::size_t index) const
    {
      return reinterpret_cast<std::uintptr_t>(&words[index]);
    }

    /// The record at `index`: the caller's frame pointer, then the return address.
    void Record(const std::size_t index, const std::uintptr_t next, const std::uintptr_t returnAddress)
    {
      words[index] = next;
      words[index + 1] = returnAddress;
    }

    [[nodiscard]] std::uintptr_t Low() const
    {
      return At(0);
    }

    [[nodiscard]] std::uintptr_t High() const
    {
      return At(0) + sizeof words;
    }
  };

  [[nodiscard]] std::vector<std::uintptr_t> Walk(const Words& stack, const std::uintptr_t fp, const std::size_t room = 8)
  {
    std::uintptr_t out[8] = {};
    const std::size_t count = simprofile::WalkFrames(fp, stack.Low(), stack.High(), out, room);
    return {out, out + count};
  }

  [[nodiscard]] Stack StackOf(std::initializer_list<Frame> frames)
  {
    Stack stack;
    for (const Frame& frame : frames) {
      stack.frames[stack.depth++] = frame;
    }
    return stack;
  }
} // namespace

WXT_TEST(AFrameWalkReadsTheRecordsWithinTheStackEachAboveTheLast)
{
  Words stack;
  stack.Record(2, stack.At(6), 0x1111);
  stack.Record(6, stack.At(10), 0x2222);
  // The outermost frame's record: no caller's frame pointer.
  stack.Record(10, 0, 0x3333);
  WXT_CHECK(Walk(stack, stack.At(2)) == (std::vector<std::uintptr_t>{0x1111, 0x2222, 0x3333}));
  WXT_CHECK(Walk(stack, stack.At(2), 2) == (std::vector<std::uintptr_t>{0x1111, 0x2222}));
  WXT_CHECK(Walk(stack, stack.At(6)) == (std::vector<std::uintptr_t>{0x2222, 0x3333}));

  // A frame pointer that is something else is not read: outside the stack, not a word's place, half a record at its top.
  WXT_CHECK(Walk(stack, stack.Low() - 64).empty());
  WXT_CHECK(Walk(stack, stack.High()).empty());
  WXT_CHECK(Walk(stack, stack.At(2) + 1).empty());
  WXT_CHECK(Walk(stack, stack.At(15)).empty());
  stack.Record(14, 0, 0x4444);
  WXT_CHECK(Walk(stack, stack.At(14)) == (std::vector<std::uintptr_t>{0x4444}));
  // A thread whose stack is not known has no records read at all.
  std::uintptr_t out[4] = {};
  WXT_CHECK_EQ(simprofile::WalkFrames(stack.At(2), 0, 0, out, 4), 0U);
}

WXT_TEST(AFrameWalkEndsAtARecordThatGoesDownOrOutOrHasNoReturnAddress)
{
  Words stack;
  // A loop: the second record points back at the first.
  stack.Record(2, stack.At(6), 0x1111);
  stack.Record(6, stack.At(2), 0x2222);
  WXT_CHECK(Walk(stack, stack.At(2)) == (std::vector<std::uintptr_t>{0x1111, 0x2222}));
  // A caller's frame pointer past the stack's top: its record is not read.
  stack.Record(6, stack.High() + 64, 0x2222);
  WXT_CHECK(Walk(stack, stack.At(2)) == (std::vector<std::uintptr_t>{0x1111, 0x2222}));
  // A record without a return address ends the walk without it.
  stack.Record(6, stack.At(10), 0);
  WXT_CHECK(Walk(stack, stack.At(2)) == (std::vector<std::uintptr_t>{0x1111}));
}

WXT_TEST(ASampleIsThePcTheLinkRegisterAndTheChainAtMostEightFrames)
{
  Words stack;
  stack.Record(2, stack.At(6), 0x1111);
  stack.Record(6, 0, 0x2222);
  const Sample sample = simprofile::SampleOf(0xA000, 0xB000, stack.At(2), stack.Low(), stack.High());
  WXT_CHECK_EQ(sample.depth, 4U);
  WXT_CHECK_EQ(sample.frames[0], std::uintptr_t{0xA000});
  WXT_CHECK_EQ(sample.frames[1], std::uintptr_t{0xB000});
  WXT_CHECK_EQ(sample.frames[2], std::uintptr_t{0x1111});
  WXT_CHECK_EQ(sample.frames[3], std::uintptr_t{0x2222});
  // x86-64 has no link register.
  const Sample noLink = simprofile::SampleOf(0xA000, 0, stack.At(2), stack.Low(), stack.High());
  WXT_CHECK_EQ(noLink.depth, 3U);
  WXT_CHECK_EQ(noLink.frames[1], std::uintptr_t{0x1111});

  // A chain longer than a sample holds.
  Words deep;
  for (std::size_t index = 0; index + 3 < 16; index += 2) {
    deep.Record(index, deep.At(index + 2), 0x100 + index);
  }
  deep.Record(14, 0, 0x10E);
  const Sample full = simprofile::SampleOf(0xA000, 0xB000, deep.At(0), deep.Low(), deep.High());
  WXT_CHECK_EQ(full.depth, static_cast<std::uint32_t>(simprofile::kMaxFrames));
  WXT_CHECK_EQ(full.frames[simprofile::kMaxFrames - 1], std::uintptr_t{0x10A});
}

WXT_TEST(TheRingGivesItsSamplesInTheirOrderAndKeepsNoneWhenFull)
{
  simprofile::SampleRing<4> ring;
  Sample sample;
  WXT_CHECK(!ring.Pop(sample));
  for (std::uintptr_t at = 1; at <= 4; ++at) {
    Sample one;
    one.depth = 1;
    one.frames[0] = at;
    WXT_CHECK(ring.Push(one));
  }
  Sample fifth;
  fifth.depth = 1;
  fifth.frames[0] = 5;
  WXT_CHECK(!ring.Push(fifth));
  WXT_CHECK(ring.Pop(sample));
  WXT_CHECK_EQ(sample.frames[0], std::uintptr_t{1});
  WXT_CHECK(ring.Push(fifth));
  for (std::uintptr_t at = 2; at <= 5; ++at) {
    WXT_CHECK(ring.Pop(sample));
    WXT_CHECK_EQ(sample.frames[0], at);
  }
  WXT_CHECK(!ring.Pop(sample));
  // Round and round.
  for (std::uintptr_t at = 0; at < 100; ++at) {
    Sample one;
    one.depth = 1;
    one.frames[0] = at;
    WXT_CHECK(ring.Push(one));
    WXT_CHECK(ring.Pop(sample));
    WXT_CHECK_EQ(sample.frames[0], at);
  }
}

WXT_TEST(TheRingTakesTwoThreadsSamplesAtOnceAndGivesEachWhole)
{
  static simprofile::SampleRing<64> ring;
  constexpr std::uintptr_t kEach = 20000;
  std::atomic<std::uint64_t> dropped{0};
  std::atomic<int> going{2};
  const auto fill = [&](const std::uintptr_t thread) {
    for (std::uintptr_t at = 0; at < kEach; ++at) {
      Sample sample;
      sample.depth = simprofile::kMaxFrames;
      for (std::uintptr_t& frame : sample.frames) {
        frame = thread << 32 | at;
      }
      if (!ring.Push(sample)) {
        dropped.fetch_add(1);
      }
    }
    going.fetch_sub(1);
  };
  std::thread first(fill, 1);
  std::thread second(fill, 2);
  std::uint64_t taken = 0;
  bool whole = true;
  std::uintptr_t last[3] = {0, 0, 0};
  bool ordered = true;
  Sample sample;
  for (;;) {
    const bool done = going.load() == 0;
    while (ring.Pop(sample)) {
      ++taken;
      for (const std::uintptr_t frame : sample.frames) {
        whole = whole && frame == sample.frames[0];
      }
      const std::uintptr_t thread = sample.frames[0] >> 32;
      const std::uintptr_t at = sample.frames[0] & 0xFFFFFFFF;
      if (thread == 1 || thread == 2) {
        ordered = ordered && (last[thread] == 0 || at > last[thread] - 1);
        last[thread] = at + 1;
      } else {
        whole = false;
      }
    }
    if (done) {
      break;
    }
  }
  first.join();
  second.join();
  WXT_CHECK(whole);
  WXT_CHECK(ordered);
  WXT_CHECK_EQ(taken + dropped.load(), std::uint64_t{2 * kEach});
}

WXT_TEST(AStacksFramesAreByModuleTheCallersAtTheirCallsAdjacentOnesOnce)
{
  simprofile::Profile profile;
  const std::uint32_t game = profile.Module("libfafgame.so");
  const std::uint32_t libm = profile.Module("libm.so");
  WXT_CHECK_EQ(profile.Module("libfafgame.so"), game);
  simprofile::Segments segments;
  segments.Assign({{0x7000'0000, 0x7100'0000, 0x6FF0'0000, libm}, {0x5000'0000, 0x5200'0000, 0x5000'0000, game}});
  const auto resolve = [&](const std::uintptr_t address) {
    Frame frame;
    (void)segments.Find(address, frame);
    return frame;
  };
  const auto same = [](const std::uintptr_t address) { return address; };

  Sample sample;
  sample.depth = 5;
  sample.frames[0] = 0x7000'0040; // the leaf, in libm
  sample.frames[1] = 0x5000'1004; // the link register: the leaf's caller
  sample.frames[2] = 0x5000'2008;
  sample.frames[3] = 0x4000'0000; // in no module
  sample.frames[4] = 0x5000'3004;
  const Stack stack = simprofile::StackOf(sample, 4, resolve, same);
  WXT_CHECK(stack == StackOf({{libm, 0x10'0040}, {game, 0x1000}, {game, 0x2004}, {simprofile::kUnknown, 0x3FFF'FFFC},
    {game, 0x3000}}));

  // A link register that is the first record's return address, as in a prologue, counts once.
  Sample prologue;
  prologue.depth = 3;
  prologue.frames[0] = 0x5000'0100;
  prologue.frames[1] = 0x5000'1004;
  prologue.frames[2] = 0x5000'1004;
  WXT_CHECK(simprofile::StackOf(prologue, 4, resolve, same) == StackOf({{game, 0x100}, {game, 0x1000}}));

  // The callers' addresses lose what strip takes away before their call is found; the leaf's stays as it is.
  const auto strip = [](const std::uintptr_t address) { return address & 0xFFFF'FFFF; };
  Sample signed_;
  signed_.depth = 2;
  signed_.frames[0] = 0x5000'0100;
  signed_.frames[1] = std::uintptr_t{0x5000'1004} | (std::uintptr_t{0x2A} << 40);
  WXT_CHECK(simprofile::StackOf(signed_, 4, resolve, strip) == StackOf({{game, 0x100}, {game, 0x1000}}));

  // Five frames at most.
  Sample deep;
  deep.depth = simprofile::kMaxFrames;
  for (std::uint32_t at = 0; at < deep.depth; ++at) {
    deep.frames[at] = 0x5000'0000 + 0x100 * (at + 1);
  }
  WXT_CHECK_EQ(simprofile::StackOf(deep, 1, resolve, same).depth, static_cast<std::uint32_t>(simprofile::kStackFrames));
}

WXT_TEST(ASegmentHoldsItsStartAndNotItsEnd)
{
  simprofile::Segments segments;
  segments.Assign({{0x1000, 0x2000, 0x1000, 3}, {0x3000, 0x4000, 0x2000, 4}});
  Frame frame;
  WXT_CHECK(segments.Find(0x1000, frame));
  WXT_CHECK(frame == (Frame{3, 0}));
  WXT_CHECK(segments.Find(0x3FFF, frame));
  WXT_CHECK(frame == (Frame{4, 0x1FFF}));
  WXT_CHECK(!segments.Find(0x2000, frame));
  WXT_CHECK(frame == (Frame{simprofile::kUnknown, 0x2000}));
  WXT_CHECK(!segments.Find(0xFFF, frame));
  WXT_CHECK(!segments.Find(0x4000, frame));
  WXT_CHECK(!simprofile::Segments{}.Find(0x1000, frame));
}

WXT_TEST(TheProfileCountsLeavesAndStacksAndWritesProfileTxt)
{
  simprofile::Profile profile;
  const std::uint32_t game = profile.Module("libfafgame.so");
  const std::uint32_t libm = profile.Module("libm.so");
  const Stack deep = StackOf({{game, 0x1000}, {game, 0x2000}, {game, 0x3000}});
  const Stack shallow = StackOf({{game, 0x1000}, {game, 0x2500}});
  const Stack sine = StackOf({{libm, 0x40}, {game, 0x2000}});
  for (const Stack* stack : {&deep, &sine, &deep, &shallow, &sine, &deep}) {
    profile.Add(*stack);
  }
  profile.Add(Stack{});
  WXT_CHECK_EQ(profile.Samples(), std::uint64_t{6});
  profile.Name({libm, 0x40}, "sinl+0x40");
  profile.Name({libm, 0x80}, "cosl+0x10");
  profile.Name({game, 0x2500}, "");

  simprofile::Header header;
  header.buildId = "4433a795d16bab81";
  header.library = "libfafgame.so";
  header.abi = "arm64-v8a";
  header.hertz = 2;
  header.dropped = 1;
  header.sims = 2;
  header.ticks = 1342;
  header.handlerSeconds = 0.0007;
  header.countingSeconds = 0.21;
  header.seconds = 125.4;
  WXT_CHECK_EQ(profile.Text(header, 2),
    "faf-re sim profile 1\n"
    "build id: 4433a795d16bab81 (libfafgame.so, arm64-v8a)\n"
    "rate: 2 Hz of the sim thread's CPU time, its frames by frame pointer\n"
    "samples: 6 in 2 sims over 1342 ticks, 1 dropped\n"
    "cpu: 3.5 s of the sim thread's time sampled\n"
    "cost: 100.0 us a sample in the handler, 0.020% of the sim thread's time; 0.21 s on the counting thread\n"
    "written: 125 s after the profiler began\n"
    "leaves: 2, the top 2 with 100.00% of the samples\n"
    "stacks: 3, the top 2 with 83.33% of the samples\n"
    "\n"
    "leaves: samples, share, module, offset\n"
    "4 66.67% libfafgame.so 0x1000\n"
    "2 33.33% libm.so 0x40\n"
    "\n"
    "stacks: samples, then the leaf and its callers' calls as module+offset\n"
    "3 libfafgame.so+0x1000 libfafgame.so+0x2000 libfafgame.so+0x3000\n"
    "2 libm.so+0x40 libfafgame.so+0x2000\n"
    "\n"
    "symbols: the frames of modules without their files, by their dynamic symbols\n"
    "libm.so+0x40 sinl+0x40\n");
}

WXT_TEST(EqualCountsAreListedInTheOrderOfTheirFrames)
{
  simprofile::Profile profile;
  const std::uint32_t game = profile.Module("libfafgame.so");
  for (const std::uint64_t offset : {0x30, 0x10, 0x20}) {
    profile.Add(StackOf({{game, offset}}));
  }
  const std::string text = profile.Text(simprofile::Header{}, 3);
  const std::size_t leaves = text.find("\nleaves: samples");
  WXT_CHECK(leaves != std::string::npos);
  WXT_CHECK_EQ(text.substr(leaves, text.find("\n\n", leaves) - leaves),
    "\nleaves: samples, share, module, offset\n"
    "1 33.33% libfafgame.so 0x10\n"
    "1 33.33% libfafgame.so 0x20\n"
    "1 33.33% libfafgame.so 0x30");
}

WXT_TEST(AProfileAtItsCapsCountsNewLeavesAndStacksAmongTheOthersAlone)
{
  simprofile::Profile profile(2, 1);
  const std::uint32_t game = profile.Module("libfafgame.so");
  profile.Add(StackOf({{game, 0x10}}));
  profile.Add(StackOf({{game, 0x20}}));
  profile.Add(StackOf({{game, 0x30}}));
  profile.Add(StackOf({{game, 0x10}}));
  WXT_CHECK_EQ(profile.Samples(), std::uint64_t{4});
  const std::string text = profile.Text(simprofile::Header{}, 10);
  WXT_CHECK(text.find("leaves: 2, the top 2 with 75.00% of the samples; past the first 2, others had 1 of the samples\n") !=
    std::string::npos);
  WXT_CHECK(text.find("stacks: 1, the top 1 with 50.00% of the samples; past the first 1, others had 2 of the samples\n") !=
    std::string::npos);
  WXT_CHECK(text.find("\n2 50.00% libfafgame.so 0x10\n1 25.00% libfafgame.so 0x20\n") != std::string::npos);
}
