# Devices that ran the replay test

One row per device and app version, from the run zips testers send back (how to test:
[phone-testing.md](phone-testing.md); how a zip is read: [android.md](android.md#reading-the-result)).

**What the columns mean:**
- **Self-test:** arena probe / arena capacity / engine load.
- **Replay:** the reference replay [26675870](https://replay.faforever.com/26675870) unless noted.
- **Chain:** compared with `port/android/assets/replay_refs.json` for the same engine and runner
  build ids. The reference chain is `4971bbe58c5586a0` for 0.4.0 and 0.4.1, and for both the `-O0`
  and the `-O2` build of 0.4.1.
- **Load / sim:** wall-clock seconds as the runner reports them (`load_seconds`, `sim_seconds`). The
  normal build is `-O0`; since 0.4.1 the APK also has an `-O2` build (*Optimised engine*). Both write
  every diagnostic line to the engine log. Up to the first 0.4.1 build the runner process lived
  7-15 s (emulator) or about 10 s (S22 Ultra) longer than this while the engine's static destructors
  ran; since the second 0.4.1 build (versionCode 3450) it ends with `_exit` right after its result
  (see [headless-replay.md](headless-replay.md#speed-telemetry-and-device-probe-release-041)).
- **Allowed CPUs:** on a device with six or more CPUs FAF's `init_faf.lua` restricts the engine to
  every CPU but 0 and 1 (`SetProcessAffinityMask(systemMask - 3)`, as on Windows): 2-7 on an 8-core
  phone. The Speed line says `cpus 2-7 (FAF's init_faf.lua)` then. With the speed experiment the
  runner keeps its own mask against that request (`cpus 4-7 (speed experiment; FAF asked for 2-7)`
  on the S22 Ultra).

Testers are not named unless they ask to be.

| Date | Device (SoC) | Android (API), page | App (versionCode) | Self-test | Replay | Chain | 2nd run | Load / sim, beats/s | Notes |
|---|---|---|---|---|---|---|---|---|---|
| 2026-10-07 | Samsung Galaxy S22 Ultra SM-S908B (Exynos 2200, 8 cores, 11 GB) | 16 (36), 4 KB | 0.4.0 (3446, pre-release build of the published binaries) | PASS (41/41, 1952 MB, engine at 0x2410000) | PASS, game over at 467 | `4971bbe58c5586a0`, matches | identical | 15.8 / 19.8 s, 24 | First run on a real ARM core. The sim is slower than arm64 under translation on the PC; 0.4.1 measures why. The replay step took 46.6 s, 10.4 s more than the engine's wall time (shutdown). FAF's `init_faf.lua` set the engine's affinity to CPUs 2-7 (engine log: affinity set to 252). `mods.scd`/`skins.scd` not imported. |
| 2026-10-07 | Android emulator `sdk_gphone64_x86_64` (PC, Intel Core Ultra 7 265KF) | 16 (36), 4 KB | 0.4.0 x86_64 APK | PASS (41/41, 1952 MB) | PASS, game over at 467 | `4971bbe58c5586a0` (the reference) | identical | 7.3 / 1.9 s | Reference run. |
| 2026-10-07 | the same emulator, arm64 under ARM translation | 16 (36), 4 KB | 0.4.0 arm64 APK | PASS (41/41, 1648-1664 MB) | PASS, game over at 467 | `4971bbe58c5586a0` (the reference) | identical | 25.6 / 11.1 s | Reference run. |
| 2026-10-07 | Android emulator `sdk_gphone64_x86_64` (4 vCPUs, uniform) | 16 (36), 4 KB | 0.4.1 x86_64 APK (3449, pre-commit, first build) | PASS (41/41, 1952 MB) | PASS, game over at 467 (`-O0` and `-O2`) | `4971bbe58c5586a0`, matches, for both builds | identical (both builds) | `-O0` 9.0 / 2.8 s, 169; `-O2` 4.2 / 1.4 s, 332 | Release check of the first build. The PC was compiling another build at the same time, so load times are higher than 0.4.0's. Speed experiment: timer slack 50 µs → 1 ns, affinity unchanged (all CPUs the same class), chain unchanged. Each replay step lived 7.3 s past its RESULT line (shutdown). |
| 2026-10-07 | the same emulator, arm64 under ARM translation | 16 (36), 4 KB | 0.4.1 arm64 APK (3449, pre-commit, first build) | PASS (41/41, 1664 MB) | PASS, game over at 467 (`-O0` and `-O2`) | `4971bbe58c5586a0`, matches, for both builds | identical (both builds) | `-O0` 27.5 / 14.7 s, 32; `-O2` 10.8 / 2.6 s, 181 | Release check of the first build. No Speed line under translation (the sampler did not find the translated runner). |
| 2026-10-08 | Android emulator `sdk_gphone64_x86_64` (4 vCPUs, uniform) | 16 (36), 4 KB | 0.4.1 x86_64 APK (3450, pre-commit, second build: runner `_exit`, affinity hold) | PASS (41/41, 1952 MB) | PASS, game over at 467 (`-O0`, `-O2`, `-O2` + speed experiment) | `4971bbe58c5586a0`, matches, for every build | identical (each) | `-O0` 6.5 / 2.2 s, 212; `-O2` 2.7 / 1.0 s, 452; `-O2` + speed 2.6 / 1.0 s, 488 | Release check. Each replay step ended 0.0-0.1 s after the engine's wall time (was 7.3 s). Timer slack 1 ns with the experiment; all threads stayed on 0-3 (sampled from `/proc` over adb). |
| 2026-10-08 | the same emulator, arm64 under ARM translation | 16 (36), 4 KB | 0.4.1 arm64 APK (3450, pre-commit, second build) | PASS (41/41, 1648 MB) | PASS, game over at 467 (`-O2`) | `4971bbe58c5586a0`, matches | identical | `-O2` 9.8 / 2.5 s, 186 | Release check (`-O0` only over adb: 24.6 / 13.0 s). Speed line present under translation. Steps ended 0.2-0.3 s after the engine's wall time. |
| 2026-10-08 | Samsung Galaxy S22 Ultra SM-S908B (Exynos 2200: 1 X2, 3 A710, 4 A510; 11 GB) | 16 (36), 4 KB | 0.4.1 (3452, the published release) | PASS (41/41, 1952 MB, engine at 0x2410000, both builds) | PASS, game over at 467 (`-O0`, `-O2`, `-O2` + speed experiment) | `4971bbe58c5586a0`, matches, for every build | identical (each) | `-O0` 23.5 / 26.8 s, 17; `-O2` 6.9 / 2.7 s, 176 (second run 6.1 / 2.3 s, 200); `-O2` + speed 7.6 / 2.5 s, 186 | **The `-O2` build gives the same chain on a real ARM core, and its sim is 10x faster than `-O0`'s.** The CPUs were frequency-capped during all runs: `scaling_max_freq` fell to 1.54-1.92 GHz on the X2 and A710s (2.8 / 2.5 GHz rated) and 1.34 GHz on the A510s, so the `-O0` run here is slower than 0.4.0's. Each replay step ended 0.1 s after the engine's wall time (`_exit`; 0.4.0: 10.4 s). Registry as on the emulator (Core 71, Sim 731, Unsafe 1, User 257; 536 RTypes; 4 prefetch kinds). `mods.scd`/`skins.scd` not imported. |

## Speed telemetry (0.4.1 and later)

From each replay run's **Speed** line (`result.json` `speed`, details in `steps[].telemetry` and
`meta.json` `telemetry`). Phones fill this table; the emulator rows only show the format.

| Date | Device | Build, options | Load / sim, beats/s | Speed verdict | Timer slack | cpuset | Allowed CPUs | Notes |
|---|---|---|---|---|---|---|---|---|
| 2026-10-07 | emulator x86_64 (first build, 3449) | `-O0` | 9.0 / 2.8 s, 169 | `waiting` (busiest thread 36 % of the run) | 50 µs | `/top-app` | 0-3 | The sim phase lasted under 3 s, so the whole run was judged, and the busiest thread was the main thread shutting the engine down. Not meaningful; fixed in the second build. |
| 2026-10-07 | emulator x86_64 (first build) | `-O0`, replay 26119449 (local, 4697 beats) | 5.8 / 13.7 s, 342 | `cpu-bound`: sim thread on equal cores, ran 98 % of the sim time | 50 µs | `/top-app` | 0-3 | A sim phase over 3 s: the verdict names the sim thread, not the main thread. |
| 2026-10-08 | emulator x86_64 (second build, 3450) | `-O0` | 6.5 / 2.2 s, 212 | `waiting` (busiest thread 53 % of the run) | 50 µs | `/top-app` | 0-3 | Sim under 3 s: the whole run up to the RESULT line is judged; the shutdown is no longer in the window. Effective clock "4.97 GHz": the emulator's `scaling_cur_freq` is not a real clock. |
| 2026-10-08 | emulator x86_64 (second build) | `-O2`, speed experiment (`fast`) | 2.6 / 1.0 s, 488 | `waiting` (50 %) | 1 ns | `/top-app` | 0-3 (speed experiment) | One CPU class: the runner keeps all four CPUs as its own mask. Chain unchanged. |
| 2026-10-08 | emulator, arm64 under translation (second build) | `-O2` | 9.8 / 2.5 s, 186 | `waiting` (48 %) | 50 µs | `/top-app` | 0-3 | The first Speed line under translation (the sampler finds the runner by argv[1]). |
| 2026-10-08 | Galaxy S22 Ultra (0.4.1, 3452) | `-O0` | 23.5 / 26.8 s, 17 | `waiting`: sim thread ran 46 % of the sim time; on the X2 62 %, A710 36 %, A510 2 % (approximate, 8.9 migrations/s) | 50 µs | `/top-app` | 2-7 requested by FAF (engine log); the Speed line shows 0-7 | Per-second samples over the 25.6 s of play: the sim thread ran 11.5 s, was runnable but not running only 0.1 s, and slept 394 times (about 36 ms each). It is not starved: it waits on something. The X2's clock moved between 576 MHz and 1.73 GHz, effective 1.33 GHz. The `cpus 0-7` on the Speed line contradicts the engine's own `request 2-7, in_force 2-7` line: a sampler or reporting gap to check. |
| 2026-10-08 | Galaxy S22 Ultra | `-O2` | 6.9 / 2.7 s, 176 | `waiting` (window `run`: the sim phase was under 3 s, so loading is in it) | 50 µs | `/top-app` | 2-7 (FAF's init_faf.lua) | In the three playing samples the sim thread was on the X2 and ran 96 % of the time (1.94 of 2.03 s, 31 ms runnable): during play it is CPU-bound. X2 clock 1.92 GHz in play, capped there; effective 1.86 GHz (66 % of max). |
| 2026-10-08 | Galaxy S22 Ultra | `-O2`, speed experiment (`fast`) | 7.6 / 2.5 s, 186 | `waiting` (window `run`) | 1 ns | `/top-app` | 4-7 (speed experiment; FAF asked for 2-7) | The hold worked (`set_by runner`). The sim was within 7 % of the plain `-O2` run: the scheduler already put the sim thread on the X2. The CPUs were capped at 1.54 GHz this time, effective 1.54 GHz (55 %). |

## Device probe (0.4.1 and later)

From `deviceprobe.json` (`summary`, and `sections.vulkan.devices[].answers` for the Vulkan
features).

| Date | Device | GPU, driver | Vulkan: BC1-3 / ETC2 / ASTC, D24S8 / D32 | Vulkan render | GLES: version, S3TC / ETC2 / ASTC, clip_control, color_buffer_float | GLES render | glslang (placeholder shaders), first / warm |
|---|---|---|---|---|---|---|---|
| 2026-10-07 | emulator x86_64 (first build) | SwiftShader (Vulkan 1.2.0 device, instance 1.4.0); GLES through the emulator's translator | yes / yes / yes, no / yes | exact | 3.0, no / yes / yes, no, yes | exact | 70 / 41 ms |
| 2026-10-07 | emulator, arm64 under translation (first build) | the same | the same | exact | the same | exact | 461 / 185 ms |
| 2026-10-08 | emulator x86_64 (second build: drivers closed after the report) | the same | the same | exact | the same | exact | 51 / 33 ms; every section's teardown ok |
| 2026-10-08 | Galaxy S22 Ultra (0.4.1, 3452) | Samsung Xclipse 920 (AMD RDNA2), Vulkan 1.3.279, Samsung driver 24.0.545; GLES is ANGLE on that Vulkan driver (24.1.293) | yes / yes / yes, **no** / yes (also D32S8) | exact (device 2.2 ms, first pipeline 18.6 ms, cached 0.1 ms) | 3.2 (ANGLE), yes / yes / yes, yes, yes | exact | 123 / 93 ms (about 2.5x the x86_64 emulator); every section's teardown ok |

What the S22 Ultra's probe means for the graphics plan ([renderer.md](renderer.md)):
- **Textures.** The format properties list BC1-3 as sampleable, but the driver does not offer the
  `textureCompressionBC` feature, so Vulkan may not use BC formats (found by the 0.5.0 menu replay; the probe
  first read only the format properties). FA's DXT textures are therefore decoded on the CPU; transcoding
  them to ETC2 or ASTC, which the GPU has, would save memory. Mali GPUs need the same path.
- **Depth.** D24S8 is not an attachment format here. A depth-stencil request has to map to
  D32_FLOAT_S8_UINT (or D32 without stencil).
- **GLES.** OpenGL ES is Google's ANGLE running on the same Vulkan driver. A GLES backend would add
  a translation layer and no second driver, so Vulkan is the path on this phone.
- **Process model.** `VK_ANDROID_external_memory_android_hardware_buffer` import works. An engine in
  its own process can render into an AHardwareBuffer that the app shows, which makes the
  out-of-process option of the plan's step 7 viable.
- **Shader compilation.** glslang is about 2.5x slower than on the PC, and the first pipeline
  creation costs 18.6 ms. With FA's 175 mesh entry points this needs a persistent SPIR-V and
  pipeline cache.

## Menu replay (0.5.0 and later)

From each menu replay run's `galplay.json` (`device`, `timings`, `caches`, `frameHashes`) and, for frames
that differ, `gfx_capture.py parity` of the zip's BMPs against the PC's Diligent-Vulkan replay of the same
trace ([android.md](android.md#menu-replay)). The trace is 0.5.0's `menu.galtrace` (900 frames, nine
read-back frames, PC references `reference_frames.diligent:vk`).

| Date | Device | GPU, precision bits (sub-pixel / sub-texel / mipmap) | Depth, BC | Run | Frames vs the PC | Parity rule | 900 frames in, avg frame | Shaders: first / second launch |
|---|---|---|---|---|---|---|---|---|
| 2026-10-08 | emulator x86_64 (0.5.0 x86_64 APK, 3455, pre-commit) | SwiftShader (LLVM 10), 4 / 4 / 4 | D32S8 (no D24S8), BC sampled | recorded pace | 0 of 9 equal | **fail**: 34,050-135,384 px differ, max \|delta\| 10 | 30.5 s, 33.6 ms (work 8.6 ms) | 4 compiled in 58 ms / 0 compiled, 4 cached (0.4 ms) |
| 2026-10-08 | the same | the same | BC decoded on the CPU (forced) | recorded pace | 0 of 9 equal | **fail**: 25,924-108,123 px, max \|delta\| 10 | 30.5 s | cached |
| 2026-10-08 | the same emulator, arm64 under ARM translation (0.5.0 arm64 APK, 3455, pre-commit) | the same | D32S8, BC sampled | recorded pace; as fast as possible | the x86_64 frames byte for byte | fail (as x86_64) | 30.9 s, 34.0 ms (work 10.3 ms); fast 9.0 s | 4 compiled in 619 ms (without the cache) / 4 cached (20 ms) |
| 2026-10-08 | **Galaxy S22 Ultra** SM-S908B, Android 16 (0.5.0, 3456, the published release) | Samsung Xclipse 920, 8 / 8 / 8 | D32S8 (no D24S8); BC decoded on the CPU: the driver lists BC1-3 as sampleable formats but does not offer the `textureCompressionBC` feature, so the backend may not use them | recorded pace, swap chain 720x1544 RGBA8, pre-rotated 90° | 0 of 9 equal | **fail by the rule, but close**: 12,072-49,757 px differ (1.3-5.4 %), max \|delta\| **2** (98-99 % of them by 1), alpha identical | 30.2 s, 33.1 ms; replay work 5.8 ms (p95 7.8 ms); first frame after 256 ms | 0 compiled, 4 cached (1 ms; a second launch), pipeline cache warm |

The S22 Ultra's differences are filtering rounding, not a backend error. 99.9 % of the differing pixels
lie inside the PC's own 3x3 neighbourhood (+-1). All of them are within 1/16 of the local contrast + 1, and
only 3-15 per frame fall on flat areas (by 1). So the CPU BC decoder matches the PC's GPU decode, and the
rest is how two GPU generations round bilinear weights (the same kind of difference the PC's own D3D11
driver shows against D3D9 in the lobby). Exact frames across different GPUs would need filtering done in
the shader. Like the emulator, the phone ends one render pass per frame for the present
(`endsInDrawByTargets` 900; Windows 0).

The emulator's frames are deterministic (identical between runs, ABIs and with HOME in between) but not the
PC's; [renderer.md](renderer.md#the-emulators-frames-swiftshader) explains what is known.
