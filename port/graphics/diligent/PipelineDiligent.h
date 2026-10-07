#pragma once

// The draw path of the Diligent backend (M6b, component R; docs/port/renderer.md step 3): the D3D9
// render-state shadow, the hashed PSO cache, and the Diligent calls of every Device slot that touches
// the immediate context (targets, clears, viewport, draws, StretchRect).
//
// Why a shadow. D3D9 keeps every render state on the device until something changes it, and the
// engine relies on that:
//   - effects are begun with D3DXFX_DONOTSAVESTATE (EffectTechniqueD3D9::BeginTechnique,
//     D3D9Interfaces.cpp:4102), so a pass's state assignments stay on the device after the pass;
//   - PipelineStateD3D9::BeginTechnique (0x00946260, D3D9Interfaces.cpp:3955-3994) resets only nine
//     states at every technique begin; everything else (blend factors, alpha func and ref, stencil
//     ops, fill mode) carries over from earlier draws;
//   - Device slots 43-45 (fog, wireframe, colour write) write device state between draws.
// So a pass alone never describes a draw (m6u-FX.txt section 6g: of 431 FAF passes, 359 leave alpha
// test unset, 159 enable blending without both factors). D3D9StateShadow is the full render state,
// initialised the way DeviceD3D9::Setup leaves the device, and PipelineCache turns it into Diligent
// pipeline states at draw time.
//
// Header rule (DiligentHost.h, PassBinding.h): no Diligent and no engine types, only forward
// declarations, so DeviceDiligent.cpp (engine headers) and PipelineDiligent.cpp (Diligent headers)
// both include it and the two header sets never meet in one TU. The state numbers are D3D9's
// documented values (d3d9types.h), so the header compiles for Android too.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "PassBinding.h"

namespace Diligent
{
    struct IBuffer;
    struct IPipelineState;
    struct ITexture;
    struct ITextureView;
} // namespace Diligent

namespace gpg::gal::diligent
{
    class GpuShared;

    // D3DRENDERSTATETYPE values the backend reads (d3d9types.h).
    namespace rs
    {
        constexpr std::uint32_t ZEnable = 7;
        constexpr std::uint32_t FillMode = 8;
        constexpr std::uint32_t ZWriteEnable = 14;
        constexpr std::uint32_t AlphaTestEnable = 15;
        constexpr std::uint32_t SrcBlend = 19;
        constexpr std::uint32_t DestBlend = 20;
        constexpr std::uint32_t CullMode = 22;
        constexpr std::uint32_t ZFunc = 23;
        constexpr std::uint32_t AlphaRef = 24;
        constexpr std::uint32_t AlphaFunc = 25;
        constexpr std::uint32_t AlphaBlendEnable = 27;
        constexpr std::uint32_t FogEnable = 28;
        constexpr std::uint32_t FogColor = 34;
        constexpr std::uint32_t FogTableMode = 35;
        constexpr std::uint32_t FogStart = 36;
        constexpr std::uint32_t FogEnd = 37;
        constexpr std::uint32_t RangeFogEnable = 48;
        constexpr std::uint32_t StencilEnable = 52;
        constexpr std::uint32_t StencilFail = 53;
        constexpr std::uint32_t StencilZFail = 54;
        constexpr std::uint32_t StencilPass = 55;
        constexpr std::uint32_t StencilFunc = 56;
        constexpr std::uint32_t StencilRef = 57;
        constexpr std::uint32_t StencilMask = 58;
        constexpr std::uint32_t StencilWriteMask = 59;
        constexpr std::uint32_t ColorWriteEnable = 168;
        constexpr std::uint32_t BlendOp = 171;
        constexpr std::uint32_t ScissorTestEnable = 174;
        constexpr std::uint32_t SlopeScaleDepthBias = 175;
        constexpr std::uint32_t AntialiasedLineEnable = 176;
        constexpr std::uint32_t TwoSidedStencilMode = 185;
        constexpr std::uint32_t CcwStencilFail = 186;
        constexpr std::uint32_t CcwStencilZFail = 187;
        constexpr std::uint32_t CcwStencilPass = 188;
        constexpr std::uint32_t CcwStencilFunc = 189;
        constexpr std::uint32_t BlendFactor = 193;
        constexpr std::uint32_t DepthBias = 195;
        constexpr std::uint32_t SeparateAlphaBlendEnable = 206;
        constexpr std::uint32_t SrcBlendAlpha = 207;
        constexpr std::uint32_t DestBlendAlpha = 208;
        constexpr std::uint32_t BlendOpAlpha = 209;
    } // namespace rs

    /**
     * The D3D9 render state of the device, as values. Every state number is kept, also those the draw
     * path ignores (a pass may assign any D3DRS the effect compiler accepts), so the shadow is an exact
     * image of what DeviceD3D9 would hold. Sampler states are the effect layer's (PassBinding.h: the
     * pass's immutable samplers).
     */
    class D3D9StateShadow
    {
    public:
        D3D9StateShadow();

        /**
         * The device right after DeviceD3D9::Setup: D3D9's creation defaults (the D3DRENDERSTATETYPE
         * reference; no auto depth-stencil, D3D9Interfaces.cpp:1926, so ZENABLE starts FALSE), then
         * PipelineStateD3D9::InitState (D3D9Interfaces.cpp:5889-6104), which DeviceD3D9::Setup and Reset
         * run (D3D9Interfaces.cpp:1813, 2240).
         */
        void ResetToSetupState();

        void SetRenderState(std::uint32_t state, std::uint32_t value);
        void SetRenderStateFloat(std::uint32_t state, float value);
        [[nodiscard]] std::uint32_t GetRenderState(std::uint32_t state) const;
        [[nodiscard]] float GetRenderStateFloat(std::uint32_t state) const;

        /** A pass's states, in D3DX's order (PassBinding::GetStates). Texture-stage states are fixed-
         * function only and no effect sets them (m6u-FX.txt section 4); they are ignored. */
        void ApplyPassStates(const PassStateAssignment* states, std::size_t count);

        /** PipelineStateD3D9::BeginTechnique (0x00946260): the nine states every technique begin resets. */
        void BeginTechnique();

        /**
         * PipelineStateD3D9::SetColorWriteState (0x009461F0, D3D9Interfaces.cpp:3932-3947), with its
         * truth table: (true, a) -> a ? 0xF : 0x7; (false, a) -> a ? 0x8 : 0xF. The value is also kept for
         * BeginTechnique, which re-applies it.
         */
        void SetColorWriteState(bool writeColor, bool writeAlpha);

        /** PipelineStateD3D9::SetWireframeState (0x009461C0): D3DRS_FILLMODE WIREFRAME or SOLID. */
        void SetWireframeState(bool enabled);

        /**
         * PipelineStateD3D9::SetFogState (0x009460A0, D3D9Interfaces.cpp:3858-3908), its render-state
         * half. The projection transform it also sets only matters to fixed-function W-fog, which the
         * shaders do themselves here (m6u-FX.txt section 6l; the menu disables fog).
         */
        void SetFogState(bool enable, float fogStart, float fogEnd, std::uint32_t fogColor);

        [[nodiscard]] std::uint64_t GetVersion() const { return version_; }

    private:
        std::array<std::uint32_t, 256> render_{};
        std::uint32_t colorWriteEnable_ = 0x0FU; // PipelineStateD3D9 constructor, 0x00949F80
        std::uint64_t version_ = 1;
    };

    // ---------------------------------------------------------------------------------------------
    // Vertex input

    /** One element of a gal vertex format (the D3D9 table, VertexFormatTableD3D9.inl), resolved. */
    struct VertexInputElement
    {
        int attribute = -1;           // VertexAttributeSlotOf (PassBinding.h)
        std::uint32_t stream = 0;     // D3DVERTEXELEMENT9::Stream = Diligent buffer slot
        std::uint32_t offset = 0;     // D3DVERTEXELEMENT9::Offset
        std::uint32_t d3dDeclType = 0;
        std::uint32_t d3dUsage = 0;
        std::uint32_t d3dUsageIndex = 0;
    };

    /** The elements of gal vertex format `formatCode`; empty for 23 (no format) or a bad code. */
    [[nodiscard]] const std::vector<VertexInputElement>& GetVertexInputElements(std::uint32_t formatCode);

    /** What the effect layer needs to specialise its vertex shader for a format (PassBinding.h AttribKind). */
    [[nodiscard]] VertexInputDesc GetVertexInputDesc(std::uint32_t formatCode);

    constexpr std::uint32_t kMaxVertexStreams = 4; // the D3D9 table uses streams 0-2

    /** A bound vertex stream: what SetVertexBuffer bound (D3D9Interfaces.cpp:3612-3645). */
    struct VertexStreamBinding
    {
        Diligent::IBuffer* buffer = nullptr;
        std::uint32_t offset = 0;       // bytes: startVertex * stride
        std::uint32_t stride = 0;       // VertexBufferContext::stride_
        bool perInstance = false;       // type_ 3: D3DSTREAMSOURCE_INSTANCEDATA | 1
        std::uint32_t frequency = 1;    // type_ 2: the INDEXEDDATA repeat count (the instance count)
    };

    // ---------------------------------------------------------------------------------------------
    // Draw path

    /** The bound output (ClearTarget / SetRenderTarget2). Views are borrowed for the call. */
    struct OutputTargets
    {
        Diligent::ITextureView* renderTarget = nullptr;
        Diligent::ITextureView* depthStencil = nullptr;
        std::uint32_t width = 0;      // of the render target, else of the depth target
        std::uint32_t height = 0;
    };

    /** D3DVIEWPORT9 in floats. */
    struct ViewportDesc
    {
        float x = 0.0F, y = 0.0F, width = 0.0F, height = 0.0F, minZ = 0.0F, maxZ = 1.0F;
    };

    /**
     * How the D3D9 pixel-centre convention is reproduced (D3D9 samples pixel centres at integers,
     * D3D10+ at .5; the engine's matrices carry D3D9's -0.5 px, CD3DPrimBatcher.cpp:1814-1828):
     *   Shader:   the effect layer's vertex wrapper adds (1/W, -1/H) * w (PassBinding DrawState
     *             positionOffset); integer viewports, so it works on GLES too. The default.
     *   Viewport: the viewport origin moves by +0.5 px (D3D11, D3D12 and Vulkan take fractional
     *             origins); the wrapper adds nothing.
     *   None:     neither (for experiments: shows the half-pixel error).
     */
    enum class HalfPixelMode : std::uint8_t
    {
        Shader = 0,
        Viewport = 1,
        None = 2
    };

    struct DrawCall
    {
        PassBinding* binding = nullptr;
        std::uint32_t vertexFormatCode = 0x17U;
        std::array<VertexStreamBinding, kMaxVertexStreams> streams{};
        Diligent::IBuffer* indexBuffer = nullptr;
        bool index32 = false;
        std::uint32_t topology = 0;         // gal DrawContext::TOPOLOGY 1..5
        bool indexed = true;
        std::uint32_t count = 0;            // indices (indexed) or vertices, D3D9's primitive count converted
        std::uint32_t firstIndex = 0;       // startIndex_ / startVertex_
        std::int32_t baseVertex = 0;        // baseVertexIndex_
        std::uint32_t instanceCount = 1;
    };

    struct DrawPathStats
    {
        std::uint64_t draws = 0;
        std::uint64_t skippedNoBinding = 0;
        std::uint64_t skippedNoProgram = 0;
        std::uint64_t skippedNoPipeline = 0;
        std::uint64_t skippedCommit = 0;
        std::uint64_t skippedNoTarget = 0;
        std::uint64_t clearsFull = 0;
        std::uint64_t clearsQuad = 0;
        std::uint64_t blits = 0;
        std::uint64_t copies = 0;
        std::uint64_t psoLookups = 0;
        std::uint64_t psoCreated = 0;
        std::uint64_t psoFailed = 0;
    };

    /**
     * The immediate-context side of the Device slots. Every method takes the GpuShared lock.
     */
    class DrawPath
    {
    public:
        explicit DrawPath(std::shared_ptr<GpuShared> gpu);
        ~DrawPath();
        DrawPath(const DrawPath&) = delete;
        DrawPath& operator=(const DrawPath&) = delete;

        /** Slot 36 (ClearTarget): the output the next clears and draws go to. */
        void SetTargets(const OutputTargets& targets);
        /** Forget the bound views (Present unbinds; a resized head replaces them). */
        void InvalidateTargets();

        /** Slot 34. */
        void SetViewport(const ViewportDesc& viewport);
        void SetHalfPixelMode(HalfPixelMode mode) { halfPixel_ = mode; }

        /**
         * Slot 38. D3D9 clears the viewport rectangle (IDirect3DDevice9::Clear with no rects, and the
         * scissor rectangle when D3DRS_SCISSORTESTENABLE is set): a viewport covering the target clears
         * the views; a smaller one draws a quad that writes only the cleared aspects (m6u-DIL.txt).
         */
        void Clear(bool color, bool depth, bool stencil, std::uint32_t argb, float z, std::uint32_t stencilValue, const D3D9StateShadow& shadow);

        /** Slots 46/47. Returns false when the draw was skipped (counted in the stats). */
        bool Draw(const DrawCall& call, const D3D9StateShadow& shadow);

        /**
         * Slot 18: D3D9 StretchRect with D3DTEXF_LINEAR (D3D9Interfaces.cpp:2926). Equal sizes and
         * formats without rectangles copy; anything else draws the source rectangle into the destination
         * rectangle with a bilinear sampler. Rects are {left, top, right, bottom}; null means whole.
         */
        void StretchRect(Diligent::ITexture* source, const std::int32_t* sourceRect, Diligent::ITexture* destination,
                         const std::int32_t* destinationRect);

        [[nodiscard]] const DrawPathStats& GetStats() const { return stats_; }
        /** First failures, for the report. */
        [[nodiscard]] const std::vector<std::string>& GetMessages() const { return messages_; }

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        std::shared_ptr<GpuShared> gpu_;
        DrawPathStats stats_;
        std::vector<std::string> messages_;
        HalfPixelMode halfPixel_ = HalfPixelMode::Shader;
        ViewportDesc viewport_{};
        OutputTargets targets_{};
        bool targetsBound_ = false;

        void Note(const std::string& message);
        bool BindTargets();
    };

    /**
     * D3D9's DEPTHBIAS is a float in normalized depth units; D3D11, D3D12 and Vulkan take an integer
     * multiple of the format's minimum resolvable difference (2^-24 for D24, 2^-16 for D16; for float
     * depth the exponent depends on the primitive, so 2^-23 is the nearest constant). m6u-CRIT.txt Q2.
     */
    [[nodiscard]] std::int32_t ConvertDepthBias(float d3d9Bias, std::uint32_t diligentDepthFormat);
} // namespace gpg::gal::diligent
