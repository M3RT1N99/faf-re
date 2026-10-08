#include "ResourcesDiligent.h"

#include <d3dx9.h>

#include <algorithm>
#include <cstring>
#include <mutex>

#include "D3D9Oracle.h"
#include "PipelineDiligent.h"
#include "gpg/core/utils/Logging.h"

namespace gpg::gal::diligent
{
    namespace d3d9table
    {
#include "VertexFormatTableD3D9.inl"

        [[nodiscard]] bool IsEnd(const D3DVERTEXELEMENT9& element) noexcept
        {
            return element.Stream == kVertexElementEndSentinel.Stream && element.Offset == kVertexElementEndSentinel.Offset &&
                   element.Type == kVertexElementEndSentinel.Type && element.Method == kVertexElementEndSentinel.Method &&
                   element.Usage == kVertexElementEndSentinel.Usage &&
                   element.UsageIndex == kVertexElementEndSentinel.UsageIndex;
        }

        /** GetVertexElementTypeSizeBytes, D3D9Interfaces.cpp:214-243 (binary 0x0094ABF0). */
        [[nodiscard]] std::uint32_t TypeSizeBytes(const std::uint8_t elementType) noexcept
        {
            switch (elementType) {
            case 0U:
            case 4U:
            case 5U:
            case 6U:
            case 8U:
            case 9U:
            case 11U:
            case 13U:
            case 14U:
            case 15U:
                return 4U;
            case 1U:
            case 7U:
            case 10U:
            case 12U:
            case 16U:
                return 8U;
            case 2U:
                return 12U;
            case 3U:
                return 16U;
            default:
                return 0U;
            }
        }
    } // namespace d3d9table

    namespace
    {
        /** ToTextureLockFlags (D3D9Interfaces.cpp:661-676) minus DISCARD, which scratch textures reject. */
        unsigned int ToScratchLockFlags(const int flags) noexcept
        {
            return (static_cast<unsigned int>(flags) & static_cast<unsigned int>(MohoD3DLockFlags::ReadOnly)) != 0U ? D3DLOCK_READONLY : 0U;
        }

        void Union(RECT& into, const RECT& rect)
        {
            into.left = std::min(into.left, rect.left);
            into.top = std::min(into.top, rect.top);
            into.right = std::max(into.right, rect.right);
            into.bottom = std::max(into.bottom, rect.bottom);
        }

        /**
         * One row of D3D9 texels as the Diligent format TextureFormatForD3D9 chose (BGRA8 for the expanded
         * formats; the D3DFORMAT reference gives the sampled values: missing colour 0 is not used here
         * because L8/A8L8 replicate luminance, and alpha 1 where the format has none).
         */
        void ConvertRow(const TexelConversion conversion, const std::uint8_t* const from, std::uint8_t* const to, const std::uint32_t texels)
        {
            for (std::uint32_t x = 0; x < texels; ++x) {
                std::uint8_t* const out = to + x * 4U;
                switch (conversion) {
                case TexelConversion::L8ToBgra8:
                    out[0] = out[1] = out[2] = from[x];
                    out[3] = 0xFF;
                    break;
                case TexelConversion::A8L8ToBgra8:
                    out[0] = out[1] = out[2] = from[x * 2U];
                    out[3] = from[x * 2U + 1U];
                    break;
                case TexelConversion::R8G8B8ToBgra8:
                    out[0] = from[x * 3U + 0U];
                    out[1] = from[x * 3U + 1U];
                    out[2] = from[x * 3U + 2U];
                    out[3] = 0xFF;
                    break;
                case TexelConversion::X1R5G5B5ToBgra8:
                case TexelConversion::A4R4G4B4ToBgra8: {
                    const std::uint32_t texel = static_cast<std::uint32_t>(from[x * 2U]) | (static_cast<std::uint32_t>(from[x * 2U + 1U]) << 8U);
                    if (conversion == TexelConversion::X1R5G5B5ToBgra8) {
                        const std::uint32_t b = texel & 0x1FU;
                        const std::uint32_t g = (texel >> 5U) & 0x1FU;
                        const std::uint32_t r = (texel >> 10U) & 0x1FU;
                        out[0] = static_cast<std::uint8_t>((b << 3U) | (b >> 2U));
                        out[1] = static_cast<std::uint8_t>((g << 3U) | (g >> 2U));
                        out[2] = static_cast<std::uint8_t>((r << 3U) | (r >> 2U));
                        out[3] = 0xFF;
                    } else {
                        out[0] = static_cast<std::uint8_t>((texel & 0xFU) * 17U);
                        out[1] = static_cast<std::uint8_t>(((texel >> 4U) & 0xFU) * 17U);
                        out[2] = static_cast<std::uint8_t>(((texel >> 8U) & 0xFU) * 17U);
                        out[3] = static_cast<std::uint8_t>(((texel >> 12U) & 0xFU) * 17U);
                    }
                    break;
                }
                case TexelConversion::A2R10G10B10ToRgb10A2: {
                    std::uint32_t texel = 0;
                    std::memcpy(&texel, from + x * 4U, 4U);
                    const std::uint32_t r = (texel >> 20U) & 0x3FFU;
                    const std::uint32_t b = texel & 0x3FFU;
                    const std::uint32_t swapped = (texel & 0xC00FFC00U) | r | (b << 20U);
                    std::memcpy(out, &swapped, 4U);
                    break;
                }
                default:
                    break;
                }
            }
        }

        std::uint32_t ConvertedTexelBytes(const TexelConversion conversion, const std::uint32_t d3dTexelBytes)
        {
            return conversion == TexelConversion::None ? d3dTexelBytes : 4U;
        }

        /** The level desc of a scratch texture's subresource (2D level, or cube level). */
        bool GetLevelInfo(IDirect3DBaseTexture9* const texture, const std::uint32_t level, D3DSURFACE_DESC* const desc)
        {
            switch (texture->GetType()) {
            case D3DRTYPE_TEXTURE:
                return SUCCEEDED(static_cast<IDirect3DTexture9*>(texture)->GetLevelDesc(level, desc));
            case D3DRTYPE_CUBETEXTURE:
                return SUCCEEDED(static_cast<IDirect3DCubeTexture9*>(texture)->GetLevelDesc(level, desc));
            default:
                return false;
            }
        }

        std::vector<VertexInputElement> BuildVertexInputElements(const std::uint32_t formatCode)
        {
            std::vector<VertexInputElement> elements;
            if (formatCode >= d3d9table::kVertexFormatCount) {
                return elements;
            }
            const D3DVERTEXELEMENT9* const table = d3d9table::kVertexFormatsByCode[formatCode];
            for (std::size_t index = 0; !d3d9table::IsEnd(table[index]); ++index) {
                const D3DVERTEXELEMENT9& element = table[index];
                VertexInputElement out;
                out.attribute = VertexAttributeSlotOf(element.Usage, element.UsageIndex);
                out.stream = element.Stream;
                out.offset = element.Offset;
                out.d3dDeclType = element.Type;
                out.d3dUsage = element.Usage;
                out.d3dUsageIndex = element.UsageIndex;
                elements.push_back(out);
            }
            return elements;
        }
    } // namespace

    const std::vector<VertexInputElement>& GetVertexInputElements(const std::uint32_t formatCode)
    {
        // Built once from the D3D9 table; read-only afterwards, so any thread may read it.
        static const std::vector<std::vector<VertexInputElement>> kElements = [] {
            std::vector<std::vector<VertexInputElement>> all;
            for (std::uint32_t code = 0; code < d3d9table::kVertexFormatCount; ++code) {
                all.push_back(BuildVertexInputElements(code));
            }
            return all;
        }();
        static const std::vector<VertexInputElement> kNone;
        return formatCode < kElements.size() ? kElements[formatCode] : kNone;
    }

    const char* TextureSourceBegin(const TextureContext& context)
    {
        const auto base = reinterpret_cast<std::uintptr_t>(context.dataArray_);
        if (base == 0U) {
            return reinterpret_cast<const char*>(static_cast<std::uintptr_t>(context.dataBegin_));
        }
        const std::uint32_t offset = context.dataBegin_ - static_cast<std::uint32_t>(base);
        return reinterpret_cast<const char*>(base) + offset;
    }

    // ---------------------------------------------------------------------------------------------
    // Texture

    TextureDiligent::TextureDiligent(
        const TextureContext& context,
        IDirect3DBaseTexture9* const scratch,
        std::shared_ptr<GpuShared> gpu,
        const bool autoGenerateMips
    )
        : scratch_(scratch),
          gpu_(std::move(gpu)),
          autoGenerateMips_(autoGenerateMips)
    {
        context_.AssignFrom(context);
        dirty_.resize(context_.mipmapLevels_ != 0U ? context_.mipmapLevels_ : 1U);
    }

    TextureDiligent::~TextureDiligent()
    {
        if (scratch_ != nullptr) {
            if (locking_ && scratch_->GetType() == D3DRTYPE_TEXTURE) {
                static_cast<IDirect3DTexture9*>(scratch_)->UnlockRect(static_cast<UINT>(level_));
            }
            scratch_->Release();
            scratch_ = nullptr;
        }
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

        D3DLOCKED_RECT lockedRect{};
        RECT copiedRect = rect;
        lockWhole_ = copiedRect.left == copiedRect.right; // an empty rect locks the whole level (0x0094A341)
        const RECT* const d3dRect = lockWhole_ ? nullptr : &copiedRect;
        const HRESULT result = static_cast<IDirect3DTexture9*>(scratch_)->LockRect(static_cast<UINT>(level), &lockedRect, d3dRect,
                                                                                    ToScratchLockFlags(flags));
        if (FAILED(result)) {
            ThrowGalErrorFromHresult("TexDiligent.cpp", __LINE__, result);
        }

        level_ = level;
        lockFlags_ = flags;
        lockRect_ = copiedRect;
        locking_ = true;
        TextureLockRect lock{};
        lock.flags = flags;
        lock.level = level;
        lock.pitch = lockedRect.Pitch;
        lock.bits = lockedRect.pBits;
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
        const HRESULT result = static_cast<IDirect3DTexture9*>(scratch_)->UnlockRect(static_cast<UINT>(level));
        if (FAILED(result)) {
            ThrowGalErrorFromHresult("TexDiligent.cpp", __LINE__, result);
        }
        // Written texels reach the GPU texture the next time a shader needs it (GetShaderResourceView).
        if ((static_cast<unsigned int>(lockFlags_) & static_cast<unsigned int>(MohoD3DLockFlags::ReadOnly)) == 0U &&
            static_cast<std::size_t>(level) < dirty_.size()) {
            D3DSURFACE_DESC desc{};
            GetLevelInfo(scratch_, static_cast<std::uint32_t>(level), &desc);
            RECT written = lockWhole_ ? RECT{0, 0, static_cast<LONG>(desc.Width), static_cast<LONG>(desc.Height)} : lockRect_;
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
        return result;
    }

    int TextureDiligent::Unlock(const TextureLockRect lock)
    {
        return Unlock(lock.level);
    }

    // TextureD3D9::SaveToBuffer (D3D9Interfaces.cpp:5800-5835): level 0 as a DDS file in memory.
    void TextureDiligent::SaveToBuffer(gpg::MemBuffer<char>* const outBuffer)
    {
        if (scratch_ == nullptr) {
            ThrowGalError("TexDiligent.cpp", __LINE__, "attempt to unlock invalid texture");
        }
        if (context_.type_ != 1U) {
            ThrowGalError("TexDiligent.cpp", __LINE__, "currently allowed to only save 2D textures");
        }
        IDirect3DSurface9* surface = nullptr;
        HRESULT result = static_cast<IDirect3DTexture9*>(scratch_)->GetSurfaceLevel(0U, &surface);
        if (FAILED(result)) {
            ThrowGalErrorFromHresult("TexDiligent.cpp", __LINE__, result);
        }
        ID3DXBuffer* fileBuffer = nullptr;
        result = D3DXSaveSurfaceToFileInMemory(&fileBuffer, D3DXIFF_DDS, surface, nullptr, nullptr);
        surface->Release();
        if (FAILED(result)) {
            ThrowGalErrorFromHresult("TexDiligent.cpp", __LINE__, result);
        }
        if (outBuffer->Size() != fileBuffer->GetBufferSize()) {
            *outBuffer = gpg::AllocMemBuffer(fileBuffer->GetBufferSize());
        }
        std::memcpy(outBuffer->GetPtr(0U, 0U), fileBuffer->GetBufferPointer(), fileBuffer->GetBufferSize());
        fileBuffer->Release();
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

    // The whole D3DX image, every level (and face), in one CreateTexture with initial data.
    bool TextureDiligent::MakeGpu()
    {
        const D3DRESOURCETYPE type = scratch_->GetType();
        const std::uint32_t levels = scratch_->GetLevelCount();
        GpuTextureDesc desc;
        desc.name = context_.location_.c_str()[0] != '\0' ? context_.location_.c_str() : "gal texture";
        desc.mipLevels = levels;
        std::uint32_t d3dFormat = 0;
        std::uint32_t faces = 1;
        if (type == D3DRTYPE_TEXTURE || type == D3DRTYPE_CUBETEXTURE) {
            D3DSURFACE_DESC level0{};
            if (!GetLevelInfo(scratch_, 0, &level0)) {
                gpuFailed_ = true;
                return false;
            }
            d3dFormat = static_cast<std::uint32_t>(level0.Format);
            desc.width = level0.Width;
            desc.height = level0.Height;
            if (type == D3DRTYPE_CUBETEXTURE) {
                desc.kind = GpuTextureDesc::Kind::TextureCube;
                faces = 6;
            }
        } else if (type == D3DRTYPE_VOLUMETEXTURE) {
            D3DVOLUME_DESC volume{};
            static_cast<IDirect3DVolumeTexture9*>(scratch_)->GetLevelDesc(0, &volume);
            d3dFormat = static_cast<std::uint32_t>(volume.Format);
            desc.kind = GpuTextureDesc::Kind::Texture3D;
            desc.width = volume.Width;
            desc.height = volume.Height;
            desc.depth = volume.Depth;
        } else {
            gpuFailed_ = true;
            return false;
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
        if (blockCompressed && ((desc.width % 4U) != 0U || (desc.height % 4U) != 0U)) {
            // D3D11 needs BC top levels in whole blocks (D3D11_TEXTURE2D_DESC); D3D9 sampled the
            // partial block's texels. Padding keeps the blocks; the UV scale differs (logged, rare).
            gpg::Warnf("[gal-diligent] texture %s: %ux%u block-compressed top level padded to whole blocks", desc.name, desc.width,
                       desc.height);
            desc.width = (desc.width + 3U) & ~3U;
            desc.height = (desc.height + 3U) & ~3U;
        }
        desc.generateMips = autoGenerateMips_ && !blockCompressed;
        if (desc.generateMips) {
            // D3DUSAGE_AUTOGENMIPMAP: D3D9 reports one level and keeps the chain itself.
            std::uint32_t chain = 1;
            for (std::uint32_t size = std::max(desc.width, desc.height); size > 1U; size >>= 1U) {
                ++chain;
            }
            desc.mipLevels = chain;
        }

        // Lock every subresource for reading and point the initial data at it (converted where needed).
        std::vector<GpuSubresource> subresources;
        std::vector<std::vector<std::uint8_t>> converted;
        struct Locked
        {
            std::uint32_t face;
            std::uint32_t level;
        };
        std::vector<Locked> locked;
        bool ok = true;
        const std::uint32_t uploadLevels = desc.generateMips ? 1U : levels;
        for (std::uint32_t face = 0; face < faces && ok; ++face) {
            for (std::uint32_t level = 0; level < uploadLevels && ok; ++level) {
                GpuSubresource subresource;
                std::uint32_t width = 0;
                std::uint32_t height = 0;
                if (type == D3DRTYPE_VOLUMETEXTURE) {
                    D3DLOCKED_BOX box{};
                    D3DVOLUME_DESC volume{};
                    static_cast<IDirect3DVolumeTexture9*>(scratch_)->GetLevelDesc(level, &volume);
                    ok = SUCCEEDED(static_cast<IDirect3DVolumeTexture9*>(scratch_)->LockBox(level, &box, nullptr, D3DLOCK_READONLY));
                    subresource.data = box.pBits;
                    subresource.stride = static_cast<std::uint32_t>(box.RowPitch);
                    subresource.depthStride = static_cast<std::uint32_t>(box.SlicePitch);
                    width = volume.Width;
                    height = volume.Height * volume.Depth;
                } else {
                    D3DLOCKED_RECT rect{};
                    D3DSURFACE_DESC levelDesc{};
                    GetLevelInfo(scratch_, level, &levelDesc);
                    if (type == D3DRTYPE_CUBETEXTURE) {
                        ok = SUCCEEDED(static_cast<IDirect3DCubeTexture9*>(scratch_)->LockRect(static_cast<D3DCUBEMAP_FACES>(face), level, &rect,
                                                                                                 nullptr, D3DLOCK_READONLY));
                    } else {
                        ok = SUCCEEDED(static_cast<IDirect3DTexture9*>(scratch_)->LockRect(level, &rect, nullptr, D3DLOCK_READONLY));
                    }
                    subresource.data = rect.pBits;
                    subresource.stride = static_cast<std::uint32_t>(rect.Pitch);
                    width = levelDesc.Width;
                    height = levelDesc.Height;
                }
                if (!ok) {
                    break;
                }
                locked.push_back({face, level});
                if (conversion != TexelConversion::None) {
                    // Expanded formats are never block compressed and never volumes in the corpus.
                    std::vector<std::uint8_t> rows(static_cast<std::size_t>(width) * height * 4U);
                    for (std::uint32_t row = 0; row < height; ++row) {
                        ConvertRow(conversion, static_cast<const std::uint8_t*>(subresource.data) + static_cast<std::size_t>(subresource.stride) * row,
                                   rows.data() + static_cast<std::size_t>(width) * 4U * row, width);
                    }
                    subresource.data = rows.data();
                    subresource.stride = width * ConvertedTexelBytes(conversion, d3dTexelBytes);
                    converted.push_back(std::move(rows));
                    subresource.data = converted.back().data();
                }
                subresources.push_back(subresource);
            }
        }
        std::string error;
        if (ok) {
            texture_ = GpuTexture::Create(*gpu_, desc, subresources.data(), static_cast<std::uint32_t>(subresources.size()), &error);
        }
        for (const Locked& entry : locked) {
            if (type == D3DRTYPE_VOLUMETEXTURE) {
                static_cast<IDirect3DVolumeTexture9*>(scratch_)->UnlockBox(entry.level);
            } else if (type == D3DRTYPE_CUBETEXTURE) {
                static_cast<IDirect3DCubeTexture9*>(scratch_)->UnlockRect(static_cast<D3DCUBEMAP_FACES>(entry.face), entry.level);
            } else {
                static_cast<IDirect3DTexture9*>(scratch_)->UnlockRect(entry.level);
            }
        }
        if (!texture_) {
            gpg::Warnf("[gal-diligent] texture %s: %s", desc.name, ok ? error.c_str() : "cannot read the D3DX image");
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

    // UpdateTexture of one dirty rectangle of a 2D level, from the scratch image.
    void TextureDiligent::UploadRect(const std::uint32_t level, const RECT& requested)
    {
        if (scratch_->GetType() != D3DRTYPE_TEXTURE) {
            return; // D3D9 locks 2D textures only (TextureD3D9::Lock, "lock only 2D")
        }
        D3DSURFACE_DESC desc{};
        if (!GetLevelInfo(scratch_, level, &desc)) {
            return;
        }
        TexelConversion conversion = TexelConversion::None;
        static_cast<void>(TextureFormatForD3D9(static_cast<std::uint32_t>(desc.Format), &conversion));
        bool blockCompressed = false;
        const std::uint32_t texelBytes = D3D9FormatBytes(static_cast<std::uint32_t>(desc.Format), &blockCompressed);
        RECT rect = requested;
        rect.left = std::max<LONG>(rect.left, 0);
        rect.top = std::max<LONG>(rect.top, 0);
        rect.right = std::min<LONG>(rect.right, static_cast<LONG>(desc.Width));
        rect.bottom = std::min<LONG>(rect.bottom, static_cast<LONG>(desc.Height));
        if (blockCompressed) {
            // D3D9 locks DXT surfaces in whole blocks; the update region must be block-aligned too
            // (DeviceContextD3D11Impl::UpdateTexture checks it).
            rect.left &= ~3L;
            rect.top &= ~3L;
            rect.right = std::min<LONG>((rect.right + 3L) & ~3L, static_cast<LONG>((desc.Width + 3U) & ~3U));
            rect.bottom = std::min<LONG>((rect.bottom + 3L) & ~3L, static_cast<LONG>((desc.Height + 3U) & ~3U));
        }
        if (rect.right <= rect.left || rect.bottom <= rect.top) {
            return;
        }
        D3DLOCKED_RECT locked{};
        if (FAILED(static_cast<IDirect3DTexture9*>(scratch_)->LockRect(level, &locked, nullptr, D3DLOCK_READONLY))) {
            return;
        }
        const auto* const bits = static_cast<const std::uint8_t*>(locked.pBits);
        const std::uint32_t width = static_cast<std::uint32_t>(rect.right - rect.left);
        const std::uint32_t height = static_cast<std::uint32_t>(rect.bottom - rect.top);
        GpuSubresource data;
        std::vector<std::uint8_t> rows;
        if (blockCompressed) {
            data.data = bits + static_cast<std::size_t>(rect.top / 4) * static_cast<std::size_t>(locked.Pitch) +
                        static_cast<std::size_t>(rect.left / 4) * texelBytes;
            data.stride = static_cast<std::uint32_t>(locked.Pitch);
        } else if (conversion == TexelConversion::None) {
            data.data = bits + static_cast<std::size_t>(rect.top) * static_cast<std::size_t>(locked.Pitch) +
                        static_cast<std::size_t>(rect.left) * texelBytes;
            data.stride = static_cast<std::uint32_t>(locked.Pitch);
        } else {
            rows.resize(static_cast<std::size_t>(width) * height * 4U);
            for (std::uint32_t row = 0; row < height; ++row) {
                ConvertRow(conversion,
                           bits + static_cast<std::size_t>(rect.top + static_cast<LONG>(row)) * static_cast<std::size_t>(locked.Pitch) +
                               static_cast<std::size_t>(rect.left) * texelBytes,
                           rows.data() + static_cast<std::size_t>(width) * 4U * row, width);
            }
            data.data = rows.data();
            data.stride = width * 4U;
        }
        texture_->Update(*gpu_, level, 0, static_cast<std::uint32_t>(rect.left), static_cast<std::uint32_t>(rect.top), width, height, data);
        static_cast<IDirect3DTexture9*>(scratch_)->UnlockRect(level);
    }

    boost::shared_ptr<Texture> CreateTextureFromContext(const D3D9Oracle& oracle, const TextureContext& context,
                                                        const std::shared_ptr<GpuShared>& gpu)
    {
        IDirect3DDevice9* const device = oracle.GetDevice();
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

        const void* const sourceData = TextureSourceBegin(context);
        const unsigned int sourceBytes = context.dataEnd_ - context.dataBegin_;

        IDirect3DBaseTexture9* scratch = nullptr;
        bool autoGenerateMips = false;
        if (context.source_ == 2U) {
            const D3DFORMAT format = static_cast<D3DFORMAT>(D3D9Oracle::FormatGalToD3D(context.format_));
            // D3D9 asks for D3DUSAGE_AUTOGENMIPMAP when levels are 0 and the texture is not dynamic
            // (D3D9Interfaces.cpp:2575-2580); such a texture reports one level and the device keeps the
            // chain. Scratch textures cannot autogenerate, so the image has that one level and the GPU
            // texture generates the rest.
            autoGenerateMips = (context.mipmapLevels_ == 0U) && (context.usage_ != 2U) && (context.usage_ != 3U);
            const UINT levels = autoGenerateMips ? 1U : context.mipmapLevels_;
            IDirect3DTexture9* texture = nullptr;
            const HRESULT result = D3DXCreateTexture(device, context.width_, context.height_, levels, 0U, format, D3DPOOL_SCRATCH, &texture);
            if (FAILED(result)) {
                ThrowGalErrorFromHresult("DeviceDiligent.cpp", __LINE__, result);
            }
            D3DSURFACE_DESC desc;
            texture->GetLevelDesc(0U, &desc);
            textureContext.type_ = 1U;
            textureContext.mipmapLevels_ = texture->GetLevelCount();
            textureContext.format_ = D3D9Oracle::FormatD3D9ToMoho(desc.Format);
            textureContext.width_ = desc.Width;
            textureContext.height_ = desc.Height;
            scratch = texture;
        } else if (context.source_ == 1U) {
            if (context.dataEnd_ == context.dataBegin_) {
                ThrowGalError("DeviceDiligent.cpp", __LINE__, "attempt to create texture from uninitialized memory");
            }
            D3DXIMAGE_INFO imageInfo{};
            HRESULT result = D3DXGetImageInfoFromFileInMemory(sourceData, sourceBytes, &imageInfo);
            if (FAILED(result)) {
                ThrowGalErrorFromHresult("DeviceDiligent.cpp", __LINE__, result);
            }
            const D3DFORMAT format = static_cast<D3DFORMAT>(D3D9Oracle::FormatGalToD3D(context.format_));
            if (imageInfo.ResourceType == D3DRTYPE_CUBETEXTURE) {
                const unsigned int edge = (context.width_ != 0U) ? context.width_ : D3DX_DEFAULT;
                IDirect3DCubeTexture9* cube = nullptr;
                result = D3DXCreateCubeTextureFromFileInMemoryEx(device, sourceData, sourceBytes, edge, D3DX_DEFAULT, 0U, format,
                                                                 D3DPOOL_SCRATCH, D3DX_DEFAULT, D3DX_DEFAULT, 0U, nullptr, nullptr, &cube);
                if (FAILED(result)) {
                    ThrowGalErrorFromHresult("DeviceDiligent.cpp", __LINE__, result);
                }
                D3DSURFACE_DESC desc;
                cube->GetLevelDesc(0U, &desc);
                textureContext.type_ = 2U;
                textureContext.mipmapLevels_ = cube->GetLevelCount();
                textureContext.format_ = D3D9Oracle::FormatD3D9ToMoho(desc.Format);
                textureContext.width_ = desc.Width;
                textureContext.height_ = desc.Height;
                scratch = cube;
            } else if (imageInfo.ResourceType == D3DRTYPE_VOLUMETEXTURE) {
                IDirect3DVolumeTexture9* volume = nullptr;
                result = D3DXCreateVolumeTextureFromFileInMemoryEx(device, sourceData, sourceBytes, D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT,
                                                                   D3DX_DEFAULT, 0U, format, D3DPOOL_SCRATCH, D3DX_DEFAULT, D3DX_DEFAULT, 0U,
                                                                   nullptr, nullptr, &volume);
                if (FAILED(result)) {
                    ThrowGalErrorFromHresult("DeviceDiligent.cpp", __LINE__, result);
                }
                D3DVOLUME_DESC desc;
                volume->GetLevelDesc(0U, &desc);
                textureContext.type_ = 3U;
                textureContext.mipmapLevels_ = volume->GetLevelCount();
                textureContext.format_ = D3D9Oracle::FormatD3D9ToMoho(desc.Format);
                textureContext.width_ = desc.Width;
                textureContext.height_ = desc.Height;
                scratch = volume;
            } else if (imageInfo.ResourceType == D3DRTYPE_TEXTURE) {
                const unsigned int width = (context.width_ != 0U) ? context.width_ : D3DX_DEFAULT;
                const unsigned int height = (context.height_ != 0U) ? context.height_ : D3DX_DEFAULT;
                // MipFilter = D3DX_SKIP_DDS_MIP_LEVELS(skip) | D3DX_FILTER_BOX, D3D9Interfaces.cpp:2668-2676.
                const unsigned int mipFilter = ((context.reserved0x44_ & 0x1FU) << 26U) | 5U;
                IDirect3DTexture9* texture = nullptr;
                result = D3DXCreateTextureFromFileInMemoryEx(device, sourceData, sourceBytes, width, height, D3DX_DEFAULT, 0U, format,
                                                             D3DPOOL_SCRATCH, D3DX_DEFAULT, mipFilter, 0U, nullptr, nullptr, &texture);
                if (FAILED(result)) {
                    ThrowGalErrorFromHresult("DeviceDiligent.cpp", __LINE__, result);
                }
                D3DSURFACE_DESC desc;
                texture->GetLevelDesc(0U, &desc);
                textureContext.type_ = 1U;
                textureContext.mipmapLevels_ = texture->GetLevelCount();
                textureContext.format_ = D3D9Oracle::FormatD3D9ToMoho(desc.Format);
                textureContext.width_ = desc.Width;
                textureContext.height_ = desc.Height;
                scratch = texture;
            } else {
                ThrowGalError("DeviceDiligent.cpp", __LINE__, "unknown texture type");
            }
        } else {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "invalid source specified for texture data");
        }

        return boost::shared_ptr<Texture>(new TextureDiligent(textureContext, scratch, gpu, autoGenerateMips));
    }

    // ---------------------------------------------------------------------------------------------
    // Targets

    RenderTargetDiligent::RenderTargetDiligent(const RenderTargetContext& context, std::shared_ptr<GpuShared> gpu)
        : context_(context),
          gpu_(std::move(gpu))
    {}

    RenderTargetDiligent::RenderTargetDiligent(
        const RenderTargetContext& context,
        std::shared_ptr<GpuShared> gpu,
        Diligent::ITexture* const headTexture,
        const int headIndex
    )
        : context_(context),
          gpu_(std::move(gpu)),
          headIndex_(headIndex)
    {
        if (headTexture != nullptr) {
            texture_ = GpuTexture::Wrap(headTexture);
        }
    }

    RenderTargetDiligent::~RenderTargetDiligent()
    {
        if (texture_ && gpu_) {
            gpu_->Retire(std::move(texture_)); // deferred to the render thread on GL
        }
    }

    RenderTargetContext* RenderTargetDiligent::GetContext()
    {
        return &context_;
    }

    HDC RenderTargetDiligent::GetDC()
    {
        return nullptr;
    }

    GpuTexture* RenderTargetDiligent::GetGpu()
    {
        if (!texture_ && !gpuFailed_ && gpu_) {
            // DeviceD3D9::CreateRenderTarget: one level, D3DUSAGE_RENDERTARGET, default pool
            // (D3D9Interfaces.cpp:2718-2730).
            GpuTextureDesc desc;
            desc.name = "gal render target";
            desc.width = context_.width_;
            desc.height = context_.height_;
            desc.format = RenderTargetFormatForToken(context_.format_);
            desc.renderTarget = true;
            std::string error;
            texture_ = GpuTexture::Create(*gpu_, desc, nullptr, 0, &error);
            if (!texture_) {
                gpg::Warnf("[gal-diligent] render target: %s", error.c_str());
                gpuFailed_ = true;
            }
        }
        return texture_.get();
    }

    Diligent::ITextureView* RenderTargetDiligent::GetShaderResourceView()
    {
        GpuTexture* const texture = GetGpu();
        return texture != nullptr ? texture->ShaderResourceView() : nullptr;
    }

    ShaderResourceDimension RenderTargetDiligent::GetShaderResourceDimension() const
    {
        return ShaderResourceDimension::Texture2D;
    }

    CubeRenderTargetDiligent::CubeRenderTargetDiligent(const CubeRenderTargetContext& context, std::shared_ptr<GpuShared> gpu)
        : gpu_(std::move(gpu))
    {
        context_.AssignFrom(context);
    }

    CubeRenderTargetDiligent::~CubeRenderTargetDiligent()
    {
        if (texture_ && gpu_) {
            gpu_->Retire(std::move(texture_)); // deferred to the render thread on GL
        }
    }

    CubeRenderTargetContext* CubeRenderTargetDiligent::GetContext()
    {
        return &context_;
    }

    GpuTexture* CubeRenderTargetDiligent::GetGpu()
    {
        if (!texture_ && !gpuFailed_ && gpu_) {
            // DeviceD3D9::CreateCubeRenderTarget: one level, render target (D3D9Interfaces.cpp:2739-2751).
            GpuTextureDesc desc;
            desc.name = "gal cube render target";
            desc.kind = GpuTextureDesc::Kind::TextureCube;
            desc.width = context_.dimension_;
            desc.height = context_.dimension_;
            desc.format = RenderTargetFormatForToken(context_.format_);
            desc.renderTarget = true;
            std::string error;
            texture_ = GpuTexture::Create(*gpu_, desc, nullptr, 0, &error);
            if (!texture_) {
                gpg::Warnf("[gal-diligent] cube render target: %s", error.c_str());
                gpuFailed_ = true;
            }
        }
        return texture_.get();
    }

    Diligent::ITextureView* CubeRenderTargetDiligent::GetShaderResourceView()
    {
        GpuTexture* const texture = GetGpu();
        return texture != nullptr ? texture->ShaderResourceView() : nullptr;
    }

    ShaderResourceDimension CubeRenderTargetDiligent::GetShaderResourceDimension() const
    {
        return ShaderResourceDimension::TextureCube;
    }

    DepthStencilTargetDiligent::DepthStencilTargetDiligent(const DepthStencilTargetContext& context, std::shared_ptr<GpuShared> gpu)
        : context_(context.width_, context.height_, context.format_, context.field0x10_),
          gpu_(std::move(gpu))
    {}

    DepthStencilTargetDiligent::~DepthStencilTargetDiligent()
    {
        if (texture_ && gpu_) {
            gpu_->Retire(std::move(texture_)); // deferred to the render thread on GL
        }
    }

    DepthStencilTargetContext* DepthStencilTargetDiligent::GetContext()
    {
        return &context_;
    }

    GpuTexture* DepthStencilTargetDiligent::GetGpu()
    {
        if (!texture_ && !gpuFailed_ && gpu_) {
            // DeviceD3D9::CreateDepthStencilTarget: no multisampling (D3D9Interfaces.cpp:2760-2774); a
            // shader-readable one gets an SRV like D3D10's (D3D10Interfaces.cpp:3859/3882).
            GpuTextureDesc desc;
            desc.name = "gal depth-stencil target";
            desc.width = context_.width_;
            desc.height = context_.height_;
            desc.format = DepthStencilFormatForToken(context_.format_);
            desc.depthStencil = true;
            desc.shaderResource = context_.field0x10_;
            std::string error;
            texture_ = GpuTexture::Create(*gpu_, desc, nullptr, 0, &error);
            if (!texture_) {
                gpg::Warnf("[gal-diligent] depth-stencil target: %s", error.c_str());
                gpuFailed_ = true;
            }
        }
        return texture_.get();
    }

    // ---------------------------------------------------------------------------------------------
    // Buffers: the byte sizes of DeviceD3D9::CreateVertexBuffer/CreateIndexBuffer
    // (D3D9Interfaces.cpp:2806-2856).

    BufferShadow::BufferShadow(const std::uint32_t size, const bool indexBuffer, const bool dynamic, std::shared_ptr<GpuShared> gpu)
        : shadow_(size, 0U),
          gpu_(std::move(gpu)),
          indexBuffer_(indexBuffer),
          dynamic_(dynamic)
    {}

    BufferShadow::~BufferShadow()
    {
        if (buffer_ && gpu_) {
            gpu_->Retire(std::move(buffer_)); // deferred to the render thread on GL
        }
    }

    // IDirect3D*Buffer9::Lock: a byte offset and size, 0 meaning "to the end" (OffsetToLock/SizeToLock).
    std::uint8_t* BufferShadow::Lock(const std::uint32_t offset, const std::uint32_t size, const std::uint32_t flags, const bool honourNoOverwrite,
                                     const char* const file)
    {
        if (locked_) {
            ThrowGalError(file, __LINE__, "lock mismatch");
        }
        const std::uint32_t total = static_cast<std::uint32_t>(shadow_.size());
        if (offset > total || (size != 0U && static_cast<std::uint64_t>(offset) + size > total)) {
            ThrowGalError(file, __LINE__, "lock out of range");
        }
        lockOffset_ = offset;
        lockSize_ = size != 0U ? size : total - offset;
        lockFlags_ = flags;
        honourNoOverwrite_ = honourNoOverwrite;
        locked_ = true;
        return shadow_.data() + offset;
    }

    void BufferShadow::Unlock(const char* const file)
    {
        if (!locked_) {
            ThrowGalError(file, __LINE__, "lock mismatch");
        }
        locked_ = false;
        if ((lockFlags_ & static_cast<std::uint32_t>(MohoD3DLockFlags::ReadOnly)) != 0U || lockSize_ == 0U) {
            return;
        }
        const std::uint32_t begin = lockOffset_;
        const std::uint32_t end = lockOffset_ + lockSize_;
        const bool discard = (lockFlags_ & static_cast<std::uint32_t>(MohoD3DLockFlags::Discard)) != 0U;
        const bool noOverwrite = honourNoOverwrite_ && (lockFlags_ & static_cast<std::uint32_t>(MohoD3DLockFlags::NoOverwrite)) != 0U;
        if (discard) {
            // D3DLOCK_DISCARD: the rest of the buffer is undefined from here on.
            validBegin_ = begin;
            validEnd_ = end;
        } else if (validEnd_ == validBegin_) {
            validBegin_ = begin;
            validEnd_ = end;
        } else {
            validBegin_ = std::min(validBegin_, begin);
            validEnd_ = std::max(validEnd_, end);
        }
        if (pending_ == Pending::None) {
            pendingBegin_ = begin;
            pendingEnd_ = end;
        } else {
            pendingBegin_ = std::min(pendingBegin_, begin);
            pendingEnd_ = std::max(pendingEnd_, end);
        }
        if (!dynamic_ || discard || !noOverwrite) {
            pending_ = Pending::Rewrite;
        } else if (pending_ == Pending::None) {
            pending_ = Pending::NoOverwrite;
        }
    }

    Diligent::IBuffer* BufferShadow::GetBuffer()
    {
        if (!gpu_ || failed_) {
            return nullptr;
        }
        std::lock_guard<std::recursive_mutex> lock(gpu_->Lock());
        const std::uint32_t total = static_cast<std::uint32_t>(shadow_.size());
        if (!buffer_) {
            std::string error;
            buffer_ = GpuBuffer::Create(*gpu_, total, indexBuffer_, dynamic_, dynamic_ ? nullptr : shadow_.data(), &error);
            if (!buffer_) {
                gpg::Warnf("[gal-diligent] %s", error.c_str());
                failed_ = true;
                return nullptr;
            }
            if (!dynamic_) {
                pending_ = Pending::None; // created with the whole shadow
                return buffer_->Buffer();
            }
            uploadFrame_ = 0; // dynamic: the first use writes the valid range below
        }
        if (!dynamic_) {
            if (pending_ != Pending::None) {
                buffer_->Update(*gpu_, pendingBegin_, shadow_.data() + pendingBegin_, pendingEnd_ - pendingBegin_);
                pending_ = Pending::None;
            }
            return buffer_->Buffer();
        }

        const std::uint64_t frame = gpu_->Frame();
        GpuUploadStats& stats = gpu_->Stats();
        if (uploadFrame_ != frame || pending_ == Pending::Rewrite) {
            // A new allocation (D3D11 renames, Vulkan/D3D12 take fresh ring memory) with every byte the
            // engine may still draw from: the first map of each frame, and any DISCARD or plain lock.
            if (validEnd_ > validBegin_) {
                buffer_->WriteDiscard(*gpu_, validBegin_, shadow_.data() + validBegin_, validEnd_ - validBegin_);
            } else {
                buffer_->WriteDiscard(*gpu_, 0, shadow_.data(), 0);
            }
            if (pending_ == Pending::Rewrite) {
                ++stats.mapDiscard;
            } else {
                ++stats.mapFrameRestore;
            }
            uploadFrame_ = frame;
        } else if (pending_ == Pending::NoOverwrite) {
            buffer_->WriteNoOverwrite(*gpu_, pendingBegin_, shadow_.data() + pendingBegin_, pendingEnd_ - pendingBegin_);
            ++stats.mapNoOverwrite;
        }
        pending_ = Pending::None;
        return buffer_->Buffer();
    }

    VertexBufferDiligent::VertexBufferDiligent(const VertexBufferContext& context, std::shared_ptr<GpuShared> gpu)
        : shadow_(context.vertexCount_ * context.stride_, false, context.usage_ == 2U, std::move(gpu))
    {
        context_.AssignFrom(context);
    }

    VertexBufferDiligent::~VertexBufferDiligent() = default;

    VertexBufferContext* VertexBufferDiligent::GetContext()
    {
        return &context_;
    }

    void* VertexBufferDiligent::Lock(const unsigned int offset, const unsigned int size, const MohoD3DLockFlags lockFlags)
    {
        return shadow_.Lock(offset, size, static_cast<std::uint32_t>(lockFlags), true, "VtxBufDiligent.cpp");
    }

    void VertexBufferDiligent::Unlock()
    {
        shadow_.Unlock("VtxBufDiligent.cpp");
    }

    IndexBufferDiligent::IndexBufferDiligent(const IndexBufferContext& context, std::shared_ptr<GpuShared> gpu)
        : shadow_(context.size_ * ((context.format_ == 1U) ? 2U : 4U), true, context.type_ == 2U, std::move(gpu))
    {
        context_.AssignFrom(context);
    }

    IndexBufferDiligent::~IndexBufferDiligent() = default;

    IndexBufferContext* IndexBufferDiligent::GetContext()
    {
        return &context_;
    }

    std::int16_t* IndexBufferDiligent::Lock(const unsigned int offset, const unsigned int size, const MohoD3DLockFlags lockFlags)
    {
        return reinterpret_cast<std::int16_t*>(shadow_.Lock(offset, size, static_cast<std::uint32_t>(lockFlags), false, "IdxBufDiligent.cpp"));
    }

    void IndexBufferDiligent::Unlock()
    {
        shadow_.Unlock("IdxBufDiligent.cpp");
    }

    // ---------------------------------------------------------------------------------------------
    // Vertex format: VertexFormatD3D9::SetFormatDeclaration (D3D9Interfaces.cpp:6314-6347): each
    // stream's stride is the furthest end of its elements.

    VertexFormatDiligent::VertexFormatDiligent(const std::uint32_t formatCode)
    {
        if (formatCode >= d3d9table::kVertexFormatCount) {
            ThrowGalError("VertexFormatDiligent.cpp", __LINE__, "invalid vertex format specified");
        }
        formatCode_ = formatCode;
        const D3DVERTEXELEMENT9* const elements = d3d9table::kVertexFormatsByCode[formatCode];
        streamStrides_.clear();
        for (std::size_t index = 0; !d3d9table::IsEnd(elements[index]); ++index) {
            const D3DVERTEXELEMENT9& element = elements[index];
            const std::size_t stream = element.Stream;
            if (stream >= streamStrides_.size()) {
                streamStrides_.resize(stream + 1U, 0U);
            }
            const std::uint32_t end = static_cast<std::uint32_t>(element.Offset) + d3d9table::TypeSizeBytes(element.Type);
            if (streamStrides_[stream] < end) {
                streamStrides_[stream] = end;
            }
        }
    }

    VertexFormatDiligent::~VertexFormatDiligent()
    {
        formatCode_ = 0x17U; // as VertexFormatD3D9::ResetDeclarationState
    }

    PipelineStateDiligent::PipelineStateDiligent() = default;
    PipelineStateDiligent::~PipelineStateDiligent() = default;
} // namespace gpg::gal::diligent
