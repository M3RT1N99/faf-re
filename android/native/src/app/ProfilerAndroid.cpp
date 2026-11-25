// The sim thread's sampling profiler, on in this test build (android/native/README.md, The sim's profile): a phone has no
// adb, so the profile is the app's own and travels in the logs' zips beside the report. It needs no perf_event and no root.
//
// - A timer on the sim thread's CPU time signals that thread 200 times a second of it (include/SimSampler.h): the signal's
//   handler takes the interrupted PC, the link register and the return addresses the frame pointers chain (the NDK's clang
//   keeps them for all but leaf functions, -mframe-pointer=non-leaf) into a lock-free ring. A new sim's first beat gives the
//   timer to its thread (profiler::SimBeat): each sim has a thread of its own, a game's and each one a replay's move back
//   makes.
// - A thread of low priority empties the ring every 100 ms and counts the samples by module and offset from the module's
//   load bias (include/SimProfile.h), a system library's frames with their dynamic symbols, and writes profile.txt beside
//   the report every 60 s while samples come, once a sim's thread is gone (a game's end, or a move back's old sim) and as
//   the program ends: libfafgame.so's build id, the rate, the samples and ticks, what the sampling cost, then the top
//   leaves and stacks. android/tools/sim_profile.py symbolizes it against the release's unstripped library.
// - Any failure turns the profiler off with one line in the report, and the game goes on: the handler reads nothing but its
//   registers and the sim thread's own stack.

#include "ProfilerAndroid.h"

#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <pthread.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <SimProfile.h>
#include <SimSampler.h>

#include "CoresAndroid.h"
#include "GameAndroid.h"

namespace
{
  using platform::android::Report;

  constexpr int kHertz = 200;
  constexpr long kIntervalNanos = 1'000'000'000L / kHertz;
  /// How often the counting thread empties the ring and looks whether the sim's thread is still there.
  constexpr auto kCountPeriod = std::chrono::milliseconds(100);
  /// How often profile.txt is written while samples come.
  constexpr std::uint64_t kWriteNanos = 60ULL * 1'000'000'000ULL;
  /// How soon the modules are looked up again for an address in none of them, at most.
  constexpr std::uint64_t kModulesAgainNanos = 10ULL * 1'000'000'000ULL;
  /// The leaves and the stacks profile.txt lists.
  constexpr std::size_t kListed = 2000;
  /// The counting thread's nice value: it gives way to the game's threads.
  constexpr int kCountingNice = 10;

  constexpr const char* kAbi =
#if defined(__aarch64__)
    "arm64-v8a";
#else
    "x86_64";
#endif

  /// Set once the profiler stopped; the report has said why.
  std::atomic<bool> gOff{false};

  // The timer, under its lock: a new sim's first beat gives it to that sim's thread (SimBeat), the counting thread stops it
  // once the thread is gone.
  std::mutex gTimerLock;
  timer_t gTimer{};
  bool gTimerSet = false;

  void StopTimerLocked()
  {
    if (gTimerSet) {
      simsampler::Stop(gTimer);
      gTimerSet = false;
    }
  }

  /// The report's one line of why the profiler stops; the timer stops with it.
  void TurnOff(const std::string& why)
  {
    if (gOff.exchange(true)) {
      return;
    }
    {
      const std::lock_guard<std::mutex> lock(gTimerLock);
      StopTimerLocked();
    }
    Report("profile: " + why + "; the profiler is off");
  }

  // The sims' ticks as their beats tell, for profile.txt's head: those of the sims before this one, and this one's first
  // and last. Written by the sim's thread alone.
  std::atomic<std::uint64_t> gTicksBefore{0};
  std::atomic<std::uint32_t> gFirstTick{0};
  std::atomic<std::uint32_t> gLastTick{0};
  std::atomic<std::uint32_t> gSims{0};

  void NewSim(const std::uint32_t tick)
  {
    if (gSims.load(std::memory_order_relaxed) > 0) {
      gTicksBefore.fetch_add(gLastTick.load(std::memory_order_relaxed) - gFirstTick.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    }
    gFirstTick.store(tick, std::memory_order_relaxed);
    gSims.fetch_add(1, std::memory_order_relaxed);
  }

  [[nodiscard]] std::string_view BaseName(const char* const path)
  {
    const std::string_view text = path != nullptr ? path : "";
    const std::size_t slash = text.rfind('/');
    return slash == std::string_view::npos ? text : text.substr(slash + 1);
  }

  /// The library this code is in, libfafgame.so: its name and its GNU build id, in hex.
  struct Library
  {
    std::string name;
    std::string buildId;
  };

  [[nodiscard]] std::string BuildIdOf(const dl_phdr_info& info)
  {
    for (ElfW(Half) index = 0; index < info.dlpi_phnum; ++index) {
      const ElfW(Phdr)& header = info.dlpi_phdr[index];
      if (header.p_type != PT_NOTE) {
        continue;
      }
      const auto* note = reinterpret_cast<const std::uint8_t*>(info.dlpi_addr + header.p_vaddr);
      const std::uint8_t* const end = note + header.p_memsz;
      const auto aligned = [](const ElfW(Word) size) { return (static_cast<std::size_t>(size) + 3) & ~std::size_t{3}; };
      while (end - note >= static_cast<std::ptrdiff_t>(sizeof(ElfW(Nhdr)))) {
        const auto* const entry = reinterpret_cast<const ElfW(Nhdr)*>(note);
        const std::uint8_t* const name = note + sizeof(ElfW(Nhdr));
        const std::uint8_t* const description = name + aligned(entry->n_namesz);
        const std::uint8_t* const next = description + aligned(entry->n_descsz);
        if (next > end) {
          break;
        }
        if (entry->n_type == NT_GNU_BUILD_ID && entry->n_namesz == 4 && std::memcmp(name, "GNU", 4) == 0) {
          std::string hex;
          for (ElfW(Word) at = 0; at < entry->n_descsz; ++at) {
            char digits[3];
            std::snprintf(digits, sizeof digits, "%02x", description[at]);
            hex += digits;
          }
          return hex;
        }
        note = next;
      }
    }
    return "none";
  }

  [[nodiscard]] Library FindLibrary()
  {
    struct Search
    {
      std::uintptr_t address = reinterpret_cast<std::uintptr_t>(&BuildIdOf);
      Library library;
    } search;
    dl_iterate_phdr(
      [](dl_phdr_info* const info, std::size_t, void* const data) -> int {
        auto& found = *static_cast<Search*>(data);
        for (ElfW(Half) index = 0; index < info->dlpi_phnum; ++index) {
          const ElfW(Phdr)& header = info->dlpi_phdr[index];
          const std::uintptr_t start = info->dlpi_addr + header.p_vaddr;
          if (header.p_type == PT_LOAD && found.address >= start && found.address < start + header.p_memsz) {
            found.library = Library{std::string(BaseName(info->dlpi_name)), BuildIdOf(*info)};
            return 1;
          }
        }
        return 0;
      },
      &search);
    if (search.library.name.empty()) {
      search.library.name = "libfafgame.so";
    }
    return search.library;
  }

  /// What the counting thread keeps, under gCountLock: Finish writes the last file from the program's thread.
  struct Counting
  {
    Library library;
    std::string path;
    simprofile::Profile profile;
    simprofile::Segments segments;
    std::uint32_t libraryModule = simprofile::kUnknown;
    std::uint64_t start = 0;
    std::uint64_t modulesLooked = 0;
    bool modulesKnown = false;
    /// The samples in the file last written, and when.
    std::uint64_t written = 0;
    std::uint64_t writtenAt = 0;
    /// The thread the timer sampled at the last look.
    pid_t watched = 0;
    clockid_t clock{};
    bool hasClock = false;
    bool finished = false;
  };

  std::mutex gCountLock;
  Counting* gCounting = nullptr;

  void LookUpModules(Counting& counting, const std::uint64_t now)
  {
    struct Search
    {
      Counting& counting;
      std::vector<simprofile::Segment> segments;
    } search{counting, {}};
    dl_iterate_phdr(
      [](dl_phdr_info* const info, std::size_t, void* const data) -> int {
        auto& found = *static_cast<Search*>(data);
        const std::string_view base = BaseName(info->dlpi_name);
        const std::uint32_t module = found.counting.profile.Module(base.empty() ? "[main]" : std::string(base));
        for (ElfW(Half) index = 0; index < info->dlpi_phnum; ++index) {
          const ElfW(Phdr)& header = info->dlpi_phdr[index];
          if (header.p_type == PT_LOAD && (header.p_flags & PF_X) != 0) {
            const std::uintptr_t start = info->dlpi_addr + header.p_vaddr;
            found.segments.push_back(simprofile::Segment{start, start + header.p_memsz, info->dlpi_addr, module});
          }
        }
        return 0;
      },
      &search);
    counting.segments.Assign(std::move(search.segments));
    counting.libraryModule = counting.profile.Module(counting.library.name);
    counting.modulesLooked = now;
    counting.modulesKnown = true;
  }

  /// The frame of `address`: its module's and its offset there; the modules are looked up again where an address is in
  /// none of them, at most every 10 s. A system library's frame gets its dynamic symbol once, which the release cannot
  /// symbolize.
  [[nodiscard]] simprofile::Frame Resolve(Counting& counting, const std::uintptr_t address, const std::uint64_t now)
  {
    simprofile::Frame frame;
    if (!counting.segments.Find(address, frame) && (!counting.modulesKnown || now - counting.modulesLooked >= kModulesAgainNanos)) {
      LookUpModules(counting, now);
      (void)counting.segments.Find(address, frame);
    }
    if (frame.module != simprofile::kUnknown && frame.module != counting.libraryModule && !counting.profile.Named(frame)) {
      Dl_info info{};
      std::string name;
      if (dladdr(reinterpret_cast<const void*>(address), &info) != 0 && info.dli_sname != nullptr) {
        char offset[24];
        std::snprintf(offset, sizeof offset, "+0x%llx",
          static_cast<unsigned long long>(address - reinterpret_cast<std::uintptr_t>(info.dli_saddr)));
        name = std::string(info.dli_sname) + offset;
      }
      // An empty name says the module has no symbol there, so that it is not asked again.
      counting.profile.Name(frame, std::move(name));
    }
    return frame;
  }

  void Drain(Counting& counting, const std::uint64_t now)
  {
    simprofile::Sample sample;
    while (simsampler::gState.ring.Pop(sample)) {
      counting.profile.Add(simprofile::StackOf(sample, simsampler::kCallSize,
        [&](const std::uintptr_t address) { return Resolve(counting, address, now); }, &simsampler::StripCode));
    }
  }

  /// profile.txt, whole: a reader never sees half of one. False where it could not be written, with the report's line.
  bool Write(Counting& counting, const std::uint64_t now)
  {
    simprofile::Header header;
    header.buildId = counting.library.buildId;
    header.library = counting.library.name;
    header.abi = kAbi;
    header.hertz = kHertz;
    header.dropped = simsampler::gState.dropped.load(std::memory_order_relaxed);
    header.sims = gSims.load(std::memory_order_relaxed);
    // A new sim's first tick may be read before its last is: such a sim has no ticks yet.
    const std::uint32_t first = gFirstTick.load(std::memory_order_relaxed);
    const std::uint32_t last = gLastTick.load(std::memory_order_relaxed);
    header.ticks = gTicksBefore.load(std::memory_order_relaxed) + (last > first ? last - first : 0U);
    header.handlerSeconds = static_cast<double>(simsampler::gState.handlerNanos.load(std::memory_order_relaxed)) / 1e9;
    if (timespec used{}; counting.hasClock && clock_gettime(counting.clock, &used) == 0) {
      header.countingSeconds = static_cast<double>(used.tv_sec) + static_cast<double>(used.tv_nsec) / 1e9;
    }
    header.seconds = static_cast<double>(now - counting.start) / 1e9;
    const std::string text = counting.profile.Text(header, kListed);

    const std::string part = counting.path + ".part";
    std::FILE* const file = std::fopen(part.c_str(), "w");
    bool written = file != nullptr && std::fwrite(text.data(), 1, text.size(), file) == text.size();
    int error = errno;
    if (file != nullptr && std::fclose(file) != 0 && written) {
      written = false;
      error = errno;
    }
    if (written && std::rename(part.c_str(), counting.path.c_str()) != 0) {
      written = false;
      error = errno;
    }
    if (!written) {
      (void)std::remove(part.c_str());
      TurnOff(std::string("profile.txt could not be written (") + std::strerror(error) + ")");
      return false;
    }
    counting.written = counting.profile.Samples();
    counting.writtenAt = now;
    return true;
  }

  /// A look of the counting thread: the samples since the last, whether the sim's thread is still there, and the file
  /// where it is due. Under gCountLock.
  void Look(Counting& counting, const std::uint64_t now)
  {
    Drain(counting, now);
    const pid_t thread = simsampler::gState.thread.load(std::memory_order_acquire);
    // The timer went to a new sim's thread: the one before ended, a game's or a move back's old sim.
    bool ended = counting.watched != 0 && thread != counting.watched;
    if (thread != 0 && ::syscall(SYS_tgkill, ::getpid(), thread, 0) != 0 && errno == ESRCH) {
      // The sim's thread is gone without a new one: the game ended.
      const std::lock_guard<std::mutex> lock(gTimerLock);
      if (simsampler::gState.thread.load(std::memory_order_relaxed) == thread) {
        StopTimerLocked();
      }
      ended = true;
    }
    counting.watched = simsampler::gState.thread.load(std::memory_order_acquire);
    if (counting.profile.Samples() != counting.written && (ended || now - counting.writtenAt >= kWriteNanos)) {
      (void)Write(counting, now);
    }
  }

  void* Count(void* const data)
  {
    auto& counting = *static_cast<Counting*>(data);
    // On any core, as light as it is, not only on those the program's thread passed on; below the game's threads.
    cores::AnyCpu();
    (void)::setpriority(PRIO_PROCESS, 0, kCountingNice);
    for (;;) {
      std::this_thread::sleep_for(kCountPeriod);
      const std::lock_guard<std::mutex> lock(gCountLock);
      if (counting.finished || gOff.load()) {
        return nullptr;
      }
      Look(counting, simsampler::MonotonicNanos());
    }
  }
} // namespace

void profiler::Install()
{
  const char* const report = std::getenv("FAF_GAME_REPORT");
  const std::string_view folder = report != nullptr ? std::string_view(report) : std::string_view();
  const std::size_t slash = folder.rfind('/');
  if (slash == std::string_view::npos) {
    gOff = true;
    Report("profile: the game has no report folder (FAF_GAME_REPORT), so no profile");
    return;
  }
  if (const int error = simsampler::Install(); error != 0) {
    gOff = true;
    Report(std::string("profile: SIGPROF could not be had (") +
      (error == EBUSY ? "another handler has it" : std::strerror(error)) + "), so no profile");
    return;
  }
  auto* const counting = new Counting;
  counting->library = FindLibrary();
  counting->path = std::string(folder.substr(0, slash)) + "/profile.txt";
  counting->start = simsampler::MonotonicNanos();
  counting->writtenAt = counting->start;
  pthread_t thread{};
  if (const int error = pthread_create(&thread, nullptr, &Count, counting); error != 0) {
    delete counting;
    gOff = true;
    Report(std::string("profile: its counting thread did not start (") + std::strerror(error) + "), so no profile");
    return;
  }
  (void)pthread_setname_np(thread, "faf-profile");
  {
    const std::lock_guard<std::mutex> lock(gCountLock);
    counting->hasClock = pthread_getcpuclockid(thread, &counting->clock) == 0;
    gCounting = counting;
  }
  (void)pthread_detach(thread);
  Report("profile: the sim thread's CPU time sampled " + std::to_string(kHertz) + " times a second, with its callers by "
    "frame pointer, into profile.txt every 60 s and at a game's end (" + counting->library.name + ", build id " +
    counting->library.buildId + ")");
}

void profiler::SimBeat(const std::uint32_t tick)
{
  if (gOff.load(std::memory_order_relaxed)) {
    return;
  }
  // By the thread itself, not its tid, which the system may give a later thread.
  static thread_local bool sTimed = false;
  if (!sTimed) {
    // A new sim's first beat: its thread takes the timer from the last sim's.
    sTimed = true;
    int error = 0;
    {
      const std::lock_guard<std::mutex> lock(gTimerLock);
      StopTimerLocked();
      timer_t timer{};
      error = simsampler::Start(kIntervalNanos, timer);
      if (error == 0) {
        gTimer = timer;
        gTimerSet = true;
      }
    }
    if (error != 0) {
      TurnOff(std::string("the sim thread's timer could not be set (") + std::strerror(error) + ")");
      return;
    }
    NewSim(tick);
  } else if (tick < gLastTick.load(std::memory_order_relaxed)) {
    // A new sim on the same thread, whose ticks went back.
    NewSim(tick);
  }
  gLastTick.store(tick, std::memory_order_relaxed);
}

void profiler::Finish()
{
  const std::lock_guard<std::mutex> lock(gCountLock);
  {
    const std::lock_guard<std::mutex> timerLock(gTimerLock);
    StopTimerLocked();
  }
  if (gCounting == nullptr || gCounting->finished) {
    return;
  }
  gCounting->finished = true;
  if (gOff.load()) {
    return;
  }
  const std::uint64_t now = simsampler::MonotonicNanos();
  Drain(*gCounting, now);
  if (gCounting->profile.Samples() != gCounting->written) {
    (void)Write(*gCounting, now);
  }
}
