#pragma once

// gal Effect, EffectTechnique and EffectVariable on Diligent (M6b): the engine side of the effect
// layer. The Diligent side is EffectsDiligentGpu.*; the contract with the draw path is PassBinding.h.
//
// CD3DDeviceResources::DevResInitResources compiles every /effects/*.fx at device init and calls
// gpg::Die on any failure (CD3DDeviceResources.cpp:805-842); CD3DEffect then reads each effect's
// valid techniques and their abstractTechnique/fidelity annotations (CD3DEffectTechnique.cpp:517-568).
// Everything ID3DXEffect reported for that comes from the portable front end (port/graphics/fx,
// FxMetadata, equal to D3DX reflection for every effect variant, gate 2 of M6a), and the shaders are
// SM5 HLSL that FxHlslEmitter generates from the same source when a pass is first drawn. D3DX is no
// longer involved; only the HAL caps that decide which techniques are valid come from the D3D9
// oracle (D3D9Oracle.h), as in step 1.
//
// D3D9 behaviour kept (EffectTechniqueD3D9 / EffectVariableD3D9, D3D9Interfaces.cpp:4041-4996):
//   - GetTechniques lists the valid techniques in declaration order (FindNextValidTechnique's
//     order, D3D9Interfaces.cpp:4226-4250); a technique is valid when every pass's shaders are
//     within the adapter's shader versions;
//   - GetVariable/GetTechnique throw gal::Error for a missing name;
//   - BeginTechnique calls Device slot 48 (the technique-begin state defaults) and returns the pass
//     count; BeginPass hands the draw path the pass binding (PassSink::OnBeginPass) with the
//     parameter snapshot D3DX would apply at that point; the begin/end mismatch errors are the same;
//   - variable setters follow ID3DXEffect's semantics (EffectsDiligentGpu.h); textures stay referenced
//     by the effect until replaced, as ID3DXEffect::SetTexture AddRefs them.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "boost/enable_shared_from_this.h"
#include "boost/shared_ptr.h"
#include "boost/weak_ptr.h"
#include "gpg/gal/CubeRenderTarget.hpp"
#include "gpg/gal/Effect.hpp"
#include "gpg/gal/EffectContext.hpp"
#include "gpg/gal/EffectTechnique.hpp"
#include "gpg/gal/EffectVariable.hpp"
#include "gpg/gal/RenderTarget.hpp"
#include "gpg/gal/Texture.hpp"

namespace gpg::gal::diligent
{
    class D3D9Oracle;
    class EffectGpu;
    class PassBinding;

    /** What the report lists per effect: its source path and the techniques GetTechniques returned. */
    struct EffectRecord
    {
        std::string sourcePath;
        bool fromCache = false;
        std::uint32_t techniqueCount = 0;   // as D3DXEFFECT_DESC::Techniques
        std::uint32_t parameterCount = 0;   // as D3DXEFFECT_DESC::Parameters
        std::vector<std::string> validTechniques; // FindNextValidTechnique order
    };

    /** Process-wide list of the effects created so far (the report reads it). */
    std::vector<EffectRecord> GetEffectRecords();

    /**
     * The effect layer's counters (EffectsDiligentGpu.h EffectGpuStats) for the report, so a menu run
     * shows that its shaders came from FxHlslEmitter at run time and that every constant upload was
     * a Map(DISCARD). Copied here so DeviceDiligent.cpp needs no fx header.
     */
    struct EffectLayerCounts
    {
        std::uint64_t effects = 0;
        std::uint64_t programsCompiled = 0;
        std::uint64_t shadersCompiled = 0;
        std::uint64_t generationFailures = 0;
        std::uint64_t constantUploads = 0;
        std::uint64_t drawConstantUploads = 0;
        std::uint64_t commits = 0;
        std::uint64_t nullTextureBinds = 0;
    };
    EffectLayerCounts GetEffectLayerCounts();

    /**
     * DeviceD3D9::CreateEffect (D3D9Interfaces.cpp:1522-1690) on the portable front end. The source
     * buffer the engine passes (compat header + .fx, as CD3DEffect::InitEffectFromFile concatenates
     * them) is read in both of D3D9's paths: CD3DEffect hands it over with the cache flag too, and
     * this backend writes no compiled cache. When `dumpDir` is not empty the buffer is written there
     * with its macros, for tools/fxtechlist.cpp to compile on a HAL device (gate 1).
     */
    boost::shared_ptr<Effect> CreateEffectFromContext(
        const D3D9Oracle& oracle,
        const EffectContext& context,
        const std::string& dumpDir
    );

    class EffectDiligent final : public boost::enable_shared_from_this<EffectDiligent>, public Effect
    {
    public:
        EffectDiligent(
            const EffectContext& context,
            std::shared_ptr<EffectGpu> gpu,
            std::size_t recordIndex,
            std::uint32_t maxVertexShaderVersion,
            std::uint32_t maxPixelShaderVersion
        );
        ~EffectDiligent() override;

        EffectContext* GetContext() override;
        void GetTechniques(msvc8::vector<boost::shared_ptr<EffectTechnique>>& outTechniques) override;
        boost::shared_ptr<EffectVariable> GetVariable(const char* name) override;
        boost::shared_ptr<EffectTechnique> GetTechnique(const char* name) override;
        void OnReset() override;
        void OnLost() override;

        [[nodiscard]] EffectGpu& Gpu();

        /** The objects bound to texture parameters, kept alive as ID3DXEffect::SetTexture does. */
        struct TextureHold
        {
            boost::shared_ptr<Texture> texture;
            boost::shared_ptr<RenderTarget> renderTarget;
            boost::shared_ptr<CubeRenderTarget> cubeTarget;
        };
        [[nodiscard]] std::vector<TextureHold>& TextureHolds() { return textureHolds_; }

    private:
        EffectContext context_;
        std::shared_ptr<EffectGpu> gpu_;
        std::size_t recordIndex_ = 0;
        bool recorded_ = false;
        std::uint32_t maxVertexShaderVersion_ = 0; // the HAL caps' VertexShaderVersion, low word (0x0300)
        std::uint32_t maxPixelShaderVersion_ = 0;
        std::vector<TextureHold> textureHolds_; // per parameter
    };

    class EffectTechniqueDiligent final : public EffectTechnique
    {
    public:
        EffectTechniqueDiligent(const char* name, const boost::shared_ptr<EffectDiligent>& effect, int technique);
        ~EffectTechniqueDiligent() override;

        msvc8::string* GetName() override;
        int BeginTechnique() override;
        void EndTechnique() override;
        void BeginPass(int pass) override;
        void EndPass() override;
        bool GetAnnotationBool(bool* outValue, const msvc8::string& annotationName) override;
        bool GetAnnotationInt(int* outValue, const msvc8::string& annotationName) override;
        bool GetAnnotationFloat(float* outValue, const msvc8::string& annotationName) override;
        bool GetAnnotationString(msvc8::string* outValue, const msvc8::string& annotationName) override;

        /** EffectTechniqueD3D9::CommitChanges (FAF): parameters set inside the pass reach the draws. */
        void CommitChanges();

    private:
        msvc8::string name_;
        boost::weak_ptr<EffectDiligent> effect_;
        int technique_ = 0;
        bool beginEndActive_ = false;
        int passCount_ = 0;
        PassBinding* activePass_ = nullptr;
        std::vector<EffectDiligent::TextureHold> passHolds_; // what the active pass's snapshot binds
    };

    class EffectVariableDiligent final : public EffectVariable
    {
    public:
        EffectVariableDiligent(const char* name, const boost::shared_ptr<EffectDiligent>& effect, int parameter);
        ~EffectVariableDiligent() override;

        msvc8::string* GetName() override;
        void SetCubeRenderTarget(boost::shared_ptr<CubeRenderTarget> cubeTarget) override;
        void SetRenderTarget(boost::shared_ptr<RenderTarget> renderTarget) override;
        void SetTexture(boost::shared_ptr<Texture> texture) override;
        void SetMatrix4x4(const Matrix* matrix) override;
        void SetFloatArray(std::uint32_t count, const float* values) override;
        void SetVector(const float* vector4) override;
        void SetValue(const void* data, std::uint32_t byteCount) override;
        void SetFloat(float value) override;
        void SetInt(int value) override;
        void SetBool(bool value) override;
        void SetMatrixArray(std::uint32_t count, const Matrix* matrices) override;
        void SetVectorArray(std::uint32_t count, const float* vectors4) override;
        bool GetAnnotationBool(bool* outValue, const msvc8::string& annotationName) override;
        bool GetAnnotationInt(int* outValue, const msvc8::string& annotationName) override;
        bool GetAnnotationFloat(float* outValue, const msvc8::string& annotationName) override;
        bool GetAnnotationString(msvc8::string* outValue, const msvc8::string& annotationName) override;

    private:
        [[nodiscard]] boost::shared_ptr<EffectDiligent> Effect(int line);
        void BindTexture(EffectDiligent::TextureHold hold, void* source, int line);

        msvc8::string name_;
        boost::weak_ptr<EffectDiligent> effect_;
        int parameter_ = -1;
    };
} // namespace gpg::gal::diligent
