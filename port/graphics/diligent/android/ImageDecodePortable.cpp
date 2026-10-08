#include "ImageDecodePortable.h"

#include <cstring>

// stb_image from Diligent's ThirdParty, local to this TU (STB_IMAGE_STATIC), as Texture2DPortable.cpp
// compiles its own copy.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_GIF
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#include "dependencies/DiligentCore/ThirdParty/stb/stb_image.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace gpg::gal::diligent
{
    bool DecodeImageFileBgra8(const std::uint8_t* const data, const std::uint32_t bytes, DecodedImage* const out, std::string* const error)
    {
        int width = 0;
        int height = 0;
        int channels = 0;
        if (data == nullptr || bytes == 0U || stbi_info_from_memory(data, static_cast<int>(bytes), &width, &height, &channels) == 0) {
            *error = "not an image file D3DX reads (TGA, PNG, BMP, JPG)";
            return false;
        }
        stbi_uc* const pixels = stbi_load_from_memory(data, static_cast<int>(bytes), &width, &height, &channels, 4);
        if (pixels == nullptr || width <= 0 || height <= 0) {
            *error = std::string("image decoding failed: ") + (stbi_failure_reason() != nullptr ? stbi_failure_reason() : "?");
            if (pixels != nullptr) {
                stbi_image_free(pixels);
            }
            return false;
        }
        out->width = static_cast<std::uint32_t>(width);
        out->height = static_cast<std::uint32_t>(height);
        out->hasAlpha = channels == 2 || channels == 4;
        out->bgra.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
        for (std::size_t texel = 0; texel < static_cast<std::size_t>(width) * static_cast<std::size_t>(height); ++texel) {
            out->bgra[texel * 4U + 0U] = pixels[texel * 4U + 2U];
            out->bgra[texel * 4U + 1U] = pixels[texel * 4U + 1U];
            out->bgra[texel * 4U + 2U] = pixels[texel * 4U + 0U];
            out->bgra[texel * 4U + 3U] = out->hasAlpha ? pixels[texel * 4U + 3U] : 0xFFU;
        }
        stbi_image_free(pixels);
        return true;
    }
} // namespace gpg::gal::diligent
