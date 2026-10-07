// The end of the runner process (release 0.4.1). Executable-only (build_runner.py's RUNNER_EXE_SOURCES).
#pragma once

namespace faf_runner
{
  // Called by `main` once the engine thread has returned and the arena's report is out: flushes every
  // stdio stream (stdout, the engine's /log file) and ends the process with _exit(exitCode), so the
  // engine library's static destructors never run. Those cost seconds and change nothing the run
  // reports: ~FWaitHandleSet erases the VFS's zip entries one by one and restarts a linear scan of the
  // archive index after each erase (src/sdk/moho/misc/FileWaitHandleSet.cpp, RemoveEntry), 7-15 s on
  // the emulator and about 10 s on the S22 Ultra after the replay's RESULT line. The summary JSON is
  // written and closed, and the log flushed, by HEADLESS_RunReplay before it returns.
  //
  // FAF_RUNNER_EXIT=full keeps the old way: it returns exitCode, and `main` returns it, so exit() runs
  // every static destructor as before.
  //
  // Prints one line on stderr, "[runner] exit <code>: ...", either way.
  int EndProcess(int exitCode);
} // namespace faf_runner
