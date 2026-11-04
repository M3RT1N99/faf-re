// SetUnhandledExceptionFilter on Android (include/win32/Windows.h). The filter runs on the signals
// of a fault (SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGTRAP), on the faulting thread as Windows runs it,
// with the exception record Windows gives for the fault. Then the signal goes on to the handler
// that was there before: Android's debuggerd writes its tombstone, and the process ends as it would
// have without the filter. EXCEPTION_CONTINUE_EXECUTION is not honoured: it would retry the
// faulting instruction, which no handler here has repaired.

#include <Windows.h>

#include <signal.h>
#include <ucontext.h>

#include <atomic>
#include <cstring>

/// The thread's state at the fault, which the filter may pass on (moho::PLAT_GetCallStack).
struct _CONTEXT
{
  ucontext_t* machine;
  siginfo_t* signal;
};

namespace
{
  constexpr int kSignals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGTRAP};

  std::atomic<LPTOP_LEVEL_EXCEPTION_FILTER> gFilter{nullptr};
  std::atomic<bool> gInstalled{false};
  // A fault inside the filter goes straight on to the handler before; SA_NODEFER lets it reach this
  // one, as blocked it would end the process in the kernel, without a tombstone.
  std::atomic_flag gInFilter = ATOMIC_FLAG_INIT;
  struct sigaction gPrevious[NSIG];

  DWORD ExceptionCode(const int signalNumber, const siginfo_t* const info)
  {
    switch (signalNumber) {
    case SIGSEGV:
      return EXCEPTION_ACCESS_VIOLATION;
    case SIGBUS:
      return info->si_code == BUS_ADRALN ? EXCEPTION_DATATYPE_MISALIGNMENT : EXCEPTION_IN_PAGE_ERROR;
    case SIGFPE:
      switch (info->si_code) {
      case FPE_INTDIV:
        return EXCEPTION_INT_DIVIDE_BY_ZERO;
      case FPE_INTOVF:
        return EXCEPTION_INT_OVERFLOW;
      case FPE_FLTDIV:
        return EXCEPTION_FLT_DIVIDE_BY_ZERO;
      case FPE_FLTOVF:
        return EXCEPTION_FLT_OVERFLOW;
      case FPE_FLTUND:
        return EXCEPTION_FLT_UNDERFLOW;
      case FPE_FLTRES:
        return EXCEPTION_FLT_INEXACT_RESULT;
      case FPE_FLTSUB:
        return EXCEPTION_ARRAY_BOUNDS_EXCEEDED;
      default:
        return EXCEPTION_FLT_INVALID_OPERATION;
      }
    case SIGILL:
      return info->si_code == ILL_PRVOPC || info->si_code == ILL_PRVREG ? EXCEPTION_PRIV_INSTRUCTION
                                                                        : EXCEPTION_ILLEGAL_INSTRUCTION;
    case SIGTRAP:
      return info->si_code == TRAP_TRACE ? EXCEPTION_SINGLE_STEP : EXCEPTION_BREAKPOINT;
    default:
      return EXCEPTION_NONCONTINUABLE_EXCEPTION;
    }
  }

  void* InstructionPointer(const ucontext_t* const context)
  {
#if defined(__aarch64__)
    return reinterpret_cast<void*>(context->uc_mcontext.pc);
#elif defined(__x86_64__)
    return reinterpret_cast<void*>(context->uc_mcontext.gregs[REG_RIP]);
#else
#error "the instruction pointer of this architecture's signal context"
#endif
  }

  /// An access violation's kind as Windows reports it: 0 read, 1 write, 8 execute.
  ULONG_PTR AccessKind(const ucontext_t* const context, const void* const address)
  {
    if (address == InstructionPointer(context)) {
      return 8;
    }
#if defined(__aarch64__)
    // The fault's syndrome (ESR) follows the signal frame's other records; WnR is bit 6.
    const auto* record = reinterpret_cast<const _aarch64_ctx*>(context->uc_mcontext.__reserved);
    const auto* const end =
      reinterpret_cast<const _aarch64_ctx*>(context->uc_mcontext.__reserved + sizeof(context->uc_mcontext.__reserved));
    while (record < end && record->magic != 0 && record->size != 0) {
      if (record->magic == ESR_MAGIC) {
        return (reinterpret_cast<const esr_context*>(record)->esr & (1u << 6)) != 0 ? 1 : 0;
      }
      record = reinterpret_cast<const _aarch64_ctx*>(reinterpret_cast<const char*>(record) + record->size);
    }
    return 0;
#elif defined(__x86_64__)
    // The page fault's error code: bit 1 is a write.
    return (context->uc_mcontext.gregs[REG_ERR] & 2) != 0 ? 1 : 0;
#endif
  }

  /// Gives the signal back to the handler that was there before the filter's.
  void PassOn(const int signalNumber, siginfo_t* const info)
  {
    (void)::sigaction(signalNumber, &gPrevious[signalNumber], nullptr);
    // A fault repeats when the handler returns and reaches the restored handler; a signal some
    // thread sent does not, so it is sent again.
    if (info->si_code <= 0) {
      (void)::raise(signalNumber);
    }
  }

  void OnFault(const int signalNumber, siginfo_t* const info, void* const machine)
  {
    const LPTOP_LEVEL_EXCEPTION_FILTER filter = gFilter.load();
    if (filter != nullptr && !gInFilter.test_and_set()) {
      auto* const context = static_cast<ucontext_t*>(machine);
      EXCEPTION_RECORD record{};
      record.ExceptionCode = ExceptionCode(signalNumber, info);
      record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
      record.ExceptionAddress = InstructionPointer(context);
      if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION || record.ExceptionCode == EXCEPTION_IN_PAGE_ERROR) {
        record.NumberParameters = 2;
        record.ExceptionInformation[0] = AccessKind(context, info->si_addr);
        record.ExceptionInformation[1] = reinterpret_cast<ULONG_PTR>(info->si_addr);
      }
      _CONTEXT state{context, info};
      EXCEPTION_POINTERS pointers{&record, &state};
      (void)filter(&pointers);
    }
    PassOn(signalNumber, info);
  }

  void Install()
  {
    if (gInstalled.exchange(true)) {
      return;
    }
    struct sigaction action{};
    action.sa_sigaction = &OnFault;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
    (void)::sigemptyset(&action.sa_mask);
    for (const int signalNumber : kSignals) {
      (void)::sigaction(signalNumber, &action, &gPrevious[signalNumber]);
    }
  }
} // namespace

LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(const LPTOP_LEVEL_EXCEPTION_FILTER filter)
{
  const LPTOP_LEVEL_EXCEPTION_FILTER previous = gFilter.exchange(filter);
  if (filter != nullptr) {
    Install();
  }
  return previous;
}
