# port/deviceprobe — the device probe for the graphics plan

`libfafdeviceprobe.so` (release 0.4.1) answers the questions the graphics plan
([docs/port/renderer.md](../../docs/port/renderer.md), M7's "phone probe") asks of a tester's phone,
before any FA renderer exists there:

- **Vulkan:** instance and device versions, the driver's name and version, the features and formats
  the D3D9 → Vulkan mapping depends on (BC1-3 / ETC2 / ASTC sampled images, D24S8 / D32 depth,
  `fillModeNonSolid`, `samplerAnisotropy`, `depthClamp`, `independentBlend`, the largest 2D texture,
  timestamps), limits, queues, memory heaps and every device extension.
- **Vulkan in a child process:** whether a process the app exec's (as it execs the replay runner)
  can create a Vulkan device and render offscreen. It draws a test pattern into a 256×256 RGBA8
  image, reads it back, checks every pixel and writes `deviceprobe-vulkan.png`. This is the first
  half of the critic's question for M7, in-process versus out-of-process engine; sharing the image
  with the app through an AHardwareBuffer is not tested yet (the report says whether the driver has
  `VK_ANDROID_external_memory_android_hardware_buffer`).
- **OpenGL ES / EGL:** versions, renderer, every extension, the compressed formats, limits, and the
  extensions the GLES path needs or misses (S3TC, ASTC, `EXT_clip_control`,
  `EXT_color_buffer_float`, border clamp, anisotropy, polygon mode, program binaries), plus the same
  pattern drawn into an FBO and read back into `deviceprobe-gles.png`.
- **glslang timing, a placeholder:** how long HLSL → SPIR-V takes on the phone the way Diligent does it
  at run time (`GLSLangUtils.cpp` HLSLtoSPIRV: glslang's HLSL front end for Vulkan 1.0 / SPIR-V 1.0,
  then SPIRV-Tools' legalization and performance passes). **The shader set is not FA's:** the M1
  splash shaders (`port/android/src/Renderer.cpp`) and two larger synthetic shaders written for the
  probe (a skinned, lit mesh and an eight-layer terrain with decals). Real FA shaders need M6b's HLSL
  emitter; their timing comes in a later release. No game content is in the probe.

It is an executable with a `lib*.so` name, so the APK installer extracts it into `nativeLibraryDir`
next to `libfafrunner.so`, and the app runs it the same way. Vulkan, EGL and GLES are opened with
`dlopen` (`libvulkan.so`, `libEGL.so`, `libGLESv3.so`), so its `NEEDED` list is only
`libz libdl libm libc` and a device without one of them gets a report instead of a load failure.

## Running it

```sh
libfafdeviceprobe.so [--out DIR] [--sections vulkan,vulkan_render,gles,gles_render,glslang]
                     [--timeout SECONDS] [--repeats N] [--no-fork]
```

| Option | |
|---|---|
| `--out DIR` | where `deviceprobe.json` and the PNGs go (default `.`; the app passes its run directory) |
| `--sections LIST` | a subset, in the fixed order above (default: all five) |
| `--timeout SECONDS` | one limit for every section (default 60 s for the reports, 90 s for the renders, 300 s for glslang: the emulator's ARM translation is slow) |
| `--repeats N` | compiles per shader in the glslang section (default 5: the first, cold, and four warm) |
| `--no-fork` | run the sections in this process (debugging only: a crash then ends the whole probe) |

Each section runs in a child process of its own (`fork`, no `exec`; the parent never loads a
driver). A section that crashes or hangs past its limit is killed and reported as such, and the
others still run.

**The result comes before the driver teardown** (second 0.4.1 build). Mobile drivers often crash or
hang while they are torn down. So a section's objects do not destroy their driver handles in their
destructors (`vkDestroyDevice` with everything created on the device, `vkDestroyInstance`,
`eglMakeCurrent(none)`, `eglDestroyContext`/`Surface`, `eglTerminate`): in a forked child they hand
that work to a queue (`faf_probe::Teardown`, `ProbeCommon.cpp`). The child writes its result (the
summary line and the section's JSON) to the parent first, then runs the queue in the destructors'
order (device before instance), then writes `teardown ok <ms> <entries>`. The parent gives the
teardown 20 s after the result. A crash or hang there costs nothing the section found: the section
keeps its status, its `process.teardown` says what happened, its console line gets
`(then the driver teardown crashed with signal 11)`, and the `RESULT` line `,teardown=crashed` (or
`timeout`, `no_output`). It does not change the exit code. With `--no-fork` the teardown runs at once,
as before.

`FAF_PROBE_TEST_FAULT=crash:<section>` or `hang:<section>` makes that section's child crash or hang
before its result, and `teardown-crash:<section>` or `teardown-hang:<section>` after it, in place of
the teardown, to test this. Each child has `PR_SET_PDEATHSIG` (`SIGKILL`): when the app cancels
the probe (SIGTERM, then SIGKILL, to the probe's pid), the running section's child goes with it.
The probe's parent is single-threaded and lives for the whole run, so the parent-death signal, which
follows the forking thread, fires only when the probe ends.

**Exit code:** 0 when every section ran to its end, whatever it found (`unsupported` and `error` are
results; a crash or hang in a driver's teardown after the result is reported, not counted); 1 when a
section crashed, hung or gave no output before its result, or `deviceprobe.json` could not be
written; 2 for an unknown option or an output directory that cannot be written. The app saves its
logcat for exit 1 and for a section whose teardown failed.

**Stdout** (line-buffered), for the app's log and the run zip:

```
[probe] libfafdeviceprobe 1 (arm64-v8a), pid 1234, output /…/runs/20261007-170000
[probe] vulkan ...
[probe] vulkan: ok: Vulkan 1.3.0: <device> (Vulkan 1.3.x, driver <name> <info>) BC1-3 no, ETC2 yes, ASTC yes, D24S8 no, D32 yes, ...
[probe] vulkan_render: ok: Vulkan render on <device>: pattern exact (0 of 65536 pixels off), device 12.3 ms, pipeline 4.5 ms ...
[probe] gles: ok: OpenGL ES 3.2 on <renderer> (...): S3TC no, ETC2 yes, ASTC yes, clip_control yes, ...
[probe] gles_render: ok: OpenGL ES render on <renderer>: pattern exact ...
[probe] glslang: ok: glslang 16.3.0 (placeholder shader set, 5 shaders): init 0 ms, first compile of all 47 ms, warm 31 ms
[probe] wrote /…/deviceprobe.json
[probe] RESULT vulkan=ok vulkan_render=ok/exact gles=ok gles_render=ok/exact glslang=ok exit=0
```

Each section line is `[probe] <section>: <status>: <one-line summary>`. The `RESULT` line names
every section's status, for the render sections the pattern verdict after a slash, and
`,teardown=<status>` for a section whose driver teardown did not end well.

## deviceprobe.json

One JSON object (compact, one line):

| Key | What it holds |
|---|---|
| `probe` | `version` (1), `abi`, `started_utc`, `pid`, `forked_sections`, `sections_requested`, `ms` |
| `device` | `manufacturer`, `model`, `soc_manufacturer`, `soc_model`, `hardware`, `board_platform`, `android_release`, `sdk`, `vulkan_hal` (`ro.hardware.vulkan`), `egl_hal`, `abilist`, `translated` (an arm64 probe under the emulator's ARM translation) and `native_bridge`, `kernel`, `machine`, `page_size`, `cpus`. No serial number or build fingerprint. |
| `summary` | per section: `"<status>: <the console line>"` |
| `sections` | per section, an object with `status` and `process` (below) |

**Status** of a section: `ok`; `unsupported` (no `libvulkan.so` / no physical device / no
`libEGL.so`; or a probe built without glslang); `error` (a step failed: `step` and `error` say which,
for example `vkCreateDevice: VK_ERROR_INITIALIZATION_FAILED`); and, set by the parent, `crashed`,
`timeout` or `no_output`. `process` is `{ms, exit_code, signal, timed_out, teardown}` of the
section's child; `teardown` (present when the result arrived) is `{"status":"ok","ms":..,"entries":..}`
(`entries`: how many objects' cleanup ran, 2 for `vulkan_render`: device, then instance), or
`{"status":"crashed","signal":..}`, `{"status":"timeout","limit_s":20}`, `{"status":"no_output"}`.
With `--no-fork`, `process` is `{ms, forked: false, teardown: "inline"}`.

**`sections.vulkan`:** `instance_version`, `load_ms`, `create_instance_ms`, `layer_count`,
`instance_extensions`, `device_count`, and `devices[]`, each with `name`, `type`, `api_version`,
`vendor_id`, `vendor`, `device_id`, `driver_version_raw`, `driver_version` (NVIDIA's layout for
NVIDIA, else major.minor.patch of the raw value), `pipeline_cache_uuid`, `driver` (`id`, `name`,
`info`, `conformance`; null before Vulkan 1.2 without `VK_KHR_driver_properties`), `features` (the
`VkPhysicalDeviceFeatures` the plan names), `formats_optimal_tiling` (per format: `sampled`,
`filter_linear`, `color_attachment`, `blend`, `depth_stencil`, `storage`, `blit_src`, `blit_dst`),
`limits`, `queue_families`, `memory_heaps`, `memory_types`, `notable_extensions`,
`extension_count`, `extensions`, and **`answers`**, the plan's questions in one place:

| `answers` key | true when |
|---|---|
| `bc1_3_sampled` | BC1 RGBA, BC2 and BC3 can be sampled (optimal tiling): FA's DXT textures without transcoding |
| `etc2_sampled`, `astc_4x4_sampled` | ETC2 RGB8 + RGBA8, ASTC 4×4 can be sampled |
| `d24s8_attachment`, `d32_attachment`, `d32s8_attachment` | the depth format can be a depth/stencil attachment |
| `fill_mode_non_solid`, `sampler_anisotropy`, `depth_clamp`, `independent_blend` | the feature bits |
| `max_texture_2d` | `maxImageDimension2D` |
| `timestamps_graphics` | `timestampComputeAndGraphics` |
| `custom_border_color`, `ahardwarebuffer_import` | the extension is present |

**`sections.vulkan_render`:** `device`, `device_created`, `create_instance_ms`, `create_device_ms`,
`create_pipeline_ms` (an empty pipeline cache), `create_pipeline_cached_ms` (the same pipeline again
through the now warm cache), `submit_to_fence_ms`, `gpu_ms` (timestamp queries; null without
them), `pattern` (`exact`, `within 1` or `wrong`), `mismatched_pixels`, `max_channel_error`,
`first_mismatch`, `pixels` (five sample pixels), `png`.

**`sections.gles`:** `egl` (`version`, `vendor`, `version_string`, `client_apis`, `config_count`,
`notable_extensions`, `extensions`, `initialize_ms`), `context` (the GLES version the context got,
3.2 down to 3.0), `create_context_ms`, `vendor`, `renderer`, `version`, `glsl_version`, **`answers`**
(`s3tc_dxt1_5`, `etc2` (core in ES 3.0), `astc_ldr`, `clip_control`, `color_buffer_float`,
`color_buffer_half_float`, `border_clamp` (an extension or ES 3.2), `anisotropic`,
`polygon_mode_line` (`NV_`/`ANGLE_polygon_mode`), `depth_clamp`, `timer_query`,
`program_binary_formats`, `max_texture_size`), `limits`, `compressed_texture_formats` (hex enums),
`notable_extensions`, `extension_count`, `extensions`.

**`sections.gles_render`:** `renderer`, `context`, `create_context_ms`, `compile_ms`, `link_ms`,
`program_binary_length`, `draw_finish_ms`, `read_pixels_ms`, and the same pattern keys and `png`.

**`sections.glslang`:** `placeholder: true` and a `note` saying so, `pipeline`, `glslang_version`,
`initialize_ms` (glslang builds its built-in symbol tables lazily, so the first shader's parse
carries that cost), `repeats`, `first_total_ms` (every shader once, cold), `warm_total_ms` (the sum
of the warm medians), and `shaders[]`: `name`, `stage`, `source_bytes`, `ok`, `error`, `first`
(`parse_ms`, `link_ms`, `spirv_ms`, `optimize_ms`, `total_ms`), `warm_median_ms`, `warm_runs`,
`spirv_words`, `optimized_words`.

## The test pattern

Both render sections draw one triangle over the whole 256×256 target. The fragment shader writes,
from the pixel's window coordinates: red = x & 255, green = y & 255, blue = a 32-pixel checkerboard,
alpha 1 (`Probe.h` `PatternPixel`). Every value is k/255, which an RGBA8 target stores exactly, so a
correct driver gives `exact`; `within 1` allows a rounding difference of one step. Window
coordinates are the API's own (Vulkan: y from the top; GLES: y from the bottom) and the readback is
in the API's own row order, so both PNGs show row y = 0 at the top and look the same when both APIs
are right. GLES uses a vertex buffer, not `gl_VertexID` (some GLES implementations drop a draw
without an enabled attribute, `port/android/src/Renderer.cpp`).

The Vulkan shaders are GLSL in `shaders/pattern.{vert,frag}`, compiled at build time by the NDK's
`glslc` into SPIR-V words (`<build>/deviceprobe-gen/pattern.*.inc`); the GLES shaders are inline
GLSL ES 3.00 in `ProbeGles.cpp`.

## Building

```sh
python scripts/port/build_runner.py --abi arm64-v8a --only-deviceprobe --out buildstage/runner/<dir>
python scripts/port/build_runner.py --abi arm64-v8a --probe --deviceprobe     # with the runner set
```

`--only-deviceprobe` compiles and links only the probe (seconds) and writes
`deviceprobe-build.json`, leaving the runner's `report.md` and `results.json` alone. The glslang
section links the glslang and SPIRV-Tools static libraries of libfaf_android.so's own CMake build
(`buildstage/android-native` for arm64-v8a, `buildstage/android-native-x86_64` for x86_64; another
with `--glslang-build DIR`), so it times the compiler the app runs; `ProbeGlslang.cpp` and glslang's
`ResourceLimits.cpp` are compiled like those libraries (`-fno-rtti -fno-exceptions`, `ENABLE_HLSL`,
`ENABLE_OPT`). Without them the probe still builds, and its glslang section says `unsupported`. The
unstripped probe is about 140 MB (glslang's debug information); stripped it is about 6.5 MB.

| File | What it is |
|---|---|
| `DeviceProbe.cpp` | `main`: options, device facts, a child per section with a time limit, `deviceprobe.json` |
| `ProbeVulkan.cpp` | the Vulkan report and the offscreen render |
| `ProbeGles.cpp` | the EGL/GLES report and the FBO render |
| `ProbeGlslang.cpp` | the glslang timing (placeholder shader set) |
| `ProbeCommon.cpp`, `Probe.h` | JSON writer, the pattern and its check, the PNG writer (zlib) |
| `shaders/pattern.{vert,frag}` | the Vulkan pattern shaders (GLSL 450) |

## Checks (second 0.4.1 build, 2026-10-08)

On the API 36 emulator (SwiftShader for both APIs), `r041b-pkg` probes `f856507e…` (arm64-v8a) and
`ae269253…` (x86_64):

- in the app (x86_64 APK, versionCode 3450): all five sections `ok`, both patterns exact, every
  section's `process.teardown` `ok` (`vulkan_render`: 2 entries), whole probe 0.7 s, glslang 51 ms
  first / 33 ms warm; the run prepared no game data;
- over adb: `teardown-crash:vulkan_render` keeps `pattern exact`, `teardown=crashed`, exit 0;
  `teardown-hang:gles` is killed after 20 s, `teardown=timeout`, exit 0; `crash:vulkan_render` gives
  `vulkan_render=crashed`, the other sections `ok`, exit 1.
