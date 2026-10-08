#include "EffectsDiligentGpu.h"

#include <float.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <vector>

#include "gpg/gal/fx/FxHlslEmitter.h"

#include "ShaderCompileDiligent.h"

#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/Buffer.h"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/PipelineResourceSignature.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/Shader.h"
#include "Graphics/GraphicsEngine/interface/ShaderResourceBinding.h"
#include "Graphics/GraphicsEngine/interface/Texture.h"

namespace dg = Diligent;

// The emitter's vertex slots and input kinds are this backend's contract with the draw path.
static_assert(gpg::gal::fx::kSlotPosition == gpg::gal::diligent::kAttribPosition, "vertex slot mismatch");
static_assert(gpg::gal::fx::kSlotPosition1 == gpg::gal::diligent::kAttribPosition1, "vertex slot mismatch");
static_assert(gpg::gal::fx::kSlotNormal == gpg::gal::diligent::kAttribNormal, "vertex slot mismatch");
static_assert(gpg::gal::fx::kSlotTangent == gpg::gal::diligent::kAttribTangent, "vertex slot mismatch");
static_assert(gpg::gal::fx::kSlotBinormal == gpg::gal::diligent::kAttribBinormal, "vertex slot mismatch");
static_assert(gpg::gal::fx::kSlotBlendIndices == gpg::gal::diligent::kAttribBlendIndices, "vertex slot mismatch");
static_assert(gpg::gal::fx::kSlotColor0 == gpg::gal::diligent::kAttribColor0, "vertex slot mismatch");
static_assert(gpg::gal::fx::kSlotTexcoord0 == gpg::gal::diligent::kAttribTexcoord0, "vertex slot mismatch");
static_assert(gpg::gal::fx::kVertexSlotCount == gpg::gal::diligent::kAttribSlotCount, "vertex slot mismatch");
static_assert(static_cast<int>(gpg::gal::fx::InputKind::PositionT) == static_cast<int>(gpg::gal::diligent::AttribKind::PositionT),
              "input kind mismatch");
static_assert(static_cast<int>(gpg::gal::fx::InputKind::UNormBgra) == static_cast<int>(gpg::gal::diligent::AttribKind::UNormBgra),
              "input kind mismatch");

namespace gpg::gal::diligent
{
    namespace
    {
        // ---- process-wide state -------------------------------------------------------------

        std::mutex gLock;
        EffectLogFn gLog = nullptr;
        ShaderCompilerFn gCompiler = nullptr; // SetEffectShaderCompiler (ShaderCompileDiligent.h)
        int gTrilinearMode = 0; // SetEffectTrilinearMode: 0 as D3D9, 1 anisotropic x1, 2 mip POINT (experiments)
        PassSink* gSink = nullptr;
        std::set<std::string> gLogged;
        std::atomic<std::uint64_t> gNextProgramId{1};

        struct Counters
        {
            std::atomic<std::uint64_t> effects{0};
            std::atomic<std::uint64_t> programsCompiled{0};
            std::atomic<std::uint64_t> shadersCompiled{0};
            std::atomic<std::uint64_t> generationFailures{0};
            std::atomic<std::uint64_t> constantUploads{0};
            std::atomic<std::uint64_t> drawConstantUploads{0};
            std::atomic<std::uint64_t> commits{0};
            std::atomic<std::uint64_t> nullTextureBinds{0};
        } gCounters;

        /** Released and remade with the device (SetEffectRenderDevice). */
        class GpuOwner
        {
        public:
            virtual void ReleaseGpu() = 0;

        protected:
            ~GpuOwner() = default;
        };

        std::set<GpuOwner*> gOwners;

        struct SharedGpu
        {
            dg::RefCntAutoPtr<dg::IRenderDevice> device;
            GraphicsApi api = GraphicsApi::D3D11; // from the device's type (SetEffectRenderDevice)
            dg::RefCntAutoPtr<dg::IBuffer> drawBuffer; // FxGenDraw, every pass binds it
            float lastDraw[fx::kDrawConstantRegisters * 4] = {};
            std::uint64_t lastDrawFrame = 0;
            bool drawValid = false;
            dg::RefCntAutoPtr<dg::ITexture> null2D;
            dg::RefCntAutoPtr<dg::ITexture> null3D;
            dg::RefCntAutoPtr<dg::ITexture> nullCube;
            dg::RefCntAutoPtr<dg::ITexture> nullFaces; // a 1x1 2D array of six slices
            // 2D-array views of cube textures (FxGenSampleCubeD3D9). The texture is held so its
            // address cannot be reused by another texture while the view is cached.
            std::map<dg::ITexture*, std::pair<dg::RefCntAutoPtr<dg::ITexture>, dg::RefCntAutoPtr<dg::ITextureView>>> cubeFaceViews;
        } gShared;

        void Log(const std::string& message)
        {
            {
                std::lock_guard<std::mutex> lock(gLock);
                if (!gLogged.insert(message).second) {
                    return;
                }
            }
            const std::string line = "[gal-diligent fx] " + message;
            if (gLog != nullptr) {
                gLog(line.c_str());
            } else {
                std::fprintf(stderr, "%s\n", line.c_str());
            }
        }

        /**
         * The engine's main thread runs at 24-bit x87 precision (CScApp::Init); the shader compiler
         * is not written for that, so compilation runs at the CRT default and the engine's control
         * word is put back afterwards (as DiligentHost.cpp ScopedDefaultFpu, m6u-PLAN.txt B; its own, as fxdiff links this TU without DiligentHost.cpp).
         */
        class EffectScopedFpu
        {
        public:
            EffectScopedFpu()
            {
#if defined(_M_IX86)
                unsigned int unused = 0;
                ::_controlfp_s(&mSaved, 0, 0);
                ::_controlfp_s(&unused, _PC_53, _MCW_PC);
#endif
            }
            ~EffectScopedFpu()
            {
#if defined(_M_IX86)
                unsigned int unused = 0;
                ::_controlfp_s(&unused, mSaved & _MCW_PC, _MCW_PC);
#endif
            }
            EffectScopedFpu(const EffectScopedFpu&) = delete;
            EffectScopedFpu& operator=(const EffectScopedFpu&) = delete;

        private:
            unsigned int mSaved = 0;
        };

        dg::SHADER_TYPE StagesOf(const bool vertex, const bool pixel)
        {
            return static_cast<dg::SHADER_TYPE>((vertex ? dg::SHADER_TYPE_VERTEX : 0) | (pixel ? dg::SHADER_TYPE_PIXEL : 0));
        }

        dg::RefCntAutoPtr<dg::ITexture> MakeNullTexture(dg::IRenderDevice* const device, const fx::TextureDim dim)
        {
            // D3D9 samples a stage with no texture as (0, 0, 0, 1) (D3D9 documentation behaviour for
            // pixel shaders; D3D10 and later return 0 in every channel), so unbound resources sample
            // an opaque black texel.
            static const std::uint8_t texel[4] = {0, 0, 0, 255};
            dg::TextureDesc desc;
            desc.Name = dim == fx::TextureDim::Cube ? "FxGenNullTextureCube" : dim == fx::TextureDim::Tex3D ? "FxGenNullTexture3D" : "FxGenNullTexture2D";
            desc.Type = dim == fx::TextureDim::Cube ? dg::RESOURCE_DIM_TEX_CUBE : dim == fx::TextureDim::Tex3D ? dg::RESOURCE_DIM_TEX_3D : dg::RESOURCE_DIM_TEX_2D;
            desc.Width = 1;
            desc.Height = 1;
            desc.ArraySize = dim == fx::TextureDim::Cube ? 6 : 1;
            if (dim == fx::TextureDim::Tex3D) {
                desc.Depth = 1;
            }
            desc.Format = dg::TEX_FORMAT_RGBA8_UNORM;
            desc.MipLevels = 1;
            desc.Usage = dg::USAGE_IMMUTABLE;
            desc.BindFlags = dg::BIND_SHADER_RESOURCE;
            dg::TextureSubResData faces[6];
            for (dg::TextureSubResData& face : faces) {
                face.pData = texel;
                face.Stride = 4;
                face.DepthStride = 4;
            }
            dg::TextureData data{faces, static_cast<dg::Uint32>(dim == fx::TextureDim::Cube ? 6 : 1)};
            dg::RefCntAutoPtr<dg::ITexture> texture;
            device->CreateTexture(desc, &data, &texture);
            return texture;
        }

        dg::ITextureView* NullFacesView()
        {
            if (!gShared.nullFaces && gShared.device) {
                static const std::uint8_t texel[4] = {0, 0, 0, 255}; // as MakeNullTexture
                dg::TextureDesc desc;
                desc.Name = "FxGenNullTextureCubeFaces";
                desc.Type = dg::RESOURCE_DIM_TEX_2D_ARRAY;
                desc.Width = 1;
                desc.Height = 1;
                desc.ArraySize = 6;
                desc.Format = dg::TEX_FORMAT_RGBA8_UNORM;
                desc.MipLevels = 1;
                desc.Usage = dg::USAGE_IMMUTABLE;
                desc.BindFlags = dg::BIND_SHADER_RESOURCE;
                dg::TextureSubResData faces[6];
                for (dg::TextureSubResData& face : faces) {
                    face.pData = texel;
                    face.Stride = 4;
                }
                dg::TextureData data{faces, 6};
                gShared.device->CreateTexture(desc, &data, &gShared.nullFaces);
            }
            return gShared.nullFaces ? gShared.nullFaces->GetDefaultView(dg::TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
        }

        // The six faces of a cube texture as a 2D array (slice = D3DCUBEMAP_FACES order, which is
        // Diligent's and D3D11's cube face order).
        dg::ITextureView* CubeFacesView(dg::ITextureView* const cubeView)
        {
            dg::ITexture* const texture = cubeView != nullptr ? cubeView->GetTexture() : nullptr;
            if (texture == nullptr) {
                return nullptr;
            }
            auto it = gShared.cubeFaceViews.find(texture);
            if (it == gShared.cubeFaceViews.end()) {
                if (gShared.cubeFaceViews.size() >= 64) {
                    gShared.cubeFaceViews.clear(); // render-target cubes come and go with resizes
                }
                dg::TextureViewDesc desc;
                desc.ViewType = dg::TEXTURE_VIEW_SHADER_RESOURCE;
                desc.TextureDim = dg::RESOURCE_DIM_TEX_2D_ARRAY;
                desc.MostDetailedMip = 0;
                desc.NumMipLevels = texture->GetDesc().MipLevels;
                desc.FirstArraySlice = 0;
                desc.NumArraySlices = 6;
                dg::RefCntAutoPtr<dg::ITextureView> view;
                texture->CreateView(desc, &view);
                it = gShared.cubeFaceViews.emplace(texture, std::make_pair(dg::RefCntAutoPtr<dg::ITexture>(texture), view)).first;
            }
            return it->second.second;
        }

        dg::ITextureView* NullView(const fx::TextureDim dim)
        {
            dg::RefCntAutoPtr<dg::ITexture>& texture =
                dim == fx::TextureDim::Cube ? gShared.nullCube : dim == fx::TextureDim::Tex3D ? gShared.null3D : gShared.null2D;
            if (!texture && gShared.device) {
                texture = MakeNullTexture(gShared.device, dim);
            }
            return texture ? texture->GetDefaultView(dg::TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
        }

        dg::IBuffer* DrawBuffer()
        {
            if (!gShared.drawBuffer && gShared.device) {
                dg::BufferDesc desc;
                desc.Name = "FxGenDraw";
                desc.Size = fx::kDrawConstantRegisters * 16U;
                desc.BindFlags = dg::BIND_UNIFORM_BUFFER;
                desc.Usage = dg::USAGE_DYNAMIC;
                desc.CPUAccessFlags = dg::CPU_ACCESS_WRITE;
                gShared.device->CreateBuffer(desc, nullptr, &gShared.drawBuffer);
                gShared.drawValid = false;
            }
            return gShared.drawBuffer;
        }

        // ---- D3D9 sampler states -> Diligent ---------------------------------------------------

        dg::TEXTURE_ADDRESS_MODE AddressMode(const std::uint32_t value)
        {
            switch (value) { // D3DTEXTUREADDRESS
            case 2: return dg::TEXTURE_ADDRESS_MIRROR;
            case 3: return dg::TEXTURE_ADDRESS_CLAMP;
            case 4: return dg::TEXTURE_ADDRESS_BORDER;
            case 5: return dg::TEXTURE_ADDRESS_MIRROR_ONCE;
            default: return dg::TEXTURE_ADDRESS_WRAP;
            }
        }

        float FloatBits(const std::uint32_t value)
        {
            float result = 0.0F;
            std::memcpy(&result, &value, sizeof(result));
            return result;
        }

        /**
         * The sampler D3DX sets for a sampler parameter at BeginPass: its sampler_state over D3D9's
         * defaults (IDirect3DDevice9::SetSamplerState: ADDRESSU/V/W WRAP, MAG/MIN POINT, MIP NONE,
         * MIPMAPLODBIAS 0, MAXMIPLEVEL 0, MAXANISOTROPY 1, BORDERCOLOR 0). D3D9 keeps sampler state
         * across passes; fields an effect never sets therefore come from the default here, which
         * differs only if an earlier pass set them on the same register (BorderColor and AddressW,
         * set by 4 in-game samplers, m6u-FX.txt section 4).
         */
        dg::SamplerDesc SamplerOf(const fx::ParameterInfo* const parameter, const std::string& effectName)
        {
            std::uint32_t address[3] = {1, 1, 1};
            std::uint32_t mag = 1;
            std::uint32_t min = 1;
            std::uint32_t mip = 0;
            std::uint32_t lodBias = 0;
            std::uint32_t maxMipLevel = 0;
            std::uint32_t maxAnisotropy = 1;
            std::uint32_t border = 0;
            if (parameter != nullptr) {
                for (const fx::SamplerStateValue& state : parameter->samplerStates) {
                    switch (state.state) { // D3DSAMPLERSTATETYPE
                    case 1: address[0] = state.value; break;
                    case 2: address[1] = state.value; break;
                    case 3: address[2] = state.value; break;
                    case 4: border = state.value; break;
                    case 5: mag = state.value; break;
                    case 6: min = state.value; break;
                    case 7: mip = state.value; break;
                    case 8: lodBias = state.value; break;
                    case 9: maxMipLevel = state.value; break;
                    case 10: maxAnisotropy = state.value; break;
                    case 11:
                        if (state.value != 0) {
                            Log(effectName + ": sampler " + parameter->desc.name + " sets SRGBTexture, which is ignored");
                        }
                        break;
                    default: break;
                    }
                    if (state.dynamic) {
                        Log(effectName + ": sampler " + parameter->desc.name + " has a state computed from a parameter; its default is used");
                    }
                }
            }
            auto filter = [](const std::uint32_t value) {
                // D3DTEXF_POINT 1, LINEAR 2, ANISOTROPIC 3; PYRAMIDALQUAD/GAUSSIANQUAD filter like LINEAR.
                return value == 1 || value == 0 ? dg::FILTER_TYPE_POINT : value == 3 ? dg::FILTER_TYPE_ANISOTROPIC : dg::FILTER_TYPE_LINEAR;
            };
            dg::SamplerDesc desc;
            desc.MinFilter = filter(min);
            desc.MagFilter = filter(mag);
            if (desc.MinFilter == dg::FILTER_TYPE_ANISOTROPIC || desc.MagFilter == dg::FILTER_TYPE_ANISOTROPIC) {
                desc.MinFilter = desc.MagFilter = dg::FILTER_TYPE_ANISOTROPIC;
            }
            desc.MipFilter = mip == 2 ? dg::FILTER_TYPE_LINEAR : dg::FILTER_TYPE_POINT;
            float minMipLevelOverride = 0.0F;
            if (gTrilinearMode != 0 && desc.MinFilter == dg::FILTER_TYPE_LINEAR && desc.MagFilter == dg::FILTER_TYPE_LINEAR &&
                desc.MipFilter == dg::FILTER_TYPE_LINEAR) {
                if (gTrilinearMode == 1 || gTrilinearMode == 4 || gTrilinearMode == 5) {
                    desc.MinFilter = desc.MagFilter = dg::FILTER_TYPE_ANISOTROPIC;
                    maxAnisotropy = gTrilinearMode == 1 ? 1 : gTrilinearMode == 4 ? 2 : 16;
                } else if (gTrilinearMode == 2 || gTrilinearMode == 3) {
                    desc.MipFilter = dg::FILTER_TYPE_POINT;
                    if (gTrilinearMode == 3) { // level 1 only
                        minMipLevelOverride = 1.0F;
                    }
                }
            }
            if (desc.MinFilter == dg::FILTER_TYPE_ANISOTROPIC) {
                desc.MipFilter = dg::FILTER_TYPE_ANISOTROPIC;
            }
            desc.AddressU = AddressMode(address[0]);
            desc.AddressV = AddressMode(address[1]);
            desc.AddressW = AddressMode(address[2]);
            desc.MipLODBias = FloatBits(lodBias);
            desc.MaxAnisotropy = std::max<std::uint32_t>(1, maxAnisotropy);
            // MAXMIPLEVEL is the most detailed level D3D9 samples; MIPFILTER NONE samples only it.
            desc.MinLOD = static_cast<float>(maxMipLevel);
            desc.MaxLOD = mip == 0 ? static_cast<float>(maxMipLevel) : 3.402823466e+38F;
            if (minMipLevelOverride > 0.0F) {
                desc.MinLOD = desc.MaxLOD = minMipLevelOverride;
            }
            if (gTrilinearMode == 6 && desc.MipFilter == dg::FILTER_TYPE_LINEAR) {
                desc.MipLODBias += 1.0F / 64.0F;
            }
            // D3DCOLOR is 0xAARRGGBB.
            desc.BorderColor[0] = static_cast<float>((border >> 16) & 0xFFU) / 255.0F;
            desc.BorderColor[1] = static_cast<float>((border >> 8) & 0xFFU) / 255.0F;
            desc.BorderColor[2] = static_cast<float>(border & 0xFFU) / 255.0F;
            desc.BorderColor[3] = static_cast<float>((border >> 24) & 0xFFU) / 255.0F;
            return desc;
        }

        std::uint64_t LayoutKey(const VertexInputDesc& input)
        {
            std::uint64_t key = 0;
            for (std::uint32_t slot = 0; slot < kAttribSlotCount; ++slot) {
                key |= static_cast<std::uint64_t>(input.kinds[slot]) << (slot * 3U);
            }
            return key;
        }

        fx::VertexInputLayout ToFxLayout(const VertexInputDesc& input)
        {
            fx::VertexInputLayout layout;
            for (std::uint32_t slot = 0; slot < kAttribSlotCount; ++slot) {
                layout.kinds[slot] = static_cast<fx::InputKind>(input.kinds[slot]);
            }
            return layout;
        }
    } // namespace

    // ---- the effect ----------------------------------------------------------------------------

    class PassBindingImpl;

    struct EffectGpu::Impl final : GpuOwner
    {
        std::string name;
        fx::EffectInput input;
        fx::EffectMetadata metadata;
        fx::ConstantLayout layout;
        std::vector<std::uint32_t> registers;
        std::vector<ShaderResourceSource*> textures; // per parameter
        std::unique_ptr<fx::HlslEmitter> emitter;
        bool emitterFailed = false;
        std::vector<std::vector<std::unique_ptr<PassBindingImpl>>> passes;
        dg::RefCntAutoPtr<dg::IBuffer> paramsBuffer;
        std::vector<std::uint32_t> uploaded;
        std::uint64_t uploadedFrame = 0;
        bool uploadedValid = false;

        void ReleaseGpu() override;
        fx::HlslEmitter* Emitter();
        dg::IBuffer* ParamsBuffer();
        bool Write(int parameter, std::uint32_t component, std::uint32_t word, int sourceKind);
        const fx::ConstantSlot* Slot(int parameter) const;
    };

    class PassBindingImpl final : public PassBinding
    {
    public:
        PassBindingImpl(EffectGpu::Impl& effect, const int technique, const int pass)
            : mEffect(effect),
              mTechnique(technique),
              mPass(pass)
        {
            const fx::PassInfo& info = Info();
            for (const fx::PassState& state : info.states) {
                if (state.op == fx::StateOp::Render || state.op == fx::StateOp::TextureStage) {
                    PassStateAssignment assignment;
                    assignment.op = state.op == fx::StateOp::Render ? PassStateAssignment::Render : PassStateAssignment::TextureStage;
                    assignment.index = state.index;
                    assignment.state = state.state;
                    assignment.value = state.value;
                    mStates.push_back(assignment);
                    if (state.dynamic) {
                        Log(Where() + ": state " + std::to_string(state.state) +
                            " is computed from a parameter at BeginPass; the default value is used");
                    }
                } else if (state.op == fx::StateOp::Sampler || state.op == fx::StateOp::Texture) {
                    Log(Where() + ": Sampler[]/Texture[] pass states are not supported");
                }
            }
        }

        const char* GetEffectName() const override { return mEffect.name.c_str(); }
        const char* GetTechniqueName() const override { return mEffect.metadata.techniques[static_cast<std::size_t>(mTechnique)].name.c_str(); }
        std::uint32_t GetPassIndex() const override { return static_cast<std::uint32_t>(mPass); }

        const PassStateAssignment* GetStates(std::size_t* const count) const override
        {
            *count = mStates.size();
            return mStates.empty() ? nullptr : mStates.data();
        }

        bool GetProgram(const VertexInputDesc& input, PassProgram* out) override;
        bool Commit(dg::IDeviceContext* context, const DrawState& state) override;

        void Snapshot()
        {
            mSnapshot = mEffect.registers;
            mSnapshotTextures = mEffect.textures;
        }

        void ReleaseGpu()
        {
            mPixel.reset();
            mSignature.Release();
            mBinding.Release();
            mPrograms.clear();
            mTextureVariables.clear();
            mResourcesDone = false;
            mSignatureFailed = false;
        }

    private:
        struct Program
        {
            dg::RefCntAutoPtr<dg::IShader> vertexShader;
            std::uint64_t id = 0;
            bool failed = false;
        };

        struct PixelStage
        {
            fx::EmittedStage stage;
            dg::RefCntAutoPtr<dg::IShader> shader;
            std::vector<GlCombinedSampler> combined; // GL
            bool failed = false;
        };

        struct TextureBinding
        {
            fx::TextureResource resource;
            std::string variableName; // the texture's name; on GL the combined sampler's (ShaderCompileDiligent.h)
            bool pixelStage = true;
            dg::IShaderResourceVariable* variable = nullptr;
            dg::ITextureView* bound = nullptr;
        };

        const fx::PassInfo& Info() const
        {
            return mEffect.metadata.techniques[static_cast<std::size_t>(mTechnique)].passes[static_cast<std::size_t>(mPass)];
        }

        std::string Where() const
        {
            return mEffect.name + "/" + mEffect.metadata.techniques[static_cast<std::size_t>(mTechnique)].name + "/P" + std::to_string(mPass);
        }

        // D3D11 and Vulkan compile the SM5 as Diligent does (FXC, glslang); GL goes through glslang and
        // SPIRV-Cross with combined samplers named "<texture>_s_<sampler>" (ShaderCompileDiligent.h).
        dg::RefCntAutoPtr<dg::IShader> Compile(const dg::SHADER_TYPE type, const fx::EmittedStage& stage, const char* suffix,
                                               std::vector<GlCombinedSampler>* combined = nullptr)
        {
            dg::RefCntAutoPtr<dg::IShader> shader;
            if (!gShared.device) {
                return shader;
            }
            const std::string name = Where() + suffix;
            HlslShaderSource request;
            request.source = stage.source.c_str();
            request.length = stage.source.size();
            request.name = name.c_str();
            request.shaderType = static_cast<std::uint32_t>(type);
            request.entryPoint = "main";
            request.shaderModel5 = true;
            request.combinedTextureSamplers = false;
            request.naming = CombinedSamplerNaming::TextureAndSampler;
            CompiledShaderInfo info;
            {
                EffectScopedFpu fpu;
                if (gCompiler != nullptr) {
                    gCompiler(gShared.device, gShared.api, request, &shader, &info);
                } else if (gShared.api != GraphicsApi::OpenGL) {
                    // Diligent's own HLSL path (FXC on D3D11, glslang on Vulkan): what the device's
                    // compiler does for these APIs, for a host that installs none (fxdiff).
                    dg::ShaderCreateInfo create;
                    create.Source = request.source;
                    create.SourceLength = request.length;
                    create.EntryPoint = request.entryPoint;
                    create.SourceLanguage = dg::SHADER_SOURCE_LANGUAGE_HLSL;
                    create.HLSLVersion = dg::ShaderVersion{5, 0};
                    create.Desc.ShaderType = type;
                    create.Desc.Name = request.name;
                    create.Desc.UseCombinedTextureSamplers = false;
                    dg::RefCntAutoPtr<dg::IDataBlob> output;
                    gShared.device->CreateShader(create, &shader, &output);
                    if (!shader && output && output->GetSize() != 0) {
                        info.messages.assign(static_cast<const char*>(output->GetConstDataPtr()), output->GetSize());
                    }
                } else {
                    info.messages = "no shader compiler installed for GL (SetEffectShaderCompiler)";
                }
            }
            if (!shader) {
                ++gCounters.generationFailures;
                Log(name + ": shader compilation failed: " + info.messages);
            } else {
                ++gCounters.shadersCompiled;
                if (combined != nullptr) {
                    *combined = std::move(info.combined);
                }
            }
            return shader;
        }

        bool EnsurePixel()
        {
            if (mPixel) {
                return !mPixel->failed;
            }
            mPixel = std::make_unique<PixelStage>();
            fx::HlslEmitter* const emitter = mEffect.Emitter();
            if (emitter == nullptr) {
                mPixel->failed = true;
                return false;
            }
            std::vector<fx::Diagnostic> diagnostics;
            const fx::ShaderEntry& entry = Info().pixelShader;
            bool ok = false;
            if (entry.kind == fx::ShaderEntry::Kind::Compile) {
                ok = emitter->EmitPixelShader(entry, mPixel->stage, diagnostics);
            } else {
                if (entry.kind == fx::ShaderEntry::Kind::Unassigned) {
                    // D3D9 keeps the previous pass's pixel shader (D3DXFX_DONOTSAVESTATE); range.fx
                    // Cast is the only such pass and writes no colour (ColorWriteEnable = 0).
                    Log(Where() + ": the pass assigns no pixel shader; drawn with the fixed-function one");
                }
                ok = emitter->EmitFixedFunctionPixelShader(mPixel->stage, diagnostics);
            }
            if (!ok) {
                ++gCounters.generationFailures;
                std::string text;
                for (const fx::Diagnostic& diagnostic : diagnostics) {
                    text += diagnostic.message + "; ";
                }
                Log(Where() + ": pixel shader not generated: " + text);
                mPixel->failed = true;
                return false;
            }
            mPixel->shader = Compile(dg::SHADER_TYPE_PIXEL, mPixel->stage, "/ps", &mPixel->combined);
            mPixel->failed = !mPixel->shader;
            return !mPixel->failed;
        }

        bool UsesFixedFunctionVertex(const VertexInputDesc& input) const
        {
            // D3D9 does no vertex processing for pre-transformed (POSITIONT) vertices.
            return Info().vertexShader.kind != fx::ShaderEntry::Kind::Compile || input.kinds[kAttribPosition] == AttribKind::PositionT;
        }

        // The resources of every stage the pass can draw with, and the signature listing them.
        bool EnsureSignature()
        {
            if (mResourcesDone) {
                return !mSignatureFailed;
            }
            mResourcesDone = true;
            mSignatureFailed = true;
            if (!EnsurePixel() || !gShared.device) {
                return false;
            }
            fx::EmittedStage vertexProbe;
            const fx::ShaderEntry& vertexEntry = Info().vertexShader;
            if (vertexEntry.kind == fx::ShaderEntry::Kind::Compile) {
                fx::VertexInputLayout full;
                for (fx::InputKind& kind : full.kinds) {
                    kind = fx::InputKind::Float;
                }
                full.kinds[fx::kSlotColor0] = fx::InputKind::UNormBgra;
                std::vector<fx::Diagnostic> diagnostics;
                if (!mEffect.Emitter()->EmitVertexShader(vertexEntry, full, mPixel->stage.varyings, vertexProbe, diagnostics)) {
                    ++gCounters.generationFailures;
                    std::string text;
                    for (const fx::Diagnostic& diagnostic : diagnostics) {
                        text += diagnostic.message + "; ";
                    }
                    Log(Where() + ": vertex shader not generated: " + text);
                    return false;
                }
            }
            const fx::EmittedStage& pixel = mPixel->stage;

            std::deque<std::string> names; // keeps the strings the descs point to (stable addresses)
            std::vector<dg::PipelineResourceDesc> resources;
            std::vector<dg::ImmutableSamplerDesc> samplers;
            resources.emplace_back(dg::SHADER_TYPE_VERTEX | dg::SHADER_TYPE_PIXEL, "FxGenDraw", 1U,
                                   dg::SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, dg::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE);
            const bool params = vertexProbe.usesParams || pixel.usesParams;
            if (params) {
                resources.emplace_back(StagesOf(vertexProbe.usesParams, pixel.usesParams), "FxGenParams", 1U,
                                       dg::SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, dg::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE);
            }
            if (gShared.api == GraphicsApi::OpenGL) {
                return EnsureSignatureCombined(vertexEntry, vertexProbe, pixel, resources, samplers, names, params);
            }

            std::map<std::string, std::pair<fx::TextureResource, std::pair<bool, bool>>> textures;
            for (const fx::TextureResource& texture : vertexProbe.textures) {
                textures[texture.name] = {texture, {true, false}};
            }
            for (const fx::TextureResource& texture : pixel.textures) {
                auto it = textures.find(texture.name);
                if (it == textures.end()) {
                    textures[texture.name] = {texture, {false, true}};
                } else {
                    it->second.second.second = true;
                }
            }
            for (const auto& [name, entry] : textures) {
                names.push_back(name);
                resources.emplace_back(StagesOf(entry.second.first, entry.second.second), names.back().c_str(), 1U,
                                       dg::SHADER_RESOURCE_TYPE_TEXTURE_SRV, dg::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);
                TextureBinding binding;
                binding.resource = entry.first;
                binding.variableName = name;
                binding.pixelStage = entry.second.second;
                mTextureVariables.push_back(binding);
            }
            struct SamplerUse
            {
                fx::SamplerResource resource;
                bool vertex = false;
                bool pixel = false;
            };
            std::map<std::string, SamplerUse> samplerStages;
            for (const fx::SamplerResource& sampler : vertexProbe.samplers) {
                samplerStages[sampler.name].resource = sampler;
                samplerStages[sampler.name].vertex = true;
            }
            for (const fx::SamplerResource& sampler : pixel.samplers) {
                samplerStages[sampler.name].resource = sampler;
                samplerStages[sampler.name].pixel = true;
            }
            for (const auto& [name, use] : samplerStages) {
                names.push_back(name);
                const int index = use.resource.samplerParameter;
                const fx::ParameterInfo* const parameter = index >= 0 ? &mEffect.metadata.parameters[static_cast<std::size_t>(index)] : nullptr;
                dg::SamplerDesc sampler = SamplerOf(parameter, mEffect.name);
                if (use.resource.clampAddress) {
                    // The cube faces (FxGenSampleCubeD3D9): D3D9 clamps at a face's edges.
                    sampler.AddressU = sampler.AddressV = sampler.AddressW = dg::TEXTURE_ADDRESS_CLAMP;
                }
                samplers.emplace_back(StagesOf(use.vertex, use.pixel), names.back().c_str(), sampler);
            }

            return CreateSignature(resources, samplers, pixel, params, false);
        }

        // The signature and its binding, from the resources the stages use; then the variables.
        bool CreateSignature(const std::vector<dg::PipelineResourceDesc>& resources, const std::vector<dg::ImmutableSamplerDesc>& samplers,
                             const fx::EmittedStage& pixel, const bool params, const bool combinedSamplers)
        {
            const std::string signatureName = Where();
            dg::PipelineResourceSignatureDesc desc;
            desc.Name = signatureName.c_str();
            desc.Resources = resources.data();
            desc.NumResources = static_cast<dg::Uint32>(resources.size());
            desc.ImmutableSamplers = samplers.empty() ? nullptr : samplers.data();
            desc.NumImmutableSamplers = static_cast<dg::Uint32>(samplers.size());
            desc.BindingIndex = 0;
            desc.UseCombinedTextureSamplers = combinedSamplers ? dg::True : dg::False;
            gShared.device->CreatePipelineResourceSignature(desc, &mSignature);
            if (!mSignature) {
                ++gCounters.generationFailures;
                Log(Where() + ": resource signature not created");
                return false;
            }
            mSignature->CreateShaderResourceBinding(&mBinding, true);
            if (!mBinding) {
                ++gCounters.generationFailures;
                Log(Where() + ": shader resource binding not created");
                return false;
            }
            if (dg::IShaderResourceVariable* const draw = mBinding->GetVariableByName(dg::SHADER_TYPE_PIXEL, "FxGenDraw")) {
                draw->Set(DrawBuffer());
            }
            if (params) {
                const dg::SHADER_TYPE stage = pixel.usesParams ? dg::SHADER_TYPE_PIXEL : dg::SHADER_TYPE_VERTEX;
                if (dg::IShaderResourceVariable* const variable = mBinding->GetVariableByName(stage, "FxGenParams")) {
                    variable->Set(mEffect.ParamsBuffer());
                }
            }
            for (TextureBinding& binding : mTextureVariables) {
                binding.variable = mBinding->GetVariableByName(binding.pixelStage ? dg::SHADER_TYPE_PIXEL : dg::SHADER_TYPE_VERTEX,
                                                               binding.variableName.c_str());
            }
            mSignatureFailed = false;
            return true;
        }

        // GL (ShaderCompileDiligent.h): GL has no separate samplers, so the signature lists one texture
        // per (texture, sampler) pair the compiled stages combined, named as the GLSL names it, with the
        // pair's sampler_state as its immutable sampler. Each pair is bound to its texture parameter.
        bool EnsureSignatureCombined(const fx::ShaderEntry& vertexEntry, const fx::EmittedStage& vertexProbe, const fx::EmittedStage& pixel,
                                     std::vector<dg::PipelineResourceDesc>& resources, std::vector<dg::ImmutableSamplerDesc>& samplers,
                                     std::deque<std::string>& names, const bool params)
        {
            std::vector<GlCombinedSampler> vertexCombined;
            if (vertexEntry.kind == fx::ShaderEntry::Kind::Compile) {
                // The probe (every input a float, COLOR0 a D3DCOLOR) reads the same resources as the
                // shader of any input layout, so its combined samplers are the program's.
                dg::RefCntAutoPtr<dg::IShader> probe = Compile(dg::SHADER_TYPE_VERTEX, vertexProbe, "/vs-probe", &vertexCombined);
                if (!probe) {
                    return false;
                }
            }
            struct Use
            {
                GlCombinedSampler combined;
                bool vertex = false;
                bool pixel = false;
            };
            std::map<std::string, Use> uses;
            for (const GlCombinedSampler& combined : vertexCombined) {
                uses[combined.name].combined = combined;
                uses[combined.name].vertex = true;
            }
            for (const GlCombinedSampler& combined : mPixel->combined) {
                uses[combined.name].combined = combined;
                uses[combined.name].pixel = true;
            }
            auto findTexture = [&](const std::string& name) -> const fx::TextureResource* {
                for (const std::vector<fx::TextureResource>* list : {&pixel.textures, &vertexProbe.textures}) {
                    for (const fx::TextureResource& texture : *list) {
                        if (texture.name == name) {
                            return &texture;
                        }
                    }
                }
                return nullptr;
            };
            auto findSampler = [&](const std::string& name) -> const fx::SamplerResource* {
                for (const std::vector<fx::SamplerResource>* list : {&pixel.samplers, &vertexProbe.samplers}) {
                    for (const fx::SamplerResource& sampler : *list) {
                        if (sampler.name == name) {
                            return &sampler;
                        }
                    }
                }
                return nullptr;
            };
            for (const auto& [name, use] : uses) {
                const fx::TextureResource* const texture = findTexture(use.combined.texture);
                if (texture == nullptr) {
                    ++gCounters.generationFailures;
                    Log(Where() + ": GL combined sampler " + name + " names texture " + use.combined.texture + ", which the stages do not list");
                    return false;
                }
                names.push_back(name);
                resources.emplace_back(StagesOf(use.vertex, use.pixel), names.back().c_str(), 1U, dg::SHADER_RESOURCE_TYPE_TEXTURE_SRV,
                                       dg::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC, dg::PIPELINE_RESOURCE_FLAG_COMBINED_SAMPLER);
                TextureBinding binding;
                binding.resource = *texture;
                binding.variableName = name;
                binding.pixelStage = use.pixel;
                mTextureVariables.push_back(binding);
                if (!use.combined.sampler.empty()) {
                    const fx::SamplerResource* const sampler = findSampler(use.combined.sampler);
                    const int index = sampler != nullptr ? sampler->samplerParameter : -1;
                    const fx::ParameterInfo* const parameter = index >= 0 ? &mEffect.metadata.parameters[static_cast<std::size_t>(index)] : nullptr;
                    dg::SamplerDesc desc = SamplerOf(parameter, mEffect.name);
                    if (sampler != nullptr && sampler->clampAddress) {
                        desc.AddressU = desc.AddressV = desc.AddressW = dg::TEXTURE_ADDRESS_CLAMP;
                    }
                    samplers.emplace_back(StagesOf(use.vertex, use.pixel), names.back().c_str(), desc);
                }
            }
            return CreateSignature(resources, samplers, pixel, params, true);
        }

        EffectGpu::Impl& mEffect;
        int mTechnique = 0;
        int mPass = 0;
        std::vector<PassStateAssignment> mStates;
        std::vector<std::uint32_t> mSnapshot;
        std::vector<ShaderResourceSource*> mSnapshotTextures;
        std::unique_ptr<PixelStage> mPixel;
        bool mResourcesDone = false;
        bool mSignatureFailed = false;
        dg::RefCntAutoPtr<dg::IPipelineResourceSignature> mSignature;
        dg::RefCntAutoPtr<dg::IShaderResourceBinding> mBinding;
        std::vector<TextureBinding> mTextureVariables;
        std::map<std::uint64_t, Program> mPrograms;
    };

    bool PassBindingImpl::GetProgram(const VertexInputDesc& input, PassProgram* const out)
    {
        if (!EnsureSignature()) {
            return false;
        }
        const bool fixedFunction = UsesFixedFunctionVertex(input);
        const std::uint64_t key = LayoutKey(input) | (fixedFunction ? (1ULL << 63) : 0ULL);
        auto it = mPrograms.find(key);
        if (it == mPrograms.end()) {
            Program program;
            fx::EmittedStage vertex;
            std::vector<fx::Diagnostic> diagnostics;
            const fx::VertexInputLayout layout = ToFxLayout(input);
            const bool ok = fixedFunction
                                ? mEffect.Emitter()->EmitFixedFunctionVertexShader(layout, mPixel->stage.varyings, vertex, diagnostics)
                                : mEffect.Emitter()->EmitVertexShader(Info().vertexShader, layout, mPixel->stage.varyings, vertex, diagnostics);
            if (!ok) {
                ++gCounters.generationFailures;
                std::string text;
                for (const fx::Diagnostic& diagnostic : diagnostics) {
                    text += diagnostic.message + "; ";
                }
                Log(Where() + ": vertex shader not generated: " + text);
                program.failed = true;
            } else {
                program.vertexShader = Compile(dg::SHADER_TYPE_VERTEX, vertex, fixedFunction ? "/vs-fixed" : "/vs");
                program.failed = !program.vertexShader;
                if (!program.failed) {
                    program.id = gNextProgramId.fetch_add(1);
                    ++gCounters.programsCompiled;
                }
            }
            it = mPrograms.emplace(key, std::move(program)).first;
        }
        if (it->second.failed) {
            return false;
        }
        out->vertexShader = it->second.vertexShader;
        out->pixelShader = mPixel->shader;
        out->signature = mSignature;
        out->id = it->second.id;
        out->renderTargetCount = mPixel->stage.renderTargets;
        return true;
    }

    bool PassBindingImpl::Commit(dg::IDeviceContext* const context, const DrawState& state)
    {
        if (context == nullptr || !mBinding || mSignatureFailed) {
            return false;
        }
        // Effect parameters, as of BeginPass/CommitChanges.
        if (mEffect.layout.registerCount != 0) {
            dg::IBuffer* const buffer = mEffect.ParamsBuffer();
            if (buffer == nullptr) {
                return false;
            }
            if (!mEffect.uploadedValid || mEffect.uploadedFrame != state.frameIndex || mEffect.uploaded != mSnapshot) {
                void* mapped = nullptr;
                context->MapBuffer(buffer, dg::MAP_WRITE, dg::MAP_FLAG_DISCARD, mapped);
                if (mapped == nullptr) {
                    Log(mEffect.name + ": FxGenParams could not be mapped");
                    return false;
                }
                std::memcpy(mapped, mSnapshot.data(), mSnapshot.size() * sizeof(std::uint32_t));
                context->UnmapBuffer(buffer, dg::MAP_WRITE);
                mEffect.uploaded = mSnapshot;
                mEffect.uploadedFrame = state.frameIndex;
                mEffect.uploadedValid = true;
                ++gCounters.constantUploads;
            }
        }
        // Draw constants (fx::kDrawConstantRegisters).
        float draw[fx::kDrawConstantRegisters * 4] = {};
        draw[0] = state.viewport[0];
        draw[1] = state.viewport[1];
        draw[2] = state.viewport[2];
        draw[3] = state.viewport[3];
        draw[4] = state.viewport[4];
        draw[5] = state.viewport[5];
        draw[8] = state.positionOffset[0];
        draw[9] = state.positionOffset[1];
        // D3D9 compares the pixel's alpha with ALPHAREF, an 8-bit value (the low byte is used).
        draw[12] = state.alphaTestEnable != 0 ? static_cast<float>(state.alphaFunc) : 8.0F;
        draw[13] = static_cast<float>(state.alphaRef & 0xFFU) / 255.0F;
        dg::IBuffer* const drawBuffer = DrawBuffer();
        if (drawBuffer == nullptr) {
            return false;
        }
        if (!gShared.drawValid || gShared.lastDrawFrame != state.frameIndex || std::memcmp(draw, gShared.lastDraw, sizeof(draw)) != 0) {
            void* mapped = nullptr;
            context->MapBuffer(drawBuffer, dg::MAP_WRITE, dg::MAP_FLAG_DISCARD, mapped);
            if (mapped == nullptr) {
                Log("FxGenDraw could not be mapped");
                return false;
            }
            std::memcpy(mapped, draw, sizeof(draw));
            context->UnmapBuffer(drawBuffer, dg::MAP_WRITE);
            std::memcpy(gShared.lastDraw, draw, sizeof(draw));
            gShared.lastDrawFrame = state.frameIndex;
            gShared.drawValid = true;
            ++gCounters.drawConstantUploads;
        }
        // Textures, as of BeginPass/CommitChanges.
        for (TextureBinding& binding : mTextureVariables) {
            dg::ITextureView* view = nullptr;
            const int parameter = binding.resource.textureParameter;
            ShaderResourceSource* const source =
                parameter >= 0 && static_cast<std::size_t>(parameter) < mSnapshotTextures.size() ? mSnapshotTextures[static_cast<std::size_t>(parameter)] : nullptr;
            if (source != nullptr) {
                const ShaderResourceDimension dimension = source->GetShaderResourceDimension();
                const fx::TextureDim wanted = binding.resource.dim;
                const bool matches = (wanted == fx::TextureDim::Tex2D && dimension == ShaderResourceDimension::Texture2D) ||
                                     (wanted == fx::TextureDim::Tex3D && dimension == ShaderResourceDimension::Texture3D) ||
                                     (wanted == fx::TextureDim::Cube && dimension == ShaderResourceDimension::TextureCube);
                if (matches) {
                    view = source->GetShaderResourceView();
                    if (binding.resource.cubeFaces) {
                        view = CubeFacesView(view);
                    }
                }
            }
            if (view == nullptr) {
                view = binding.resource.cubeFaces ? NullFacesView() : NullView(binding.resource.dim);
                ++gCounters.nullTextureBinds;
            }
            if (binding.variable != nullptr && view != binding.bound) {
                binding.variable->Set(view);
                binding.bound = view;
            }
        }
        context->CommitShaderResources(mBinding, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        ++gCounters.commits;
        return true;
    }

    void EffectGpu::Impl::ReleaseGpu()
    {
        for (auto& technique : passes) {
            for (auto& pass : technique) {
                if (pass) {
                    pass->ReleaseGpu();
                }
            }
        }
        paramsBuffer.Release();
        uploadedValid = false;
    }

    fx::HlslEmitter* EffectGpu::Impl::Emitter()
    {
        if (!emitter && !emitterFailed) {
            std::vector<fx::Diagnostic> diagnostics;
            emitter = fx::HlslEmitter::Create(input, metadata, diagnostics);
            if (!emitter) {
                emitterFailed = true;
                ++gCounters.generationFailures;
                std::string text;
                for (const fx::Diagnostic& diagnostic : diagnostics) {
                    text += diagnostic.message + "; ";
                }
                Log(name + ": the shader generator cannot read the effect: " + text);
            }
        }
        return emitter.get();
    }

    dg::IBuffer* EffectGpu::Impl::ParamsBuffer()
    {
        if (!paramsBuffer && gShared.device) {
            const std::string bufferName = name + "/FxGenParams";
            dg::BufferDesc desc;
            desc.Name = bufferName.c_str();
            desc.Size = std::max<std::uint32_t>(1, layout.registerCount) * 16U;
            desc.BindFlags = dg::BIND_UNIFORM_BUFFER;
            desc.Usage = dg::USAGE_DYNAMIC;
            desc.CPUAccessFlags = dg::CPU_ACCESS_WRITE;
            gShared.device->CreateBuffer(desc, nullptr, &paramsBuffer);
            uploadedValid = false;
        }
        return paramsBuffer;
    }

    const fx::ConstantSlot* EffectGpu::Impl::Slot(const int parameter) const
    {
        if (parameter < 0 || static_cast<std::size_t>(parameter) >= layout.slotOfParameter.size()) {
            return nullptr;
        }
        const int slot = layout.slotOfParameter[static_cast<std::size_t>(parameter)];
        return slot < 0 ? nullptr : &layout.slots[static_cast<std::size_t>(slot)];
    }

    // Writes component `component` (storage order: element, row, column) of a parameter.
    // sourceKind: 0 float bits, 1 int, 2 bool (as int), 3 raw (no conversion).
    bool EffectGpu::Impl::Write(const int parameter, const std::uint32_t component, const std::uint32_t word, const int sourceKind)
    {
        const fx::ConstantSlot* const slot = Slot(parameter);
        if (slot == nullptr) {
            return false;
        }
        std::uint32_t index = 0;
        fx::ParameterType type = fx::ParameterType::Float;
        if (!fx::ConstantComponentWord(*slot, component, index, type)) {
            return false; // past the parameter's last component
        }
        std::uint32_t value = word;
        if (sourceKind != 3) {
            // ID3DXEffect converts a value to the parameter's type.
            const bool isFloat = sourceKind == 0;
            const float asFloat = isFloat ? FloatBits(word) : static_cast<float>(static_cast<std::int32_t>(word));
            const std::int32_t asInt = isFloat ? static_cast<std::int32_t>(asFloat) : static_cast<std::int32_t>(word);
            switch (type) {
            case fx::ParameterType::Float:
                std::memcpy(&value, &asFloat, sizeof(value));
                break;
            case fx::ParameterType::Int:
                value = static_cast<std::uint32_t>(asInt);
                break;
            default: // Bool: TRUE is 1
                value = (isFloat ? asFloat != 0.0F : asInt != 0) ? 1U : 0U;
                break;
            }
        }
        registers[index] = value;
        return true;
    }

    // ---- public --------------------------------------------------------------------------------

    void SetEffectLog(const EffectLogFn log)
    {
        gLog = log;
    }

    void SetEffectTrilinearMode(const int mode)
    {
        std::lock_guard<std::mutex> lock(gLock);
        gTrilinearMode = mode;
    }

    void SetEffectShaderCompiler(const ShaderCompilerFn compiler)
    {
        std::lock_guard<std::mutex> lock(gLock);
        gCompiler = compiler;
    }

    void SetPassSink(PassSink* const sink)
    {
        std::lock_guard<std::mutex> lock(gLock);
        gSink = sink;
    }

    PassSink* GetPassSink()
    {
        std::lock_guard<std::mutex> lock(gLock);
        return gSink;
    }

    void SetEffectRenderDevice(dg::IRenderDevice* const device)
    {
        std::lock_guard<std::mutex> lock(gLock);
        if (gShared.device.RawPtr() == device) {
            return;
        }
        for (GpuOwner* const owner : gOwners) {
            owner->ReleaseGpu();
        }
        gShared.drawBuffer.Release();
        gShared.drawValid = false;
        gShared.null2D.Release();
        gShared.null3D.Release();
        gShared.nullCube.Release();
        gShared.nullFaces.Release();
        gShared.cubeFaceViews.clear();
        gShared.device = device;
        gShared.api = GraphicsApi::D3D11;
        if (device != nullptr) {
            switch (device->GetDeviceInfo().Type) {
            case dg::RENDER_DEVICE_TYPE_VULKAN:
                gShared.api = GraphicsApi::Vulkan;
                break;
            case dg::RENDER_DEVICE_TYPE_GL:
            case dg::RENDER_DEVICE_TYPE_GLES:
                gShared.api = GraphicsApi::OpenGL;
                break;
            default:
                break;
            }
        }
    }

    EffectGpuStats GetEffectGpuStats()
    {
        EffectGpuStats stats;
        stats.effects = gCounters.effects;
        stats.programsCompiled = gCounters.programsCompiled;
        stats.shadersCompiled = gCounters.shadersCompiled;
        stats.generationFailures = gCounters.generationFailures;
        stats.constantUploads = gCounters.constantUploads;
        stats.drawConstantUploads = gCounters.drawConstantUploads;
        stats.commits = gCounters.commits;
        stats.nullTextureBinds = gCounters.nullTextureBinds;
        return stats;
    }

    EffectGpu::EffectGpu()
        : mImpl(std::make_unique<Impl>())
    {}

    EffectGpu::~EffectGpu()
    {
        std::lock_guard<std::mutex> lock(gLock);
        gOwners.erase(mImpl.get());
    }

    std::shared_ptr<EffectGpu> EffectGpu::Create(const std::string& name, const fx::EffectInput& input, std::string* const errors)
    {
        fx::FrontEndResult result = fx::BuildEffectMetadata(input);
        if (!result.ok) {
            if (errors != nullptr) {
                *errors = fx::FormatDiagnostics(result.source, result.diagnostics);
            }
            return nullptr;
        }
        std::shared_ptr<EffectGpu> effect(new EffectGpu());
        Impl& impl = *effect->mImpl;
        impl.name = name;
        impl.input = input;
        impl.metadata = std::move(result.metadata);
        impl.layout = fx::BuildConstantLayout(impl.metadata);
        impl.registers = impl.layout.defaults; // ID3DXEffect starts from the default values
        impl.textures.assign(impl.metadata.parameters.size(), nullptr);
        impl.passes.resize(impl.metadata.techniques.size());
        for (std::size_t t = 0; t < impl.metadata.techniques.size(); ++t) {
            impl.passes[t].resize(impl.metadata.techniques[t].passes.size());
        }
        ++gCounters.effects;
        std::lock_guard<std::mutex> lock(gLock);
        gOwners.insert(&impl);
        return effect;
    }

    const std::string& EffectGpu::GetName() const
    {
        return mImpl->name;
    }

    const fx::EffectMetadata& EffectGpu::GetMetadata() const
    {
        return mImpl->metadata;
    }

    int EffectGpu::FindParameter(const char* const name) const
    {
        if (name == nullptr) {
            return -1;
        }
        const std::vector<fx::ParameterInfo>& parameters = mImpl->metadata.parameters;
        for (std::size_t i = 0; i < parameters.size(); ++i) {
            if (parameters[i].desc.name == name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    int EffectGpu::FindTechnique(const char* const name) const
    {
        if (name == nullptr) {
            return -1;
        }
        const std::vector<fx::TechniqueInfo>& techniques = mImpl->metadata.techniques;
        for (std::size_t i = 0; i < techniques.size(); ++i) {
            if (techniques[i].name == name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    bool EffectGpu::SetFloats(const int parameter, const float* const values, const std::uint32_t count)
    {
        if (mImpl->Slot(parameter) == nullptr || values == nullptr) {
            return false;
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t word = 0;
            std::memcpy(&word, &values[i], sizeof(word));
            if (!mImpl->Write(parameter, i, word, 0)) {
                break; // past the parameter's last component
            }
        }
        return true;
    }

    bool EffectGpu::SetInts(const int parameter, const int* const values, const std::uint32_t count)
    {
        if (mImpl->Slot(parameter) == nullptr || values == nullptr) {
            return false;
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            if (!mImpl->Write(parameter, i, static_cast<std::uint32_t>(values[i]), 1)) {
                break;
            }
        }
        return true;
    }

    bool EffectGpu::SetBools(const int parameter, const int* const values, const std::uint32_t count)
    {
        if (mImpl->Slot(parameter) == nullptr || values == nullptr) {
            return false;
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            if (!mImpl->Write(parameter, i, values[i] != 0 ? 1U : 0U, 2)) {
                break;
            }
        }
        return true;
    }

    bool EffectGpu::SetVectors(const int parameter, const float* const vectors4, const std::uint32_t count)
    {
        const fx::ConstantSlot* const slot = mImpl->Slot(parameter);
        if (slot == nullptr || vectors4 == nullptr) {
            return false;
        }
        // Each vector fills one element (one row of a matrix element) up to the parameter's columns.
        const std::uint32_t perVector = slot->columns;
        for (std::uint32_t v = 0; v < count; ++v) {
            for (std::uint32_t c = 0; c < std::min<std::uint32_t>(perVector, 4); ++c) {
                std::uint32_t word = 0;
                std::memcpy(&word, &vectors4[v * 4 + c], sizeof(word));
                if (!mImpl->Write(parameter, v * perVector + c, word, 0)) {
                    return true;
                }
            }
        }
        return true;
    }

    bool EffectGpu::SetMatrices(const int parameter, const float* const matrices16, const std::uint32_t count)
    {
        const fx::ConstantSlot* const slot = mImpl->Slot(parameter);
        if (slot == nullptr || matrices16 == nullptr || !slot->matrix) {
            return false;
        }
        // The top-left rows x columns of each D3DXMATRIX, row by row: the matrix the shader multiplies with.
        const std::uint32_t perElement = slot->rows * slot->columns;
        for (std::uint32_t m = 0; m < count; ++m) {
            for (std::uint32_t r = 0; r < slot->rows; ++r) {
                for (std::uint32_t c = 0; c < slot->columns; ++c) {
                    std::uint32_t word = 0;
                    std::memcpy(&word, &matrices16[m * 16 + r * 4 + c], sizeof(word));
                    if (!mImpl->Write(parameter, m * perElement + r * slot->columns + c, word, 0)) {
                        return true;
                    }
                }
            }
        }
        return true;
    }

    bool EffectGpu::SetRaw(const int parameter, const void* const data, const std::uint32_t bytes)
    {
        if (mImpl->Slot(parameter) == nullptr || data == nullptr) {
            if (parameter >= 0 && static_cast<std::size_t>(parameter) < mImpl->metadata.parameters.size() &&
                mImpl->metadata.parameters[static_cast<std::size_t>(parameter)].desc.parameterClass == fx::ParameterClass::Struct) {
                Log(mImpl->name + ": SetValue on struct parameter " + mImpl->metadata.parameters[static_cast<std::size_t>(parameter)].desc.name +
                    " is not supported yet");
            }
            return false;
        }
        const auto* const words = static_cast<const std::uint8_t*>(data);
        for (std::uint32_t i = 0; i < bytes / 4U; ++i) {
            std::uint32_t word = 0;
            std::memcpy(&word, words + static_cast<std::size_t>(i) * 4U, sizeof(word));
            if (!mImpl->Write(parameter, i, word, 3)) {
                break;
            }
        }
        return true;
    }

    bool EffectGpu::SetTexture(const int parameter, ShaderResourceSource* const source)
    {
        if (parameter < 0 || static_cast<std::size_t>(parameter) >= mImpl->textures.size()) {
            return false;
        }
        const fx::ParameterType type = mImpl->metadata.parameters[static_cast<std::size_t>(parameter)].desc.type;
        if (type != fx::ParameterType::Texture && type != fx::ParameterType::Texture1D && type != fx::ParameterType::Texture2D &&
            type != fx::ParameterType::Texture3D && type != fx::ParameterType::TextureCube) {
            return false;
        }
        mImpl->textures[static_cast<std::size_t>(parameter)] = source;
        return true;
    }

    PassBinding* EffectGpu::BeginPass(const int technique, const int pass)
    {
        if (technique < 0 || static_cast<std::size_t>(technique) >= mImpl->passes.size() || pass < 0 ||
            static_cast<std::size_t>(pass) >= mImpl->passes[static_cast<std::size_t>(technique)].size()) {
            return nullptr;
        }
        std::unique_ptr<PassBindingImpl>& binding = mImpl->passes[static_cast<std::size_t>(technique)][static_cast<std::size_t>(pass)];
        if (!binding) {
            binding = std::make_unique<PassBindingImpl>(*mImpl, technique, pass);
        }
        binding->Snapshot();
        return binding.get();
    }

    void EffectGpu::CommitChanges(PassBinding* const binding)
    {
        if (binding != nullptr) {
            static_cast<PassBindingImpl*>(binding)->Snapshot();
        }
    }
} // namespace gpg::gal::diligent
