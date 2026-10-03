# Cross-platform renderer port

Port work on top of the recovered engine. Nothing here is recovered from the binary: the
D3D9 backend stays the 1:1 reference, new code sits next to it.

## Direction

- **Renderer:** a new `gpg::gal` backend on [Diligent Engine](https://github.com/DiligentGraphics/DiligentCore)
  (D3D11/12, Vulkan, Metal, OpenGL/GLES, WebGL, WebGPU). Diligent compiles HLSL at runtime on
  every backend, so shaders can stay in the game data (FAF and mods ship and change `.fx` files).
- **Platform layer:** SDL3 (window, input incl. touch, audio device) to replace wxWidgets 2.4.2 / Win32
  later; XACT audio via FAudio.
- Alternatives considered: SDL3 GPU (no GL/GLES/WebGPU backend), bgfx (own shader language, offline
  compiled shaders), raw Vulkan/Metal (too much work), DXVK-native (Linux only).

Bigger blockers than the renderer for mobile targets: the recovered code is pinned to 32-bit MSVC8
layouts (arm64 needs 64-bit clean code), the simulation relies on x87 floating point (desyncs against
PC players on ARM), and the Windows-only platform layer.

## Dependencies

`dependencies\DiligentCore` is a pinned, gitignored upstream checkout. The Android bootstrap
script fetches revision `1436d1fea00763178ae835fd36b065efb31d96e` and initializes the required
shader/Vulkan submodules; googletest is not needed for the port target.

## Backend scope

A new backend implements the `gpg::gal` interfaces the D3D9 and D3D10 backends implement:
`Device` (~50 virtual slots, several still unnamed `purecallN` / `FuncN` - name them from the
two existing backends first), `Texture`, `RenderTarget`, `CubeRenderTarget`, `DepthStencilTarget`,
`VertexBuffer`, `IndexBuffer`, `VertexFormat`, `PipelineState`, and the effect layer `Effect` /
`EffectTechnique` / `EffectVariable` (35 methods), which sits on top of the `gpg::gal::fx` output.
`DeviceD3D10` is the closer template: state objects, no fixed function, DXGI formats.

## Roadmap

1. Measure a Release build first; the Debug build is `/Od` and the engine is CPU bound.
2. Route the ten files that call D3D9 / D3DX directly through `gpg::gal`
   (`CD3DDevice`, `Shadow`, `CRenFrame`, `Cartographic`, `HighFidelityTerrain`, `Mesh`,
   `MeshThumbnailRenderer`, `WxRuntimeTypes`).
3. Implement the **effect front end** (`gpg::gal::fx`): D3DX effects to Shader Model 5 HLSL plus metadata.
4. Diligent backend on Windows, compared pixel by pixel against the D3D9 backend.
5. SDL3 platform layer, then Linux.

## Android bring-up

The first Android target is under `port/android`. It uses Android `NativeActivity`
and DiligentCore's Vulkan backend, builds for `arm64-v8a`, creates a swap chain,
and clears the screen. It packages a signed debug APK; this proves the NDK and
graphics backend build and initialize together, but it is not yet the game engine.

```powershell
powershell -ExecutionPolicy Bypass -File scripts/port/bootstrap_android.ps1
powershell -ExecutionPolicy Bypass -File scripts/port/build_android.ps1
```

The build needs Android NDK 29, platform/build-tools 35, host CMake 3.22 or newer,
Ninja, and a JDK. `ANDROID_SDK_ROOT` selects a non-default SDK location.

The native library and APK are written to `buildstage/android/`. The APK uses a
local debug key generated on first build; it is for development installs only.
The current Android target does not yet compile recovered simulation or gameplay
code: the engine still depends on Win32, wxWidgets, 32-bit MSVC layouts, and x87
simulation behavior. The next steps are asset access, portable simulation and
platform seams, and a GAL implementation that can consume the effect conversion
pipeline.

## Planned effect front end: `gpg::gal::fx`

The planned `src/sdk/gpg/gal/fx/` module will read an effect the way `CD3DEffectTechnique` feeds D3DX (compat header
`d3d9states.compat` prepended) and produces SM5 HLSL with one entry point per pass stage, plus JSON
metadata (parameters with defaults and annotations, textures, samplers and their states, techniques,
passes with render states, alpha test, entry points with their vertex inputs).

Pipeline: `FxLexer` -> `FxPreprocessor` (defines, `##`, `#if`, hide sets) -> `FxEffectParser`
-> `FxHlslEmitter` / `FxMetadata`; `FxConverter::ConvertEffect` runs all of it.

D3D9 behavior the generated code keeps:

| D3D9 | SM5 translation |
| --- | --- |
| `sampler2D` + `tex2D*` / `tex1D*` / `tex3D*` / `texCUBE*` | `Texture*` + `SamplerState`, `Sample` / `SampleBias` / `SampleLevel` / `SampleGrad`; 1D textures declared as 2D (GLES has no 1D) |
| sampler passed to a function | split into texture and sampler parameters, overloads resolved by arity |
| texture parameter bound to samplers of different types | one resource per type (`name`, `name_3D`, ...) |
| `compile vs_1_1 VS(true, 2)` uniform arguments | wrapper entry point binding the literals |
| vertex `COLOR` outputs clamped to [0, 1] | `saturate` in the vertex wrapper |
| `VPOS` / `VFACE` | `SV_Position.xy - 0.5` / `SV_IsFrontFace ? 1 : -1` |
| fixed-function alpha test (`AlphaTestEnable`, `AlphaFunc`, `AlphaRef`) | `discard` in the pixel wrapper |
| `PixelShader = null` | pixel stage forwarding `COLOR0` |
| `FIXED_FUNC_VS` (pre-transformed full-screen quads) | the effects' own `FixedFuncVS` like `d3d10states.compat`; a generated one using `FxFixedFunction.fxw_ScreenSize` where the effect has none |
| shader code writing a uniform | `static` copy initialized at every entry point |
| `float x = f(x.y)` reading the parameter `x` | local renamed (old compiler scoping) |

### Planned tools

```
msbuild src\fxconv\fxconv.vcxproj /p:Configuration=Release /p:Platform=Win32
output\fxconv\Win32\Release\fxconv.exe --compat d3d9states.compat --out <dir> <effect.fx>...
python scripts\port\fx_validate.py <effects.nx2> <effects.scd>
```

The validation tool will extract effects from local game archives (nothing is copied into the
repository), convert them and compile every entry point with FXC (`vs_5_0` / `ps_5_0`).
