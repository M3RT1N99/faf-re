// The game's answers to the two questions the Vulkan backend asks before it creates a device
// (DeviceVulkan::Setup): is there a shader compiler, is there a texture codec? On Android there
// is neither yet (ShaderCompilerAndroid.cpp, ImageCodecAndroid.cpp), so device creation stops
// with the compiler's LoadError, as on a Windows host without DXC. The renderer test answers
// differently (src/smoke/GalSmokeDoubles.cpp).

#include "gpg/core/utils/Logging.h"
#include "gpg/gal/backends/common/ImageCodec.h"
#include "gpg/gal/effects/ShaderCompiler.h"

namespace gpg::gal
{
  bool effects::ShaderCompiler::Available() const noexcept
  {
    return false;
  }

  imaging::ImageCodec* imaging::ImageCodec::Instance()
  {
    static const bool sReported = [] {
      gpg::Warnf("imaging: no texture codec on Android yet (android/CLIENT_PLAN.md, C2)");
      return true;
    }();
    (void)sReported;
    return nullptr;
  }
} // namespace gpg::gal
