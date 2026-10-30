#pragma once

// Engine addition (dedicated-server plan WPL.5): <intrin.h> for the Linux build. clang's own
// intrin.h is for Microsoft targets; on Linux the x86 intrinsics come from <x86intrin.h>, and
// the MSVC-only ones the shared code names (the _Interlocked* family) are clang builtins
// under -fms-extensions.
//
// arm64 (Android port): the MSVC-only ones (_Interlocked*, _BitScan*, __debugbreak) are clang
// builtins there too; the x86 ones the code takes from <intrin.h> (_mm_pause, _mm_getcsr) are
// defined for AArch64 in platform/posix/arm64.

#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#else
#include <xmmintrin.h>
#endif
