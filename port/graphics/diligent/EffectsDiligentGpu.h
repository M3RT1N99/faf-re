#pragma once

// The Diligent side of the effect layer: one effect's parameters, its passes as PassBindings, and
// the shaders FxHlslEmitter generates for them, compiled through Diligent at first use.
//
// What ID3DXEffect keeps per effect and applies at BeginPass (D3D9Interfaces.cpp:4084-4110,
// 4739) lives here, without D3DX:
//   - the parameter values, in the register layout of fx::BuildConstantLayout. The setters follow
//     ID3DXEffect's (SetFloatArray, SetVectorArray, SetMatrixArray, SetValue, SetInt, SetBool):
//     components in storage order (element, row, column), converted to the parameter's type;
//   - the textures set on texture parameters;
//   - BeginPass takes a snapshot of both (D3DX applies parameters only at BeginPass and
//     CommitChanges), which the pass binding uploads at its next Commit.
//
// This header names neither engine types (boost, msvc8, gal objects) nor Diligent's, so both the
// engine's EffectsDiligent.cpp and the host tool fxdiff (port/graphics/fx/tools/fxdiff) use it;
// EffectsDiligentGpu.cpp is the only TU behind it that includes Diligent.

#include <cstdint>
#include <memory>
#include <string>

// The effect layer is built on the portable front end: its headers (port/graphics/fx/include) must be
// on the include path and its sources (port/graphics/fx/src/*.cpp) compiled into the same image.
#if defined(__has_include)
#if !__has_include("gpg/gal/fx/FxMetadata.h")
#error "port/graphics/fx/include is not on the include path; the graphics build needs port/graphics/fx (port_graphics.props, M6b)"
#endif
#endif
#include "gpg/gal/fx/FxMetadata.h"
#include "port/graphics/diligent/PassBinding.h"

namespace gpg::gal::diligent
{
    /** Process-wide counters for reports and tests. */
    struct EffectGpuStats
    {
        std::uint64_t effects = 0;              // EffectGpu objects created
        std::uint64_t programsCompiled = 0;     // PassProgram objects made (vertex + pixel shader pairs)
        std::uint64_t shadersCompiled = 0;      // IShader objects made
        std::uint64_t generationFailures = 0;   // emitter or compiler failures (each logged once)
        std::uint64_t constantUploads = 0;      // FxGenParams maps (MAP_FLAG_DISCARD)
        std::uint64_t drawConstantUploads = 0;  // FxGenDraw maps (MAP_FLAG_DISCARD)
        std::uint64_t commits = 0;              // PassBinding::Commit calls that committed
        std::uint64_t nullTextureBinds = 0;     // texture resources bound to the null texture
    };

    /** Where the effect layer reports problems (once each); nullptr: OutputDebugString/stderr only. */
    using EffectLogFn = void (*)(const char* message);
    void SetEffectLog(EffectLogFn log);

    /** The sink SetPassSink registered (PassBinding.h), or nullptr. */
    [[nodiscard]] PassSink* GetPassSink();

    [[nodiscard]] EffectGpuStats GetEffectGpuStats();

    class EffectGpu
    {
    public:
        /**
         * Runs the portable front end (fx::BuildEffectMetadata) over `input`. Returns nullptr and
         * fills `errors` (the front end's diagnostics) when the effect does not parse; D3DX would
         * have failed D3DXCreateEffect then too.
         */
        [[nodiscard]] static std::shared_ptr<EffectGpu> Create(const std::string& name, const fx::EffectInput& input, std::string* errors);

        ~EffectGpu();
        EffectGpu(const EffectGpu&) = delete;
        EffectGpu& operator=(const EffectGpu&) = delete;

        [[nodiscard]] const std::string& GetName() const;
        [[nodiscard]] const fx::EffectMetadata& GetMetadata() const;
        /** Metadata index of a top-level parameter, or -1 (ID3DXEffect::GetParameterByName(NULL, name)). */
        [[nodiscard]] int FindParameter(const char* name) const;
        /** Metadata index of a technique, or -1 (GetTechniqueByName). */
        [[nodiscard]] int FindTechnique(const char* name) const;

        // ID3DXEffect setters. Each returns false where D3DX returns an error: no such parameter,
        // or a parameter with no numeric value (textures, samplers, strings, structs).
        bool SetFloats(int parameter, const float* values, std::uint32_t count);      // SetFloat, SetFloatArray
        bool SetInts(int parameter, const int* values, std::uint32_t count);          // SetInt, SetIntArray
        bool SetBools(int parameter, const int* values, std::uint32_t count);         // SetBool (BOOL), SetBoolArray
        bool SetVectors(int parameter, const float* vectors4, std::uint32_t count);   // SetVector, SetVectorArray
        bool SetMatrices(int parameter, const float* matrices16, std::uint32_t count); // SetMatrix, SetMatrixArray
        bool SetRaw(int parameter, const void* data, std::uint32_t bytes);           // SetValue
        /** SetTexture on a texture parameter; `source` may be null (unbinds). Not owned. */
        bool SetTexture(int parameter, ShaderResourceSource* source);

        /**
         * The binding of one pass with a snapshot of the parameters and textures taken now
         * (ID3DXEffect::BeginPass). nullptr if the indices are out of range. Owned by the effect.
         */
        [[nodiscard]] PassBinding* BeginPass(int technique, int pass);
        /** ID3DXEffect::CommitChanges: the binding takes a new snapshot. */
        void CommitChanges(PassBinding* binding);

        /** The implementation (EffectsDiligentGpu.cpp); public only so the pass bindings can name it. */
        struct Impl;

    private:
        EffectGpu();
        std::unique_ptr<Impl> mImpl;
    };
} // namespace gpg::gal::diligent
