#pragma once
// The device's classes of cores, as sysfs rates each CPU, and where the game's threads go by them (src/app/CoresAndroid.cpp):
// the sim's thread on the fastest class and the next, the render thread, the issue thread and the workers off the little
// cores; the report's line of it; what /proc says of a thread, by which a thread placed is known again and the sim's speed
// lines share its time out among the classes; and those lines. Headers only, so that tests/griff pins the choice without
// Android or the engine.

#include <bit>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace coreclasses
{
  /// CPUs as a mask: bit n is CPU n. A device with more CPUs than it holds has no classes here.
  using CpuSet = std::uint64_t;
  inline constexpr std::size_t kMaxCpus = 64;

  /// A whole number of decimal digits alone, as /proc and sysfs write them; false for anything else, or one too long.
  [[nodiscard]] inline bool ParseNumber(const std::string_view text, std::uint64_t& value)
  {
    if (text.empty() || text.size() > 19) {
      return false;
    }
    std::uint64_t number = 0;
    for (const char digit : text) {
      if (digit < '0' || digit > '9') {
        return false;
      }
      number = number * 10 + static_cast<std::uint64_t>(digit - '0');
    }
    value = number;
    return true;
  }

  /// The rating a sysfs file holds ("1024\n"), 0 where it holds none: no file, another word, or 0 itself, which rates no CPU.
  [[nodiscard]] inline std::uint64_t Rating(std::string_view text)
  {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
      text.remove_suffix(1);
    }
    std::uint64_t value = 0;
    return ParseNumber(text, value) ? value : 0;
  }

  /// CPUs that sysfs rates alike.
  struct CoreClass
  {
    std::uint64_t rating = 0;
    CpuSet cpus = 0;
  };

  /// The file that rated the CPUs.
  enum class Source
  {
    kNone,
    /// cpu<n>/cpu_capacity: the scheduler's own rating of a core, 1024 for the fastest.
    kCapacity,
    /// cpu<n>/cpufreq/cpuinfo_max_freq, in kHz, as the app's replay test rates the cores (SpeedSampler.Cores).
    kMaxFrequency,
  };

  struct Reading
  {
    Source source = Source::kNone;
    /// Fastest first.
    std::vector<CoreClass> classes;
    /// Every CPU rated.
    CpuSet all = 0;
  };

  /// The classes of one file's texts, cpu0's first, fastest first. None where a CPU's text holds no rating: a reading that
  /// leaves one out says nothing of where that one goes.
  [[nodiscard]] inline std::vector<CoreClass> ClassesOf(const std::vector<std::string>& texts)
  {
    if (texts.empty() || texts.size() > kMaxCpus) {
      return {};
    }
    std::vector<CoreClass> classes;
    for (std::size_t cpu = 0; cpu < texts.size(); ++cpu) {
      const std::uint64_t rating = Rating(texts[cpu]);
      if (rating == 0) {
        return {};
      }
      auto at = classes.begin();
      while (at != classes.end() && at->rating > rating) {
        ++at;
      }
      if (at == classes.end() || at->rating != rating) {
        at = classes.insert(at, CoreClass{rating, 0});
      }
      at->cpus |= CpuSet{1} << cpu;
    }
    return classes;
  }

  /// The device's classes: by cpu_capacity where every CPU has one, else by cpuinfo_max_freq where every CPU has that. A
  /// cpu_capacity of one class gives way to clocks that tell classes apart: a kernel whose device tree has no
  /// capacity-dmips-mhz rates every CPU 1024, however fast it is. One text of each for each CPU, cpu0's first; a missing
  /// file's is empty.
  [[nodiscard]] inline Reading Read(const std::vector<std::string>& capacities, const std::vector<std::string>& maxFrequencies)
  {
    Reading reading;
    reading.classes = ClassesOf(capacities);
    reading.source = reading.classes.empty() ? Source::kNone : Source::kCapacity;
    if (reading.classes.size() < 2) {
      if (std::vector<CoreClass> byClock = ClassesOf(maxFrequencies); byClock.size() > reading.classes.size()) {
        reading.classes = std::move(byClock);
        reading.source = Source::kMaxFrequency;
      }
    }
    for (const CoreClass& coreClass : reading.classes) {
      reading.all |= coreClass.cpus;
    }
    return reading;
  }

  /// Where the threads go on a device's classes.
  struct Plan
  {
    /// The sim's thread: the fastest class and the next, but never the little one (the slowest). None on a device of one
    /// class, which keeps FAF's way: its init's mask, and Android's choice within it.
    CpuSet sim = 0;
    /// Where the sim's thread starts: the fastest class.
    CpuSet simFirst = 0;
    /// The render thread's, the issue thread's and the workers': every class but the little one, where those hold at least
    /// half of the CPUs; every CPU where they hold fewer, as the little cores then are most of the device.
    CpuSet others = 0;
    /// Whether `others` leaves the little cores out.
    bool offLittle = false;

    [[nodiscard]] bool Places() const
    {
      return sim != 0;
    }
  };

  [[nodiscard]] inline Plan PlanFor(const Reading& reading)
  {
    Plan plan;
    const std::vector<CoreClass>& classes = reading.classes;
    if (classes.size() < 2) {
      return plan;
    }
    plan.simFirst = classes.front().cpus;
    // With two classes the next one is the little one.
    plan.sim = classes.size() >= 3 ? classes[0].cpus | classes[1].cpus : classes[0].cpus;
    const CpuSet notLittle = reading.all & ~classes.back().cpus;
    plan.offLittle = 2 * std::popcount(notLittle) >= std::popcount(reading.all);
    plan.others = plan.offLittle ? notLittle : reading.all;
    return plan;
  }

  /// The kernel's way of listing CPUs: "4-7", "0,2,5-6"; "none" for none.
  [[nodiscard]] inline std::string CpuList(const CpuSet cpus)
  {
    std::string list;
    for (std::size_t first = 0; first < kMaxCpus; ++first) {
      if (((cpus >> first) & 1U) == 0) {
        continue;
      }
      std::size_t last = first;
      while (last + 1 < kMaxCpus && ((cpus >> (last + 1)) & 1U) != 0) {
        ++last;
      }
      list += list.empty() ? "" : ",";
      list += std::to_string(first);
      if (last > first) {
        list += "-" + std::to_string(last);
      }
      first = last;
    }
    return list.empty() ? "none" : list;
  }

  /// A class's name in the report: big, mid and little where there are two or three classes, as the app's replay test names
  /// them; else its CPUs.
  [[nodiscard]] inline std::string Label(const std::vector<CoreClass>& classes, const std::size_t index)
  {
    if (classes.size() == 2) {
      return index == 0 ? "big" : "little";
    }
    if (classes.size() == 3) {
      return index == 0 ? "big" : index == 1 ? "mid" : "little";
    }
    return "cpu" + CpuList(classes[index].cpus);
  }

  /// The report's line of where the threads go, and why, as the program starts.
  [[nodiscard]] inline std::string PlacementLine(const Reading& reading, const Plan& plan)
  {
    if (reading.source == Source::kNone) {
      return "cores: sysfs rates no CPU (cpu_capacity, cpufreq's cpuinfo_max_freq), so the threads go where FAF's init and "
             "Android put them";
    }
    std::string line = "cores:";
    const std::vector<CoreClass>& classes = reading.classes;
    for (std::size_t index = 0; index < classes.size(); ++index) {
      char rating[32];
      if (reading.source == Source::kCapacity) {
        std::snprintf(rating, sizeof rating, "capacity %llu", static_cast<unsigned long long>(classes[index].rating));
      } else {
        std::snprintf(rating, sizeof rating, "%.2f GHz", static_cast<double>(classes[index].rating) / 1e6);
      }
      const std::string cpus = "cpu" + CpuList(classes[index].cpus);
      const std::string label = Label(classes, index);
      line += std::string(index == 0 ? " " : ", ") + (label == cpus ? cpus : label + " " + cpus) + " (" + rating + ")";
    }
    line += reading.source == Source::kCapacity ? " by cpu_capacity" : " by cpuinfo_max_freq";
    if (!plan.Places()) {
      return line + ": one class, so the threads go where FAF's init and Android put them";
    }
    line += "; the sim's thread on cpu" + CpuList(plan.sim);
    if (plan.simFirst != plan.sim) {
      line += ", starting on cpu" + CpuList(plan.simFirst);
    }
    if (plan.offLittle) {
      line += "; the render thread, the issue thread and the workers on cpu" + CpuList(plan.others) +
        ", off the little cores, which the engine sizes its worker pools by";
    } else {
      line += "; the render thread, the issue thread and the workers on every core, as " +
        std::to_string(std::popcount(classes.back().cpus)) + " of the " + std::to_string(std::popcount(reading.all)) +
        " are little";
    }
    return line + "; FAF's init's mask is not taken";
  }

  /// What the cores' watch makes of a placed thread's CPUs as the kernel answers them now.
  enum class Look
  {
    /// Its own, or fewer of them: the kernel answers only the active ones, and a core that is paused or gone off leaves.
    kOwn,
    /// CPUs the plan did not give it, as at the last look: Android holds the process there, as in the back.
    kHeld,
    /// CPUs the plan did not give it, anew: Android moved the process into another of its CPU groups, which set the
    /// threads' CPUs to the group's (on kernels before Linux 6.2).
    kMoved,
  };

  /// `now`: the thread's CPUs as the kernel answers them; `cpus`: those the plan gave it; `seen`: those at the last look.
  [[nodiscard]] inline Look LookAt(const CpuSet now, const CpuSet cpus, const CpuSet seen)
  {
    if ((now & ~cpus) == 0) {
      return Look::kOwn;
    }
    return now == seen ? Look::kHeld : Look::kMoved;
  }

  /// What /proc/<pid>/task/<tid>/stat says of a thread: the CPU it ran on last (field 39, processor), its CPU time in clock
  /// ticks (fields 14 and 15, utime and stime) and when it started, in clock ticks since the boot (field 22, starttime).
  struct ThreadStat
  {
    bool valid = false;
    int processor = -1;
    std::uint64_t cpuTicks = 0;
    std::uint64_t startTime = 0;
  };

  [[nodiscard]] inline ThreadStat ParseStat(const std::string_view text)
  {
    // The thread's name, field 2, is in parentheses, and may hold spaces and parentheses itself.
    const std::size_t close = text.rfind(')');
    if (close == std::string_view::npos) {
      return {};
    }
    ThreadStat stat;
    std::uint64_t utime = 0;
    std::uint64_t stime = 0;
    std::size_t at = close + 1;
    for (int field = 3; field <= 39; ++field) {
      while (at < text.size() && text[at] == ' ') {
        ++at;
      }
      const std::size_t begin = at;
      while (at < text.size() && text[at] != ' ' && text[at] != '\n') {
        ++at;
      }
      const std::string_view token = text.substr(begin, at - begin);
      if (token.empty()) {
        return {};
      }
      std::uint64_t number = 0;
      const bool wanted = field == 14 || field == 15 || field == 22 || field == 39;
      if (wanted && !ParseNumber(token, number)) {
        return {};
      }
      if (field == 14) {
        utime = number;
      } else if (field == 15) {
        stime = number;
      } else if (field == 22) {
        stat.startTime = number;
      } else if (field == 39) {
        if (number >= kMaxCpus) {
          return {};
        }
        stat.processor = static_cast<int>(number);
      }
    }
    stat.cpuTicks = utime + stime;
    stat.valid = true;
    return stat;
  }

  /// What /proc/<pid>/task/<tid>/schedstat says of a thread's time on a CPU, in ns: its first number. False where it has
  /// none, as a kernel without schedstats.
  [[nodiscard]] inline bool ParseRunNanos(const std::string_view text, std::uint64_t& nanos)
  {
    return ParseNumber(text.substr(0, text.find_first_of(" \n")), nanos);
  }

  /// The class of `cpu` among `classes`, -1 for none.
  [[nodiscard]] inline int ClassOf(const std::vector<CoreClass>& classes, const int cpu)
  {
    if (cpu < 0 || static_cast<std::size_t>(cpu) >= kMaxCpus) {
      return -1;
    }
    for (std::size_t index = 0; index < classes.size(); ++index) {
      if (((classes[index].cpus >> cpu) & 1U) != 0) {
        return static_cast<int>(index);
      }
    }
    return -1;
  }

  /// A window of the sim's speed lines: how far its ticks went, from `firstTick` to `lastTick`, and its thread's time on a
  /// CPU, in all and on each class.
  struct SpeedWindow
  {
    double seconds = 0.0;
    std::uint32_t firstTick = 0;
    std::uint32_t lastTick = 0;
    std::uint64_t runNanos = 0;
    /// As Reading::classes. What ran on a CPU of none of them is in runNanos alone.
    std::vector<std::uint64_t> classNanos;
    /// A replay's move (its fast-forward) went on in it, where the sim takes the beats as fast as it can.
    bool moved = false;
    /// The phone's thermal headroom as Android gave it last; none where it is no number.
    float headroom = -1.0f;
  };

  /// The report's line of a window: "sim: 3.6 ticks a second over 30 s (ticks 6120 to 6228), in a replay's move; its thread
  /// on a CPU 97% of the time: big 71%, mid 29%, little 0%; thermal headroom 0.78". The classes' shares are of its time on
  /// a CPU, and only where there are classes to tell apart.
  [[nodiscard]] inline std::string SpeedLine(const SpeedWindow& window, const std::vector<CoreClass>& classes)
  {
    char text[160];
    const double ticks = window.lastTick > window.firstTick ? static_cast<double>(window.lastTick - window.firstTick) : 0.0;
    std::snprintf(text, sizeof text, "sim: %.1f ticks a second over %.0f s (ticks %u to %u)",
      window.seconds > 0.0 ? ticks / window.seconds : 0.0, window.seconds, window.firstTick, window.lastTick);
    std::string line = text;
    if (window.moved) {
      line += ", in a replay's move";
    }
    const double busy = window.seconds > 0.0 ? static_cast<double>(window.runNanos) / (window.seconds * 1e9) : 0.0;
    std::snprintf(text, sizeof text, "; its thread on a CPU %.0f%% of the time", 100.0 * busy);
    line += text;
    if (classes.size() > 1 && window.classNanos.size() == classes.size() && window.runNanos > 0) {
      for (std::size_t index = 0; index < classes.size(); ++index) {
        std::snprintf(text, sizeof text, "%s %s %.0f%%", index == 0 ? ":" : ",", Label(classes, index).c_str(),
          100.0 * static_cast<double>(window.classNanos[index]) / static_cast<double>(window.runNanos));
        line += text;
      }
    }
    if (window.headroom >= 0.0f) {
      std::snprintf(text, sizeof text, "; thermal headroom %.2f", static_cast<double>(window.headroom));
      line += text;
    }
    return line;
  }
} // namespace coreclasses
