// The runner executable's crash reporter (Android). The engine's own crash-to-summary filter is
// Windows-only (HeadlessReplay.cpp), and an app cannot read the tombstone of a child it exec'd, so
// faf_headless_runner reports a fatal signal itself before the platform's handler (debuggerd) runs:
//
//   [runner] CRASH sig=11 (SIGSEGV) code=1 (SEGV_MAPERR) addr=0x25388150 pc=0x2e9c5f4 (libfafengine.so+0x8ac5f4)
//       lr=0x2e9c5d0 (libfafengine.so+0x8ac5d0) sp=0x7d5f3e0 tid=4711 libfafengine.so base=0x2410000
//       build_id=69fe6ab3013c...
//   [runner] CRASH #00 pc 0x2e9c5f4 (libfafengine.so+0x8ac5f4)
//   [runner] CRASH #01 pc ...          (frame-pointer walk, at most 32 frames)
//
// (the first line is one line; for a signal another process sent, code <= 0 such as SI_USER, it
// names the sender, `sender_pid=N sender_uid=N`, in place of addr=). Offsets are relative to the
// module's load base, so they symbolize
// with `llvm-symbolizer --obj=<unstripped .so> --relative-address`. The handler is async-signal-
// safe: it formats into a stack buffer, writes with write(2), reads stack memory only through
// process_vm_readv (a bad frame pointer ends the walk instead of faulting), and then chains to the
// action that was installed before it (debuggerd's, which writes the tombstone and the logcat crash
// lines and lets the process die with the signal, so the exit status stays 128 + signal).
#pragma once

namespace faf_runner
{
  // SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, plus SIGTRAP (arm64 brk/__builtin_trap) and SIGSYS (a
  // seccomp-blocked system call in an app process), with SA_SIGINFO | SA_ONSTACK. Call once in main,
  // before any thread starts, with argv[0] (the executable's name in the report). It allocates
  // nothing.
  void CrashHandlerInstall(const char* exeName);

  // Records the engine library (or the probe) once it is loaded: its load base, the end of its
  // mapped segments and its build id, read from the loaded image.
  void CrashHandlerSetEngine(const void* base, const char* path);
} // namespace faf_runner
