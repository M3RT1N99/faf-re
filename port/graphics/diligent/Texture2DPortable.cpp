#include "Texture2DPortable.h"

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <limits>

// stb_image from Diligent's ThirdParty (dependencies/DiligentCore/ThirdParty/stb), compiled into this
// TU only (STB_IMAGE_STATIC keeps its symbols local, so no other copy can clash).
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_GIF
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4100 4244 4456 4457 4505 4701 4703)
#endif
#include "dependencies/DiligentCore/ThirdParty/stb/stb_image.h" // the repo root is on the include path (port_graphics.props)
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// How D3DX turns an image into DXT5 (DeviceD3D9::GetTexture2D's D3DXLoadSurfaceFromSurface, and the
// load of a DXT file whose size is not in whole blocks): the source is converted to four floats per
// texel, texels outside the source are transparent black (D3DX_FILTER_NONE), and each 4x4 block is
// encoded with D3DX's BC3 codec. The codec below follows the one Microsoft published from D3DX
// (DirectXTex BC.cpp, "D3DXEncodeBC3", whose own comments name the places where DirectXTex later
// deviates from D3DX; the D3DX behaviour is kept here). Floats are 32-bit throughout, as D3DX's.

namespace gpg::gal::diligent
{
    namespace
    {
        constexpr std::uint32_t FourCC(const char a, const char b, const char c, const char d)
        {
            return static_cast<std::uint32_t>(static_cast<std::uint8_t>(a)) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b)) << 8U) |
                   (static_cast<std::uint32_t>(static_cast<std::uint8_t>(c)) << 16U) |
                   (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d)) << 24U);
        }

        std::uint32_t ReadU32(const std::uint8_t* const p)
        {
            return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8U) | (static_cast<std::uint32_t>(p[2]) << 16U) |
                   (static_cast<std::uint32_t>(p[3]) << 24U);
        }

        std::uint16_t ReadU16(const std::uint8_t* const p)
        {
            return static_cast<std::uint16_t>(p[0] | (p[1] << 8U));
        }

        // DDS_PIXELFORMAT flags (the DDS reference).
        constexpr std::uint32_t kDdpfAlphaPixels = 0x1, kDdpfAlpha = 0x2, kDdpfFourCC = 0x4, kDdpfRgb = 0x40, kDdpfLuminance = 0x20000;

        struct Color4
        {
            float r = 0.0F, g = 0.0F, b = 0.0F, a = 0.0F;
        };

        /** The source in D3DX's working format: four floats per texel, 0..1. */
        struct FloatImage
        {
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            std::vector<Color4> texels;
        };

        // ---- D3DX's BC decoding (floats) -----------------------------------------------------------

        Color4 Decode565(const std::uint16_t w565)
        {
            Color4 color;
            color.r = static_cast<float>((w565 >> 11) & 31) * (1.0F / 31.0F);
            color.g = static_cast<float>((w565 >> 5) & 63) * (1.0F / 63.0F);
            color.b = static_cast<float>((w565 >> 0) & 31) * (1.0F / 31.0F);
            color.a = 1.0F;
            return color;
        }

        Color4 Lerp(const Color4& a, const Color4& b, const float s)
        {
            Color4 out;
            out.r = a.r + s * (b.r - a.r);
            out.g = a.g + s * (b.g - a.g);
            out.b = a.b + s * (b.b - a.b);
            out.a = a.a + s * (b.a - a.a);
            return out;
        }

        /** D3DXDecodeBC1's colour half; `isBC1` allows the 3-colour mode with transparent black. */
        void DecodeColorFloat(const std::uint8_t* const block, Color4 out[16], const bool isBC1)
        {
            const std::uint16_t w0 = ReadU16(block);
            const std::uint16_t w1 = ReadU16(block + 2);
            Color4 colors[4];
            colors[0] = Decode565(w0);
            colors[1] = Decode565(w1);
            if (isBC1 && w0 <= w1) {
                colors[2] = Lerp(colors[0], colors[1], 0.5F);
                colors[3] = Color4{0.0F, 0.0F, 0.0F, 0.0F};
            } else {
                colors[2] = Lerp(colors[0], colors[1], 1.0F / 3.0F);
                colors[3] = Lerp(colors[0], colors[1], 2.0F / 3.0F);
            }
            std::uint32_t bits = ReadU32(block + 4);
            for (int texel = 0; texel < 16; ++texel, bits >>= 2) {
                out[texel] = colors[bits & 3U];
            }
        }

        void DecodeBC2Float(const std::uint8_t* const block, Color4 out[16])
        {
            DecodeColorFloat(block + 8, out, false);
            std::uint32_t low = ReadU32(block);
            std::uint32_t high = ReadU32(block + 4);
            for (int texel = 0; texel < 8; ++texel, low >>= 4) {
                out[texel].a = static_cast<float>(low & 0xF) * (1.0F / 15.0F);
            }
            for (int texel = 8; texel < 16; ++texel, high >>= 4) {
                out[texel].a = static_cast<float>(high & 0xF) * (1.0F / 15.0F);
            }
        }

        void DecodeBC3Float(const std::uint8_t* const block, Color4 out[16])
        {
            DecodeColorFloat(block + 8, out, false);
            float alpha[8];
            alpha[0] = static_cast<float>(block[0]) * (1.0F / 255.0F);
            alpha[1] = static_cast<float>(block[1]) * (1.0F / 255.0F);
            if (block[0] > block[1]) {
                for (int i = 1; i < 7; ++i) {
                    alpha[i + 1] = (alpha[0] * static_cast<float>(7 - i) + alpha[1] * static_cast<float>(i)) * (1.0F / 7.0F);
                }
            } else {
                for (int i = 1; i < 5; ++i) {
                    alpha[i + 1] = (alpha[0] * static_cast<float>(5 - i) + alpha[1] * static_cast<float>(i)) * (1.0F / 5.0F);
                }
                alpha[6] = 0.0F;
                alpha[7] = 1.0F;
            }
            std::uint32_t low = static_cast<std::uint32_t>(block[2]) | (static_cast<std::uint32_t>(block[3]) << 8U) |
                                (static_cast<std::uint32_t>(block[4]) << 16U);
            std::uint32_t high = static_cast<std::uint32_t>(block[5]) | (static_cast<std::uint32_t>(block[6]) << 8U) |
                                 (static_cast<std::uint32_t>(block[7]) << 16U);
            for (int texel = 0; texel < 8; ++texel, low >>= 3) {
                out[texel].a = alpha[low & 7U];
            }
            for (int texel = 8; texel < 16; ++texel, high >>= 3) {
                out[texel].a = alpha[high & 7U];
            }
        }

        // ---- D3DX's BC encoding ---------------------------------------------------------------------

        // Perceptual channel weights (D3DX's default; DirectXTex keeps them unless asked for uniform).
        constexpr Color4 kLuminance{0.2125F / 0.7154F, 1.0F, 0.0721F / 0.7154F, 1.0F};
        constexpr Color4 kLuminanceInv{0.7154F / 0.2125F, 1.0F, 0.7154F / 0.0721F, 1.0F};

        std::uint16_t Encode565(const Color4& color)
        {
            const float r = color.r < 0.0F ? 0.0F : color.r > 1.0F ? 1.0F : color.r;
            const float g = color.g < 0.0F ? 0.0F : color.g > 1.0F ? 1.0F : color.g;
            const float b = color.b < 0.0F ? 0.0F : color.b > 1.0F ? 1.0F : color.b;
            return static_cast<std::uint16_t>((static_cast<std::int32_t>(r * 31.0F + 0.5F) << 11) | (static_cast<std::int32_t>(g * 63.0F + 0.5F) << 5) |
                                              (static_cast<std::int32_t>(b * 31.0F + 0.5F) << 0));
        }

        void OptimizeRGB(Color4* const outX, Color4* const outY, const Color4 points[16], const std::size_t steps)
        {
            static const float kEpsilon = (0.25F / 64.0F) * (0.25F / 64.0F);
            static const float kC3[] = {2.0F / 2.0F, 1.0F / 2.0F, 0.0F / 2.0F};
            static const float kD3[] = {0.0F / 2.0F, 1.0F / 2.0F, 2.0F / 2.0F};
            static const float kC4[] = {3.0F / 3.0F, 2.0F / 3.0F, 1.0F / 3.0F, 0.0F / 3.0F};
            static const float kD4[] = {0.0F / 3.0F, 1.0F / 3.0F, 2.0F / 3.0F, 3.0F / 3.0F};
            const float* const pC = (steps == 3) ? kC3 : kC4;
            const float* const pD = (steps == 3) ? kD3 : kD4;

            // Min and max points as the start.
            Color4 x = kLuminance;
            Color4 y{0.0F, 0.0F, 0.0F, 1.0F};
            for (std::size_t point = 0; point < 16; ++point) {
                x.r = std::min(x.r, points[point].r);
                x.g = std::min(x.g, points[point].g);
                x.b = std::min(x.b, points[point].b);
                y.r = std::max(y.r, points[point].r);
                y.g = std::max(y.g, points[point].g);
                y.b = std::max(y.b, points[point].b);
            }

            // The diagonal axis.
            Color4 ab{y.r - x.r, y.g - x.g, y.b - x.b, 0.0F};
            const float lengthAB = ab.r * ab.r + ab.g * ab.g + ab.b * ab.b;
            if (lengthAB < FLT_MIN) { // one colour
                *outX = x;
                *outY = y;
                return;
            }

            // Which of the four diagonals fits the data best.
            const float inverseAB = 1.0F / lengthAB;
            Color4 direction{ab.r * inverseAB, ab.g * inverseAB, ab.b * inverseAB, 0.0F};
            const Color4 mid{(x.r + y.r) * 0.5F, (x.g + y.g) * 0.5F, (x.b + y.b) * 0.5F, 0.0F};
            float fit[4] = {0.0F, 0.0F, 0.0F, 0.0F};
            for (std::size_t point = 0; point < 16; ++point) {
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
            float fitMax = fit[0];
            std::size_t best = 0;
            for (std::size_t index = 1; index < 4; ++index) {
                if (fit[index] > fitMax) {
                    fitMax = fit[index];
                    best = index;
                }
            }
            if ((best & 2) != 0) {
                std::swap(x.g, y.g);
            }
            if ((best & 1) != 0) {
                std::swap(x.b, y.b);
            }

            if (lengthAB < 1.0F / 4096.0F) { // two colours
                *outX = x;
                *outY = y;
                return;
            }

            // Newton's method on the sum of squared errors.
            const float stepsF = static_cast<float>(steps - 1);
            for (int iteration = 0; iteration < 8; ++iteration) {
                Color4 palette[4];
                for (std::size_t step = 0; step < steps; ++step) {
                    palette[step].r = x.r * pC[step] + y.r * pD[step];
                    palette[step].g = x.g * pC[step] + y.g * pD[step];
                    palette[step].b = x.b * pC[step] + y.b * pD[step];
                }
                direction = Color4{y.r - x.r, y.g - x.g, y.b - x.b, 0.0F};
                const float length = direction.r * direction.r + direction.g * direction.g + direction.b * direction.b;
                if (length < (1.0F / 4096.0F)) {
                    break;
                }
                const float scale = stepsF / length;
                direction.r *= scale;
                direction.g *= scale;
                direction.b *= scale;

                float d2X = 0.0F, d2Y = 0.0F;
                Color4 dX, dY;
                for (std::size_t point = 0; point < 16; ++point) {
                    const float dot = (points[point].r - x.r) * direction.r + (points[point].g - x.g) * direction.g +
                                      (points[point].b - x.b) * direction.b;
                    std::size_t step;
                    if (dot <= 0.0F) {
                        step = 0;
                    } else if (dot >= stepsF) {
                        step = steps - 1;
                    } else {
                        step = static_cast<std::size_t>(dot + 0.5F);
                    }
                    const Color4 diff{palette[step].r - points[point].r, palette[step].g - points[point].g, palette[step].b - points[point].b, 0.0F};
                    const float fC = pC[step] * (1.0F / 8.0F);
                    const float fD = pD[step] * (1.0F / 8.0F);
                    d2X += fC * pC[step];
                    dX.r += fC * diff.r;
                    dX.g += fC * diff.g;
                    dX.b += fC * diff.b;
                    d2Y += fD * pD[step];
                    dY.r += fD * diff.r;
                    dY.g += fD * diff.g;
                    dY.b += fD * diff.b;
                }
                if (d2X > 0.0F) {
                    const float f = -1.0F / d2X;
                    x.r += dX.r * f;
                    x.g += dX.g * f;
                    x.b += dX.b * f;
                }
                if (d2Y > 0.0F) {
                    const float f = -1.0F / d2Y;
                    y.r += dY.r * f;
                    y.g += dY.g * f;
                    y.b += dY.b * f;
                }
                if ((dX.r * dX.r < kEpsilon) && (dX.g * dX.g < kEpsilon) && (dX.b * dX.b < kEpsilon) && (dY.r * dY.r < kEpsilon) &&
                    (dY.g * dY.g < kEpsilon) && (dY.b * dY.b < kEpsilon)) {
                    break;
                }
            }
            *outX = x;
            *outY = y;
        }

        /** EncodeBC1 without colour keying (the colour half of BC3, four colours). */
        void EncodeColorD3DX(const Color4 input[16], std::uint8_t out[8])
        {
            constexpr std::size_t steps = 4;
            // Quantise to 5:6:5 first; that makes colours land on the quantised endpoints more often.
            Color4 colors[16];
            for (std::size_t i = 0; i < 16; ++i) {
                colors[i].r = static_cast<float>(static_cast<std::int32_t>(input[i].r * 31.0F + 0.5F)) * (1.0F / 31.0F);
                colors[i].g = static_cast<float>(static_cast<std::int32_t>(input[i].g * 63.0F + 0.5F)) * (1.0F / 63.0F);
                colors[i].b = static_cast<float>(static_cast<std::int32_t>(input[i].b * 31.0F + 0.5F)) * (1.0F / 31.0F);
                colors[i].a = 1.0F;
                colors[i].r *= kLuminance.r;
                colors[i].g *= kLuminance.g;
                colors[i].b *= kLuminance.b;
            }
            Color4 a, b;
            OptimizeRGB(&a, &b, colors, steps);
            Color4 c{a.r * kLuminanceInv.r, a.g * kLuminanceInv.g, a.b * kLuminanceInv.b, 1.0F};
            Color4 d{b.r * kLuminanceInv.r, b.g * kLuminanceInv.g, b.b * kLuminanceInv.b, 1.0F};
            const std::uint16_t wA = Encode565(c);
            const std::uint16_t wB = Encode565(d);
            if (wA == wB) {
                out[0] = static_cast<std::uint8_t>(wA & 0xFF);
                out[1] = static_cast<std::uint8_t>(wA >> 8);
                out[2] = static_cast<std::uint8_t>(wB & 0xFF);
                out[3] = static_cast<std::uint8_t>(wB >> 8);
                out[4] = out[5] = out[6] = out[7] = 0;
                return;
            }
            c = Decode565(wA);
            d = Decode565(wB);
            a = Color4{c.r * kLuminance.r, c.g * kLuminance.g, c.b * kLuminance.b, 1.0F};
            b = Color4{d.r * kLuminance.r, d.g * kLuminance.g, d.b * kLuminance.b, 1.0F};

            std::uint16_t w0, w1;
            Color4 step[4];
            if (wA <= wB) { // four colours need w0 > w1: swap ((3 == steps) == (wA <= wB) is false here)
                w0 = wB;
                w1 = wA;
                step[0] = b;
                step[1] = a;
            } else {
                w0 = wA;
                w1 = wB;
                step[0] = a;
                step[1] = b;
            }
            static const std::size_t kSteps4[] = {0, 2, 3, 1};
            step[2] = Lerp(step[0], step[1], 1.0F / 3.0F);
            step[3] = Lerp(step[0], step[1], 2.0F / 3.0F);

            Color4 direction{step[1].r - step[0].r, step[1].g - step[0].g, step[1].b - step[0].b, 0.0F};
            const float stepsF = static_cast<float>(steps - 1);
            const float scale = (wA != wB) ? (stepsF / (direction.r * direction.r + direction.g * direction.g + direction.b * direction.b)) : 0.0F;
            direction.r *= scale;
            direction.g *= scale;
            direction.b *= scale;

            std::uint32_t bits = 0;
            for (std::size_t i = 0; i < 16; ++i) {
                const float r = input[i].r * kLuminance.r;
                const float g = input[i].g * kLuminance.g;
                const float bl = input[i].b * kLuminance.b;
                const float dot = (r - step[0].r) * direction.r + (g - step[0].g) * direction.g + (bl - step[0].b) * direction.b;
                std::uint32_t index;
                if (dot <= 0.0F) {
                    index = 0;
                } else if (dot >= stepsF) {
                    index = 1;
                } else {
                    index = static_cast<std::uint32_t>(kSteps4[static_cast<std::size_t>(dot + 0.5F)]);
                }
                bits = (index << 30) | (bits >> 2);
            }
            out[0] = static_cast<std::uint8_t>(w0 & 0xFF);
            out[1] = static_cast<std::uint8_t>(w0 >> 8);
            out[2] = static_cast<std::uint8_t>(w1 & 0xFF);
            out[3] = static_cast<std::uint8_t>(w1 >> 8);
            out[4] = static_cast<std::uint8_t>(bits & 0xFF);
            out[5] = static_cast<std::uint8_t>((bits >> 8) & 0xFF);
            out[6] = static_cast<std::uint8_t>((bits >> 16) & 0xFF);
            out[7] = static_cast<std::uint8_t>(bits >> 24);
        }

        void OptimizeAlpha(float* const outX, float* const outY, const float points[16], const std::size_t steps)
        {
            static const float kC6[] = {5.0F / 5.0F, 4.0F / 5.0F, 3.0F / 5.0F, 2.0F / 5.0F, 1.0F / 5.0F, 0.0F / 5.0F};
            static const float kD6[] = {0.0F / 5.0F, 1.0F / 5.0F, 2.0F / 5.0F, 3.0F / 5.0F, 4.0F / 5.0F, 5.0F / 5.0F};
            static const float kC8[] = {7.0F / 7.0F, 6.0F / 7.0F, 5.0F / 7.0F, 4.0F / 7.0F, 3.0F / 7.0F, 2.0F / 7.0F, 1.0F / 7.0F, 0.0F / 7.0F};
            static const float kD8[] = {0.0F / 7.0F, 1.0F / 7.0F, 2.0F / 7.0F, 3.0F / 7.0F, 4.0F / 7.0F, 5.0F / 7.0F, 6.0F / 7.0F, 7.0F / 7.0F};
            const float* const pC = (steps == 6) ? kC6 : kC8;
            const float* const pD = (steps == 6) ? kD6 : kD8;
            constexpr float kMax = 1.0F;
            constexpr float kMin = 0.0F;

            float x = kMax;
            float y = kMin;
            if (steps == 8) {
                for (std::size_t point = 0; point < 16; ++point) {
                    x = std::min(x, points[point]);
                    y = std::max(y, points[point]);
                }
            } else {
                for (std::size_t point = 0; point < 16; ++point) {
                    if (points[point] < x && points[point] > kMin) {
                        x = points[point];
                    }
                    if (points[point] > y && points[point] < kMax) {
                        y = points[point];
                    }
                }
                if (x == y) {
                    y = kMax;
                }
            }

            const float stepsF = static_cast<float>(steps - 1);
            for (int iteration = 0; iteration < 8; ++iteration) {
                if ((y - x) < (1.0F / 256.0F)) {
                    break;
                }
                const float scale = stepsF / (y - x);
                float palette[8];
                for (std::size_t step = 0; step < steps; ++step) {
                    palette[step] = pC[step] * x + pD[step] * y;
                }
                if (steps == 6) {
                    palette[6] = kMin;
                    palette[7] = kMax;
                }
                float dX = 0.0F, dY = 0.0F, d2X = 0.0F, d2Y = 0.0F;
                for (std::size_t point = 0; point < 16; ++point) {
                    const float dot = (points[point] - x) * scale;
                    std::size_t step;
                    if (dot <= 0.0F) {
                        step = ((steps == 6) && (points[point] <= x * 0.5F)) ? 6 : 0;
                    } else if (dot >= stepsF) {
                        step = ((steps == 6) && (points[point] >= (y + 1.0F) * 0.5F)) ? 7 : (steps - 1);
                    } else {
                        step = static_cast<std::size_t>(static_cast<std::int32_t>(dot + 0.5F));
                    }
                    if (step < steps) {
                        // D3DX computes the difference this way round (DirectXTex reversed it later:
                        // "D3DX had this computation backwards").
                        const float diff = points[point] - palette[step];
                        dX += pC[step] * diff;
                        d2X += pC[step] * pC[step];
                        dY += pD[step] * diff;
                        d2Y += pD[step] * pD[step];
                    }
                }
                if (d2X > 0.0F) {
                    x -= dX / d2X;
                }
                if (d2Y > 0.0F) {
                    y -= dY / d2Y;
                }
                if (x > y) {
                    std::swap(x, y);
                }
                if ((dX * dX < (1.0F / 64.0F)) && (dY * dY < (1.0F / 64.0F))) {
                    break;
                }
            }
            *outX = (x < kMin) ? kMin : (x > kMax) ? kMax : x;
            *outY = (y < kMin) ? kMin : (y > kMax) ? kMax : y;
        }

        void EncodeAlphaD3DX(const Color4 input[16], std::uint8_t out[8])
        {
            float alpha[16];
            float minAlpha = 1.0F;
            float maxAlpha = 0.0F;
            for (std::size_t i = 0; i < 16; ++i) {
                const float value = input[i].a < 0.0F ? 0.0F : input[i].a > 1.0F ? 1.0F : input[i].a;
                alpha[i] = static_cast<float>(static_cast<std::int32_t>(value * 255.0F + 0.5F)) * (1.0F / 255.0F);
                minAlpha = std::min(minAlpha, alpha[i]);
                maxAlpha = std::max(maxAlpha, alpha[i]);
            }
            if (minAlpha == 1.0F) {
                out[0] = 0xFF;
                out[1] = 0xFF;
                std::memset(out + 2, 0, 6);
                return;
            }
            const std::size_t steps = ((minAlpha == 0.0F) || (maxAlpha == 1.0F)) ? 6 : 8;
            float alphaA = 0.0F, alphaB = 0.0F;
            OptimizeAlpha(&alphaA, &alphaB, alpha, steps);
            const std::uint8_t byteA = static_cast<std::uint8_t>(static_cast<std::int32_t>(alphaA * 255.0F + 0.5F));
            const std::uint8_t byteB = static_cast<std::uint8_t>(static_cast<std::int32_t>(alphaB * 255.0F + 0.5F));
            alphaA = static_cast<float>(byteA) * (1.0F / 255.0F);
            alphaB = static_cast<float>(byteB) * (1.0F / 255.0F);
            if ((steps == 8) && (byteA == byteB)) {
                out[0] = byteA;
                out[1] = byteB;
                std::memset(out + 2, 0, 6);
                return;
            }
            static const std::size_t kSteps6[] = {0, 2, 3, 4, 5, 1};
            static const std::size_t kSteps8[] = {0, 2, 3, 4, 5, 6, 7, 1};
            const std::size_t* stepOrder;
            float palette[8];
            if (steps == 6) {
                out[0] = byteA;
                out[1] = byteB;
                palette[0] = alphaA;
                palette[1] = alphaB;
                for (int i = 1; i < 5; ++i) {
                    palette[i + 1] = (palette[0] * static_cast<float>(5 - i) + palette[1] * static_cast<float>(i)) * (1.0F / 5.0F);
                }
                palette[6] = 0.0F;
                palette[7] = 1.0F;
                stepOrder = kSteps6;
            } else {
                out[0] = byteB;
                out[1] = byteA;
                palette[0] = alphaB;
                palette[1] = alphaA;
                for (int i = 1; i < 7; ++i) {
                    palette[i + 1] = (palette[0] * static_cast<float>(7 - i) + palette[1] * static_cast<float>(i)) * (1.0F / 7.0F);
                }
                stepOrder = kSteps8;
            }
            const float stepsF = static_cast<float>(steps - 1);
            const float scale = (palette[0] != palette[1]) ? (stepsF / (palette[1] - palette[0])) : 0.0F;
            for (std::size_t set = 0; set < 2; ++set) {
                std::uint32_t bits = 0;
                for (std::size_t i = set * 8; i < set * 8 + 8; ++i) {
                    const float value = alpha[i];
                    const float dot = (value - palette[0]) * scale;
                    std::uint32_t index;
                    if (dot <= 0.0F) {
                        index = ((steps == 6) && (value <= palette[0] * 0.5F)) ? 6 : 0;
                    } else if (dot >= stepsF) {
                        index = ((steps == 6) && (value >= (palette[1] + 1.0F) * 0.5F)) ? 7 : 1;
                    } else {
                        index = static_cast<std::uint32_t>(stepOrder[static_cast<std::size_t>(dot + 0.5F)]);
                    }
                    bits = (index << 21) | (bits >> 3);
                }
                out[2 + set * 3] = static_cast<std::uint8_t>(bits & 0xFF);
                out[3 + set * 3] = static_cast<std::uint8_t>((bits >> 8) & 0xFF);
                out[4 + set * 3] = static_cast<std::uint8_t>((bits >> 16) & 0xFF);
            }
        }

        void EncodeBC3Float(const Color4 input[16], std::uint8_t out[16])
        {
            EncodeAlphaD3DX(input, out);
            EncodeColorD3DX(input, out + 8);
        }

        // ---- sources ------------------------------------------------------------------------------

        /** A channel by bit mask, as a 0..1 float (n-bit UNORM). */
        float ChannelFloat(const std::uint32_t pixel, const std::uint32_t mask)
        {
            if (mask == 0U) {
                return 0.0F;
            }
            std::uint32_t shift = 0;
            while (((mask >> shift) & 1U) == 0U) {
                ++shift;
            }
            std::uint32_t bits = 0;
            while (shift + bits < 32U && ((mask >> (shift + bits)) & 1U) != 0U) {
                ++bits;
            }
            const std::uint32_t value = (pixel & mask) >> shift;
            const std::uint32_t maximum = (bits >= 32U) ? 0xFFFFFFFFU : ((1U << bits) - 1U);
            return static_cast<float>(value) * (1.0F / static_cast<float>(maximum));
        }

        bool ReadDds(const std::uint8_t* const data, const std::uint32_t bytes, FloatImage* const image, std::vector<std::uint8_t>* const dxt5,
                     std::string* const format, std::string* const error)
        {
            if (bytes < 128U) {
                *error = "DDS file shorter than its header";
                return false;
            }
            // Lenient like D3DX: the header's size fields are not checked, only what the data needs.
            const std::uint32_t height = ReadU32(data + 12);
            const std::uint32_t width = ReadU32(data + 16);
            const std::uint8_t* const pf = data + 76;
            const std::uint32_t pfFlags = ReadU32(pf + 4);
            std::uint32_t fourCC = ReadU32(pf + 8);
            const std::uint32_t bitCount = ReadU32(pf + 12);
            const std::uint32_t masks[4] = {ReadU32(pf + 16), ReadU32(pf + 20), ReadU32(pf + 24), ReadU32(pf + 28)};
            std::uint32_t offset = 128U;
            if ((pfFlags & kDdpfFourCC) != 0U && fourCC == FourCC('D', 'X', '1', '0')) {
                if (bytes < 148U) {
                    *error = "DDS DX10 header truncated";
                    return false;
                }
                // DXGI_FORMAT_BC1/2/3_UNORM(_SRGB): 71/72, 74/75, 77/78.
                const std::uint32_t dxgi = ReadU32(data + 128);
                fourCC = (dxgi == 71U || dxgi == 72U) ? FourCC('D', 'X', 'T', '1')
                         : (dxgi == 74U || dxgi == 75U) ? FourCC('D', 'X', 'T', '3')
                         : (dxgi == 77U || dxgi == 78U) ? FourCC('D', 'X', 'T', '5')
                                                        : 0U;
                offset = 148U;
                if (fourCC == 0U) {
                    *error = "DDS DX10 format " + std::to_string(dxgi) + " not supported";
                    return false;
                }
            }
            if (width == 0U || height == 0U || width > 16384U || height > 16384U) {
                *error = "DDS size out of range";
                return false;
            }
            const std::uint32_t blocksWide = (width + 3U) / 4U;
            const std::uint32_t blocksHigh = (height + 3U) / 4U;

            if ((pfFlags & kDdpfFourCC) != 0U) {
                std::uint32_t blockBytes = 16U;
                if (fourCC == FourCC('D', 'X', 'T', '1')) {
                    blockBytes = 8U;
                    *format = "DDS DXT1";
                } else if (fourCC == FourCC('D', 'X', 'T', '2') || fourCC == FourCC('D', 'X', 'T', '3')) {
                    *format = fourCC == FourCC('D', 'X', 'T', '2') ? "DDS DXT2" : "DDS DXT3";
                } else if (fourCC == FourCC('D', 'X', 'T', '4') || fourCC == FourCC('D', 'X', 'T', '5')) {
                    *format = fourCC == FourCC('D', 'X', 'T', '4') ? "DDS DXT4" : "DDS DXT5";
                } else {
                    *error = "DDS FourCC not supported";
                    return false;
                }
                const std::uint64_t needed = static_cast<std::uint64_t>(blocksWide) * blocksHigh * blockBytes;
                if (offset + needed > bytes) {
                    *error = "DDS data shorter than its top level";
                    return false;
                }
                const std::uint8_t* const blocks = data + offset;
                // D3DX makes a block-compressed texture in whole blocks, so GetTexture2D reports the size
                // rounded up to multiples of 4 (measured: a 10x18 DXT5 file comes back 12x20, 1x1 as
                // 4x4; tools/tex2d_test.py over all 8861 /textures/ui files).
                image->width = blocksWide * 4U;
                image->height = blocksHigh * 4U;
                if (fourCC == FourCC('D', 'X', 'T', '5') && (width % 4U) == 0U && (height % 4U) == 0U) {
                    // Same format, same size, no filter: D3DX copies the blocks (7925 of 7952 DXT5 UI files).
                    dxt5->assign(blocks, blocks + needed);
                    return true;
                }
                // Otherwise D3DX decodes the file's texels and encodes again; texels outside the file's
                // width x height are transparent black (D3DX_FILTER_NONE).
                image->texels.assign(static_cast<std::size_t>(image->width) * image->height, Color4{});
                Color4 texels[16];
                for (std::uint32_t by = 0; by < blocksHigh; ++by) {
                    for (std::uint32_t bx = 0; bx < blocksWide; ++bx) {
                        const std::uint8_t* const block = blocks + (static_cast<std::size_t>(by) * blocksWide + bx) * blockBytes;
                        if (blockBytes == 8U) {
                            DecodeColorFloat(block, texels, true);
                        } else if (fourCC == FourCC('D', 'X', 'T', '4') || fourCC == FourCC('D', 'X', 'T', '5')) {
                            DecodeBC3Float(block, texels);
                        } else {
                            DecodeBC2Float(block, texels);
                        }
                        for (std::uint32_t y = 0; y < 4U; ++y) {
                            for (std::uint32_t x = 0; x < 4U; ++x) {
                                const std::uint32_t px = bx * 4U + x;
                                const std::uint32_t py = by * 4U + y;
                                if (px < width && py < height) {
                                    image->texels[static_cast<std::size_t>(py) * image->width + px] = texels[y * 4U + x];
                                }
                            }
                        }
                    }
                }
                return true;
            }

            // Uncompressed: RGB(A), luminance or alpha-only by bit masks.
            if (bitCount != 8U && bitCount != 16U && bitCount != 24U && bitCount != 32U) {
                *error = "DDS bit count not supported";
                return false;
            }
            const std::uint32_t texelBytes = bitCount / 8U;
            const std::uint64_t needed = static_cast<std::uint64_t>(width) * height * texelBytes;
            if (offset + needed > bytes) {
                *error = "DDS data shorter than its top level";
                return false;
            }
            *format = "DDS " + std::to_string(bitCount) + "-bit";
            image->width = width;
            image->height = height;
            image->texels.resize(static_cast<std::size_t>(width) * height);
            const std::uint8_t* source = data + offset;
            for (std::size_t texel = 0; texel < static_cast<std::size_t>(width) * height; ++texel, source += texelBytes) {
                std::uint32_t pixel = 0;
                for (std::uint32_t byte = 0; byte < texelBytes; ++byte) {
                    pixel |= static_cast<std::uint32_t>(source[byte]) << (8U * byte);
                }
                Color4& out = image->texels[texel];
                if ((pfFlags & kDdpfLuminance) != 0U) {
                    out.r = out.g = out.b = ChannelFloat(pixel, masks[0]);
                } else if ((pfFlags & kDdpfRgb) != 0U) {
                    out.r = ChannelFloat(pixel, masks[0]);
                    out.g = ChannelFloat(pixel, masks[1]);
                    out.b = ChannelFloat(pixel, masks[2]);
                } else {
                    out.r = out.g = out.b = 0.0F; // alpha-only formats (A8): colour reads as 0 in D3D9
                }
                out.a = ((pfFlags & (kDdpfAlphaPixels | kDdpfAlpha)) != 0U) ? ChannelFloat(pixel, masks[3]) : 1.0F;
            }
            return true;
        }

        // ---- 8-bit decoders (the public API, for CPU decoding where a GPU has no BC formats) --------

        void Expand565(const std::uint16_t color, std::uint8_t out[3])
        {
            const std::uint32_t r = (color >> 11U) & 0x1FU;
            const std::uint32_t g = (color >> 5U) & 0x3FU;
            const std::uint32_t b = color & 0x1FU;
            out[0] = static_cast<std::uint8_t>((r << 3U) | (r >> 2U));
            out[1] = static_cast<std::uint8_t>((g << 2U) | (g >> 4U));
            out[2] = static_cast<std::uint8_t>((b << 3U) | (b >> 2U));
        }

        void DecodeColor(const std::uint8_t* const block, std::uint8_t rgba[64], const bool fourColor)
        {
            const std::uint16_t c0 = ReadU16(block);
            const std::uint16_t c1 = ReadU16(block + 2);
            std::uint8_t palette[4][4] = {};
            Expand565(c0, palette[0]);
            Expand565(c1, palette[1]);
            palette[0][3] = palette[1][3] = 255;
            if (fourColor || c0 > c1) {
                for (int channel = 0; channel < 3; ++channel) {
                    palette[2][channel] = static_cast<std::uint8_t>((2 * palette[0][channel] + palette[1][channel] + 1) / 3);
                    palette[3][channel] = static_cast<std::uint8_t>((palette[0][channel] + 2 * palette[1][channel] + 1) / 3);
                }
                palette[2][3] = palette[3][3] = 255;
            } else {
                for (int channel = 0; channel < 3; ++channel) {
                    palette[2][channel] = static_cast<std::uint8_t>((palette[0][channel] + palette[1][channel]) / 2);
                }
                palette[2][3] = 255; // index 3 stays transparent black
            }
            const std::uint32_t indices = ReadU32(block + 4);
            for (int texel = 0; texel < 16; ++texel) {
                std::memcpy(rgba + texel * 4, palette[(indices >> (2 * texel)) & 3U], 4);
            }
        }
    } // namespace

    void DecodeBlockBC1(const std::uint8_t block[8], std::uint8_t rgba[64])
    {
        DecodeColor(block, rgba, false);
    }

    void DecodeBlockBC2(const std::uint8_t block[16], std::uint8_t rgba[64])
    {
        DecodeColor(block + 8, rgba, true);
        for (int texel = 0; texel < 16; ++texel) {
            const std::uint32_t nibble = (block[texel / 2] >> ((texel & 1) * 4)) & 0xFU;
            rgba[texel * 4 + 3] = static_cast<std::uint8_t>(nibble * 17U);
        }
    }

    void DecodeBlockBC3(const std::uint8_t block[16], std::uint8_t rgba[64])
    {
        DecodeColor(block + 8, rgba, true);
        const int a0 = block[0];
        const int a1 = block[1];
        int palette[8];
        palette[0] = a0;
        palette[1] = a1;
        if (a0 > a1) {
            for (int step = 1; step <= 6; ++step) {
                palette[1 + step] = ((7 - step) * a0 + step * a1 + 3) / 7;
            }
        } else {
            for (int step = 1; step <= 4; ++step) {
                palette[1 + step] = ((5 - step) * a0 + step * a1 + 2) / 5;
            }
            palette[6] = 0;
            palette[7] = 255;
        }
        std::uint64_t bits = 0;
        for (int byte = 0; byte < 6; ++byte) {
            bits |= static_cast<std::uint64_t>(block[2 + byte]) << (8 * byte);
        }
        for (int texel = 0; texel < 16; ++texel) {
            rgba[texel * 4 + 3] = static_cast<std::uint8_t>(palette[(bits >> (3 * texel)) & 7U]);
        }
    }

    void EncodeBlockBC3(const std::uint8_t rgba[64], std::uint8_t out[16])
    {
        Color4 texels[16];
        for (int texel = 0; texel < 16; ++texel) {
            texels[texel].r = static_cast<float>(rgba[texel * 4 + 0]) * (1.0F / 255.0F);
            texels[texel].g = static_cast<float>(rgba[texel * 4 + 1]) * (1.0F / 255.0F);
            texels[texel].b = static_cast<float>(rgba[texel * 4 + 2]) * (1.0F / 255.0F);
            texels[texel].a = static_cast<float>(rgba[texel * 4 + 3]) * (1.0F / 255.0F);
        }
        EncodeBC3Float(texels, out);
    }

    bool DecodeTexture2DPortable(const void* const data, const std::uint32_t bytes, Texture2DBlocks* const out, std::string* const error)
    {
        *out = Texture2DBlocks{};
        const auto* const bytesIn = static_cast<const std::uint8_t*>(data);
        FloatImage image;
        std::vector<std::uint8_t> dxt5;
        if (bytes >= 4U && ReadU32(bytesIn) == FourCC('D', 'D', 'S', ' ')) {
            if (!ReadDds(bytesIn, bytes, &image, &dxt5, &out->sourceFormat, error)) {
                return false;
            }
        } else {
            int width = 0;
            int height = 0;
            int channels = 0;
            stbi_uc* const pixels = stbi_load_from_memory(bytesIn, static_cast<int>(bytes), &width, &height, &channels, 4);
            if (pixels == nullptr) {
                *error = std::string("not a readable image: ") + stbi_failure_reason();
                return false;
            }
            image.width = static_cast<std::uint32_t>(width);
            image.height = static_cast<std::uint32_t>(height);
            image.texels.resize(static_cast<std::size_t>(width) * height);
            for (std::size_t texel = 0; texel < image.texels.size(); ++texel) {
                image.texels[texel].r = static_cast<float>(pixels[texel * 4 + 0]) * (1.0F / 255.0F);
                image.texels[texel].g = static_cast<float>(pixels[texel * 4 + 1]) * (1.0F / 255.0F);
                image.texels[texel].b = static_cast<float>(pixels[texel * 4 + 2]) * (1.0F / 255.0F);
                image.texels[texel].a = static_cast<float>(pixels[texel * 4 + 3]) * (1.0F / 255.0F);
            }
            stbi_image_free(pixels);
            const bool png = bytes >= 4U && bytesIn[0] == 0x89 && bytesIn[1] == 'P';
            const bool bmp = bytes >= 2U && bytesIn[0] == 'B' && bytesIn[1] == 'M';
            const bool jpg = bytes >= 2U && bytesIn[0] == 0xFF && bytesIn[1] == 0xD8;
            out->sourceFormat = png ? "PNG" : bmp ? "BMP" : jpg ? "JPG" : "TGA";
        }
        out->width = image.width;
        out->height = image.height;
        const std::uint32_t blocksWide = (image.width + 3U) / 4U;
        const std::uint32_t blocksHigh = (image.height + 3U) / 4U;
        if (!dxt5.empty()) {
            out->data = std::move(dxt5);
            out->passThrough = true;
            return true;
        }
        out->data.resize(static_cast<std::size_t>(blocksWide) * blocksHigh * 16U);
        Color4 texels[16];
        for (std::uint32_t by = 0; by < blocksHigh; ++by) {
            for (std::uint32_t bx = 0; bx < blocksWide; ++bx) {
                for (std::uint32_t y = 0; y < 4U; ++y) {
                    for (std::uint32_t x = 0; x < 4U; ++x) {
                        const std::uint32_t px = bx * 4U + x;
                        const std::uint32_t py = by * 4U + y;
                        // D3DX_FILTER_NONE: outside the image is transparent black.
                        texels[y * 4U + x] = (px < image.width && py < image.height) ? image.texels[static_cast<std::size_t>(py) * image.width + px] : Color4{};
                    }
                }
                EncodeBC3Float(texels, &out->data[(static_cast<std::size_t>(by) * blocksWide + bx) * 16U]);
            }
        }
        return true;
    }
} // namespace gpg::gal::diligent
