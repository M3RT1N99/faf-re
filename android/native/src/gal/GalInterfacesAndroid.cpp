// The out-of-line constructors and destructors of five gal interfaces, for the Android client
// build. The engine defines them in backends/d3d9/D3D9BaseClasses.cpp (the binary keeps them with
// the Direct3D 9 backend's code, 0x008F3810 to 0x00902240), which this build leaves out with the
// rest of that backend. Every backend's objects, the Vulkan backend's too, run them.

#include "gpg/gal/CubeRenderTarget.hpp"
#include "gpg/gal/EffectTechnique.hpp"
#include "gpg/gal/IndexBuffer.hpp"
#include "gpg/gal/PipelineState.hpp"
#include "gpg/gal/RenderTarget.hpp"

namespace gpg::gal
{
  EffectTechnique::EffectTechnique() = default;
  EffectTechnique::~EffectTechnique() = default;

  IndexBuffer::IndexBuffer() = default;
  IndexBuffer::~IndexBuffer() = default;

  RenderTarget::RenderTarget() = default;
  RenderTarget::~RenderTarget() = default;

  CubeRenderTarget::CubeRenderTarget() = default;
  CubeRenderTarget::~CubeRenderTarget() = default;

  PipelineState::PipelineState() = default;
  PipelineState::~PipelineState() = default;
} // namespace gpg::gal
