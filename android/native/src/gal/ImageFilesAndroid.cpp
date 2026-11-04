// PNG, TGA and BMP for the Android client build's texture codec: see ImageFilesAndroid.h.

#include "ImageFilesAndroid.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <zlib.h>

namespace gpg::gal::imaging::files
{
  namespace
  {
    [[nodiscard]] std::uint32_t Be32(const std::uint8_t* const at) noexcept
    {
      return (static_cast<std::uint32_t>(at[0]) << 24U) | (static_cast<std::uint32_t>(at[1]) << 16U) |
        (static_cast<std::uint32_t>(at[2]) << 8U) | at[3];
    }

    [[nodiscard]] std::uint32_t Le16(const std::uint8_t* const at) noexcept
    {
      return static_cast<std::uint32_t>(at[0]) | (static_cast<std::uint32_t>(at[1]) << 8U);
    }

    [[nodiscard]] std::uint32_t Le32(const std::uint8_t* const at) noexcept
    {
      return Le16(at) | (Le16(at + 2) << 16U);
    }

    void PutBe32(std::vector<std::uint8_t>& out, const std::uint32_t value)
    {
      for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
      }
    }

    void PutLe(std::vector<std::uint8_t>& out, const std::uint32_t value, const int bytes)
    {
      for (int index = 0; index < bytes; ++index) {
        out.push_back(static_cast<std::uint8_t>(value >> (index * 8)));
      }
    }

    constexpr std::uint8_t kPngSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

    [[nodiscard]] bool PlausibleTga(const std::uint8_t* const data, const std::size_t bytes) noexcept
    {
      if (bytes < 18U) {
        return false;
      }
      const std::uint32_t mapType = data[1];
      const std::uint32_t type = data[2];
      const std::uint32_t bpp = data[16];
      const bool mapped = type == 1U || type == 9U;
      const bool gray = type == 3U || type == 11U;
      const bool known = mapped || gray || type == 2U || type == 10U;
      // True colour takes 15 bits at least.
      const bool bppKnown = bpp == 15U || bpp == 16U || bpp == 24U || bpp == 32U || (bpp == 8U && (mapped || gray));
      return known && mapType == (mapped ? 1U : 0U) && bppKnown && Le16(data + 12) != 0U && Le16(data + 14) != 0U;
    }

    // ---- PNG ------------------------------------------------------------------------------------

    /**
     * The header is the first chunk: that is where ReadSize takes the size from, and the engine sizes
     * its copy of a batch texture's blocks by that answer (CD3DRawBatchTexture::BuildTextureData).
     */
    [[nodiscard]] bool PngHeaderFirst(const std::uint8_t* const data, const std::size_t bytes) noexcept
    {
      return bytes >= 33U && Be32(data + 8) == 13U && std::memcmp(data + 12, "IHDR", 4) == 0;
    }

    /** The colour types and depths PNG has. */
    [[nodiscard]] bool PngFormatKnown(const std::uint32_t colorType, const std::uint32_t depth) noexcept
    {
      switch (colorType) {
      case 0:
        return depth == 1U || depth == 2U || depth == 4U || depth == 8U || depth == 16U;
      case 3:
        return depth == 1U || depth == 2U || depth == 4U || depth == 8U;
      case 2:
      case 4:
      case 6:
        return depth == 8U || depth == 16U;
      default:
        return false;
      }
    }

    /** Inflates a zlib stream into exactly `expected` bytes. */
    [[nodiscard]] bool Inflate(const std::vector<std::uint8_t>& input, const std::size_t expected, std::vector<std::uint8_t>& output)
    {
      output.assign(expected, 0U);
      z_stream stream{};
      if (inflateInit(&stream) != Z_OK) {
        return false;
      }
      stream.next_in = const_cast<Bytef*>(input.data());
      stream.avail_in = static_cast<uInt>(input.size());
      stream.next_out = output.data();
      stream.avail_out = static_cast<uInt>(output.size());
      const int result = inflate(&stream, Z_FINISH);
      const bool complete = stream.total_out == expected && (result == Z_STREAM_END || result == Z_BUF_ERROR || result == Z_OK);
      inflateEnd(&stream);
      return complete;
    }

    [[nodiscard]] std::uint8_t Paeth(const int left, const int up, const int upLeft) noexcept
    {
      const int estimate = left + up - upLeft;
      const int toLeft = std::abs(estimate - left);
      const int toUp = std::abs(estimate - up);
      const int toUpLeft = std::abs(estimate - upLeft);
      if (toLeft <= toUp && toLeft <= toUpLeft) {
        return static_cast<std::uint8_t>(left);
      }
      return static_cast<std::uint8_t>(toUp <= toUpLeft ? up : upLeft);
    }

    /** Undoes the row filters of one (sub)image in place: `rows` rows of 1 + `rowBytes` bytes. */
    [[nodiscard]] bool Unfilter(std::uint8_t* const data, const std::uint32_t rows, const std::size_t rowBytes, const std::size_t pixelBytes)
    {
      for (std::uint32_t row = 0U; row < rows; ++row) {
        std::uint8_t* const line = data + row * (rowBytes + 1U);
        std::uint8_t* const current = line + 1;
        const std::uint8_t* const previous = row > 0U ? data + (row - 1U) * (rowBytes + 1U) + 1 : nullptr;
        const std::uint8_t filter = line[0];
        for (std::size_t at = 0U; at < rowBytes; ++at) {
          const int left = at >= pixelBytes ? current[at - pixelBytes] : 0;
          const int up = previous != nullptr ? previous[at] : 0;
          const int upLeft = (previous != nullptr && at >= pixelBytes) ? previous[at - pixelBytes] : 0;
          switch (filter) {
          case 0:
            break;
          case 1:
            current[at] = static_cast<std::uint8_t>(current[at] + left);
            break;
          case 2:
            current[at] = static_cast<std::uint8_t>(current[at] + up);
            break;
          case 3:
            current[at] = static_cast<std::uint8_t>(current[at] + ((left + up) >> 1));
            break;
          case 4:
            current[at] = static_cast<std::uint8_t>(current[at] + Paeth(left, up, upLeft));
            break;
          default:
            return false;
          }
        }
      }
      return true;
    }

    struct PngHeader
    {
      std::uint32_t width = 0;
      std::uint32_t height = 0;
      std::uint32_t depth = 0;
      std::uint32_t colorType = 0;
      std::uint32_t interlace = 0;
      std::uint8_t palette[256][4] = {};
      bool hasKey = false;
      std::uint32_t key[3] = {};
    };

    [[nodiscard]] std::uint32_t Channels(const std::uint32_t colorType) noexcept
    {
      switch (colorType) {
      case 0:
      case 3:
        return 1U;
      case 2:
        return 3U;
      case 4:
        return 2U;
      default:
        return 4U;
      }
    }

    /** Sample `index` of a row at the header's depth (16-bit samples whole, big endian). */
    [[nodiscard]] std::uint32_t Sample(const std::uint8_t* const row, const std::size_t index, const std::uint32_t depth) noexcept
    {
      if (depth == 8U) {
        return row[index];
      }
      if (depth == 16U) {
        return (static_cast<std::uint32_t>(row[index * 2U]) << 8U) | row[index * 2U + 1U];
      }
      const std::size_t bit = index * depth;
      return (row[bit / 8U] >> (8U - depth - (bit % 8U))) & ((1U << depth) - 1U);
    }

    /** A sample to 8 bits: low depths scaled up exactly, 16 bits rounded to the nearest. */
    [[nodiscard]] std::uint8_t To8(const std::uint32_t value, const std::uint32_t depth) noexcept
    {
      if (depth == 16U) {
        return static_cast<std::uint8_t>((value * 255U + 32767U) / 65535U);
      }
      return static_cast<std::uint8_t>(value * 255U / ((1U << depth) - 1U));
    }

    void PngPixel(const PngHeader& header, const std::uint8_t* const row, const std::uint32_t x, std::uint8_t* const out) noexcept
    {
      const std::uint32_t channels = Channels(header.colorType);
      const std::uint32_t depth = header.depth;
      std::uint32_t samples[4] = {};
      for (std::uint32_t channel = 0U; channel < channels; ++channel) {
        samples[channel] = Sample(row, static_cast<std::size_t>(x) * channels + channel, depth);
      }
      switch (header.colorType) {
      case 3:
        std::memcpy(out, header.palette[samples[0] & 255U], 4U);
        return;
      case 0:
        out[0] = out[1] = out[2] = To8(samples[0], depth);
        out[3] = (header.hasKey && samples[0] == header.key[0]) ? 0U : 255U;
        return;
      case 4:
        out[0] = out[1] = out[2] = To8(samples[0], depth);
        out[3] = To8(samples[1], depth);
        return;
      case 2:
        for (int channel = 0; channel < 3; ++channel) {
          out[channel] = To8(samples[channel], depth);
        }
        out[3] = (header.hasKey && samples[0] == header.key[0] && samples[1] == header.key[1] && samples[2] == header.key[2]) ? 0U : 255U;
        return;
      default:
        for (int channel = 0; channel < 4; ++channel) {
          out[channel] = To8(samples[channel], depth);
        }
        return;
      }
    }

    [[nodiscard]] bool DecodePng(const std::uint8_t* const data, const std::size_t bytes, Pixels& out)
    {
      if (!PngHeaderFirst(data, bytes)) {
        return false;
      }
      PngHeader header;
      header.width = Be32(data + 16);
      header.height = Be32(data + 20);
      header.depth = data[24];
      header.colorType = data[25];
      header.interlace = data[28];
      if (!PngFormatKnown(header.colorType, header.depth) || header.interlace > 1U || header.width == 0U || header.height == 0U ||
          header.width > kMaxSide || header.height > kMaxSide) {
        return false;
      }
      std::vector<std::uint8_t> compressed;
      for (std::uint32_t entry = 0U; entry < 256U; ++entry) {
        header.palette[entry][3] = 255U;
      }
      std::size_t at = 33U;
      while (at + 12U <= bytes) {
        const std::uint32_t length = Be32(data + at);
        const std::uint8_t* const type = data + at + 4;
        const std::uint8_t* const body = data + at + 8;
        if (length > bytes - at - 12U) {
          return false;
        }
        if (std::memcmp(type, "PLTE", 4) == 0) {
          for (std::uint32_t entry = 0U; entry < 256U && entry * 3U + 2U < length; ++entry) {
            std::memcpy(header.palette[entry], body + entry * 3U, 3U);
          }
        } else if (std::memcmp(type, "tRNS", 4) == 0) {
          if (header.colorType == 3U) {
            for (std::uint32_t entry = 0U; entry < 256U && entry < length; ++entry) {
              header.palette[entry][3] = body[entry];
            }
          } else if (header.colorType == 0U && length >= 2U) {
            header.hasKey = true;
            header.key[0] = (static_cast<std::uint32_t>(body[0]) << 8U) | body[1];
          } else if (header.colorType == 2U && length >= 6U) {
            header.hasKey = true;
            for (int channel = 0; channel < 3; ++channel) {
              header.key[channel] = (static_cast<std::uint32_t>(body[channel * 2]) << 8U) | body[channel * 2 + 1];
            }
          }
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
          compressed.insert(compressed.end(), body, body + length);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
          break;
        }
        at += 12U + static_cast<std::size_t>(length);
      }
      const std::size_t pixelBits = static_cast<std::size_t>(Channels(header.colorType)) * header.depth;
      const std::size_t pixelBytes = std::max<std::size_t>(1U, pixelBits / 8U);
      struct Pass
      {
        std::uint32_t x0, y0, dx, dy;
      };
      static const Pass kAdam7[7] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4}, {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};
      static const Pass kWhole[1] = {{0, 0, 1, 1}};
      const Pass* const passes = header.interlace != 0U ? kAdam7 : kWhole;
      const int passCount = header.interlace != 0U ? 7 : 1;
      const auto passSize = [&](const Pass& pass, std::uint32_t& columns, std::uint32_t& rows) {
        columns = pass.x0 < header.width ? (header.width - pass.x0 + pass.dx - 1U) / pass.dx : 0U;
        rows = pass.y0 < header.height ? (header.height - pass.y0 + pass.dy - 1U) / pass.dy : 0U;
        return columns != 0U && rows != 0U;
      };
      std::size_t expected = 0U;
      for (int pass = 0; pass < passCount; ++pass) {
        std::uint32_t columns = 0U, rows = 0U;
        if (passSize(passes[pass], columns, rows)) {
          expected += static_cast<std::size_t>(rows) * (1U + (columns * pixelBits + 7U) / 8U);
        }
      }
      // Deflate makes at most 1032 bytes of one: a stream too short for the image takes no memory.
      if (expected > compressed.size() * 1032U) {
        return false;
      }
      std::vector<std::uint8_t> raw;
      if (!Inflate(compressed, expected, raw)) {
        return false;
      }
      out.width = header.width;
      out.height = header.height;
      out.rgba.assign(static_cast<std::size_t>(header.width) * header.height * 4U, 0U);
      std::size_t offset = 0U;
      for (int pass = 0; pass < passCount; ++pass) {
        const Pass& p = passes[pass];
        std::uint32_t columns = 0U, rows = 0U;
        if (!passSize(p, columns, rows)) {
          continue;
        }
        const std::size_t rowBytes = (columns * pixelBits + 7U) / 8U;
        if (!Unfilter(raw.data() + offset, rows, rowBytes, pixelBytes)) {
          return false;
        }
        for (std::uint32_t row = 0U; row < rows; ++row) {
          const std::uint8_t* const line = raw.data() + offset + row * (rowBytes + 1U) + 1U;
          for (std::uint32_t column = 0U; column < columns; ++column) {
            const std::size_t x = p.x0 + column * p.dx;
            const std::size_t y = p.y0 + row * p.dy;
            PngPixel(header, line, column, out.rgba.data() + (y * header.width + x) * 4U);
          }
        }
        offset += rows * (rowBytes + 1U);
      }
      return true;
    }

    // ---- TGA ------------------------------------------------------------------------------------

    /** One TGA pixel (BGR, BGRA, A1R5G5B5 or gray) as RGBA. */
    void TgaPixel(const std::uint8_t* const at, const std::uint32_t bits, const bool gray, const bool alpha, std::uint8_t* const out) noexcept
    {
      if (gray) {
        out[0] = out[1] = out[2] = at[0];
        out[3] = 255U;
      } else if (bits == 15U || bits == 16U) {
        const std::uint32_t value = Le16(at);
        out[0] = static_cast<std::uint8_t>((((value >> 10U) & 31U) * 255U + 15U) / 31U);
        out[1] = static_cast<std::uint8_t>((((value >> 5U) & 31U) * 255U + 15U) / 31U);
        out[2] = static_cast<std::uint8_t>(((value & 31U) * 255U + 15U) / 31U);
        out[3] = (alpha && bits == 16U) ? ((value & 0x8000U) != 0U ? 255U : 0U) : 255U;
      } else {
        out[0] = at[2];
        out[1] = at[1];
        out[2] = at[0];
        out[3] = (bits == 32U && alpha) ? at[3] : 255U;
      }
    }

    [[nodiscard]] bool DecodeTga(const std::uint8_t* const data, const std::size_t bytes, Pixels& out)
    {
      const std::uint32_t type = data[2];
      const std::uint32_t mapFirst = Le16(data + 3);
      const std::uint32_t mapLength = Le16(data + 5);
      const std::uint32_t mapBits = data[7];
      const std::uint32_t width = Le16(data + 12);
      const std::uint32_t height = Le16(data + 14);
      const std::uint32_t bits = data[16];
      const std::uint32_t descriptor = data[17];
      const bool rle = type >= 9U;
      const bool mapped = type == 1U || type == 9U;
      const bool gray = type == 3U || type == 11U;
      // The attribute bits say whether the alpha channel holds anything.
      const bool alpha = (descriptor & 15U) != 0U;
      const std::uint32_t pixelBytes = (bits + 7U) / 8U;
      const std::uint32_t mapEntryBytes = (mapBits + 7U) / 8U;
      const std::size_t mapAt = 18U + data[0];
      std::size_t at = mapAt + static_cast<std::size_t>(mapLength) * mapEntryBytes;
      if (width > kMaxSide || height > kMaxSide || at > bytes || (mapped && mapBits != 15U && mapBits != 16U && mapBits != 24U && mapBits != 32U)) {
        return false;
      }
      const std::uint8_t* const map = data + mapAt;
      const std::size_t count = static_cast<std::size_t>(width) * height;
      // A file too short for its image takes no memory: a run packet holds 128 pixels at most.
      const std::size_t leastBytes = rle ? (count + 127U) / 128U * (1U + pixelBytes) : count * pixelBytes;
      if (leastBytes > bytes - at) {
        return false;
      }
      std::vector<std::uint8_t> linear(count * 4U, 0U);
      const auto pixel = [&](const std::uint8_t* const source, std::uint8_t* const target) {
        if (mapped) {
          const std::uint32_t index = (pixelBytes == 1U ? source[0] : Le16(source)) - mapFirst;
          if (index < mapLength) {
            TgaPixel(map + index * mapEntryBytes, mapBits, false, alpha || mapBits == 32U, target);
          }
        } else {
          TgaPixel(source, bits, gray, alpha, target);
        }
      };
      std::size_t written = 0U;
      while (written < count) {
        if (!rle) {
          pixel(data + at, linear.data() + written * 4U);
          at += pixelBytes;
          ++written;
          continue;
        }
        if (at >= bytes) {
          return false;
        }
        const std::uint32_t packet = data[at++];
        const std::size_t run = std::min<std::size_t>((packet & 0x7FU) + 1U, count - written);
        if ((packet & 0x80U) != 0U) {
          if (at + pixelBytes > bytes) {
            return false;
          }
          for (std::size_t index = 0U; index < run; ++index) {
            pixel(data + at, linear.data() + (written + index) * 4U);
          }
          at += pixelBytes;
        } else {
          if (at + run * pixelBytes > bytes) {
            return false;
          }
          for (std::size_t index = 0U; index < run; ++index) {
            pixel(data + at + index * pixelBytes, linear.data() + (written + index) * 4U);
          }
          at += run * pixelBytes;
        }
        written += run;
      }
      // Bit 5: the first row is the top one; bit 4: the first column is the right one.
      const bool topFirst = (descriptor & 0x20U) != 0U;
      const bool rightFirst = (descriptor & 0x10U) != 0U;
      out.width = width;
      out.height = height;
      out.rgba.resize(linear.size());
      for (std::uint32_t y = 0U; y < height; ++y) {
        const std::uint32_t sourceY = topFirst ? y : height - 1U - y;
        for (std::uint32_t x = 0U; x < width; ++x) {
          const std::uint32_t sourceX = rightFirst ? width - 1U - x : x;
          std::memcpy(out.rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4U,
                      linear.data() + (static_cast<std::size_t>(sourceY) * width + sourceX) * 4U, 4U);
        }
      }
      return true;
    }

    // ---- BMP ------------------------------------------------------------------------------------

    [[nodiscard]] std::uint8_t MaskValue(const std::uint32_t value, const std::uint32_t mask) noexcept
    {
      if (mask == 0U) {
        return 255U;
      }
      std::uint32_t shift = 0U;
      while (((mask >> shift) & 1U) == 0U) {
        ++shift;
      }
      const std::uint32_t maximum = mask >> shift;
      return static_cast<std::uint8_t>((static_cast<std::uint64_t>((value & mask) >> shift) * 255U + maximum / 2U) / maximum);
    }

    /** A BMP's height whichever way its rows run; -2^31 has one as an unsigned number too. */
    [[nodiscard]] std::uint32_t Magnitude(const std::int32_t value) noexcept
    {
      return value < 0 ? 0U - static_cast<std::uint32_t>(value) : static_cast<std::uint32_t>(value);
    }

    [[nodiscard]] bool DecodeBmp(const std::uint8_t* const data, const std::size_t bytes, Pixels& out)
    {
      if (bytes < 54U) {
        return false;
      }
      const std::size_t pixelsAt = Le32(data + 10);
      const std::size_t headerBytes = Le32(data + 14);
      const auto width = static_cast<std::int32_t>(Le32(data + 18));
      const auto signedHeight = static_cast<std::int32_t>(Le32(data + 22));
      const std::uint32_t bits = Le16(data + 28);
      const std::uint32_t compression = Le32(data + 30);
      const std::uint32_t height = Magnitude(signedHeight);
      const bool bitsKnown = bits == 1U || bits == 2U || bits == 4U || bits == 8U || bits == 16U || bits == 24U || bits == 32U;
      // A shorter header has these fields elsewhere; one longer than the file is no header.
      if (headerBytes < 40U || headerBytes > bytes - 14U || !bitsKnown || width <= 0 || height == 0U ||
          static_cast<std::uint32_t>(width) > kMaxSide || height > kMaxSide || (compression != 0U && compression != 3U)) {
        return false;
      }
      // 16 and 32 bits: X1R5G5B5 and X8R8G8B8 unless masks say otherwise; no alpha, as WIC reads them.
      std::uint32_t masks[4] = {bits == 16U ? 0x7C00U : 0x00FF0000U, bits == 16U ? 0x03E0U : 0x0000FF00U,
                                bits == 16U ? 0x001FU : 0x000000FFU, 0U};
      if (compression == 3U) {
        const std::size_t maskAt = 14U + (headerBytes >= 52U ? 40U : headerBytes);
        if (maskAt + 12U > bytes) {
          return false;
        }
        for (int channel = 0; channel < 3; ++channel) {
          masks[channel] = Le32(data + maskAt + channel * 4);
        }
        if (headerBytes >= 56U) {
          masks[3] = Le32(data + 14U + 52U);
        }
      }
      // No more colours than the pixels can index, whatever the header says.
      const std::uint32_t indexable = bits <= 8U ? 1U << bits : 0U;
      const std::uint32_t declared = Le32(data + 46);
      const std::uint32_t colors = (declared == 0U || declared > indexable) ? indexable : declared;
      const std::size_t paletteAt = 14U + headerBytes;
      const std::size_t stride = ((static_cast<std::size_t>(width) * bits + 31U) / 32U) * 4U;
      if (pixelsAt > bytes || stride * height > bytes - pixelsAt || colors * 4U > bytes - paletteAt) {
        return false;
      }
      const std::uint8_t* const palette = data + paletteAt;
      out.width = static_cast<std::uint32_t>(width);
      out.height = height;
      out.rgba.assign(static_cast<std::size_t>(width) * height * 4U, 255U);
      for (std::uint32_t y = 0U; y < height; ++y) {
        const std::uint8_t* const row = data + pixelsAt + stride * (signedHeight < 0 ? y : height - 1U - y);
        for (std::uint32_t x = 0U; x < out.width; ++x) {
          std::uint8_t* const target = out.rgba.data() + (static_cast<std::size_t>(y) * out.width + x) * 4U;
          if (bits <= 8U) {
            const std::uint32_t index = (row[x * bits / 8U] >> (8U - bits - (x * bits) % 8U)) & ((1U << bits) - 1U);
            if (index < colors) {
              target[0] = palette[index * 4U + 2U];
              target[1] = palette[index * 4U + 1U];
              target[2] = palette[index * 4U];
            }
          } else if (bits == 24U) {
            target[0] = row[x * 3U + 2U];
            target[1] = row[x * 3U + 1U];
            target[2] = row[x * 3U];
          } else {
            const std::uint32_t value = bits == 16U ? Le16(row + x * 2U) : Le32(row + x * 4U);
            for (int channel = 0; channel < 4; ++channel) {
              target[channel] = MaskValue(value, masks[channel]);
            }
          }
        }
      }
      return true;
    }
  } // namespace

  Container Identify(const std::uint8_t* const data, const std::size_t bytes) noexcept
  {
    if (bytes >= 4U && std::memcmp(data, "DDS ", 4) == 0) {
      return Container::Dds;
    }
    if (bytes >= 8U && std::memcmp(data, kPngSignature, 8) == 0) {
      return Container::Png;
    }
    if (bytes >= 2U && data[0] == 'B' && data[1] == 'M') {
      return Container::Bmp;
    }
    if (bytes >= 3U && data[0] == 0xFFU && data[1] == 0xD8U && data[2] == 0xFFU) {
      return Container::Jpg;
    }
    return PlausibleTga(data, bytes) ? Container::Tga : Container::Unknown;
  }

  bool ReadSize(const Container container, const std::uint8_t* const data, const std::size_t bytes, std::uint32_t& outWidth,
                std::uint32_t& outHeight) noexcept
  {
    switch (container) {
    case Container::Png:
      if (!PngHeaderFirst(data, bytes)) {
        return false;
      }
      outWidth = Be32(data + 16);
      outHeight = Be32(data + 20);
      return true;
    case Container::Bmp:
      if (bytes < 26U) {
        return false;
      }
      outWidth = Le32(data + 18);
      outHeight = Magnitude(static_cast<std::int32_t>(Le32(data + 22)));
      return true;
    case Container::Tga:
      outWidth = Le16(data + 12);
      outHeight = Le16(data + 14);
      return true;
    case Container::Jpg: {
      // The first start-of-frame marker (C0 to CF but C4, C8 and CC) holds the size.
      std::size_t at = 2U;
      while (at + 9U <= bytes && data[at] == 0xFFU) {
        const std::uint32_t marker = data[at + 1];
        const std::uint32_t length = (static_cast<std::uint32_t>(data[at + 2]) << 8U) | data[at + 3];
        if (marker >= 0xC0U && marker <= 0xCFU && marker != 0xC4U && marker != 0xC8U && marker != 0xCCU) {
          outHeight = (static_cast<std::uint32_t>(data[at + 5]) << 8U) | data[at + 6];
          outWidth = (static_cast<std::uint32_t>(data[at + 7]) << 8U) | data[at + 8];
          return true;
        }
        at += 2U + length;
      }
      return false;
    }
    default:
      return false;
    }
  }

  bool Decode(const Container container, const std::uint8_t* const data, const std::size_t bytes, Pixels& outPixels)
  {
    switch (container) {
    case Container::Png:
      return DecodePng(data, bytes, outPixels);
    case Container::Tga:
      return DecodeTga(data, bytes, outPixels);
    case Container::Bmp:
      return DecodeBmp(data, bytes, outPixels);
    default:
      return false;
    }
  }

  void WriteBmp(const Pixels& pixels, std::vector<std::uint8_t>& outBytes)
  {
    const std::uint32_t imageBytes = pixels.width * pixels.height * 4U;
    outBytes.clear();
    outBytes.reserve(54U + imageBytes);
    outBytes.push_back('B');
    outBytes.push_back('M');
    PutLe(outBytes, 54U + imageBytes, 4);
    PutLe(outBytes, 0U, 4);
    PutLe(outBytes, 54U, 4);
    PutLe(outBytes, 40U, 4);
    PutLe(outBytes, pixels.width, 4);
    PutLe(outBytes, static_cast<std::uint32_t>(-static_cast<std::int32_t>(pixels.height)), 4); // top-down
    PutLe(outBytes, 1U, 2);
    PutLe(outBytes, 32U, 2);
    PutLe(outBytes, 0U, 4); // BI_RGB
    PutLe(outBytes, imageBytes, 4);
    for (int field = 0; field < 4; ++field) {
      PutLe(outBytes, 0U, 4);
    }
    for (std::size_t texel = 0U; texel < static_cast<std::size_t>(pixels.width) * pixels.height; ++texel) {
      const std::uint8_t* const rgba = pixels.rgba.data() + texel * 4U;
      outBytes.insert(outBytes.end(), {rgba[2], rgba[1], rgba[0], rgba[3]});
    }
  }

  bool WritePng(const Pixels& pixels, std::vector<std::uint8_t>& outBytes)
  {
    const std::size_t rowBytes = static_cast<std::size_t>(pixels.width) * 4U;
    std::vector<std::uint8_t> raw((rowBytes + 1U) * pixels.height, 0U);
    for (std::uint32_t y = 0U; y < pixels.height; ++y) {
      std::memcpy(raw.data() + y * (rowBytes + 1U) + 1U, pixels.rgba.data() + y * rowBytes, rowBytes);
    }
    uLongf compressedBytes = compressBound(static_cast<uLong>(raw.size()));
    std::vector<std::uint8_t> compressed(compressedBytes);
    if (compress2(compressed.data(), &compressedBytes, raw.data(), static_cast<uLong>(raw.size()), 6) != Z_OK) {
      return false;
    }
    compressed.resize(compressedBytes);
    outBytes.assign(kPngSignature, kPngSignature + 8);
    const auto chunk = [&](const char* const type, const std::uint8_t* const body, const std::size_t length) {
      PutBe32(outBytes, static_cast<std::uint32_t>(length));
      const std::size_t start = outBytes.size();
      outBytes.insert(outBytes.end(), type, type + 4);
      if (length != 0U) {
        outBytes.insert(outBytes.end(), body, body + length);
      }
      PutBe32(outBytes, static_cast<std::uint32_t>(crc32(0L, outBytes.data() + start, static_cast<uInt>(length + 4U))));
    };
    std::vector<std::uint8_t> header;
    PutBe32(header, pixels.width);
    PutBe32(header, pixels.height);
    header.insert(header.end(), {8U, 6U, 0U, 0U, 0U});
    chunk("IHDR", header.data(), header.size());
    chunk("IDAT", compressed.data(), compressed.size());
    chunk("IEND", nullptr, 0U);
    return true;
  }
} // namespace gpg::gal::imaging::files
