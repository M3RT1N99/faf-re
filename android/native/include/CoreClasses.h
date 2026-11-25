#pragma once
// The device's classes of cores, as sysfs rates each CPU, and where the game's threads go by them (src/app/CoresAndroid.cpp):
// the sim's thread on the fastest class and the next, the render thread, the issue thread and the workers off the little
// cores; the report's line of it; and what /proc says of a thread, by which a thread placed is known again. Headers only,
// so that tests/griff pins the choice without Android or the engine.

#include <bit>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
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

  /// The device's classes: by cpu_capacity where every CPU has one, else by cpuinfo_max_freq where every CPU has that. One
  /// text of each for each CPU, cpu0's first; a missing file's is empty.
  [[nodiscard]] inline Reading Read(const std::vector<std::string>& capacities, const std::vector<std::string>& maxFrequencies)
  {
    Reading reading;
    reading.classes = ClassesOf(capacities);
    reading.source = Source::kCapacity;
    if (reading.classes.empty()) {
      reading.classes = ClassesOf(maxFrequencies);
      reading.source = reading.classes.empty() ? Source::kNone : Source::kMaxFrequency;
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
} // namespace coreclasses
