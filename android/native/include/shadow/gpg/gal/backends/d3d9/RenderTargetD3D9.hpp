#pragma once

// Stand-in for src/gpg/gal/backends/d3d9/RenderTargetD3D9.hpp in the Android client build
// (android/native/include/shadow/README.md). moho/render/Shadow.cpp's shadow-map dump names
// GetSurface, a Direct3D 9 surface it saves with D3DX: declared here, defined in
// android/native/src/gal/D3D9Unavailable.cpp.

#include <cstddef>

#include <d3d9.h>

#include "gpg/gal/RenderTarget.hpp"
#include "gpg/gal/RenderTargetContext.hpp"

namespace gpg::gal
{
  class RenderTargetD3D9 : public RenderTarget
  {
  public:
    /// No Direct3D 9 surface exists here: null.
    IDirect3DSurface9* GetSurface();
  };
} // namespace gpg::gal
