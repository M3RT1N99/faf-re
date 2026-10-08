#pragma once

// TGA/PNG/BMP/JPG decoding for the texture path without D3DX (Android, M7a1): what
// D3DXCreateTextureFromFileInMemoryEx reads besides DDS (ResourcesDiligentPortable.inl reads DDS
// itself). stb_image from Diligent's ThirdParty copy, compiled into ImageDecodePortable.cpp only.
// Pure C++: no engine, Diligent or D3D types. Built for Android only (port/android/CMakeLists.txt);
// port_graphics.props leaves port/graphics/diligent/android out of the Windows build.

#include <cstdint>
#include <string>
#include <vector>

namespace gpg::gal::diligent
{
    struct DecodedImage
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool hasAlpha = false;           ///< the file has an alpha channel (A8R8G8B8, else X8R8G8B8)
        std::vector<std::uint8_t> bgra;  ///< B, G, R, A per texel, top row first (D3DFMT_A8R8G8B8 bytes)
    };

    /** False with `error` set when the bytes are not an image stb_image reads. */
    bool DecodeImageFileBgra8(const std::uint8_t* data, std::uint32_t bytes, DecodedImage* out, std::string* error);
} // namespace gpg::gal::diligent
