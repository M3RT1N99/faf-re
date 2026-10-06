#pragma once

/**
 * Two-phase static initialization for the reflection registry.
 *
 * The engine registers reflected types from static initializers. Those split
 * cleanly into two kinds:
 *
 *   providers - `register_<T>TypeInfo` constructs T's descriptor singleton,
 *               whose constructor calls `gpg::PreRegisterRType`. A provider
 *               only inserts into the type map; it depends on nothing.
 *   consumers - `register_<T>Serializer` / `register_<T>Construct` and friends
 *               call `gpg::LookupRType`, which throws unless the type - and
 *               every type its `Init()` touches - was already pre-registered.
 *
 * The 2007 link ordered all 5071 initializers so that every provider preceded
 * the consumers needing it. We cannot reproduce that order by ordering source
 * files, because our decomposition differs from the original's: a single one of
 * our translation units routinely holds initializers the original had spread
 * thousands of slots apart (`CConCommand.cpp` alone covers slots 606..5055), so
 * every file's index span overlaps another's and no file ordering satisfies all
 * the constraints.
 *
 * Ordering by *initializer* rather than by file does satisfy them, and since
 * providers have no dependencies it is enough to run every provider before any
 * consumer. `GPG_PREREGISTER_INIT` does that by emitting the provider's address
 * into `.CRT$XCL`, the section MSVC reserves for `#pragma init_seg(lib)`, which
 * the CRT walks before the default `.CRT$XCU` block that holds ordinary dynamic
 * initializers.
 *
 * The macro is purely additive: provider functions guard their singleton, so
 * the existing bootstrap object calling the same provider later is a no-op.
 *
 * The table entry is a small thunk that calls `FN()`, not `FN` cast to a
 * `void()` pointer: a provider that takes a parameter is then a compile error
 * instead of a call that reads its argument off the caller's stack.
 *
 * Usage - place directly after the provider's definition, at namespace scope:
 *
 *     void register_SFootprintTypeInfo() { ... }
 *     GPG_PREREGISTER_INIT(SFootprintTypeInfo, register_SFootprintTypeInfo)
 *
 * Other compilers (the Android arm64 build, docs/port/android-roadmap.md W1.4)
 * have no `.CRT$X*` walk. On ELF, `__declspec(allocate(".CRT$XCL"))` only
 * names an ordinary data section, so the table entry would be emitted and
 * never called. The ELF counterpart is `.init_array`: the linker sorts the
 * `.init_array.<priority>` sections in ascending order and puts the plain
 * `.init_array`, where ordinary dynamic initializers go (priority 65535, the
 * `.CRT$XCU` block), after all of them. So the thunk becomes a constructor
 * with priority GPG_STATIC_INIT_PRIORITY_CRT_XCL, which runs every provider
 * before any ordinary initializer, in link order among themselves, as the CRT
 * runs `.CRT$XCL` before `.CRT$XCU`. Priorities up to 100 are reserved for the
 * implementation (libc++ initialises the standard streams there), so they
 * still come first.
 */

#if defined(_MSC_VER)

#define GPG_PREREGISTER_INIT(TAG, FN)                                              \
  static void __cdecl gGpgPreRegisterThunk_##TAG()                                 \
  {                                                                                \
    (void)FN();                                                                    \
  }                                                                                \
  __pragma(section(".CRT$XCL", read))                                              \
  extern "C" __declspec(allocate(".CRT$XCL")) void(__cdecl* const                  \
                                                   gGpgPreRegisterInit_##TAG)() =  \
    &gGpgPreRegisterThunk_##TAG;

#else

// `.CRT$XCL` as an `.init_array` priority: above the reserved 0..100, below
// the 65535 of ordinary dynamic initializers (`.CRT$XCU`).
#define GPG_STATIC_INIT_PRIORITY_CRT_XCL 1000

#define GPG_PREREGISTER_INIT(TAG, FN)                                              \
  __attribute__((constructor(GPG_STATIC_INIT_PRIORITY_CRT_XCL))) static void       \
  gGpgPreRegisterThunk_##TAG()                                                     \
  {                                                                                \
    (void)FN();                                                                    \
  }

#endif
