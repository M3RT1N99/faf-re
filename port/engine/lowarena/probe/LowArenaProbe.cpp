// LowArena probe: libfafarenaprobe.so, a stand-in for libfafengine.so that faf_headless_runner loads
// instead (FAF_ENGINE_LIB). It is built and linked exactly like the engine library (build_runner.py
// --probe: the engine's compile command with port/engine/shim, the same -Wl,--wrap flags and
// LowArenaThreads.o) and checks where the things the engine uses end up:
//
//   heap (malloc family, operator new, libc's internal allocations, exception objects), thread stacks
//   (std::thread, pthread default/large/detached), thread_local (emulated TLS), the library image
//   (.text/.data/.bss/.rodata), argv and the environment, VirtualAlloc reservations and
//   MapViewOfFile views (the shim's stand-ins), plus a multi-threaded allocator stress, thread churn
//   with stack recycling, and pointers the arena did not hand out (freed through bionic).
//
// With the arena on (default) every one of them must lie in [16 MB, 2 GB) and survive the round trip
// through int; with FAF_LOWARENA=0 they must not (bionic's allocator: the heap's mapping is Scudo's).
// Exit code: the number of failed checks. Usage on the device:
//   FAF_ENGINE_LIB=$PWD/libfafarenaprobe.so ./faf_headless_runner [capacity]   (FAF_LOWARENA=0: arena off)
// "capacity" first fills the arena to its limit (ProbeCapacity).

#include <windows.h>

#include <alloca.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "../LowArena.h"

extern "C" char** environ;
// Not declared below API 28; weak, because the NDK's API 26 libc.so has none at link time. At run
// time it binds to faf_headless_runner's (the arena's) or bionic's definition.
extern "C" __attribute__((weak)) void* aligned_alloc(size_t alignment, size_t size);

namespace
{
  constexpr uintptr_t kTwoGB = 0x80000000u;
  constexpr uintptr_t kFourGB = 0x100000000ull;

  bool gArena = false;
  int gFailures = 0;
  int gChecks = 0;

  int gData = 7;     // .data
  int gBss;          // .bss
  thread_local int tLocal = 3;  // emulated TLS (minSdk 26): allocated through malloc

  uintptr_t Untagged(const void* p) { return reinterpret_cast<uintptr_t>(p) & 0x00FFFFFFFFFFFFFFull; }

  const char* Where(const void* p)
  {
    const uintptr_t a = Untagged(p);
    return a < kTwoGB ? "LOW<2G" : (a < kFourGB ? "LOW<4G" : "HIGH");
  }

  // The /proc/self/maps name of the mapping holding `p` ("" when anonymous and unnamed).
  std::string MapName(const void* p)
  {
    const uintptr_t a = Untagged(p);
    FILE* const maps = fopen("/proc/self/maps", "r");
    if (maps == nullptr) {
      return "?";
    }
    char line[512];
    std::string name = "(unmapped)";
    while (fgets(line, sizeof(line), maps) != nullptr) {
      unsigned long long lo = 0, hi = 0;
      int consumed = 0;
      if (sscanf(line, "%llx-%llx %*s %*s %*s %*s%n", &lo, &hi, &consumed) < 2) {
        continue;
      }
      if (a >= lo && a < hi) {
        const char* rest = line + consumed;
        while (*rest == ' ') {
          ++rest;
        }
        name.assign(rest);
        while (!name.empty() && (name.back() == '\n' || name.back() == ' ')) {
          name.pop_back();
        }
        break;
      }
    }
    fclose(maps);
    return name;
  }

  void Fail(const char* what, const char* why)
  {
    ++gFailures;
    printf("probe FAIL %s: %s\n", what, why);
  }

  // `moved`: something the arena places low. Arena on: it must be in [16 MB, 2 GB) and round-trip
  // through int. Arena off: it must not be below 4 GB.
  void Check(const char* what, const void* p, const bool moved = true)
  {
    const uintptr_t a = reinterpret_cast<uintptr_t>(p);
    bool ok = true;
    const char* expect = "-";
    if (moved) {
      ++gChecks;
      if (gArena) {
        expect = "low";
        const void* const roundTrip = reinterpret_cast<const void*>(static_cast<intptr_t>(static_cast<int32_t>(a)));
        ok = a >= LOWARENA_LOW && a < LOWARENA_HIGH && roundTrip == p;
      } else {
        expect = "high";
        ok = Untagged(p) >= kFourGB;
      }
    }
    printf("probe %-52s %016llx %-6s expect=%-4s %-4s %s\n", what, static_cast<unsigned long long>(a), Where(p),
           expect, ok ? "ok" : "FAIL", MapName(p).c_str());
    if (!ok) {
      ++gFailures;
    }
  }

  // ---------------------------------------------------------------------------------------------
  void* ReportThread(void* raw)
  {
    const char* const label = static_cast<const char*>(raw);
    int local = 0;
    std::string a = std::string(label) + ": stack local";
    Check(a.c_str(), &local);
    a = std::string(label) + ": thread_local int";
    Check(a.c_str(), &tLocal);
    a = std::string(label) + ": malloc(64)";
    void* const block = malloc(64);
    Check(a.c_str(), block);
    free(block);
    pthread_attr_t attr;
    if (pthread_getattr_np(pthread_self(), &attr) == 0) {
      void* base = nullptr;
      size_t size = 0;
      pthread_attr_getstack(&attr, &base, &size);
      printf("probe %s: stack %p + %zu KB\n", label, base, size >> 10);
      pthread_attr_destroy(&attr);
    }
    return nullptr;
  }

  void* LargeStackThread(void* raw)
  {
    ReportThread(raw);
    // Use most of the requested 8 MB: below a 4 MB stack this would hit the guard.
    volatile char* const deep = static_cast<volatile char*>(alloca(6u << 20));
    deep[0] = 1;
    deep[(6u << 20) - 1] = 2;
    return nullptr;
  }

  std::atomic<int> gDetachedDone{0};

  void* DetachedThread(void* raw)
  {
    if (raw != nullptr) {
      ReportThread(raw);
    }
    gDetachedDone.fetch_add(1);
    return nullptr;
  }

  // ---------------------------------------------------------------------------------------------
  void ProbeHeap()
  {
    Check("malloc(48)", malloc(48));
    Check("malloc(1 MB)", malloc(1u << 20));
    void* const big = malloc(100u << 20);  // larger than one 16 MB heap step
    Check("malloc(100 MB)", big);
    if (big != nullptr) {
      memset(big, 0x5a, 100u << 20);
      free(big);
    }
    void* const zeroed = calloc(1000, 8);
    Check("calloc(1000, 8)", zeroed);
    if (zeroed != nullptr) {
      for (int i = 0; i < 8000; ++i) {
        if (static_cast<unsigned char*>(zeroed)[i] != 0) {
          Fail("calloc", "memory not zeroed");
          break;
        }
      }
    }
    char* grown = static_cast<char*>(malloc(16));
    strcpy(grown, "fafre-lowarena");
    grown = static_cast<char*>(realloc(grown, 1u << 20));
    Check("realloc(16 -> 1 MB)", grown);
    if (grown == nullptr || strcmp(grown, "fafre-lowarena") != 0) {
      Fail("realloc", "content lost");
    }
    if (realloc(grown, 0) != nullptr) {
      Fail("realloc(p, 0)", "did not free and return null");
    }
    void* aligned = memalign(4096, 100);
    Check("memalign(4096, 100)", aligned);
    if ((reinterpret_cast<uintptr_t>(aligned) & 4095u) != 0) {
      Fail("memalign", "misaligned");
    }
    void* posix = nullptr;
    if (posix_memalign(&posix, 64, 1000) != 0 || (reinterpret_cast<uintptr_t>(posix) & 63u) != 0) {
      Fail("posix_memalign", "failed or misaligned");
    }
    Check("posix_memalign(64, 1000)", posix);
    if (posix_memalign(&posix, 3, 10) != EINVAL) {
      Fail("posix_memalign(3)", "did not return EINVAL");
    }
    if (aligned_alloc != nullptr) {
      void* const al = aligned_alloc(256, 512);
      Check("aligned_alloc(256, 512)", al);
      if ((reinterpret_cast<uintptr_t>(al) & 255u) != 0) {
        Fail("aligned_alloc", "misaligned");
      }
    } else {
      printf("probe aligned_alloc: not available\n");
    }
    void* const tiny = malloc(5);
    if (malloc_usable_size(tiny) < 5) {
      Fail("malloc_usable_size", "smaller than requested");
    }
    if ((reinterpret_cast<uintptr_t>(tiny) & 15u) != 0) {
      Fail("malloc", "not 16-byte aligned");
    }
    Check("new int", new int(5));
    Check("new char[1 MB]", new char[1u << 20]);
    std::string text(200, 'x');
    Check("std::string(200).data()", text.data());
    std::vector<int> numbers(1000);
    Check("std::vector<int>(1000).data()", numbers.data());
    Check("strdup (libc-internal malloc)", strdup("hello"));
    FILE* const file = fopen("/proc/self/stat", "r");
    Check("fopen FILE* (libc-internal)", file);
    if (file != nullptr) {
      fclose(file);
    }
    // /proc/self, not "/": an app's untrusted_app domain may not list the root directory.
    DIR* const dir = opendir("/proc/self");
    Check("opendir DIR* (libc-internal)", dir);
    if (dir != nullptr) {
      closedir(dir);
    }
    try {
      throw std::runtime_error("probe");
    } catch (const std::exception& e) {
      Check("exception object", &e);
    }
  }

  void ProbeImage(int argc, char** argv)
  {
    Check(".text (a function of the library)", reinterpret_cast<const void*>(&ProbeImage));
    Check(".data global", &gData);
    Check(".bss global", &gBss);
    Check(".rodata literal", "literal");
    Check("argv array", argv);
    if (argc > 0) {
      Check("argv[0] string", argv[0]);
    }
    Check("environ array", environ);
    if (const char* const path = getenv("PATH")) {
      Check("getenv(\"PATH\")", path);
    }
    // Informational: what stays high in any case (bionic's own data).
    Check("errno location", &errno, false);
    Check("stdout FILE*", stdout, false);
    Check("libc's pthread_self (struct)", reinterpret_cast<const void*>(pthread_self()), false);
  }

  void ProbeThreads()
  {
    int local = 0;
    Check("engine thread: stack local", &local);
    Check("engine thread: thread_local int", &tLocal);
    std::thread worker([] { ReportThread(const_cast<char*>("std::thread")); });
    worker.join();
    pthread_t thread;
    if (pthread_create(&thread, nullptr, ReportThread, const_cast<char*>("pthread default attr")) == 0) {
      pthread_join(thread, nullptr);
    } else {
      Fail("pthread_create", "default attr");
    }
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8u << 20);
    if (pthread_create(&thread, &attr, LargeStackThread, const_cast<char*>("pthread 8 MB stack")) == 0) {
      pthread_join(thread, nullptr);
    } else {
      Fail("pthread_create", "8 MB stack");
    }
    pthread_attr_destroy(&attr);
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&thread, &attr, DetachedThread, const_cast<char*>("pthread detached")) != 0) {
      Fail("pthread_create", "detached");
    }
    pthread_attr_destroy(&attr);
    while (gDetachedDone.load() < 1) {
      usleep(1000);
    }
  }

  // The file the view checks map: the runner executable, or, when its name ends in ".so" (in the APK
  // it is libfafrunner.so, because Android only extracts lib*.so names), a copy of its first 64 KB in
  // $TMPDIR (else the working directory), which the caller deletes. Empty on failure.
  std::string ViewFile(const char* exe, bool* temporary)
  {
    *temporary = false;
    const size_t length = strlen(exe);
    if (length < 3 || strcmp(exe + length - 3, ".so") != 0) {
      return exe;
    }
    const char* const tmp = getenv("TMPDIR");
    const std::string path = std::string(tmp != nullptr && *tmp != '\0' ? tmp : ".") + "/lowarena-probe-view-" +
                             std::to_string(static_cast<long>(getpid())) + ".bin";
    const int in = open(exe, O_RDONLY | O_CLOEXEC);
    const int out = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    bool ok = in >= 0 && out >= 0;
    char buffer[4096];
    for (size_t copied = 0; ok && copied < 65536;) {
      const ssize_t n = read(in, buffer, sizeof(buffer));
      if (n <= 0) {
        ok = n == 0 && copied > 0;
        break;
      }
      ok = write(out, buffer, static_cast<size_t>(n)) == n;
      copied += static_cast<size_t>(n);
    }
    if (in >= 0) {
      close(in);
    }
    if (out >= 0) {
      close(out);
    }
    if (!ok) {
      unlink(path.c_str());
      return std::string();
    }
    *temporary = true;
    return path;
  }

  // `filePath`: a file that is not a shared library (the shim reports views of "*.so" files as
  // MEM_IMAGE, as /proc/self/maps cannot tell them from loaded libraries); see ViewFile.
  void ProbeWin32Memory(const char* filePath)
  {
    // VirtualAlloc reserve+commit, release, again: the arena hands the same range out again.
    void* const first = VirtualAlloc(nullptr, 1u << 20, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    Check("VirtualAlloc(1 MB, reserve+commit)", first);
    if (first == nullptr) {
      Fail("VirtualAlloc", "null");
      return;
    }
    static_cast<volatile char*>(first)[0] = 1;
    static_cast<volatile char*>(first)[(1u << 20) - 1] = 2;
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(first, &info, sizeof(info)) == 0 || info.State != MEM_COMMIT || info.Type != MEM_PRIVATE ||
        info.Protect != PAGE_READWRITE || info.AllocationBase != first) {
      Fail("VirtualQuery(VirtualAlloc block)", "not MEM_COMMIT/MEM_PRIVATE/PAGE_READWRITE at its base");
    }
    if (!VirtualFree(first, 0, MEM_RELEASE)) {
      Fail("VirtualFree", "MEM_RELEASE failed");
    }
    void* const again = VirtualAlloc(nullptr, 1u << 20, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (gArena && again != first) {
      Fail("VirtualAlloc after VirtualFree", "the released range was not reused");
    }
    if (again != nullptr && static_cast<volatile char*>(again)[0] != 0) {
      Fail("VirtualAlloc after VirtualFree", "old contents visible");
    }
    VirtualFree(again, 0, MEM_RELEASE);

    // Reserve, commit one page inside, decommit, release.
    char* const reserved = static_cast<char*>(VirtualAlloc(nullptr, 4u << 20, MEM_RESERVE, PAGE_READWRITE));
    Check("VirtualAlloc(4 MB, reserve only)", reserved);
    if (reserved != nullptr) {
      if (VirtualQuery(reserved, &info, sizeof(info)) == 0 || info.State != MEM_RESERVE) {
        Fail("VirtualQuery(reserved)", "not MEM_RESERVE");
      }
      char* const page = static_cast<char*>(VirtualAlloc(reserved + (1u << 20), 65536, MEM_COMMIT, PAGE_READWRITE));
      if (page != reserved + (1u << 20)) {
        Fail("VirtualAlloc(MEM_COMMIT) inside a reservation", "wrong address");
      } else {
        page[0] = 3;
        if (!VirtualFree(page, 65536, MEM_DECOMMIT)) {
          Fail("VirtualFree", "MEM_DECOMMIT failed");
        }
      }
      if (!VirtualFree(reserved, 0, MEM_RELEASE)) {
        Fail("VirtualFree", "MEM_RELEASE of the reservation failed");
      }
    }

    // MapViewOfFile of the executable, twice (the view's range is reused after UnmapViewOfFile).
    HANDLE const file = CreateFileA(filePath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
      Fail("CreateFileA", filePath);
      return;
    }
    HANDLE const mapping = CreateFileMappingA(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping == nullptr) {
      Fail("CreateFileMappingA", "null");
      CloseHandle(file);
      return;
    }
    const void* const view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    Check("MapViewOfFile (the runner executable)", view);
    if (view == nullptr || memcmp(view, "\x7f" "ELF", 4) != 0) {
      Fail("MapViewOfFile", "no ELF header in the view");
    } else {
      if (VirtualQuery(view, &info, sizeof(info)) == 0 || info.Type != MEM_MAPPED || info.State != MEM_COMMIT) {
        Fail("VirtualQuery(view)", "not MEM_MAPPED/MEM_COMMIT");
      }
      if (!UnmapViewOfFile(view)) {
        Fail("UnmapViewOfFile", "failed");
      }
      const void* const view2 = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
      if (gArena && view2 != view) {
        Fail("MapViewOfFile after UnmapViewOfFile", "the released range was not reused");
      }
      if (view2 == nullptr || memcmp(view2, "\x7f" "ELF", 4) != 0) {
        Fail("MapViewOfFile again", "no ELF header");
      }
      UnmapViewOfFile(view2);
    }
    CloseHandle(mapping);
    CloseHandle(file);
  }

  // Bionic's own malloc (libc.so's definition, not the executable's): what free() and realloc() get
  // when a pointer was not handed out by the arena.
  void ProbeForeign()
  {
    void* const libc = dlopen("libc.so", RTLD_NOW | RTLD_NOLOAD);
    using MallocFn = void* (*)(size_t);
    const MallocFn libcMalloc = libc != nullptr ? reinterpret_cast<MallocFn>(dlsym(libc, "malloc")) : nullptr;
    if (libcMalloc == nullptr) {
      Fail("dlsym(libc.so, malloc)", dlerror());
      return;
    }
    if (reinterpret_cast<void*>(libcMalloc) == reinterpret_cast<void*>(&malloc) && gArena) {
      Fail("libc.so's malloc", "is the executable's (dlsym did not reach libc's definition)");
    }
    void* const foreign = libcMalloc(100);
    Check("libc.so's own malloc(100) (foreign)", foreign, false);
    if (foreign == nullptr) {
      Fail("libc.so's malloc(100)", "null");
      return;
    }
    memset(foreign, 0x5a, 100);
    lowarena_stats before{};
    if (lowarena_get_stats != nullptr) {
      lowarena_get_stats(&before);
    }
    if (malloc_usable_size(foreign) < 100) {
      Fail("malloc_usable_size(foreign)", "smaller than requested");
    }
    // realloc moves a foreign block into the arena (release 0.4.0): low, contents kept, libc's block
    // freed through libc. With the arena off it is bionic's realloc and stays high.
    unsigned char* const moved = static_cast<unsigned char*>(realloc(foreign, 200));
    Check("realloc(foreign, 200)", moved, false);
    if (moved == nullptr) {
      Fail("realloc(foreign, 200)", "null");
      return;
    }
    for (int i = 0; i < 100; ++i) {
      if (moved[i] != 0x5a) {
        Fail("realloc(foreign, 200)", "the first 100 bytes were not kept");
        break;
      }
    }
    const uintptr_t at = reinterpret_cast<uintptr_t>(moved);
    if (gArena && !(at >= LOWARENA_LOW && at < LOWARENA_HIGH)) {
      Fail("realloc(foreign, 200)", "the block was not moved into the arena");
    }
    void* const other = libcMalloc(64);
    free(other);  // a foreign free: forwarded to bionic
    free(moved);  // the arena's block now (or bionic's with the arena off)
    if (lowarena_get_stats != nullptr && gArena) {
      lowarena_stats after{};
      lowarena_get_stats(&after);
      if (after.foreign_frees != before.foreign_frees + 1 || after.foreign_reallocs != before.foreign_reallocs + 1) {
        Fail("foreign pointers", "free/realloc of libc's blocks were not counted");
      }
    }
  }

  // Four threads allocate, check, reallocate and free blocks of mixed sizes and alignments through a
  // shared slot table, so many blocks are freed or reallocated on another thread than the one that
  // allocated them. A block holds its size in its first 8 bytes and a fill byte everywhere else.
  bool BlockIntact(const unsigned char* block)
  {
    size_t size = 0;
    memcpy(&size, block, sizeof(size));
    const unsigned char fill = block[sizeof(size)];
    return size >= 16 && block[size - 1] == fill && block[size / 2] == fill;
  }

  void FillBlock(unsigned char* block, const size_t size, const unsigned char fill)
  {
    memset(block, fill, size);
    memcpy(block, &size, sizeof(size));
  }

  void ProbeStress()
  {
    constexpr int kThreads = 4;
    constexpr int kSlots = 4096;
    constexpr int kOps = 60000;
    static std::atomic<unsigned char*> shared[kSlots];
    std::atomic<int> errors{0};
    std::atomic<int> outside{0};
    auto worker = [&](const unsigned seed) {
      unsigned state = seed * 2654435761u + 1u;
      auto next = [&state] {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
      };
      for (int op = 0; op < kOps; ++op) {
        const unsigned r = next();
        std::atomic<unsigned char*>& slot = shared[r % kSlots];
        unsigned char* const old = slot.exchange(nullptr);
        if (old != nullptr) {
          if (!BlockIntact(old)) {
            errors.fetch_add(1);
          }
          if ((r >> 8) % 3 == 0) {
            free(old);
            continue;
          }
        }
        size_t size = 16 + (next() % 4096);
        if ((r >> 12) % 64 == 0) {
          size = (1u << 20) + next() % (3u << 20);
        }
        const unsigned char fill = static_cast<unsigned char>(next());
        unsigned char* block = nullptr;
        const unsigned how = (r >> 20) % 4;
        if (old != nullptr && how == 0) {
          size_t oldSize = 0;
          memcpy(&oldSize, old, sizeof(oldSize));
          const unsigned char oldFill = old[sizeof(oldSize)];
          block = static_cast<unsigned char*>(realloc(old, size));
          const size_t kept = oldSize < size ? oldSize : size;
          if (block != nullptr && (memcmp(block, &oldSize, sizeof(oldSize)) != 0 || block[sizeof(oldSize)] != oldFill ||
                                   block[kept - 1] != oldFill)) {
            errors.fetch_add(1);  // realloc must keep the contents up to the smaller size
          }
        } else {
          free(old);
          if (how == 1) {
            const size_t alignment = static_cast<size_t>(16) << (next() % 9);  // 16 .. 4096
            block = static_cast<unsigned char*>(memalign(alignment, size));
            if ((reinterpret_cast<uintptr_t>(block) & (alignment - 1)) != 0) {
              errors.fetch_add(1);
            }
          } else if (how == 2) {
            block = static_cast<unsigned char*>(calloc(1, size));
            if (block != nullptr && (block[0] != 0 || block[size - 1] != 0)) {
              errors.fetch_add(1);
            }
          } else {
            block = static_cast<unsigned char*>(malloc(size));
          }
        }
        if (block == nullptr) {
          errors.fetch_add(1);
          continue;
        }
        if (malloc_usable_size(block) < size) {
          errors.fetch_add(1);
        }
        const uintptr_t address = reinterpret_cast<uintptr_t>(block);
        if (gArena && (address < LOWARENA_LOW || address >= LOWARENA_HIGH)) {
          outside.fetch_add(1);
        }
        FillBlock(block, size, fill);
        unsigned char* const displaced = slot.exchange(block);
        if (displaced != nullptr) {
          free(displaced);  // another thread filled the slot meanwhile
        }
      }
    };
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
      threads.emplace_back(worker, static_cast<unsigned>(t + 1));
    }
    for (std::thread& t : threads) {
      t.join();
    }
    for (std::atomic<unsigned char*>& slot : shared) {
      unsigned char* const block = slot.exchange(nullptr);
      if (block != nullptr && !BlockIntact(block)) {
        errors.fetch_add(1);
      }
      free(block);
    }
    printf("probe allocator stress: %d threads x %d ops, %d errors, %d blocks outside the arena\n", kThreads, kOps,
           errors.load(), outside.load());
    if (errors.load() != 0 || outside.load() != 0) {
      Fail("allocator stress", "errors or blocks outside the arena");
    }
  }

  // Joined and detached threads give their stacks back.
  void ProbeThreadChurn()
  {
    lowarena_stats before{};
    if (lowarena_get_stats != nullptr) {
      lowarena_get_stats(&before);
    }
    for (int i = 0; i < 64; ++i) {
      std::thread([] {}).join();
    }
    gDetachedDone.store(0);
    for (int i = 0; i < 32; ++i) {
      pthread_t thread;
      if (pthread_create(&thread, nullptr, DetachedThread, nullptr) == 0) {
        pthread_detach(thread);
      }
    }
    while (gDetachedDone.load() < 32) {
      usleep(1000);
    }
    usleep(200000);                 // let the detached threads leave the kernel
    std::thread([] {}).join();      // a create/join reaps them
    if (lowarena_get_stats != nullptr) {
      lowarena_stats after{};
      lowarena_get_stats(&after);
      printf("probe thread churn: threads live %u -> %u, created %u -> %u (+%u on bionic stacks), peak %u, "
             "in use %llu -> %llu KB\n",
             before.threads_live, after.threads_live, before.threads_total, after.threads_total,
             after.thread_fallbacks - before.thread_fallbacks, after.threads_peak,
             static_cast<unsigned long long>(before.in_use_bytes >> 10),
             static_cast<unsigned long long>(after.in_use_bytes >> 10));
      // A thread that found no room in the arena (capacity mode) runs on a bionic stack: counted as a
      // fallback instead of a creation.
      const uint32_t created = (after.threads_total + after.thread_fallbacks) - (before.threads_total + before.thread_fallbacks);
      if (gArena && (after.threads_live > before.threads_live || created < 97)) {
        Fail("thread churn", "stacks of joined or detached threads were not returned");
      }
    }
  }

  // `capacity` mode: fill the arena with 16 MB blocks until malloc fails, check that nothing went
  // above the limit (2 GB, or 1.75 GB under the ARM translator), then free everything. The probes
  // that follow run code the translator has not seen yet, so under translation this also shows that
  // its code cache still finds room once the arena has reserved all it may.
  void ProbeCapacity()
  {
    static void* blocks[256];
    size_t count = 0;
    uintptr_t highest = 0;
    while (count < 256) {
      void* const block = malloc(16u << 20);
      if (block == nullptr) {
        break;
      }
      blocks[count++] = block;
      const uintptr_t end = reinterpret_cast<uintptr_t>(block) + (16u << 20);
      highest = end > highest ? end : highest;
    }
    lowarena_stats stats{};
    if (lowarena_get_stats != nullptr) {
      lowarena_get_stats(&stats);
    }
    printf("probe capacity: %zu blocks of 16 MB (%zu MB) before malloc failed, highest end %#llx, limit %#llx, "
           "reserved %llu MB\n", count, count * 16, static_cast<unsigned long long>(highest),
           static_cast<unsigned long long>(stats.limit_address), static_cast<unsigned long long>(stats.reserved_bytes >> 20));
    if (gArena && (count == 256 || highest > stats.limit_address || count < 32)) {
      Fail("capacity", "the arena did not fill up below its limit");
    }
    for (size_t i = 0; i < count; ++i) {
      free(blocks[i]);
    }
  }

  void PrintLowMaps()
  {
    FILE* const maps = fopen("/proc/self/maps", "r");
    if (maps == nullptr) {
      return;
    }
    char line[512];
    int low = 0, total = 0;
    printf("probe --- /proc/self/maps below 4 GB ---\n");
    while (fgets(line, sizeof(line), maps) != nullptr) {
      unsigned long long lo = 0;
      sscanf(line, "%llx", &lo);
      ++total;
      if (lo < kFourGB) {
        ++low;
        printf("probe maps %s", line);
      }
    }
    fclose(maps);
    printf("probe --- %d of %d mappings below 4 GB ---\n", low, total);
  }
} // namespace

extern "C" __attribute__((visibility("default"))) int faf_headless_main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IOLBF, 0);
  gArena = lowarena_enabled != nullptr && lowarena_enabled() != 0;
  printf("probe LowArena probe: %s, arena %s, sizeof(void*)=%zu, page %ld\n",
#if defined(__aarch64__)
         "arm64",
#elif defined(__x86_64__)
         "x86_64",
#else
         "other",
#endif
         gArena ? "on" : "off (FAF_LOWARENA=0 or no runner)", sizeof(void*), sysconf(_SC_PAGESIZE));
  char exe[512] = {};
  if (readlink("/proc/self/exe", exe, sizeof(exe) - 1) <= 0) {
    strcpy(exe, "/proc/self/exe");
  }

  bool capacity = false;
  for (int i = 1; i < argc; ++i) {
    capacity = capacity || strcmp(argv[i], "capacity") == 0;
  }
  if (capacity) {
    ProbeCapacity();
  }
  ProbeImage(argc, argv);
  ProbeHeap();
  ProbeThreads();
  bool viewTemporary = false;
  const std::string viewFile = ViewFile(exe, &viewTemporary);
  if (viewFile.empty()) {
    Fail("ViewFile", "cannot copy the runner executable for the view checks");
  } else {
    if (viewTemporary) {
      printf("probe view checks map %s (a copy of %s's first 64 KB)\n", viewFile.c_str(), exe);
    }
    ProbeWin32Memory(viewFile.c_str());
    if (viewTemporary) {
      unlink(viewFile.c_str());
    }
  }
  ProbeForeign();
  ProbeStress();
  ProbeThreadChurn();
  if (!gArena) {
    // FAF_LOWARENA=0: the heap must be bionic's allocator (Scudo names its mappings).
    void* const block = malloc(48);
    const std::string name = MapName(block);
    printf("probe malloc(48) with the arena off lies in \"%s\"\n", name.c_str());
    if (name.find("scudo") == std::string::npos) {
      Fail("FAF_LOWARENA=0", "malloc(48) is not in a Scudo mapping");
    }
    free(block);
  }
  PrintLowMaps();
  if (lowarena_report != nullptr) {
    lowarena_report(1);
  }
  printf("probe RESULT %s: %d of %d placement checks, %d failures\n", gFailures == 0 ? "PASS" : "FAIL", gChecks,
         gChecks, gFailures);
  return gFailures > 100 ? 100 : gFailures;
}
