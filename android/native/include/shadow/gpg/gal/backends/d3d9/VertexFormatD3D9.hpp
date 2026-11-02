#pragma once

// Stand-in for src/gpg/gal/backends/d3d9/VertexFormatD3D9.hpp in the Android client build
// (android/native/include/shadow/README.md): the engine headers the real one pulls in, and the
// class's name.

#include <cstddef>
#include <cstdint>

#include <d3d9.h>

#include "gpg/gal/VertexFormat.hpp"

namespace gpg::gal
{
  class VertexFormatD3D9;
} // namespace gpg::gal
