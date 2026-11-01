// Engine addition (android/CLIENT_PLAN.md, C1): ImageCodec.h where d3dx11_43.dll cannot be
// loaded, in place of ImageCodec.cpp. There is no codec, as on a Windows host without the
// DirectX End-User Runtime: Instance() is null and the backends take that path. C2 makes the
// codec portable (the direct DDS load and a decoder without D3DX). Image::Allocate and
// FromGalFormat are ImageCodec.cpp's, unchanged.
//
// FAF_GAL_SMOKE_TEST (the renderer test library) gives an instance whose decodes fail, so the
// Vulkan device comes up for a test without textures.

#include "ImageCodec.h"

#include <algorithm>

#include "gpg/core/utils/Logging.h"
#include "gpg/gal/backends/dxgi/DxgiFormats.h"

namespace gpg::gal::imaging
{
  struct ImageCodec::State
  {
  };

  void Image::Allocate(
    const DXGI_FORMAT imageFormat,
    const std::uint32_t imageWidth,
    const std::uint32_t imageHeight,
    const std::uint32_t levels,
    const std::uint32_t slices
  )
  {
    format = imageFormat;
    width = std::max(imageWidth, 1U);
    height = std::max(imageHeight, 1U);
    depth = 1U;
    mipLevels = std::max(levels, 1U);
    arraySize = std::max(slices, 1U);
    subresources.clear();
    std::size_t offset = 0U;
    for (std::uint32_t slice = 0U; slice < arraySize; ++slice) {
      for (std::uint32_t mip = 0U; mip < mipLevels; ++mip) {
        Subresource subresource;
        subresource.offset = offset;
        subresource.width = std::max(1U, width >> mip);
        subresource.height = std::max(1U, height >> mip);
        const dxgi::LevelLayout layout = dxgi::ComputeLevelLayout(format, subresource.width, subresource.height);
        subresource.rowPitch = layout.rowPitch;
        subresource.rowCount = layout.rowCount;
        subresource.slicePitch = layout.ByteSize();
        offset += subresource.slicePitch;
        subresources.push_back(subresource);
      }
    }
    bytes.assign(offset, 0U);
  }

  bool gDdsDirectLoad = true;
  bool gTextureDecodeCache = true;

  ImageCodec* ImageCodec::Instance()
  {
#if defined(FAF_GAL_SMOKE_TEST)
    static ImageCodec* const sCodec = new ImageCodec();
    return sCodec;
#else
    static const bool sReported = [] {
      gpg::Warnf("imaging: no texture codec on this platform yet (android/CLIENT_PLAN.md C2)");
      return true;
    }();
    (void)sReported;
    return nullptr;
#endif
  }

  ImageCodec::~ImageCodec() = default;

  bool ImageCodec::Load()
  {
    return false;
  }

  const char* ImageCodec::LastDecodeRoute() noexcept
  {
    return "none";
  }

  HRESULT ImageCodec::Decode(const void*, std::size_t, const DecodeOptions&, Image&)
  {
    return E_NOTIMPL;
  }

  HRESULT ImageCodec::GetImageSize(const void*, std::size_t, std::uint32_t&, std::uint32_t&)
  {
    return E_NOTIMPL;
  }

  HRESULT ImageCodec::DecodeWithD3dx(const void*, std::size_t, const DecodeOptions&, Image&)
  {
    return E_NOTIMPL;
  }

  HRESULT ImageCodec::Encode(const Image&, FileFormat, std::vector<std::uint8_t>&)
  {
    return E_NOTIMPL;
  }

  HRESULT ImageCodec::EncodeToFile(const Image&, FileFormat, const char*)
  {
    return E_NOTIMPL;
  }

  FileFormat ImageCodec::FromGalFormat(const int galFormat) noexcept
  {
    // The D3D10 table (0x00D430A0): BMP, JPG, PNG, PNG, DDS.
    constexpr FileFormat kFormats[5] = {FileFormat::Bmp, FileFormat::Jpg, FileFormat::Png, FileFormat::Png, FileFormat::Dds};
    return (galFormat >= 0 && galFormat < 5) ? kFormats[galFormat] : FileFormat::Bmp;
  }
} // namespace gpg::gal::imaging
