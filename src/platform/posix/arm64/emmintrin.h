#pragma once

// Engine addition (Android port): the SSE2 intrinsics the shared code names, for arm64 builds;
// see xmmintrin.h next to this file. The scalar double lane is lane 0 of a two-double vector,
// as in an XMM register; sqrt is IEEE square root on both (AArch64 `fsqrt` rounds as `sqrtsd`).

#include <xmmintrin.h>

typedef double __m128d __attribute__((__vector_size__(16), __aligned__(16)));
typedef long long __m128i __attribute__((__vector_size__(16), __aligned__(16)));

static inline __m128d _mm_set_sd(const double value) noexcept
{
  return __m128d{value, 0.0};
}

static inline __m128d _mm_setzero_pd(void) noexcept
{
  return __m128d{0.0, 0.0};
}

/// Lane 0 is the square root of `source`'s lane 0; lane 1 is `upper`'s (`sqrtsd`).
static inline __m128d _mm_sqrt_sd(const __m128d upper, const __m128d source) noexcept
{
  return __m128d{__builtin_sqrt(source[0]), upper[1]};
}

static inline double _mm_cvtsd_f64(const __m128d value) noexcept
{
  return value[0];
}
