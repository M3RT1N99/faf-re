#pragma once

// gal resource objects of the Diligent spike (M6 step 1). Only the head render target is a GPU
// object (it lives in DiligentHost); everything else is a placeholder that keeps the engine's
// contracts - sizes, formats, level counts, lockable memory - without GPU resources, because draws
// are no-ops in this step:
//   - textures are D3DX textures in the scratch pool of the oracle's NULLREF device, created with the
//     arguments DeviceD3D9::CreateTexture uses, so width/height/levels/format come back as on D3D9
//     and Lock/Unlock hand out real texel memory;
//   - vertex and index buffers are CPU arrays;
//   - render targets, cube targets and depth-stencil targets only carry their contexts.
// Step 3 (m6u-CRIT.txt plan) replaces them with Diligent textures and buffers.

#include <d3d9.h>

#include <cstdint>
#include <vector>

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

    class TextureDiligent final : public Texture
    {
    public:
        /** Takes ownership of `scratch` (may be null for a texture D3DX could not make). */
        TextureDiligent(const TextureContext& context, IDirect3DBaseTexture9* scratch);
        ~TextureDiligent() override;

        TextureContext* GetContext() override;
        TextureLockRect Lock(int level, const RECT& rect, int flags) override;
        int Unlock(TextureLockRect lock) override;
        int Unlock(int level) override;
        void SaveToBuffer(gpg::MemBuffer<char>* outBuffer) override;

        [[nodiscard]] IDirect3DBaseTexture9* GetScratch() const { return scratch_; }

    private:
        TextureContext context_;
        IDirect3DBaseTexture9* scratch_ = nullptr;
        bool locking_ = false;
        int level_ = 0;
    };

    /**
     * DeviceD3D9::CreateTexture (D3D9Interfaces.cpp:2555-2711) with the D3DX calls on the oracle's
     * NULLREF device in D3DPOOL_SCRATCH: file data through D3DXCreate{,Cube,Volume}TextureFromFileInMemoryEx
     * with the same size, filter and DDS mip-skip arguments; empty textures through D3DXCreateTexture,
     * with a D3D9 autogen-mip texture's single reported level.
     */
    boost::shared_ptr<Texture> CreateTextureFromContext(const D3D9Oracle& oracle, const TextureContext& context);

    class RenderTargetDiligent final : public RenderTarget
    {
    public:
        RenderTargetDiligent(const RenderTargetContext& context, int headIndex);
        ~RenderTargetDiligent() override;
        RenderTargetContext* GetContext() override;
        /** No GDI surface; the D3D10 backend returns null too and nothing in the engine calls it. */
        HDC GetDC() override;
        /** The head this is the back buffer of, or -1 for an offscreen target. */
        [[nodiscard]] int GetHeadIndex() const { return headIndex_; }

    private:
        RenderTargetContext context_;
        int headIndex_ = -1;
    };

    class CubeRenderTargetDiligent final : public CubeRenderTarget
    {
    public:
        explicit CubeRenderTargetDiligent(const CubeRenderTargetContext& context);
        ~CubeRenderTargetDiligent() override;
        CubeRenderTargetContext* GetContext() override;

    private:
        CubeRenderTargetContext context_;
    };

    class DepthStencilTargetDiligent final : public DepthStencilTarget
    {
    public:
        explicit DepthStencilTargetDiligent(const DepthStencilTargetContext& context);
        ~DepthStencilTargetDiligent() override;
        DepthStencilTargetContext* GetContext() override;

    private:
        DepthStencilTargetContext context_;
    };

    class VertexBufferDiligent final : public VertexBuffer
    {
    public:
        explicit VertexBufferDiligent(const VertexBufferContext& context);
        ~VertexBufferDiligent() override;
        VertexBufferContext* GetContext() override;
        void* Lock(unsigned int offset, unsigned int size, MohoD3DLockFlags lockFlags) override;
        void Unlock() override;

    private:
        VertexBufferContext context_;
        std::vector<std::uint8_t> data_;
        bool locked_ = false;
    };

    class IndexBufferDiligent final : public IndexBuffer
    {
    public:
        explicit IndexBufferDiligent(const IndexBufferContext& context);
        ~IndexBufferDiligent() override;
        IndexBufferContext* GetContext() override;
        std::int16_t* Lock(unsigned int offset, unsigned int size, MohoD3DLockFlags lockFlags) override;
        void Unlock() override;

    private:
        IndexBufferContext context_;
        std::vector<std::uint8_t> data_;
        bool locked_ = false;
    };

    class VertexFormatDiligent final : public VertexFormat
    {
    public:
        /** Throws gal::Error for a code outside the 24-entry D3D9 table, as VertexFormatD3D9 does. */
        explicit VertexFormatDiligent(std::uint32_t formatCode);
        ~VertexFormatDiligent() override;
    };

    class PipelineStateDiligent final : public PipelineState
    {
    public:
        PipelineStateDiligent();
        ~PipelineStateDiligent() override;
    };
} // namespace gpg::gal::diligent
