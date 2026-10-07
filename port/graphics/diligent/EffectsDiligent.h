#pragma once

// Metadata-only effects for the Diligent spike (M6 step 1, Windows oracle mode).
//
// CD3DDeviceResources::DevResInitResources compiles every /effects/*.fx at device init and calls
// gpg::Die on any failure (CD3DDeviceResources.cpp:805-842), and CD3DEffect reads each effect's
// technique list and the abstractTechnique/fidelity annotations (CD3DEffectTechnique.cpp:517-568).
// Until the portable front end (port/graphics/fx, step 2) provides the metadata, it comes from D3DX
// reflection: the effect is compiled exactly as DeviceD3D9::CreateEffectFromSourceBuffer does
// (D3D9Interfaces.cpp:1529-1626: D3DXCreateEffectCompiler with D3DXSHADER_DEBUG |
// D3DXSHADER_USE_LEGACY_D3DX9_31_DLL, CompileEffect, D3DXCreateEffect) and created on the oracle's
// NULLREF device. GetTechniques returns the techniques in declaration order, as
// EffectD3D9::GetTechniques walks them with FindNextValidTechnique, keeping those whose passes'
// shader versions the HAL supports (FindNextValidTechnique itself crashes on a NULLREF device,
// D3D9Oracle.h).
//
// Draws are no-ops in this step, so nothing is bound: BeginPass/EndPass only count, and texture
// variables are ignored. Numeric variable values are forwarded to the D3DX effect, which keeps them
// with D3DX's own semantics for step 3 to read.

#include <d3dx9effect.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "boost/enable_shared_from_this.h"
#include "boost/shared_ptr.h"
#include "boost/weak_ptr.h"
#include "gpg/gal/Effect.hpp"
#include "gpg/gal/EffectContext.hpp"
#include "gpg/gal/EffectTechnique.hpp"
#include "gpg/gal/EffectVariable.hpp"

namespace gpg::gal::diligent
{
    class D3D9Oracle;

    /** What the report lists per effect: its source path and the techniques GetTechniques returned. */
    struct EffectRecord
    {
        std::string sourcePath;
        bool fromCache = false;
        std::uint32_t techniqueCount = 0;   // D3DXEFFECT_DESC::Techniques
        std::uint32_t parameterCount = 0;   // D3DXEFFECT_DESC::Parameters
        std::vector<std::string> validTechniques; // FindNextValidTechnique order
    };

    /** Process-wide list of the effects created so far (the report reads it). */
    std::vector<EffectRecord> GetEffectRecords();

    /**
     * DeviceD3D9::CreateEffect's two paths (cache or source) on the NULLREF device. When `dumpDir`
     * is not empty, the source buffer the engine passed (compat header + .fx, as
     * CD3DEffect::InitEffectFromFile concatenates them) is written there with its macros, for
     * tools/fxtechlist.cpp to compile on a HAL device.
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
            ID3DXEffect* effect,
            std::size_t recordIndex,
            DWORD halVertexShaderVersion,
            DWORD halPixelShaderVersion
        );
        ~EffectDiligent() override;

        EffectContext* GetContext() override;
        void GetTechniques(msvc8::vector<boost::shared_ptr<EffectTechnique>>& outTechniques) override;
        boost::shared_ptr<EffectVariable> GetVariable(const char* name) override;
        boost::shared_ptr<EffectTechnique> GetTechnique(const char* name) override;
        void OnReset() override;
        void OnLost() override;

        [[nodiscard]] ID3DXEffect* GetDxEffect();

    private:
        EffectContext context_;
        ID3DXEffect* dxEffect_ = nullptr;
        [[nodiscard]] bool IsTechniqueValid(D3DXHANDLE technique);

        std::size_t recordIndex_ = 0;
        bool recorded_ = false;
        DWORD halVertexShaderVersion_ = 0;
        DWORD halPixelShaderVersion_ = 0;
    };

    class EffectTechniqueDiligent final : public EffectTechnique
    {
    public:
        EffectTechniqueDiligent(const char* name, const boost::shared_ptr<EffectDiligent>& effect, D3DXHANDLE handle);
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

    private:
        msvc8::string name_;
        boost::weak_ptr<EffectDiligent> effect_;
        D3DXHANDLE handle_ = nullptr;
        bool beginEndActive_ = false;
        int passCount_ = 0;
    };

    class EffectVariableDiligent final : public EffectVariable
    {
    public:
        EffectVariableDiligent(const char* name, const boost::shared_ptr<EffectDiligent>& effect, D3DXHANDLE handle);
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
        [[nodiscard]] ID3DXEffect* Effect();

        msvc8::string name_;
        boost::weak_ptr<EffectDiligent> effect_;
        D3DXHANDLE handle_ = nullptr;
    };
} // namespace gpg::gal::diligent
