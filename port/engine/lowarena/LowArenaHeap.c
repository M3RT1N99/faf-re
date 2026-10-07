/*
 * The arena's heap: Doug Lea's dlmalloc (dlmalloc.c beside this file, public domain, vendored
 * unchanged; see README.md for its version and origin), configured here and compiled only through
 * this file. LowArena.cpp's malloc family calls the dl* entry points.
 *
 * - All memory comes from lowarena_morecore (LowArena.cpp), an sbrk over 64 KB-aligned ranges of the
 *   arena below 2 GB. It extends the current range in place when the address space above it is free
 *   and starts a new range otherwise, so the heap may consist of several segments
 *   (MORECORE_CONTIGUOUS 0: dlmalloc merges the contiguous ones).
 * - No mmap at all (HAVE_MMAP 0): large blocks come from the same ranges instead of from mmap, which
 *   would put them above 4 GB.
 * - The heap never shrinks (MORECORE_CANNOT_TRIM): freed memory is reused, address space is not
 *   returned. Its high-water mark is dlmalloc's max footprint.
 * - One lock around every call (USE_LOCKS, pthread mutexes): the engine allocates from many threads.
 * - 16-byte alignment, as bionic's malloc and alignof(max_align_t) on both 64-bit ABIs (dlmalloc
 *   2.8.3's own default is 8).
 * - Heap misuse (a bad free, a corrupted chunk) ends the process with a message naming the pointer.
 */

#include <stddef.h>

#define USE_DL_PREFIX 1
#define MALLOC_ALIGNMENT ((size_t)16U)
#define HAVE_MMAP 0
#define HAVE_MREMAP 0
#define HAVE_MORECORE 1
#define MORECORE lowarena_morecore
#define MORECORE_CONTIGUOUS 0
#define MORECORE_CANNOT_TRIM 1
#define DEFAULT_GRANULARITY ((size_t)64U * (size_t)1024U)
#define USE_LOCKS 1
#define FOOTERS 0
#define INSECURE 0
#define NO_MALLINFO 1
#define ABORT_ON_ASSERT_FAILURE 1

void* lowarena_morecore(ptrdiff_t increment);
void lowarena_heap_error(const char* what, const void* pointer);

#define CORRUPTION_ERROR_ACTION(m) lowarena_heap_error("corrupted heap", (const void*)0)
#define USAGE_ERROR_ACTION(m, p) lowarena_heap_error("bad pointer passed to free/realloc", (const void*)(p))

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wnull-pointer-arithmetic"
#pragma clang diagnostic ignored "-Wexpansion-to-defined"
#endif

#include "dlmalloc.c"
