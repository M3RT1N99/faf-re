# The engine files the Android build compiles changed

Each `<path>.patch` is a `git diff` of the engine file `<path>`, applied to a copy at configure time
(`../cmake/FafAndroidOverlay.cmake`, `../README.md`). The repository's files stay as they are.

| File | Change | Why there is no other way |
|---|---|---|
| `src/gpg/gal/Device.cpp` | The device factory and `SupportsVertexTextureFormat` without the Direct3D 9 to 12 cases and their includes | The factory names every backend's class; Android has the Vulkan backend alone |
| `src/gpg/gal/backends/vulkan/DeviceVulkan.cpp` | The swap chain takes R8G8B8A8 where the window has no B8G8R8A8; copies between the two orders blit, readbacks swap red and blue; the swap chain's usage is what the surface supports; a device without BC textures comes up, and refuses a BC texture | Android's window surfaces have no B8G8R8A8, and phones' GPUs mostly lack BC (the Galaxy S22's Xclipse 920 has ETC2 and ASTC) |
| `src/gpg/gal/backends/vulkan/VulkanLoader.cpp` | Opens `libvulkan.so` | The loader names `vulkan-1.dll`, and the engine's POSIX `LoadLibraryA` is a plain `dlopen` |
| `src/gpg/gal/D3D9Utils.h` | Includes `<d3d9types.h>` where the stand-ins provide it | The gal interfaces take a `D3DVIEWPORT9`; the header includes the type on Windows only |
| `src/moho/render/ID3DTextureSheet.h` | The same, instead of its own `RECT` and `D3DLOCKED_RECT` | Its own `RECT` clashes with the one the engine's POSIX `Windows.h` defines, which the client's files include as well |
| `src/moho/terrain/HighFidelityTerrain.cpp`, `LowFidelityTerrain.cpp`, `MediumFidelityTerrain.cpp`, `water/Shoreline.cpp` | The render-stat counters cast to `platform::InterlockedLong*` | They cast a 32-bit field to `long*`, which is 64-bit on Android; the engine's `platform::InterlockedLong` (`src/platform/Interlocked.h`) is its own type for this cast, as its simulation code uses it |
| `src/moho/audio/CUserSoundManager.cpp`, `app/CScApp.cpp`, `app/WxRuntimeTypes.cpp`, `render/d3d/CD3DDevice.cpp`, `ui/UiRuntimeTypes.cpp` | The stat counters cast to `LONG*`, Win32's 32-bit type, for the Win32 `Interlocked*` functions | The same `long*` cast of a 32-bit field; the engine's POSIX layer declares these functions with `LONG`, as Win32 does |
| `src/moho/console/ClientConCommands.cpp` | `EFX_CreateEmitterWindow` says in the log that the build has no emitter editor; the selection helper only it used goes | The command creates the editor, `WEmitterWx`, a wx tool window that wx-lite leaves out (`../WX_LITE.md`); a stand-in for its constructor would need the whole frame class |

`src/moho/render/ID3DTextureSheet.cpp` is compiled from the overlay as well, unchanged: it includes
`ID3DTextureSheet.h` by its bare name, which would find the unpatched header next to it.
