// The NDK's ADPF calls behind their API levels (AdpfAndroid.h). Compiled with the NDK's weak API references
// (__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__ and -Werror=unguarded-availability, CMakeLists.txt's faf_adpf): a call
// newer than Android 8.0 links as a weak reference into libandroid.so, which the phone's linker leaves null where its
// Android lacks the call, and the compiler takes each only behind __builtin_available.

#include "AdpfAndroid.h"

#include <android/performance_hint.h>
#include <android/thermal.h>

#include <cerrno>
#include <limits>

namespace
{
  /// Android's, one for the process, which is never released.
  [[nodiscard]] APerformanceHintManager* HintManager() noexcept
  {
    if (__builtin_available(android 33, *)) {
      static APerformanceHintManager* const manager = APerformanceHint_getManager();
      return manager;
    }
    return nullptr;
  }
} // namespace

namespace adpf
{
  bool HasHintManager() noexcept
  {
    return HintManager() != nullptr;
  }

  APerformanceHintSession* CreateSession(const pid_t thread, const std::int64_t targetNanos) noexcept
  {
    if (__builtin_available(android 33, *)) {
      if (APerformanceHintManager* const manager = HintManager()) {
        const std::int32_t threads[] = {thread};
        return APerformanceHint_createSession(manager, threads, 1, targetNanos);
      }
    }
    return nullptr;
  }

  int UpdateTarget(APerformanceHintSession* const session, const std::int64_t targetNanos) noexcept
  {
    if (__builtin_available(android 33, *)) {
      return APerformanceHint_updateTargetWorkDuration(session, targetNanos);
    }
    return ENOSYS;
  }

  int ReportActual(APerformanceHintSession* const session, const std::int64_t actualNanos) noexcept
  {
    if (__builtin_available(android 33, *)) {
      return APerformanceHint_reportActualWorkDuration(session, actualNanos);
    }
    return ENOSYS;
  }

  void CloseSession(APerformanceHintSession* const session) noexcept
  {
    if (__builtin_available(android 33, *)) {
      APerformanceHint_closeSession(session);
    }
  }

  float ThermalHeadroom() noexcept
  {
    if (__builtin_available(android 31, *)) {
      // Android's, one for the process, which is never released. Null where its thermal service is not up.
      static AThermalManager* const manager = AThermal_acquireManager();
      if (manager != nullptr) {
        return AThermal_getThermalHeadroom(manager, 0);
      }
    }
    return std::numeric_limits<float>::quiet_NaN();
  }
} // namespace adpf
