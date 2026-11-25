#pragma once

// Android's performance hints and thermal headroom (ADPF) for PerformanceAndroid.cpp: the NDK's calls, each behind the
// API level that has it. AdpfAndroid.cpp is a target of its own (faf_adpf, CMakeLists.txt) that takes those APIs as weak
// references, so that the library still loads on Android 8.0, which this build is for
// (cmake/toolchains/android-pins.cmake), while the engine's files keep the NDK's strict check that they call nothing newer.

#include <sys/types.h>

#include <cstdint>

struct APerformanceHintSession;

namespace adpf
{
  /// Whether Android has a hint manager for the process: from Android 13 (API 33), with its service up.
  [[nodiscard]] bool HasHintManager() noexcept;
  /// How often Android would have a session's work told (APerformanceHint_getPreferredUpdateRateNanos): -1 where the
  /// phone's power HAL has no hint sessions, which is Android's own test of their support (HintManagerService), and
  /// without a hint manager.
  [[nodiscard]] std::int64_t PreferredUpdateRateNanos() noexcept;
  /// A hint session for the thread `thread` of this process, `targetNanos` a cycle of its work; null below Android 13 and
  /// where Android makes none, as on a phone or an emulator whose power HAL has no hint sessions. The NDK gives no reason
  /// for a null one (no errno); from Android 16 the system log's perf_hint line has Android's.
  [[nodiscard]] APerformanceHintSession* CreateSession(pid_t thread, std::int64_t targetNanos) noexcept;
  /// 0, or Android's errno: EPIPE once its service cannot be reached.
  [[nodiscard]] int UpdateTarget(APerformanceHintSession* session, std::int64_t targetNanos) noexcept;
  /// The work of the session's last cycle, positive; 0 or Android's errno, as UpdateTarget.
  [[nodiscard]] int ReportActual(APerformanceHintSession* session, std::int64_t actualNanos) noexcept;
  void CloseSession(APerformanceHintSession* session) noexcept;

  /// How much of the phone's thermal envelope is in use now (AThermal_getThermalHeadroom, from Android 12, API 31): 1.0 is
  /// where Android throttles it. NaN below Android 12, where Android gives none, and when asked much more often than once a
  /// second.
  [[nodiscard]] float ThermalHeadroom() noexcept;
} // namespace adpf
