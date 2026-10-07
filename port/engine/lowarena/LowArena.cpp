// LowArena: the runner executable's allocator and address-space manager below 2 GB (LowArena.h,
// docs/port/android-roadmap.md W1.3, README.md beside this file). Linked only into
// faf_headless_runner; libfafengine.so reaches it through the exported malloc family and the weak
// lowarena_* hooks.
//
// Layout of the code:
//  - the range allocator: address space in [16 MB, 2 GB), reserved from the kernel in 16 MB-aligned
//    segments (first fit, MAP_FIXED_NOREPLACE with an address check) and handed out in 64 KB granules;
//    a byte per granule records what it holds;
//  - lowarena_morecore: dlmalloc's sbrk (LowArenaHeap.c) over heap ranges;
//  - the heap check (FAF_LOWARENA_CHECK), an optional layer of canaries and a quarantine over it;
//  - the malloc family, exported from the executable so it interposes bionic's for every library;
//  - thread stacks, VirtualAlloc reservations and file views (the shim and LowArenaThreads.cpp call in);
//  - the report.
//
// Nothing here may allocate through malloc while it serves malloc: the range allocator works on static
// arrays, and the messages are formatted into stack buffers and written with write(2).

#define LOWARENA_IMPLEMENTATION 1
#include "LowArena.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
#ifndef PR_SET_VMA
#define PR_SET_VMA 0x53564d41
#endif
#ifndef PR_SET_VMA_ANON_NAME
#define PR_SET_VMA_ANON_NAME 0
#endif

// dlmalloc 2.8.x with USE_DL_PREFIX (LowArenaHeap.c).
extern "C" {
void* dlmalloc(size_t);
void dlfree(void*);
void* dlcalloc(size_t, size_t);
void* dlrealloc(void*, size_t);
void* dlmemalign(size_t, size_t);
size_t dlmalloc_usable_size(void*);
size_t dlmalloc_footprint(void);
size_t dlmalloc_max_footprint(void);
}

extern "C" char** environ;

namespace
{
  // ------------------------------------------------------------------------------------------------
  // Constants
  // ------------------------------------------------------------------------------------------------

  constexpr uintptr_t kLow = LOWARENA_LOW;
  constexpr uintptr_t kHigh = LOWARENA_HIGH;
  // Windows' allocation granularity, and a multiple of every Android page size (4 KB, 16 KB).
  constexpr uintptr_t kGranule = 0x10000u;
  constexpr size_t kGranuleCount = kHigh / kGranule;  // 32768
  constexpr uintptr_t kSegmentAlign = 0x01000000u;    // 16 MB
  constexpr size_t kMinSegment = 0x04000000u;         // reserve at least 64 MB at a time
  constexpr size_t kHeapStep = 0x01000000u;           // the heap grows by 16 MB ranges
  constexpr size_t kStackSize = 0x00400000u;          // 4 MB (main.exe: StackReserveSize 4000000)
  constexpr size_t kGuardSize = kGranule;             // PROT_NONE below every arena stack
  constexpr size_t kMaxThreads = 1024;
  constexpr size_t kMaxSegments = 128;

  // Granule states; the low 7 bits of gMap. kStart marks the first granule of a range handed out.
  constexpr uint8_t kNone = 0;  // not the arena's
  constexpr uint8_t kFree = 1;  // reserved by the arena, not handed out
  constexpr uint8_t kStart = 0x80;
  constexpr uint8_t kKindMask = 0x7f;

  constexpr uintptr_t RoundUp(const uintptr_t value, const uintptr_t to) { return (value + to - 1u) & ~(to - 1u); }

  // ------------------------------------------------------------------------------------------------
  // Messages (no malloc: a fixed buffer, written with write(2))
  // ------------------------------------------------------------------------------------------------

  struct Line
  {
    char text[1024];
    size_t used = 0;

    Line& Str(const char* s)
    {
      while (*s != '\0' && used + 1 < sizeof(text)) {
        text[used++] = *s++;
      }
      return *this;
    }
    Line& Dec(unsigned long long v)
    {
      char digits[24];
      int n = 0;
      do {
        digits[n++] = static_cast<char>('0' + v % 10u);
        v /= 10u;
      } while (v != 0 && n < 24);
      while (n > 0 && used + 1 < sizeof(text)) {
        text[used++] = digits[--n];
      }
      return *this;
    }
    Line& Hex(unsigned long long v)
    {
      Str("0x");
      char digits[17];
      int n = 0;
      do {
        digits[n++] = "0123456789abcdef"[v & 15u];
        v >>= 4u;
      } while (v != 0 && n < 16);
      while (n < 8) {
        digits[n++] = '0';
      }
      while (n > 0 && used + 1 < sizeof(text)) {
        text[used++] = digits[--n];
      }
      return *this;
    }
    // Megabytes with one decimal.
    Line& Mb(unsigned long long bytes)
    {
      const unsigned long long tenths = (bytes * 10u + (1u << 19)) >> 20;
      Dec(tenths / 10u).Str(".").Dec(tenths % 10u);
      return *this;
    }
    void Write(const int fd)
    {
      if (used + 1 < sizeof(text)) {
        text[used++] = '\n';
      }
      size_t done = 0;
      while (done < used) {
        const ssize_t n = write(fd, text + done, used - done);
        if (n <= 0) {
          if (n < 0 && errno == EINTR) {
            continue;
          }
          break;
        }
        done += static_cast<size_t>(n);
      }
    }
  };

  [[noreturn]] void Fatal(const char* what, const uintptr_t value)
  {
    Line line;
    line.Str("[lowarena] fatal: ").Str(what).Str(" ").Hex(value).Write(2);
    abort();
  }

  // ------------------------------------------------------------------------------------------------
  // Mode: on unless FAF_LOWARENA=0
  // ------------------------------------------------------------------------------------------------

  int gMode = -1;  // -1 undecided, 0 off, 1 on (accessed with __atomic builtins)

  int Mode()
  {
    int mode = __atomic_load_n(&gMode, __ATOMIC_ACQUIRE);
    if (mode >= 0) {
      return mode;
    }
    // The first malloc can come from libc's own initialisation; until the environment is set up the
    // arena serves (its blocks are recognised by address later, whatever the decision).
    if (environ == nullptr) {
      return 1;
    }
    const char* const value = getenv("FAF_LOWARENA");
    mode = (value != nullptr && value[0] == '0' && value[1] == '\0') ? 0 : 1;
    __atomic_store_n(&gMode, mode, __ATOMIC_RELEASE);
    return mode;
  }

  // ------------------------------------------------------------------------------------------------
  // The range allocator
  // ------------------------------------------------------------------------------------------------

  uint8_t gMap[kGranuleCount];  // per 64 KB granule: kNone/kFree or a LOWARENA_KIND_*, plus kStart
  pthread_mutex_t gLock = PTHREAD_MUTEX_INITIALIZER;

  struct Segment
  {
    uintptr_t start;
    uintptr_t end;
  };
  Segment gSegments[kMaxSegments];
  uint32_t gSegmentCount = 0;

  // Statistics (written under gLock; read without it for the report).
  uint64_t gReserved = 0;
  uint64_t gInUse = 0;
  uint64_t gInUseHighWater = 0;
  uint64_t gTop = 0;
  uint32_t gViewsLive = 0, gViewsPeak = 0, gViewFallbacks = 0;
  uint32_t gVirtualLive = 0, gVirtualPeak = 0, gVirtualFallbacks = 0;
  uint64_t gImageBase = 0, gImageSize = 0;
  uint64_t gForeignFrees = 0, gForeignReallocs = 0;  // __atomic
  bool gFullReported = false;

  inline uint8_t Get(const size_t index) { return __atomic_load_n(&gMap[index], __ATOMIC_ACQUIRE); }
  inline void Set(const size_t index, const uint8_t value) { __atomic_store_n(&gMap[index], value, __ATOMIC_RELEASE); }

  const char* KindName(const uint8_t kind)
  {
    switch (kind) {
      case LOWARENA_KIND_HEAP:
        return "lowarena:heap";
      case LOWARENA_KIND_STACK:
        return "lowarena:stack";
      case LOWARENA_KIND_VIRTUAL:
        return "lowarena:virtual";
      case LOWARENA_KIND_VIEW:
        return "lowarena:view";
      case LOWARENA_KIND_IMAGE:
        return "lowarena:image";
      default:
        return "lowarena";
    }
  }

  // Names an anonymous range in /proc/self/maps ("[anon:lowarena:heap]"). The kernel may keep the
  // pointer, so only string literals are passed. Failure (older kernels, translation) is harmless.
  void NameRange(const uintptr_t base, const size_t length, const char* name)
  {
    (void)prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, base, length, reinterpret_cast<unsigned long>(name));
  }

  uintptr_t PageSize()
  {
    static uintptr_t size = 0;
    if (size == 0) {
      const long value = sysconf(_SC_PAGESIZE);
      size = value > 0 ? static_cast<uintptr_t>(value) : 4096u;
    }
    return size;
  }

  void AddSegment(const uintptr_t start, const uintptr_t end)
  {
    for (uint32_t i = 0; i < gSegmentCount; ++i) {
      if (gSegments[i].end == start) {
        gSegments[i].end = end;
        // A segment that now touches the next one absorbs it.
        for (uint32_t j = 0; j < gSegmentCount; ++j) {
          if (j != i && gSegments[j].start == end) {
            gSegments[i].end = gSegments[j].end;
            gSegments[j] = gSegments[--gSegmentCount];
            break;
          }
        }
        return;
      }
      if (gSegments[i].start == end) {
        gSegments[i].start = start;
        return;
      }
    }
    if (gSegmentCount < kMaxSegments) {
      gSegments[gSegmentCount++] = Segment{start, end};
    }
  }

  // True when this process is an arm64 guest of the emulator's ARM translator (libndk_translation /
  // Berberis). The translator keeps its code cache in MAP_32BIT memory, i.e. in [1 GB, 2 GB), and
  // keeps adding to it while guest code runs; the arena must not take all of that window.
  // Read once from /proc/self/maps with plain read(2) (this can run inside malloc).
  bool UnderTranslation()
  {
#if defined(__aarch64__)
    static int known = -1;
    if (known >= 0) {
      return known != 0;
    }
    const int fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    int found = 0;
    if (fd >= 0) {
      static const char* const kNeedles[] = {"libndk_translation", "libberberis"};
      char buffer[4096 + 64];
      size_t carry = 0;
      for (;;) {
        const ssize_t got = read(fd, buffer + carry, sizeof(buffer) - 64 - carry);
        if (got < 0 && errno == EINTR) {
          continue;
        }
        if (got <= 0) {
          break;
        }
        const size_t used = carry + static_cast<size_t>(got);
        for (const char* needle : kNeedles) {
          const size_t n = strlen(needle);
          for (size_t i = 0; i + n <= used && found == 0; ++i) {
            found = memcmp(buffer + i, needle, n) == 0 ? 1 : 0;
          }
        }
        if (found != 0) {
          break;
        }
        carry = used < 32 ? used : 32;  // a needle may straddle two reads
        memmove(buffer, buffer + used - carry, carry);
      }
      close(fd);
    }
    known = found;
    return found != 0;
#else
    return false;
#endif
  }

  // Under translation the arena stays below 1.75 GB, leaving 256 MB of the MAP_32BIT window free for
  // the translator's code cache; otherwise it may use everything up to 2 GB.
  constexpr uintptr_t kTranslatedHigh = 0x70000000u;
  uintptr_t ReserveLimit()
  {
    return UnderTranslation() ? kTranslatedHigh : kHigh;
  }

  // Reserves [at, at + length) from the kernel (PROT_NONE, no commit). Caller holds gLock; every
  // granule of the range is kNone. MAP_FIXED_NOREPLACE fails with EEXIST where anything is mapped;
  // kernels before 4.17 ignore the flag and treat the address as a hint, hence the address check.
  bool ReserveSegment(const uintptr_t at, const size_t length)
  {
    if (at < kLow || at + length > ReserveLimit() || length == 0) {
      return false;
    }
    void* const mapped = mmap(reinterpret_cast<void*>(at), length, PROT_NONE,
                              MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (mapped == MAP_FAILED) {
      return false;
    }
    if (reinterpret_cast<uintptr_t>(mapped) != at) {
      munmap(mapped, length);
      return false;
    }
    NameRange(at, length, "lowarena");
    for (size_t i = at / kGranule; i < (at + length) / kGranule; ++i) {
      Set(i, kFree);
    }
    gReserved += length;
    AddSegment(at, at + length);
    return true;
  }

  bool AllNone(const size_t first, const size_t count)
  {
    for (size_t i = first; i < first + count; ++i) {
      if (Get(i) != kNone) {
        return false;
      }
    }
    return true;
  }

  // A new segment of `length` (a multiple of 16 MB) at the lowest 16 MB-aligned address where the
  // kernel has nothing mapped. Caller holds gLock.
  bool ReserveFirstFit(const size_t length)
  {
    const uintptr_t limit = ReserveLimit();
    for (uintptr_t at = kLow; at + length <= limit; at += kSegmentAlign) {
      if (!AllNone(at / kGranule, length / kGranule)) {
        continue;
      }
      if (ReserveSegment(at, length)) {
        return true;
      }
    }
    return false;
  }

  // The lowest run of `count` free granules; kGranuleCount when there is none. Caller holds gLock.
  size_t FindRun(const size_t count)
  {
    size_t run = 0;
    size_t start = 0;
    for (size_t i = kLow / kGranule; i < kGranuleCount; ++i) {
      if (Get(i) == kFree) {
        if (run == 0) {
          start = i;
        }
        if (++run == count) {
          return start;
        }
      } else {
        run = 0;
      }
    }
    return kGranuleCount;
  }

  void MarkUsed(const size_t first, const size_t count, const uint8_t kind)
  {
    for (size_t i = count; i-- > 1;) {
      Set(first + i, kind);
    }
    Set(first, static_cast<uint8_t>(kind | kStart));
    const uint64_t bytes = static_cast<uint64_t>(count) * kGranule;
    gInUse += bytes;
    if (gInUse > gInUseHighWater) {
      gInUseHighWater = gInUse;
    }
    const uint64_t end = static_cast<uint64_t>(first + count) * kGranule;
    if (end > gTop) {
      gTop = end;
    }
  }

  void ReportFull(const size_t length, const uint8_t kind)
  {
    if (gFullReported) {
      return;
    }
    gFullReported = true;
    Line line;
    line.Str("[lowarena] no room below ").Hex(ReserveLimit()).Str(" for ").Dec(length).Str(" bytes (").Str(KindName(kind))
      .Str("); in use ").Mb(gInUse).Str(" MB of ").Mb(gReserved).Str(" MB reserved");
    line.Write(2);
  }

  // A range of `length` bytes (rounded up to granules) of `kind`, with `protection`. Null when no
  // room is left below 2 GB.
  void* AllocRange(const size_t length, const uint8_t kind, const int protection)
  {
    if (length == 0 || length > kHigh) {
      return nullptr;
    }
    const size_t count = RoundUp(length, kGranule) / kGranule;
    pthread_mutex_lock(&gLock);
    size_t first = FindRun(count);
    if (first == kGranuleCount) {
      const size_t needed = RoundUp(count * kGranule, kSegmentAlign);
      if ((needed < kMinSegment && ReserveFirstFit(kMinSegment)) || ReserveFirstFit(needed)) {
        first = FindRun(count);
      }
    }
    if (first == kGranuleCount) {
      ReportFull(length, kind);
      pthread_mutex_unlock(&gLock);
      return nullptr;
    }
    MarkUsed(first, count, kind);
    pthread_mutex_unlock(&gLock);
    const uintptr_t base = first * kGranule;
    const size_t bytes = count * kGranule;
    if (protection != PROT_NONE && mprotect(reinterpret_cast<void*>(base), bytes, protection) != 0) {
      Fatal("mprotect failed on an arena range at", base);
    }
    NameRange(base, bytes, KindName(kind));
    return reinterpret_cast<void*>(base);
  }

  // Extends a range in place: hands out [at, at + length) when every granule there is free, or is
  // not the arena's yet and can be reserved from the kernel. The heap uses it to stay contiguous.
  bool AllocRangeAt(const uintptr_t at, const size_t length, const uint8_t kind, const int protection)
  {
    if (at < kLow || at % kGranule != 0 || length == 0 || at + length > kHigh) {
      return false;
    }
    const size_t first = at / kGranule;
    const size_t count = RoundUp(length, kGranule) / kGranule;
    pthread_mutex_lock(&gLock);
    // [first, split) must be free, [split, first + count) not the arena's (reserved below).
    size_t split = first;
    while (split < first + count && Get(split) == kFree) {
      ++split;
    }
    bool ok = true;
    if (split < first + count) {
      const uintptr_t from = split * kGranule;
      const uintptr_t needEnd = RoundUp(at + length, kSegmentAlign);
      const uintptr_t limit = ReserveLimit();
      ok = from % kSegmentAlign == 0 && needEnd <= limit && AllNone(split, (needEnd - from) / kGranule);
      if (ok) {
        // Reserve generously when the space is there, so the heap keeps growing in place.
        uintptr_t end = from + kMinSegment > needEnd ? from + kMinSegment : needEnd;
        if (end > limit || !AllNone(split, (end - from) / kGranule) || !ReserveSegment(from, end - from)) {
          ok = ReserveSegment(from, needEnd - from);
        }
      }
    }
    if (ok) {
      MarkUsed(first, count, kind);
    }
    pthread_mutex_unlock(&gLock);
    if (!ok) {
      return false;
    }
    if (protection != PROT_NONE && mprotect(reinterpret_cast<void*>(at), count * kGranule, protection) != 0) {
      Fatal("mprotect failed on an arena range at", at);
    }
    NameRange(at, count * kGranule, KindName(kind));
    return true;
  }

  // Gives a range back: its pages are replaced by a fresh PROT_NONE reservation (dropping them and,
  // for a view, the file), then its granules are free again. Returns false when `base` does not start
  // a range of a releasable kind.
  bool ReleaseRange(const uintptr_t base, const size_t length, uint8_t* kindOut)
  {
    if (base < kLow || base >= kHigh || base % kGranule != 0) {
      return false;
    }
    const size_t first = base / kGranule;
    pthread_mutex_lock(&gLock);
    const uint8_t head = Get(first);
    const uint8_t kind = head & kKindMask;
    if ((head & kStart) == 0 || kind == kNone || kind == kFree || kind == LOWARENA_KIND_HEAP ||
        kind == LOWARENA_KIND_IMAGE) {
      pthread_mutex_unlock(&gLock);
      return false;
    }
    size_t count = 1;
    while (first + count < kGranuleCount && Get(first + count) == kind) {
      ++count;
    }
    const size_t bytes = count * kGranule;
    void* const replaced = mmap(reinterpret_cast<void*>(base), bytes, PROT_NONE,
                                MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);
    if (replaced == MAP_FAILED) {
      pthread_mutex_unlock(&gLock);
      Fatal("cannot restore the reservation of an arena range at", base);
    }
    NameRange(base, bytes, "lowarena");
    for (size_t i = 0; i < count; ++i) {
      Set(first + i, kFree);
    }
    gInUse -= bytes;
    pthread_mutex_unlock(&gLock);
    if (length != 0 && RoundUp(length, kGranule) != bytes) {
      Line line;
      line.Str("[lowarena] release of ").Hex(base).Str(": caller length ").Dec(length).Str(", range ")
        .Dec(bytes).Str(" (").Str(KindName(kind)).Str("); released the range");
      line.Write(2);
    }
    if (kindOut != nullptr) {
      *kindOut = kind;
    }
    return true;
  }

  inline bool IsHeapPointer(const void* pointer)
  {
    const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
    if (address - kLow >= kHigh - kLow) {  // also rejects tagged (top-byte) and high pointers
      return false;
    }
    return (Get(address / kGranule) & kKindMask) == LOWARENA_KIND_HEAP;
  }

  // ------------------------------------------------------------------------------------------------
  // The heap's sbrk (called by dlmalloc with its morecore lock held)
  // ------------------------------------------------------------------------------------------------

  uintptr_t gBrk = 0;
  uintptr_t gBrkEnd = 0;

  void* MoreCore(const ptrdiff_t increment)
  {
    void* const failed = reinterpret_cast<void*>(~static_cast<uintptr_t>(0));
    if (increment == 0) {
      return reinterpret_cast<void*>(gBrk);
    }
    if (increment < 0) {
      return failed;  // MORECORE_CANNOT_TRIM: never asked
    }
    const size_t bytes = static_cast<size_t>(increment);
    if (gBrk != 0 && gBrkEnd - gBrk >= bytes) {
      const uintptr_t old = gBrk;
      gBrk += bytes;
      return reinterpret_cast<void*>(old);
    }
    if (gBrk != 0) {
      const size_t more = RoundUp(bytes - (gBrkEnd - gBrk), kHeapStep);
      if (AllocRangeAt(gBrkEnd, more, LOWARENA_KIND_HEAP, PROT_READ | PROT_WRITE)) {
        gBrkEnd += more;
        const uintptr_t old = gBrk;
        gBrk += bytes;
        return reinterpret_cast<void*>(old);
      }
    }
    const size_t length = RoundUp(bytes, kHeapStep);
    void* const range = AllocRange(length, LOWARENA_KIND_HEAP, PROT_READ | PROT_WRITE);
    if (range == nullptr) {
      return failed;
    }
    gBrk = reinterpret_cast<uintptr_t>(range) + bytes;
    gBrkEnd = reinterpret_cast<uintptr_t>(range) + length;
    return range;
  }

  // ------------------------------------------------------------------------------------------------
  // bionic's allocator, for FAF_LOWARENA=0 and for pointers the arena did not hand out
  // ------------------------------------------------------------------------------------------------

  enum RealFn { kRealMalloc, kRealFree, kRealCalloc, kRealRealloc, kRealMemalign, kRealPosixMemalign,
                kRealAlignedAlloc, kRealUsableSize, kRealCount };
  const char* const kRealNames[kRealCount] = {"malloc", "free", "calloc", "realloc", "memalign",
                                              "posix_memalign", "aligned_alloc", "malloc_usable_size"};
  void* gReal[kRealCount];
  int gResolving = 0;

  // The next definition after this executable (libc's), or null: unknown (aligned_alloc before API
  // 28), or asked recursively while dlsym runs, in which case the caller uses the arena (its blocks
  // are recognised by address wherever they are freed).
  void* Real(const RealFn which)
  {
    void* fn = __atomic_load_n(&gReal[which], __ATOMIC_ACQUIRE);
    if (fn != nullptr) {
      return fn;
    }
    if (__atomic_exchange_n(&gResolving, 1, __ATOMIC_ACQ_REL) != 0) {
      return nullptr;
    }
    fn = dlsym(RTLD_NEXT, kRealNames[which]);
    __atomic_store_n(&gReal[which], fn, __ATOMIC_RELEASE);
    __atomic_store_n(&gResolving, 0, __ATOMIC_RELEASE);
    return fn;
  }

  template <typename F>
  F RealAs(const RealFn which)
  {
    return reinterpret_cast<F>(Real(which));
  }

  bool IsPowerOfTwo(const size_t value) { return value != 0 && (value & (value - 1u)) == 0; }

  // ------------------------------------------------------------------------------------------------
  // Heap check mode: FAF_LOWARENA_CHECK=1 (2 also records where each block was allocated)
  // ------------------------------------------------------------------------------------------------
  // A debugging aid for heap corruption (README.md, "Heap check"). Every block gets a 96-byte header
  // (magic, requested size, the dlmalloc block, a sequence number, the allocating frames, and 16
  // canary bytes right before the block) and 16 canary bytes after it; free and realloc check them. A freed block is filled with 0xdd and kept in
  // a quarantine (256 MB or 1M blocks) before dlmalloc gets it back; leaving the quarantine checks
  // that the fill is intact, which catches writes after free. Damage is reported on stderr (block,
  // size, sequence number, allocating frames, the first damaged byte and a dump) and aborts, so the
  // tombstone holds the stack of the call that found it. malloc_usable_size reports the requested
  // size. Off (the default) the malloc family is dlmalloc alone, as before.
  //
  // With FAF_LOWARENA=0 the checked blocks come from bionic's allocator instead (BaseAlloc): the same
  // checks at the high addresses the APK sees, for the truncation hunt of M3d.
  //
  // Live blocks are also on a list, and every FAF_LOWARENA_CHECK_SCAN allocations (default 65536, 0
  // never) and at exit all of them are scanned: header and canary. That finds an overflow of a block
  // that is never freed, between two scans.

  constexpr size_t kCheckHeader = 96;
  constexpr size_t kCheckTail = 16;
  constexpr size_t kCheckSites = 4;
  constexpr uint64_t kCheckLive = 0x6c6976652d626c6bull;   // "live-blk"
  constexpr uint64_t kCheckFreed = 0x667265652d626c6bull;  // "free-blk"
  constexpr uint8_t kCheckTailByte = 0xab;
  constexpr uint8_t kCheckFreeByte = 0xdd;
  constexpr size_t kQuarantineSlots = size_t{1} << 20;
  constexpr size_t kQuarantineBytes = size_t{256} << 20;

  struct CheckHeader
  {
    uint64_t magic;  // kCheckLive or kCheckFreed, xor the block's address
    uint64_t size;   // requested bytes
    uintptr_t base;  // what BaseAlloc returned
    uint64_t seq;    // allocation number (1, 2, ...)
    CheckHeader* prev;  // the list of live blocks (gLiveLock)
    CheckHeader* next;
    uintptr_t site[kCheckSites];
    uint8_t front[16];  // kCheckTailByte: an underflow hits these first
  };
  static_assert(sizeof(CheckHeader) == kCheckHeader, "the header keeps 16-byte alignment");

  int gCheck = -1;  // -1 undecided, else FAF_LOWARENA_CHECK's level (0 off); __atomic
  uint64_t gCheckScanEvery = 65536;  // FAF_LOWARENA_CHECK_SCAN, read with gCheck
  uint64_t gCheckScans = 0, gCheckLive = 0;
  CheckHeader* gLiveHead = nullptr;
  pthread_mutex_t gLiveLock = PTHREAD_MUTEX_INITIALIZER;
  uint64_t gCheckSeq = 0, gCheckFrees = 0, gCheckVerified = 0;  // __atomic
  void* gQuarantine[kQuarantineSlots];
  size_t gQuarantineHead = 0, gQuarantineCount = 0, gQuarantineBytes = 0;
  pthread_mutex_t gQuarantineLock = PTHREAD_MUTEX_INITIALIZER;

  int CheckLevel()
  {
    int level = __atomic_load_n(&gCheck, __ATOMIC_ACQUIRE);
    if (level >= 0) {
      return level;
    }
    if (environ == nullptr) {
      return 0;  // libc's own start-up: plain blocks (recognised by their missing header later)
    }
    const char* const value = getenv("FAF_LOWARENA_CHECK");
    level = (value != nullptr && value[0] >= '1' && value[0] <= '9') ? value[0] - '0' : 0;
    if (const char* const scan = getenv("FAF_LOWARENA_CHECK_SCAN")) {
      uint64_t every = 0;
      for (const char* c = scan; *c >= '0' && *c <= '9'; ++c) {
        every = every * 10u + static_cast<uint64_t>(*c - '0');
      }
      gCheckScanEvery = every;
    }
    __atomic_store_n(&gCheck, level, __ATOMIC_RELEASE);
    return level;
  }

  inline CheckHeader* HeaderOf(const void* block)
  {
    return reinterpret_cast<CheckHeader*>(reinterpret_cast<uintptr_t>(const_cast<void*>(block)) - kCheckHeader);
  }

  // Where checked blocks live: the arena's dlmalloc, or bionic's allocator with FAF_LOWARENA=0.
  void* BaseAlloc(const size_t bytes)
  {
    if (Mode() == 0) {
      if (auto real = RealAs<void* (*)(size_t)>(kRealMalloc)) {
        return real(bytes);
      }
    }
    return dlmalloc(bytes);
  }

  void BaseFree(void* const base)
  {
    if (IsHeapPointer(base)) {
      dlfree(base);
    } else if (auto real = RealAs<void (*)(void*)>(kRealFree)) {
      real(base);
    }
  }

  // True when the check layer serves pointers outside the arena (FAF_LOWARENA=0): every such pointer
  // handed to free/realloc since the decision is then one of its blocks.
  inline bool CheckServesHigh() { return CheckLevel() != 0 && Mode() == 0; }

  // A block from the check layer: its header carries the magic for its address. The header is read
  // when it lies in the arena's heap, or, with FAF_LOWARENA=0, for any pointer outside the arena.
  bool IsCheckedBlock(const void* block, const uint64_t magic)
  {
    const uintptr_t address = reinterpret_cast<uintptr_t>(block);
    if ((address & 15u) != 0) {
      return false;
    }
    if (!IsHeapPointer(reinterpret_cast<const void*>(address - kCheckHeader)) &&
        !(CheckServesHigh() && address - kLow >= kHigh - kLow && address > kCheckHeader)) {
      return false;
    }
    return HeaderOf(block)->magic == (magic ^ address);
  }

  // The allocating frames: return addresses inside the engine library's image (any, before it is
  // loaded into the arena), from the frame-pointer chain. The engine is built without optimisation
  // and LowArena.cpp with -fno-omit-frame-pointer; the chain is followed only while it stays inside
  // the thread's stack: an arena stack, or with FAF_LOWARENA=0 bionic's stack of a non-main thread
  // (pthread_getattr_np allocates only for the main thread, which is skipped).
  __attribute__((noinline)) void CaptureSites(uintptr_t* const out)
  {
    const uintptr_t imageBase = gImageBase, imageEnd = gImageBase + gImageSize;
    uintptr_t stackLow = 0, stackHigh = 0;
    if (Mode() == 0) {
      if (syscall(SYS_gettid) == getpid()) {
        return;
      }
      pthread_attr_t attr;
      if (pthread_getattr_np(pthread_self(), &attr) != 0) {
        return;
      }
      void* stackAddr = nullptr;
      size_t stackSize = 0;
      pthread_attr_getstack(&attr, &stackAddr, &stackSize);
      pthread_attr_destroy(&attr);
      stackLow = reinterpret_cast<uintptr_t>(stackAddr);
      stackHigh = stackLow + stackSize;
    }
    const auto onStack = [stackLow, stackHigh](const uintptr_t at) {
      if (stackHigh != 0) {
        return at >= stackLow && at < stackHigh;
      }
      return at - kLow < kHigh - kLow && (Get(at / kGranule) & kKindMask) == LOWARENA_KIND_STACK;
    };
    uintptr_t fp = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
    size_t count = 0;
    for (int depth = 0; depth < 32 && count < kCheckSites; ++depth) {
      if ((fp & 7u) != 0 || !onStack(fp) || !onStack(fp + 15u)) {
        break;
      }
      const uintptr_t next = reinterpret_cast<const uintptr_t*>(fp)[0];
      const uintptr_t ret = reinterpret_cast<const uintptr_t*>(fp)[1];
      if (imageBase == 0 || (ret >= imageBase && ret < imageEnd)) {
        out[count++] = ret;
      }
      if (next <= fp || next - fp > kStackSize) {
        break;
      }
      fp = next;
    }
  }

  [[noreturn]] void CheckFailed(const char* what, const void* block, const CheckHeader* header,
                                const uintptr_t badAt)
  {
    Line line;
    line.Str("[lowarena] check: ").Str(what).Str(" block=").Hex(reinterpret_cast<uintptr_t>(block));
    if (header != nullptr) {
      line.Str(" size=").Dec(header->size).Str(" seq=").Dec(header->seq).Str(" image=").Hex(gImageBase)
        .Str(" allocated-by");
      for (size_t i = 0; i < kCheckSites && header->site[i] != 0; ++i) {
        line.Str(" ").Hex(header->site[i]);
      }
    }
    if (badAt != 0) {
      line.Str(" first-bad=").Hex(badAt).Str(" (offset ")
        .Dec(badAt - reinterpret_cast<uintptr_t>(block)).Str(") bytes");
      // Up to 48 bytes from the 16-byte line holding the first damaged byte, in groups of four, not
      // beyond the block's canary (without a header: that line only, the damaged header itself).
      const uintptr_t from = badAt & ~uintptr_t{15};
      const uintptr_t end = header != nullptr ? reinterpret_cast<uintptr_t>(block) + header->size + kCheckTail
                                              : from + 16;
      // (a damaged front canary dumps from the canary into the block)
      for (uintptr_t at = from; at < from + 48 && at < end; ++at) {
        const uint8_t byte = *reinterpret_cast<const uint8_t*>(at);
        const char pair[3] = {"0123456789abcdef"[byte >> 4], "0123456789abcdef"[byte & 15u], '\0'};
        line.Str(at % 4u == 0 ? " " : "").Str(pair);
      }
    }
    line.Write(2);
    abort();
  }

  uintptr_t FirstNotEqual(const uintptr_t from, const size_t length, const uint8_t value)
  {
    const uint8_t* const p = reinterpret_cast<const uint8_t*>(from);
    size_t i = 0;
    uint64_t word = value * 0x0101010101010101ull;
    for (; i < length && ((from + i) & 7u) != 0; ++i) {
      if (p[i] != value) {
        return from + i;
      }
    }
    for (; i + 8 <= length; i += 8) {
      if (*reinterpret_cast<const uint64_t*>(p + i) != word) {
        break;
      }
    }
    for (; i < length; ++i) {
      if (p[i] != value) {
        return from + i;
      }
    }
    return 0;
  }

  // Every live block: header magic and canary. Caller holds gLiveLock.
  void ScanLiveLocked(const char* const when)
  {
    ++gCheckScans;
    for (CheckHeader* header = gLiveHead; header != nullptr; header = header->next) {
      void* const block = reinterpret_cast<uint8_t*>(header) + kCheckHeader;
      const uintptr_t address = reinterpret_cast<uintptr_t>(block);
      if (header->magic != (kCheckLive ^ address)) {
        Line line;
        line.Str("[lowarena] check: scan ").Str(when).Str(": header of a live block overwritten");
        line.Write(2);
        CheckFailed("live block header overwritten (underflow, or overflow of the block before it)", block,
                    nullptr, reinterpret_cast<uintptr_t>(header));
      }
      if (const uintptr_t bad = FirstNotEqual(address + header->size, kCheckTail, kCheckTailByte)) {
        Line line;
        line.Str("[lowarena] check: scan ").Str(when).Str(" found it");
        line.Write(2);
        CheckFailed("canary after a live block overwritten (overflow)", block, header, bad);
      }
      if (const uintptr_t bad = FirstNotEqual(reinterpret_cast<uintptr_t>(header->front), sizeof(header->front),
                                              kCheckTailByte)) {
        Line line;
        line.Str("[lowarena] check: scan ").Str(when).Str(" found it");
        line.Write(2);
        CheckFailed("canary before a live block overwritten (underflow)", block, header, bad);
      }
    }
    // The quarantine too: a write after free shows here, not only when the block leaves it.
    pthread_mutex_lock(&gQuarantineLock);
    for (size_t i = 0; i < gQuarantineCount; ++i) {
      void* const block = gQuarantine[(gQuarantineHead + i) % kQuarantineSlots];
      const CheckHeader* const header = HeaderOf(block);
      const uintptr_t address = reinterpret_cast<uintptr_t>(block);
      const uintptr_t bad =
        header->magic != (kCheckFreed ^ address) ? reinterpret_cast<uintptr_t>(header)
        : FirstNotEqual(address, header->size, kCheckFreeByte) ? FirstNotEqual(address, header->size, kCheckFreeByte)
        : FirstNotEqual(address + header->size, kCheckTail, kCheckTailByte)
          ? FirstNotEqual(address + header->size, kCheckTail, kCheckTailByte)
          : FirstNotEqual(reinterpret_cast<uintptr_t>(header->front), sizeof(header->front), kCheckTailByte);
      if (bad != 0) {
        Line line;
        line.Str("[lowarena] check: scan ").Str(when).Str(" found a quarantined block damaged");
        line.Write(2);
        CheckFailed("freed block written after free", block,
                    header->magic == (kCheckFreed ^ address) ? header : nullptr, bad);
      }
    }
    pthread_mutex_unlock(&gQuarantineLock);
  }

  void* CheckedAlloc(const size_t size, const size_t alignment)
  {
    const size_t extra = kCheckHeader + kCheckTail + (alignment > 16 ? alignment : 0);
    if (size > SIZE_MAX - extra) {
      errno = ENOMEM;
      return nullptr;
    }
    void* const base = BaseAlloc(size + extra);
    if (base == nullptr) {
      return nullptr;
    }
    uintptr_t block = reinterpret_cast<uintptr_t>(base) + kCheckHeader;
    if (alignment > 16) {
      block = RoundUp(block, alignment);
    }
    CheckHeader* const header = HeaderOf(reinterpret_cast<void*>(block));
    header->magic = kCheckLive ^ block;
    header->size = size;
    header->base = reinterpret_cast<uintptr_t>(base);
    header->seq = __atomic_add_fetch(&gCheckSeq, 1u, __ATOMIC_RELAXED);
    for (uintptr_t& site : header->site) {
      site = 0;
    }
    if (CheckLevel() >= 2) {
      CaptureSites(header->site);
    }
    memset(reinterpret_cast<void*>(block + size), kCheckTailByte, kCheckTail);
    memset(header->front, kCheckTailByte, sizeof(header->front));
    pthread_mutex_lock(&gLiveLock);
    header->prev = nullptr;
    header->next = gLiveHead;
    if (gLiveHead != nullptr) {
      gLiveHead->prev = header;
    }
    gLiveHead = header;
    ++gCheckLive;
    if (gCheckScanEvery != 0 && header->seq % gCheckScanEvery == 0) {
      ScanLiveLocked("periodic");
    }
    pthread_mutex_unlock(&gLiveLock);
    return reinterpret_cast<void*>(block);
  }

  void VerifyLive(void* const block, const char* const operation)
  {
    CheckHeader* const header = HeaderOf(block);
    if (header->magic == (kCheckFreed ^ reinterpret_cast<uintptr_t>(block))) {
      CheckFailed(operation[0] == 'r' ? "realloc of a freed block" : "double free", block, header, 0);
    }
    const uintptr_t tail = reinterpret_cast<uintptr_t>(block) + header->size;
    if (const uintptr_t bad = FirstNotEqual(tail, kCheckTail, kCheckTailByte)) {
      CheckFailed("canary after the block overwritten (overflow)", block, header, bad);
    }
    if (const uintptr_t bad = FirstNotEqual(reinterpret_cast<uintptr_t>(header->front), sizeof(header->front),
                                            kCheckTailByte)) {
      CheckFailed("canary before the block overwritten (underflow)", block, header, bad);
    }
  }

  void CheckedFree(void* const block)
  {
    VerifyLive(block, "free");
    CheckHeader* const header = HeaderOf(block);
    pthread_mutex_lock(&gLiveLock);
    if (header->prev != nullptr) {
      header->prev->next = header->next;
    } else {
      gLiveHead = header->next;
    }
    if (header->next != nullptr) {
      header->next->prev = header->prev;
    }
    --gCheckLive;
    pthread_mutex_unlock(&gLiveLock);
    header->magic = kCheckFreed ^ reinterpret_cast<uintptr_t>(block);
    memset(block, kCheckFreeByte, header->size);
    __atomic_add_fetch(&gCheckFrees, 1u, __ATOMIC_RELAXED);

    void* evicted[8];
    size_t evictedCount = 0;
    pthread_mutex_lock(&gQuarantineLock);
    if (gQuarantineCount == kQuarantineSlots) {
      evicted[evictedCount++] = gQuarantine[gQuarantineHead];
      gQuarantineHead = (gQuarantineHead + 1u) % kQuarantineSlots;
      --gQuarantineCount;
      gQuarantineBytes -= HeaderOf(evicted[0])->size;
    }
    gQuarantine[(gQuarantineHead + gQuarantineCount) % kQuarantineSlots] = block;
    ++gQuarantineCount;
    gQuarantineBytes += header->size;
    while (gQuarantineBytes > kQuarantineBytes && gQuarantineCount > 1 && evictedCount < 8) {
      void* const old = gQuarantine[gQuarantineHead];
      gQuarantineHead = (gQuarantineHead + 1u) % kQuarantineSlots;
      --gQuarantineCount;
      gQuarantineBytes -= HeaderOf(old)->size;
      evicted[evictedCount++] = old;
    }
    pthread_mutex_unlock(&gQuarantineLock);

    for (size_t i = 0; i < evictedCount; ++i) {
      void* const old = evicted[i];
      CheckHeader* const oldHeader = HeaderOf(old);
      if (oldHeader->magic != (kCheckFreed ^ reinterpret_cast<uintptr_t>(old))) {
        CheckFailed("header of a freed block overwritten (underflow or write after free)", old, nullptr,
                    reinterpret_cast<uintptr_t>(oldHeader));
      }
      if (const uintptr_t bad = FirstNotEqual(reinterpret_cast<uintptr_t>(old), oldHeader->size, kCheckFreeByte)) {
        CheckFailed("freed block written after free", old, oldHeader, bad);
      }
      if (const uintptr_t bad = FirstNotEqual(reinterpret_cast<uintptr_t>(old) + oldHeader->size, kCheckTail,
                                              kCheckTailByte)) {
        CheckFailed("canary after a freed block overwritten", old, oldHeader, bad);
      }
      if (const uintptr_t bad = FirstNotEqual(reinterpret_cast<uintptr_t>(oldHeader->front),
                                              sizeof(oldHeader->front), kCheckTailByte)) {
        CheckFailed("canary before a freed block overwritten", old, oldHeader, bad);
      }
      __atomic_add_fetch(&gCheckVerified, 1u, __ATOMIC_RELAXED);
      BaseFree(reinterpret_cast<void*>(oldHeader->base));
    }
  }

  // The heap entry points the malloc family uses: dlmalloc, or the check layer above it.
  // FAF_LOWARENA_FILL=<byte, decimal or 0x..>: fill every block malloc/memalign hands out (not calloc)
  // with that byte, without the check layer, so addresses stay as they are and only the contents a
  // read of uninitialised memory sees change. -1: not set.
  int gFill = -2;  // -2 undecided; __atomic

  int FillByte()
  {
    int fill = __atomic_load_n(&gFill, __ATOMIC_ACQUIRE);
    if (fill != -2) {
      return fill;
    }
    if (environ == nullptr) {
      return -1;
    }
    const char* const value = getenv("FAF_LOWARENA_FILL");
    fill = value != nullptr && value[0] != '\0' ? static_cast<int>(strtoul(value, nullptr, 0) & 0xffu) : -1;
    __atomic_store_n(&gFill, fill, __ATOMIC_RELEASE);
    return fill;
  }

  void* Filled(void* const block)
  {
    const int fill = FillByte();
    if (block != nullptr && fill >= 0) {
      memset(block, fill, dlmalloc_usable_size(block));
    }
    return block;
  }

  void* HeapMalloc(const size_t size)
  {
    return CheckLevel() != 0 ? CheckedAlloc(size, 16) : Filled(dlmalloc(size));
  }

  void* HeapMemalign(const size_t alignment, const size_t size)
  {
    return CheckLevel() != 0 ? CheckedAlloc(size, alignment) : Filled(dlmemalign(alignment, size));
  }

  void* HeapCalloc(const size_t count, const size_t size)
  {
    if (CheckLevel() == 0) {
      return dlcalloc(count, size);
    }
    size_t total = 0;
    if (__builtin_mul_overflow(count, size, &total)) {
      errno = ENOMEM;
      return nullptr;
    }
    void* const block = CheckedAlloc(total, 16);
    if (block != nullptr) {
      memset(block, 0, total);
    }
    return block;
  }

  // A pointer outside the arena that the check layer (FAF_LOWARENA=0) did not make: bionic's.
  void ForeignFree(void* const block);
  void* ForeignRealloc(void* const block, const size_t size);
  size_t ForeignUsableSize(const void* const block);

  void HeapFree(void* const block)
  {
    if (CheckLevel() != 0 && IsCheckedBlock(block, kCheckLive)) {
      CheckedFree(block);
      return;
    }
    if (CheckLevel() != 0 && IsCheckedBlock(block, kCheckFreed)) {
      CheckFailed("double free", block, HeaderOf(block), 0);
    }
    if (IsHeapPointer(block)) {
      dlfree(block);
    } else {
      ForeignFree(block);
    }
  }

  size_t HeapUsableSize(void* const block)
  {
    if (CheckLevel() != 0 && IsCheckedBlock(block, kCheckLive)) {
      return HeaderOf(block)->size;
    }
    return IsHeapPointer(block) ? dlmalloc_usable_size(block) : ForeignUsableSize(block);
  }

  void* HeapRealloc(void* const block, const size_t size)
  {
    if (CheckLevel() == 0 || !(IsCheckedBlock(block, kCheckLive) || IsCheckedBlock(block, kCheckFreed))) {
      return IsHeapPointer(block) ? dlrealloc(block, size) : ForeignRealloc(block, size);
    }
    VerifyLive(block, "realloc");
    void* const grown = CheckedAlloc(size, 16);
    if (grown == nullptr) {
      return nullptr;
    }
    const size_t old = HeaderOf(block)->size;
    memcpy(grown, block, old < size ? old : size);
    CheckedFree(block);
    return grown;
  }

  // ------------------------------------------------------------------------------------------------
  // Threads
  // ------------------------------------------------------------------------------------------------

  // kJoining/kDetaching: inside pthread_join/pthread_detach, which read bionic's pthread_internal_t
  // at the top of the stack; neither released nor matched again until the call returns.
  enum ThreadState : uint32_t { kSlotFree = 0, kJoinable = 1, kDetached = 2, kJoining = 3, kDetaching = 4 };

  struct ThreadRecord
  {
    uint32_t state;
    pid_t tid;           // set by the thread itself; 0 until it runs
    pthread_t thread;    // set by whichever comes first: the creator or the thread
    bool threadKnown;
    bool published;      // the creator is done with the record; until then it is never reaped
    uintptr_t base;      // the range: guard + stack
    size_t length;
    ThreadState previous;  // state before kJoining/kDetaching
  };

  ThreadRecord gThreads[kMaxThreads];
  pthread_mutex_t gThreadLock = PTHREAD_MUTEX_INITIALIZER;
  uint32_t gThreadsLive = 0, gThreadsPeak = 0, gThreadsTotal = 0, gThreadFallbacks = 0;

  struct StartBlock
  {
    void* (*start)(void*);
    void* arg;
    ThreadRecord* record;
  };

  pid_t CurrentTid() { return static_cast<pid_t>(syscall(SYS_gettid)); }

  void* ThreadTrampoline(void* raw)
  {
    StartBlock block = *static_cast<StartBlock*>(raw);
    free(raw);
    pthread_mutex_lock(&gThreadLock);
    block.record->tid = CurrentTid();
    if (!block.record->threadKnown) {
      block.record->thread = pthread_self();
      block.record->threadKnown = true;
    }
    pthread_mutex_unlock(&gThreadLock);
    return block.start(block.arg);
  }

  // Caller holds gThreadLock. Releases the stacks of detached threads the kernel no longer knows
  // (tgkill with signal 0 fails with ESRCH once the thread is gone; a reused tid only delays this).
  void ReapLocked()
  {
    for (ThreadRecord& record : gThreads) {
      if (record.state != kDetached || record.tid == 0 || !record.published) {
        continue;
      }
      if (syscall(SYS_tgkill, getpid(), record.tid, 0) == 0 || errno != ESRCH) {
        continue;
      }
      ReleaseRange(record.base, record.length, nullptr);
      record = ThreadRecord{};
      --gThreadsLive;
    }
  }

  ThreadRecord* FindLocked(const pthread_t thread, const uint32_t state)
  {
    for (ThreadRecord& record : gThreads) {
      if (record.state == state && record.threadKnown && pthread_equal(record.thread, thread)) {
        return &record;
      }
    }
    return nullptr;
  }
} // namespace

// ==================================================================================================
// dlmalloc's hooks (LowArenaHeap.c)
// ==================================================================================================

extern "C" void* lowarena_morecore(const ptrdiff_t increment)
{
  return MoreCore(increment);
}

extern "C" void lowarena_heap_error(const char* what, const void* pointer)
{
  Line line;
  line.Str("[lowarena] dlmalloc: ").Str(what).Str(" ").Hex(reinterpret_cast<uintptr_t>(pointer));
  line.Write(2);
  abort();
}

// ==================================================================================================
// The malloc family. Exported from the executable (build_runner.py: --export-dynamic-symbol), so it
// interposes bionic's for libc itself, libc++ and libfafengine.so.
// ==================================================================================================

extern "C" __attribute__((visibility("default"))) void* malloc(const size_t size)
{
  if (Mode() == 0 && CheckLevel() == 0) {
    if (auto real = RealAs<void* (*)(size_t)>(kRealMalloc)) {
      return real(size);
    }
  }
  return HeapMalloc(size);
}

extern "C" __attribute__((visibility("default"))) void free(void* const pointer)
{
  if (pointer == nullptr) {
    return;
  }
  if (IsHeapPointer(pointer) || CheckServesHigh()) {
    HeapFree(pointer);
    return;
  }
  ForeignFree(pointer);
}

namespace
{
  void ForeignFree(void* const pointer)
  {
    if (Mode() != 0) {
      __atomic_add_fetch(&gForeignFrees, 1u, __ATOMIC_RELAXED);
    }
    if (auto real = RealAs<void (*)(void*)>(kRealFree)) {
      real(pointer);
      return;
    }
    Fatal("free of a pointer the arena does not own, and libc's free is unavailable:",
          reinterpret_cast<uintptr_t>(pointer));
  }

  void* ForeignRealloc(void* const pointer, const size_t size)
  {
    if (Mode() != 0) {
      __atomic_add_fetch(&gForeignReallocs, 1u, __ATOMIC_RELAXED);
    }
    if (auto real = RealAs<void* (*)(void*, size_t)>(kRealRealloc)) {
      return real(pointer, size);
    }
    Fatal("realloc of a pointer the arena does not own, and libc's realloc is unavailable:",
          reinterpret_cast<uintptr_t>(pointer));
  }

  size_t ForeignUsableSize(const void* const pointer)
  {
    if (auto real = RealAs<size_t (*)(const void*)>(kRealUsableSize)) {
      return real(pointer);
    }
    return 0;
  }
} // namespace

extern "C" __attribute__((visibility("default"))) void* calloc(const size_t count, const size_t size)
{
  if (Mode() == 0 && CheckLevel() == 0) {
    if (auto real = RealAs<void* (*)(size_t, size_t)>(kRealCalloc)) {
      return real(count, size);
    }
  }
  return HeapCalloc(count, size);
}

extern "C" __attribute__((visibility("default"))) void* realloc(void* const pointer, const size_t size)
{
  if (pointer == nullptr) {
    return malloc(size);
  }
  if (IsHeapPointer(pointer) || CheckServesHigh()) {
    if (size == 0) {  // bionic (and the MSVC CRT): free and return null
      HeapFree(pointer);
      return nullptr;
    }
    return HeapRealloc(pointer, size);
  }
  return ForeignRealloc(pointer, size);
}

extern "C" __attribute__((visibility("default"))) void* reallocarray(void* const pointer, const size_t count,
                                                                     const size_t size)
{
  size_t total = 0;
  if (__builtin_mul_overflow(count, size, &total)) {
    errno = ENOMEM;
    return nullptr;
  }
  return realloc(pointer, total);
}

extern "C" __attribute__((visibility("default"))) void* memalign(const size_t alignment, const size_t size)
{
  if (Mode() == 0 && CheckLevel() == 0) {
    if (auto real = RealAs<void* (*)(size_t, size_t)>(kRealMemalign)) {
      return real(alignment, size);
    }
  }
  return HeapMemalign(alignment, size);
}

extern "C" __attribute__((visibility("default"))) int posix_memalign(void** const out, const size_t alignment,
                                                                     const size_t size)
{
  if (!IsPowerOfTwo(alignment) || alignment % sizeof(void*) != 0) {
    return EINVAL;
  }
  if (Mode() == 0 && CheckLevel() == 0) {
    if (auto real = RealAs<int (*)(void**, size_t, size_t)>(kRealPosixMemalign)) {
      return real(out, alignment, size);
    }
  }
  void* const block = HeapMemalign(alignment, size);
  if (block == nullptr) {
    return ENOMEM;
  }
  *out = block;
  return 0;
}

extern "C" __attribute__((visibility("default"))) void* aligned_alloc(const size_t alignment, const size_t size)
{
  if (!IsPowerOfTwo(alignment)) {
    errno = EINVAL;
    return nullptr;
  }
  if (Mode() == 0 && CheckLevel() == 0) {
    if (auto real = RealAs<void* (*)(size_t, size_t)>(kRealAlignedAlloc)) {
      return real(alignment, size);
    }
  }
  return memalign(alignment, size);
}

extern "C" __attribute__((visibility("default"))) void* valloc(const size_t size)
{
  return memalign(PageSize(), size);
}

extern "C" __attribute__((visibility("default"))) void* pvalloc(const size_t size)
{
  const uintptr_t page = PageSize();
  return memalign(page, RoundUp(size != 0 ? size : 1u, page));
}

extern "C" __attribute__((visibility("default"))) size_t malloc_usable_size(const void* const pointer)
{
  if (pointer == nullptr) {
    return 0;
  }
  if (IsHeapPointer(pointer) || CheckServesHigh()) {
    return HeapUsableSize(const_cast<void*>(pointer));
  }
  return ForeignUsableSize(pointer);
}

// ==================================================================================================
// lowarena_* (LowArena.h)
// ==================================================================================================

extern "C" int lowarena_enabled(void)
{
  return Mode();
}

extern "C" int lowarena_owns(const void* const address)
{
  const uintptr_t value = reinterpret_cast<uintptr_t>(address);
  if (value - kLow >= kHigh - kLow) {
    return 0;
  }
  const uint8_t kind = Get(value / kGranule) & kKindMask;
  return kind != kNone && kind != kFree;
}

extern "C" void* lowarena_reserve(const size_t length, const int protection, const int kind)
{
  if (Mode() == 0 || length == 0) {
    return nullptr;
  }
  void* const range = AllocRange(length, static_cast<uint8_t>(kind), protection);
  pthread_mutex_lock(&gLock);
  if (kind == LOWARENA_KIND_VIRTUAL) {
    if (range == nullptr) {
      ++gVirtualFallbacks;
    } else if (++gVirtualLive > gVirtualPeak) {
      gVirtualPeak = gVirtualLive;
    }
  } else if (kind == LOWARENA_KIND_IMAGE && range != nullptr) {
    gImageBase = reinterpret_cast<uintptr_t>(range);
    gImageSize = RoundUp(length, kGranule);
  }
  pthread_mutex_unlock(&gLock);
  return range;
}

extern "C" int lowarena_release(void* const base, const size_t length)
{
  uint8_t kind = kNone;
  if (!ReleaseRange(reinterpret_cast<uintptr_t>(base), length, &kind)) {
    return 0;
  }
  pthread_mutex_lock(&gLock);
  if (kind == LOWARENA_KIND_VIEW && gViewsLive > 0) {
    --gViewsLive;
  } else if (kind == LOWARENA_KIND_VIRTUAL && gVirtualLive > 0) {
    --gVirtualLive;
  }
  pthread_mutex_unlock(&gLock);
  return 1;
}

extern "C" void* lowarena_map_file(const size_t length, const int protection, const int flags, const int fd,
                                   const off_t offset)
{
  void* range = nullptr;
  if (Mode() != 0 && length != 0) {
    range = AllocRange(length, LOWARENA_KIND_VIEW, PROT_NONE);
    pthread_mutex_lock(&gLock);
    if (range == nullptr) {
      ++gViewFallbacks;  // no room below 2 GB: the view goes wherever the kernel puts it
    }
    pthread_mutex_unlock(&gLock);
  }
  if (range == nullptr) {
    void* const mapped = mmap(nullptr, length, protection, flags, fd, offset);
    return mapped != MAP_FAILED ? mapped : nullptr;
  }
  void* const mapped = mmap(range, length, protection, flags | MAP_FIXED, fd, offset);
  if (mapped == MAP_FAILED) {
    const int error = errno;
    ReleaseRange(reinterpret_cast<uintptr_t>(range), length, nullptr);
    errno = error;
    return nullptr;
  }
  pthread_mutex_lock(&gLock);
  if (++gViewsLive > gViewsPeak) {
    gViewsPeak = gViewsLive;
  }
  pthread_mutex_unlock(&gLock);
  return mapped;
}

extern "C" int lowarena_thread_create(const lowarena_pthread_create_fn create, pthread_t* const thread,
                                      const pthread_attr_t* const attr, void* (*const start)(void*),
                                      void* const arg)
{
  if (Mode() == 0) {
    return create(thread, attr, start, arg);
  }
  pthread_attr_t attributes;
  if (attr != nullptr) {
    attributes = *attr;
  } else {
    pthread_attr_init(&attributes);
  }
  void* userStack = nullptr;
  size_t userSize = 0;
  if (pthread_attr_getstack(&attributes, &userStack, &userSize) == 0 && userStack != nullptr) {
    return create(thread, attr, start, arg);  // the caller brought its own stack
  }
  size_t requested = 0;
  (void)pthread_attr_getstacksize(&attributes, &requested);
  const size_t stackSize = RoundUp(requested > kStackSize ? requested : kStackSize, kGranule);
  int detachState = PTHREAD_CREATE_JOINABLE;
  (void)pthread_attr_getdetachstate(&attributes, &detachState);

  pthread_mutex_lock(&gThreadLock);
  ReapLocked();
  ThreadRecord* record = nullptr;
  for (ThreadRecord& candidate : gThreads) {
    if (candidate.state == kSlotFree) {
      record = &candidate;
      break;
    }
  }
  if (record != nullptr) {
    *record = ThreadRecord{};
    record->state = detachState == PTHREAD_CREATE_DETACHED ? kDetached : kJoinable;
  }
  pthread_mutex_unlock(&gThreadLock);

  void* const range = record != nullptr ? AllocRange(kGuardSize + stackSize, LOWARENA_KIND_STACK, PROT_NONE) : nullptr;
  StartBlock* const block = range != nullptr ? static_cast<StartBlock*>(malloc(sizeof(StartBlock))) : nullptr;
  if (block == nullptr) {
    pthread_mutex_lock(&gThreadLock);
    if (range != nullptr) {
      ReleaseRange(reinterpret_cast<uintptr_t>(range), kGuardSize + stackSize, nullptr);
    }
    if (record != nullptr) {
      *record = ThreadRecord{};
    }
    ++gThreadFallbacks;
    pthread_mutex_unlock(&gThreadLock);
    return create(thread, attr, start, arg);  // no room (reported): bionic's stack
  }
  const uintptr_t stack = reinterpret_cast<uintptr_t>(range) + kGuardSize;
  if (mprotect(reinterpret_cast<void*>(stack), stackSize, PROT_READ | PROT_WRITE) != 0) {
    Fatal("mprotect failed on a thread stack at", stack);
  }
  NameRange(reinterpret_cast<uintptr_t>(range), kGuardSize, "lowarena:guard");
  if (pthread_attr_setstack(&attributes, reinterpret_cast<void*>(stack), stackSize) != 0) {
    pthread_mutex_lock(&gThreadLock);
    ReleaseRange(reinterpret_cast<uintptr_t>(range), kGuardSize + stackSize, nullptr);
    *record = ThreadRecord{};
    ++gThreadFallbacks;
    pthread_mutex_unlock(&gThreadLock);
    free(block);
    return create(thread, attr, start, arg);
  }
  record->base = reinterpret_cast<uintptr_t>(range);
  record->length = kGuardSize + stackSize;
  *block = StartBlock{start, arg, record};

  const int result = create(thread, &attributes, &ThreadTrampoline, block);
  pthread_mutex_lock(&gThreadLock);
  if (result != 0) {
    ReleaseRange(record->base, record->length, nullptr);
    *record = ThreadRecord{};
    pthread_mutex_unlock(&gThreadLock);
    free(block);
    return result;
  }
  if (!record->threadKnown) {
    record->thread = *thread;
    record->threadKnown = true;
  }
  // Only now may ReapLocked release it: a detached thread can finish before create() returns, and
  // the record must not be freed (and reused) under this function.
  record->published = true;
  ++gThreadsTotal;
  if (++gThreadsLive > gThreadsPeak) {
    gThreadsPeak = gThreadsLive;
  }
  pthread_mutex_unlock(&gThreadLock);
  return 0;
}

extern "C" uintptr_t lowarena_thread_join_begin(const pthread_t thread)
{
  pthread_mutex_lock(&gThreadLock);
  ThreadRecord* const record = FindLocked(thread, kJoinable);
  if (record != nullptr) {
    record->previous = kJoinable;
    record->state = kJoining;
  }
  pthread_mutex_unlock(&gThreadLock);
  return reinterpret_cast<uintptr_t>(record);
}

extern "C" void lowarena_thread_join_end(const uintptr_t token, const int result)
{
  ThreadRecord* const record = reinterpret_cast<ThreadRecord*>(token);
  pthread_mutex_lock(&gThreadLock);
  if (record != nullptr && record->state == kJoining) {
    if (result == 0) {  // the thread is gone and joined: its stack is free
      ReleaseRange(record->base, record->length, nullptr);
      *record = ThreadRecord{};
      --gThreadsLive;
    } else {
      record->state = record->previous;
    }
  }
  ReapLocked();
  pthread_mutex_unlock(&gThreadLock);
}

extern "C" uintptr_t lowarena_thread_detach_begin(const pthread_t thread)
{
  pthread_mutex_lock(&gThreadLock);
  ThreadRecord* const record = FindLocked(thread, kJoinable);
  if (record != nullptr) {
    record->previous = kJoinable;
    record->state = kDetaching;
  }
  pthread_mutex_unlock(&gThreadLock);
  return reinterpret_cast<uintptr_t>(record);
}

extern "C" void lowarena_thread_detach_end(const uintptr_t token, const int result)
{
  ThreadRecord* const record = reinterpret_cast<ThreadRecord*>(token);
  pthread_mutex_lock(&gThreadLock);
  if (record != nullptr && record->state == kDetaching) {
    // Detached: released once the kernel no longer knows the thread (ReapLocked).
    record->state = result == 0 ? kDetached : record->previous;
  }
  ReapLocked();
  pthread_mutex_unlock(&gThreadLock);
}

extern "C" void lowarena_get_stats(struct lowarena_stats* const out)
{
  if (out == nullptr) {
    return;
  }
  *out = lowarena_stats{};
  out->enabled = Mode();
  pthread_mutex_lock(&gThreadLock);
  ReapLocked();
  out->threads_live = gThreadsLive;
  out->threads_peak = gThreadsPeak;
  out->threads_total = gThreadsTotal;
  out->thread_fallbacks = gThreadFallbacks;
  pthread_mutex_unlock(&gThreadLock);
  pthread_mutex_lock(&gLock);
  out->segments = gSegmentCount;
  out->reserved_bytes = gReserved;
  out->in_use_bytes = gInUse;
  out->in_use_high_water = gInUseHighWater;
  out->top_address = gTop;
  out->views_live = gViewsLive;
  out->views_peak = gViewsPeak;
  out->view_fallbacks = gViewFallbacks;
  out->virtual_live = gVirtualLive;
  out->virtual_peak = gVirtualPeak;
  out->virtual_fallbacks = gVirtualFallbacks;
  out->image_base = gImageBase;
  out->limit_address = out->enabled != 0 ? ReserveLimit() : 0;
  out->image_size = gImageSize;
  pthread_mutex_unlock(&gLock);
  out->heap_footprint = gBrk != 0 ? dlmalloc_footprint() : 0;
  out->heap_max_footprint = gBrk != 0 ? dlmalloc_max_footprint() : 0;
  out->foreign_frees = __atomic_load_n(&gForeignFrees, __ATOMIC_RELAXED);
  out->foreign_reallocs = __atomic_load_n(&gForeignReallocs, __ATOMIC_RELAXED);
}

extern "C" void lowarena_report(const int fd)
{
  lowarena_stats stats;
  lowarena_get_stats(&stats);
  Line line;
  line.Str("[lowarena] enabled=").Dec(static_cast<unsigned>(stats.enabled));
  if (stats.enabled == 0) {
    line.Str(" (FAF_LOWARENA=0: bionic's allocator and thread stacks, plain dlopen)");
  }
  line.Str(" heap_max_mb=").Mb(stats.heap_max_footprint).Str(" heap_mb=").Mb(stats.heap_footprint)
    .Str(" high_water_mb=").Mb(stats.in_use_high_water).Str(" in_use_mb=").Mb(stats.in_use_bytes)
    .Str(" reserved_mb=").Mb(stats.reserved_bytes).Str(" top=").Hex(stats.top_address)
    .Str(" limit=").Hex(stats.limit_address).Str(stats.limit_address == kTranslatedHigh ? "(translated)" : "")
    .Str(" segments=").Dec(stats.segments);
  pthread_mutex_lock(&gLock);
  for (uint32_t i = 0; i < gSegmentCount && i < 8; ++i) {
    line.Str(i == 0 ? " [" : ",").Hex(gSegments[i].start).Str("-").Hex(gSegments[i].end);
  }
  if (gSegmentCount != 0) {
    line.Str(gSegmentCount > 8 ? ",...]" : "]");
  }
  pthread_mutex_unlock(&gLock);
  line.Str(" image=").Hex(stats.image_base).Str("+").Hex(stats.image_size)
    .Str(" threads_total=").Dec(stats.threads_total).Str(" threads_peak=").Dec(stats.threads_peak)
    .Str(" threads_live=").Dec(stats.threads_live).Str(" thread_fallbacks=").Dec(stats.thread_fallbacks)
    .Str(" views_peak=").Dec(stats.views_peak).Str(" view_fallbacks=").Dec(stats.view_fallbacks)
    .Str(" virtual_peak=").Dec(stats.virtual_peak).Str(" virtual_fallbacks=").Dec(stats.virtual_fallbacks)
    .Str(" foreign_frees=").Dec(stats.foreign_frees).Str(" foreign_reallocs=").Dec(stats.foreign_reallocs);
  if (const int level = CheckLevel()) {
    pthread_mutex_lock(&gLiveLock);
    ScanLiveLocked("at exit");
    const uint64_t live = gCheckLive, scans = gCheckScans;
    pthread_mutex_unlock(&gLiveLock);
    pthread_mutex_lock(&gQuarantineLock);
    const size_t quarantined = gQuarantineCount;
    pthread_mutex_unlock(&gQuarantineLock);
    line.Str(" check=").Dec(static_cast<unsigned>(level))
      .Str(" checked_allocs=").Dec(__atomic_load_n(&gCheckSeq, __ATOMIC_RELAXED))
      .Str(" checked_frees=").Dec(__atomic_load_n(&gCheckFrees, __ATOMIC_RELAXED))
      .Str(" quarantine_verified=").Dec(__atomic_load_n(&gCheckVerified, __ATOMIC_RELAXED))
      .Str(" quarantined=").Dec(quarantined).Str(" live=").Dec(live).Str(" scans=").Dec(scans);
  }
  line.Write(fd);
}
