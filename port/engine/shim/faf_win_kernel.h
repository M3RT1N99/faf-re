#pragma once

// Win32 kernel objects and thread services for the non-Windows targets of the
// port, over pthreads: events, mutexes, semaphores, the wait functions,
// asynchronous procedure calls (APCs), thread handles, priority and affinity,
// system and version queries, and modules (dlopen). Included by
// faf_win_compat.h (<windows.h>); see there for the conventions (C linkage,
// Win32 widths).
//
// The engine relies on exact Win32 semantics here, and the sim path uses all
// of it: the driver's manual-reset "sync data available" event and auto-reset
// connection event (moho/sim/SimDriver.cpp:763), the session-info worker's
// event/semaphore/mutex trio (moho/sim/WldSessionInfo.cpp:163), the client
// manager's beat event (moho/net/CLocalClient.cpp:91,
// moho/net/CReplayClient.cpp:43), and THREAD_InvokeAsync/InvokeWait
// (moho/core/Thread.cpp), which queue APCs that run only while the target
// thread sleeps alertably (SleepEx(.., TRUE) in CSimDriver::PerformNextEvent
// and CReplayClient, WaitForSingleObjectEx(.., TRUE) in WaitCtx::block).
//
// The model:
// - A HANDLE is a pointer to a heap object that starts with a KernelObject
//   header (a magic word, the kind, a reference count). CloseHandle drops a
//   reference; a wait holds one for its duration, as the Windows object
//   manager does, so closing a handle that another thread waits on is safe.
//   The pseudo-handles of GetCurrentProcess (-1) and GetCurrentThread (-2)
//   are accepted where Windows accepts them.
// - All synchronisation state lives under one process-wide lock. A waiting
//   thread links a WaitBlock into each object it waits on and sleeps on its
//   own condition variable (CLOCK_MONOTONIC); signalling an object wakes only
//   the threads waiting on it. A woken thread re-checks every object of its
//   wait, so WaitForMultipleObjects with bWaitAll acquires all objects in one
//   step, and an auto-reset event or a semaphore count releases exactly as
//   many waiters as Windows would (which of several waiters wins is not
//   specified on Windows either).
// - Satisfying a wait: an event is signalled (an auto-reset event is reset by
//   the wait that takes it); a mutex is free or already owned by the waiting
//   thread (recursive ownership, released by ReleaseMutex); a semaphore count
//   is above zero (taken by one). An object that is signalled when the wait
//   starts satisfies it before any pending APC runs, as on Windows.
// - APCs: QueueUserAPC appends to the target thread's queue. An alertable
//   wait (SleepEx, WaitFor*ObjectEx with bAlertable) that finds the queue
//   non-empty runs every queued APC on the waiting thread, in order, outside
//   the lock, and returns WAIT_IO_COMPLETION. A thread that never waits
//   alertably never runs its APCs, exactly as on Windows. APCs still queued
//   when a thread exits are discarded.
// - Named objects share one per-process namespace (CreateMutexA with a name in
//   gpg/core/utils/BoostWrappers.cpp:84): creating an existing name returns the
//   same object with ERROR_ALREADY_EXISTS. Nothing is shared across processes.
//
// Not here: waiting on thread, process or file handles; abandoned mutexes
// (WAIT_ABANDONED: a mutex whose owner exits stays owned); PulseEvent;
// security attributes and access masks (accepted and ignored).
//
// The state lives in function-local statics of inline functions, so there is
// one instance per linked image. The engine links into one shared library
// (libfafengine.so, scripts/port/build_runner.py), whose code all shares it.

#if defined(_WIN32) || defined(_MSC_VER)
#error "port/engine/shim is for non-Windows targets; it must not be on a Windows include path"
#endif

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

extern "C++" {

// ---------------------------------------------------------------------------
// Error codes (winerror.h). GetLastError() values the shim sets.
// ---------------------------------------------------------------------------
#define ERROR_INVALID_FUNCTION 1L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_PATH_NOT_FOUND 3L
#define ERROR_TOO_MANY_OPEN_FILES 4L
#define ERROR_ACCESS_DENIED 5L
#define ERROR_INVALID_HANDLE 6L
#define ERROR_NOT_ENOUGH_MEMORY 8L
#define ERROR_OUTOFMEMORY 14L
#define ERROR_NOT_SAME_DEVICE 17L
#define ERROR_NO_MORE_FILES 18L
#define ERROR_WRITE_PROTECT 19L
#define ERROR_BAD_LENGTH 24L
#define ERROR_GEN_FAILURE 31L
#define ERROR_SHARING_VIOLATION 32L
#define ERROR_HANDLE_EOF 38L
#define ERROR_NOT_SUPPORTED 50L
#define ERROR_FILE_EXISTS 80L
#define ERROR_INVALID_PARAMETER 87L
#define ERROR_BROKEN_PIPE 109L
#define ERROR_DISK_FULL 112L
#define ERROR_INSUFFICIENT_BUFFER 122L
#define ERROR_INVALID_NAME 123L
#define ERROR_MOD_NOT_FOUND 126L
#define ERROR_PROC_NOT_FOUND 127L
#define ERROR_NEGATIVE_SEEK 131L
#define ERROR_DIR_NOT_EMPTY 145L
#define ERROR_BUSY 170L
#define ERROR_ALREADY_EXISTS 183L
#define ERROR_FILENAME_EXCED_RANGE 206L
#define ERROR_DIRECTORY 267L
#define ERROR_NOT_OWNER 288L
#define ERROR_TOO_MANY_POSTS 298L
#define ERROR_MR_MID_NOT_FOUND 317L
#define ERROR_INVALID_ADDRESS 487L
#define ERROR_FILE_INVALID 1006L
#define ERROR_TIMEOUT 1460L

// ---------------------------------------------------------------------------
// Wait results and limits (winbase.h, winnt.h)
// ---------------------------------------------------------------------------
#define MAXIMUM_WAIT_OBJECTS 64
#define STATUS_WAIT_0 ((DWORD)0x00000000L)
#define STATUS_ABANDONED_WAIT_0 ((DWORD)0x00000080L)
#define STATUS_USER_APC ((DWORD)0x000000C0L)
#define WAIT_ABANDONED_0 ((STATUS_ABANDONED_WAIT_0) + 0)
#define WAIT_IO_COMPLETION STATUS_USER_APC

// ---------------------------------------------------------------------------
// Threads, processors, versions (winbase.h, winnt.h)
// ---------------------------------------------------------------------------
#define THREAD_BASE_PRIORITY_LOWRT 15
#define THREAD_BASE_PRIORITY_MAX 2
#define THREAD_BASE_PRIORITY_MIN (-2)
#define THREAD_BASE_PRIORITY_IDLE (-15)
#define THREAD_PRIORITY_LOWEST THREAD_BASE_PRIORITY_MIN
#define THREAD_PRIORITY_BELOW_NORMAL (THREAD_PRIORITY_LOWEST + 1)
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_HIGHEST THREAD_BASE_PRIORITY_MAX
#define THREAD_PRIORITY_ABOVE_NORMAL (THREAD_PRIORITY_HIGHEST - 1)
#define THREAD_PRIORITY_ERROR_RETURN (MAXLONG)
#define THREAD_PRIORITY_TIME_CRITICAL THREAD_BASE_PRIORITY_LOWRT
#define THREAD_PRIORITY_IDLE THREAD_BASE_PRIORITY_IDLE
#ifndef MAXLONG
#define MAXLONG 0x7fffffff
#endif

#define PROCESSOR_ARCHITECTURE_INTEL 0
#define PROCESSOR_ARCHITECTURE_ARM 5
#define PROCESSOR_ARCHITECTURE_AMD64 9
#define PROCESSOR_ARCHITECTURE_ARM64 12
#define PROCESSOR_ARCHITECTURE_UNKNOWN 0xffff

#define VER_PLATFORM_WIN32s 0
#define VER_PLATFORM_WIN32_WINDOWS 1
#define VER_PLATFORM_WIN32_NT 2

// DllMain / TLS-callback reasons; PIMAGE_TLS_CALLBACK as winnt.h spells it.
#define DLL_PROCESS_DETACH 0
#define DLL_PROCESS_ATTACH 1
#define DLL_THREAD_ATTACH 2
#define DLL_THREAD_DETACH 3
typedef void(NTAPI* PIMAGE_TLS_CALLBACK)(PVOID DllHandle, DWORD Reason, PVOID Reserved);

// Structured-exception filter results (excpt.h). There is no SEH off Windows;
// the values exist for code that names them outside a __try.
#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0
#define EXCEPTION_CONTINUE_EXECUTION (-1)

typedef UCHAR* PUCHAR;
typedef ULONGLONG* PULONGLONG;
typedef DWORD_PTR* PDWORD_PTR;

typedef struct _SECURITY_ATTRIBUTES {
  DWORD nLength;
  LPVOID lpSecurityDescriptor;
  BOOL bInheritHandle;
} SECURITY_ATTRIBUTES, *PSECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

typedef VOID(NTAPI* PAPCFUNC)(ULONG_PTR Parameter);

typedef struct _SYSTEM_INFO {
  union {
    DWORD dwOemId;
    struct {
      WORD wProcessorArchitecture;
      WORD wReserved;
    };
  };
  DWORD dwPageSize;
  LPVOID lpMinimumApplicationAddress;
  LPVOID lpMaximumApplicationAddress;
  DWORD_PTR dwActiveProcessorMask;
  DWORD dwNumberOfProcessors;
  DWORD dwProcessorType;
  DWORD dwAllocationGranularity;
  WORD wProcessorLevel;
  WORD wProcessorRevision;
} SYSTEM_INFO, *LPSYSTEM_INFO;

typedef struct _OSVERSIONINFOA {
  DWORD dwOSVersionInfoSize;
  DWORD dwMajorVersion;
  DWORD dwMinorVersion;
  DWORD dwBuildNumber;
  DWORD dwPlatformId;
  CHAR szCSDVersion[128];
} OSVERSIONINFOA, *POSVERSIONINFOA, *LPOSVERSIONINFOA;

typedef struct _OSVERSIONINFOW {
  DWORD dwOSVersionInfoSize;
  DWORD dwMajorVersion;
  DWORD dwMinorVersion;
  DWORD dwBuildNumber;
  DWORD dwPlatformId;
  WCHAR szCSDVersion[128];
} OSVERSIONINFOW, *POSVERSIONINFOW, *LPOSVERSIONINFOW;

typedef struct _OSVERSIONINFOEXW {
  DWORD dwOSVersionInfoSize;
  DWORD dwMajorVersion;
  DWORD dwMinorVersion;
  DWORD dwBuildNumber;
  DWORD dwPlatformId;
  WCHAR szCSDVersion[128];
  WORD wServicePackMajor;
  WORD wServicePackMinor;
  WORD wSuiteMask;
  BYTE wProductType;
  BYTE wReserved;
} OSVERSIONINFOEXW, *POSVERSIONINFOEXW, *LPOSVERSIONINFOEXW;

// ---------------------------------------------------------------------------
// Kernel objects
// ---------------------------------------------------------------------------
namespace faf_compat
{
  enum class KernelKind : uint32_t {
    Event = 1,
    Mutex,
    Semaphore,
    Thread,
    File,
    FileMapping,
    FindFile,
  };

  constexpr uint32_t kKernelObjectMagic = 0x4B464146u; // "FAFK"
  constexpr uint32_t kDeadKernelObjectMagic = 0x44414544u;

  struct ThreadRecord;

  // One thread's wait on one object, linked into the object while it sleeps.
  struct WaitBlock {
    WaitBlock* next;
    WaitBlock* previous;
    ThreadRecord* thread;
  };

  struct KernelObject {
    uint32_t magic;
    KernelKind kind;
    int32_t references;              // under KernelLock()
    char* name;                      // named objects only (malloc'd UTF-8)
    KernelObject* nextNamed;
    WaitBlock* waiters;              // sync objects only
    void (*destroy)(KernelObject*);  // kind-specific teardown before free(), or null
  };

  struct EventObject : KernelObject {
    bool manualReset;
    bool signaled;
  };

  struct MutexObject : KernelObject {
    pid_t owner;  // 0 = free
    uint32_t recursion;
  };

  struct SemaphoreObject : KernelObject {
    LONG count;
    LONG maximum;
  };

  struct ThreadObject : KernelObject {
    pid_t tid;
  };

  struct ApcEntry {
    PAPCFUNC routine;
    ULONG_PTR parameter;
    ApcEntry* next;
  };

  // Per-thread wait state. Created by the thread's first wait, or by a
  // QueueUserAPC aimed at it before that (the thread adopts it by id).
  struct ThreadRecord {
    pid_t tid;
    pthread_cond_t wake;
    bool alertable;  // inside an alertable wait right now
    ApcEntry* apcHead;
    ApcEntry* apcTail;
    ThreadRecord* next;
  };

  inline pthread_mutex_t* KernelLock() noexcept
  {
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    return &lock;
  }

  inline ThreadRecord*& ThreadRecords() noexcept
  {
    static ThreadRecord* head = nullptr;
    return head;
  }

  inline KernelObject*& NamedObjects() noexcept
  {
    static KernelObject* head = nullptr;
    return head;
  }

  class KernelLockGuard
  {
  public:
    KernelLockGuard() noexcept { pthread_mutex_lock(KernelLock()); }
    ~KernelLockGuard() { pthread_mutex_unlock(KernelLock()); }
    KernelLockGuard(const KernelLockGuard&) = delete;
    KernelLockGuard& operator=(const KernelLockGuard&) = delete;
  };

  inline pid_t CurrentTid() noexcept
  {
    return gettid();
  }

  // A new object of kind T with one reference, or null (ERROR_NOT_ENOUGH_MEMORY).
  template <class T>
  inline T* AllocateKernelObject(const KernelKind kind) noexcept
  {
    T* const object = static_cast<T*>(calloc(1, sizeof(T)));
    if (object == nullptr) {
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return nullptr;
    }
    object->magic = kKernelObjectMagic;
    object->kind = kind;
    object->references = 1;
    return object;
  }

  // The object behind a real handle; null for null, INVALID_HANDLE_VALUE, the
  // pseudo-handles and anything that is not a live shim object.
  inline KernelObject* ObjectFromHandle(HANDLE handle) noexcept
  {
    const intptr_t value = reinterpret_cast<intptr_t>(handle);
    if (handle == nullptr || value == -1 || value == -2) {
      return nullptr;
    }
    KernelObject* const object = static_cast<KernelObject*>(handle);
    return object->magic == kKernelObjectMagic ? object : nullptr;
  }

  inline bool IsSyncKind(const KernelKind kind) noexcept
  {
    return kind == KernelKind::Event || kind == KernelKind::Mutex || kind == KernelKind::Semaphore;
  }

  // Drops one reference (lock held). True when it was the last: the object is
  // unpublished and must be passed to DestroyKernelObject after unlocking.
  inline bool ReleaseKernelReference(KernelObject* object) noexcept
  {
    if (--object->references > 0) {
      return false;
    }
    if (object->name != nullptr) {
      for (KernelObject** link = &NamedObjects(); *link != nullptr; link = &(*link)->nextNamed) {
        if (*link == object) {
          *link = object->nextNamed;
          break;
        }
      }
    }
    object->magic = kDeadKernelObjectMagic;
    return true;
  }

  inline void DestroyKernelObject(KernelObject* object) noexcept
  {
    if (object->destroy != nullptr) {
      object->destroy(object);
    }
    free(object->name);
    free(object);
  }

  // Publishes a new sync object under `name`, or returns the existing object of
  // that name (one more reference, ERROR_ALREADY_EXISTS) and frees the new one.
  inline HANDLE PublishKernelObject(KernelObject* object, const char* name) noexcept
  {
    if (object == nullptr) {
      return nullptr;
    }
    if (name == nullptr || name[0] == '\0') {
      SetLastError(ERROR_SUCCESS);
      return object;
    }
    char* const nameCopy = strdup(name);
    if (nameCopy == nullptr) {
      free(object);
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return nullptr;
    }
    {
      KernelLockGuard guard;
      for (KernelObject* existing = NamedObjects(); existing != nullptr; existing = existing->nextNamed) {
        if (strcmp(existing->name, nameCopy) != 0) {
          continue;
        }
        const bool sameKind = existing->kind == object->kind;
        free(nameCopy);
        free(object);
        if (!sameKind) {
          SetLastError(ERROR_INVALID_HANDLE);
          return nullptr;
        }
        ++existing->references;
        SetLastError(ERROR_ALREADY_EXISTS);
        return existing;
      }
      object->name = nameCopy;
      object->nextNamed = NamedObjects();
      NamedObjects() = object;
    }
    SetLastError(ERROR_SUCCESS);
    return object;
  }

  // A kernel-object name from a wide string; false if it does not convert.
  inline bool NarrowObjectName(LPCWSTR name, char* out, const size_t size) noexcept
  {
    if (name == nullptr) {
      out[0] = '\0';
      return true;
    }
    return WideToUtf8(name, out, size);
  }

  // --- threads ---------------------------------------------------------------

  inline void ReleaseThreadRecord(void* value) noexcept;

  inline pthread_key_t ThreadRecordKey() noexcept
  {
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    static pthread_key_t key;
    pthread_once(&once, [] { (void)pthread_key_create(&key, &ReleaseThreadRecord); });
    return key;
  }

  inline ThreadRecord* FindThreadRecord(const pid_t tid) noexcept
  {
    for (ThreadRecord* record = ThreadRecords(); record != nullptr; record = record->next) {
      if (record->tid == tid) {
        return record;
      }
    }
    return nullptr;
  }

  // Lock held.
  inline ThreadRecord* NewThreadRecord(const pid_t tid) noexcept
  {
    ThreadRecord* const record = static_cast<ThreadRecord*>(calloc(1, sizeof(ThreadRecord)));
    if (record == nullptr) {
      return nullptr;
    }
    pthread_condattr_t attributes;
    pthread_condattr_init(&attributes);
    pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
    pthread_cond_init(&record->wake, &attributes);
    pthread_condattr_destroy(&attributes);
    record->tid = tid;
    record->next = ThreadRecords();
    ThreadRecords() = record;
    return record;
  }

  // The calling thread's record (lock held); null only when out of memory.
  inline ThreadRecord* CurrentThreadRecord() noexcept
  {
    const pthread_key_t key = ThreadRecordKey();
    if (ThreadRecord* const record = static_cast<ThreadRecord*>(pthread_getspecific(key))) {
      return record;
    }
    const pid_t tid = CurrentTid();
    ThreadRecord* record = FindThreadRecord(tid);
    if (record == nullptr) {
      record = NewThreadRecord(tid);
    }
    if (record != nullptr) {
      (void)pthread_setspecific(key, record);
    }
    return record;
  }

  inline void FreeApcList(ApcEntry* entry) noexcept
  {
    while (entry != nullptr) {
      ApcEntry* const next = entry->next;
      free(entry);
      entry = next;
    }
  }

  // Thread exit (pthread key destructor): unregister and discard pending APCs.
  inline void ReleaseThreadRecord(void* value) noexcept
  {
    ThreadRecord* const record = static_cast<ThreadRecord*>(value);
    ApcEntry* pending = nullptr;
    {
      KernelLockGuard guard;
      for (ThreadRecord** link = &ThreadRecords(); *link != nullptr; link = &(*link)->next) {
        if (*link == record) {
          *link = record->next;
          break;
        }
      }
      pending = record->apcHead;
    }
    FreeApcList(pending);
    pthread_cond_destroy(&record->wake);
    free(record);
  }

  // Runs the calling thread's queued APCs until the queue stays empty: those
  // queued by an APC run in the same delivery, as on Windows. Lock not held.
  inline void RunQueuedApcs(ApcEntry* entries) noexcept
  {
    while (entries != nullptr) {
      while (entries != nullptr) {
        ApcEntry* const entry = entries;
        entries = entry->next;
        entry->routine(entry->parameter);
        free(entry);
      }
      KernelLockGuard guard;
      ThreadRecord* const self = CurrentThreadRecord();
      if (self != nullptr) {
        entries = self->apcHead;
        self->apcHead = nullptr;
        self->apcTail = nullptr;
      }
    }
  }

  // --- waits -----------------------------------------------------------------

  inline bool IsSignaledFor(const KernelObject* object, const pid_t tid) noexcept
  {
    switch (object->kind) {
      case KernelKind::Event:
        return static_cast<const EventObject*>(object)->signaled;
      case KernelKind::Mutex: {
        const pid_t owner = static_cast<const MutexObject*>(object)->owner;
        return owner == 0 || owner == tid;
      }
      case KernelKind::Semaphore:
        return static_cast<const SemaphoreObject*>(object)->count > 0;
      default:
        return false;
    }
  }

  inline void AcquireSignaled(KernelObject* object, const pid_t tid) noexcept
  {
    switch (object->kind) {
      case KernelKind::Event: {
        EventObject* const event = static_cast<EventObject*>(object);
        if (!event->manualReset) {
          event->signaled = false;
        }
        break;
      }
      case KernelKind::Mutex: {
        MutexObject* const mutex = static_cast<MutexObject*>(object);
        mutex->owner = tid;
        ++mutex->recursion;
        break;
      }
      case KernelKind::Semaphore:
        --static_cast<SemaphoreObject*>(object)->count;
        break;
      default:
        break;
    }
  }

  // Lock held.
  inline void WakeWaiters(const KernelObject* object) noexcept
  {
    for (const WaitBlock* block = object->waiters; block != nullptr; block = block->next) {
      pthread_cond_signal(&block->thread->wake);
    }
  }

  inline void LinkWaitBlock(KernelObject* object, WaitBlock* block, ThreadRecord* thread) noexcept
  {
    block->thread = thread;
    block->previous = nullptr;
    block->next = object->waiters;
    if (object->waiters != nullptr) {
      object->waiters->previous = block;
    }
    object->waiters = block;
  }

  inline void UnlinkWaitBlock(KernelObject* object, WaitBlock* block) noexcept
  {
    if (block->previous != nullptr) {
      block->previous->next = block->next;
    } else {
      object->waiters = block->next;
    }
    if (block->next != nullptr) {
      block->next->previous = block->previous;
    }
  }

  inline bool DeadlinePassed(const timespec& deadline) noexcept
  {
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec > deadline.tv_sec || (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec);
  }

  // WaitForMultipleObjectsEx, and with count 0 the alertable sleep of SleepEx.
  inline DWORD WaitForKernelObjects(
    const DWORD count, const HANDLE* handles, const BOOL waitAll, const DWORD milliseconds, const BOOL alertable
  ) noexcept
  {
    if (count > MAXIMUM_WAIT_OBJECTS || (count != 0 && handles == nullptr)) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return WAIT_FAILED;
    }
    timespec deadline{};
    if (milliseconds != INFINITE && milliseconds != 0) {
      clock_gettime(CLOCK_MONOTONIC, &deadline);
      deadline.tv_sec += static_cast<time_t>(milliseconds / 1000u);
      deadline.tv_nsec += static_cast<long>(milliseconds % 1000u) * 1000000L;
      if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_nsec -= 1000000000L;
        ++deadline.tv_sec;
      }
    }

    KernelObject* objects[MAXIMUM_WAIT_OBJECTS];
    WaitBlock blocks[MAXIMUM_WAIT_OBJECTS];
    KernelObject* released[MAXIMUM_WAIT_OBJECTS];
    size_t releasedCount = 0;
    DWORD result = WAIT_FAILED;
    DWORD error = ERROR_SUCCESS;
    ApcEntry* apcs = nullptr;

    pthread_mutex_lock(KernelLock());
    ThreadRecord* const self = CurrentThreadRecord();
    DWORD referenced = 0;
    if (self == nullptr) {
      error = ERROR_NOT_ENOUGH_MEMORY;
    } else {
      for (; referenced < count; ++referenced) {
        KernelObject* const object = ObjectFromHandle(handles[referenced]);
        if (object == nullptr || !IsSyncKind(object->kind)) {
          error = ERROR_INVALID_HANDLE;
          break;
        }
        ++object->references;
        objects[referenced] = object;
      }
    }

    if (error == ERROR_SUCCESS) {
      bool registered = false;
      for (;;) {
        if (count != 0 && waitAll) {
          bool all = true;
          for (DWORD i = 0; i < count && all; ++i) {
            all = IsSignaledFor(objects[i], self->tid);
          }
          if (all) {
            for (DWORD i = 0; i < count; ++i) {
              AcquireSignaled(objects[i], self->tid);
            }
            result = WAIT_OBJECT_0;
            break;
          }
        } else if (count != 0) {
          DWORD hit = count;
          for (DWORD i = 0; i < count; ++i) {
            if (IsSignaledFor(objects[i], self->tid)) {
              hit = i;
              break;
            }
          }
          if (hit < count) {
            AcquireSignaled(objects[hit], self->tid);
            result = WAIT_OBJECT_0 + hit;
            break;
          }
        }
        if (alertable && self->apcHead != nullptr) {
          result = WAIT_IO_COMPLETION;
          break;
        }
        if (milliseconds == 0 || (milliseconds != INFINITE && DeadlinePassed(deadline))) {
          result = WAIT_TIMEOUT;
          break;
        }
        if (!registered) {
          for (DWORD i = 0; i < count; ++i) {
            LinkWaitBlock(objects[i], &blocks[i], self);
          }
          registered = true;
        }
        self->alertable = alertable != FALSE;
        if (milliseconds == INFINITE) {
          pthread_cond_wait(&self->wake, KernelLock());
        } else {
          pthread_cond_timedwait(&self->wake, KernelLock(), &deadline);
        }
        self->alertable = false;
      }
      if (registered) {
        for (DWORD i = 0; i < count; ++i) {
          UnlinkWaitBlock(objects[i], &blocks[i]);
        }
      }
      if (result == WAIT_IO_COMPLETION) {
        apcs = self->apcHead;
        self->apcHead = nullptr;
        self->apcTail = nullptr;
      }
    }

    for (DWORD i = 0; i < referenced; ++i) {
      if (ReleaseKernelReference(objects[i])) {
        released[releasedCount++] = objects[i];
      }
    }
    pthread_mutex_unlock(KernelLock());

    for (size_t i = 0; i < releasedCount; ++i) {
      DestroyKernelObject(released[i]);
    }
    if (error != ERROR_SUCCESS) {
      SetLastError(error);
      return WAIT_FAILED;
    }
    RunQueuedApcs(apcs);
    return result;
  }

  // The thread id behind a thread handle (GetCurrentThread() or OpenThread);
  // 0 with ERROR_INVALID_HANDLE otherwise.
  inline pid_t TidFromThreadHandle(HANDLE thread) noexcept
  {
    if (reinterpret_cast<intptr_t>(thread) == -2) {
      return CurrentTid();
    }
    KernelLockGuard guard;
    const KernelObject* const object = ObjectFromHandle(thread);
    if (object == nullptr || object->kind != KernelKind::Thread) {
      SetLastError(ERROR_INVALID_HANDLE);
      return 0;
    }
    return static_cast<const ThreadObject*>(object)->tid;
  }

  // Bits 0..63 of a CPU set; DWORD_PTR has no room for more.
  inline DWORD_PTR CpuSetToMask(const cpu_set_t& set) noexcept
  {
    DWORD_PTR mask = 0;
    for (int cpu = 0; cpu < static_cast<int>(sizeof(DWORD_PTR) * 8) && cpu < CPU_SETSIZE; ++cpu) {
      if (CPU_ISSET(cpu, &set)) {
        mask |= static_cast<DWORD_PTR>(1) << cpu;
      }
    }
    return mask;
  }

  inline cpu_set_t MaskToCpuSet(const DWORD_PTR mask) noexcept
  {
    cpu_set_t set;
    CPU_ZERO(&set);
    for (int cpu = 0; cpu < static_cast<int>(sizeof(DWORD_PTR) * 8); ++cpu) {
      if ((mask & (static_cast<DWORD_PTR>(1) << cpu)) != 0) {
        CPU_SET(cpu, &set);
      }
    }
    return set;
  }

  // Every configured CPU, as GetProcessAffinityMask's system mask.
  inline DWORD_PTR SystemCpuMask() noexcept
  {
    long cpus = sysconf(_SC_NPROCESSORS_CONF);
    if (cpus < 1) {
      cpus = 1;
    }
    if (cpus >= static_cast<long>(sizeof(DWORD_PTR) * 8)) {
      return ~static_cast<DWORD_PTR>(0);
    }
    return (static_cast<DWORD_PTR>(1) << cpus) - 1u;
  }
} // namespace faf_compat

extern "C" {

// ---------------------------------------------------------------------------
// Handles, events, mutexes, semaphores
// ---------------------------------------------------------------------------
inline BOOL CloseHandle(HANDLE handle) noexcept
{
  const intptr_t value = reinterpret_cast<intptr_t>(handle);
  if (value == -1 || value == -2) {
    return TRUE;  // pseudo-handles: closing them has no effect
  }
  faf_compat::KernelObject* object = nullptr;
  {
    faf_compat::KernelLockGuard guard;
    object = faf_compat::ObjectFromHandle(handle);
    if (object == nullptr || object->kind == faf_compat::KernelKind::FindFile) {
      SetLastError(ERROR_INVALID_HANDLE);
      return FALSE;
    }
    if (!faf_compat::ReleaseKernelReference(object)) {
      return TRUE;
    }
  }
  faf_compat::DestroyKernelObject(object);
  return TRUE;
}

inline HANDLE CreateEventA(LPSECURITY_ATTRIBUTES, BOOL manualReset, BOOL initialState, LPCSTR name) noexcept
{
  faf_compat::EventObject* const event =
    faf_compat::AllocateKernelObject<faf_compat::EventObject>(faf_compat::KernelKind::Event);
  if (event != nullptr) {
    event->manualReset = manualReset != FALSE;
    event->signaled = initialState != FALSE;
  }
  return faf_compat::PublishKernelObject(event, name);
}

inline HANDLE CreateEventW(LPSECURITY_ATTRIBUTES attributes, BOOL manualReset, BOOL initialState, LPCWSTR name) noexcept
{
  char narrow[1024];
  if (!faf_compat::NarrowObjectName(name, narrow, sizeof(narrow))) {
    SetLastError(ERROR_INVALID_NAME);
    return nullptr;
  }
  return CreateEventA(attributes, manualReset, initialState, name != nullptr ? narrow : nullptr);
}

inline BOOL SetEvent(HANDLE handle) noexcept
{
  faf_compat::KernelLockGuard guard;
  faf_compat::KernelObject* const object = faf_compat::ObjectFromHandle(handle);
  if (object == nullptr || object->kind != faf_compat::KernelKind::Event) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  static_cast<faf_compat::EventObject*>(object)->signaled = true;
  faf_compat::WakeWaiters(object);
  return TRUE;
}

inline BOOL ResetEvent(HANDLE handle) noexcept
{
  faf_compat::KernelLockGuard guard;
  faf_compat::KernelObject* const object = faf_compat::ObjectFromHandle(handle);
  if (object == nullptr || object->kind != faf_compat::KernelKind::Event) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  static_cast<faf_compat::EventObject*>(object)->signaled = false;
  return TRUE;
}

inline HANDLE CreateMutexA(LPSECURITY_ATTRIBUTES, BOOL initialOwner, LPCSTR name) noexcept
{
  faf_compat::MutexObject* const mutex =
    faf_compat::AllocateKernelObject<faf_compat::MutexObject>(faf_compat::KernelKind::Mutex);
  if (mutex != nullptr && initialOwner != FALSE) {
    mutex->owner = faf_compat::CurrentTid();
    mutex->recursion = 1;
  }
  return faf_compat::PublishKernelObject(mutex, name);
}

inline HANDLE CreateMutexW(LPSECURITY_ATTRIBUTES attributes, BOOL initialOwner, LPCWSTR name) noexcept
{
  char narrow[1024];
  if (!faf_compat::NarrowObjectName(name, narrow, sizeof(narrow))) {
    SetLastError(ERROR_INVALID_NAME);
    return nullptr;
  }
  return CreateMutexA(attributes, initialOwner, name != nullptr ? narrow : nullptr);
}

inline BOOL ReleaseMutex(HANDLE handle) noexcept
{
  faf_compat::KernelLockGuard guard;
  faf_compat::KernelObject* const object = faf_compat::ObjectFromHandle(handle);
  if (object == nullptr || object->kind != faf_compat::KernelKind::Mutex) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  faf_compat::MutexObject* const mutex = static_cast<faf_compat::MutexObject*>(object);
  if (mutex->owner != faf_compat::CurrentTid() || mutex->recursion == 0) {
    SetLastError(ERROR_NOT_OWNER);
    return FALSE;
  }
  if (--mutex->recursion == 0) {
    mutex->owner = 0;
    faf_compat::WakeWaiters(object);
  }
  return TRUE;
}

inline HANDLE CreateSemaphoreA(LPSECURITY_ATTRIBUTES, LONG initialCount, LONG maximumCount, LPCSTR name) noexcept
{
  if (maximumCount <= 0 || initialCount < 0 || initialCount > maximumCount) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
  }
  faf_compat::SemaphoreObject* const semaphore =
    faf_compat::AllocateKernelObject<faf_compat::SemaphoreObject>(faf_compat::KernelKind::Semaphore);
  if (semaphore != nullptr) {
    semaphore->count = initialCount;
    semaphore->maximum = maximumCount;
  }
  return faf_compat::PublishKernelObject(semaphore, name);
}

inline HANDLE CreateSemaphoreW(LPSECURITY_ATTRIBUTES attributes, LONG initialCount, LONG maximumCount, LPCWSTR name) noexcept
{
  char narrow[1024];
  if (!faf_compat::NarrowObjectName(name, narrow, sizeof(narrow))) {
    SetLastError(ERROR_INVALID_NAME);
    return nullptr;
  }
  return CreateSemaphoreA(attributes, initialCount, maximumCount, name != nullptr ? narrow : nullptr);
}

inline BOOL ReleaseSemaphore(HANDLE handle, LONG releaseCount, LPLONG previousCount) noexcept
{
  faf_compat::KernelLockGuard guard;
  faf_compat::KernelObject* const object = faf_compat::ObjectFromHandle(handle);
  if (object == nullptr || object->kind != faf_compat::KernelKind::Semaphore) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  faf_compat::SemaphoreObject* const semaphore = static_cast<faf_compat::SemaphoreObject*>(object);
  if (releaseCount <= 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (releaseCount > semaphore->maximum - semaphore->count) {
    SetLastError(ERROR_TOO_MANY_POSTS);
    return FALSE;
  }
  if (previousCount != nullptr) {
    *previousCount = semaphore->count;
  }
  semaphore->count += releaseCount;
  faf_compat::WakeWaiters(object);
  return TRUE;
}

// ---------------------------------------------------------------------------
// Waits and alertable sleeps
// ---------------------------------------------------------------------------
inline DWORD WaitForMultipleObjectsEx(
  DWORD count, const HANDLE* handles, BOOL waitAll, DWORD milliseconds, BOOL alertable
) noexcept
{
  if (count == 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return WAIT_FAILED;
  }
  return faf_compat::WaitForKernelObjects(count, handles, waitAll, milliseconds, alertable);
}

inline DWORD WaitForMultipleObjects(DWORD count, const HANDLE* handles, BOOL waitAll, DWORD milliseconds) noexcept
{
  return WaitForMultipleObjectsEx(count, handles, waitAll, milliseconds, FALSE);
}

inline DWORD WaitForSingleObjectEx(HANDLE handle, DWORD milliseconds, BOOL alertable) noexcept
{
  return faf_compat::WaitForKernelObjects(1, &handle, FALSE, milliseconds, alertable);
}

inline DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds) noexcept
{
  return faf_compat::WaitForKernelObjects(1, &handle, FALSE, milliseconds, FALSE);
}

// 0 when the time ran out, WAIT_IO_COMPLETION when APCs ran (alertable only).
inline DWORD SleepEx(DWORD milliseconds, BOOL alertable) noexcept
{
  if (alertable == FALSE) {
    Sleep(milliseconds);
    return 0;
  }
  if (faf_compat::WaitForKernelObjects(0, nullptr, FALSE, milliseconds, TRUE) == WAIT_IO_COMPLETION) {
    return WAIT_IO_COMPLETION;
  }
  if (milliseconds == 0) {
    sched_yield();
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Thread handles and APCs
// ---------------------------------------------------------------------------
// A handle to a live thread of this process, by the id GetCurrentThreadId
// returns (the kernel tid). It serves QueueUserAPC, SetThreadPriority and
// SetThreadAffinityMask; it cannot be waited on.
inline HANDLE OpenThread(DWORD, BOOL, DWORD threadId) noexcept
{
  if (threadId == 0 || syscall(SYS_tgkill, getpid(), static_cast<pid_t>(threadId), 0) != 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
  }
  faf_compat::ThreadObject* const thread =
    faf_compat::AllocateKernelObject<faf_compat::ThreadObject>(faf_compat::KernelKind::Thread);
  if (thread != nullptr) {
    thread->tid = static_cast<pid_t>(threadId);
  }
  return thread;
}

inline DWORD QueueUserAPC(PAPCFUNC routine, HANDLE thread, ULONG_PTR parameter) noexcept
{
  if (routine == nullptr) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  const pid_t tid = faf_compat::TidFromThreadHandle(thread);
  if (tid == 0) {
    return 0;
  }
  faf_compat::ApcEntry* const entry = static_cast<faf_compat::ApcEntry*>(malloc(sizeof(faf_compat::ApcEntry)));
  if (entry == nullptr) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
  entry->routine = routine;
  entry->parameter = parameter;
  entry->next = nullptr;

  faf_compat::KernelLockGuard guard;
  faf_compat::ThreadRecord* record = tid == faf_compat::CurrentTid() ? faf_compat::CurrentThreadRecord()
                                                                     : faf_compat::FindThreadRecord(tid);
  if (record == nullptr) {
    record = faf_compat::NewThreadRecord(tid);
  }
  if (record == nullptr) {
    free(entry);
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
  }
  if (record->apcTail != nullptr) {
    record->apcTail->next = entry;
  } else {
    record->apcHead = entry;
  }
  record->apcTail = entry;
  if (record->alertable) {
    pthread_cond_signal(&record->wake);
  }
  return 1;
}

// Linux keeps a nice value per thread. THREAD_PRIORITY_* maps onto it
// (HIGHEST -10 ... IDLE 19). Raising priority needs a privilege an app or an
// adb shell does not have; that is ignored, as it only changes scheduling.
inline BOOL SetThreadPriority(HANDLE thread, int priority) noexcept
{
  const pid_t tid = faf_compat::TidFromThreadHandle(thread);
  if (tid == 0) {
    return FALSE;
  }
  int nice = 0;
  if (priority <= THREAD_PRIORITY_IDLE) {
    nice = 19;
  } else if (priority >= THREAD_PRIORITY_TIME_CRITICAL) {
    nice = -20;
  } else {
    nice = -5 * priority;  // LOWEST 10, BELOW_NORMAL 5, ABOVE_NORMAL -5, HIGHEST -10
  }
  (void)setpriority(PRIO_PROCESS, static_cast<id_t>(tid), nice);
  return TRUE;
}

// Returns the previous mask, or 0 on failure. Pins for real (sched_setaffinity).
inline DWORD_PTR SetThreadAffinityMask(HANDLE thread, DWORD_PTR mask) noexcept
{
  const pid_t tid = faf_compat::TidFromThreadHandle(thread);
  if (tid == 0) {
    return 0;
  }
  cpu_set_t previous;
  CPU_ZERO(&previous);
  if (sched_getaffinity(tid, sizeof(previous), &previous) != 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  const cpu_set_t wanted = faf_compat::MaskToCpuSet(mask);
  if (mask == 0 || sched_setaffinity(tid, sizeof(wanted), &wanted) != 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  return faf_compat::CpuSetToMask(previous);
}

// The process mask is the calling thread's (Linux keeps affinity per thread;
// threads inherit it from their creator); the system mask is every configured
// CPU.
inline BOOL GetProcessAffinityMask(HANDLE process, PDWORD_PTR processMask, PDWORD_PTR systemMask) noexcept
{
  if (reinterpret_cast<intptr_t>(process) != -1 || processMask == nullptr || systemMask == nullptr) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  cpu_set_t set;
  CPU_ZERO(&set);
  if (sched_getaffinity(0, sizeof(set), &set) != 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  *processMask = faf_compat::CpuSetToMask(set);
  *systemMask = faf_compat::SystemCpuMask();
  return TRUE;
}

// Applies the mask to every thread of the process (/proc/self/task).
inline BOOL SetProcessAffinityMask(HANDLE process, DWORD_PTR mask) noexcept
{
  if (reinterpret_cast<intptr_t>(process) != -1 || mask == 0 || (mask & ~faf_compat::SystemCpuMask()) != 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  const cpu_set_t wanted = faf_compat::MaskToCpuSet(mask);
  if (sched_setaffinity(0, sizeof(wanted), &wanted) != 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (DIR* const tasks = opendir("/proc/self/task")) {
    while (const dirent* const entry = readdir(tasks)) {
      const long tid = strtol(entry->d_name, nullptr, 10);
      if (tid > 0) {
        (void)sched_setaffinity(static_cast<pid_t>(tid), sizeof(wanted), &wanted);
      }
    }
    closedir(tasks);
  }
  return TRUE;
}

// ---------------------------------------------------------------------------
// System and version queries
// ---------------------------------------------------------------------------
inline void GetSystemInfo(LPSYSTEM_INFO info) noexcept
{
  memset(info, 0, sizeof(*info));
#if defined(__aarch64__)
  info->wProcessorArchitecture = PROCESSOR_ARCHITECTURE_ARM64;
#elif defined(__x86_64__)
  info->wProcessorArchitecture = PROCESSOR_ARCHITECTURE_AMD64;
#elif defined(__i386__)
  info->wProcessorArchitecture = PROCESSOR_ARCHITECTURE_INTEL;
#elif defined(__arm__)
  info->wProcessorArchitecture = PROCESSOR_ARCHITECTURE_ARM;
#else
  info->wProcessorArchitecture = PROCESSOR_ARCHITECTURE_UNKNOWN;
#endif
  const long pageSize = sysconf(_SC_PAGESIZE);
  info->dwPageSize = static_cast<DWORD>(pageSize > 0 ? pageSize : 4096);
  info->lpMinimumApplicationAddress = reinterpret_cast<LPVOID>(static_cast<uintptr_t>(0x10000u));
  info->lpMaximumApplicationAddress = reinterpret_cast<LPVOID>(~static_cast<uintptr_t>(0) >> 17);
  info->dwActiveProcessorMask = faf_compat::SystemCpuMask();
  const long cpus = sysconf(_SC_NPROCESSORS_CONF);
  info->dwNumberOfProcessors = static_cast<DWORD>(cpus > 0 ? cpus : 1);
  // VirtualAlloc reservations are aligned to this, as on Windows (faf_win_memory.h).
  info->dwAllocationGranularity = info->dwPageSize > 65536u ? info->dwPageSize : 65536u;
}

inline void GetNativeSystemInfo(LPSYSTEM_INFO info) noexcept
{
  GetSystemInfo(info);
}

// One NUMA node (0) holding every processor.
inline BOOL GetNumaHighestNodeNumber(PULONG highestNodeNumber) noexcept
{
  *highestNodeNumber = 0;
  return TRUE;
}

inline BOOL GetNumaProcessorNode(UCHAR processor, PUCHAR nodeNumber) noexcept
{
  const long cpus = sysconf(_SC_NPROCESSORS_CONF);
  if (processor >= (cpus > 0 ? cpus : 1)) {
    *nodeNumber = 0xFF;
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  *nodeNumber = 0;
  return TRUE;
}

inline BOOL GetNumaNodeProcessorMask(UCHAR node, PULONGLONG processorMask) noexcept
{
  if (node != 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  *processorMask = static_cast<ULONGLONG>(faf_compat::SystemCpuMask());
  return TRUE;
}

// What GetVersionEx reports on Windows 8 and later to an executable without a
// compatibility manifest: NT 6.2, build 9200, no service pack.
inline BOOL GetVersionExW(LPOSVERSIONINFOW info) noexcept
{
  if (info == nullptr ||
      (info->dwOSVersionInfoSize != sizeof(OSVERSIONINFOW) && info->dwOSVersionInfoSize != sizeof(OSVERSIONINFOEXW))) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  const DWORD size = info->dwOSVersionInfoSize;
  memset(info, 0, size);
  info->dwOSVersionInfoSize = size;
  info->dwMajorVersion = 6;
  info->dwMinorVersion = 2;
  info->dwBuildNumber = 9200;
  info->dwPlatformId = VER_PLATFORM_WIN32_NT;
  if (size == sizeof(OSVERSIONINFOEXW)) {
    reinterpret_cast<LPOSVERSIONINFOEXW>(info)->wProductType = 1;  // VER_NT_WORKSTATION
  }
  return TRUE;
}

inline BOOL GetVersionExA(LPOSVERSIONINFOA info) noexcept
{
  if (info == nullptr || info->dwOSVersionInfoSize != sizeof(OSVERSIONINFOA)) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  memset(info, 0, sizeof(*info));
  info->dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
  info->dwMajorVersion = 6;
  info->dwMinorVersion = 2;
  info->dwBuildNumber = 9200;
  info->dwPlatformId = VER_PLATFORM_WIN32_NT;
  return TRUE;
}

// ---------------------------------------------------------------------------
// Modules (dlopen). There are no DLLs here: LoadLibrary of "x.dll" fails with
// ERROR_MOD_NOT_FOUND, and GetModuleHandle(NULL) is the main program.
// ---------------------------------------------------------------------------
inline HMODULE LoadLibraryA(LPCSTR fileName) noexcept
{
  void* const module = fileName != nullptr ? dlopen(fileName, RTLD_NOW) : nullptr;
  if (module == nullptr) {
    SetLastError(ERROR_MOD_NOT_FOUND);
  }
  return reinterpret_cast<HMODULE>(module);
}

inline HMODULE LoadLibraryW(LPCWSTR fileName) noexcept
{
  char narrow[faf_compat::kMaxNativePath];
  if (fileName == nullptr || !faf_compat::WideToUtf8(fileName, narrow, sizeof(narrow))) {
    SetLastError(ERROR_MOD_NOT_FOUND);
    return nullptr;
  }
  return LoadLibraryA(narrow);
}

// No reference is added, as on Windows.
inline HMODULE GetModuleHandleA(LPCSTR moduleName) noexcept
{
  void* const module = dlopen(moduleName, RTLD_NOW | RTLD_NOLOAD);
  if (module == nullptr) {
    SetLastError(ERROR_MOD_NOT_FOUND);
    return nullptr;
  }
  (void)dlclose(module);
  return reinterpret_cast<HMODULE>(module);
}

inline HMODULE GetModuleHandleW(LPCWSTR moduleName) noexcept
{
  if (moduleName == nullptr) {
    return GetModuleHandleA(nullptr);
  }
  char narrow[faf_compat::kMaxNativePath];
  if (!faf_compat::WideToUtf8(moduleName, narrow, sizeof(narrow))) {
    SetLastError(ERROR_MOD_NOT_FOUND);
    return nullptr;
  }
  return GetModuleHandleA(narrow);
}

inline FARPROC GetProcAddress(HMODULE module, LPCSTR procName) noexcept
{
  void* const symbol = (module != nullptr && procName != nullptr) ? dlsym(reinterpret_cast<void*>(module), procName)
                                                                  : nullptr;
  if (symbol == nullptr) {
    SetLastError(ERROR_PROC_NOT_FOUND);
  }
  return reinterpret_cast<FARPROC>(symbol);
}

inline BOOL FreeLibrary(HMODULE module) noexcept
{
  if (module == nullptr || dlclose(reinterpret_cast<void*>(module)) != 0) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  return TRUE;
}

// The executable's path for a null module (/proc/self/exe); other modules are
// not supported. Returns the length copied without the terminator; when the
// buffer is too small, `size` with the text truncated and
// ERROR_INSUFFICIENT_BUFFER, as on Windows.
inline DWORD GetModuleFileNameA(HMODULE module, LPSTR fileName, DWORD size) noexcept
{
  if (module != nullptr || fileName == nullptr || size == 0) {
    SetLastError(ERROR_INVALID_HANDLE);
    return 0;
  }
  char path[faf_compat::kMaxNativePath];
  const ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1u);
  if (length <= 0) {
    SetLastError(ERROR_GEN_FAILURE);
    return 0;
  }
  path[length] = '\0';
  if (static_cast<DWORD>(length) >= size) {
    memcpy(fileName, path, size - 1u);
    fileName[size - 1u] = '\0';
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return size;
  }
  memcpy(fileName, path, static_cast<size_t>(length) + 1u);
  SetLastError(ERROR_SUCCESS);
  return static_cast<DWORD>(length);
}

inline DWORD GetModuleFileNameW(HMODULE module, LPWSTR fileName, DWORD size) noexcept
{
  if (fileName == nullptr || size == 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  char narrow[faf_compat::kMaxNativePath];
  if (GetModuleFileNameA(module, narrow, sizeof(narrow)) == 0) {
    return 0;
  }
  wchar_t wide[faf_compat::kMaxNativePath];
  const size_t length = faf_compat::Utf8ToWide(narrow, wide, faf_compat::kMaxNativePath);
  if (length >= size) {
    memcpy(fileName, wide, (size - 1u) * sizeof(wchar_t));
    fileName[size - 1u] = L'\0';
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return size;
  }
  memcpy(fileName, wide, (length + 1u) * sizeof(wchar_t));
  return static_cast<DWORD>(length);
}

} // extern "C"

} // extern "C++"
