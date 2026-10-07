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
| 3 | The menu's draw path on Diligent-D3D11 (`port/graphics/diligent`) | frames 10-45, 60, 300 and 900 RGB-identical to step 0, or max \|delta\| <= 1 on <= 0.1 % of the pixels, with heat maps; `GetTexture2D` gives D3DX's DXT5 blocks for every menu UI file, oracle and portable; debug layer 0 errors over 900 frames; no `UpdateBuffer` per draw | **passed** (identical) |
| 4 (D3D11) | The front end's SM5 (`FxHlslEmitter`) in the backend; `fxdiff` | step 3's images unchanged; every primbatcher, ui and frame stage compiles with FXC and through glslang to SPIR-V (spirv-val clean); `fxdiff` renders each pass in D3D9 and Diligent-D3D11 within tolerance | **passed**; rendering on Vulkan and GL is step 6 |
| 5-11 | galtrace, Vulkan/GL, phone, in-game | see [The plan](#the-plan) | open |

Steps 0-2 are milestone M6a, steps 3 and 4 (D3D11) are M6b ([below](#steps-3-and-4-the-menu-drawn-by-diligent-m6b)).
Measured on the dev PC (AMD Radeon RX 9070 XT, Windows 11, Debug|Win32 graphics build, FAF 3839
data). M6b first:

- **Pixel parity (step 3).** `/gal diligent:d3d11` draws FAF's main menu, and the frames are the
  D3D9 frames. In three runs, frames 10, 15, 20, 25, 30, 45, 60, 300 and 900 have the D3D9 hashes
  (10 `d92c3a2233b80c55` ... 60 `9ab312ba8320f018`, 300 `a901156c8b13d860`, 900 `fea3146c72876a69`);
  `gfx_capture.py parity` finds 0 of 921,600 pixels different in every frame, alpha included, and
  writes the heat maps. A longer run goes further: all 540 frames from 1 to 540 (the opening
  animation and 16 s after it, 534 distinct images) are byte-identical BMPs in both backends. The
  comparison is not vacuous: without D3D9's half-pixel offset (`/galhalfpixel none`) 250,268 pixels of
  frame 60 differ and the gate fails.
- **Debug layer and draws.** Over 900 frames the D3D11 debug layer reports 0 errors, 0 warnings and 0
  corruption (its self test provokes 2, not counted); Diligent reports 0 errors. 7192 draws, 0 skipped,
  3 pipeline states. The menu calls 28 of the 50 `Device` slots.
- **Vulkan readiness.** `UpdateBuffer` is never called (0 in the whole run). Vertex and index data go
  through `Map(DISCARD)`, 14,384 times, 2 per draw, as D3D9 locks them; constants go through
  `Map(DISCARD)` too. `UpdateTexture` runs 4 times (1 MB each: the texture batcher's DXT5 atlas, which the engine
  uploads whole, CD3DTextureBatcher.cpp:244-261). No GPU object is
  created off the render thread.
- **Portable effects (step 4).** The shaders the menu runs are SM5 HLSL that `FxHlslEmitter` generates in
  the process from the effect source (report: `effectLayer.shaderSource` = FxHlslEmitter, 2 shaders
  compiled, 0 failures; the menu draws with one primbatcher technique). `fxdiff` over every pass of
  primbatcher, ui and frame: FXC 39/39 stages, glslang 39/39 and spirv-val 39/39, 78 of 78 pass renders
  agree with D3D9 (max |delta| 0.0022 on a float target, 1 LSB on an 8-bit target).
- **GetTexture2D.** The menu decodes 20 `/textures/ui` files, all DXT5 in whole blocks. In oracle and in
  portable mode the blocks equal D3DX's on a D3D9 HAL device for all 20, and so do the engine's outputs.
- **Gates 1 and 2 still pass** on the M6b build; the backend's 12 known-answer checks (`/galselftest`)
  pass with 0 debug-layer messages.

M6a:

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
- **D3D9 against Diligent at the end of M6a.** Draws were still no-ops in the spike, so a Diligent frame
  read back through gal was the engine's clear colour, RGB (0, 0, 0) in every pixel (the frame's
  `SetRenderTarget2(mHead, clear=1, color=0)`, WxRuntimeTypes.cpp:3763). Against the D3D9 frame 60,
  270,499 of 921,600 pixels differed, exactly the D3D9 frame's non-black pixels. Step 3 started there.

The videos are in `buildstage/media/`, which is gitignored like every game-derived image:
`m6a/` holds the M6a progress video (36 s, from the M6a captures); `m6b/faf-re-m6b-menu-diligent.mp4`
(18 s) shows frames 1-540 of both backends side by side in real time, with the per-frame hashes and
the heat map of their difference, next to stills and the parity strips of frames 30, 60, 300 and 900.

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
- compile `port/graphics/capture/**` and, when the Diligent libraries exist, `port/graphics/diligent/**`
  (without its `tools/`) and the effect front end's library `port/graphics/fx/src/*.cpp`, each into
  its own object subdirectory. Debug|Win32 then compiles 1043 TUs, the default 1023;
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
  Debug|Win32, Release|Win32, Debug|x64 and Release|x64. All 12 object pairs are byte-identical. Even
  unpinned the objects are byte-identical (no `__LINE__` reaches code); line shifts appear only in the
  PDB line tables. As a positive control, compiling the working tree with `FAF_PORT_GRAPHICS` and
  `FAF_PORT_GRAPHICS_DILIGENT` defined gives different objects for all three files. The M6a review
  repeated the check through MSBuild's own CL task and command lines, with the same result.
- **Project evaluation.** MSBuild's evaluation of `main.vcxproj` at HEAD and in the working tree
  (`-getProperty`/`-getItem` over every property and item type) is identical in all four
  configurations: 1252 or 1253 properties, 284 item types and 2346 items each.
- **Behaviour.** A default Debug|Win32 build plays the M3a replays to the same checkpoint chains:
  T1 `7e159db290f576f1`, T2 `cac943d2cc0593a9`, T3 `411acb68b7c57709`
  ([headless-replay.md](headless-replay.md)). The graphics build gives the same three chains.

Checked again for M6b: M6b changed no file under `src/` (`git diff HEAD -- src` is empty, nothing
untracked), so there is no touched engine TU to compare, and the headers the guarded hooks include
(`port/graphics/capture/GalCapture.h`, `port/graphics/diligent/GalDiligent.h`) are unchanged too.
Everything new is under `port/graphics`, which only the graphics props compile: the default
evaluation still has 1023 `ClCompile` items, none under `port/graphics`, and the post-build copy on.
A fresh default Debug|Win32 build of the working tree plays T1/T2/T3 to the three chains above, and
so does the M6b graphics build. (HEAD's temporary probe at `CScriptEvent.cpp:1910` appends to
`C:\ProgramData\FAForever\bin\navlay.txt` whenever a sim runs; for these chain runs the file was made
read-only, so the probe's `fopen` failed and the FAF folder stayed unchanged, sha256 and mtime.)

## Step 0: the D3D9 reference harness

`port/graphics/capture` ([README](../../port/graphics/capture/README.md)) turns the graphics
`main.exe` into a frame harness with `/galharness <dir>`. It renders the main menu in a window that
is never shown or activated, captures chosen frames through gal right after `EndScene`, and exits.
The capture is `GetHeadOutputContext`, then `GetRenderTargetData` into a system-memory A8R8G8B8
texture, then `Lock` (`HarnessSceneEnded`, WxRuntimeTypes.cpp:4271-4275). Any backend that
implements these three calls is read back the same way.

```bash
MSYS_NO_PATHCONV=1 python scripts/port/gfx_capture.py --runs 3 --lock <lock file>             # gate 0
MSYS_NO_PATHCONV=1 python scripts/port/gfx_capture.py --gal diligent:d3d11 --frames 60,300,900 --runs 1 --lock <lock file>
python scripts/port/gfx_capture.py compare <run dir A> <run dir B>          # exact, max |delta|, heat map
python scripts/port/gfx_capture.py parity <D3D9 run dir> <Diligent run dir> --out <dir>   # step 3 gate
```

Every GUI run on the machine, `gate1.py` and `fxdiff.py` included, has to name the same lock file
(`--lock` or `GFX_GUI_LOCK`): the default lock is `buildstage/gfx-capture/gui.lock`, and `gate1.py`
takes none unless given one.

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

The spike as it stood at the end of M6a (M6b replaced every placeholder and no-op, see
[steps 3 and 4](#steps-3-and-4-the-menu-drawn-by-diligent-m6b)):

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
  shader version is within the adapter's HAL caps. All 11 effects agree with a HAL device. On this
  PC's RX 9070 XT, though, the D3D9 HAL rejects none of the 11 effects' techniques, so gate 1's list
  check cannot tell this rule from "every declared technique in order"; a lower-caps device profile
  (for example `fxd3dx_dump`'s sm2/sm1 fake-device caps) is still needed to exercise a rejection.
  Since M6b the metadata comes from the portable front end, not from D3DX; only the HAL caps remain
  from the oracle.
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
- `FxMetadata`;
- `FxHlslEmitter` (M6b, step 4: SM5 HLSL per pass stage, below).

It produces everything `ID3DXEffect` reports and the engine's effect layer uses: parameters with
defaults and annotations, techniques with their validity per device profile, and passes with their
states in D3DX order and the shader entry points. Its tools and tests have their own CMake project;
since M6b the graphics props also compile the library into the graphics `main.exe`.

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

## Steps 3 and 4: the menu drawn by Diligent (M6b)

`/gal diligent:d3d11` now draws FAF's main menu with real GPU resources, and every captured frame
equals D3D9's (status [above](#status-2026-10-07)). The backend's parts, all in `port/graphics`
([diligent README](../../port/graphics/diligent/README.md), [fx README](../../port/graphics/fx/README.md)):

| Part | Files | What it does |
|---|---|---|
| Resources | `diligent/ResourcesDiligent.*`, `DiligentHost.*` | Textures: the image the D3DX texel oracle decodes becomes a Diligent texture with the full mip chain, created on the render thread when first used; locked rectangles upload as dirty rectangles; L8, A8L8, R8G8B8, X1R5G5B5, A4R4G4B4 and A2R10G10B10 are converted on upload. The `TextureContext` data pointer is rebuilt from `dataArray_` (m6u-CRIT R9). Render, cube and depth targets, depth with the shader-readable flag. Vertex and index buffers keep a CPU shadow: dynamic ones are written with `Map(DISCARD)` or `Map(NO_OVERWRITE)` as D3D9 locks them and rewritten whole on the first map of a frame; static ones are created from the shadow. Input layouts come from the D3D9 vertex table (`VertexFormatTableD3D9.inl`) |
| State shadow, PSO cache, draws | `diligent/PipelineDiligent.*` | D3D9's render states from its creation defaults plus `InitState` (D3D9Interfaces.cpp:5889), the technique-begin reset (0x00946260), the pass's states from the effect layer, the colour-write, wireframe and fog slots. A hashed PSO cache keyed by the program, the normalised blend, depth, stencil and raster state, the input layout, the target formats and the topology. Target binding, viewport, `Clear` (a quad when the viewport is smaller than the target), draws, `StretchRect` (a copy or a bilinear blit) |
| Device | `diligent/DeviceDiligent.*` | The 50 slots on the above, including `ClearTarget` (resets the viewport when the colour target changes, as `IDirect3DDevice9::SetRenderTarget` does), `GetRenderTargetData` of any target, `UpdateSurface`, the Save slots, the Win32 cursor (as `CursorD3D10` makes it) and Reset on resize |
| Effect layer | `diligent/EffectsDiligent.*`, `EffectsDiligentGpu.*` | gal `Effect`/`EffectTechnique`/`EffectVariable` on the portable front end: metadata from `FxMetadata` (D3DX is no longer used; only the HAL caps that decide technique validity come from the oracle); setters with `ID3DXEffect` semantics into CPU shadows; `BeginPass`/`CommitChanges` snapshots; constants uploaded with `Map(DISCARD)` only, and again on the first use in a frame (the Vulkan/D3D12 rule for discarded dynamic memory); immutable samplers from `sampler_state` over the D3D9 defaults; unbound textures sample a (0, 0, 0, 1) null texture. Every GPU object is released and remade with the device |
| Contract | `diligent/PassBinding.h` | The interface between effect layer and draw path (version 1): vertex attribute slots per D3D9 usage and how each element is fed (D3DCOLOR as UNORM8 swizzled `.bgra` in the shader), the pass's render and texture-stage states in D3DX order, the program (shaders, resource signature, unique id), the draw state (viewport, half-pixel offset, raw alpha-test state, frame index) and `PassSink` (`OnBeginPass`/`OnEndPass`). Forward declarations only |
| SM5 | `fx/src/FxHlslEmitter.cpp` | One SM5 shader per pass stage with only what its entry point reaches, plus what to bind; generated in the process when a pass is first drawn and compiled by Diligent (FXC, `d3dcompiler_47`) under `_PC_53` |
| `GetTexture2D` | `diligent/DeviceDiligent.cpp`, `Texture2DPortable.*` | `/galtex2d oracle` (default): D3DX on the NULLREF device, as D3D9 does it. `/galtex2d portable`, the Android path: no D3DX; a lenient DDS reader, DXT5 blocks copied when the file holds whole blocks, otherwise a float BC3 encoder modelled on D3DX's |

Running the gates (Debug|Win32 graphics build; `<lock>` is the machine's shared GUI lock):

```bash
EXE=output/main-gfx/Win32/Debug/main.exe; F=10,15,20,25,30,45,60,300,900
# pixel parity: the D3D9 reference, three Diligent runs, then per run the parity gate with heat maps
MSYS_NO_PATHCONV=1 python scripts/port/gfx_capture.py --exe $EXE --frames $F --runs 1 --pace 0 --lock <lock> --out <dir>/d3d9
MSYS_NO_PATHCONV=1 python scripts/port/gfx_capture.py --exe $EXE --gal diligent:d3d11 --frames $F --runs 3 --pace 0 --lock <lock> \
    --out <dir>/dil -- /galreport <dir>/dil/report.json /galreportframe 900 /galdebuglayerselftest
python scripts/port/gfx_capture.py parity <dir>/d3d9/run1 <dir>/dil/run1 --out <dir>/parity1     # run2, run3 alike
# GetTexture2D, per mode (oracle, portable): dump what the menu decodes, compare with D3DX on a HAL device
MSYS_NO_PATHCONV=1 python scripts/port/gfx_capture.py --exe $EXE --gal diligent:d3d11 --frames 60,300,900 --runs 1 --pace 0 --lock <lock> \
    --out <dir>/cap-portable -- /galdumptex <dir>/tex-portable /galtex2d portable
python port/graphics/diligent/tools/tex2d_test.py --dump <dir>/tex-portable --out <dir>/tex2d-portable
# effects: FXC, glslang SPIR-V + spirv-val, and every menu pass rendered by D3D9 and by Diligent-D3D11
python scripts/port/fxdiff.py --lock <lock> --work <scratch dir> --build buildstage/fxdiff
```

The `/galreport` JSON holds what the gates read besides the pixels: `debugLayer` and `diligentMessages`
(errors, warnings, the self test), `drawPath` (draws, skips, PSOs), `uploads` (`mapDiscard`,
`mapNoOverwrite`, `updateBuffer`, `updateBufferInFrame`, `updateTexture`, `createdOffRenderThread`),
`effectLayer` (where the shaders come from, programs and shaders compiled, generation failures,
constant uploads) and `texture2D` (mode, calls, off-thread calls). `tex2d_test.py` creates a hidden
D3D9 HAL device, so run it while no other GUI run holds the lock. `fxdiff.py` needs a short `--build`
path: CMake's try-compile fails under a deep scratch directory (MAX_PATH).

Found by measurement in M6b, and reproduced by the backend:

- **D3D9 point sampling picks texel floor(u x size) exactly** (0 of 388,598 samples deviate); this
  GPU's D3D11 deviates on 691. The generated shaders sample the texel centre instead.
- **D3D9 samples cube maps face by face with clamping**, while D3D10+ filters across faces. The
  backend samples a 2D-array view with a CLAMP sampler and the hardware LOD; the face-edge difference
  drops from 0.41 to 0.0036.
- **`sqrt`, `rsqrt` and `log` take |x|** in the legacy compiler's code, `pow` too unless the exponent
  is an integer literal, and `normalize(0)` is 0.
- **D3DX rounds a block-compressed image up to whole blocks**: a 10x18 DXT5 file comes back 12x20, a
  1x1 one 4x4. Such files are decoded and re-encoded, not copied.
- **D3DX's DXT encoder depends on the x87 precision**: 487 of the 8861 `/textures/ui` files encode to
  different blocks under `_PC_24` (the engine's main thread, and what D3D9's `CreateDevice` leaves)
  and `_PC_53`. The menu calls `GetTexture2D` and `CreateTexture` only on the render thread.
- **D3D9's half-pixel offset** can be reproduced in the vertex shader (the default) or by offsetting
  the viewport (`/galhalfpixel viewport`); both give D3D9's pixels, and without it 250,268 pixels of
  frame 60 differ.

Not covered yet, and where it belongs:

- Rendering on Vulkan and GL (step 6). Every menu stage already compiles to valid SPIR-V.
- The in-game effects, informational from the same tools: FXC compiles all 341 generated stages of
  the 11 FAF effects; glslang compiles 291 of 315, and the 24 failures are FAF terrain's overloaded
  `PBR`/`splatLerp` calls (the typed emitter of step 11); `fxdiff` agrees on 595 of 862 pass renders,
  fully on frame, particle, primbatcher, ui, vision, sky and cartographic.
- With `fxdiff --seed 2` and `--seed 3`, one pixel of one point-sampled pass takes the next coarser
  mip level than D3D9 (within the gate's 0.1 % outlier rule).
- Release|Win32 and x64 builds of the backend (see above).

## The plan

From the M6 review (2026-10-07). Durations are for one engineer.

| Step | What | Gate |
|---|---|---|
| 0 | D3D9 reference harness | done (above) |
| 1 | Diligent spike | done (above) |
| 2 | Front-end metadata | done (above) |
| 3 (3-4 wk) | Menu draw path on Diligent-D3D11: D3D9 state shadow and PSO cache; dynamic vertex and index buffers through `Map(DISCARD/NO_OVERWRITE)`, re-uploaded on the first map of a frame; the dynamic DXT5 atlas; `GetTexture2D` (D3DX oracle and portable); viewport as scissor; the Win32 cursor; primbatcher, ui and frame as SM5 generated into buildstage; the `TextureContext` pointer rebuilt from `dataArray_` | done (M6b, above): RGB identical to step 0 at frames 60/300/900, or max \|delta\| <= 1 on <= 0.1% of pixels with the heat map; `GetTexture2D` gives D3DX's DXT5 blocks for every menu UI file |
| 4 (2-3 wk) | Swap in the front end's SM5 and metadata; `fxdiff` | done for D3D11 (M6b, above; Vulkan and GL rendering moves to step 6): step 3's images unchanged; `fxdiff` of every primbatcher, ui and frame entry point agrees with the legacy bytecode on Diligent D3D11, Vulkan and GL |
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
  metadata from D3DX, so first pixels (step 3) do not wait for the SM5 emitter. (M6b in the end did
  steps 3 and 4 together: the backend runs on the front end's metadata and SM5 from the start.)

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
| `scripts/port/gfx_capture.py` | Runs the harness: prefs, lock, priority, monitor, comparison and heat maps; `parity`, the step-3 pixel gate |
| `port/graphics/diligent/` | Steps 1 and 3: the Diligent backend (resources, state shadow and PSO cache, draw path, effect layer, `PassBinding.h`, portable `GetTexture2D`, self test); `tools/gate1.py`, `tools/fxtechlist.cpp`, `tools/check_vertex_table.py`, `tools/tex2d_test.{cpp,py}` (the `GetTexture2D` unit test) |
| `scripts/port/build_diligent_win32.ps1` | The Diligent Win32 build, offline |
| `port/graphics/fx/` | Steps 2 and 4: `gpg::gal::fx` with `FxHlslEmitter`, `fxmeta`, `fxhlsl`, `fxd3dx_dump`, `tools/fxdiff/`, unit tests, test effects |
| `scripts/port/fx_metadata_gate.py` | Gate 2 |
| `scripts/port/fxdiff.py` | The step-4 effect gate: FXC, glslang SPIR-V and spirv-val, D3D9 against Diligent-D3D11 renders |
| `src/sdk/gpg/gal/Device.cpp`, `src/sdk/moho/app/CScApp.cpp`, `src/sdk/moho/app/WxRuntimeTypes.cpp`, `src/sdk/main.vcxproj` | The only engine-side edits, all guarded or conditional |
