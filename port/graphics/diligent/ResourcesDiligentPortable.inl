// The texture path without D3DX (Android, M7a1). Included by ResourcesDiligent.cpp inside namespace
// gpg::gal::diligent, after its helpers (ConvertRow, Union), on builds without the DirectX SDK only;
// Windows keeps the D3DX scratch textures above it.
//
// What D3DX did for the D3D9 backend, done here on the CPU in D3D9's own layout:
//   - ScratchImage: levels x faces subresources of D3DFORMAT texels (block rows for DXT), the layout
//     IDirect3DTexture9::LockRect hands out on a scratch-pool texture (pitch = one row of texels or of
//     4x4 blocks), so the engine's locks write exactly the bytes they wrote on D3D9;
//   - CreateTexture with source 2 (D3DXCreateTexture): an empty image of the requested format, size and
//     level count (0: the full chain);
//   - CreateTexture with source 1 (D3DXCreateTextureFromFileInMemoryEx and its cube/volume forms): DDS
//     read with its own levels (minus the DDS mip skip, D3DX_SKIP_DDS_MIP_LEVELS), missing levels made
//     with a 2x2 box filter for the 8-bit-per-channel formats (D3DX_FILTER_BOX), TGA/PNG/BMP/JPG decoded
//     (android/ImageDecodePortable.h) to A8R8G8B8. Not reproduced: D3DX's TRIANGLE|DITHER resampling of
//     images whose size is not a power of two (the size is kept and a warning logged), format
//     conversions to another requested format (the file's format is kept), and mips of DXT files that
//     lack them (the file's levels are kept). The menu creates one file texture, the 32x32 A8R8G8B8
//     cursor, which this path makes exactly (galplay compares every created texture context with the
//     recorded one and lists differences).
//   - Upload: BC1-3 go to the GPU as they are when it samples BC, or are decoded on the CPU into BGRA8
//     (GpuShared::UseCpuBcDecode: GPUs without BC, as Mali; or forced to test that path).

    // ---------------------------------------------------------------------------------------------
    // ScratchImage

    class ScratchImage
    {
    public:
        enum class Kind : std::uint8_t
        {
            Texture2D,
            Cube,
            Volume
        };

        struct Level
        {
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            std::uint32_t depth = 1;
            std::uint32_t pitch = 0;      // bytes per row of texels, or per row of 4x4 blocks
            std::uint32_t rows = 0;       // texel rows, or block rows
            std::uint32_t slicePitch = 0; // pitch * rows
            std::vector<std::uint8_t> bytes;
        };

        /** A zeroed image of `levels` levels (0: the full chain) per face. False for an unknown format. */
        static std::unique_ptr<ScratchImage> Create(const Kind kind, const std::uint32_t format, const std::uint32_t width,
                                                    const std::uint32_t height, const std::uint32_t depth, std::uint32_t levels)
        {
            if (width == 0U || height == 0U || depth == 0U) {
                return nullptr;
            }
            const std::uint32_t chain = FullChain(width, height, kind == Kind::Volume ? depth : 1U);
            if (levels == 0U || levels > chain) {
                levels = chain;
            }
            auto image = std::unique_ptr<ScratchImage>(new ScratchImage());
            image->kind = kind;
            image->format = format;
            image->levels = levels;
            image->faces = kind == Kind::Cube ? 6U : 1U;
            image->subresources.resize(static_cast<std::size_t>(image->faces) * levels);
            for (std::uint32_t face = 0; face < image->faces; ++face) {
                for (std::uint32_t level = 0; level < levels; ++level) {
                    Level& out = image->At(face, level);
                    out.width = std::max<std::uint32_t>(width >> level, 1U);
                    out.height = std::max<std::uint32_t>(height >> level, 1U);
                    out.depth = kind == Kind::Volume ? std::max<std::uint32_t>(depth >> level, 1U) : 1U;
                    image->Layout(out);
                    out.bytes.assign(static_cast<std::size_t>(out.slicePitch) * out.depth, 0U);
                }
            }
            return image;
        }

        static std::uint32_t FullChain(const std::uint32_t width, const std::uint32_t height, const std::uint32_t depth)
        {
            std::uint32_t chain = 1;
            for (std::uint32_t size = std::max({width, height, depth}); size > 1U; size >>= 1U) {
                ++chain;
            }
            return chain;
        }

        Level& At(const std::uint32_t face, const std::uint32_t level)
        {
            return subresources[static_cast<std::size_t>(face) * levels + level];
        }

        [[nodiscard]] bool BlockCompressed() const
        {
            bool block = false;
            static_cast<void>(D3D9FormatBytes(format, &block));
            return block;
        }

        /** Bytes per texel, or per 4x4 block. */
        [[nodiscard]] std::uint32_t UnitBytes() const
        {
            bool block = false;
            return D3D9FormatBytes(format, &block);
        }

        void Layout(Level& level) const
        {
            if (BlockCompressed()) {
                level.pitch = std::max<std::uint32_t>((level.width + 3U) / 4U, 1U) * UnitBytes();
                level.rows = std::max<std::uint32_t>((level.height + 3U) / 4U, 1U);
            } else {
                level.pitch = level.width * UnitBytes();
                level.rows = level.height;
            }
            level.slicePitch = level.pitch * level.rows;
        }

        Kind kind = Kind::Texture2D;
        std::uint32_t format = 0; // D3DFORMAT
        std::uint32_t levels = 0;
        std::uint32_t faces = 1;
        std::vector<Level> subresources; // face-major: face * levels + level

    private:
        ScratchImage() = default;
    };

    namespace portable
    {
        /** 5/6-bit channel to 8 bits by bit replication, as GPUs expand it. */
        inline void Expand565(const std::uint16_t color, std::uint8_t out[3])
        {
            const unsigned r = (color >> 11U) & 0x1FU;
            const unsigned g = (color >> 5U) & 0x3FU;
            const unsigned b = color & 0x1FU;
            out[0] = static_cast<std::uint8_t>((r << 3U) | (r >> 2U));
            out[1] = static_cast<std::uint8_t>((g << 2U) | (g >> 4U));
            out[2] = static_cast<std::uint8_t>((b << 3U) | (b >> 2U));
        }

        /**
         * One BC1-3 block (D3DFORMAT DXT1..5) into 4x4 BGRA8 texels, row by row (`out` 64 bytes). The
         * interpolated colours round to nearest, as the D3D10+ block-compression rules ask
         * ((2 c0 + c1 + 1) / 3; alpha ((7 - i) a0 + i a1 + 3) / 7). The CPU fallback for GPUs without BC;
         * the M1 decoder in port/native (Image.cpp) truncates instead.
         */
        inline void DecodeBcBlockBgra8(const std::uint32_t format, const std::uint8_t* const block, std::uint8_t out[64])
        {
            const bool bc1 = format == d3dfmt::kDxt1;
            const std::uint8_t* const color = bc1 ? block : block + 8;
            const std::uint16_t c0 = static_cast<std::uint16_t>(color[0] | (color[1] << 8U));
            const std::uint16_t c1 = static_cast<std::uint16_t>(color[2] | (color[3] << 8U));
            std::uint8_t palette[4][4] = {};
            Expand565(c0, palette[0]);
            Expand565(c1, palette[1]);
            palette[0][3] = palette[1][3] = 255U;
            if (!bc1 || c0 > c1) {
                for (int k = 0; k < 3; ++k) {
                    palette[2][k] = static_cast<std::uint8_t>((2U * palette[0][k] + palette[1][k] + 1U) / 3U);
                    palette[3][k] = static_cast<std::uint8_t>((palette[0][k] + 2U * palette[1][k] + 1U) / 3U);
                }
                palette[2][3] = palette[3][3] = 255U;
            } else {
                for (int k = 0; k < 3; ++k) {
                    palette[2][k] = static_cast<std::uint8_t>((palette[0][k] + palette[1][k] + 1U) / 2U);
                }
                palette[2][3] = 255U; // index 3: transparent black (palette[3] stays zero)
            }
            const std::uint32_t indices = static_cast<std::uint32_t>(color[4]) | (static_cast<std::uint32_t>(color[5]) << 8U) |
                                          (static_cast<std::uint32_t>(color[6]) << 16U) | (static_cast<std::uint32_t>(color[7]) << 24U);
            for (std::uint32_t i = 0; i < 16U; ++i) {
                const std::uint8_t* const rgba = palette[(indices >> (2U * i)) & 3U];
                out[i * 4U + 0U] = rgba[2];
                out[i * 4U + 1U] = rgba[1];
                out[i * 4U + 2U] = rgba[0];
                out[i * 4U + 3U] = rgba[3];
            }
            if (format == d3dfmt::kDxt2 || format == d3dfmt::kDxt3) {
                for (std::uint32_t i = 0; i < 16U; ++i) {
                    const unsigned nibble = (block[i / 2U] >> (4U * (i & 1U))) & 0xFU;
                    out[i * 4U + 3U] = static_cast<std::uint8_t>(nibble * 17U);
                }
            } else if (format == d3dfmt::kDxt4 || format == d3dfmt::kDxt5) {
                std::uint8_t alpha[8] = {block[0], block[1]};
                if (alpha[0] > alpha[1]) {
                    for (unsigned i = 1; i < 7; ++i) {
                        alpha[i + 1] = static_cast<std::uint8_t>(((7U - i) * alpha[0] + i * alpha[1] + 3U) / 7U);
                    }
                } else {
                    for (unsigned i = 1; i < 5; ++i) {
                        alpha[i + 1] = static_cast<std::uint8_t>(((5U - i) * alpha[0] + i * alpha[1] + 2U) / 5U);
                    }
                    alpha[6] = 0U;
                    alpha[7] = 255U;
                }
                std::uint64_t bits = 0;
                for (std::uint32_t i = 0; i < 6U; ++i) {
                    bits |= static_cast<std::uint64_t>(block[2 + i]) << (8U * i);
                }
                for (std::uint32_t i = 0; i < 16U; ++i) {
                    out[i * 4U + 3U] = alpha[(bits >> (3U * i)) & 7U];
                }
            }
        }

        /**
         * A block-aligned rectangle of BC blocks (rows of `pitch` bytes starting at `blocks`) decoded into
         * BGRA8 rows of width * 4 bytes. `width`/`height` are texels, multiples of 4 except at the level's
         * edge, where the texels beyond it are dropped.
         */
        inline void DecodeBcRect(const std::uint32_t format, const std::uint8_t* const blocks, const std::uint32_t pitch,
                                 const std::uint32_t width, const std::uint32_t height, std::vector<std::uint8_t>* const out)
        {
            const std::uint32_t blockBytes = format == d3dfmt::kDxt1 ? 8U : 16U;
            out->assign(static_cast<std::size_t>(width) * height * 4U, 0U);
            std::uint8_t texels[64];
            for (std::uint32_t by = 0; by * 4U < height; ++by) {
                for (std::uint32_t bx = 0; bx * 4U < width; ++bx) {
                    DecodeBcBlockBgra8(format, blocks + static_cast<std::size_t>(by) * pitch + static_cast<std::size_t>(bx) * blockBytes, texels);
                    for (std::uint32_t y = 0; y < 4U && by * 4U + y < height; ++y) {
                        for (std::uint32_t x = 0; x < 4U && bx * 4U + x < width; ++x) {
                            std::memcpy(out->data() + (static_cast<std::size_t>(by * 4U + y) * width + bx * 4U + x) * 4U, texels + (y * 4U + x) * 4U, 4U);
                        }
                    }
                }
            }
        }

        [[nodiscard]] inline bool IsBc(const std::uint32_t format)
        {
            return format == d3dfmt::kDxt1 || format == d3dfmt::kDxt2 || format == d3dfmt::kDxt3 || format == d3dfmt::kDxt4 ||
                   format == d3dfmt::kDxt5;
        }

        /** Bytes per channel-set for the formats the box filter handles: 8 bits per channel, n channels. */
        [[nodiscard]] inline std::uint32_t ByteChannels(const std::uint32_t format)
        {
            switch (format) {
            case d3dfmt::kA8R8G8B8:
            case d3dfmt::kX8R8G8B8:
            case d3dfmt::kA8B8G8R8:
            case d3dfmt::kX8B8G8R8:
                return 4U;
            case d3dfmt::kR8G8B8:
                return 3U;
            case d3dfmt::kA8L8:
                return 2U;
            case d3dfmt::kL8:
            case d3dfmt::kA8:
                return 1U;
            default:
                return 0U;
            }
        }

        /** Level `level` from `level - 1` with a 2x2(x2) box filter, rounding to nearest (D3DX_FILTER_BOX). */
        inline bool BoxDownsample(ScratchImage& image, const std::uint32_t face, const std::uint32_t level)
        {
            const std::uint32_t channels = ByteChannels(image.format);
            if (channels == 0U || level == 0U) {
                return false;
            }
            const ScratchImage::Level& from = image.At(face, level - 1U);
            ScratchImage::Level& to = image.At(face, level);
            for (std::uint32_t z = 0; z < to.depth; ++z) {
                for (std::uint32_t y = 0; y < to.height; ++y) {
                    for (std::uint32_t x = 0; x < to.width; ++x) {
                        for (std::uint32_t c = 0; c < channels; ++c) {
                            std::uint32_t sum = 0;
                            std::uint32_t count = 0;
                            for (std::uint32_t dz = 0; dz < (from.depth > 1U ? 2U : 1U); ++dz) {
                                for (std::uint32_t dy = 0; dy < (from.height > 1U ? 2U : 1U); ++dy) {
                                    for (std::uint32_t dx = 0; dx < (from.width > 1U ? 2U : 1U); ++dx) {
                                        const std::uint32_t sx = std::min(x * 2U + dx, from.width - 1U);
                                        const std::uint32_t sy = std::min(y * 2U + dy, from.height - 1U);
                                        const std::uint32_t sz = std::min(z * 2U + dz, from.depth - 1U);
                                        sum += from.bytes[static_cast<std::size_t>(sz) * from.slicePitch + static_cast<std::size_t>(sy) * from.pitch +
                                                          static_cast<std::size_t>(sx) * channels + c];
                                        ++count;
                                    }
                                }
                            }
                            to.bytes[static_cast<std::size_t>(z) * to.slicePitch + static_cast<std::size_t>(y) * to.pitch +
                                     static_cast<std::size_t>(x) * channels + c] = static_cast<std::uint8_t>((sum + count / 2U) / count);
                        }
                    }
                }
            }
            return true;
        }

        [[nodiscard]] inline std::uint32_t ReadU32(const std::uint8_t* const p)
        {
            return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8U) | (static_cast<std::uint32_t>(p[2]) << 16U) |
                   (static_cast<std::uint32_t>(p[3]) << 24U);
        }

        [[nodiscard]] inline bool IsPowerOfTwo(const std::uint32_t value)
        {
            return value != 0U && (value & (value - 1U)) == 0U;
        }

        [[nodiscard]] inline std::uint32_t NextPowerOfTwo(std::uint32_t value)
        {
            std::uint32_t result = 1U;
            while (result < value) {
                result <<= 1U;
            }
            return result;
        }

        /** The D3DFORMAT of a DDS pixel format (DDS_PIXELFORMAT at header offset 76), or 0. */
        inline std::uint32_t DdsFormat(const std::uint8_t* const pf)
        {
            constexpr std::uint32_t kAlphaPixels = 0x1U, kAlpha = 0x2U, kFourCC = 0x4U, kRgb = 0x40U, kLuminance = 0x20000U;
            const std::uint32_t flags = ReadU32(pf + 4);
            const std::uint32_t fourCC = ReadU32(pf + 8);
            const std::uint32_t bits = ReadU32(pf + 12);
            const std::uint32_t r = ReadU32(pf + 16), g = ReadU32(pf + 20), b = ReadU32(pf + 24), a = ReadU32(pf + 28);
            const bool alpha = (flags & kAlphaPixels) != 0U && a != 0U;
            if ((flags & kFourCC) != 0U) {
                return fourCC; // DXT1..5, and the numeric D3DFORMATs D3DX writes as FourCC (111..116)
            }
            if ((flags & kRgb) != 0U) {
                if (bits == 32U && r == 0x00FF0000U && g == 0x0000FF00U && b == 0x000000FFU) {
                    return alpha ? d3dfmt::kA8R8G8B8 : d3dfmt::kX8R8G8B8;
                }
                if (bits == 32U && r == 0x000000FFU && g == 0x0000FF00U && b == 0x00FF0000U) {
                    return alpha ? d3dfmt::kA8B8G8R8 : d3dfmt::kX8B8G8R8;
                }
                if (bits == 32U && r == 0x3FF00000U) {
                    return d3dfmt::kA2R10G10B10;
                }
                if (bits == 32U && r == 0x000003FFU) {
                    return d3dfmt::kA2B10G10R10;
                }
                if (bits == 32U && r == 0x0000FFFFU && g == 0xFFFF0000U) {
                    return d3dfmt::kG16R16;
                }
                if (bits == 24U && r == 0x00FF0000U) {
                    return d3dfmt::kR8G8B8;
                }
                if (bits == 16U && r == 0xF800U) {
                    return d3dfmt::kR5G6B5;
                }
                if (bits == 16U && r == 0x7C00U) {
                    return alpha ? d3dfmt::kA1R5G5B5 : d3dfmt::kX1R5G5B5;
                }
                if (bits == 16U && r == 0x0F00U) {
                    return d3dfmt::kA4R4G4B4;
                }
                return 0U;
            }
            if ((flags & kLuminance) != 0U) {
                return bits == 16U && alpha ? d3dfmt::kA8L8 : bits == 8U ? d3dfmt::kL8 : 0U;
            }
            if ((flags & kAlpha) != 0U && bits == 8U) {
                return d3dfmt::kA8;
            }
            return 0U;
        }

        /**
         * A DDS file as a ScratchImage with D3DX's choices: the file's format, its levels (after skipping
         * `skip` top levels when it has more than that), the missing levels of a full chain made with the
         * box filter where the format allows it (otherwise the file's levels are kept).
         */
        inline std::unique_ptr<ScratchImage> LoadDds(const std::uint8_t* const data, const std::uint32_t bytes, const std::uint32_t skip,
                                                     std::string* const note, std::string* const error)
        {
            constexpr std::uint32_t kMipMapCountFlag = 0x20000U, kCubeMap = 0x200U, kVolume = 0x200000U;
            if (bytes < 128U || ReadU32(data) != 0x20534444U) { // "DDS "
                *error = "not a DDS file";
                return nullptr;
            }
            const std::uint32_t flags = ReadU32(data + 8);
            const std::uint32_t height = ReadU32(data + 12);
            const std::uint32_t width = ReadU32(data + 16);
            const std::uint32_t depthField = ReadU32(data + 24);
            const std::uint32_t mipCount = (flags & kMipMapCountFlag) != 0U ? std::max<std::uint32_t>(ReadU32(data + 28), 1U) : 1U;
            const std::uint32_t caps2 = ReadU32(data + 112);
            const std::uint32_t format = DdsFormat(data + 76);
            TexelConversion unused = TexelConversion::None;
            if (format == 0U || TextureFormatForD3D9(format, &unused) == 0U) {
                *error = "DDS pixel format not supported";
                return nullptr;
            }
            const ScratchImage::Kind kind = (caps2 & kCubeMap) != 0U    ? ScratchImage::Kind::Cube
                                            : (caps2 & kVolume) != 0U ? ScratchImage::Kind::Volume
                                                                      : ScratchImage::Kind::Texture2D;
            const std::uint32_t depth = kind == ScratchImage::Kind::Volume ? std::max<std::uint32_t>(depthField, 1U) : 1U;
            if (width == 0U || height == 0U || width > 16384U || height > 16384U) {
                *error = "DDS size out of range";
                return nullptr;
            }
            // The file's own layout, to walk its data.
            std::unique_ptr<ScratchImage> file = ScratchImage::Create(kind, format, width, height, depth, mipCount);
            if (!file) {
                *error = "DDS size out of range";
                return nullptr;
            }
            std::size_t offset = 128U;
            for (std::uint32_t face = 0; face < file->faces; ++face) {
                for (std::uint32_t level = 0; level < file->levels; ++level) {
                    ScratchImage::Level& out = file->At(face, level);
                    const std::size_t size = out.bytes.size();
                    if (offset + size > bytes) {
                        *error = "DDS data shorter than its levels";
                        return nullptr;
                    }
                    std::memcpy(out.bytes.data(), data + offset, size);
                    offset += size;
                }
            }
            // D3DX_SKIP_DDS_MIP_LEVELS: the top levels are dropped when the file has more.
            const std::uint32_t skipped = (skip != 0U && file->levels > skip) ? skip : 0U;
            const std::uint32_t baseWidth = std::max<std::uint32_t>(width >> skipped, 1U);
            const std::uint32_t baseHeight = std::max<std::uint32_t>(height >> skipped, 1U);
            const std::uint32_t baseDepth = kind == ScratchImage::Kind::Volume ? std::max<std::uint32_t>(depth >> skipped, 1U) : 1U;
            const std::uint32_t fileLevels = file->levels - skipped;
            const std::uint32_t chain = ScratchImage::FullChain(baseWidth, baseHeight, baseDepth);
            const bool canGenerate = ByteChannels(format) != 0U;
            const std::uint32_t levels = canGenerate ? chain : std::min(chain, fileLevels);
            if (!canGenerate && fileLevels < chain) {
                *note = "DXT/other file without a full mip chain: its " + std::to_string(fileLevels) + " levels are kept (D3DX makes " +
                        std::to_string(chain) + ")";
            }
            std::unique_ptr<ScratchImage> image = ScratchImage::Create(kind, format, baseWidth, baseHeight, baseDepth, levels);
            for (std::uint32_t face = 0; face < image->faces; ++face) {
                for (std::uint32_t level = 0; level < image->levels; ++level) {
                    if (level < fileLevels) {
                        image->At(face, level).bytes = file->At(face, level + skipped).bytes;
                    } else {
                        BoxDownsample(*image, face, level);
                    }
                }
            }
            return image;
        }

        /** TGA/PNG/BMP/JPG: the decoded texels as A8R8G8B8 (alpha) or X8R8G8B8, full chain with the box filter. */
        inline std::unique_ptr<ScratchImage> LoadImageFile(const std::uint8_t* const data, const std::uint32_t bytes, std::string* const error)
        {
            DecodedImage decoded;
            if (!DecodeImageFileBgra8(data, bytes, &decoded, error)) {
                return nullptr;
            }
            std::unique_ptr<ScratchImage> image = ScratchImage::Create(ScratchImage::Kind::Texture2D,
                                                                       decoded.hasAlpha ? d3dfmt::kA8R8G8B8 : d3dfmt::kX8R8G8B8,
                                                                       decoded.width, decoded.height, 1U, 0U);
            if (!image) {
                *error = "empty image";
                return nullptr;
            }
            image->At(0, 0).bytes = std::move(decoded.bgra);
            for (std::uint32_t level = 1; level < image->levels; ++level) {
                BoxDownsample(*image, 0, level);
            }
            return image;
        }
    } // namespace portable

    // ---------------------------------------------------------------------------------------------
    // TextureDiligent without D3DX

    TextureDiligent::TextureDiligent(const TextureContext& context, ScratchImage* const scratch, std::shared_ptr<GpuShared> gpu,
                                     const bool autoGenerateMips)
        : scratch_(scratch),
          gpu_(std::move(gpu)),
          autoGenerateMips_(autoGenerateMips)
    {
        context_.AssignFrom(context);
        dirty_.resize(context_.mipmapLevels_ != 0U ? context_.mipmapLevels_ : 1U);
    }

    TextureDiligent::~TextureDiligent()
    {
        delete scratch_;
        scratch_ = nullptr;
        if (texture_ && gpu_) {
            gpu_->Retire(std::move(texture_)); // deferred to the render thread on GL
        }
    }

    TextureContext* TextureDiligent::GetContext()
    {
        return &context_;
    }

    // The checks and messages of TextureD3D9::Lock (D3D9Interfaces.cpp:5703-5747).
    TextureLockRect TextureDiligent::Lock(const int level, const RECT& rect, const int flags)
    {
        if (scratch_ == nullptr) {
            ThrowGalError("TexDiligent.cpp", __LINE__, "lock invalid tex");
        }
        if (level >= static_cast<int>(context_.mipmapLevels_)) {
            ThrowGalError("TexDiligent.cpp", __LINE__, "lock invalid lvl");
        }
        if (locking_) {
            ThrowGalError("TexDiligent.cpp", __LINE__, "tex lock mismatch");
        }
        if (context_.type_ != 1U) {
            ThrowGalError("TexDiligent.cpp", __LINE__, "lock only 2D");
        }
        if (level < 0 || static_cast<std::uint32_t>(level) >= scratch_->levels) {
            ThrowGalError("TexDiligent.cpp", __LINE__, "lock invalid lvl");
        }
        ScratchImage::Level& image = scratch_->At(0, static_cast<std::uint32_t>(level));
        lockWhole_ = rect.left == rect.right; // an empty rect locks the whole level (0x0094A341)
        std::size_t offset = 0;
        if (!lockWhole_) {
            if (rect.left < 0 || rect.top < 0 || static_cast<std::uint32_t>(rect.right) > image.width ||
                static_cast<std::uint32_t>(rect.bottom) > image.height || rect.right < rect.left || rect.bottom < rect.top) {
                ThrowGalErrorFromHresult("TexDiligent.cpp", __LINE__, D3DERR_INVALIDCALL);
            }
            if (scratch_->BlockCompressed()) {
                offset = static_cast<std::size_t>(rect.top / 4) * image.pitch + static_cast<std::size_t>(rect.left / 4) * scratch_->UnitBytes();
            } else {
                offset = static_cast<std::size_t>(rect.top) * image.pitch + static_cast<std::size_t>(rect.left) * scratch_->UnitBytes();
            }
        }
        level_ = level;
        lockFlags_ = flags;
        lockRect_ = rect;
        locking_ = true;
        TextureLockRect lock{};
        lock.flags = flags;
        lock.level = level;
        lock.pitch = static_cast<int>(image.pitch);
        lock.bits = image.bytes.data() + offset;
        return lock;
    }

    int TextureDiligent::Unlock(const int level)
    {
        if (scratch_ == nullptr) {
            ThrowGalError("TexDiligent.cpp", __LINE__, "unlock invalid");
        }
        if (level != level_) {
            ThrowGalError("TexDiligent.cpp", __LINE__, "unlock bad lvl");
        }
        if (!locking_) {
            ThrowGalError("TexDiligent.cpp", __LINE__, "tex lock mismatch");
        }
        // Written texels reach the GPU texture the next time a shader needs it (GetShaderResourceView).
        if ((static_cast<unsigned int>(lockFlags_) & static_cast<unsigned int>(MohoD3DLockFlags::ReadOnly)) == 0U &&
            static_cast<std::size_t>(level) < dirty_.size()) {
            const ScratchImage::Level& image = scratch_->At(0, static_cast<std::uint32_t>(level));
            RECT written = lockWhole_ ? RECT{0, 0, static_cast<LONG>(image.width), static_cast<LONG>(image.height)} : lockRect_;
            Dirty& dirty = dirty_[static_cast<std::size_t>(level)];
            if (dirty.dirty) {
                Union(dirty.rect, written);
            } else {
                dirty.rect = written;
                dirty.dirty = true;
            }
        }
        locking_ = false;
        level_ = 0;
        return 0;
    }

    int TextureDiligent::Unlock(const TextureLockRect lock)
    {
        return Unlock(lock.level);
    }

    // TextureD3D9::SaveToBuffer (D3D9Interfaces.cpp:5800-5835): level 0 as a DDS file in memory, here
    // with the plain DDS header D3DX writes for a 2D texture of the image's format.
    void TextureDiligent::SaveToBuffer(gpg::MemBuffer<char>* const outBuffer)
    {
        if (scratch_ == nullptr) {
            ThrowGalError("TexDiligent.cpp", __LINE__, "attempt to unlock invalid texture");
        }
        if (context_.type_ != 1U) {
            ThrowGalError("TexDiligent.cpp", __LINE__, "currently allowed to only save 2D textures");
        }
        const ScratchImage::Level& level = scratch_->At(0, 0);
        std::vector<std::uint8_t> file(128U, 0U);
        const auto put = [&file](const std::size_t at, const std::uint32_t value) {
            for (int i = 0; i < 4; ++i) {
                file[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value >> (8 * i));
            }
        };
        put(0, 0x20534444U);
        put(4, 124U);
        put(8, 0x1007U | (scratch_->BlockCompressed() ? 0x80000U : 0x8U)); // CAPS HEIGHT WIDTH PIXELFORMAT + LINEARSIZE/PITCH
        put(12, level.height);
        put(16, level.width);
        put(20, scratch_->BlockCompressed() ? static_cast<std::uint32_t>(level.bytes.size()) : level.pitch);
        put(76, 32U);
        if (portable::IsBc(scratch_->format)) {
            put(80, 0x4U);
            put(84, scratch_->format);
        } else {
            // The 8-bit-per-channel ARGB family; other formats are written as their D3DFORMAT FourCC.
            switch (scratch_->format) {
            case d3dfmt::kA8R8G8B8:
            case d3dfmt::kX8R8G8B8:
                put(80, scratch_->format == d3dfmt::kA8R8G8B8 ? 0x41U : 0x40U);
                put(88, 32U);
                put(92, 0x00FF0000U);
                put(96, 0x0000FF00U);
                put(100, 0x000000FFU);
                put(104, scratch_->format == d3dfmt::kA8R8G8B8 ? 0xFF000000U : 0U);
                break;
            default:
                put(80, 0x4U);
                put(84, scratch_->format);
                break;
            }
        }
        put(108, 0x1000U); // DDSCAPS_TEXTURE
        file.insert(file.end(), level.bytes.begin(), level.bytes.end());
        if (outBuffer->Size() != file.size()) {
            *outBuffer = gpg::AllocMemBuffer(file.size());
        }
        std::memcpy(outBuffer->GetPtr(0U, 0U), file.data(), file.size());
    }

    ShaderResourceDimension TextureDiligent::GetShaderResourceDimension() const
    {
        switch (context_.type_) {
        case 2U:
            return ShaderResourceDimension::TextureCube;
        case 3U:
            return ShaderResourceDimension::Texture3D;
        default:
            return ShaderResourceDimension::Texture2D;
        }
    }

    GpuTexture* TextureDiligent::GetGpu()
    {
        // D3DPOOL_SYSTEMMEM textures (usage 3) cannot be sampled on D3D9 either.
        if (scratch_ == nullptr || context_.usage_ == 3U || !gpu_) {
            return nullptr;
        }
        std::lock_guard<std::recursive_mutex> lock(gpu_->Lock());
        if (!texture_) {
            if (gpuFailed_ || !MakeGpu()) {
                return nullptr;
            }
        } else {
            FlushDirty();
        }
        return texture_.get();
    }

    Diligent::ITextureView* TextureDiligent::GetShaderResourceView()
    {
        GpuTexture* const texture = GetGpu();
        return texture != nullptr ? texture->ShaderResourceView() : nullptr;
    }

    // The whole image, every level (and face), in one CreateTexture with initial data. BC formats are
    // decoded into BGRA8 first when the GPU cannot sample them.
    bool TextureDiligent::MakeGpu()
    {
        GpuTextureDesc desc;
        desc.name = context_.location_.c_str()[0] != '\0' ? context_.location_.c_str() : "gal texture";
        desc.mipLevels = scratch_->levels;
        const std::uint32_t d3dFormat = scratch_->format;
        const std::uint32_t faces = scratch_->faces;
        const ScratchImage::Level& level0 = scratch_->At(0, 0);
        desc.width = level0.width;
        desc.height = level0.height;
        if (scratch_->kind == ScratchImage::Kind::Cube) {
            desc.kind = GpuTextureDesc::Kind::TextureCube;
        } else if (scratch_->kind == ScratchImage::Kind::Volume) {
            desc.kind = GpuTextureDesc::Kind::Texture3D;
            desc.depth = level0.depth;
        }
        TexelConversion conversion = TexelConversion::None;
        desc.format = TextureFormatForD3D9(d3dFormat, &conversion);
        bool blockCompressed = false;
        const std::uint32_t d3dTexelBytes = D3D9FormatBytes(d3dFormat, &blockCompressed);
        if (desc.format == 0U) {
            gpg::Warnf("[gal-diligent] texture %s: D3DFORMAT %u has no Diligent format", desc.name, d3dFormat);
            gpuFailed_ = true;
            return false;
        }
        const bool cpuDecode = blockCompressed && portable::IsBc(d3dFormat) && gpu_->UseCpuBcDecode();
        if (blockCompressed && ((desc.width % 4U) != 0U || (desc.height % 4U) != 0U)) {
            // As on Windows: BC top levels in whole blocks (the CPU-decoded copy keeps the same size, so
            // the UV scale is the same on both paths).
            gpg::Warnf("[gal-diligent] texture %s: %ux%u block-compressed top level padded to whole blocks", desc.name, desc.width,
                       desc.height);
            desc.width = (desc.width + 3U) & ~3U;
            desc.height = (desc.height + 3U) & ~3U;
        }
        if (cpuDecode) {
            desc.format = TextureFormatForD3D9(d3dfmt::kA8R8G8B8, &conversion); // BGRA8, the same bytes
            conversion = TexelConversion::None;
            gpu_->Stats().bcDecodedOnCpu++;
        }
        desc.generateMips = autoGenerateMips_ && !blockCompressed;
        if (desc.generateMips) {
            // D3DUSAGE_AUTOGENMIPMAP: D3D9 reports one level and keeps the chain itself.
            desc.mipLevels = ScratchImage::FullChain(desc.width, desc.height, 1U);
        }

        std::vector<GpuSubresource> subresources;
        std::vector<std::vector<std::uint8_t>> converted;
        converted.reserve(static_cast<std::size_t>(faces) * scratch_->levels);
        const std::uint32_t uploadLevels = desc.generateMips ? 1U : scratch_->levels;
        for (std::uint32_t face = 0; face < faces; ++face) {
            for (std::uint32_t level = 0; level < uploadLevels; ++level) {
                ScratchImage::Level& image = scratch_->At(face, level);
                GpuSubresource subresource;
                subresource.data = image.bytes.data();
                subresource.stride = image.pitch;
                subresource.depthStride = image.slicePitch;
                if (cpuDecode) {
                    // The level padded to whole blocks, as the BC texture would have it.
                    const std::uint32_t width = std::max<std::uint32_t>(desc.width >> level, 1U);
                    const std::uint32_t height = std::max<std::uint32_t>(desc.height >> level, 1U);
                    std::vector<std::uint8_t> rows;
                    portable::DecodeBcRect(d3dFormat, image.bytes.data(), image.pitch, width, height, &rows);
                    converted.push_back(std::move(rows));
                    subresource.data = converted.back().data();
                    subresource.stride = width * 4U;
                    subresource.depthStride = width * height * 4U;
                } else if (conversion != TexelConversion::None) {
                    const std::uint32_t rowsTotal = image.height * image.depth;
                    std::vector<std::uint8_t> rows(static_cast<std::size_t>(image.width) * rowsTotal * 4U);
                    for (std::uint32_t row = 0; row < rowsTotal; ++row) {
                        ConvertRow(conversion, image.bytes.data() + static_cast<std::size_t>(image.pitch) * row,
                                   rows.data() + static_cast<std::size_t>(image.width) * 4U * row, image.width);
                    }
                    converted.push_back(std::move(rows));
                    subresource.data = converted.back().data();
                    subresource.stride = image.width * ConvertedTexelBytes(conversion, d3dTexelBytes);
                    subresource.depthStride = subresource.stride * image.height;
                }
                subresources.push_back(subresource);
            }
        }
        std::string error;
        texture_ = GpuTexture::Create(*gpu_, desc, subresources.data(), static_cast<std::uint32_t>(subresources.size()), &error);
        if (!texture_) {
            gpg::Warnf("[gal-diligent] texture %s: %s", desc.name, error.c_str());
            gpuFailed_ = true;
            return false;
        }
        if (desc.generateMips) {
            texture_->GenerateMips(*gpu_);
        }
        for (Dirty& dirty : dirty_) {
            dirty.dirty = false;
        }
        return true;
    }

    void TextureDiligent::FlushDirty()
    {
        bool any = false;
        for (std::size_t level = 0; level < dirty_.size(); ++level) {
            if (!dirty_[level].dirty) {
                continue;
            }
            UploadRect(static_cast<std::uint32_t>(level), dirty_[level].rect);
            dirty_[level].dirty = false;
            any = true;
        }
        if (any && autoGenerateMips_) {
            texture_->GenerateMips(*gpu_);
        }
    }

    // UpdateTexture of one dirty rectangle of a 2D level, from the scratch image (decoded on the CPU
    // for GPUs without BC).
    void TextureDiligent::UploadRect(const std::uint32_t level, const RECT& requested)
    {
        if (scratch_->kind != ScratchImage::Kind::Texture2D || level >= scratch_->levels) {
            return; // D3D9 locks 2D textures only (TextureD3D9::Lock, "lock only 2D")
        }
        const ScratchImage::Level& image = scratch_->At(0, level);
        TexelConversion conversion = TexelConversion::None;
        static_cast<void>(TextureFormatForD3D9(scratch_->format, &conversion));
        bool blockCompressed = false;
        const std::uint32_t texelBytes = D3D9FormatBytes(scratch_->format, &blockCompressed);
        const bool cpuDecode = blockCompressed && portable::IsBc(scratch_->format) && gpu_->UseCpuBcDecode();
        RECT rect = requested;
        rect.left = std::max<LONG>(rect.left, 0);
        rect.top = std::max<LONG>(rect.top, 0);
        rect.right = std::min<LONG>(rect.right, static_cast<LONG>(image.width));
        rect.bottom = std::min<LONG>(rect.bottom, static_cast<LONG>(image.height));
        if (blockCompressed) {
            // D3D9 locks DXT surfaces in whole blocks; the update region must be block-aligned too.
            rect.left &= ~3L;
            rect.top &= ~3L;
            rect.right = std::min<LONG>((rect.right + 3L) & ~3L, static_cast<LONG>((image.width + 3U) & ~3U));
            rect.bottom = std::min<LONG>((rect.bottom + 3L) & ~3L, static_cast<LONG>((image.height + 3U) & ~3U));
        }
        if (rect.right <= rect.left || rect.bottom <= rect.top) {
            return;
        }
        const std::uint8_t* const bits = image.bytes.data();
        const std::uint32_t width = static_cast<std::uint32_t>(rect.right - rect.left);
        const std::uint32_t height = static_cast<std::uint32_t>(rect.bottom - rect.top);
        GpuSubresource data;
        std::vector<std::uint8_t> rows;
        if (cpuDecode) {
            portable::DecodeBcRect(scratch_->format,
                                   bits + static_cast<std::size_t>(rect.top / 4) * image.pitch + static_cast<std::size_t>(rect.left / 4) * texelBytes,
                                   image.pitch, width, height, &rows);
            data.data = rows.data();
            data.stride = width * 4U;
        } else if (blockCompressed) {
            data.data = bits + static_cast<std::size_t>(rect.top / 4) * image.pitch + static_cast<std::size_t>(rect.left / 4) * texelBytes;
            data.stride = image.pitch;
        } else if (conversion == TexelConversion::None) {
            data.data = bits + static_cast<std::size_t>(rect.top) * image.pitch + static_cast<std::size_t>(rect.left) * texelBytes;
            data.stride = image.pitch;
        } else {
            rows.resize(static_cast<std::size_t>(width) * height * 4U);
            for (std::uint32_t row = 0; row < height; ++row) {
                ConvertRow(conversion,
                           bits + static_cast<std::size_t>(rect.top + static_cast<LONG>(row)) * image.pitch +
                               static_cast<std::size_t>(rect.left) * texelBytes,
                           rows.data() + static_cast<std::size_t>(width) * 4U * row, width);
            }
            data.data = rows.data();
            data.stride = width * 4U;
        }
        texture_->Update(*gpu_, level, 0, static_cast<std::uint32_t>(rect.left), static_cast<std::uint32_t>(rect.top), width, height, data);
    }

    boost::shared_ptr<Texture> CreateTextureFromContext(const D3D9Oracle& oracle, const TextureContext& context,
                                                        const std::shared_ptr<GpuShared>& gpu)
    {
        static_cast<void>(oracle);
        TextureContext textureContext{};
        textureContext.AssignFrom(context);
        // ClearTextureContextData, D3D9Interfaces.cpp:2497-2508: the texture keeps no source bytes.
        if (textureContext.dataCount_ != nullptr) {
            textureContext.dataCount_->release();
        }
        textureContext.dataArray_ = nullptr;
        textureContext.dataCount_ = nullptr;
        textureContext.dataBegin_ = 0U;
        textureContext.dataEnd_ = 0U;

        const auto* const sourceData = reinterpret_cast<const std::uint8_t*>(TextureSourceBegin(context));
        const std::uint32_t sourceBytes = context.dataEnd_ - context.dataBegin_;

        std::unique_ptr<ScratchImage> scratch;
        bool autoGenerateMips = false;
        if (context.source_ == 2U) {
            const std::uint32_t format = D3D9Oracle::FormatGalToD3D(context.format_);
            // D3D9 asks for D3DUSAGE_AUTOGENMIPMAP when levels are 0 and the texture is not dynamic
            // (D3D9Interfaces.cpp:2575-2580); such a texture reports one level and the GPU keeps the chain.
            autoGenerateMips = (context.mipmapLevels_ == 0U) && (context.usage_ != 2U) && (context.usage_ != 3U);
            const std::uint32_t levels = autoGenerateMips ? 1U : context.mipmapLevels_;
            scratch = ScratchImage::Create(ScratchImage::Kind::Texture2D, format, context.width_, context.height_, 1U, levels);
            if (!scratch || format == 0U) {
                ThrowGalErrorFromHresult("DeviceDiligent.cpp", __LINE__, D3DERR_INVALIDCALL);
            }
            textureContext.type_ = 1U;
        } else if (context.source_ == 1U) {
            if (context.dataEnd_ == context.dataBegin_) {
                ThrowGalError("DeviceDiligent.cpp", __LINE__, "attempt to create texture from uninitialized memory");
            }
            std::string note;
            std::string error;
            const bool dds = sourceBytes >= 4U && portable::ReadU32(sourceData) == 0x20534444U;
            scratch = dds ? portable::LoadDds(sourceData, sourceBytes, context.reserved0x44_ & 0x1FU, &note, &error)
                          : portable::LoadImageFile(sourceData, sourceBytes, &error);
            if (!scratch) {
                ThrowGalError("DeviceDiligent.cpp", __LINE__, ("cannot load texture " + std::string(context.location_.c_str()) + ": " + error).c_str());
            }
            const ScratchImage::Level& top = scratch->At(0, 0);
            const std::uint32_t requestedWidth = context.width_ != 0U ? context.width_ : portable::NextPowerOfTwo(top.width);
            const std::uint32_t requestedHeight = context.height_ != 0U ? context.height_ : portable::NextPowerOfTwo(top.height);
            if (scratch->kind == ScratchImage::Kind::Texture2D && (requestedWidth != top.width || requestedHeight != top.height)) {
                gpg::Warnf("[gal-diligent] texture %s: %ux%u kept (D3DX resamples it to %ux%u)", context.location_.c_str(), top.width,
                           top.height, requestedWidth, requestedHeight);
            }
            const std::uint32_t requestedFormat = D3D9Oracle::FormatGalToD3D(context.format_);
            if (requestedFormat != 0U && requestedFormat != scratch->format) {
                gpg::Warnf("[gal-diligent] texture %s: kept in D3DFORMAT %u (D3DX converts it to %u)", context.location_.c_str(),
                           scratch->format, requestedFormat);
            }
            if (!note.empty()) {
                gpg::Warnf("[gal-diligent] texture %s: %s", context.location_.c_str(), note.c_str());
            }
            textureContext.type_ = scratch->kind == ScratchImage::Kind::Cube ? 2U : scratch->kind == ScratchImage::Kind::Volume ? 3U : 1U;
        } else {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "invalid source specified for texture data");
        }
        const ScratchImage::Level& top = scratch->At(0, 0);
        textureContext.mipmapLevels_ = scratch->levels;
        textureContext.format_ = D3D9Oracle::FormatD3D9ToMoho(scratch->format);
        textureContext.width_ = top.width;
        textureContext.height_ = top.height;
        return boost::shared_ptr<Texture>(new TextureDiligent(textureContext, scratch.release(), gpu, autoGenerateMips));
    }
