// Thread stacks below 2 GB for libfafengine.so (LowArena.h, README.md beside this file).
//
// build_runner.py links the engine library with -Wl,--wrap=pthread_create,--wrap=pthread_join,
// --wrap=pthread_detach and this object, so every thread the engine starts - boost::thread,
// std::thread, the shim's CreateThread/_beginthreadex - comes through here, and the stack goes back
// to the arena when the thread is joined or, if detached, has gone. Interposing pthread_create by name
// from the executable would not do: under the emulator's ARM translation the translator intercepts
// that symbol before a guest definition is seen (measured, docs/port/android-roadmap.md W1.3).
//
// The lowarena_* functions are defined by faf_headless_runner and only declared weak here: when the
// library runs in another host, or the executable's arena is off (FAF_LOWARENA=0), the wrappers do
// exactly what pthread_* do.

#include "LowArena.h"

#include <pthread.h>

extern "C" {

int __real_pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start)(void*), void* arg);
int __real_pthread_join(pthread_t thread, void** result);
int __real_pthread_detach(pthread_t thread);

__attribute__((visibility("hidden"))) int __wrap_pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                                                                void* (*start)(void*), void* arg)
{
  if (lowarena_thread_create != nullptr) {
    return lowarena_thread_create(&__real_pthread_create, thread, attr, start, arg);
  }
  return __real_pthread_create(thread, attr, start, arg);
}

__attribute__((visibility("hidden"))) int __wrap_pthread_join(pthread_t thread, void** result)
{
  if (lowarena_thread_join_begin == nullptr || lowarena_thread_join_end == nullptr) {
    return __real_pthread_join(thread, result);
  }
  const uintptr_t token = lowarena_thread_join_begin(thread);
  const int joined = __real_pthread_join(thread, result);
  lowarena_thread_join_end(token, joined);
  return joined;
}

__attribute__((visibility("hidden"))) int __wrap_pthread_detach(pthread_t thread)
{
  if (lowarena_thread_detach_begin == nullptr || lowarena_thread_detach_end == nullptr) {
    return __real_pthread_detach(thread);
  }
  const uintptr_t token = lowarena_thread_detach_begin(thread);
  const int detached = __real_pthread_detach(thread);
  lowarena_thread_detach_end(token, detached);
  return detached;
}

} // extern "C"
