#include "faf/port/Image.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <vector>

#include <zlib.h>

#include "NativeFile.h"

namespace faf::port {

  namespace {
    // DDS layout: "DDS " + DDS_HEADER (124 bytes) [+ DDS_HEADER_DXT10 (20 bytes)].
    constexpr std::size_t kMagicSize = 4;
    constexpr std::size_t kHeaderSize = 124;
    constexpr std::size_t kDx10HeaderSize = 20;
    constexpr std::size_t kHeaderOffset = kMagicSize;
    constexpr std::size_t kPixelFormatOffset = kHeaderOffset + 72;
    constexpr std::uint32_t kPixelFormatSize = 32;

    constexpr std::uint32_t kPfAlphaPixels = 0x1;
    constexpr std::uint32_t kPfAlpha = 0x2;
    constexpr std::uint32_t kPfFourCc = 0x4;
    constexpr std::uint32_t kPfRgb = 0x40;
    constexpr std::uint32_t kPfLuminance = 0x20000;

    /// No SCFA/FAF texture is larger than 4096; the cap only stops a corrupt
    /// header from overflowing width * height * 4.
    constexpr std::uint32_t kMaxDimension = 16384;

    // DXGI_FORMAT values for DX10-style headers.
    constexpr std::uint32_t kDxgiR8G8B8A8Unorm = 28;
    constexpr std::uint32_t kDxgiR8G8B8A8UnormSrgb = 29;
    constexpr std::uint32_t kDxgiBc1Unorm = 71;
    constexpr std::uint32_t kDxgiBc1UnormSrgb = 72;
    constexpr std::uint32_t kDxgiBc2Unorm = 74;
    constexpr std::uint32_t kDxgiBc2UnormSrgb = 75;
    constexpr std::uint32_t kDxgiBc3Unorm = 77;
    constexpr std::uint32_t kDxgiBc3UnormSrgb = 78;
    constexpr std::uint32_t kDxgiB8G8R8A8Unorm = 87;
    constexpr std::uint32_t kDxgiB8G8R8X8Unorm = 88;
    constexpr std::uint32_t kDxgiB8G8R8A8UnormSrgb = 91;

    [[nodiscard]] std::uint16_t Le16(const std::uint8_t* const p)
    {
      return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
    }

    [[nodiscard]] std::uint32_t Le32(const std::uint8_t* const p)
    {
      return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) | (std::uint32_t{p[2]} << 16) |
             (std::uint32_t{p[3]} << 24);
    }

    [[nodiscard]] constexpr std::uint32_t FourCc(const char a, const char b, const char c, const char d)
    {
      return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
             (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8) |
             (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16) |
             (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24);
    }

    bool Fail(std::string* const error, const char* const message)
    {
      if (error != nullptr) {
        *error = message;
      }
      return false;
    }

    enum class BlockFormat
    {
      Bc1,
      Bc2,
      Bc3,
    };

    using Rgba = std::array<std::uint8_t, 4>;

    /// 5/6-bit channel to 8 bits by bit replication (what GPUs do).
    [[nodiscard]] Rgba Expand565(const std::uint16_t color)
    {
      const unsigned r = (color >> 11) & 0x1F;
      const unsigned g = (color >> 5) & 0x3F;
      const unsigned b = color & 0x1F;
      return {
        static_cast<std::uint8_t>((r << 3) | (r >> 2)),
        static_cast<std::uint8_t>((g << 2) | (g >> 4)),
        static_cast<std::uint8_t>((b << 3) | (b >> 2)),
        255,
      };
    }

    [[nodiscard]] std::uint8_t Mix(const unsigned a, const unsigned b, const unsigned weightA, const unsigned weightB)
    {
      return static_cast<std::uint8_t>((a * weightA + b * weightB) / (weightA + weightB));
    }

    /// The 4x4 colour block shared by BC1-3. BC1 with color0 <= color1 is the
    /// 3-colour mode whose index 3 is transparent black; BC2/3 always use the
    /// 4-colour mode.
    void DecodeColorBlock(const std::uint8_t* const block, const bool bc1, std::array<Rgba, 16>& out)
    {
      const std::uint16_t c0 = Le16(block);
      const std::uint16_t c1 = Le16(block + 2);
      std::array<Rgba, 4> palette{Expand565(c0), Expand565(c1), Rgba{}, Rgba{}};
      if (!bc1 || c0 > c1) {
        for (std::size_t k = 0; k < 3; ++k) {
          palette[2][k] = Mix(palette[0][k], palette[1][k], 2, 1);
          palette[3][k] = Mix(palette[0][k], palette[1][k], 1, 2);
        }
        palette[2][3] = 255;
        palette[3][3] = 255;
      } else {
        for (std::size_t k = 0; k < 3; ++k) {
          palette[2][k] = Mix(palette[0][k], palette[1][k], 1, 1);
        }
        palette[2][3] = 255;
        palette[3] = {0, 0, 0, 0};
      }
      const std::uint32_t indices = Le32(block + 4);
      for (std::size_t i = 0; i < 16; ++i) {
        out[i] = palette[(indices >> (2 * i)) & 3u];
      }
    }

    /// BC3 alpha: two endpoints and 16 3-bit indices (8- or 6-value mode).
    void DecodeAlphaBlock(const std::uint8_t* const block, std::array<Rgba, 16>& out)
    {
      std::array<std::uint8_t, 8> alpha{block[0], block[1]};
      if (alpha[0] > alpha[1]) {
        for (unsigned i = 1; i < 7; ++i) {
          alpha[i + 1] = Mix(alpha[0], alpha[1], 7 - i, i);
        }
      } else {
        for (unsigned i = 1; i < 5; ++i) {
          alpha[i + 1] = Mix(alpha[0], alpha[1], 5 - i, i);
        }
        alpha[6] = 0;
        alpha[7] = 255;
      }
      std::uint64_t bits = 0;
      for (std::size_t i = 0; i < 6; ++i) {
        bits |= std::uint64_t{block[2 + i]} << (8 * i);
      }
      for (std::size_t i = 0; i < 16; ++i) {
        out[i][3] = alpha[(bits >> (3 * i)) & 7u];
      }
    }

    void DecodeBlocks(const std::uint8_t* const data, const BlockFormat format, Image& image)
    {
      const std::size_t blockSize = format == BlockFormat::Bc1 ? 8 : 16;
      const auto width = static_cast<std::size_t>(image.width);
      const auto height = static_cast<std::size_t>(image.height);
      const std::size_t blocksWide = (width + 3) / 4;
      const std::size_t blocksHigh = (height + 3) / 4;
      std::array<Rgba, 16> pixels{};
      for (std::size_t by = 0; by < blocksHigh; ++by) {
        for (std::size_t bx = 0; bx < blocksWide; ++bx) {
          const std::uint8_t* const block = data + (by * blocksWide + bx) * blockSize;
          switch (format) {
          case BlockFormat::Bc1:
            DecodeColorBlock(block, true, pixels);
            break;
          case BlockFormat::Bc2:
            DecodeColorBlock(block + 8, false, pixels);
            for (std::size_t i = 0; i < 16; ++i) {
              const unsigned nibble = (block[i / 2] >> (4 * (i & 1))) & 0xFu;
              pixels[i][3] = static_cast<std::uint8_t>(nibble * 17);
            }
            break;
          case BlockFormat::Bc3:
            DecodeColorBlock(block + 8, false, pixels);
            DecodeAlphaBlock(block, pixels);
            break;
          }
          for (std::size_t i = 0; i < 16; ++i) {
            const std::size_t x = bx * 4 + (i & 3);
            const std::size_t y = by * 4 + (i >> 2);
            if (x < width && y < height) {
              std::memcpy(&image.rgba[(y * width + x) * 4], pixels[i].data(), 4);
            }
          }
        }
      }
    }

    /// One channel of an uncompressed pixel described by a bit mask.
    struct Channel
    {
      std::uint32_t mask = 0;
      unsigned shift = 0;
      unsigned bits = 0;

      explicit Channel(const std::uint32_t channelMask)
        : mask(channelMask)
      {
        if (mask != 0) {
          shift = static_cast<unsigned>(std::countr_zero(mask));
          bits = static_cast<unsigned>(std::popcount(mask));
        }
      }

      [[nodiscard]] std::uint8_t Extract(const std::uint32_t pixel, const std::uint8_t fallback) const
      {
        if (mask == 0) {
          return fallback;
        }
        const std::uint32_t value = (pixel & mask) >> shift;
        const std::uint32_t maximum = bits >= 32 ? 0xFFFFFFFFu : ((1u << bits) - 1u);
        return static_cast<std::uint8_t>((static_cast<std::uint64_t>(value) * 255u + maximum / 2) / maximum);
      }
    };

    [[nodiscard]] std::string MaskFormatName(
      const std::uint32_t bitCount,
      const std::uint32_t r,
      const std::uint32_t g,
      const std::uint32_t b,
      const std::uint32_t a
    )
    {
      struct Known
      {
        std::uint32_t bits, r, g, b, a;
        const char* name;
      };
      static constexpr Known kKnown[] = {
        {32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000, "ARGB32"},
        {32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0x00000000, "XRGB32"},
        {32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000, "ABGR32"},
        {32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0x00000000, "XBGR32"},
        {24, 0x00FF0000, 0x0000FF00, 0x000000FF, 0x00000000, "RGB24"},
        {16, 0x0000F800, 0x000007E0, 0x0000001F, 0x00000000, "RGB565"},
        {16, 0x00007C00, 0x000003E0, 0x0000001F, 0x00008000, "ARGB1555"},
        {16, 0x00007C00, 0x000003E0, 0x0000001F, 0x00000000, "XRGB1555"},
        {16, 0x00000F00, 0x000000F0, 0x0000000F, 0x0000F000, "ARGB4444"},
        {8, 0x000000FF, 0x00000000, 0x00000000, 0x00000000, "L8"},
        {8, 0x00000000, 0x00000000, 0x00000000, 0x000000FF, "A8"},
        {16, 0x000000FF, 0x00000000, 0x00000000, 0x0000FF00, "A8L8"},
      };
      for (const Known& known : kKnown) {
        if (known.bits == bitCount && known.r == r && known.g == g && known.b == b && known.a == a) {
          return known.name;
        }
      }
      return "RGBA" + std::to_string(bitCount);
    }

    struct FileCloser
    {
      void operator()(std::FILE* const file) const
      {
        std::fclose(file);
      }
    };

    void PutBe32(std::vector<std::uint8_t>& out, const std::uint32_t value)
    {
      for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
      }
    }

    void PutChunk(
      std::vector<std::uint8_t>& png,
      const char* const type,
      const std::uint8_t* const data,
      const std::size_t size
    )
    {
      PutBe32(png, static_cast<std::uint32_t>(size));
      const std::size_t start = png.size();
      png.insert(png.end(), type, type + 4);
      if (size != 0) {
        png.insert(png.end(), data, data + size);
      }
      const uLong crc = crc32(0L, png.data() + start, static_cast<uInt>(png.size() - start));
      PutBe32(png, static_cast<std::uint32_t>(crc));
    }
  } // namespace

  bool DecodeDds(const std::uint8_t* const data, const std::size_t size, Image& out, std::string* const error)
  {
    out = Image{};
    if (data == nullptr || size < kMagicSize + kHeaderSize) {
      return Fail(error, "not a DDS file (too small)");
    }
    if (std::memcmp(data, "DDS ", kMagicSize) != 0) {
      return Fail(error, "not a DDS file (bad magic)");
    }
    const std::uint8_t* const header = data + kHeaderOffset;
    if (Le32(header) != kHeaderSize) {
      return Fail(error, "unsupported DDS header size");
    }
    const std::uint32_t height = Le32(header + 8);
    const std::uint32_t width = Le32(header + 12);
    if (width == 0 || height == 0 || width > kMaxDimension || height > kMaxDimension) {
      return Fail(error, "unsupported DDS dimensions");
    }

    const std::uint8_t* const pixelFormat = data + kPixelFormatOffset;
    if (Le32(pixelFormat) != kPixelFormatSize) {
      return Fail(error, "unsupported DDS pixel format size");
    }
    const std::uint32_t flags = Le32(pixelFormat + 4);
    const std::uint32_t fourCc = Le32(pixelFormat + 8);
    const std::uint32_t bitCount = Le32(pixelFormat + 12);
    const std::uint32_t redMask = Le32(pixelFormat + 16);
    const std::uint32_t greenMask = Le32(pixelFormat + 20);
    const std::uint32_t blueMask = Le32(pixelFormat + 24);
    const std::uint32_t alphaMask = Le32(pixelFormat + 28);

    std::size_t dataOffset = kMagicSize + kHeaderSize;
    const std::size_t pixelCount = static_cast<std::size_t>(width) * height;

    // The top mip of the first face (or slice) comes first in every layout,
    // so cube maps and volumes need no special handling.
    std::optional<BlockFormat> block;
    std::string name;
    bool rgba8 = false; // DX10 R8G8B8A8 / B8G8R8A8 handled by the mask path below.
    std::uint32_t maskBits = bitCount;
    std::uint32_t masks[4] = {redMask, greenMask, blueMask, alphaMask};

    if ((flags & kPfFourCc) != 0) {
      if (fourCc == FourCc('D', 'X', 'T', '1')) {
        block = BlockFormat::Bc1;
        name = "DXT1";
      } else if (fourCc == FourCc('D', 'X', 'T', '2') || fourCc == FourCc('D', 'X', 'T', '3')) {
        block = BlockFormat::Bc2;
        name = fourCc == FourCc('D', 'X', 'T', '2') ? "DXT2" : "DXT3";
      } else if (fourCc == FourCc('D', 'X', 'T', '4') || fourCc == FourCc('D', 'X', 'T', '5')) {
        block = BlockFormat::Bc3;
        name = fourCc == FourCc('D', 'X', 'T', '4') ? "DXT4" : "DXT5";
      } else if (fourCc == FourCc('D', 'X', '1', '0')) {
        if (size < dataOffset + kDx10HeaderSize) {
          return Fail(error, "truncated DDS DX10 header");
        }
        const std::uint32_t dxgi = Le32(data + dataOffset);
        dataOffset += kDx10HeaderSize;
        switch (dxgi) {
        case kDxgiBc1Unorm:
        case kDxgiBc1UnormSrgb:
          block = BlockFormat::Bc1;
          name = "BC1";
          break;
        case kDxgiBc2Unorm:
        case kDxgiBc2UnormSrgb:
          block = BlockFormat::Bc2;
          name = "BC2";
          break;
        case kDxgiBc3Unorm:
        case kDxgiBc3UnormSrgb:
          block = BlockFormat::Bc3;
          name = "BC3";
          break;
        case kDxgiR8G8B8A8Unorm:
        case kDxgiR8G8B8A8UnormSrgb:
          rgba8 = true;
          maskBits = 32;
          masks[0] = 0x000000FF;
          masks[1] = 0x0000FF00;
          masks[2] = 0x00FF0000;
          masks[3] = 0xFF000000;
          name = "R8G8B8A8";
          break;
        case kDxgiB8G8R8A8Unorm:
        case kDxgiB8G8R8A8UnormSrgb:
        case kDxgiB8G8R8X8Unorm:
          rgba8 = true;
          maskBits = 32;
          masks[0] = 0x00FF0000;
          masks[1] = 0x0000FF00;
          masks[2] = 0x000000FF;
          masks[3] = dxgi == kDxgiB8G8R8X8Unorm ? 0u : 0xFF000000u;
          name = dxgi == kDxgiB8G8R8X8Unorm ? "B8G8R8X8" : "B8G8R8A8";
          break;
        default:
          return Fail(error, "unsupported DXGI format");
        }
      } else {
        return Fail(error, "unsupported DDS FourCC");
      }
    } else if ((flags & (kPfRgb | kPfLuminance | kPfAlpha)) == 0) {
      return Fail(error, "unsupported DDS pixel format");
    }

    try {
      out.rgba.assign(pixelCount * 4, 0);
    } catch (const std::bad_alloc&) {
      return Fail(error, "out of memory");
    }
    out.width = static_cast<int>(width);
    out.height = static_cast<int>(height);

    if (block) {
      const std::size_t blockBytes = *block == BlockFormat::Bc1 ? 8 : 16;
      const std::size_t needed = ((static_cast<std::size_t>(width) + 3) / 4) * ((height + 3) / 4) * blockBytes;
      if (size - dataOffset < needed) {
        out = Image{};
        return Fail(error, "truncated DDS pixel data");
      }
      DecodeBlocks(data + dataOffset, *block, out);
      out.format = name;
      return true;
    }

    // Uncompressed: tightly packed rows of 8/16/24/32-bit pixels described by masks.
    if (maskBits != 8 && maskBits != 16 && maskBits != 24 && maskBits != 32) {
      out = Image{};
      return Fail(error, "unsupported DDS bit count");
    }
    if (!rgba8 && (flags & kPfAlphaPixels) == 0 && (flags & kPfAlpha) == 0) {
      masks[3] = 0; // Alpha mask present but not flagged: treat as opaque, like D3DX.
    }
    const std::size_t bytesPerPixel = maskBits / 8;
    if ((size - dataOffset) / bytesPerPixel < pixelCount) {
      out = Image{};
      return Fail(error, "truncated DDS pixel data");
    }
    const bool luminance = !rgba8 && (flags & kPfLuminance) != 0;
    const Channel red(masks[0]);
    const Channel green(masks[1]);
    const Channel blue(masks[2]);
    const Channel alpha(masks[3]);
    const std::uint8_t* source = data + dataOffset;
    for (std::size_t i = 0; i < pixelCount; ++i, source += bytesPerPixel) {
      std::uint32_t pixel = 0;
      for (std::size_t b = 0; b < bytesPerPixel; ++b) {
        pixel |= std::uint32_t{source[b]} << (8 * b);
      }
      std::uint8_t* const target = &out.rgba[i * 4];
      if (luminance) {
        const std::uint8_t level = red.Extract(pixel, 0);
        target[0] = level;
        target[1] = level;
        target[2] = level;
      } else {
        target[0] = red.Extract(pixel, 0);
        target[1] = green.Extract(pixel, 0);
        target[2] = blue.Extract(pixel, 0);
      }
      target[3] = alpha.Extract(pixel, 255);
    }
    out.format = rgba8 ? name : MaskFormatName(maskBits, masks[0], masks[1], masks[2], masks[3]);
    return true;
  }

  bool WritePng(const std::string& path, const Image& image)
  {
    if (image.width <= 0 || image.height <= 0) {
      return false;
    }
    const auto width = static_cast<std::size_t>(image.width);
    const auto height = static_cast<std::size_t>(image.height);
    if (image.rgba.size() != width * height * 4) {
      return false;
    }

    try {
      // Filter type 0 (None) on every row: simple, and zlib still compresses
      // decoded textures well.
      const std::size_t rowBytes = width * 4;
      std::vector<std::uint8_t> raw;
      raw.reserve((rowBytes + 1) * height);
      for (std::size_t y = 0; y < height; ++y) {
        raw.push_back(0);
        const auto row = image.rgba.begin() + static_cast<std::ptrdiff_t>(y * rowBytes);
        raw.insert(raw.end(), row, row + static_cast<std::ptrdiff_t>(rowBytes));
      }
      if (raw.size() > std::numeric_limits<uLong>::max()) {
        return false;
      }
      uLongf compressedSize = compressBound(static_cast<uLong>(raw.size()));
      std::vector<std::uint8_t> compressed(compressedSize);
      if (compress2(compressed.data(), &compressedSize, raw.data(), static_cast<uLong>(raw.size()), 6) != Z_OK) {
        return false;
      }
      compressed.resize(compressedSize);

      std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
      std::vector<std::uint8_t> ihdr;
      PutBe32(ihdr, static_cast<std::uint32_t>(width));
      PutBe32(ihdr, static_cast<std::uint32_t>(height));
      ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0}); // 8-bit RGBA, deflate, adaptive filter set, no interlace.
      PutChunk(png, "IHDR", ihdr.data(), ihdr.size());
      // IDAT chunks are capped well below the format's 2^31-1 limit.
      constexpr std::size_t kMaxIdat = std::size_t{1} << 20;
      for (std::size_t offset = 0; offset < compressed.size(); offset += kMaxIdat) {
        PutChunk(png, "IDAT", compressed.data() + offset, std::min(kMaxIdat, compressed.size() - offset));
      }
      PutChunk(png, "IEND", nullptr, 0);

      std::unique_ptr<std::FILE, FileCloser> file(detail::OpenStdioFile(path, "wb"));
      if (file == nullptr) {
        return false;
      }
      const bool written = std::fwrite(png.data(), 1, png.size(), file.get()) == png.size();
      // fclose flushes; a full disk shows up there, not in fwrite.
      return std::fclose(file.release()) == 0 && written;
    } catch (const std::bad_alloc&) {
      return false;
    }
  }

} // namespace faf::port
