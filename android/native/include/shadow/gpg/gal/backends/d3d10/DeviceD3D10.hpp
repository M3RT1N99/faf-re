#pragma once

// Stand-in for src/gpg/gal/backends/d3d10/DeviceD3D10.hpp in the Android client build, where the
// Direct3D 10 backend does not exist (android/native/include/shadow/README.md). The shared files
// that include the real header use nothing of the device itself; they get the engine headers it
// pulls in and its forward declarations.

#include <cstddef>
#include <cstdint>

#include "boost/shared_ptr.h"
#include "boost/weak_ptr.h"
#include "gpg/core/streams/MemBufferStream.h"
#include "gpg/gal/Device.hpp"
#include "gpg/gal/DeviceContext.hpp"
#include "gpg/gal/OutputContext.hpp"
#include "gpg/gal/Texture.hpp"
#include "legacy/containers/String.h"
#include "legacy/containers/Vector.h"

namespace gpg::gal
{
  class Device;
  class OutputContext;
  class CursorContext;
  class DeviceContext;
  class DrawContext;
  class DrawIndexedContext;
  class CubeRenderTargetContext;
  class DepthStencilTargetContext;
  class EffectContext;
  class IndexBufferContext;
  class RenderTargetContext;
  class TextureContext;
  class VertexBufferContext;
  class CubeRenderTargetD3D10;
  class DepthStencilTargetD3D10;
  class Effect;
  class EffectD3D10;
  class PipelineStateD3D10;
  class DeviceD3D10;
} // namespace gpg::gal
