# port/graphics/fx — the effect front end (`gpg::gal::fx`)

The game's effects (`/effects/*.fx`, HLSL in the Direct3D 9 effect format)
have so far only been read by D3DX. This directory reads them without D3DX,
in portable C++17, as far as the effect *metadata*: everything
`ID3DXEffect` reports about an effect and the D3D9 backend's effect layer
uses (`Effect` / `EffectTechnique` / `EffectVariable`, 35 slots). It is step 2
of milestone M6a (the renderer track in [docs/port](../../../docs/port/)); shader
code generation (type checking, SM5 and GLSL emitters: `FxSema` /
`FxHlslEmitter` in [docs/port/README.md](../../../docs/port/README.md)) builds
on it later. Nothing here is part of
`main.vcxproj`: the engine build is unchanged.

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
| `tools/fxmeta.cpp` | Host tool: an effect's metadata as JSON (or its preprocessed text) |
| `tools/fxd3dx_dump.cpp`, `tools/FakeD3D9Device.*` | The oracle: the same JSON from D3DX (Win32, DirectX SDK `d3dx9.lib`) |
| `tests/fx_tests.cpp` | Unit tests (CTest) |
| `tests/effects/*.fx` | Effects written for the gate: grammar, constant folding, states, validity, macros |
| [`../../../scripts/port/fx_metadata_gate.py`](../../../scripts/port/fx_metadata_gate.py) | The gate: front end against D3DX on every effect the game and FAF load |

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
```

`fxmeta` and `fxd3dx_dump` take `--compat FILE`, `-D NAME[=VALUE]` and
`--out FILE`; they exit 0 on success, 1 when the effect has errors (printed to
stderr), 2 on bad usage. `fxd3dx_dump` also takes `--hal-caps` (validity with
this PC's adapter caps, see below) and `--x87 24|53|64`.

The library builds with any C++17 compiler; the gate's `--arm64-check`
compiles it and links `fxmeta` for `aarch64-linux-android26` with the NDK
(`-Wall -Wextra -Wshadow -Werror`).

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

## Limits

- Function bodies are not parsed; no types are checked beyond what metadata
  needs. Shader code generation is the next step of the plan.
- Not supported (an error, so a future FAF effect cannot slip through
  silently): multi-dimensional arrays, `Sampler[n]` pass states, light,
  material, transform, FVF and shader-constant states, `#include`.
  `#pragma` and `#line` are ignored with a warning.
- A few forms D3DX refuses are accepted, e.g. a bare parameter name as a
  state value (`FogStart = P;` instead of `FogStart = (P);`) or a bare
  texture name in `sampler_state`. Shipped effects are D3DX-valid (the game
  dies on any effect D3DX rejects, `CD3DDeviceResources.cpp:805-842`), so this
  only matters for new effects.
