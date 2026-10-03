#include "CrashHandler.h"

#include <android/log.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/prctl.h>
#include <ucontext.h>
#include <unistd.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>

namespace faf::android {

  namespace {

    constexpr char kLogcatTag[] = "faf_android";
    constexpr int kSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP, SIGSYS};
    constexpr int kMaxFrames = 48;

    struct sigaction gPrevious[std::size(kSignals)];
    std::atomic<int> gLogFd{-1};
    std::atomic<bool> gInstalled{false};
    std::atomic<bool> gReporting{false};

    const char* SignalName(const int signal)
    {
      switch (signal) {
      case SIGSEGV:
        return "SIGSEGV";
      case SIGBUS:
        return "SIGBUS";
      case SIGILL:
        return "SIGILL";
      case SIGFPE:
        return "SIGFPE";
      case SIGABRT:
        return "SIGABRT";
      case SIGTRAP:
        return "SIGTRAP";
      case SIGSYS:
        return "SIGSYS";
      default:
        return "signal";
      }
    }

    /// Formats one line into a fixed buffer. The handler runs on a crashed
    /// thread with a possibly corrupt heap, so nothing here allocates.
    class Line
    {
    public:
      Line& Text(const char* text)
      {
        while (text != nullptr && *text != '\0' && mLength + 1 < sizeof(mBuffer)) {
          mBuffer[mLength++] = *text++;
        }
        mBuffer[mLength] = '\0';
        return *this;
      }

      Line& Hex(std::uintptr_t value)
      {
        char digits[2 * sizeof(value)];
        int count = 0;
        do {
          digits[count++] = "0123456789abcdef"[value & 0xF];
          value >>= 4;
        } while (value != 0);
        Text("0x");
        while (count > 0 && mLength + 1 < sizeof(mBuffer)) {
          mBuffer[mLength++] = digits[--count];
        }
        mBuffer[mLength] = '\0';
        return *this;
      }

      Line& Decimal(long value)
      {
        char digits[24];
        int count = 0;
        const bool negative = value < 0;
        unsigned long magnitude = negative ? 0UL - static_cast<unsigned long>(value) : static_cast<unsigned long>(value);
        do {
          digits[count++] = static_cast<char>('0' + magnitude % 10);
          magnitude /= 10;
        } while (magnitude != 0);
        if (negative) {
          Text("-");
        }
        while (count > 0 && mLength + 1 < sizeof(mBuffer)) {
          mBuffer[mLength++] = digits[--count];
        }
        mBuffer[mLength] = '\0';
        return *this;
      }

      /// To logcat (without the newline) and to the log file (with it).
      void Emit()
      {
        __android_log_write(ANDROID_LOG_FATAL, kLogcatTag, mBuffer);
        const int fd = gLogFd.load(std::memory_order_relaxed);
        if (fd >= 0) {
          (void)!write(fd, mBuffer, mLength);
          (void)!write(fd, "\n", 1);
        }
        mLength = 0;
        mBuffer[0] = '\0';
      }

    private:
      char mBuffer[512] = {};
      std::size_t mLength = 0;
    };

    void ReportFrame(const int index, const std::uintptr_t pc)
    {
      Line line;
      line.Text("    #").Decimal(index).Text(" pc ").Hex(pc);
      Dl_info info{};
      if (pc != 0 && dladdr(reinterpret_cast<void*>(pc), &info) != 0 && info.dli_fname != nullptr) {
        const char* const slash = std::strrchr(info.dli_fname, '/');
        line.Text("  ").Text(slash != nullptr ? slash + 1 : info.dli_fname);
        line.Text("+").Hex(pc - reinterpret_cast<std::uintptr_t>(info.dli_fbase));
        if (info.dli_sname != nullptr) {
          line.Text(" (").Text(info.dli_sname).Text("+").Hex(pc - reinterpret_cast<std::uintptr_t>(info.dli_saddr)).Text(")");
        }
      }
      line.Emit();
    }

    /// pc, lr and then the frame-record chain. AArch64 Android code keeps
    /// x29 as the frame pointer, and each record is {previous fp, return
    /// address}. Records are 16-byte aligned and lie above the stack pointer in
    /// increasing order; anything else ends the walk instead of faulting.
    void ReportBacktrace(const ucontext_t* const context)
    {
#if defined(__aarch64__)
      const std::uintptr_t pc = context->uc_mcontext.pc;
      const std::uintptr_t lr = context->uc_mcontext.regs[30];
      std::uintptr_t fp = context->uc_mcontext.regs[29];
      const std::uintptr_t sp = context->uc_mcontext.sp;
      const std::uintptr_t stackLimit = sp + 8u * 1024u * 1024u;

      Line line;
      line.Text("  registers: pc ").Hex(pc).Text(" lr ").Hex(lr).Text(" sp ").Hex(sp).Text(" fp ").Hex(fp);
      line.Emit();
      Line().Text("  backtrace:").Emit();

      int index = 0;
      ReportFrame(index++, pc);
      // The caller of a leaf function is only in lr; for others lr is also the
      // first record's return address, which the walk below then skips.
      ReportFrame(index++, lr);
      std::uintptr_t previous = 0;
      while (index < kMaxFrames && fp != 0 && (fp & 0xF) == 0 && fp >= sp && fp < stackLimit && fp > previous) {
        const auto* const record = reinterpret_cast<const std::uintptr_t*>(fp);
        const std::uintptr_t returnAddress = record[1];
        if (returnAddress == 0) {
          break;
        }
        if (!(index == 2 && returnAddress == lr)) {
          ReportFrame(index++, returnAddress);
        }
        previous = fp;
        fp = record[0];
      }
#else
      (void)context;
      Line().Text("  backtrace: not implemented for this ABI").Emit();
#endif
    }

    void RestorePreviousHandlers()
    {
      for (std::size_t i = 0; i < std::size(kSignals); ++i) {
        sigaction(kSignals[i], &gPrevious[i], nullptr);
      }
    }

    void OnFatalSignal(const int signal, siginfo_t* const info, void* const rawContext)
    {
      // A second fault while reporting (or on another thread at the same
      // time) goes straight to the previous handler.
      if (!gReporting.exchange(true)) {
        char threadName[17] = {};
        prctl(PR_GET_NAME, threadName, 0, 0, 0);
        Line line;
        line.Text("CRASH: ").Text(SignalName(signal)).Text(" (").Decimal(signal).Text("), code ").Decimal(info->si_code);
        line.Text(", fault address ").Hex(reinterpret_cast<std::uintptr_t>(info->si_addr));
        line.Text(", thread '").Text(threadName).Text("' tid ").Decimal(gettid());
        line.Emit();
        ReportBacktrace(static_cast<const ucontext_t*>(rawContext));
        Line().Text("  (offsets are into libfaf_android.so; symbolise with the unstripped copy in buildstage)").Emit();
      }

      RestorePreviousHandlers();
      for (std::size_t i = 0; i < std::size(kSignals); ++i) {
        if (kSignals[i] != signal) {
          continue;
        }
        const struct sigaction& previous = gPrevious[i];
        if ((previous.sa_flags & SA_SIGINFO) != 0 && previous.sa_sigaction != nullptr) {
          previous.sa_sigaction(signal, info, rawContext);
          return;
        }
        if (previous.sa_handler != SIG_DFL && previous.sa_handler != SIG_IGN && previous.sa_handler != nullptr) {
          previous.sa_handler(signal);
          return;
        }
      }
      // Default action. A hardware fault re-faults on return; a sent signal
      // (abort) has to be raised again.
      if (info->si_code <= 0) {
        raise(signal);
      }
    }

  } // namespace

  void InstallCrashHandler(const std::string& logPath)
  {
    const int fd = open(logPath.c_str(), O_WRONLY | O_APPEND | O_CLOEXEC);
    const int old = gLogFd.exchange(fd);
    if (old >= 0) {
      close(old);
    }
    if (gInstalled.exchange(true)) {
      return;
    }
    struct sigaction action{};
    action.sa_sigaction = &OnFatalSignal;
    // bionic gives every thread an alternate signal stack, so a stack
    // overflow still reaches the handler.
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (std::size_t i = 0; i < std::size(kSignals); ++i) {
      sigaction(kSignals[i], &action, &gPrevious[i]);
    }
  }

} // namespace faf::android
