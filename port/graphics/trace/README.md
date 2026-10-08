# port/graphics/trace — galtrace and galplay (M6c step 5; format version 2 in M7a1)

`galtrace` records the `gpg::gal` call stream of a run into a file that does not depend on pointer
width; `galplay` replays such a file into a backend with no engine, no Lua and no game logic. The plan
in [docs/port/renderer.md](../../../docs/port/renderer.md) uses it to compare backends below the engine
(D3D9, Diligent-D3D11/Vulkan/GL on Windows) and to replay the menu on the phone before the engine runs
there (step 8, M7a1: the app's **Menu replay**).

A recording holds every payload (format version 1). The trace the app ships is **version 2**: it names
each game file it uses by VFS path and content hash instead of holding it, and the phone reads the files
from the user's own imported data (`PayloadRef`, `PayloadDigest`, `PayloadCompose` below).
`galtrace-refs` makes a version 2 trace from a recording against the game data and checks it.

Nothing here is in the default `main.exe`. The graphics build (`/p:FafPortGraphics=true`) compiles
`format/`, `record/` and `play/`; the recorder does nothing without `/galtrace <file>`, galplay nothing
without `/galplay <trace>`. `format/` and `tools/` are also portable C++17 host tools (Windows x86 and
x64, Android x86_64 and arm64); the Android app links `format/` and `play/` (port/android).

| Path | What |
|---|---|
| `format/GalTraceFormat.h/.cpp` | The format: ops, field types, the schema of every op, texel layouts, payload hashes, metadata keys, the frame hash |
| `format/GalTraceIO.h/.cpp` | `Writer`, `Reader` (payloads of every kind, hash-checked), the schema-checked `Cursor`, the generic decoder, the `Validator` |
| `format/GalTraceResolver.h/.cpp` | Where a version 2 trace's game files come from: `PayloadResolver`, `DirectoryResolver`, `CallbackResolver`; the data check (`ScanPayloadRefs`, `VerifyPayloadRefs`, `DescribeRefProblems`) |
| `format/GalTraceVfsResolver.h` | `VfsResolver`: port/native's `VirtualFileSystem` as a resolver (header only; the phone, galtrace-refs) |
| `record/GalTraceRecorder.h/.cpp` | The recorder: a decorator `Device` and a decorator per gal object type (writes version 1) |
| `play/GalPlayer.h/.cpp` | galplay's core: replays a trace into a `Device` (engine-free); hooks for pacing, stop and readbacks |
| `play/GalPlayMain.cpp` | `main.exe /galplay`: windows, device, replay, BMPs, exit before WinMain |
| `tools/galtrace_dump.cpp` | `galtrace-dump`: validate, decode, digest, payload kinds, extract a payload (`--data` reads references) |
| `tools/GalTraceRefs.h/.cpp`, `tools/galtrace_refs.cpp` | `galtrace-refs`: recording -> version 2 (`convert`), its checks (`check`: references, game content, equivalence), `extract`, `setmeta`, `refs` |
| `tests/galtrace_format_test.cpp` | Format unit test (93 checks: version 1 round trips, deduplication, truncation, a changed blob byte, a wrong id type; version 2 references whole and in parts, digests, compositions, resolvers, the data check's messages, the version rules) |
| `CMakeLists.txt` | Host build of the format library, galtrace-dump, galtrace-refs (with port/native) and the test (`/W4 /WX`, `-Werror`) |
| `android/build_reader.py` | NDK build of the reader and decoder for x86_64 and arm64, and runs on WSL or a device |
| `scripts/port/galtrace.py` | record, play, compare, dump, tools, gate, refs, release |

## Running it

```bash
msbuild src\sdk\main.vcxproj /p:Configuration=Debug /p:Platform=Win32 /p:FafPortGraphics=true
python scripts/port/galtrace.py tools                       # galtrace-dump, galtrace-refs + format test, Win32 and x64
L=<the machine's GUI lock>; EXE=output/main-gfx/Win32/Debug/main.exe
# record a 900-frame menu trace on D3D9 (the frame harness with /galtrace), replay it into every backend,
# compare every replay with the live captures, record a second trace and compare the content digests:
MSYS_NO_PATHCONV=1 python scripts/port/galtrace.py gate --exe $EXE --out <dir> --determinism --lock $L \
    --backends d3d9,diligent:d3d11,diligent:vk,diligent:gl --live diligent:vk=<live vk run dir> ...
# the pieces
MSYS_NO_PATHCONV=1 python scripts/port/galtrace.py record --exe $EXE --out <dir> [--gal diligent:d3d11] --lock $L
MSYS_NO_PATHCONV=1 python scripts/port/galtrace.py play --exe $EXE --trace <dir>/trace.galtrace --out <play> --gal diligent:vk --lock $L
python scripts/port/galtrace.py compare --play <play> --ref <dir>/live/run1 [--exact] --out <play>/compare
python scripts/port/galtrace.py dump --trace <dir>/trace.galtrace [--records 100] [--data <game files>]
python port/graphics/trace/android/build_reader.py --run-wsl <trace> [--run-adb <trace> --adb <adb.exe>]

# M7a1: the app's trace. Records the 900-frame menu on D3D9 (or --recording <v1 trace>), converts it to
# version 2 against the FAF install's data (init_faf.lua), checks that it holds no game data, replays the
# recording and the version 2 trace into d3d9, d3d11, vk and gl (frames must be byte-identical), stores
# the PC's Diligent-Vulkan frame hashes in its header and writes buildstage/traces/menu.galtrace + .json:
MSYS_NO_PATHCONV=1 python scripts/port/galtrace.py release --exe $EXE --work <scratch dir> --lock $L
# the pieces (galtrace-refs; --init defaults to C:/ProgramData/FAForever/bin/init_faf.lua)
python scripts/port/galtrace.py refs convert <recording> <out.galtrace> [--meta key=value]...
python scripts/port/galtrace.py refs check <trace> [--against <recording>]   # references, game content, equivalence
python scripts/port/galtrace.py refs extract <trace> --out <scratch dir>     # the game files (scratch only), for --data
python scripts/port/galtrace.py refs refs <trace>                            # what the app's data check sees
MSYS_NO_PATHCONV=1 python scripts/port/galtrace.py play --exe $EXE --trace <v2 trace> --data <dir> --gal diligent:vk --out <play> --lock $L
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
[/galplaynofpu] [/galplayskip <Op>] [/galplaydata <dir>] [/galplayref <backend>]`, plus any backend
option (`/galreport`, `/galnovalidation`, ...). `/galplaydata` is the tree of game files a version 2
trace refers to (`<dir>/<vfs path, lower case>`, what `galtrace-refs extract` writes); `/galplayref`
picks the `reference_frames.<backend>` hashes the readbacks are judged against (default: the backend
replayed into). Exit 0: complete, every readback equals the recording; 4: complete, some readback
differs; 2: stopped early or no device; 3: bad options; 5: a game file the trace refers to is missing
from `/galplaydata` or differs (`galplay.json` `data_problems` names each). Output: `frame_<N>.bmp` per
readback (the harness's BMP format and frame numbers, from the trace's `harness_frames` metadata),
`galplay.json` (records, calls, presents, draws, each readback with the recorded, replayed and
reference hashes and its verdict, references read, backend outputs provided, mismatches by category
with examples, gal errors) and `galplay.log`.

## The format (versions 1 and 2)

```
file    "GALTRACE" | u32 version (1 or 2) | u32 n | n x (str key, str value) | record* | End record
record  u16 op | u16 flags | u32 payload bytes | payload
str     u32 length | bytes          (also Bytes)
```

A reader of version 2 reads version 1 files; the version 2 payload ops in a file whose header says 1 are
an error. The recorder writes version 1; only `galtrace-refs` writes version 2.

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

### Version 2: payloads without the game's data (M7a1)

A payload number (`Blob` fields, 1..n in file order, defined before first use) can be defined four
ways. Every call op is unchanged, so a version 2 trace has the records of its recording, one for one,
with the same payload numbers:

| Op | Fields | The bytes |
|---|---|---|
| `Blob` (0x0001) | payload, hashA, hashB, bytes | in the trace (version 1's only kind) |
| `PayloadRef` (0x0008) | payload, hashA, hashB, size, path, archive, parts[path, archive, hashA, hashB, size] | a game file: the reader asks its `PayloadResolver` for the VFS `path` and checks size and both hashes. With `parts`, the files one after the other, each checked on its own (the engine hands gal an effect source as `/effects/d3d9states.compat` followed by the `.fx`); `path` is then the part the record names. `archive` names where the recording machine found it, for messages |
| `PayloadDigest` (0x0009) | payload, hashA, hashB, size, origin | not in the trace. `origin` 1: a backend's answer the replay produces itself (`GetTexture2D`'s output: galplay hands its own output to `Reader::ProvidePayload`); 2: a readback (compared by hash); 3: a saved output; 4: other |
| `PayloadCompose` (0x000A) | payload, hashA, hashB, size, base, copies[source, sourceOffset, sourcePitch, offset, pitch, rowBytes, rows], literals[offset, bytes] | built: `base` (or zeros), then each copy's rows from an earlier payload, then the literal runs; checked against the hashes. A texture atlas the engine wrote is its `GetTexture2D` outputs at their places plus the engine's own glyph blocks |

`Reader::ReadPayload` produces the bytes of any kind and says whether they equal the recorded hashes (a
digest or a composition may be available but not exact when this backend answered differently: galplay
lists that as a "derived payload" mismatch). A missing or different game file is fatal in galplay
(exit 5 on Windows; the app stops with the message). The `End` record counts embedded payloads only.

`galtrace-refs convert` decides by how the calls use each recorded payload: the file image of
`DevCreateTexture`, an effect source of `DevCreateEffect` and the input of `DevGetTexture2D` become
references (to the record's own path when its bytes match, else to the first VFS file with these bytes,
archives before directory mounts; else a split into two files, one of them the record's own); readbacks
and `GetTexture2D` outputs become digests; a texture write that contains `GetTexture2D` outputs becomes a
composition (the outputs found row by row, literal runs for the rest); vertex and index data stay
embedded. Path strings of the recording machine in records (`sourcePath`, `location`) become the VFS
path, `command_line` loses its paths. It fails when a game-file payload matches no file, and checks that
every reference reads back equal and every composition rebuilds the recorded bytes before it writes.
`galtrace-refs check` validates with every reference read, scans every embedded byte (blobs and
composition literals) for 64-byte windows of the referenced files and of the recording's digested
payloads and for whole game files of the same size (archives and directory mounts), and with
`--against <recording>` proves record-by-record and payload-by-payload equivalence.

### Metadata (header pairs; `GalTraceFormat.h` has the constants)

| Key | Value | Written by |
|---|---|---|
| `recorder`, `recorder_build`, `recorder_pointer_bits`, `gal` | the recorder; `gal` is the backend recorded on (`d3d9`, `diligent:vk`) | recorder |
| `harness_frames` | `10,15,20,25,30,45,60,300,900`: the frame number of each readback, in order | recorder (from `/galframes`) |
| `command_line` | the run's options; in version 2 every path is `<path>` | recorder, sanitised by galtrace-refs |
| `frame_rate` | `30`: presents per second of the recording's pace (the harness's `/framerate`) | galtrace-refs |
| `presents`, `readbacks` | totals, up front (`presents` = the End record's) | galtrace-refs |
| `payload_refs`, `ref_archives` | the number of `PayloadRef` records; the archives they name (`effects.nx2,textures.scd`) | galtrace-refs |
| `game_version` | `3839`: the FAF game version of the data the references were made against (the recording's engine.log) | `galtrace.py release` |
| `reference_frames.<backend>` | `10:d92c3a2233b80c55,15:...`: the frame hash of each readback as that backend renders it on the PC (FNV-1a 64 over R,G,B of the 1280x720 head, `galtrace::FrameRgbHash`, the harness's hash). The phone compares with `reference_frames.diligent:vk`; galplay with `reference_frames.<backend replayed into>` (`/galplayref`); `reference_frames.<gal>` stands in for the recorded bytes when a readback is a digest | `galtrace.py release` |
| `source_version`, `source_content_digest`, `converted_by`, `content` | the recording it was made from (its galtrace-dump digest) and what it shows | galtrace-refs, `galtrace.py release` |

### Game files on the phone (the resolver API)

`galtrace::PayloadResolver::ReadGameFile(vfsPath, &bytes, &error)` is all a reader needs; the reader
checks sizes and hashes. `VfsResolver` (`GalTraceVfsResolver.h`) wraps port/native's
`VirtualFileSystem`, mounted by init_faf.lua as the engine mounts it; set it on the reader
(`Reader::SetResolver`) or galplay (`PlayOptions::resolver`). Before a replay, `ScanPayloadRefs` lists
every game file of a trace (one entry per file and content, parts listed as files of their own, at
their first use), `VerifyPayloadRefs` reads each, and `DescribeRefProblems` words what is missing (by
archive) or different, for a tester. galplay's other hooks for the app (`PlayHooks`): `afterPresent`
(pacing, progress; false stops), `shouldStop` (polled before every record), `onReadback` (the replayed
pixels of each readback, B,G,R,A rows top-down, with its verdict against `reference_frames.<backend>`).

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

### The release trace (M7a1, `galtrace.py release`, HEAD 915bed23 + the version 2 sources)

- **Recording:** a fresh 900-frame D3D9 recording has the M6c digest `fc02e894c9b56083` (the version 1
  writer is unchanged in effect); the harness's captures are the D3D9 reference hashes.
- **`buildstage/traces/menu.galtrace`:** version 2, 111,039,865 bytes (the recording: 151,856,751),
  18.2 MB deflated (APK). 1,115 payloads: 1,044 embedded (107,748,156 bytes: vertex and index data the
  engine generated, 14,409 buffer uploads deduplicated), 32 references (1,297,663 bytes of 33 game files:
  11 effect sources = `/effects/d3d9states.compat` + the `.fx`, from effects.nx2; 21 UI textures and the
  cursor from textures.scd), 29 digests (20 `GetTexture2D` outputs, 9 readbacks), 10 compositions (the
  UI atlases: 55 placed `GetTexture2D` outputs and 42,607 literal bytes). Content digest
  `2dfe22a133934955`; the same on Windows x86/x64 and the NDK x86_64 reader under WSL.
- **No game data:** `galtrace-refs check` resolves all 32 references and finds no embedded byte equal to
  a game file (82,385 files of the archives and directory mounts, by size and hash) and no 64-byte
  window of the referenced files or of the recording's `GetTexture2D` outputs and frames in the embedded
  bytes or the literals. Equivalent to the recording: 136,900 records, 1,115 payloads, 12 records differ
  only in the path strings replaced by VFS paths.
- **The glyphs:** the 42,607 literal bytes are DXT5 blocks of text the engine rasterised through GDI on
  the PC ("© 2007 Gas Powered Games", the menu's button labels). The fonts are the TrueType files the
  engine registers from SCFA's install `fonts` folder (engine.log: arial*.ttf, butterbe.ttf, vdub.ttf,
  wintermu.ttf, zeroes_3.ttf), so the bitmaps are renderings of the game's fonts, not of Windows' own.
- **Version 2 replays = version 1 replays:** the recording and `menu.galtrace` replayed into D3D9 (5.1 s),
  Diligent-D3D11 (1.9 s), Diligent-Vulkan (3.0 s) and Diligent-GL (1.9 s): every BMP byte-identical
  between the two and 9/9 readbacks equal to the recording on each; the version 2 replays read 32 game
  files from the extracted tree and provide 20 `GetTexture2D` outputs from the backend.
- **`reference_frames.diligent:vk`** (from the version 2 replay into Vulkan) equals
  `reference_frames.d3d9` frame for frame; the written trace replays into Vulkan with 9/9 "pass".
- **Not vacuous:** `/galplayskip VarSetTexture` gives 0/9 pass; `/galplayskip DevGetTexture2D` stops at the
  first atlas write ("payload #34 (backend output) is not in the trace and the replay has not produced
  it"); a data tree without `d3d9states.compat` and with one changed byte in `logo.dds` stops before the
  first call (exit 5) naming both files and effects.nx2.

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
- The release trace holds frame hashes, not frames: the phone judges "exact"; the parity rule needs the
  PC's frames (`galtrace.py play --trace buildstage/traces/menu.galtrace --data <extract> --gal
  diligent:vk`, then `gfx_capture.py parity <that dir> <the phone's run dir>`).
- References are bound to the data the trace was converted against (FAF 3839's effects.nx2, SCFA's
  textures.scd): a tester with another FAF version gets "different from the files the trace was recorded
  with" for the changed files.
- The skirmish-lobby navigation is not in a release trace yet (only the main menu).
