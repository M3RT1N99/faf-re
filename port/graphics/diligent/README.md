# Diligent gal backend (M6, step 1: the integration spike)

`gpg::gal::Device` on [Diligent Engine](https://github.com/DiligentGraphics/DiligentCore) (pinned
1436d1fe), selected with `/gal diligent:d3d11`. It exists only in the opt-in graphics build of main.exe
(`/p:FafPortGraphics=true`, `port/graphics/port_graphics.props`); the default main.exe does not contain
it. The plan it belongs to is in `docs/port/renderer.md`.

## What step 1 does

| Part | State | Where |
| --- | --- | --- |
| D3D11 device, immediate context, swap chain on the engine window (head 0's viewport, or its frame when full screen, as D3D9 picks it) | real | `DiligentHost.cpp` |
| Head render target (RGBA8_UNORM, render target + shader resource) | real | `DiligentHost.cpp` |
| `ClearTarget` + `Clear` on the head | real | `DeviceDiligent.cpp` |
| `Present`: a full-screen triangle draws the head into the back buffer, then the swap chain presents (a draw, not a copy: a BGRA swap chain and Android pre-rotation need it, m6u-CRIT.txt R11) | real | `DiligentHost.cpp` |
| DeviceContext capability lanes, adapter modes | D3D9 values, from `IDirect3D9` HAL queries (no device) | `D3D9Oracle.cpp` |
| Effects | metadata only, from D3DX reflection | `EffectsDiligent.cpp` |
| Textures, `GetTexture2D` | D3DX textures in the scratch/system-memory pool of a NULLREF device | `ResourcesDiligent.cpp`, `D3D9Oracle.cpp` |
| Render, cube and depth targets; vertex and index buffers; vertex formats; pipeline state | placeholders (contexts, CPU memory, D3D9 strides) | `ResourcesDiligent.cpp` |
| `GetRenderTargetData` of the head (CopyTexture into a staging texture, read into the system-memory A8R8G8B8 texture in D3D9's byte order), so the frame harness captures this backend through gal as it does D3D9 | real | `DiligentHost.cpp`, `DeviceDiligent.cpp` |
| Draws, state slots, other readbacks, saves, cursor | logged no-ops (first call of each in the engine log) | `DeviceDiligent.cpp` |

All 50 Device slots are overridden; the report counts every slot's calls, which gives the census of what
the main menu uses (the 900-frame menu run calls 27 of them; draws: 8 `DrawIndexedPrimitive` per frame).

Engine hooks, all inside `#if defined(FAF_PORT_GRAPHICS_DILIGENT)`:
- `src/sdk/gpg/gal/Device.cpp`, `Device::Create`: the `DeviceApiDiligent` case.
- `src/sdk/moho/app/CScApp.cpp`, `CScApp::CreateDevice`: `/gal diligent:<api>` sets the context's API
  (and its fallback's), and the Win32 cursor as `/D3D10` does.

`DeviceApiDiligent` is `static_cast<DeviceApi>(3)` in `GalDiligent.h`, not a new enumerator, so that
`DeviceContext.hpp` and its forty-odd includers stay untouched in the default build.

## Decisions

**Effect metadata (Windows oracle mode).** Each effect is compiled as `DeviceD3D9::CreateEffectFromSourceBuffer`
compiles it (D3DXCreateEffectCompiler with `D3DXSHADER_DEBUG | D3DXSHADER_USE_LEGACY_D3DX9_31_DLL`,
CompileEffect, D3DXCreateEffect) and created on a **NULLREF** device. Measured on this machine's d3d9.dll:
a NULLREF device creates effects and textures and answers `GetPassDesc`, but
`FindNextValidTechnique`/`ValidateTechnique` crash in d3d9.dll (access violation reading 0). A device-less
`ID3DXEffectCompiler` carries the metadata too, but its `GetPassDesc` returns no shader bytecode. So the
technique list is computed by the backend: the techniques in declaration order (the order
`FindNextValidTechnique` walks), keeping those whose passes' vertex and pixel shader versions are within
the HAL caps. Gate 1 checks the result against a D3D9 HAL device's `FindNextValidTechnique` for the same
source bytes: all 11 FAF effects agree (cartographic 4, frame 18, mesh 180, particle 68, primbatcher 14,
range 1, sky 4, terrain 68, ui 6, vision 3, water2 5).

**Capabilities.** The DeviceContext gets the D3D9 backend's values (`DeviceD3D9::BuildDeviceCapabilities`)
from IDirect3D9 HAL queries, so the engine takes the same fidelity paths. Two lanes are derived rather
than asked: the shader profile tokens (exact for shader model 3.0 parts, the base 2.0 profile below) and
hardware instancing (the D3DRS_POINTSIZE probe needs a HAL device).

**Debug layer.** `/galdebuglayerselftest` asks the native device for a zero-sized buffer at setup; the
runtime refuses it and the layer reports two errors, which are cleared and not counted. That shows the
layer is live. An invalid *draw* is not a safe probe: on this machine (AMD RX 9070 XT) one removed the
device (`DXGI_ERROR_DRIVER_INTERNAL_ERROR`), after which every Present timed out.

**Diligent build.** `scripts/port/build_diligent_win32.ps1` builds the pin into
`buildstage/diligent/install/Win32`, which the props link. It is the recipe of the older
`dependencies/DiligentCore/install/Win32` (made by the `feature/diligent-renderer` branch's
`bootstrap_diligent.py`, not in this repository) without its two downloads: abseil-cpp comes from
`dependencies/abseil-cpp`, and NVAPI stays off. The older install's Debug `DiligentCore.lib` references
`NvAPI_*` but ships no `nvapi.lib`, so it cannot be linked cleanly.

## Options (read by the backend)

| Option | Effect |
| --- | --- |
| `/gal diligent:d3d11` | select the backend (only `d3d11` so far; others throw gal::Error) |
| `/galreport <file.json>` | write the report (adapter, debug-layer and Diligent message counts, DeviceContext, slot calls, effects with their technique lists) |
| `/galreportframe <N>` | write the report after present N and keep running (for runs under the frame harness, which ends the process) |
| `/galexitframes <N>` | write the report after present N and ask the app to exit |
| `/galdumpfx <dir>` | write each effect source buffer the engine passes to CreateEffect, with its macros (game data: scratch only) |
| `/galdebuglayerselftest` | the debug-layer self test above |
| `/galnovalidation` | no Diligent validation and no D3D11 debug layer |

The backend does nothing about the window: without the frame harness (`/galharness`) main.exe shows
and activates its window as usual. On a machine someone is using, run it through `gate1.py` or
`scripts/port/gfx_capture.py`.

## Gate 1

```
msbuild src\sdk\main.vcxproj /p:Configuration=Debug /p:Platform=Win32 /p:FafPortGraphics=true
python port/graphics/diligent/tools/gate1.py --exe output/main-gfx/Win32/Debug/main.exe --lock <scratch>/m6a/gui.lock --out <scratch>/gate1
```

`gate1.py` runs main.exe under the step-0 frame harness (`scripts/port/gfx_capture.py`: hidden window,
input dropped, pinned prefs, one GUI run at a time, below-normal priority) with `/gal diligent:d3d11` for
900 frames and checks: the main menu module loaded and 900 frames rendered and presented; the D3D11 debug
layer active and live with 0 errors and 0 corruption, and no Diligent error; every effect's technique list
equal to a D3D9 HAL device's (`tools/fxtechlist.cpp`, built by the script). `tools/check_vertex_table.py`
checks that `VertexFormatTableD3D9.inl` is still the D3D9 backend's table.

## Not yet

- Release|Win32: Diligent's Release libraries are built with `/GL`, and main.vcxproj's Release link runs
  with `/FORCE` and without LTCG, so the props leave the backend out of Release gfx builds (the capture
  harness still builds there). A Release backend needs a Diligent build without `/GL`.
- x64: no Diligent install for it yet.
- Step 3 replaces the placeholders and no-ops (state shadow and PSO cache, dynamic buffers through Map,
  the DXT5 atlas, viewport-limited Clear, the Win32 cursor from the cursor texture).
