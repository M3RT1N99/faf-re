// Shared part of the runner's user-side stand-ins; see HeadlessStubs.h for the rules they follow.

#include "HeadlessStubs.h"

#include <cstdio>

#include "gpg/core/utils/Global.h"
#include "gpg/core/utils/Logging.h"

namespace faf_runner
{
  void StubReached(const char* const name) noexcept
  {
    std::fprintf(stderr, "[stub] %s\n", name);
    std::fflush(stderr);
    try {
      gpg::Warnf("[stub] %s", name);
    } catch (...) {
      // Reporting must never change what the caller does; stderr already has the line.
    }
  }

  void TrapReached(const char* const name)
  {
    StubReached(name);
    gpg::Die("[stub] %s: not reached by the Windows headless runner; no stand-in exists", name);
  }
} // namespace faf_runner
