#pragma once

// The image files besides DDS that the texture codec (ImageCodecAndroid.cpp) reads and writes on
// Android, where D3DX11 reads them through WIC: PNG (on zlib), TGA and BMP, as R8G8B8A8.
// The game has PNG files (the UI's and FAF's), one TGA and four BMPs, and no JPG; a JPG's size is
// read, its pixels are not.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gpg::gal::imaging::files
{
  enum class Container
  {
    Unknown,
    Dds,
    Png,
    Bmp,
    Jpg,
    Tga,
  };

  /** R8G8B8A8, `width` * 4 bytes per row, top row first. */
  struct Pixels
  {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;
  };

  /** The file's kind, as D3DX tells them apart: by their signatures, TGA by a plausible header. */
  [[nodiscard]] Container Identify(const std::uint8_t* data, std::size_t bytes) noexcept;

  /** The image's size from the header of a PNG, BMP, JPG or TGA. */
  [[nodiscard]] bool ReadSize(Container container, const std::uint8_t* data, std::size_t bytes, std::uint32_t& outWidth,
                              std::uint32_t& outHeight) noexcept;

  /** The pixels of a PNG, BMP or TGA. */
  [[nodiscard]] bool Decode(Container container, const std::uint8_t* data, std::size_t bytes, Pixels& outPixels);

  /** A 32 bpp top-down BMP, as ImageCodec.cpp writes 8-bit RGBA and BGRA images. */
  void WriteBmp(const Pixels& pixels, std::vector<std::uint8_t>& outBytes);

  /** An 8-bit RGBA PNG. */
  [[nodiscard]] bool WritePng(const Pixels& pixels, std::vector<std::uint8_t>& outBytes);
} // namespace gpg::gal::imaging::files
