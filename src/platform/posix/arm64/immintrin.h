#pragma once

// Engine addition (Android port): <immintrin.h> for arm64 builds (platform/Platform.h includes
// it on every non-MSVC build): the SSE and SSE2 intrinsics the shared code names, as
// xmmintrin.h and emmintrin.h next to this file define them on AArch64.

#include <emmintrin.h>
