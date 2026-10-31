#pragma once

// The Direct3D 9 types that the engine's shared render code uses outside the Windows-only render
// backends (gpg/gal/backends/d3d9, d3d10, d3d11, d3d12, dxgi), for the builds without the DirectX
// SDK: Android. The gal device interface takes a D3DVIEWPORT9, and texture sheets lock into a
// D3DLOCKED_RECT, whichever backend draws. Layouts as the SDK's d3d9types.h declares them.

#include <windows.h> // DWORD, INT: platform/posix/win32/Windows.h

typedef struct _D3DVIEWPORT9
{
  DWORD X;
  DWORD Y;
  DWORD Width;
  DWORD Height;
  float MinZ;
  float MaxZ;
} D3DVIEWPORT9;

typedef struct _D3DLOCKED_RECT
{
  INT Pitch;
  void* pBits;
} D3DLOCKED_RECT;
