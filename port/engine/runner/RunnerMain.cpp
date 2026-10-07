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
//  - joins that thread, prints the arena's report ("[lowarena] ..." on stderr) and ends the process
//    with that exit code: _exit after flushing, no static destructors (RunnerExit.cpp, 0.4.1).
// With FAF_LOWARENA=0 the same steps run on bionic's allocator, a bionic 4 MB thread stack and a plain
// dlopen (the M3d truncation oracle). See port/engine/lowarena/README.md.
//
// The command line is the Windows runner's, unchanged (docs/port/headless-replay.md):
//   faf_headless_runner /headlessreplay <file.scfareplay> /init <init_faf.lua> [/log <file>] ...
// The leading /headlessreplay is optional here: the engine side has no other mode, so it is added
// when missing, and the engine sees the same argument list as main.exe.
//
// Release 0.4.0 (the in-app replay test) adds, in the executable only (the engine is unchanged):
//  - the replay file may be a .fafreplay as FAF's vault or a browser download has it (or a
//    .scfareplay of another version): it is decoded (zstd or the legacy base64+zlib body,
//    ReplayFile.cpp) in a child process into
//    <directory of /headlesssummary, else of /log, else .>/<lower-case name>.scfareplay with the
//    version rewritten to 3764 (scripts/perf/convert_replay.py --as-version 3764), and the engine gets
//    that path. One "[runner] input {...}" JSON line on stdout says what was read;
//  - `faf_headless_runner /replayinfo <file>` prints one JSON object about a replay (id, map,
//    featured mod and version, players, game time, sha256, decoded size, header version, beats, has
//    EndGame, ...) and exits 0, or 1 when the file cannot be decoded; no engine is loaded;
//  - `faf_headless_runner /convertreplay <in> <out>` writes the .scfareplay the engine reads and
//    prints the same JSON object (plus "output");
//  - a crash reporter for fatal signals (RunnerCrash.cpp): "[runner] CRASH ..." lines with offsets in
//    libfafengine.so and its build id on stderr, then the platform's handler as before.

#if defined(FAF_RUNNER_EXECUTABLE)

#include <android/dlext.h>
#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>

#include "../lowarena/LowArena.h"
#include "ReplayFile.h"
#include "RunnerCrash.h"
#include "RunnerExit.h"

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
      faf_runner::CrashHandlerSetEngine(where.dli_fbase, path);
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

  // ------------------------------------------------------------------------------------------------
  // Replay input (release 0.4.0)
  // ------------------------------------------------------------------------------------------------

  void WriteAll(const int fd, const char* text, size_t length)
  {
    while (length > 0) {
      const ssize_t n = write(fd, text, length);
      if (n < 0 && errno == EINTR) {
        continue;
      }
      if (n <= 0) {
        return;
      }
      text += n;
      length -= static_cast<size_t>(n);
    }
  }

  // A line from up to three pieces with write(2): no stdio buffer and no malloc, so the engine's
  // arena starts from the same state as without the message.
  void Say(const int fd, const char* a, const char* b = "", const char* c = "")
  {
    WriteAll(fd, a, strlen(a));
    WriteAll(fd, b, strlen(b));
    WriteAll(fd, c, strlen(c));
    WriteAll(fd, "\n", 1);
  }

  // The value after `option` (matched ignoring case, as the engine's CFG_GetArgOption), or null.
  const char* OptionValue(const int argc, char** const argv, const char* const option)
  {
    for (int i = 1; i + 1 < argc; ++i) {
      if (strcasecmp(argv[i], option) == 0) {
        return argv[i + 1];
      }
    }
    return nullptr;
  }

  int ReplayInfoMode(const int argc, char** const argv)
  {
    if (argc < 3) {
      fprintf(stderr, "usage: %s /replayinfo <file.fafreplay|file.scfareplay>\n", argv[0]);
      return 1;
    }
    faf_runner::ReplayInput input;
    faf_runner::ReadReplay(argv[2], "3764", input);
    const std::string json = faf_runner::ReplayInfoJson(input, nullptr);
    printf("%s\n", json.c_str());
    fflush(stdout);
    return input.error.empty() ? 0 : 1;
  }

  int ConvertReplayMode(const int argc, char** const argv)
  {
    if (argc < 4) {
      fprintf(stderr, "usage: %s /convertreplay <in.fafreplay> <out.scfareplay>\n", argv[0]);
      return 1;
    }
    faf_runner::ReplayInput input;
    std::string error;
    if (faf_runner::ReadReplay(argv[2], "3764", input) && !faf_runner::WriteFileAtomic(argv[3], input.data, error)) {
      input.error = error;
    }
    const std::string json = faf_runner::ReplayInfoJson(input, argv[3]);
    printf("%s\n", json.c_str());
    fflush(stdout);
    return input.error.empty() ? 0 : 1;
  }

  char gConvertedReplay[PATH_MAX];

  // `/headlessreplay <file>` with a file that is not a 3764 .scfareplay (a .fafreplay, or a
  // .scfareplay recorded with another version): decode and convert it into
  // <directory of /headlesssummary, else of /log, else .>/<lower-case name>.scfareplay (the engine
  // lower-cases the paths it opens) and point the argument there. The decoding runs in a child
  // process, so its allocations never touch this process's heap; this side allocates nothing.
  // Returns 0 to go on, 1 when the file could not be converted.
  int PrepareReplayArgument(const int argc, char** const argv)
  {
    int index = -1;
    bool flagged = false;
    for (int i = 1; i < argc; ++i) {
      if (strcasecmp(argv[i], "/headlessreplay") == 0) {
        flagged = true;
        index = i + 1 < argc ? i + 1 : -1;
        break;
      }
    }
    if (!flagged && argc > 1) {
      index = 1;  // faf_headless_main puts /headlessreplay in front of it
    }
    if (index < 0 || access(argv[index], R_OK) != 0 || faf_runner::IsEngineReadyReplay(argv[index])) {
      return 0;  // a converted replay, or nothing the runner can read (the engine reports that)
    }
    const char* const source = argv[index];

    // The directory: where the summary (or the log) goes, i.e. the run directory.
    char dir[PATH_MAX] = ".";
    const char* anchor = OptionValue(argc, argv, "/headlesssummary");
    if (anchor == nullptr) {
      anchor = OptionValue(argc, argv, "/log");
    }
    if (anchor != nullptr) {
      const char* const slash = strrchr(anchor, '/');
      if (slash == anchor) {
        strlcpy(dir, "/", sizeof(dir));
      } else if (slash != nullptr && static_cast<size_t>(slash - anchor) < sizeof(dir)) {
        memcpy(dir, anchor, static_cast<size_t>(slash - anchor));
        dir[slash - anchor] = '\0';
      }
    }
    // The name: the source's, without its extension, lower case, [a-z0-9._-] only.
    const char* base = strrchr(source, '/');
    base = base != nullptr ? base + 1 : source;
    char stem[256] = {};
    size_t length = 0;
    for (const char* p = base; *p != '\0' && length + 1 < sizeof(stem); ++p) {
      char c = *p;
      c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
      const bool keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
      stem[length++] = keep ? c : '_';
    }
    stem[length] = '\0';
    if (char* const dot = strrchr(stem, '.'); dot != nullptr && dot != stem) {
      *dot = '\0';
    }
    if (stem[0] == '\0') {
      strlcpy(stem, "replay", sizeof(stem));
    }
    gConvertedReplay[0] = '\0';
    strlcat(gConvertedReplay, dir, sizeof(gConvertedReplay));
    strlcat(gConvertedReplay, "/", sizeof(gConvertedReplay));
    strlcat(gConvertedReplay, stem, sizeof(gConvertedReplay));
    // Never the source itself (a .scfareplay-named file that is not decoded yet).
    char sourceName[256] = {};
    strlcpy(sourceName, base, sizeof(sourceName));
    char candidate[256] = {};
    strlcpy(candidate, stem, sizeof(candidate));
    strlcat(candidate, ".scfareplay", sizeof(candidate));
    if (strcmp(candidate, sourceName) == 0) {
      strlcat(gConvertedReplay, ".v3764", sizeof(gConvertedReplay));
    }
    if (strlcat(gConvertedReplay, ".scfareplay", sizeof(gConvertedReplay)) >= sizeof(gConvertedReplay)) {
      Say(2, "[runner] the converted replay's path is too long: ", source);
      return 1;
    }

    fflush(stdout);
    fflush(stderr);
    const pid_t child = fork();
    if (child < 0) {
      Say(2, "[runner] cannot fork to convert the replay: ", strerror(errno));
      return 1;
    }
    if (child == 0) {
      faf_runner::ReplayInput input;
      std::string error;
      if (faf_runner::ReadReplay(source, "3764", input) &&
          !faf_runner::WriteFileAtomic(gConvertedReplay, input.data, error)) {
        input.error = error;
      }
      const std::string line = "[runner] input " + faf_runner::ReplayInfoJson(input, gConvertedReplay) + "\n";
      WriteAll(1, line.data(), line.size());
      if (!input.error.empty()) {
        const std::string why = "[runner] cannot convert " + std::string(source) + ": " + input.error + "\n";
        WriteAll(1, why.data(), why.size());
      }
      _exit(input.error.empty() ? 0 : 1);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
      if (errno != EINTR) {
        Say(2, "[runner] lost the replay conversion process: ", strerror(errno));
        return 1;
      }
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      Say(2, "[runner] the replay could not be converted: ", source);
      return 1;
    }
    argv[index] = gConvertedReplay;
    return 0;
  }
} // namespace

int main(int argc, char** argv)
{
  faf_runner::CrashHandlerInstall(argc > 0 ? argv[0] : nullptr);
  if (argc >= 2 && strcasecmp(argv[1], "/replayinfo") == 0) {
    return ReplayInfoMode(argc, argv);
  }
  if (argc >= 2 && strcasecmp(argv[1], "/convertreplay") == 0) {
    return ConvertReplayMode(argc, argv);
  }
  if (PrepareReplayArgument(argc, argv) != 0) {
    return 1;
  }

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
  return faf_runner::EndProcess(run.exitCode);  // returns only with FAF_RUNNER_EXIT=full
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
