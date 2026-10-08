# port/graphics/trace — galtrace and galplay (M6c, step 5)

`galtrace` records the `gpg::gal` call stream of a run into a file that does not depend on pointer
width; `galplay` replays such a file into a backend with no engine, no Lua and no game logic. The plan
in [docs/port/renderer.md](../../../docs/port/renderer.md) uses it to compare backends below the engine
(D3D9, Diligent-D3D11/Vulkan/GL on Windows now, the phone's Vulkan and GLES in step 8) and to replay the
menu on the phone before the engine runs there.

Nothing here is in the default `main.exe`. The graphics build (`/p:FafPortGraphics=true`) compiles
`format/`, `record/` and `play/`; the recorder does nothing without `/galtrace <file>`, galplay nothing
without `/galplay <trace>`. `format/` and `tools/` are also portable C++17 host tools (Windows x86 and
x64, Android x86_64 and arm64).

| Path | What |
|---|---|
| `format/GalTraceFormat.h/.cpp` | The format: ops, field types, the schema of every op, texel layouts, blob hashes |
| `format/GalTraceIO.h/.cpp` | `Writer`, `Reader`, the schema-checked `Cursor`, the generic decoder, the `Validator` |
| `record/GalTraceRecorder.h/.cpp` | The recorder: a decorator `Device` and a decorator per gal object type |
| `play/GalPlayer.h/.cpp` | galplay's core: replays a trace into a `Device` (engine-free) |
| `play/GalPlayMain.cpp` | `main.exe /galplay`: windows, device, replay, BMPs, exit before WinMain |
| `tools/galtrace_dump.cpp` | `galtrace-dump`: validate, decode, digest, extract a blob |
| `tests/galtrace_format_test.cpp` | Format unit test (45 checks: round trips, deduplication, truncation, a changed blob byte, a wrong id type) |
| `CMakeLists.txt` | Host build of the format library, galtrace-dump and the test (`/W4 /WX`, `-Werror`) |
| `android/build_reader.py` | NDK build of the reader and decoder for x86_64 and arm64, and runs on WSL or a device |
| `scripts/port/galtrace.py` | record, play, compare, dump, tools, gate |

## Running it

```bash
msbuild src\sdk\main.vcxproj /p:Configuration=Debug /p:Platform=Win32 /p:FafPortGraphics=true
python scripts/port/galtrace.py tools                       # galtrace-dump + format test, Win32 and x64
L=<the machine's GUI lock>; EXE=output/main-gfx/Win32/Debug/main.exe
# record a 900-frame menu trace on D3D9 (the frame harness with /galtrace), replay it into every backend,
# compare every replay with the live captures, record a second trace and compare the content digests:
MSYS_NO_PATHCONV=1 python scripts/port/galtrace.py gate --exe $EXE --out <dir> --determinism --lock $L \
    --backends d3d9,diligent:d3d11,diligent:vk,diligent:gl --live diligent:vk=<live vk run dir> ...
# the pieces
MSYS_NO_PATHCONV=1 python scripts/port/galtrace.py record --exe $EXE --out <dir> [--gal diligent:d3d11] --lock $L
MSYS_NO_PATHCONV=1 python scripts/port/galtrace.py play --exe $EXE --trace <dir>/trace.galtrace --out <play> --gal diligent:vk --lock $L
python scripts/port/galtrace.py compare --play <play> --ref <dir>/live/run1 [--exact] --out <play>/compare
python scripts/port/galtrace.py dump --trace <dir>/trace.galtrace [--records 100]
python port/graphics/trace/android/build_reader.py --run-wsl <trace> [--run-adb <trace> --adb <adb.exe>]
```

`record` runs `scripts/port/gfx_capture.py` with `-- /galtrace <dir>/trace.galtrace`, so the trace is
of a frame-harness run (pinned clock, seed, prefs, hidden window) and the harness's own BMPs are the
live captures. `play` starts `main.exe /galplay` hidden, below normal priority, in a job object, under
the GUI lock, and kills it if a window of it becomes visible or takes the foreground (as
gfx_capture.py does). `compare` checks each galplay BMP against each reference byte for byte and runs
`gfx_capture.py parity` (max |delta| <= 1 on <= 0.1 % of the pixels, heat maps).

Engine options: `/galtrace <file>` records; `/galtraceframes <n>` ends the trace after n presents
(default: when the device is destroyed, or at exit). The trace's side file `<file>.json` has the
recorder's counters (records, blobs, deduplicated bytes, objects, calls off the device thread, calls
that threw, objects passed in that the recorder never handed out, texture locks it could not size,
`RenderTarget::GetDC` calls) and where its time went.

galplay: `main.exe /galplay <trace> /galplayout <dir> [/gal diligent:<api>] [/galplayframes <list>]
[/galplaynofpu] [/galplayskip <Op>]`, plus any backend option (`/galreport`, `/galnovalidation`, ...).
Exit 0: complete, every readback equals the recording; 4: complete, some readback differs; 2: stopped
early or no device; 3: bad options. Output: `frame_<N>.bmp` per readback (the harness's BMP format and
frame numbers, from the trace's `harness_frames` metadata), `galplay.json` (records, calls, presents,
draws, each readback with both hashes, mismatches by category with examples, gal errors) and
`galplay.log`.

## The format (version 1)

```
file    "GALTRACE" | u32 version (1) | u32 n | n x (str key, str value) | record* | End record
record  u16 op | u16 flags | u32 payload bytes | payload
str     u32 length | bytes          (also Bytes)
```

- **Little-endian, fixed widths.** Integers are u8/u32/i32/u64; floats are the IEEE single's bits.
  Nothing is a pointer: a trace written by the Win32 recorder decodes identically in the x64 and
  Android builds of the reader (same content digest).
- **Field by field.** Every gal context (`DeviceContext` with its heads, sample options, adapter modes and
  format lists; `TextureContext`; `EffectContext` with its macros; `OutputContext`; `D3DVIEWPORT9`; the
  draw contexts; `CursorContext`) is written member by member. The payload of every op is its field list
  in `GalTraceFormat.cpp`; the recorder writes it, the player reads it through a `Cursor` that checks
  every read against that list, and the generic decoder needs nothing else.
- **Objects are ids.** u32, numbered in the order the engine first got the object, never reused; 0 is
  null. Field types: `IdRef` (refers to a live object of a given type) and `IdDef` (the object a call
  returned: created, or the same decorator again). `Release` ends an id when the engine drops its last
  reference; `DeviceDestroy` ends the device.
- **Payloads are blobs.** A `Blob` record (u32 id, two 64-bit content hashes — FNV-1a and a SplitMix
  word hash — and the bytes) precedes the first record that uses it; later uses only name the id. The
  reader verifies both hashes. Blobs are: texture file images (`CreateTexture`), locked texture
  rectangles packed to whole rows of blocks (`TexUnlock*`: what the engine wrote, or for a read-only lock
  what it read), locked buffer ranges (`VbUnlock`/`IbUnlock`), effect sources, a compiled-effect cache
  file when the context asks the backend to read one, `GetTexture2D` inputs and outputs.
- **Flags:** bit 0 the call came from another thread than the one that created the device; bit 1 is
  reserved for calls that threw (v1 writes a `Note` record instead of the call).
- **Stream ops:** `Blob`, `End` (records, blobs, blob bytes, presents, objects — the validator checks
  them), `FpuState` (x87 and SSE control words, written before a call whenever they changed; D3DX's DXT
  encoder depends on the x87 precision), `Note`, `DeviceCreate` (the context the engine asked for and
  the one the backend reports after setup), `DeviceDestroy`, `Release`.
- **Call ops** (0x01xx Device slots, 0x02xx Texture, 0x03xx VertexBuffer, 0x04xx IndexBuffer, 0x05xx
  RenderTarget, 0x06xx CubeRenderTarget, 0x07xx DepthStencilTarget, 0x08xx Effect, 0x09xx
  EffectTechnique, 0x0Axx EffectVariable): one per method, arguments first, then what the backend
  answered (result fields). Window handles are kept as "set or not"; the effect cache path as its file
  name only (its directory is the run's `/cachedir`), so the stream of a run does not depend on where
  it ran.

## How the recorder attaches

The recorder installs itself as the process's `DeviceDecorator` (`port/graphics/diligent/DeviceFactory.h`,
owned by the backend) during static initialisation when `/galtrace` is given. A backend's creation hook
calls `DecorateCreatedDevice(backend)` (the engine installs what it returns) and `NotifyDeviceSetup`
after the backend's `Setup`; the Diligent backend does both in `diligent::CreateDevice`/`SetupDevice`.
D3D9 has the same two calls in `Device::Create`'s D3D9 case (`src/sdk/gpg/gal/Device.cpp`, guarded;
see "Engine hooks" below).

Rules of the decorators:

- every engine call is forwarded unchanged and in order, under one lock, so the file's order is the
  backend's order; objects passed in are unwrapped to the backend's own, objects handed out are wrapped
  (one decorator per backend object, so an object keeps its id), the head output contexts included;
- calls the backend makes into the active device while it runs a forwarded call (D3D9's technique calls
  `Device::BeginTechnique`, the Diligent effect layer too, D3D9's `OnReset` asks for the pipeline state)
  are forwarded without being recorded — a replay makes the backend repeat them itself — and get the
  backend's own objects; between `Wrap` and `NotifyDeviceSetup` (the backend's setup) nothing is
  recorded;
- payloads are copied where the backend takes them: at `Unlock` for locks (texture rows at the lock's
  pitch, packed), at the call for the rest. Mapped memory is copied out with one `memcpy` before it is
  hashed: D3D9's dynamic buffers are write-combined, and hashing them byte by byte in place cost 21 s
  for 23 MB (60 menu frames); copied first, 0.08 s.

## Measured (2026-10-08, dev PC, Debug|Win32 graphics build, FAF 3839 data)

- **A 900-frame menu trace** of a D3D9 harness run (frames 10, 15, 20, 25, 30, 45, 60, 300, 900): 152 MB,
  136,900 records, 1,115 blobs (149 MB unique of 164 MB referenced), 3,645 objects, 900 presents, 7,200
  draws, 0 calls off the device thread, 0 that threw, 0 unknown objects, 0 unsized locks, 0 `GetDC`. The
  harness's captures are the D3D9 reference hashes (60 `9ab312ba8320f018`, 300 `a901156c8b13d860`, 900
  `fea3146c72876a69`), so recording does not change the frames. The run takes 17.8 s at `/galpace 0`.
- **Determinism:** two recordings of the same run have the same content digest (`fc02e894c9b56083`),
  three 60-frame recordings too (`17ec90a495aa1652`).
- **Replay:** the trace replayed by galplay into D3D9 (4.8 s), Diligent-D3D11 (1.9 s), Diligent-Vulkan
  (2.8 s) and Diligent-GL (1.9 s): 9 of 9 readbacks are byte-identical to the recorded ones, and every
  galplay BMP is byte-identical to the live D3D9 capture and to the live capture of the same backend.
- **The engine's call stream is the same on D3D9 and Diligent:** a trace recorded on Diligent-D3D11
  (through the backend's own DeviceFactory hook) and the D3D9 trace differ only in: the API in
  `DeviceCreate` (the reported contexts are otherwise equal), the head output context's `face` (D3D9
  6, Diligent an uninitialised value), the head target's `RenderTargetContext::format_` (D3D9 0,
  Diligent 2), `ShowCursor`'s result (0/1) and two more `GetDeviceContext` calls. Every payload and
  every draw is the same.
- **On the integrated tree** (the merged `Device.cpp` hook and props, M6c integration): the 900-frame
  D3D9 recording has the same content digest `fc02e894c9b56083`, twice, and the four replays are again
  byte-identical to the recording, to the live D3D9 capture and to each backend's own live capture.
- **The comparison is not vacuous:** `/galplayskip VarSetMatrix4x4` or `VarSetTexture` makes 0 of 9
  readbacks equal and fails the parity gate; the format test's changed blob byte fails validation.
- **The reader on other ABIs:** galtrace-dump validates the 900-frame trace with the same digest on
  Windows x86 and x64, an NDK x86_64 static build under WSL, and NDK x86_64 and arm64 PIE builds on the
  Android emulator (arm64 through its ARM translation, which cannot run static arm64 executables).

## Engine hooks

Recording a D3D9 run needs the decorator hook in `src/sdk/gpg/gal/Device.cpp`, inside
`#if defined(FAF_PORT_GRAPHICS)` (the Diligent backend has it in `diligent::CreateDevice`). Merged in M6c
(integration), as T proposed it:

- `Device::Create`, D3D9 case: `Device* installed = diligent::DecorateCreatedDevice(device);
  sDeviceD3D.reset(installed); device->Setup(context); diligent::NotifyDeviceSetup(installed, device, *context);`
- `SupportsVertexTextureFormat`: cast the backend behind a decorator, not the decorator
  (`diligent::BackendOfInstalled`). Not reached with FAF 3839, whose mesh.fx has no `FAF_BONE_TEXTURE`; a
  mesh.fx with it reaches this cast when the effects are created;
- `Device::GetInstance`: `diligent::ResolveActiveInstance(sDeviceD3D.get())`, the backend while a forwarded
  call runs. The D3D9 backend static_casts `GetInstance()` to `DeviceD3D9` (`ActiveDeviceD3D9`); the menu
  reaches it only through virtual calls, which land in the decorator and are forwarded (a 60-frame
  recording without this hook has the same digest), but the backend's committed probes read members
  through it.

`BackendOfInstalled` and `ResolveActiveInstance` route to two virtuals of `DeviceDecorator`,
`Device* BackendOf(Device* installed)` (default nullptr) and `Device* ResolveInstance(Device* installed)`
(default `installed`), which the recorder's decorator overrides. With the three hooks, `Device.cpp`
compiles to the same object as before in the default configurations (Debug and Release, Win32 and x64;
docs/port/renderer.md, "The default main.exe does not change").

## Not yet

- In-game paths: the menu trace does not reach `HardwareMeshBatch.cpp:844,1125` or `Mesh.cpp:85,92`,
  which downcast gal objects to D3D9 types and would get decorators; they need guards before in-game
  tracing.
- `RenderTarget::GetDC`: GDI drawing into a surface is invisible to the trace (counted, never seen in
  the menu). Calls from other threads are recorded in lock order and flagged (none in the menu).
- `Device::Reset` and lost devices, cube and depth targets, `StretchRect`, saves and annotations other
  than the menu's are recorded and replayed by the code but no menu trace exercises them.
- A trace ends with its device; a second device in one run is not recorded.
- The Android galplay itself (the plan's step 8) links the player with the Diligent backend; only the
  reader and decoder are built for Android so far.
