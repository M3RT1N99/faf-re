// Engine addition (Android port): the software x87 (src/legacy/math/X87MathSoft.cpp, arm64's x87
// helpers) against the x87 itself (X87Math64.S), routine by routine and bit for bit. Android x86_64
// is the one target that has both an x87 unit and the binary128 long double the software x87
// needs. This file compiles X87MathSoft.cpp again with every routine renamed (soft_msvc8_x87_...,
// FAF_X87_SOFT_NAME), and the build links X87Math64.S, so both run in one program on the same
// inputs (cmake/FafLinux.cmake, faf_x87_soft_check).
//
// Usage:
//   faf_x87_soft_check                    edge cases and seeded random inputs (CI)
//   faf_x87_soft_check --exhaustive [N]   also every float input of the routines that take one
//                                         float, on N threads (default: one per CPU)
//
// Inputs: zeros, denormals, infinities and NaNs of both signs, quiet and signaling, in every
// argument and in every pair of arguments; floats with short significands, whose sums and
// products land on rounding boundaries and next to them; for fsin, fcos and fptan the float of
// each binade up to 2^63 nearest to a multiple of the x87's pi/2, where its 66-bit pi decides the
// result; twister words whose product with the range is a rounding tie only beyond double's 53
// bits; fistp under each rounding mode.
//
// One difference is documented, not failed: acos and asin store fpatan's 64-bit value to a double,
// and a CPU's fpatan can round the last of its 64 bits another way than the C library's binary128
// atan2l rounded to 64 bits does (Intel's and AMD's differ there too, tests/math_dump's
// cpu_variants.txt). Such a pair has to be adjacent doubles that round to the same float, which is
// what every sim caller keeps; it is counted and printed.
//
// One rule is the host's to decide. Of two quiet NaNs with the same significand and opposite
// signs, the software x87 gives the positive one, whichever operand holds it (X87MathSoft.cpp's
// NaNResult): Intel's x87 does, measured on a Core Ultra 7 265KF, while the SDM names only the
// larger significand and AMD's APM (vol. 1, table 6-7, note 4) calls the result undefined. So the
// program first asks this machine's x87, in every two-operand form X87Math64.S assembles to, and
// prints what it gives (ProbeNaNTie). Where it gives the positive NaN every time, a NaN result's
// sign is compared as every other bit; elsewhere a NaN result that differs in its sign alone is
// counted and printed, not failed.

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cpuid.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

// The software x87, every routine renamed: soft_msvc8_x87_sinf beside X87Math64.S's msvc8_x87_sinf.
#define FAF_X87_SOFT_NAME(name) soft_##name
#include "legacy/math/X87MathSoft.cpp"

// X87Math64.S.
extern "C" {
std::int32_t msvc8_x87_round_to_int(float value);
float msvc8_x87_bilinear_height(float h00, float h01, float h10, float h11, float fracX, float fracZ);
float msvc8_x87_subtract_bilinear_height(
  float top,
  float h00,
  float h01,
  float h10,
  float h11,
  float fracX,
  float fracZ
);
float msvc8_x87_sinf(float x);
float msvc8_x87_cosf(float x);
float msvc8_x87_tanf(float x);
float msvc8_x87_atan2f(float y, float x);
double msvc8_x87_acos(double x);
double msvc8_x87_asin(double x);
double msvc8_x87_pow(double x, double y);
float msvc8_x87_atan2_difference(float firstY, float firstX, float secondY, float secondX);
float msvc8_x87_atan2_plus(float y, float x, float addend);
float msvc8_x87_sin_times_cos(float sinArg, float cosArg);
float msvc8_x87_sin_of_product_times(float a, float b, float factor);
float msvc8_x87_twister_word_in_range(float lower, float upper, std::uint32_t word);
void msvc8_x87_slerp_spherical_blend(
  float* outLanes,
  const float* q1Lanes,
  const float* rhsLanes,
  float angle,
  float oneMinusT,
  float t
);
float msvc8_x87_intrinsic_exp(float x);
float msvc8_x87_intrinsic_ln(float x);
float msvc8_x87_intrinsic_ln_quotient(float x, double base);
float msvc8_x87_scaled_ln_quotient(float numerator, float denominator, float scale);
float msvc8_x87_intrinsic_log10(float x);
float msvc8_x87_cos_times_cos(float firstArg, float secondArg);
void msvc8_x87_scale_vector_max_length(float* x, float* y, float* z, float maxLength, float lengthSq);
void msvc8_x87_scale_vector_to_length(float* destLanes, const float* sourceLanes, float lengthSq, float length);
void msvc8_x87_scale_vector(float* destLanes, const float* sourceLanes, float scale);
void msvc8_x87_scale_vector_to_length_of(
  float* destLanes,
  const float* sourceLanes,
  const float* targetLanes,
  float sourceLengthSq
);
float msvc8_x87_box_radius_on_axis(const float* boxLanes, const float* axisLanes);
float msvc8_x87_vector_length_zyx(const float* lanes);
}

namespace softcheck
{
  constexpr int kRandomCases = 1 << 16;
  constexpr std::size_t kExamples = 8;
  unsigned int gExhaustiveThreads = 0; // 0: the exhaustive sweeps are skipped
  bool gTieAsSoftware = true;          // this x87 breaks a NaN tie as the software x87 (ProbeNaNTie)

  // ------------------------------------------------------------------- bits
  std::string Hex(
    const float value
  )
  {
    char text[16];
    std::snprintf(text, sizeof(text), "%08X", std::bit_cast<std::uint32_t>(value));
    return text;
  }

  std::string Hex(
    const double value
  )
  {
    char text[24];
    std::snprintf(text, sizeof(text), "%016llX", static_cast<unsigned long long>(std::bit_cast<std::uint64_t>(value)));
    return text;
  }

  std::string Hex(
    const std::int32_t value
  )
  {
    char text[16];
    std::snprintf(text, sizeof(text), "i%08X", static_cast<std::uint32_t>(value));
    return text;
  }

  std::string Hex(
    const std::uint32_t value
  )
  {
    char text[16];
    std::snprintf(text, sizeof(text), "u%08X", value);
    return text;
  }

  template <std::size_t N>
  std::string Hex(
    const std::array<float, N>& values
  )
  {
    std::string out;
    for (const float value : values) {
      out += (out.empty() ? "" : " ") + Hex(value);
    }
    return out;
  }

  template <class... Args>
  std::string Describe(
    const Args&... args
  )
  {
    std::string out;
    ((out += (out.empty() ? "" : " ") + Hex(args)), ...);
    return out;
  }

  bool Same(
    const float a,
    const float b
  )
  {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
  }

  bool Same(
    const double a,
    const double b
  )
  {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
  }

  bool Same(
    const std::int32_t a,
    const std::int32_t b
  )
  {
    return a == b;
  }

  template <std::size_t N>
  bool Same(
    const std::array<float, N>& a,
    const std::array<float, N>& b
  )
  {
    for (std::size_t lane = 0; lane < N; ++lane) {
      if (!Same(a[lane], b[lane])) {
        return false;
      }
    }
    return true;
  }

  /// The documented acos/asin difference: two finite doubles of one sign, next to each other,
  /// that round to the same float.
  bool LastBitOnly(
    const double x87,
    const double software
  )
  {
    const std::uint64_t a = std::bit_cast<std::uint64_t>(x87);
    const std::uint64_t b = std::bit_cast<std::uint64_t>(software);
    const bool adjacent =
      std::isfinite(x87) && std::isfinite(software) && (a >> 63) == (b >> 63) && (a > b ? a - b : b - a) == 1u;
    return adjacent && Same(static_cast<float>(x87), static_cast<float>(software));
  }

  /// Two NaNs that differ in their sign alone: the two x87s broke a tie of two NaNs of one
  /// significand differently (see the file comment). Only called on results that differ.
  bool NaNSignOnly(
    const float a,
    const float b
  )
  {
    return std::isnan(a) && (std::bit_cast<std::uint32_t>(a) ^ std::bit_cast<std::uint32_t>(b)) == 0x80000000u;
  }

  bool NaNSignOnly(
    const double a,
    const double b
  )
  {
    return std::isnan(a) && (std::bit_cast<std::uint64_t>(a) ^ std::bit_cast<std::uint64_t>(b)) == (1ull << 63);
  }

  bool NaNSignOnly(
    const std::int32_t,
    const std::int32_t
  )
  {
    return false;
  }

  template <std::size_t N>
  bool NaNSignOnly(
    const std::array<float, N>& a,
    const std::array<float, N>& b
  )
  {
    for (std::size_t lane = 0; lane < N; ++lane) {
      if (!Same(a[lane], b[lane]) && !NaNSignOnly(a[lane], b[lane])) {
        return false;
      }
    }
    return true;
  }

  // ------------------------------------------------------------------- the report
  /// What one routine showed: the cases compared, the differences, and the first few of each kind.
  class Differences
  {
  public:
    explicit Differences(
      std::string routine
    )
      : mRoutine(std::move(routine))
    {}

    void Compared(
      const std::uint64_t cases = 1
    )
    {
      mCases += cases;
    }

    void Add(
      std::string text
    )
    {
      const std::lock_guard<std::mutex> lock(mMutex);
      ++mCount;
      if (mExamples.size() < kExamples) {
        mExamples.push_back(std::move(text));
      }
    }

    /// The documented difference: the last bit of acos's or asin's double.
    void AddLastBit(
      std::string text
    )
    {
      const std::lock_guard<std::mutex> lock(mMutex);
      ++mLastBit;
      if (mLastBitExamples.size() < kExamples) {
        mLastBitExamples.push_back(std::move(text));
      }
    }

    /// A NaN result that differs in its sign alone (NaNSignOnly).
    void AddNaNSign(
      std::string text
    )
    {
      const std::lock_guard<std::mutex> lock(mMutex);
      ++mNaNSign;
      if (mNaNSignExamples.size() < kExamples) {
        mNaNSignExamples.push_back(std::move(text));
      }
    }

    /// Prints the documented differences; fails the test for every other one, and for a NaN's
    /// sign where this x87 breaks NaN ties as the software x87 does.
    void Expect() const
    {
      std::printf("%s: %llu cases", mRoutine.c_str(), static_cast<unsigned long long>(mCases.load()));
      if (mLastBit != 0) {
        std::printf(
          ", %llu double result(s) differ in their last bit only (fpatan's last bit; the same float)",
          static_cast<unsigned long long>(mLastBit)
        );
      }
      if (mNaNSign != 0) {
        std::printf(
          ", %llu NaN result(s) differ in their sign only (%s)",
          static_cast<unsigned long long>(mNaNSign),
          gTieAsSoftware ? "a failure: this x87 breaks NaN ties as the software x87" : "this x87's NaN tie"
        );
      }
      std::printf(", %llu other difference(s)\n", static_cast<unsigned long long>(mCount));
      for (const std::string& example : mLastBitExamples) {
        std::printf("  last bit: %s\n", example.c_str());
      }
      for (const std::string& example : mNaNSignExamples) {
        std::printf("  NaN sign: %s\n", example.c_str());
      }
      std::fflush(stdout);
      std::string examples;
      for (const std::string& example : mExamples) {
        examples += "\n  " + example;
      }
      EXPECT_EQ(mCount, 0u) << mRoutine << ": " << mCount << " of " << mCases.load()
                            << " results differ from the x87, the first:" << examples;
      if (gTieAsSoftware) {
        EXPECT_EQ(mNaNSign, 0u) << mRoutine << ": " << mNaNSign
                                << " NaN results differ from the x87's in their sign, and this x87 gives the "
                                   "positive NaN of a tie, as the software x87 does (printed above)";
      }
    }

  private:
    std::string mRoutine;
    std::atomic<std::uint64_t> mCases{0};
    std::mutex mMutex;
    std::uint64_t mCount = 0;
    std::uint64_t mLastBit = 0;
    std::uint64_t mNaNSign = 0;
    std::vector<std::string> mExamples;
    std::vector<std::string> mLastBitExamples;
    std::vector<std::string> mNaNSignExamples;
  };

  /// Runs the x87 and the software routine on `args` and records a difference.
  template <class X87, class Software, class... Args>
  void Compare(
    Differences& differences,
    const X87& x87,
    const Software& software,
    const Args&... args
  )
  {
    const auto expected = x87(args...);
    const auto actual = software(args...);
    differences.Compared();
    if (Same(expected, actual)) {
      return;
    }
    std::string text = Describe(args...) + " -> x87 " + Hex(expected) + ", software " + Hex(actual);
    if (NaNSignOnly(expected, actual)) {
      differences.AddNaNSign(std::move(text));
    } else {
      differences.Add(std::move(text));
    }
  }

  /// Compare for acos and asin, which may differ in the double's last bit (see the file comment).
  void CompareFpatanDouble(
    Differences& differences,
    double (*const x87)(double),
    double (*const software)(double),
    const double x
  )
  {
    const double expected = x87(x);
    const double actual = software(x);
    differences.Compared();
    if (Same(expected, actual)) {
      return;
    }
    std::string text = Hex(x) + " -> x87 " + Hex(expected) + ", software " + Hex(actual);
    if (LastBitOnly(expected, actual)) {
      differences.AddLastBit(std::move(text));
    } else if (NaNSignOnly(expected, actual)) {
      differences.AddNaNSign(std::move(text));
    } else {
      differences.Add(std::move(text));
    }
  }

  // ------------------------------------------------------------------- inputs
  /// splitmix64, seeded by a name: the same inputs on every run and every machine.
  class Random
  {
  public:
    explicit Random(
      const std::string& name
    )
    {
      for (const char c : name) {
        mState = (mState ^ static_cast<unsigned char>(c)) * 0x100000001B3ull;
      }
    }

    std::uint64_t Next()
    {
      std::uint64_t z = (mState += 0x9E3779B97F4A7C15ull);
      z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
      z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
      return z ^ (z >> 31);
    }

    /// 1 to 12 significant bits, between 2^-20 and 2^20, either sign: sums and products of such
    /// values land exactly on rounding boundaries and next to them.
    float Short()
    {
      const std::uint64_t r = Next();
      const int bits = 1 + static_cast<int>(r % 12u);
      const std::uint32_t significand =
        (1u << (bits - 1)) | (static_cast<std::uint32_t>(r >> 8) & ((1u << (bits - 1)) - 1u));
      const int exponent = static_cast<int>((r >> 40) % 41u) - 20;
      const float magnitude = std::ldexp(static_cast<float>(significand), exponent - (bits - 1));
      return ((r >> 63) != 0) ? -magnitude : magnitude;
    }

    /// Any bit pattern (1/8), a short float (3/8), in [-1, 1] (1/4), or of any magnitude from 2^-40
    /// to 2^40 (1/4).
    float Float()
    {
      const std::uint64_t r = Next();
      const auto bits = static_cast<std::uint32_t>(r >> 32);
      switch (r & 7u) {
      case 0:
        return std::bit_cast<float>(bits);
      case 1:
      case 2:
      case 3:
        return Short();
      case 4:
      case 5: {
        const float unit = static_cast<float>(bits >> 8) * 0x1p-24f;
        return ((r >> 8) & 1u) ? -unit : unit;
      }
      default: {
        const int exponent = static_cast<int>((r >> 8) % 81u) - 40;
        const float magnitude = std::ldexp(1.0f + static_cast<float>(bits >> 9) * 0x1p-23f, exponent);
        return ((r >> 16) & 1u) ? -magnitude : magnitude;
      }
      }
    }

    /// A widened float (1/2), a full double in [-1, 1] (1/4), any bit pattern (1/8), or a short
    /// float's value (1/8).
    double Double()
    {
      const std::uint64_t r = Next();
      switch (r & 7u) {
      case 0:
        return std::bit_cast<double>(Next());
      case 1:
        return static_cast<double>(Short());
      case 2:
      case 3: {
        const double unit = static_cast<double>(Next() >> 11) * 0x1p-53;
        return ((r >> 8) & 1u) ? -unit : unit;
      }
      default:
        return static_cast<double>(Float());
      }
    }

  private:
    std::uint64_t mState = 0x9E3779B97F4A7C15ull;
  };

  const std::vector<float>& EdgeFloats()
  {
    static const std::vector<float> values = [] {
      const std::uint32_t bits[] = {
        0x00000000u, 0x80000000u,                                        // +-0
        0x00000001u, 0x80000001u, 0x007FFFFFu, 0x00400000u, 0x80400001u, // denormals
        0x00800000u, 0x80800000u, 0x7F7FFFFFu, 0xFF7FFFFFu,              // +-FLT_MIN, +-FLT_MAX
        0x7F800000u, 0xFF800000u,                                        // +-inf
        0x7FC00000u, 0xFFC00000u, 0x7FC00001u, 0xFFFFFFFFu,              // quiet NaNs
        0x7FA00000u, 0xFFA00000u, 0x7F800001u, 0xFFBFFFFFu,              // signaling NaNs
        0x3F800000u, 0xBF800000u, 0x3F000000u, 0xBF000000u,              // +-1, +-0.5
        0x3FC00000u, 0xC0200000u, 0x40200000u, 0xBFC00000u,              // +-1.5, +-2.5 (halfway)
        0x3EFFFFFFu, 0x3F7FFFFFu, 0x3F800001u, 0xBF7FFFFFu, 0xBF800001u, // next to 0.5 and +-1
        0x40490FDBu, 0xC0490FDBu, 0x3FC90FDBu, 0x40C90FDBu, 0x4096CBE4u, // +-pi, pi/2, 2pi, 3pi/2
        0x3F490FDBu, 0x4B000000u, 0x4AFFFFFFu, 0x4B7FFFFFu, 0x4B800000u, // pi/4, 2^23, 2^23 - 0.5, 2^24
        0x4EFFFFFFu, 0x4F000000u, 0xCF000000u, 0xCF000001u, 0x4F32D05Eu, // next to +-2^31, 3e9
        0x5EFFFFFFu, 0x5F000000u, 0xDF000000u, 0xDEFFFFFFu, 0x5F800000u, // next to +-2^63, 2^64
        0x42B17217u, 0x42B17218u, 0xC2AEAC50u, 0xC2CE8ED0u, 0x43000000u, // exp's limits, 128
        0x0DA24260u, 0x1E3CE508u, 0x5F0AC723u, 0x358637BDu, 0x49742400u, // 1e-30, 1e-20, 1e19, 1e-6, 1e6
        0x3DCCCCCDu, 0xC2C80000u,                                        // 0.1, -100
      };
      std::vector<float> out;
      for (const std::uint32_t b : bits) {
        out.push_back(std::bit_cast<float>(b));
      }
      return out;
    }();
    return values;
  }

  /// The values that make an x87 instruction take its special rules, for pairs of arguments.
  const std::vector<float>& SpecialFloats()
  {
    static const std::vector<float> values = [] {
      const std::uint32_t bits[] = {
        0x00000000u,
        0x80000000u,
        0x7F800000u,
        0xFF800000u,
        0x7FC00000u,
        0xFFC00000u,
        0x7FA00000u,
        0xFF800001u,
        0x7FC00001u,
        0xFFFFFFFFu,
        0x00000001u,
        0xFF7FFFFFu,
        0x3F800000u,
      };
      std::vector<float> out;
      for (const std::uint32_t b : bits) {
        out.push_back(std::bit_cast<float>(b));
      }
      return out;
    }();
    return values;
  }

  const std::vector<double>& EdgeDoubles()
  {
    static const std::vector<double> values = [] {
      std::vector<double> out;
      for (const float value : EdgeFloats()) {
        out.push_back(static_cast<double>(value));
      }
      const std::uint64_t bits[] = {
        0x0000000000000001ull, 0x800FFFFFFFFFFFFFull,
        0x0010000000000000ull, 0x7FEFFFFFFFFFFFFFull, // denormals, limits
        0x7FF8000000000001ull, 0xFFF8000000000000ull,
        0x7FF0000000000001ull, 0xFFF4000000000000ull, // NaNs
        0x3FEFFFFFFFFFFFFFull, 0x3FF0000000000001ull,
        0xBFEFFFFFFFFFFFFFull, 0xBFF0000000000001ull, // next to +-1
        0x3FDFFFFFFFFFFFFFull, 0x3FE0000000000001ull,
        0x3E50000000000000ull, 0x3CA0000000000000ull, // 0.5, 2^-26, 2^-53
        0x7E37E43C8800759Cull, 0x01A56E1FC2F8F359ull,
        0x3FE6A09E667F3BCDull, 0xBFE6A09E667F3BCDull, // 1e300, 1e-300, +-sqrt(0.5)
      };
      for (const std::uint64_t b : bits) {
        out.push_back(std::bit_cast<double>(b));
      }
      return out;
    }();
    return values;
  }

  /**
   * For every binade from [1, 2) to [2^62, 2^63), the float nearest to a multiple of the x87's
   * pi/2, its two neighbours, and their negatives. fsin, fcos and fptan reduce their argument by
   * the x87's pi, pi truncated to 66 bits (0xC90FDAA22168C234C * 2^-66), so these are the arguments
   * where that pi decides the most bits. A float is m * 2^(e - 23), m of 24 bits, so its quotient by
   * pi/2 is m * 2^(e + 44) / P with P = 0xC90FDAA22168C234C (pi/2 * 2^67): the m whose
   * m * 2^(e + 44) mod P lies nearest to 0 or P.
   */
  const std::vector<float>& ReductionCases()
  {
    static const std::vector<float> values = [] {
      using Wide = unsigned __int128;
      const Wide halfPi = (Wide{0xCu} << 64) | 0x90FDAA22168C234Cull;
      std::vector<float> out;
      for (int e = 0; e <= 62; ++e) {
        Wide step = 1; // 2^(e + 44) mod P
        for (int i = 0; i < e + 44; ++i) {
          step = (step << 1) % halfPi;
        }
        Wide residue = (step << 23) % halfPi; // m = 2^23
        Wide best = halfPi;
        std::uint32_t bestSignificand = 0;
        for (std::uint32_t m = 1u << 23; m < (1u << 24); ++m) {
          const Wide distance = residue < halfPi - residue ? residue : halfPi - residue;
          if (distance < best) {
            best = distance;
            bestSignificand = m;
          }
          residue += step;
          if (residue >= halfPi) {
            residue -= halfPi;
          }
        }
        const float x = std::ldexp(static_cast<float>(bestSignificand), e - 23);
        for (const float value : {std::nextafter(x, 0.0f), x, std::nextafter(x, HUGE_VALF)}) {
          out.push_back(value);
          out.push_back(-value);
        }
      }
      return out;
    }();
    return values;
  }

  /**
   * The cases for a routine of N float arguments: each edge value in each argument (the others
   * short random values), each pair of special values in each pair of arguments, the edge values
   * together (every pair for N = 2, rotations otherwise), then random arguments.
   */
  template <std::size_t N, class Fn>
  void ForEachFloats(
    const std::string& name,
    const std::vector<float>& extraEdges,
    const Fn& fn
  )
  {
    std::vector<float> edges = EdgeFloats();
    edges.insert(edges.end(), extraEdges.begin(), extraEdges.end());
    Random random(name);
    std::array<float, N> args{};
    const auto background = [&] {
      for (float& arg : args) {
        arg = random.Short();
      }
    };
    for (std::size_t slot = 0; slot < N; ++slot) {
      for (const float edge : edges) {
        background();
        args[slot] = edge;
        fn(args);
      }
    }
    for (std::size_t first = 0; first < N; ++first) {
      for (std::size_t second = first + 1; second < N; ++second) {
        for (const float a : SpecialFloats()) {
          for (const float b : SpecialFloats()) {
            background();
            args[first] = a;
            args[second] = b;
            fn(args);
          }
        }
      }
    }
    if constexpr (N == 2) {
      for (const float a : EdgeFloats()) {
        for (const float b : EdgeFloats()) {
          args = {a, b};
          fn(args);
        }
      }
    } else if constexpr (N > 2) {
      for (const std::size_t stride : {1u, 7u, 13u}) {
        for (std::size_t i = 0; i < edges.size(); ++i) {
          for (std::size_t slot = 0; slot < N; ++slot) {
            args[slot] = edges[(i + slot * stride) % edges.size()];
          }
          fn(args);
        }
      }
    }
    for (int i = 0; i < kRandomCases; ++i) {
      for (float& arg : args) {
        arg = random.Float();
      }
      fn(args);
    }
  }

  /// A routine of N float arguments, on every case ForEachFloats makes.
  template <std::size_t N, class X87, class Software>
  void CheckFloats(
    const std::string& name,
    const X87& x87,
    const Software& software,
    const std::vector<float>& extraEdges = {}
  )
  {
    Differences differences(name);
    ForEachFloats<N>(name, extraEdges, [&](const std::array<float, N>& args) {
      Compare(differences, x87, software, args);
    });
    differences.Expect();
  }

  /// A routine of plain arguments as a function of its argument array.
  template <class Fn>
  auto OnArray(
    Fn* const fn
  )
  {
    return [fn](const auto& args) {
      return std::apply(fn, args);
    };
  }

  // The routines that write through pointers, as functions of their inputs.
  template <auto Fn>
  std::array<float, 4> SlerpLanes(
    const std::array<float, 11>& a
  )
  {
    std::array<float, 4> out{};
    Fn(out.data(), a.data(), a.data() + 4, a[8], a[9], a[10]);
    return out;
  }

  template <auto Fn>
  std::array<float, 3> ScaleVectorMaxLengthLanes(
    const std::array<float, 5>& a
  )
  {
    std::array<float, 3> xyz = {a[0], a[1], a[2]};
    Fn(&xyz[0], &xyz[1], &xyz[2], a[3], a[4]);
    return xyz;
  }

  template <auto Fn>
  std::array<float, 3> ScaleVectorToLengthLanes(
    const std::array<float, 5>& a
  )
  {
    std::array<float, 3> out{};
    Fn(out.data(), a.data(), a[3], a[4]);
    return out;
  }

  template <auto Fn>
  std::array<float, 3> ScaleVectorLanes(
    const std::array<float, 4>& a
  )
  {
    std::array<float, 3> out{};
    Fn(out.data(), a.data(), a[3]);
    return out;
  }

  template <auto Fn>
  std::array<float, 3> ScaleVectorToLengthOfLanes(
    const std::array<float, 7>& a
  )
  {
    std::array<float, 3> out{};
    Fn(out.data(), a.data(), a.data() + 3, a[6]);
    return out;
  }

  /// box: center (0-2), axes (3-11), extents (12-14); then the axis (15-17).
  template <auto Fn>
  float BoxRadiusOfLanes(
    const std::array<float, 18>& a
  )
  {
    return Fn(a.data(), a.data() + 15);
  }

  template <auto Fn>
  float VectorLengthOfLanes(
    const std::array<float, 3>& a
  )
  {
    return Fn(a.data());
  }

  // ------------------------------------------------------------------- the x87's state
  std::uint16_t X87ControlWord()
  {
    std::uint16_t controlWord = 0;
    __asm__ volatile("fnstcw %0" : "=m"(controlWord));
    return controlWord;
  }

  void SetX87ControlWord(
    std::uint16_t controlWord
  )
  {
    __asm__ volatile("fldcw %0" : : "m"(controlWord));
  }

  /// The CPU's vendor and brand string: an x87 transcendental's last bit depends on it.
  std::string CpuName()
  {
    unsigned int regs[4] = {};
    __cpuid(0u, regs[0], regs[1], regs[2], regs[3]);
    char vendor[13] = {};
    std::memcpy(vendor + 0, &regs[1], 4);
    std::memcpy(vendor + 4, &regs[3], 4);
    std::memcpy(vendor + 8, &regs[2], 4);
    std::string name = vendor;
    __cpuid(0x80000000u, regs[0], regs[1], regs[2], regs[3]);
    if (regs[0] >= 0x80000004u) {
      char brand[49] = {};
      for (unsigned int leaf = 0x80000002u; leaf <= 0x80000004u; ++leaf) {
        __cpuid(leaf, regs[0], regs[1], regs[2], regs[3]);
        std::memcpy(brand + (leaf - 0x80000002u) * 16u, regs, 16);
      }
      const std::string text = brand;
      const std::size_t start = text.find_first_not_of(' ');
      name += " " + (start == std::string::npos ? std::string{} : text.substr(start));
    }
    return name;
  }

  // ------------------------------------------------------------------- the NaN tie
  /**
   * What this machine's x87 gives where the software x87 gives the positive NaN (see the file
   * comment): each two-operand form of X87Math64.S meets two quiet NaNs of one significand and
   * opposite signs, the positive one in st(0) and then in the other operand, for each quiet NaN
   * that the inputs bring together with its negative. Prints what it found; true when every case
   * gave the positive NaN.
   */
  bool ProbeNaNTie()
  {
    // A register form: st(1) = first, st(0) = second; a memory form: st(0) = second, the float
    // operand = first. Then the instruction, and the result from st(0), after a pop where the form
    // writes st(1) and pops nothing. fninit before and after: an empty stack, every exception masked.
#define FAF_TIE_REGISTERS(INSTRUCTION)                                                                                 \
  [](const float first, const float second) {                                                                          \
    std::uint32_t out = 0;                                                                                             \
    __asm__ volatile("fninit\n\tflds %1\n\tflds %2\n\t" INSTRUCTION "\n\tfstps %0\n\tfninit"                           \
                     : "=m"(out)                                                                                       \
                     : "m"(first), "m"(second));                                                                       \
    return out;                                                                                                        \
  }
#define FAF_TIE_MEMORY(INSTRUCTION)                                                                                    \
  [](const float first, const float second) {                                                                          \
    std::uint32_t out = 0;                                                                                             \
    __asm__ volatile("fninit\n\tflds %1\n\t" INSTRUCTION " %2\n\tfstps %0\n\tfninit"                                   \
                     : "=m"(out)                                                                                       \
                     : "m"(second), "m"(first));                                                                       \
    return out;                                                                                                        \
  }
    struct Form
    {
      const char* name;  // llvm-objdump's
      const char* other; // the operand that is not st(0)
      std::uint32_t (*run)(float first, float second);
    };
    // The opcodes of X87Math64.S's object that take two operands, with st(1) for its st(i). The
    // register forms are bytes: in AT&T syntax, fsub, fsubr, fdiv and fdivr with a st(i)
    // destination name each other's opcodes.
    const Form forms[] = {
      {"fadd st, st(1)", "st(1)", FAF_TIE_REGISTERS(".byte 0xD8, 0xC1")},
      {"fmul st, st(1)", "st(1)", FAF_TIE_REGISTERS(".byte 0xD8, 0xC9")},
      {"fsub st, st(1)", "st(1)", FAF_TIE_REGISTERS(".byte 0xD8, 0xE1")},
      {"fmul st(1), st", "st(1)", FAF_TIE_REGISTERS(".byte 0xDC, 0xC9\n\tfstp %%st(0)")},
      {"fsubr st(1), st", "st(1)", FAF_TIE_REGISTERS(".byte 0xDC, 0xE1\n\tfstp %%st(0)")},
      {"fsub st(1), st", "st(1)", FAF_TIE_REGISTERS(".byte 0xDC, 0xE9\n\tfstp %%st(0)")},
      {"faddp st(1), st", "st(1)", FAF_TIE_REGISTERS(".byte 0xDE, 0xC1")},
      {"fmulp st(1), st", "st(1)", FAF_TIE_REGISTERS(".byte 0xDE, 0xC9")},
      {"fdivrp st(1), st", "st(1)", FAF_TIE_REGISTERS(".byte 0xDE, 0xF1")},
      {"fdivp st(1), st", "st(1)", FAF_TIE_REGISTERS(".byte 0xDE, 0xF9")},
      {"fyl2x", "st(1)", FAF_TIE_REGISTERS("fyl2x")},
      {"fpatan", "st(1)", FAF_TIE_REGISTERS("fpatan")},
      {"fscale", "st(1)", FAF_TIE_REGISTERS("fscale")},
      {"fadd dword ptr", "memory", FAF_TIE_MEMORY("fadds")},
      {"fmul dword ptr", "memory", FAF_TIE_MEMORY("fmuls")},
      {"fsubr dword ptr", "memory", FAF_TIE_MEMORY("fsubrs")},
      {"fdiv dword ptr", "memory", FAF_TIE_MEMORY("fdivs")},
      {"fdivr dword ptr", "memory", FAF_TIE_MEMORY("fdivrs")},
    };
#undef FAF_TIE_REGISTERS
#undef FAF_TIE_MEMORY
    const std::uint16_t controlWord = X87ControlWord();
    int cases = 0;
    std::vector<std::string> others;
    // The quiet NaNs that the inputs bring together with their negatives: 0x7FC00000 with 0xFFC00000
    // and the indefinite; 0x7FE00000 and 0xFFE00000, which fld makes of 0x7FA00000 and 0xFFA00000;
    // 0x7FC00001 with 0xFFC00001, which fld makes of 0xFF800001.
    for (const std::uint32_t positive : {0x7FC00000u, 0x7FE00000u, 0x7FC00001u}) {
      const float plus = std::bit_cast<float>(positive);
      const float minus = std::bit_cast<float>(positive | 0x80000000u);
      for (const Form& form : forms) {
        for (const bool positiveInSt0 : {true, false}) {
          const std::uint32_t result = positiveInSt0 ? form.run(minus, plus) : form.run(plus, minus);
          ++cases;
          if (result != positive) {
            char text[96];
            std::snprintf(
              text,
              sizeof(text),
              "%s, %08X in %s: %08X",
              form.name,
              positive,
              positiveInSt0 ? "st(0)" : form.other,
              result
            );
            others.emplace_back(text);
          }
        }
      }
    }
    SetX87ControlWord(controlWord);
    std::printf(
      "NaN tie: the positive NaN in %d of %d cases, as the software x87 gives it (two quiet NaNs of one significand "
      "and opposite signs, either one in st(0), in each of X87Math64.S's %zu two-operand forms)",
      cases - static_cast<int>(others.size()),
      cases,
      std::size(forms)
    );
    if (others.empty()) {
      std::printf("; a NaN result's sign is compared as every other bit\n");
    } else {
      std::printf(
        "; AMD's APM leaves the result undefined, so a NaN result that differs in its sign alone is counted, not "
        "failed. Not the positive NaN:\n"
      );
      for (const std::string& other : others) {
        std::printf("  %s\n", other.c_str());
      }
    }
    std::fflush(stdout);
    return others.empty();
  }

  // ------------------------------------------------------------------- every float input
  struct Sweep
  {
    /// What the bits hold.
    enum class Result
    {
      Integer,
      Float,
      FpatanDouble, // acos, asin: the double's last bit is documented
    };

    const char* name;
    std::uint64_t (*x87)(float);
    std::uint64_t (*software)(float);
    Result result;
  };

  template <float (*Fn)(float)>
  std::uint64_t FloatBits(
    const float x
  )
  {
    return std::bit_cast<std::uint32_t>(Fn(x));
  }

  template <float (*Fn)(float, float)>
  std::uint64_t ArctangentBits(
    const float x
  )
  {
    return std::bit_cast<std::uint32_t>(Fn(x, 1.0f)); // fa::atanf
  }

  template <double (*Fn)(double)>
  std::uint64_t WidenedBits(
    const float x
  )
  {
    return std::bit_cast<std::uint64_t>(Fn(static_cast<double>(x))); // fa::acosf, Lua's math.acos and math.asin
  }

  template <std::int32_t (*Fn)(float)>
  std::uint64_t IntegerBits(
    const float x
  )
  {
    return static_cast<std::uint32_t>(Fn(x));
  }

  /// Every float bit pattern through `sweep`, 2^20 inputs per block, `threads` threads.
  void RunSweep(
    const Sweep& sweep,
    const unsigned int threads
  )
  {
    Differences differences(std::string(sweep.name) + " (every float)");
    constexpr std::uint32_t kBlocks = 1u << 12;
    std::atomic<std::uint32_t> nextBlock{0};
    const auto start = std::chrono::steady_clock::now();
    const auto work = [&] {
      for (std::uint32_t block = nextBlock++; block < kBlocks; block = nextBlock++) {
        for (std::uint32_t index = 0; index < (1u << 20); ++index) {
          const float x = std::bit_cast<float>((block << 20) | index);
          const std::uint64_t expected = sweep.x87(x);
          const std::uint64_t actual = sweep.software(x);
          if (expected == actual) {
            continue;
          }
          if (sweep.result == Sweep::Result::FpatanDouble) {
            const double a = std::bit_cast<double>(expected);
            const double b = std::bit_cast<double>(actual);
            std::string text = Hex(x) + " -> x87 " + Hex(a) + ", software " + Hex(b);
            if (LastBitOnly(a, b)) {
              differences.AddLastBit(std::move(text));
            } else if (NaNSignOnly(a, b)) {
              differences.AddNaNSign(std::move(text));
            } else {
              differences.Add(std::move(text));
            }
          } else {
            char text[96];
            std::snprintf(
              text,
              sizeof(text),
              "%s -> x87 %llX, software %llX",
              Hex(x).c_str(),
              static_cast<unsigned long long>(expected),
              static_cast<unsigned long long>(actual)
            );
            const auto asFloat = [](const std::uint64_t bits) {
              return std::bit_cast<float>(static_cast<std::uint32_t>(bits));
            };
            if (sweep.result == Sweep::Result::Float && NaNSignOnly(asFloat(expected), asFloat(actual))) {
              differences.AddNaNSign(text);
            } else {
              differences.Add(text);
            }
          }
        }
      }
    };
    std::vector<std::thread> workers;
    for (unsigned int i = 1; i < threads; ++i) {
      workers.emplace_back(work);
    }
    work();
    for (std::thread& worker : workers) {
      worker.join();
    }
    differences.Compared(std::uint64_t{1} << 32);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("%s: every float input in %.0f s on %u thread(s)\n", sweep.name, seconds, threads);
    differences.Expect();
  }
} // namespace softcheck

using softcheck::CheckFloats;
using softcheck::OnArray;

TEST(
  X87SoftCheck,
  RoundToInt
)
{
  // fistp follows the rounding field: each of the four, set alike for the x87 and the software.
  const std::uint16_t saved = softcheck::X87ControlWord();
  for (const unsigned int rounding : {0u, 1u, 2u, 3u}) {
    const auto controlWord = static_cast<std::uint16_t>(0x027Fu | (rounding << 10));
    softcheck::SetX87ControlWord(controlWord);
    soft_msvc8_x87_set_control_word(controlWord);
    CheckFloats<1>(
      "round_to_int, rounding field " + std::to_string(rounding),
      OnArray(msvc8_x87_round_to_int),
      OnArray(soft_msvc8_x87_round_to_int)
    );
  }
  softcheck::SetX87ControlWord(saved);
  soft_msvc8_x87_set_control_word(0x027Fu);
}

TEST(
  X87SoftCheck,
  BilinearHeight
)
{
  CheckFloats<6>("bilinear_height", OnArray(msvc8_x87_bilinear_height), OnArray(soft_msvc8_x87_bilinear_height));
}

TEST(
  X87SoftCheck,
  SubtractBilinearHeight
)
{
  CheckFloats<7>(
    "subtract_bilinear_height",
    OnArray(msvc8_x87_subtract_bilinear_height),
    OnArray(soft_msvc8_x87_subtract_bilinear_height)
  );
}

TEST(
  X87SoftCheck,
  Sinf
)
{
  CheckFloats<1>("sinf", OnArray(msvc8_x87_sinf), OnArray(soft_msvc8_x87_sinf), softcheck::ReductionCases());
}

TEST(
  X87SoftCheck,
  Cosf
)
{
  CheckFloats<1>("cosf", OnArray(msvc8_x87_cosf), OnArray(soft_msvc8_x87_cosf), softcheck::ReductionCases());
}

TEST(
  X87SoftCheck,
  Tanf
)
{
  CheckFloats<1>("tanf", OnArray(msvc8_x87_tanf), OnArray(soft_msvc8_x87_tanf), softcheck::ReductionCases());
}

TEST(
  X87SoftCheck,
  Atan2f
)
{
  CheckFloats<2>("atan2f", OnArray(msvc8_x87_atan2f), OnArray(soft_msvc8_x87_atan2f));
}

TEST(
  X87SoftCheck,
  AcosAndAsin
)
{
  for (const bool arcsine : {false, true}) {
    const auto x87 = arcsine ? msvc8_x87_asin : msvc8_x87_acos;
    const auto software = arcsine ? soft_msvc8_x87_asin : soft_msvc8_x87_acos;
    const char* const name = arcsine ? "asin" : "acos";
    softcheck::Differences differences(name);
    for (const double x : softcheck::EdgeDoubles()) {
      softcheck::CompareFpatanDouble(differences, x87, software, x);
    }
    softcheck::Random random(name);
    for (int i = 0; i < softcheck::kRandomCases * 4; ++i) {
      softcheck::CompareFpatanDouble(differences, x87, software, random.Double());
    }
    differences.Expect();
  }
}

TEST(
  X87SoftCheck,
  Pow
)
{
  softcheck::Differences differences("pow");
  for (const double x : softcheck::EdgeDoubles()) {
    for (const double y : softcheck::EdgeDoubles()) {
      softcheck::Compare(differences, msvc8_x87_pow, soft_msvc8_x87_pow, x, y);
    }
  }
  // fa::pow's positive lane: a finite positive base, a finite exponent.
  softcheck::Random random("pow");
  for (int i = 0; i < softcheck::kRandomCases; ++i) {
    const double x = random.Double();
    softcheck::Compare(differences, msvc8_x87_pow, soft_msvc8_x87_pow, std::fabs(x), random.Double());
    softcheck::Compare(differences, msvc8_x87_pow, soft_msvc8_x87_pow, x, static_cast<double>(random.Short()));
  }
  differences.Expect();
}

TEST(
  X87SoftCheck,
  Atan2Difference
)
{
  CheckFloats<4>("atan2_difference", OnArray(msvc8_x87_atan2_difference), OnArray(soft_msvc8_x87_atan2_difference));
}

TEST(
  X87SoftCheck,
  Atan2Plus
)
{
  CheckFloats<3>("atan2_plus", OnArray(msvc8_x87_atan2_plus), OnArray(soft_msvc8_x87_atan2_plus));
}

TEST(
  X87SoftCheck,
  SinTimesCos
)
{
  CheckFloats<2>(
    "sin_times_cos",
    OnArray(msvc8_x87_sin_times_cos),
    OnArray(soft_msvc8_x87_sin_times_cos),
    softcheck::ReductionCases()
  );
}

TEST(
  X87SoftCheck,
  SinOfProductTimes
)
{
  CheckFloats<3>(
    "sin_of_product_times", OnArray(msvc8_x87_sin_of_product_times), OnArray(soft_msvc8_x87_sin_of_product_times)
  );
  // The reduction's hard cases as the product itself.
  softcheck::Differences differences("sin_of_product_times, reduction cases");
  softcheck::Random random("sin_of_product_times, reduction cases");
  for (const float x : softcheck::ReductionCases()) {
    for (const float factor : {1.0f, random.Short()}) {
      softcheck::Compare(
        differences, msvc8_x87_sin_of_product_times, soft_msvc8_x87_sin_of_product_times, x, 1.0f, factor
      );
      softcheck::Compare(
        differences, msvc8_x87_sin_of_product_times, soft_msvc8_x87_sin_of_product_times, 1.0f, x, factor
      );
    }
  }
  differences.Expect();
}

TEST(
  X87SoftCheck,
  TwisterWordInRange
)
{
  const auto x87 = msvc8_x87_twister_word_in_range;
  const auto software = soft_msvc8_x87_twister_word_in_range;
  softcheck::Differences differences("twister_word_in_range");
  const std::uint32_t words[] = {
    0u,
    1u,
    2u,
    0x00FFFFFFu,
    0x01000000u,
    0x01000001u,
    0x7FFFFF80u,
    0x7FFFFFC0u,
    0x7FFFFFFFu,
    0x80000000u,
    0x80000001u,
    0xFFFFFF7Fu,
    0xFFFFFF80u,
    0xFFFFFFFFu,
  };
  for (const float lower : softcheck::EdgeFloats()) {
    for (const float upper : softcheck::EdgeFloats()) {
      for (const std::uint32_t word : words) {
        softcheck::Compare(differences, x87, software, lower, upper, word);
      }
    }
  }
  // A word that loads non-negative keeps its 31 bits, so range * word can need 55: words that make
  // it a 24-bit halfway point +-1, which double's 53 bits round onto the halfway point itself.
  softcheck::Random random("twister_word_in_range");
  for (int ties = 0; ties < 512;) {
    const std::uint32_t range = (1u << 23) | static_cast<std::uint32_t>(random.Next() & 0x7FFFFFu) | 1u;
    std::uint32_t inverse = range; // range * inverse == 1 (mod 2^32), by Newton's iteration
    for (int i = 0; i < 5; ++i) {
      inverse *= 2u - range * inverse;
    }
    for (const std::uint32_t halfway : {(1u << 30) + 1u, (1u << 30) - 1u}) {
      const std::uint32_t word = (halfway * inverse) & 0x7FFFFFFFu;
      if (((std::uint64_t{range} * word) >> 54) != 1u) {
        continue; // not 55 bits
      }
      const float upper = std::ldexp(static_cast<float>(range), static_cast<int>(random.Next() % 61u) - 30);
      softcheck::Compare(differences, x87, software, 0.0f, upper, word);
      softcheck::Compare(differences, x87, software, -upper, 0.0f, word);
      ++ties;
    }
  }
  for (int i = 0; i < softcheck::kRandomCases; ++i) {
    const float lower = random.Float();
    const float upper = random.Float();
    softcheck::Compare(differences, x87, software, lower, upper, static_cast<std::uint32_t>(random.Next()));
  }
  differences.Expect();
}

TEST(
  X87SoftCheck,
  SlerpSphericalBlend
)
{
  using softcheck::SlerpLanes;
  CheckFloats<11>(
    "slerp_spherical_blend",
    SlerpLanes<msvc8_x87_slerp_spherical_blend>,
    SlerpLanes<soft_msvc8_x87_slerp_spherical_blend>
  );
  // The reduction's hard cases as the angle.
  softcheck::Differences differences("slerp_spherical_blend, reduction cases");
  softcheck::Random random("slerp_spherical_blend, reduction cases");
  for (const float angle : softcheck::ReductionCases()) {
    std::array<float, 11> args{};
    for (float& arg : args) {
      arg = random.Short();
    }
    args[8] = angle;
    args[9] = 1.0f;
    args[10] = 1.0f;
    softcheck::Compare(
      differences, SlerpLanes<msvc8_x87_slerp_spherical_blend>, SlerpLanes<soft_msvc8_x87_slerp_spherical_blend>, args
    );
  }
  differences.Expect();
}

TEST(
  X87SoftCheck,
  IntrinsicExp
)
{
  CheckFloats<1>("intrinsic_exp", OnArray(msvc8_x87_intrinsic_exp), OnArray(soft_msvc8_x87_intrinsic_exp));
}

TEST(
  X87SoftCheck,
  IntrinsicLn
)
{
  CheckFloats<1>("intrinsic_ln", OnArray(msvc8_x87_intrinsic_ln), OnArray(soft_msvc8_x87_intrinsic_ln));
}

TEST(
  X87SoftCheck,
  IntrinsicLnQuotient
)
{
  const auto x87 = msvc8_x87_intrinsic_ln_quotient;
  const auto software = soft_msvc8_x87_intrinsic_ln_quotient;
  softcheck::Differences differences("intrinsic_ln_quotient");
  for (const float x : softcheck::EdgeFloats()) {
    for (const double base : softcheck::EdgeDoubles()) {
      softcheck::Compare(differences, x87, software, x, base);
    }
  }
  softcheck::Random random("intrinsic_ln_quotient");
  for (int i = 0; i < softcheck::kRandomCases; ++i) {
    const float x = random.Float();
    softcheck::Compare(differences, x87, software, x, random.Double());
  }
  differences.Expect();
}

TEST(
  X87SoftCheck,
  ScaledLnQuotient
)
{
  CheckFloats<3>(
    "scaled_ln_quotient", OnArray(msvc8_x87_scaled_ln_quotient), OnArray(soft_msvc8_x87_scaled_ln_quotient)
  );
}

TEST(
  X87SoftCheck,
  IntrinsicLog10
)
{
  CheckFloats<1>("intrinsic_log10", OnArray(msvc8_x87_intrinsic_log10), OnArray(soft_msvc8_x87_intrinsic_log10));
}

TEST(
  X87SoftCheck,
  CosTimesCos
)
{
  CheckFloats<2>(
    "cos_times_cos",
    OnArray(msvc8_x87_cos_times_cos),
    OnArray(soft_msvc8_x87_cos_times_cos),
    softcheck::ReductionCases()
  );
}

TEST(
  X87SoftCheck,
  ScaleVectorMaxLength
)
{
  using softcheck::ScaleVectorMaxLengthLanes;
  CheckFloats<5>(
    "scale_vector_max_length",
    ScaleVectorMaxLengthLanes<msvc8_x87_scale_vector_max_length>,
    ScaleVectorMaxLengthLanes<soft_msvc8_x87_scale_vector_max_length>
  );
}

TEST(
  X87SoftCheck,
  ScaleVectorToLength
)
{
  using softcheck::ScaleVectorToLengthLanes;
  CheckFloats<5>(
    "scale_vector_to_length",
    ScaleVectorToLengthLanes<msvc8_x87_scale_vector_to_length>,
    ScaleVectorToLengthLanes<soft_msvc8_x87_scale_vector_to_length>
  );
}

TEST(
  X87SoftCheck,
  ScaleVector
)
{
  using softcheck::ScaleVectorLanes;
  CheckFloats<4>(
    "scale_vector", ScaleVectorLanes<msvc8_x87_scale_vector>, ScaleVectorLanes<soft_msvc8_x87_scale_vector>
  );
}

TEST(
  X87SoftCheck,
  ScaleVectorToLengthOf
)
{
  using softcheck::ScaleVectorToLengthOfLanes;
  CheckFloats<7>(
    "scale_vector_to_length_of",
    ScaleVectorToLengthOfLanes<msvc8_x87_scale_vector_to_length_of>,
    ScaleVectorToLengthOfLanes<soft_msvc8_x87_scale_vector_to_length_of>
  );
}

TEST(
  X87SoftCheck,
  BoxRadiusOnAxis
)
{
  using softcheck::BoxRadiusOfLanes;
  CheckFloats<18>(
    "box_radius_on_axis",
    BoxRadiusOfLanes<msvc8_x87_box_radius_on_axis>,
    BoxRadiusOfLanes<soft_msvc8_x87_box_radius_on_axis>
  );
}

TEST(
  X87SoftCheck,
  VectorLengthZyx
)
{
  using softcheck::VectorLengthOfLanes;
  CheckFloats<3>(
    "vector_length_zyx",
    VectorLengthOfLanes<msvc8_x87_vector_length_zyx>,
    VectorLengthOfLanes<soft_msvc8_x87_vector_length_zyx>
  );
}

TEST(
  X87SoftCheck,
  EveryFloatInput
)
{
  if (softcheck::gExhaustiveThreads == 0) {
    GTEST_SKIP() << "every float input: faf_x87_soft_check --exhaustive [threads]";
  }
  using namespace softcheck;
  using Result = Sweep::Result;
  const Sweep sweeps[] = {
    {"round_to_int", IntegerBits<msvc8_x87_round_to_int>, IntegerBits<soft_msvc8_x87_round_to_int>, Result::Integer},
    {"sinf", FloatBits<msvc8_x87_sinf>, FloatBits<soft_msvc8_x87_sinf>, Result::Float},
    {"cosf", FloatBits<msvc8_x87_cosf>, FloatBits<soft_msvc8_x87_cosf>, Result::Float},
    {"tanf", FloatBits<msvc8_x87_tanf>, FloatBits<soft_msvc8_x87_tanf>, Result::Float},
    {"atan2f(x, 1)", ArctangentBits<msvc8_x87_atan2f>, ArctangentBits<soft_msvc8_x87_atan2f>, Result::Float},
    {"intrinsic_exp", FloatBits<msvc8_x87_intrinsic_exp>, FloatBits<soft_msvc8_x87_intrinsic_exp>, Result::Float},
    {"intrinsic_ln", FloatBits<msvc8_x87_intrinsic_ln>, FloatBits<soft_msvc8_x87_intrinsic_ln>, Result::Float},
    {"intrinsic_log10", FloatBits<msvc8_x87_intrinsic_log10>, FloatBits<soft_msvc8_x87_intrinsic_log10>, Result::Float},
    {"acos(double(x))", WidenedBits<msvc8_x87_acos>, WidenedBits<soft_msvc8_x87_acos>, Result::FpatanDouble},
    {"asin(double(x))", WidenedBits<msvc8_x87_asin>, WidenedBits<soft_msvc8_x87_asin>, Result::FpatanDouble},
  };
  for (const Sweep& sweep : sweeps) {
    RunSweep(sweep, gExhaustiveThreads);
  }
}

int main(
  int argc,
  char** argv
)
{
  ::testing::InitGoogleTest(&argc, argv);
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--exhaustive") == 0) {
      softcheck::gExhaustiveThreads = std::max(1u, std::thread::hardware_concurrency());
      if (i + 1 < argc && std::atoi(argv[i + 1]) > 0) {
        softcheck::gExhaustiveThreads = static_cast<unsigned int>(std::atoi(argv[++i]));
      }
    } else {
      std::fprintf(stderr, "usage: faf_x87_soft_check [--exhaustive [threads]]\n");
      return 2;
    }
  }
  std::printf("faf_x87_soft_check: the x87 of %s\n", softcheck::CpuName().c_str());
  softcheck::gTieAsSoftware = softcheck::ProbeNaNTie();
  return RUN_ALL_TESTS();
}
