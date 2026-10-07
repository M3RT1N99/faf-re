// The end of the runner process (release 0.4.1): flush, then _exit. See RunnerExit.h.

#include "RunnerExit.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace
{
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

  // "[runner] exit <status><how>": the status the parent sees (0-255).
  void Line(const int code, const char* how)
  {
    const unsigned status = static_cast<unsigned>(code) & 0xFFu;
    char number[4] = {};
    int n = 0;
    if (status >= 100) {
      number[n++] = static_cast<char>('0' + status / 100u);
    }
    if (status >= 10) {
      number[n++] = static_cast<char>('0' + status / 10u % 10u);
    }
    number[n++] = static_cast<char>('0' + status % 10u);
    WriteAll(2, "[runner] exit ", 14);
    WriteAll(2, number, static_cast<size_t>(n));
    WriteAll(2, how, strlen(how));
    WriteAll(2, "\n", 1);
  }
} // namespace

namespace faf_runner
{
  int EndProcess(const int exitCode)
  {
    const char* const mode = getenv("FAF_RUNNER_EXIT");
    if (mode != nullptr && strcmp(mode, "full") == 0) {
      fflush(nullptr);
      Line(exitCode, ": through exit(), the engine's static destructors run (FAF_RUNNER_EXIT=full)");
      return exitCode;
    }
    // Every FILE: stdout (a pipe to the app) and the engine's /log file, which HEADLESS_RunReplay has
    // flushed already but which other engine threads may still have written to since.
    fflush(nullptr);
    Line(exitCode, ": _exit after flushing, without the engine's static destructors");
    _exit(exitCode);
  }
} // namespace faf_runner
