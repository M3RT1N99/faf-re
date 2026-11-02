#pragma once

// Stand-in for src/gpg/gal/backends/d3d9/DeviceD3D9.hpp in the Android client build, where the
// Direct3D 9 backend does not exist (android/native/include/shadow/README.md). The shared files
// that include the real header use nothing of the device itself; they get the engine headers it
// pulls in and its forward declarations.

#include <cstdint>

#include <d3d9.h>

#include "boost/shared_ptr.h"
#include "boost/weak_ptr.h"
#include "gpg/core/streams/MemBufferStream.h"
#include "gpg/gal/Device.hpp"
#include "gpg/gal/DeviceContext.hpp"
#include "legacy/containers/String.h"
#include "legacy/containers/Vector.h"

namespace gpg::gal
{
  class Head;
  class DeviceContext;
  class Device;
  class CursorContext;
  class OutputContext;
  class CubeRenderTargetContext;
  class DepthStencilTargetContext;
  class EffectContext;
  class IndexBufferContext;
  class RenderTargetContext;
  class TextureContext;
  class VertexBufferContext;
  class CubeRenderTargetD3D9;
  class DepthStencilTargetD3D9;
  class EffectD3D9;
  class IndexBufferD3D9;
  class PipelineStateD3D9;
  class RenderTargetD3D9;
  class TextureD3D9;
  class VertexBufferD3D9;
  class VertexFormatD3D9;
  class DeviceD3D9;
} // namespace gpg::gal
