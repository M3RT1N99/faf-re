// The frame sampler (gpg/core/diagnostics/FrameSampler.h) for the Android client build, in place
// of FrameSampler.cpp. The Windows sampler suspends the main thread and walks its stack from
// GetThreadContext; Android has no such call, and a sampler on signals is a tool of its own. A
// frame mark records nothing here.

#include "gpg/core/diagnostics/FrameSampler.h"

namespace gpg::diag
{
  void FrameSamplerMark() {}
} // namespace gpg::diag
