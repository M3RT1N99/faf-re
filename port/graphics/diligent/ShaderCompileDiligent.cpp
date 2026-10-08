#include "ShaderCompileDiligent.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>

#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/APIInfo.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/Shader.h"
#include "Primitives/interface/DataBlob.h"

// SPIRV-Cross as Diligent built it into the install (ThirdParty/CMakeLists.txt:96-118: GLSL backend on
// with GL, C++ API in the namespace diligent_spirv_cross). The install ships the libraries but not the
// headers, so they come from the pinned source tree the install was built from.
#define SPIRV_CROSS_NAMESPACE_OVERRIDE diligent_spirv_cross
#include "../../../dependencies/DiligentCore/ThirdParty/SPIRV-Cross/spirv_glsl.hpp"

// glslang's HLSL front end as Diligent's Vulkan backend calls it (Graphics/ShaderTools/include/
// GLSLangUtils.hpp at the pin; compiled into DiligentCore.lib, which the install has, while ShaderTools'
// headers are not installed). The declarations are the header's, so the mangled names match the
// library's (?HLSLtoSPIRV@GLSLangUtils@Diligent@@...W4SpirvVersion@12@...).
namespace Diligent
{
    namespace GLSLangUtils
    {
        enum class SpirvVersion
        {
            Vk100,
            Vk110,
            Vk110_Spirv14,
            Vk120,
            GL,
            GLES,
            Count
        };
        void InitializeGlslang();
        std::vector<unsigned int> HLSLtoSPIRV(const ShaderCreateInfo& ShaderCI, SpirvVersion Version, const char* ExtraDefinitions,
                                              IDataBlob** ppCompilerOutput);
    } // namespace GLSLangUtils
} // namespace Diligent

// The libraries the Vulkan and GL engines and this route pull in (Debug names: the backend is Debug
// only, port_graphics.props). The props put the install's lib directory on the library path. (Windows
// only: clang turns these into ELF dependent-library entries; the Android build links its own.)
#if defined(_WIN32)
#if defined(_DEBUG)
#    pragma comment(lib, "glslangd.lib")
#    pragma comment(lib, "spirv-cross-cored.lib")
#    pragma comment(lib, "spirv-cross-glsld.lib")
#else
#    pragma comment(lib, "glslang.lib")
#    pragma comment(lib, "spirv-cross-core.lib")
#    pragma comment(lib, "spirv-cross-glsl.lib")
#endif
#pragma comment(lib, "SPIRV-Tools-opt.lib")
#pragma comment(lib, "SPIRV-Tools.lib")
#endif

namespace dg = Diligent;
namespace sc = diligent_spirv_cross;

namespace gpg::gal::diligent
{
    namespace
    {
        std::atomic<std::uint64_t> gGlCompiled{0};
        std::atomic<std::uint64_t> gGlFailed{0};
        std::once_flag gGlslangOnce;

        // M7a1: the shader cache (ShaderBytecodeCache) and the compile counters.
        std::mutex gCompileLock;
        ShaderBytecodeCache* gBytecodeCache = nullptr;
        ShaderCompileCounts gCompileCounts;

        double MillisecondsSince(const std::chrono::steady_clock::time_point start)
        {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        }

        /**
         * The cache key of a Vulkan shader: two 64-bit hashes (FNV-1a and a multiply-xorshift mix) over
         * every input that decides the SPIR-V, the compiler build first.
         */
        std::string BytecodeKey(const HlslShaderSource& source, const std::size_t length)
        {
            std::uint64_t a = 0xCBF29CE484222325ULL;
            std::uint64_t b = 0x9E3779B97F4A7C15ULL;
            const auto add = [&](const void* const data, const std::size_t size) {
                const auto* bytes = static_cast<const std::uint8_t*>(data);
                for (std::size_t index = 0; index < size; ++index) {
                    a = (a ^ bytes[index]) * 0x100000001B3ULL;
                    b = (b ^ bytes[index]) * 0xFF51AFD7ED558CCDULL;
                    b ^= b >> 29U;
                }
                const std::uint64_t separator = size;
                a = (a ^ separator) * 0x100000001B3ULL;
                b = (b + separator) * 0xC4CEB9FE1A85EC53ULL;
            };
            const auto text = [&](const char* const value) { add(value != nullptr ? value : "", value != nullptr ? std::strlen(value) : 0U); };
            // The compiler build: Diligent's API version at the pin (1436d1fe, which pins glslang), and
            // this route's own revision.
            static const std::string kCompiler = "galplay-vk-spirv-1 diligent " + std::to_string(DILIGENT_API_VERSION) + " 1436d1fe";
            add(kCompiler.data(), kCompiler.size());
            const std::uint32_t flags[3] = {source.shaderType, source.shaderModel5 ? 1U : 0U, source.combinedTextureSamplers ? 1U : 0U};
            add(flags, sizeof(flags));
            text(source.entryPoint);
            for (const auto& [name, definition] : source.macros) {
                add(name.data(), name.size());
                add(definition.data(), definition.size());
            }
            add(source.source, length);
            char key[40];
            std::snprintf(key, sizeof(key), "%016llx%016llx", static_cast<unsigned long long>(a), static_cast<unsigned long long>(b));
            return key;
        }

        std::string BlobText(dg::IDataBlob* const blob)
        {
            if (blob == nullptr || blob->GetSize() == 0) {
                return {};
            }
            const char* const text = static_cast<const char*>(blob->GetConstDataPtr());
            return std::string(text, strnlen(text, blob->GetSize()));
        }

        /**
         * GLSL reserves identifiers with "__" (and "gl_"), and SPIRV-Cross rewrites them when it emits
         * the code, so a name set on a combined sampler is made safe here: then the name the caller
         * records is the uniform's name in the GLSL, which Diligent's reflection reports.
         */
        std::string GlslSafeName(std::string name)
        {
            for (std::size_t at = name.find("__"); at != std::string::npos; at = name.find("__")) {
                name.erase(at, 1);
            }
            if (name.rfind("gl_", 0) == 0) {
                name.insert(0, "u");
            }
            return name;
        }

        /** "ATTRIB<n>" (any case) -> n. */
        bool AttribLocation(const std::string& semantic, std::uint32_t* const location)
        {
            static const char kPrefix[] = "attrib";
            if (semantic.size() <= sizeof(kPrefix) - 1) {
                return false;
            }
            for (std::size_t i = 0; i < sizeof(kPrefix) - 1; ++i) {
                if (std::tolower(static_cast<unsigned char>(semantic[i])) != kPrefix[i]) {
                    return false;
                }
            }
            char* end = nullptr;
            const unsigned long value = std::strtoul(semantic.c_str() + sizeof(kPrefix) - 1, &end, 10);
            if (end == nullptr || *end != '\0') {
                return false;
            }
            *location = static_cast<std::uint32_t>(value);
            return true;
        }

        void FillMacros(const HlslShaderSource& source, std::vector<dg::ShaderMacro>* const storage, dg::ShaderCreateInfo* const info)
        {
            storage->clear();
            for (const auto& [name, definition] : source.macros) {
                storage->push_back(dg::ShaderMacro{name.c_str(), definition.c_str()});
            }
            if (!storage->empty()) {
                info->Macros = dg::ShaderMacroArray{storage->data(), static_cast<dg::Uint32>(storage->size())};
            }
        }

        bool CompileForGl(dg::IRenderDevice* const device, const HlslShaderSource& source, dg::IShader** const shader, CompiledShaderInfo* const info)
        {
            std::call_once(gGlslangOnce, [] { dg::GLSLangUtils::InitializeGlslang(); });

            // 1. HLSL -> SPIR-V, the Vulkan backend's own front end and legalisation.
            dg::ShaderCreateInfo hlsl;
            hlsl.Source = source.source;
            hlsl.SourceLength = source.length;
            hlsl.EntryPoint = source.entryPoint;
            hlsl.SourceLanguage = dg::SHADER_SOURCE_LANGUAGE_HLSL;
            if (source.shaderModel5) {
                hlsl.HLSLVersion = dg::ShaderVersion{5, 0};
            }
            hlsl.Desc.ShaderType = static_cast<dg::SHADER_TYPE>(source.shaderType);
            hlsl.Desc.Name = source.name;
            std::vector<dg::ShaderMacro> macros;
            FillMacros(source, &macros, &hlsl);
            dg::RefCntAutoPtr<dg::IDataBlob> output;
            std::vector<unsigned int> spirv = dg::GLSLangUtils::HLSLtoSPIRV(hlsl, dg::GLSLangUtils::SpirvVersion::Vk100, nullptr, &output);
            if (spirv.empty()) {
                info->messages = "glslang: " + BlobText(output);
                return false;
            }

            // 2. SPIR-V -> GLSL for this device.
            const dg::RenderDeviceInfo& deviceInfo = device->GetDeviceInfo();
            const bool es = deviceInfo.Type == dg::RENDER_DEVICE_TYPE_GLES;
            const dg::Version version = es ? deviceInfo.MaxShaderVersion.GLESSL : deviceInfo.MaxShaderVersion.GLSL;
            std::uint32_t glslVersion = version.Major * 100U + version.Minor * 10U;
            if (!es) {
                glslVersion = std::clamp<std::uint32_t>(glslVersion, 430U, 460U);
            } else {
                glslVersion = std::max<std::uint32_t>(glslVersion, 310U);
            }

            try {
                sc::CompilerGLSL compiler(std::move(spirv));
                const sc::ShaderResources resources = compiler.get_shader_resources();

                if (source.shaderType == dg::SHADER_TYPE_VERTEX) {
                    for (const sc::Resource& input : resources.stage_inputs) {
                        const std::string semantic = compiler.get_decoration_string(input.id, spv::DecorationUserSemantic);
                        std::uint32_t location = 0;
                        if (!AttribLocation(semantic, &location)) {
                            info->messages = "vertex input '" + input.name + "' has semantic '" + semantic + "', not ATTRIBn";
                            return false;
                        }
                        compiler.set_decoration(input.id, spv::DecorationLocation, location);
                    }
                }

                // Diligent's GL backend binds uniform blocks and texture units itself.
                const sc::SmallVector<sc::Resource>* const lists[] = {&resources.uniform_buffers, &resources.storage_buffers,
                                                                      &resources.sampled_images, &resources.separate_images,
                                                                      &resources.separate_samplers, &resources.storage_images};
                for (const sc::SmallVector<sc::Resource>* const list : lists) {
                    for (const sc::Resource& resource : *list) {
                        compiler.unset_decoration(resource.id, spv::DecorationBinding);
                        compiler.unset_decoration(resource.id, spv::DecorationDescriptorSet);
                    }
                }

                // GL has no separate samplers: one combined sampler per (texture, sampler) pair, and a
                // dummy sampler for textures that are only fetched (Load, GetDimensions).
                const sc::VariableID dummy = compiler.build_dummy_sampler_for_combined_images();
                compiler.build_combined_image_samplers();
                for (const sc::CombinedImageSampler& pair : compiler.get_combined_image_samplers()) {
                    GlCombinedSampler combined;
                    combined.texture = compiler.get_name(pair.image_id);
                    combined.sampler = (dummy != 0 && pair.sampler_id == dummy) ? std::string() : compiler.get_name(pair.sampler_id);
                    combined.name = (source.naming == CombinedSamplerNaming::TextureAndSampler && !combined.sampler.empty())
                                        ? GlslSafeName(combined.texture + "_s_" + combined.sampler)
                                        : GlslSafeName(combined.texture);
                    compiler.set_name(pair.combined_id, combined.name);
                    info->combined.push_back(std::move(combined));
                }

                sc::CompilerGLSL::Options options = compiler.get_common_options();
                options.version = glslVersion;
                options.es = es;
                options.separate_shader_objects = deviceInfo.Features.SeparablePrograms != dg::DEVICE_FEATURE_STATE_DISABLED;
                options.enable_420pack_extension = false;
                // DiligentHost.h FlipsRenderTargets: D3D's z range and row order on GL.
                options.vertex.fixup_clipspace = true;
                options.vertex.flip_vert_y = FlipsRenderTargets(GraphicsApi::OpenGL);
                options.fragment.default_float_precision = sc::CompilerGLSL::Options::Precision::Highp;
                options.fragment.default_int_precision = sc::CompilerGLSL::Options::Precision::Highp;
                compiler.set_common_options(options);
                info->glsl = compiler.compile();
            } catch (const std::exception& error) {
                info->messages = std::string("SPIRV-Cross: ") + error.what();
                return false;
            }

            // 3. The GLSL, verbatim, to Diligent's GL backend.
            dg::ShaderCreateInfo glsl;
            glsl.Source = info->glsl.c_str();
            glsl.SourceLength = info->glsl.size();
            glsl.EntryPoint = "main";
            glsl.SourceLanguage = dg::SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;
            glsl.Desc.ShaderType = static_cast<dg::SHADER_TYPE>(source.shaderType);
            glsl.Desc.Name = source.name;
            glsl.Desc.UseCombinedTextureSamplers = dg::True;
            dg::RefCntAutoPtr<dg::IDataBlob> glOutput;
            device->CreateShader(glsl, shader, &glOutput);
            if (*shader == nullptr) {
                info->messages = "GL driver: " + BlobText(glOutput);
                return false;
            }
            return true;
        }
    } // namespace

    bool CompileHlslShader(dg::IRenderDevice* const device, const GraphicsApi api, const HlslShaderSource& source, dg::IShader** const shader,
                           CompiledShaderInfo* const info)
    {
        *shader = nullptr;
        if (device == nullptr || source.source == nullptr) {
            info->messages = "no device or no source";
            return false;
        }
        if (api == GraphicsApi::OpenGL) {
            const bool ok = CompileForGl(device, source, shader, info);
            ++(ok ? gGlCompiled : gGlFailed);
            return ok;
        }

        // M7a1: the Vulkan route takes the SPIR-V from the cache when it has it.
        ShaderBytecodeCache* cache = nullptr;
        {
            std::lock_guard<std::mutex> lock(gCompileLock);
            cache = api == GraphicsApi::Vulkan ? gBytecodeCache : nullptr;
        }
        std::string cacheKey;
        if (cache != nullptr) {
            const auto start = std::chrono::steady_clock::now();
            cacheKey = BytecodeKey(source, source.length != 0U ? source.length : std::strlen(source.source));
            std::vector<std::uint8_t> bytecode;
            if (cache->Load(cacheKey, &bytecode) && !bytecode.empty() && bytecode.size() % 4U == 0U) {
                dg::ShaderCreateInfo cached;
                cached.ByteCode = bytecode.data();
                cached.ByteCodeSize = bytecode.size();
                cached.EntryPoint = source.entryPoint;
                cached.Desc.ShaderType = static_cast<dg::SHADER_TYPE>(source.shaderType);
                cached.Desc.Name = source.name;
                cached.Desc.UseCombinedTextureSamplers = source.combinedTextureSamplers ? dg::True : dg::False;
                device->CreateShader(cached, shader, nullptr);
                if (*shader != nullptr) {
                    std::lock_guard<std::mutex> lock(gCompileLock);
                    ++gCompileCounts.cacheHits;
                    gCompileCounts.cacheMilliseconds += MillisecondsSince(start);
                    return true;
                }
            }
            std::lock_guard<std::mutex> lock(gCompileLock);
            ++gCompileCounts.cacheMisses;
        }

        // D3D11 (FXC) and Vulkan (glslang inside Diligent): the HLSL as it is.
        dg::ShaderCreateInfo create;
        create.Source = source.source;
        create.SourceLength = source.length;
        create.EntryPoint = source.entryPoint;
        create.SourceLanguage = dg::SHADER_SOURCE_LANGUAGE_HLSL;
        if (source.shaderModel5) {
            create.HLSLVersion = dg::ShaderVersion{5, 0};
        }
        create.Desc.ShaderType = static_cast<dg::SHADER_TYPE>(source.shaderType);
        create.Desc.Name = source.name;
        create.Desc.UseCombinedTextureSamplers = source.combinedTextureSamplers ? dg::True : dg::False;
        std::vector<dg::ShaderMacro> macros;
        FillMacros(source, &macros, &create);
        dg::RefCntAutoPtr<dg::IDataBlob> output;
        const auto start = std::chrono::steady_clock::now();
        device->CreateShader(create, shader, &output);
        const double milliseconds = MillisecondsSince(start);
        if (*shader == nullptr) {
            info->messages = BlobText(output);
            std::lock_guard<std::mutex> lock(gCompileLock);
            ++gCompileCounts.failed;
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(gCompileLock);
            ++gCompileCounts.compiled;
            gCompileCounts.compileMilliseconds += milliseconds;
        }
        if (cache != nullptr) {
            const void* bytecode = nullptr;
            dg::Uint64 size = 0;
            (*shader)->GetBytecode(&bytecode, size);
            if (bytecode != nullptr && size != 0U) {
                cache->Store(cacheKey, bytecode, static_cast<std::size_t>(size));
                std::lock_guard<std::mutex> lock(gCompileLock);
                ++gCompileCounts.cacheStores;
            }
        }
        return true;
    }

    void SetShaderBytecodeCache(ShaderBytecodeCache* const cache)
    {
        std::lock_guard<std::mutex> lock(gCompileLock);
        gBytecodeCache = cache;
    }

    ShaderCompileCounts GetShaderCompileCounts()
    {
        std::lock_guard<std::mutex> lock(gCompileLock);
        return gCompileCounts;
    }

    GlShaderRouteCounts GetGlShaderRouteCounts()
    {
        return GlShaderRouteCounts{gGlCompiled.load(), gGlFailed.load()};
    }
} // namespace gpg::gal::diligent
