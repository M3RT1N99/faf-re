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
