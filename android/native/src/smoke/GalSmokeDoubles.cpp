// The renderer test's answers to the two questions the Vulkan backend asks before it creates a
// device (DeviceVulkan::Setup), in place of the game's (src/gal/GalAvailabilityAndroid.cpp). The
// test draws without effects or textures, so it only needs the device: the compiler counts as
// available and a codec exists. Every compile and every decode still fails, with
// ShaderCompilerAndroid.cpp's and ImageCodecAndroid.cpp's messages.

#include "gpg/gal/backends/common/ImageCodec.h"
#include "gpg/gal/effects/ShaderCompiler.h"

namespace gpg::gal
{
  bool effects::ShaderCompiler::Available() const noexcept
  {
    return true;
  }

  imaging::ImageCodec* imaging::ImageCodec::Instance()
  {
    static ImageCodec* const sCodec = new ImageCodec();
    return sCodec;
  }
} // namespace gpg::gal
