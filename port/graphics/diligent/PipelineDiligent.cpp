#include "PipelineDiligent.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <type_traits>
#include <unordered_map>

#include "Common/interface/RefCntAutoPtr.hpp"
#include "DiligentHost.h"
#include "ShaderCompileDiligent.h"
#include "Graphics/GraphicsEngine/interface/Buffer.h"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/PipelineState.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/Sampler.h"
#include "Graphics/GraphicsEngine/interface/Shader.h"
#include "Graphics/GraphicsEngine/interface/ShaderResourceBinding.h"
#include "Graphics/GraphicsEngine/interface/Texture.h"
#include "Graphics/GraphicsEngine/interface/TextureView.h"

namespace dg = Diligent;

namespace gpg::gal::diligent
{
    namespace
    {
        std::uint32_t FloatBits(const float value)
        {
            return std::bit_cast<std::uint32_t>(value);
        }

        struct StateValue
        {
            std::uint32_t state;
            std::uint32_t value;
        };

        // D3D9's creation value of every render state (the D3DRENDERSTATETYPE reference). ZENABLE starts
        // FALSE because the D3D9 backend creates its device without an automatic depth buffer
        // (EnableAutoDepthStencil = 0, D3D9Interfaces.cpp:1926). States not listed start at 0.
        const StateValue kD3D9CreationDefaults[] = {
            {7, 0},           // ZENABLE (no auto depth-stencil)
            {8, 3},           // FILLMODE SOLID
            {9, 2},           // SHADEMODE GOURAUD
            {14, 1},          // ZWRITEENABLE
            {16, 1},          // LASTPIXEL
            {19, 2},          // SRCBLEND ONE
            {20, 1},          // DESTBLEND ZERO
            {22, 3},          // CULLMODE CCW
            {23, 4},          // ZFUNC LESSEQUAL
            {25, 8},          // ALPHAFUNC ALWAYS
            {37, 0x3F800000}, // FOGEND 1.0
            {38, 0x3F800000}, // FOGDENSITY 1.0
            {53, 1},          // STENCILFAIL KEEP
            {54, 1},          // STENCILZFAIL KEEP
            {55, 1},          // STENCILPASS KEEP
            {56, 8},          // STENCILFUNC ALWAYS
            {58, 0xFFFFFFFF}, // STENCILMASK
            {59, 0xFFFFFFFF}, // STENCILWRITEMASK
            {60, 0xFFFFFFFF}, // TEXTUREFACTOR
            {136, 1},         // CLIPPING
            {137, 1},         // LIGHTING
            {141, 1},         // COLORVERTEX
            {142, 1},         // LOCALVIEWER
            {145, 1},         // DIFFUSEMATERIALSOURCE COLOR1
            {146, 2},         // SPECULARMATERIALSOURCE COLOR2
            {154, 0x3F800000},// POINTSIZE 1.0
            {155, 0x3F800000},// POINTSIZE_MIN 1.0
            {158, 0x3F800000},// POINTSCALE_A 1.0
            {161, 1},         // MULTISAMPLEANTIALIAS
            {162, 0xFFFFFFFF},// MULTISAMPLEMASK
            {166, 0x42800000},// POINTSIZE_MAX 64.0
            {168, 0x0F},      // COLORWRITEENABLE
            {171, 1},         // BLENDOP ADD
            {172, 3},         // POSITIONDEGREE CUBIC
            {173, 1},         // NORMALDEGREE LINEAR
            {178, 0x3F800000},// MINTESSELLATIONLEVEL 1.0
            {179, 0x3F800000},// MAXTESSELLATIONLEVEL 1.0
            {182, 0x3F800000},// ADAPTIVETESS_Z 1.0
            {186, 1},         // CCW_STENCILFAIL KEEP
            {187, 1},         // CCW_STENCILZFAIL KEEP
            {188, 1},         // CCW_STENCILPASS KEEP
            {189, 8},         // CCW_STENCILFUNC ALWAYS
            {190, 0x0F},      // COLORWRITEENABLE1
            {191, 0x0F},      // COLORWRITEENABLE2
            {192, 0x0F},      // COLORWRITEENABLE3
            {193, 0xFFFFFFFF},// BLENDFACTOR
            {207, 2},         // SRCBLENDALPHA ONE
            {208, 1},         // DESTBLENDALPHA ZERO
            {209, 1},         // BLENDOPALPHA ADD
        };

        // PipelineStateD3D9::InitState's render-state tables, verbatim (D3D9Interfaces.cpp:5941-6051,
        // binary 0x00945730).
        const StateValue kInitStateRender[] = {
            {7U, 1U},   {8U, 3U},   {9U, 2U},   {14U, 1U},  {15U, 0U},  {16U, 1U},  {19U, 2U},  {20U, 1U},
            {22U, 1U},  {23U, 8U},  {24U, 0U},  {25U, 8U},  {26U, 0U},  {27U, 0U},  {28U, 0U},  {29U, 0U},
            {34U, 0U},  {35U, 0U},  {48U, 0U},  {52U, 0U},  {53U, 1U},  {54U, 1U},  {55U, 1U},  {56U, 8U},
            {57U, 0U},  {58U, 0xFFFFFFFFU}, {59U, 0xFFFFFFFFU}, {60U, 0U}, {128U, 0U}, {129U, 0U}, {130U, 0U},
            {131U, 0U}, {132U, 0U}, {133U, 0U}, {134U, 0U}, {135U, 0U}, {136U, 1U}, {137U, 1U}, {139U, 0U},
            {140U, 0U}, {141U, 1U}, {142U, 0U}, {143U, 1U}, {145U, 1U}, {146U, 2U}, {147U, 0U}, {148U, 0U},
            {151U, 0U}, {152U, 0U}, {156U, 0U}, {157U, 0U}, {161U, 1U}, {162U, 0xFFFFFFFFU}, {163U, 0U},
            {167U, 0U}, {168U, 0x0FU}, {171U, 1U}, {172U, 3U}, {173U, 1U}, {174U, 0U}, {176U, 0U}, {184U, 0U},
            {185U, 0U}, {186U, 1U}, {187U, 1U}, {188U, 1U}, {189U, 8U}, {190U, 0x0FU}, {191U, 0x0FU},
            {192U, 0x0FU}, {193U, 0xFFFFFFFFU}, {194U, 0U}, {198U, 0U}, {199U, 0U}, {200U, 0U}, {201U, 0U},
            {202U, 0U}, {203U, 0U}, {204U, 0U}, {205U, 0U}, {206U, 0U}, {207U, 2U}, {208U, 1U}, {209U, 1U},
        };
        struct StateFloat
        {
            std::uint32_t state;
            float value;
        };
        const StateFloat kInitStateFloat[] = {
            {36U, 0.0f},  {37U, 1.0f},  {38U, 1.0f},  {154U, 1.0f}, {155U, 1.0f}, {158U, 1.0f},
            {159U, 0.0f}, {160U, 0.0f}, {166U, 64.0f}, {170U, 0.0f}, {175U, 0.0f}, {178U, 1.0f},
            {179U, 1.0f}, {180U, 1.0f}, {181U, 1.0f}, {182U, 1.0f}, {183U, 1.0f}, {195U, 0.0f},
        };

        // D3DBLEND -> BLEND_FACTOR. 1..11 have the same numbering (ZERO .. SRCALPHASAT); BLENDFACTOR and
        // INVBLENDFACTOR are 14/15 in D3D9 and 12/13 in Diligent. BOTHSRCALPHA and BOTHINVSRCALPHA
        // (12/13) set both factors and are resolved by the caller.
        dg::BLEND_FACTOR MapBlend(const std::uint32_t d3dBlend)
        {
            if (d3dBlend >= 1U && d3dBlend <= 11U) {
                return static_cast<dg::BLEND_FACTOR>(d3dBlend);
            }
            switch (d3dBlend) {
            case 14U:
                return dg::BLEND_FACTOR_BLEND_FACTOR;
            case 15U:
                return dg::BLEND_FACTOR_INV_BLEND_FACTOR;
            case 16U: // SRCCOLOR2 (dual source)
                return dg::BLEND_FACTOR_SRC1_COLOR;
            case 17U:
                return dg::BLEND_FACTOR_INV_SRC1_COLOR;
            default:
                return dg::BLEND_FACTOR_ONE;
            }
        }

        // D3D9 applies a colour factor to alpha as its alpha component; D3D11 refuses colour factors in
        // the alpha slots (D3D11_RENDER_TARGET_BLEND_DESC), so they become their alpha equivalents
        // (m6u-FX.txt section 6m).
        dg::BLEND_FACTOR ToAlphaFactor(const dg::BLEND_FACTOR factor)
        {
            switch (factor) {
            case dg::BLEND_FACTOR_SRC_COLOR:
                return dg::BLEND_FACTOR_SRC_ALPHA;
            case dg::BLEND_FACTOR_INV_SRC_COLOR:
                return dg::BLEND_FACTOR_INV_SRC_ALPHA;
            case dg::BLEND_FACTOR_DEST_COLOR:
                return dg::BLEND_FACTOR_DEST_ALPHA;
            case dg::BLEND_FACTOR_INV_DEST_COLOR:
                return dg::BLEND_FACTOR_INV_DEST_ALPHA;
            case dg::BLEND_FACTOR_SRC1_COLOR:
                return dg::BLEND_FACTOR_SRC1_ALPHA;
            case dg::BLEND_FACTOR_INV_SRC1_COLOR:
                return dg::BLEND_FACTOR_INV_SRC1_ALPHA;
            default:
                return factor;
            }
        }

        dg::BLEND_OPERATION MapBlendOp(const std::uint32_t d3dOp)
        {
            // D3DBLENDOP ADD..MAX = 1..5, as BLEND_OPERATION_ADD..MAX.
            return (d3dOp >= 1U && d3dOp <= 5U) ? static_cast<dg::BLEND_OPERATION>(d3dOp) : dg::BLEND_OPERATION_ADD;
        }

        dg::COMPARISON_FUNCTION MapCompare(const std::uint32_t d3dCmp)
        {
            // D3DCMP_NEVER..ALWAYS = 1..8, as COMPARISON_FUNC_NEVER..ALWAYS.
            return (d3dCmp >= 1U && d3dCmp <= 8U) ? static_cast<dg::COMPARISON_FUNCTION>(d3dCmp) : dg::COMPARISON_FUNC_ALWAYS;
        }

        dg::STENCIL_OP MapStencilOp(const std::uint32_t d3dOp)
        {
            // D3DSTENCILOP_KEEP..DECR = 1..8, as STENCIL_OP_KEEP..DECR_WRAP.
            return (d3dOp >= 1U && d3dOp <= 8U) ? static_cast<dg::STENCIL_OP>(d3dOp) : dg::STENCIL_OP_KEEP;
        }

        bool FormatHasStencil(const dg::TEXTURE_FORMAT format)
        {
            return format == dg::TEX_FORMAT_D24_UNORM_S8_UINT || format == dg::TEX_FORMAT_D32_FLOAT_S8X24_UINT;
        }

        // gal DrawContext::TOPOLOGY 1..5 (kTopologyPrimitiveTypes, D3D9Interfaces.cpp:192-199; no fans).
        dg::PRIMITIVE_TOPOLOGY MapTopology(const std::uint32_t topology)
        {
            switch (topology) {
            case 1U:
                return dg::PRIMITIVE_TOPOLOGY_POINT_LIST;
            case 2U:
                return dg::PRIMITIVE_TOPOLOGY_LINE_LIST;
            case 3U:
                return dg::PRIMITIVE_TOPOLOGY_LINE_STRIP;
            case 5U:
                return dg::PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            default:
                return dg::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            }
        }

        /**
         * The PSO-relevant part of the shadow, normalised so that states the pipeline ignores do not
         * split the cache (blend factors with blending off, stencil ops with stencil off, depth func with
         * depth off). Bytes and 32-bit words only, so the struct hashes as bytes.
         */
        struct FixedState
        {
            std::uint8_t blendEnable = 0;
            std::uint8_t srcBlend = 0, destBlend = 0, blendOp = 0;
            std::uint8_t srcBlendAlpha = 0, destBlendAlpha = 0, blendOpAlpha = 0;
            std::uint8_t writeMask = 0;
            std::uint8_t depthEnable = 0, depthWrite = 0, depthFunc = 0;
            std::uint8_t stencilEnable = 0, stencilReadMask = 0, stencilWriteMask = 0;
            std::uint8_t frontFail = 0, frontDepthFail = 0, frontPass = 0, frontFunc = 0;
            std::uint8_t backFail = 0, backDepthFail = 0, backPass = 0, backFunc = 0;
            std::uint8_t fillMode = 0, cullMode = 0, scissor = 0, antialiasedLine = 0;
            // The int32 below starts at offset 28: these two bytes would otherwise be padding, which an
            // aggregate's `{}` leaves undetermined and the struct copy carries into the key, so equal
            // states hashed and compared unequal (M6c: the lobby made one PSO 6-8 times per run).
            std::uint8_t reserved[2] = {0, 0};
            std::int32_t depthBias = 0;
            std::uint32_t slopeScaledDepthBias = 0; // float bits
        };
        static_assert(sizeof(FixedState) == 36, "FixedState must have no padding");
        static_assert(offsetof(FixedState, depthBias) == 28, "FixedState: no padding before depthBias");

        FixedState PackFixedState(const D3D9StateShadow& shadow, const dg::TEXTURE_FORMAT depthFormat)
        {
            FixedState state{};

            // Blending: D3DRS_ALPHABLENDENABLE, SRCBLEND/DESTBLEND/BLENDOP and the separate-alpha states.
            state.writeMask = static_cast<std::uint8_t>(shadow.GetRenderState(rs::ColorWriteEnable) & 0x0FU);
            if (shadow.GetRenderState(rs::AlphaBlendEnable) != 0U) {
                state.blendEnable = 1;
                const std::uint32_t src = shadow.GetRenderState(rs::SrcBlend);
                dg::BLEND_FACTOR srcFactor = MapBlend(src);
                dg::BLEND_FACTOR destFactor = MapBlend(shadow.GetRenderState(rs::DestBlend));
                // D3DBLEND_BOTHSRCALPHA / BOTHINVSRCALPHA: the source factor names both (D3DBLEND reference).
                if (src == 12U) {
                    srcFactor = dg::BLEND_FACTOR_SRC_ALPHA;
                    destFactor = dg::BLEND_FACTOR_INV_SRC_ALPHA;
                } else if (src == 13U) {
                    srcFactor = dg::BLEND_FACTOR_INV_SRC_ALPHA;
                    destFactor = dg::BLEND_FACTOR_SRC_ALPHA;
                }
                state.srcBlend = static_cast<std::uint8_t>(srcFactor);
                state.destBlend = static_cast<std::uint8_t>(destFactor);
                state.blendOp = static_cast<std::uint8_t>(MapBlendOp(shadow.GetRenderState(rs::BlendOp)));
                if (shadow.GetRenderState(rs::SeparateAlphaBlendEnable) != 0U) {
                    state.srcBlendAlpha = static_cast<std::uint8_t>(ToAlphaFactor(MapBlend(shadow.GetRenderState(rs::SrcBlendAlpha))));
                    state.destBlendAlpha = static_cast<std::uint8_t>(ToAlphaFactor(MapBlend(shadow.GetRenderState(rs::DestBlendAlpha))));
                    state.blendOpAlpha = static_cast<std::uint8_t>(MapBlendOp(shadow.GetRenderState(rs::BlendOpAlpha)));
                } else {
                    state.srcBlendAlpha = static_cast<std::uint8_t>(ToAlphaFactor(srcFactor));
                    state.destBlendAlpha = static_cast<std::uint8_t>(ToAlphaFactor(destFactor));
                    state.blendOpAlpha = state.blendOp;
                }
            }

            // Depth and stencil. Without a depth-stencil target D3D9 neither tests nor writes, and
            // Diligent refuses a PSO that enables depth with DSVFormat UNKNOWN.
            const bool hasDepth = depthFormat != dg::TEX_FORMAT_UNKNOWN;
            if (hasDepth && shadow.GetRenderState(rs::ZEnable) != 0U) {
                state.depthEnable = 1;
                state.depthWrite = shadow.GetRenderState(rs::ZWriteEnable) != 0U ? 1 : 0;
                state.depthFunc = static_cast<std::uint8_t>(MapCompare(shadow.GetRenderState(rs::ZFunc)));
            }
            if (hasDepth && FormatHasStencil(depthFormat) && shadow.GetRenderState(rs::StencilEnable) != 0U) {
                state.stencilEnable = 1;
                state.stencilReadMask = static_cast<std::uint8_t>(shadow.GetRenderState(rs::StencilMask) & 0xFFU);
                state.stencilWriteMask = static_cast<std::uint8_t>(shadow.GetRenderState(rs::StencilWriteMask) & 0xFFU);
                // Clockwise faces use the plain stencil states, counter-clockwise faces the CCW_ ones when
                // TWOSIDEDSTENCILMODE is set (D3DRS reference). The rasterizer keeps D3D9's winding
                // (FrontCounterClockwise = false), so "front" is clockwise.
                state.frontFail = static_cast<std::uint8_t>(MapStencilOp(shadow.GetRenderState(rs::StencilFail)));
                state.frontDepthFail = static_cast<std::uint8_t>(MapStencilOp(shadow.GetRenderState(rs::StencilZFail)));
                state.frontPass = static_cast<std::uint8_t>(MapStencilOp(shadow.GetRenderState(rs::StencilPass)));
                state.frontFunc = static_cast<std::uint8_t>(MapCompare(shadow.GetRenderState(rs::StencilFunc)));
                if (shadow.GetRenderState(rs::TwoSidedStencilMode) != 0U) {
                    state.backFail = static_cast<std::uint8_t>(MapStencilOp(shadow.GetRenderState(rs::CcwStencilFail)));
                    state.backDepthFail = static_cast<std::uint8_t>(MapStencilOp(shadow.GetRenderState(rs::CcwStencilZFail)));
                    state.backPass = static_cast<std::uint8_t>(MapStencilOp(shadow.GetRenderState(rs::CcwStencilPass)));
                    state.backFunc = static_cast<std::uint8_t>(MapCompare(shadow.GetRenderState(rs::CcwStencilFunc)));
                } else {
                    state.backFail = state.frontFail;
                    state.backDepthFail = state.frontDepthFail;
                    state.backPass = state.frontPass;
                    state.backFunc = state.frontFunc;
                }
            }

            // Rasterizer. D3DFILL_POINT has no D3D11 equivalent (D3D11_FILL_MODE): it rasterizes solid.
            state.fillMode = static_cast<std::uint8_t>(shadow.GetRenderState(rs::FillMode) == 2U ? dg::FILL_MODE_WIREFRAME : dg::FILL_MODE_SOLID);
            // D3DCULL_NONE = 1, CW = 2 (culls clockwise faces), CCW = 3 (culls counter-clockwise faces).
            switch (shadow.GetRenderState(rs::CullMode)) {
            case 2U:
                state.cullMode = static_cast<std::uint8_t>(dg::CULL_MODE_FRONT);
                break;
            case 3U:
                state.cullMode = static_cast<std::uint8_t>(dg::CULL_MODE_BACK);
                break;
            default:
                state.cullMode = static_cast<std::uint8_t>(dg::CULL_MODE_NONE);
                break;
            }
            state.scissor = shadow.GetRenderState(rs::ScissorTestEnable) != 0U ? 1 : 0;
            state.antialiasedLine = shadow.GetRenderState(rs::AntialiasedLineEnable) != 0U ? 1 : 0;
            if (hasDepth) {
                state.depthBias = ConvertDepthBias(shadow.GetRenderStateFloat(rs::DepthBias), static_cast<std::uint32_t>(depthFormat));
                state.slopeScaledDepthBias = shadow.GetRenderState(rs::SlopeScaleDepthBias);
            }
            return state;
        }

        // D3DDECLTYPE -> Diligent component type, count and normalisation (d3d9types.h; the D3DDECLTYPE
        // reference). D3DCOLOR is fed as R8G8B8A8_UNORM and swizzled by the vertex wrapper; UBYTE4 and
        // SHORT2/4 arrive as integers (D3D11 has no USCALED/SSCALED) and the wrapper converts them
        // (PassBinding.h AttribKind).
        struct ElementType
        {
            dg::VALUE_TYPE type;
            std::uint32_t components;
            bool normalized;
        };
        bool MapDeclType(const std::uint32_t declType, ElementType* const out)
        {
            switch (declType) {
            case 0: *out = {dg::VT_FLOAT32, 1, false}; return true; // FLOAT1
            case 1: *out = {dg::VT_FLOAT32, 2, false}; return true; // FLOAT2
            case 2: *out = {dg::VT_FLOAT32, 3, false}; return true; // FLOAT3
            case 3: *out = {dg::VT_FLOAT32, 4, false}; return true; // FLOAT4
            case 4: *out = {dg::VT_UINT8, 4, true}; return true;    // D3DCOLOR
            case 5: *out = {dg::VT_UINT8, 4, false}; return true;   // UBYTE4
            case 6: *out = {dg::VT_INT16, 2, false}; return true;   // SHORT2
            case 7: *out = {dg::VT_INT16, 4, false}; return true;   // SHORT4
            case 8: *out = {dg::VT_UINT8, 4, true}; return true;    // UBYTE4N
            case 9: *out = {dg::VT_INT16, 2, true}; return true;    // SHORT2N
            case 10: *out = {dg::VT_INT16, 4, true}; return true;   // SHORT4N
            case 11: *out = {dg::VT_UINT16, 2, true}; return true;  // USHORT2N
            case 12: *out = {dg::VT_UINT16, 4, true}; return true;  // USHORT4N
            case 15: *out = {dg::VT_FLOAT16, 2, false}; return true;// FLOAT16_2
            case 16: *out = {dg::VT_FLOAT16, 4, false}; return true;// FLOAT16_4
            default: return false;                                  // UDEC3, DEC3N: no DXGI format
            }
        }

        // FNV-1a over bytes, for the cache keys.
        std::uint64_t Hash(const void* const data, const std::size_t size, std::uint64_t hash = 0xCBF29CE484222325ULL)
        {
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            for (std::size_t index = 0; index < size; ++index) {
                hash ^= bytes[index];
                hash *= 0x100000001B3ULL;
            }
            return hash;
        }

        struct PipelineKey
        {
            std::uint64_t programId = 0;
            FixedState fixed;
            std::uint32_t renderTargetFormat = 0;
            std::uint32_t depthStencilFormat = 0;
            std::uint32_t topology = 0;
            std::uint32_t vertexFormatCode = 0;
            std::uint32_t streamStride[kMaxVertexStreams] = {};
            std::uint32_t streamInstanced = 0;
            std::uint32_t renderTargetCount = 0;

            bool operator==(const PipelineKey& other) const
            {
                return std::memcmp(this, &other, sizeof(PipelineKey)) == 0;
            }
        };
        static_assert(std::is_trivially_copyable_v<PipelineKey>, "PipelineKey hashes as bytes");

        struct PipelineKeyHash
        {
            std::size_t operator()(const PipelineKey& key) const
            {
                return static_cast<std::size_t>(Hash(&key, sizeof(key)));
            }
        };

        // Internal draws: a triangle covering the viewport from SV_VertexID (no vertex buffer).
        constexpr const char* kQuadVS = R"(
cbuffer QuadConstants { float4 g_Color; float4 g_DepthUv; float4 g_UvRect; };
struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
VSOutput main(uint vertexId : SV_VertexID)
{
    VSOutput output;
    float2 corner = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(corner * float2(2.0, -2.0) + float2(-1.0, 1.0), g_DepthUv.x, 1.0);
    output.uv = g_UvRect.xy + corner * g_UvRect.zw;
    return output;
}
)";
        constexpr const char* kClearPS = R"(
cbuffer QuadConstants { float4 g_Color; float4 g_DepthUv; float4 g_UvRect; };
struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
float4 main(VSOutput input) : SV_TARGET { return g_Color; }
)";
        constexpr const char* kBlitPS = R"(
Texture2D g_Source;
SamplerState g_Source_sampler;
struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
float4 main(VSOutput input) : SV_TARGET { return g_Source.SampleLevel(g_Source_sampler, input.uv, 0.0); }
)";

        struct QuadConstants
        {
            float color[4];
            float depthUv[4];
            float uvRect[4];
        };

        /**
         * A D3D viewport for Diligent. On GL the target is drawn mirrored (DiligentHost.h
         * FlipsRenderTargets) and Diligent's GL backend turns the top-left origin into GL's bottom-left
         * itself (DeviceContextGLImpl.cpp:251), so the rectangle goes in mirrored: then GL's bottom
         * edge is D3D's top row and memory rows stay D3D rows.
         */
        dg::Viewport TargetViewport(const float x, const float y, const float width, const float height, const float minZ, const float maxZ,
                                    const std::uint32_t targetHeight, const bool flipY)
        {
            return dg::Viewport(x, flipY ? static_cast<float>(targetHeight) - (y + height) : y, width, height, minZ, maxZ);
        }

        /** The backend's own shaders (quad, clear, blit), compiled for the device's API. */
        dg::RefCntAutoPtr<dg::IShader> CompileInternal(dg::IRenderDevice* const device, const GraphicsApi api, const dg::SHADER_TYPE type,
                                                       const char* const name, const char* const source, const bool combinedSamplers,
                                                       std::string* const messages)
        {
            HlslShaderSource request;
            request.source = source;
            request.name = name;
            request.shaderType = static_cast<std::uint32_t>(type);
            request.entryPoint = "main";
            request.combinedTextureSamplers = combinedSamplers;
            request.naming = CombinedSamplerNaming::Texture;
            dg::RefCntAutoPtr<dg::IShader> shader;
            CompiledShaderInfo info;
            if (!CompileHlslShader(device, api, request, &shader, &info) && messages != nullptr) {
                *messages += std::string(name) + ": " + info.messages + " ";
            }
            return shader;
        }
    } // namespace

    // ---------------------------------------------------------------------------------------------
    // D3D9StateShadow

    D3D9StateShadow::D3D9StateShadow()
    {
        ResetToSetupState();
    }

    void D3D9StateShadow::ResetToSetupState()
    {
        render_.fill(0U);
        for (const StateValue& entry : kD3D9CreationDefaults) {
            render_[entry.state] = entry.value;
        }
        // InitState (D3D9Interfaces.cpp:6053-6061), which DeviceD3D9::Setup runs right after creation.
        for (const StateValue& entry : kInitStateRender) {
            render_[entry.state] = entry.value;
        }
        for (const StateFloat& entry : kInitStateFloat) {
            render_[entry.state] = FloatBits(entry.value);
        }
        colorWriteEnable_ = 0x0FU;
        ++version_;
    }

    void D3D9StateShadow::SetRenderState(const std::uint32_t state, const std::uint32_t value)
    {
        if (state < render_.size() && render_[state] != value) {
            render_[state] = value;
            ++version_;
        }
    }

    void D3D9StateShadow::SetRenderStateFloat(const std::uint32_t state, const float value)
    {
        SetRenderState(state, FloatBits(value));
    }

    std::uint32_t D3D9StateShadow::GetRenderState(const std::uint32_t state) const
    {
        return state < render_.size() ? render_[state] : 0U;
    }

    float D3D9StateShadow::GetRenderStateFloat(const std::uint32_t state) const
    {
        return std::bit_cast<float>(GetRenderState(state));
    }

    void D3D9StateShadow::ApplyPassStates(const PassStateAssignment* const states, const std::size_t count)
    {
        for (std::size_t index = 0; index < count; ++index) {
            if (states[index].op == PassStateAssignment::Render) {
                SetRenderState(states[index].state, states[index].value);
            }
        }
    }

    void D3D9StateShadow::BeginTechnique()
    {
        // 0x00946260, in its order.
        SetRenderState(rs::ColorWriteEnable, colorWriteEnable_);
        SetRenderState(rs::AlphaBlendEnable, 0U);
        SetRenderState(rs::AlphaTestEnable, 0U);
        SetRenderState(rs::StencilEnable, 0U);
        SetRenderState(rs::ZEnable, 1U);
        SetRenderState(rs::ZFunc, 4U); // D3DCMP_LESSEQUAL
        SetRenderState(rs::ZWriteEnable, 1U);
        SetRenderState(rs::DepthBias, 0U);
        SetRenderState(rs::CullMode, 1U); // D3DCULL_NONE
    }

    void D3D9StateShadow::SetColorWriteState(const bool writeColor, const bool writeAlpha)
    {
        if (writeColor) {
            colorWriteEnable_ = writeAlpha ? 0x0FU : 0x07U;
        } else {
            colorWriteEnable_ = writeAlpha ? 0x08U : 0x0FU;
        }
        SetRenderState(rs::ColorWriteEnable, colorWriteEnable_);
    }

    void D3D9StateShadow::SetWireframeState(const bool enabled)
    {
        SetRenderState(rs::FillMode, enabled ? 2U : 3U); // D3DFILL_WIREFRAME : D3DFILL_SOLID
    }

    void D3D9StateShadow::SetFogState(const bool enable, const float fogStart, const float fogEnd, const std::uint32_t fogColor)
    {
        if (!enable) {
            SetRenderState(rs::FogEnable, 0U);
            return;
        }
        SetRenderState(rs::FogEnable, 1U);
        SetRenderState(rs::RangeFogEnable, 1U);
        SetRenderState(rs::FogColor, fogColor);
        SetRenderState(rs::FogTableMode, 3U); // D3DFOG_LINEAR
        SetRenderStateFloat(rs::FogStart, fogStart);
        SetRenderStateFloat(rs::FogEnd, fogEnd);
    }

    std::int32_t ConvertDepthBias(const float d3d9Bias, const std::uint32_t diligentDepthFormat)
    {
        if (d3d9Bias == 0.0f) {
            return 0;
        }
        double unitsPerOne = 16777216.0; // 2^24: D24
        switch (static_cast<dg::TEXTURE_FORMAT>(diligentDepthFormat)) {
        case dg::TEX_FORMAT_D16_UNORM:
            unitsPerOne = 65536.0;
            break;
        case dg::TEX_FORMAT_D32_FLOAT:
        case dg::TEX_FORMAT_D32_FLOAT_S8X24_UINT:
            unitsPerOne = 8388608.0; // 2^23
            break;
        default:
            break;
        }
        return static_cast<std::int32_t>(std::lround(static_cast<double>(d3d9Bias) * unitsPerOne));
    }

    VertexInputDesc GetVertexInputDesc(const std::uint32_t formatCode)
    {
        VertexInputDesc desc;
        for (const VertexInputElement& element : GetVertexInputElements(formatCode)) {
            if (element.attribute < 0) {
                continue;
            }
            AttribKind kind = AttribKind::Float;
            if (element.d3dUsage == 9U) { // D3DDECLUSAGE_POSITIONT
                kind = AttribKind::PositionT;
            } else if (element.d3dDeclType == 4U) { // D3DCOLOR
                kind = AttribKind::UNormBgra;
            } else if (element.d3dDeclType == 5U) { // UBYTE4
                kind = AttribKind::UInt;
            } else if (element.d3dDeclType == 6U || element.d3dDeclType == 7U) { // SHORT2, SHORT4
                kind = AttribKind::SInt;
            }
            ElementType type{};
            if (!MapDeclType(element.d3dDeclType, &type)) {
                continue; // UDEC3/DEC3N: never in the table
            }
            desc.kinds[element.attribute] = kind;
        }
        return desc;
    }

    // ---------------------------------------------------------------------------------------------
    // DrawPath

    struct DrawPath::Impl
    {
        std::unordered_map<PipelineKey, dg::RefCntAutoPtr<dg::IPipelineState>, PipelineKeyHash> pipelines;
        std::array<bool, 32> inputDescValid{};
        std::array<VertexInputDesc, 32> inputDescs{};
        dg::IPipelineState* currentPipeline = nullptr;
        std::uint64_t nextName = 0;

        // Internal pipelines (clear quads, blits), keyed by formats and write masks.
        std::unordered_map<std::uint64_t, dg::RefCntAutoPtr<dg::IPipelineState>> clearPipelines;
        std::unordered_map<std::uint64_t, dg::RefCntAutoPtr<dg::IShaderResourceBinding>> clearBindings;
        std::unordered_map<std::uint32_t, dg::RefCntAutoPtr<dg::IPipelineState>> blitPipelines;
        std::unordered_map<std::uint32_t, dg::RefCntAutoPtr<dg::IShaderResourceBinding>> blitBindings;
        dg::RefCntAutoPtr<dg::IShader> quadVS;
        dg::RefCntAutoPtr<dg::IShader> clearPS;
        dg::RefCntAutoPtr<dg::IShader> blitPS;
        dg::RefCntAutoPtr<dg::IBuffer> quadConstants;
    };

    DrawPath::DrawPath(std::shared_ptr<GpuShared> gpu)
        : impl_(std::make_unique<Impl>()),
          gpu_(std::move(gpu))
    {}

    DrawPath::~DrawPath() = default;

    void DrawPath::Note(const std::string& message)
    {
        if (messages_.size() < 32) {
            messages_.push_back(message);
        }
    }

    void DrawPath::SetTargets(const OutputTargets& targets)
    {
        targets_ = targets;
        targetsBound_ = false;
    }

    void DrawPath::InvalidateTargets()
    {
        targets_ = OutputTargets{};
        targetsBound_ = false;
        impl_->currentPipeline = nullptr;
    }

    void DrawPath::SetViewport(const ViewportDesc& viewport)
    {
        viewport_ = viewport;
    }

    bool DrawPath::BindTargets()
    {
        if (targets_.renderTarget == nullptr && targets_.depthStencil == nullptr) {
            return false;
        }
        if (!targetsBound_) {
            dg::ITextureView* renderTarget = targets_.renderTarget;
            gpu_->Context()->SetRenderTargets(renderTarget != nullptr ? 1 : 0, renderTarget != nullptr ? &renderTarget : nullptr,
                                              targets_.depthStencil, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            targetsBound_ = true;
        }
        return true;
    }

    void DrawPath::Clear(
        const bool color,
        const bool depth,
        const bool stencil,
        const std::uint32_t argb,
        const float z,
        const std::uint32_t stencilValue,
        const D3D9StateShadow& shadow
    )
    {
        static_cast<void>(shadow);
        std::lock_guard<std::recursive_mutex> lock(gpu_->Lock());
        if (!BindTargets()) {
            return;
        }
        dg::IDeviceContext* const context = gpu_->Context();
        // D3DCOLOR is A8R8G8B8; the clear converts it to the target's format. For UNORM8 c / 255 is exact.
        const float rgba[4] = {
            static_cast<float>((argb >> 16U) & 0xFFU) / 255.0f,
            static_cast<float>((argb >> 8U) & 0xFFU) / 255.0f,
            static_cast<float>(argb & 0xFFU) / 255.0f,
            static_cast<float>((argb >> 24U) & 0xFFU) / 255.0f,
        };
        const bool clearColor = color && targets_.renderTarget != nullptr;
        dg::ITextureView* const depthView = targets_.depthStencil;
        bool clearDepth = depth && depthView != nullptr;
        bool clearStencil = stencil && depthView != nullptr &&
                            FormatHasStencil(depthView->GetTexture()->GetDesc().Format);
        if (!clearColor && !clearDepth && !clearStencil) {
            return;
        }

        const bool coversTarget = viewport_.x <= 0.0F && viewport_.y <= 0.0F &&
                                  viewport_.x + viewport_.width >= static_cast<float>(targets_.width) &&
                                  viewport_.y + viewport_.height >= static_cast<float>(targets_.height);
        if (coversTarget) {
            if (clearColor) {
                context->ClearRenderTarget(targets_.renderTarget, rgba, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            }
            if (clearDepth || clearStencil) {
                dg::CLEAR_DEPTH_STENCIL_FLAGS flags = dg::CLEAR_DEPTH_FLAG_NONE;
                if (clearDepth) {
                    flags |= dg::CLEAR_DEPTH_FLAG;
                }
                if (clearStencil) {
                    flags |= dg::CLEAR_STENCIL_FLAG;
                }
                context->ClearDepthStencil(depthView, flags, z, static_cast<dg::Uint8>(stencilValue & 0xFFU),
                                           dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            }
            ++stats_.clearsFull;
            return;
        }

        // A viewport smaller than the target: a quad over the viewport that writes exactly the cleared
        // aspects. D3D9's Clear ignores the blend, depth, stencil and colour-write states, so the quad's
        // pipeline sets them itself; depth is written as given (viewport depth range 0..1 for the draw).
        const dg::TEXTURE_FORMAT colorFormat = clearColor ? targets_.renderTarget->GetDesc().Format : dg::TEX_FORMAT_UNKNOWN;
        const dg::TEXTURE_FORMAT depthFormat = depthView != nullptr ? depthView->GetDesc().Format : dg::TEX_FORMAT_UNKNOWN;
        const std::uint64_t key = (static_cast<std::uint64_t>(colorFormat) << 32U) | (static_cast<std::uint64_t>(depthFormat) << 8U) |
                                  (clearColor ? 1U : 0U) | (clearDepth ? 2U : 0U) | (clearStencil ? 4U : 0U) |
                                  (targets_.renderTarget != nullptr ? 8U : 0U);
        dg::IRenderDevice* const device = gpu_->Device();
        if (!impl_->quadConstants) {
            dg::BufferDesc desc;
            desc.Name = "gal quad constants";
            desc.Size = sizeof(QuadConstants);
            desc.Usage = dg::USAGE_DYNAMIC;
            desc.BindFlags = dg::BIND_UNIFORM_BUFFER;
            desc.CPUAccessFlags = dg::CPU_ACCESS_WRITE;
            device->CreateBuffer(desc, nullptr, &impl_->quadConstants);
        }
        ScopedDefaultFpu fpu;
        if (!impl_->quadVS) {
            std::string messages;
            impl_->quadVS = CompileInternal(device, gpu_->Api(), dg::SHADER_TYPE_VERTEX, "gal quad VS", kQuadVS, false, &messages);
            impl_->clearPS = CompileInternal(device, gpu_->Api(), dg::SHADER_TYPE_PIXEL, "gal clear PS", kClearPS, false, &messages);
            if (!messages.empty()) {
                Note(messages);
            }
        }
        auto found = impl_->clearPipelines.find(key);
        if (found == impl_->clearPipelines.end()) {
            dg::GraphicsPipelineStateCreateInfo info;
            info.PSODesc.Name = "gal clear quad";
            info.PSODesc.PipelineType = dg::PIPELINE_TYPE_GRAPHICS;
            dg::GraphicsPipelineDesc& graphics = info.GraphicsPipeline;
            graphics.NumRenderTargets = targets_.renderTarget != nullptr ? 1 : 0;
            graphics.RTVFormats[0] = targets_.renderTarget != nullptr ? targets_.renderTarget->GetDesc().Format : dg::TEX_FORMAT_UNKNOWN;
            graphics.DSVFormat = depthFormat;
            graphics.PrimitiveTopology = dg::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            graphics.RasterizerDesc.CullMode = dg::CULL_MODE_NONE;
            graphics.BlendDesc.RenderTargets[0].RenderTargetWriteMask = clearColor ? dg::COLOR_MASK_ALL : dg::COLOR_MASK_NONE;
            graphics.DepthStencilDesc.DepthEnable = clearDepth;
            graphics.DepthStencilDesc.DepthWriteEnable = clearDepth;
            graphics.DepthStencilDesc.DepthFunc = dg::COMPARISON_FUNC_ALWAYS;
            graphics.DepthStencilDesc.StencilEnable = clearStencil;
            graphics.DepthStencilDesc.StencilWriteMask = 0xFF;
            graphics.DepthStencilDesc.FrontFace.StencilPassOp = dg::STENCIL_OP_REPLACE;
            graphics.DepthStencilDesc.FrontFace.StencilFunc = dg::COMPARISON_FUNC_ALWAYS;
            graphics.DepthStencilDesc.BackFace = graphics.DepthStencilDesc.FrontFace;
            info.pVS = impl_->quadVS;
            // Depth/stencil only: no pixel shader, so nothing writes a colour target that is not bound
            // (the D3D11 debug layer warns about that, #3146081).
            info.pPS = targets_.renderTarget != nullptr ? impl_->clearPS.RawPtr() : nullptr;
            info.PSODesc.ResourceLayout.DefaultVariableType = dg::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
            info.pPSOCache = gpu_->PipelineCache(); // M7a1: the Android pipeline cache (null on Windows)
            dg::RefCntAutoPtr<dg::IPipelineState> pipeline;
            device->CreateGraphicsPipelineState(info, &pipeline);
            if (!pipeline) {
                Note("cannot create the clear-quad pipeline");
                impl_->clearPipelines.emplace(key, nullptr);
                return;
            }
            if (auto* variable = pipeline->GetStaticVariableByName(dg::SHADER_TYPE_VERTEX, "QuadConstants")) {
                variable->Set(impl_->quadConstants);
            }
            if (info.pPS != nullptr) {
                if (auto* variable = pipeline->GetStaticVariableByName(dg::SHADER_TYPE_PIXEL, "QuadConstants")) {
                    variable->Set(impl_->quadConstants);
                }
            }
            dg::RefCntAutoPtr<dg::IShaderResourceBinding> binding;
            pipeline->CreateShaderResourceBinding(&binding, true);
            impl_->clearBindings[key] = binding;
            found = impl_->clearPipelines.emplace(key, std::move(pipeline)).first;
        }
        if (!found->second) {
            return;
        }
        QuadConstants constants{};
        std::memcpy(constants.color, rgba, sizeof(rgba));
        constants.depthUv[0] = z;
        {
            void* mapped = nullptr;
            context->MapBuffer(impl_->quadConstants, dg::MAP_WRITE, dg::MAP_FLAG_DISCARD, mapped);
            std::memcpy(mapped, &constants, sizeof(constants));
            context->UnmapBuffer(impl_->quadConstants, dg::MAP_WRITE);
        }
        context->SetPipelineState(found->second);
        impl_->currentPipeline = nullptr;
        context->SetStencilRef(stencilValue & 0xFFU);
        context->CommitShaderResources(impl_->clearBindings[key], dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const dg::Viewport viewport =
            TargetViewport(viewport_.x, viewport_.y, viewport_.width, viewport_.height, 0.0F, 1.0F, targets_.height, gpu_->FlipY());
        context->SetViewports(1, &viewport, targets_.width, targets_.height);
        dg::DrawAttribs draw;
        draw.NumVertices = 3;
        context->Draw(draw);
        ++stats_.clearsQuad;
    }

    bool DrawPath::Draw(const DrawCall& call, const D3D9StateShadow& shadow)
    {
        std::lock_guard<std::recursive_mutex> lock(gpu_->Lock());
        if (call.binding == nullptr) {
            ++stats_.skippedNoBinding;
            return false;
        }
        bool bound = false;
        {
            ScopedRenderPhase phase(ScopedRenderPhase::Kind::Targets);
            bound = BindTargets();
        }
        if (!bound) {
            ++stats_.skippedNoTarget;
            return false;
        }
        dg::IDeviceContext* const context = gpu_->Context();

        const std::uint32_t code = call.vertexFormatCode;
        VertexInputDesc input;
        if (code < impl_->inputDescs.size()) {
            if (!impl_->inputDescValid[code]) {
                impl_->inputDescs[code] = GetVertexInputDesc(code);
                impl_->inputDescValid[code] = true;
            }
            input = impl_->inputDescs[code];
        }
        PassProgram program;
        bool haveProgram = false;
        {
            ScopedDefaultFpu fpu; // GetProgram compiles on first use
            haveProgram = call.binding->GetProgram(input, &program);
        }
        if (!haveProgram || program.vertexShader == nullptr) {
            ++stats_.skippedNoProgram;
            return false;
        }

        const dg::TEXTURE_FORMAT colorFormat = targets_.renderTarget != nullptr ? targets_.renderTarget->GetDesc().Format : dg::TEX_FORMAT_UNKNOWN;
        const dg::TEXTURE_FORMAT depthFormat = targets_.depthStencil != nullptr ? targets_.depthStencil->GetDesc().Format : dg::TEX_FORMAT_UNKNOWN;
        const std::uint32_t sampleCount =
            targets_.renderTarget != nullptr ? targets_.renderTarget->GetTexture()->GetDesc().SampleCount
            : targets_.depthStencil != nullptr ? targets_.depthStencil->GetTexture()->GetDesc().SampleCount : 1U;

        PipelineKey key;
        std::memset(&key, 0, sizeof(key));
        key.programId = program.id;
        key.fixed = PackFixedState(shadow, depthFormat);
        key.renderTargetFormat = static_cast<std::uint32_t>(colorFormat);
        key.depthStencilFormat = static_cast<std::uint32_t>(depthFormat);
        key.topology = static_cast<std::uint32_t>(MapTopology(call.topology));
        key.vertexFormatCode = code;
        for (std::uint32_t stream = 0; stream < kMaxVertexStreams; ++stream) {
            key.streamStride[stream] = call.streams[stream].stride;
            key.streamInstanced |= call.streams[stream].perInstance ? (1U << stream) : 0U;
        }
        key.renderTargetCount = targets_.renderTarget != nullptr ? 1U : 0U;
        key.fixed.writeMask = targets_.renderTarget != nullptr ? key.fixed.writeMask : 0U;

        ++stats_.psoLookups;
        dg::IPipelineState* pipeline = nullptr;
        const auto found = impl_->pipelines.find(key);
        if (found != impl_->pipelines.end()) {
            pipeline = found->second;
        } else {
            const FixedState& fixed = key.fixed;
            dg::GraphicsPipelineStateCreateInfo info;
            char name[160];
            std::snprintf(name, sizeof(name), "gal %s/%s/%u vf%u #%llu", call.binding->GetEffectName(), call.binding->GetTechniqueName(),
                          call.binding->GetPassIndex(), code, static_cast<unsigned long long>(impl_->nextName++));
            info.PSODesc.Name = name;
            info.PSODesc.PipelineType = dg::PIPELINE_TYPE_GRAPHICS;

            dg::GraphicsPipelineDesc& graphics = info.GraphicsPipeline;
            dg::RenderTargetBlendDesc& blend = graphics.BlendDesc.RenderTargets[0];
            blend.BlendEnable = fixed.blendEnable != 0;
            if (blend.BlendEnable) {
                blend.SrcBlend = static_cast<dg::BLEND_FACTOR>(fixed.srcBlend);
                blend.DestBlend = static_cast<dg::BLEND_FACTOR>(fixed.destBlend);
                blend.BlendOp = static_cast<dg::BLEND_OPERATION>(fixed.blendOp);
                blend.SrcBlendAlpha = static_cast<dg::BLEND_FACTOR>(fixed.srcBlendAlpha);
                blend.DestBlendAlpha = static_cast<dg::BLEND_FACTOR>(fixed.destBlendAlpha);
                blend.BlendOpAlpha = static_cast<dg::BLEND_OPERATION>(fixed.blendOpAlpha);
            }
            blend.RenderTargetWriteMask = static_cast<dg::COLOR_MASK>(fixed.writeMask);

            dg::DepthStencilStateDesc& depth = graphics.DepthStencilDesc;
            depth.DepthEnable = fixed.depthEnable != 0;
            depth.DepthWriteEnable = fixed.depthWrite != 0;
            depth.DepthFunc = fixed.depthEnable != 0 ? static_cast<dg::COMPARISON_FUNCTION>(fixed.depthFunc) : dg::COMPARISON_FUNC_ALWAYS;
            depth.StencilEnable = fixed.stencilEnable != 0;
            if (depth.StencilEnable) {
                depth.StencilReadMask = fixed.stencilReadMask;
                depth.StencilWriteMask = fixed.stencilWriteMask;
                depth.FrontFace.StencilFailOp = static_cast<dg::STENCIL_OP>(fixed.frontFail);
                depth.FrontFace.StencilDepthFailOp = static_cast<dg::STENCIL_OP>(fixed.frontDepthFail);
                depth.FrontFace.StencilPassOp = static_cast<dg::STENCIL_OP>(fixed.frontPass);
                depth.FrontFace.StencilFunc = static_cast<dg::COMPARISON_FUNCTION>(fixed.frontFunc);
                depth.BackFace.StencilFailOp = static_cast<dg::STENCIL_OP>(fixed.backFail);
                depth.BackFace.StencilDepthFailOp = static_cast<dg::STENCIL_OP>(fixed.backDepthFail);
                depth.BackFace.StencilPassOp = static_cast<dg::STENCIL_OP>(fixed.backPass);
                depth.BackFace.StencilFunc = static_cast<dg::COMPARISON_FUNCTION>(fixed.backFunc);
            }

            dg::RasterizerStateDesc& raster = graphics.RasterizerDesc;
            raster.FillMode = static_cast<dg::FILL_MODE>(fixed.fillMode);
            raster.CullMode = static_cast<dg::CULL_MODE>(fixed.cullMode);
            // D3D9's winding; on GL the mirrored image turns it around (DiligentHost.h FlipsRenderTargets).
            raster.FrontCounterClockwise = gpu_->FlipY() ? dg::True : dg::False;
            raster.DepthClipEnable = dg::True; // D3D9 clips against the near and far planes
            raster.ScissorEnable = fixed.scissor != 0;
            raster.AntialiasedLineEnable = fixed.antialiasedLine != 0;
            raster.DepthBias = fixed.depthBias;
            raster.SlopeScaledDepthBias = std::bit_cast<float>(fixed.slopeScaledDepthBias);

            // The input layout: the D3D9 declaration on the bound streams' strides and frequencies.
            std::vector<dg::LayoutElement> layout;
            for (const VertexInputElement& element : GetVertexInputElements(code)) {
                ElementType type{};
                if (element.attribute < 0 || !MapDeclType(element.d3dDeclType, &type) || element.stream >= kMaxVertexStreams) {
                    continue;
                }
                const VertexStreamBinding& stream = call.streams[element.stream];
                dg::LayoutElement out;
                out.HLSLSemantic = "ATTRIB";
                out.InputIndex = static_cast<dg::Uint32>(element.attribute);
                out.BufferSlot = element.stream;
                out.NumComponents = type.components;
                out.ValueType = type.type;
                out.IsNormalized = type.normalized;
                out.RelativeOffset = element.offset;
                out.Stride = stream.stride;
                out.Frequency = stream.perInstance ? dg::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE : dg::INPUT_ELEMENT_FREQUENCY_PER_VERTEX;
                out.InstanceDataStepRate = 1;
                layout.push_back(out);
            }
            graphics.InputLayout.LayoutElements = layout.empty() ? nullptr : layout.data();
            graphics.InputLayout.NumElements = static_cast<dg::Uint32>(layout.size());

            graphics.PrimitiveTopology = static_cast<dg::PRIMITIVE_TOPOLOGY>(key.topology);
            graphics.NumRenderTargets = static_cast<dg::Uint8>(key.renderTargetCount);
            graphics.RTVFormats[0] = colorFormat;
            graphics.DSVFormat = depthFormat;
            graphics.SmplDesc.Count = static_cast<dg::Uint8>(sampleCount != 0U ? sampleCount : 1U);

            info.pVS = program.vertexShader;
            info.pPS = program.pixelShader;
            dg::IPipelineResourceSignature* signatures[1] = {program.signature};
            info.ppResourceSignatures = program.signature != nullptr ? signatures : nullptr;
            info.ResourceSignaturesCount = program.signature != nullptr ? 1U : 0U;

            info.pPSOCache = gpu_->PipelineCache(); // M7a1: the Android pipeline cache (null on Windows)
            dg::RefCntAutoPtr<dg::IPipelineState> created;
            {
                ScopedDefaultFpu fpu;
                gpu_->Device()->CreateGraphicsPipelineState(info, &created);
            }
            if (created) {
                ++stats_.psoCreated;
                if (pipelineLog_.size() < 64) {
                    char summary[320];
                    std::snprintf(summary, sizeof(summary),
                                  "%s: rt %u ds %u topology %u strides %u/%u/%u/%u instanced %u blend %u %u>%u op %u mask %x depth %u/%u/%u "
                                  "stencil %u cull %u fill %u scissor %u bias %d",
                                  name, key.renderTargetFormat, key.depthStencilFormat, key.topology, key.streamStride[0], key.streamStride[1],
                                  key.streamStride[2], key.streamStride[3], key.streamInstanced, fixed.blendEnable, fixed.srcBlend, fixed.destBlend,
                                  fixed.blendOp, fixed.writeMask, fixed.depthEnable, fixed.depthWrite, fixed.depthFunc, fixed.stencilEnable,
                                  fixed.cullMode, fixed.fillMode, fixed.scissor, fixed.depthBias);
                    pipelineLog_.push_back(summary);
                }
            } else {
                ++stats_.psoFailed;
                Note(std::string("cannot create the pipeline ") + name);
            }
            pipeline = created;
            impl_->pipelines.emplace(key, std::move(created));
        }
        if (pipeline == nullptr) {
            ++stats_.skippedNoPipeline;
            return false;
        }

        if (pipeline != impl_->currentPipeline) {
            context->SetPipelineState(pipeline);
            impl_->currentPipeline = pipeline;
        }
        context->SetStencilRef(shadow.GetRenderState(rs::StencilRef) & 0xFFU);
        {
            // D3DRS_BLENDFACTOR is a D3DCOLOR.
            const std::uint32_t factor = shadow.GetRenderState(rs::BlendFactor);
            const float factors[4] = {
                static_cast<float>((factor >> 16U) & 0xFFU) / 255.0f, static_cast<float>((factor >> 8U) & 0xFFU) / 255.0f,
                static_cast<float>(factor & 0xFFU) / 255.0f, static_cast<float>((factor >> 24U) & 0xFFU) / 255.0f};
            context->SetBlendFactors(factors);
        }

        // The viewport: D3D9's, or with the origin half a pixel further (HalfPixelMode::Viewport).
        const float shift = halfPixel_ == HalfPixelMode::Viewport ? 0.5F : 0.0F;
        const dg::Viewport viewport = TargetViewport(viewport_.x + shift, viewport_.y + shift, viewport_.width, viewport_.height, viewport_.minZ,
                                                     viewport_.maxZ, targets_.height, gpu_->FlipY());
        context->SetViewports(1, &viewport, targets_.width, targets_.height);
        if (key.fixed.scissor != 0U) {
            // D3D9 resets the scissor rectangle to the whole target at SetRenderTarget, and gal has no
            // slot that sets another one.
            const dg::Rect rect(0, 0, static_cast<dg::Int32>(targets_.width), static_cast<dg::Int32>(targets_.height));
            context->SetScissorRects(1, &rect, targets_.width, targets_.height);
        }

        // Streams and indices.
        dg::IBuffer* buffers[kMaxVertexStreams] = {};
        dg::Uint64 offsets[kMaxVertexStreams] = {};
        std::uint32_t streamCount = 0;
        for (const VertexInputElement& element : GetVertexInputElements(code)) {
            streamCount = std::max(streamCount, element.stream + 1U);
        }
        streamCount = std::min(streamCount, kMaxVertexStreams);
        for (std::uint32_t stream = 0; stream < streamCount; ++stream) {
            buffers[stream] = call.streams[stream].buffer;
            offsets[stream] = call.streams[stream].offset;
            if (buffers[stream] == nullptr) {
                ++stats_.skippedNoProgram;
                Note("draw with an unbound vertex stream");
                return false;
            }
        }
        if (streamCount != 0U) {
            context->SetVertexBuffers(0, streamCount, buffers, offsets, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                      dg::SET_VERTEX_BUFFERS_FLAG_RESET);
        }
        if (call.indexed) {
            if (call.indexBuffer == nullptr) {
                ++stats_.skippedNoProgram;
                Note("indexed draw without an index buffer");
                return false;
            }
            context->SetIndexBuffer(call.indexBuffer, 0, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }

        DrawState drawState;
        drawState.viewport[0] = viewport_.x;
        drawState.viewport[1] = viewport_.y;
        drawState.viewport[2] = viewport_.width;
        drawState.viewport[3] = viewport_.height;
        drawState.viewport[4] = viewport_.minZ;
        drawState.viewport[5] = viewport_.maxZ;
        if (halfPixel_ == HalfPixelMode::Shader && viewport_.width > 0.0F && viewport_.height > 0.0F) {
            drawState.positionOffset[0] = 1.0F / viewport_.width;
            drawState.positionOffset[1] = -1.0F / viewport_.height;
        }
        drawState.alphaTestEnable = shadow.GetRenderState(rs::AlphaTestEnable) != 0U ? 1U : 0U;
        drawState.alphaFunc = shadow.GetRenderState(rs::AlphaFunc);
        drawState.alphaRef = shadow.GetRenderState(rs::AlphaRef);
        drawState.frameIndex = gpu_->Frame();
        bool committed = false;
        {
            ScopedRenderPhase phase(ScopedRenderPhase::Kind::Commit);
            committed = call.binding->Commit(context, drawState);
        }
        if (!committed) {
            ++stats_.skippedCommit;
            return false;
        }

        gpu_->MarkDraw();
        if (call.indexed) {
            dg::DrawIndexedAttribs draw;
            draw.NumIndices = call.count;
            draw.IndexType = call.index32 ? dg::VT_UINT32 : dg::VT_UINT16;
            draw.NumInstances = std::max(call.instanceCount, 1U);
            draw.FirstIndexLocation = call.firstIndex;
            draw.BaseVertex = static_cast<dg::Uint32>(call.baseVertex);
            context->DrawIndexed(draw);
        } else {
            dg::DrawAttribs draw;
            draw.NumVertices = call.count;
            draw.NumInstances = std::max(call.instanceCount, 1U);
            draw.StartVertexLocation = call.firstIndex;
            context->Draw(draw);
        }
        ++stats_.draws;
        return true;
    }

    void DrawPath::StretchRect(
        dg::ITexture* const source,
        const std::int32_t* const sourceRect,
        dg::ITexture* const destination,
        const std::int32_t* const destinationRect
    )
    {
        std::lock_guard<std::recursive_mutex> lock(gpu_->Lock());
        if (source == nullptr || destination == nullptr) {
            return;
        }
        dg::IDeviceContext* const context = gpu_->Context();
        const dg::TextureDesc& sourceDesc = source->GetDesc();
        const dg::TextureDesc& destinationDesc = destination->GetDesc();
        const std::int32_t whole[2][4] = {
            {0, 0, static_cast<std::int32_t>(sourceDesc.Width), static_cast<std::int32_t>(sourceDesc.Height)},
            {0, 0, static_cast<std::int32_t>(destinationDesc.Width), static_cast<std::int32_t>(destinationDesc.Height)},
        };
        const std::int32_t* const from = sourceRect != nullptr ? sourceRect : whole[0];
        const std::int32_t* const to = destinationRect != nullptr ? destinationRect : whole[1];
        const std::int32_t fromWidth = from[2] - from[0];
        const std::int32_t fromHeight = from[3] - from[1];
        const std::int32_t toWidth = to[2] - to[0];
        const std::int32_t toHeight = to[3] - to[1];
        if (fromWidth <= 0 || fromHeight <= 0 || toWidth <= 0 || toHeight <= 0) {
            return;
        }

        // The current targets are rebound at the next clear or draw.
        context->SetRenderTargets(0, nullptr, nullptr, dg::RESOURCE_STATE_TRANSITION_MODE_NONE);
        targetsBound_ = false;
        impl_->currentPipeline = nullptr;

        if (fromWidth == toWidth && fromHeight == toHeight && sourceDesc.Format == destinationDesc.Format &&
            sourceDesc.SampleCount == destinationDesc.SampleCount) {
            dg::CopyTextureAttribs copy{source, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, destination,
                                        dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION};
            const dg::Box box(static_cast<dg::Uint32>(from[0]), static_cast<dg::Uint32>(from[2]), static_cast<dg::Uint32>(from[1]),
                              static_cast<dg::Uint32>(from[3]));
            copy.pSrcBox = &box;
            copy.DstX = static_cast<dg::Uint32>(to[0]);
            copy.DstY = static_cast<dg::Uint32>(to[1]);
            context->CopyTexture(copy);
            ++stats_.copies;
            return;
        }

        // A bilinear blit: pixel (x, y) of the destination rectangle samples the source at the
        // corresponding point of the source rectangle, both at pixel centres (D3DTEXF_LINEAR).
        dg::IRenderDevice* const device = gpu_->Device();
        ScopedDefaultFpu fpu;
        if (!impl_->quadVS) {
            std::string messages;
            impl_->quadVS = CompileInternal(device, gpu_->Api(), dg::SHADER_TYPE_VERTEX, "gal quad VS", kQuadVS, false, &messages);
            impl_->clearPS = CompileInternal(device, gpu_->Api(), dg::SHADER_TYPE_PIXEL, "gal clear PS", kClearPS, false, &messages);
            if (!messages.empty()) {
                Note(messages);
            }
        }
        if (!impl_->blitPS) {
            std::string messages;
            impl_->blitPS = CompileInternal(device, gpu_->Api(), dg::SHADER_TYPE_PIXEL, "gal blit PS", kBlitPS, true, &messages);
            if (!messages.empty()) {
                Note(messages);
            }
        }
        if (!impl_->quadConstants) {
            dg::BufferDesc desc;
            desc.Name = "gal quad constants";
            desc.Size = sizeof(QuadConstants);
            desc.Usage = dg::USAGE_DYNAMIC;
            desc.BindFlags = dg::BIND_UNIFORM_BUFFER;
            desc.CPUAccessFlags = dg::CPU_ACCESS_WRITE;
            device->CreateBuffer(desc, nullptr, &impl_->quadConstants);
        }
        const std::uint32_t key = static_cast<std::uint32_t>(destinationDesc.Format);
        auto found = impl_->blitPipelines.find(key);
        if (found == impl_->blitPipelines.end()) {
            dg::GraphicsPipelineStateCreateInfo info;
            info.PSODesc.Name = "gal stretch-rect blit";
            info.PSODesc.PipelineType = dg::PIPELINE_TYPE_GRAPHICS;
            info.GraphicsPipeline.NumRenderTargets = 1;
            info.GraphicsPipeline.RTVFormats[0] = destinationDesc.Format;
            info.GraphicsPipeline.PrimitiveTopology = dg::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            info.GraphicsPipeline.RasterizerDesc.CullMode = dg::CULL_MODE_NONE;
            info.GraphicsPipeline.DepthStencilDesc.DepthEnable = dg::False;
            info.pVS = impl_->quadVS;
            info.pPS = impl_->blitPS;
            dg::ShaderResourceVariableDesc variables[] = {
                {dg::SHADER_TYPE_PIXEL, "g_Source", dg::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            };
            info.PSODesc.ResourceLayout.Variables = variables;
            info.PSODesc.ResourceLayout.NumVariables = 1;
            dg::SamplerDesc linear;
            linear.MinFilter = dg::FILTER_TYPE_LINEAR;
            linear.MagFilter = dg::FILTER_TYPE_LINEAR;
            linear.MipFilter = dg::FILTER_TYPE_POINT;
            linear.AddressU = dg::TEXTURE_ADDRESS_CLAMP;
            linear.AddressV = dg::TEXTURE_ADDRESS_CLAMP;
            linear.AddressW = dg::TEXTURE_ADDRESS_CLAMP;
            dg::ImmutableSamplerDesc samplers[] = {{dg::SHADER_TYPE_PIXEL, "g_Source", linear}};
            info.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
            info.PSODesc.ResourceLayout.NumImmutableSamplers = 1;
            info.pPSOCache = gpu_->PipelineCache(); // M7a1: the Android pipeline cache (null on Windows)
            dg::RefCntAutoPtr<dg::IPipelineState> pipeline;
            device->CreateGraphicsPipelineState(info, &pipeline);
            if (!pipeline) {
                Note("cannot create the stretch-rect pipeline");
                impl_->blitPipelines.emplace(key, nullptr);
                return;
            }
            if (auto* variable = pipeline->GetStaticVariableByName(dg::SHADER_TYPE_VERTEX, "QuadConstants")) {
                variable->Set(impl_->quadConstants);
            }
            dg::RefCntAutoPtr<dg::IShaderResourceBinding> binding;
            pipeline->CreateShaderResourceBinding(&binding, true);
            impl_->blitBindings[key] = binding;
            found = impl_->blitPipelines.emplace(key, std::move(pipeline)).first;
        }
        if (!found->second) {
            return;
        }
        dg::ITextureView* const sourceView = source->GetDefaultView(dg::TEXTURE_VIEW_SHADER_RESOURCE);
        dg::ITextureView* destinationView = destination->GetDefaultView(dg::TEXTURE_VIEW_RENDER_TARGET);
        if (sourceView == nullptr || destinationView == nullptr) {
            Note("stretch-rect between targets without shader-resource or render-target views");
            return;
        }
        QuadConstants constants{};
        constants.uvRect[0] = static_cast<float>(from[0]) / static_cast<float>(sourceDesc.Width);
        constants.uvRect[1] = static_cast<float>(from[1]) / static_cast<float>(sourceDesc.Height);
        // The triangle's corner runs 0..2 over 0..2 viewports, so the UV span per viewport is the
        // rectangle's.
        constants.uvRect[2] = static_cast<float>(fromWidth) / static_cast<float>(sourceDesc.Width);
        constants.uvRect[3] = static_cast<float>(fromHeight) / static_cast<float>(sourceDesc.Height);
        {
            void* mapped = nullptr;
            context->MapBuffer(impl_->quadConstants, dg::MAP_WRITE, dg::MAP_FLAG_DISCARD, mapped);
            std::memcpy(mapped, &constants, sizeof(constants));
            context->UnmapBuffer(impl_->quadConstants, dg::MAP_WRITE);
        }
        dg::IShaderResourceBinding* const binding = impl_->blitBindings[key];
        binding->GetVariableByName(dg::SHADER_TYPE_PIXEL, "g_Source")->Set(sourceView);
        context->SetRenderTargets(1, &destinationView, nullptr, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->SetPipelineState(found->second);
        context->CommitShaderResources(binding, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const dg::Viewport viewport = TargetViewport(static_cast<float>(to[0]), static_cast<float>(to[1]), static_cast<float>(toWidth),
                                                     static_cast<float>(toHeight), 0.0F, 1.0F, destinationDesc.Height, gpu_->FlipY());
        context->SetViewports(1, &viewport, destinationDesc.Width, destinationDesc.Height);
        dg::DrawAttribs draw;
        draw.NumVertices = 3;
        context->Draw(draw);
        context->SetRenderTargets(0, nullptr, nullptr, dg::RESOURCE_STATE_TRANSITION_MODE_NONE);
        ++stats_.blits;
    }
} // namespace gpg::gal::diligent
