#pragma once

// <d3dcompiler.h> for the Android client build: what the Vulkan backend's effect creation calls,
// d3dcompiler_47's preprocessor (DeviceVulkan::CreateEffect). The declarations follow the
// Windows SDK; android/native/src/gal/D3DCompilerAndroid.cpp has the function.

#include <Windows.h>

struct D3D_SHADER_MACRO
{
  LPCSTR Name;
  LPCSTR Definition;
};

/// The include handler; the engine passes null.
struct ID3DInclude;

/// d3dcommon.h's blob: a buffer with a COM lifetime.
struct ID3D10Blob : IUnknown
{
  virtual LPVOID GetBufferPointer() = 0;
  virtual SIZE_T GetBufferSize() = 0;
};
using ID3DBlob = ID3D10Blob;

/// The effect source `data` with `defines` applied, in `*codeText`; on failure the reason in
/// `*errorMessages`.
HRESULT D3DPreprocess(
  LPCVOID data, SIZE_T bytes, LPCSTR sourceName, const D3D_SHADER_MACRO* defines, ID3DInclude* include, ID3DBlob** codeText,
  ID3DBlob** errorMessages
);
