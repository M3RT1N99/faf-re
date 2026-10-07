# port/engine/lowarena — the headless runner's memory below 2 GB

The x64 Windows build links with `/LARGEADDRESSAWARE:NO` (`src/sdk/main.vcxproj`), so every pointer
it makes fits in 31 bits, and the engine's many pointer-in-`int` words (the reflection callbacks, the
hidden 32-bit pointer fields of [W1.3](../../../docs/port/android-roadmap.md)) keep working. Android
has no such switch. LowArena is its stand-in for **the headless replay runner executable only**
(`faf_headless_runner`, milestone M3c; since release 0.4.0 also packaged in the APK as
`lib/<abi>/libfafrunner.so`, which the in-app replay test execs as a process of its own): it keeps
the heap, thread stacks, `thread_local` data, the engine library's image, `VirtualAlloc`
reservations, file views, argv and the environment in **[16 MB, 0x7FFF0000)**. The app process
itself never uses it (an app process cannot interpose malloc, and ART leaves at most a few hundred
MB free below 2 GB); M3d removes the need for it with real truncation fixes.

2 GB, not 4 GB: the engine stores pointers in `int`s, and `(char*)(int)0x90001000` is
`0xffffffff90001000` on both 64-bit ABIs. Below 2 GB the round trip through `int` is exact.

`FAF_LOWARENA=0` in the environment turns it off: the malloc family forwards to bionic's allocator
(Scudo), threads get bionic's stacks, the engine library is loaded with plain `dlopen`. That run is
the truncation oracle for M3d.

| File | What it is |
|---|---|
| `LowArena.h` | The `lowarena_*` interface (C). Defined by the runner executable; everything else declares it weak |
| `LowArena.cpp` | The range allocator, the heap's `sbrk`, the exported malloc family, thread stacks, VirtualAlloc/MapViewOfFile ranges, the report. Linked into `faf_headless_runner` only |
| `LowArenaHeap.c` | dlmalloc's configuration; compiles `dlmalloc.c` |
| `dlmalloc.c` | Doug Lea's malloc, public domain, vendored unchanged (version below) |
| `LowArenaThreads.cpp` | `__wrap_pthread_create/join/detach`, linked into `libfafengine.so` |
| `probe/LowArenaProbe.cpp` | `libfafarenaprobe.so`: a stand-in for the engine library that checks where everything ends up |

`scripts/port/build_runner.py` builds all of it: LowArena.cpp and LowArenaHeap.c into the
executable (exported with `--export-dynamic-symbol` for the malloc family and `lowarena_*`, not
`-rdynamic`, which would also export the executable's static libc++), LowArenaThreads.cpp plus
`-Wl,--wrap=pthread_create,--wrap=pthread_join,--wrap=pthread_detach` into the engine library, and
with `--probe` the probe library.

## How it works

- **Address space.** A range allocator owns the arena. It reserves segments from the kernel as
  needed (first fit from 16 MB, 16 MB-aligned, at least 64 MB at a time, `PROT_NONE`,
  `MAP_NORESERVE`, `MAP_FIXED_NOREPLACE` with an address check because kernels before 4.17 treat the
  flag as a hint) and hands out 64 KB granules, the Windows allocation granularity. One byte per
  granule (32 KB for 2 GB) says what it holds: free, heap, stack, VirtualAlloc, view or image. The
  ranges are named in `/proc/self/maps` (`[anon:lowarena:heap]`, `...:stack`, `...:guard`,
  `...:virtual`, `...:image`; free space is `[anon:lowarena]`). Segments can be anywhere below the
  limit, so the arena works around whatever else is mapped low.
- **Heap.** dlmalloc's global heap, with one lock (pthread mutex), 16-byte alignment, no mmap of its
  own and `lowarena_morecore` as `sbrk`. The heap grows in 16 MB ranges, in place when the address
  space above it is free, otherwise as a new dlmalloc segment. It never shrinks: freed memory is
  reused, address space is not returned (its high-water mark is dlmalloc's max footprint).
- **The malloc family** (`malloc`, `free`, `calloc`, `realloc`, `reallocarray`, `memalign`,
  `posix_memalign`, `aligned_alloc`, `valloc`, `pvalloc`, `malloc_usable_size`) is defined in the
  executable and exported, so libc's own allocations (`strdup`, `fopen`, `opendir`), libc++, emulated
  TLS, exception objects and everything in `libfafengine.so` come from the arena. A pointer the arena
  did not hand out (bionic's, recognised by its address) is freed and sized by the next definition
  (`dlsym(RTLD_NEXT)`, i.e. libc's); `realloc` of such a block moves it into the arena (a new arena
  block, the old contents copied up to libc's usable size, libc's block freed), so realloc never
  hands the engine a pointer above 2 GB. The report counts both (`foreign_frees`,
  `foreign_reallocs`; 0 in every replay so far). libc's eight functions are resolved together, once,
  under a lock, when the mode is decided (the first `malloc` after libc has set up the environment,
  before `main` and before any other thread); a call that comes back into the allocator from inside
  that `dlsym` gets the arena. `realloc(p, 0)` frees and returns null, as bionic and the MSVC CRT do.
- **Thread stacks.** Every thread the engine library starts without its own stack (boost::thread,
  std::thread, the shim's thread functions) gets a 4 MB arena stack (`main.exe`'s StackReserveSize is
  4000000; a larger requested size wins) above a 64 KB `PROT_NONE` guard. A plain interposer named
  `pthread_create` is not enough: under the emulator's ARM translation the translator takes that call
  first, so the library is linked with `--wrap`. Bionic keeps the thread's `pthread_internal_t` at
  the top of a caller-supplied stack and reads it in `pthread_join`/`pthread_detach` (on API 36 its
  static TLS and `bionic_tls` are not there but in a separate per-thread mapping above 2 GB, see
  below), so a stack is released only after a successful `pthread_join`, or, for a detached thread, once the
  kernel no longer knows the thread (`tgkill(pid, tid, 0)` fails with `ESRCH`; checked at the next
  thread create/join/detach). Join and detach are bracketed (`*_begin`/`*_end`) so that a new thread
  reusing a `pthread_t` value cannot be mistaken for the old one.
- **VirtualAlloc and MapViewOfFile** (`port/engine/shim/faf_win_memory.h`, `faf_win_file.h`): a
  reservation without an address and every file view take an arena range (the view is mapped
  `MAP_FIXED` into it); `VirtualFree(MEM_RELEASE)` and `UnmapViewOfFile` give it back
  (`faf_compat::UnmapRegion`), which drops the pages and restores the reservation. Commit, decommit
  and `VirtualQuery` work on arena ranges as on any other mapping.
- **The executable** (`port/engine/runner/RunnerMain.cpp`) copies argv and the environment into the
  arena, then starts one thread on a 4 MB arena stack and does everything else there: it reserves a
  range for `libfafengine.so` from its ELF program headers (about 17 MB today, reserved in 16 MB
  steps) and loads it into it with `android_dlopen_ext(ANDROID_DLEXT_RESERVED_ADDRESS)`, so the
  library's static initialisers already run on a low stack, then calls `faf_headless_main`, joins,
  prints the report and returns the exit code.
- **The top.** Natively the arena stops at **0x7FFF0000**, where a `/LARGEADDRESSAWARE:NO` process's
  user space ends on Windows (0x7FFEFFFF is its last byte), so no range ends exactly at 2 GB, whose
  one-past-the-end pointer would turn negative through an `int` (release 0.4.0; before, the limit
  was 0x80000000). Segments are 16 MB-aligned, so the last usable byte is 0x7EFFFFFF.
- **Under the emulator's ARM translation** (detected from `libndk_translation`/`libberberis` in
  `/proc/self/maps`, arm64 builds only) the translator keeps its code cache in `MAP_32BIT` memory,
  [1 GB, 2 GB), and adds to it while guest code runs. The arena then stays below **1.75 GB**, leaving
  the top 256 MB of that window to the translator.

What stays high in any case (measured from `/proc/<pid>/maps` during a T1 run on the API 36
emulator, x86_64):

- the main thread's stack (the engine does not run on it) and the original argv/environment block
  (copied);
- every thread's static TLS, TCB and `bionic_tls` (the buffers behind `strerror`, `strsignal`,
  `basename`, `dirname`, ...): on API 36 a separate per-thread mapping
  `[anon:stack_and_tls:<tid>]`, not part of the caller-supplied stack;
- every thread's alternate signal stack (`[anon:thread signal stack]`);
- the executable image, so `&malloc`, `&free` and the executable's other exported functions are
  high function pointers;
- bionic's own data (`stdout`, its globals), the dynamic linker and the system libraries, including
  `/system/lib64/libc++.so` (pulled in by liblog), `bionic_alloc`/`linker_alloc`, Scudo's primary
  reserve (no live blocks), the CFI shadow and an unnamed 24 MB `PROT_NONE` reservation.

`errno` and `pthread_self()` are in that per-thread mapping too (the probe reports them HIGH under
translation); the shim's `GetCurrentThreadId` uses `gettid`, so nothing the engine keeps in an `int`
comes from them. With the NDK's minSdk 26 `thread_local` is emulated TLS, allocated through malloc and
therefore low; raising the engine to minSdk 29 or later would put it in ELF TLS, which is high.

## Report

The executable prints one line on stderr after the engine thread has returned:

```
[lowarena] enabled=1 heap_max_mb=354.3 heap_mb=354.3 high_water_mb=412.2 in_use_mb=400.0 reserved_mb=448.0 top=0x1ac30000 limit=0x7fff0000 segments=1 [0x01000000-0x1d000000] image=0x02410000+0x02000000 threads_total=4 threads_peak=3 threads_live=0 thread_fallbacks=0 views_peak=1 view_fallbacks=0 virtual_peak=0 virtual_fallbacks=0 foreign_frees=0 foreign_reallocs=0
```

`heap_max_mb` is the heap's high-water mark, `high_water_mb` that of everything the arena handed out
(heap, stacks, views, reservations, image), `top` the highest address it ever handed out, `limit`
0x7fff0000 (0x80000000 before release 0.4.0) or 1.75 GB (`(translated)`). The `*_fallbacks` count what found no room and went to bionic
instead (high); they should be 0. Before the engine starts it prints where the library and the
engine thread's stack are (`[lowarena] .../libfafengine.so at 0x2410000, engine thread stack near
0x240fbc4`). When the arena runs out it prints `[lowarena] no room below <limit> ...` once; the
allocation then fails (heap) or falls back to bionic (thread stacks, views, reservations).

## Heap check

`FAF_LOWARENA_CHECK=1` (or `2`, which also records the allocating frames) puts a checking layer over
the heap, for hunting heap corruption on the device:

- Every block gets a 96-byte header (magic, requested size, sequence number, up to four return
  addresses in `libfafengine.so` from the frame-pointer chain, and 16 canary bytes right before
  the block) and 16 canary bytes after it. `malloc_usable_size` reports the requested size.
- `free` and `realloc` check the header and both canaries. A freed block is filled with `0xdd` and
  kept in a quarantine (256 MB or 1M blocks) before the allocator gets it back; leaving the
  quarantine checks that the fill is intact, which catches writes after free.
- Every live block is also on a list. Every `FAF_LOWARENA_CHECK_SCAN` allocations (default 65536,
  `0` = only at exit) every live block and every quarantined one is checked, so an overflow of a
  block that is never freed is found too, between two scans.
- Damage is reported on one `[lowarena] check: ...` line (what, block, size, sequence number, the
  allocating frames as absolute addresses with `image=` to subtract, the first damaged byte and a
  dump) and the process aborts, so the tombstone holds the stack of the call that found it.
- With `FAF_LOWARENA=0` the checked blocks come from bionic's allocator instead, at the high
  addresses the APK sees.

It costs memory (heap high-water about 960 MB for T1 instead of 350 MB) and time (T1: 10 s instead
of 8 s on x86_64, 43 s instead of 30 s under translation). The exit report adds
`check=... checked_allocs checked_frees quarantine_verified quarantined live scans`.

What it found in M3c: `Cluster.cpp`'s mirror of boost's shared-count release, which under the
Itanium ABI deleted the control block through vtable slot 1 and then decremented a word of the
freed block (`ReleaseSharedCount`, now guarded; see [M3c in the runner README](../runner/README.md)).
Symbolize the frames with `llvm-symbolizer --obj=libfafengine.so --relative-address` after
subtracting `image=` (and 1, for the call instruction). With the fixes of M3c, T1-T3 and R4 run
clean under `FAF_LOWARENA_CHECK=1` on x86_64 and T1 under `=2` as arm64.

`FAF_LOWARENA_FILL=<byte>` (e.g. `0xff`) is the companion for uninitialised reads: without the
check layer, so every address stays the same, it fills each block `malloc`/`memalign` hand out
with that byte. A different checkpoint chain under a different fill means the sim reads memory
nobody wrote (M3c: T3 and R4 do, see the runner README). Only those blocks are filled: not the tail
`realloc` adds to a grown block, not `calloc` (zeroed anyway) and not the stack. So "0x00 and 0xcd
change nothing" bounds only uninitialised reads of freshly malloc'd memory.

## Probe

```sh
python scripts/port/build_runner.py --abi x86_64 --out buildstage/runner/<dir> --probe
adb push <dir>/faf_headless_runner <dir>/libfafarenaprobe.so /data/local/tmp/<dir>/
adb shell 'cd /data/local/tmp/<dir> && chmod 755 faf_headless_runner && FAF_ENGINE_LIB=$PWD/libfafarenaprobe.so ./faf_headless_runner'
#   ... FAF_LOWARENA=0 ...            the arena off: everything must be high, the heap Scudo's
#   ... ./faf_headless_runner capacity  fill the arena to its limit first
```

It checks 41 placements (heap in all its forms, libc-internal allocations, exception objects, the
library's `.text/.data/.bss/.rodata`, argv, the environment, `thread_local` and stack of the engine
thread, std::thread, pthreads with default, 8 MB and detached attributes, VirtualAlloc, a file view)
plus the round trip through `int`, a 4-thread allocator stress with cross-thread frees and
`realloc`/`memalign`/`calloc` checks, 97 threads of churn (joined and detached stacks come back),
VirtualAlloc/VirtualFree/VirtualQuery and MapViewOfFile/UnmapViewOfFile reuse, and pointers from
libc's own malloc: freed through libc, and moved into the arena by `realloc` with their contents
(release 0.4.0). Exit code = failed checks. The APK (0.4.0) ships it as
`lib/<abi>/libfafarenaprobe.so` for the replay test's self-test, run as
`FAF_ENGINE_LIB=<nativeLibraryDir>/libfafarenaprobe.so libfafrunner.so [capacity]`.

Two checks had to change for the app (the first in-app run failed them, not the arena): the
`opendir` placement opens `/proc/self` instead of `/`, which an app's `untrusted_app` domain may not
list; and the file-view checks map the runner executable only when its name does not end in `.so`.
The shim reports a view of a `*.so` file as `MEM_IMAGE` (`/proc/self/maps` cannot tell it from a
loaded library), and in the APK the executable is `libfafrunner.so`, so there the probe maps a copy of
its first 64 KB in `$TMPDIR` (the app's cache directory) and deletes it afterwards.

Measured 2026-10-07 on the API 36 emulator (`fafre_api36`, kernel 6.6, 4 KB pages):

| | arena on | FAF_LOWARENA=0 | capacity |
|---|---|---|---|
| x86_64 (native) | PASS 41/41 | PASS 41/41, heap `[anon:scudo:primary]`, stacks `[anon:stack_and_tls:*]` | 1968 MB of heap in one segment [16 MB, 2 GB); 0.4.0 (top 0x7FFF0000): 1952 MB, segment [0x01000000-0x7f000000] |
| arm64-v8a (translated) | PASS 41/41 | PASS 41/41, heap `[anon:scudo:primary]` | 1648-1664 MB in two segments around the translator's code cache at 1 GB; limit 1.75 GB; translated code kept running afterwards |

The 0.4.0 binaries (build ids in the [runner README](../runner/README.md#status-2026-10-07-m3c-release-040)) pass the same three runs on both ABIs (41/41, `realloc` of libc's block moved into the arena and its contents kept; with the arena off it stays bionic's).
In the app (the replay test's self-test, the runner exec'd from `nativeLibraryDir` under the
`untrusted_app` seccomp filter and the linker's "system" namespace), with the probe changed as above:
41/41 and capacity 1952 MB with the x86_64 APK, 41/41 and 1664 MB (limit 0x70000000) with the
arm64-v8a APK under translation.

With the real engine (M3c, the results in
[headless-replay.md](../../../docs/port/headless-replay.md#android-runner-m3c)), all four M3a replays
play to the end with the arena on x86_64 and T1 as arm64 under translation, with no fallbacks:
heap high-water 309-344 MB, everything the arena handed out 364-396 MB, highest address
0x19c30000, the same T1 checkpoint chain on both ABIs.

With `FAF_LOWARENA=0` x86_64 T1 stops while the rules load, on the map loader thread: SIGSEGV at
a fault address that is a heap pointer cut to 32 bits, in `AddMappedBlueprintOrdinalBits`
(`EntityCategoryReflection.cpp:169`) <- `EntityCategory::Add` <- `ParseEntityCategory` <-
`RUnitBlueprint::AddEconomyRestrictions` <- `RRuleGameRulesImpl::SetupCategories`: the category
set's `mWordUniverseHandle`, class C2 of the M3 truncation analysis and the first item on M3d's
list. (Before the M3c `WrapFile` fix it stopped earlier, with Scudo reporting a corrupted chunk
header while Lua loaded its libraries; that was the stray write, not a truncation.)

## dlmalloc version

`dlmalloc.c` is **version 2.8.3** (Doug Lea, public domain), taken unchanged from libffi 3.3's
`src/dlmalloc.c` (sha256 `d2028560b37849ca6b7b12504422aa832d6cf2f85f720cdd87799379744f54ad`), the
only copy available locally. libffi's own changes to it are inside `#if FFI_MMAP_EXEC_WRIT` (off
here), an OS/2 port and warning fixes. The intended version is 2.8.6 (2012, CC0); replacing the file
is a drop-in (the configuration macros in `LowArenaHeap.c` are the same) once someone downloads it
from Doug Lea's site. Of the fixes between 2.8.3 and 2.8.6 none applies to this configuration: no
mmap, no footers, no `MORECORE_CONTIGUOUS` (the 2.8.6 `sys_alloc` fix), and every morecore range
starts 64 KB-aligned (the 2.8.4 16-byte-alignment padding fix).

## Limits

- **It hides truncation bugs.** A runner that works with the arena says nothing about the APK; the
  `FAF_LOWARENA=0` run and M3d's fixes do.
- **Capacity:** about 1.97 GB natively, 1.65 GB under translation, shared by heap, stacks (4 MB each),
  views and the 32 MB image range. The heap's address space is never given back, so its peak stays
  reserved for the rest of the run.
- **Addresses differ from Windows.** Deterministic per run and ABI, but not the PC's; Lua hashes light
  userdata by address, so pointer-ordered iteration can differ between platforms.
- **The phone is unmeasured:** a Samsung kernel, a 39-bit address space, 16 KB pages and Scudo's
  top-byte tags may lay the low 2 GB out differently. The arena only needs `MAP_FIXED_NOREPLACE`
  (or hint semantics) and free space below 2 GB; the probe above is the first thing to run there
  (release 0.4.0's in-app self-test does exactly that).
