// The game's answers to the two questions the Vulkan backend asks before it creates a device
// (DeviceVulkan::Setup): is there a shader compiler, is there a texture codec? The compiler is
// there when DXC loaded (ShaderCompilerAndroid.cpp), as on Windows. There is no codec yet
// (ImageCodecAndroid.cpp), so device creation stops there. The renderer test answers differently
// (src/smoke/GalSmokeDoubles.cpp).

#include "gpg/core/utils/Logging.h"
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
    static const bool sReported = [] {
      gpg::Warnf("imaging: no texture codec on Android yet (android/CLIENT_PLAN.md, C2)");
      return true;
    }();
    (void)sReported;
    return nullptr;
  }
} // namespace gpg::gal
