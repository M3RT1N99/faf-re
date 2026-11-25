#pragma once

// Where the game's threads run, by the device's classes of cores, and the sim's speed in the report (CoresAndroid.cpp).

namespace moho
{
  class Sim;
}

namespace cores
{
  /// Reads the device's classes of cores, puts the program's thread by them and hands the engine's threads to the app's
  /// placement as they start; starts the watch that places them again after Android set their CPUs anew and has the sim's
  /// speed in the report every 30 s of play. The report has a line of what was set and why. On the program's thread,
  /// before the engine starts.
  void Install();
  /// A beat begins (PerformanceAndroid.cpp hears it from the engine's hook): the sim's thread and its tick, for the speed
  /// lines. On the sim's thread.
  void SimBeat(const moho::Sim& sim);
  /// Puts the calling thread, a light one of the app's, on every CPU, as the cores' watch is. After Install.
  void AnyCpu();
} // namespace cores
