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

*2026-11-01:*
- **Done:**
  - the DirectX type stand-ins (`src/platform/posix/directx`);
  - the Windows-only backends left out of non-Windows builds;
  - the Vulkan backend's Android surface;
  - what the render code needs from the POSIX Win32 layer;
  - stand-ins for DXC, D3DX and the Win32 cursor.
- **Client build:** `-DFAF_ANDROID_CLIENT=ON` (`cmake/FafAndroidClient.cmake`) builds the renderer
  (`faf_gal`) and the client objects (`faf_client`, outside the default build until they compile).
- **Renderer test:** `libfafgalsmoke.so` is card 4 of the app, from release 0.7.0. The engine's gal
  brings up its Vulkan device on the phone's screen, then clears and presents. On the emulator it
  stops at SwiftShader's Vulkan 1.2.
- **Open for C1:**
  - wx-lite;
  - the XACT stand-in;
  - the GDI font (`CD3DFont`);
  - the DirectSound movie sink;
  - the mesh vertex formatters (in the Direct3D 9 and 10 backends' files, on D3DX's float16).

  After that, `libfafgame.so` links.
