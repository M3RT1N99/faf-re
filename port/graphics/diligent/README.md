# Diligent gal backend (M6: step 1 the spike, step 3 the menu's draw path, step 6 Vulkan and GL)

`gpg::gal::Device` on [Diligent Engine](https://github.com/DiligentGraphics/DiligentCore) (pinned
1436d1fe), selected with `/gal diligent:d3d11`, `/gal diligent:vk` or `/gal diligent:gl`. It exists only
in the opt-in graphics build of main.exe (`/p:FafPortGraphics=true`, `port/graphics/port_graphics.props`);
the default main.exe does not contain it. The plan it belongs to is in `docs/port/renderer.md`.

Since M6b it draws FAF's main menu for real, and every captured frame is byte-identical to the D3D9
backend's: frames 10-45, 60, 300 and 900 in three runs, and all frames 1-540 in one run
(`scripts/port/gfx_capture.py parity`, docs/port/renderer.md "Steps 3 and 4"). Since M6c the same holds on
Vulkan and OpenGL (Windows, the same graphics main.exe): frames 10-900 byte-identical to D3D9 in three
runs each, and the scripted navigation into the skirmish lobby
(`port/graphics/capture/scripts/skirmish_lobby.lua`) within the parity rule on both
([Vulkan and OpenGL](#vulkan-and-opengl-m6c-step-6)).

## What is real

| Part | Where |
| --- | --- |
| D3D11, Vulkan or OpenGL device, immediate context, swap chain on the engine window (head 0's viewport, or its frame when full screen, as D3D9 picks it), RGBA8 head render target | `DiligentHost.cpp` |
| `Present`: a full-screen triangle draws the head into the back buffer, then the swap chain presents (a draw, not a copy: a BGRA swap chain and Android pre-rotation need it, m6u-CRIT.txt R11; the present pipeline takes the swap chain's actual format) | `DiligentHost.cpp` |
| Every shader through one route per API: FXC (D3D11), Diligent's glslang (Vulkan), glslang + SPIRV-Cross to GLSL (GL) | `ShaderCompileDiligent.*` |
| The GL conventions (NDC z, render-target row order), once for GL and GLES | `DiligentHost.h` `FlipsRenderTargets`, `ShaderCompileDiligent.cpp`, `PipelineDiligent.cpp` |
| Vulkan render-pass counters by phase (volk hooks), the GL debug output, Diligent's command counters | `DiligentHost.cpp` |
| The device creation seam for a decorator Device (galtrace) and for tools without `/gal` | `DeviceFactory.h`, `DeviceDiligent.cpp` |
| Textures: the image the D3DX texel oracle decodes, as a Diligent texture with the full mip chain, created on the render thread at first use; dirty-rectangle uploads after `Lock`; L8, A8L8, R8G8B8, X1R5G5B5, A4R4G4B4, A2R10G10B10 converted on upload; the `TextureContext` data pointer rebuilt from `dataArray_` (m6u-CRIT R9) | `ResourcesDiligent.cpp`, `DiligentHost.cpp` |
| Render, cube and depth targets (depth with the shader-readable flag) | `ResourcesDiligent.cpp` |
| Vertex and index buffers with a CPU shadow: dynamic ones through `Map(DISCARD)` / `Map(NO_OVERWRITE)` as D3D9 locks them, rewritten whole on the first map of a frame (Vulkan and D3D12 discard dynamic memory at frame end); static ones created from the shadow. Never `UpdateBuffer` in a frame (m6u-CRIT R2) | `ResourcesDiligent.cpp`, `DiligentHost.cpp` |
| Vertex formats: the D3D9 backend's table (`VertexFormatTableD3D9.inl`) as input layouts on `PassBinding.h`'s attribute slots | `ResourcesDiligent.cpp` |
| The D3D9 render-state shadow: creation defaults plus `InitState` (D3D9Interfaces.cpp:5889), the technique-begin reset (slot 48, 0x00946260), the pass's states, the colour-write, wireframe and fog slots | `PipelineDiligent.cpp` |
| A hashed PSO cache: program id, normalised blend/depth/stencil/raster state, input layout, target formats, topology | `PipelineDiligent.cpp` |
| The draw path: target binding, viewport, `Clear` (a quad when the viewport is smaller than the target), `DrawPrimitive`/`DrawIndexedPrimitive`, `StretchRect` (copy or bilinear blit), `ClearTarget` (resets the viewport when the colour target changes, as `IDirect3DDevice9::SetRenderTarget` does) | `PipelineDiligent.cpp`, `DeviceDiligent.cpp` |
| Readbacks: `GetRenderTargetData` of any render target into the system-memory texture in D3D9's byte order (the step-0 harness captures through it), `UpdateSurface`, the Save slots | `DeviceDiligent.cpp`, `DiligentHost.cpp` |
| Effects: gal `Effect`/`EffectTechnique`/`EffectVariable` on the portable front end (metadata from `FxMetadata`, shaders from `FxHlslEmitter`, compiled when a pass is first drawn); setters with `ID3DXEffect` semantics into CPU shadows; constants through `Map(DISCARD)` only; immutable samplers from `sampler_state`; a (0, 0, 0, 1) null texture for unbound textures | `EffectsDiligent.cpp`, `EffectsDiligentGpu.cpp` |
| `GetTexture2D`: D3DX on the NULLREF device (`oracle`, default) or without D3DX (`portable`, the Android path) | `DeviceDiligent.cpp`, `Texture2DPortable.cpp` |
| The Win32 cursor, built from the cursor texture as the D3D10 backend's `CursorD3D10` does | `DeviceDiligent.cpp` |
| Device loss and Reset on resize | `DeviceDiligent.cpp`, `DiligentHost.cpp` |
| Capability lanes and adapter modes: D3D9 values from `IDirect3D9` HAL queries (no device) | `D3D9Oracle.cpp` |

All 50 Device slots are overridden; the report counts every slot's calls (the 900-frame menu run calls 28
of them: 7192 draws, 3 pipeline states).

### The contract between the effect layer and the draw path: `PassBinding.h`

Interface version 1, forward declarations only (no Diligent, boost or msvc8 types), written first so both
sides could build against it:

- the vertex attribute slot of each D3D9 usage (`VertexAttributeSlotOf`, 15 slots) and how each element
  is fed (`AttribKind`/`VertexInputDesc`): D3DCOLOR as UNORM8 swizzled `.bgra` in the shader (Diligent's
  input layouts have no BGRA format), UBYTE4/SHORT as integers, POSITIONT meaning the fixed-function
  vertex shader;
- `PassStateAssignment`: the pass's render and texture-stage states in D3DX order;
- `PassProgram`: vertex and pixel shader, one resource signature, a unique program id (part of the PSO key);
- `DrawState`: viewport, half-pixel offset, the raw alpha-test enable/function/reference, frame index;
- `PassBinding` (`GetStates`, `GetProgram`, `Commit`), `PassSink` (`OnBeginPass`/`OnEndPass`, which
  `DeviceDiligent` implements), `SetPassSink`, `SetEffectRenderDevice`, and `ShaderResourceSource`
  (implemented by the texture and target classes).

## Engine hooks

Inside `#if defined(FAF_PORT_GRAPHICS_DILIGENT)`, unchanged since M6a:
- `src/sdk/gpg/gal/Device.cpp`, `Device::Create`: the `DeviceApiDiligent` case.
- `src/sdk/moho/app/CScApp.cpp`, `CScApp::CreateDevice`: `/gal diligent:<api>` sets the context's API
  (and its fallback's), and the Win32 cursor as `/D3D10` does.

Vulkan and GL needed no new hook: the API is the backend's own option (`/gal diligent:vk|gl`).

`DeviceFactory.h` (M6c) is the seam a decorator Device uses (galtrace's recorder, `port/graphics/trace`):
`SetDeviceDecorator` installs it, `CreateDevice` returns `decorator->Wrap(backend)`, `SetupDevice` sets up
the backend and then calls `decorator->OnSetup(installed, backend, context)`. The engine deletes what it
installed, so the wrapper owns the backend. `CreateDeviceForApi(api)` makes a device for a tool that has no
`/gal` (galplay), and `GetBackendDevice` finds the backend behind a wrapper. The decorator also answers two
queries: `BackendOf(installed)` (the backend behind its wrapper) and `ResolveInstance(installed)` (what
`Device::GetInstance` returns: the wrapper for the engine, the backend while the wrapper runs a forwarded
call). The header is header-only and free of Diligent types, so any graphics-build TU can include it.

Since M6c the D3D9 backend can be wrapped too. `src/sdk/gpg/gal/Device.cpp` includes `DeviceFactory.h` and
has three hunks, all inside `#if defined(FAF_PORT_GRAPHICS)`:
- `Device::Create`, D3D9 case: `DecorateCreatedDevice(device)` is installed, the backend is set up, then
  `NotifyDeviceSetup`;
- `Device::GetInstance` returns `ResolveActiveInstance(sDeviceD3D.get())`: the D3D9 backend
  static_casts `GetInstance()` to `DeviceD3D9`, which must stay the backend inside a forwarded call;
- `SupportsVertexTextureFormat` casts `BackendOfInstalled(GetInstance())`, not the wrapper.

Without a decorator (every run without `/galtrace`) all three answer what the default build does. The
default main.exe compiles `Device.cpp` to the same object as before (docs/port/renderer.md, "The default
main.exe does not change").

`DeviceApiDiligent` is `static_cast<DeviceApi>(3)` in `GalDiligent.h`, not a new enumerator, so that
`DeviceContext.hpp` and its forty-odd includers stay untouched in the default build.

## Options (read by the backend)

| Option | Effect |
| --- | --- |
| `/gal diligent:d3d11\|vk\|gl` | select the backend and its API (`vulkan`, `opengl` work too; anything else throws gal::Error) |
| `/galreport <file.json>` | write the report (below) |
| `/galreportframe <N>` | write the report after present N and keep running (for runs under the frame harness, which ends the process) |
| `/galexitframes <N>` | write the report after present N and ask the app to exit |
| `/galdumpfx <dir>` | write each effect source buffer the engine passes to CreateEffect, with its macros (game data: scratch only) |
| `/galdumptex <dir>` | write every `GetTexture2D` and `CreateTexture` source, and each `GetTexture2D` result, for the unit test (game data: scratch only) |
| `/galtex2d oracle\|portable` | `GetTexture2D` through D3DX (default) or the portable decoder and encoder |
| `/galhalfpixel shader\|viewport\|none` | how D3D9's pixel centres are reproduced: an offset in the vertex shader (default), an offset viewport, or not at all (a mutation for the gates) |
| `/galselftest` | at setup, 12 known-answer checks of the paths the menu does not reach (viewport clear, readback, StretchRect copy/scale/rects, texture uploads, L8, dynamic and static buffers, cube faces, readable depth, reset) |
| `/galdebuglayerselftest` | provoke one D3D11 debug-layer error at setup (on GL: one GL debug-output error), not counted, to show the layer is live |
| `/galnovalidation` | no Diligent validation, no D3D11 debug layer, no Vulkan validation layer request, no GL debug context |
| `/galglnomirror` | diagnostic: GL without the mirror convention (below); its frames come back upside down |
| `/galswapchainbgra` | diagnostic: ask for a BGRA8 swap chain (the R11 path; this machine gives RGBA8 unless asked) |
| `/galtrilinear d3d9\|aniso1\|aniso2\|aniso16\|mippoint\|level1\|bias64` | diagnostic: how the effect layer's trilinear samplers (min/mag/mip LINEAR) are made; `d3d9` (the default) as D3D9 has them. The other modes isolate the D3D11 trilinear finding below |

The report: adapter, swap-chain format, presents, the half-pixel mode, `texture2D` (mode, calls, calls off
the render thread), `debugLayer` and `diligentMessages` (counts, first messages, the self test),
`selfTest`, `drawPath` (draws, skips by reason, clears, blits, PSO lookups/creations/failures),
`uploads` (`mapDiscard`, `mapNoOverwrite`, `mapFrameRestore`, `updateBuffer`, `updateBufferInFrame`,
`updateTexture`, objects created, objects created and released off the render thread), `effectLayer`
(shader source, programs and shaders compiled, generation failures, constant uploads, commits), the
DeviceContext, the per-slot call counts and every effect with its technique list. Since M6c also: `api`;
`drawPath.pipelines` (every PSO created, with its key); `glDebugOutput` (the GL debug output by message
type, and the first messages); `glShaderRoute` (shaders through glslang and SPIRV-Cross, failures);
`vulkanLayers` (the instance layers the loader offers this 32-bit process, and whether the Khronos
validation layer is among them); `renderPass` (Vulkan render-pass scopes, and the ends inside draw slots by
cause, below); `contextStats` (Diligent's `DeviceContextStats` command counters).

The backend does nothing about the window: without the frame harness (`/galharness`) main.exe shows
and activates its window as usual. On a machine someone is using, run it through `gate1.py` or
`scripts/port/gfx_capture.py`, with the shared GUI lock.

## Gates

```
msbuild src\sdk\main.vcxproj /p:Configuration=Debug /p:Platform=Win32 /p:FafPortGraphics=true
python port/graphics/diligent/tools/gate1.py --exe output/main-gfx/Win32/Debug/main.exe --lock <lock> --out <scratch>/gate1
python port/graphics/diligent/tools/tex2d_test.py --dump <dir of a /galdumptex run> --out <scratch>/tex2d
```

The pixel-parity and effect gates (`gfx_capture.py parity`, `scripts/port/fxdiff.py`) are described in
docs/port/renderer.md, "Steps 3 and 4". Step 6 runs the same parity gate per API, and the lobby navigation:

```bash
EXE=output/main-gfx/Win32/Debug/main.exe; F=10,15,20,25,30,45,60,300,900
MSYS_NO_PATHCONV=1 python scripts/port/gfx_capture.py --exe $EXE --frames $F --runs 1 --pace 0 --lock <lock> --out <dir>/d3d9
for api in vk gl; do   # report outside --out (gfx_capture.py writes its own report.json there)
  MSYS_NO_PATHCONV=1 python scripts/port/gfx_capture.py --exe $EXE --gal diligent:$api --frames $F --runs 3 --pace 0 --lock <lock> \
      --out <dir>/$api -- /galreport <dir>/report-$api.json /galreportframe 900 /galdebuglayerselftest
  python scripts/port/gfx_capture.py parity <dir>/d3d9/run1 <dir>/$api/run1 --out <dir>/parity-$api   # run2, run3 alike
done
# menu -> skirmish lobby, the same rule; LF=60,90,95,100,105,110,115,120,135,150,180,240,300,450,600
SC=port/graphics/capture/scripts/skirmish_lobby.lua
MSYS_NO_PATHCONV=1 python scripts/port/gfx_capture.py --exe $EXE --script $SC --frames $LF --runs 2 --pace 0 --lock <lock> --out <dir>/lobby-d3d9
MSYS_NO_PATHCONV=1 python scripts/port/gfx_capture.py --exe $EXE --gal diligent:vk --script $SC --frames $LF --runs 1 --pace 0 --lock <lock> \
    --out <dir>/lobby-vk -- /galreport <dir>/report-lobby-vk.json /galreportframe 600
python scripts/port/gfx_capture.py parity <dir>/lobby-d3d9/run1 <dir>/lobby-vk/run1 --out <dir>/parity-lobby-vk
```

What the report must show besides the pixels: `diligentMessages.error` 0; GL `glDebugOutput` errors and
undefined behaviour 0 with the self test's 1 provoked error; Vulkan `renderPass.endsInDrawByMap`,
`endsInDrawByCommit` and `endsInDrawOther` 0; `uploads.updateBuffer` 0 and `createdOffRenderThread` 0;
`drawPath.psoFailed` 0. The known-answer self test (`/galselftest`) passes 12/12 on all three APIs.

- **Gate 1** (`tools/gate1.py`) runs main.exe under the step-0 frame harness with `/gal diligent:d3d11`
  for 900 frames and checks: the main menu module loaded and 900 frames rendered and presented; the
  D3D11 debug layer active and live with 0 errors and 0 corruption, and no Diligent error; every
  effect's technique list equal to a D3D9 HAL device's (`tools/fxtechlist.cpp`, built by the script).
  `tools/check_vertex_table.py` checks that `VertexFormatTableD3D9.inl` is still the D3D9 backend's
  table.
- **`GetTexture2D`** (`tools/tex2d_test.py`, `tools/tex2d_test.cpp`): every source a run fed
  `GetTexture2D` (`/galdumptex`), or every image under a directory, is decoded three ways: the D3D9
  backend's code on a D3D9 HAL device (the reference), the same calls on a NULLREF device (the oracle
  mode), and `Texture2DPortable`. The DXT5 blocks, the sizes and the engine's own outputs are compared
  byte for byte. The menu decodes 20 `/textures/ui` files (DXT5 in whole blocks); all 20 are equal in
  both modes. The tool creates a hidden D3D9 HAL device: run it while no other GUI run holds the lock.

## Vulkan and OpenGL (M6c, step 6)

`/gal diligent:vk` and `/gal diligent:gl` run the same backend on Diligent's Vulkan and OpenGL engines, in the
same Win32 graphics main.exe (the Vulkan loader `SysWOW64\vulkan-1.dll` with the driver's 32-bit ICD, and
`opengl32.dll` with the driver's GL). Only device and swap-chain creation, the shader route and the GL
conventions differ per API; resources, the D3D9 state shadow, the PSO cache, the effect layer and the
readbacks are shared.

**Shaders** (`ShaderCompileDiligent.*`). D3D11 compiles the generated SM5 with FXC as before (unchanged).
Vulkan lets Diligent compile the same HLSL with glslang; Diligent maps the `ATTRIBn` inputs itself. For GL,
Diligent's own HLSL route is a text converter that needs combined samplers and does no type checking
(m6u-DIL.txt 9), so the backend takes Vulkan's front end instead: `GLSLangUtils::HLSLtoSPIRV` (in
DiligentCore.lib), then SPIRV-Cross to GLSL, handed to Diligent verbatim:
- `ATTRIBn` vertex inputs get location n, from the semantic glslang keeps (SPV_GOOGLE_hlsl_functionality1);
- bindings and descriptor sets are removed, since Diligent's GL backend binds uniform blocks and texture
  units itself;
- one combined sampler per (texture, sampler) pair, named `<texture>_s_<sampler>` for effect shaders (made
  GLSL-safe: SPIRV-Cross rewrites reserved `__`), and the effect layer's GL signature lists those pairs with
  `PIPELINE_RESOURCE_FLAG_COMBINED_SAMPLER` and the pair's `sampler_state` as immutable sampler;
- separable programs (`SeparablePrograms` enabled): each generated stage declares only the `FxGenParams`
  members it reads, which a single linked GL program rejects ("member names and types must match").

**The GL conventions** (m6u-CRIT.txt R4; `DiligentHost.h` `FlipsRenderTargets`). GL's NDC z runs -1..1 without
clip control, and its framebuffer rows count from the bottom. Rather than flip every sampled render target,
the backend renders every target mirrored, so that texture memory keeps D3D's row order everywhere:
- every vertex shader ends with `gl_Position.y = -y` and `z = 2z - w` (SPIRV-Cross `flip_vert_y`,
  `fixup_clipspace`); clip control stays off, as on GLES devices without `GL_EXT_clip_control`;
- viewports and scissor rectangles go to Diligent mirrored (`top = height - bottom`), because Diligent's GL
  backend converts D3D's top-left origin itself;
- front faces are counter-clockwise (the mirror turns the winding around);
- Present reads the head's rows bottom-up into the window, GL's only y-up framebuffer.

Uploads, copies, sampling and readbacks then need nothing: memory row 0 is the top. The Android GLES path
inherits this unchanged. `/galglnomirror` turns the mirror off as a control: the menu's frames then come
back exactly upside down (frames 30, 60 and 300 equal the D3D9 frames flipped, 0 pixels differ), and
parity fails with 204,402-326,026 pixels.

**GL threads.** Diligent's GL device creates resources on its context's thread only (m6u-CRIT.txt R3). The
backend creates textures and buffers lazily on the render thread already; GPU objects dropped on another
thread wait for the render thread (`GpuShared::Retire`, drained at Present). Both are counted
(`uploads.createdOffRenderThread`, `releasedOffRenderThread`): 0 and 0 in the menu and the lobby.

**Vulkan render passes** (m6u-CRIT.txt R2). The backend counts `vkCmdBegin/EndRenderPass` and
`vkCmdBegin/EndRendering(KHR)` by swapping volk's function pointers after device creation, and attributes each
end inside a draw slot (`DeviceDiligent::SubmitDraw`: the stream buffers' maps, the effect's constants and
bindings, the draw) to its innermost phase: a texture created or written at bind time, a target switch, the
buffer maps, the commit, or anything else. New and rewritten textures are transitioned to the sampled state
inside their upload (otherwise the first commit's barrier ends the pass). Menu, 900 frames: 1813 passes,
7192 draws; 10 ends inside draw slots, all 10 texture uploads (the batcher's DXT5 atlas and first-use
textures); 0 by buffer maps, 0 by commits, 0 other. The primbatcher's DISCARD locks never end a render pass.

**BGRA swap chains.** Present draws into whatever format the swap chain got (on this machine Vulkan gives
RGBA8, as asked); readback swaps red and blue for RGBA targets only.

**Validation.** Diligent's validation is on in every run. D3D11 has its debug layer as before. GL runs on a
debug context with the backend's own synchronous `glDebugMessageCallback`, counted by type; the self test
(`/galdebuglayerselftest`) provokes one `GL_INVALID_ENUM`, which arrives. Vulkan asks for
`VK_LAYER_KHRONOS_validation`, but this machine has no 32-bit Khronos validation layer (no Vulkan SDK; the
loader offers AMD, Steam, Galaxy, EOS, Medal and Rockstar layers, `report.vulkanLayers`); Diligent logs that
as its one warning and continues. Nothing was downloaded.

## Decisions and measurements

**D3D11 blends mip levels differently on this AMD driver** (M6c). In the skirmish lobby the 256x256 map preview
is drawn at 200x198 px with primbatcher's trilinear `LinearSampler`. Vulkan and GL match D3D9 there; D3D11
differs on 7,655 pixels by up to 3. With `/galtrilinear mippoint` (nearest level) and `level1` (level 1 only)
D3D11 and Vulkan give identical frames, so both levels sample alike and only the blend between them differs.
Fitting `out = l0 + w (l1 - l0)` over the preview gives w = 97/256 for D3D9 and Vulkan and 88/256 = 11/32 for
D3D11 (the LOD is log2(256/198) = 0.3706: D3D11 truncates its fraction to 1/32). Anisotropic x1 gives D3D11's
own result again, x2 and x16 differ more, and `bias64` (MipLODBias +1/64) brings D3D11 to max |delta| 1 on
3,282 pixels: D3D9's fraction is finer than any constant bias can reproduce. The driver's precision is not
an API setting, so D3D11 does not pass the lobby gate on this machine; the menu (no minified textures) is
unaffected.

**The PSO cache key had two bytes of padding** (M6c). `FixedState`'s `int32 depthBias` starts at offset 28, after
26 bytes of `uint8` states; the two bytes between were padding, undetermined in an aggregate's `{}` and copied
into the key, so equal states hashed differently. The lobby made the same pipeline 6-8 times per run (the
number varied per run and per API). They are named members now; every run makes 1 PSO for the menu and the
lobby.

**Effect metadata, step 1 (Windows oracle mode).** Each effect was compiled as
`DeviceD3D9::CreateEffectFromSourceBuffer` compiles it and created on a **NULLREF** device. Measured on
this machine's d3d9.dll: a NULLREF device creates effects and textures and answers `GetPassDesc`, but
`FindNextValidTechnique`/`ValidateTechnique` crash in d3d9.dll (access violation reading 0), and a
device-less `ID3DXEffectCompiler` returns no shader bytecode. So the technique list is computed by the
backend: the techniques in declaration order, keeping those whose passes' vertex and pixel shader
versions are within the HAL caps. Gate 1 checks the result against a D3D9 HAL device; all 11 FAF effects
agree (cartographic 4, frame 18, mesh 180, particle 68, primbatcher 14, range 1, sky 4, terrain 68, ui 6,
vision 3, water2 5). This PC's HAL rejects none of them, so the rule itself is not exercised yet. Since
M6b the metadata comes from the portable front end (`FxMetadata`, equal to D3DX for every variant,
gate 2); only the caps still come from the oracle.

**Capabilities.** The DeviceContext gets the D3D9 backend's values (`DeviceD3D9::BuildDeviceCapabilities`)
from IDirect3D9 HAL queries, so the engine takes the same fidelity paths. Two lanes are derived rather
than asked: the shader profile tokens (exact for shader model 3.0 parts, the base 2.0 profile below) and
hardware instancing (the D3DRS_POINTSIZE probe needs a HAL device).

**Debug layer.** `/galdebuglayerselftest` asks the native device for a zero-sized buffer at setup; the
runtime refuses it and the layer reports two errors, which are cleared and not counted. An invalid *draw*
is not a safe probe: on this machine (AMD RX 9070 XT) one removed the device
(`DXGI_ERROR_DRIVER_INTERNAL_ERROR`), after which every Present timed out.

**x87 precision.** The engine runs under `_PC_24`. Diligent device, shader and pipeline creation run
under `_PC_53` (`ScopedDefaultFpu`), and the control word is put back afterwards. `GetTexture2D` runs
under the caller's precision, as the D3D9 backend's does, because D3DX's DXT encoder is x87-sensitive:
487 of the 8861 `/textures/ui` files encode to different blocks under `_PC_24` and `_PC_53`. The menu
calls `GetTexture2D` and `CreateTexture` only on the render thread (the report counts calls off it: 0),
and none of its 20 files (whole-block DXT5) gives different D3DX blocks under the two precisions.

**D3DX rounds block-compressed images up to whole blocks.** A 10x18 DXT5 file comes back 12x20, a 1x1
one 4x4. `Texture2DPortable` therefore copies blocks only when the file holds whole blocks, and decodes
and re-encodes everything else.

**Half-pixel offset.** D3D9 samples pixel centres at integer coordinates. Both the vertex-shader offset
(default) and the offset viewport give D3D9's pixels in every captured frame; without either, 250,268
pixels of frame 60 differ.

**Diligent build.** `scripts/port/build_diligent_win32.ps1` builds the pin into
`buildstage/diligent/install/Win32`, which the props link. It is the recipe of the older
`dependencies/DiligentCore/install/Win32` (made by the `feature/diligent-renderer` branch's
`bootstrap_diligent.py`, not in this repository) without its two downloads: abseil-cpp comes from
`dependencies/abseil-cpp`, and NVAPI stays off. The older install's Debug `DiligentCore.lib` references
`NvAPI_*` but ships no `nvapi.lib`, so it cannot be linked cleanly.

## Not yet

- A Vulkan validation-layer run: needs a 32-bit `VK_LAYER_KHRONOS_validation` (a Vulkan SDK install).
- D3D11's trilinear blend (above): matching D3D9 would take an explicit two-level blend in the generated
  pixel shader for D3D11.
- GLES specifics the Windows GL run does not exercise: GLSL ES (SPIRV-Cross targets ES from the device type;
  untested), a GLES device without separate shader objects (GLES 3.0 without the extension), `layout(offset)`
  in uniform blocks (desktop GL 4.4+; the generated `FxGenParams` packing needs it unless it is std140), the
  cube-face 2D-array view (`glTextureView`, optional on GLES). The z remap is applied but no depth-tested draw
  in the menu or the lobby checks it.
- `fxdiff` renders on Vulkan and GL: `fxdiff` still renders D3D9 against Diligent-D3D11 only.
- Release|Win32: Diligent's Release libraries are built with `/GL`, and main.vcxproj's Release link runs
  with `/FORCE` and without LTCG, so the props leave the backend out of Release gfx builds (the capture
  harness still builds there). A Release backend needs a Diligent build without `/GL`.
- x64: no Diligent install for it yet.
- In-game paths the menu does not reach: `gpg/gal/MeshVertex.cpp` `GetHardwareVertexFormatter` and
  `HardwareMeshBatch.cpp:1135` (`!= Direct3D10`) need `DeviceApiDiligent` cases before mesh draws.
