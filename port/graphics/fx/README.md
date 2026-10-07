# port/graphics/fx — the effect front end (`gpg::gal::fx`)

The game's effects (`/effects/*.fx`, HLSL in the Direct3D 9 effect format)
have so far only been read by D3DX. This directory reads them without D3DX,
in portable C++17: the effect *metadata*, everything `ID3DXEffect` reports
about an effect and the D3D9 backend's effect layer uses (`Effect` /
`EffectTechnique` / `EffectVariable`, 35 slots; step 2, milestone M6a), and
since M6b the *shaders*: `FxHlslEmitter` generates Shader Model 5 HLSL for
every pass stage (step 4; the renderer track is in
[docs/port/renderer.md](../../../docs/port/renderer.md)). The Diligent
backend uses both at run time: the graphics props
(`port/graphics/port_graphics.props`) compile `src/*.cpp` into the graphics
`main.exe`. The default `main.exe` does not contain any of it.

| Path | What it is |
|---|---|
| `include/gpg/gal/fx/`, `src/` | The library `faf_gal_fx` (namespace `gpg::gal::fx`), no platform dependencies |
| `FxSource` | The merged source (compat prelude + .fx, as the engine concatenates them), positions, diagnostics |
| `FxLexer` | Preprocessing tokens: line splices, comments, pp-numbers, punctuators |
| `FxPreprocessor` | Object- and function-like macros with hide sets, `#`, `##`, `#if`/`#elif` expressions with `defined`, caller macros (`EffectContext::mMacros`); `#include` is an error |
| `FxEffectParser`, `FxAst` | The fx_2_0 effect grammar: globals with modifiers, semantics, annotations, initializers and `sampler_state`; structs, typedefs, `vector<>`/`matrix<>`; techniques and passes; `compile`, `asm`, `null`. Function bodies are skipped |
| `FxStates` | The effect states: names, D3D9 ids, value kinds and value names, as D3DX maps them |
| `FxMetadata` | `BuildEffectMetadata()`: parameters (D3DX descs, default values, strings, annotations, struct members, sampler states), techniques, passes (states in D3DX order, shader entry points), technique validity per device profile |
| `FxJson` | The JSON schema `faf-fx-metadata/1` shared with the D3DX dumper |
| `FxHlslEmitter` | SM5 HLSL for one pass stage plus its binding metadata (constant layout, textures and samplers, vertex inputs, varyings); [below](#sm5-generation-fxhlslemitter) |
| `tools/fxmeta.cpp` | Host tool: an effect's metadata as JSON (or its preprocessed text) |
| `tools/fxhlsl.cpp` | Host tool: the generated SM5 of every pass stage of an effect, optionally compiled with FXC |
| `tools/fxd3dx_dump.cpp`, `tools/FakeD3D9Device.*` | The oracle: the same JSON from D3DX (Win32, DirectX SDK `d3dx9.lib`) |
| `tools/fxdiff/` | `fxdiff`: every pass rendered by D3D9 and by the Diligent backend's effect layer on D3D11, and every stage through Diligent's HLSL to SPIR-V path (its own CMake project, Win32 Debug) |
| `tests/fx_tests.cpp`, `tests/fx_hlsl_tests.cpp` | Unit tests (CTest): metadata (88 checks), SM5 generation (84 checks) |
| `tests/effects/*.fx` | Effects written for the gate: grammar, constant folding, states, validity, macros |
| [`../../../scripts/port/fx_metadata_gate.py`](../../../scripts/port/fx_metadata_gate.py) | Gate 2: front end against D3DX on every effect the game and FAF load |
| [`../../../scripts/port/fxdiff.py`](../../../scripts/port/fxdiff.py) | The step-4 gate: FXC, glslang SPIR-V plus spirv-val, and `fxdiff` renders for the menu effects |

## Use

```cpp
#include "gpg/gal/fx/FxMetadata.h"

gpg::gal::fx::EffectInput input;
input.parts.emplace_back("d3d9states.compat", compatText); // as CD3DEffect::InitEffectFromFile merges them
input.parts.emplace_back("mesh.fx", effectText);
input.macros.push_back({"FAF_BONE_TEXTURE", "1"});         // EffectContext::mMacros, if any
const gpg::gal::fx::FrontEndResult result = gpg::gal::fx::BuildEffectMetadata(input);
if (!result.ok) {
  log(gpg::gal::fx::FormatDiagnostics(result.source, result.diagnostics)); // "mesh.fx(12,5) [merged line 409]: error: ..."
}
for (const auto& technique : result.metadata.techniques) {
  if (gpg::gal::fx::IsTechniqueValid(technique, profile)) { /* GetTechniques() lists it */ }
}
```

## Build and run

```powershell
cmake -S port/graphics/fx -B buildstage/fx/win32 -A Win32      # Win32 like main.exe, for the D3DX dumper
cmake --build buildstage/fx/win32 --config Release
ctest --test-dir buildstage/fx/win32 -C Release --output-on-failure

buildstage\fx\win32\Release\fxmeta.exe --compat <dir>\d3d9states.compat <dir>\mesh.fx --out mesh.json
buildstage\fx\win32\Release\fxmeta.exe --preprocess --compat <dir>\d3d9states.compat <dir>\mesh.fx
buildstage\fx\win32\Release\fxd3dx_dump.exe --compat <dir>\d3d9states.compat <dir>\mesh.fx --out mesh.d3dx.json
buildstage\fx\win32\Release\fxhlsl.exe --compat <dir>\d3d9states.compat --out <scratch>\hlsl --fxc <dir>\primbatcher.fx
```

`fxmeta` and `fxd3dx_dump` take `--compat FILE`, `-D NAME[=VALUE]` and
`--out FILE`; they exit 0 on success, 1 when the effect has errors (printed to
stderr), 2 on bad usage. `fxd3dx_dump` also takes `--hal-caps` (validity with
this PC's adapter caps, see below) and `--x87 24|53|64`. `fxhlsl` writes each
unique stage as `<effect>.<stage><n>.hlsl` with an `index.json`, for the
techniques valid on the `all` profile (`--all-techniques` for every one);
`--fxc` compiles each with `d3dcompiler_47` as `vs_5_0`/`ps_5_0`.

The library builds with any C++17 compiler; the gate's `--arm64-check`
compiles it (now with `FxHlslEmitter`, 9 sources) and links `fxmeta` for
`aarch64-linux-android26` with the NDK (`-Wall -Wextra -Wshadow -Werror`).

## The gate

```powershell
python scripts/port/fx_metadata_gate.py --arm64-check
```

1. Finds the effect archives in SCFA's gamedata and FAF's
   (`C:\ProgramData\FAForever\gamedata`), reads them and extracts every
   `effects/*.fx` and `effects/d3d9states.compat` into `--work` (default
   `buildstage/fx-gate`; never the repository).
2. Forms the variants: compat + .fx. An archive without a compat gets the one
   FAF mounts (`effects.nx2`). Byte-identical variants are checked once
   (`effects.nx5` = `effects.nx2`). That gives the 26 variants of the M6
   inventory: 11 SCFA (`effects.scd`), 11 FAF 3839 (`effects.nx2`), the
   Nomads `particle.fx` (`effects.nmd`), 3 from `faforever.faf`. The six
   `tests/effects` files are added.
3. Builds this directory (`--build`, default `buildstage/fx/win32`) and runs
   the unit tests.
4. Runs both tools on every variant and compares the JSON:
   - parameters: order, name, semantic, class, type, rows, columns, elements,
     struct members, flags, bytes; default values word for word; strings;
     annotations with their values; struct member descs; the sampler_state of
     every sampler a pass uses (texture and states in order);
   - techniques: name, annotations, validity on three device profiles and
     the `FindNextValidTechnique` order;
   - passes: name, annotations, the states D3DX applies at `BeginPass`, in
     order, as (op, index, state, DWORD); vertex and pixel shader: not
     assigned, `null`, or bytecode version and entry point; sampler
     arguments (`PrimBatcherPS(LinearSampler)`) against the shader's
     constant table.

Result on 2026-10-07 (d3dx9_43 + the legacy d3dx9_31 compiler, fresh build):

```
variants: 32 (26 from the game archives, 6 synthetic), failed: 0
compared: 891 techniques (validity on 3 device profiles), 1018 passes, 6172 pass states,
2016 shader slots (16 sampler arguments), 1036 parameters, 623 default values,
2240 annotations, 205 of 216 sampler_state blocks (the rest no pass uses)
GATE PASSED
```

FAF 3839 alone: 371 techniques, 431 passes, the counts of the M6 inventory.
`fxmeta` takes 0.12 s for FAF's `mesh.fx` (301 KB), `fxd3dx_dump` 2.1 s.

Not checked against D3DX: uniform arguments other than samplers (they are
folded into the bytecode) and the sampler_state of the 11 samplers no pass
uses (D3DX applies sampler states only for samplers a shader binds).

## The oracle: `fxd3dx_dump`

It makes the calls `DeviceD3D9::CreateEffectFromSourceBuffer` makes
(`D3D9Interfaces.cpp:1522-1600`, 0x008F09A0) with the same `d3dx9.lib` as
`main.exe` (DirectX SDK June 2010, `d3dx9_43.dll`) and the same flags,
`D3DXSHADER_DEBUG | D3DXSHADER_USE_LEGACY_D3DX9_31_DLL` (plus
`D3DXSHADER_AVOID_FLOW_CONTROL` with `FAF_BONE_TEXTURE`), so the legacy
October 2006 compiler in `d3dx9_31.dll` does the work, as in the game.

`D3DXCreateEffect` needs a device. `FakeD3D9Device` is an `IDirect3DDevice9`
that renders nothing: no GPU, window or desktop session is needed, and no
other D3D application can be disturbed (in the M6 inventory run neither a
HAL nor a NULLREF device could be created). Measured on it:

- `D3DXCreateEffect` creates every pass shader through the device and reads
  each back with `GetFunction`.
- `ValidateTechnique` records each pass into a state block and calls
  `GetDeviceCaps` and `ValidateDevice`, but the shader versions in the caps
  do not decide: with caps saying ps_2_0 and every shader accepted, all 180
  `mesh.fx` techniques are valid; with ps_2_x shaders refused, 109 are. A
  technique is valid when the device created every shader of its passes.
  The fake device therefore refuses shaders above a profile — `all` (3.0),
  `sm2` (2.0; `ps_2_a`/`ps_2_b` compile to 2.1) and `sm1` (vs 1.1 / ps 1.4) —
  and reports generous caps otherwise. `--hal-caps` uses the real adapter's
  caps (only `IDirect3D9::GetDeviceCaps`, no device): on this PC's AMD Radeon
  RX 9070 XT (vs 3.0 / ps 3.0) every technique of every variant is valid,
  as in `all`.
- Pass states are read through an `ID3DXEffectStateManager`:
  `Begin(D3DXFX_DONOTSAVESTATE)` as `D3D9Interfaces.cpp:4102`, then
  `BeginPass`. One fake texture per texture parameter makes `SetTexture`
  name its parameter. `SetTexture`/`SetSamplerState` calls for a register
  the pass shader's constant table gives a sampler parameter are that
  sampler's states.
- Entry points come from the `D3DXSHADER_DEBUG` information in the bytecode
  (a `DBUG` comment block; the tenth header DWORD is the offset of the entry
  point name).
- The x87 precision does not matter: `main.exe` compiles effects under
  `_PC_24` (its device is created without `D3DCREATE_FPU_PRESERVE`, flags 0x44
  at `D3D9Interfaces.cpp:1772`, and `WinApp.cpp:2773` sets `_PC_24`); the dumper
  runs under `_PC_24` too, and `_PC_53` / `_PC_64` give identical output for
  all 37 effect files of the corpus.

## D3DX behaviour the front end reproduces

All measured with the dumper; `tests/effects` and `tests/fx_tests.cpp` keep
each one checked.

- **Constant folding is double precision.** Literals are not rounded to
  float, neither are `float3(...)` arguments, casts or `static const`
  values a later initializer reads; a value is rounded once, when it is
  stored. `16777216.0 + 1.0 - 16777216.0` is 1, `0.1 + 0.2 == 0.3` is false.
  Of 300 random `normalize(float2/3/4(...))` initializers only double
  evaluation reproduces all; the variants with a single-precision step
  (literals rounded to float, a float dot product, a float square root or
  reciprocal) got 72 to 192 of them right. water2.fx's `SunColor` is one that
  needs double precision. Integers are 32-bit and wrap
  (`2147483647 + 2` is -2147483647), `/` and `%` truncate, a float given to
  an int truncates.
- **Intrinsics in initializers** fold too, with the legacy compiler's quirks:
  `length(v)` folds to `|v.x|` and `distance(a, b)` to `|b.x - a.x|`
  (`length(float2(3, 4))` is 3), `round(x)` is `floor(x + 0.5)`,
  `radians`/`degrees` use pi rounded to float, and `sqrt`/`log`/`asin`/`acos`
  outside their domain are compile errors.
- **Parameter descs:** every matrix is `D3DXPC_MATRIX_ROWS`, `row_major` and
  `column_major` alike, with its values in written order; `float1` is a
  vector of one; `half` and `double` are 4-byte floats; samplers report 0
  bytes, other objects a 4-byte pointer (Win32); `shared` sets flag 1 and
  every annotation carries flag 4 (`D3DX_PARAMETER_ANNOTATION`); a scalar
  initializer fills a vector (`float4 v = 0.5;`); `static` globals are not
  parameters.
- **States:** a state assigned twice in one pass or `sampler_state` keeps
  only the later assignment, at the later position (mesh.fx
  `FakeRingsNoDepth` assigns `ZEnable` twice). D3DX sends
  `CCW_StencilFail` to `D3DRS_STENCILFAIL` (53) and `BlendOpAlpha` to
  `D3DRS_BLENDOP` (171) instead of their own states. So in the D3D9 game
  range.fx and vision.fx, which use two-sided stencil, end up with
  `StencilFail` set to their CCW value (`StencilFail = zero; ...
  CCW_StencilFail = keep;` leaves KEEP) and `D3DRS_CCW_STENCILFAIL`
  untouched; a backend reproducing D3D9 has to apply the metadata as it is,
  not the source's intent. Value names are per state and case-insensitive;
  the 519 names in `FxStates.cpp` each give D3DX's value, and the names D3DX
  refuses (`SrcColor2`, `Convolutionmono`, colour factors for
  `Src/DestBlendAlpha`, `DebugMonitorToken`'s) are refused. A float given to a
  DWORD state truncates (`AlphaRef = 0.5` sets 0), an int given to a float
  state converts, a `float4` given to a colour state becomes a D3DCOLOR
  (`BorderColor = float4(1, 0.5, 0, 1)` sets 0xFFFF8000), bool states keep
  integers (`AlphaTestEnable = 3` sets 3), and `|` combines value names
  (`ColorWriteEnable = RED | GREEN`). A value in parentheses that reads a
  parameter (`FogStart = (P)`) is re-evaluated at `BeginPass`; the metadata
  holds its value at the default and marks it `dynamic`.

## SM5 generation: `FxHlslEmitter`

`HlslEmitter::Create(input, metadata)` once per effect, then per pass stage
`EmitPixelShader` first and `EmitVertexShader` for the vertex layout the
draw uses, so the vertex shader writes exactly the varyings the pixel shader
reads (`EmitFixedFunctionVertexShader` / `EmitFixedFunctionPixelShader` for
`VertexShader = null`, POSITIONT and `PixelShader = null`). Each result is
one SM5 shader with the entry point `main` and what a backend binds for it:
the `FxGenParams` constant layout, the texture resources and samplers with
their `sampler_state`, the vertex inputs by attribute slot, the varyings. Only
what the stage's entry point reaches is emitted (m6u-CRIT R5), so a function
a stage does not call cannot fail it. The Diligent backend calls it at run
time when a pass is first drawn (`port/graphics/diligent/EffectsDiligentGpu.cpp`).

The D3D9 behaviour it keeps (the header has the full list with the reasons):

- **Samplers and textures.** A D3D9 sampler becomes an SM5 `SamplerState`
  plus one texture resource per dimension it is sampled with, named after its
  texture parameter; sampler parameters of helper functions are split too,
  uniform sampler arguments of `compile` specialise the entry function,
  `tex*proj/lod/bias/grad` are translated, and `tex1D` samples a 2D texture at
  v = 0.5.
- **Constants.** One `FxGenParams` cbuffer at fixed `packoffset` registers
  (the effect's own layout, so every stage of an effect shares one buffer),
  `row_major` matrices; struct parameters padded so every member starts a
  register (sky.fx `aCirrus`); a parameter the code writes is read through a
  static copy (water2.fx:443), since SM5 cannot write a cbuffer.
- **Stage interface.** `ATTRIB<n>` inputs with the D3DCOLOR `.bgra` swizzle,
  missing inputs read (0, 0, 0, 1); vs_1_1 `COLOR` outputs saturated; the
  canonical varyings; D3D9's half-pixel offset added to the clip-space
  position; the fixed-function alpha test as `discard` driven by the draw
  constants; a D3D9-exact fixed-function vertex shader for POSITIONT and
  `VertexShader = null`, and a fixed-function pixel shader for `PixelShader = null`.
- **Measured with `fxdiff` and emulated** (renders of D3D9 and of the generated
  code on the same GPU):
  - D3D9 point sampling picks texel floor(u x size) exactly (0 of 388,598
    samples deviate); this GPU's D3D11 deviates on 691. The generated code
    samples the texel centre.
  - D3D9 samples cube maps face by face with clamping; D3D10+ filters across
    faces. The generated code samples a 2D-array view with a CLAMP sampler and
    the hardware LOD (the face-edge difference drops from 0.41 to 0.0036).
  - `sqrt`, `rsqrt` and `log` take |x|; `pow` takes |x| unless the exponent is
    an integer literal; `normalize(0)` returns 0.

Coverage: all 11 FAF 3839 effects generate (341 unique stages) and FXC
compiles all 341 as `vs_5_0`/`ps_5_0`. glslang (Diligent's HLSL to SPIR-V path
for Vulkan) compiles every stage of the menu effects; across all effects it
compiles 291 of 315, the 24 failures being FAF terrain's overloaded `PBR` and
`splatLerp` calls, which need the typed emitter (m6u-CRIT R5).

### The step-4 gate: `fxdiff`

```powershell
python scripts/port/fxdiff.py --lock <the shared GUI lock> --work <scratch dir> --build buildstage/fxdiff
```

For primbatcher, ui and frame of FAF's `effects.nx2` (`--effects` for
others): every generated stage compiles with FXC; every stage goes through
Diligent's HLSL to SPIR-V path and passes `spirv-val --target-env vulkan1.0`
(the NDK's); and `tools/fxdiff` draws every pass of every valid technique
with D3D9 (D3DX and the legacy compiler on a HAL device, as the game) and with
the Diligent backend's own effect layer on D3D11, same parameters, textures
and vertices, on a float target (shader arithmetic, |delta| <= 1/255) and an
8-bit target with the pass's alpha test (coverage identical, 1 LSB). A pass
render passes when no pixel is drawn by one side only and at most 0.1 % of
its drawn pixels exceed the tolerance. The D3D9 side needs a HAL device, so
the render step takes the GUI lock, runs at below-normal priority, and is
killed if one of its windows becomes visible. `--build` needs a short path
(CMake's try-compile fails under a deep scratch directory).

Result on 2026-10-07 (M6b integration): FXC 39/39 stages (primbatcher 14, ui
6, frame 19), glslang 39/39, spirv-val 39/39; 78 of 78 pass renders agree, max
|delta| 0.0022 on the float target and 1 LSB on the 8-bit target; `FXDIFF GATE:
PASSED`. `FXDIFF_MUTATE=swizzle|offset|alpharef` breaks one D3D9 rule on the
Diligent side to show the comparison is not vacuous (18/30, 30/30 and 7/30
renders then differ). With `--seed 2` and `--seed 3` one pixel of one
point-sampled pass takes the next coarser mip level than D3D9 (within the 0.1 %
rule; open).

## Limits

- Function bodies are not parsed into a typed tree: `FxHlslEmitter` rewrites
  the token stream of the functions a stage reaches, and no types are checked
  beyond what metadata needs. A typed emitter (explicit conversions, resolved
  overloads) is needed before glslang accepts the in-game effects.
- Not supported (an error, so a future FAF effect cannot slip through
  silently): multi-dimensional arrays, `Sampler[n]` pass states, light,
  material, transform, FVF and shader-constant states, `#include`.
  `#pragma` and `#line` are ignored with a warning.
- A few forms D3DX refuses are accepted, e.g. a bare parameter name as a
  state value (`FogStart = P;` instead of `FogStart = (P);`) or a bare
  texture name in `sampler_state`. Shipped effects are D3DX-valid (the game
  dies on any effect D3DX rejects, `CD3DDeviceResources.cpp:805-842`), so this
  only matters for new effects.
