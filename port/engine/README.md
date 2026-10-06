# port/engine — the engine on Android arm64

The engine is `src/sdk`, built on Windows by `src/sdk/main.vcxproj`. This
directory holds what it takes to compile the same sources with the Android
NDK for arm64. It is milestone M2 of [the roadmap](../../docs/port/android-roadmap.md)
(W1.1–W1.2): every translation unit compiles for `aarch64-linux-android`, the
Windows Win32 and x64 builds stay exactly as they are. Nothing is linked yet.

| Path | What it is |
|---|---|
| `compile_flags.txt` | The arm64 compile flags, plus which Windows-only project defines and include directories to drop |
| `shim/` | Android-only include directory, never on the Windows include path. `windows.h`, `winsock2.h`, `ws2tcpip.h`, `mmsystem.h`, `intrin.h`, `io.h`, `direct.h`, `sys/timeb.h`, `crtdbg.h` forward to `faf_win_compat.h` (Win32 types, macros, structs, CRT names, 32-bit Interlocked over `__atomic`). `string.h`, `wchar.h`, `ctype.h`, `wctype.h` and `stdlib.h` are interposed (`#include_next` the real header, then `faf_msvc_crt.h` adds the MSVC CRT names), so TUs that never reach `Platform.h` get them too. `boost/thread/xtime.hpp` applies the repo's `TIME_UTC` push/undef/pop patch for clang |
| `../../scripts/port/engine_sweep.py` | The sweep: compiles every engine TU with these flags and reports what stops each one |

## How a TU is compiled

The sweep does not keep a second list of sources or settings. For every
`ClCompile` item that `main.vcxproj` builds in **Release|x64** (items with
`ExcludedFromBuild` and `<None>` items are skipped), it builds one clang
command, in this order:

1. `-c <tu> -o <obj>` and the sweep's diagnostic switches
   (`-ferror-limit=0 -fno-color-diagnostics -fdiagnostics-absolute-paths`).
2. The project's `PreprocessorDefinitions` (with per-item overrides such as
   `moho/app/WxUrl.cpp`'s, and `UNICODE;_UNICODE` from `CharacterSet`, as
   MSBuild adds them) and `UndefinePreprocessorDefinitions`, minus the
   `drop-define:` entries of `compile_flags.txt` (`WIN32`, `_WINDOWS`).
3. `-I port/engine/shim`, ahead of everything else, so the shim's
   `windows.h`/`intrin.h` win over clang's own x86-only headers.
4. The project's `AdditionalIncludeDirectories`, then its `IncludePath`, minus
   the `drop-include:` entries (wxWidgets 2.4.2, the D3DX SDK). The toolset's
   `$(IncludePath)` (Windows SDK, MSVC headers) is empty here.
5. The project's `ForcedIncludeFiles`: `platform/X64LayoutAsserts.h`, which
   turns the Win32 layout `static_assert`s off on every non-x86 target. They
   describe the 2007 x86 image and fail on every type that holds a pointer.
6. The rest of `compile_flags.txt`: `--target=aarch64-linux-android26
   -std=c++20 -fms-extensions -fsigned-char -ffp-contract=off ...`, each with
   the reason in the file.

`python scripts/port/engine_sweep.py --files <tu> --show-command` prints the
exact command for a TU, ready to paste into Git Bash.

`main.vcxproj` has 1022 `ClCompile` items. 12 are excluded from the build
(`moho/misc/CrtRuntimeHelpers.cpp`, and 11 libpng/zlib runtime files with
`Condition="true"`), so the sweep covers **1010** TUs. The first measurements
in the roadmap counted 1021, because their script did not treat
`Condition="true"` as excluding.

## Running the sweep

Needs Python 3.8+ and the NDK (default `C:\Android\sdk\ndk\29.0.14206865`,
the version `scripts/port/build_android.ps1` uses; or `ANDROID_NDK_ROOT`,
`--ndk DIR`, `--clang PATH`). A full sweep takes about 4 minutes on 20 cores.

```sh
# Everything, into buildstage/engine-sweep/latest:
python scripts/port/engine_sweep.py

# Re-check what a change touches, against the baseline, in a directory of its own:
python scripts/port/engine_sweep.py --files "gpg/core/containers/*" "moho/net/CMessage.cpp" \
    --out buildstage/engine-sweep/mine --compare buildstage/engine-sweep/baseline

# Re-run only what failed in a sweep, and update that sweep in place:
python scripts/port/engine_sweep.py --rerun-failed buildstage/engine-sweep/mine \
    --out buildstage/engine-sweep/mine --merge

# Gate: exit 1 if a TU that compiled in the baseline no longer does:
python scripts/port/engine_sweep.py --compare buildstage/engine-sweep/baseline --fail-on-regression
```

| Option | |
|---|---|
| `--files GLOB...` | TU paths relative to `src/sdk`, or globs over them. `/` or `\`, optional `src/sdk/` prefix, case-insensitive; `*` also crosses `/`. A pattern that matches nothing is an error. |
| `--rerun-failed SWEEP` | Only the TUs that failed in that sweep (a directory or its `results.json`). Combines with `--files`. |
| `--compare SWEEP` | Adds newly compiling / no longer compiling TUs and a before column per subsystem to the report. |
| `--merge` | Replace only the TUs compiled now in `--out`'s existing results; the report covers all of them. |
| `--out DIR` | Default `buildstage/engine-sweep/latest`. Without `--merge`, the sweep's own `logs/`, `obj/`, `results.json` and `report.md` there are replaced; nothing else is deleted. |
| `--jobs N` | Parallel compilers, default the CPU count. |
| `--sdk DIR` | Sweep another `src/sdk` (a snapshot or a worktree). Its `main.vcxproj` decides the TUs, and its `FafRepoRoot` the dependencies. |
| `--no-shim`, `--shim DIR` | Leave the shim off, or use another one. |
| `--flags FILE`, `--extra=ARG` | Another flags file; one more clang argument (repeatable), e.g. `--extra=-Wshorten-64-to-32`. |
| `--config CFG` | Another `main.vcxproj` configuration, default `Release|x64`. |
| `--list`, `--show-command` | Print the selected TUs, or their clang commands, and stop. |
| `--report-only` | Rewrite `--out`'s report (and the classes in its `results.json`) from its results and logs without compiling, e.g. with another `--compare`. |
| `--timeout S`, `--error-limit N`, `--log-limit-mb M`, `--keep-objects` | Per-TU time limit (600 s), clang's `-ferror-limit` (0 = all), how much of each TU's output its log keeps (8 MB; the counts always cover all of it), keep the `.o` files. |

### Output

- `report.md` — the OK count; the first-error class of every failing TU; the
  top first-error sites (the `file:line` that stops the most TUs is the best
  next fix); the files that hold first errors; the total of unique error sites;
  the undeclared identifiers and unknown type names by the number of TUs they
  stop (the shim's to-do list); warnings by flag; a per-subsystem table; and
  every failing TU with its first error.
- `results.json` — per TU: `ok`, `exit_code`, `errors`, `warnings`,
  `first_error` (file, line, column, message, the include chain that led there,
  the notes that follow it), `class`, `seconds`, `log`. Plus the sweep's
  metadata: git revision, clang version, the flags file's hash, the arguments
  common to all TUs.
- `logs/<tu>.log` — the command and clang's complete output.

The classes come from a heuristic over the first error's message and location
(`classify()` in the script). They say what kind of fix a TU needs first, not
everything it needs: a TU often stops on the next problem once the first is
gone. The sites and the identifier list are exact.

## Changing the engine for arm64

These rules hold for every change that M2 and later make to `src/sdk` for
the port:

- **The Windows builds compile exactly what they compiled before.** A change
  is either semantically identical under MSVC (moving a default argument to
  the first declaration) or guarded so MSVC, x86 and x64 take the old code
  (`#if defined(_MSC_VER)`, `#if !defined(_WIN32)`, `__aarch64__`,
  `_M_IX86`). When in doubt, guard.
- Platform stand-ins go into `shim/`, not into engine files. Engine files
  change only where the code itself is not portable C++.
- A non-x86 fallback that is not bit-exact (x87 math, for instance) is marked
  as such and names its roadmap workstream (W5).
- Re-run the sweep with `--compare` against the baseline before and after,
  and keep the Windows build green.

## Baseline

`buildstage/engine-sweep/baseline` is the reference sweep for M2: commit
`6c1dfd1c` plus the generalised `platform/X64LayoutAsserts.h` and this
`compile_flags.txt`, without a shim, run on a snapshot of `src/sdk`:

```sh
git archive HEAD src/sdk | tar -x -C <tree>      # plus <tree>/dependencies -> dependencies (junction)
python scripts/port/engine_sweep.py --sdk <tree>/src/sdk --no-shim --out buildstage/engine-sweep/baseline
```

It compiled **276 of 1010** TUs. What stopped the other 734 first:

| TUs | First error |
|---:|---|
| 457 | `gpg/core/containers/FastVector.h:2950`: default argument added to an already declared function template |
| 211 | x86-only intrinsic headers: `<immintrin.h>` from `platform/Platform.h:35` (108), `<intrin.h>` from `moho/misc/InstanceCounter.h:2` (98), 5 direct includes |
| 26 | Windows SDK / MSVC CRT headers (`Windows.h`, `winsock2.h`, `direct.h`, `sys/timeb.h`) |
| 19 | boost 1.34 `boost/thread/xtime.hpp:24`: its `TIME_UTC` enumerator against C11's macro |
| 10 | `wx/defs.h` via `platform/WxWidgets.h:39` |
| 11 | the rest: MSVC CRT names (`_stricmp`, `memcpy_s`, `_towlower_l`), `d3d9.h`/`dxgi.h`, `Reflection.h:5908` (two-phase lookup), `std::auto_ptr` in boost.thread, `X87Math.cpp` without its x86 half, `AutoPtr.h:198` |

## Status after M2

`buildstage/engine-sweep/final`, the shim plus the engine changes of M2: **896 of 1010** TUs compile
for aarch64 (baseline 276, +620, none lost). The Windows build is unchanged: MSBuild Debug|Win32
rebuilds with the same 129 warnings, and in Debug/Release × Win32/x64 1002 of 1010 objects are
byte-identical to the previous commit; the other 8 differ only in `__LINE__` values that an added
`#include` line shifted.

What stops the remaining 114 is the platform layer and the renderer, not the engine code:

| TUs | First error | Workstream |
|---:|---|---|
| 60 | `wx/defs.h` (`moho/ui` alone 26 of 32) | W2 platform layer |
| 18 | `d3d9.h`, `d3dx9effect.h`, `dxgi.h`, `d3d10.h` | W3 renderer |
| 18 | Win32 API without a stand-in (`CreateEventW`, `SetEvent`, `WSAEventSelect`, `GetFileAttributesA`, ...) | W2 |
| 10 | Engine code tied to the MSVC CRT or Winsock layouts (`LuaObject.cpp:75` `_iob`, `in_addr::S_un`, `fd_set::fd_count`) | W2 |
| 4 | `<xmmintrin.h>`/`<emmintrin.h>`/`<mmintrin.h>` for MXCSR and movie SIMD | W5 / W4 |
| 4 | `mmreg.h`, `DbgHelp.h`, `shellapi.h` | W2 |

Known ARM hazards found on the way, not fixed yet (M3): `Cluster.cpp:30-60` mirrors boost's Win32
`sp_counted_base` with `volatile long` counts; `platform/Platform.h` `LODWORD`/`HIDWORD` read
`unsigned long` (8 bytes on LP64; unused today); `Global.cpp`'s TLS callback in `.CRT$XLB` is the
same ELF trap `GPG_PREREGISTER_INIT` had; `CConCommand.cpp:1505` throws `std::exception(const char*)`,
which only MSVC's library has.
