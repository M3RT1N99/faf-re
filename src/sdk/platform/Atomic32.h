#pragma once

#include <cstdint>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace platform
{
  /**
   * Interlocked operations on the engine's 32-bit words (stat values,
   * reference counts).
   *
   * The recovered code reached the Win32 interlocked functions through
   * `reinterpret_cast<volatile long*>(&word)`. `long` is 32 bits only on
   * Windows (LLP64). On LP64 targets such as aarch64 Android it is 64 bits, so
   * the same cast would make an 8-byte atomic over the 4-byte word and the
   * field after it (docs/port/android-roadmap.md, W1.2). These take the word
   * itself.
   *
   * On MSVC they are the intrinsics those call sites used (`lock xadd`,
   * `lock cmpxchg`). Elsewhere they are the GCC/Clang `__atomic` builtins with
   * sequentially consistent ordering, the full barrier the Interlocked
   * functions give. Both wrap on overflow.
   *
   * The engine's call sites keep their original Interlocked calls under
   * `#if defined(_MSC_VER)`, call these only in the `#else` branch, and include
   * this header only when not compiling with MSVC. Routing MSVC through the
   * inline wrapper would be equivalent but not the same code: /Od builds call
   * it instead of expanding the intrinsic in place, at /O2 it shifts MSVC's
   * inlining choices in some x86 TUs, and even an unused inclusion renumbers
   * the compiler's string-literal symbols and reorders .rdata. The MSVC branch
   * below keeps the helper usable from code that Windows compiles too.
   */

  /**
   * `InterlockedExchangeAdd`: adds `addend` to `*target` and returns the value
   * `*target` held before.
   */
  inline std::int32_t AtomicExchangeAdd32(volatile std::int32_t* const target, const std::int32_t addend) noexcept
  {
#if defined(_MSC_VER)
    return static_cast<std::int32_t>(
      ::_InterlockedExchangeAdd(reinterpret_cast<volatile long*>(target), static_cast<long>(addend))
    );
#else
    return __atomic_fetch_add(target, addend, __ATOMIC_SEQ_CST);
#endif
  }

  /**
   * `InterlockedCompareExchange`: stores `exchange` into `*target` if it holds
   * `comparand`, and returns the value `*target` held before either way.
   */
  inline std::int32_t AtomicCompareExchange32(
    volatile std::int32_t* const target, const std::int32_t exchange, const std::int32_t comparand
  ) noexcept
  {
#if defined(_MSC_VER)
    return static_cast<std::int32_t>(::_InterlockedCompareExchange(
      reinterpret_cast<volatile long*>(target), static_cast<long>(exchange), static_cast<long>(comparand)
    ));
#else
    std::int32_t observed = comparand;
    (void)__atomic_compare_exchange_n(target, &observed, exchange, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return observed;
#endif
  }
} // namespace platform
