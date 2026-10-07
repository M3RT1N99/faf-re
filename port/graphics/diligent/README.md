# Diligent gal backend (M6: step 1 the spike, step 3 the menu's draw path)

`gpg::gal::Device` on [Diligent Engine](https://github.com/DiligentGraphics/DiligentCore) (pinned
1436d1fe), selected with `/gal diligent:d3d11`. It exists only in the opt-in graphics build of main.exe
(`/p:FafPortGraphics=true`, `port/graphics/port_graphics.props`); the default main.exe does not contain
it. The plan it belongs to is in `docs/port/renderer.md`.

Since M6b it draws FAF's main menu for real, and every captured frame is byte-identical to the D3D9
backend's: frames 10-45, 60, 300 and 900 in three runs, and all frames 1-540 in one run
(`scripts/port/gfx_capture.py parity`, docs/port/renderer.md "Steps 3 and 4").

## What is real

| Part | Where |
| --- | --- |
| D3D11 device, immediate context, swap chain on the engine window (head 0's viewport, or its frame when full screen, as D3D9 picks it), RGBA8 head render target | `DiligentHost.cpp` |
| `Present`: a full-screen triangle draws the head into the back buffer, then the swap chain presents (a draw, not a copy: a BGRA swap chain and Android pre-rotation need it, m6u-CRIT.txt R11) | `DiligentHost.cpp` |
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

All inside `#if defined(FAF_PORT_GRAPHICS_DILIGENT)`, unchanged since M6a:
- `src/sdk/gpg/gal/Device.cpp`, `Device::Create`: the `DeviceApiDiligent` case.
- `src/sdk/moho/app/CScApp.cpp`, `CScApp::CreateDevice`: `/gal diligent:<api>` sets the context's API
  (and its fallback's), and the Win32 cursor as `/D3D10` does.

`DeviceApiDiligent` is `static_cast<DeviceApi>(3)` in `GalDiligent.h`, not a new enumerator, so that
`DeviceContext.hpp` and its forty-odd includers stay untouched in the default build.

## Options (read by the backend)

| Option | Effect |
| --- | --- |
| `/gal diligent:d3d11` | select the backend (only `d3d11` so far; others throw gal::Error) |
| `/galreport <file.json>` | write the report (below) |
| `/galreportframe <N>` | write the report after present N and keep running (for runs under the frame harness, which ends the process) |
| `/galexitframes <N>` | write the report after present N and ask the app to exit |
| `/galdumpfx <dir>` | write each effect source buffer the engine passes to CreateEffect, with its macros (game data: scratch only) |
| `/galdumptex <dir>` | write every `GetTexture2D` and `CreateTexture` source, and each `GetTexture2D` result, for the unit test (game data: scratch only) |
| `/galtex2d oracle\|portable` | `GetTexture2D` through D3DX (default) or the portable decoder and encoder |
| `/galhalfpixel shader\|viewport\|none` | how D3D9's pixel centres are reproduced: an offset in the vertex shader (default), an offset viewport, or not at all (a mutation for the gates) |
| `/galselftest` | at setup, 12 known-answer checks of the paths the menu does not reach (viewport clear, readback, StretchRect copy/scale/rects, texture uploads, L8, dynamic and static buffers, cube faces, readable depth, reset) |
| `/galdebuglayerselftest` | provoke one D3D11 debug-layer error at setup (not counted), to show the layer is live |
| `/galnovalidation` | no Diligent validation and no D3D11 debug layer |

The report: adapter, swap-chain format, presents, the half-pixel mode, `texture2D` (mode, calls, calls off
the render thread), `debugLayer` and `diligentMessages` (counts, first messages, the self test),
`selfTest`, `drawPath` (draws, skips by reason, clears, blits, PSO lookups/creations/failures),
`uploads` (`mapDiscard`, `mapNoOverwrite`, `mapFrameRestore`, `updateBuffer`, `updateBufferInFrame`,
`updateTexture`, objects created, objects created off the render thread), `effectLayer` (shader source,
programs and shaders compiled, generation failures, constant uploads, commits), the DeviceContext, the
per-slot call counts and every effect with its technique list.

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
docs/port/renderer.md, "Steps 3 and 4".

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

## Decisions and measurements

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

- Vulkan and GL (`diligent:vk`, `diligent:gl`; step 6). Every menu shader stage already compiles to
  valid SPIR-V (`scripts/port/fxdiff.py`).
- Release|Win32: Diligent's Release libraries are built with `/GL`, and main.vcxproj's Release link runs
  with `/FORCE` and without LTCG, so the props leave the backend out of Release gfx builds (the capture
  harness still builds there). A Release backend needs a Diligent build without `/GL`.
- x64: no Diligent install for it yet.
- In-game paths the menu does not reach: `gpg/gal/MeshVertex.cpp` `GetHardwareVertexFormatter` and
  `HardwareMeshBatch.cpp:1135` (`!= Direct3D10`) need `DeviceApiDiligent` cases before mesh draws.
