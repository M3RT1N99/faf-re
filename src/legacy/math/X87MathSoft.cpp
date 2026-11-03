// Engine addition (Android port): legacy/math/X87Math64.S for targets without an x87 unit
// (arm64). Every routine computes, in software, what its x87 instruction sequence computes;
// each one below follows the assembly instruction for instruction.
//
// An x87 register holds a 64-bit significand and a 15-bit exponent. binary128, the `long double`
// of AArch64 (and of Android x86_64), has a 113-bit significand and the same exponent range, so
// it holds every register value exactly. Each emulated instruction forms its result exactly, or
// to 113 bits with the exact error beside it, and rounds it as the x87 does:
//
//  - fadd, fsub, fmul, fdiv, fsqrt to the precision-control width (24 bits in every routine
//    that computes: SET_PC24), with the extended exponent range, so nothing overflows or goes
//    denormal at float limits before the store;
//  - fsin, fcos, fptan, fpatan, fyl2x, f2xm1 to 64 bits (precision control does not apply to
//    them);
//  - fld, fabs, fchs, fscale and frndint exactly; fstp to the memory format, once.
//
// binary128 arithmetic is software on every CPU, and the collision test's box radius alone made
// a game's simulation several times slower with it. So the routines without a transcendental
// instruction compute in double when their inputs and results are finite, which gives the same
// bits by construction (see "The routines that only compute, in double" below); binary128 is
// left for NaNs, infinities and the transcendental routines.
//
// The transcendental results come from the C library's binary128 functions (bionic and glibc
// both carry FreeBSD's ld128 msun, about one ulp at 113 bits), rounded to the nearest 64-bit
// value. The x87 rounds its own internal result instead, within one 64-bit ulp of the true
// value, so the two can differ in the last of the 64 bits; every routine rounds that value to
// 24 bits or to float before anything uses it, which turns such a difference into a different
// result only within 2^-63 of a rounding boundary. (Intel and AMD differ there too.)
//
// fsin, fcos and fptan first reduce their argument by multiples of pi/2 taken from the x87's
// own pi, which is pi truncated to 66 bits (Intel SDM: 0.C90FDAA22168C234C h * 2^2). The
// reduction here is exact with that pi, so a large argument (k * pi/2 with k past about 2^40)
// gives the x87's result, not the true function's.
//
// NaNs as the x87 handles them (Intel SDM vol. 1, table 4-7): fld of a float or double
// signaling NaN makes it quiet, while a memory operand of an arithmetic instruction (fmul
// dword ptr [..]) reaches it signaling, so the routines below load each input the way their
// assembly does (Load or Memory). An instruction with a signaling and a quiet NaN gives the
// quiet one; with two of a kind, the larger significand; one NaN comes out quiet. An invalid
// operation (inf - inf, 0 * inf, 0 / 0, the square root or logarithm of a negative number)
// gives the indefinite, the negative quiet NaN, not AArch64's positive one.
//
// The rounding-control field is round to nearest on every simulation thread (FpEnvPosix.cpp,
// and nothing in the engine changes it), and the routines here round to nearest. fistp is the
// exception: msvc8_x87_round_to_int follows the thread's rounding field, as fistp does.
//
// tests/sim_math checks every routine against golden values from the x87 (windows-x64), and
// tests/sim_math/X87SoftCheck.cpp against X87Math64.S on Android x86_64, which has both an x87
// unit and a binary128 long double.

#include <bit>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <limits>

#if !(LDBL_MANT_DIG == 113 && LDBL_MAX_EXP == 16384)
#error "legacy/math/X87MathSoft.cpp needs a binary128 long double (AArch64, Android x86_64)"
#endif

// The comparison test compiles this file again with every routine renamed, to run it next to
// X87Math64.S in one program.
#if !defined(FAF_X87_SOFT_NAME)
#define FAF_X87_SOFT_NAME(name) name
#endif

namespace
{
  /// One x87 register value: at most 64 significant bits, the x87 exponent range.
  using Reg = long double;
  using U128 = unsigned __int128;

  constexpr int kExtendedBits = 64;
  constexpr int kPc24Bits = 24;
  constexpr U128 kQuietBit = U128{1} << 111;
  constexpr U128 kSignificandMask = (U128{1} << 112) - 1;

  /// The x64 threads' control word (platform/posix/FpEnvPosix.h, kSimX87ControlWord): 53-bit
  /// precision, round to nearest, every exception masked.
  constexpr unsigned int kThreadStartControlWord = 0x027F;
  thread_local unsigned int tControlWord = kThreadStartControlWord;

  constexpr Reg kTwoPow63 = 9223372036854775808.0L;

  /// The x87 "real indefinite": the quiet NaN an invalid operation gives (0xFFC00000 as float).
  [[nodiscard]] Reg Indefinite() noexcept
  {
    return -std::numeric_limits<Reg>::quiet_NaN();
  }

  // --- Loads and rounding ----------------------------------------------------------------------

  [[nodiscard]] bool IsSignaling(const Reg value) noexcept
  {
    return std::isnan(value) && (std::bit_cast<U128>(value) & kQuietBit) == 0;
  }

  [[nodiscard]] Reg Quiet(const Reg value) noexcept
  {
    return std::isnan(value) ? std::bit_cast<Reg>(std::bit_cast<U128>(value) | kQuietBit) : value;
  }

  /// A float memory operand of an arithmetic instruction: exact, a signaling NaN still
  /// signaling (the binary128 conversion keeps the quiet bit as it is).
  [[nodiscard]] Reg Memory(const float value) noexcept
  {
    return static_cast<Reg>(value);
  }

  /// fld dword / qword: exact; a signaling NaN becomes quiet.
  [[nodiscard]] Reg Load(const float value) noexcept
  {
    return Quiet(static_cast<Reg>(value));
  }

  [[nodiscard]] Reg Load(const double value) noexcept
  {
    return Quiet(static_cast<Reg>(value));
  }

  /**
   * `value + residual` (the exact result: `value` is it to 113 bits, `residual` the rest)
   * rounded to `bits` significant bits, to nearest even, keeping the x87 exponent range. Only
   * the sign of `residual` matters, and only when `value` lies exactly halfway between two
   * `bits`-bit numbers: anywhere else, the exact result is on the same side.
   */
  [[nodiscard]] Reg RoundToBits(const Reg value, const Reg residual, const int bits) noexcept
  {
    U128 raw = std::bit_cast<U128>(value);
    const unsigned int exponentField = static_cast<unsigned int>(raw >> 112) & 0x7FFFu;
    if (exponentField == 0 || exponentField == 0x7FFFu) {
      // Zero, infinity, NaN; a binary128 denormal is no x87 register value.
      return value;
    }
    const int dropped = 113 - bits;
    const U128 unit = U128{1} << dropped;
    const U128 low = raw & (unit - 1);
    const U128 half = unit >> 1;
    raw -= low; // toward zero; sign and exponent stay
    bool up = low > half;
    if (low == half) {
      up = residual != 0 ? ((residual > 0) == (value > 0)) : ((raw & unit) != 0);
    }
    if (up) {
      raw += unit; // a carry out of the significand moves to the next binade, exactly
    }
    return std::bit_cast<Reg>(raw);
  }

  [[nodiscard]] Reg Round64(const Reg value) noexcept
  {
    return RoundToBits(value, 0.0L, kExtendedBits);
  }

  /**
   * The result of an instruction with NaN operands: of a signaling and a quiet one, the quiet
   * one; of two of a kind, the one with the larger significand; quiet in every case. The
   * indefinite when neither operand was a NaN (an invalid operation).
   */
  [[nodiscard]] Reg NaNResult(const Reg a, const Reg b) noexcept
  {
    if (std::isnan(a) && std::isnan(b)) {
      if (IsSignaling(a) != IsSignaling(b)) {
        return IsSignaling(a) ? b : a;
      }
      const U128 significandA = std::bit_cast<U128>(a) & kSignificandMask;
      const U128 significandB = std::bit_cast<U128>(b) & kSignificandMask;
      return Quiet(significandA >= significandB ? a : b);
    }
    if (std::isnan(a)) {
      return Quiet(a);
    }
    if (std::isnan(b)) {
      return Quiet(b);
    }
    return Indefinite();
  }

  // --- Arithmetic under precision control (`bits`) -------------------------------------------

  [[nodiscard]] Reg Add(const Reg a, const Reg b, const int bits = kPc24Bits) noexcept
  {
    const Reg sum = a + b;
    if (std::isnan(sum)) {
      return NaNResult(a, b);
    }
    if (std::isinf(sum)) {
      return sum;
    }
    // TwoSum: the exact error of the 113-bit sum.
    const Reg bPart = sum - a;
    const Reg error = (a - (sum - bPart)) + (b - bPart);
    return RoundToBits(sum, error, bits);
  }

  /// a - b (a NaN operand passes through unnegated).
  [[nodiscard]] Reg Sub(const Reg a, const Reg b, const int bits = kPc24Bits) noexcept
  {
    if (std::isnan(a) || std::isnan(b)) {
      return NaNResult(a, b);
    }
    return Add(a, -b, bits);
  }

  [[nodiscard]] Reg Mul(const Reg a, const Reg b, const int bits = kPc24Bits) noexcept
  {
    const Reg product = a * b;
    if (std::isnan(product)) {
      return NaNResult(a, b);
    }
    if (std::isinf(product)) {
      return product;
    }
    return RoundToBits(product, std::fma(a, b, -product), bits);
  }

  [[nodiscard]] Reg Div(const Reg a, const Reg b, const int bits = kPc24Bits) noexcept
  {
    const Reg quotient = a / b;
    if (std::isnan(quotient)) {
      return NaNResult(a, b);
    }
    if (std::isinf(quotient) || quotient == 0) {
      return quotient;
    }
    // a - quotient*b is exact (fma) and has the sign of (exact quotient - quotient) * b.
    const Reg remainder = std::fma(-quotient, b, a);
    return RoundToBits(quotient, remainder / b, bits);
  }

  [[nodiscard]] Reg Sqrt(const Reg a, const int bits = kPc24Bits) noexcept
  {
    if (std::isnan(a)) {
      return a;
    }
    if (a < 0) {
      return Indefinite();
    }
    const Reg root = std::sqrt(a);
    if (std::isinf(root) || root == 0) {
      return root;
    }
    // a - root^2 is exact (fma) and has the sign of (exact root - root).
    return RoundToBits(root, std::fma(-root, root, a), bits);
  }

  // --- Transcendental instructions (64-bit results) -------------------------------------------
  //
  // The C library calls are in the out-of-line helpers marked so: the libm gate
  // (tests/linux/LibmGate.py) names the function that calls the C library, and
  // tests/linux/libm_allowlist.txt lists these.

  /// The x87's pi/2 (pi truncated to 66 bits, halved) in two parts, 0x1921FB544 * 2^-32 and
  /// 0x85A308D3 * 2^-65, short enough that their products with any quotient below 2^63 are
  /// exact in binary128.
  const Reg kX87HalfPiHigh = std::ldexp(static_cast<Reg>(0x1921FB544ull), -32);
  const Reg kX87HalfPiLow = std::ldexp(static_cast<Reg>(0x85A308D3ull), -65);

  struct SReducedAngle
  {
    Reg remainder;
    unsigned int quadrant;
  };

  /**
   * x = k * (the x87's pi/2) + remainder, with k the nearest integer: x - k * high is exact
   * (it needs at most about 96 bits), and so is subtracting k * low, so the remainder is the
   * exact one for that pi.
   */
  [[nodiscard]] SReducedAngle ReduceByX87HalfPi(const Reg x) noexcept
  {
    const Reg k = nearbyintl(x / (kX87HalfPiHigh + kX87HalfPiLow));
    if (k == 0) {
      return {x, 0u};
    }
    const Reg remainder = (x - k * kX87HalfPiHigh) - k * kX87HalfPiLow;
    return {remainder, static_cast<unsigned int>(static_cast<std::int64_t>(k) & 3)};
  }

  /// sin(remainder + quadrant * pi/2). C library calls.
  [[nodiscard, gnu::noinline]] Reg SineOfQuadrant(const Reg remainder, const unsigned int quadrant) noexcept
  {
    switch (quadrant & 3u) {
    case 0:
      return sinl(remainder);
    case 1:
      return cosl(remainder);
    case 2:
      return -sinl(remainder);
    default:
      return -cosl(remainder);
    }
  }

  /// tan(remainder + quadrant * pi/2). C library call.
  [[nodiscard, gnu::noinline]] Reg TangentOfQuadrant(const Reg remainder, const unsigned int quadrant) noexcept
  {
    const Reg tangent = tanl(remainder);
    return (quadrant & 1u) == 0 ? tangent : -1.0L / tangent;
  }

  /// fsin. An operand of magnitude 2^63 or more is left as it is (C2 set; no routine checks it).
  [[nodiscard]] Reg Fsin(const Reg x) noexcept
  {
    if (std::isnan(x)) {
      return x;
    }
    if (std::isinf(x)) {
      return Indefinite();
    }
    if (std::fabs(x) >= kTwoPow63) {
      return x;
    }
    const SReducedAngle reduced = ReduceByX87HalfPi(x);
    return Round64(SineOfQuadrant(reduced.remainder, reduced.quadrant));
  }

  /// fcos, as fsin: cos(x) = sin(x + pi/2).
  [[nodiscard]] Reg Fcos(const Reg x) noexcept
  {
    if (std::isnan(x)) {
      return x;
    }
    if (std::isinf(x)) {
      return Indefinite();
    }
    if (std::fabs(x) >= kTwoPow63) {
      return x;
    }
    const SReducedAngle reduced = ReduceByX87HalfPi(x);
    return Round64(SineOfQuadrant(reduced.remainder, reduced.quadrant + 1u));
  }

  /// fptan's tan(x) (the 1.0 it pushes after it is dropped by every routine). Out of range
  /// (|x| >= 2^63) it pushes nothing, the drop pops x, and the store underflows the stack: the
  /// indefinite.
  [[nodiscard]] Reg Fptan(const Reg x) noexcept
  {
    if (std::isnan(x)) {
      return x;
    }
    if (std::isinf(x) || std::fabs(x) >= kTwoPow63) {
      return Indefinite();
    }
    const SReducedAngle reduced = ReduceByX87HalfPi(x);
    return Round64(TangentOfQuadrant(reduced.remainder, reduced.quadrant));
  }

  /// fpatan: atan(st1 / st0) in the quadrant of (st0, st1), here atan2(y, x). C library call.
  [[nodiscard, gnu::noinline]] Reg Fpatan(const Reg y, const Reg x) noexcept
  {
    if (std::isnan(y) || std::isnan(x)) {
      return NaNResult(y, x);
    }
    return Round64(atan2l(y, x));
  }

  /// fyl2x: st1 * log2(st0), here y * log2(x). Its invalid cases (x < 0, 0 * log2 of 0 or
  /// infinity, infinity * log2(1)) give the indefinite. C library call.
  [[nodiscard, gnu::noinline]] Reg Fyl2x(const Reg x, const Reg y) noexcept
  {
    if (std::isnan(x) || std::isnan(y)) {
      return NaNResult(x, y);
    }
    if (x < 0) {
      return Indefinite();
    }
    const Reg result = y * log2l(x);
    if (std::isnan(result)) {
      return Indefinite();
    }
    return Round64(result);
  }

  /// f2xm1: 2^x - 1, for |x| <= 1 (every routine passes |x| <= 0.5). C library call.
  [[nodiscard, gnu::noinline]] Reg F2xm1(const Reg x) noexcept
  {
    constexpr Reg kLn2 = 0.6931471805599453094172321214581765680755L;
    if (std::isnan(x) || x == 0) {
      return x;
    }
    return Round64(expm1l(x * kLn2));
  }

  /// frndint under the thread's rounding field (always round to nearest here).
  [[nodiscard]] Reg Frndint(const Reg x) noexcept
  {
    return nearbyintl(x);
  }

  /// fscale: st0 * 2^trunc(st1), exact.
  [[nodiscard]] Reg Fscale(const Reg value, const Reg scale) noexcept
  {
    if (std::isnan(value) || std::isnan(scale)) {
      return NaNResult(value, scale);
    }
    const Reg power = std::trunc(scale);
    const int exponent = power > 40000 ? 40000 : (power < -40000 ? -40000 : static_cast<int>(power));
    return std::ldexp(value, exponent);
  }

  // fldl2e, fldln2, fldlg2 under round to nearest: the x87's 64-bit constants.
  const Reg kFldl2e = std::ldexp(static_cast<Reg>(0xB8AA3B295C17F0BCull), -63);
  const Reg kFldln2 = std::ldexp(static_cast<Reg>(0xB17217F7D1CF79ACull), -64);
  const Reg kFldlg2 = std::ldexp(static_cast<Reg>(0x9A209A84FBCFF799ull), -65);

  // --- Stores ----------------------------------------------------------------------------------

  /// fstp dword: one rounding to float, with float's range (denormals, overflow to infinity).
  [[nodiscard]] float StoreFloat(const Reg value) noexcept
  {
    return static_cast<float>(value);
  }

  /// fstp qword.
  [[nodiscard]] double StoreDouble(const Reg value) noexcept
  {
    return static_cast<double>(value);
  }

  /// fistp dword of a float under the thread's rounding field: the integer indefinite
  /// 0x80000000 for NaN and for a result outside int32. A float is exact in double.
  [[nodiscard]] std::int32_t StoreInt32(const double value) noexcept
  {
    if (std::isnan(value)) {
      return INT32_MIN;
    }
    double rounded = 0;
    switch ((tControlWord >> 10) & 3u) {
    case 0:
      rounded = std::nearbyint(value);
      break;
    case 1:
      rounded = std::floor(value);
      break;
    case 2:
      rounded = std::ceil(value);
      break;
    default:
      rounded = std::trunc(value);
      break;
    }
    if (!(rounded >= -2147483648.0 && rounded <= 2147483647.0)) {
      return INT32_MIN;
    }
    return static_cast<std::int32_t>(rounded);
  }

  // --- The routines that only compute, in double -----------------------------------------------
  //
  // The routines without a transcendental instruction start from floats, and every step under
  // PC24 leaves at most 24 significant bits. In double, a product of two such values is exact,
  // and a sum, quotient or square root rounded to double and then to 24 bits is the one rounding
  // to 24 bits the x87 makes (rounding twice is harmless when the wider format has at least
  // 2p + 2 bits: 53 >= 50). Nothing they form from floats comes near double's exponent limits
  // (a product of three floats is at least 2^-447), so double also holds the extended exponent
  // range the x87 keeps until the store. This path takes finite inputs and results only: a NaN
  // or an infinity anywhere goes to the binary128 path, which follows the x87's NaN rules.

  /// `value` (finite, zero or normal) rounded to 24 significant bits, to nearest even.
  [[nodiscard]] inline double Round24(const double value) noexcept
  {
    std::uint64_t raw = std::bit_cast<std::uint64_t>(value);
    if ((raw & 0x7FF0000000000000ull) == 0) {
      return value; // zero: no value formed here is a double denormal
    }
    constexpr std::uint64_t kUnit = std::uint64_t{1} << 29;
    const std::uint64_t low = raw & (kUnit - 1);
    raw -= low;
    if (low > (kUnit >> 1) || (low == (kUnit >> 1) && (raw & kUnit) != 0)) {
      raw += kUnit; // a carry out of the significand moves to the next binade, exactly
    }
    return std::bit_cast<double>(raw);
  }

  [[nodiscard]] inline double Mul24(const double a, const double b) noexcept
  {
    return Round24(a * b);
  }

  /**
   * a * b rounded to 24 bits, a of 24 bits and b of more (a 31-bit integer): the exact product may
   * need more than double's 53 bits. Rounding to double cannot pass a 24-bit halfway point, only
   * land on one, so the double product decides alone but there, where the sign of the exact rest
   * (fma) does.
   */
  [[nodiscard]] inline double MulWide24(const double a, const double b) noexcept
  {
    const double product = a * b;
    std::uint64_t raw = std::bit_cast<std::uint64_t>(product);
    if ((raw & 0x7FF0000000000000ull) == 0) {
      return product; // zero
    }
    constexpr std::uint64_t kUnit = std::uint64_t{1} << 29;
    const std::uint64_t low = raw & (kUnit - 1);
    raw -= low;
    bool up = low > (kUnit >> 1);
    if (low == (kUnit >> 1)) {
      const double rest = std::fma(a, b, -product);
      up = rest != 0.0 ? ((rest > 0.0) == (product > 0.0)) : ((raw & kUnit) != 0);
    }
    if (up) {
      raw += kUnit;
    }
    return std::bit_cast<double>(raw);
  }

  [[nodiscard]] inline double Add24(const double a, const double b) noexcept
  {
    return Round24(a + b);
  }

  [[nodiscard]] inline double Sub24(const double a, const double b) noexcept
  {
    return Round24(a - b);
  }

  [[nodiscard]] inline double Div24(const double a, const double b) noexcept
  {
    return Round24(a / b);
  }

  [[nodiscard]] inline double Sqrt24(const double a) noexcept
  {
    return Round24(std::sqrt(a));
  }

  template <class... Values>
  [[nodiscard]] inline bool AllFinite(const Values... values) noexcept
  {
    return (std::isfinite(values) && ...);
  }
} // namespace

extern "C" unsigned int FAF_X87_SOFT_NAME(msvc8_x87_control_word)()
{
  return tControlWord;
}

/// The thread's control word for the routines here (FpEnvPosix.cpp, Win32Shim's _controlfp).
extern "C" void FAF_X87_SOFT_NAME(msvc8_x87_set_control_word)(const unsigned int controlWord)
{
  tControlWord = controlWord & 0xFFFFu;
}

// fld dword; fistp dword.
extern "C" std::int32_t FAF_X87_SOFT_NAME(msvc8_x87_round_to_int)(const float value)
{
  return StoreInt32(value);
}

// a = h00 + (h01 - h00) * fracZ, b = h10 + (h11 - h10) * fracZ, then a + (b - a) * fracX.
extern "C" float FAF_X87_SOFT_NAME(msvc8_x87_bilinear_height)(
  const float h00, const float h01, const float h10, const float h11, const float fracX, const float fracZ
)
{
  if (AllFinite(h00, h01, h10, h11, fracX, fracZ)) {
    const double a = Add24(h00, Mul24(Sub24(h01, h00), fracZ));
    const double b = Add24(Mul24(fracZ, Sub24(h11, h10)), h10);
    return static_cast<float>(Add24(a, Mul24(Sub24(b, a), fracX)));
  }
  const Reg z = Load(fracZ);
  const Reg a = Add(Load(h00), Mul(Sub(Load(h01), Load(h00)), z));
  const Reg b = Add(Mul(z, Sub(Load(h11), Load(h10))), Load(h10));
  return StoreFloat(Add(a, Mul(Sub(b, a), Memory(fracX))));
}

// The same block as msvc8_x87_bilinear_height, then top - height (fsubr m32) before the store.
extern "C" float FAF_X87_SOFT_NAME(msvc8_x87_subtract_bilinear_height)(
  const float top, const float h00, const float h01, const float h10, const float h11, const float fracX, const float fracZ
)
{
  if (AllFinite(top, h00, h01, h10, h11, fracX, fracZ)) {
    const double a = Add24(h00, Mul24(Sub24(h01, h00), fracZ));
    const double b = Add24(Mul24(fracZ, Sub24(h11, h10)), h10);
    return static_cast<float>(Sub24(top, Add24(a, Mul24(Sub24(b, a), fracX))));
  }
  const Reg z = Load(fracZ);
  const Reg a = Add(Load(h00), Mul(Sub(Load(h01), Load(h00)), z));
  const Reg b = Add(Mul(z, Sub(Load(h11), Load(h10))), Load(h10));
  return StoreFloat(Sub(Memory(top), Add(a, Mul(Sub(b, a), Memory(fracX)))));
}

extern "C" float FAF_X87_SOFT_NAME(msvc8_x87_sinf)(const float x)
{
  return StoreFloat(Fsin(Load(x)));
}

extern "C" float FAF_X87_SOFT_NAME(msvc8_x87_cosf)(const float x)
{
  return StoreFloat(Fcos(Load(x)));
}

// fptan, then the 1.0 it pushed is dropped.
extern "C" float FAF_X87_SOFT_NAME(msvc8_x87_tanf)(const float x)
{
  return StoreFloat(Fptan(Load(x)));
}

extern "C" float FAF_X87_SOFT_NAME(msvc8_x87_atan2f)(const float y, const float x)
{
  return StoreFloat(Fpatan(Load(y), Load(x)));
}

// atan2(sqrt((1 + x) * (1 - x)), x) at PC24.
extern "C" double FAF_X87_SOFT_NAME(msvc8_x87_acos)(const double x)
{
  const Reg operand = Load(x);
  const Reg root = Sqrt(Mul(Add(1.0L, operand), Sub(1.0L, operand)));
  return StoreDouble(Fpatan(root, operand));
}

// atan2(x, sqrt((1 + x) * (1 - x))) at PC24.
extern "C" double FAF_X87_SOFT_NAME(msvc8_x87_asin)(const double x)
{
  const Reg operand = Load(x);
  const Reg root = Sqrt(Mul(Add(1.0L, operand), Sub(1.0L, operand)));
  return StoreDouble(Fpatan(operand, root));
}

// z = y * log2(x); n = frndint(z); (1 + (2^(z - n) - 1)) * 2^n, the subtraction (n - z, then
// fchs) and the addition at PC24.
extern "C" double FAF_X87_SOFT_NAME(msvc8_x87_pow)(const double x, const double y)
{
  const Reg z = Fyl2x(Load(x), Load(y));
  const Reg n = Frndint(z);
  const Reg fraction = -Sub(n, z);
  const Reg power = Add(F2xm1(fraction), 1.0L);
  return StoreDouble(Fscale(power, n));
}

// atan2(firstY, firstX) stored to float, minus atan2(secondY, secondX) (fsubr m32).
extern "C" float FAF_X87_SOFT_NAME(msvc8_x87_atan2_difference)(
  const float firstY, const float firstX, const float secondY, const float secondX
)
{
  const float first = StoreFloat(Fpatan(Load(firstY), Load(firstX)));
  const Reg second = Fpatan(Load(secondY), Load(secondX));
  return StoreFloat(Sub(Memory(first), second));
}

extern "C" float FAF_X87_SOFT_NAME(msvc8_x87_atan2_plus)(const float y, const float x, const float addend)
{
  return StoreFloat(Add(Fpatan(Load(y), Load(x)), Memory(addend)));
}

extern "C" float FAF_X87_SOFT_NAME(msvc8_x87_sin_times_cos)(const float sinArg, const float cosArg)
{
  const Reg cosine = Fcos(Load(cosArg));
  const Reg sine = Fsin(Load(sinArg));
  return StoreFloat(Mul(cosine, sine));
}

extern "C" float FAF_X87_SOFT_NAME(msvc8_x87_sin_times)(const float sinArg, const float factor)
{
  return StoreFloat(Mul(Fsin(Load(sinArg)), Memory(factor)));
}

// fld a; fmul b; fsin; fmul factor.
extern "C" float FAF_X87_SOFT_NAME(msvc8_x87_sin_of_product_times)(const float a, const float b, const float factor)
{
  return StoreFloat(Mul(Fsin(Mul(Load(a), Memory(b))), Memory(factor)));
}

// fild the word as signed and fadd 2^32 when it loaded negative (the only rounding of the word),
// then lower + (upper - lower) * word * 2^-32 (the range by fsub st(1), the sum by faddp).
extern "C" float FAF_X87_SOFT_NAME(msvc8_x87_twister_word_in_range)(
  const float lower, const float upper, const std::uint32_t word
)
{
  constexpr float kTwoTo32 = 4294967296.0f;
  constexpr float kInvTwoTo32 = 2.3283064365386962890625e-10f;
  const auto signedWord = static_cast<std::int32_t>(word);
  if (AllFinite(lower, upper)) {
    // A word that loads non-negative keeps up to 31 bits: its product needs MulWide24.
    const double wordValue = (signedWord < 0) ? Round24(static_cast<double>(word)) : static_cast<double>(signedWord);
    const double scaled = MulWide24(Sub24(upper, lower), wordValue) * static_cast<double>(kInvTwoTo32);
    return static_cast<float>(Add24(lower, scaled));
  }
  Reg loaded = static_cast<Reg>(signedWord);
  if (signedWord < 0) {
    loaded = Add(loaded, Memory(kTwoTo32));
  }
  const Reg range = Sub(Load(upper), Load(lower));
  return StoreFloat(Add(Load(lower), Mul(Mul(range, loaded), Memory(kInvTwoTo32))));
}

// inv = 1 / sin(angle); w1 = sin(angle * oneMinusT) * inv; w2 = sin(angle * t) * inv; then
// out[i] = q1[i] * w1 + rhs[i] * w2, lane by lane (each lane's inputs read before it is stored;
// rhs[3] is fmul's memory operand, the other lanes are loaded).
extern "C" void FAF_X87_SOFT_NAME(msvc8_x87_slerp_spherical_blend)(
  float* const outLanes, const float* const q1Lanes, const float* const rhsLanes, const float angle,
  const float oneMinusT, const float t
)
{
  const Reg angleValue = Load(angle);
  const Reg inverse = Div(1.0L, Fsin(angleValue));
  const Reg firstWeight = Mul(Fsin(Mul(angleValue, Memory(oneMinusT))), inverse);
  const Reg secondWeight = Mul(inverse, Fsin(Mul(angleValue, Load(t))));
  for (int lane = 0; lane < 4; ++lane) {
    const Reg first = Mul(Load(q1Lanes[lane]), firstWeight);
    const Reg second = Mul(lane == 3 ? Memory(rhsLanes[lane]) : Load(rhsLanes[lane]), secondWeight);
    outLanes[lane] = StoreFloat(Add(first, second));
  }
}

// MSVC8's /Oi exp: z = x * log2(e); n = frndint(z); (1 + (2^(z - n) - 1)) * 2^n.
extern "C" float FAF_X87_SOFT_NAME(msvc8_x87_intrinsic_exp)(const float x)
{
  const Reg z = Mul(Load(x), kFldl2e);
  const Reg n = Frndint(z);
  const Reg power = Add(F2xm1(Sub(z, n)), 1.0L);
  return StoreFloat(Fscale(power, n));
}

// ln(2) * log2(x).
extern "C" float FAF_X87_SOFT_NAME(msvc8_x87_intrinsic_ln)(const float x)
{
  return StoreFloat(Fyl2x(Load(x), kFldln2));
}

// ln(x) / ln(base), each as ln(2) * log2(..).
extern "C" float FAF_X87_SOFT_NAME(msvc8_x87_intrinsic_ln_quotient)(const float x, const double base)
{
  return StoreFloat(Div(Fyl2x(Load(x), kFldln2), Fyl2x(Load(base), kFldln2)));
}

// ln(numerator / denominator) * scale.
extern "C" float FAF_X87_SOFT_NAME(msvc8_x87_scaled_ln_quotient)(
  const float numerator, const float denominator, const float scale
)
{
  const Reg quotient = Div(Load(numerator), Memory(denominator));
  return StoreFloat(Mul(Fyl2x(quotient, kFldln2), Memory(scale)));
}

// log10(2) * log2(x).
extern "C" float FAF_X87_SOFT_NAME(msvc8_x87_intrinsic_log10)(const float x)
{
  return StoreFloat(Fyl2x(Load(x), kFldlg2));
}

extern "C" float FAF_X87_SOFT_NAME(msvc8_x87_cos_times_cos)(const float firstArg, const float secondArg)
{
  const Reg second = Fcos(Load(secondArg));
  const Reg first = Fcos(Load(firstArg));
  return StoreFloat(Mul(second, first));
}

// scale = maxLength / sqrt(lengthSq); each component times scale, stored once.
extern "C" void FAF_X87_SOFT_NAME(msvc8_x87_scale_vector_max_length)(
  float* const x, float* const y, float* const z, const float maxLength, const float lengthSq
)
{
  if (AllFinite(maxLength, lengthSq, *x, *y, *z)) {
    const double factor = Div24(maxLength, Sqrt24(lengthSq));
    if (std::isfinite(factor)) {
      *x = static_cast<float>(Mul24(*x, factor));
      *y = static_cast<float>(Mul24(*y, factor));
      *z = static_cast<float>(Mul24(factor, *z));
      return;
    }
  }
  const Reg scale = Div(Memory(maxLength), Sqrt(Load(lengthSq)));
  *x = StoreFloat(Mul(Load(*x), scale));
  *y = StoreFloat(Mul(Load(*y), scale));
  *z = StoreFloat(Mul(scale, Memory(*z)));
}

// scale = length / sqrt(lengthSq); dest[i] = source[i] * scale.
extern "C" void FAF_X87_SOFT_NAME(msvc8_x87_scale_vector_to_length)(
  float* const destLanes, const float* const sourceLanes, const float lengthSq, const float length
)
{
  if (AllFinite(length, lengthSq, sourceLanes[0], sourceLanes[1], sourceLanes[2])) {
    const double factor = Div24(length, Sqrt24(lengthSq));
    if (std::isfinite(factor)) {
      destLanes[0] = static_cast<float>(Mul24(sourceLanes[0], factor));
      destLanes[1] = static_cast<float>(Mul24(factor, sourceLanes[1]));
      destLanes[2] = static_cast<float>(Mul24(factor, sourceLanes[2]));
      return;
    }
  }
  const Reg scale = Div(Memory(length), Sqrt(Load(lengthSq)));
  destLanes[0] = StoreFloat(Mul(Load(sourceLanes[0]), scale));
  destLanes[1] = StoreFloat(Mul(scale, Memory(sourceLanes[1])));
  destLanes[2] = StoreFloat(Mul(scale, Memory(sourceLanes[2])));
}

extern "C" void FAF_X87_SOFT_NAME(msvc8_x87_scale_vector)(
  float* const destLanes, const float* const sourceLanes, const float scale
)
{
  if (AllFinite(scale, sourceLanes[0], sourceLanes[1], sourceLanes[2])) {
    destLanes[0] = static_cast<float>(Mul24(sourceLanes[0], scale));
    destLanes[1] = static_cast<float>(Mul24(sourceLanes[1], scale));
    destLanes[2] = static_cast<float>(Mul24(scale, sourceLanes[2]));
    return;
  }
  const Reg factor = Load(scale);
  destLanes[0] = StoreFloat(Mul(Load(sourceLanes[0]), factor));
  destLanes[1] = StoreFloat(Mul(Load(sourceLanes[1]), factor));
  destLanes[2] = StoreFloat(Mul(factor, Memory(sourceLanes[2])));
}

// scale = sqrt(((tx*tx + ty*ty) + tz*tz) / sourceLengthSq); dest[i] = source[i] * scale.
extern "C" void FAF_X87_SOFT_NAME(msvc8_x87_scale_vector_to_length_of)(
  float* const destLanes, const float* const sourceLanes, const float* const targetLanes, const float sourceLengthSq
)
{
  if (AllFinite(targetLanes[0], targetLanes[1], targetLanes[2], sourceLengthSq, sourceLanes[0], sourceLanes[1], sourceLanes[2])) {
    const double x = targetLanes[0];
    const double y = targetLanes[1];
    const double z = targetLanes[2];
    const double factor = Sqrt24(Div24(Add24(Add24(Mul24(x, x), Mul24(y, y)), Mul24(z, z)), sourceLengthSq));
    if (std::isfinite(factor)) {
      destLanes[0] = static_cast<float>(Mul24(sourceLanes[0], factor));
      destLanes[1] = static_cast<float>(Mul24(factor, sourceLanes[1]));
      destLanes[2] = static_cast<float>(Mul24(factor, sourceLanes[2]));
      return;
    }
  }
  const Reg tx = Load(targetLanes[0]);
  const Reg ty = Load(targetLanes[1]);
  const Reg tz = Load(targetLanes[2]);
  const Reg lengthSq = Add(Add(Mul(tx, tx), Mul(ty, ty)), Mul(tz, tz));
  const Reg scale = Sqrt(Div(lengthSq, Memory(sourceLengthSq)));
  destLanes[0] = StoreFloat(Mul(Load(sourceLanes[0]), scale));
  destLanes[1] = StoreFloat(Mul(scale, Memory(sourceLanes[1])));
  destLanes[2] = StoreFloat(Mul(scale, Memory(sourceLanes[2])));
}

// box: center (0-2), axis0 (3-5), axis1 (6-8), axis2 (9-11), extent (12-14). Each dot is
// (y*ay + z*az) + x*ax; the radius (|dot2|*e2 + |dot1|*e1) + |dot0|*e0. The extents and
// axis0.x are fmul's memory operands, everything else is loaded.
extern "C" float FAF_X87_SOFT_NAME(msvc8_x87_box_radius_on_axis)(const float* const boxLanes, const float* const axisLanes)
{
  bool finite = AllFinite(axisLanes[0], axisLanes[1], axisLanes[2]);
  for (int lane = 3; lane < 15 && finite; ++lane) {
    finite = std::isfinite(boxLanes[lane]);
  }
  if (finite) {
    const double ax = axisLanes[0];
    const double ay = axisLanes[1];
    const double az = axisLanes[2];
    const auto dot = [&](const float* const axis) {
      return Add24(Add24(Mul24(axis[1], ay), Mul24(axis[2], az)), Mul24(axis[0], ax));
    };
    const double radius2 = Mul24(std::fabs(dot(boxLanes + 9)), boxLanes[14]);
    const double radius1 = Mul24(std::fabs(dot(boxLanes + 6)), boxLanes[13]);
    const double radius0 = Mul24(std::fabs(dot(boxLanes + 3)), boxLanes[12]);
    return static_cast<float>(Add24(Add24(radius2, radius1), radius0));
  }
  const Reg ax = Load(axisLanes[0]);
  const Reg ay = Load(axisLanes[1]);
  const Reg az = Load(axisLanes[2]);
  const auto dot = [&](const float* const axis, const Reg axisX) {
    return Add(Add(Mul(Load(axis[1]), ay), Mul(Load(axis[2]), az)), Mul(axisX, ax));
  };
  const Reg radius2 = Mul(std::fabs(dot(boxLanes + 9, Load(boxLanes[9]))), Memory(boxLanes[14]));
  const Reg radius1 = Mul(std::fabs(dot(boxLanes + 6, Load(boxLanes[6]))), Memory(boxLanes[13]));
  const Reg radius0 = Mul(std::fabs(dot(boxLanes + 3, Memory(boxLanes[3]))), Memory(boxLanes[12]));
  return StoreFloat(Add(Add(radius2, radius1), radius0));
}

// sqrt((z*z + y*y) + x*x).
extern "C" float FAF_X87_SOFT_NAME(msvc8_x87_vector_length_zyx)(const float* const lanes)
{
  if (AllFinite(lanes[0], lanes[1], lanes[2])) {
    const double x = lanes[0];
    const double y = lanes[1];
    const double z = lanes[2];
    return static_cast<float>(Sqrt24(Add24(Add24(Mul24(z, z), Mul24(y, y)), Mul24(x, x))));
  }
  const Reg x = Load(lanes[0]);
  const Reg y = Load(lanes[1]);
  const Reg z = Load(lanes[2]);
  return StoreFloat(Sqrt(Add(Add(Mul(z, z), Mul(y, y)), Mul(x, x))));
}
