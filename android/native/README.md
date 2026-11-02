# The Android native build

The engine for Android, and the game client on top of it, without changing a file of the engine.
The engine's own tree (`src/`, `cmake/`, the root `CMakeLists.txt`) stays as the repository has
it, and so does its Windows build. Everything Android needs beyond it is in this folder.

```sh
cd android/native
cmake --preset android-arm64          # or android-x64 for the emulator
cmake --build --preset android-arm64  # headless, faf_sim_math_tests, fafgalsmoke
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
| `faf_gal` | The renderer (`gpg/gal`): the Vulkan backend and what it shares with the others |
| `faf_client` | The rest of the client; outside the default build until it compiles (CLIENT_PLAN.md, C1) |
| `fafgame` | `libfafgame.so`, all of the above; likewise |
| `fafgalsmoke` | `libfafgalsmoke.so`, the renderer test (card 4 of the app) |

The client's files are the engine's (`src/sources.cmake`) minus the simulation's
(`src/sim_sources.cmake`) and minus the Windows-only ones (`CMakeLists.txt`,
`_faf_client_windows_only`: the Direct3D and DXGI backends, Effects 11, the wx tool windows).

### Headers

The client's targets put four folders ahead of the engine's include path, in this order:

| Folder | What |
|---|---|
| `build/android-<abi>/overlay/src` | Patched engine headers (see Patches) |
| `include/shadow` | Stand-ins for the Windows-only backends' headers that shared code includes (`include/shadow/README.md`) |
| `include/win32` | The Win32 names the client needs beyond the engine's POSIX layer: `Windows.h` adds them (COM's base, the registry, `OVERLAPPED`, ...) and continues with `#include_next` to the engine's own `src/platform/posix/win32/Windows.h`; `shlobj.h`, `mmsystem.h`, `mmreg.h` and `dsound.h` do the same for the engine's headers of those names |
| `include/directx` | DirectX type stand-ins (`include/directx/README.md`) |

An HWND of the engine is an `ANativeWindow*` here: the surface an activity hands over.
`GetClientRect` gives its size, and the host reports the window in front with
`SetForegroundWindow` (`src/win32/Win32Android.cpp`).

### Sources

| File | Stands for |
|---|---|
| `src/win32/Win32Android.cpp` | The Win32 functions `include/win32/Windows.h` adds, on the engine's POSIX layer's public functions; no COM class and no registry key exist |
| `src/audio/AudioAndroid.cpp` | The audio platform: none yet, so the game is silent as on a PC without XACT and a sound device (`DirectSoundCreate`, X3DAudio, the XACT clock) |
| `src/gal/VulkanSurfaceAndroid.cpp` | The Vulkan backend's platform unit (`VulkanSurface.h`) for Android: `VK_KHR_android_surface` |
| `src/gal/WindowsCursorAndroid.cpp` | The backends' Win32 cursor helper: no system cursor |
| `src/gal/D3DCompilerAndroid.cpp` | `D3DPreprocess`: no effect preprocessor yet (C2) |
| `src/gal/ShaderCompilerAndroid.cpp` | The effects' DXC compiler: none yet (C2) |
| `src/gal/ImageCodecAndroid.cpp` | The D3DX11 texture codec: none yet (C2) |
| `src/gal/GalAvailabilityAndroid.cpp` | The game's answer to "is there a compiler, a codec?": no |
| `src/gal/GalInterfacesAndroid.cpp` | Five gal interfaces' constructors and destructors, which the engine keeps in the Direct3D 9 backend's file |
| `src/gal/D3D9Unavailable.cpp` | The two Direct3D 9 methods shared code calls on paths only a Direct3D 9 device takes |
| `src/smoke/GalSmoke.cpp` | The renderer test |
| `src/smoke/GalSmokeDoubles.cpp` | The renderer test's answer: compiler and codec count as there, so the device comes up |

Where a file copies engine code, it names the original and keeps it unchanged.

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
