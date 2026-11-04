# The Android native build

The engine for Android, and the game client on top of it, without changing a file of the engine.
The engine's own tree (`src/`, `cmake/`, the root `CMakeLists.txt`) stays as the repository has
it, and so does its Windows build. Everything Android needs beyond it is in this folder.

```sh
cd android/native
cmake --preset android-arm64          # or android-x64 for the emulator
cmake --build --preset android-arm64  # headless, faf_sim_math_tests, fafgalsmoke, fafgame, faf_wx_lite_tests
```

The outputs land in `build/android-<abi>/bin`, where the app (`android/app`) takes them from.
The dependencies come first, once, as for any engine build:
`cmake -DFAF_DEPENDENCIES_ONLY="WildMagic3p8;zlib-1.2.3;boost_1_34_1" -P cmake/FafDependencies.cmake`
(from the repository root).

## How it fits together

`CMakeLists.txt` adds the repository's root project as a subproject. With an Android toolchain
that project builds the headless simulation and the sim-math tests (`cmake/FafLinux.cmake`), as
the repository's own `android-*-release` presets do. On top of it come the client's targets:

| Target | What |
|---|---|
| `faf_game_sim` | The simulation's objects again, with this build's headers, so every object of a library sees the same types |
| `faf_android_platform` | `src/win32`, `src/gal`: what the Vulkan backend stands on here |
| `freetype` | FreeType, under the GDI font functions (see The fonts) |
| `faf_gal` | The renderer (`gpg/gal`): the Vulkan backend and what it shares with the others |
| `faf_wx_lite` | wx-lite, the part of wxWindows 2.4.2 the client uses, on the message layer (`WX_LITE.md`) |
| `faf_client` | The rest of the client, with the Android halves of what it needs from the left-out files |
| `faf_host` | `src/host`: the program's start as `WinMain` does it, and the activity's surface, input and lifecycle as Win32 messages |
| `fafgame` | `libfafgame.so`, all of the above: the whole engine and its host (card 5 of the app) |
| `fafgalsmoke` | `libfafgalsmoke.so`, the renderer test (card 4 of the app) |
| `faf_wx_lite_tests` | wx-lite's, the message layer's and the GDI fonts' tests (`tests/wx`, `WX_LITE.md` Checks), linked statically |

The client's files are the engine's (`src/sources.cmake`) minus the simulation's
(`src/sim_sources.cmake`) and minus the Windows-only ones (`CMakeLists.txt`,
`_faf_client_windows_only`: the Direct3D and DXGI backends, Effects 11, the wx tool windows).

### Headers

The client's targets put four folders ahead of the engine's include path, in this order:

| Folder | What |
|---|---|
| `build/android-<abi>/overlay/src` | Patched engine headers (see Patches) |
| `include/shadow` | Stand-ins for the Windows-only backends' headers that shared code includes (`include/shadow/README.md`) |
| `include/win32` | The Win32 names the client needs beyond the engine's POSIX layer: `Windows.h` adds them (COM's base, the registry, `OVERLAPPED`, structured exceptions, ...) and continues with `#include_next` to the engine's own `src/platform/posix/win32/Windows.h`; `WinUser.h` is the message layer's USER API; `shlobj.h`, `mmsystem.h`, `mmreg.h` and `dsound.h` do the same for the engine's headers of those names; `psapi.h`, `DbgHelp.h`, `TlHelp32.h`, `objbase.h` and `commctrl.h` stand in for Windows headers the engine's layer does not have |
| `include/directx` | DirectX type stand-ins (`include/directx/README.md`) |

`faf_wx_lite` and the targets that use it also have `include` itself on the path, for wx-lite's
`<wx/...>` headers.

An HWND is a window of the message layer (`src/win32/User32Android.cpp`): an object with a window
procedure, a size and a parent, whose messages come from its thread's queue as on Windows. The
activity's surface is the screen every window is shown on, as a Windows window is part of the
desktop; the Vulkan backend draws on it. The host (`include/win32/Win32AndroidHost.h`) gives the
layer the surface and feeds the input and the activity's state in as Win32 messages.

### Sources

| File | Stands for |
|---|---|
| `src/win32/Win32Android.cpp` | The Win32 functions `include/win32/Windows.h` adds, on the engine's POSIX layer's public functions; no COM class and no registry key exist, no program starts, and `ShellExecuteW` opens a link through the host |
| `src/win32/User32Android.cpp` | The message layer: windows, message queues per thread, `SendMessageW` across threads, focus, activation, capture, the cursor, keyboard state and hooks, the clipboard; the host's input arrives through it |
| `src/win32/ExceptionsAndroid.cpp` | `SetUnhandledExceptionFilter` on the fault signals, with the fault's code, address and context |
| `src/win32/FindFileAndroid.cpp` | `FindFirstFileA` and the rest, on POSIX directories with Win32's matching |
| `src/win32/PsapiAndroid.cpp` | `GetProcessMemoryInfo`, from `/proc/self/status` |
| `src/win32/DbgHelpAndroid.cpp` | `SymFromAddr`, from the dynamic symbol tables with demangled names |
| `src/win32/GdiAndroid.cpp` | GDI's font functions, as the font renderer calls them: handles for fonts, bitmaps, brushes and memory DCs, `DrawTextW`, the bitmap's pixels (see The fonts) |
| `src/win32/GdiFonts.cpp` | GDI's TrueType fonts on FreeType: the fonts from memory, the face a name finds, metrics, advances, kerning pairs and glyphs as GDI has them (checked on Windows by `tests/gdifont`) |
| `src/audio/AudioAndroid.cpp` | The audio platform: none yet (`DirectSoundCreate` finds no device, X3DAudio places nothing, no XACT clock) |
| `src/audio/XactEngineAndroid.cpp` | An XACT engine that plays nothing, until FACT (C3): the sound functions work and so do the options, which import the music's Lua; `CoCreateInstance` creates it |
| `src/movie/FfmpegSfdDecoderAndroid.cpp` | The movie decoder: no FFmpeg for Android yet, so the player skips movies |
| `src/diag/FrameSamplerAndroid.cpp` | The frame sampler's mark: nothing to sample with (the Windows one suspends the main thread) |
| `src/gal/VulkanSurfaceAndroid.cpp` | The Vulkan backend's platform unit (`VulkanSurface.h`) for Android: `VK_KHR_android_surface`, and for the patched `DeviceVulkan.cpp` a surface's end and the first frame |
| `src/gal/WindowsCursorAndroid.cpp` | The backends' Win32 cursor helper: no system cursor |
| `src/gal/D3DCompilerAndroid.cpp` | `D3DPreprocess`, on `FxcPreprocessor.cpp` |
| `src/gal/FxcPreprocessor.cpp` | d3dcompiler_47's preprocessor, byte for byte: the effect translator reads its exact text (checked on Windows by `tests/fxc`) |
| `src/gal/ShaderCompilerAndroid.cpp` | The effects' shader compiler (`gpg/gal/effects/ShaderCompiler.h`) on DXC, with `ShaderCompiler.cpp`'s arguments (the configure stops when that file changes) |
| `src/gal/DxcAndroid.cpp` | DXC itself: `libdxcompiler.so` loaded from the app's libraries (or `FAF_DXC_DIR`), a compiler per thread, the reflection; compiled with DXC's headers alone (`faf_android_dxc`) |
| `src/gal/ImageCodecAndroid.cpp` | The D3DX11 texture codec: DDS with BC1-3 and a full chain through the engine's direct path, everything else as D3DX11 loads it (its formats, padding and mip chain; generated and converted levels close to D3DX11's, not equal), PNG/TGA/BMP from `ImageFilesAndroid.cpp` |
| `src/gal/BlockCompressionAndroid.cpp` | BC1-3 on the CPU: WARP's decode (for the codec and for GPUs without BC, patches/README.md), and an encoder after D3DX's |
| `src/gal/GalAvailabilityAndroid.cpp` | The game's answer to "is there a compiler, a codec?": the compiler when DXC loaded, the codec always |
| `src/gal/GalInterfacesAndroid.cpp` | Five gal interfaces' constructors and destructors, which the engine keeps in the Direct3D 9 backend's file |
| `src/gal/D3D9Unavailable.cpp` | The Direct3D 9 methods and mesh formatters shared code names, on paths only a Direct3D 9 device takes |
| `src/gal/MeshFormattersAndroid.cpp` | The Direct3D 10 mesh formatters, which every other API packs its vertices with, the engine's code with a float-to-half conversion that matches D3DX's |
| `src/gal/D3D12SwitchesAndroid.cpp` | The storage of two Direct3D 12 console switches |
| `src/net/DatagramSocketAndroid.cpp` | The game discovery's broadcast socket: none, as outside Windows (the Winsock transport is Windows only) |
| `src/net/NetlinkPeersAndroid.cpp` | The in-engine peer links and native networking, off: netlink (libjuice, libdatachannel) is not built for Android yet (C5); asking for them says so in the log |
| `src/app/EntityDialogAndroid.cpp` | `SC_CreateEntityDialog`: no reflection editor (a wx tool window) |
| `src/gal/D3DXMathAndroid.cpp` | `D3DXMatrixLookAtRH`, which the shadows call: D3DX's own C routine, checked against the DLL |
| `src/host/GameHost.cpp` | The host: `WinMain`'s program on the engine's thread, with the command line, HOME and a Windows user profile's folders; the surface, the pointer, the keys and the foreground into the message layer; a report of its own |
| `src/host/KeysAndroid.cpp` | Android's key codes as a PC keyboard's: virtual keys, set-1 scan codes, the extended keys |
| `src/wx` | wx-lite (`WX_LITE.md`) |
| `src/smoke/GalSmoke.cpp` | The renderer test |
| `src/smoke/GalSmokeDoubles.cpp` | The renderer test's answer: compiler and codec count as there, so the device comes up |

Where a file copies engine code, it names the original and keeps it unchanged.

### The shader compiler

The Vulkan backend compiles the effects with DXC, which Microsoft does not build for Android.
`cmake/FafAndroidDxc.cmake` builds `libdxcompiler.so` from the sources of the release the Windows
build downloads (v1.9.2609), pinned by SHA256 and cached in `dependencies/.downloads`: first its
table generators for the build host, then the library for the ABI (target `faf_dxc`, a few minutes
the first time). The app packages it with the engine's libraries and its licences in the assets
(`licenses/`).

### The fonts

The font renderer (`moho/render/d3d/CD3DFont.cpp`) draws each new character with GDI into a bitmap
and turns that into a texture. `src/win32/GdiAndroid.cpp` has the GDI functions it calls;
`src/win32/GdiFonts.cpp` is what GDI does behind them, on FreeType.
`cmake/FafAndroidFreetype.cmake` builds FreeType 2.14.3 from source as a static library of this
build, pinned by SHA256 and cached in `dependencies/.downloads`, without any of its optional
libraries; its licence goes to `licenses/`. FreeType reads and hints the fonts; these are GDI's own:

- **The face.** GDI goes by the family name alone, whatever the case. `"Arial Bold"`, a full name
  the game's Lua asks for, is unknown to it, as are `"Arial Gras"` and `"Andale Mono"`, so they are
  Arial's regular style. (On Windows such a name finds Arial, but the bitmap font MS Sans Serif at the
  sizes that has exactly: 16, 18 and 24 pixels. Android has no such font.) Fonts Windows has installed
  and the game does not bring (`"Courier New"`) are Arial here too.
- **The metrics.** The ascent and descent from the VDMX table at the sizes it lists, else scaled from
  the OS/2 table's Windows extents; TEXTMETRIC's other fields by the formulas of Wine's GDI.
- **The advances.** Whole pixels, from the `hdmx` table at its sizes, else scaled where the font says
  no instruction changes them (`head`, `LTSH`), else hinted. FreeType runs the instructions as
  Windows does without ClearType (interpreter version 35), and its auto-hinter stays out.
- **The glyphs.** The font's `gasp` table says whether a size has grey levels, even for
  `ANTIALIASED_QUALITY`: Arial is bi-level from 9 to 17 pixels. Without the table GDI takes those same
  sizes. A grey pixel counts which of its 4 x 4 samples are inside the hinted outline: GDI's 17
  levels, with the intensities Wine measured, and nothing for a single sample.

A character a face lacks is its missing glyph here; GDI takes it from a font linked to the face, where
Windows links one (CJK, for instance). That is open.

## Tests

`faf_wx_lite_tests` (`tests/wx`) checks wx-lite's event system, the Win32 message layer and the
translation of its messages into wx events against wxWindows 2.4.2 and Win32, and GDI's fonts. It is
linked statically, so the same program runs on the emulator and on a Linux host; the app's workflow runs
the arm64 build on an arm64 runner. It takes the simulation's objects from an archive
(`faf_game_sim_archive`), so that only the engine's POSIX layer is linked in, and stands in for the
three functions of liblog and libandroid the layer calls (`tests/wx/NdkDoubles.cpp`). On the
x86_64 emulator:

```sh
adb push build/android-x64/bin/faf_wx_lite_tests /data/local/tmp/
adb shell "chmod 755 /data/local/tmp/faf_wx_lite_tests && /data/local/tmp/faf_wx_lite_tests"
```

It prints each test's name and the checks that failed, then `<n> tests, <m> failed`, and exits
with 0 when every check passed. The emulator's ARM translation runs no static executables, so the
arm64 build runs on real ARM only (the CI runner).

`faf_wx_lite_tests` also checks GDI's fonts (`tests/wx/GdiTests.cpp`) on TrueType files it makes
itself: boxes whose edges fall where the metrics and pixels can be worked out by hand.

`gdifont_compare` (`tests/gdifont`, a CMake project of its own for a Windows host) registers a
folder's fonts (the game's `fonts/`, copied somewhere outside the repository) with GDI as the game
does and with `GdiFonts.cpp`, and compares for the faces and sizes the game's Lua asks for: the face
each picks, and on the file GDI took, TEXTMETRIC, the advances, the kerning pairs and the glyphs pixel
by pixel. With the game's 14 fonts and those Windows brought for the faces it has, at 8 to 32, 36 and
48 pixels (414 fonts): the metrics are equal in all, 67 of 119952 advances differ, 12 of 903123
kerning pairs; 85 % of the glyphs are equal, and 1.1 % of the inked pixels differ, most of those by a
grey level.

`imagecodec_compare` (`tests/imagecodec`, a CMake project of its own for a Windows host) decodes
texture files with D3DX11 on WARP as `ImageCodec.cpp` does and with the Android codec, and compares
format, shape and bytes by route (`files <list>`, the files extracted from the game's archives);
`blocks <seed> <count>` checks the CPU decode of BC1-3 against WARP's, texel for texel.

## Patches

`patches/<path>.patch` is a `git diff` of the engine file `<path>`. At configure time
`cmake/FafAndroidOverlay.cmake` copies the file to `build/android-<abi>/overlay/<path>` and
applies the patch to the copy. A patched source replaces the original in the targets; a patched
header is found first through the overlay folder. The repository's file is never touched.

Each patch is in `patches/README.md` with the reason. When the engine changes a patched file so
that its patch no longer applies, the configure stops and names the file. To make the patch again:

```sh
git apply android/native/patches/<path>.patch       # if it still applies in part, or edit by hand
# ... edit <path> in the repository until it is what Android needs ...
git diff <path> > android/native/patches/<path>.patch
git checkout -- <path>                               # the repository keeps the engine's file
```

Keep a patch to what Android cannot do another way: a missing Win32 or DirectX name belongs in
`include/` and `src/`, a Windows-only header that shared code includes in `include/shadow`.
