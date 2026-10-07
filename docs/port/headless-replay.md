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

## Baseline (2026-10-06, Debug|Win32, FAF 3839 data + Steam SCFA)

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

Status 2026-10-07: arm64 and x86_64 link with zero undefined and zero duplicate symbols; Windows
gives the same checkpoint chains as before. The Android runner has not been run yet: that is M3c
(WSL, emulator, then the phone, with the low-address arena of the roadmap's W1.3).

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
  between identical runs (radius 31.5 vs 23.1). No checkpoint changed in these replays, but it
  can change target acquisition, and the heap differs between x86 and arm64. It has to be settled
  against the binary before x86 and arm64 digests are compared.
- **No GUI reference yet.** The runner is deterministic, but its digests have not been compared
  with a GUI `/replay` of the same file. Pointer-ordered containers in the recovered code would
  make the two diverge. The check: run `main.exe /replay <file> /init init_faf.lua /nosound`; the
  engine logs `Checksum for beat N mismatched: <sim digest>` at every checkpoint, which can be
  compared with the runner's.
- The runner reads only `v1.50.3764` headers, because `VCR_SetupReplaySession` is unchanged.
