// The texture codec (gpg/gal/backends/common/ImageCodec.h) for the Android client build, in place
// of ImageCodec.cpp, which decodes with D3DX11 on a WARP device (d3dx11_43.dll). Android has no
// D3DX: this codec gives the Vulkan backend what D3DX11 gives it on Windows, for the options the
// engine passes (DeviceVulkan's CreateTexture: none; GetTexture2D, the UI's batch textures: BC3,
// one level, no filter; UpdateSurface: the destination's format, size and levels).
//
// - DDS files holding BC1/BC2/BC3 with a full mip chain go the engine's direct path
//   (TryReadBlockCompressedDds, ImageCodec.cpp's, unchanged): the same bytes as on Windows.
// - Everything else is D3DX11's load emulated: the format D3DX11 picks (BC1 to BC3 stay, the
//   uncompressed DDS formats, PNG, TGA and BMP become R8G8B8A8, A8 stays), a full mip chain unless
//   the options say otherwise, a BC top level padded to whole blocks (stretched over them by
//   default, the rest transparent black with no filter), the file's levels taken where they fit
//   (a BC level that is not whole blocks compressed again, as D3DX11 does), the others made with
//   a box filter and compressed with BlockCompressionAndroid's encoder. The file's levels come out
//   with D3DX11's bytes. Generated and converted levels are close to D3DX11's, not equal:
//   D3DX11's mip filter and BC compressor are approximated, not reproduced bit for bit (sample of
//   tests/imagecodec: converted UI textures 228 of 250 identical; generated levels within a mean
//   difference of 0.9 per channel, more at odd sides, where D3DX11 filters differently).
//
// The codec has no state and no lock: decodes run in parallel. There is no decode cache
// (gTextureDecodeCache): what took D3DX11 30-40 ms per texture takes a fraction of that here.
//
// Image::Allocate, FromGalFormat and the two settings are ImageCodec.cpp's, unchanged: that file
// holds them with the D3DX code. Instance() is in GalAvailabilityAndroid.cpp, so the renderer test
// can give its own answer (src/smoke/GalSmokeDoubles.cpp).

#include "gpg/gal/backends/common/ImageCodec.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "BlockCompressionAndroid.h"
#include "ImageFilesAndroid.h"
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
    return true;
  }

  namespace
  {
    constexpr std::uint32_t FourCc(const char a, const char b, const char c, const char d) noexcept
    {
      return static_cast<std::uint32_t>(static_cast<std::uint8_t>(a)) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b)) << 8U) |
        (static_cast<std::uint32_t>(static_cast<std::uint8_t>(c)) << 16U) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d)) << 24U);
    }

    [[nodiscard]] std::uint32_t ReadU32(const std::uint8_t* const at) noexcept
    {
      std::uint32_t value = 0U;
      std::memcpy(&value, at, sizeof(value));
      return value;
    }

    // ImageCodec.cpp's TryReadBlockCompressedDds, unchanged.
    /**
     * The DDS fast path (see gDdsDirectLoad): a plain DXT1/3/5 file, 2D or a full cube, whose
     * (skipped-to) top level is a multiple of 4 on both sides, read into `outImage`. Anything
     * else - another format, a DX10 header, a volume, a short file, a size or format override -
     * returns false and goes through D3DX11.
     */
    [[nodiscard]] bool TryReadBlockCompressedDds(
      const std::uint8_t* const data, const std::size_t bytes, const DecodeOptions& options, Image& outImage
    )
    {
      constexpr std::size_t kHeaderBytes = 4U + 124U;
      if (options.format != DXGI_FORMAT_UNKNOWN || options.width != 0U || options.height != 0U || options.mipLevels != 0U) {
        return false;
      }
      if (bytes < kHeaderBytes || ReadU32(data) != FourCc('D', 'D', 'S', ' ') || ReadU32(data + 4) != 124U) {
        return false;
      }
      constexpr std::uint32_t kMipMapCountFlag = 0x20000U;
      constexpr std::uint32_t kFourCcFlag = 0x4U;
      constexpr std::uint32_t kCubemap = 0x200U;
      constexpr std::uint32_t kAllFaces = 0xFC00U;
      constexpr std::uint32_t kVolume = 0x200000U;
      const std::uint32_t flags = ReadU32(data + 8);
      const std::uint32_t height = ReadU32(data + 12);
      const std::uint32_t width = ReadU32(data + 16);
      const std::uint32_t mipCount = (flags & kMipMapCountFlag) != 0U ? std::max(ReadU32(data + 28), 1U) : 1U;
      const std::uint32_t pixelFlags = ReadU32(data + 80);
      const std::uint32_t fourCc = ReadU32(data + 84);
      const std::uint32_t caps2 = ReadU32(data + 112);
      if ((pixelFlags & kFourCcFlag) == 0U || (caps2 & kVolume) != 0U || width == 0U || height == 0U || mipCount > 16U) {
        return false;
      }
      DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
      if (fourCc == FourCc('D', 'X', 'T', '1')) {
        format = DXGI_FORMAT_BC1_UNORM;
      } else if (fourCc == FourCc('D', 'X', 'T', '3')) {
        format = DXGI_FORMAT_BC2_UNORM;
      } else if (fourCc == FourCc('D', 'X', 'T', '5')) {
        format = DXGI_FORMAT_BC3_UNORM;
      } else {
        return false;
      }
      const bool cube = (caps2 & kCubemap) != 0U;
      if (cube && (caps2 & kAllFaces) != kAllFaces) {
        return false;
      }
      // D3DX builds the levels a file lacks (a 512x512 file with one level comes back with
      // ten); only a full chain is read directly.
      std::uint32_t fullChain = 1U;
      for (std::uint32_t side = std::max(width, height); side > 1U; side >>= 1U) {
        ++fullChain;
      }
      if (mipCount != fullChain) {
        return false;
      }
      const std::uint32_t faces = cube ? 6U : 1U;

      // D3DX's FirstMipLevel; the caller retries from level 0 when D3DX refuses the skip.
      std::uint32_t skip = options.skipMipLevels;
      if (skip >= mipCount) {
        skip = 0U;
      }
      const std::uint32_t topWidth = std::max(1U, width >> skip);
      const std::uint32_t topHeight = std::max(1U, height >> skip);
      // Power-of-two sides only: D3DX11 changes the data of a level whose side drops below a
      // multiple of 4 while still above 4 (the 128x6 level of a 256x12 texture).
      const auto powerOfTwo = [](const std::uint32_t side) { return (side & (side - 1U)) == 0U; };
      if (!powerOfTwo(width) || !powerOfTwo(height) || (topWidth % 4U) != 0U || (topHeight % 4U) != 0U) {
        return false;
      }

      // Every face's full chain in the file, to know where each level starts.
      std::vector<std::size_t> levelBytes(mipCount);
      std::size_t faceBytes = 0U;
      for (std::uint32_t mip = 0U; mip < mipCount; ++mip) {
        levelBytes[mip] = dxgi::ComputeLevelLayout(format, std::max(1U, width >> mip), std::max(1U, height >> mip)).ByteSize();
        faceBytes += levelBytes[mip];
      }
      if (bytes - kHeaderBytes < faceBytes * faces) {
        return false;
      }

      outImage.Allocate(format, topWidth, topHeight, mipCount - skip, faces);
      outImage.cube = cube;
      outImage.volume = false;
      for (std::uint32_t face = 0U; face < faces; ++face) {
        std::size_t source = kHeaderBytes + face * faceBytes;
        for (std::uint32_t mip = 0U; mip < mipCount; ++mip) {
          if (mip >= skip) {
            const Image::Subresource& target = outImage.subresources[face * outImage.mipLevels + (mip - skip)];
            std::memcpy(outImage.bytes.data() + target.offset, data + source, target.slicePitch);
          }
          source += levelBytes[mip];
        }
      }
      return true;
    }

    // ---- D3DX11's load, emulated ------------------------------------------------------------------

    /** One level of a file, in the format the file holds it in (`Source::format`). */
    struct Level
    {
      std::uint32_t width = 1;
      std::uint32_t height = 1;
      std::vector<std::uint8_t> data; // ComputeLevelLayout's layout: whole blocks, tight rows
    };

    /** A texture file as D3DX11 reads it, before any conversion the options ask for. */
    struct Source
    {
      DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN; // one SupportedFormat names
      std::uint32_t mipCount = 1;
      bool cube = false;
      std::vector<std::vector<Level>> faces; // [face][mip]
    };

    [[nodiscard]] bool SupportedFormat(const DXGI_FORMAT format) noexcept
    {
      switch (format) {
      case DXGI_FORMAT_R8G8B8A8_UNORM:
      case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
      case DXGI_FORMAT_B8G8R8A8_UNORM:
      case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
      case DXGI_FORMAT_B8G8R8X8_UNORM:
      case DXGI_FORMAT_A8_UNORM:
      case DXGI_FORMAT_R8_UNORM:
        return true;
      default:
        return IsBc123(format);
      }
    }

    /** A level held as `format`, as R8G8B8A8 the way D3DX converts: missing colours 0, alpha 1. */
    [[nodiscard]] std::vector<std::uint8_t> ToRgba(const DXGI_FORMAT format, const std::uint8_t* const data, const std::uint32_t width,
                                                   const std::uint32_t height)
    {
      const std::size_t texels = static_cast<std::size_t>(width) * height;
      std::vector<std::uint8_t> rgba(texels * 4U);
      if (IsBc123(format)) {
        DecodeBcLevel(format, data, dxgi::ComputeLevelLayout(format, width, height).rowPitch, width, height, rgba.data());
        return rgba;
      }
      for (std::size_t texel = 0U; texel < texels; ++texel) {
        std::uint8_t* const out = rgba.data() + texel * 4U;
        switch (format) {
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8X8_UNORM:
          out[0] = data[texel * 4U + 2U];
          out[1] = data[texel * 4U + 1U];
          out[2] = data[texel * 4U];
          out[3] = format == DXGI_FORMAT_B8G8R8X8_UNORM ? 255U : data[texel * 4U + 3U];
          break;
        case DXGI_FORMAT_A8_UNORM:
          out[0] = out[1] = out[2] = 0U;
          out[3] = data[texel];
          break;
        case DXGI_FORMAT_R8_UNORM:
          out[0] = data[texel];
          out[1] = out[2] = 0U;
          out[3] = 255U;
          break;
        default:
          std::memcpy(out, data + texel * 4U, 4U);
          break;
        }
      }
      return rgba;
    }

    /** R8G8B8A8 texels as a level of `format`. */
    [[nodiscard]] std::vector<std::uint8_t> FromRgba(const DXGI_FORMAT format, const std::vector<std::uint8_t>& rgba, const std::uint32_t width,
                                                     const std::uint32_t height)
    {
      const dxgi::LevelLayout layout = dxgi::ComputeLevelLayout(format, width, height);
      std::vector<std::uint8_t> data(layout.ByteSize());
      if (IsBc123(format)) {
        EncodeBcLevel(format, rgba.data(), width, height, data.data(), layout.rowPitch);
        return data;
      }
      const std::size_t texels = static_cast<std::size_t>(width) * height;
      for (std::size_t texel = 0U; texel < texels; ++texel) {
        const std::uint8_t* const in = rgba.data() + texel * 4U;
        switch (format) {
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8X8_UNORM:
          data[texel * 4U] = in[2];
          data[texel * 4U + 1U] = in[1];
          data[texel * 4U + 2U] = in[0];
          data[texel * 4U + 3U] = format == DXGI_FORMAT_B8G8R8X8_UNORM ? 255U : in[3];
          break;
        case DXGI_FORMAT_A8_UNORM:
          data[texel] = in[3];
          break;
        case DXGI_FORMAT_R8_UNORM:
          data[texel] = in[0];
          break;
        default:
          std::memcpy(data.data() + texel * 4U, in, 4U);
          break;
        }
      }
      return data;
    }

    /** Which source texels a destination texel covers along one side, and how much of each. */
    struct Span
    {
      std::uint32_t first = 0;
      std::uint32_t count = 0;
      float weights[3] = {};
    };

    /**
     * The texels of a side of `from` that each of `to` texels covers (a box over the source): two
     * halves for an even side, and for an odd one parts of three, so no texel is lost.
     */
    [[nodiscard]] std::vector<Span> Spans(const std::uint32_t from, const std::uint32_t to)
    {
      std::vector<Span> spans(to);
      const float scale = static_cast<float>(from) / static_cast<float>(to);
      for (std::uint32_t at = 0U; at < to; ++at) {
        const float begin = static_cast<float>(at) * scale;
        const float end = std::min(static_cast<float>(at + 1U) * scale, static_cast<float>(from));
        Span& span = spans[at];
        span.first = static_cast<std::uint32_t>(begin);
        for (std::uint32_t texel = span.first; static_cast<float>(texel) < end && span.count < 3U; ++texel) {
          const float covered = std::min(end, static_cast<float>(texel + 1U)) - std::max(begin, static_cast<float>(texel));
          span.weights[span.count++] = covered / (end - begin);
        }
      }
      return spans;
    }

    /**
     * The next mip level: each texel the mean of the texels above it, a 2x2 box where the sides are
     * even, weighted over three texels where one is odd. D3DX11's default mip filter comes close to
     * this but is not the same (tests/imagecodec measures it).
     */
    [[nodiscard]] std::vector<std::uint8_t> HalveRgba(const std::vector<std::uint8_t>& rgba, const std::uint32_t width, const std::uint32_t height,
                                                      const std::uint32_t newWidth, const std::uint32_t newHeight)
    {
      std::vector<std::uint8_t> out(static_cast<std::size_t>(newWidth) * newHeight * 4U);
      const std::vector<Span> columns = Spans(width, newWidth);
      const std::vector<Span> rows = Spans(height, newHeight);
      for (std::uint32_t y = 0U; y < newHeight; ++y) {
        const Span& row = rows[y];
        for (std::uint32_t x = 0U; x < newWidth; ++x) {
          const Span& column = columns[x];
          float sums[4] = {};
          for (std::uint32_t j = 0U; j < row.count; ++j) {
            for (std::uint32_t i = 0U; i < column.count; ++i) {
              const float weight = row.weights[j] * column.weights[i];
              const std::uint8_t* const texel = rgba.data() + (static_cast<std::size_t>(row.first + j) * width + column.first + i) * 4U;
              for (int channel = 0; channel < 4; ++channel) {
                sums[channel] += weight * static_cast<float>(texel[channel]);
              }
            }
          }
          for (int channel = 0; channel < 4; ++channel) {
            out[(static_cast<std::size_t>(y) * newWidth + x) * 4U + channel] = static_cast<std::uint8_t>(std::min(sums[channel] + 0.5f, 255.0f));
          }
        }
      }
      return out;
    }

    /**
     * The image fitted into a new size: with no filter at the top left and the rest transparent
     * black (D3DX11_FILTER_NONE); else stretched with a bilinear filter, where D3DX11's default
     * is a triangle filter.
     */
    [[nodiscard]] std::vector<std::uint8_t> FitRgba(const std::vector<std::uint8_t>& rgba, const std::uint32_t width, const std::uint32_t height,
                                                    const std::uint32_t newWidth, const std::uint32_t newHeight, const ResizeFilter filter)
    {
      std::vector<std::uint8_t> out(static_cast<std::size_t>(newWidth) * newHeight * 4U, 0U);
      if (filter == ResizeFilter::None) {
        for (std::uint32_t y = 0U; y < std::min(height, newHeight); ++y) {
          std::memcpy(out.data() + static_cast<std::size_t>(y) * newWidth * 4U, rgba.data() + static_cast<std::size_t>(y) * width * 4U,
                      static_cast<std::size_t>(std::min(width, newWidth)) * 4U);
        }
        return out;
      }
      const auto source = [](const std::uint32_t at, const std::uint32_t from, const std::uint32_t to) {
        return std::clamp((static_cast<float>(at) + 0.5f) * static_cast<float>(from) / static_cast<float>(to) - 0.5f, 0.0f,
                          static_cast<float>(from - 1U));
      };
      for (std::uint32_t y = 0U; y < newHeight; ++y) {
        const float sourceY = source(y, height, newHeight);
        const auto y0 = static_cast<std::uint32_t>(sourceY);
        const std::uint32_t y1 = std::min(y0 + 1U, height - 1U);
        const float fy = sourceY - static_cast<float>(y0);
        for (std::uint32_t x = 0U; x < newWidth; ++x) {
          const float sourceX = source(x, width, newWidth);
          const auto x0 = static_cast<std::uint32_t>(sourceX);
          const std::uint32_t x1 = std::min(x0 + 1U, width - 1U);
          const float fx = sourceX - static_cast<float>(x0);
          for (std::uint32_t channel = 0U; channel < 4U; ++channel) {
            const auto at = [&](const std::uint32_t sx, const std::uint32_t sy) {
              return static_cast<float>(rgba[(static_cast<std::size_t>(sy) * width + sx) * 4U + channel]);
            };
            const float upper = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * fx;
            const float lower = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * fx;
            out[(static_cast<std::size_t>(y) * newWidth + x) * 4U + channel] = static_cast<std::uint8_t>(upper + (lower - upper) * fy + 0.5f);
          }
        }
      }
      return out;
    }

    [[nodiscard]] std::uint32_t FullChain(const std::uint32_t width, const std::uint32_t height) noexcept
    {
      std::uint32_t levels = 1U;
      for (std::uint32_t side = std::max(width, height); side > 1U; side >>= 1U) {
        ++levels;
      }
      return levels;
    }

    /** One channel of a DDS pixel, by its mask, to 8 bits rounded to the nearest. */
    [[nodiscard]] std::uint8_t MaskChannel(const std::uint32_t pixel, const std::uint32_t mask, const std::uint8_t missing) noexcept
    {
      if (mask == 0U) {
        return missing;
      }
      std::uint32_t shift = 0U;
      while (((mask >> shift) & 1U) == 0U) {
        ++shift;
      }
      const std::uint32_t maximum = mask >> shift;
      return static_cast<std::uint8_t>((static_cast<std::uint64_t>((pixel & mask) >> shift) * 255U + maximum / 2U) / maximum);
    }

    /**
     * A DDS file: BC1 to BC3 as blocks, A8 as it is, the other legacy formats as R8G8B8A8 (what
     * D3DX11 makes of A8R8G8B8, X8R8G8B8, R8G8B8, A1R5G5B5, A4R4G4B4, L8, A8L8 ...). A DX10 header
     * is read for the formats SupportedFormat names. Volumes are refused (the game has none).
     */
    [[nodiscard]] HRESULT ReadDds(const std::uint8_t* const data, const std::size_t bytes, Source& out)
    {
      // The header's size field is not checked: D3DX11 reads files that have 24 there
      // (textures/ui/common/hotstats/panel_bmp.dds).
      constexpr std::size_t kHeaderBytes = 4U + 124U;
      if (bytes < kHeaderBytes) {
        return E_FAIL;
      }
      const std::uint32_t flags = ReadU32(data + 8);
      const std::uint32_t height = ReadU32(data + 12);
      const std::uint32_t width = ReadU32(data + 16);
      const std::uint32_t mipCount = (flags & 0x20000U) != 0U ? std::max(ReadU32(data + 28), 1U) : 1U;
      const std::uint32_t pixelFlags = ReadU32(data + 80);
      const std::uint32_t fourCc = ReadU32(data + 84);
      const std::uint32_t bitCount = ReadU32(data + 88);
      const std::uint32_t masks[4] = {ReadU32(data + 92), ReadU32(data + 96), ReadU32(data + 100), ReadU32(data + 104)};
      const std::uint32_t caps2 = ReadU32(data + 112);
      if (width == 0U || height == 0U || width > 16384U || height > 16384U || mipCount > 16U || (caps2 & 0x200000U) != 0U) {
        return E_NOTIMPL;
      }
      std::size_t at = kHeaderBytes;
      std::uint32_t faces = 1U;
      if ((caps2 & 0x200U) != 0U) {
        if ((caps2 & 0xFC00U) != 0xFC00U) {
          return E_NOTIMPL;
        }
        faces = 6U;
      }
      // How the file holds its texels: a DXGI format to copy, or legacy texels to convert.
      DXGI_FORMAT stored = DXGI_FORMAT_UNKNOWN;
      bool legacy = false;
      if ((pixelFlags & 0x4U) != 0U) {
        if (fourCc == FourCc('D', 'X', 'T', '1')) {
          stored = DXGI_FORMAT_BC1_UNORM;
        } else if (fourCc == FourCc('D', 'X', 'T', '2') || fourCc == FourCc('D', 'X', 'T', '3')) {
          stored = DXGI_FORMAT_BC2_UNORM;
        } else if (fourCc == FourCc('D', 'X', 'T', '4') || fourCc == FourCc('D', 'X', 'T', '5')) {
          stored = DXGI_FORMAT_BC3_UNORM;
        } else if (fourCc == FourCc('D', 'X', '1', '0') && bytes >= kHeaderBytes + 20U) {
          stored = static_cast<DXGI_FORMAT>(ReadU32(data + kHeaderBytes));
          const std::uint32_t slices = std::max(1U, ReadU32(data + kHeaderBytes + 12));
          faces = (ReadU32(data + kHeaderBytes + 8) & 0x4U) != 0U ? 6U * slices : slices;
          at += 20U;
          if (!SupportedFormat(stored) || (faces != 1U && faces != 6U) || ReadU32(data + kHeaderBytes + 4) != 3U) {
            return E_NOTIMPL;
          }
        } else {
          return E_NOTIMPL;
        }
      } else if ((pixelFlags & 0x2U) != 0U && (pixelFlags & 0x40U) == 0U && bitCount == 8U) {
        stored = DXGI_FORMAT_A8_UNORM;
      } else if ((pixelFlags & (0x40U | 0x20000U)) != 0U && (bitCount == 8U || bitCount == 16U || bitCount == 24U || bitCount == 32U)) {
        legacy = true;
      } else {
        return E_NOTIMPL;
      }
      out.format = legacy ? DXGI_FORMAT_R8G8B8A8_UNORM : stored;
      out.mipCount = mipCount;
      out.cube = faces == 6U;
      out.faces.assign(faces, {});
      const bool luminance = (pixelFlags & 0x20000U) != 0U;
      const bool alpha = (pixelFlags & 0x1U) != 0U;
      const std::uint32_t pixelBytes = bitCount / 8U;
      for (std::uint32_t face = 0U; face < faces; ++face) {
        for (std::uint32_t mip = 0U; mip < mipCount; ++mip) {
          Level level;
          level.width = std::max(1U, width >> mip);
          level.height = std::max(1U, height >> mip);
          const std::size_t texels = static_cast<std::size_t>(level.width) * level.height;
          const std::size_t fileBytes = legacy ? texels * pixelBytes : dxgi::ComputeLevelLayout(stored, level.width, level.height).ByteSize();
          if (fileBytes > bytes - at) {
            return E_FAIL;
          }
          if (!legacy) {
            level.data.assign(data + at, data + at + fileBytes);
          } else {
            level.data.resize(texels * 4U);
            for (std::size_t texel = 0U; texel < texels; ++texel) {
              std::uint32_t pixel = 0U;
              for (std::uint32_t byte = 0U; byte < pixelBytes; ++byte) {
                pixel |= static_cast<std::uint32_t>(data[at + texel * pixelBytes + byte]) << (byte * 8U);
              }
              std::uint8_t* const rgba = level.data.data() + texel * 4U;
              rgba[0] = MaskChannel(pixel, masks[0], 0U);
              rgba[1] = luminance ? rgba[0] : MaskChannel(pixel, masks[1], 0U);
              rgba[2] = luminance ? rgba[0] : MaskChannel(pixel, masks[2], 0U);
              rgba[3] = alpha ? MaskChannel(pixel, masks[3], 255U) : 255U;
            }
          }
          at += fileBytes;
          out.faces[face].push_back(std::move(level));
        }
      }
      return S_OK;
    }

    [[nodiscard]] HRESULT ReadSource(const std::uint8_t* const data, const std::size_t bytes, Source& out)
    {
      const files::Container container = files::Identify(data, bytes);
      if (container == files::Container::Dds) {
        return ReadDds(data, bytes, out);
      }
      if (container == files::Container::Jpg) {
        return E_NOTIMPL; // no JPG decoder: the game has no JPG
      }
      files::Pixels pixels;
      if (container == files::Container::Unknown || !files::Decode(container, data, bytes, pixels)) {
        return E_FAIL;
      }
      out.format = DXGI_FORMAT_R8G8B8A8_UNORM;
      out.mipCount = 1U;
      out.cube = false;
      out.faces.assign(1U, {});
      Level level;
      level.width = pixels.width;
      level.height = pixels.height;
      level.data = std::move(pixels.rgba);
      out.faces[0].push_back(std::move(level));
      return S_OK;
    }

    thread_local const char* tLastDecodeRoute = "none";

    /** D3DX11CreateTextureFromMemory with the options as DecodeWithD3dx sets them, `skip` as its FirstMipLevel. */
    [[nodiscard]] HRESULT EmulateD3dx(const std::uint8_t* const data, const std::size_t bytes, const DecodeOptions& options,
                                      const std::uint32_t skip, Image& outImage)
    {
      Source source;
      const HRESULT read = ReadSource(data, bytes, source);
      if (read != S_OK) {
        return read;
      }
      if (skip != 0U) {
        if (skip >= source.mipCount) {
          return E_INVALIDARG;
        }
        for (std::vector<Level>& levels : source.faces) {
          levels.erase(levels.begin(), levels.begin() + skip);
        }
        source.mipCount -= skip;
      }
      const DXGI_FORMAT format = options.format != DXGI_FORMAT_UNKNOWN ? options.format : source.format;
      if (!SupportedFormat(format)) {
        return E_NOTIMPL;
      }
      const Level& top = source.faces[0][0];
      std::uint32_t width = options.width != 0U ? options.width : top.width;
      std::uint32_t height = options.height != 0U ? options.height : top.height;
      if (IsBc123(format)) {
        // A BC texture's top level is whole blocks.
        width = (width + 3U) & ~3U;
        height = (height + 3U) & ~3U;
      }
      const std::uint32_t chain = FullChain(width, height);
      const std::uint32_t mipLevels = options.mipLevels != 0U ? std::min(options.mipLevels, chain) : chain;
      // The file's levels are taken as they are where nothing about them changes.
      const bool sameBytes = format == source.format && width == top.width && height == top.height;
      const auto faces = static_cast<std::uint32_t>(source.faces.size());
      bool copiedAll = true;

      outImage.Allocate(format, width, height, mipLevels, faces);
      outImage.cube = source.cube;
      outImage.volume = false;
      for (std::uint32_t face = 0U; face < faces; ++face) {
        const std::vector<Level>& levels = source.faces[face];
        std::vector<std::uint8_t> previous; // the level above as R8G8B8A8; empty when it was copied
        for (std::uint32_t mip = 0U; mip < mipLevels; ++mip) {
          const Image::Subresource& target = outImage.subresources[face * mipLevels + mip];
          const bool inFile = sameBytes && mip < source.mipCount && levels[mip].width == target.width && levels[mip].height == target.height;
          // D3DX11 compresses a BC level whose sides are not whole blocks again (most of its blocks
          // change, not only the partial ones): such a level is decoded and encoded here too.
          const bool partialBlocks = IsBc123(format) && ((target.width % 4U) != 0U || (target.height % 4U) != 0U);
          if (inFile && !partialBlocks) {
            std::memcpy(outImage.bytes.data() + target.offset, levels[mip].data.data(), target.slicePitch);
            previous.clear();
            continue;
          }
          copiedAll = false;
          std::vector<std::uint8_t> rgba;
          if (inFile) {
            rgba = ToRgba(format, levels[mip].data.data(), target.width, target.height);
          } else if (mip == 0U) {
            rgba = ToRgba(source.format, levels[0].data.data(), levels[0].width, levels[0].height);
            if (levels[0].width != width || levels[0].height != height) {
              rgba = FitRgba(rgba, levels[0].width, levels[0].height, width, height, options.filter);
            }
          } else {
            const Image::Subresource& above = outImage.subresources[face * mipLevels + mip - 1U];
            if (previous.empty()) {
              previous = ToRgba(format, outImage.bytes.data() + above.offset, above.width, above.height);
            }
            rgba = HalveRgba(previous, above.width, above.height, target.width, target.height);
          }
          const std::vector<std::uint8_t> level = FromRgba(format, rgba, target.width, target.height);
          std::memcpy(outImage.bytes.data() + target.offset, level.data(), target.slicePitch);
          previous = std::move(rgba);
        }
      }
      tLastDecodeRoute = copiedAll ? "d3dx: the file's levels" : (sameBytes ? "d3dx: levels made" : "d3dx: converted");
      return S_OK;
    }

    /** A DDS file of `image`: the legacy header where D3DX9's formats have one, else DX10's. */
    void WriteDds(const Image& image, std::vector<std::uint8_t>& out)
    {
      std::uint8_t header[4 + 124 + 20] = {};
      const auto put = [&](const std::size_t at, const std::uint32_t value) { std::memcpy(header + at, &value, 4U); };
      put(0, FourCc('D', 'D', 'S', ' '));
      put(4, 124U);
      put(8, 0x1U | 0x2U | 0x4U | 0x1000U | 0x20000U); // caps, height, width, pixel format, mip count
      put(12, image.height);
      put(16, image.width);
      put(28, image.mipLevels);
      put(76, 32U);
      put(108, 0x1000U | (image.mipLevels > 1U ? 0x400008U : 0U) | (image.cube ? 0x8U : 0U));
      put(112, image.cube ? 0xFE00U : 0U);
      bool dx10 = false;
      switch (image.format) {
      case DXGI_FORMAT_BC1_UNORM:
        put(80, 0x4U);
        put(84, FourCc('D', 'X', 'T', '1'));
        break;
      case DXGI_FORMAT_BC2_UNORM:
        put(80, 0x4U);
        put(84, FourCc('D', 'X', 'T', '3'));
        break;
      case DXGI_FORMAT_BC3_UNORM:
        put(80, 0x4U);
        put(84, FourCc('D', 'X', 'T', '5'));
        break;
      case DXGI_FORMAT_R8G8B8A8_UNORM:
      case DXGI_FORMAT_B8G8R8A8_UNORM:
      case DXGI_FORMAT_B8G8R8X8_UNORM: {
        const bool rgba = image.format == DXGI_FORMAT_R8G8B8A8_UNORM;
        const bool opaque = image.format == DXGI_FORMAT_B8G8R8X8_UNORM;
        put(80, 0x40U | (opaque ? 0U : 0x1U));
        put(88, 32U);
        put(92, rgba ? 0x000000FFU : 0x00FF0000U);
        put(96, 0x0000FF00U);
        put(100, rgba ? 0x00FF0000U : 0x000000FFU);
        put(104, opaque ? 0U : 0xFF000000U);
        break;
      }
      case DXGI_FORMAT_A8_UNORM:
        put(80, 0x2U);
        put(88, 8U);
        put(104, 0xFFU);
        break;
      default:
        dx10 = true;
        put(80, 0x4U);
        put(84, FourCc('D', 'X', '1', '0'));
        put(128, static_cast<std::uint32_t>(image.format));
        put(132, 3U); // TEXTURE2D
        put(136, image.cube ? 0x4U : 0U);
        put(140, image.cube ? 1U : image.arraySize);
        break;
      }
      out.assign(header, header + (dx10 ? sizeof(header) : 128U));
      out.insert(out.end(), image.bytes.begin(), image.bytes.end());
    }

    /** The first subresource as R8G8B8A8, for the BMP and PNG writers. */
    [[nodiscard]] bool TopAsPixels(const Image& image, files::Pixels& out)
    {
      if (image.subresources.empty() || !SupportedFormat(image.format)) {
        return false;
      }
      const Image::Subresource& top = image.subresources.front();
      out.width = top.width;
      out.height = top.height;
      out.rgba = ToRgba(image.format, image.bytes.data() + top.offset, top.width, top.height);
      return true;
    }
  } // namespace

  const char* ImageCodec::LastDecodeRoute() noexcept
  {
    return tLastDecodeRoute;
  }

  HRESULT ImageCodec::Decode(const void* const data, const std::size_t bytes, const DecodeOptions& options, Image& outImage)
  {
    tLastDecodeRoute = "direct";
    if (gDdsDirectLoad && TryReadBlockCompressedDds(static_cast<const std::uint8_t*>(data), bytes, options, outImage)) {
      return S_OK;
    }
    return DecodeWithD3dx(data, bytes, options, outImage);
  }

  HRESULT ImageCodec::DecodeWithD3dx(const void* const data, const std::size_t bytes, const DecodeOptions& options, Image& outImage)
  {
    tLastDecodeRoute = "d3dx: failed";
    const auto* const file = static_cast<const std::uint8_t*>(data);
    // As ImageCodec.cpp: with the skip first, from level 0 when D3DX refuses it.
    HRESULT result = E_FAIL;
    if (options.skipMipLevels != 0U) {
      result = EmulateD3dx(file, bytes, options, options.skipMipLevels, outImage);
    }
    if (result != S_OK) {
      outImage = Image{};
      result = EmulateD3dx(file, bytes, options, 0U, outImage);
    }
    return result;
  }

  HRESULT ImageCodec::GetImageSize(const void* const data, const std::size_t bytes, std::uint32_t& outWidth, std::uint32_t& outHeight)
  {
    const auto* const file = static_cast<const std::uint8_t*>(data);
    const files::Container container = files::Identify(file, bytes);
    if (container == files::Container::Dds) {
      if (bytes < 128U) {
        return E_FAIL;
      }
      outHeight = ReadU32(file + 12);
      outWidth = ReadU32(file + 16);
      return S_OK;
    }
    return files::ReadSize(container, file, bytes, outWidth, outHeight) ? S_OK : E_FAIL;
  }

  HRESULT ImageCodec::Encode(const Image& image, const FileFormat format, std::vector<std::uint8_t>& outBytes)
  {
    if (format == FileFormat::Dds) {
      WriteDds(image, outBytes);
      return S_OK;
    }
    files::Pixels pixels;
    if ((format != FileFormat::Bmp && format != FileFormat::Png) || !TopAsPixels(image, pixels)) {
      return E_NOTIMPL; // no JPG writer
    }
    if (format == FileFormat::Bmp) {
      files::WriteBmp(pixels, outBytes);
      return S_OK;
    }
    return files::WritePng(pixels, outBytes) ? S_OK : E_FAIL;
  }

  HRESULT ImageCodec::EncodeToFile(const Image& image, const FileFormat format, const char* const path)
  {
    std::vector<std::uint8_t> encoded;
    const HRESULT result = Encode(image, format, encoded);
    if (result != S_OK) {
      return result;
    }
    HANDLE const file = ::CreateFileA(path, GENERIC_WRITE, 0U, nullptr, CREATE_ALWAYS, 0U, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
      return E_FAIL;
    }
    DWORD written = 0U;
    const bool ok = ::WriteFile(file, encoded.data(), static_cast<DWORD>(encoded.size()), &written, nullptr) && written == encoded.size();
    ::CloseHandle(file);
    return ok ? S_OK : E_FAIL;
  }

  FileFormat ImageCodec::FromGalFormat(const int galFormat) noexcept
  {
    // The D3D10 table (0x00D430A0): BMP, JPG, PNG, PNG, DDS.
    constexpr FileFormat kFormats[5] = {FileFormat::Bmp, FileFormat::Jpg, FileFormat::Png, FileFormat::Png, FileFormat::Dds};
    return (galFormat >= 0 && galFormat < 5) ? kFormats[galFormat] : FileFormat::Bmp;
  }
} // namespace gpg::gal::imaging
