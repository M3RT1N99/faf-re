#pragma once

// The sim thread's sampling profiler, which writes profile.txt beside the game's report (ProfilerAndroid.cpp).

#include <cstdint>

namespace profiler
{
  /// Gives SIGPROF to the sampler and starts the thread that counts the samples and writes profile.txt; the report has a
  /// line of it, or of why there is no profile. On the program's thread, before the engine starts.
  void Install();
  /// A beat begins (PerformanceAndroid.cpp hears it from the engine's hook), at the sim's tick `tick`: a new sim's thread
  /// takes the timer. On the sim's thread.
  void SimBeat(std::uint32_t tick);
  /// The program ends: the timer stops, and profile.txt gets what came since it was last written. On the program's thread.
  void Finish();
} // namespace profiler
