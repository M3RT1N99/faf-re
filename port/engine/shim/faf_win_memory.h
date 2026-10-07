#pragma once

// Win32 memory services for the non-Windows targets of the port: virtual
// memory (VirtualAlloc/VirtualFree/VirtualQuery/VirtualProtect), the process
// heap (HeapAlloc family), LocalAlloc/LocalFree, memory status and
// RtlCaptureStackBackTrace. Included by faf_win_compat.h (<windows.h>).
//
// - VirtualAlloc reserves with an anonymous PROT_NONE mmap, aligned to the
//   64 KB allocation granularity as on Windows, and commits with mprotect;
//   committed pages read as zero until written. MEM_DECOMMIT drops the pages
//   (madvise MADV_DONTNEED) and makes them inaccessible again; MEM_RELEASE
//   unmaps the whole reservation, which must be named by its base. The shim
//   keeps a registry of its reservations (base, size, protection), so
//   VirtualFree and VirtualQuery know them. Users: the engine allocator in
//   gpg/core/utils/Global.cpp (x86 only at run time; it compiles everywhere)
//   and the Sofdec movie code.
// - VirtualQuery answers for any address, as on Windows, from
//   /proc/self/maps: a mapping with no access is MEM_RESERVE, any other is
//   MEM_COMMIT; anonymous memory (the C heap, thread stacks, the shim's
//   reservations) is MEM_PRIVATE, a file mapped from a shared library
//   MEM_IMAGE, any other file MEM_MAPPED; unmapped space is MEM_FREE up to the
//   next mapping. BaseAddress is the page of the queried address and
//   RegionSize runs to the end of its mapping, so BaseAddress + RegionSize is
//   where the mapping ends. moho/path/PathTables.cpp:1680
//   (ClusterMapPointerLooksLive) decides from this whether to dirty a path
//   cluster, so a heap pointer must come back MEM_COMMIT + MEM_PRIVATE +
//   PAGE_READWRITE here exactly as it does on Windows. On AArch64 the C heap
//   hands out tagged pointers (top-byte-ignore; Scudo's 0xb4 tag): the tag is
//   ignored for the lookup and kept on BaseAddress and AllocationBase, so the
//   caller's pointer arithmetic against them stays consistent.
// - The process heap is the C library's allocator: HeapAlloc is malloc (calloc
//   for HEAP_ZERO_MEMORY), HeapFree free. HeapSize is malloc_usable_size,
//   which can be larger than the size that was requested, where Windows
//   returns the requested size; code that sizes copies by it
//   (gpg/core/utils/Global.cpp realloc_0) copies a few slack bytes more and may
//   keep a block in place where Windows moves it. Nothing but addresses can
//   differ.
// - M3c's low arena (docs/port/android-roadmap.md W1.3) replaces
//   MapAnonymous() and MapFileView() below to keep reservations and file views
//   under 2 GB.

#if defined(_WIN32) || defined(_MSC_VER)
#error "port/engine/shim is for non-Windows targets; it must not be on a Windows include path"
#endif

#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <unwind.h>

extern "C++" {

// ---------------------------------------------------------------------------
// Constants (winnt.h, winbase.h)
// ---------------------------------------------------------------------------
#define PAGE_NOACCESS 0x01
#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define PAGE_WRITECOPY 0x08
#define PAGE_EXECUTE 0x10
#define PAGE_EXECUTE_READ 0x20
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_EXECUTE_WRITECOPY 0x80
#define PAGE_GUARD 0x100
#define PAGE_NOCACHE 0x200
#define PAGE_WRITECOMBINE 0x400

#define MEM_COMMIT 0x00001000
#define MEM_RESERVE 0x00002000
#define MEM_DECOMMIT 0x00004000
#define MEM_RELEASE 0x00008000
#define MEM_FREE 0x00010000
#define MEM_PRIVATE 0x00020000
#define MEM_MAPPED 0x00040000
#define MEM_RESET 0x00080000
#define MEM_TOP_DOWN 0x00100000
#define MEM_IMAGE 0x01000000

#define HEAP_NO_SERIALIZE 0x00000001
#define HEAP_GENERATE_EXCEPTIONS 0x00000004
#define HEAP_ZERO_MEMORY 0x00000008
#define HEAP_REALLOC_IN_PLACE_ONLY 0x00000010

#define LMEM_FIXED 0x0000
#define LMEM_MOVEABLE 0x0002
#define LMEM_ZEROINIT 0x0040
#define LPTR (LMEM_FIXED | LMEM_ZEROINIT)

typedef struct _MEMORY_BASIC_INFORMATION {
  PVOID BaseAddress;
  PVOID AllocationBase;
  DWORD AllocationProtect;
  WORD PartitionId;
  SIZE_T RegionSize;
  DWORD State;
  DWORD Protect;
  DWORD Type;
} MEMORY_BASIC_INFORMATION, *PMEMORY_BASIC_INFORMATION;

typedef struct _MEMORYSTATUS {
  DWORD dwLength;
  DWORD dwMemoryLoad;
  SIZE_T dwTotalPhys;
  SIZE_T dwAvailPhys;
  SIZE_T dwTotalPageFile;
  SIZE_T dwAvailPageFile;
  SIZE_T dwTotalVirtual;
  SIZE_T dwAvailVirtual;
} MEMORYSTATUS, *LPMEMORYSTATUS;

typedef struct _MEMORYSTATUSEX {
  DWORD dwLength;
  DWORD dwMemoryLoad;
  DWORDLONG ullTotalPhys;
  DWORDLONG ullAvailPhys;
  DWORDLONG ullTotalPageFile;
  DWORDLONG ullAvailPageFile;
  DWORDLONG ullTotalVirtual;
  DWORDLONG ullAvailVirtual;
  DWORDLONG ullAvailExtendedVirtual;
} MEMORYSTATUSEX, *LPMEMORYSTATUSEX;

namespace faf_compat
{
  constexpr uintptr_t kAllocationGranularity = 65536u;

  inline uintptr_t PageSize() noexcept
  {
    static const long size = sysconf(_SC_PAGESIZE);
    return static_cast<uintptr_t>(size > 0 ? size : 4096);
  }

  // The tag bits AArch64 ignores in a data address (top-byte-ignore).
  inline uintptr_t AddressTagMask() noexcept
  {
#if defined(__aarch64__)
    return static_cast<uintptr_t>(0xFF) << 56;
#else
    return 0;
#endif
  }

  inline int ProtectToPosix(const DWORD protect, bool* valid) noexcept
  {
    *valid = true;
    switch (protect & 0xFFu) {
      case PAGE_NOACCESS:
        return PROT_NONE;
      case PAGE_READONLY:
        return PROT_READ;
      case PAGE_READWRITE:
      case PAGE_WRITECOPY:
        return PROT_READ | PROT_WRITE;
      case PAGE_EXECUTE:
        return PROT_EXEC;
      case PAGE_EXECUTE_READ:
        return PROT_READ | PROT_EXEC;
      case PAGE_EXECUTE_READWRITE:
      case PAGE_EXECUTE_WRITECOPY:
        return PROT_READ | PROT_WRITE | PROT_EXEC;
      default:
        *valid = false;
        return PROT_NONE;
    }
  }

  // Anonymous private memory; the one place the shim takes address space for
  // VirtualAlloc. `hint` is honoured or the call fails.
  inline void* MapAnonymous(void* hint, const size_t length, const int protection) noexcept
  {
    void* const mapped =
      mmap(hint, length, protection, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (mapped == MAP_FAILED) {
      return nullptr;
    }
    if (hint != nullptr && mapped != hint) {
      munmap(mapped, length);
      return nullptr;
    }
    return mapped;
  }

  // A view of a file; the one place the shim takes address space for
  // MapViewOfFile (faf_win_file.h).
  inline void* MapFileView(const size_t length, const int protection, const int flags, const int fd, const off_t offset) noexcept
  {
    void* const mapped = mmap(nullptr, length, protection, flags, fd, offset);
    return mapped != MAP_FAILED ? mapped : nullptr;
  }

  // A VirtualAlloc reservation or a MapViewOfFile view.
  enum class RegionKind : uint32_t {
    Reservation,
    FileView,
  };

  struct MemoryRegion {
    uintptr_t base;
    size_t length;
    DWORD protect;
    RegionKind kind;
    MemoryRegion* next;
  };

  inline pthread_mutex_t* RegionLock() noexcept
  {
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    return &lock;
  }

  inline MemoryRegion*& Regions() noexcept
  {
    static MemoryRegion* head = nullptr;
    return head;
  }

  inline bool AddRegion(const uintptr_t base, const size_t length, const DWORD protect, const RegionKind kind) noexcept
  {
    MemoryRegion* const region = static_cast<MemoryRegion*>(malloc(sizeof(MemoryRegion)));
    if (region == nullptr) {
      return false;
    }
    region->base = base;
    region->length = length;
    region->protect = protect;
    region->kind = kind;
    pthread_mutex_lock(RegionLock());
    region->next = Regions();
    Regions() = region;
    pthread_mutex_unlock(RegionLock());
    return true;
  }

  // The region containing `address` (copied out), or false.
  inline bool FindRegion(const uintptr_t address, MemoryRegion* out) noexcept
  {
    bool found = false;
    pthread_mutex_lock(RegionLock());
    for (const MemoryRegion* region = Regions(); region != nullptr; region = region->next) {
      if (address >= region->base && address - region->base < region->length) {
        *out = *region;
        found = true;
        break;
      }
    }
    pthread_mutex_unlock(RegionLock());
    return found;
  }

  // Unregisters the region of `kind` that starts at `base`; false if none.
  inline bool RemoveRegion(const uintptr_t base, const RegionKind kind, MemoryRegion* out) noexcept
  {
    MemoryRegion* removed = nullptr;
    pthread_mutex_lock(RegionLock());
    for (MemoryRegion** link = &Regions(); *link != nullptr; link = &(*link)->next) {
      if ((*link)->base == base && (*link)->kind == kind) {
        removed = *link;
        *link = removed->next;
        break;
      }
    }
    pthread_mutex_unlock(RegionLock());
    if (removed == nullptr) {
      return false;
    }
    *out = *removed;
    free(removed);
    return true;
  }

  // --- /proc/self/maps -------------------------------------------------------

  struct MapsEntry {
    uintptr_t start;
    uintptr_t end;
    char permissions[5];  // "rwxp"
    bool fileBacked;
    bool sharedLibrary;
  };

  inline const char* ParseHex(const char* text, uintptr_t* value) noexcept
  {
    uintptr_t result = 0;
    for (;; ++text) {
      const char c = *text;
      if (c >= '0' && c <= '9') {
        result = (result << 4) | static_cast<uintptr_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        result = (result << 4) | static_cast<uintptr_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        result = (result << 4) | static_cast<uintptr_t>(c - 'A' + 10);
      } else {
        break;
      }
    }
    *value = result;
    return text;
  }

  // "start-end perms offset dev inode [path]"
  inline bool ParseMapsLine(const char* line, MapsEntry* entry) noexcept
  {
    const char* p = ParseHex(line, &entry->start);
    if (*p != '-') {
      return false;
    }
    p = ParseHex(p + 1, &entry->end);
    if (*p != ' ' || p[1] == '\0' || p[2] == '\0' || p[3] == '\0' || p[4] == '\0') {
      return false;
    }
    memcpy(entry->permissions, p + 1, 4);
    entry->permissions[4] = '\0';
    p += 5;
    for (int field = 0; field < 3; ++field) {  // offset, dev, inode
      while (*p == ' ') {
        ++p;
      }
      const char* const fieldStart = p;
      while (*p != ' ' && *p != '\0') {
        ++p;
      }
      if (field == 2) {
        entry->fileBacked = !(p - fieldStart == 1 && *fieldStart == '0');
      }
    }
    while (*p == ' ') {
      ++p;
    }
    entry->sharedLibrary = entry->fileBacked && strstr(p, ".so") != nullptr;
    return true;
  }

  // The mapping that contains `address`, or, when none does, the start of the
  // next mapping above it in *nextStart (0 when there is none).
  inline bool FindMapping(const uintptr_t address, MapsEntry* entry, uintptr_t* nextStart, bool* readable) noexcept
  {
    *nextStart = 0;
    *readable = false;
    const int fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
      return false;
    }
    *readable = true;
    char buffer[4096];
    char line[512];
    size_t lineLength = 0;
    bool found = false;
    bool done = false;
    while (!done) {
      const ssize_t got = read(fd, buffer, sizeof(buffer));
      if (got < 0 && errno == EINTR) {
        continue;
      }
      if (got <= 0) {
        break;
      }
      for (ssize_t i = 0; i < got && !done; ++i) {
        if (buffer[i] != '\n') {
          if (lineLength + 1 < sizeof(line)) {
            line[lineLength++] = buffer[i];
          }
          continue;
        }
        line[lineLength] = '\0';
        lineLength = 0;
        MapsEntry candidate{};
        if (!ParseMapsLine(line, &candidate)) {
          continue;
        }
        if (address >= candidate.start && address < candidate.end) {
          *entry = candidate;
          found = true;
          done = true;
        } else if (candidate.start > address) {
          *nextStart = candidate.start;
          done = true;
        }
      }
    }
    close(fd);
    return found;
  }

  inline DWORD PermissionsToProtect(const MapsEntry& entry) noexcept
  {
    const bool read = entry.permissions[0] == 'r';
    const bool write = entry.permissions[1] == 'w';
    const bool execute = entry.permissions[2] == 'x';
    const bool copyOnWrite = write && entry.permissions[3] == 'p' && entry.fileBacked;
    if (execute) {
      if (write) {
        return copyOnWrite ? PAGE_EXECUTE_WRITECOPY : PAGE_EXECUTE_READWRITE;
      }
      return read ? PAGE_EXECUTE_READ : PAGE_EXECUTE;
    }
    if (write) {
      return copyOnWrite ? PAGE_WRITECOPY : PAGE_READWRITE;
    }
    return read ? PAGE_READONLY : PAGE_NOACCESS;
  }

  // --- RtlCaptureStackBackTrace ---------------------------------------------

  struct BacktraceState {
    PVOID* frames;
    ULONG skip;
    ULONG capacity;
    ULONG count;
  };

  inline _Unwind_Reason_Code BacktraceStep(_Unwind_Context* context, void* argument) noexcept
  {
    BacktraceState* const state = static_cast<BacktraceState*>(argument);
    const uintptr_t pc = _Unwind_GetIP(context);
    if (pc == 0) {
      return _URC_END_OF_STACK;
    }
    if (state->skip != 0) {
      --state->skip;
      return _URC_NO_REASON;
    }
    if (state->count >= state->capacity) {
      return _URC_END_OF_STACK;
    }
    state->frames[state->count++] = reinterpret_cast<PVOID>(pc);
    return _URC_NO_REASON;
  }
} // namespace faf_compat

extern "C" {

// ---------------------------------------------------------------------------
// Virtual memory
// ---------------------------------------------------------------------------
inline LPVOID VirtualAlloc(LPVOID address, SIZE_T size, DWORD allocationType, DWORD protect) noexcept
{
  bool validProtect = false;
  const int protection = faf_compat::ProtectToPosix(protect, &validProtect);
  if (size == 0 || !validProtect || (allocationType & (MEM_COMMIT | MEM_RESERVE | MEM_RESET)) == 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return nullptr;
  }
  if ((allocationType & MEM_RESET) != 0) {
    return address;  // "contents no longer needed": a hint, kept as is
  }
  const uintptr_t page = faf_compat::PageSize();
  const uintptr_t requested = reinterpret_cast<uintptr_t>(address);

  if (address == nullptr || (allocationType & MEM_RESERVE) != 0) {
    const int initial = (allocationType & MEM_COMMIT) != 0 ? protection : PROT_NONE;
    const uintptr_t base = requested & ~(faf_compat::kAllocationGranularity - 1u);
    const size_t length = ((requested - base) + size + page - 1u) & ~(page - 1u);
    void* mapped = nullptr;
    if (address != nullptr) {
      mapped = faf_compat::MapAnonymous(reinterpret_cast<void*>(base), length, initial);
    } else {
      // Over-reserve, then trim to the 64 KB granularity Windows guarantees.
      const size_t padded = length + faf_compat::kAllocationGranularity;
      void* const raw = faf_compat::MapAnonymous(nullptr, padded, initial);
      if (raw != nullptr) {
        const uintptr_t rawBase = reinterpret_cast<uintptr_t>(raw);
        const uintptr_t aligned =
          (rawBase + faf_compat::kAllocationGranularity - 1u) & ~(faf_compat::kAllocationGranularity - 1u);
        if (aligned > rawBase) {
          munmap(raw, aligned - rawBase);
        }
        const uintptr_t tail = aligned + length;
        if (rawBase + padded > tail) {
          munmap(reinterpret_cast<void*>(tail), rawBase + padded - tail);
        }
        mapped = reinterpret_cast<void*>(aligned);
      }
    }
    if (mapped == nullptr) {
      SetLastError(address != nullptr ? ERROR_INVALID_ADDRESS : ERROR_NOT_ENOUGH_MEMORY);
      return nullptr;
    }
    if (!faf_compat::AddRegion(reinterpret_cast<uintptr_t>(mapped), length, protect, faf_compat::RegionKind::Reservation)) {
      munmap(mapped, length);
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return nullptr;
    }
    return mapped;
  }

  // MEM_COMMIT inside an existing reservation.
  const uintptr_t start = requested & ~(page - 1u);
  const uintptr_t end = (requested + size + page - 1u) & ~(page - 1u);
  faf_compat::MemoryRegion region{};
  if (!faf_compat::FindRegion(start, &region) || region.kind != faf_compat::RegionKind::Reservation ||
      end - region.base > region.length) {
    SetLastError(ERROR_INVALID_ADDRESS);
    return nullptr;
  }
  if (mprotect(reinterpret_cast<void*>(start), end - start, protection) != 0) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
  }
  return reinterpret_cast<LPVOID>(start);
}

inline BOOL VirtualFree(LPVOID address, SIZE_T size, DWORD freeType) noexcept
{
  const uintptr_t value = reinterpret_cast<uintptr_t>(address);
  if (freeType == MEM_RELEASE) {
    faf_compat::MemoryRegion region{};
    if (size != 0 || !faf_compat::RemoveRegion(value, faf_compat::RegionKind::Reservation, &region)) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
    }
    munmap(reinterpret_cast<void*>(region.base), region.length);
    return TRUE;
  }
  if (freeType == MEM_DECOMMIT) {
    const uintptr_t page = faf_compat::PageSize();
    faf_compat::MemoryRegion region{};
    if (!faf_compat::FindRegion(value, &region) || region.kind != faf_compat::RegionKind::Reservation) {
      SetLastError(ERROR_INVALID_ADDRESS);
      return FALSE;
    }
    const uintptr_t start = value & ~(page - 1u);
    const uintptr_t end = size == 0 ? region.base + region.length : ((value + size + page - 1u) & ~(page - 1u));
    if (end - region.base > region.length) {
      SetLastError(ERROR_INVALID_ADDRESS);
      return FALSE;
    }
    (void)madvise(reinterpret_cast<void*>(start), end - start, MADV_DONTNEED);
    (void)mprotect(reinterpret_cast<void*>(start), end - start, PROT_NONE);
    return TRUE;
  }
  SetLastError(ERROR_INVALID_PARAMETER);
  return FALSE;
}

inline SIZE_T VirtualQuery(LPCVOID address, PMEMORY_BASIC_INFORMATION info, SIZE_T length) noexcept
{
  if (info == nullptr || length < sizeof(MEMORY_BASIC_INFORMATION)) {
    SetLastError(ERROR_BAD_LENGTH);
    return 0;
  }
  const uintptr_t tag = reinterpret_cast<uintptr_t>(address) & faf_compat::AddressTagMask();
  const uintptr_t raw = reinterpret_cast<uintptr_t>(address) & ~faf_compat::AddressTagMask();
  const uintptr_t page = raw & ~(faf_compat::PageSize() - 1u);
  const uintptr_t highest = ~static_cast<uintptr_t>(0) >> 17;  // GetSystemInfo's lpMaximumApplicationAddress
  if (raw > highest) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }

  faf_compat::MapsEntry entry{};
  uintptr_t nextStart = 0;
  bool readable = false;
  memset(info, 0, sizeof(*info));
  info->BaseAddress = reinterpret_cast<PVOID>(tag | page);
  if (!faf_compat::FindMapping(raw, &entry, &nextStart, &readable)) {
    if (!readable) {
      SetLastError(ERROR_GEN_FAILURE);
      return 0;
    }
    info->State = MEM_FREE;
    info->Protect = PAGE_NOACCESS;
    info->RegionSize = (nextStart != 0 ? nextStart : highest + 1u) - page;
    return sizeof(MEMORY_BASIC_INFORMATION);
  }

  const DWORD protect = faf_compat::PermissionsToProtect(entry);
  info->RegionSize = entry.end - page;
  info->State = protect == PAGE_NOACCESS ? MEM_RESERVE : MEM_COMMIT;
  info->Protect = info->State == MEM_RESERVE ? 0u : protect;
  info->Type = !entry.fileBacked ? MEM_PRIVATE : (entry.sharedLibrary ? MEM_IMAGE : MEM_MAPPED);
  faf_compat::MemoryRegion region{};
  if (faf_compat::FindRegion(raw, &region)) {
    info->AllocationBase = reinterpret_cast<PVOID>(tag | region.base);
    info->AllocationProtect = region.protect;
    if (region.base + region.length < entry.end) {
      info->RegionSize = region.base + region.length - page;
    }
  } else {
    info->AllocationBase = reinterpret_cast<PVOID>(tag | entry.start);
    info->AllocationProtect = protect;
  }
  return sizeof(MEMORY_BASIC_INFORMATION);
}

inline BOOL VirtualProtect(LPVOID address, SIZE_T size, DWORD newProtect, PDWORD oldProtect) noexcept
{
  bool validProtect = false;
  const int protection = faf_compat::ProtectToPosix(newProtect, &validProtect);
  if (!validProtect || size == 0 || oldProtect == nullptr) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  MEMORY_BASIC_INFORMATION info;
  if (VirtualQuery(address, &info, sizeof(info)) == 0 || info.State != MEM_COMMIT) {
    SetLastError(ERROR_INVALID_ADDRESS);
    return FALSE;
  }
  const uintptr_t page = faf_compat::PageSize();
  const uintptr_t raw = reinterpret_cast<uintptr_t>(address) & ~faf_compat::AddressTagMask();
  const uintptr_t start = raw & ~(page - 1u);
  const uintptr_t end = (raw + size + page - 1u) & ~(page - 1u);
  if (mprotect(reinterpret_cast<void*>(start), end - start, protection) != 0) {
    SetLastError(ERROR_INVALID_ADDRESS);
    return FALSE;
  }
  *oldProtect = info.Protect;
  return TRUE;
}

// ---------------------------------------------------------------------------
// Heaps: the process heap is the C library's allocator.
// ---------------------------------------------------------------------------
inline HANDLE GetProcessHeap() noexcept
{
  static int processHeap = 0;
  return &processHeap;
}

inline LPVOID HeapAlloc(HANDLE, DWORD flags, SIZE_T bytes) noexcept
{
  const size_t size = bytes != 0 ? bytes : 1u;
  void* const block = (flags & HEAP_ZERO_MEMORY) != 0 ? calloc(1, size) : malloc(size);
  if (block == nullptr) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
  }
  return block;
}

inline BOOL HeapFree(HANDLE, DWORD, LPVOID block) noexcept
{
  free(block);
  return TRUE;
}

inline SIZE_T HeapSize(HANDLE, DWORD, LPCVOID block) noexcept
{
  return block != nullptr ? malloc_usable_size(block) : static_cast<SIZE_T>(-1);
}

inline LPVOID HeapReAlloc(HANDLE, DWORD flags, LPVOID block, SIZE_T bytes) noexcept
{
  const size_t size = bytes != 0 ? bytes : 1u;
  const size_t previous = block != nullptr ? malloc_usable_size(block) : 0u;
  if ((flags & HEAP_REALLOC_IN_PLACE_ONLY) != 0) {
    if (size > previous) {
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return nullptr;
    }
    return block;
  }
  void* const grown = realloc(block, size);
  if (grown == nullptr) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return nullptr;
  }
  if ((flags & HEAP_ZERO_MEMORY) != 0) {
    const size_t now = malloc_usable_size(grown);
    if (now > previous) {
      memset(static_cast<char*>(grown) + previous, 0, now - previous);
    }
  }
  return grown;
}

// LMEM_FIXED blocks only (the handle is the pointer), which is what
// FormatMessage's FORMAT_MESSAGE_ALLOCATE_BUFFER hands out.
inline HLOCAL LocalAlloc(UINT flags, SIZE_T bytes) noexcept
{
  if ((flags & LMEM_MOVEABLE) != 0) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return nullptr;
  }
  return HeapAlloc(GetProcessHeap(), (flags & LMEM_ZEROINIT) != 0 ? HEAP_ZERO_MEMORY : 0u, bytes);
}

inline HLOCAL LocalFree(HLOCAL block) noexcept
{
  free(block);
  return nullptr;
}

// ---------------------------------------------------------------------------
// Memory status (sysinfo). The virtual figures are the 47-bit user range.
// ---------------------------------------------------------------------------
inline BOOL GlobalMemoryStatusEx(LPMEMORYSTATUSEX status) noexcept
{
  if (status == nullptr || status->dwLength != sizeof(MEMORYSTATUSEX)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  struct sysinfo system;
  if (sysinfo(&system) != 0) {
    SetLastError(ERROR_GEN_FAILURE);
    return FALSE;
  }
  const DWORDLONG unit = system.mem_unit != 0 ? system.mem_unit : 1u;
  status->ullTotalPhys = static_cast<DWORDLONG>(system.totalram) * unit;
  status->ullAvailPhys = (static_cast<DWORDLONG>(system.freeram) + system.bufferram) * unit;
  status->ullTotalPageFile = status->ullTotalPhys + static_cast<DWORDLONG>(system.totalswap) * unit;
  status->ullAvailPageFile = status->ullAvailPhys + static_cast<DWORDLONG>(system.freeswap) * unit;
  status->ullTotalVirtual = static_cast<DWORDLONG>(~static_cast<uintptr_t>(0) >> 17) + 1u;
  status->ullAvailVirtual = status->ullTotalVirtual;
  status->ullAvailExtendedVirtual = 0;
  status->dwMemoryLoad = status->ullTotalPhys != 0
    ? static_cast<DWORD>(100u - (status->ullAvailPhys * 100u) / status->ullTotalPhys)
    : 0u;
  return TRUE;
}

inline void GlobalMemoryStatus(LPMEMORYSTATUS status) noexcept
{
  MEMORYSTATUSEX extended{};
  extended.dwLength = sizeof(extended);
  if (status == nullptr || !GlobalMemoryStatusEx(&extended)) {
    return;
  }
  status->dwLength = sizeof(MEMORYSTATUS);
  status->dwMemoryLoad = extended.dwMemoryLoad;
  status->dwTotalPhys = static_cast<SIZE_T>(extended.ullTotalPhys);
  status->dwAvailPhys = static_cast<SIZE_T>(extended.ullAvailPhys);
  status->dwTotalPageFile = static_cast<SIZE_T>(extended.ullTotalPageFile);
  status->dwAvailPageFile = static_cast<SIZE_T>(extended.ullAvailPageFile);
  status->dwTotalVirtual = static_cast<SIZE_T>(extended.ullTotalVirtual);
  status->dwAvailVirtual = static_cast<SIZE_T>(extended.ullAvailVirtual);
}

// ---------------------------------------------------------------------------
// Stack capture (libunwind). Frame 0 is the caller of this function, as on
// Windows; the hash is the sum of the captured addresses.
// ---------------------------------------------------------------------------
__attribute__((noinline)) inline USHORT RtlCaptureStackBackTrace(
  ULONG framesToSkip, ULONG framesToCapture, PVOID* backTrace, PULONG backTraceHash
) noexcept
{
  if (backTrace == nullptr || framesToCapture == 0) {
    return 0;
  }
  faf_compat::BacktraceState state{backTrace, framesToSkip + 1u, framesToCapture, 0};
  _Unwind_Backtrace(&faf_compat::BacktraceStep, &state);
  if (backTraceHash != nullptr) {
    ULONG hash = 0;
    for (ULONG i = 0; i < state.count; ++i) {
      hash += static_cast<ULONG>(reinterpret_cast<uintptr_t>(backTrace[i]));
    }
    *backTraceHash = hash;
  }
  return static_cast<USHORT>(state.count);
}

} // extern "C"

#define CaptureStackBackTrace RtlCaptureStackBackTrace

} // extern "C++"
