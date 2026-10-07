/*
 * LowArena: keeps the headless runner's memory below 2 GB on Android (docs/port/android-roadmap.md,
 * W1.3, milestone M3c). It is the Android counterpart of the x64 build's /LARGEADDRESSAWARE:NO and
 * exists only for the adb-shell runner (faf_headless_runner); the APK never uses it.
 *
 * The runner executable defines every function below (LowArena.cpp) and exports it, together with
 * the malloc family it replaces. Code in libfafengine.so (the shim's VirtualAlloc/MapViewOfFile, the
 * pthread_create wrapper in LowArenaThreads.cpp) only declares them weak, so the library still links
 * with -Wl,--no-undefined and runs unchanged without them: a null function means "no arena".
 *
 * Why 2 GB and not 4 GB: the engine keeps pointers in int-typed words (the reflection callbacks, the
 * hidden 32-bit pointer fields of docs/port/android-roadmap.md W1.3), and (char*)(int)0x90001000 is
 * 0xffffffff90001000 on both ABIs. Below 2 GB the round trip through int is exact.
 *
 * FAF_LOWARENA=0 in the environment turns the arena off: malloc and friends forward to bionic's
 * allocator (Scudo), threads keep bionic's stacks, the engine library is loaded with plain dlopen.
 * That run is the truncation oracle for M3d.
 */
#pragma once

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#if defined(LOWARENA_IMPLEMENTATION)
#define LOWARENA_API __attribute__((visibility("default")))
#else
#define LOWARENA_API __attribute__((weak))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* The arena's address range: [LOWARENA_LOW, LOWARENA_HIGH). */
#define LOWARENA_LOW ((uintptr_t)0x01000000u)  /* 16 MB */
#define LOWARENA_HIGH ((uintptr_t)0x80000000u) /* 2 GB */

/* What a range of the arena holds (lowarena_reserve's kind, and the report). */
enum {
  LOWARENA_KIND_HEAP = 2,    /* dlmalloc's memory (the malloc family) */
  LOWARENA_KIND_STACK = 3,   /* a thread stack with its guard */
  LOWARENA_KIND_VIRTUAL = 4, /* a VirtualAlloc reservation (port/engine/shim/faf_win_memory.h) */
  LOWARENA_KIND_VIEW = 5,    /* a MapViewOfFile view (port/engine/shim/faf_win_file.h) */
  LOWARENA_KIND_IMAGE = 6    /* the reserved range libfafengine.so is loaded into */
};

/* 1 when the arena serves this process (FAF_LOWARENA is not "0"), else 0. */
LOWARENA_API int lowarena_enabled(void);

/* True when `address` lies in memory the arena handed out (any kind). */
LOWARENA_API int lowarena_owns(const void* address);

/*
 * Address space below 2 GB, aligned to the 64 KB allocation granularity, `length` rounded up to it.
 * The range is mapped anonymous and private with `protection` (PROT_NONE leaves it reserved). Null
 * when the arena is off or full (the latter is reported on stderr and counted).
 */
LOWARENA_API void* lowarena_reserve(size_t length, int protection, int kind);

/*
 * Gives back a range from lowarena_reserve or lowarena_map_file: its pages are dropped and the
 * address space returns to the arena (it stays reserved, so nothing else lands there). Returns 1
 * when `base` starts such a range, 0 when the arena does not own it (the caller then munmaps it).
 * `length` is the caller's length; the arena knows the real one and reports a mismatch.
 */
LOWARENA_API int lowarena_release(void* base, size_t length);

/* mmap(fd, offset) of `length` bytes into a fresh arena range (MapViewOfFile); null as above. */
LOWARENA_API void* lowarena_map_file(size_t length, int protection, int flags, int fd, off_t offset);

/*
 * Thread stacks. The engine library is linked with -Wl,--wrap=pthread_create/join/detach
 * (LowArenaThreads.cpp); its wrappers call these. A thread created without its own stack gets a
 * 4 MB arena stack (or the attribute's size if larger; main.exe's StackReserveSize is 4000000)
 * above a PROT_NONE guard. The stack goes back to the arena after a successful pthread_join, or,
 * for a detached thread, once the kernel no longer knows the thread.
 *
 * Join and detach are bracketed: lowarena_thread_join_begin / lowarena_thread_detach_begin before
 * pthread_join / pthread_detach pick the thread's record (so a new thread that reuses the pthread_t
 * value meanwhile cannot be mistaken for it), and the _end call with the pthread function's result
 * frees the stack after a join, marks it for release once the thread has gone after a detach, or
 * restores the record when the call failed. Bionic keeps the thread's pthread_internal_t (and its
 * TLS) at the top of a caller-supplied stack and reads it in pthread_join/pthread_detach, so the
 * stack must stay mapped until those calls return.
 */
typedef int (*lowarena_pthread_create_fn)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);
LOWARENA_API int lowarena_thread_create(lowarena_pthread_create_fn create, pthread_t* thread,
                                        const pthread_attr_t* attr, void* (*start)(void*), void* arg);
LOWARENA_API uintptr_t lowarena_thread_join_begin(pthread_t thread);
LOWARENA_API void lowarena_thread_join_end(uintptr_t token, int join_result);
LOWARENA_API uintptr_t lowarena_thread_detach_begin(pthread_t thread);
LOWARENA_API void lowarena_thread_detach_end(uintptr_t token, int detach_result);

/* What the arena did so far (the report at exit prints the same). */
struct lowarena_stats {
  int enabled;
  uint32_t segments;            /* reserved segments (after merging adjacent ones) */
  uint64_t reserved_bytes;      /* address space reserved from the kernel */
  uint64_t in_use_bytes;        /* handed out now (all kinds) */
  uint64_t in_use_high_water;   /* most ever handed out at once */
  uint64_t top_address;         /* highest address ever handed out (exclusive) */
  uint64_t limit_address;       /* the arena stays below this: 2 GB, or 1.75 GB under the ARM translator */
  uint64_t heap_footprint;      /* dlmalloc: memory obtained for the heap now */
  uint64_t heap_max_footprint;  /* dlmalloc: most ever */
  uint32_t threads_live, threads_peak, threads_total, thread_fallbacks;
  uint32_t views_live, views_peak, view_fallbacks;
  uint32_t virtual_live, virtual_peak, virtual_fallbacks;
  uint64_t image_base, image_size;
  uint64_t foreign_frees, foreign_reallocs; /* pointers the arena did not hand out, forwarded to bionic */
};
LOWARENA_API void lowarena_get_stats(struct lowarena_stats* out);

/* One "[lowarena] ..." line with the stats (to `fd`, normally 2). The executable prints it at exit. */
LOWARENA_API void lowarena_report(int fd);

#ifdef __cplusplus
}
#endif
