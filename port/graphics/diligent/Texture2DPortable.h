#pragma once

// GetTexture2D without D3DX (Device slot 23; M6b, component R).
//
// DeviceD3D9::GetTexture2D (D3D9Interfaces.cpp:3134-3244, binary 0x008ECD20) turns any image file
// into DXT5 blocks for the batch textures (SBatchTextureDataFactory.cpp:19-50: UI bitmaps for the
// texture batcher's atlas). It loads the file's top level with D3DXCreateTextureFromFileInMemoryEx
// (D3DX_DEFAULT_NONPOW2 sizes, one level, the file's format, D3DX_FILTER_NONE), converts anything that
// is not DXT5 into a DXT5 surface of the size rounded up to whole blocks with D3DXLoadSurfaceFromSurface
// (FILTER_NONE: texels outside the image are transparent black), and copies the blocks out row by row:
// (width + 3) / 4 blocks of 16 bytes per row, (height + 3) / 4 rows.
//
// The portable path does the same without D3DX, for Android (no D3DX) and as the D3DX-free
// reference on Windows:
//   - DDS files are read leniently (D3DX accepts headers strict loaders reject, m6u-DIL.txt 9); a DXT5
//     top level is copied block for block, which is what D3DX does with a DXT5 source of the same
//     format and size (tools/tex2d_test.cpp checks it on every menu file);
//   - other DDS formats (DXT1/2/3/4, the uncompressed mask formats) and TGA/PNG/BMP/JPG (stb_image,
//     Diligent's ThirdParty copy) are decoded to RGBA8 and encoded with EncodeBlockBC3 below. That
//     encoder is deterministic but is not D3DX's, so for those inputs the blocks are not D3DX's bytes;
//     the unit test reports which inputs take this path.
//
// Pure C++17: no D3D9, D3DX, Diligent or engine types, so the same file builds into main.exe, the
// unit test and an Android library.

#include <cstdint>
#include <string>
#include <vector>

namespace gpg::gal::diligent
{
    struct Texture2DBlocks
    {
        std::vector<std::uint8_t> data; // DXT5 blocks, ((width + 3) / 4) * ((height + 3) / 4) * 16 bytes
        std::uint32_t width = 0;        // the image's size, as GetTexture2D reports it
        std::uint32_t height = 0;
        bool passThrough = false;       // DXT5 blocks copied from the file (D3DX-identical by construction)
        std::string sourceFormat;       // "DDS DXT5", "DDS DXT1", "PNG", ... for the test report
    };

    /** GetTexture2D's result for one file. False with `error` set when the file cannot be read. */
    bool DecodeTexture2DPortable(const void* data, std::uint32_t bytes, Texture2DBlocks* out, std::string* error);

    /** One 4x4 block of RGBA8 texels (row-major, 64 bytes) as a 16-byte DXT5 block. */
    void EncodeBlockBC3(const std::uint8_t rgba[64], std::uint8_t out[16]);

    /** DXT1/DXT3/DXT5 blocks to RGBA8 texels (row-major 4x4, 64 bytes), D3D9's decoding rules. */
    void DecodeBlockBC1(const std::uint8_t block[8], std::uint8_t rgba[64]);
    void DecodeBlockBC2(const std::uint8_t block[16], std::uint8_t rgba[64]);
    void DecodeBlockBC3(const std::uint8_t block[16], std::uint8_t rgba[64]);
} // namespace gpg::gal::diligent
