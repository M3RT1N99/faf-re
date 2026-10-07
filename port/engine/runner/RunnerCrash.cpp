// The runner executable's crash reporter; see RunnerCrash.h. Everything the signal handler touches is
// static storage filled in before the crash; it calls only async-signal-safe functions (write,
// sigaction, syscall, getpid, gettid).

#include "RunnerCrash.h"

#include <elf.h>
#include <errno.h>
#include <link.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <ucontext.h>
#include <unistd.h>

namespace faf_runner
{
  namespace
  {
    struct Module
    {
      uintptr_t base;  // load bias (dlpi_addr): the first PT_LOAD's p_vaddr is 0, so offsets are ELF addresses
      uintptr_t end;   // end of the highest PT_LOAD
      char name[64];
      char buildId[48];
    };

    Module gExe{};
    Module gEngine{};
    int gEngineReady = 0;  // __atomic; set after gEngine is filled in

    constexpr int kSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP, SIGSYS};
    constexpr int kMaxSignal = 65;
    struct sigaction gPrevious[kMaxSignal];
    int gBusy = 0;  // __atomic: the first fatal signal reports, later ones only chain

    // ------------------------------------------------------------------------------------------------
    // Formatting without malloc or stdio
    // ------------------------------------------------------------------------------------------------

    struct Line
    {
      char text[768];
      size_t used = 0;

      Line& Str(const char* s)
      {
        while (s != nullptr && *s != '\0' && used + 1 < sizeof(text)) {
          text[used++] = *s++;
        }
        return *this;
      }
      Line& Dec(long long value)
      {
        unsigned long long v = value < 0 ? 0ull - static_cast<unsigned long long>(value) : static_cast<unsigned long long>(value);
        if (value < 0) {
          Str("-");
        }
        char digits[24];
        int n = 0;
        do {
          digits[n++] = static_cast<char>('0' + v % 10u);
          v /= 10u;
        } while (v != 0 && n < 24);
        while (n > 0 && used + 1 < sizeof(text)) {
          text[used++] = digits[--n];
        }
        return *this;
      }
      Line& Hex(unsigned long long v)
      {
        Str("0x");
        char digits[17];
        int n = 0;
        do {
          digits[n++] = "0123456789abcdef"[v & 15u];
          v >>= 4u;
        } while (v != 0 && n < 16);
        while (n > 0 && used + 1 < sizeof(text)) {
          text[used++] = digits[--n];
        }
        return *this;
      }
      void Write()
      {
        if (used + 1 < sizeof(text)) {
          text[used++] = '\n';
        }
        size_t done = 0;
        while (done < used) {
          const ssize_t n = write(2, text + done, used - done);
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

    const char* SignalName(const int sig)
    {
      switch (sig) {
        case SIGSEGV: return "SIGSEGV";
        case SIGBUS: return "SIGBUS";
        case SIGILL: return "SIGILL";
        case SIGFPE: return "SIGFPE";
        case SIGABRT: return "SIGABRT";
        case SIGTRAP: return "SIGTRAP";
        case SIGSYS: return "SIGSYS";
        default: return "?";
      }
    }

    const char* CodeName(const int sig, const int code)
    {
      if (code == SI_USER) return "SI_USER";
      if (code == SI_QUEUE) return "SI_QUEUE";
      if (code == SI_TKILL) return "SI_TKILL";
      switch (sig) {
        case SIGSEGV:
          return code == SEGV_MAPERR ? "SEGV_MAPERR" : code == SEGV_ACCERR ? "SEGV_ACCERR" : "?";
        case SIGBUS:
          return code == BUS_ADRALN ? "BUS_ADRALN" : code == BUS_ADRERR ? "BUS_ADRERR" : code == BUS_OBJERR ? "BUS_OBJERR" : "?";
        case SIGILL:
          return code == ILL_ILLOPC ? "ILL_ILLOPC" : code == ILL_ILLOPN ? "ILL_ILLOPN" : code == ILL_PRVOPC ? "ILL_PRVOPC" : "?";
        case SIGFPE:
          return code == FPE_INTDIV ? "FPE_INTDIV" : code == FPE_FLTDIV ? "FPE_FLTDIV" : code == FPE_FLTINV ? "FPE_FLTINV" : "?";
        case SIGTRAP:
          return code == TRAP_BRKPT ? "TRAP_BRKPT" : code == TRAP_TRACE ? "TRAP_TRACE" : "?";
        case SIGSYS:
          return code == SYS_SECCOMP ? "SYS_SECCOMP" : "?";
        default:
          return "?";
      }
    }

    // One machine word of this process, or false when it is not readable (no fault either way).
    bool ReadWord(const uintptr_t at, uintptr_t* const out)
    {
      iovec local{out, sizeof(*out)};
      iovec remote{reinterpret_cast<void*>(at), sizeof(*out)};
      const long n = syscall(SYS_process_vm_readv, static_cast<long>(getpid()), &local, 1L, &remote, 1L, 0L);
      return n == static_cast<long>(sizeof(*out));
    }

    const Module* ModuleOf(const uintptr_t address)
    {
      if (__atomic_load_n(&gEngineReady, __ATOMIC_ACQUIRE) != 0 && address >= gEngine.base && address < gEngine.end) {
        return &gEngine;
      }
      if (gExe.end != 0 && address >= gExe.base && address < gExe.end) {
        return &gExe;
      }
      return nullptr;
    }

    Line& Where(Line& line, const uintptr_t address)
    {
      line.Hex(address);
      if (const Module* const m = ModuleOf(address)) {
        line.Str(" (").Str(m->name).Str("+").Hex(address - m->base).Str(")");
      }
      return line;
    }

    void Report(const int sig, const siginfo_t* const info, void* const context)
    {
      const ucontext_t* const uc = static_cast<const ucontext_t*>(context);
      uintptr_t pc = 0, sp = 0, fp = 0, lr = 0;
      bool hasLr = false;
#if defined(__aarch64__)
      pc = static_cast<uintptr_t>(uc->uc_mcontext.pc);
      sp = static_cast<uintptr_t>(uc->uc_mcontext.sp);
      fp = static_cast<uintptr_t>(uc->uc_mcontext.regs[29]);
      lr = static_cast<uintptr_t>(uc->uc_mcontext.regs[30]);
      hasLr = true;
#elif defined(__x86_64__)
      pc = static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RIP]);
      sp = static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RSP]);
      fp = static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RBP]);
#endif
      Line line;
      line.Str("[runner] CRASH sig=").Dec(sig).Str(" (").Str(SignalName(sig)).Str(") code=").Dec(info->si_code)
        .Str(" (").Str(CodeName(sig, info->si_code)).Str(")");
      if (info->si_code <= 0) {
        // Sent by a process (kill, tgkill, sigqueue, abort): si_addr would be the pid/uid union.
        line.Str(" sender_pid=").Dec(info->si_pid).Str(" sender_uid=").Dec(info->si_uid);
      } else {
        line.Str(" addr=").Hex(reinterpret_cast<uintptr_t>(info->si_addr));
      }
      line.Str(" pc=");
      Where(line, pc).Str(" lr=");
      if (hasLr) {
        Where(line, lr);
      } else {
        line.Str("-");  // x86_64 has no link register; the return address is frame #01 below
      }
      line.Str(" sp=").Hex(sp).Str(" tid=").Dec(static_cast<long long>(syscall(SYS_gettid)));
      if (sig == SIGSYS) {
        line.Str(" syscall=").Dec(info->si_syscall);
      }
      if (__atomic_load_n(&gEngineReady, __ATOMIC_ACQUIRE) != 0) {
        line.Str(" ").Str(gEngine.name).Str(" base=").Hex(gEngine.base).Str(" build_id=").Str(gEngine.buildId);
      } else {
        line.Str(" (engine library not loaded yet)");
      }
      line.Str(" runner build_id=").Str(gExe.buildId[0] != '\0' ? gExe.buildId : "?");
      line.Write();

      // Frame-pointer walk: the engine keeps frame pointers (built without optimisation; arm64
      // always has frame records). Each record is {previous fp, return address}.
      Line frame;
      frame.Str("[runner] CRASH #00 pc ");
      Where(frame, pc).Write();
      int depth = 1;
      for (; depth < 32; ++depth) {
        uintptr_t next = 0, ret = 0;
        if (fp == 0 || (fp & 7u) != 0 || !ReadWord(fp, &next) || !ReadWord(fp + sizeof(uintptr_t), &ret) || ret == 0) {
          break;
        }
        Line l;
        l.Str("[runner] CRASH #").Str(depth < 10 ? "0" : "").Dec(depth).Str(" pc ");
        Where(l, ret).Write();
        if (next <= fp || next - fp > (uintptr_t{16} << 20)) {
          break;
        }
        fp = next;
      }
    }

    void Chain(const int sig, siginfo_t* const info, void* const context)
    {
      const struct sigaction& previous = gPrevious[sig];
      if ((previous.sa_flags & SA_SIGINFO) != 0 && previous.sa_sigaction != nullptr) {
        previous.sa_sigaction(sig, info, context);  // debuggerd: tombstone, then the signal again with SIG_DFL
        return;
      }
      if (previous.sa_handler != SIG_DFL && previous.sa_handler != SIG_IGN && previous.sa_handler != nullptr) {
        previous.sa_handler(sig);
        return;
      }
      // The default action: die with this signal. A fault re-executes and meets SIG_DFL; a sent
      // signal (abort, kill) is sent again and delivered when this handler returns.
      struct sigaction fallback{};
      fallback.sa_handler = SIG_DFL;
      sigemptyset(&fallback.sa_mask);
      sigaction(sig, &fallback, nullptr);
      if (info->si_code <= 0) {
        syscall(SYS_tgkill, static_cast<long>(getpid()), static_cast<long>(syscall(SYS_gettid)), static_cast<long>(sig));
      }
    }

    void Handler(const int sig, siginfo_t* const info, void* const context)
    {
      const int savedErrno = errno;
      if (__atomic_exchange_n(&gBusy, 1, __ATOMIC_ACQ_REL) == 0) {
        Report(sig, info, context);
      }
      errno = savedErrno;
      Chain(sig, info, context);
    }

    // ------------------------------------------------------------------------------------------------
    // Modules (outside the signal handler)
    // ------------------------------------------------------------------------------------------------

    struct Lookup
    {
      uintptr_t address;  // a byte inside the wanted module
      Module* out;
      bool found;
    };

    void ReadBuildId(const dl_phdr_info* info, Module* out)
    {
      out->buildId[0] = '\0';
      for (int i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr)& ph = info->dlpi_phdr[i];
        if (ph.p_type != PT_NOTE) {
          continue;
        }
        const uint8_t* p = reinterpret_cast<const uint8_t*>(info->dlpi_addr + ph.p_vaddr);
        const uint8_t* const end = p + ph.p_memsz;
        while (p + sizeof(ElfW(Nhdr)) <= end) {
          const ElfW(Nhdr)* const note = reinterpret_cast<const ElfW(Nhdr)*>(p);
          const uint8_t* const name = p + sizeof(ElfW(Nhdr));
          const uint8_t* const desc = name + ((note->n_namesz + 3u) & ~3u);
          const uint8_t* const next = desc + ((note->n_descsz + 3u) & ~3u);
          if (next > end) {
            break;
          }
          if (note->n_type == NT_GNU_BUILD_ID && note->n_namesz == 4 && memcmp(name, "GNU", 4) == 0) {
            size_t n = 0;
            for (uint32_t b = 0; b < note->n_descsz && n + 2 < sizeof(out->buildId); ++b) {
              out->buildId[n++] = "0123456789abcdef"[desc[b] >> 4];
              out->buildId[n++] = "0123456789abcdef"[desc[b] & 15u];
            }
            out->buildId[n] = '\0';
            return;
          }
          p = next;
        }
      }
    }

    int FindModule(dl_phdr_info* info, size_t, void* raw)
    {
      Lookup* const lookup = static_cast<Lookup*>(raw);
      uintptr_t low = UINTPTR_MAX, high = 0;
      for (int i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr)& ph = info->dlpi_phdr[i];
        if (ph.p_type != PT_LOAD) {
          continue;
        }
        const uintptr_t start = info->dlpi_addr + ph.p_vaddr;
        const uintptr_t stop = start + ph.p_memsz;
        low = start < low ? start : low;
        high = stop > high ? stop : high;
      }
      if (lookup->address < low || lookup->address >= high) {
        return 0;
      }
      lookup->out->base = info->dlpi_addr;
      lookup->out->end = high;
      ReadBuildId(info, lookup->out);
      lookup->found = true;
      return 1;
    }

    void CopyName(Module* out, const char* path)
    {
      const char* base = path;
      for (const char* p = path; p != nullptr && *p != '\0'; ++p) {
        if (*p == '/') {
          base = p + 1;
        }
      }
      size_t n = 0;
      while (base != nullptr && base[n] != '\0' && n + 1 < sizeof(out->name)) {
        out->name[n] = base[n];
        ++n;
      }
      out->name[n] = '\0';
    }
  } // namespace

  void CrashHandlerInstall(const char* exeName)
  {
    Lookup self{reinterpret_cast<uintptr_t>(&CrashHandlerInstall), &gExe, false};
    dl_iterate_phdr(&FindModule, &self);
    if (self.found) {
      CopyName(&gExe, exeName != nullptr && exeName[0] != '\0' ? exeName : "faf_headless_runner");
    } else {
      gExe = Module{};
    }
    for (const int sig : kSignals) {
      struct sigaction action{};
      action.sa_sigaction = &Handler;
      action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
      sigemptyset(&action.sa_mask);
      sigaction(sig, &action, &gPrevious[sig]);
    }
  }

  void CrashHandlerSetEngine(const void* base, const char* path)
  {
    Module module{};
    Lookup lookup{reinterpret_cast<uintptr_t>(base), &module, false};
    dl_iterate_phdr(&FindModule, &lookup);
    if (!lookup.found) {
      return;
    }
    CopyName(&module, path);
    gEngine = module;
    __atomic_store_n(&gEngineReady, 1, __ATOMIC_RELEASE);
  }
} // namespace faf_runner
