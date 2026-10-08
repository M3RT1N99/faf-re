#pragma once

// Every shader the backend makes goes through here (M6c step 6): the effect layer's generated SM5
// (FxHlslEmitter) and the backend's own (present, clear quad, StretchRect blit).
//
//   - D3D11: Diligent compiles the HLSL with FXC, as before (unchanged since M6b).
//   - Vulkan: Diligent compiles the HLSL with glslang to SPIR-V and maps the ATTRIBn inputs to their
//     locations itself (ShaderVkImpl.cpp, MapHLSLVertexShaderInputs).
//   - OpenGL: Diligent's own GL route for HLSL is its text converter (HLSL2GLSLConverter), which needs
//     combined texture samplers and does no type checking (m6u-DIL.txt 9). The backend instead
//     compiles the HLSL with the same glslang front end Vulkan uses (GLSLangUtils::HLSLtoSPIRV), and
//     SPIRV-Cross turns the SPIR-V into GLSL that Diligent compiles verbatim:
//       * ATTRIBn vertex inputs get location n (the GL vertex attribute index = LayoutElement
//         InputIndex), from the HLSL semantic glslang keeps (SPV_GOOGLE_hlsl_functionality1);
//       * descriptor sets and bindings are removed: Diligent's GL backend assigns uniform-block and
//         texture-unit bindings itself when it links;
//       * each (texture, sampler) pair becomes one combined sampler; the caller learns the names and
//         the pairs, so a resource signature can list them (UseCombinedTextureSamplers) with the
//         pair's sampler as its immutable sampler;
//       * the GL conventions of DiligentHost.h (FlipsRenderTargets): every vertex shader ends with
//         gl_Position.y = -y and z = 2z - w (SPIRV-Cross flip_vert_y, fixup_clipspace).
//     The GLSL dialect is the device's (desktop "core", or ES on Android), with separate shader
//     objects when the device has them.
//
// Header rule (DiligentHost.h): Diligent types only as forward declarations.

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "DiligentHost.h"

namespace Diligent
{
    struct IRenderDevice;
    struct IShader;
} // namespace Diligent

namespace gpg::gal::diligent
{
    /** How the GL path names a combined sampler. */
    enum class CombinedSamplerNaming : std::uint8_t
    {
        /** The texture's name: for the backend's shaders, one sampler per texture (or a fetch). */
        Texture,
        /** "<texture>_s_<sampler>": for effect shaders, where one texture may meet several samplers. */
        TextureAndSampler
    };

    struct HlslShaderSource
    {
        const char* source = nullptr;
        std::size_t length = 0;              // 0: null-terminated
        const char* name = "";
        std::uint32_t shaderType = 0;        // Diligent SHADER_TYPE
        const char* entryPoint = "main";
        bool shaderModel5 = false;           // HLSLVersion 5.0 (the effect layer's SM5)
        bool combinedTextureSamplers = false;// D3D11/Vulkan: Texture2D t + SamplerState t_sampler (UseCombinedTextureSamplers)
        CombinedSamplerNaming naming = CombinedSamplerNaming::Texture;
        std::vector<std::pair<std::string, std::string>> macros;
    };

    /** One combined sampler of the GL path. `sampler` is empty for a fetch-only texture (a dummy sampler). */
    struct GlCombinedSampler
    {
        std::string name;
        std::string texture;
        std::string sampler;
    };

    struct CompiledShaderInfo
    {
        std::vector<GlCombinedSampler> combined; // GL only
        std::string messages;                    // compiler output on failure (and SPIRV-Cross's)
        std::string glsl;                        // GL only: the GLSL handed to Diligent
    };

    /**
     * Compiles `source` for `device` (whose API is `api`). Returns false with `info->messages` set when
     * any stage of it fails; on success `*shader` holds a reference the caller releases. The x87
     * control word is the caller's business (ScopedDefaultFpu).
     */
    bool CompileHlslShader(Diligent::IRenderDevice* device, GraphicsApi api, const HlslShaderSource& source, Diligent::IShader** shader,
                           CompiledShaderInfo* info);

    /**
     * The effect layer (EffectsDiligentGpu.cpp) compiles through this when it is set, which the device
     * does at setup; without it (fxdiff links the layer alone) the layer calls Diligent's HLSL path
     * directly, which is the D3D11 and Vulkan route above. Defined in EffectsDiligentGpu.cpp.
     */
    using ShaderCompilerFn = bool (*)(Diligent::IRenderDevice*, GraphicsApi, const HlslShaderSource&, Diligent::IShader**, CompiledShaderInfo*);
    void SetEffectShaderCompiler(ShaderCompilerFn compiler);

    /**
     * Experiment switch (`/galtrilinear d3d9|aniso1|mippoint`, 0/1/2): the effect layer's trilinear
     * samplers (min, mag and mip LINEAR) as D3D9 has them, as anisotropic x1 (the same footprint), or
     * with POINT mip selection. Set before the effects make their signatures. Defined in
     * EffectsDiligentGpu.cpp.
     */
    void SetEffectTrilinearMode(int mode);

    /**
     * M7a1 (Android): a persistent store of compiled shaders, so that a second launch compiles nothing
     * (m6u-CRIT.txt Q1; the S22 Ultra's glslang is about 2.5x slower than the PC's). The Vulkan route
     * looks a shader up by a key over everything that decides its SPIR-V - the source text, the entry
     * point, the stage, the HLSL version, the sampler mode, the macros, and the compiler build (Diligent's
     * API version and its pinned glslang) - and creates it from the stored SPIR-V when it is there;
     * otherwise it compiles the HLSL and stores what Diligent made of it (IShader::GetBytecode: the SPIR-V
     * after Diligent's own vertex-input mapping, which it applies again, unchanged, when it loads it).
     * Without a cache (Windows) nothing changes.
     */
    class ShaderBytecodeCache
    {
    public:
        virtual ~ShaderBytecodeCache() = default;
        /** The stored bytecode for `key` (a hex string), or false. */
        virtual bool Load(const std::string& key, std::vector<std::uint8_t>* bytecode) = 0;
        virtual void Store(const std::string& key, const void* bytecode, std::size_t size) = 0;
    };

    /** Installs the cache the Vulkan route reads and fills (null removes it). Not owned. */
    void SetShaderBytecodeCache(ShaderBytecodeCache* cache);

    /** Every shader the backend compiled or loaded, and the time it took (for the report and galplay.json). */
    struct ShaderCompileCounts
    {
        std::uint64_t compiled = 0;        // compiled from HLSL (D3D11 and Vulkan routes)
        std::uint64_t failed = 0;
        std::uint64_t cacheHits = 0;       // created from cached SPIR-V
        std::uint64_t cacheMisses = 0;     // looked up, not there (then compiled and stored)
        std::uint64_t cacheStores = 0;
        double compileMilliseconds = 0.0;  // wall time in CreateShader from HLSL
        double cacheMilliseconds = 0.0;    // wall time to look up, load and create from cached SPIR-V
    };
    [[nodiscard]] ShaderCompileCounts GetShaderCompileCounts();

    /** How many shaders went through the GL route, and how many of them failed (for the report). */
    struct GlShaderRouteCounts
    {
        std::uint64_t compiled = 0;
        std::uint64_t failed = 0;
    };
    [[nodiscard]] GlShaderRouteCounts GetGlShaderRouteCounts();
} // namespace gpg::gal::diligent
