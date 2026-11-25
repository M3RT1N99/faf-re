#pragma once

// Where the game's threads run, by the device's classes of cores (CoresAndroid.cpp).

namespace cores
{
  /// Reads the device's classes of cores, puts the program's thread by them and hands the engine's threads to the app's
  /// placement as they start; starts the watch that places them again after Android set their CPUs anew. The report has a
  /// line of what was set and why. On the program's thread, before the engine starts.
  void Install();
} // namespace cores
