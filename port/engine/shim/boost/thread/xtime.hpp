#pragma once

// boost 1.34's <boost/thread/xtime.hpp> declares `enum xtime_clock_types
// { TIME_UTC=1 }` (xtime.hpp:24). C11 <time.h> defines TIME_UTC as a macro
// (bionic: `(CLOCK_REALTIME+1)`, glibc: `1`), which turns the enumerator into
// `1=1`. The repo's copy handles the same clash with MSVC's UCRT, which defines
// the macro too: under `_MSC_VER` it pushes, undefines and at the end pops
// TIME_UTC (xtime.hpp:16-20, :58-61). This applies that patch for clang, so
// TIME_UTC means what it means on Windows on both sides of the header. The
// boost.thread sources that pass boost::TIME_UTC undefine the macro themselves
// (libs/thread/src/timeconv.inl:9, a FAF change for the same reason).

#if defined(_WIN32) || defined(_MSC_VER)
#error "port/engine/shim is for non-Windows targets; it must not be on a Windows include path"
#endif

// <time.h> comes first so the macro is in place before the check: xtime.hpp's
// own includes might otherwise bring it in between the check and the enum.
#include <time.h>

#if defined(TIME_UTC)
#pragma push_macro("TIME_UTC")
#undef TIME_UTC
#include_next <boost/thread/xtime.hpp>
#pragma pop_macro("TIME_UTC")
#else
#include_next <boost/thread/xtime.hpp>
#endif
