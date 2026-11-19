# The game client on Android: inventory and plan

The headless simulation runs on Android and matches the recorded checksums (core set on a Galaxy
S22 Ultra, extended set on the x86_64 emulator with the x87 and with the software x87). This is
what the playable game still needs, measured on upstream fa160592.

*Inventory of 2026-10-31.*

*Since 2026-11-14 the client is the engine's own POSIX client on SDL3 (upstream removed wx, and
`cmake/FafLinuxClient.cmake` builds that client for Linux), built for Android by
[native/README.md](native/README.md): wx-lite, the Win32 message layer, the GDI fonts, the XACT
stand-in and the renderer test below are history, in git before that date. What the plan says of
the renderer, the shaders, the textures, sound and the network still describes the problems.*

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
  tap on it places it, and *Del* deletes the order nearest to a tap (0.10.0). That took away the line
  of buildings a finger drew: a finger that stays on the ghost until the tick and then moves draws one
  now (0.10.1). All of this on the emulator's fingers only so far.
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
- **The game's own lobby on Android, C5's first half:** *Multiplayer LAN* plays against FAF's real game on a PC, with
  nothing from FAF's servers ([native README](native/README.md), The network). The engine's UDP transport runs on a
  small POSIX Winsock (`WSAEventSelect` as a thread that polls), so `NET_MakeUDPConnector` and
  `NET_OpenDatagramSocket` are real and the discovery stand-in is gone. Checked on the x86_64 emulator against
  `ForgedAlliance.exe` of FAF 3835 on the PC, through the emulator's NAT:
  - Creating a game, joining by address and port by taps, both lobbies with both players, and a whole game with an
    easy AI in lockstep, with orders by fingers, until the AI had killed both commanders: 12:58 of game time, 155
    checksums each way, all equal, and the same beats for each army's end in both logs. The emulator hosts as well,
    with a PC that reaches it through a redirect and a small proxy. The arm64 build, under the emulator's ARM
    translation, played 5:30 of game time the same way, every checksum equal.
  - The first game ran against the PC's newer exe (the FAF client's `bin` folder) on 3835's Lua: 46 checksums agreed,
    and from beat 2350 on every one differed. So both sides need the same game version, exe included.
  - Discovery: the emulator's broadcasts reach the PC's game, and its replies, which the NAT drops, handed over by
    hand put the PC's game in the emulator's list, where *Join* joins it. A datagram sent to itself through the real
    socket and a broadcast that comes back are in `faf_net_tests`.
  - Android blocks an app's network in the back (`APP_BACKGROUND`): after Home the game stood still for everybody and
    the connection dialogs opened; after about 3 minutes each side had ejected the other, by the game's rules, and played
    on alone. A foreground service now runs while a UDP connector's socket is open (`NetworkGameService`, [README](README.md)).
    Against the PC's 3835 game, Home for 60 s and for 5 minutes and the screen off for 60 s, also under a forced Doze:
    the PC's game time went on without a stall (9 s behind real time over 9 1/2 minutes, as in front), no connection dialog on
    either side, `effective=NONE` in `dumpsys netpolicy`, 0 mismatched, and a game the host launched loaded in the back. The
    engine needs no change: its window is minimized, so `CScApp::Main` stops drawing, as the PC's does, and the main thread
    takes about 5 % of a core. The service ends with the socket, and a swipe in Recents ends the process, which a foreground
    service would otherwise keep alive and frozen (`onTaskRemoved`). Not checked on a phone.
  - Two things the rebuilt engine had never run: the connector was never deleted, so a lobby left its port taken, and
    *End Game* froze the game with no score screen, as the 3835 exe's own quit handshake is not in the engine's body
    (patches/README.md).
  - A third player in the lobby: `LOBMSG_NewPeer` carried a `u_long`, 8 bytes here and 4 on Windows, the protocol's only
    field of that kind. As 4 bytes, the emulator, a Windows host and a second Windows client in one lobby all connect to
    each other, with all three joining by the PC's LAN address, and two Windows clients read what the emulator as host
    passes on ([README](README.md)). No game of three humans has been played.
  - Open: a phone on real Wi-Fi (broadcasts through a router, the multicast lock, its power saving, Wi-Fi without
    internet beside mobile data); the service on a phone (its own battery rules, the game's speed in the back, Wi-Fi with
    the screen off); the TCP half of the transport, for GPGNet (C5).
- **Next:** the phone's word on sound, movies and the LAN game, then the speed (C4) and the rest of online (C5).

*2026-11-15, stage 2a on the SDL3 client:* what android-client's game had, but Griff and network games in the back. All
on the emulator (fafre_api36, x86_64, `-gpu host`, 2400x1080 on 65.3 mm), FAF 3841, the app's own Gradle and native
builds; both native presets build.
- **A skirmish** against an easy AI on Desert Arena (5 x 5 km): 8 minutes of game time with terrain, units, wrecks,
  smoke, the AI's lasers with their hit flares on the commander, and FAF's HUD at 1.67; a tap selects the commander, the
  orders panel's Move or Attack Move and a tap send it to the tapped place (SDL's own touch as the mouse); Menu, End
  Game, the score, Continue, Exit: `exit 0` after 928 s.
- **Sound** is set up: FAudio's FACT over SDL's AAudio output (the report's line; the system log has the game's process
  open its stream), 179 banks loaded and none failed, no error after the engine's XACT initialisation, 432 cues started
  over the game (`/spewsound`), and AudioFlinger has the game's process's track active (float, stereo, 44.1 kHz, no
  underrun).
  Still with `-no-audio`: nobody heard it.
- **BC textures**: the report says which way they go; the emulator's GPU samples them, and with the activity's
  `bcOnCpu` the phones' CPU decode draws the menus, the dialogs and a skirmish (at 900 lines) the same.
- **The UI package** is mounted and hooked again, the HUD's factor and the picture's size take effect (1707x768,
  2000x900 and the screen's own 2400x1080 tried), and taps land where the finger is at each size (`GameSurface`):
  FAF's dialogs and choices come out at TOUCH_UI.md's sizes (Options' rows and choices 5 mm). Its pages (the lobby and
  the LAN screen larger, scrolled by a finger) come with the touch schemes: until then those screens keep FAF's size,
  and the choices on them, which the package sizes for the page's zoom, are about 3 mm (the lobby's slot menu 2.8 mm).
  Griff's Lua does nothing (`FafreGriffWanted` says no).
- **The phone's keyboard** comes for a tap on a text field (the lobby's chat, a profile's name, the console's line)
  and for the Menu tab's Keyboard; typed text arrives, Back hides it before it is Esc again. The console that ` opened
  closed again on that key's char on every SDL platform: the shell now drops the char of a key-down a handler took,
  as wxWindows 2.4.2 did (`patches/src/platform/shell/Shell.cpp.patch`, a candidate for the engine).
- **In the back** the skirmish's simulation ran at half speed (30 s of game time lost in 61 s with Home): SDL's pump
  waited 100 ms each time while paused. A build-time patch of SDL (`patches/dependencies/SDL3`) lets the engine pace
  itself: no game time lost in 61 s and in 63 s with Home (within the log's second), the process at 8 to 12 % of a core
  in the back.
- **The renderer card (4) is retired**: the game's report names the GPU, each swap chain, the BC way and the sound's
  output, which is what a tester needed of it.
- Checks: `faf_griff_tests` 140 tests, 0 failed; `faf_movie_tests` six movies OK; the math card PASS (22188 cases);
  the replay card's two default replays PASS. The arm64 `faf_griff_tests` is a static program, and the emulator's ARM
  translation starts none (a static hello world crashes too, a dynamic one runs): the app's workflow runs it on an arm64
  runner.
- **Open, for stage 2b** besides Griff and network games in the back: the touch schemes on SDL's input (a tap in FAF's
  build mode lets the selection go instead of placing, as nothing hovered first; the pointer stays where the last finger
  was, so rollovers stay), the menus' pages, what the keyboard typed shown over the picture; audio focus,
  `NetworkGameService` and `NetTests` on `platform::net`, the program kept when Android destroys a stopped activity, a LAN
  game between two emulators. Not on a phone yet: the CPU's BC decode, AAudio, a phone's keyboard and the pace in the
  back under SDL. The engine makes its hidden log window at each start (`WINX_PrecreateLogWindow`), which SDL refuses on
  Android: its warning is the last one the report names after a clean end.

*2026-11-15, stage 2b's network half on the SDL3 client:* network games go on with the app in the back, on the
engine's own networking. All on two read-only instances of the emulator (fafre_api36, x86_64, `-gpu host`), FAF 3841,
from the head's builds; both native presets build.
- **The sound follows the activity and the audio focus**, as on android-client (`GameActivity`,
  `src/app/SoundAndroid.cpp`): the activity asks for the focus in front (`USAGE_GAME`, delayed gain during a call), and
  SDL's own pause of its output stops the game's sound and a movie's in the back and while another app has the focus.
  The game's AAudio track paused at Home (`sound: paused in the back`) and played again in front; a call (`emu gsm
  call`) took the focus (telecom `GAIN_TRANSIENT`, the game `LOSS_TRANSIENT`, its track paused: `sound: paused, another
  app has the audio focus`), and the call's end gave it back. Nobody heard it (`-no-audio`).
- **`NetworkGameService` and `NetworkGameRule`**, android-client's unchanged, run on SDL's activity, told by the engine's
  state: a UDP connector with its socket open, which the patched `CNetUDPConnector.cpp` counts
  (`NET_OpenUDPConnectors`), and `UI_GamePlaying`. A lobby started it on both sides (`isForeground`, type `specialUse`);
  it ran through the game and stopped a few seconds after *End Game* (the rule waits 3 s), and within a second of
  leaving a lobby.
- **The program outlives an activity Android destroys**, with small changes to SDL's way: `GameActivity` keeps the
  program in `onDestroy` and gives SDL its thread back in the next `onCreate`, the program allows SDL's activity to be
  made anew, and a build-time patch of `SDL_android.c` keeps the lifecycle's queue. With *Don't keep activities*, at
  Home in the menu and in a LAN game: `the activity is destroyed, the program goes on`, then `a new activity shows the
  running program`, its sound back and taps reaching the game. Outside a network game Android froze the process, a
  cached one now (`isFrozen`, nothing of a core), until the new activity came; with the service it went on (10 % of a
  core).
- **A LAN game between two emulators** with an easy AI on Desert Arena: the host by `/hostgame UDP 16010`, the guest by
  *Connect* to 10.0.2.2 through the emulators' redirection (`emu redir add udp:16010:16010`), both with Wi-Fi off, as
  the netsim network's Wi-Fi takes the replies past the redirection. 7:20 of game time, both commanders given orders by
  taps, no desync in either log and no connection dialog. Home on the guest for a minute: the host's game went on (2:59
  to 3:50), the guest's process at 16 % of a core in the back, both in step after. The host's activity destroyed for a
  minute: the guest's game went on (4:25 to 5:31), and the new activity showed the host's game, which a tap's order
  reached. The game ran at about 0.8 of real time on both.
- **Tests**: `faf_net_tests` runs on `platform::net` again (21 tests), `faf_shell_tests` pins the shell's rule for a
  key-down a handler took (5); with `faf_griff_tests` (140) they pass on the x86_64 emulator. The app's workflow runs
  the arm64 builds on its arm64 runner, but only for android-app, android-client and pull requests: no run has had this
  branch's yet. Under WSL's qemu one network test fails (its address-space limit meets qemu's own mappings). The app's
  unit tests (358, 2 skipped) pass; the math card PASS (22188 cases); the replay card's two default replays PASS.
- **Stage 2a's review**: Back hides SDL's keyboard edit too (a patch of `SDL_android.c`), so that below Android 11 a tap
  brings the keyboard after Back, and on every version typed text keeps coming; a patch of a dependency that replaces
  none of its target's sources stops the configure; the report says why the sound did not start; `GameSurface` scales a
  pointer's events only; Griff's Lua no longer starts on SDL's activity; the renderer card's files go; stage 2a's engine
  lines keep 120 columns. The shell drops the char of a taken key-down where the key's text comes before its key-up: a
  keyboard that commits text (AOSP's, for most characters) still closes the console its grave accent opened, as the docs
  now say; SDL's order of those keys is unchanged.
- **Stage 2b's review**: SDL's AAudio driver builds a stream anew when Android disconnects one, a paused one too, and
  started each it built, so that a new output device during the pause (headphones, Bluetooth, a call's routing) brought
  the game's sound back. Patches of `SDL_aaudio.c` and `SDL_openslES.c` make the pause a state, which a stream or a
  player built meanwhile keeps; AAudio's device threads hold their streams to it. On the emulator a call's pause held while the media moved
  to another output (the remote submix, attached through `IAudioService`); at the call's end the track's restart met
  Android's disconnect, and SDL's device thread built it anew on the new output, playing. Home paused it, the front
  played it again. The keep path removes SDL's clipboard listener, by which the system held each destroyed activity:
  after three re-creations with *Don't keep activities* and a GC in the process and in `system_server` it had four
  activities, now two. A start with extras while the program runs says in the report that they do not reach it (the
  README's `am start -S`). `faf_net_tests` checks the count of open connectors exactly, also after a thread that did not
  start (21 tests pass on the x86_64 emulator). The READMEs claim of the back what was measured.
- **Not shown**: anything on a phone; Android 8 to 10 (the keyboard after Back); a keyboard that commits text; on SDL's
  activity the earlier host's checks against a PC's game (the screen off, Doze, five minutes in the back, a game the
  host launched from the back), the 30-minute limit, a refused service, a swipe in Recents, Android ending the process.
  A paused stream disconnected in the pause, which takes a second device on its own output (the emulator's speaker
  output has none): the patch's way there is read from SDL's and AOSP's code. Found on the way: the program's first
  activity stays referenced after it is destroyed (SDL's Java: `SDLMain` runs in the activity's instance method `main()`,
  and `HIDDeviceManager` and the audio device callback keep the context they were made with); a start of the activity
  with extras while the program runs leaves the earlier activity under the new one; the LAN screen's
  *Create* dialog took no typed name in this check, as no field had the keyboard focus (the keys ran FAF's hotkeys,
  *S* the orders' Stop).
- **Next**: the touch half of stage 2b (the touch schemes, Griff v4 and the pages on SDL's input) once Griff v4 is merged
  in from android-client.

*2026-11-19, C5's GPGNet connect on the SDL3 client:* the FAF client, the phone's launcher in the same APK, hosts GPGNet
and starts the game with `/gpgnet 127.0.0.1:<port>` (`docs/LAUNCHER_NATIVE_NETWORK.md`); the game connects to it over
TCP ([native README](native/README.md), The network). On the emulator (fafre_api36, x86_64), with the activity's
`LAUNCHER_ARGUMENTS` and `adb reverse` to `android/tools/gpgnet_host.py` on the PC:
- **The connect needed no change.** GPGNet's socket is the engine's TCP stream on `platform::net`, which the build already
  made as Linux does; the patch of `CGpgNetInterface.cpp` stopped only the other way, the launch through `/bin/sh`. The
  game connected some 7 s after its start, said `GameState Idle`, and on `CreateLobby [0, 0, "tester", 1, 1]` answered
  `GameState Lobby` within 30 ms and showed FAF's lobby (*Connecting...*, which waits for `HostGame` or `JoinGame`), its
  UDP port open and the network service running. When the launcher closed the connection 25 s later, the game stayed in
  its lobby, as before.
- **A launcher that is not there** left the game on gpgnet.lua's black screen for good: a refused connect
  (`WSAECONNREFUSED`) and a connection closed before `CreateLobby` were each one warning of an unknown command, and Back
  ran into a Lua error of FAF's Esc handler. Now the game says so in a box and ends after its OK (`exit 0`, so a launcher
  gets `RESULT_CANCELED`), both checked. On POSIX `WIN_OkBox` wrote to stderr alone, so that none of the engine's boxes came
  to a screen; it shows SDL's now (`patches/README.md`).
- **Found for native mode:** upstream's `CNetTCPBuf::Read` counts too few bytes for a read of 2 KB or more that it takes
  from the socket itself, so a launcher's argument with 2 KB or more left after what the stream's buffer holds of it ends
  the game's GPGNet connection (`CommunicationError`); one of 4 KB does so wherever it starts. FAF's messages to the game
  are shorter; an ICE server list (`IceServers`) or a peer's `IceMsg` need not be. `faf_net_tests` pins it.
- **Tests**: `faf_net_tests` checks GPGNet's socket (23 tests), with `faf_griff_tests` (140), `faf_shell_tests` (5) and
  `faf_movie_tests` (three movies) passing on the x86_64 emulator; the app's unit tests (372, 2 skipped) pass; both native
  presets build.
- **Not shown**: anything on a phone or on arm64 beyond the build; the FAF client as the launcher; `HostGame`, `JoinGame`
  and a whole online game; the end of a `/rejoin` start or a server session whose launcher is not there (by reading
  only: they go on, as before).
- **The review's fixes**, each reproduced on the emulator first: a launcher's start while a game ran showed that game,
  which its version and `/gpgnet` port never reached, and gave its result only at that game's end; it now gives
  `RUNNING` and ends at once, in a task of its own or on the game's, and the game goes on (its picture, its GPGNet
  connection, and a tap's way to it after Home and back). A launcher's game whose process was killed in the back came
  back through Recents as a new game on the old port, which accepted a second `CreateLobby`; it now gives
  `PROCESS_ENDED` and connects nowhere, and the gone game's report says how it ended. With *Don't keep activities* a
  launcher's game still comes back around its program. A `/rejoin` start with no launcher showed no GPGNet box: the
  rejoin refused itself in its own box (no seat key on the device) and went on to the menu without GPGNet. A launcher's
  result was not read (am start has none).
- **Next**: the game's own links in native mode (`/nativenet`), with netlink's libraries built for Android.

*2026-11-19, C5's start from the FAF client's own app (E2b, part 1):* the FAF client becomes an app of its own
(`com.faforever.rustclient`, signed by another key), and this app stays the game. The launcher's contract is in the
[README](README.md) (The launcher's contract):
- **The way in** is `LaunchGameActivity`, exported, with the action `io.github.m3rt1n99.fafre.action.PLAY` and no UI. It
  checks the caller's signing certificate against the allowlist (`Callers`: `hasSigningCertificate` from Android 9,
  `GET_SIGNATURES` before it; `knownSigner` would hold from Android 12 only), then the extras (`Launch`), and starts the
  game's activity, which stays unexported, with `FLAG_ACTIVITY_FORWARD_RESULT`. The launcher's switches pass a whitelist;
  this side adds what only it may give, the rejoin's key file and console variables for `REJOIN_OFFER` and `/rejoin` for
  `REJOIN_PEERS` with `REJOIN_SOURCE`. The map and mods a start names come into the game's vault, and its replay, a
  `content://` URI it grants, into the app's cache, before the program starts: on SDL's thread (`GameActivity.main`,
  `Preparation`), with the step over the surface, and Back cancels. New codes: `CALLER`, `NO_MAP`, `NO_MOD`, `DOWNLOAD`,
  `REPLAY`, `CANCELLED`.
- **The status** is `StatusProvider`, exported for allowed callers only, read-only: `game_data` (the SCFA import, the
  ready versions, the app's job, whether the game runs, the app's version) and `last_game` (its start, end, exit code and
  crash, the end in the app's words alone, as the report's END line quotes the engine's log). `-PfafVersionCode` names
  the version code where it is given.
- **Found on the way**: FAF's API answers `401` without a login and its content server lists nothing, so a mod's uid alone
  names no file the game app could fetch: `MOD_FILES` carries the file of each mod of `MODS`, which the client has from
  the API, and the game checks the uid in the file it gets. And mods' uids are not all hex: M28AI's are
  `fnewm028-v247-...`, so `MODS` takes letters, digits, `_ . -`. Android 11's package visibility needs the client's
  `<queries>` for the status provider; the game sees its callers without one, as they start it for a result or ask its
  provider. A download's wait between tries, up to 5 minutes while the network is down, heard the user's Cancel only at
  its end (Back on a start, and the Progress card's Cancel): it now ends within half a second.
- **Checked on the emulator** (fafre_api36, x86_64, read-only) with two throwaway callers outside the repository, one
  signed with this PC's debug key and one with a throwaway key, each starting PLAY with `startActivityForResult` and
  logging the result: the allowed one reached FAF's main menu with `/numgames 3` on the command line, and got
  `RESULT_CANCELED` at the game's end; while that game ran its second start got `RUNNING`; `/init` and a `/gpgnet` host off
  the loopback got `ARGUMENTS`, a start that waited for no result `CALLER`, and `FEATURED_MOD` `coop`, by the explicit
  component, `NO_GAME_FILES`; the refused caller got `CALLER`. The provider answered the allowed caller (also its
  observer, at each game's start) and gave the refused one columns without rows. A replay of the parity runs (26930624,
  3835) as the allowed caller's `content://` URI played on its map, `scmp_007`, which `MAP` fetched from FAF's vault
  (1.3 MB) while the activity showed the step; the grant reached the game's activity through the trampoline's end.
  `MODS` with `MOD_FILES` brought M28AI v247 (1.9 MB) into the vault, and FAF's Mods Manager listed it. A map FAF has not
  gave `NO_MAP`, a replay without the grant `REPLAY`; with the emulator's network off, Back on a map's download gave
  `CANCELLED` 0.4 s later, and the game's process ended. `REJOIN_OFFER` put its switches after the launcher's, the seat
  key in the app's no-backup folder, and the game's box for a `/gpgnet` port with no launcher ended it with
  `RESULT_CANCELED`. `last_game` said `the program returned 0 after 93.7 s` for the menu's Exit and `CRASHED: killed by a
  signal, status 9` for a kill, with nothing of the engine's log.
- **Tests**: the app's unit tests (426, 2 skipped) pin the whitelist case by case, the allowlist with a fake package
  manager on both sides of Android 9, the rejoin's arguments and rules, the patterns of `MAP`, `MODS` and `MOD_FILES`,
  the fetches and their codes, the provider's columns, the version code and the Cancel in a wait between tries.
  `faf_griff_tests` (140), `faf_net_tests` (23), `faf_shell_tests` (5) and `faf_movie_tests` (six movies) pass on the
  emulator; both native presets build.
- **Not shown**: a phone; Android before 11, where the `GET_SIGNATURES` path runs (the JVM tests pin it); a client's
  `finishActivity` while the preparation runs, and Android destroying the activity meanwhile (read only: the process
  ends, or the program starts on the next activity's surface); the FAF client itself.
- **Next**: part 2, the import of a game data ZIP (`SETUP_GAME_DATA`); the client's release certificate on the allowlist.

*2026-11-19, C5's setup from the FAF client's app (E2b, part 2):* the client sends the player to this app for the game's
data, which its start needs (`NO_SCFA`, `NO_FONTS`, `NO_GAME_FILES`). The contract is in the [README](README.md) (The
setup):
- **The way in** is `SetupActivity`, exported, with the action `io.github.m3rt1n99.fafre.action.SETUP_GAME_DATA`, which any
  app may open: it shows this app's own screen and takes nothing but `STEP`, `scfa` (the SCFA import from its folder or a
  ZIP of it), `game_files` (FAF's newest game files, with the metered network's question) or none (both). Its jobs are
  `TestService`'s, as the test app's are, and go on after Back; the caller reads `game_data`. Another `STEP` or extra of
  the prefix gives `ARGUMENTS`. The test app's look, pickers and question moved to a base of both screens (`Screen`).
- **The ZIP's import** (`ScfaZip`) shares the folder import's rules (`ScfaImport`: what it takes, what is there, a file
  written whole by way of its `.part`). It reads the content URI twice with `ZipInputStream`, as a stream reaches no list
  at the ZIP's end and its local headers need not hold the sizes: its list, then its files, into `scfa/.zip-import/`, which
  they leave for their places once the whole ZIP has been read. It checks the ZIP's first bytes, each name against Zip
  Slip, the SCFA folder's place (the ZIP's top or one folder there) and the room, up front and as it writes.
- **Found on the way**: Android 16 refuses a `..` with `/` itself (`ZipPathValidator`), but not with `\`, which the app's
  guard catches. A second start with `FLAG_ACTIVITY_NEW_TASK` found the first's screen in its task and kept its step; the
  screen takes the new start's now. The folder import left a cut-off file's `.part`; the shared copy removes it.
- **Checked on the emulator** (fafre_api36, x86_64, read-only, the import moved aside and put back), each ZIP through the
  file picker of `SETUP_GAME_DATA`: an SCFA-shaped ZIP of 30 entries from the PC's own files, in its folder, stored and
  deflated, gave its 27 files, each equal to the PC's, and left `bin/` and `installscript.vdf`; a ZIP64 of 4 GiB and 1 MiB of
  zeros before three real files gave those three, from Python's ZIP (the sizes in the local header's ZIP64 field) and from
  Java's (a ZIP64 data descriptor); `../evil.txt` was refused by Android's check, `gamedata\..\..\evil.scd` by the app's,
  with nothing written; a ZIP whose movie of 2 GiB did not fit was refused up front (1301 MB free, 2148 MB needed); a text
  file named `.zip` was refused by its bytes; a Cancel while two files waited in `scfa/.zip-import/` left `scfa/` as it was.
  The folder picker's import of a small folder went the shared way. A throwaway app on no allowlist opened the screen in
  its task and got `RESULT_CANCELED` on Back, and `ARGUMENTS` for `STEP` `bogus` and for an extra `GAME_VERSION`. With Wi-Fi
  off (mobile data, metered) and 3841 out of sight, *Get FAF's newest version* asked about 683 MB on the screen, and *Not
  now* downloaded nothing. The game started on the import put back (FAF 3841, 18 archives).
- **Tests**: the app's unit tests (459, 3 skipped) pin the rules the imports share, the ZIP's first bytes, the paths and the
  guard, the SCFA folder's place, the plan, the room up front and as it writes, a Cancel, a damaged ZIP, one that changed
  between its reads, a name not in UTF-8, and a deflated ZIP64 entry of more than 4 GB; with `FAFRE_ZIP64_DIR` a stored one
  of 4 GB passed too. Both native presets build.
- **Not shown**: a phone; the whole SCFA folder as one ZIP of some 9 GB (the emulator has 1.3 GB free); a stored ZIP over
  4 GB on Android; the move into place failing half way (read only: the log says how many files scfa/ has then).
- **Next**: the client's release certificate on the allowlist.

*2026-11-19, C5's start and setup from the FAF client's app, the review's fixes (E2b):* a review of parts 1 and 2 found
seven faults, each checked against the code and shown on the emulator (fafre_api36, x86_64, read-only) before its fix,
with throwaway callers outside the repository: one signed by this PC's debug key, one by a throwaway key, one by both.
The contract's changes are in the [README](README.md) (The launcher's contract):
- **The start's app**: Android names a start's caller only as the app its result goes to (`getCallingPackage`), which an
  app the client starts for a result can borrow: the refused caller, started for a result by the allowed one, forwarded
  PLAY with `GAME_VERSION` 1 (`FLAG_ACTIVITY_FORWARD_RESULT`), and the game's activity checked its extra. Each start now
  carries a token that the status provider's call `play_token` gives an allowed app, as a provider sees the app that
  asks (`PlayTokens`: one start, within 60 s); the forwarded start gets `CALLER`.
- **A replay its launcher cannot read**: the trampoline passed on a URI that the app itself could read. The refused caller
  opened the setup screen with a read grant on its replay, and the allowed one's start of that URI without a grant made
  the game copy it. A start with data now needs `FLAG_GRANT_READ_URI_PERMISSION` itself, which Android lets through only
  for an app that may read the URI (the allowed one's start with the grant on the other's URI failed in its own
  `startActivity`). The review's way, `checkUriPermission` for the launcher's uid, sees explicit grants only and would
  refuse a launcher's own provider. The folder import gives back its lasting grant at its end, as the ZIP's did.
- **The status's observers**: Android lets any app that sees the provider observe its URIs, whatever a query answers it:
  the refused caller's observer heard a failed start. The provider notifies nothing now, and the launcher queries.
- **Two signers**: `hasSigningCertificate` gives an app of more than one signer none of theirs (the caller signed by both
  keys got `CALLER`), while the `GET_SIGNATURES` path before Android 9 took any one, and `CallersTest`'s fake said yes on
  9 and later too. One rule on every version now, one signer on the allowlist, and the fake answers as Android does.
- **A ZIP's leftover**: what an import whose process ended left in `scfa/.zip-import` went only once a later ZIP passed its
  room check, which the leftover's own room failed for good: 1.15 GB of it there, and a ZIP of 60 MB was refused for
  160 MB. Either import drops it first now, and the same ZIP came in.
- **Back while a zip unpacks**: Back during the unpack of scmp_029 (28 MB) was lost, and the game started. The unpack
  checks the Cancel now, and the preparation after each fetch and at its end: `CANCELLED`, with nothing of the map left.
- **Half a mod in the vault**: a mod's zip unpacked in `vault/mods/.unzip-<uid>`, where a kill -9 of the game while it
  unpacked M28AI left 40 of its files, for FAF's mod scan and the next fetch. A zip unpacks beside it in `downloads/` now,
  and its folder joins the vault whole: after the same kill the vault had nothing of it, and the next start brought M28AI
  whole (its 110 files).
- **Checked again**: with its token the allowed caller reached FAF's menu with `/numgames 3`, got `RUNNING` beside it and
  `RESULT_CANCELED` at its end; `CALLER` without a token, for the refused caller (no token, no rows) and for a start that
  waited for no result; `ARGUMENTS` for `/init` and a `/gpgnet` host off the loopback; `NO_GAME_FILES` for `FEATURED_MOD`
  `coop`; the replay with its map fetched played on Open Palms; `MODS` with `MOD_FILES` brought M28AI; a map FAF has not
  gave `NO_MAP`; with the network off Back gave `CANCELLED` 0.34 s later; `REJOIN_OFFER`'s switches came after the
  launcher's, and the GPGNet box ended the game with `RESULT_CANCELED` and `the program returned 0 after 43.0 s`.
- **Tests**: the app's unit tests (471, 3 skipped) pin the tokens, the one-signer rule on both sides of Android 9, the
  replay's grant, the leftover's room, Back after a download's last byte and where a zip unpacks; each fails with its
  fix taken out. `faf_griff_tests` (140), `faf_net_tests` (23), `faf_shell_tests` (5) and `faf_movie_tests` (three movies)
  pass on the emulator; both native presets build.
- **Not shown**: a phone; Android before 9 (the JVM tests pin it). Back during a stalled connection still acts only at its
  read's timeout (60 s), as before. A replay whose map the phone has not, started without `MAP`, ends in a native crash
  (SIGSEGV after the engine's `Error in background task: No such file or directory`): the client names the map.
- **Next**: the token on the client's side; the client's release certificate on the allowlist.

*2026-11-19, the review's fixes checked again (E2b):* each of the seven faults is real by the code before its fix, and
each fix holds. Two gaps in the vault's were left, both fixed now (`Vault.place`):
- **The replay test's M28AI**: the unpack's new Cancel check stops it too, and it unpacked into the test's cache, which
  takes any folder with a `mod_info.lua` for whole: a Cancel there would have left half of M28AI for every later run.
  It unpacks beside its zip now, as the vaults' do (`VaultTest` pins it, and fails with the old unpack).
- **A mod's older version**: the new folder took the place of one of its name that was deleted in place, in the vault.
  With the earlier build a kill -9 of the game during that deletion left 20,108 of the older version's 20,110 files
  there, its `mod_info.lua` with the older uid among them, which a start of that uid would have taken for whole. The
  older folder now leaves the vault in one rename and is deleted beside the download: the same kill left the new M28AI
  whole in the vault (110 files) and the older one's files beside the zip.
- **Checked again** on the emulator (read-only, x86_64), with the throwaway callers: the allowed caller's token and rows;
  no token and no rows for the refused one and the one of two signers; `CALLER` without a token, for a start that waits
  for no result, for the refused caller, for the two signers and for PLAY forwarded by the refused caller under the
  allowed one's name; `NO_GAME_FILES` for version 1, with the refused caller's observer on the provider hearing nothing;
  `REPLAY` for a replay without the start's grant, also when the game held a grant on it from the setup screen, and the
  allowed caller's own `SecurityException` for a grant on another app's URI; the replay with its grant played on Open
  Palms, whose map came into the vault; FAF's menu with `/numgames 3`, `RUNNING` beside it, and `RESULT_CANCELED` with
  `the program returned 0` after Exit; Back while scmp_029 (28 MB) unpacked gave `CANCELLED` within 20 ms, with nothing
  of it left; a kill as M28AI's unpack began left the vault empty, and the next start brought it whole (110 files); a
  ZIP of 60 MB came in beside a staging leftover of 1.2 GB that left 120 MB free, which the import dropped first; the
  folder import dropped such a leftover too and gave back its folder's lasting grant (`persisted=0x0`).
- **Tests**: the app's unit tests (472, 3 skipped); `faf_griff_tests` (140), `faf_net_tests` (23), `faf_shell_tests` (5)
  and `faf_movie_tests` (three movies) on the emulator; both native presets build.
- **Open**: what a process that ended left beside the downloads (a zip, its unpacked files, an older folder) goes only
  when the same map or mod is fetched again, which takes room but nothing of the game's; an import the service drops
  because a job runs keeps its lasting grant, which a release there would take from a running import of the same folder.
  Back during a stalled connection still acts only at its read's timeout (60 s).

*2026-11-19, C5's GPGNet reads long arguments whole (E2c):* native mode's messages from the launcher (`IceServers`,
`IceMsg`) can be 2 KB or more, and upstream's `CNetTCPBuf::Read` left out of its count the bytes it received straight
from the socket, as it does with what is left of a read after its buffer's bytes once that is 2 KB or more. The patched
one counts them ([patches README](native/patches/README.md), `CNetTCPBuf.cpp`):
- **What reads through it**: GPGNet's link to the launcher (`CGpgNetInterface`: the `/gpgnet` connect, and the listener
  for a GPGNet the game launches) and a live replay's stream (`/replay gpgnet://`, which `CReplayClient` reads a message
  at a time without blocking); `/savereplay gpgnet://` only writes through it. Peers' games use the UDP connector, or
  the TCP connector (`NET_MakeTCPConnector`), whose connections read with buffers of their own: neither a played game's
  commands nor anything a peer receives passes it. Every read the binary counted right returns the same, and the buffer
  is left as before; only a live replay's viewer now gets whole the long messages the old count corrupted.
- **Shown on the emulator** (fafre_api36, x86_64, read-only) with `gpgnet_host.py --ice-msg`, which sends a
  `CandidatesMessage` of that length as `IceMsg` and as `Test` before `CreateLobby`: the build before the fix ended its
  GPGNet on 4,096 bytes before the lobby (the box with `Premature EOF`, `exit 0` after its OK; the script saw no lobby).
  With the fix, 4,096 and 65,536 bytes: the game answered `GameState Lobby` within 30 ms, and its log ignores the
  `IceMsg` (this build has no native mode) and has the `Test`'s text whole, its length and SHA-256 those sent.
- **Tests**: `faf_net_tests` (25) reads arguments of 2 KB that the buffer holds nothing of, 4 KB, 64 KB written while
  the game reads, a short one after them, and a live replay's message of 4 KB without blocking; relinked with
  upstream's object it fails exactly those. `faf_griff_tests` (140) and `faf_shell_tests` (5) on the emulator; the app's
  unit tests (472, 3 skipped, inputs unchanged); both native presets build.
- **Open**: a report for upstream with the patch is drafted, and whether it goes out is the user's decision: native mode
  on the desktop reads the same messages through the same function. `faf_movie_tests` not run (no movie code changed).

*2026-11-19, the client contract's three additions (E2c):* what the FAF client's app needs of this one beside the start and
the setup. The contract's changes are in the [README](README.md) (The launcher's contract, The logs):
- **The whitelist** takes `/division` and `/subdivision`, which FAF's Java client passes for a matchmaker game: the league
  service's names of the player's division (`bronze`, `silver`, `gold`, `diamond`, `master`, `grandmaster`, or `unlisted`
  for none) and its step (`I` to `V`; none for `grandmaster` and `unlisted`), which FAF's autolobby takes for its texture
  `textures/divisions/<division> <subdivision>.png` (`AutolobbyController.lua` lists both): exactly those words.
  `/savereplay` and `/replay` take `localhost` beside `127.0.0.1`, as `/gpgnet` does.
- **Card 2** has *Import SCFA ZIP…* beside *Import SCFA folder…*: E2b's ZIP import, for phone testers without the client.
- **The game's logs** for the client's problem report: the status provider's call `game_logs` zips, for an allowed caller,
  what Share results takes of the game (both games' reports, the engine's log, its newest crash report), each tombstone as
  text (`Tombstone`: the signal, cause and abort message, the stacks and build ids, nothing of its memory, open files or
  log) and `report.txt`, into the app's cache, and answers with the zip's `content://` URI, which it grants the caller to
  read; `ResultsProvider` serves it, as it serves the results zips. The zip goes into public issues, so its texts are
  scrubbed (`LogScrubber`): players' names become `player1`, `player2`, ..., learned from the lines that name players in
  all of the zip's files, their ids `id1`, ..., a FAF game's id, a game's title, chat, the account's values and ICE
  credentials placeholders, hosts' addresses `<ip>`, and the app's folders `<external>`, `<internal>` and `<apk>`, the
  user's `<storage>`.
- **Found on the way**: the inventory found more than names, addresses and paths. FAF's Lua logs every GPGNet message to
  the launcher (`GpgNetSend`: players' ids, a team kill's names, chat, the stats' names) and the game's id (`REPLAY ID`, as
  in `/savereplay`), the LAN games its screen found (host, title) and chat the lobby could not show; a launcher's switches
  carry the account (rating, games, clan, country, division), and a game's or a player's id names the players on FAF's own
  pages. Desert Arena's civilians are `civilian`, in lower case. `tools/tombstone.py` reads a signal's fault address from
  fields 7 and 8 (`sender_pid`, `has_fault_address`) where tombstone.proto has 8 and 9, so it never shows the address;
  `Tombstone` reads 8 and 9 (0x40 on a system tombstone whose text says 0x40).
- **Checked on the emulator** (fafre_api36, x86_64, read-only) with E2b's throwaway callers, given a mode that calls
  `game_logs`, copies the zip by its URI and lists it: the refused caller got a null answer, and a `SecurityException` on
  the allowed one's URI; the allowed one got the zip in 0.09 to 0.12 s, its name and size by `OpenableColumns`, and read
  it; a later call's zip took the earlier one's place and grant (its URI then failed). The pulled zips' texts had none of
  the players' names (counted by a script that took them from the raw logs, which stayed on the PC), no host's address and
  no path of the phone: after a skirmish against seven AIs from FAF's lobby, also while it ran; after a replay of the
  parity runs, a multiplayer game, whose two players became `player1` and `player2`; after that replay's native crash
  without its map, whose tombstone's summary (SIGSEGV at 0x8, 33 threads) has the same 345 frames as `tools/tombstone.py`;
  and after a launcher's start against `gpgnet_host.py --mode 1` with an invented name, uid, game id, clan, rating and
  division (`/division gold /subdivision II` and `/savereplay gpgnet://localhost:...` passed the whitelist, and the
  autolobby's `CreateLobby`, FAF's `REPLAY ID` and the Test's ICE message were scrubbed). Card 2 shows both buttons; *Import
  SCFA ZIP…* opened the file picker and imported a small SCFA ZIP (27 files, 23 there already). An engine log of 10 MB
  took 8 s.
- **Tests**: the app's unit tests (494, 3 skipped) pin the whitelist's new values, the Game card's line for an import
  without fonts, the scrubber on the logs' line shapes with invented names and documentation addresses, the tombstone's
  summary of debuggerd's fields and what it leaves out, and the zip's entries, order and scrubbing across its files. Both
  native presets build (nothing native changed).
- **Not shown**: a phone; a LAN or online game with other players, whose peers' lines are pinned from the engine's and
  FAF's sources; the FAF client itself.
- **Open**: FAF's Java client passes more for a matchmaker game, which the whitelist still refuses: the faction (`/uef`,
  `/aeon`, `/cybran`, `/seraphim`), `/map`, `/gameoptions` with `key:value` arguments and `/replayid`. A name that only a
  mod's own log line gives, or only a crash report of an earlier game, stays in the zip. `tools/tombstone.py`'s fault
  address.

*2026-11-19, the game's logs, the review's fixes (E2c):* a review of `game_logs` found two gaps in the scrubber, both real
by the engine's and FAF's sources. The contract's changes are in the [README](README.md) (The logs):
- **Players' ids**: an id went only where a shape the scrubber knew gave it, and real lines give ids elsewhere, beside
  their names' stand-ins: a rejoin's seats (`/rejoin <player id>=<source>,...` in `game.txt`, the engine's `REJOIN:` lines
  of the offer and the return), FAF's moderator event, which every player's sim logs for each `GpgNetSend` of a game (a
  team kill's `data '<time>,<id>,<name>,<id>,<name>,'`), a game server's `SERVER LOBBY:` lines and session, and the
  engine's `PEERS: lobby player <id>:<name>`. Ids are learned now as names are, from those shapes and the earlier ones, and
  replaced where the shapes give them; one of three digits or more, as FAF's are but for its first 99 accounts, also
  wherever it stands as a number of its own. Found on the way: the engine's line of a join it ignored
  (`ignoring unexpected join (name="...", uid=...)`) named a player that no shape learned.
- **A title and repr's quotes**: the title the host sets in FAF's lobby, which `GpgNetSend` logs (`GameOption Title`),
  stayed, as did the chat and a LAN game's title or host that FAF's `repr` puts in single quotes (a text with `"` and no
  `'`). The scrubber takes both now, and the names in the stats' moderator event.
- **The provider's time**: on Android a matcher (ICU's) costs twice its find on a line of the logs, and with the shapes on
  the learn pass an engine log of 10 MB took 14 s. The scrubber keeps one matcher a pattern now: 5.3 s, where the earlier
  scrubber took 8 s (on ART, scrubbing alone: 4.4 s against 6.4 s, the same text out).
- **Checked on the emulator** (fafre_api36, x86_64, read-only): the allowed throwaway caller started a rejoin with
  `REJOIN_PEERS` `104857=0,2=1`, which the game refused in its box for want of the seat's key and went on to FAF's menu;
  its `game_logs` zip had `/rejoin id1=0,id2=1 1 0` in `game.txt`. With the review's lines and those above appended to the
  engine's log (invented names, documentation addresses), the zip had none of the names, ids, title, chat or addresses,
  the stand-ins the JVM tests expect, and no stand-in elsewhere.
- **Tests**: the app's unit tests (498, 3 skipped): `LogScrubberTest` pins the rejoin's seats, the `PEERS:`,
  `SERVER LOBBY:` and server session lines, the ignored join, the moderator events, the title, repr's single quotes, and
  an id of FAF's size wherever it stands as a number of its own and a shorter one only where the lines give it; its four
  new tests fail on the earlier scrubber. `faf_griff_tests` (140), `faf_net_tests` (25) and `faf_shell_tests` (5) on the
  emulator; both native presets build (nothing native changed).
- **Not shown**: a rejoin that takes its seat back, a game server's lobby, a team kill or a title set in the lobby on the
  game itself: their lines are the sources'.
- **Open**: an id of one or two digits stays where no shape gives it, as in native mode's `NETLINK:` lines, which the
  Android build does not have yet (an id of FAF's size there goes). The refused caller was not tried again: the
  provider's checks did not change.

*2026-11-19, C5's own peer links in native mode (E3, part 1):* the FAF client's app will start the game with
`/gpgnet 127.0.0.1:<port> /nativenet`, and the game then runs every peer link itself
(`docs/LAUNCHER_NATIVE_NETWORK.md`; the launcher's side in the [README](README.md), The launcher's contract). The
Android build makes upstream's links now, and the stand-in that kept them off is gone ([native README](native/README.md),
The network):
- **The build**: libjuice 1.7.4, libdatachannel 0.24.6 with usrsctp, plog and Mbed TLS 3.6.7, and `src/netlink`, by
  upstream's recipes (`cmake/deps`) at the pins of `cmake/FafDependencies.cmake`, as the Linux client adds them;
  `moho/net/NetlinkPeers.cpp` is compiled as upstream has it, and `src/net/NetlinkPeersAndroid.cpp` went. WebRTC
  builds whole with the NDK: Mbed TLS comes from source, and its media part (SRTP) is off upstream as well, so nothing is
  left out; it stays off by default (`net_NativeNetWebRtc 0`). The licences, MPL-2.0 (libjuice, libdatachannel),
  BSD-3-Clause (usrsctp), MIT (plog) and Apache-2.0 (Mbed TLS, which WebRTC's DTLS needs), are in the app's
  `assets/licenses`.
- **What the platform needs**: sockets, threads and `getaddrinfo` are bionic's, as on Linux. libjuice leaves
  `getifaddrs` out on Android (`NO_IFADDRS`): its host candidates come from `SIOCGIFCONF`, the IPv4 addresses, and from
  the IPv6 address of the default route; it binds every interface. Its random bytes on Android, STUN's transaction ids
  and ICE's tie-breaker, were `random()` seeded by the clock, as its `getrandom` leaves Android out: the patched
  `random.c` takes bionic's `arc4random_buf` ([patches README](native/patches/README.md)). `Game.networkInUse` counts
  the links' transports too (`NET_PeerTransportsActive`), so that `NetworkGameService` keeps the network while a link is
  up, also without a UDP connector.
- **Shown on the emulator** (fafre_api36, x86_64, two read-only instances, whose Wi-Fi netsim puts in one network): a
  game started with `/gpgnet 127.0.0.1:<port> /nativenet` said `GameState Idle`, took `IceServers` (FAF's list: two server
  addresses of three URLs, as TURN over TCP is skipped; the API's session object with the list under `servers` as well)
  and `CreateLobby`, answered `GameState Lobby` within 40 ms, and its log has `NETLINK: native mode`. With
  `gpgnet_host.py` as both games' launcher and as FAF's lobby server, one game hosted and the other joined it: each
  gathered three host candidates, the links connected over the emulators' IPv6 Wi-Fi addresses, took each other for
  ours, moved onto the encrypted lane, and the host's lobby seated the other player (`PlayerOption`) about 2 s after
  `JoinGame`; with an ICE server named `localhost` that never answers, which libjuice resolved and then waited for, about
  10 s after it. The network service ran in both games.
- **Tests**: upstream's `faf_netlink_tests` (13: the JSON and the signalling against the Java adapter's, two links over
  the device's addresses with 10,000 datagrams each way, the lane encrypted and plain, a reconnect from either side, a
  silent ICE server, our peers' detection, the duplication and the WebRTC data channel) and `faf_net_tests` (25) pass on
  the x86_64 emulator; the app's unit tests (498, 3 skipped); both native presets build.
- **Not shown**: a phone and arm64 beyond the build; a game played over the links (the lobby only); ICE servers that
  answer (server-reflexive and relayed candidates); a Java adapter at the other end; WebRTC in the game; the FAF client
  as the launcher.
- **Open**: `.github/workflows/android-app.yml` runs three native tests on its arm64 runner; `faf_netlink_tests` could be
  the fourth (outside `android/`, not changed). The patch of libjuice's `random.c` is a candidate for libjuice. The
  game's logs now have native mode's `NETLINK:` lines: the scrubber (`game_logs`) took both games' names and every
  address out of them, the selected pairs' and the peers' engine addresses too, in a throwaway JVM check over the two
  engine logs; their ids of one digit stay, as the scrubber leaves such ids where no shape gives them.
