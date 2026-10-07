# Headless replay runner

`main.exe /headlessreplay <file.scfareplay>` plays one replay to its end with no window, device,
sound or UI and exits with a JSON summary. It is milestone M3a of the
[Android roadmap](android-roadmap.md): the x86 reference that the arm64 runner (M3b/M3c) is
compared against, and the first evidence that the recovered sim survives complete FAF games.

The code is `src/sdk/moho/app/HeadlessReplay.{h,cpp}`. WinMain branches into it before any GUI
setup, so without the flag main.exe takes exactly its old path. The TU is portable (no wx, D3D or
UI) and also compiles for `aarch64-linux-android`.

## Running it

The engine only loads a replay whose header says `Supreme Commander v1.50.3764`, so convert a
vault `.fafreplay` first:

```bash
python scripts/perf/convert_replay.py <id>.fafreplay buildstage/replays-m3/<id>.scfareplay --as-version 3764
```

```bash
output/main/Win32/Debug/main.exe /headlessreplay buildstage\replays-m3\<id>.scfareplay /init C:\ProgramData\FAForever\bin\init_faf.lua /log run.log /headlesssummary run.json
```

From Git Bash, prefix `MSYS_NO_PATHCONV=1` so the `/options` are not rewritten as paths.
Replays contain player names: keep them under `buildstage/` (gitignored), never in the repository.

| Option | Meaning |
|---|---|
| `/headlessreplay <file>` | the replay; the only required option |
| `/headlesssummary <file.json>` | write the JSON summary there (it is always printed as one `RESULT` line) |
| `/headlessprogress <beats>` | progress line every N beats (default 500, 0 = off) |
| `/headlesstimeout <seconds>` | give up after this long without a new beat (default 120) |
| `/headlessspeed <rate>` | requested game speed, -10..50 (default 50 = as fast as possible) |
| `/headlessinterlocked` | run the beats on the main thread instead of the driver's "Sim" thread |

The engine's own options keep their meaning: `/init`, `/log`, `/synclog`, `/simworkers`, `/sse2`,
`/prefs`.

Exit codes: 0 the replay played to its end (checksum mismatches do not change this), 1 bad
arguments or setup, 2 the scenario failed to load, 3 the sim failed or the engine died, 4 no
progress within the timeout. Asserts, `gpg::Die` and access violations are caught and written to
the summary, so no dialog can block an unattended run.

## What it does

It follows the GUI's replay path (`/replay` -> `VCR_SetupReplaySession` -> the `WLD_Frame` states
Preload, Loading, Initialize, PostInitialize, Playing) and keeps every step that feeds the sim:
the world-session loader, the four launch-info fields `WLD_DoLoading` fills (rules, a copy of the
map, props, language), `SIM_CreateDriver`, the ready handshake and the beat pipeline. The user side
(`CWldSession`, UI, renderer, sound, cameras) is replaced by a loop that drains the driver's sync
packets. Each skipped step is listed in the code where it would have happened, with the reason it
cannot change what the sim computes.

Before the engine reads the replay, the runner scans it for the number of beats and for every
checksum the players' clients recorded. At each recorded beat it takes the sim's own digest
(`Sim::GetBeatChecksum`) and compares. The engine compares too (`Sim::VerifyChecksum`); its desync
reports always agree with the runner's count.

## Comparing runs

Compare runs by `checkpoint_chain_fnv1a` (a hash over every checkpoint digest in order),
`game_over_beat` and the `checkpoints` list, and only between runs whose `inputs` block is equal:

```json
"inputs": {"language": "us", "prefs": "", "sim_workers": "default", "sse2": false, "seed": 126390721, "armies": 4, "command_sources": 2}
```

`language` comes from `/prefs` (default: none, so Localization.lua picks `us`). It did not change
any digest in the runs below. Wall times are not comparable between Debug and Release builds.

## Baseline (Debug|Win32, FAF 3839 data + Steam SCFA)

Measured 2026-10-06 (M3a); measured again 2026-10-07 after the M3c `SetAutoMode` fix (below), with
the same chains. These chains are the baseline from M3c on.

| Replay (vault id) | Map, players | Recorded on | Beats | Sim speed | Peak working set | Checkpoint chain |
|---|---|---|---|---|---|---|
| 26675870 | SCMP_026, 1v1 | 3831 | 465 | 240 beats/s | 271 MB | `7e159db290f576f1` |
| 26119449 | SCMP_017, 1v1 | 3829 | 4695 | 355 beats/s | 273 MB | `cac943d2cc0593a9` |
| 26679175 | SCMP_007, 1v1 | 3831 | 7315 | 170 beats/s | 330 MB | `411acb68b7c57709` |
| 26693269 | SCMP_015, 2v2 | 3831 | 10549 | 104 beats/s | 411 MB | `7ec6527e3f1efcb8` |

- All four play to the end, with exit code 0 and the sim's game-over flag set.
- Every chain is identical across repeated runs, threaded and interlocked mode, `/simworkers 0`,
  and three separate builds.
- Loading takes 11-12 s per replay.
- **M3c: `Unit:SetAutoMode` is registered.** Until M3c, `Sim.cpp` gave the user-Lua global
  `SetAutoMode` binder (0x008BAD80) the name of the sim method's binder
  (`func_UnitSetAutoMode_LuaFuncDef`, 0x006C8000), and the `/FORCE` link kept `Sim.cpp`'s copy, so
  the sim's `Unit:SetAutoMode` (FAF calls it for silos and AI platoons) was missing from the Sim
  set. Now both are registered, as in the binary: the Sim set has 731 binders instead of 730, the
  User set keeps `<global> SetAutoMode` (499). None of the four replays calls `Unit:SetAutoMode`
  (their logs have no Lua error naming it), so all four chains are unchanged.

**Checksums do not match the recordings yet, and that is expected.** The data is FAF 3839 and the
replays were recorded on 3829/3831. The beat-0 digest covers the blueprints, so it differs from
the first checkpoint on. The one exception confirms the method: in 26679175 the beat-50 digest
equals both players' recorded digests, so its first 50 beats are bit-identical to the original
game. Checksum-clean runs need version-matched data; that is M4.

## Android runner (M3b)

The same code links for Android: `libfafengine.so` (every translation unit the runner reaches,
including every static initialiser that registers something) plus `faf_headless_runner`, a small
executable that loads it and calls `faf_headless_main`, the counterpart of WinMain's
`/headlessreplay` branch. The command line is main.exe's.

```sh
python scripts/port/build_runner.py                # arm64-v8a -> buildstage/runner/arm64-v8a
python scripts/port/build_runner.py --abi x86_64   # for WSL and the x86_64 emulator
```

How the link set is chosen, what stands in for the user side and how to rebuild it is in
[port/engine/runner/README.md](../../port/engine/runner/README.md). In short:

- **Same code, same order.** The closure is computed from the Windows objects, linked as whole
  objects in main.vcxproj's order, and every TU that registers Lua binders, reflected types or
  console variables is in it, except 29 user-side ones that M2's sweep could not build for Android
  (UI types, `CWldSession`, `UserUnit`, the lobby; 7 of them build since M3b, and linking them is
  open). None of the 29 registers into the Core or Sim Lua sets. The runner prints the registry
  sizes at startup and writes them to the summary (`registry`), so an Android run can be checked
  against the Windows values: `/headlessregistry <file>` dumps every entry for a diff.
- **The user side** that the code references but the runner never uses is replaced by stand-ins
  that return what the Windows runner sees (null session, null wx application, no sound engine),
  or by traps that end the run if they are ever reached.
- **One deliberate difference:** Android has no D3D9 device, so the map's textures (preview,
  stratum masks, water map, background, sky cube, environment maps, decal frames) are not created
  while the map loads, nor the texture `UnitWeapon:DoInstaHit` holds. The Windows runner creates
  them only because the map loader always does; nothing in the sim reads them, and the .scmap is
  read byte for byte as on Windows.
- **Event wait:** the sync loop waits on the driver's "sync data available" event on Android too
  (port/engine/shim's Win32 events), as on Windows.

Status at the end of M3b: arm64 and x86_64 link with zero undefined and zero duplicate symbols;
Windows gives the same checkpoint chains as before.

## Android runner (M3c)

The runner runs on Android with the low-address arena
([port/engine/lowarena](../../port/engine/lowarena/README.md)), the counterpart of x64's
`/LARGEADDRESSAWARE:NO` for this adb-shell runner only. One command builds nothing, pushes the
binaries, the game data and the replays, runs, and pulls the results:

```sh
MSYS_NO_PATHCONV=1 python scripts/port/run_runner_android.py --abi x86_64 --registry-dump \
    --windows-ref buildstage/replays-m3/runs-m3c-F-s2 --replay buildstage/replays-m3/T1-26675870.scfareplay
MSYS_NO_PATHCONV=1 python scripts/port/run_runner_android.py --serial <phone> --replay ... --dry-run
```

Results on the API 36 emulator (`fafre_api36`, x86_64 native and arm64 under the emulator's ARM
translation), 2026-10-07, the final M3c build, with the Windows baseline above (Debug|Win32,
after the `SetAutoMode` fix) in parentheses:

| Replay | ABI | End | Checkpoint chain | First diverging checkpoint | Lua errors load/sim | Wall (load + sim), sim speed | Heap / arena high-water |
|---|---|---|---|---|---|---|---|
| T1 26675870 | x86_64 | exit 0, game over at 467 | `4971bbe58c5586a0` (`7e159db290f576f1`) | beat 100 of 450 | 2/11 (2/11) | 8.1 s (5.7 + 2.2), 212 beats/s | 344 / 396 MB |
| T2 26119449 | x86_64 | exit 0, game over at 4697 | `7eec974b19f98ebc` (`cac943d2cc0593a9`) | beat 100 of 4650 | 2/36 (2/36) | 17.3 s (5.2 + 11.9), 396 beats/s | 310 / 364 MB |
| T3 26679175 | x86_64 | exit 0, game over at 7317 | `1b81ed7b47069327` (`411acb68b7c57709`) | beat 150 of 7300 | 2/546 (2/515) | 32.1 s (4.9 + 27.0), 271 beats/s | 323 / 380 MB |
| R4 26693269 | x86_64 | exit 0, game over at 10551 | `5ea7204b71239f97` (`7ec6527e3f1efcb8`) | beat 100 of 10500 | 2/485 (2/556) | 48.9 s (5.4 + 43.3), 244 beats/s | 334 / 380 MB |
| T1 26675870 | arm64 (translated) | exit 0, game over at 467 | `4971bbe58c5586a0` | beat 100 | 2/11 (2/11) | 32.1 s (20.3 + 11.5), 41 beats/s | 346 / 396 MB |

- **All four replays reach the end** on x86_64, and T1 as arm64: exit 0, the sim's game-over flag
  at the same beat as on Windows, no trap, teardown done, no arena fallbacks. On the final build
  T1 ran three times (once from a fresh device directory) with the same chain; T2-R4 ran once
  each, T3 and R4 again with fills `0x00` and `0xcd`, with the same chains; an earlier build
  gave R4's chain three more times.
- **Beat 0 is equal in every replay**, so rules and blueprints load as on Windows. The checkpoints
  then stay equal up to beat 50 (T3: 100), and T3's beat 50 also matches both players' recorded
  digests, as on Windows. From beat 100 (T3: 150) on they differ. That is the W5 baseline: off x86
  the x87 layer is a non-bit-exact fallback, and that is the expected cause; it is not proven
  beat by beat.
- **x86_64 and arm64 give the same chain** for T1: the fallback math and everything else agree
  between the two Android ABIs (arm64 runs translated here, so this says nothing yet about a
  real ARM core's floating point).
- **Lua errors:** the same kinds as on Windows in every replay (the `GetSource` intel error, the
  destroyed UEF build-effect projectile, `AttackMove`, and the two load errors); equal counts for
  T1 and T2, different counts in T3 and R4 after their sims have diverged.
- **Warnings:** T1 has 1968 against 1962: 8 `[stub]` first-call lines on Android, 2 `CDiskWatch`
  privilege warnings on Windows only. The other replays add the differences of their diverged
  games (the engine's `[MOTDIAG]`/`[STEERDIAG]` probes).
- **Registry:** Core (71), Sim (731) and Unsafe (1) Lua sets equal name for name, the four
  prefetch kinds and four resource factories equal. Missing on Android, all from user-side TUs
  that cannot be linked: 242 User binders, 18 RTypes (17 UI classes of the wx-dependent
  `UiRuntimeTypes.cpp`, and `multimap<int,std::string>` of `CWldSession.cpp`), 8 console commands
  (`SC_*` of `WinMain.cpp`/`ResolutionCommands.cpp`, `dump_Frame*`), the
  `SSessionSaveDataSerializer` helper. Nothing exists on Android that Windows lacks. Details in
  [port/engine/runner/README.md](../../port/engine/runner/README.md#registry-check).
- **Speed:** wall times are not comparable with the Windows table (Debug `/Od` there, `-O0` and an
  emulator here); on the emulator x86_64 runs at 210-400 beats/s, arm64 under translation at about
  a fifth of that.
- **Heap contents change the sim.** With every new block filled with `0xff`
  (`FAF_LOWARENA_FILL=0xff`) T3's chain changes from beat 6150 (`40e37f07c27ba2d5`) and R4's from
  beat 3600 (`9f46db69f387eafe`); `0x00` and `0xcd` change nothing. Most likely that is the
  uninitialised read in `Unit::UpdateBlipsInRange` below: its gate tests bits `0x30`, which `0xff`
  (and the heap check's `0xdd`) set and `0x00`/`0xcd` do not, and the `[BLIPSUPPLY]` probe's scan
  radii change with the fill. The first divergence from Windows does not move.
- **Without the arena** (`FAF_LOWARENA=0`, bionic's allocator, high pointers) T1 stops while the
  rules load: a pointer cut to 32 bits in `AddMappedBlueprintOrdinalBits`
  (`EntityCategoryReflection.cpp:169`, the category universe word). That is M3d's first item.

Two engine bugs were found and fixed on the way (both guarded, the Windows objects unchanged; see
[the runner README](../../port/engine/runner/README.md#status-2026-10-07-m3c)): the HaStar
cluster cache's shared-count release used MSVC vtable slots, so at every teardown it deleted its
control block and then wrote into it (`Cluster.cpp`), and the io library's `WrapFile` reflection callbacks used
the MSVC return convention, so every Lua state wrote 16 bytes through a stray register
(`LuaObject.cpp`).

The phone run does not need adb any more: release 0.4.0's app runs the probe and the replay itself
(next section). With adb, `run_runner_android.py --serial <phone>` (arm64-v8a) still works; its
`--dry-run` prints every command.

## In-app replay test (release 0.4.0)

The launcher's **Replay test** ([android.md](android.md#replay-test)) execs the runner from the APK
(`nativeLibraryDir/libfafrunner.so`) in the app's own process tree: the linker's "system" namespace
instead of "unrestricted", the app's seccomp filter and `untrusted_app` domain, a data root reached
through the lower-case alias `/data/user/0/io.github.m3rt1n99.fafre/files/r`, no `Game.prefs`. Checked
on the API 36 emulator on 2026-10-07 through the app's UI (taps, `uiautomator`, screenshots), with
the data under `/sdcard/Android/data/io.github.m3rt1n99.fafre/files` and the 0.4.0 binaries:

| Replay (input) | APK | Result | Checkpoint chain | Against the adb runs | Wall (load + sim) |
|---|---|---|---|---|---|
| T1 26675870 (vault download, picked in the file picker) | x86_64 | PASS, game over at 467 | `4971bbe58c5586a0` | the reference | 9.4-9.8 s (7.3 + 1.9) |
| the same, run twice | x86_64 | PASS, second run identical | `4971bbe58c5586a0` | equal | 9.2 + 9.3 s |
| the same without `loc_DE.scd`, `mods.scd`, `skins.scd` | x86_64 | PASS | `4971bbe58c5586a0` | equal | 9.2 s |
| the same | arm64-v8a (translated) | PASS, self-test 41/41, capacity 1664 MB | `4971bbe58c5586a0` | equal | 37.2 s (25.6 + 11.1) |
| T2 26119449 (local recording, "Open with" from Files) | x86_64 | PASS, game over at 4697, run twice, identical | `7eec974b19f98ebc` | equal | 18.0 s |
| T3 26679175 (local recording) | x86_64 | PASS, game over at 7317, run twice, identical | `40e37f07c27ba2d5` | **differs** (`1b81ed7b47069327`) | 31.7 s |
| T1 with `FAF_LOWARENA=0` | x86_64 | CRASHED (expected): SIGSEGV, exit 139 | - | the known C2 stop | 5 s |

- **The mechanism works**: exec from `nativeLibraryDir`, the arena under the app's seccomp filter
  (no SIGSYS), `dlopen` of `libfafengine.so` from `/data/app/...` in the "system" namespace, the
  lower-case alias, the zstd decoder, `/replayinfo` and `/convertreplay` from the app.
- **T3 lands in the other heap-content class.** `40e37f07c27ba2d5` is the chain the adb runs gave
  only with `FAF_LOWARENA_FILL=0xff`; in the app it comes without a fill, and stays the same on a
  second run. The engine gets the same bytes (the app's `26679175.3764.scfareplay` and the adb runs'
  `T3-26679175.scfareplay` have the same sha256, as do T2's). The app's runner has a different allocation history (longer paths, another
  environment), so a reused block holds other leftovers, and the uninitialised read in
  `Unit::UpdateBlipsInRange` (known issues) sees them. T1 and T2 do not depend on it; they are the
  parity checks, and the app's reference table holds only T1.
- **`loc_DE.scd`, `mods.scd` and `skins.scd`** (mounted in the reference runs, not part of a phone
  import's required and recommended tiers) do not change T1's chain.
- **Crash capture**: the runner's `[runner] CRASH` report (`libfafengine.so+0xa86ad8`, which
  symbolizes to `AddMappedBlueprintOrdinalBits`, `EntityCategoryReflection.cpp:169`) and, in the
  saved `logcat`, crash_dump's full backtrace: the app may read its own uid's crash lines.
- **Lifetime**: a 64000-beat local replay (SCMP_010) ran 7 minutes with the app in the background
  and the screen off; the app stayed at process state 4 (foreground service), the partial wake lock
  was held, and the runner kept running. Cancel ended it with SIGTERM within a second (exit 143, no
  SIGKILL needed), and the service and the wake lock were released. That replay ran at 20-25
  beats/s, bound by the engine log (the `[MOTDIAG]` diagnostics: 199 MB after 9050 beats), not by
  Android; long replays need minutes and hundreds of MB.
- **GUI Start** with Vulkan and with OpenGL ES (x86_64 APK) and with Vulkan (arm64-v8a APK) still
  reaches the main menu with `extractNativeLibs="true"`.

**After the review fixes** (same day, x86_64 APK with the final runner set `r040b-pkg`: only the
executable changed, the engine and the probe are byte-identical; reference runs of the vault T1 with
`run_runner_android.py --host-prefs none` gave `4971bbe58c5586a0` on x86_64 and arm64 under
translation again, and the table now names the new runner build ids):

| Check | Result |
|---|---|
| T1, self-test + replay, run twice | PASS, chain `4971bbe58c5586a0` matches the reference "measured with these binaries"; second run identical (wall 9.4 s for the first run) |
| T3, killed during the replay step (`su 0 kill -9` of the app's main process at beat 5300; `am kill` does not touch a process with a foreground service) | the runner went with the app (no `libfafrunner.so` left); after reopening, the card showed `INTERRUPTED · the run stopped during step 4/5 (Replay)`, and Save run (zip) exported that run: 10 entries, its partial `4-replay.out` and engine log, no replay |
| `my..game.scfareplay` (no uid) picked in the file picker | imported as `my.game` (`replays/my.game.scfareplay` + `my.game.json`), read by the runner |
| Self-test with a `:game` process whose `status.json` said `running` (set by hand after a real start, to stand for a game left in the background) | not started ("Not started: the game is running ..."), the game process kept; with the real `exited` status the next self-test ended the cached process and passed |

### First run on a real ARM core

On 2026-10-07 the user ran the in-app test on a Samsung Galaxy S22 Ultra (SM-S908B, Exynos 2200,
Android 16, 4 KB pages). It was the 0.4.0 build with engine `cbb70516…` and runner `235eb467…`, the
binaries the reference table names. Its input was the vault download of 26675870.

| Step | Result |
|---|---|
| Arena probe | PASS, 41 of 41 placement checks |
| Arena capacity | 122 blocks of 16 MB (1952 MB) up to the 0x7FFF0000 limit |
| Engine load | `libfafengine.so` at 0x2410000 |
| Replay | PASS: game over at beat 467, exit 0, chain **`4971bbe58c5586a0`**, equal to the reference |
| Second run | identical |
| Registry | the same counts as on the emulator (Core 71, Sim 731, Unsafe 1, User 257; 536 RTypes) |
| Lua errors | 2 load, 11 sim, the same as on Windows |
| Arena use | heap peak 353 MB, high-water 412 MB, no fallbacks |

- **Same chain everywhere.** The first native execution on ARM gives the same checkpoint chain as
  x86_64 and as arm64 under the emulator's translation. A second run on the phone matches too, so
  the result does not depend on the core's weak memory ordering.
- **Speed: load 15.8 s, sim 19.8 s, about 24 beats/s.** The rate falls from 75 to 25 beats/s as the
  game grows. The sim is slower than arm64 under translation on the PC (11.5 s), while loading is
  not (25.6 s there). So scheduling probably holds it back more than the CPU does: the app's process
  group, its cpuset, or timer slack inherited by the child. Release 0.4.1 measures it
  ([below](#speed-telemetry-and-device-probe-release-041)).
- **Build flags.** The binaries are built at `-O0` and write every diagnostic probe line to the
  engine log.

## Speed telemetry and device probe (release 0.4.1)

Release 0.4.1 adds three things to the in-app test ([android.md](android.md#speed-telemetry-and-the-experiments-041)):

- **Speed telemetry.** While a replay step runs, the app samples the runner's threads in `/proc`
  (`RunTelemetry.java`) and shows one **Speed:** line.
- **Two experiments**, both Advanced options:
  - an `-O2` build of the runner and engine (`build_runner.py --opt O2`, the other flags unchanged);
  - timer slack 1 ns plus an affinity without the little cores. The runner applies them itself
    (`port/engine/runner/RunnerSched.cpp`, `FAF_RUNNER_TIMERSLACK_NS`, `FAF_RUNNER_AFFINITY`) and
    reports them in its `[runner] sched {...}` line.
- **A device probe** for the graphics plan (`port/deviceprobe`, `libfafdeviceprobe.so`).

The reference table names all four engine/runner pairs (`-O0` and `-O2`, arm64-v8a and x86_64). It
holds the same chain for each of them, `4971bbe58c5586a0`; the adb reference runs are described in
the [runner README](../../port/engine/runner/README.md).

**Checked through the app's UI** on the API 36 emulator on 2026-10-07, with the first release
candidate APKs (versionCode 3449, packaged sets `r041-pkg` and `r041-pkg-O2`; the second build is
[below](#second-041-build-runner-_exit-affinity-hold-probe-teardown)). The PC was compiling another
build at the same time, so the load times are higher than 0.4.0's; compare times only within this
table.

| Replay, build, options | APK | Result | Chain | Load / sim, beats/s | Speed line |
|---|---|---|---|---|---|
| T1, `-O0`, run twice | x86_64 | PASS, second run identical | `4971bbe58c5586a0`, "measured with these binaries" | 9.0 / 2.8 s, 169 | `waiting` (see below) |
| T1, `-O2`, run twice | x86_64 | PASS, identical | `4971bbe58c5586a0`, "measured with these -O2 binaries" | 4.2 / 1.4 s, 332 | `waiting` |
| T1, `-O2` + speed experiment, run twice | x86_64 | PASS, identical | `4971bbe58c5586a0` | 3.7 / 1.4 s, 337 | timer slack 1 ns; affinity `fast` "not changed" (all CPUs the same class) |
| T1, `-O0` + speed experiment, *only the biggest cores* | x86_64 | PASS | `4971bbe58c5586a0` | 8.6 / 2.7 s, 173 | timer slack 1 ns; affinity `big` not changed |
| T2 26119449 (local), `-O0` | x86_64 | PASS, game over at 4697 | `7eec974b19f98ebc` (0.4.0's) | 5.8 / 13.7 s, 342 | `cpu-bound`: "sim thread CPU-bound on equal cores (ran 98% of the sim time)" |
| T1, `-O0`, run twice | arm64-v8a (translated) | PASS, identical; self-test 41/41, 1664 MB | `4971bbe58c5586a0` | 27.5 / 14.7 s, 32 | no telemetry (see below) |
| T1, `-O2`, run twice | arm64-v8a (translated) | PASS, identical | `4971bbe58c5586a0` | 10.8 / 2.6 s, 181 | no telemetry |
| T1, `-O2` + speed experiment | arm64-v8a (translated) | PASS | `4971bbe58c5586a0` | 11.4 / 2.5 s, 184 | no telemetry; timer slack 1 ns |
| Device probe | x86_64 | PASS: all five sections `ok`, both patterns exact (0 of 65536 pixels off) | - | 0.6 s | glslang 70 ms first / 41 ms warm |
| Device probe | arm64-v8a (translated) | PASS, the same | - | 2.2 s | glslang 461 / 185 ms |

The emulator's GPU is SwiftShader: Vulkan device 1.2.0 (instance 1.4.0) with BC1-3, ETC2 and ASTC
sampled images, D32 but no D24S8, fillModeNonSolid, anisotropy, depth clamp, independent blend, 16384
max 2D, timestamps; GLES 3.0 through the emulator's translator with ETC2 and ASTC, no S3TC, no
clip_control, color_buffer_float. *Save all runs (zip)* wrote all 24 runs of the emulator in one
zip (214 entries, both probe PNGs, no replay file, the 192 MB engine log of an old run cut to 64 MB).

What the runs show:

- **`-O2` keeps the chain.** On both ABIs, with and without the speed experiment, the `-O2` build
  gives the same chain and game-over beat as `-O0`, and its second runs are identical. It roughly
  halves the load time and, under ARM translation, cuts the sim from 14.7 s to 2.6 s.
- **The verdict judges the sim thread when the sim phase lasts 3 s or more.** On T2 (13.7 s of sim)
  the busiest thread in the sim phase was not the runner's main thread but the engine's sim thread,
  which ran 98 % of the time. T1 on x86_64 simulates in under 3 s. The verdict then falls back to the
  whole run, and the busiest thread is the main thread during the engine's shutdown (next point). The
  resulting "mostly waiting" says nothing. On a phone, T1's sim phase lasts about 20 s at `-O0`.
  The second build leaves the shutdown out of the window.
- **The engine takes seconds to shut down.** The runner process outlives its final `RESULT` line:

  | Where | Build | Process lifetime | Engine wall time | Difference |
  |---|---|---|---|---|
  | emulator x86_64 | `-O0` | 19.3 s | 12.0 s | 7.3 s |
  | emulator x86_64 | `-O2` | 9.3 s | 5.8 s | 3.5 s |
  | emulator, arm64 translated | `-O0` | 57.4 s | 42.7 s | 14.7 s |
  | emulator, arm64 translated | `-O2` | 17.1 s | 13.8 s | 3.3 s |
  | S22 Ultra, 0.4.0 | `-O0` | 46.6 s | 36.2 s | 10.4 s |

  Two `debuggerd -b` stacks taken in that phase are the same:
  `exit` → `__cxa_finalize` → `FWaitHandleSet::~FWaitHandleSet` → `CVFSImpl::~CVFSImpl` →
  `SVFSMountPoint::~SVFSMountPoint` → `FWaitHandleSet::RemoveEntry`. `RemoveEntry` restarts a
  linear `FindZipEntryByHandle` scan of the archive index (38,237 entries) after every erase, so the
  teardown is quadratic. It runs only at exit and changes neither the load and sim times nor the
  chain, but the tester waits for it. The second build's runner ends with `_exit` once its summary
  is written and skips it.
- **No Speed line under ARM translation.** The emulator starts an arm64 executable through
  binfmt_misc, so the runner's `cmdline` begins with
  `/system/bin/ndk_translation_program_runner_binfmt_misc_arm64`. The sampler looks for a child
  whose first argument is the runner and does not find it. On an arm64 phone the first argument is
  the runner. The runner's own `[runner] sched` line is there either way. The second build's
  sampler takes argv[1] in that case.
- **The speed experiment cannot show anything on the emulator.** All CPUs report the same
  `cpuinfo_max_freq`, so `fast` and `big` leave the allowed CPUs unchanged. The timer slack does
  change, from 50 µs to 1 ns, and the chain stays the same.
- **On a phone, FAF would have undone the experiment's affinity** (found in review, not on the
  emulator). FAF's `init_faf.lua` calls `SetProcessAffinityMask(systemMask - 3)` on a device with six
  or more CPUs ("every CPU but 0 and 1"), and the shim applies it to every thread
  (`port/engine/shim/faf_win_kernel.h`). The 0.4.0 phone log shows it: affinity set to 252, CPUs 2-7.
  It runs while the engine loads, after the runner's constructor set 4-7, so the first build's
  experiment would have run on 2-7 like every other run, and its Speed line would have named the
  constructor's 4-7.

### Second 0.4.1 build (runner `_exit`, affinity hold, probe teardown)

Four native changes and the app's follow-ups, after the review of the first build. The engines did
not change (the `-O0` engine is still 0.4.0's); the runners and the device probe did:

- **The runner's affinity holds.** `RunnerSched.cpp` exports its own `sched_setaffinity`
  (`build_runner.py`'s `EXE_EXPORTS`), which `libfafengine.so` binds to ahead of libc's, as it does
  for `malloc`. When `FAF_RUNNER_AFFINITY` asked for something the device can do, the runner owns the
  mask: an engine request still returns success, but the thread gets the runner's mask. Otherwise the
  request goes to the kernel unchanged, as in the reference runs. The `[runner] sched` line gains
  `affinity.in_force`, `set_by` (`runner` or `inherited`) and `owner` (`runner` or `engine`), and each
  distinct engine request prints `[runner] affinity {"tid":..,"request":"2-7","in_force":"4-7","set_by":"runner",..}`.
- **Fast exit.** `RunnerExit.cpp`: after `faf_headless_main` returns and the arena's report is out,
  `main` flushes every stdio stream and calls `_exit(code)`, so the engine's static destructors (the
  quadratic VFS teardown above) never run. `FAF_RUNNER_EXIT=full` keeps the old way. The last line is
  `[runner] exit <code>: _exit after flushing, ...`.
- **The probe reports before it tears down.** A section's child sends its result to the parent and
  only then destroys its Vulkan device and instance or terminates EGL
  (`port/deviceprobe/README.md`). A crash or hang in that teardown is reported
  (`process.teardown`, `,teardown=crashed` in the RESULT line) and costs nothing the section found.
- **The app:** the Speed line takes `cpus` from the allowed CPUs the busiest thread had during the
  judged window, with who set them; the run window ends at the RESULT line; the busiest thread's
  effective clock and migrations per second are in the line; the sampler finds a translated runner;
  the device probe runs without game data and saves logcat when a section failed; the engine-load
  line names `libfafengine_o2.so` for `-O2`; the Licenses screen carries DiligentCore's and its
  third-party libraries' license texts ([android.md](android.md)).

**References re-measured** with the packaged sets `buildstage/runner/r041b-pkg/<abi>` and
`r041b-pkg-O2/<abi>` (vault T1, `--host-prefs none`, over adb on 2026-10-08). Every run gives chain
`4971bbe58c5586a0`, game over at beat 467, the first difference from Windows at beat 100, and the
same registry counts:

| ABI | Build | Runner build id | Engine build id | Load / sim |
|---|---|---|---|---|
| x86_64 | `-O0` | `858ee967d326…` | `9f688a0f7c6c…` (0.4.0's) | 7.9 / 2.2 s |
| x86_64 | `-O2` | `f051e4534494…` | `485f666a914b…` | 3.5 / 1.1 s |
| arm64-v8a (translated) | `-O0` | `d9922c82a7d3…` | `cbb70516dc5a…` (0.4.0's) | 24.6 / 13.0 s |
| arm64-v8a (translated) | `-O2` | `b102c84b1658…` | `45d6cdd0434a…` | 10.9 / 2.5 s |

T2 (26119449, 4697 beats) gives `7eec974b19f98ebc` with both x86_64 sets (`-O0` 6.3 / 13.3 s, `-O2`
2.9 / 6.8 s), as in 0.4.0.

**Fast exit, checked.**
- Exit codes are the same with and without `FAF_RUNNER_EXIT=full`: replay 0, no replay (the
  engine-load step) 1, a missing replay file 1, a missing init script 1, arena probe 0, arena
  capacity 0.
- Output is complete: the engine log (6,984 lines, 763,807 bytes), the summary JSON and stdout up to
  `[runner] exit` are the same in both modes. Only heap addresses in two Lua error messages differ,
  as between any two runs. The full mode adds the destructors' `[stub]` lines after it.
- In the app, each replay step now ends 0.0-0.1 s after the engine's wall time on x86_64 (0.2-0.3 s
  under translation), where the first build's steps lived 7.3 s (x86_64) and 14.7 s (translated)
  longer. Over adb the destructors stayed short (0.1-0.3 s), even with a copy of the app's data root
  (symlinks to the same archives and SCFA folder, home and output on shared storage), so the saving
  was measured in the app only. Why the app's runner pays 7 s in `RemoveEntry` and the shell's does
  not was not found.

**Affinity hold, checked over adb** (`-O2` runner, T1, every thread's `Cpus_allowed_list` sampled
from `/proc/<pid>/task/*/status` every 0.2 s). The emulator has four CPUs, so FAF's branch for six
or more never runs; a copy of `init_faf.lua` with the threshold lowered from 63 to 15 asks for CPUs
2-3 (mask 12) instead:

| `FAF_RUNNER_AFFINITY` | init script | Threads seen | `[runner] affinity` | Chain |
|---|---|---|---|---|
| unset | copy (asks 2-3) | main thread 0-3, then every thread 2-3 | request 2-3, in force 2-3, `set_by` engine | `4971bbe58c5586a0` |
| `0-1` | copy | every thread 0-1 in every sample | request 2-3, in force 0-1, `set_by` runner | `4971bbe58c5586a0` |
| `all` | copy | every thread 0-3 | request 2-3, in force 0-3, `set_by` runner | `4971bbe58c5586a0` |
| `fast` + timer slack 1 | copy | every thread 0-3 (one CPU class: the runner owns all four) | request 2-3, in force 0-3, `set_by` runner | `4971bbe58c5586a0` |
| `0,2` | FAF's own | every thread 0,2 | request 0,2 (the process mask), "asked for the same CPUs" | `4971bbe58c5586a0` |
| `0-1`, arm64 `-O2` translated | copy | every thread 0-1 | request 2-3, in force 0-1, `set_by` runner | `4971bbe58c5586a0` |

**Checked through the app's UI** on 2026-10-08 with the second release candidates (versionCode
3450, pre-commit):

| Run | APK | Result | Chain | Load / sim, beats/s | Speed line |
|---|---|---|---|---|---|
| Self-test only | x86_64 | PASS, 41/41, 1952 MB | - | - | - |
| Device probe | x86_64 | PASS, all five sections `ok`, both patterns exact, every section's teardown `ok` after its report; no data root prepared | - | 0.7 s | glslang 51 / 33 ms |
| T1, `-O0`, run twice | x86_64 | PASS, identical, "measured with these binaries" | `4971bbe58c5586a0` | 6.5 / 2.2 s, 212 | `waiting` (53 % of the run, window up to the RESULT line), `cpus 0-3` |
| T1, `-O2`, run twice | x86_64 | PASS, identical, "measured with these -O2 binaries"; "libfafengine_o2.so loaded at 0x2410000" | `4971bbe58c5586a0` | 2.7 / 1.0 s, 452 | `waiting`, `cpus 0-3` |
| T1, `-O2` + speed experiment, run twice (two such runs) | x86_64 | PASS, identical | `4971bbe58c5586a0` | 2.6 / 1.0 s, 488 | timer slack 1 ns, `cpus 0-3 (speed experiment)`; the app's runner threads read over adb: all 0-3 |
| T1, self-test + `-O2`, run twice | arm64-v8a (translated) | PASS, identical; self-test 41/41, 1648 MB | `4971bbe58c5586a0` | 9.8 / 2.5 s, 186 | present under translation: `waiting`, `cpus 0-3` |

*Save all runs (zip)* wrote 46 runs, 443 entries, both probe PNGs, no replay or data file. The probe's
failure paths over adb: `FAF_PROBE_TEST_FAULT=teardown-crash:vulkan_render` keeps the section's
"pattern exact" and reports `teardown=crashed` (exit 0); `teardown-hang:gles` is killed after 20 s
(`teardown=timeout`, exit 0); `crash:vulkan_render` (before the result) gives `crashed`, exit 1.

## Known issues

- **Sim Lua errors that do not stop the sim:**
  - `blip:GetSource` is nil in `OnIntelChange` (FAF `jammermanagerbraincomponent.lua:93`): 11-556 per replay.
  - `GetCommandQueue` is nil in the `AttackMove` sim callback.
  - "Game object has been destroyed" for the UEF build-effect projectile (`effectutilitiesuef.lua`).

  The methods have registered binders, so the engine probably hands Lua the wrong objects.
  These go to M4.
- **Uninitialised read in `Unit::UpdateBlipsInRange`.** The guard-scan gate (`Unit.cpp`, the
  `mEntIds.InlineStorage()[1] & 0x30` test) reads a slot that no constructor writes, so the scan
  radius depends on leftover heap contents. Pointer-normalised engine logs show it differing
  between identical runs (radius 31.5 vs 23.1). No checkpoint changed in these replays on
  Windows, but it can change target acquisition, and the heap differs between x86 and arm64. On
  Android (M3c) it does change checkpoints: filling new heap blocks with `0xff` changes T3 from
  beat 6150 and R4 from beat 3600, and in the app (0.4.0) T3 gives that `0xff` chain without any
  fill. It has to be settled against the binary before x86 and arm64 digests are compared.
- **No GUI reference yet.** The runner is deterministic, but its digests have not been compared
  with a GUI `/replay` of the same file. Pointer-ordered containers in the recovered code would
  make the two diverge. The check: run `main.exe /replay <file> /init init_faf.lua /nosound`; the
  engine logs `Checksum for beat N mismatched: <sim digest>` at every checkpoint, which can be
  compared with the runner's.
- The runner reads only `v1.50.3764` headers, because `VCR_SetupReplaySession` is unchanged.
