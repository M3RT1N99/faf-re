// The test is linked statically, so that it runs on Linux hosts as well (CI's arm64 runner), where
// Android's liblog and libandroid do not exist. These stand in for the three functions of theirs
// the message layer calls; the surface's references are counted, which a test checks.

#include <android/log.h>
#include <android/native_window.h>

#include <cstdarg>
#include <cstdio>
#include <map>

#include "Support.h"

namespace
{
  std::map<ANativeWindow*, int>& References()
  {
    static std::map<ANativeWindow*, int> references;
    return references;
  }
} // namespace

extern "C" void ANativeWindow_acquire(ANativeWindow* const window)
{
  ++References()[window];
}

extern "C" void ANativeWindow_release(ANativeWindow* const window)
{
  --References()[window];
}

extern "C" int __android_log_print(const int priority, const char* const tag, const char* const format, ...)
{
  (void)priority;
  std::va_list arguments;
  va_start(arguments, format);
  std::fprintf(stderr, "[%s] ", tag != nullptr ? tag : "");
  std::vfprintf(stderr, format, arguments);
  std::fputc('\n', stderr);
  va_end(arguments);
  return 1;
}

int wxtest::SurfaceReferences(ANativeWindow* const window)
{
  const auto it = References().find(window);
  return it != References().end() ? it->second : 0;
}
