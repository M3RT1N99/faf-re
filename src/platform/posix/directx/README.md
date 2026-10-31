# DirectX headers for builds without the Windows SDK

Stand-ins for the DirectX headers that the engine's shared render code and the Vulkan backend
include, for the Android client build (android/CLIENT_PLAN.md). The Windows builds use the SDK's
own headers and never see these.

| File | What it is |
|---|---|
| `dxgiformat.h` | Unchanged from Microsoft's [DirectX-Headers](https://github.com/microsoft/DirectX-Headers) v1.721.2 (`include/directx/dxgiformat.h`, MIT licence, notice in the file): the `DXGI_FORMAT` enumeration the Vulkan backend maps its formats from. |
| `d3d9types.h` | The two Direct3D 9 structs the shared code uses outside the Windows-only backends, `D3DVIEWPORT9` and `D3DLOCKED_RECT`, with the SDK's layouts. `gpg/gal/D3D9Utils.h` includes it where the SDK is missing. |
| `d3d9.h` | Includes `d3d9types.h`: the shared files that include `<d3d9.h>` use only its types. |
