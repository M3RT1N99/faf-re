#pragma once

// Fatal-signal reports for builds that run without adb. A native crash kills
// the :game process before any C++ code can log; the launcher then only sees a
// log that stops mid-run. This handler appends the signal, the faulting
// address and a backtrace (library + offset, symbol where the export table has
// one) to the runtime log, then hands the signal to the handler that was there
// before (Android's debuggerd), so the tombstone and `adb logcat -b crash`
// still get it.
//
// Offsets are into the stripped library from the APK; resolve them with the
// unstripped copy the build keeps (see docs/port/android.md, "Crash reports").

#include <string>

namespace faf::android {

  /// Installs the handler for SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP
  /// and SIGSYS. `logPath` is opened for appending now, so the handler itself
  /// opens nothing. Safe to call more than once; later calls only switch the file.
  void InstallCrashHandler(const std::string& logPath);

} // namespace faf::android
