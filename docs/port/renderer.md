# Renderer port: a Diligent `gpg::gal` backend

The renderer track (M6/M7 of the [Android roadmap](android-roadmap.md), workstream W3). The goal,
in order:

1. FAF's real main menu through a new `gpg::gal` backend on
   [Diligent Engine](https://github.com/DiligentGraphics/DiligentCore) on Windows, compared pixel
   by pixel with the D3D9 backend;
2. the same menu on the Android phone;
3. the in-game skirmish view.

The D3D9 backend stays the 1:1 reference and is never edited. All new code is in an opt-in
graphics build of `main.exe`, and the default `main.exe` does not change (checked per object
file, see [The default main.exe](#the-default-mainexe-does-not-change)).

## Status (2026-10-07)

| Step | What | Gate | State |
|---|---|---|---|
| 0 | D3D9 reference harness (`port/graphics/capture`) | 3 runs at 1280x720 give byte-identical RGB at frames 60, 300 and 900 | **passed** |
| 1 | Diligent integration spike (`port/graphics/diligent`), `/gal diligent:d3d11` | main menu and 900 frames; D3D11 debug layer 0 errors; the 11 effects list the same techniques as D3D9; a committed script rebuilds the Diligent Win32 install | **passed** |
| 2 | Effect front end, metadata (`port/graphics/fx`) | the metadata JSON equals D3DX reflection for every effect variant the game and FAF load | **passed** |
| 3-11 | The menu's draw path, phone, in-game | see [The plan](#the-plan) | open |

Steps 0-2 are milestone M6a. Measured on the dev PC (AMD Radeon RX 9070 XT, Windows 11,
Debug|Win32 graphics build, FAF 3839 data):

- **Gate 0.** Three consecutive runs give the RGB hashes 60 `9ab312ba8320f018`, 300
  `a901156c8b13d860` and 900 `fea3146c72876a69` in every run, and byte-identical BMP files. The same
  three hashes come out at `/galpace 0` and `/galpace 12`, with 123 frames captured in one run, on
  separately built executables, and from a Release|Win32 graphics build.
- **Gate 1.** 902 harness frames, 901 presents, 900 `EndScene` calls, the main-menu module loaded. The
  D3D11 debug layer is live (a self-test provokes 2 errors, which are not counted) and reports 0 errors,
  0 corruption and 0 warnings; Diligent logs no error. Technique lists, declared = Diligent = D3D9 HAL:
  cartographic 4, frame 18, mesh 180, particle 68, primbatcher 14, range 1, sky 4, terrain 68, ui 6,
  vision 3, water2 5. The menu uses 27 of the 50 `Device` slots.
- **Gate 2.** 26 of 26 game variants and 6 test effects match D3DX: 891 techniques with their validity
  on 3 device profiles, 1018 passes, 6172 pass states, 2016 shader slots, 1036 parameters, 623 default
  values and 2240 annotations. The library also compiles and links for `aarch64-linux-android26`
  with `-Werror`.
- **D3D9 against Diligent today.** Draws are still no-ops in the spike, so a Diligent frame read
  back through gal is the engine's clear colour, RGB (0, 0, 0) in every pixel (the frame's
  `SetRenderTarget2(mHead, clear=1, color=0)`, WxRuntimeTypes.cpp:3763). Against the D3D9 frame 60,
  270,499 of 921,600 pixels differ, exactly the D3D9 frame's non-black pixels. That is where step 3
  starts.

The M6a progress video (36 s, made from these captures) is in `buildstage/media/m6a/`, which is
gitignored like every game-derived image.

## Building the graphics option

```powershell
# once: the pinned DiligentCore for Win32, offline, about 10 minutes, below-normal priority
powershell -ExecutionPolicy Bypass -File scripts/port/build_diligent_win32.ps1

msbuild src\sdk\main.vcxproj /p:Configuration=Debug /p:Platform=Win32 /p:FafPortGraphics=true
# -> output\main-gfx\Win32\Debug\main.exe
```

`/p:FafPortGraphics=true` makes `main.vcxproj` import `port/graphics/port_graphics.props`
(main.vcxproj:2758-2761, right before `Microsoft.Cpp.targets`). Without the property the import is
skipped. The props:

- define `FAF_PORT_GRAPHICS`. Every engine hook in `src/sdk` is inside
  `#if defined(FAF_PORT_GRAPHICS)` or `FAF_PORT_GRAPHICS_DILIGENT`;
- build into `output\main-gfx` and `buildstage\main-gfx`, never the default directories;
- turn the post-build step off. That step (main.vcxproj:196-200 and the three other configurations)
  copies `main.exe` and `main.pdb` into `C:\ProgramData\FAForever\bin`, the folder FAF plays from;
- compile `port/graphics/capture/**` and, when the Diligent libraries exist, `port/graphics/diligent/**`,
  each into its own object subdirectory;
- link `buildstage\diligent\install\Win32` (`DiligentCore.lib`, `d3d11.lib`, `d3dcompiler.lib`)
  and define `FAF_PORT_GRAPHICS_DILIGENT`.

The Diligent backend is built only in **Debug**. Diligent's Release libraries are `/GL` objects,
and main.vcxproj links Release with `/FORCE` and without LTCG, so a Release graphics build contains
the capture harness only. x64 has no Diligent install yet.

**Every build of the default configuration runs that copy, whatever its `/p:OutDir`,** and so
replaces the `main.exe` in `C:\ProgramData\FAForever\bin`. Pass
`/p:PostBuildEventUseInBuild=false` for any build that is not meant to be played; the graphics
props do this themselves. (With `/p:OutDir` on the command line, the WildMagic project references
also resolve there: copy `Foundation.lib`, `Dx9Renderer.lib` and `Dx9Application.lib` from
`dependencies\WildMagic3p8\*\Debug` into that directory, or the link fails with LNK1181.)

### The Diligent build

`scripts/port/build_diligent_win32.ps1` is the recipe of the Win32 build that was already in
`dependencies/DiligentCore/install/Win32`. That build came from the `feature/diligent-renderer`
branch's `bootstrap_diligent.py`, which is not in this repository. The recipe makes two changes so
that it runs offline:

- abseil-cpp comes from `dependencies/abseil-cpp`;
- NVAPI stays off.

The old install's Debug `DiligentCore.lib` references `NvAPI_*` but ships no `nvapi.lib`. Because the
project links with `/FORCE`, those unresolved symbols would only show up as a warning, so the props
link the new install, not the old one.

Compared library by library with the old install:

- the same 46 files;
- Debug `DiligentCore.lib`: the same 245 members, with identical `/FAILIFMISMATCH` directives
  (`_ITERATOR_DEBUG_LEVEL=0`, `MDd_DynamicDebug`, matching main.vcxproj:143-161). Symbols are identical
  in 234 members; the other 11 differ by the NVAPI imports and three names that hash the source path;
- Release: the member lists and directives are identical.

### The default main.exe does not change

Checked at the end of M6a, on the working tree with the hooks of steps 0 and 1:

- **Objects.** Every touched engine file is compiled twice with `cl.exe`, once from HEAD and once
  from the working tree with a `#line` after each hunk, so that `__LINE__` keeps HEAD's numbers. The
  flags come from main.vcxproj plus `/DUNICODE /D_UNICODE`, with `/Brepro` and no debug info. The
  files are `gpg/gal/Device.cpp`, `moho/app/CScApp.cpp` and `moho/app/WxRuntimeTypes.cpp`, in
  Debug|Win32, Release|Win32, Debug|x64 and Release|x64. All 12 object pairs are byte-identical. As a
  control, the working tree without the `#line` pins gives different objects for all three files.
- **Project evaluation.** MSBuild's evaluation of `main.vcxproj` at HEAD and in the working tree
  (`-getProperty`/`-getItem` over every property and item type) is identical in all four
  configurations: 1252 or 1253 properties, 284 item types and 2346 items each.
- **Behaviour.** A default Debug|Win32 build plays the M3a replays to the same checkpoint chains:
  T1 `7e159db290f576f1`, T2 `cac943d2cc0593a9`, T3 `411acb68b7c57709`
  ([headless-replay.md](headless-replay.md)). The graphics build gives the same three chains.

## Step 0: the D3D9 reference harness

`port/graphics/capture` ([README](../../port/graphics/capture/README.md)) turns the graphics
`main.exe` into a frame harness with `/galharness <dir>`. It renders the main menu in a window that
is never shown or activated, captures chosen frames through gal right after `EndScene`, and exits.
The capture is `GetHeadOutputContext`, then `GetRenderTargetData` into a system-memory A8R8G8B8
texture, then `Lock` (`HarnessSceneEnded`, WxRuntimeTypes.cpp:4271-4275). Any backend that
implements these three calls is read back the same way.

```bash
MSYS_NO_PATHCONV=1 python scripts/port/gfx_capture.py --runs 3 --lock <lock file>             # gate 0
MSYS_NO_PATHCONV=1 python scripts/port/gfx_capture.py --gal diligent:d3d11 --frames 60,300,900 --runs 1
python scripts/port/gfx_capture.py compare <run dir A> <run dir B>          # exact, max |delta|, heat map
```

`gfx_capture.py` does everything around a run:

- a scratch `Game.prefs` passed with `/prefs`, and a copy of `init_faf.lua` that mounts nothing of the
  user's and prunes no real cache;
- the lock file, so that only one GUI run happens at a time;
- below-normal CPU and GPU priority, SW_HIDE, a job object and a timeout;
- its own 50 ms monitor, which kills the run if a window shows or takes the foreground.

It refuses to start while any `FAF_*` variable is set, because the committed render-path probes arm
themselves from those.

What makes the menu deterministic, in short (the full table is in the capture README):

| Source of variation | Pin |
|---|---|
| Frame delta | `/framerate 30`: `CScApp::Main` hands out exactly 1/30 s (CScApp.cpp:297-315, 1039-1059) |
| Lua wall clock: `CurrentTime`, `GetSystemTimeSeconds`, `GetSystemTime`. FAF's `WaitSeconds` uses `CurrentTime`, and the menu animation starts with `WaitSeconds(.2)` | The three binders read a virtual clock, the sum of Main's deltas (`HarnessPins.cpp`) |
| `math_GlobalRandomStream` seeded from the time | Fixed seed (`/galseed`) |
| Movie, sound, music | `/nomovie /nosound /nomusic`, `mainmenu_bgmovie = false` |
| The user's prefs, vault, cache, `fa_path.lua` | Scratch copies only |
| Mouse, keyboard, focus | Hidden window beyond the virtual screen; activation refused (WH_CBT); input messages dropped (WH_GETMESSAGE) |
| Wall-clock pacing | None needed: `/galpace 0`, 12 and 30 give the same bytes |

The clock pin is what matters. Without it (`/galnopins`), frames 10-30, while the brackets drop in,
differ between runs at different paces: up to 58,711 pixels, with a maximum delta of 255. From frame
45 on the menu has settled, so frames 60/300/900 alone would not show this.

The menu never becomes static: the legal text keeps scrolling (FAF `lua/ui/menus/main.lua:202-209`).
So the gate compares fixed frame numbers, never "the settled menu". With `/windowed 1280 720` the
client area is 1280x729, because the frame window's minimum size is 1024x768
(WxRuntimeTypes.cpp:1237). The back buffer is the head size, 1280x720, and its bottom rows of UI
fall outside the capture. That is the engine's own behaviour.

## Step 1: the Diligent integration spike

`port/graphics/diligent` ([README](../../port/graphics/diligent/README.md)) is a `gpg::gal::Device`
on Diligent. It overrides all 50 slots, and is selected with `/gal diligent:d3d11`. The engine hooks
are the `DeviceApiDiligent` case in `Device::Create` (Device.cpp:188-198) and the `/gal` parse plus
the Win32 cursor in `CScApp::CreateDevice` (CScApp.cpp:1198-1206, 1335-1339).

`DeviceApiDiligent` is `static_cast<DeviceApi>(3)` in `GalDiligent.h`, not a new enumerator. That
keeps `DeviceContext.hpp`, which about 40 TUs include, untouched.

| Part | State |
|---|---|
| D3D11 device, immediate context, swap chain on the engine window, RGBA8 head render target | real |
| `ClearTarget` + `Clear` on the head | real |
| `Present`: a full-screen triangle draws the head into the back buffer. A draw, not a copy: a BGRA swap chain and Android pre-rotation need it | real |
| `GetRenderTargetData` of the head: a staging copy, written into the system-memory texture in D3D9's byte order, so the step-0 harness captures this backend through gal unchanged | real |
| Capabilities and adapter modes | D3D9 values, from `IDirect3D9` HAL queries without a device |
| Effects | metadata only, from D3DX reflection (below) |
| Textures, `GetTexture2D` | D3DX textures in the scratch pool of a NULLREF device |
| Other targets, buffers, vertex formats, pipeline state | placeholders |
| Draws, states, other readbacks, saves, cursor | logged no-ops, counted per slot in `/galreport` |

```bash
python port/graphics/diligent/tools/gate1.py --exe output/main-gfx/Win32/Debug/main.exe --lock <lock file> --out <scratch dir>
```

`gate1.py` runs the backend under the step-0 harness for 900 frames, with the debug-layer self test
and `/galdumpfx`. It then compares each effect's technique list with a D3D9 HAL device's
`FindNextValidTechnique` order for the same source bytes (`tools/fxtechlist.cpp`).

Decisions recorded in step 1:

- **Effect metadata in Windows oracle mode uses a NULLREF device with its own validity rule.** On a
  NULLREF device, `D3DXCreateEffect`, textures and `GetPassDesc` work. `FindNextValidTechnique` and
  `ValidateTechnique` crash in `d3d9.dll`. A device-less `ID3DXEffectCompiler` returns no pass
  bytecode. So the backend keeps the techniques in declaration order and accepts one when every pass's
  shader version is within the adapter's HAL caps. All 11 effects agree with a HAL device.
- **The debug-layer self test is a zero-sized `CreateBuffer`, not an invalid draw.** On the RX 9070
  XT, an invalid draw removed the hidden run's device (`DXGI_ERROR_DRIVER_INTERNAL_ERROR`). The
  runtime rejects a zero-sized buffer before anything reaches the driver.
- **The x87 control word is set to `_PC_53` around device and shader creation** and restored after.
  The engine runs under `_PC_24`.

## Step 2: the effect front end

`port/graphics/fx` ([README](../../port/graphics/fx/README.md)) is the portable C++17 library
`gpg::gal::fx`:

- `FxSource`, `FxLexer`, `FxPreprocessor` (hide sets, `#`/`##`, `#if`, caller macros);
- `FxEffectParser`/`FxAst` (the fx_2_0 grammar; function bodies are skipped);
- `FxStates`;
- `FxMetadata`.

It produces everything `ID3DXEffect` reports and the engine's effect layer uses: parameters with
defaults and annotations, techniques with their validity per device profile, and passes with their
states in D3DX order and the shader entry points. It has its own CMake project and is not part of
`main.vcxproj`.

```powershell
python scripts/port/fx_metadata_gate.py --arm64-check     # build, unit tests, all variants, arm64 compile
```

The gate extracts every effect from the SCFA and FAF archives into `buildstage` or scratch, never
into the repository. It runs `fxmeta` and the D3DX dumper `fxd3dx_dump` on each variant and diffs
the JSON field by field. The dumper makes the calls `DeviceD3D9::CreateEffectFromSourceBuffer` makes,
with the same flags and the legacy `d3dx9_31` compiler, on a fake `IDirect3DDevice9` that renders
nothing.

Measured D3DX behaviour that the front end reproduces:

- **Constant folding** is double precision. Of 300 random `normalize()` initializers, only double
  evaluation reproduces all of them; water2.fx `SunColor` needs it.
- **Folding quirks of the legacy compiler:** `length(v)` folds to `|v.x|`, and `round(x)` is
  `floor(x + 0.5)`.
- **Two state mappings are wrong in D3DX itself.** `CCW_StencilFail` sets `D3DRS_STENCILFAIL`, and
  `BlendOpAlpha` sets `D3DRS_BLENDOP`. A backend that reproduces D3D9 must apply the metadata as it is,
  not the source's intent; this affects range.fx and vision.fx.
- **Validity follows shader creation, not the caps.** A technique is valid when the device created
  every shader of its passes.

## The plan

From the M6 review (2026-10-07). Durations are for one engineer.

| Step | What | Gate |
|---|---|---|
| 0 | D3D9 reference harness | done (above) |
| 1 | Diligent spike | done (above) |
| 2 | Front-end metadata | done (above) |
| 3 (3-4 wk) | Menu draw path on Diligent-D3D11: D3D9 state shadow and PSO cache; dynamic vertex and index buffers through `Map(DISCARD/NO_OVERWRITE)`, re-uploaded on the first map of a frame; the dynamic DXT5 atlas; `GetTexture2D` (D3DX oracle and portable); viewport as scissor; the Win32 cursor; primbatcher, ui and frame as SM5 generated into buildstage; the `TextureContext` pointer rebuilt from `dataArray_` | RGB identical to step 0 at frames 60/300/900, or max \|delta\| <= 1 on <= 0.1% of pixels with the heat map; `GetTexture2D` gives D3DX's DXT5 blocks for every menu UI file |
| 4 (2-3 wk) | Swap in the front end's SM5 and metadata; `fxdiff` | step 3's images unchanged; `fxdiff` of every primbatcher, ui and frame entry point agrees with the legacy bytecode on Diligent D3D11, Vulkan and GL |
| 5 (2 wk) | galtrace and galplay, after first pixels | a 900-frame menu trace replays into D3D9 and Diligent byte for byte, and decodes in an x86_64 build |
| 6 (1-2 wk) | Vulkan and GL on Windows | step 3's tolerances on `diligent:vk` and `:gl`; 0 render-pass ends per primbatcher flush on Vulkan; scripted navigation into the skirmish lobby |
| 7 (3-4 days, any time) | Phone probe: BC, ETC2, ASTC, D24S8, fillModeNonSolid, border clamp, clip control; glslang timings; Vulkan from an exec'd child process | numbers in the docs; decide in-process or out-of-process engine for M7 |
| 8 (2 wk) | galplay on the phone: GL thread marshalling, GL z remap and v-flip, CPU BC decode, BGRA swizzle, pre-rotated Present, persistent SPIR-V/GLSL caches | the menu trace on Vulkan and GLES matches Windows `diligent:vk`; a second launch compiles 0 shaders |
| 9 (6-8 wk) | GUI closure on Android: wx-lite shim over an AppHost, the 19 menu TUs, a trap set for in-game renderers, FreeType fonts | 0 undefined and 0 duplicate symbols; the emulator boots to the menu in-app; registry counts equal Windows |
| 10 (2-3 wk) | Live main menu on the phone | phone frames match the Windows Diligent reference outside a font mask; 30 minutes of navigation without a crash |
| 11 | Before any in-game parity: freeze and validate the in-game D3D9 reference against retail; typed emitter (glslang 776/776); ETC2/ASTC cache; PSO pre-warm; DepthBias conversion; fog; M3d | a skirmish replay view matches retail and D3D9 within tolerance; first-minute frame spikes under 50 ms on the phone |

What changed against the first draft of the plan, and why:

- **The Diligent Win32 build already existed** (`dependencies/DiligentCore/install/Win32`). It is now
  scripted (step 1).
- **Dynamic buffers use `Map`, not `UpdateBuffer`.** On Vulkan, `UpdateBuffer` ends the render pass
  (DiligentCore `VulkanUtilities/CommandBuffer.hpp:526-547`). The primbatcher locks with discard on
  every flush (CD3DPrimBatcher.cpp:1222, 1227).
- **GLES needs its own handling:**
  - resource creation is single-threaded there (`RenderDeviceGLImpl.cpp:877`), so the prefetch
    thread's creations must move to the render thread;
  - NDC z and the render-target y origin are left to the application, so the GL wrapper remaps z and
    v-flips sampled render targets.
- **glslang accepts the menu effects but not the game's.** FXC compiles 767 of 776 converted entry
  points, glslang 403, with 0 of 78 for FAF terrain. A typed emitter is a hard prerequisite for
  in-game on Vulkan and GLES, not an optimisation.
- **Present is a draw** (a BGRA swap chain, pre-rotation).
- **Bloom is no menu gate.** `DoBloom` runs before `RenderUI` (WxRuntimeTypes.cpp:4250-4257), so in
  the front end it blooms an empty world.
- **The render driver is a wx window.** The 19 menu TUs use about 97 wx identifiers, so Android needs
  a wx-lite shim, not `_WIN32` guards.
- **The full front end is off the Windows critical path.** In Windows oracle mode the backend takes
  metadata from D3DX, so first pixels (step 3) do not wait for the SM5 emitter.

Risks:

- glslang's HLSL front end is deprecated upstream (`ThirdParty/glslang/CHANGES.md:7-13`).
- D3D9 and Diligent share every bug above gal. Retail `ForgedAlliance.exe` frames (its own
  `DUMP_Frame`, 0x007F5840) are the missing third oracle.
- The in-game D3D9 reference is still moving: probes on the render path, and performance changes on
  master.
- No program-binary cache for GLES in Diligent, and Vulkan PSOs are created on first use.
- Phones without BC need an ETC2/ASTC transcode: CPU-decoded DXT1 is 8x larger.
- Pointer-hashed Lua tables can change `pairs()` order between backends (LuaObject.cpp:3563-3578);
  galtrace-level comparison avoids it.

Open questions: which commit freezes the in-game D3D9 reference, and may the committed render-path
probes be retired? In-process or out-of-process engine on the phone (step 7)? Does `main_x64.exe`
reach the menu (M6d)? Vulkan and GL on Windows before the phone work, or after?

## Files

| Path | What |
|---|---|
| `port/graphics/port_graphics.props` | The opt-in build (`/p:FafPortGraphics=true`) |
| `port/graphics/capture/` | Step 0: the frame harness inside `main.exe` |
| `scripts/port/gfx_capture.py` | Runs the harness: prefs, lock, priority, monitor, comparison and heat maps |
| `port/graphics/diligent/` | Step 1: the Diligent backend; `tools/gate1.py`, `tools/fxtechlist.cpp`, `tools/check_vertex_table.py` |
| `scripts/port/build_diligent_win32.ps1` | The Diligent Win32 build, offline |
| `port/graphics/fx/` | Step 2: `gpg::gal::fx`, `fxmeta`, `fxd3dx_dump`, unit tests, test effects |
| `scripts/port/fx_metadata_gate.py` | Gate 2 |
| `src/sdk/gpg/gal/Device.cpp`, `src/sdk/moho/app/CScApp.cpp`, `src/sdk/moho/app/WxRuntimeTypes.cpp`, `src/sdk/main.vcxproj` | The only engine-side edits, all guarded or conditional |
