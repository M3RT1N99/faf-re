# DirectX headers for the Android client build

Stand-ins for the DirectX headers that the engine's shared render code and the Vulkan backend
include. Only the Android client build (`android/native`) has this directory on its include path;
the Windows builds use the SDK's own headers.

| File | What it is |
|---|---|
| `dxgiformat.h` | Unchanged from Microsoft's [DirectX-Headers](https://github.com/microsoft/DirectX-Headers) v1.721.2 (`include/directx/dxgiformat.h`, MIT licence, notice in the file): the `DXGI_FORMAT` enumeration the Vulkan backend maps its formats from. |
| `d3d9types.h` | The two Direct3D 9 structs the shared code uses outside the Windows-only backends, `D3DVIEWPORT9` and `D3DLOCKED_RECT`, with the SDK's layouts. The patched `gpg/gal/D3D9Utils.h` includes it. |
| `d3d9.h` | Includes `d3d9types.h`, and declares `IDirect3DSurface9` without methods for the shadow-map dump's stand-in. |
| `d3d11.h` | The input-layout description the Direct3D 11 vertex layouts give the Vulkan backend. |
| `d3dcompiler.h` | `D3DPreprocess`, `D3D_SHADER_MACRO` and the blob, as the Vulkan backend's effect creation calls them. `src/gal/D3DCompilerAndroid.cpp` implements the function. |
| `wrl/client.h` | `Microsoft::WRL::ComPtr`, the subset the engine uses, with WRL's ownership rules. |
