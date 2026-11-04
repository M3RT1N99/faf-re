# The game client on Android: inventory and plan

The headless simulation runs on Android and matches the recorded checksums (core set on a Galaxy
S22 Ultra, extended set on the x86_64 emulator with the x87 and with the software x87). This is
what the playable game still needs, measured on upstream fa160592.

*Inventory of 2026-10-31.*

## What the client is

The client is every translation unit of `FAF_ENGINE_SOURCES` (src/sources.cmake) that
`src/sim_sources.cmake` leaves out of the simulation: 256 files of the engine's own code, about
214,000 lines (render, UI, application shell, render backends, audio, movies, user world).

**Compile probe:** each of them run through the arm64 simulation build's compile command with
`-fsyntax-only` (the POSIX and Win32 shims of `src/platform/posix` included):

| Result | Files | Where |
|---|---|---|
| Compiles already | 91 | render (20), misc (9), sim (8), ui (6), textures (5), the FAF effect layer (5), meshes, water, camera |
| Stops at a wxWindows header | 56 | ui (27), misc (8), app (8), render (4), net (3): through `moho/ui/UiRuntimeTypes.h` (input events), `moho/app/WxRuntimeTypes.h`, `moho/render/d3d/WD3DViewport.h`, the script debugger windows |
| Stops at a Direct3D 9 header | 13 | the D3D9 backend and `moho/render/d3d`, meshes, frame, shadows |
| Stops at a DXGI, D3D10/11/12 header | 24 | those backends, and the **Vulkan backend** (`dxgiformat.h`: format enums only) |
| Stops at `xact3.h` | 7 | audio, and user-side Lua and commands that play sounds |
| Other missing headers | 6 | DbgHelp, psapi, wrl, ws2tcpip, ffmpeg |
| Errors after the headers | 59 | mostly `D3DVIEWPORT9` in the gal device interface (50), a few Win32 types (cursors, icons, GDI bitmaps, `SECURITY_ATTRIBUTES`) |

## What ties it to Windows

- **Window and input:** wxWindows 2.4.2 (MSW) hosts the main frame and the viewport. The UI takes
  keyboard and mouse through wx event handlers (`CUIKeyHandler`, `CMauiWxEventMapper`).
- **Vulkan backend** (upstream's, Vulkan 1.3: about 5,000 lines, and the FAF effect layer it shares
  with Direct3D 12):
  - the Win32 surface (`VK_KHR_win32_surface`, `GetClientRect`);
  - DXC loaded as `dxcompiler.dll`;
  - `D3DPreprocess` from d3dcompiler_47 as the effects' preprocessor;
  - D3DX11 on a WARP device for the textures the direct DDS path does not take
    (`backends/common/ImageCodec`).
- **Direct3D 9 types in shared code:** the gal device interface and `moho/render/d3d` use D3D9
  structs and enums (`D3DVIEWPORT9`, formats) even when the Vulkan backend draws.
- **Audio:** XACT 3 (`xact3.h`).
- **Movies:** ffmpeg (a dependency already; needs an Android build).
- **Fonts:** GDI in a few places.
- **Online:** the lockstep transport is Winsock (the Linux simulation build leaves
  `moho/net/CNet*` out); the lobby and GPGNet (`CLobby`, `CGpgNetInterface`) are client files.
- **The S22's GPU (Xclipse 920)** samples no BC formats (device probe of 0.4.1): DXT textures
  have to be decoded on the CPU.

## Plan

| Step | Goal | Work |
|---|---|---|
| **C1** | The whole engine compiles and links for Android | Portable DirectX type headers (types and enums only: d3d9 types, `dxgiformat.h`), the Windows-only backends (D3D9/10/11/12, DXGI) and the script debugger left out on Android, a wx subset ("wx-lite": the event handlers, events, window, frame and app the engine uses) on a small Android host, XACT and movies as silent stubs. One library, started by an Android activity |
| **C2** | First picture: the main menu drawn on the phone | Vulkan on `VK_KHR_android_surface`; DXC built for arm64 with a preprocessor in place of `D3DPreprocess`; textures without D3DX11 (direct DDS, CPU decode of BC where the GPU lacks it, mip generation) |
| **C3** | The menu works | Touch as mouse, Bluetooth mouse and keyboard; fonts on FreeType; audio on FACT (FAudio's reimplementation of XACT 3, the one Wine uses for xactengine3); movies on ffmpeg |
| **C4** | A skirmish against the AI | Speed with the renderer running (performance hints, threads), the UI scaled for a phone |
| **C5** | Online | The lockstep transport on POSIX sockets, GPGNet and the ICE adapter on Android, FAF login, a launcher (the Rust client is the candidate) |

C1 to C3 are engine work testable on the PC (emulator, x86_64) and on the phone in steps; C5
needs FAF's side (ICE adapter, login).

**From the old port** (branch `android-port-v1`): the CPU DXT decoder, the FreeType font
rasteriser, the galtrace/galplay tools (replaying recorded draw calls on the phone without the
game) and the device probe can move over; its Diligent renderer and its own effect compiler are
replaced by upstream's Vulkan backend and effect layer.

**First step of C1:** the portable DirectX type headers and leaving the Windows-only backends out,
then the probe again, which shows the real errors behind the headers. wx-lite follows: it decides
most of C1, as 56 files stop at wx.

## Progress

*2026-11-02:*
- **The client builds in `android/native`** ([native/README.md](native/README.md)). That project
  builds the engine for Android as the repository's own presets do and adds the client on top. It
  changes no file of the engine, so the engine's tree and its Windows build stay as upstream has
  them. It holds:
  - the Win32 names the client needs beyond the engine's POSIX layer;
  - DirectX type stand-ins and stand-ins for the Windows-only backends' headers that shared code
    includes;
  - the Vulkan backend's Android surface;
  - stand-ins for DXC, D3DX and the Win32 cursor;
  - nine engine files the Android build compiles patched ([native/patches/README.md](native/patches/README.md)).
- **Client objects:** 139 of the client's 200 translation units compile for arm64. The other 61
  stop at wxWindows (45), XACT (8), DbgHelp, psapi, Winsock, DirectSound, ffmpeg, the GDI font and
  `std::unary_function`.
- **Renderer test:** `libfafgalsmoke.so` is card 4 of the app, from release 0.7.0. The engine's gal
  brings up its Vulkan device on the phone's screen, then clears and presents.
  - On the emulator with SwiftShader (Vulkan 1.2) it stops at the device.
  - With `-gpu host` on a Radeon RX 9070 XT (Vulkan 1.3 through the emulator) it passes: 602 frames
    in 10 s at the 60 Hz vsync.
  - Android's window surfaces have no B8G8R8A8, so the backend's swap chain takes R8G8B8A8 there.
  - **Galaxy S22 Ultra (Xclipse 920, Vulkan 1.3.279, release 0.7.1, 2026-11-02): PASS.**
    - 1203 frames in 10 s at the 120 Hz vsync, the slowest 18 ms. The device was ready in 43 ms,
      with a swap chain of 4 R8G8B8A8 images.
    - The GPU has every feature the backend checks (dynamic rendering, synchronization2, timeline
      semaphores, scalar block layout, anisotropy, wireframe) and textures up to 16384.
    - It has no BC: 0.7.0 stopped there, and from 0.7.1 the device comes up without. It has ETC2
      and ASTC, and D32S8 but no D24S8.
    - The game's DXT textures need the CPU decode of C2.
    - The phone was set to HD+: the display mode was 720x1544 at 120 Hz, of WQHD+, FHD+ and HD+.
- **Open for C1**, each in `android/native` and none by changing the engine:
  - wx-lite: the wxWindows names the engine uses, as headers under `include/` and an Android
    implementation, for the engine's own wx code to compile against;
  - `xact3.h`: the engine brings its own XACT interfaces and needs only the header's data
    structures; without an engine it stays silent, as on a PC without XACT;
  - stand-ins for `DbgHelp.h`, `psapi.h` and `ws2tcpip.h`;
  - the GDI font (`CD3DFont`) and the DirectSound movie sink;
  - the mesh vertex formatters: their headers need nothing of Windows, their code is in the
    Direct3D 9 and 10 backends' files, so Android gets its own copy with a float16 conversion
    that matches D3DX's.

  After that, `libfafgame.so` links.

*2026-11-03:*
- **`libfafgame.so` links**, for arm64 and x86_64: the whole engine, with every client translation
  unit but the Windows-only ones (195 of the engine's and the renderer's 24), on 15 patched engine
  files. Everything that was open for C1 above is done. The library has no entry point yet. It is
  in the default build now, so the app's CI builds it too.
- **wx-lite** ([native/WX_LITE.md](native/WX_LITE.md)) is the part of wxWindows 2.4.2 the engine
  uses, with wxWindows' behaviour: event tables, `ProcessEvent`'s order, `MSWWindowProc`'s message
  translation. It runs on a **Win32 message layer** (`include/win32/WinUser.h`,
  `src/win32/User32Android.cpp`): windows, a queue per thread, `SendMessageW` across threads,
  focus, activation, capture and the keyboard state. The host feeds the input and the activity's
  state into it (`include/win32/Win32AndroidHost.h`). The renderer test draws on one of its
  windows.
- **The mesh formatters** are the engine's Direct3D 10 code. Their halves match D3DX9_43's
  `D3DXFloat32To16Array` (the x64 DLL the Windows build links) for all 2^32 floats, called one value
  at a time as the engine calls it. D3DX picks its routine by CPU: this is the one it runs on Intel
  CPUs. On other CPUs its SSE routine rounds 2.3 % of the patterns differently (half denormals, the
  top of each binade, NaNs).
- Left out, and said so in the log: the wx tool windows (the script debugger, the emitter and
  reflection editors, the directory dialogs). Game discovery has no socket, as outside Windows.
  `OpenURL` parses the link with wxURL and opens it through the host.
- **The engine starts under the host** (`native/src/host`, card 5 of the app from release 0.8.0),
  on the emulator for x86_64 and for arm64 under its ARM translation, with SCFA and FAF 3835's
  game files. `CScApp`'s initialisation runs: the archives are mounted, the preferences and the
  documents get their folders, the user Lua and the UI start, the fonts are tried. `CreateDevice`
  tries the renderers in FAF's order; Vulkan stops without its shader compiler (C2), and the
  engine ends with exit code 1, as a PC without a usable renderer does. Only now does the linker
  see the program's whole graph (before, nothing reached `CScApp`); it named one more function:
  `D3DXMatrixLookAtRH`, which the shadows call. Android has D3DX's own C routine for it, checked
  against the DLL for 50 million inputs (D3DX's Intel and SSE routines differ from it in the last
  bits).
- **Galaxy S22 Ultra (Exynos 2200, Android 16, release 0.8.0, 2026-11-03): as on the emulator.**
  The engine starts under the host on the phone and runs the same initialisation, the preferences'
  and documents' folders included, up to Vulkan's missing shader compiler: exit code 1. The math
  check and the renderer test pass (1203 frames in 10 s at 120 Hz). The surface was 1544x720
  (landscape, the phone set to HD+). The phone's SCFA import holds 8 archives, without the
  localisation, mods and skins archives, which the menu will want (C2).
- **Found on the way**, for later steps:
  - Without XACT the sound functions raise Lua errors ("SND: Error retrieving XACT COM
    interface"), which stop the import of `/lua/usermusic.lua` and with it
    `/lua/options/options.lua`, as on a PC without XACT. The menu needs the options: audio on
    FACT (C3) or an XACT engine that is silent but there.
  - Each font fails to load (no GDI fonts yet, C3).
  - FAF's init asks for the process priority "high", which Android does not give an app (C4).
  - The SCFA import takes `gamedata/*.scd` and `maps/`; the game also wants `fonts/`, `sounds/`
    and `movies/` (C3).
- **The test program for the event system and the message layer** (`native/tests/wx`,
  `faf_wx_lite_tests`): 39 tests of wx-lite's event order, the message layer's queue, sends, input
  parameters, hooks, activation and waits, and the translation into wx events, against wxWindows
  2.4.2 and Win32. All pass on the x86_64 emulator; the app's workflow runs the arm64 build on an
  arm64 runner.
- **C1 is done:** the whole engine compiles and links for Android, as one library an activity
  starts, and its window and input layer is tested.
- **Next, C2 (the first picture):** the shader compiler for the Vulkan backend (DXC for arm64, a
  preprocessor in place of `D3DPreprocess`), the textures (direct DDS, the CPU decode of BC, mips),
  and for the menu the options' Lua, which needs the sound functions to work without XACT.

*2026-11-04:*
- **C2 on the emulator (x86_64, the PC's GPU): the main menu and a skirmish draw and run**, with the
  game's real files, every effect through the preprocessor and DXC, and the textures through the codec.
  The menu holds 60 fps; the skirmish about 13 fps, which is C4's.
  - The effects' preprocessor: all 22 effects byte for byte against d3dcompiler_47
    (`tests/fxc`; its header comment has what still differs on generated sources).
  - The texture codec: BC1 to BC3 texel for texel against WARP, the game's files by route against
    D3DX11, malformed files refused (`tests/imagecodec`).
  - Taken ahead from C3: fonts on FreeType, as GDI renders them (`tests/gdifont`), and touch as the
    mouse. Without sound yet: an XACT engine that plays nothing, so the options load; `/nomovie`.
  - Android's own: the surface comes and goes (Home and back in the menu, in the skirmish and during
    its loading), a new size is followed, also one that comes before the program's window;
    a fatal error ends the process with its message as the app's verdict; a crash leaves the engine's
    report and debuggerd's tombstone, both in the results zip.
  - The app keeps FAF's packages whole (about 700 MB) and imports SCFA's `fonts/`.
- **On the phone it needs:** SCFA imported again (`fonts/`), FAF's files downloaded again (whole).
- **On a phone (0.9.0, a Galaxy S22 Ultra set to HD+: Xclipse 920, Vulkan 1.3.279):** the picture is
  right and the speed good. A skirmish on an 8-player map against 7 AIs ran at about 95 % of real time at
  1647 x 768, with the CPU's BC decode and all the GPU-driven features on and BC as the only Vulkan
  warning, with at most 185 MB of heap; it ended after 170 s by the player's own hand.
- **Playable by fingers, from C4:** the game draws at a size of its own, 768 lines on a phone, which
  makes its UI larger and leaves fewer pixels to fill, and fingers act as a mouse, with the camera on
  two fingers and a bar for what fingers have not (`TouchInput`, [README](README.md)). Checked on the
  emulator with real multi-touch events: selecting, orders, queues, the camera, the keyboard on a
  first start without a profile. The phone showed what was missing: a finger covers the place it
  chooses for a building, which made setting one against a factory hard, and a queued order was hard
  to hit, so that a tap placed a new one. So a tap now puts a building's ghost, a finger moves it and a
  tap on it places it, and *Del* deletes the order nearest to a tap (0.9.1; the emulator's fingers
  only so far).
- **Sound and movies, C3's rest:** the XACT engine on FACT over AAudio, X3DAudio on F3DAudio, the
  movies on FFmpeg ([native README](native/README.md), Sound and movies). Checked on the emulator: the
  game's banks through the mix's numbers (`faf_audio_tests`), the logos, the intro film with its music
  and voice, the menu's film and the timeline by screenshots and the mix's peaks, Home and back, a call,
  six movies on both ABIs (`faf_movie_tests`). Nobody has heard it: the emulator has no audio output.
  Its frame rate says nothing about a phone's during a film: copying a frame into its GPU memory takes
  about 110 ms there, 40 MB/s. Open for the phone: the levels (the game's categories put the intro
  film over full scale at its loudest, peaks of 1.2 to 1.3 in each of two streams) and the speed in the
  menu, whose film is 1824x1024. A film keeps decoding while the app is in the back.
  - FAF's Lua breaks the campaign from the main menu: `selectcampaign.lua` calls
    `operationbriefing.CreateUI('X1CA_001', opData)`, which takes `(parent, scenarioPath)`, so that the
    screen stays black after the timeline film. Not the port's (read in its Lua, not run on a PC).
- **Next:** the phone's word on sound and movies, then the speed (C4) and online (C5).
