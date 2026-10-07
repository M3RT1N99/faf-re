#include "ResourcesDiligent.h"

#include <d3dx9.h>

#include <cstring>

#include "D3D9Oracle.h"

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
        /** ClearTextureContextData, D3D9Interfaces.cpp:2483-2494: the texture keeps no source bytes. */
        void ClearTextureContextData(TextureContext& context) noexcept
        {
            if (context.dataCount_ != nullptr) {
                context.dataCount_->release();
            }
            context.dataArray_ = nullptr;
            context.dataCount_ = nullptr;
            context.dataBegin_ = 0U;
            context.dataEnd_ = 0U;
        }

        /** ToTextureLockFlags (D3D9Interfaces.cpp:661-676) minus DISCARD, which scratch textures reject. */
        unsigned int ToScratchLockFlags(const int flags) noexcept
        {
            return (static_cast<unsigned int>(flags) & static_cast<unsigned int>(MohoD3DLockFlags::ReadOnly)) != 0U
                       ? D3DLOCK_READONLY
                       : 0U;
        }
    } // namespace

    // ---------------------------------------------------------------------------------------------
    // Texture

    TextureDiligent::TextureDiligent(const TextureContext& context, IDirect3DBaseTexture9* const scratch)
        : scratch_(scratch)
    {
        context_.AssignFrom(context);
    }

    TextureDiligent::~TextureDiligent()
    {
        if (scratch_ != nullptr) {
            if (locking_) {
                static_cast<IDirect3DTexture9*>(scratch_)->UnlockRect(static_cast<UINT>(level_));
            }
            scratch_->Release();
            scratch_ = nullptr;
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
        const RECT* const d3dRect = (copiedRect.left != copiedRect.right) ? &copiedRect : nullptr;
        const HRESULT result = static_cast<IDirect3DTexture9*>(scratch_)->LockRect(
            static_cast<UINT>(level), &lockedRect, d3dRect, ToScratchLockFlags(flags)
        );
        if (FAILED(result)) {
            ThrowGalErrorFromHresult("TexDiligent.cpp", __LINE__, result);
        }

        level_ = level;
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

    boost::shared_ptr<Texture> CreateTextureFromContext(const D3D9Oracle& oracle, const TextureContext& context)
    {
        IDirect3DDevice9* const device = oracle.GetDevice();
        TextureContext textureContext{};
        textureContext.AssignFrom(context);
        ClearTextureContextData(textureContext);

        const auto* const sourceData = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(context.dataBegin_));
        const unsigned int sourceBytes = context.dataEnd_ - context.dataBegin_;

        IDirect3DBaseTexture9* scratch = nullptr;
        if (context.source_ == 2U) {
            const D3DFORMAT format = static_cast<D3DFORMAT>(D3D9Oracle::FormatGalToD3D(context.format_));
            // D3D9 asks for D3DUSAGE_AUTOGENMIPMAP when levels are 0 and the texture is not dynamic
            // (D3D9Interfaces.cpp:2575-2578); such a texture reports one level. Scratch textures
            // cannot autogenerate, so they are made with that one level.
            const bool autogen = (context.mipmapLevels_ == 0U) && (context.usage_ != 2U);
            const UINT levels = autogen ? 1U : context.mipmapLevels_;
            IDirect3DTexture9* texture = nullptr;
            const HRESULT result =
                D3DXCreateTexture(device, context.width_, context.height_, levels, 0U, format, D3DPOOL_SCRATCH, &texture);
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
                result = D3DXCreateCubeTextureFromFileInMemoryEx(
                    device, sourceData, sourceBytes, edge, D3DX_DEFAULT, 0U, format, D3DPOOL_SCRATCH, D3DX_DEFAULT,
                    D3DX_DEFAULT, 0U, nullptr, nullptr, &cube
                );
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
                result = D3DXCreateVolumeTextureFromFileInMemoryEx(
                    device, sourceData, sourceBytes, D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT, 0U, format,
                    D3DPOOL_SCRATCH, D3DX_DEFAULT, D3DX_DEFAULT, 0U, nullptr, nullptr, &volume
                );
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
                result = D3DXCreateTextureFromFileInMemoryEx(
                    device, sourceData, sourceBytes, width, height, D3DX_DEFAULT, 0U, format, D3DPOOL_SCRATCH, D3DX_DEFAULT,
                    mipFilter, 0U, nullptr, nullptr, &texture
                );
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

        return boost::shared_ptr<Texture>(new TextureDiligent(textureContext, scratch));
    }

    // ---------------------------------------------------------------------------------------------
    // Targets

    RenderTargetDiligent::RenderTargetDiligent(const RenderTargetContext& context, const int headIndex)
        : context_(context),
          headIndex_(headIndex)
    {}

    RenderTargetDiligent::~RenderTargetDiligent() = default;

    RenderTargetContext* RenderTargetDiligent::GetContext()
    {
        return &context_;
    }

    HDC RenderTargetDiligent::GetDC()
    {
        return nullptr;
    }

    CubeRenderTargetDiligent::CubeRenderTargetDiligent(const CubeRenderTargetContext& context)
    {
        context_.AssignFrom(context);
    }

    CubeRenderTargetDiligent::~CubeRenderTargetDiligent() = default;

    CubeRenderTargetContext* CubeRenderTargetDiligent::GetContext()
    {
        return &context_;
    }

    DepthStencilTargetDiligent::DepthStencilTargetDiligent(const DepthStencilTargetContext& context)
        : context_(context.width_, context.height_, context.format_, context.field0x10_)
    {}

    DepthStencilTargetDiligent::~DepthStencilTargetDiligent() = default;

    DepthStencilTargetContext* DepthStencilTargetDiligent::GetContext()
    {
        return &context_;
    }

    // ---------------------------------------------------------------------------------------------
    // Buffers: the byte sizes of DeviceD3D9::CreateVertexBuffer/CreateIndexBuffer
    // (D3D9Interfaces.cpp:2806-2864); Lock takes a byte offset like IDirect3D*Buffer9::Lock, and a
    // size of 0 means "to the end".

    VertexBufferDiligent::VertexBufferDiligent(const VertexBufferContext& context)
    {
        context_.AssignFrom(context);
        data_.resize(static_cast<std::size_t>(context.vertexCount_) * context.stride_);
    }

    VertexBufferDiligent::~VertexBufferDiligent() = default;

    VertexBufferContext* VertexBufferDiligent::GetContext()
    {
        return &context_;
    }

    void* VertexBufferDiligent::Lock(const unsigned int offset, const unsigned int size, const MohoD3DLockFlags lockFlags)
    {
        static_cast<void>(lockFlags);
        if (locked_) {
            ThrowGalError("VtxBufDiligent.cpp", __LINE__, "lock mismatch");
        }
        if (offset > data_.size() || (size != 0U && static_cast<std::size_t>(offset) + size > data_.size())) {
            ThrowGalError("VtxBufDiligent.cpp", __LINE__, "lock out of range");
        }
        locked_ = true;
        return data_.data() + offset;
    }

    void VertexBufferDiligent::Unlock()
    {
        if (!locked_) {
            ThrowGalError("VtxBufDiligent.cpp", __LINE__, "lock mismatch");
        }
        locked_ = false;
    }

    IndexBufferDiligent::IndexBufferDiligent(const IndexBufferContext& context)
    {
        context_.AssignFrom(context);
        const std::size_t bytesPerIndex = (context.format_ == 1U) ? 2U : 4U;
        data_.resize(static_cast<std::size_t>(context.size_) * bytesPerIndex);
    }

    IndexBufferDiligent::~IndexBufferDiligent() = default;

    IndexBufferContext* IndexBufferDiligent::GetContext()
    {
        return &context_;
    }

    std::int16_t* IndexBufferDiligent::Lock(const unsigned int offset, const unsigned int size, const MohoD3DLockFlags lockFlags)
    {
        static_cast<void>(lockFlags);
        if (locked_) {
            ThrowGalError("IdxBufDiligent.cpp", __LINE__, "lock mismatch");
        }
        if (offset > data_.size() || (size != 0U && static_cast<std::size_t>(offset) + size > data_.size())) {
            ThrowGalError("IdxBufDiligent.cpp", __LINE__, "lock out of range");
        }
        locked_ = true;
        return reinterpret_cast<std::int16_t*>(data_.data() + offset);
    }

    void IndexBufferDiligent::Unlock()
    {
        if (!locked_) {
            ThrowGalError("IdxBufDiligent.cpp", __LINE__, "lock mismatch");
        }
        locked_ = false;
    }

    // ---------------------------------------------------------------------------------------------
    // Vertex format: VertexFormatD3D9::SetFormatDeclaration (D3D9Interfaces.cpp:6316-6346): each
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
