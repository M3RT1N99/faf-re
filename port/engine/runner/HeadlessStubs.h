// Runner-only stand-ins for user-side engine code (docs/port/android-roadmap.md, M3b). Port code,
// never part of main.exe: scripts/port/build_runner.py compiles port/engine/runner/*.cpp into
// libfafengine.so next to the closure TUs (port/engine/runner/closure.txt).
//
// The Windows headless runner (moho/app/HeadlessReplay.cpp) is main.exe, so it links every TU. The
// Android runner links only the closure; the user-side TUs outside it (UI, renderer, audio, wx, live
// networking, the user session) cannot be linked, and the closure still references some of their
// functions and variables. HeadlessStubs*.cpp define those, under the M3b stub rule:
//
//  - only user-side code;
//  - each definition gives what the real one gives *in the Windows headless runner's state*: no wx
//    application, no D3D device, no CWldSession, no sound engine, no UI, no live networking. Every
//    definition cites the real one (file:line) and says why the result is the same;
//  - sim-relevant code is never stubbed (it is ported instead), and a function whose real result in
//    that state is something the runner uses is not stubbed either (see the M3b S report).
//
// Every function here logs its first call once (FAF_RUNNER_STUB), so a run shows which stand-ins it
// reached. Some are reached in every run by design (static initialisers, the loader); their comments
// say so. Variables cannot log; they hold the value the real definition starts with, and nothing in
// the Windows runner changes it.

#pragma once

#include <atomic>

namespace faf_runner
{
  // Writes "[stub] <name>" to stderr and through gpg::Warnf. stderr because a stub can run before the
  // engine's log has a target (static initialisation, or a run without /log).
  void StubReached(const char* name) noexcept;

  // For a function the Windows headless runner never calls and whose real result in the runner's
  // state cannot be reproduced (a constructor of a class the runner cannot link, the D3D9 device):
  // reports "[stub] <name>" as above, then ends the run through gpg::Die (the runner's die handler
  // writes the summary). Reaching one is a finding for M3c, never a silent difference.
  [[noreturn]] void TrapReached(const char* name);
} // namespace faf_runner

// Body of a trap: see faf_runner::TrapReached.
#define FAF_RUNNER_TRAP(name) ::faf_runner::TrapReached(name)

// First call of the enclosing stand-in: report it once per process.
#define FAF_RUNNER_STUB(name)                                                   \
  do {                                                                          \
    static std::atomic<bool> fafRunnerStubReported{false};                      \
    if (!fafRunnerStubReported.exchange(true, std::memory_order_relaxed)) {     \
      ::faf_runner::StubReached(name);                                          \
    }                                                                           \
  } while (false)
