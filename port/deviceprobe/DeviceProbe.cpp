// libfafdeviceprobe.so: the device probe for the graphics plan (release 0.4.1). An executable (a PIE
// with a lib*.so name, so the APK installer extracts it next to the runner), exec'd by the app like
// libfafrunner.so. See README.md for what it reports and how the app reads it.
//
//   libfafdeviceprobe.so [--out DIR] [--sections vulkan,vulkan_render,gles,gles_render,glslang]
//                        [--timeout SECONDS] [--repeats N] [--no-fork]
//
// Each section runs in a child process of its own (fork, no exec), with a time limit, so a driver
// that crashes or hangs costs that section only; the parent never loads a driver. The child sends its
// result to the parent before it tears the driver objects down (vkDestroyDevice, vkDestroyInstance,
// eglTerminate: faf_probe::Teardown), and then reports how the teardown went, so a driver that crashes
// or hangs in its cleanup loses nothing the section found ("process":{"teardown":{...}}). The parent
// writes DIR/deviceprobe.json (all sections), prints one "[probe] <section>: ..." line per section and
// a final "[probe] RESULT ..." line. The render sections also write DIR/deviceprobe-vulkan.png and
// DIR/deviceprobe-gles.png.
//
// A section's child dies with the parent (PR_SET_PDEATHSIG), so cancelling the probe (SIGTERM or
// SIGKILL to its pid) leaves no process behind.
//
// Exit code: 0 when every section ran to its end (whatever it found, "unsupported" and "error"
// included: those are results; a crash or hang in the driver's teardown after the result was sent
// included too: it is reported, not counted), 1 when a section crashed, hung or gave no output before
// its result, 2 for usage errors (an unknown option, an output directory that cannot be written).

#include "Probe.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/system_properties.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace
{
  using faf_probe::Json;
  using faf_probe::NowMs;
  using faf_probe::Options;
  using faf_probe::SectionResult;

#if defined(__aarch64__)
  constexpr char kAbi[] = "arm64-v8a";
#elif defined(__x86_64__)
  constexpr char kAbi[] = "x86_64";
#else
  constexpr char kAbi[] = "unknown";
#endif

  constexpr int kProbeVersion = 1;

  struct Section
  {
    const char* name;
    SectionResult (*run)(const Options&);
    int timeoutSeconds;
  };

  // Generous limits: the emulator's ARM translation is several times slower than a phone.
  const Section kSections[] = {
    {"vulkan", &faf_probe::RunVulkanReport, 60},
    {"vulkan_render", &faf_probe::RunVulkanRender, 90},
    {"gles", &faf_probe::RunGlesReport, 60},
    {"gles_render", &faf_probe::RunGlesRender, 90},
    {"glslang", &faf_probe::RunGlslang, 300},
  };

  std::string Property(const char* name)
  {
    char value[PROP_VALUE_MAX] = {};
    __system_property_get(name, value);
    return value;
  }

  std::string UtcNow()
  {
    const time_t now = time(nullptr);
    tm parts{};
    gmtime_r(&now, &parts);
    char text[32];
    strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &parts);
    return text;
  }

  // What the probe runs on. No serial numbers or build fingerprints.
  std::string DeviceJson(bool& translated)
  {
    Json d;
    d.Str("manufacturer", Property("ro.product.manufacturer"));
    d.Str("model", Property("ro.product.model"));
    d.Str("soc_manufacturer", Property("ro.soc.manufacturer"));
    d.Str("soc_model", Property("ro.soc.model"));
    d.Str("hardware", Property("ro.hardware"));
    d.Str("board_platform", Property("ro.board.platform"));
    d.Str("android_release", Property("ro.build.version.release"));
    d.Str("sdk", Property("ro.build.version.sdk"));
    d.Str("vulkan_hal", Property("ro.hardware.vulkan"));
    d.Str("egl_hal", Property("ro.hardware.egl"));
    d.Str("gpu_driver_property", Property("ro.gfx.driver.0"));
    d.Str("abilist", Property("ro.product.cpu.abilist"));
    const std::string primary = Property("ro.product.cpu.abi");
    const std::string bridge = Property("ro.dalvik.vm.native.bridge");
    translated = !primary.empty() && primary != kAbi;
    d.Bool("translated", translated);
    if (translated) {
      d.Str("native_bridge", bridge);
    }
    utsname u{};
    if (uname(&u) == 0) {
      d.Str("kernel", u.release);
      d.Str("machine", u.machine);
    }
    d.Num("page_size", sysconf(_SC_PAGESIZE));
    d.Num("cpus", sysconf(_SC_NPROCESSORS_CONF));
    return d.Text();
  }

  void WriteAll(const int fd, const std::string& text)
  {
    size_t done = 0;
    while (done < text.size()) {
      const ssize_t n = write(fd, text.data() + done, text.size() - done);
      if (n < 0 && errno == EINTR) {
        continue;
      }
      if (n <= 0) {
        return;
      }
      done += static_cast<size_t>(n);
    }
  }

  // After a section's result is in, how long its child may take to tear the driver objects down.
  constexpr int kTeardownSeconds = 20;

  struct Outcome
  {
    std::string status;  // the section's own status, or crashed / timeout / no_output
    std::string json;    // the section's object (with "process" added)
    std::string summary;
    bool failed = false;  // crashed, hung or silent: counts against the exit code
    std::string teardown;  // after the result: ok, crashed, timeout, or "" (in-process / no result)
  };

  // The section's own status from its JSON ("status":"...").
  std::string StatusOf(const std::string& json)
  {
    const std::string key = "\"status\":\"";
    const size_t at = json.find(key);
    if (at == std::string::npos) {
      return "?";
    }
    const size_t end = json.find('"', at + key.size());
    return end == std::string::npos ? "?" : json.substr(at + key.size(), end - at - key.size());
  }

  // The verdict of a render section ("pattern":"exact"), or "".
  std::string PatternOf(const std::string& json)
  {
    const std::string key = "\"pattern\":\"";
    const size_t at = json.find(key);
    if (at == std::string::npos) {
      return "";
    }
    const size_t end = json.find('"', at + key.size());
    return end == std::string::npos ? "" : json.substr(at + key.size(), end - at - key.size());
  }

  std::string WithProcess(const std::string& json, const std::string& process)
  {
    if (json.size() < 2 || json.back() != '}') {
      return json;
    }
    return json.substr(0, json.size() - 1) + (json.size() > 2 ? "," : "") + "\"process\":" + process + "}";
  }

  Outcome RunInChild(const Section& section, const Options& options, const int timeoutSeconds)
  {
    Outcome out;
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) {
      out.status = "error";
      out.failed = true;
      out.summary = std::string("cannot create a pipe: ") + strerror(errno);
      Json j;
      j.Str("status", "error");
      j.Str("error", out.summary);
      out.json = j.Text();
      return out;
    }
    fflush(stdout);
    fflush(stderr);
    const double start = NowMs();
    const pid_t parent = getpid();
    const pid_t child = fork();
    if (child < 0) {
      close(fds[0]);
      close(fds[1]);
      out.status = "error";
      out.failed = true;
      out.summary = std::string("cannot fork: ") + strerror(errno);
      Json j;
      j.Str("status", "error");
      j.Str("error", out.summary);
      out.json = j.Text();
      return out;
    }
    if (child == 0) {
      close(fds[0]);
      // When the app cancels the probe (SIGTERM, then SIGKILL, to this parent), the section's child
      // goes too. The parent is single-threaded and lives for the whole run, so the parent-death
      // signal (which follows the forking thread) fires only when the probe itself ends.
      prctl(PR_SET_PDEATHSIG, SIGKILL);
      if (getppid() != parent) {
        _exit(1);
      }
      // Test hook for the parent's crash and timeout handling: FAF_PROBE_TEST_FAULT=crash:<section>
      // or hang:<section> (before the result), teardown-crash:<section> or teardown-hang:<section>
      // (after it, in place of the driver teardown).
      const char* const fault = getenv("FAF_PROBE_TEST_FAULT");
      const std::string faultText = fault != nullptr ? fault : "";
      if (fault != nullptr) {
        if (std::string(fault) == std::string("crash:") + section.name) {
          raise(SIGSEGV);
        }
        if (std::string(fault) == std::string("hang:") + section.name) {
          for (;;) {
            pause();
          }
        }
      }
      // The result first (two lines: the summary, the section's JSON object), then the driver
      // teardown the section's destructors queued, then "teardown ok <ms> <entries>".
      faf_probe::DeferTeardown(true);
      const SectionResult r = section.run(options);
      std::string summary = r.summary;
      for (char& c : summary) {
        c = (c == '\n' || c == '\r') ? ' ' : c;
      }
      std::string json = r.json;
      for (char& c : json) {
        c = (c == '\n' || c == '\r') ? ' ' : c;
      }
      WriteAll(fds[1], summary + "\n" + json + "\n");
      if (faultText == std::string("teardown-crash:") + section.name) {
        raise(SIGSEGV);
      }
      if (faultText == std::string("teardown-hang:") + section.name) {
        for (;;) {
          pause();
        }
      }
      const double teardownStart = NowMs();
      const int entries = faf_probe::RunDeferredTeardown();
      WriteAll(fds[1], "teardown ok " + faf_probe::RealText(NowMs() - teardownStart, 1) + " " +
                           std::to_string(entries) + "\n");
      close(fds[1]);
      _exit(0);
    }
    close(fds[1]);
    std::string data;
    bool timedOut = false;
    double resultAt = -1.0;  // when the result's two lines were complete
    for (;;) {
      if (resultAt < 0) {
        const size_t first = data.find('\n');
        if (first != std::string::npos && data.find('\n', first + 1) != std::string::npos) {
          resultAt = NowMs();
        }
      }
      const double sectionLeft = timeoutSeconds * 1000.0 - (NowMs() - start);
      const double teardownLeft = resultAt < 0 ? sectionLeft : kTeardownSeconds * 1000.0 - (NowMs() - resultAt);
      const double left = sectionLeft < teardownLeft ? sectionLeft : teardownLeft;
      if (left <= 0) {
        timedOut = true;
        break;
      }
      pollfd p{fds[0], POLLIN, 0};
      const int ready = poll(&p, 1, left > 1000 ? 1000 : static_cast<int>(left) + 1);
      if (ready < 0 && errno == EINTR) {
        continue;
      }
      if (ready > 0) {
        char buffer[65536];
        const ssize_t n = read(fds[0], buffer, sizeof(buffer));
        if (n > 0) {
          data.append(buffer, static_cast<size_t>(n));
          continue;
        }
        if (n < 0 && errno == EINTR) {
          continue;
        }
        break;  // EOF: the child closed its end (it is exiting)
      }
    }
    close(fds[0]);
    if (timedOut) {
      kill(child, SIGKILL);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    const double ms = NowMs() - start;

    // "summary\n{json}\n" and, once the driver objects are torn down, "teardown ok <ms> <entries>\n".
    const size_t newline = data.find('\n');
    const size_t second = newline == std::string::npos ? std::string::npos : data.find('\n', newline + 1);
    const std::string resultJson =
      second == std::string::npos ? std::string() : data.substr(newline + 1, second - newline - 1);
    const bool complete = !resultJson.empty() && resultJson.front() == '{' && resultJson.back() == '}';
    std::string teardownLine;
    if (second != std::string::npos) {
      const size_t third = data.find('\n', second + 1);
      teardownLine = data.substr(second + 1, third == std::string::npos ? std::string::npos : third - second - 1);
    }

    Json process;
    process.Real("ms", ms, 1);
    if (WIFEXITED(status)) {
      process.Num("exit_code", WEXITSTATUS(status));
      process.Null("signal");
    } else if (WIFSIGNALED(status)) {
      process.Null("exit_code");
      process.Num("signal", WTERMSIG(status));
    }
    process.Bool("timed_out", timedOut && !complete);
    if (complete) {
      // How the child's driver teardown went, after it had sent the result.
      Json teardown;
      if (teardownLine.compare(0, 12, "teardown ok ") == 0) {
        out.teardown = "ok";
        teardown.Str("status", "ok");
        char* end = nullptr;
        const double teardownMs = strtod(teardownLine.c_str() + 12, &end);
        teardown.Real("ms", teardownMs, 1);
        teardown.Num("entries", end != nullptr ? strtol(end, nullptr, 10) : 0);
      } else if (timedOut) {
        out.teardown = "timeout";
        teardown.Str("status", "timeout");
        teardown.Num("limit_s", kTeardownSeconds);
      } else if (WIFSIGNALED(status)) {
        out.teardown = "crashed";
        teardown.Str("status", "crashed");
        teardown.Num("signal", WTERMSIG(status));
      } else {
        out.teardown = "no_output";
        teardown.Str("status", "no_output");
      }
      process.Raw("teardown", teardown.Text());
    }

    if (complete) {
      out.summary = data.substr(0, newline);
      out.json = WithProcess(resultJson, process.Text());
      out.status = StatusOf(out.json);
      if (out.teardown == "crashed") {
        out.summary += " (then the driver teardown crashed with signal " + std::to_string(WTERMSIG(status)) + ")";
      } else if (out.teardown == "timeout") {
        out.summary += " (then the driver teardown hung: killed after " + std::to_string(kTeardownSeconds) + " s)";
      } else if (out.teardown == "no_output") {
        out.summary += " (then the process ended without finishing the driver teardown)";
      }
      return out;
    }
    Json j;
    out.failed = true;
    if (timedOut) {
      out.status = "timeout";
      out.summary = "no result within " + std::to_string(timeoutSeconds) + " s (killed)";
    } else if (WIFSIGNALED(status)) {
      out.status = "crashed";
      out.summary = "the section's process died with signal " + std::to_string(WTERMSIG(status)) + " (" +
                    strsignal(WTERMSIG(status)) + ")";
    } else {
      out.status = "no_output";
      out.summary = "the section's process exited without a result (exit " +
                    std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : -1) + ")";
    }
    j.Str("status", out.status);
    j.Str("error", out.summary);
    j.Raw("process", process.Text());
    out.json = j.Text();
    return out;
  }

  Outcome RunInProcess(const Section& section, const Options& options)
  {
    const double start = NowMs();
    const SectionResult r = section.run(options);
    Outcome out;
    out.summary = r.summary;
    Json process;
    process.Real("ms", NowMs() - start, 1);
    process.Bool("forked", false);
    process.Str("teardown", "inline");
    out.json = WithProcess(r.json, process.Text());
    out.status = StatusOf(out.json);
    return out;
  }

  bool WriteFileAtomic(const std::string& path, const std::string& text, std::string& error)
  {
    const std::string temp = path + ".tmp";
    const int fd = open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
      error = "cannot create " + temp + ": " + strerror(errno);
      return false;
    }
    WriteAll(fd, text);
    const bool ok = fsync(fd) == 0 || errno == EINVAL;
    close(fd);
    if (!ok || rename(temp.c_str(), path.c_str()) != 0) {
      error = "cannot write " + path + ": " + strerror(errno);
      unlink(temp.c_str());
      return false;
    }
    return true;
  }

  int Usage(const char* self, const char* why)
  {
    if (why != nullptr) {
      fprintf(stderr, "[probe] %s\n", why);
    }
    fprintf(stderr,
            "usage: %s [--out DIR] [--sections vulkan,vulkan_render,gles,gles_render,glslang] [--timeout SECONDS]\n"
            "          [--repeats N] [--no-fork]\n",
            self);
    return 2;
  }
} // namespace

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IOLBF, 0);
  Options options;
  std::vector<std::string> wanted;
  int timeoutOverride = 0;
  bool useFork = true;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto value = [&](const char* what) -> const char* {
      if (i + 1 >= argc) {
        return nullptr;
      }
      (void)what;
      return argv[++i];
    };
    if (arg == "--out") {
      const char* v = value("--out");
      if (v == nullptr) return Usage(argv[0], "--out needs a directory");
      options.outDir = v;
    } else if (arg == "--sections") {
      const char* v = value("--sections");
      if (v == nullptr) return Usage(argv[0], "--sections needs a list");
      std::string list = v;
      size_t at = 0;
      while (at <= list.size()) {
        const size_t comma = list.find(',', at);
        const std::string name = list.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        if (!name.empty()) {
          bool known = false;
          for (const Section& s : kSections) {
            known = known || name == s.name;
          }
          if (!known) return Usage(argv[0], ("unknown section " + name).c_str());
          wanted.push_back(name);
        }
        if (comma == std::string::npos) break;
        at = comma + 1;
      }
    } else if (arg == "--timeout") {
      const char* v = value("--timeout");
      if (v == nullptr || atoi(v) <= 0) return Usage(argv[0], "--timeout needs a number of seconds");
      timeoutOverride = atoi(v);
    } else if (arg == "--repeats") {
      const char* v = value("--repeats");
      if (v == nullptr || atoi(v) <= 0) return Usage(argv[0], "--repeats needs a number");
      options.glslangRepeats = atoi(v);
    } else if (arg == "--no-fork") {
      useFork = false;
    } else if (arg == "--help" || arg == "-h") {
      Usage(argv[0], nullptr);
      return 0;
    } else {
      return Usage(argv[0], ("unknown option " + arg).c_str());
    }
  }
  while (options.outDir.size() > 1 && options.outDir.back() == '/') {
    options.outDir.pop_back();
  }
  if (access(options.outDir.c_str(), W_OK) != 0) {
    return Usage(argv[0], ("cannot write to " + options.outDir + ": " + strerror(errno)).c_str());
  }

  const double start = NowMs();
  bool translated = false;
  const std::string device = DeviceJson(translated);
  printf("[probe] libfafdeviceprobe %d (%s%s), pid %d, output %s\n", kProbeVersion, kAbi,
         translated ? ", under ARM translation" : "", static_cast<int>(getpid()), options.outDir.c_str());

  Json sections;
  Json summaries;
  Json requested('[');
  std::string resultLine = "[probe] RESULT";
  bool anyFailed = false;
  for (const Section& section : kSections) {
    if (!wanted.empty()) {
      bool selected = false;
      for (const std::string& w : wanted) {
        selected = selected || w == section.name;
      }
      if (!selected) {
        continue;
      }
    }
    requested.AddStr(section.name);
    printf("[probe] %s ...\n", section.name);
    const int timeout = timeoutOverride > 0 ? timeoutOverride : section.timeoutSeconds;
    const Outcome outcome = useFork ? RunInChild(section, options, timeout) : RunInProcess(section, options);
    anyFailed = anyFailed || outcome.failed;
    printf("[probe] %s: %s: %s\n", section.name, outcome.status.c_str(), outcome.summary.c_str());
    sections.Raw(section.name, outcome.json);
    summaries.Str(section.name, outcome.status + ": " + outcome.summary);
    const std::string pattern = PatternOf(outcome.json);
    resultLine += std::string(" ") + section.name + "=" + outcome.status + (pattern.empty() ? "" : "/" + pattern) +
                  (outcome.teardown.empty() || outcome.teardown == "ok" ? "" : ",teardown=" + outcome.teardown);
  }

  Json probe;
  probe.Num("version", kProbeVersion);
  probe.Str("abi", kAbi);
  probe.Str("started_utc", UtcNow());
  probe.Num("pid", getpid());
  probe.Bool("forked_sections", useFork);
  probe.Raw("sections_requested", requested.Text());
  probe.Real("ms", NowMs() - start, 1);
  Json root;
  root.Raw("probe", probe.Text());
  root.Raw("device", device);
  root.Raw("summary", summaries.Text());
  root.Raw("sections", sections.Text());
  const std::string path = options.outDir + "/deviceprobe.json";
  std::string error;
  if (WriteFileAtomic(path, root.Text() + "\n", error)) {
    printf("[probe] wrote %s\n", path.c_str());
  } else {
    printf("[probe] %s\n", error.c_str());
    anyFailed = true;
  }
  resultLine += anyFailed ? " exit=1" : " exit=0";
  printf("%s\n", resultLine.c_str());
  return anyFailed ? 1 : 0;
}
