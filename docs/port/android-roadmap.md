# Android port: roadmap to full FAF playability

Goal: play Forged Alliance Forever on an Android phone or tablet, natively. That includes online games
against PC players on the normal FAF servers. The engine is ported to arm64; Wine, Box64 and
Winlator are deliberately not used. Deployment is one APK. Game data is never shipped: SCFA comes
from the user's own copy, and the FAF parts come from FAF.

This document records what has to be built, in what order, and what only FAF can decide. It is
based on measurements taken on 2026-10-03; the evidence (commands, file:line references, sweeps)
is summarised under [Evidence](#evidence). Effort figures are person-time for one experienced
engineer. They are estimates, and several rest on premises that are not proven yet. Those
premises are called out where they matter.

## Levels of "playable"

| Level | What works | Gate |
|---|---|---|
| L0 | APK installs, data deployed, FAF's `init_faf.lua` runs on the device, archives mount, a texture from the data is shown | M1 (this branch) |
| L1 | Engine boots to the main menu on the device | engine on arm64 + renderer + platform layer |
| L2 | Offline skirmish against the AI on a tablet with mouse and keyboard | L1 + input + sound + performance basics |
| L3 | FAF replays play back without desync on arm64 | L2 + bit-exact sim + FAF patch parity |
| L4 | Online games against PC players on FAF | L3 + login, lobby, ICE adapter + FAF approvals |
| L5 | Comfortable on a phone (touch HUD, performance tiers) | L4 + touch UX work, partly in FAF Lua |

L4 is the goal. L2 is the first point where the port is fun to use. L3 is the first point where it is
*correct*: until the sim is bit-exact, the port can only play alone.

## Where we stand

- **Engine source (M2 done).** The tree builds as Win32 `main.exe` and, since commit `b67e033d`
  (Draiget), as x64 `main_x64.exe`. `scripts/port/engine_sweep.py` compiles every engine
  translation unit for `aarch64-linux-android`: 276 of 1010 compiled at the start of M2, **896 of
  1010** after it, with the Windows build unchanged (same warnings; 1002 of 1010 MSVC objects
  byte-identical, the other 8 differ only in `__LINE__`). The 114 that still fail are wxWidgets,
  D3D, Win32/Winsock calls and four SIMD headers: platform layer and renderer work. M2 linked
  nothing; M3b links the headless runner (below). See [port/engine/README.md](../../port/engine/README.md).
- **Android app (M1 done).** One APK holds the Java launcher, a `NativeActivity` in its own `:game`
  process, and the native bring-up. A portable core runs FAF's real `init_faf.lua` and mounts the
  `.nx2`/`.scd` archives with the engine's first-mount-wins rules. It then draws the main-menu
  background from the user's SCFA data through Diligent. The core is LuaPlus 1081 plus the VFS,
  zip and DDS code. Confirmed on a Samsung Exynos 2200 (Xclipse 920, Android 16) with both Vulkan
  and OpenGL ES: 17 archives and 38,201 entries mounted, background on screen 100-150 ms after
  start. See [android.md](android.md) and [gamedata.md](gamedata.md).
- **Headless replay runner (M3a done).** `main.exe /headlessreplay <file>` plays a replay with
  no window, renderer, sound or UI and writes a JSON summary of the sim's checksums. Four FAF vault
  replays play to the end (465 to 10,549 beats, 1v1 and 2v2), with identical digests on every
  repeat, threaded or not. That is the first evidence that the recovered sim survives complete
  games. It needed two fixes to recovered code, both checked against the binary: replay acks
  (every replay stalled at beat 0) and the driver's initial focus army (every replay crashed on
  beat 1).

  Checksums cannot match the recordings yet, because the local data is FAF 3839 and the replays
  are 3829/3831. Even so, one replay is bit-identical to the original game for its first 50 beats.
  See [headless-replay.md](headless-replay.md).
- **The runner links for Android (M3b done).** Everything `/headlessreplay` reaches links for
  arm64 and x86_64 as `libfafengine.so` (836 engine and 31 Wild Magic translation units, every
  static initialiser that registers something) plus a small `faf_headless_runner`, with zero
  undefined and zero duplicate symbols. The user side is replaced by stand-ins that return what
  the Windows runner sees, or by traps where it never calls them; the one deliberate difference is
  that Android creates no map textures (no D3D9 device). Windows gives the same checkpoint chains
  as before. See [port/engine/runner/README.md](../../port/engine/runner/README.md).
- **The runner runs on Android (M3c, emulator).** With a low-address arena that keeps every
  pointer below 2 GB (the runner's stand-in for x64's `/LARGEADDRESSAWARE:NO`, never used by the
  APK), the four M3a replays play to the end on the API 36 emulator as x86_64, and T1 as arm64
  under the emulator's ARM translation, with the same checkpoints on both ABIs. Beat 0 (rules and
  blueprints) equals Windows in every replay; the checkpoints diverge from beat 100 (T3: 150), the
  baseline for W5. The registry matches Windows except user-side entries (UI types, User Lua
  binders, a few console commands). Two engine bugs that only Itanium/LP64 builds hit were fixed
  on the way, and the sim was found to read uninitialised heap memory that changes later
  checkpoints. Without the arena the run stops at the first pointer truncation (the category
  universe word), M3d's starting point. The phone run is prepared, not done. See
  [headless-replay.md](headless-replay.md#android-runner-m3c).
- **Not yet:** nothing has run on a real ARM core (the phone run of M3c), and nothing runs without
  the low-address arena (M3d).

## Workstreams

### W1 - Engine on arm64, 64-bit clean (about 3 months)

What the compiler does not catch matters more than what it does.

1. **Guardrails (1-2 weeks).**
   - A CMake target for the engine sources, generated from `src/sdk/main.vcxproj`, building for the
     NDK and for x86_64 clang.
   - Flags: `-ffp-contract=off -fno-fast-math -fsigned-char -pthread`, plus
     `-Werror=pointer-to-int-cast,void-pointer-to-int-cast,int-to-pointer-cast` and
     `-Wshorten-64-to-32` as a report.
   - Change the architecture guards from `_M_X64` to "not `_M_IX86`". Today these sites take the
     x86 branch on aarch64: `Global.cpp:434` (system heap), `X87Math.cpp:5`, `MmxOnX64.h:14`,
     `UiRuntimeTypes.cpp:15238`.
   - An arm64 switch for the layout asserts, plus an always-on wire/file-format assert. The x64
     switch-off is wrong for `wchar_t`/`long` formats.
2. **High-leverage compile fixes (in the sweep, 274 -> 815 TUs).**
   - `platform/Platform.h:35` includes `<immintrin.h>` only on x86.
   - `FastVector.h:2659/2950`: move the default argument to the first declaration.
   - `InstanceCounter.h` drops `<intrin.h>`. The 48 `reinterpret_cast<volatile long*>` Interlocked
     sites move to a 32-bit atomic helper; on LP64 they would otherwise do 8-byte atomics on 4-byte
     fields.
   - A non-Windows `platform/WinTypes.h` with fixed-width types, `LOBYTE` and friends, and CRT
     aliases.
   - `Reflection.h:5908`: two-phase lookup.
3. **Pointer truncation (4-8 weeks).** There is no Android equivalent of
   `/LARGEADDRESSAWARE:NO`. Heap pointers on arm64 are far above 4 GB and carry a tag in the top
   byte, so every place that keeps a pointer in 32 bits crashes.
   - The reflection save/load callback ABI passes objects as `int`
     (`Reflection.h:2220/2223`, 1014 signatures in 373 files). Change it to `void*` with a scripted
     rewrite.
   - Hidden 32-bit pointer words: `EntityCategoryReflection` `mWordUniverseHandle` (dereferenced,
     sim-critical), `BVSet.h:227`, HaStar `Cluster.cpp`, `ResourceManager.cpp`, `CCommandDb`,
     `CWldSession`, the movie decoder and others. The Global allocator's page-owner map covers
     exactly 4 GB.
   - **Test oracle on Windows first:** build x64 with `LargeAddressAware=true`, `/HIGHENTROPYVA`
     and top-down allocation, then fix what breaks there before any device is involved.
4. **ABI traps that compile but misbehave.**
   - `GPG_PREREGISTER_INIT` (593 uses) places pointers in `.CRT$XCL`. On ELF that emits no
     `.init_array` entry, so no pre-registration would ever run. Move it to
     `__attribute__((constructor(prio)))`.
   - Raw vtable calls to MSVC's flagged deleting destructor (`CArmyImpl.cpp:54`,
     `BoostWrappers.cpp:837`, `ISimResources.cpp:6`, ...) are wrong under the Itanium ABI. M3c
     fixed one that corrupted the heap (`Cluster.cpp`'s shared-count release, slots 1/2).
   - Recovered functions written in MSVC's return convention (`RRef* f(RRef* out, ...)` for a
     by-value `RRef`) and cast into by-value function pointers: x86-64 and AArch64 return a small
     trivially copyable struct in registers. M3c fixed the `WrapFile` callbacks in `LuaObject.cpp`.
   - `#pragma pack(4)` puts atomics on misaligned fields, which raises SIGBUS on AArch64.
   - `long` in binary formats (`ReadArchive.cpp:551-565`) and in hashing (`HashMap.h:104`).
   - `wchar_t` in the save-game header (`SaveGameFileHeader.h:34`) and the map preview name
     (`CWldMap.cpp:1505`).
   - Boost 1.34 relies on `std::auto_ptr`; libc++ needs `_LIBCPP_ENABLE_CXX17_REMOVED_FEATURES` or
     a move to `std::thread`.
5. **Exit:** a headless arm64 "replay runner" (sim, ai, unit, entity, lua, serialization,
   gpg/core) pushed to a phone with `adb push` and run from `adb shell`. It plays a vault replay to
   the end. No APK and no renderer are needed for this.

### W2 - Platform layer (2-4 months)

- **Window, event loop, input:** SDL3, replacing wxWidgets 2.4.2 (22 files include it directly,
  62 use it). Do it on Windows first so the D3D9 build stays a reference, then Android.
- **Input seam:** `CMauiWxEventMapper` and `CUIKeyHandler` (Win32 VK codes plus auto-repeat).
  Replace `GetKeyState`, `SetCursorPos`/`GetCursorPos`/`ClipCursor` (mouse scrub, edge scroll)
  with an engine key table and a virtual cursor. Draw a software cursor, because the engine relies
  on the D3D9 hardware cursor. Set `SDL_HINT_ANDROID_TRAP_BACK_BUTTON`.
- **Sockets:** Winsock to BSD sockets (16 files, including the `#error` at
  `CNetUDPConnection.cpp:1331`).
- **Files:** the engine's `CVFSImpl` lower-cases disk paths, which only works on NTFS. It needs
  `stat`/`readdir` and case-insensitive resolution, which `port/native` `FileSystem` already
  implements and is meant to become.
  - Known folders: `SHGetFolderPath` maps to app storage.
  - `DISK_GetLaunchDir` must not use `__argv`.
  - Preferences: the engine writes `Game.prefs` while the VFS looks up `game.prefs`.
- **Threads, time, crash reports:** `dbghelp` -> `libunwind`/tombstones,
  `RaiseException(0x406D1388)` -> `pthread_setname_np`.

### W3 - Renderer (3-6 months)

- A `gpg::gal` backend on Diligent (Vulkan, GLES fallback), as planned in [README.md](README.md),
  with the `gpg::gal::fx` effect front end (D3DX effects to SM5 HLSL). Bring it up on Windows and
  compare pixel by pixel against D3D9.
- Route the ~18 files that call D3D9/D3DX directly through GAL. `RenderTarget.hpp:69` exposes
  `HDC`.
- **Texture formats:** Mali, Immortalis, Xclipse (Exynos) and PowerVR do not expose BC/DXT. All
  FA, FAF, map and mod textures are DXT1/3/5, about 2.2 GB in SCFA alone.
  - Plan: transcode DXT1 -> ETC2 RGB8 and DXT5 -> ETC2 RGBA8/EAC (same size as BC) at import time,
    into a disk cache.
  - Keep native BC on Adreno 7xx/8xx.
  - M1 decodes on the CPU for the splash only.
- **Mobile preset:** render scale 0.6-0.75 for the 3D view with the UI at native resolution,
  shadows low, bloom off, 30 fps cap, and load/store-op discipline on the tile-based GPUs.

### W4 - Audio and movies (1-2 months)

- XACT banks are content version 43 (0x2B). Check whether FAudio/FACT reads them; otherwise write
  a small loader. Move DirectSound streaming to AAudio through SDL3.
- Sofdec `.sfd` movies (MPEG-1 + ADX): the recovered decoder is SSE-bound and already off on x64.
  Use `/nomovie` until it is ported with NEON or replaced.

### W5 - Bit-exact simulation (3-4 months, the highest risk)

FA runs in lockstep and compares a sim checksum every 50 beats. A one-ulp difference desyncs.
Measured on retail `ForgedAlliance.exe`:

- Most sim float math is SSE scalar single, which is identical on ARM.
- x87 is used for transcendentals, PC_24 `double` arithmetic, `fild`/`fistp` and return values.
- The x87 `sin`/`cos`/`tan`/`atan`/`log`, rounded to float, equal CORE-MATH correctly rounded
  results over all 2^32 inputs in the ranges the game uses. Large arguments need the 66-bit pi
  reduction and the `|x| >= 2^63` and NaN quirks.

The plan is a portable "msvc8 FP layer", used by every build (x86, x64, arm64):

1. **Compiler policy:** `-ffp-contract=off` (NDK clang emits `fmadd` by default), no fast-math,
   `-fsigned-char`, default FPCR on sim threads. Add a link check that sim objects call no
   libm/printf/strtod directly.
2. **Functions:**
   - CORE-MATH-based `sin`/`cos`/`tan`/`atan`/`atan2`/`log` with x87 large-argument behaviour.
   - Ports of the retail MSVC8 SSE2 `acos`/`asin`/`pow` cores. They are instruction-identical to
     msvcr80, so the port is exact by construction.
   - Fused helpers for the sites where retail feeds the 80-bit result straight into the next x87
     operation (for example `CheckTracking`). Rebuild that site list with x87 stack tracking.
   - x86 float-to-int semantics per site (`fistp` under its control word vs `_ftol2_sse`).
   - Canonical x86 NaN `0xFFC00000`.
3. **Wild Magic** (the collision library) is pure x87 in retail, with 4,884 double-typed operands
   rounded to 24 bits under PC_24. Either prove the sim paths are float-only, or give them PC_24
   double semantics.
4. **CRT text layer:** VC8 `%.14g` (Lua `tostring`) and `strtod`. Host sprintf differs from VC8 in
   2.3% of floats in 1e-4 .. 1e6.
5. **Lua GC timing:** count `nblocks` in x86-32 object sizes, so weak tables and `__gc` behave as
   on PC.
6. **Recovery bugs that affect x86/x64 too, so fix them upstream first:**
   - `std::atan2` in `CalculateFiringPitch` (1,478 inputs differ).
   - `msvc8::log` (5 inputs).
   - The pathfinding exp table in `Cluster.cpp`: use the retail bytes.
   - `tostring` via host `sprintf`.
7. **Harness:**
   - Exhaustive 2^32 sweeps against x87 on Intel and AMD.
   - Headless re-simulation of vault replays, each pinned to its featured-mod version. Pass = zero
     "Checksum for beat ... mismatched". The runner exists since M3a
     ([headless-replay.md](headless-replay.md)); version-pinned data does not yet.
   - mpemu retail vs x64 vs arm64 with `/synclog`.
   - "arm64 synclog == x64 synclog" for the same replay, to separate ABI bugs from recovery bugs.

Premise: the recovered sim must already match retail on x86. That has not been shown beyond short
runs, so this estimate is optimistic.

### W6 - FAF engine-patch parity (3-5 months, then about 2 weeks per exe-changing FAF release)

- FAF does not run the 2007 engine. It runs the 2009 retail exe plus FA-Binary-Patches: 75 hook
  files, about 170 hook sites, 65 section files. About 40 of them change sim results or the sim
  Lua API.
- faf-re reverses an FAF-patched exe of roughly 2023. All nine sim patches checked are missing:
  - intel update every 5 ticks;
  - TestCommandCaps;
  - transport load factor;
  - fractional silo progress;
  - aim fix #143;
  - collision-offset target point #145;
  - `SetNavigatorPersonalPosMaxDistance`;
  - the new Projectile methods;
  - ForceAltFootprint.

  Also missing are the `SetFocusArmy`, `GetDepositsAroundPoint` and `GetTimeForProfile` binders,
  and the 12-hook "old desync fix" subsystem that is live in 3839.
- **Plan:**
  1. Generate a hook-by-hook inventory (`docs/port/faf-patch-parity.md`), pinned to the deployed
     exe by its embedded Exe GitSHA, not its version stamp.
  2. Close the sim gaps on x86/x64 first.
  3. Derive the engine version (replay header, `GetEngineVersion`) from the featured mod instead of
     the hard-coded 3764.
  4. Put faf-re's own non-1:1 fix for #125 behind an offline-only switch.
  5. Track fafdevelop/fafbeta so a port is ready when stable is promoted.
- Nothing checks engine versions between peers. A lagging engine is not rejected in the lobby; it
  desyncs in game. Ask FAF for an engine patch-level field in the lobby check.

### W7 - Launcher and FAF client functions (1-2 months, plus FAF approvals)

- **Launcher:** the community [Rust + Tauri client](https://github.com/FAForeverRustClient/FAForeverRustClient)
  becomes the Android launcher.
  - Its core compiles and links for `aarch64-linux-android` after a 19-line patch: desktop-only
    tray/single-instance/window-state behind `cfg(desktop)`, `mobile_entry_point`, rustls instead
    of OpenSSL.
  - Still to do:
    - an Intent-based `ProcessPort` that starts our `GameActivity` with the desktop argv;
    - Android stand-ins for `open::that`, keyring and home directories;
    - packaging the engine as a Tauri mobile plugin.

  Upstream the gating and seams to that project. Until then, the M1 Java launcher stays the
  launcher.
- **Login:** use the OAuth device flow (RFC 8628), as the official client does since July 2026.
  Use the full grant URN `urn:ietf:params:oauth:grant-type:device_code`. Then `/lobby/access`,
  `ask_session` and `auth`.
  - Develop against a local FAF stack (gitops-stack + Tilt).
  - Production needs FAF's approval (W9).
- **FAF game files:** with a login, use the official featured-mod API. Without one, M1 uses FAF's
  public legacy download path, which needs FAF's OK before any public release, or a local FAF
  install.
- **Lifecycle:** while the game Activity is in front, the launcher process holds the lobby socket
  and the ICE adapter, so it must run as a foreground service.

### W8 - ICE adapter (1-2 months)

- The engine stays adapter-agnostic: `/gpgnet 127.0.0.1:port` plus loopback UDP, unchanged except
  for the POSIX socket port.
- **Production today** is java-ice-adapter 3.3.14 (ice4j, raw ICE, `d`/`e` framing, candidates over
  the lobby's `iceMsg`). faf-pioneer (Go, WebRTC DTLS/SCTP, icebreaker signalling) is experimental.
  **The two cannot play together.**
- **Phase 1: the Java adapter on Android's runtime**, in its own `:ice` service process, driven over
  JSON-RPC exactly as the desktop clients drive it.
  - Replace 5 virtual-thread sites.
  - Remove AWT/JavaFX.
  - Replace `System.exit`.
  - Use an slf4j-android binding.
  - Handle the API gaps at minSdk 26: `CompletableFuture.delayedExecutor`, `String.formatted`,
    `Stream.toList`, `List.copyOf`.
- **Phase 2: faf-pioneer for android/arm64.** It needs cgo against the NDK for DNS and
  `-ldflags=-checklinkname=0`. Ship it as a JNI library, or as a `lib*.so` executable in
  `nativeLibraryDir`.
- **Watch upstream:** java-ice-adapter PR #91 (replaces ice4j with webrtc-java, which is
  desktop-only, and moves signalling to icebreaker), and downlords-faf-client PR #3331 (switch to
  pioneer). Ask FAF which direction is final before investing heavily in the ice4j fork.

### W9 - FAF cooperation (start now; weeks to months of calendar time)

Only FAF can decide these. None of them may be worked around.

1. **An OAuth client** for the Android app (public client, device-code and refresh grants, scopes
   `openid offline public_profile lobby`), registered by a PR to gitops-stack. The `lobby` scope
   gets scrutiny; agree on it first. Do not reuse the official or Python client ids.
2. **Machine proof (`faf-uid`):** the lobby needs a decryptable `unique_id`. There is no Android
   build.
   - The clean route follows the macOS precedent (community PR #17, built by FAF CI): contribute
     `machine_info_android` plus an arm64 job to FAForever/uid, and FAF builds it with their key.
     The proof should carry meaningful device fields so moderators can tell Android players apart.
   - Building our own proof with the key extracted from the public binaries is technically
     possible. It is not done: it defeats an anti-abuse control and would get the project banned.
3. **Acceptance of a non-retail engine** in normal, rated and matchmaker games, under FAF's
   fair-play rules, plus advance notice of exe-changing releases and an engine patch-level check
   in the lobby.
4. **Use of the content endpoints:**
   - the legacy featured-mod download path;
   - a replay corpus for the determinism CI, with rate limits agreed.
5. **The application id and name:** this app is `io.github.m3rt1n99.fafre`, not `com.faforever.*`.
6. **Lockstep pacing:** the game runs at the slowest client. Agree with FAF how phones join large
   lobbies or queues (CPU-score gating, a mobile badge).

Every Android player's FAF account also needs a Steam or GOG ownership link, as on PC.

### W10 - Input, UI scale, performance (5-8 months, mostly after L2)

1. **Tablets, DeX and Android desktop windows with mouse and keyboard first.** This gives near-PC
   parity, and all FAF hotkeys work unchanged.
2. **An engine-side touch layer:**
   - one-finger pan and pinch zoom (fits strategic zoom);
   - tap = click;
   - long-press (~350 ms) = right-click, for smart orders;
   - box select via a toggle;
   - an on-screen Shift/Ctrl/Alt strip;
   - a trackpad mode for small UI;
   - S Pen hover = mouse hover.

   Suitable for tablets, spectating, replays, coop and AI games.
3. **A phone HUD in FAF Lua** (touch layout, larger build grid, command wheel, control-group bar),
   shipped through FAF, not as a hidden override.
   - `ui_scale` is Lua-only (0.8-2.0). The stock HUD cannot reach 48 dp touch targets on phones.
4. **Performance gates** use measured max sim speed on deterministic replays after a 20-minute
   thermal soak, not SoC names.
   - faf-re's maxSP formula cannot go negative while retail's can. Fix it first.
   - 2024+ flagships score 1700-2270 in Geekbench 7 single-core, against 2722 for the dev PC, and
     lose 27-41% under sustained load. Expect phones to slow down 8-12 player late games.

## Sequence

```
M1  one APK, data deploy, native data bring-up (L0)              done
M2  W1.1-2: arm64 compile sweep + compile fixes (896/1010)       done; x64 LargeAddressAware oracle open
M3  W1.3-5: 64-bit clean; headless arm64 replay runner runs a replay to the end (adb shell)
    M3a x86 runner (main.exe /headlessreplay), 4 vault replays to the end    done
    M3b arm64 (and x86_64) link of the runner, 0 undefined / 0 duplicate      done
    M3c run on the emulator (x86_64 T1-T3+R4, arm64 T1) with the low arena   done; phone run open
    M3d truncation fixes + reflection codemod, run without the low-address arena
M4  W5 + W6 on x86/x64: vault replays checksum-clean on the recovered engine (fixes upstream)
M5  W5 on arm64: same replays checksum-clean on a phone (L3)
M6  W2 + W3 on Windows: SDL3 + Diligent backend, pixel-compared against D3D9
M7  W2 + W3 + W4 on Android: main menu on the device (L1), then offline skirmish (L2)
M8  W7 + W8 against a local FAF stack: login, lobby, ICE, Android vs Android
M9  W9 approvals in place: online vs PC (L4), beta
M10 W10: touch layer, phone HUD, performance tiers (L5)
```

M4/M5 (correctness) and M6/M7 (graphics, platform) are independent and can run in parallel.
The critical path to L4 is M2 -> M3 -> M4 -> M5 plus the W9 approvals.

Total: about two person-years. With two or three contributors that is roughly 9-15 months of
calendar time. It also depends on FAF's decisions, which cannot be scheduled from this side.

## Risks

- **The recovered sim is not yet proven 1:1 with retail on x86.** Every determinism and parity
  estimate assumes it will be. This is the dominant schedule risk. Since M3a it does play complete
  FAF games deterministically; checksum parity needs version-matched replays (M4). One known
  source of run-to-run variance is an uninitialised read in `Unit::UpdateBlipsInRange` (see
  [headless-replay.md](headless-replay.md#known-issues)).
- **FAF may decline** online play for a reimplemented engine or for mobile clients. In that case
  L3 is the ceiling: offline play and replays.
- **ICE direction:** if FAF moves the Java adapter to webrtc-java or icebreaker signalling, the
  ice4j fork stops interoperating.
- **Performance:** a phone may not keep up in large games, even when everything is correct.
- **Device variety:** Mali, Immortalis, Xclipse (AMD) and PowerVR drivers. Vulkan quirks need the
  GLES fallback.
- **AMD vs Intel x87:** whether their transcendental results round to float identically is
  unmeasured. If they differ, PC players already diverge on those inputs today.

## Will not be done

- Ship SCFA data, FAF data or `ForgedAlliance.exe` in the APK.
- Spoof, relay or self-build `faf-uid`, or borrow another client's OAuth id.
- Run the retail exe under Wine/Box64 as "the port".
- Connect a non-bit-exact engine to online games against PC players.

## Evidence

The measurements behind this roadmap, taken 2026-10-03, come from:

- the arm64 compile sweeps over all 1021 x64 translation units;
- reading `init_faf.lua` against the engine's VFS code, plus a host run of the real script with
  the portable core;
- exhaustive 2^32 x87 sweeps on an Intel Core Ultra 7 265KF, run against CORE-MATH and msvcr80;
- disassembly of the deployed FAF 3839 exe;
- code reads of FAForever/uid, server, faf-policy-server, gitops-stack, faf-user-service,
  downlords-faf-client, java-ice-adapter, faf-pioneer, faf-icebreaker, FA-Binary-Patches and
  FAForever/fa;
- a compile/link check of the Rust client for `aarch64-linux-android`.

Each claim the plan depends on was re-checked by a second, independent pass, which tried to
refute it. Corrections from that pass are already folded in.
