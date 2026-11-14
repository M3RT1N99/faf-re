#pragma once

// Force-included into DirectXTex's sources (faf_directxtex_android, CMakeLists.txt). DirectXTex takes its
// aligned arrays from aligned_alloc (C11) and frees them with free, and bionic has aligned_alloc only from
// Android 9 (API 28) on, while this build is for Android 8.0 (API 26, cmake/toolchains/android-pins.cmake).
// posix_memalign gives memory that free releases as well. Declared before the C++ library's <cstdlib>,
// which then takes it into std as it would take bionic's.

#include <stdlib.h>

#if __ANDROID_API__ < 28
static inline void* aligned_alloc(const size_t alignment, const size_t size)
{
  void* memory = NULL;
  return posix_memalign(&memory, alignment, size) == 0 ? memory : NULL;
}
#endif
