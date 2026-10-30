#pragma once

// Engine addition (Android port): the calling thread's return addresses, for the crash
// diagnostics and moho::PLAT_GetCallStack on POSIX. backtrace(3) is a glibc extension:
// bionic declares it only from Android 13 (API 33) and musl not at all, so these walk the
// stack with the unwinder's _Unwind_Backtrace, which every C++ runtime with exceptions has.

#if !defined(_WIN32)

namespace platform::posix
{
  /**
   * Up to `maxFrames` return addresses of the calling thread, the innermost first, as
   * backtrace(3): frame 0 is an address in the function that called Backtrace.
   */
  [[nodiscard]] int Backtrace(void** frames, int maxFrames) noexcept;

  /**
   * One line per frame on `fd`, as backtrace_symbols_fd(3): the module, the nearest symbol
   * and its offset where dladdr finds them, and the address. Allocates nothing.
   */
  void BacktraceSymbolsFd(void* const* frames, int count, int fd) noexcept;
} // namespace platform::posix

#endif // !defined(_WIN32)
