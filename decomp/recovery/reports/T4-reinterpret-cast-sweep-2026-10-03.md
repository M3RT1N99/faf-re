# T4 — Residual `reinterpret_cast` sweep, batch 1 (2026-10-03)

Baseline after T1–T3: **3893 occurrences** = **1924 vendored** (`cri/`, `lua/`,
`libpng/`, `zlib/` — verbatim C-library recovery, punning is the original
idiom; class (c) wholesale) + **1969 engine** (`moho/`, `gpg/`, `platform/`,
`util/`, `legacy/`). The DoD target (<500) is therefore an engine-code target.

## Engine cast census (pattern-classified, post batch 2)

1916 engine casts classified by cast-target shape + line context:

| class | count | notes |
|---|---|---|
| typed pun (b) | 696 | onto named structs — reflection `mObj` downcasts, container-node puns, `Stats<T>*`, session map views; each needs its T1 owner pass |
| byte-buffer IO (c) | 349 | `char*`/`void*`/`uint8_t*` cursor + stream IO |
| addr-integer (c) | 254 | `uintptr_t` key/order/compare lanes |
| int-pun (b/c) | 169 | word-lane typed puns outside serialization |
| blob IO (c) | 107 | serialize/archive buffers |
| CRT model (c) | 77 | `CrtRuntimeHelpers` internals |
| boost control-block model (b) | 43 | `SharedCountPair`/`SpCountedImplStorage` overlays |
| intrusive-node pun (b) | 41 | stat/list node reinterpretation |
| win32 handle (c) | 41 | `HANDLE`/`HWND` punning |
| d3d/win API interop (c) | 38 | D3DX/D3D9 parameter marshalling |
| reflection upcast (b/c) | 24 | `upcast.mObj` downcasts (CScriptEvent family) |
| interlocked counter (c) | 19 | `volatile long*` atomics |
| lua interop (c) | 12 | `LexState`/`GCObject` |
| buffer-lock/word-lane/overlay (c/a) | 27 | locks, SIMD words, remaining `*Lane` overlays |

Path to <500: the 915 clear-(c) casts survive classified; the target is met by
converting the (b) mass — dominated by the 696 typed puns — which is T1
owner work directory by directory (WinApp `Legacy*Lane` overlays, CWldSession
session-map views, StatItem node puns, BoostWrappers control-block model,
FastVector raw storage), plus the lane/view overlays (9) already queued.

## Batch 2 addendum (same day, commit 1d8d345f)

Probe purge extended to 37 more files (all remaining `TEMPORARY PROBE`
markers outside in-flight sets): ui/UiRuntimeTypes, render/camera/CameraImpl,
entity/Entity (+ DMGDIAG budget blocks), mesh/HardwareMeshBatch, lua/
{LuaObject,LuaParser}, unit/{CUnitMotion, Unit, UnitWeapon, UserUnit,
tasks/*}, sim/{CWldMap, CIntelGrid, *LuaFunctionRegistrations}, render/
{Shadow, Cartographic, MapImager, CWldTerrainDecal, tess/CTesselator},
audio/{CSndParams, CUserSoundManager}, ai/{CAiSteeringImpl, CAiReconDBImpl,
CAiNavigatorAir}, effects/CEfx{Beam,Emitter}, path/PathTables, terrain/
{Medium,Low}FidelityTerrain, splat/CWldSplat, ui/SelectionDragger,
gpg/{BitArray2D, Logging}. Every file: brace/paren-balanced, normalized
semantic diff vs HEAD **pure-removal**. Marker count **196 → 49**.

Remaining 49 markers:
- `gpg/core/utils/Global.cpp` (18) — heap-corruption triage instrumentation
  (cache registry, watch-lane stamping, `FAF_SYSHEAP` escape modes) interlocked
  with the recovered allocator at ~12 gates and possibly still in active use by
  the triage owner; left deliberately.
- `sim/CWldSession.cpp` (15), `sim/Sim.cpp` (12),
  `effects/CEffectManagerImpl.cpp` (3), `script/CScriptObject.cpp` (1) —
  another agent's in-flight working set.

Markerless rate-limited `[XPORTDIAG]`/`[BOMBDIAG]`/`[SKINDIAG]` budget blocks
(`DiagnosticBudget s*Probe`) also survive in a few unit/script files — they
compile cleanly and belong to the transport/bomber triage sessions; remove
with their owners.

After batches 1–2: **engine 1916**, total 3841.


After batch 1: **engine 1923**, total 3847. Reduction is small because this
batch was spent on (a)-class correctness fixes and the committed-probe purge;
the count falls fast once the (b)-class per-directory passes run.

## (a) vtable/offset-magic fixes (correctness)

| Site | Was | Now | Evidence |
|---|---|---|---|
| `moho/ai/IAiNavigator.h` slot 15 + `CAiNavigatorAir/Land` overrides + `Unit.cpp` shim | `void***` slot-15 dispatch through a local fn-ptr shim; **wrong signature and wrong arg order** (`(goal, out)` vs binary `(out, goal)`) | typed `virtual bool CanPathTo(Wm3::Vector3f* outTargetPos, const SAiNavigatorGoal& goal) const` | FUN_005A49E0 `ret 8`; FUN_006CBD70 pushes NaN out-slot (esp+0x14) first, then goal (esp+0x58) at 0x6CBFB3/0x6CBFC5; FUN_005A6A80 same shape (reuses goal-pos scratch as out slot); FUN_005A3CD0 forwards (goal→0x5ADAD0 arg2, out→arg3) |
| `moho/resource/ISimResources.cpp` `DeleteSimResourcesViaVTable` | `vftable[0](resources, 1)` | `delete resources;` | virtual dtor chain `CSimResources : ISimResources` — slot-0 deleting dtor is exactly what `delete` emits |
| `moho/sim/CArmyImpl.cpp` `CallDeletingDestructorSlot<2>/<0>` + `ReplaceDeletingDtorOwnedPointer` | vtable-slot deleting-dtor templates | `delete *it;` / `delete AiReconDb;` / `delete AiBrain;` / `ReplaceDeleteOwnedPointer` | FUN_006FF9A0 slot-2 (+0x08) for `CPlatoon` (CScriptObject dtor slot 2), slot-0 (+0x00) for `CAiReconDBImpl` (IAiReconDB dtor slot 0); both typed classes declare their dtors at those slots |
| `gpg/core/utils/BoostWrappers.cpp` `InvokeSpCountedDeletingDtorIfPresent` / `DeletePolymorphicSharedCountCtorPointeeOnUnwind` | `vtable[0]` dispatch | `delete control;` / `delete pointer;` | boost 1.34.1 `sp_counted_base`/pointee classes have virtual dtors at slot 0 |
| `moho/misc/CrtRuntimeHelpers.cpp` `EngineReleaseFacetNode` | `owned->vftable[0](owned, 1)` | `delete node->facet;` | `std::locale::facet` virtual dtor; CRT emission shape |
| `util/VTCall.h` | generic vtable-dispatch helper, **zero users** | **file deleted** (with `.vcxproj`/`.filters` entries) | dead generic offset helper |
| `moho/render/camera/CameraImpl.cpp` `Initialize*Vtable` family + 3 aliases + probe classes (103 lines) | vptr store via `*reinterpret_cast<void**>`; **zero callers anywhere** | **removed** — covered by the typed defaulted ctor/dtor emissions of `CameraTimeSource`/`SystemTimeSource`/`GameTimeSource` | zero binary callers (meta), zero source callers; ICF ctor/dtor fragments |
| `moho/terrain/HighFidelityTerrain.cpp` | 47 committed `TEMPORARY PROBE (do not commit)` blocks incl. COM AddRef/Release/LockRect slot dispatches | **429 probe lines purged**; 23→6 casts | markers say "do not commit"; probes were debug scaffolding (decalswap/nodecals/nosplat toggles, [DECALREF]/[DECALDUMP] dumps) |
| `gpg/gal/backends/d3d9/D3D9Interfaces.cpp` | 14 probe markers incl. raw `TestCooperativeLevel` slot-3 dispatch | 198 lines purged; zero markers | same session's scaffolding |
| `gpg/gal/backends/d3d9/StateManagerD3D9.cpp` | probe namespace + [STATEDIAG] hooks | purged; balanced | depends on `gProbeCurrentTechnique` (removed above) |
| `moho/app/WxRuntimeTypes.cpp` | 17 markers: fog/effects/minimap toggles, frame dumper, SKYDIAG steppers | 117 lines purged; semantic rewrites restored (`FogOn(...)`, `RenderEffects(true/false)` unconditional) | same |
| `moho/mesh/Mesh.cpp` | 26 markers: preview-mesh registry, prop-funnel counters, SKINDIAG/LIGHTDIAG | 237 lines purged; ctor/dtor probe inserts removed | same |
| `moho/resource/SScmFile.{h,cpp}` | `&file + 0x40` raw offset walk | named `GetBoneNames(file)` accessor alongside existing `GetVertices`/`GetIndices` blob pattern | file-image blob IO (class c) lifted into the accessor family |

### Real defect found: CanPathTo argument order

The recovered `IAiNavigator::CanPathTo(const SAiNavigatorGoal&)` had the wrong
signature (1 arg, `ret 4` shape) *and* the Unit.cpp shim passed `(goal, out)`.
The binary dispatches `(outTargetPos, goal)`. With the typed fix, the Land
override now forwards its out lane to `CAiPathNavigator::CanPathTo` (which
already had the correct 2-arg form), and Air returns `true` as the binary does.

## Committed-probe debt (cross-file cluster, partially purged)

~350 `TEMPORARY PROBE (do not commit)` / "delete when resolved" markers were
committed across ~50 files by a black-triangle/terrain triage session. The
linkage cluster (`FafProbeFrameSeq/Diag`, `gProbeCurrentTechnique`,
`ProbeNativeTexture1/3`, `HighFidelityProbeToggle`) is fully purged:
D3D9Interfaces, StateManagerD3D9, WxRuntimeTypes, Mesh, HighFidelityTerrain —
zero dangling references tree-wide. **196 markers remain** in ~35 files
(notably `UiRuntimeTypes.cpp` 19, `Global.cpp` 18, `CWldSession.cpp` 15 and
`Sim.cpp` 12 — the latter two are in another agent's in-flight working set and
were deliberately left). Purging the rest is the first action of the next T4
batch.

## (b)/(c) classification begun

- `D3D9Interfaces.cpp` survivors (20 casts): D3DX API interop
  (`D3DXFloat32To16Array`/`D3DMATRIX*`/`D3DXVECTOR4*`), HWND/handle punning,
  lock-buffer `void**` — class (c), the 2007 source had the same casts at the
  D3DX boundary.
- `CrtRuntimeHelpers.cpp` (~245): CRT-internal models (`FILE*` ↔
  `CrtLegacyFile`, locale facet/handle punning, small-block-heap headers,
  `_locale_t`) — external-runtime recovery, class (c).
- `HighFidelityTerrain.cpp` survivors (6): addr-to-int stat keys, `_InterlockedExchangeAdd`
  on stat counters, float-pair reinterpret for shader constants — (c).
- `Mesh.cpp` survivor (1): pointer-order key — (c).
- `SScmFile.*`: file-image blob accessors — (c).

## Verification

- `SScmFile.cpp` compiled in isolation (`cl /c`, project include set):
  **EXIT=0**.
- All other touched TUs are blocked by the 2026-10-03 dependency deletion
  (`dependencies/RESTORE-NEEDED.md`: LuaPlus, wxWindows, WildMagic, zlib gone)
  — full build/scenario gate must re-run after dependency restore.
  Substitute checks per file: brace/paren balance vs HEAD (all zero-delta),
  and normalized semantic diff vs HEAD is **pure deletion** for every purged
  file (zero added/changed lines except the intentional semantic rewrites in
  WxRuntimeTypes and the typed fixes listed above).
- Progress DB updated for FUN_005A3CD0/FUN_005A49E0/FUN_005A6A80/FUN_006CBD70
  (signature correction) and FUN_007A6500/…/FUN_007A7EF0 (covered-by-emission,
  orphan anchors removed).

## Next batches (ordered)

1. Purge remaining 196 probe markers (skip `CWldSession.cpp`, `Sim.cpp`,
   `CEffectManagerImpl.cpp`, `CScriptObject.cpp`, `EntitySetReflection.*`,
   `Projectile.cpp` while in-flight).
2. `moho/app/WinApp.cpp` (52): `LegacyCallbackPayloadLane`/`LegacyTypeInfoLane`
   overlays — T1 owner work (wx type-info adapter ABI), then rewrite to typed
   members.
3. `gpg/core/containers/FastVector.h` (43): raw storage lanes → named container
   members.
4. `moho/misc` remainder, `moho/sim` (after in-flight lands), `moho/movie`
   MPVDecoder (108, mostly SIMD/buffer punning — expect mostly (c)),
   `moho/script`, `moho/net`, etc., 10 files per batch.
5. Fold per-directory survivor appendices into the by-source reports as each
   directory completes.
