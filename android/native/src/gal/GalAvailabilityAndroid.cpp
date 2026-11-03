// The game's answers to the two questions the Vulkan backend asks before it creates a device
// (DeviceVulkan::Setup): is there a shader compiler, is there a texture codec? The compiler is
// there when DXC loaded (ShaderCompilerAndroid.cpp), as on Windows. The codec is always there
// (ImageCodecAndroid.cpp: no D3DX, nothing to load). The renderer test answers differently
// (src/smoke/GalSmokeDoubles.cpp).

#include "gpg/gal/backends/common/ImageCodec.h"
#include "gpg/gal/effects/ShaderCompiler.h"

namespace gpg::gal
{
  bool effects::ShaderCompiler::Available() const noexcept
  {
    return mLibrary != nullptr;
  }

  imaging::ImageCodec* imaging::ImageCodec::Instance()
  {
    static ImageCodec* const sCodec = new ImageCodec();
    return sCodec;
  }
} // namespace gpg::gal
