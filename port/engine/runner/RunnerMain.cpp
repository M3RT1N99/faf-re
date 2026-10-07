// Entry of the Android headless replay runner (docs/port/android-roadmap.md, M3b). Port code, never
// part of main.exe; scripts/port/build_runner.py compiles this file twice:
//
//  - into libfafengine.so (the default): `faf_headless_main`, the engine side of the entry. It plays
//    the role WinMain plays on Windows for `/headlessreplay` (moho/app/WinMain.cpp: the command line
//    is in __argc/__argv before anything reads it, then HEADLESS_RunReplay), and defines the two
//    CRT globals the engine reads its command line from (CFG_GetArgOption, StartupHelpers.cpp:65).
//  - into faf_headless_runner with FAF_RUNNER_EXECUTABLE: `main`, which loads libfafengine.so from
//    the executable's own directory (or $FAF_ENGINE_LIB) and calls `faf_headless_main`. The engine
//    stays a shared library so that it can be loaded at a low address without relinking it.
//
// M3c: the executable also carries the low arena (port/engine/lowarena, linked in by build_runner.py),
// the Android counterpart of x64's /LARGEADDRESSAWARE:NO: its malloc family replaces bionic's for the
// whole process, and `main`
//  - copies argv (and the environment) into the arena, so the engine's __argv and getenv() strings
//    are low too;
//  - starts one thread on a 4 MB arena stack (main.exe's StackReserveSize is 4000000) and does
//    everything else there, because the main thread's stack is always high: it reserves a range
//    for libfafengine.so and loads it there (android_dlopen_ext, ANDROID_DLEXT_RESERVED_ADDRESS), so
//    the library's static initialisers already run on the low stack, then calls `faf_headless_main`;
//  - joins that thread, prints the arena's report ("[lowarena] ..." on stderr) and returns the exit
//    code.
// With FAF_LOWARENA=0 the same steps run on bionic's allocator, a bionic 4 MB thread stack and a plain
// dlopen (the M3d truncation oracle). See port/engine/lowarena/README.md.
//
// The command line is the Windows runner's, unchanged (docs/port/headless-replay.md):
//   faf_headless_runner /headlessreplay <file.scfareplay> /init <init_faf.lua> [/log <file>] ...
// The leading /headlessreplay is optional here: the runner has no other mode, so it is added when
// missing, and the engine sees the same argument list as main.exe.

#if defined(FAF_RUNNER_EXECUTABLE)

#include <android/dlext.h>
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <string>

#include "../lowarena/LowArena.h"

namespace
{
  using EntryFn = int (*)(int, char**);

  constexpr size_t kEngineStackSize = 0x00400000u;  // 4 MB, as main.exe's StackReserveSize 4000000

  std::string EngineLibraryPath(const char* const argv0)
  {
    if (const char* const env = getenv("FAF_ENGINE_LIB"); env != nullptr && env[0] != '\0') {
      return env;
    }
    char self[PATH_MAX] = {};
    const ssize_t length = readlink("/proc/self/exe", self, sizeof(self) - 1);
    std::string dir = (length > 0) ? std::string(self, static_cast<size_t>(length)) : std::string(argv0);
    const size_t slash = dir.rfind('/');
    dir = (slash == std::string::npos) ? std::string(".") : dir.substr(0, slash);
    return dir + "/libfafengine.so";
  }

  // The address span the library's PT_LOAD segments cover (what the dynamic linker maps), from its
  // ELF headers; 0 when the file cannot be read as a 64-bit ELF.
  size_t LoadSpan(const char* const path)
  {
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
      return 0;
    }
    size_t span = 0;
    Elf64_Ehdr header{};
    if (pread(fd, &header, sizeof(header), 0) == static_cast<ssize_t>(sizeof(header)) &&
        memcmp(header.e_ident, ELFMAG, SELFMAG) == 0 && header.e_ident[EI_CLASS] == ELFCLASS64 &&
        header.e_phentsize == sizeof(Elf64_Phdr) && header.e_phnum > 0 && header.e_phnum <= 64) {
      Elf64_Phdr segments[64];
      const size_t bytes = sizeof(Elf64_Phdr) * header.e_phnum;
      if (pread(fd, segments, bytes, static_cast<off_t>(header.e_phoff)) == static_cast<ssize_t>(bytes)) {
        uint64_t low = UINT64_MAX;
        uint64_t high = 0;
        for (unsigned i = 0; i < header.e_phnum; ++i) {
          if (segments[i].p_type != PT_LOAD) {
            continue;
          }
          // 64 KB granules cover every page size the linker may round to.
          const uint64_t start = segments[i].p_vaddr & ~static_cast<uint64_t>(0xFFFFu);
          const uint64_t end = (segments[i].p_vaddr + segments[i].p_memsz + 0xFFFFu) & ~static_cast<uint64_t>(0xFFFFu);
          low = start < low ? start : low;
          high = end > high ? end : high;
        }
        span = high > low ? static_cast<size_t>(high - low) : 0;
      }
    }
    close(fd);
    return span;
  }

  // A copy of an argv-like array (count entries and the terminating null) in malloc'd memory, which
  // is the arena's when it is on.
  char** CopyStrings(const int count, char* const* const strings)
  {
    char** const copy = static_cast<char**>(malloc(sizeof(char*) * (static_cast<size_t>(count) + 1u)));
    if (copy == nullptr) {
      return nullptr;
    }
    for (int i = 0; i < count; ++i) {
      copy[i] = strdup(strings[i]);
      if (copy[i] == nullptr) {
        return nullptr;
      }
    }
    copy[count] = nullptr;
    return copy;
  }

  struct EngineRun
  {
    std::string library;
    int argc = 0;
    char** argv = nullptr;
    bool arena = false;
    int exitCode = 1;
  };

  // Runs on the engine thread: load the library (static initialisers included), call its entry.
  void* EngineThread(void* const raw)
  {
    EngineRun* const run = static_cast<EngineRun*>(raw);
    const char* const path = run->library.c_str();
    void* handle = nullptr;
    if (run->arena) {
      const size_t span = LoadSpan(path);
      if (span == 0) {
        fprintf(stderr, "faf_headless_runner: cannot read the ELF headers of %s\n", path);
        return nullptr;
      }
      // Slack for the linker's own alignment of the first segment, rounded to 16 MB.
      const size_t reserve = (span + 0x00100000u + 0x00FFFFFFu) & ~static_cast<size_t>(0x00FFFFFFu);
      void* const range = lowarena_reserve(reserve, PROT_NONE, LOWARENA_KIND_IMAGE);
      if (range == nullptr) {
        fprintf(stderr, "faf_headless_runner: no room in the low arena for %s (%zu bytes)\n", path, reserve);
        return nullptr;
      }
      android_dlextinfo info{};
      info.flags = ANDROID_DLEXT_RESERVED_ADDRESS;
      info.reserved_addr = range;
      info.reserved_size = reserve;
      handle = android_dlopen_ext(path, RTLD_NOW | RTLD_GLOBAL, &info);
    } else {
      handle = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    }
    if (handle == nullptr) {
      fprintf(stderr, "faf_headless_runner: cannot load %s: %s\n", path, dlerror());
      return nullptr;
    }
    const auto entry = reinterpret_cast<EntryFn>(dlsym(handle, "faf_headless_main"));
    if (entry == nullptr) {
      fprintf(stderr, "faf_headless_runner: %s has no faf_headless_main: %s\n", path, dlerror());
      return nullptr;
    }
    Dl_info where{};
    int stackProbe = 0;
    if (dladdr(reinterpret_cast<void*>(entry), &where) != 0) {
      fprintf(stderr, "[lowarena] %s at %p, engine thread stack near %p%s\n", path, where.dli_fbase,
              static_cast<void*>(&stackProbe), run->arena ? "" : " (FAF_LOWARENA=0)");
    }
    run->exitCode = entry(run->argc, run->argv);
    return nullptr;
  }

  // The arena's report, once: after the engine thread has returned, or at exit() if the engine ends
  // the process itself.
  void ReportOnce()
  {
    static int reported = 0;
    if (__atomic_exchange_n(&reported, 1, __ATOMIC_ACQ_REL) == 0) {
      fflush(stdout);
      lowarena_report(2);
    }
  }
} // namespace

int main(int argc, char** argv)
{
  EngineRun run;
  run.arena = lowarena_enabled() != 0;
  run.library = EngineLibraryPath(argc > 0 ? argv[0] : ".");
  run.argc = argc;
  run.argv = CopyStrings(argc, argv);
  if (run.argv == nullptr) {
    fprintf(stderr, "faf_headless_runner: out of memory copying the command line\n");
    return 1;
  }
  if (run.arena && environ != nullptr) {
    int count = 0;
    while (environ[count] != nullptr) {
      ++count;
    }
    if (char** const copy = CopyStrings(count, environ)) {
      environ = copy;  // getenv() now hands out arena strings; no other thread runs yet
    }
  }

  atexit(&ReportOnce);

  pthread_attr_t attributes;
  pthread_attr_init(&attributes);
  pthread_attr_setstacksize(&attributes, kEngineStackSize);
  pthread_t thread{};
  const int created = lowarena_thread_create(&pthread_create, &thread, &attributes, &EngineThread, &run);
  pthread_attr_destroy(&attributes);
  if (created != 0) {
    fprintf(stderr, "faf_headless_runner: cannot start the engine thread: %s\n", strerror(created));
    return 1;
  }
  const uintptr_t token = lowarena_thread_join_begin(thread);
  const int joined = pthread_join(thread, nullptr);
  lowarena_thread_join_end(token, joined);
  ReportOnce();
  return run.exitCode;
}

#else // the engine side, linked into libfafengine.so

#include <cstring>
#include <vector>

#include "moho/app/HeadlessReplay.h"

// The MSVC CRT's command line (stdlib.h: __argc/__argv), which the engine declares itself
// (StartupHelpers.cpp:65, IWinApp.cpp:22) and reads through CFG_GetArgOption. Bionic has no such
// globals; the runner owns them.
int __argc = 0;
char** __argv = nullptr;

extern "C" __attribute__((visibility("default"))) int faf_headless_main(int argc, char** argv)
{
  // Same argument list main.exe gets: program name, then /headlessreplay and the rest. The vector
  // lives as long as the process (the engine keeps reading __argv), hence static.
  static std::vector<char*> args;
  args.clear();
  args.push_back((argc > 0 && argv[0] != nullptr) ? argv[0] : const_cast<char*>("faf_headless_runner"));
  bool hasFlag = false;
  for (int i = 1; i < argc; ++i) {
    hasFlag = hasFlag || std::strcmp(argv[i], "/headlessreplay") == 0;
  }
  if (!hasFlag) {
    args.push_back(const_cast<char*>("/headlessreplay"));
  }
  for (int i = 1; i < argc; ++i) {
    args.push_back(argv[i]);
  }
  args.push_back(nullptr);
  __argc = static_cast<int>(args.size()) - 1;
  __argv = args.data();
  return moho::HEADLESS_RunReplay();
}

#endif
