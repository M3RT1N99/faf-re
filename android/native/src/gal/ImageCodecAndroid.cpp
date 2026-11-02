// The texture codec (gpg/gal/backends/common/ImageCodec.h) for the Android client build, in place
// of ImageCodec.cpp, which decodes with D3DX11 on a WARP device (d3dx11_43.dll). There is no
// codec here yet, as on a Windows host without the DirectX End-User Runtime: every decode fails.
// CLIENT_PLAN.md's C2 brings a codec without D3DX.
//
// Image::Allocate, FromGalFormat and the two settings are ImageCodec.cpp's, unchanged: that file
// holds them with the D3DX code. Instance() is in GalAvailabilityAndroid.cpp, so the renderer test
// can give its own answer (src/smoke/GalSmokeDoubles.cpp).

#include "gpg/gal/backends/common/ImageCodec.h"

#include <algorithm>

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
