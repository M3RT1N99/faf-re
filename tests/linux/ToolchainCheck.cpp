// Engine addition (dedicated-server plan WPL.2): checks that the Linux toolchain
// builds code the way the engine's simulation needs, and prints the facts later
// work depends on (data model, default floating-point environment). The Android
// toolchains build it too; on arm64 the floating-point environment is FPCR.
//
// Exit code 0 when every check passes; each failed check prints a line.

#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
  int gFailures = 0;

  void Check(const bool ok, const char* const what)
  {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
      ++gFailures;
    }
  }

#if defined(__aarch64__)
  std::uint64_t ReadFpcr()
  {
    std::uint64_t fpcr = 0;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    return fpcr;
  }

  void PrintFloatingPointEnvironment(const char* const thread)
  {
    std::printf("%s fpcr=0x%08llX\n", thread, static_cast<unsigned long long>(ReadFpcr()));
  }
#else
  std::uint16_t ReadX87ControlWord()
  {
    std::uint16_t cw = 0;
    __asm__ __volatile__("fnstcw %0" : "=m"(cw));
    return cw;
  }

  std::uint32_t ReadMxcsr()
  {
    std::uint32_t mxcsr = 0;
    __asm__ __volatile__("stmxcsr %0" : "=m"(mxcsr));
    return mxcsr;
  }

  void PrintFloatingPointEnvironment(const char* const thread)
  {
    std::printf("%s fcw=0x%04X mxcsr=0x%08X\n", thread, ReadX87ControlWord(), ReadMxcsr());
  }
#endif

  // Kept out of line so the compiler sees a*b+c as one expression it could
  // contract; with -ffp-contract=off it must round the product first.
  [[gnu::noinline]] double MulAdd(const double a, const double b, const double c)
  {
    return a * b + c;
  }

  extern "C" [[gnu::noinline]] void ThrowFromExternC()
  {
    throw std::runtime_error("from extern C");
  }
} // namespace

int main()
{
  std::printf("sizeof(long)=%zu sizeof(void*)=%zu sizeof(wchar_t)=%zu FLT_EVAL_METHOD=%d\n", sizeof(long),
              sizeof(void*), sizeof(wchar_t), static_cast<int>(FLT_EVAL_METHOD));

  Check(static_cast<char>(0xFF) < 0, "plain char is signed");

  // 1 + 2^-30 squared minus (1 + 2^-29): exactly 2^-60 with a fused
  // multiply-add, 0 with the product rounded to double first.
  const double x = 1.0 + 0x1p-30;
  const double contracted = MulAdd(x, x, -(1.0 + 0x1p-29));
  Check(contracted == 0.0, "a*b+c is not contracted into an FMA");

  bool caught = false;
  try {
    ThrowFromExternC();
  } catch (const std::runtime_error& e) {
    caught = std::strcmp(e.what(), "from extern C") == 0;
  }
  Check(caught, "a C++ exception propagates out of an extern \"C\" function");

  std::vector<std::string> fromThread;
  std::thread worker([&fromThread] {
    PrintFloatingPointEnvironment("worker thread:");
    fromThread.emplace_back("worker");
  });
  worker.join();
  Check(fromThread.size() == 1, "std::thread runs");

  PrintFloatingPointEnvironment("main thread:  ");
#if defined(__aarch64__)
  // FPCR.RMode (22-23) round to nearest, FZ (24) and DN (25) clear.
  Check((ReadFpcr() & 0x03C00000u) == 0, "round to nearest, no flush-to-zero, no default NaN in the default FPCR");
#else
  Check((ReadMxcsr() & 0x8040u) == 0, "no FTZ or DAZ in the default MXCSR");
#endif

  std::printf("%d check(s) failed\n", gFailures);
  return gFailures == 0 ? 0 : 1;
}
