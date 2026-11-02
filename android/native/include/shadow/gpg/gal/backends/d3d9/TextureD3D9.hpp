#pragma once

// Stand-in for src/gpg/gal/backends/d3d9/TextureD3D9.hpp in the Android client build
// (android/native/include/shadow/README.md): the engine headers the real one pulls in, and the
// class's name.

#include <cstddef>
#include <cstdint>

#include <d3d9.h>

#include "gpg/core/streams/MemBufferStream.h"
#include "gpg/gal/D3D9Utils.h"
#include "gpg/gal/Texture.hpp"
#include "gpg/gal/TextureContext.h"

namespace gpg::gal
{
  class TextureD3D9;
} // namespace gpg::gal
