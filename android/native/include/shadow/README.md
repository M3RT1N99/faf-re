# Stand-ins for the Windows-only backends' headers

Shared engine files (render, mesh, application) include headers of the Direct3D 9 and 10 backends,
which the Android build leaves out. The real headers pull in the DirectX SDK. A stand-in here has
the same path below `gpg/gal/backends`, comes first on the include path, and gives:

- the engine headers its original includes, so the including files get what they got before;
- the forward declarations it makes;
- a class only where shared code uses one, with only the members it calls.

No Direct3D 9 or 10 device is created on Android, so those members are never reached.
`../../src/gal/D3D9Unavailable.cpp` defines them anyway, so the libraries link.

| Stand-in | Shared code that includes it | Used beyond its name |
|---|---|---|
| `d3d9/DeviceD3D9.hpp` | render, mesh, application files | no |
| `d3d9/TextureD3D9.hpp` | render and mesh files | no |
| `d3d9/IndexBufferD3D9.hpp`, `VertexBufferD3D9.hpp`, `VertexFormatD3D9.hpp` | `moho/mesh/HardwareMeshBatch.cpp`, `moho/render/d3d/CD3DDeviceResources.cpp` | no |
| `d3d9/EffectTechniqueD3D9.hpp` | `moho/mesh/HardwareMeshBatch.cpp` | `CommitChanges`, in the open-pass draw, which only a Direct3D 9 device takes |
| `d3d9/RenderTargetD3D9.hpp` | `moho/render/Shadow.cpp` | `GetSurface`, in the shadow-map dump (`FAF_TOGGLE_DIR`): null here |
| `d3d10/DeviceD3D10.hpp` | `moho/app/StartupClientHelpers.cpp` | no |

The vertex formatters' headers (`Float16HardwareVertexFormatterD3D9.hpp` and the others) need no
stand-in: they include only engine headers.
