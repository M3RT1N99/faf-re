// Where the runner runs, and the speed experiment (release 0.4.1). Executable-only (build_runner.py's
// RUNNER_EXE_SOURCES): libfafengine.so does not change.
//
// The S22 Ultra (Exynos 2200) simulated the reference replay at about 24 beats/s, slower than arm64
// under translation on the PC, while it loaded it faster (docs/port/headless-replay.md, "First run
// on a real ARM core"). A child the app exec's inherits its cgroups, cpuset, affinity, timer slack
// and utilisation clamps from the app thread that forked it, and keeps them when the app's own
// state changes later. This file reports them, and applies the experiment's settings, from a
// constructor of the executable: it runs on the main thread before `main`, so before any other
// thread exists, and every thread the engine starts later inherits what it sets (the affinity mask
// and the timer slack are copied from the creating thread at clone time).
//
// Environment (both unset: nothing changes, only the report is printed):
//   FAF_RUNNER_TIMERSLACK_NS=<n>  prctl(PR_SET_TIMERSLACK, n), n >= 1 (the experiment uses 1). The
//                                 slack is how far the kernel may defer a timed wait or sleep; Android
//                                 gives background work tens of milliseconds.
//   FAF_RUNNER_AFFINITY=<mode>    fast: the allowed CPUs without the device's lowest capacity class
//                                 (on the Exynos 2200: the X2 and the A710s, when the cpuset allows
//                                 them); big: only the highest capacity class among the allowed CPUs;
//                                 a CPU list such as 4-7; or all: every allowed CPU. Always within the
//                                 allowed set (sched_getaffinity, i.e. the cpuset): nothing is widened.
//                                 Unset or empty: the runner sets nothing and the engine decides.
//   FAF_RUNNER_SYSFS_CPU=<dir>    test hook: read possible, online, cpu<n>/cpu_capacity,
//                                 cpu<n>/cpufreq/cpuinfo_max_freq and cpuinfo from <dir> instead of
//                                 /sys/devices/system/cpu and /proc/cpuinfo (a fake big.LITTLE tree on
//                                 the emulator, whose CPUs all look alike).
//
// The capacity classes come from cpu_capacity (1024 = the biggest core), else from
// cpufreq/cpuinfo_max_freq, else from /proc/cpuinfo's "CPU part" with a table of Arm's cores (A5x and
// A510/A520 little, A7x mid, X big), for a phone whose sysfs an app cannot read.
//
// Report, one line on stderr (the app merges it into stdout):
//   [runner] sched {"pid":..,"timerslack_ns":{"before":..,"after":..},"policy":"SCHED_OTHER","nice":..,
//                   "uclamp":{"min":..,"max":..},"uclamp_cgroup":{"min":"0.00","max":"max",..},
//                   "cpuset":"/top-app","cpus_allowed":{"before":"0-7","after":"4-7"},"affinity":{..,
//                   "in_force":"4-7","set_by":"runner","owner":"runner"},"cpus":[{"cpu":0,
//                   "capacity":..,"max_khz":..,"class":"little"},...],"classes":{"little":"0-3",...},...}
// The timer slack comes from prctl(PR_GET_TIMERSLACK): the app cannot read a child's
// /proc/<pid>/timerslack_ns (another task's needs CAP_SYS_NICE). "uclamp" is the task's own
// utilisation clamp request from sched_getattr (Java has no call for it); the clamp its cpu cgroup
// applies is "uclamp_cgroup" (/dev/cpuctl/<group>/cpu.uclamp.min and .max, as the files say them).
//
// Not printed for /replayinfo and /convertreplay, whose stdout is one JSON object. Allocation-free
// (stack buffers, open/read/write/close, no stdio, no opendir), so the low arena starts from the same
// state with and without it.
//
// Who owns the affinity afterwards. FAF's init_faf.lua, which the engine runs with /init, calls
// SetProcessAffinityMask(systemMask - 3) on a device with 6 or more CPUs ("all but CPUs 0 and 1": 2-7
// on the S22 Ultra), and the shim applies that to every thread (port/engine/shim/faf_win_kernel.h,
// sched_setaffinity per /proc/self/task entry). That happens while the engine loads, before the sim
// thread exists, so it would undo FAF_RUNNER_AFFINITY. The executable therefore exports its own
// sched_setaffinity (build_runner.py's EXE_EXPORTS), which libfafengine.so binds to ahead of libc's:
//  - FAF_RUNNER_AFFINITY set and usable (fast, big, all or a list with an allowed CPU): the runner owns
//    the mask. An engine request still returns success, but the thread gets the runner's mask instead
//    of the requested one.
//  - otherwise the request goes to the kernel unchanged: FAF's own mask applies, as in the reference
//    runs.
// The sched line names the owner ("affinity":{"owner":"runner"|"engine","in_force":"4-7",...}); each
// distinct engine request prints one more line:
//   [runner] affinity {"tid":..,"request":"2-7","in_force":"4-7","set_by":"runner","result":"ok"}
// ("set_by":"engine" when the engine's mask was applied).

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace
{
  constexpr int kMaxCpus = 64;

  // --------------------------------------------------------------------------------------------------
  // Output without malloc or stdio
  // --------------------------------------------------------------------------------------------------

  struct Json
  {
    char text[12288];
    size_t used = 0;
    bool first = true;  // no comma before the next member

    void Raw(const char* s)
    {
      while (s != nullptr && *s != '\0' && used + 2 < sizeof(text)) {
        text[used++] = *s++;
      }
    }
    void Char(const char c)
    {
      if (used + 2 < sizeof(text)) {
        text[used++] = c;
      }
    }
    void Dec(long long value)
    {
      unsigned long long v = value < 0 ? 0ull - static_cast<unsigned long long>(value) : static_cast<unsigned long long>(value);
      if (value < 0) {
        Char('-');
      }
      char digits[24];
      int n = 0;
      do {
        digits[n++] = static_cast<char>('0' + v % 10u);
        v /= 10u;
      } while (v != 0 && n < 24);
      while (n > 0) {
        Char(digits[--n]);
      }
    }
    // A JSON string: quotes, backslashes and control characters escaped; at most `limit` bytes.
    void Str(const char* s, size_t limit = 512)
    {
      Char('"');
      for (size_t i = 0; s != nullptr && s[i] != '\0' && i < limit; ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '"' || c == '\\') {
          Char('\\');
          Char(static_cast<char>(c));
        } else if (c == '\n') {
          Raw("\\n");
        } else if (c < 0x20) {
          Raw("\\u00");
          Char("0123456789abcdef"[c >> 4]);
          Char("0123456789abcdef"[c & 15u]);
        } else {
          Char(static_cast<char>(c));
        }
      }
      Char('"');
    }
    void Key(const char* name)
    {
      if (!first) {
        Char(',');
      }
      first = false;
      Str(name);
      Char(':');
    }
    void Open(const char brace)
    {
      Char(brace);
      first = true;
    }
    void Close(const char brace)
    {
      Char(brace);
      first = false;
    }
    void Item()
    {
      if (!first) {
        Char(',');
      }
      first = false;
    }
    void Null() { Raw("null"); }
    void Bool(const bool v) { Raw(v ? "true" : "false"); }
    void Write(const int fd)
    {
      text[used++] = '\n';
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

  // --------------------------------------------------------------------------------------------------
  // Files and CPU lists
  // --------------------------------------------------------------------------------------------------

  // The file's first `size - 1` bytes, NUL-terminated, trailing white space removed; false when it
  // cannot be opened or read (out is then "").
  bool ReadSmall(const char* path, char* out, const size_t size)
  {
    out[0] = '\0';
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
      return false;
    }
    size_t used = 0;
    while (used + 1 < size) {
      const ssize_t n = read(fd, out + used, size - 1 - used);
      if (n < 0 && errno == EINTR) {
        continue;
      }
      if (n <= 0) {
        break;
      }
      used += static_cast<size_t>(n);
    }
    close(fd);
    out[used] = '\0';
    while (used > 0 && (out[used - 1] == '\n' || out[used - 1] == ' ' || out[used - 1] == '\t')) {
      out[--used] = '\0';
    }
    return true;
  }

  bool ReadLong(const char* path, long long& value)
  {
    char text[64];
    if (!ReadSmall(path, text, sizeof(text)) || text[0] < '0' || text[0] > '9') {
      return false;
    }
    long long v = 0;
    for (const char* p = text; *p >= '0' && *p <= '9'; ++p) {
      v = v * 10 + (*p - '0');
    }
    value = v;
    return true;
  }

  const char* gCpuRoot = "/sys/devices/system/cpu";

  // "<root>/<leaf>"
  void RootPath(char* out, const size_t size, const char* leaf)
  {
    out[0] = '\0';
    strlcat(out, gCpuRoot, size);
    strlcat(out, "/", size);
    strlcat(out, leaf, size);
  }

  // "<root>/cpu<n>/<leaf>" (root /sys/devices/system/cpu)
  void CpuPath(char* out, const size_t size, const int cpu, const char* leaf)
  {
    char number[12];
    int n = 0;
    int v = cpu;
    do {
      number[n++] = static_cast<char>('0' + v % 10);
      v /= 10;
    } while (v != 0 && n < 11);
    size_t used = 0;
    auto put = [&](const char* s) {
      while (*s != '\0' && used + 1 < size) {
        out[used++] = *s++;
      }
    };
    put(gCpuRoot);
    put("/cpu");
    while (n > 0 && used + 1 < size) {
      out[used++] = number[--n];
    }
    put("/");
    put(leaf);
    out[used] = '\0';
  }

  // A CPU list ("0-3,5,7") into a mask of the first kMaxCpus CPUs; false for anything else.
  bool ParseList(const char* text, uint64_t& mask)
  {
    mask = 0;
    const char* p = text;
    bool any = false;
    while (*p != '\0') {
      if (*p < '0' || *p > '9') {
        return false;
      }
      int from = 0;
      while (*p >= '0' && *p <= '9') {
        from = from * 10 + (*p++ - '0');
      }
      int to = from;
      if (*p == '-') {
        ++p;
        if (*p < '0' || *p > '9') {
          return false;
        }
        to = 0;
        while (*p >= '0' && *p <= '9') {
          to = to * 10 + (*p++ - '0');
        }
      }
      for (int c = from; c <= to && c < kMaxCpus; ++c) {
        mask |= 1ull << c;
      }
      any = true;
      if (*p == ',') {
        ++p;
      } else if (*p != '\0') {
        return false;
      }
    }
    return any;
  }

  // The mask as a CPU list ("0-3,7"), "" for none.
  void FormatList(const uint64_t mask, char* out, const size_t size)
  {
    size_t used = 0;
    auto put = [&](const int v) {
      char digits[4];
      int n = 0;
      int x = v;
      do {
        digits[n++] = static_cast<char>('0' + x % 10);
        x /= 10;
      } while (x != 0 && n < 4);
      while (n > 0 && used + 1 < size) {
        out[used++] = digits[--n];
      }
    };
    for (int c = 0; c < kMaxCpus; ++c) {
      if ((mask >> c & 1u) == 0) {
        continue;
      }
      int end = c;
      while (end + 1 < kMaxCpus && (mask >> (end + 1) & 1u) != 0) {
        ++end;
      }
      if (used > 0 && used + 1 < size) {
        out[used++] = ',';
      }
      put(c);
      if (end > c) {
        if (used + 1 < size) {
          out[used++] = '-';
        }
        put(end);
      }
      c = end;
    }
    out[used] = '\0';
  }

  uint64_t Affinity(bool& ok, const pid_t tid = 0)
  {
    cpu_set_t set;
    CPU_ZERO(&set);
    ok = sched_getaffinity(tid, sizeof(set), &set) == 0;
    uint64_t mask = 0;
    for (int c = 0; ok && c < kMaxCpus; ++c) {
      if (CPU_ISSET(c, &set)) {
        mask |= 1ull << c;
      }
    }
    return mask;
  }

  // The kernel's sched_setaffinity, past this executable's own (exported) definition below.
  int RawSetAffinity(const pid_t tid, const size_t size, const cpu_set_t* const set)
  {
    return static_cast<int>(syscall(SYS_sched_setaffinity, tid, size, set));
  }

  int RawSetAffinityMask(const pid_t tid, const uint64_t mask)
  {
    cpu_set_t set;
    CPU_ZERO(&set);
    for (int c = 0; c < kMaxCpus; ++c) {
      if ((mask >> c & 1u) != 0) {
        CPU_SET(c, &set);
      }
    }
    return RawSetAffinity(tid, sizeof(set), &set);
  }

  // Set by the constructor before any other thread exists; read by the interposer afterwards.
  bool gRunnerOwnsAffinity = false;
  uint64_t gRunnerMask = 0;
  // The last request line printed (request, in force, who), so the shim's per-thread loop prints once.
  int gAffinityLogLock = 0;
  uint64_t gLoggedRequest = 0;
  uint64_t gLoggedInForce = 0;
  int gLoggedSetBy = -1;
  int gLoggedResult = -1;

  // The value after "<key>:" in a "key:\tvalue" file such as /proc/self/status, or "".
  void StatusField(const char* text, const char* key, char* out, const size_t size)
  {
    out[0] = '\0';
    const size_t keyLength = strlen(key);
    for (const char* line = text; line != nullptr && *line != '\0';) {
      if (strncmp(line, key, keyLength) == 0 && line[keyLength] == ':') {
        const char* v = line + keyLength + 1;
        while (*v == ' ' || *v == '\t') {
          ++v;
        }
        size_t n = 0;
        while (v[n] != '\0' && v[n] != '\n' && n + 1 < size) {
          out[n] = v[n];
          ++n;
        }
        out[n] = '\0';
        return;
      }
      line = strchr(line, '\n');
      line = line != nullptr ? line + 1 : nullptr;
    }
  }

  // sched_getattr's structure (SCHED_ATTR_SIZE_VER1: with the utilisation clamps).
  struct SchedAttr
  {
    uint32_t size;
    uint32_t sched_policy;
    uint64_t sched_flags;
    int32_t sched_nice;
    uint32_t sched_priority;
    uint64_t sched_runtime;
    uint64_t sched_deadline;
    uint64_t sched_period;
    uint32_t sched_util_min;
    uint32_t sched_util_max;
  };

  const char* PolicyName(const int policy)
  {
    switch (policy & ~SCHED_RESET_ON_FORK) {
      case SCHED_OTHER: return "SCHED_OTHER";
      case SCHED_FIFO: return "SCHED_FIFO";
      case SCHED_RR: return "SCHED_RR";
      case SCHED_BATCH: return "SCHED_BATCH";
      case SCHED_IDLE: return "SCHED_IDLE";
      case 6: return "SCHED_DEADLINE";
      default: return "?";
    }
  }

  struct Cpu
  {
    long long capacity = -1;  // cpu_capacity (1024 = the biggest core), -1 when unreadable
    long long maxKhz = -1;    // cpufreq/cpuinfo_max_freq, -1 when unreadable
    long long part = -1;      // /proc/cpuinfo "CPU part" (Arm: 0xd48 = Cortex-X2, ...), -1 when absent
    long long value = -1;     // what the classes are built from
    const char* className = "";
  };

  // Arm's core part numbers by class (only used when neither cpu_capacity nor cpuinfo_max_freq can be
  // read): 1 little (in-order), 2 mid (A7x), 3 big (X), 0 unknown.
  int PartRank(const long long part)
  {
    switch (part) {
      case 0xd03: case 0xd04: case 0xd05: case 0xd46: case 0xd80:  // A53, A35, A55, A510, A520
        return 1;
      case 0xd07: case 0xd08: case 0xd09: case 0xd0a: case 0xd0b: case 0xd0d: case 0xd41:  // A57-A78
      case 0xd47: case 0xd4d: case 0xd81: case 0xd87:                                   // A710-A725
        return 2;
      case 0xd44: case 0xd4c: case 0xd48: case 0xd4e: case 0xd82: case 0xd85:  // X1, X1C, X2, X3, X4, X925
        return 3;
      default:
        return 0;
    }
  }

  // /proc/cpuinfo's "processor : n" and "CPU part : 0x..." lines.
  void ReadCpuParts(const char* path, Cpu* cpus)
  {
    char text[16384];
    if (!ReadSmall(path, text, sizeof(text))) {
      return;
    }
    int current = -1;
    for (const char* line = text; line != nullptr && *line != '\0';) {
      const char* colon = strchr(line, ':');
      const char* end = strchr(line, '\n');
      if (colon != nullptr && (end == nullptr || colon < end)) {
        const char* v = colon + 1;
        while (*v == ' ' || *v == '\t') {
          ++v;
        }
        if (strncmp(line, "processor", 9) == 0) {
          current = (*v >= '0' && *v <= '9') ? static_cast<int>(strtol(v, nullptr, 10)) : -1;
        } else if (strncmp(line, "CPU part", 8) == 0 && current >= 0 && current < kMaxCpus) {
          cpus[current].part = strtoll(v, nullptr, 0);
        }
      }
      line = end != nullptr ? end + 1 : nullptr;
    }
  }

  // --------------------------------------------------------------------------------------------------
  // The constructor
  // --------------------------------------------------------------------------------------------------

  const char* EnvValue(char** envp, const char* name)
  {
    const size_t length = strlen(name);
    for (char** e = envp; e != nullptr && *e != nullptr; ++e) {
      if (strncmp(*e, name, length) == 0 && (*e)[length] == '=') {
        return *e + length + 1;
      }
    }
    return nullptr;
  }

  void Report(const int argc, char** const argv, char** const envp)
  {
    if (argc >= 2 && argv != nullptr && argv[1] != nullptr &&
        (strcasecmp(argv[1], "/replayinfo") == 0 || strcasecmp(argv[1], "/convertreplay") == 0)) {
      return;
    }
    const char* const slackRequest = EnvValue(envp, "FAF_RUNNER_TIMERSLACK_NS");
    const char* const affinityRequest = EnvValue(envp, "FAF_RUNNER_AFFINITY");
    const char* const testRoot = EnvValue(envp, "FAF_RUNNER_SYSFS_CPU");
    const bool testTree = testRoot != nullptr && testRoot[0] != '\0';
    if (testTree) {
      gCpuRoot = testRoot;
    }

    // CPUs: possible, online, capacities.
    char text[1024];
    char path[256];
    uint64_t possible = 0;
    RootPath(path, sizeof(path), "possible");
    if (!ReadSmall(path, text, sizeof(text)) || !ParseList(text, possible)) {
      possible = 0;
    }
    uint64_t online = 0;
    RootPath(path, sizeof(path), "online");
    const bool onlineKnown = ReadSmall(path, text, sizeof(text)) && ParseList(text, online);
    Cpu cpus[kMaxCpus];
    if (testTree) {
      RootPath(path, sizeof(path), "cpuinfo");
      ReadCpuParts(path, cpus);
    } else {
      ReadCpuParts("/proc/cpuinfo", cpus);
    }
    bool anyCapacity = false;
    bool anyFreq = false;
    for (int c = 0; c < kMaxCpus; ++c) {
      if ((possible >> c & 1u) == 0) {
        continue;
      }
      CpuPath(path, sizeof(path), c, "cpu_capacity");
      anyCapacity = ReadLong(path, cpus[c].capacity) || anyCapacity;
      CpuPath(path, sizeof(path), c, "cpufreq/cpuinfo_max_freq");
      anyFreq = ReadLong(path, cpus[c].maxKhz) || anyFreq;
    }
    bool anyPart = false;
    for (int c = 0; c < kMaxCpus; ++c) {
      anyPart = anyPart || ((possible >> c & 1u) != 0 && PartRank(cpus[c].part) > 0);
    }
    const char* const source =
      anyCapacity ? "cpu_capacity" : (anyFreq ? "cpuinfo_max_freq" : (anyPart ? "cpuinfo_part" : "none"));
    // Distinct values, ascending: lowest = little, highest = big, the rest mid; one value: uniform.
    long long distinct[kMaxCpus];
    int classes = 0;
    for (int c = 0; c < kMaxCpus; ++c) {
      if ((possible >> c & 1u) == 0) {
        continue;
      }
      cpus[c].value = anyCapacity ? cpus[c].capacity
                                  : (anyFreq ? cpus[c].maxKhz
                                             : (PartRank(cpus[c].part) > 0 ? PartRank(cpus[c].part) : -1));
      if (cpus[c].value < 0) {
        continue;
      }
      int i = 0;
      while (i < classes && distinct[i] != cpus[c].value) {
        ++i;
      }
      if (i == classes) {
        distinct[classes++] = cpus[c].value;
      }
    }
    for (int i = 1; i < classes; ++i) {
      for (int j = i; j > 0 && distinct[j - 1] > distinct[j]; --j) {
        const long long t = distinct[j];
        distinct[j] = distinct[j - 1];
        distinct[j - 1] = t;
      }
    }
    uint64_t littleMask = 0;
    uint64_t midMask = 0;
    uint64_t bigMask = 0;
    for (int c = 0; c < kMaxCpus; ++c) {
      if ((possible >> c & 1u) == 0 || cpus[c].value < 0) {
        continue;
      }
      if (classes == 1) {
        cpus[c].className = "uniform";
      } else if (cpus[c].value == distinct[0]) {
        cpus[c].className = "little";
        littleMask |= 1ull << c;
      } else if (cpus[c].value == distinct[classes - 1]) {
        cpus[c].className = "big";
        bigMask |= 1ull << c;
      } else {
        cpus[c].className = "mid";
        midMask |= 1ull << c;
      }
    }

    // Before.
    const int slackBefore = prctl(PR_GET_TIMERSLACK, 0, 0, 0, 0);
    bool affinityOk = false;
    const uint64_t allowedBefore = Affinity(affinityOk);

    // Timer slack.
    const char* slackNote = "";
    bool slackApplied = false;
    if (slackRequest != nullptr && slackRequest[0] != '\0') {
      unsigned long long v = 0;
      bool digits = true;
      for (const char* p = slackRequest; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
          digits = false;
          break;
        }
        v = v * 10u + static_cast<unsigned>(*p - '0');
      }
      if (!digits || v == 0) {
        slackNote = "FAF_RUNNER_TIMERSLACK_NS must be a number >= 1; not changed";
      } else if (prctl(PR_SET_TIMERSLACK, static_cast<unsigned long>(v), 0, 0, 0) != 0) {
        slackNote = strerror(errno);
      } else {
        slackApplied = true;
      }
    }
    const int slackAfter = prctl(PR_GET_TIMERSLACK, 0, 0, 0, 0);

    // Affinity. `own`: the runner holds the resulting mask against the engine's later requests (see the
    // top of the file); only when FAF_RUNNER_AFFINITY asks for something this device can do.
    const char* affinityNote = "";
    const char* mode = "";
    bool affinityApplied = false;
    bool own = false;
    if (affinityRequest != nullptr && affinityRequest[0] != '\0') {
      mode = affinityRequest;
      uint64_t wanted = 0;
      bool valid = true;      // a mask to apply
      bool keepAll = false;   // no mask to apply, but the allowed set is the runner's choice (own it)
      if (strcasecmp(affinityRequest, "all") == 0) {
        keepAll = true;
        affinityNote = "every allowed CPU: not changed";
        valid = false;
      } else if (strcasecmp(affinityRequest, "fast") == 0) {
        if (classes < 2) {
          affinityNote = classes == 0 ? "no CPU capacities: not changed" : "all CPUs have the same capacity: not changed";
          keepAll = classes == 1;
          valid = false;
        } else {
          wanted = allowedBefore & ~littleMask;
          if (wanted == 0) {
            affinityNote = "the allowed CPUs are all in the lowest capacity class: not changed";
            keepAll = true;
            valid = false;
          }
        }
      } else if (strcasecmp(affinityRequest, "big") == 0) {
        long long best = -1;
        for (int c = 0; c < kMaxCpus; ++c) {
          if ((allowedBefore >> c & 1u) != 0 && cpus[c].value > best) {
            best = cpus[c].value;
          }
        }
        if (best < 0 || classes < 2) {
          affinityNote = classes == 0 ? "no CPU capacities: not changed" : "all CPUs have the same capacity: not changed";
          keepAll = classes == 1;
          valid = false;
        } else {
          for (int c = 0; c < kMaxCpus; ++c) {
            if ((allowedBefore >> c & 1u) != 0 && cpus[c].value == best) {
              wanted |= 1ull << c;
            }
          }
        }
      } else if (ParseList(affinityRequest, wanted)) {
        wanted &= allowedBefore;
        if (wanted == 0) {
          affinityNote = "none of these CPUs is allowed: not changed";
          valid = false;
        }
      } else {
        affinityNote = "FAF_RUNNER_AFFINITY must be fast, big, all or a CPU list: not changed";
        valid = false;
      }
      if ((valid || keepAll) && !affinityOk) {
        affinityNote = "sched_getaffinity failed: not changed";
        valid = false;
        keepAll = false;
      }
      if (valid) {
        if (wanted == allowedBefore) {
          affinityNote = "already the allowed set";
        }
        if (RawSetAffinityMask(0, wanted) != 0) {
          affinityNote = strerror(errno);
        } else {
          affinityApplied = true;
          own = true;
        }
      } else if (keepAll) {
        own = true;
      }
    }
    bool afterOk = false;
    const uint64_t allowedAfter = Affinity(afterOk);
    if (own && afterOk) {
      gRunnerMask = allowedAfter;
      gRunnerOwnsAffinity = true;
    }

    // Policy, nice, clamps.
    const int policy = sched_getscheduler(0);
    errno = 0;
    const int nice = getpriority(PRIO_PROCESS, 0);
    const bool niceOk = errno == 0;
    SchedAttr attr{};
    const bool attrOk = syscall(SYS_sched_getattr, 0, &attr, static_cast<unsigned>(sizeof(attr)), 0u) == 0;

    // Cgroups.
    char cpuset[256];
    const bool cpusetOk = ReadSmall("/proc/self/cpuset", cpuset, sizeof(cpuset));
    char cgroup[1024];
    const bool cgroupOk = ReadSmall("/proc/self/cgroup", cgroup, sizeof(cgroup));
    // The cpuset's own CPU list: cgroup v1 (/dev/cpuset/<path>/cpus), else v2.
    char cpusetCpus[128] = {};
    const char* cpusetCpusFrom = "";
    if (cpusetOk && cpuset[0] == '/') {
      char path[384];
      const char* const roots[][2] = {{"/dev/cpuset", "/cpus"}, {"/dev/cpuset", "/cpuset.cpus"},
                                      {"/sys/fs/cgroup", "/cpuset.cpus.effective"}};
      for (const auto& root : roots) {
        path[0] = '\0';
        strlcat(path, root[0], sizeof(path));
        if (strcmp(cpuset, "/") != 0) {
          strlcat(path, cpuset, sizeof(path));
        }
        strlcat(path, root[1], sizeof(path));
        if (ReadSmall(path, cpusetCpus, sizeof(cpusetCpus)) && cpusetCpus[0] != '\0') {
          cpusetCpusFrom = root[0];
          break;
        }
        cpusetCpus[0] = '\0';
      }
    }
    // The utilisation clamp the cpu cgroup applies (sched_getattr's uclamp is only the task's own
    // request): /dev/cpuctl<path>/cpu.uclamp.{min,max} for cgroup v1's cpu controller, else cgroup v2's.
    char clampPath[384] = {};
    char clampMin[32] = {};
    char clampMax[32] = {};
    bool clampOk = false;
    if (cgroupOk) {
      for (const char* line = cgroup; line != nullptr && *line != '\0';) {
        const char* end = strchr(line, '\n');
        const size_t length = end != nullptr ? static_cast<size_t>(end - line) : strlen(line);
        const char* first = static_cast<const char*>(memchr(line, ':', length));
        const char* second = first != nullptr ? static_cast<const char*>(memchr(first + 1, ':', length - static_cast<size_t>(first + 1 - line))) : nullptr;
        if (second != nullptr) {
          const size_t controllersLength = static_cast<size_t>(second - first - 1);
          bool cpu = false;
          bool v2 = controllersLength == 0 && first == line + 1 && line[0] == '0';
          for (const char* c = first + 1; c < second;) {
            const char* comma = static_cast<const char*>(memchr(c, ',', static_cast<size_t>(second - c)));
            const char* stop = comma != nullptr ? comma : second;
            cpu = cpu || (stop - c == 3 && strncmp(c, "cpu", 3) == 0);
            c = stop + 1;
          }
          if (cpu || (v2 && clampPath[0] == '\0')) {
            const size_t pathLength = length - static_cast<size_t>(second + 1 - line);
            char group[256] = {};
            memcpy(group, second + 1, pathLength < sizeof(group) - 1 ? pathLength : sizeof(group) - 1);
            clampPath[0] = '\0';
            strlcat(clampPath, cpu ? "/dev/cpuctl" : "/sys/fs/cgroup", sizeof(clampPath));
            if (strcmp(group, "/") != 0) {
              strlcat(clampPath, group, sizeof(clampPath));
            }
            if (cpu) {
              break;
            }
          }
        }
        line = end != nullptr ? end + 1 : nullptr;
      }
      if (clampPath[0] != '\0') {
        char file[448];
        file[0] = '\0';
        strlcat(file, clampPath, sizeof(file));
        strlcat(file, "/cpu.uclamp.min", sizeof(file));
        clampOk = ReadSmall(file, clampMin, sizeof(clampMin));
        file[0] = '\0';
        strlcat(file, clampPath, sizeof(file));
        strlcat(file, "/cpu.uclamp.max", sizeof(file));
        clampOk = ReadSmall(file, clampMax, sizeof(clampMax)) && clampOk;
      }
    }
    char status[4096];
    char allowedList[128] = {};
    if (ReadSmall("/proc/self/status", status, sizeof(status))) {
      StatusField(status, "Cpus_allowed_list", allowedList, sizeof(allowedList));
    }

    // The line.
    Json j;
    j.Raw("[runner] sched ");
    j.Open('{');
    j.Key("pid");
    j.Dec(getpid());
    if (testTree) {
      j.Key("cpu_root");
      j.Str(gCpuRoot);
    }
    j.Key("requested");
    j.Open('{');
    j.Key("timerslack_ns");
    if (slackRequest != nullptr) { j.Str(slackRequest, 32); } else { j.Null(); }
    j.Key("affinity");
    if (affinityRequest != nullptr) { j.Str(affinityRequest, 64); } else { j.Null(); }
    j.Close('}');
    j.Key("timerslack_ns");
    j.Open('{');
    j.Key("before");
    j.Dec(slackBefore);
    j.Key("after");
    j.Dec(slackAfter);
    j.Key("applied");
    j.Bool(slackApplied);
    if (slackNote[0] != '\0') {
      j.Key("note");
      j.Str(slackNote);
    }
    j.Close('}');
    j.Key("policy");
    j.Str(policy >= 0 ? PolicyName(policy) : "?");
    j.Key("nice");
    if (niceOk) { j.Dec(nice); } else { j.Null(); }
    j.Key("uclamp");
    if (attrOk && attr.size >= 56) {
      j.Open('{');
      j.Key("min");
      j.Dec(attr.sched_util_min);
      j.Key("max");
      j.Dec(attr.sched_util_max);
      j.Close('}');
    } else {
      j.Null();
    }
    j.Key("uclamp_cgroup");
    if (clampOk) {
      j.Open('{');
      j.Key("min");
      j.Str(clampMin, 32);
      j.Key("max");
      j.Str(clampMax, 32);
      j.Key("path");
      j.Str(clampPath);
      j.Close('}');
    } else {
      j.Null();
    }
    j.Key("cpuset");
    if (cpusetOk) { j.Str(cpuset); } else { j.Null(); }
    j.Key("cpuset_cpus");
    if (cpusetCpus[0] != '\0') { j.Str(cpusetCpus); } else { j.Null(); }
    if (cpusetCpusFrom[0] != '\0') {
      j.Key("cpuset_cpus_from");
      j.Str(cpusetCpusFrom);
    }
    j.Key("cgroup");
    if (cgroupOk) { j.Str(cgroup); } else { j.Null(); }
    char list[128];
    j.Key("cpus_allowed");
    j.Open('{');
    j.Key("before");
    FormatList(allowedBefore, list, sizeof(list));
    if (affinityOk) { j.Str(list); } else { j.Null(); }
    j.Key("after");
    FormatList(allowedAfter, list, sizeof(list));
    if (afterOk) { j.Str(list); } else { j.Null(); }
    j.Key("status");
    j.Str(allowedList);
    j.Close('}');
    j.Key("affinity");
    j.Open('{');
    j.Key("mode");
    j.Str(mode[0] != '\0' ? mode : "unchanged", 64);
    j.Key("applied");
    j.Bool(affinityApplied);
    if (affinityNote[0] != '\0') {
      j.Key("note");
      j.Str(affinityNote);
    }
    // What is in force when main starts, who set it, and who decides later (the engine's requests).
    j.Key("in_force");
    FormatList(allowedAfter, list, sizeof(list));
    if (afterOk) { j.Str(list); } else { j.Null(); }
    j.Key("set_by");
    j.Str(affinityApplied ? "runner" : "inherited");
    j.Key("owner");
    j.Str(gRunnerOwnsAffinity ? "runner" : "engine");
    j.Close('}');
    j.Key("capacity_source");
    j.Str(source);
    j.Key("cpus");
    j.Open('[');
    for (int c = 0; c < kMaxCpus; ++c) {
      if ((possible >> c & 1u) == 0) {
        continue;
      }
      j.Item();
      j.Open('{');
      j.Key("cpu");
      j.Dec(c);
      j.Key("capacity");
      if (cpus[c].capacity >= 0) { j.Dec(cpus[c].capacity); } else { j.Null(); }
      j.Key("max_khz");
      if (cpus[c].maxKhz >= 0) { j.Dec(cpus[c].maxKhz); } else { j.Null(); }
      if (cpus[c].part >= 0) {
        char part[8] = {'0', 'x', 0, 0, 0, 0, 0, 0};
        for (int k = 0; k < 3; ++k) {
          part[2 + k] = "0123456789abcdef"[(cpus[c].part >> (8 - 4 * k)) & 15];
        }
        j.Key("part");
        j.Str(part);
      }
      j.Key("class");
      j.Str(cpus[c].className);
      if (onlineKnown) {
        j.Key("online");
        j.Bool((online >> c & 1u) != 0);
      }
      j.Close('}');
    }
    j.Close(']');
    j.Key("classes");
    j.Open('{');
    if (classes == 1) {
      j.Key("uniform");
      FormatList(possible, list, sizeof(list));
      j.Str(list);
    } else if (classes > 1) {
      const struct { const char* name; uint64_t mask; } named[] = {{"little", littleMask}, {"mid", midMask}, {"big", bigMask}};
      for (const auto& n : named) {
        if (n.mask != 0) {
          j.Key(n.name);
          FormatList(n.mask, list, sizeof(list));
          j.Str(list);
        }
      }
    }
    j.Close('}');
    j.Close('}');
    j.Write(2);
  }

  // Bionic calls an executable's constructors with (argc, argv, envp), as glibc does.
  __attribute__((constructor)) void RunnerSchedInit(int argc, char** argv, char** envp)
  {
    const int saved = errno;
    Report(argc, argv, envp);
    errno = saved;
  }

  // One "[runner] affinity" line per distinct engine request (the shim's SetProcessAffinityMask calls
  // sched_setaffinity once for the calling thread and once per /proc/self/task entry, all alike).
  void LogEngineRequest(const pid_t tid, const uint64_t request, const bool requestOk, const uint64_t inForce,
                        const bool inForceOk, const bool byRunner, const int error)
  {
    while (__atomic_exchange_n(&gAffinityLogLock, 1, __ATOMIC_ACQUIRE) != 0) {
      sched_yield();
    }
    const int setBy = byRunner ? 1 : 0;
    const int result = error;
    const bool repeat = gLoggedSetBy == setBy && gLoggedRequest == request && gLoggedInForce == inForce &&
                        gLoggedResult == result;
    gLoggedSetBy = setBy;
    gLoggedRequest = request;
    gLoggedInForce = inForce;
    gLoggedResult = result;
    __atomic_store_n(&gAffinityLogLock, 0, __ATOMIC_RELEASE);
    if (repeat) {
      return;
    }
    char list[128];
    Json j;
    j.Raw("[runner] affinity ");
    j.Open('{');
    j.Key("tid");
    j.Dec(tid);
    j.Key("request");
    FormatList(request, list, sizeof(list));
    if (requestOk) { j.Str(list); } else { j.Null(); }
    j.Key("in_force");
    FormatList(inForce, list, sizeof(list));
    if (inForceOk) { j.Str(list); } else { j.Null(); }
    j.Key("set_by");
    j.Str(byRunner ? "runner" : "engine");
    j.Key("result");
    j.Str(error == 0 ? "ok" : strerror(error));
    if (byRunner) {
      j.Key("note");
      j.Str(request == inForce ? "FAF_RUNNER_AFFINITY holds: the engine asked for the same CPUs"
                               : "FAF_RUNNER_AFFINITY holds: the engine's request is not applied");
    }
    j.Close('}');
    j.Write(2);
  }
} // namespace

// The engine's sched_setaffinity (the shim's SetProcessAffinityMask and SetThreadAffinityMask), bound
// here ahead of libc's because the executable exports it (build_runner.py's EXE_EXPORTS). Allocation-
// free, like the rest of this file. RunnerSched's own call goes straight to the kernel (RawSetAffinity).
extern "C" __attribute__((visibility("default"))) int sched_setaffinity(pid_t tid, size_t size, const cpu_set_t* set)
{
  const int saved = errno;
  uint64_t request = 0;
  const bool requestOk = set != nullptr;
  for (int c = 0; requestOk && c < kMaxCpus && static_cast<size_t>(c) < size * 8u; ++c) {
    if (CPU_ISSET_S(c, size, set)) {
      request |= 1ull << c;
    }
  }
  const bool byRunner = gRunnerOwnsAffinity;
  const int rc = byRunner ? RawSetAffinityMask(tid, gRunnerMask) : RawSetAffinity(tid, size, set);
  const int error = rc == 0 ? 0 : errno;
  const pid_t target = tid != 0 ? tid : static_cast<pid_t>(syscall(SYS_gettid));
  bool inForceOk = false;
  const uint64_t inForce = Affinity(inForceOk, target);
  LogEngineRequest(target, request, requestOk, inForce, inForceOk, byRunner, error);
  errno = rc == 0 ? saved : error;
  return rc;
}
