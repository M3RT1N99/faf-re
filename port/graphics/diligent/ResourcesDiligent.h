#pragma once

// gal resource objects of the Diligent backend (M6b, component R; docs/port/renderer.md step 3).
//
// Every object keeps D3D9's CPU-visible contract and puts its GPU object behind it:
//   - Textures. On Windows the texels come from D3DX in the oracle's scratch pool, made with the very
//     arguments DeviceD3D9::CreateTexture passes (D3D9Interfaces.cpp:2555-2711): D3DX_DEFAULT sizes
//     (power-of-two rounding with TRIANGLE|DITHER resampling), the DDS mip skip, BOX mips. So width,
//     height, level count, format and every texel equal D3D9's (m6u-DIL.txt 1, "D3DX texel oracle").
//     The scratch texture is the CPU image Lock/Unlock work on, in D3D9's layout and pitch; the Diligent
//     texture holds the full mip chain (GLES samples incomplete chains as black, M1) and is made, and
//     brought up to date with the dirty rectangles, when a shader first needs it - on the render thread,
//     which is the only thread GLES may create on (RenderDeviceGLImpl.cpp:877, m6u-CRIT.txt R3).
//   - Render, cube and depth targets: Diligent textures with render-target/depth and shader-resource
//     views; the head is the host's RGBA8 target.
//   - Vertex and index buffers: a CPU shadow written by Lock, and a Diligent buffer brought up to date
//     when it is bound for a draw. Dynamic ones (D3DUSAGE_DYNAMIC) are written only with
//     Map(MAP_FLAG_DISCARD) or Map(MAP_FLAG_NO_OVERWRITE), as D3D9's locks are, and the first map of a
//     frame writes the whole valid shadow, because Vulkan and D3D12 discard dynamic memory at the end of
//     each frame (DeviceContextVkImpl.cpp:2432-2433, m6u-CRIT.txt R2). Static ones are created with
//     their shadow and change through UpdateBuffer only when locked again.
//   - Vertex formats: the D3D9 declaration table (VertexFormatTableD3D9.inl) on PassBinding.h's fixed
//     ATTRIB slots.
//
// Without the DirectX SDK (Android, M7a1) the texture's CPU image is a ScratchImage instead of a D3DX
// scratch texture: the same D3D9 layout (levels, faces, D3DFORMAT texels, pitches) filled by the
// portable loaders in ResourcesDiligent.cpp (DDS and the image formats D3DX reads, the empty textures
// D3DXCreateTexture makes). Block-compressed images are decoded on the CPU at upload when the GPU
// cannot sample BC formats (GpuShared::UseCpuBcDecode), as on Mali.
//
// This file includes engine and D3D9 headers, never Diligent's (the GPU side is DiligentHost.h's
// GpuTexture/GpuBuffer).

#if defined(_WIN32)
#include <d3d9.h>
#else
#include "D3D9Portable.h"
#endif

#include <cstdint>
#include <memory>
#include <vector>

#include "DiligentHost.h"
#include "PassBinding.h"
#include "gpg/gal/CubeRenderTarget.hpp"
#include "gpg/gal/CubeRenderTargetContext.hpp"
#include "gpg/gal/DepthStencilTarget.hpp"
#include "gpg/gal/DepthStencilTargetContext.hpp"
#include "gpg/gal/IndexBuffer.hpp"
#include "gpg/gal/IndexBufferContext.hpp"
#include "gpg/gal/PipelineState.hpp"
#include "gpg/gal/RenderTarget.hpp"
#include "gpg/gal/RenderTargetContext.hpp"
#include "gpg/gal/Texture.hpp"
#include "gpg/gal/TextureContext.hpp"
#include "gpg/gal/VertexBuffer.hpp"
#include "gpg/gal/VertexBufferContext.hpp"
#include "gpg/gal/VertexFormat.hpp"

namespace gpg::gal::diligent
{
    class D3D9Oracle;

#if defined(_WIN32)
    /** The texture's CPU image: a D3DX texture in the oracle's scratch pool. */
    using ScratchTexture = IDirect3DBaseTexture9;
#else
    /** The texture's CPU image in D3D9's layout (ResourcesDiligent.cpp). */
    class ScratchImage;
    using ScratchTexture = ScratchImage;
#endif

    /**
     * The source bytes of a TextureContext. dataBegin_/dataEnd_ are 32-bit copies of pointers
     * (TextureContext::SetDataBuffer, TextureContext.hpp:85-91; ContextInterfaces.cpp:375-376), which
     * truncates on a 64-bit process without the low arena; dataArray_ at +0x24 is the full-width start of
     * the same shared array. The pointer is rebuilt as dataArray_ plus the low-32-bit offset, which needs
     * no layout change and gives the same pointer on Win32 (m6u-CRIT.txt R9).
     */
    [[nodiscard]] const char* TextureSourceBegin(const TextureContext& context);

    class TextureDiligent final : public Texture, public ShaderResourceSource
    {
    public:
        /** Takes ownership of `scratch` (the D3DX image; may be null for a texture D3DX could not make). */
        TextureDiligent(const TextureContext& context, ScratchTexture* scratch, std::shared_ptr<GpuShared> gpu, bool autoGenerateMips);
        ~TextureDiligent() override;

        TextureContext* GetContext() override;
        TextureLockRect Lock(int level, const RECT& rect, int flags) override;
        int Unlock(TextureLockRect lock) override;
        int Unlock(int level) override;
        void SaveToBuffer(gpg::MemBuffer<char>* outBuffer) override;

        /** The GPU texture, made or brought up to date first. Null for system-memory textures. */
        Diligent::ITextureView* GetShaderResourceView() override;
        [[nodiscard]] ShaderResourceDimension GetShaderResourceDimension() const override;

        [[nodiscard]] ScratchTexture* GetScratch() const { return scratch_; }

        /** The GPU texture, made or brought up to date first; null for system-memory textures. */
        GpuTexture* GetGpu();

    private:
        bool MakeGpu();
        void FlushDirty();
        void UploadRect(std::uint32_t level, const RECT& rect);

        TextureContext context_;
        ScratchTexture* scratch_ = nullptr;
        std::shared_ptr<GpuShared> gpu_;
        std::unique_ptr<GpuTexture> texture_;
        bool gpuFailed_ = false;
        bool autoGenerateMips_ = false;
        bool locking_ = false;
        int level_ = 0;
        int lockFlags_ = 0;
        RECT lockRect_{};
        bool lockWhole_ = true;
        struct Dirty
        {
            bool dirty = false;
            RECT rect{};
        };
        std::vector<Dirty> dirty_; // per level (2D textures; the only ones D3D9 locks)
    };

    /**
     * DeviceD3D9::CreateTexture (D3D9Interfaces.cpp:2555-2711) with the D3DX calls on the oracle's
     * NULLREF device in D3DPOOL_SCRATCH: file data through D3DXCreate{,Cube,Volume}TextureFromFileInMemoryEx
     * with the same size, filter and DDS mip-skip arguments; empty textures through D3DXCreateTexture.
     */
    boost::shared_ptr<Texture> CreateTextureFromContext(const D3D9Oracle& oracle, const TextureContext& context,
                                                        const std::shared_ptr<GpuShared>& gpu);

    class RenderTargetDiligent final : public RenderTarget, public ShaderResourceSource
    {
    public:
        /** An offscreen target; its texture is made on first use. */
        RenderTargetDiligent(const RenderTargetContext& context, std::shared_ptr<GpuShared> gpu);
        /** Head `headIndex`'s back buffer, wrapping the host's head texture. */
        RenderTargetDiligent(const RenderTargetContext& context, std::shared_ptr<GpuShared> gpu, Diligent::ITexture* headTexture, int headIndex);
        ~RenderTargetDiligent() override;
        RenderTargetContext* GetContext() override;
        /** No GDI surface; the D3D10 backend returns null too and nothing in the engine calls it. */
        HDC GetDC() override;

        Diligent::ITextureView* GetShaderResourceView() override;
        [[nodiscard]] ShaderResourceDimension GetShaderResourceDimension() const override;

        /** The GPU texture (made on first use), or null when it cannot be made. */
        GpuTexture* GetGpu();
        /** The head this is the back buffer of, or -1 for an offscreen target. */
        [[nodiscard]] int GetHeadIndex() const { return headIndex_; }

    private:
        RenderTargetContext context_;
        std::shared_ptr<GpuShared> gpu_;
        std::unique_ptr<GpuTexture> texture_;
        bool gpuFailed_ = false;
        int headIndex_ = -1;
    };

    class CubeRenderTargetDiligent final : public CubeRenderTarget, public ShaderResourceSource
    {
    public:
        CubeRenderTargetDiligent(const CubeRenderTargetContext& context, std::shared_ptr<GpuShared> gpu);
        ~CubeRenderTargetDiligent() override;
        CubeRenderTargetContext* GetContext() override;

        Diligent::ITextureView* GetShaderResourceView() override;
        [[nodiscard]] ShaderResourceDimension GetShaderResourceDimension() const override;

        GpuTexture* GetGpu();

    private:
        CubeRenderTargetContext context_;
        std::shared_ptr<GpuShared> gpu_;
        std::unique_ptr<GpuTexture> texture_;
        bool gpuFailed_ = false;
    };

    class DepthStencilTargetDiligent final : public DepthStencilTarget
    {
    public:
        DepthStencilTargetDiligent(const DepthStencilTargetContext& context, std::shared_ptr<GpuShared> gpu);
        ~DepthStencilTargetDiligent() override;
        DepthStencilTargetContext* GetContext() override;

        /** With field0x10_ ("shader-readable", D3D10Interfaces.cpp:3859/3882) the texture has an SRV too. */
        GpuTexture* GetGpu();

    private:
        DepthStencilTargetContext context_;
        std::shared_ptr<GpuShared> gpu_;
        std::unique_ptr<GpuTexture> texture_;
        bool gpuFailed_ = false;
    };

    /**
     * The CPU shadow and GPU buffer shared by vertex and index buffers. D3D9's lock flags arrive as
     * MohoD3DLockFlags (D3D9Utils.h:24); NoOverwrite is honoured for vertex buffers only, as
     * ToIndexBufferLockFlags drops it (D3D9Interfaces.cpp:620-636).
     */
    class BufferShadow
    {
    public:
        BufferShadow(std::uint32_t size, bool indexBuffer, bool dynamic, std::shared_ptr<GpuShared> gpu);
        ~BufferShadow();

        std::uint8_t* Lock(std::uint32_t offset, std::uint32_t size, std::uint32_t flags, bool honourNoOverwrite, const char* file);
        void Unlock(const char* file);
        /** The GPU buffer, made and brought up to date first (render thread, draw time). */
        Diligent::IBuffer* GetBuffer();

    private:
        enum class Pending : std::uint8_t
        {
            None,
            NoOverwrite, // only NOOVERWRITE locks since the last upload
            Rewrite      // a DISCARD or plain lock: rename the buffer and write the valid range
        };

        std::vector<std::uint8_t> shadow_;
        std::shared_ptr<GpuShared> gpu_;
        std::unique_ptr<GpuBuffer> buffer_;
        bool indexBuffer_ = false;
        bool dynamic_ = false;
        bool locked_ = false;
        std::uint32_t lockOffset_ = 0;
        std::uint32_t lockSize_ = 0;
        std::uint32_t lockFlags_ = 0;
        bool honourNoOverwrite_ = false;
        Pending pending_ = Pending::None;
        std::uint32_t pendingBegin_ = 0;
        std::uint32_t pendingEnd_ = 0;
        std::uint32_t validBegin_ = 0;
        std::uint32_t validEnd_ = 0;
        std::uint64_t uploadFrame_ = 0;
        bool failed_ = false;
    };

    class VertexBufferDiligent final : public VertexBuffer
    {
    public:
        VertexBufferDiligent(const VertexBufferContext& context, std::shared_ptr<GpuShared> gpu);
        ~VertexBufferDiligent() override;
        VertexBufferContext* GetContext() override;
        void* Lock(unsigned int offset, unsigned int size, MohoD3DLockFlags lockFlags) override;
        void Unlock() override;

        Diligent::IBuffer* GetBuffer() { return shadow_.GetBuffer(); }

    private:
        VertexBufferContext context_;
        BufferShadow shadow_;
    };

    class IndexBufferDiligent final : public IndexBuffer
    {
    public:
        IndexBufferDiligent(const IndexBufferContext& context, std::shared_ptr<GpuShared> gpu);
        ~IndexBufferDiligent() override;
        IndexBufferContext* GetContext() override;
        std::int16_t* Lock(unsigned int offset, unsigned int size, MohoD3DLockFlags lockFlags) override;
        void Unlock() override;

        Diligent::IBuffer* GetBuffer() { return shadow_.GetBuffer(); }
        [[nodiscard]] bool Is32Bit() const { return context_.format_ != 1U; }

    private:
        IndexBufferContext context_;
        BufferShadow shadow_;
    };

    class VertexFormatDiligent final : public VertexFormat
    {
    public:
        /** Throws gal::Error for a code outside the 24-entry D3D9 table, as VertexFormatD3D9 does. */
        explicit VertexFormatDiligent(std::uint32_t formatCode);
        ~VertexFormatDiligent() override;
    };

    /** gal's opaque PipelineState (Device slot 8); the state itself is DeviceDiligent's shadow. */
    class PipelineStateDiligent final : public PipelineState
    {
    public:
        PipelineStateDiligent();
        ~PipelineStateDiligent() override;
    };
} // namespace gpg::gal::diligent
