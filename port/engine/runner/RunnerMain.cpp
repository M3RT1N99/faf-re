// Entry of the Android headless replay runner (docs/port/android-roadmap.md, M3b). Port code, never
// part of main.exe; scripts/port/build_runner.py compiles this file twice:
//
//  - into libfafengine.so (the default): `faf_headless_main`, the engine side of the entry. It plays
//    the role WinMain plays on Windows for `/headlessreplay` (moho/app/WinMain.cpp: the command line
//    is in __argc/__argv before anything reads it, then HEADLESS_RunReplay), and defines the two
//    CRT globals the engine reads its command line from (CFG_GetArgOption, StartupHelpers.cpp:65).
//  - into faf_headless_runner with FAF_RUNNER_EXECUTABLE: `main`, which loads libfafengine.so from
//    the executable's own directory (or $FAF_ENGINE_LIB) and calls `faf_headless_main`. The engine
//    stays a shared library so that M3c can load it at a low address (android_dlopen_ext into a
//    reserved range, the counterpart of x64's /LARGEADDRESSAWARE:NO) without relinking it.
//
// The command line is the Windows runner's, unchanged (docs/port/headless-replay.md):
//   faf_headless_runner /headlessreplay <file.scfareplay> /init <init_faf.lua> [/log <file>] ...
// The leading /headlessreplay is optional here: the runner has no other mode, so it is added when
// missing, and the engine sees the same argument list as main.exe.

#if defined(FAF_RUNNER_EXECUTABLE)

#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>

namespace
{
  using EntryFn = int (*)(int, char**);

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
} // namespace

int main(int argc, char** argv)
{
  const std::string library = EngineLibraryPath(argc > 0 ? argv[0] : ".");
  void* const handle = dlopen(library.c_str(), RTLD_NOW | RTLD_GLOBAL);
  if (handle == nullptr) {
    fprintf(stderr, "faf_headless_runner: cannot load %s: %s\n", library.c_str(), dlerror());
    return 1;
  }
  const auto entry = reinterpret_cast<EntryFn>(dlsym(handle, "faf_headless_main"));
  if (entry == nullptr) {
    fprintf(stderr, "faf_headless_runner: %s has no faf_headless_main: %s\n", library.c_str(), dlerror());
    return 1;
  }
  return entry(argc, argv);
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
