#pragma once

// The Direct3D 11 types the Vulkan backend shares with the Direct3D 11 backend, for the builds
// without the Windows SDK (Android): the input-layout element of the vertex layout tables
// (gpg/gal/backends/d3d11/D3D11VertexLayouts.h). Layouts and values as the SDK's d3d11.h
// declares them. The Direct3D 11 device interfaces belong to the Windows-only backend.

#include <Windows.h> // LPCSTR, UINT: platform/posix/win32/Windows.h
#include <dxgiformat.h>

#define D3D11_APPEND_ALIGNED_ELEMENT (0xffffffff)

typedef enum D3D11_INPUT_CLASSIFICATION
{
  D3D11_INPUT_PER_VERTEX_DATA = 0,
  D3D11_INPUT_PER_INSTANCE_DATA = 1
} D3D11_INPUT_CLASSIFICATION;

typedef struct D3D11_INPUT_ELEMENT_DESC
{
  LPCSTR SemanticName;
  UINT SemanticIndex;
  DXGI_FORMAT Format;
  UINT InputSlot;
  UINT AlignedByteOffset;
  D3D11_INPUT_CLASSIFICATION InputSlotClass;
  UINT InstanceDataStepRate;
} D3D11_INPUT_ELEMENT_DESC;
