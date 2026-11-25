#pragma once
// The sim thread's sampling profile (src/app/ProfilerAndroid.cpp, android/native/README.md, The sim's profile): what a
// sample holds and how it is taken from the interrupted thread's registers and stack (include/SimSampler.h takes it), the
// ring the samples wait in, and how the counting thread counts them by module and offset and writes profile.txt, which
// android/tools/sim_profile.py symbolizes. Headers only, so that tests/griff pins the walk, the ring, the counting and the
// file without a signal, Android or the engine.

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iterator>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace simprofile
{
  /// A sample's addresses: the interrupted PC, the link register (arm64's; none on x86-64), and the return addresses the
  /// frame pointers chain, the innermost first.
  inline constexpr std::size_t kMaxFrames = 8;

  struct Sample
  {
    std::uint32_t depth = 0;
    std::uintptr_t frames[kMaxFrames] = {};
  };

  /// The return addresses of the frame records from `fp` on, at most `room` into `out`: a record is the caller's frame
  /// pointer and the return address, as AAPCS64 and x86-64's frame pointers keep them. Only records wholly within
  /// [`low`, `high`), the thread's stack, are read, each above the one before, so that a frame pointer that holds
  /// something else ends the walk instead of being read. Async-signal-safe.
  [[nodiscard]] inline std::size_t WalkFrames(
    std::uintptr_t fp, const std::uintptr_t low, const std::uintptr_t high, std::uintptr_t* const out, const std::size_t room
  ) noexcept
  {
    constexpr std::uintptr_t kRecord = 2 * sizeof(std::uintptr_t);
    std::size_t count = 0;
    while (count < room && high - low >= kRecord && fp >= low && fp <= high - kRecord && fp % sizeof(std::uintptr_t) == 0) {
      const auto* const record = reinterpret_cast<const std::uintptr_t*>(fp);
      const std::uintptr_t next = record[0];
      const std::uintptr_t returnAddress = record[1];
      if (returnAddress == 0) {
        break;
      }
      out[count++] = returnAddress;
      if (next <= fp) {
        break;
      }
      fp = next;
    }
    return count;
  }

  /// The sample of a thread interrupted at `pc`, with the link register `lr` (0 where the architecture has none) and the
  /// frame pointer `fp`, on a stack of [`low`, `high`). Async-signal-safe.
  [[nodiscard]] inline Sample SampleOf(
    const std::uintptr_t pc, const std::uintptr_t lr, const std::uintptr_t fp, const std::uintptr_t low,
    const std::uintptr_t high
  ) noexcept
  {
    Sample sample;
    sample.frames[sample.depth++] = pc;
    if (lr != 0) {
      sample.frames[sample.depth++] = lr;
    }
    sample.depth += static_cast<std::uint32_t>(
      WalkFrames(fp, low, high, sample.frames + sample.depth, kMaxFrames - sample.depth)
    );
    return sample;
  }

  /// A bounded ring of samples that the signal's handler fills, on any thread, and one thread empties (Vyukov's bounded
  /// queue): lock-free and async-signal-safe on both ends. `kSlots` is a power of two.
  template <std::size_t kSlots>
  class SampleRing
  {
    static_assert(kSlots >= 2 && (kSlots & (kSlots - 1)) == 0, "a power of two");

  public:
    SampleRing() noexcept
    {
      for (std::size_t index = 0; index < kSlots; ++index) {
        mSlots[index].sequence.store(index, std::memory_order_relaxed);
      }
    }

    SampleRing(const SampleRing&) = delete;
    SampleRing& operator=(const SampleRing&) = delete;

    /// False where the ring is full: the sample is not kept.
    bool Push(const Sample& sample) noexcept
    {
      std::uint64_t at = mHead.load(std::memory_order_relaxed);
      for (;;) {
        Slot& slot = mSlots[at & (kSlots - 1)];
        const std::uint64_t sequence = slot.sequence.load(std::memory_order_acquire);
        const auto ahead = static_cast<std::int64_t>(sequence - at);
        if (ahead == 0) {
          if (mHead.compare_exchange_weak(at, at + 1, std::memory_order_relaxed)) {
            slot.sample = sample;
            slot.sequence.store(at + 1, std::memory_order_release);
            return true;
          }
        } else if (ahead < 0) {
          return false;
        } else {
          at = mHead.load(std::memory_order_relaxed);
        }
      }
    }

    /// On the one thread that empties the ring: false while it holds no sample that is whole.
    bool Pop(Sample& sample) noexcept
    {
      Slot& slot = mSlots[mTail & (kSlots - 1)];
      if (slot.sequence.load(std::memory_order_acquire) != mTail + 1) {
        return false;
      }
      sample = slot.sample;
      slot.sequence.store(mTail + kSlots, std::memory_order_release);
      ++mTail;
      return true;
    }

  private:
    struct Slot
    {
      std::atomic<std::uint64_t> sequence{0};
      Sample sample;
    };

    Slot mSlots[kSlots];
    std::atomic<std::uint64_t> mHead{0};
    std::uint64_t mTail = 0;
  };

  /// An address as counted: the index of its module (Profile::Module) and its offset from the module's load bias, which is
  /// the address in the ELF file (the tombstone's rel_pc). An address in no module is kUnknown's at the address itself.
  struct Frame
  {
    std::uint32_t module = 0;
    std::uint64_t offset = 0;

    friend bool operator==(const Frame&, const Frame&) = default;
  };

  inline constexpr std::uint32_t kUnknown = 0;

  /// A sample as counted: its leaf, the instruction the thread was at, then its callers' calls, the innermost first.
  inline constexpr std::size_t kStackFrames = 5;

  struct Stack
  {
    std::uint32_t depth = 0;
    Frame frames[kStackFrames] = {};

    friend bool operator==(const Stack& a, const Stack& b) noexcept
    {
      return a.depth == b.depth && std::equal(a.frames, a.frames + a.depth, b.frames);
    }
  };

  struct FrameHash
  {
    std::size_t operator()(const Frame& frame) const noexcept
    {
      return std::hash<std::uint64_t>{}(frame.offset * 0x9E3779B97F4A7C15ULL ^ frame.module);
    }
  };

  struct StackHash
  {
    std::size_t operator()(const Stack& stack) const noexcept
    {
      std::uint64_t hash = 0xCBF29CE484222325ULL;
      for (std::uint32_t at = 0; at < stack.depth; ++at) {
        hash = (hash ^ FrameHash{}(stack.frames[at])) * 0x100000001B3ULL;
      }
      return static_cast<std::size_t>(hash);
    }
  };

  /// A return address's call: the instruction before it, `callSize` bytes back (4 on arm64; 1 on x86-64, which is inside
  /// the call whatever its length), so that the caller's frame is the call's function and line, not the next one's.
  /// `strip` takes what the address carries besides itself away (arm64's pointer authentication code; nothing elsewhere).
  /// Adjacent frames at the same call count once: a link register that is the first frame record's return address, as in
  /// a function's prologue, or a function that calls itself from one place.
  template <class Resolve, class Strip>
  [[nodiscard]] Stack StackOf(const Sample& sample, const std::uintptr_t callSize, Resolve&& resolve, Strip&& strip)
  {
    Stack stack;
    for (std::uint32_t at = 0; at < sample.depth && stack.depth < kStackFrames; ++at) {
      const std::uintptr_t address = at == 0 ? sample.frames[0] : strip(sample.frames[at]) - callSize;
      const Frame frame = resolve(address);
      if (stack.depth > 0 && stack.frames[stack.depth - 1] == frame) {
        continue;
      }
      stack.frames[stack.depth++] = frame;
    }
    return stack;
  }

  /// What profile.txt's head says of the run, beyond the counts.
  struct Header
  {
    /// libfafgame.so's GNU build id in hex, which sim_profile.py checks against the release's unstripped library.
    std::string buildId;
    std::string library;
    std::string abi;
    int hertz = 0;
    /// Samples the ring had no room for.
    std::uint64_t dropped = 0;
    std::uint32_t sims = 0;
    /// The sims' ticks while the timer sampled them.
    std::uint64_t ticks = 0;
    /// Spent in the signal's handler, over all samples, and on the counting thread.
    double handlerSeconds = 0.0;
    double countingSeconds = 0.0;
    /// When the file was written, from the profiler's start.
    double seconds = 0.0;
  };

  /// The samples counted by their leaf and by their stack, with the modules' names, as the counting thread keeps them. A
  /// leaf or a stack that comes after `maxLeaves` or `maxStacks` others is counted among the others alone, so the memory
  /// stays bounded however long the game.
  class Profile
  {
  public:
    explicit Profile(const std::size_t maxLeaves = 1U << 16, const std::size_t maxStacks = 1U << 16)
      : mMaxLeaves(maxLeaves)
      , mMaxStacks(maxStacks)
    {
      mModules.emplace_back("?");
    }

    /// The index of the module `name`, for Frame::module; kUnknown is "?".
    [[nodiscard]] std::uint32_t Module(const std::string& name)
    {
      for (std::size_t index = 0; index < mModules.size(); ++index) {
        if (mModules[index] == name) {
          return static_cast<std::uint32_t>(index);
        }
      }
      mModules.push_back(name);
      return static_cast<std::uint32_t>(mModules.size() - 1);
    }

    /// What a module's dynamic symbols say of a frame of it ("sinl+0x40"), for the modules whose unstripped files the
    /// release does not keep: the system's. An empty name: the module has none there, and the file lists none.
    void Name(const Frame& frame, std::string name)
    {
      mNames.emplace(frame, std::move(name));
    }

    [[nodiscard]] bool Named(const Frame& frame) const
    {
      return mNames.find(frame) != mNames.end();
    }

    void Add(const Stack& stack)
    {
      if (stack.depth == 0) {
        return;
      }
      ++mSamples;
      Count(mLeaves, stack.frames[0], mMaxLeaves, mOtherLeaves);
      Count(mStacks, stack, mMaxStacks, mOtherStacks);
    }

    [[nodiscard]] std::uint64_t Samples() const noexcept
    {
      return mSamples;
    }

    /// profile.txt: its head, then the top `listed` leaves and stacks, the most samples first.
    [[nodiscard]] std::string Text(const Header& header, const std::size_t listed) const
    {
      const auto leaves = Top(mLeaves, listed);
      const auto stacks = Top(mStacks, listed);
      std::string text = "faf-re sim profile 1\n";
      Append(text, "build id: %s (%s, %s)\n", header.buildId.c_str(), header.library.c_str(), header.abi.c_str());
      Append(text, "rate: %d Hz of the sim thread's CPU time, its frames by frame pointer\n", header.hertz);
      Append(text, "samples: %llu in %u sims over %llu ticks, %llu dropped\n", Number(mSamples), header.sims,
        Number(header.ticks), Number(header.dropped));
      const double sampled = header.hertz > 0 ? static_cast<double>(mSamples + header.dropped) / header.hertz : 0.0;
      Append(text, "cpu: %.1f s of the sim thread's time sampled\n", sampled);
      const double perSample = mSamples + header.dropped > 0
        ? header.handlerSeconds / static_cast<double>(mSamples + header.dropped) : 0.0;
      Append(text, "cost: %.1f us a sample in the handler, %.3f%% of the sim thread's time; %.2f s on the counting thread\n",
        perSample * 1e6, sampled > 0.0 ? 100.0 * header.handlerSeconds / sampled : 0.0, header.countingSeconds);
      Append(text, "written: %.0f s after the profiler began\n", header.seconds);
      Append(text, "leaves: %zu, the top %zu with %s of the samples%s\n", mLeaves.size(), leaves.size(),
        Share(Sum(leaves), mSamples).c_str(), Past(mOtherLeaves, mMaxLeaves).c_str());
      Append(text, "stacks: %zu, the top %zu with %s of the samples%s\n", mStacks.size(), stacks.size(),
        Share(Sum(stacks), mSamples).c_str(), Past(mOtherStacks, mMaxStacks).c_str());
      text += "\nleaves: samples, share, module, offset\n";
      for (const auto& [frame, count] : leaves) {
        Append(text, "%llu %s %s 0x%llx\n", Number(count), Share(count, mSamples).c_str(),
          mModules[frame.module].c_str(), Number(frame.offset));
      }
      text += "\nstacks: samples, then the leaf and its callers' calls as module+offset\n";
      for (const auto& [stack, count] : stacks) {
        Append(text, "%llu", Number(count));
        for (std::uint32_t at = 0; at < stack.depth; ++at) {
          Append(text, " %s+0x%llx", mModules[stack.frames[at].module].c_str(), Number(stack.frames[at].offset));
        }
        text += '\n';
      }
      std::string names;
      for (const auto& [frame, name] : SortedNames(leaves, stacks)) {
        Append(names, "%s+0x%llx %s\n", mModules[frame.module].c_str(), Number(frame.offset), name.c_str());
      }
      if (!names.empty()) {
        text += "\nsymbols: the frames of modules without their files, by their dynamic symbols\n" + names;
      }
      return text;
    }

  private:
    template <class Key, class Hash>
    static void Count(
      std::unordered_map<Key, std::uint64_t, Hash>& counts, const Key& key, const std::size_t most, std::uint64_t& other
    )
    {
      if (const auto found = counts.find(key); found != counts.end()) {
        ++found->second;
      } else if (counts.size() < most) {
        counts.emplace(key, 1);
      } else {
        ++other;
      }
    }

    static bool Before(const Frame& a, const Frame& b) noexcept
    {
      return a.module != b.module ? a.module < b.module : a.offset < b.offset;
    }

    static bool Before(const Stack& a, const Stack& b) noexcept
    {
      for (std::uint32_t at = 0; at < a.depth && at < b.depth; ++at) {
        if (!(a.frames[at] == b.frames[at])) {
          return Before(a.frames[at], b.frames[at]);
        }
      }
      return a.depth < b.depth;
    }

    /// The `listed` most counted, the most first; equal counts in the order of their frames, so a file is the same however
    /// the map's order went.
    template <class Key, class Hash>
    static std::vector<std::pair<Key, std::uint64_t>> Top(
      const std::unordered_map<Key, std::uint64_t, Hash>& counts, const std::size_t listed
    )
    {
      std::vector<std::pair<Key, std::uint64_t>> top(counts.begin(), counts.end());
      const auto more = [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : Before(a.first, b.first);
      };
      if (top.size() > listed) {
        std::partial_sort(top.begin(), top.begin() + static_cast<std::ptrdiff_t>(listed), top.end(), more);
        top.resize(listed);
      } else {
        std::sort(top.begin(), top.end(), more);
      }
      return top;
    }

    template <class Key>
    static std::uint64_t Sum(const std::vector<std::pair<Key, std::uint64_t>>& counts)
    {
      std::uint64_t sum = 0;
      for (const auto& entry : counts) {
        sum += entry.second;
      }
      return sum;
    }

    /// The names of the listed frames that have one, in the order of their frames.
    [[nodiscard]] std::vector<std::pair<Frame, std::string>> SortedNames(
      const std::vector<std::pair<Frame, std::uint64_t>>& leaves, const std::vector<std::pair<Stack, std::uint64_t>>& stacks
    ) const
    {
      std::vector<std::pair<Frame, std::string>> named;
      const auto take = [&](const Frame& frame) {
        if (const auto found = mNames.find(frame); found != mNames.end() && !found->second.empty()) {
          named.emplace_back(frame, found->second);
        }
      };
      for (const auto& leaf : leaves) {
        take(leaf.first);
      }
      for (const auto& stack : stacks) {
        for (std::uint32_t at = 0; at < stack.first.depth; ++at) {
          take(stack.first.frames[at]);
        }
      }
      std::sort(named.begin(), named.end(), [](const auto& a, const auto& b) { return Before(a.first, b.first); });
      named.erase(std::unique(named.begin(), named.end(), [](const auto& a, const auto& b) { return a.first == b.first; }),
        named.end());
      return named;
    }

    static unsigned long long Number(const std::uint64_t value) noexcept
    {
      return static_cast<unsigned long long>(value);
    }

    /// "12.34%" of `total`.
    static std::string Share(const std::uint64_t count, const std::uint64_t total)
    {
      char text[16];
      std::snprintf(text, sizeof text, "%.2f%%", total > 0 ? 100.0 * static_cast<double>(count) / static_cast<double>(total) : 0.0);
      return text;
    }

    static std::string Past(const std::uint64_t other, const std::size_t most)
    {
      if (other == 0) {
        return {};
      }
      char text[96];
      std::snprintf(text, sizeof text, "; past the first %zu, others had %llu of the samples", most, Number(other));
      return text;
    }

    __attribute__((format(printf, 2, 3))) static void Append(std::string& text, const char* const format, ...)
    {
      char line[512];
      std::va_list arguments;
      va_start(arguments, format);
      const int length = std::vsnprintf(line, sizeof line, format, arguments);
      va_end(arguments);
      if (length > 0) {
        text.append(line, std::min(static_cast<std::size_t>(length), sizeof line - 1));
      }
    }

    std::size_t mMaxLeaves;
    std::size_t mMaxStacks;
    std::vector<std::string> mModules;
    std::unordered_map<Frame, std::uint64_t, FrameHash> mLeaves;
    std::unordered_map<Stack, std::uint64_t, StackHash> mStacks;
    std::unordered_map<Frame, std::string, FrameHash> mNames;
    std::uint64_t mSamples = 0;
    std::uint64_t mOtherLeaves = 0;
    std::uint64_t mOtherStacks = 0;
  };

  /// A module's executable segments, by which the counting thread finds an address's module (the app's from
  /// dl_iterate_phdr).
  struct Segment
  {
    std::uintptr_t start = 0;
    std::uintptr_t end = 0;
    /// The module's load bias: an address less it is the address in the ELF file.
    std::uintptr_t bias = 0;
    std::uint32_t module = kUnknown;
  };

  /// The segments, in their order in memory, and the frame of an address in them.
  class Segments
  {
  public:
    void Assign(std::vector<Segment> segments)
    {
      std::sort(segments.begin(), segments.end(), [](const Segment& a, const Segment& b) { return a.start < b.start; });
      mSegments = std::move(segments);
    }

    /// False for an address in none of them, which is kUnknown's at the address itself.
    [[nodiscard]] bool Find(const std::uintptr_t address, Frame& frame) const
    {
      const auto after = std::upper_bound(mSegments.begin(), mSegments.end(), address,
        [](const std::uintptr_t value, const Segment& segment) { return value < segment.start; });
      if (after != mSegments.begin() && address < std::prev(after)->end) {
        frame = Frame{std::prev(after)->module, address - std::prev(after)->bias};
        return true;
      }
      frame = Frame{kUnknown, address};
      return false;
    }

  private:
    std::vector<Segment> mSegments;
  };
} // namespace simprofile
