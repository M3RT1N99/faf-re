#pragma once
// A replay's move back holds the new sim's beats while it plays towards the move's target, the screen keeping what the old
// sim published (the patched moho/sim/ReplayTimeline.cpp and WldSessionControl.cpp, patches/README.md): when the hold
// ends, by the target, the count of the beats held, the engine's heap they grew or a stall, and the slice of the held beats
// a frame applies after it. Headers only, so that tests/griff pins the rules without the engine.

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace replayhold
{
  /// Why a hold ends, or kHolding while it goes on.
  enum class End
  {
    kHolding,
    /// The new sim's beats reached the move's target.
    kTarget,
    /// As many beats are held as the count's cap.
    kBeats,
    /// The engine's heap grew by the memory's cap since the hold's first beat; a cap of none ends it at its first.
    kMemory,
    /// No beat came for the stall's time: the replay ends before the target.
    kStalled,
  };

  struct Hold
  {
    /// The tick of the last beat held; -1 before the first.
    std::int32_t heldTick = -1;
    std::int32_t targetTick = 0;
    std::size_t beats = 0;
    /// How far the engine's heap grew since the hold's first beat, in bytes; less than none where it shrank.
    std::int64_t heapGrowth = 0;
    bool stalled = false;
  };

  struct Caps
  {
    std::size_t beats = 0;
    /// 0: no beat is held past the first, and the move goes on in view at once.
    std::int64_t heapBytes = 0;
  };

  /// Whether the hold ends now, and why: the target first, then the count, the heap and a stall.
  [[nodiscard]] constexpr End HoldEnds(const Hold& hold, const Caps& caps) noexcept
  {
    if (hold.heldTick >= 0 && hold.heldTick >= hold.targetTick) {
      return End::kTarget;
    }
    if (hold.beats >= caps.beats) {
      return End::kBeats;
    }
    // The heap counts from the first beat held, and a hold of none would leave the session no beat of the new sim: its
    // tick, the old sim's, would stay, and the move would end where it began.
    if (hold.beats > 0 && (caps.heapBytes <= 0 || hold.heapGrowth >= caps.heapBytes)) {
      return End::kMemory;
    }
    return hold.stalled ? End::kStalled : End::kHolding;
  }

  /// What a frame of the held beats' application takes, as a fast-forward takes its beats (CWldSession::SessionFrame):
  /// the UI goes on drawing between the slices.
  inline constexpr std::chrono::milliseconds kSliceBudget{15};

  /// The slice rule: a frame applies another held beat while it applied none yet, or its slice took less than `budget`.
  [[nodiscard]] constexpr bool SliceGoesOn(
    const std::size_t appliedThisFrame, const std::chrono::nanoseconds spent,
    const std::chrono::nanoseconds budget = kSliceBudget
  ) noexcept
  {
    return appliedThisFrame == 0 || spent < budget;
  }
} // namespace replayhold
