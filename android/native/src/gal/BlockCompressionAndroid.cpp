// BC1, BC2 and BC3 on the CPU for the Android client build: see BlockCompressionAndroid.h.

#include "BlockCompressionAndroid.h"

#include <algorithm>
#include <cfloat>
#include <cstring>

// The encoder's results depend on the order of its float operations: no fused multiply-adds, so
// arm64 computes what x86-64 does (tests/imagecodec runs on x86-64).
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace gpg::gal::imaging
{
  namespace
  {
    enum class Bc
    {
      Bc1,
      Bc2,
      Bc3,
    };

    [[nodiscard]] Bc KindOf(const DXGI_FORMAT format) noexcept
    {
      switch (format) {
      case DXGI_FORMAT_BC2_TYPELESS:
      case DXGI_FORMAT_BC2_UNORM:
      case DXGI_FORMAT_BC2_UNORM_SRGB:
        return Bc::Bc2;
      case DXGI_FORMAT_BC3_TYPELESS:
      case DXGI_FORMAT_BC3_UNORM:
      case DXGI_FORMAT_BC3_UNORM_SRGB:
        return Bc::Bc3;
      default:
        return Bc::Bc1;
      }
    }

    [[nodiscard]] std::uint16_t Read16(const std::uint8_t* const at) noexcept
    {
      return static_cast<std::uint16_t>(at[0] | (at[1] << 8));
    }

    [[nodiscard]] std::uint32_t Read32(const std::uint8_t* const at) noexcept
    {
      return static_cast<std::uint32_t>(at[0]) | (static_cast<std::uint32_t>(at[1]) << 8) | (static_cast<std::uint32_t>(at[2]) << 16) |
        (static_cast<std::uint32_t>(at[3]) << 24);
    }

    // ---- Decode ------------------------------------------------------------------------------
    // Direct3D's rules (the D3D11 functional specification, BC1 to BC3) with WARP's arithmetic,
    // which tests/imagecodec matches texel for texel: the endpoints expand from 5 and 6 bits to 8
    // by repeating their top bits; the colours between are weighted in 256ths and truncated, the
    // alphas between are the exact means rounded to the nearest. A BC1 block whose first
    // endpoint is not greater than the second has three colours and transparent black; inside
    // BC2 and BC3 the colour block always has four colours.

    [[nodiscard]] std::uint32_t Expand5(const std::uint32_t value) noexcept
    {
      return (value << 3U) | (value >> 2U);
    }

    [[nodiscard]] std::uint32_t Expand6(const std::uint32_t value) noexcept
    {
      return (value << 2U) | (value >> 4U);
    }

    /** The 16 colours of a colour block as R8G8B8A8, texel i at rgba[i * 4]. */
    void DecodeColorBlock(const std::uint8_t* const block, const bool allowThreeColors, std::uint8_t* const rgba) noexcept
    {
      const std::uint32_t c0 = Read16(block);
      const std::uint32_t c1 = Read16(block + 2);
      std::uint8_t palette[4][4];
      const std::uint32_t r0 = Expand5(c0 >> 11U), g0 = Expand6((c0 >> 5U) & 63U), b0 = Expand5(c0 & 31U);
      const std::uint32_t r1 = Expand5(c1 >> 11U), g1 = Expand6((c1 >> 5U) & 63U), b1 = Expand5(c1 & 31U);
      const auto set = [&](const int index, const std::uint32_t r, const std::uint32_t g, const std::uint32_t b, const std::uint32_t a) {
        palette[index][0] = static_cast<std::uint8_t>(r);
        palette[index][1] = static_cast<std::uint8_t>(g);
        palette[index][2] = static_cast<std::uint8_t>(b);
        palette[index][3] = static_cast<std::uint8_t>(a);
      };
      set(0, r0, g0, b0, 255U);
      set(1, r1, g1, b1, 255U);
      // Two thirds and one third are 171 and 85 of 256; a half is the truncated mean.
      const auto mix = [](const std::uint32_t near, const std::uint32_t far) { return (near * 171U + far * 85U) >> 8U; };
      if (c0 > c1 || !allowThreeColors) {
        set(2, mix(r0, r1), mix(g0, g1), mix(b0, b1), 255U);
        set(3, mix(r1, r0), mix(g1, g0), mix(b1, b0), 255U);
      } else {
        set(2, (r0 + r1) / 2U, (g0 + g1) / 2U, (b0 + b1) / 2U, 255U);
        set(3, 0U, 0U, 0U, 0U);
      }
      const std::uint32_t indices = Read32(block + 4);
      for (std::uint32_t texel = 0U; texel < 16U; ++texel) {
        std::memcpy(rgba + texel * 4U, palette[(indices >> (texel * 2U)) & 3U], 4U);
      }
    }

    /** BC3's alpha block into the alpha bytes of 16 R8G8B8A8 texels. */
    void DecodeAlphaBlock(const std::uint8_t* const block, std::uint8_t* const rgba) noexcept
    {
      const std::uint32_t a0 = block[0];
      const std::uint32_t a1 = block[1];
      std::uint32_t palette[8] = {a0, a1};
      if (a0 > a1) {
        for (std::uint32_t step = 1U; step < 7U; ++step) {
          palette[step + 1U] = ((7U - step) * a0 + step * a1 + 3U) / 7U;
        }
      } else {
        for (std::uint32_t step = 1U; step < 5U; ++step) {
          palette[step + 1U] = ((5U - step) * a0 + step * a1 + 2U) / 5U;
        }
        palette[6] = 0U;
        palette[7] = 255U;
      }
      std::uint64_t indices = 0U;
      for (int byte = 7; byte >= 2; --byte) {
        indices = (indices << 8U) | block[byte];
      }
      for (std::uint32_t texel = 0U; texel < 16U; ++texel) {
        rgba[texel * 4U + 3U] = static_cast<std::uint8_t>(palette[(indices >> (texel * 3U)) & 7U]);
      }
    }

    void DecodeBlock(const Bc kind, const std::uint8_t* const block, std::uint8_t* const rgba) noexcept
    {
      if (kind == Bc::Bc1) {
        DecodeColorBlock(block, true, rgba);
        return;
      }
      DecodeColorBlock(block + 8, false, rgba);
      if (kind == Bc::Bc2) {
        for (std::uint32_t texel = 0U; texel < 16U; ++texel) {
          const std::uint32_t nibble = (block[texel / 2U] >> ((texel & 1U) * 4U)) & 15U;
          rgba[texel * 4U + 3U] = static_cast<std::uint8_t>(nibble * 17U);
        }
      } else {
        DecodeAlphaBlock(block, rgba);
      }
    }

    // ---- Encode ------------------------------------------------------------------------------
    // D3DX's method as Microsoft published it with DirectXTex (BC.cpp, whose encoder comes from
    // D3DX): texels as floats, BC1 endpoints from a min/max box, the diagonal that fits best and
    // eight Newton steps on the squared error, perceptual weights (the Rec. 709 luminances), BC3
    // alpha the same way in one dimension. No dithering. D3DX11's own arithmetic differs in places
    // we cannot see, so a block can come out different in an endpoint's last bit or an index.

    struct Color
    {
      float r = 0.0f;
      float g = 0.0f;
      float b = 0.0f;
      float a = 0.0f;
    };

    constexpr Color kLuminance{0.2125f / 0.7154f, 1.0f, 0.0721f / 0.7154f, 1.0f};
    constexpr Color kLuminanceInverse{0.7154f / 0.2125f, 1.0f, 0.7154f / 0.0721f, 1.0f};

    [[nodiscard]] Color Lerp(const Color& from, const Color& to, const float t) noexcept
    {
      return Color{from.r + t * (to.r - from.r), from.g + t * (to.g - from.g), from.b + t * (to.b - from.b), from.a + t * (to.a - from.a)};
    }

    [[nodiscard]] std::uint16_t Encode565(const Color& color) noexcept
    {
      const auto clamp = [](const float value) { return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); };
      return static_cast<std::uint16_t>((static_cast<std::int32_t>(clamp(color.r) * 31.0f + 0.5f) << 11) |
                                        (static_cast<std::int32_t>(clamp(color.g) * 63.0f + 0.5f) << 5) |
                                        static_cast<std::int32_t>(clamp(color.b) * 31.0f + 0.5f));
    }

    [[nodiscard]] Color Decode565(const std::uint16_t value) noexcept
    {
      return Color{static_cast<float>((value >> 11) & 31) * (1.0f / 31.0f), static_cast<float>((value >> 5) & 63) * (1.0f / 63.0f),
                   static_cast<float>(value & 31) * (1.0f / 31.0f), 1.0f};
    }

    /** The two endpoints of the colour axis through `points` (already weighted), D3DX's root finding. */
    void OptimizeRgb(Color& outX, Color& outY, const Color* const points, const std::size_t steps) noexcept
    {
      constexpr float kEpsilon = (0.25f / 64.0f) * (0.25f / 64.0f);
      static const float kC3[] = {2.0f / 2.0f, 1.0f / 2.0f, 0.0f / 2.0f};
      static const float kD3[] = {0.0f / 2.0f, 1.0f / 2.0f, 2.0f / 2.0f};
      static const float kC4[] = {3.0f / 3.0f, 2.0f / 3.0f, 1.0f / 3.0f, 0.0f / 3.0f};
      static const float kD4[] = {0.0f / 3.0f, 1.0f / 3.0f, 2.0f / 3.0f, 3.0f / 3.0f};
      const float* const pC = (steps == 3U) ? kC3 : kC4;
      const float* const pD = (steps == 3U) ? kD3 : kD4;

      Color x = kLuminance;
      Color y{0.0f, 0.0f, 0.0f, 1.0f};
      for (std::size_t point = 0U; point < 16U; ++point) {
        x.r = std::min(x.r, points[point].r);
        x.g = std::min(x.g, points[point].g);
        x.b = std::min(x.b, points[point].b);
        y.r = std::max(y.r, points[point].r);
        y.g = std::max(y.g, points[point].g);
        y.b = std::max(y.b, points[point].b);
      }
      const Color ab{y.r - x.r, y.g - x.g, y.b - x.b, 0.0f};
      const float lengthSquared = ab.r * ab.r + ab.g * ab.g + ab.b * ab.b;
      if (lengthSquared < FLT_MIN) {
        outX = x;
        outY = y;
        return;
      }
      // The diagonal of the box that fits the points best.
      const float inverse = 1.0f / lengthSquared;
      const Color direction{ab.r * inverse, ab.g * inverse, ab.b * inverse, 0.0f};
      const Color mid{(x.r + y.r) * 0.5f, (x.g + y.g) * 0.5f, (x.b + y.b) * 0.5f, 0.0f};
      float fit[4] = {0.0f, 0.0f, 0.0f, 0.0f};
      for (std::size_t point = 0U; point < 16U; ++point) {
        const float pr = (points[point].r - mid.r) * direction.r;
        const float pg = (points[point].g - mid.g) * direction.g;
        const float pb = (points[point].b - mid.b) * direction.b;
        float f = pr + pg + pb;
        fit[0] += f * f;
        f = pr + pg - pb;
        fit[1] += f * f;
        f = pr - pg + pb;
        fit[2] += f * f;
        f = pr - pg - pb;
        fit[3] += f * f;
      }
      std::size_t best = 0U;
      for (std::size_t axis = 1U; axis < 4U; ++axis) {
        if (fit[axis] > fit[best]) {
          best = axis;
        }
      }
      if ((best & 2U) != 0U) {
        std::swap(x.g, y.g);
      }
      if ((best & 1U) != 0U) {
        std::swap(x.b, y.b);
      }
      if (lengthSquared < 1.0f / 4096.0f) {
        outX = x;
        outY = y;
        return;
      }
      // Newton's method on the sum of squared errors.
      const float stepCount = static_cast<float>(steps - 1U);
      for (int iteration = 0; iteration < 8; ++iteration) {
        Color stepColors[4];
        for (std::size_t step = 0U; step < steps; ++step) {
          stepColors[step].r = x.r * pC[step] + y.r * pD[step];
          stepColors[step].g = x.g * pC[step] + y.g * pD[step];
          stepColors[step].b = x.b * pC[step] + y.b * pD[step];
        }
        Color axis{y.r - x.r, y.g - x.g, y.b - x.b, 0.0f};
        const float length = axis.r * axis.r + axis.g * axis.g + axis.b * axis.b;
        if (length < 1.0f / 4096.0f) {
          break;
        }
        const float scale = stepCount / length;
        axis.r *= scale;
        axis.g *= scale;
        axis.b *= scale;
        float d2X = 0.0f, d2Y = 0.0f;
        Color dX, dY;
        for (std::size_t point = 0U; point < 16U; ++point) {
          const float dot =
            (points[point].r - x.r) * axis.r + (points[point].g - x.g) * axis.g + (points[point].b - x.b) * axis.b;
          std::size_t step = 0U;
          if (dot <= 0.0f) {
            step = 0U;
          } else if (dot >= stepCount) {
            step = steps - 1U;
          } else {
            step = static_cast<std::size_t>(dot + 0.5f);
          }
          const Color diff{stepColors[step].r - points[point].r, stepColors[step].g - points[point].g,
                           stepColors[step].b - points[point].b, 0.0f};
          const float fC = pC[step] * (1.0f / 8.0f);
          const float fD = pD[step] * (1.0f / 8.0f);
          d2X += fC * pC[step];
          dX.r += fC * diff.r;
          dX.g += fC * diff.g;
          dX.b += fC * diff.b;
          d2Y += fD * pD[step];
          dY.r += fD * diff.r;
          dY.g += fD * diff.g;
          dY.b += fD * diff.b;
        }
        if (d2X > 0.0f) {
          const float f = -1.0f / d2X;
          x.r += dX.r * f;
          x.g += dX.g * f;
          x.b += dX.b * f;
        }
        if (d2Y > 0.0f) {
          const float f = -1.0f / d2Y;
          y.r += dY.r * f;
          y.g += dY.g * f;
          y.b += dY.b * f;
        }
        if (dX.r * dX.r < kEpsilon && dX.g * dX.g < kEpsilon && dX.b * dX.b < kEpsilon && dY.r * dY.r < kEpsilon &&
            dY.g * dY.g < kEpsilon && dY.b * dY.b < kEpsilon) {
          break;
        }
      }
      outX = x;
      outY = y;
    }

    void Write16(std::uint8_t* const at, const std::uint32_t value) noexcept
    {
      at[0] = static_cast<std::uint8_t>(value);
      at[1] = static_cast<std::uint8_t>(value >> 8U);
    }

    void Write32(std::uint8_t* const at, const std::uint32_t value) noexcept
    {
      Write16(at, value);
      Write16(at + 2, value >> 16U);
    }

    /** A BC1 colour block; `colorKey`: texels with alpha under one half become transparent (3-colour mode). */
    void EncodeColorBlock(const Color* const texels, const bool colorKey, std::uint8_t* const block) noexcept
    {
      constexpr float kAlphaRef = 0.5f;
      std::size_t steps = 4U;
      if (colorKey) {
        std::size_t transparent = 0U;
        for (std::size_t texel = 0U; texel < 16U; ++texel) {
          transparent += texels[texel].a < kAlphaRef ? 1U : 0U;
        }
        if (transparent == 16U) {
          Write16(block, 0x0000U);
          Write16(block + 2, 0xFFFFU);
          Write32(block + 4, 0xFFFFFFFFU);
          return;
        }
        steps = transparent > 0U ? 3U : 4U;
      }
      // Quantised to 5:6:5 first, which makes the colours land on the axis' endpoints more often.
      Color quantized[16];
      for (std::size_t texel = 0U; texel < 16U; ++texel) {
        quantized[texel].r = static_cast<float>(static_cast<std::int32_t>(texels[texel].r * 31.0f + 0.5f)) * (1.0f / 31.0f);
        quantized[texel].g = static_cast<float>(static_cast<std::int32_t>(texels[texel].g * 63.0f + 0.5f)) * (1.0f / 63.0f);
        quantized[texel].b = static_cast<float>(static_cast<std::int32_t>(texels[texel].b * 31.0f + 0.5f)) * (1.0f / 31.0f);
        quantized[texel].a = 1.0f;
        quantized[texel].r *= kLuminance.r;
        quantized[texel].g *= kLuminance.g;
        quantized[texel].b *= kLuminance.b;
      }
      Color colorA, colorB;
      OptimizeRgb(colorA, colorB, quantized, steps);
      const Color endC{colorA.r * kLuminanceInverse.r, colorA.g * kLuminanceInverse.g, colorA.b * kLuminanceInverse.b, 1.0f};
      const Color endD{colorB.r * kLuminanceInverse.r, colorB.g * kLuminanceInverse.g, colorB.b * kLuminanceInverse.b, 1.0f};
      const std::uint16_t wordA = Encode565(endC);
      const std::uint16_t wordB = Encode565(endD);
      if (steps == 4U && wordA == wordB) {
        Write16(block, wordA);
        Write16(block + 2, wordB);
        Write32(block + 4, 0U);
        return;
      }
      const Color decodedC = Decode565(wordA);
      const Color decodedD = Decode565(wordB);
      colorA = Color{decodedC.r * kLuminance.r, decodedC.g * kLuminance.g, decodedC.b * kLuminance.b, 1.0f};
      colorB = Color{decodedD.r * kLuminance.r, decodedD.g * kLuminance.g, decodedD.b * kLuminance.b, 1.0f};
      Color stepColors[4];
      if ((steps == 3U) == (wordA <= wordB)) {
        Write16(block, wordA);
        Write16(block + 2, wordB);
        stepColors[0] = colorA;
        stepColors[1] = colorB;
      } else {
        Write16(block, wordB);
        Write16(block + 2, wordA);
        stepColors[0] = colorB;
        stepColors[1] = colorA;
      }
      static const std::uint32_t kSteps3[] = {0U, 2U, 1U};
      static const std::uint32_t kSteps4[] = {0U, 2U, 3U, 1U};
      const std::uint32_t* const order = (steps == 3U) ? kSteps3 : kSteps4;
      if (steps == 3U) {
        stepColors[2] = Lerp(stepColors[0], stepColors[1], 0.5f);
      } else {
        stepColors[2] = Lerp(stepColors[0], stepColors[1], 1.0f / 3.0f);
        stepColors[3] = Lerp(stepColors[0], stepColors[1], 2.0f / 3.0f);
      }
      Color axis{stepColors[1].r - stepColors[0].r, stepColors[1].g - stepColors[0].g, stepColors[1].b - stepColors[0].b, 0.0f};
      const float stepCount = static_cast<float>(steps - 1U);
      const float scale = (wordA != wordB) ? (stepCount / (axis.r * axis.r + axis.g * axis.g + axis.b * axis.b)) : 0.0f;
      axis.r *= scale;
      axis.g *= scale;
      axis.b *= scale;
      std::uint32_t bits = 0U;
      for (std::size_t texel = 0U; texel < 16U; ++texel) {
        if (steps == 3U && texels[texel].a < kAlphaRef) {
          bits = (3U << 30U) | (bits >> 2U);
          continue;
        }
        const float r = texels[texel].r * kLuminance.r;
        const float g = texels[texel].g * kLuminance.g;
        const float b = texels[texel].b * kLuminance.b;
        const float dot = (r - stepColors[0].r) * axis.r + (g - stepColors[0].g) * axis.g + (b - stepColors[0].b) * axis.b;
        std::uint32_t step = 0U;
        if (dot <= 0.0f) {
          step = 0U;
        } else if (dot >= stepCount) {
          step = 1U;
        } else {
          step = order[static_cast<std::size_t>(dot + 0.5f)];
        }
        bits = (step << 30U) | (bits >> 2U);
      }
      Write32(block + 4, bits);
    }

    /** BC3's alpha endpoints, D3DX's one-dimensional root finding. */
    void OptimizeAlpha(float& outX, float& outY, const float* const points, const std::size_t steps) noexcept
    {
      static const float kC6[] = {5.0f / 5.0f, 4.0f / 5.0f, 3.0f / 5.0f, 2.0f / 5.0f, 1.0f / 5.0f, 0.0f / 5.0f};
      static const float kD6[] = {0.0f / 5.0f, 1.0f / 5.0f, 2.0f / 5.0f, 3.0f / 5.0f, 4.0f / 5.0f, 5.0f / 5.0f};
      static const float kC8[] = {7.0f / 7.0f, 6.0f / 7.0f, 5.0f / 7.0f, 4.0f / 7.0f, 3.0f / 7.0f, 2.0f / 7.0f, 1.0f / 7.0f, 0.0f / 7.0f};
      static const float kD8[] = {0.0f / 7.0f, 1.0f / 7.0f, 2.0f / 7.0f, 3.0f / 7.0f, 4.0f / 7.0f, 5.0f / 7.0f, 6.0f / 7.0f, 7.0f / 7.0f};
      const float* const pC = (steps == 6U) ? kC6 : kC8;
      const float* const pD = (steps == 6U) ? kD6 : kD8;
      float x = 1.0f;
      float y = 0.0f;
      if (steps == 8U) {
        for (std::size_t point = 0U; point < 16U; ++point) {
          x = std::min(x, points[point]);
          y = std::max(y, points[point]);
        }
      } else {
        // The 6-step mode has 0 and 1 of its own: the endpoints span the values between.
        for (std::size_t point = 0U; point < 16U; ++point) {
          if (points[point] < x && points[point] > 0.0f) {
            x = points[point];
          }
          if (points[point] > y && points[point] < 1.0f) {
            y = points[point];
          }
        }
        if (x == y) {
          y = 1.0f;
        }
      }
      const float stepCount = static_cast<float>(steps - 1U);
      for (int iteration = 0; iteration < 8; ++iteration) {
        if ((y - x) < (1.0f / 256.0f)) {
          break;
        }
        const float scale = stepCount / (y - x);
        float stepValues[8];
        for (std::size_t step = 0U; step < steps; ++step) {
          stepValues[step] = pC[step] * x + pD[step] * y;
        }
        if (steps == 6U) {
          stepValues[6] = 0.0f;
          stepValues[7] = 1.0f;
        }
        float dX = 0.0f, dY = 0.0f, d2X = 0.0f, d2Y = 0.0f;
        for (std::size_t point = 0U; point < 16U; ++point) {
          const float dot = (points[point] - x) * scale;
          std::size_t step = 0U;
          if (dot <= 0.0f) {
            step = (steps == 6U && points[point] <= x * 0.5f) ? 6U : 0U;
          } else if (dot >= stepCount) {
            step = (steps == 6U && points[point] >= (y + 1.0f) * 0.5f) ? 7U : (steps - 1U);
          } else {
            step = static_cast<std::size_t>(dot + 0.5f);
          }
          if (step < steps) {
            // D3DX's sign, which DirectXTex turned around: half of the blocks that differed from
            // D3DX11's agree with it this way.
            const float diff = points[point] - stepValues[step];
            dX += pC[step] * diff;
            d2X += pC[step] * pC[step];
            dY += pD[step] * diff;
            d2Y += pD[step] * pD[step];
          }
        }
        if (d2X > 0.0f) {
          x -= dX / d2X;
        }
        if (d2Y > 0.0f) {
          y -= dY / d2Y;
        }
        if (x > y) {
          std::swap(x, y);
        }
        if (dX * dX < (1.0f / 64.0f) && dY * dY < (1.0f / 64.0f)) {
          break;
        }
      }
      outX = std::clamp(x, 0.0f, 1.0f);
      outY = std::clamp(y, 0.0f, 1.0f);
    }

    void EncodeAlphaBlock(const Color* const texels, std::uint8_t* const block) noexcept
    {
      float alpha[16];
      float minAlpha = 1.0f;
      float maxAlpha = 0.0f;
      for (std::size_t texel = 0U; texel < 16U; ++texel) {
        alpha[texel] = static_cast<float>(static_cast<std::int32_t>(texels[texel].a * 255.0f + 0.5f)) * (1.0f / 255.0f);
        minAlpha = std::min(minAlpha, alpha[texel]);
        maxAlpha = std::max(maxAlpha, alpha[texel]);
      }
      if (minAlpha == 1.0f) {
        block[0] = 0xFFU;
        block[1] = 0xFFU;
        std::memset(block + 2, 0, 6U);
        return;
      }
      const std::size_t steps = (minAlpha == 0.0f || maxAlpha == 1.0f) ? 6U : 8U;
      float alphaA = 0.0f, alphaB = 0.0f;
      OptimizeAlpha(alphaA, alphaB, alpha, steps);
      const auto byteA = static_cast<std::uint8_t>(static_cast<std::int32_t>(alphaA * 255.0f + 0.5f));
      const auto byteB = static_cast<std::uint8_t>(static_cast<std::int32_t>(alphaB * 255.0f + 0.5f));
      alphaA = static_cast<float>(byteA) * (1.0f / 255.0f);
      alphaB = static_cast<float>(byteB) * (1.0f / 255.0f);
      if (steps == 8U && byteA == byteB) {
        block[0] = byteA;
        block[1] = byteB;
        std::memset(block + 2, 0, 6U);
        return;
      }
      static const std::uint32_t kSteps6[] = {0U, 2U, 3U, 4U, 5U, 1U};
      static const std::uint32_t kSteps8[] = {0U, 2U, 3U, 4U, 5U, 6U, 7U, 1U};
      const std::uint32_t* order = nullptr;
      float stepValues[8];
      if (steps == 6U) {
        block[0] = byteA;
        block[1] = byteB;
        stepValues[0] = alphaA;
        stepValues[1] = alphaB;
        for (std::uint32_t step = 1U; step < 5U; ++step) {
          stepValues[step + 1U] = (stepValues[0] * static_cast<float>(5U - step) + stepValues[1] * static_cast<float>(step)) * (1.0f / 5.0f);
        }
        stepValues[6] = 0.0f;
        stepValues[7] = 1.0f;
        order = kSteps6;
      } else {
        block[0] = byteB;
        block[1] = byteA;
        stepValues[0] = alphaB;
        stepValues[1] = alphaA;
        for (std::uint32_t step = 1U; step < 7U; ++step) {
          stepValues[step + 1U] = (stepValues[0] * static_cast<float>(7U - step) + stepValues[1] * static_cast<float>(step)) * (1.0f / 7.0f);
        }
        order = kSteps8;
      }
      const float stepCount = static_cast<float>(steps - 1U);
      const float scale = (stepValues[0] != stepValues[1]) ? (stepCount / (stepValues[1] - stepValues[0])) : 0.0f;
      for (std::size_t half = 0U; half < 2U; ++half) {
        std::uint32_t bits = 0U;
        for (std::size_t texel = half * 8U; texel < half * 8U + 8U; ++texel) {
          const float value = texels[texel].a;
          const float dot = (value - stepValues[0]) * scale;
          std::uint32_t step = 0U;
          if (dot <= 0.0f) {
            step = (steps == 6U && value <= stepValues[0] * 0.5f) ? 6U : 0U;
          } else if (dot >= stepCount) {
            step = (steps == 6U && value >= (stepValues[1] + 1.0f) * 0.5f) ? 7U : 1U;
          } else {
            step = order[static_cast<std::size_t>(dot + 0.5f)];
          }
          bits = (step << 21U) | (bits >> 3U);
        }
        block[2 + half * 3U] = static_cast<std::uint8_t>(bits);
        block[3 + half * 3U] = static_cast<std::uint8_t>(bits >> 8U);
        block[4 + half * 3U] = static_cast<std::uint8_t>(bits >> 16U);
      }
    }

    void EncodeBlock(const Bc kind, const Color* const texels, std::uint8_t* const block) noexcept
    {
      if (kind == Bc::Bc1) {
        EncodeColorBlock(texels, true, block);
        return;
      }
      if (kind == Bc::Bc2) {
        std::uint32_t words[2] = {0U, 0U};
        for (std::size_t texel = 0U; texel < 16U; ++texel) {
          const auto nibble = static_cast<std::uint32_t>(static_cast<std::int32_t>(texels[texel].a * 15.0f + 0.5f));
          words[texel >> 3U] = (words[texel >> 3U] >> 4U) | (nibble << 28U);
        }
        Write32(block, words[0]);
        Write32(block + 4, words[1]);
      } else {
        EncodeAlphaBlock(texels, block);
      }
      EncodeColorBlock(texels, false, block + 8);
    }
  } // namespace

  bool IsBc123(const DXGI_FORMAT format) noexcept
  {
    return format >= DXGI_FORMAT_BC1_TYPELESS && format <= DXGI_FORMAT_BC3_UNORM_SRGB;
  }

  void DecodeBcLevel(
    const DXGI_FORMAT format, const void* const blocks, const std::size_t rowPitch, const std::uint32_t width, const std::uint32_t height,
    std::uint8_t* const rgba
  )
  {
    const Bc kind = KindOf(format);
    const std::size_t blockBytes = (kind == Bc::Bc1) ? 8U : 16U;
    std::uint8_t texels[16 * 4];
    for (std::uint32_t blockY = 0U; blockY * 4U < height; ++blockY) {
      const auto* const row = static_cast<const std::uint8_t*>(blocks) + blockY * rowPitch;
      for (std::uint32_t blockX = 0U; blockX * 4U < width; ++blockX) {
        DecodeBlock(kind, row + blockX * blockBytes, texels);
        for (std::uint32_t y = 0U; y < 4U && blockY * 4U + y < height; ++y) {
          const std::uint32_t columns = std::min(4U, width - blockX * 4U);
          std::memcpy(rgba + (static_cast<std::size_t>(blockY * 4U + y) * width + blockX * 4U) * 4U, texels + y * 16U, columns * 4U);
        }
      }
    }
  }

  void EncodeBcLevel(
    const DXGI_FORMAT format, const std::uint8_t* const rgba, const std::uint32_t width, const std::uint32_t height, void* const blocks,
    const std::size_t rowPitch
  )
  {
    const Bc kind = KindOf(format);
    const std::size_t blockBytes = (kind == Bc::Bc1) ? 8U : 16U;
    // Texels beyond a partial block repeat the block's own: columns 0 1 0 1 for two, 0 1 2 1 for three.
    static const std::uint32_t kRepeat[4][4] = {{0U, 0U, 0U, 0U}, {0U, 0U, 0U, 0U}, {0U, 1U, 0U, 1U}, {0U, 1U, 2U, 1U}};
    Color texels[16];
    for (std::uint32_t blockY = 0U; blockY * 4U < height; ++blockY) {
      auto* const row = static_cast<std::uint8_t*>(blocks) + blockY * rowPitch;
      const std::uint32_t rows = std::min(4U, height - blockY * 4U);
      for (std::uint32_t blockX = 0U; blockX * 4U < width; ++blockX) {
        const std::uint32_t columns = std::min(4U, width - blockX * 4U);
        for (std::uint32_t y = 0U; y < 4U; ++y) {
          const std::uint32_t sourceY = blockY * 4U + (rows == 4U ? y : kRepeat[rows][y]);
          for (std::uint32_t x = 0U; x < 4U; ++x) {
            const std::uint32_t sourceX = blockX * 4U + (columns == 4U ? x : kRepeat[columns][x]);
            const std::uint8_t* const texel = rgba + (static_cast<std::size_t>(sourceY) * width + sourceX) * 4U;
            texels[y * 4U + x] = Color{static_cast<float>(texel[0]) * (1.0f / 255.0f), static_cast<float>(texel[1]) * (1.0f / 255.0f),
                                       static_cast<float>(texel[2]) * (1.0f / 255.0f), static_cast<float>(texel[3]) * (1.0f / 255.0f)};
          }
        }
        EncodeBlock(kind, texels, row + blockX * blockBytes);
      }
    }
  }
} // namespace gpg::gal::imaging
