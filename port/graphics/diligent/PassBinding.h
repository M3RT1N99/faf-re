#pragma once

// The contract between the Diligent effect layer (EffectsDiligent.*, which runs the portable fx front
// end and FxHlslEmitter) and the draw path (DeviceDiligent.*, PipelineDiligent.*, ResourcesDiligent.*).
//
// What D3D9 does, and who does it here:
//   - EffectTechniqueD3D9::BeginTechnique calls Device slot 48 (PipelineStateD3D9::BeginTechnique,
//     D3D9Interfaces.cpp:3955-3994, the technique-begin defaults), then ID3DXEffect::Begin with
//     D3DXFX_DONOTSAVESTATE (D3D9Interfaces.cpp:4102), so pass states stay in the device afterwards.
//     The effect layer still calls slot 48; the draw path owns the D3D9 render-state shadow.
//   - ID3DXEffect::BeginPass (D3D9Interfaces.cpp:4739) sets the pass's render states, its shaders,
//     the shader constants and the sampler states and textures of every sampler the shaders read.
//     Here BeginPass hands the draw path a PassBinding (PassSink::OnBeginPass): the render states to
//     apply to the shadow, the shaders and their resource signature, and Commit(), which uploads the
//     constants and binds the textures and samplers at draw time.
//   - D3DX applies parameters only at BeginPass (and CommitChanges): a value set inside the pass is
//     invisible until then. The binding keeps the parameter snapshot taken at BeginPass.
//
// This header names no Diligent, boost, msvc8 or wx type beyond forward declarations, so both kinds of
// TU can include it: those that include Diligent's headers and those that include the engine's (the
// DiligentHost.h rule: the two sets of headers never meet in one TU).
//
// Interface version 1 (M6b). Every later change is listed in the M6b E report.

#include <cstddef>
#include <cstdint>

namespace Diligent
{
    struct IDeviceContext;
    struct IPipelineResourceSignature;
    struct IRenderDevice;
    struct IShader;
    struct ITextureView;
} // namespace Diligent

namespace gpg::gal::diligent
{
    constexpr std::uint32_t kPassBindingInterfaceVersion = 1U;

    // ---------------------------------------------------------------------------------------------
    // Vertex inputs.
    //
    // The generated vertex shaders read their inputs as ATTRIB<n>, Diligent's LayoutElement default
    // (HLSLSemantic "ATTRIB" + InputIndex on D3D11; on GL and Vulkan InputIndex is the attribute
    // location). Every D3D9 usage the 24 vertex formats use (VertexFormatTableD3D9.inl) has a fixed
    // slot, so an input layout depends on the vertex format alone. 15 slots, under Diligent's
    // MAX_LAYOUT_ELEMENTS (16, InputLayout.h:40) and GLES 3.0's 16 attributes.
    enum VertexAttributeSlot : std::uint32_t
    {
        kAttribPosition = 0U,      // POSITION0, and POSITIONT0 (no format has both)
        kAttribPosition1 = 1U,     // POSITION1
        kAttribNormal = 2U,        // NORMAL0
        kAttribTangent = 3U,       // TANGENT0
        kAttribBinormal = 4U,      // BINORMAL0
        kAttribBlendIndices = 5U,  // BLENDINDICES0
        kAttribColor0 = 6U,        // COLOR0
        kAttribTexcoord0 = 7U,     // TEXCOORD0..7 are slots 7..14
        kAttribSlotCount = 15U
    };

    /**
     * The slot of a D3D9 vertex element (D3DDECLUSAGE_* and its usage index, d3d9types.h), or -1 when
     * no slot exists (the element is then not fed to the vertex shader).
     */
    constexpr int VertexAttributeSlotOf(const std::uint32_t d3dDeclUsage, const std::uint32_t usageIndex)
    {
        switch (d3dDeclUsage) {
        case 0U: // D3DDECLUSAGE_POSITION
            return usageIndex == 0U ? static_cast<int>(kAttribPosition) : usageIndex == 1U ? static_cast<int>(kAttribPosition1) : -1;
        case 9U: // D3DDECLUSAGE_POSITIONT
            return usageIndex == 0U ? static_cast<int>(kAttribPosition) : -1;
        case 3U: // D3DDECLUSAGE_NORMAL
            return usageIndex == 0U ? static_cast<int>(kAttribNormal) : -1;
        case 6U: // D3DDECLUSAGE_TANGENT
            return usageIndex == 0U ? static_cast<int>(kAttribTangent) : -1;
        case 7U: // D3DDECLUSAGE_BINORMAL
            return usageIndex == 0U ? static_cast<int>(kAttribBinormal) : -1;
        case 2U: // D3DDECLUSAGE_BLENDINDICES
            return usageIndex == 0U ? static_cast<int>(kAttribBlendIndices) : -1;
        case 10U: // D3DDECLUSAGE_COLOR
            return usageIndex == 0U ? static_cast<int>(kAttribColor0) : -1;
        case 5U: // D3DDECLUSAGE_TEXCOORD
            return usageIndex < 8U ? static_cast<int>(kAttribTexcoord0 + usageIndex) : -1;
        default:
            return -1;
        }
    }

    /**
     * How the input layout feeds a slot, which decides how the generated vertex shader declares and
     * converts it so the shader sees what D3D9 gives a vs_1_1/vs_2_0 input register:
     *   Float     D3DDECLTYPE_FLOAT1..4, FLOAT16_2/4: Diligent VT_FLOAT32 (or VT_FLOAT16) x n. The input
     *             assembler fills missing components with (0, 0, 0, 1), as D3D9 does.
     *   UNormBgra D3DDECLTYPE_D3DCOLOR: VT_UINT8 x 4, normalized. The bytes in memory are B, G, R, A
     *             (D3DCOLOR is 0xAARRGGBB); Diligent has no BGRA vertex type (InputLayout.h:68-122), so
     *             the shader swizzles .bgra to D3D9's (R, G, B, A).
     *   UInt      D3DDECLTYPE_UBYTE4: VT_UINT8 x 4, not normalized; D3D9 gives 0..255 as floats, D3D11
     *             has no USCALED format, so the shader converts.
     *   SInt      D3DDECLTYPE_SHORT2/4: VT_INT16 x n, not normalized; converted like UInt.
     *   PositionT D3DDECLUSAGE_POSITIONT (slot 0 only): pre-transformed render-target pixels. D3D9 does
     *             no vertex processing for such vertices, so the effect layer draws them with a
     *             generated pass-through vertex shader whatever the pass's VertexShader is.
     * Normalized types (UBYTE4N, SHORT2N, ...) are Float with Diligent's IsNormalized = true.
     */
    enum class AttribKind : std::uint8_t
    {
        Absent = 0U,
        Float = 1U,
        UNormBgra = 2U,
        UInt = 3U,
        SInt = 4U,
        PositionT = 5U
    };

    /**
     * The vertex inputs the current vertex format provides, per slot. The draw path fills it from the
     * format's D3D9 elements (VertexFormatTableD3D9.inl) with VertexAttributeSlotOf. A slot the vertex
     * shader reads but the format lacks gets the constant (0, 0, 0, 1) inside the shader, so every
     * input the shader declares is in the layout (D3D11 and Vulkan reject a missing one; D3D9 leaves it
     * to the driver).
     */
    struct VertexInputDesc
    {
        AttribKind kinds[kAttribSlotCount] = {};
    };

    // ---------------------------------------------------------------------------------------------
    // The pass.

    /** One state ID3DXEffect::BeginPass sets, in D3DX's order (FxMetadata PassInfo::states). */
    struct PassStateAssignment
    {
        enum Op : std::uint32_t
        {
            Render = 0U,        // IDirect3DDevice9::SetRenderState(state, value)
            TextureStage = 1U   // SetTextureStageState(index, state, value)
        };
        std::uint32_t op = Render;
        std::uint32_t index = 0U;  // texture stage
        std::uint32_t state = 0U;  // D3DRENDERSTATETYPE / D3DTEXTURESTAGESTATETYPE
        std::uint32_t value = 0U;  // the DWORD D3DX passes (floats as their bit pattern)
    };
    // Sampler states and textures are not in the list: the effect layer realises them as the immutable
    // samplers of the pass's resource signature and as its texture variables.

    /** The compiled shaders of a pass for one vertex input layout. Owned by the effect layer. */
    struct PassProgram
    {
        Diligent::IShader* vertexShader = nullptr;
        Diligent::IShader* pixelShader = nullptr;
        /** The PSO's only resource signature (PipelineStateCreateInfo::ppResourceSignatures). */
        Diligent::IPipelineResourceSignature* signature = nullptr;
        /**
         * Unique per (vertex shader, pixel shader, signature) in the process and never reused, so it
         * can stand for all three in a PSO cache key.
         */
        std::uint64_t id = 0U;
        /** Number of colour outputs the pixel shader writes (SV_Target0..n-1). */
        std::uint32_t renderTargetCount = 1U;
    };

    /** What the generated wrappers need from the draw path's state at each draw. */
    struct DrawState
    {
        /** The D3D9 viewport (D3DVIEWPORT9 X, Y, Width, Height in pixels, MinZ, MaxZ). */
        float viewport[6] = {0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 1.0F};
        /**
         * Added to the clip-space position after the vertex shader, times w. D3D9 puts pixel centres
         * on integer coordinates, D3D10 and later on .5, and the engine's matrices carry D3D9's
         * offset (CD3DPrimBatcher.cpp:1818-1826). (1 / Width, -1 / Height) moves every vertex by half
         * a pixel right and down, which gives D3D9's coverage and interpolation with an integer
         * viewport on every API (GLES viewports are integers). (0, 0) if the draw path shifts the
         * viewport itself.
         */
        float positionOffset[2] = {0.0F, 0.0F};
        /** D3DRS_ALPHATESTENABLE (15), D3DRS_ALPHAFUNC (25, a D3DCMPFUNC) and D3DRS_ALPHAREF (24). */
        std::uint32_t alphaTestEnable = 0U;
        std::uint32_t alphaFunc = 8U;
        std::uint32_t alphaRef = 0U;
        /**
         * Increments once per Present. Dynamic buffers are written again on their first use in a
         * frame: Vulkan and D3D12 discard dynamic memory at the end of each frame
         * (DeviceContextVkImpl.cpp:2432-2433).
         */
        std::uint64_t frameIndex = 0U;
    };

    /**
     * One pass of one technique between EffectTechnique::BeginPass and EndPass. Owned by the effect
     * layer; valid until OnEndPass.
     */
    class PassBinding
    {
    public:
        [[nodiscard]] virtual const char* GetEffectName() const = 0;     // "primbatcher"
        [[nodiscard]] virtual const char* GetTechniqueName() const = 0;  // "TAlphaBlendLinearSampleNoDepth"
        [[nodiscard]] virtual std::uint32_t GetPassIndex() const = 0;

        /** The render and texture-stage states the pass sets, in D3DX's order. */
        [[nodiscard]] virtual const PassStateAssignment* GetStates(std::size_t* count) const = 0;

        /**
         * The pass's shaders for this vertex layout, compiled on first use (Diligent's compiler for the
         * device's API, at load time from the effect source). Returns false when the pass cannot draw
         * (a shader failed to generate or compile; logged once): skip the draw.
         */
        virtual bool GetProgram(const VertexInputDesc& input, PassProgram* out) = 0;

        /**
         * Before each draw, after IDeviceContext::SetPipelineState with a PSO made from GetProgram's
         * signature: writes the constant buffers (Map with MAP_FLAG_DISCARD, never UpdateBuffer),
         * binds the textures, then calls CommitShaderResources with RESOURCE_STATE_TRANSITION_MODE_TRANSITION.
         * Returns false when there is nothing valid to commit (skip the draw).
         */
        virtual bool Commit(Diligent::IDeviceContext* context, const DrawState& state) = 0;

    protected:
        ~PassBinding() = default;
    };

    /** Implemented by the draw path; the effect layer calls it from EffectTechnique::BeginPass/EndPass. */
    class PassSink
    {
    public:
        /** Apply binding.GetStates() to the D3D9 render-state shadow now, then draw with the binding. */
        virtual void OnBeginPass(PassBinding& binding) = 0;
        /** The binding is no longer valid; the states it applied stay (D3DXFX_DONOTSAVESTATE). */
        virtual void OnEndPass(PassBinding& binding) = 0;

    protected:
        ~PassSink() = default;
    };

    /** Registers the draw path's sink (nullptr to unregister). Implemented by the effect layer. */
    void SetPassSink(PassSink* sink);

    /**
     * Hands the effect layer the device it creates shaders, buffers and signatures on. Call after
     * creating the device, and with nullptr before releasing it: the effect layer then releases every
     * GPU object it made (and makes them again on the next device). Implemented by the effect layer.
     */
    void SetEffectRenderDevice(Diligent::IRenderDevice* device);

    // ---------------------------------------------------------------------------------------------
    // Textures for effect variables.

    enum class ShaderResourceDimension : std::uint8_t
    {
        Texture2D = 0U,
        Texture3D = 1U,
        TextureCube = 2U
    };

    /**
     * Implemented by the draw path's gal Texture, RenderTarget and CubeRenderTarget classes (as a
     * second base class). EffectVariable::SetTexture/SetRenderTarget/SetCubeRenderTarget keep the gal
     * object and find this interface with dynamic_cast; Commit asks for the view at each draw, so a
     * texture whose GPU object is made or remade later is still bound correctly.
     */
    class ShaderResourceSource
    {
    public:
        /** A shader resource view of the whole texture, or nullptr if there is nothing to sample yet. */
        [[nodiscard]] virtual Diligent::ITextureView* GetShaderResourceView() = 0;
        [[nodiscard]] virtual ShaderResourceDimension GetShaderResourceDimension() const = 0;

    protected:
        ~ShaderResourceSource() = default;
    };
} // namespace gpg::gal::diligent
