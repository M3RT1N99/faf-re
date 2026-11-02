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
