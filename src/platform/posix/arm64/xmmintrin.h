#pragma once

// Engine addition (Android port): the SSE intrinsics the shared code names, for arm64 builds
// (cmake/FafLinux.cmake puts this directory first on the include path there). Each one does on
// AArch64 what it does on x86-64; anything else the code starts to use fails to compile here
// and is added with its meaning spelled out, not stubbed.
//
// MXCSR maps onto FPCR (controls) and FPSR (sticky flags):
//
//   MXCSR                          FPCR / FPSR
//   RC (13-14): near/down/up/chop  FPCR.RMode (22-23): RN/RM/RP/RZ
//   FZ (15), DAZ (6)               FPCR.FZ (24), which flushes inputs and results alike
//   IM ZM OM UM PM DM (7-12)       FPCR.IOE DZE OFE UFE IXE IDE (8-12, 15), inverted: a set
//                                  enable bit traps, a set mask bit does not
//   IE ZE OE UE PE DE (0-5)        FPSR.IOC DZC OFC UFC IXC IDC (0-4, 7)
//
// The simulation's threads run with FPCR 0 (platform/posix/FpEnvPosix.cpp), which reads back as
// the x64 threads' MXCSR 0x1F80.

#if !defined(__aarch64__)
#error "platform/posix/arm64 is for arm64 builds only"
#endif

#include <stdint.h>

#define _MM_HINT_NTA 0
#define _MM_HINT_T2 1
#define _MM_HINT_T1 2
#define _MM_HINT_T0 3

namespace platform::posix::arm64
{
  inline uint64_t ReadFpcr() noexcept
  {
    uint64_t value;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(value));
    return value;
  }

  inline void WriteFpcr(const uint64_t value) noexcept
  {
    __asm__ __volatile__("msr fpcr, %0" : : "r"(value));
  }

  inline uint64_t ReadFpsr() noexcept
  {
    uint64_t value;
    __asm__ __volatile__("mrs %0, fpsr" : "=r"(value));
    return value;
  }

  inline void WriteFpsr(const uint64_t value) noexcept
  {
    __asm__ __volatile__("msr fpsr, %0" : : "r"(value));
  }

  // MXCSR bit, FPCR enable / FPSR flag bit, for the six exceptions.
  struct ExceptionBits
  {
    unsigned mxcsrFlag;
    unsigned mxcsrMask;
    unsigned armBit;
  };
  inline constexpr ExceptionBits kInvalid{0, 7, 0};
  inline constexpr ExceptionBits kDenormal{1, 8, 7};
  inline constexpr ExceptionBits kDivideByZero{2, 9, 1};
  inline constexpr ExceptionBits kOverflow{3, 10, 2};
  inline constexpr ExceptionBits kUnderflow{4, 11, 3};
  inline constexpr ExceptionBits kInexact{5, 12, 4};
  inline constexpr ExceptionBits kExceptions[] = {kInvalid, kDenormal, kDivideByZero, kOverflow, kUnderflow, kInexact};

  // FPCR's trap-enable bits sit 8 above the FPSR flag bits.
  inline constexpr unsigned kTrapEnableShift = 8;
  inline constexpr unsigned kFpcrRModeShift = 22;
  inline constexpr uint64_t kFpcrFlushToZero = uint64_t{1} << 24;

  // MXCSR RC (near, down, up, chop) <-> FPCR.RMode (RN, RP, RM, RZ).
  inline constexpr unsigned kRModeFromRc[4] = {0u, 2u, 1u, 3u};
  inline constexpr unsigned kRcFromRMode[4] = {0u, 2u, 1u, 3u};

  /// The MXCSR an x86 thread would have with these FPCR and FPSR.
  inline unsigned int MxcsrFromFpcr(const uint64_t fpcr, const uint64_t fpsr) noexcept
  {
    unsigned int csr = 0;
    for (const ExceptionBits bits : kExceptions) {
      if ((fpcr & (uint64_t{1} << (bits.armBit + kTrapEnableShift))) == 0) {
        csr |= 1u << bits.mxcsrMask;
      }
      if ((fpsr & (uint64_t{1} << bits.armBit)) != 0) {
        csr |= 1u << bits.mxcsrFlag;
      }
    }
    csr |= kRcFromRMode[(fpcr >> kFpcrRModeShift) & 3u] << 13;
    if ((fpcr & kFpcrFlushToZero) != 0) {
      csr |= (1u << 15) | (1u << 6);
    }
    return csr;
  }
} // namespace platform::posix::arm64

static inline unsigned int _mm_getcsr(void) noexcept
{
  using namespace platform::posix::arm64;
  return MxcsrFromFpcr(ReadFpcr(), ReadFpsr());
}

static inline void _mm_setcsr(const unsigned int csr) noexcept
{
  using namespace platform::posix::arm64;
  uint64_t fpcr = ReadFpcr();
  uint64_t fpsr = ReadFpsr();
  for (const ExceptionBits bits : kExceptions) {
    const uint64_t enable = uint64_t{1} << (bits.armBit + kTrapEnableShift);
    fpcr = (csr & (1u << bits.mxcsrMask)) != 0 ? (fpcr & ~enable) : (fpcr | enable);
    const uint64_t flag = uint64_t{1} << bits.armBit;
    fpsr = (csr & (1u << bits.mxcsrFlag)) != 0 ? (fpsr | flag) : (fpsr & ~flag);
  }
  fpcr = (fpcr & ~(uint64_t{3} << kFpcrRModeShift)) | (uint64_t{kRModeFromRc[(csr >> 13) & 3u]} << kFpcrRModeShift);
  fpcr = (csr & ((1u << 15) | (1u << 6))) != 0 ? (fpcr | kFpcrFlushToZero) : (fpcr & ~kFpcrFlushToZero);
  WriteFpcr(fpcr);
  WriteFpsr(fpsr);
}

static inline void _mm_prefetch(const char* const address, const int hint) noexcept
{
  // __builtin_prefetch needs a constant locality; the hints are constants at every call.
  switch (hint) {
  case _MM_HINT_NTA:
    __builtin_prefetch(address, 0, 0);
    break;
  case _MM_HINT_T2:
    __builtin_prefetch(address, 0, 1);
    break;
  case _MM_HINT_T1:
    __builtin_prefetch(address, 0, 2);
    break;
  default:
    __builtin_prefetch(address, 0, 3);
    break;
  }
}

/// The spin-wait hint (x86 `pause`): AArch64 `yield`.
static inline void _mm_pause(void) noexcept
{
  __asm__ __volatile__("yield");
}
