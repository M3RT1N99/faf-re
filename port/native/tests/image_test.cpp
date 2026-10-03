// DDS decoding of synthetic DXT1/3/5 and uncompressed images, and the PNG writer.

#include <array>
#include <cstring>
#include <string>
#include <vector>

#include <zlib.h>

#include "faf/port/FileSystem.h"
#include "faf/port/Image.h"

#include "TestSupport.h"

using faf::port::DecodeDds;
using faf::port::Image;

namespace {
  void Put32(std::vector<std::uint8_t>& out, const std::size_t offset, const std::uint32_t value)
  {
    for (int i = 0; i < 4; ++i) {
      out[offset + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value >> (8 * i));
    }
  }

  struct PixelFormat
  {
    std::uint32_t flags = 0;
    std::uint32_t fourCc = 0;
    std::uint32_t bits = 0;
    std::uint32_t r = 0, g = 0, b = 0, a = 0;
  };

  constexpr std::uint32_t FourCc(const char (&text)[5])
  {
    return static_cast<std::uint32_t>(text[0]) | (static_cast<std::uint32_t>(text[1]) << 8) |
           (static_cast<std::uint32_t>(text[2]) << 16) | (static_cast<std::uint32_t>(text[3]) << 24);
  }

  std::vector<std::uint8_t> MakeDds(
    const std::uint32_t width,
    const std::uint32_t height,
    const PixelFormat& format,
    const std::vector<std::uint8_t>& payload
  )
  {
    std::vector<std::uint8_t> dds(128, 0);
    std::memcpy(dds.data(), "DDS ", 4);
    Put32(dds, 4, 124);
    Put32(dds, 8, 0x1007); // CAPS | HEIGHT | WIDTH | PIXELFORMAT
    Put32(dds, 12, height);
    Put32(dds, 16, width);
    Put32(dds, 76, 32);
    Put32(dds, 80, format.flags);
    Put32(dds, 84, format.fourCc);
    Put32(dds, 88, format.bits);
    Put32(dds, 92, format.r);
    Put32(dds, 96, format.g);
    Put32(dds, 100, format.b);
    Put32(dds, 104, format.a);
    Put32(dds, 108, 0x1000); // DDSCAPS_TEXTURE
    dds.insert(dds.end(), payload.begin(), payload.end());
    return dds;
  }

  using Rgba = std::array<int, 4>;

  Rgba PixelAt(const Image& image, const int x, const int y)
  {
    const std::size_t offset =
      (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)) * 4;
    return {image.rgba[offset], image.rgba[offset + 1], image.rgba[offset + 2], image.rgba[offset + 3]};
  }

  /// A BC1 colour block: two RGB565 endpoints and index i -> indices[i].
  std::vector<std::uint8_t> ColorBlock(
    const std::uint16_t c0,
    const std::uint16_t c1,
    const std::array<int, 16>& indices
  )
  {
    std::uint32_t bits = 0;
    for (std::size_t i = 0; i < 16; ++i) {
      bits |= static_cast<std::uint32_t>(indices[i] & 3) << (2 * i);
    }
    return {
      static_cast<std::uint8_t>(c0), static_cast<std::uint8_t>(c0 >> 8), static_cast<std::uint8_t>(c1),
      static_cast<std::uint8_t>(c1 >> 8), static_cast<std::uint8_t>(bits), static_cast<std::uint8_t>(bits >> 8),
      static_cast<std::uint8_t>(bits >> 16), static_cast<std::uint8_t>(bits >> 24),
    };
  }

  constexpr std::array<int, 16> kCycle4 = {0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3};
  constexpr std::uint16_t kRed565 = 0xF800;
  constexpr std::uint16_t kBlue565 = 0x001F;
} // namespace

FAF_TEST(DecodesDxt1FourAndThreeColourBlocks)
{
  // Two blocks side by side: 4-colour (red > blue) and 3-colour (blue <= red).
  std::vector<std::uint8_t> payload = ColorBlock(kRed565, kBlue565, kCycle4);
  const std::vector<std::uint8_t> second = ColorBlock(kBlue565, kRed565, kCycle4);
  payload.insert(payload.end(), second.begin(), second.end());

  Image image;
  std::string error;
  FAF_REQUIRE(DecodeDds(MakeDds(8, 4, {0x4, FourCc("DXT1")}, payload).data(), 128 + payload.size(), image, &error));
  FAF_CHECK_EQ(image.format, std::string("DXT1"));
  FAF_CHECK_EQ(image.width, 8);
  FAF_CHECK_EQ(image.height, 4);
  FAF_CHECK((PixelAt(image, 0, 0) == Rgba{255, 0, 0, 255}));
  FAF_CHECK((PixelAt(image, 1, 0) == Rgba{0, 0, 255, 255}));
  FAF_CHECK((PixelAt(image, 2, 0) == Rgba{170, 0, 85, 255}));
  FAF_CHECK((PixelAt(image, 3, 3) == Rgba{85, 0, 170, 255}));
  FAF_CHECK((PixelAt(image, 4, 0) == Rgba{0, 0, 255, 255}));
  FAF_CHECK((PixelAt(image, 6, 1) == Rgba{127, 0, 127, 255}));
  FAF_CHECK((PixelAt(image, 7, 2) == Rgba{0, 0, 0, 0})); // transparent black
}

FAF_TEST(DecodesDxt5AlphaModes)
{
  // Block 1: alpha 255..0 in 8-value mode; block 2: 0..255 in 6-value mode.
  const auto alphaBlock = [](const std::uint8_t a0, const std::uint8_t a1) {
    std::uint64_t bits = 0;
    for (std::uint64_t i = 0; i < 16; ++i) {
      bits |= (i % 8) << (3 * i);
    }
    std::vector<std::uint8_t> block = {a0, a1};
    for (int i = 0; i < 6; ++i) {
      block.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
    }
    const std::vector<std::uint8_t> color = ColorBlock(kRed565, kBlue565, kCycle4);
    block.insert(block.end(), color.begin(), color.end());
    return block;
  };
  std::vector<std::uint8_t> payload = alphaBlock(255, 0);
  const std::vector<std::uint8_t> second = alphaBlock(0, 255);
  payload.insert(payload.end(), second.begin(), second.end());

  Image image;
  FAF_REQUIRE(DecodeDds(MakeDds(8, 4, {0x4, FourCc("DXT5")}, payload).data(), 128 + payload.size(), image));
  FAF_CHECK_EQ(image.format, std::string("DXT5"));
  const std::array<int, 8> eight = {255, 0, 218, 182, 145, 109, 72, 36};
  const std::array<int, 8> six = {0, 255, 51, 102, 153, 204, 0, 255};
  for (int i = 0; i < 16; ++i) {
    FAF_CHECK_EQ(PixelAt(image, i % 4, i / 4)[3], eight[static_cast<std::size_t>(i % 8)]);
    FAF_CHECK_EQ(PixelAt(image, 4 + i % 4, i / 4)[3], six[static_cast<std::size_t>(i % 8)]);
  }
  // DXT5 colour always uses the 4-colour mode.
  FAF_CHECK_EQ(PixelAt(image, 2, 0)[0], 170);
}

FAF_TEST(DecodesDxt3AndClipsPartialBlocks)
{
  std::vector<std::uint8_t> block;
  for (int i = 0; i < 8; ++i) {
    block.push_back(static_cast<std::uint8_t>((((2 * i + 1) & 0xF) << 4) | ((2 * i) & 0xF)));
  }
  const std::vector<std::uint8_t> color = ColorBlock(kRed565, kRed565, kCycle4);
  block.insert(block.end(), color.begin(), color.end());
  std::vector<std::uint8_t> payload = block;
  payload.insert(payload.end(), block.begin(), block.end()); // 5x3 -> 2x1 blocks

  Image image;
  FAF_REQUIRE(DecodeDds(MakeDds(5, 3, {0x4, FourCc("DXT3")}, payload).data(), 128 + payload.size(), image));
  FAF_CHECK_EQ(image.rgba.size(), std::size_t{5 * 3 * 4});
  FAF_CHECK_EQ(PixelAt(image, 0, 0)[3], 0);
  FAF_CHECK_EQ(PixelAt(image, 1, 0)[3], 17);
  FAF_CHECK_EQ(PixelAt(image, 3, 2)[3], 11 * 17);
  FAF_CHECK_EQ(PixelAt(image, 4, 2)[3], 8 * 17); // first column of the second block
  FAF_CHECK_EQ(PixelAt(image, 4, 2)[0], 255);
}

FAF_TEST(DecodesUncompressedFormats)
{
  Image image;
  // A8R8G8B8 as D3DX writes it: bytes B, G, R, A.
  const std::vector<std::uint8_t> argb = {10, 20, 30, 40, 50, 60, 70, 80};
  FAF_REQUIRE(DecodeDds(
    MakeDds(2, 1, {0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000}, argb).data(), 128 + argb.size(), image
  ));
  FAF_CHECK_EQ(image.format, std::string("ARGB32"));
  FAF_CHECK((PixelAt(image, 0, 0) == Rgba{30, 20, 10, 40}));
  FAF_CHECK((PixelAt(image, 1, 0) == Rgba{70, 60, 50, 80}));

  // R5G6B5: pure green, opaque.
  const std::vector<std::uint8_t> rgb565 = {0xE0, 0x07};
  FAF_REQUIRE(DecodeDds(MakeDds(1, 1, {0x40, 0, 16, 0xF800, 0x07E0, 0x001F, 0}, rgb565).data(), 130, image));
  FAF_CHECK_EQ(image.format, std::string("RGB565"));
  FAF_CHECK((PixelAt(image, 0, 0) == Rgba{0, 255, 0, 255}));

  // RGB24, luminance L8, alpha-only A8.
  const std::vector<std::uint8_t> rgb24 = {1, 2, 3};
  FAF_REQUIRE(DecodeDds(MakeDds(1, 1, {0x40, 0, 24, 0xFF0000, 0x00FF00, 0x0000FF, 0}, rgb24).data(), 131, image));
  FAF_CHECK((PixelAt(image, 0, 0) == Rgba{3, 2, 1, 255}));
  const std::vector<std::uint8_t> l8 = {77};
  FAF_REQUIRE(DecodeDds(MakeDds(1, 1, {0x20000, 0, 8, 0xFF, 0, 0, 0}, l8).data(), 129, image));
  FAF_CHECK((PixelAt(image, 0, 0) == Rgba{77, 77, 77, 255}));
  FAF_REQUIRE(DecodeDds(MakeDds(1, 1, {0x2, 0, 8, 0, 0, 0, 0xFF}, l8).data(), 129, image));
  FAF_CHECK((PixelAt(image, 0, 0) == Rgba{0, 0, 0, 77}));
}

FAF_TEST(DecodesDx10Bc1)
{
  std::vector<std::uint8_t> payload(20, 0);
  Put32(payload, 0, 71); // DXGI_FORMAT_BC1_UNORM
  Put32(payload, 4, 3);  // TEXTURE2D
  const std::vector<std::uint8_t> block = ColorBlock(kRed565, kBlue565, kCycle4);
  payload.insert(payload.end(), block.begin(), block.end());
  Image image;
  FAF_REQUIRE(DecodeDds(MakeDds(4, 4, {0x4, FourCc("DX10")}, payload).data(), 128 + payload.size(), image));
  FAF_CHECK_EQ(image.format, std::string("BC1"));
  FAF_CHECK((PixelAt(image, 1, 1) == Rgba{0, 0, 255, 255}));
}

FAF_TEST(RejectsTruncatedAndInvalidFiles)
{
  const std::vector<std::uint8_t> block = ColorBlock(kRed565, kBlue565, kCycle4);
  const std::vector<std::uint8_t> dds = MakeDds(8, 8, {0x4, FourCc("DXT1")}, std::vector<std::uint8_t>(31, 0));
  Image image;
  std::string error;
  FAF_CHECK(!DecodeDds(dds.data(), dds.size(), image, &error)); // needs 32 bytes of blocks
  FAF_CHECK(error.find("truncated") != std::string::npos);
  FAF_CHECK(image.rgba.empty());

  for (std::size_t length = 0; length < 128 + block.size(); ++length) {
    const std::vector<std::uint8_t> full = MakeDds(4, 4, {0x4, FourCc("DXT5")}, std::vector<std::uint8_t>(16, 0xAB));
    FAF_CHECK(!DecodeDds(full.data(), std::min(length, std::size_t{128 + 15}), image));
  }

  std::vector<std::uint8_t> bad = MakeDds(4, 4, {0x4, FourCc("DXT1")}, block);
  bad[0] = 'X';
  FAF_CHECK(!DecodeDds(bad.data(), bad.size(), image));
  FAF_CHECK(!DecodeDds(MakeDds(0, 4, {0x4, FourCc("DXT1")}, block).data(), 136, image));
  FAF_CHECK(!DecodeDds(MakeDds(100000, 100000, {0x4, FourCc("DXT1")}, block).data(), 136, image));
  FAF_CHECK(!DecodeDds(MakeDds(4, 4, {0x4, FourCc("ATI2")}, block).data(), 136, image));
  FAF_CHECK(!DecodeDds(MakeDds(4, 4, {0x40, 0, 12, 1, 2, 4, 0}, block).data(), 136, image));
  FAF_CHECK(!DecodeDds(nullptr, 0, image));
  // 32-bit 4x4 needs 64 bytes; give 63.
  FAF_CHECK(!DecodeDds(
    MakeDds(4, 4, {0x41, 0, 32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000}, std::vector<std::uint8_t>(63, 1)).data(),
    191,
    image
  ));
}

FAF_TEST(WritesReadablePng)
{
  faf::test::TempDir dir;
  Image image;
  image.width = 3;
  image.height = 2;
  for (int i = 0; i < 24; ++i) {
    image.rgba.push_back(static_cast<std::uint8_t>(i * 10));
  }
  const std::string path = dir / "out.png";
  FAF_REQUIRE(faf::port::WritePng(path, image));

  std::vector<unsigned char> png;
  FAF_REQUIRE(faf::port::fs::ReadWholeFile(path, png));
  FAF_REQUIRE(png.size() > 8 + 25 + 12);
  FAF_CHECK(std::memcmp(png.data(), "\x89PNG\r\n\x1A\n", 8) == 0);
  const auto be32 = [&](const std::size_t at) {
    return (std::uint32_t{png[at]} << 24) | (std::uint32_t{png[at + 1]} << 16) | (std::uint32_t{png[at + 2]} << 8) |
           std::uint32_t{png[at + 3]};
  };
  FAF_CHECK_EQ(be32(8), 13u);
  FAF_CHECK(std::memcmp(&png[12], "IHDR", 4) == 0);
  FAF_CHECK_EQ(be32(16), 3u);
  FAF_CHECK_EQ(be32(20), 2u);
  FAF_CHECK_EQ(be32(8 + 8 + 13), static_cast<std::uint32_t>(crc32(0L, &png[12], 17)));

  // Walk the chunks, inflate IDAT and compare the scanlines.
  std::vector<std::uint8_t> idat;
  std::size_t pos = 8;
  bool sawEnd = false;
  while (pos + 12 <= png.size()) {
    const std::uint32_t length = be32(pos);
    const std::string type(reinterpret_cast<const char*>(&png[pos + 4]), 4);
    FAF_REQUIRE(pos + 12 + length <= png.size());
    FAF_CHECK_EQ(be32(pos + 8 + length), static_cast<std::uint32_t>(crc32(0L, &png[pos + 4], length + 4)));
    if (type == "IDAT") {
      idat.insert(
        idat.end(),
        png.begin() + static_cast<std::ptrdiff_t>(pos + 8),
        png.begin() + static_cast<std::ptrdiff_t>(pos + 8 + length)
      );
    }
    sawEnd = type == "IEND";
    pos += 12 + length;
  }
  FAF_CHECK(sawEnd);
  std::vector<std::uint8_t> raw(2 * (1 + 12));
  uLongf rawSize = static_cast<uLongf>(raw.size());
  FAF_REQUIRE(uncompress(raw.data(), &rawSize, idat.data(), static_cast<uLong>(idat.size())) == Z_OK);
  FAF_CHECK_EQ(rawSize, static_cast<uLongf>(raw.size()));
  FAF_CHECK_EQ(static_cast<int>(raw[0]), 0);
  FAF_CHECK(std::memcmp(&raw[1], image.rgba.data(), 12) == 0);
  FAF_CHECK(std::memcmp(&raw[14], image.rgba.data() + 12, 12) == 0);

  Image empty;
  FAF_CHECK(!faf::port::WritePng(dir / "empty.png", empty));
}
